#include "Raven/Scene/SceneProject.hpp"

#include "Assets/AssetKinds.hpp"
#include "Assets/AssetPath.hpp"
#include "Assets/SettledFileWatch.hpp"
#include "Audio/AudioClip.hpp"
#include "Raven/Core/Files.hpp"
#include "Core/PathText.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/Core/Paths.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace Raven
{
	namespace
	{
		std::vector<std::byte> ReadProject(const std::filesystem::path& path)
		{
			return Files::ReadBinary(path, 4 * 1024 * 1024);
		}

		void StripComment(std::string& line)
		{
			bool quoted = false;
			bool escaped = false;
			for (std::size_t index = 0; index < line.size(); ++index)
			{
				const char value = line[index];
				if (!escaped && value == '"')
					quoted = !quoted;
				if (!quoted && value == '#')
				{
					line.resize(index);
					return;
				}
				escaped = quoted && !escaped && value == '\\';
			}
		}
	}

	SceneProjectDefinition SceneProject::Decode(std::span<const std::byte> bytes, const std::filesystem::path& path)
	{
		SceneProjectDefinition result;
		result.SourceBytes.assign(bytes.begin(), bytes.end());
		const auto* text = bytes.empty() ? "" : reinterpret_cast<const char*>(bytes.data());
		std::istringstream file(std::string(text, bytes.size()));
		std::string line;
		bool header = false;
		std::size_t lineNumber = 0;
		while (std::getline(file, line))
		{
			++lineNumber;
			StripComment(line);
			std::istringstream input(line);
			input.imbue(std::locale::classic());
			std::string kind;
			if (!(input >> kind))
				continue;
			try
			{
				if (!header)
				{
					int version = 0;
					if (kind != "raven-project" || !(input >> version) || version != 2)
						throw std::runtime_error("expected raven-project 2");
					header = true;
				}
				else if (kind == "fixed-step")
				{
					double rate = 0.0;
					if (!(input >> rate) || !(rate >= 10.0 && rate <= 480.0))
						throw std::runtime_error("expected fixed-step rate in Hz, from 10 to 480");
					result.FixedStepRate = rate;
				}
				else if (kind == "physics-limits")
				{
					std::int64_t bodies = 0, pairs = 0, contacts = 0;
					const auto valid = [](std::int64_t value) { return value >= 16 && value <= 4194304; };
					if (!(input >> bodies >> pairs >> contacts) || !valid(bodies) || !valid(pairs) || !valid(contacts))
						throw std::runtime_error("expected physics-limits bodies, body pairs and contact constraints, "
							"each from 16 to 4194304");
					result.Physics = { static_cast<std::uint32_t>(bodies), static_cast<std::uint32_t>(pairs),
						static_cast<std::uint32_t>(contacts) };
				}
				else if (!FindAssetKindByKeyword(kind))
				{
					// raven-project 2 is tolerant (docs/DataModelDesign.md, FD-10): records a later
					// version adds are skipped by this reader.
					Log(LogLevel::Warning, "Project line " + std::to_string(lineNumber) + ": unknown record '" + kind +
						"' skipped");
					continue;
				}
				else
				{
					std::string identity, source, setting;
					if (!(input >> identity))
						throw std::runtime_error("expected asset UUID");
					const auto id = AssetId::Parse(identity);
					if (kind == "scene")
					{
						if (!(input >> std::quoted(source)))
							throw std::runtime_error("expected scene UUID and path");
						result.Catalog.Register({ id, AssetKind::Scene, PathFromUtf8(source) });
						result.Scenes.push_back({ id, PathFromUtf8(source) });
					}
					else if (kind == "texture")
					{
						if (!(input >> std::quoted(source) >> setting) || (setting != "srgb" && setting != "linear"))
							throw std::runtime_error("expected texture UUID, PNG path and srgb/linear");
						result.Catalog.Register(TextureAssetDesc{ id, PathFromUtf8(source), setting == "srgb" ? TextureColorSpace::SRGB : TextureColorSpace::Linear });
					}
					else
					{
						// Every other kind's record is its keyword, ID and source path.
						if (!(input >> std::quoted(source)))
							throw std::runtime_error("expected source path");
						result.Catalog.Register({ id, FindAssetKindByKeyword(kind)->Kind, PathFromUtf8(source) });
					}
				}
				std::string extra;
				if (input >> extra)
					throw std::runtime_error("unexpected project field: " + extra);
			}
			catch (const std::exception& error)
			{
				throw std::runtime_error("project line " + std::to_string(lineNumber) + ": " + error.what());
			}
		}
		if (!header || file.bad() || result.Scenes.empty())
			throw std::runtime_error("project needs a supported header and at least one scene");
		for (const auto& record : result.Catalog.GetRecords())
		{
			const auto source = ResolveAssetPath(path.parent_path(), record.SourcePath);
			if (!std::filesystem::is_regular_file(source))
				throw std::runtime_error("Missing project source: " + PathToUtf8(source));
			if (!AcceptsSource(record.Kind, source))
				throw std::runtime_error("Incorrect source type for " + record.Id.ToString() + ": expected " +
					std::string(AssetKindName(record.Kind)));
		}
		return result;
	}

	struct SceneProject::Impl
	{
		Impl(VulkanContext& renderer, std::filesystem::path root, bool watch, bool meshes, bool textures)
			: Root(std::filesystem::absolute(std::move(root)).lexically_normal()), Path(Root / "Project.ravenproject"),
			  Current(SceneProject::Decode(ReadProject(Path), Path)), Assets(renderer, Root, meshes, textures),
			  FixedStepRate(Current.FixedStepRate), Physics(Current.Physics)
		{
			if (watch)
			{
				Watch = std::make_unique<SettledFileWatch<SceneProjectDefinition>>(Path,
					"project", ImporterVersion::Project, ReadProject, SceneProject::Decode, Current.SourceBytes, true);
				Log(LogLevel::Info, "Watching project " + PathToUtf8(Path));
			}
			Log(LogLevel::Info, "Scene content root " + PathToUtf8(Root));
		}

		const std::filesystem::path Root;
		const std::filesystem::path Path;
		SceneProjectDefinition Current;
		AssetStore Assets;
		std::unique_ptr<SettledFileWatch<SceneProjectDefinition>> Watch;
		const double FixedStepRate;
		const PhysicsLimits Physics;
	};

	SceneProject::SceneProject(VulkanContext& renderer, std::filesystem::path root, bool watch, bool meshes, bool textures)
		: m_Impl(std::make_unique<Impl>(renderer, std::move(root), watch, meshes, textures)) {}
	SceneProject::~SceneProject() = default;
	const AssetCatalog& SceneProject::GetCatalog() const noexcept { return m_Impl->Current.Catalog; }
	AssetStore& SceneProject::GetAssets() noexcept { return m_Impl->Assets; }
	const std::filesystem::path& SceneProject::GetContentRoot() const noexcept { return m_Impl->Root; }
	std::span<const SceneDescription> SceneProject::GetScenes() const noexcept { return m_Impl->Current.Scenes; }
	double SceneProject::GetFixedStepRate() const noexcept { return m_Impl->FixedStepRate; }
	const PhysicsLimits& SceneProject::GetPhysicsLimits() const noexcept { return m_Impl->Physics; }
	std::optional<SceneProjectDefinition> SceneProject::TakeUpdate()
	{
		return m_Impl->Watch ? m_Impl->Watch->Take() : std::nullopt;
	}
	bool SceneProject::IsCurrent(const SceneProjectDefinition& candidate) const { return ReadProject(m_Impl->Path) == candidate.SourceBytes; }
	bool SceneProject::IsWatching() const noexcept { return m_Impl->Watch != nullptr; }

	std::string SceneProject::ReportPublished(std::chrono::duration<double, std::milli> publish)
	{
		return m_Impl->Watch ? m_Impl->Watch->Published(publish) : std::string("not watched");
	}
	void SceneProject::Commit(SceneProjectDefinition& candidate) noexcept
	{
		m_Impl->Current.Catalog.Swap(candidate.Catalog);
		m_Impl->Current.Scenes.swap(candidate.Scenes);
		m_Impl->Current.SourceBytes.swap(candidate.SourceBytes);
		if (candidate.FixedStepRate != m_Impl->Current.FixedStepRate)
		{
			m_Impl->Current.FixedStepRate = candidate.FixedStepRate;
			Log(LogLevel::Warning, "Project fixed-step rate changed; it applies when the host restarts");
		}
		if (!(candidate.Physics == m_Impl->Current.Physics))
		{
			m_Impl->Current.Physics = candidate.Physics;
			Log(LogLevel::Warning, "Project physics limits changed; they apply when the host restarts");
		}
	}
}
