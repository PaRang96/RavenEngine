#include "Raven/Scene/SceneSession.hpp"

#include "Assets/AssetPath.hpp"
#include "Assets/SettledFileWatch.hpp"
#include "Raven/Core/Files.hpp"
#include "Core/PathText.hpp"
#include "Raven/Core/Log.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace Raven
{
	namespace
	{
		std::vector<std::byte> ReadScene(const std::filesystem::path& path)
		{
			return Files::ReadBinary(path, 4 * 1024 * 1024);
		}
		std::vector<std::byte> DecodeSnapshot(std::span<const std::byte> bytes, const std::filesystem::path&)
		{
			return { bytes.begin(), bytes.end() };
		}
	}

	struct SceneSession::Impl
	{
		using Watcher = SettledFileWatch<std::vector<std::byte>>;
		struct Candidate
		{
			std::unique_ptr<Scene> World;
			std::unique_ptr<Watcher> Watch;
			SceneDescription Description;
			std::filesystem::path Path;
			std::vector<std::byte> Bytes;
		};

		Impl(SceneProject& project, SceneType type, Factory factory, std::optional<AssetId> initialScene)
			: Project(project), Type(type), Build(std::move(factory))
		{
			const auto scenes = Project.GetScenes();
			const auto first = std::find_if(scenes.begin(), scenes.end(), [&](const auto& scene)
			{
				return scene.Type == type && (!initialScene || *initialScene == scene.Id);
			});
			if (first == scenes.end())
			{
				if (initialScene)
					throw std::runtime_error("The project has no matching scene " + initialScene->ToString());
				throw std::runtime_error("Project has no scenes for the selected sample");
			}
			Publish(Prepare(*first, Project.GetCatalog()));
		}

		Candidate Prepare(const SceneDescription& description, const AssetCatalog& catalog,
			std::optional<std::vector<std::byte>> bytes = std::nullopt)
		{
			Candidate candidate;
			candidate.Description = description;
			candidate.Path = ResolveAssetPath(Project.GetContentRoot(), description.SourcePath);
			candidate.Bytes = bytes ? std::move(*bytes) : ReadScene(candidate.Path);
			candidate.World = Build(candidate.Bytes, candidate.Path, catalog);
			if (!candidate.World)
				throw std::runtime_error("Scene factory returned no world");
			if (Project.IsWatching())
				candidate.Watch = std::make_unique<Watcher>(candidate.Path, "scene", ReadScene, DecodeSnapshot, candidate.Bytes);
			if (ReadScene(candidate.Path) != candidate.Bytes)
				throw std::runtime_error("Scene save was superseded while preparing its replacement");
			return candidate;
		}

		void Publish(Candidate candidate)
		{
			Active.swap(candidate.World);
			Watch.swap(candidate.Watch);
			std::swap(Description, candidate.Description);
			Path.swap(candidate.Path);
			Log(LogLevel::Info, "Loaded level '" + PathToUtf8(Path.filename()) + "'");
			if (Watch)
				Log(LogLevel::Info, "Watching scene " + PathToUtf8(Path));
		}

		bool UpdateProject(SceneProjectDefinition& candidate)
		{
			const auto scene = std::find_if(candidate.Scenes.begin(), candidate.Scenes.end(), [this](const auto& value)
			{
				return value.Id == Description.Id && value.Type == Type;
			});
			if (scene == candidate.Scenes.end())
				throw std::runtime_error("Cannot remove or change the type of the active scene; switch away first");
			SceneDescription nextDescription = *scene;
			auto resources = Project.GetAssets().PrepareCatalog(candidate.Catalog);
			std::optional<Candidate> world;
			if (ResolveAssetPath(Project.GetContentRoot(), scene->SourcePath) != Path)
				world = Prepare(*scene, candidate.Catalog);
			if (!Project.IsCurrent(candidate) || !resources.IsCurrent() || (world && ReadScene(world->Path) != world->Bytes))
				throw std::runtime_error("Project save was superseded while preparing its replacement");
			resources.Commit();
			Project.Commit(candidate);
			if (world)
				Publish(std::move(*world));
			else
				std::swap(Description, nextDescription);
			Log(LogLevel::Info, "Published project metadata");
			return world.has_value();
		}

		SceneProject& Project;
		const SceneType Type;
		Factory Build;
		std::unique_ptr<Scene> Active;
		std::unique_ptr<Watcher> Watch;
		SceneDescription Description;
		std::filesystem::path Path;
		std::optional<SceneProjectDefinition> PendingProject;
		std::optional<std::vector<std::byte>> PendingScene;
		std::chrono::steady_clock::time_point RetryAfter{};
		std::string ReportedError;
	};

	SceneSession::SceneSession(SceneProject& project, SceneType type, Factory factory, std::optional<AssetId> initialScene)
		: m_Impl(std::make_unique<Impl>(project, type, std::move(factory), initialScene)) {}
	SceneSession::~SceneSession() = default;
	Scene& SceneSession::GetActive() const noexcept { return *m_Impl->Active; }
	const SceneDescription& SceneSession::GetActiveDescription() const noexcept { return m_Impl->Description; }

	void SceneSession::Restart()
	{
		m_Impl->Publish(m_Impl->Prepare(m_Impl->Description, m_Impl->Project.GetCatalog()));
	}

	bool SceneSession::Update(bool restart, bool next)
	{
		try
		{
			if (restart || next)
			{
				m_Impl->PendingScene.reset();
				if (next)
				{
					std::vector<SceneDescription> scenes;
					for (const auto& scene : m_Impl->Project.GetScenes())
						if (scene.Type == m_Impl->Type)
							scenes.push_back(scene);
					const auto found = std::find_if(scenes.begin(), scenes.end(), [this](const auto& scene)
					{
						return scene.Id == m_Impl->Description.Id;
					});
					const auto index = static_cast<std::size_t>(found - scenes.begin());
					m_Impl->Publish(m_Impl->Prepare(scenes.at((index + 1) % scenes.size()), m_Impl->Project.GetCatalog()));
				}
				else
					Restart();
				return true;
			}
			if (auto candidate = m_Impl->Project.TakeUpdate())
			{
				m_Impl->PendingProject = std::move(candidate);
				m_Impl->RetryAfter = {};
				m_Impl->ReportedError.clear();
			}
			if (m_Impl->Watch)
				if (auto bytes = m_Impl->Watch->Take())
				{
					m_Impl->PendingScene = std::move(bytes);
					m_Impl->RetryAfter = {};
					m_Impl->ReportedError.clear();
				}
			if (std::chrono::steady_clock::now() < m_Impl->RetryAfter)
				return false;
			if (m_Impl->PendingProject)
			{
				if (!m_Impl->Project.IsCurrent(*m_Impl->PendingProject))
					m_Impl->PendingProject.reset();
				else
				{
					const bool replaced = m_Impl->UpdateProject(*m_Impl->PendingProject);
					m_Impl->PendingProject.reset();
					m_Impl->ReportedError.clear();
					if (replaced)
					{
						m_Impl->PendingScene.reset();
						return true;
					}
				}
			}
			if (m_Impl->PendingScene)
			{
				if (ReadScene(m_Impl->Path) != *m_Impl->PendingScene)
				{
					m_Impl->PendingScene.reset();
					return false;
				}
				m_Impl->Publish(m_Impl->Prepare(m_Impl->Description, m_Impl->Project.GetCatalog(), *m_Impl->PendingScene));
				m_Impl->PendingScene.reset();
				m_Impl->ReportedError.clear();
				return true;
			}
		}
		catch (const std::exception& error)
		{
			if (m_Impl->ReportedError != error.what())
				Log(LogLevel::Warning, "Cannot replace scene/project; keeping active world: " + std::string(error.what()));
			m_Impl->ReportedError = error.what();
			m_Impl->RetryAfter = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
		}
		return false;
	}
}
