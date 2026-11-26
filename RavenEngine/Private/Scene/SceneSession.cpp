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
			// Generous for 100k-entity scenes; a corrupt length still cannot allocate without bound.
			return Files::ReadBinary(path, std::size_t{ 1 } << 30);
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

		Impl(SceneProject& project, Factory factory, std::optional<AssetId> initialScene)
			: Project(project), Build(std::move(factory))
		{
			const auto scenes = Project.GetScenes();
			const auto first = std::find_if(scenes.begin(), scenes.end(), [&](const auto& scene)
			{
				return !initialScene || *initialScene == scene.Id;
			});
			if (first == scenes.end())
			{
				if (initialScene)
					throw std::runtime_error("The project has no matching scene " + initialScene->ToString());
				throw std::runtime_error("The project lists no scenes");
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
				candidate.Watch = std::make_unique<Watcher>(candidate.Path, "scene", ImporterVersion::Scene, ReadScene,
					DecodeSnapshot, candidate.Bytes);
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
			const auto start = std::chrono::steady_clock::now();
			const auto scene = std::find_if(candidate.Scenes.begin(), candidate.Scenes.end(), [this](const auto& value)
			{
				return value.Id == Description.Id;
			});
			if (scene == candidate.Scenes.end())
				throw std::runtime_error("Cannot remove the active scene; switch away first");
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
			Log(LogLevel::Info, "Published project metadata; " +
				Project.ReportPublished(std::chrono::steady_clock::now() - start));
			return world.has_value();
		}

		SceneProject& Project;
		Factory Build;
		std::unique_ptr<Scene> Active;
		std::unique_ptr<Watcher> Watch;
		SceneDescription Description;
		std::filesystem::path Path;
		std::optional<SceneProjectDefinition> PendingProject;
		std::optional<std::vector<std::byte>> PendingScene;
		std::chrono::steady_clock::time_point RetryAfter{};
		// Grows while the scene file cannot be read (an editor holding it open), up to two seconds.
		std::chrono::milliseconds ReadBackoff{ 0 };
		std::string ReportedError;
	};

	SceneSession::SceneSession(SceneProject& project, Factory factory, std::optional<AssetId> initialScene)
		: m_Impl(std::make_unique<Impl>(project, std::move(factory), initialScene)) {}
	SceneSession::~SceneSession() = default;
	Scene& SceneSession::GetActive() const noexcept { return *m_Impl->Active; }
	const SceneDescription& SceneSession::GetActiveDescription() const noexcept { return m_Impl->Description; }

	void SceneSession::Restart()
	{
		m_Impl->Publish(m_Impl->Prepare(m_Impl->Description, m_Impl->Project.GetCatalog()));
	}

	bool SceneSession::Load(AssetId scene)
	{
		try
		{
			const auto all = m_Impl->Project.GetScenes();
			const std::vector<SceneDescription> scenes(all.begin(), all.end());
			const auto found = std::find_if(scenes.begin(), scenes.end(), [scene](const auto& description)
			{
				return description.Id == scene;
			});
			if (found == scenes.end())
				throw std::runtime_error("the project has no scene " + scene.ToString());
			m_Impl->PendingScene.reset();
			m_Impl->Publish(m_Impl->Prepare(*found, m_Impl->Project.GetCatalog()));
			return true;
		}
		catch (const std::exception& error)
		{
			Log(LogLevel::Warning, "Cannot load the requested scene; keeping the active world: " + std::string(error.what()));
			return false;
		}
	}

	void SceneSession::RestoreSnapshot(std::span<const std::byte> bytes)
	{
		auto world = m_Impl->Build(bytes, m_Impl->Path, m_Impl->Project.GetCatalog());
		if (!world)
			throw std::runtime_error("Scene factory returned no world");
		m_Impl->Active.swap(world);
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
					const auto all = m_Impl->Project.GetScenes();
					const std::vector<SceneDescription> scenes(all.begin(), all.end());
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
				std::vector<std::byte> current;
				try
				{
					current = ReadScene(m_Impl->Path);
					m_Impl->ReadBackoff = {};
				}
				catch (const std::exception& error)
				{
					// Only a file that cannot be read is retried, with a growing pause.
					if (m_Impl->ReportedError != error.what())
						Log(LogLevel::Warning, "Cannot read scene; retrying: " + std::string(error.what()));
					m_Impl->ReportedError = error.what();
					m_Impl->ReadBackoff = std::clamp(m_Impl->ReadBackoff * 2, std::chrono::milliseconds(200),
						std::chrono::milliseconds(2000));
					m_Impl->RetryAfter = std::chrono::steady_clock::now() + m_Impl->ReadBackoff;
					return false;
				}
				if (current != *m_Impl->PendingScene)
				{
					m_Impl->PendingScene.reset();
					return false;
				}
				// The summary comes from the watch that saw the save; publishing replaces it.
				const auto start = std::chrono::steady_clock::now();
				auto prepared = m_Impl->Prepare(m_Impl->Description, m_Impl->Project.GetCatalog(), *m_Impl->PendingScene);
				const auto summary = m_Impl->Watch ? m_Impl->Watch->Published(std::chrono::steady_clock::now() - start)
					: std::string("not watched");
				m_Impl->Publish(std::move(prepared));
				Log(LogLevel::Info, "Reloaded scene " + PathToUtf8(m_Impl->Path) + "; " + summary);
				m_Impl->PendingScene.reset();
				m_Impl->ReportedError.clear();
				return true;
			}
		}
		catch (const std::exception& error)
		{
			// A candidate that failed is dropped, not retried: a retry costs a full scene build, and the
			// watcher delivers the next save anyway (AA2-RT-05).
			if (m_Impl->ReportedError != error.what())
				Log(LogLevel::Warning, "Cannot replace scene/project; keeping the active world until the file changes: " +
					std::string(error.what()));
			m_Impl->ReportedError = error.what();
			m_Impl->PendingScene.reset();
			m_Impl->PendingProject.reset();
			m_Impl->RetryAfter = {};
		}
		return false;
	}
}
