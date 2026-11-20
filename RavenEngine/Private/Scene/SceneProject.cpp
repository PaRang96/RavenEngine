#include "Raven/Scene/SceneProject.hpp"

#include "Assets/AssetPath.hpp"
#include "Assets/SettledFileWatch.hpp"
#include "Raven/Core/Files.hpp"
#include "Core/PathText.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/Core/Paths.hpp"
#include "ContentSourceConfig.hpp"

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
					if (kind != "raven-project" || !(input >> version) || version != 1)
						throw std::runtime_error("expected raven-project 1");
					header = true;
				}
				else
				{
					std::string identity, source, setting;
					if (!(input >> identity))
						throw std::runtime_error("expected asset UUID");
					const auto id = AssetId::Parse(identity);
					if (kind == "scene")
					{
						if (!(input >> setting >> std::quoted(source)) || (setting != "perspective" && setting != "orthographic"))
							throw std::runtime_error("expected scene UUID, perspective/orthographic and path");
						result.Catalog.Register({ id, AssetKind::Scene, PathFromUtf8(source) });
						result.Scenes.push_back({ id, setting == "perspective" ? SceneType::Perspective : SceneType::Orthographic, PathFromUtf8(source) });
					}
					else if (kind == "texture")
					{
						if (!(input >> std::quoted(source) >> setting) || (setting != "srgb" && setting != "linear"))
							throw std::runtime_error("expected texture UUID, PNG path and srgb/linear");
						result.Catalog.Register(TextureAssetDesc{ id, PathFromUtf8(source), setting == "srgb" ? TextureColorSpace::SRGB : TextureColorSpace::Linear });
					}
					else if (kind == "mesh" || kind == "audio")
					{
						if (!(input >> std::quoted(source)))
							throw std::runtime_error("expected source path");
						result.Catalog.Register({ id, kind == "mesh" ? AssetKind::Mesh : AssetKind::Audio, PathFromUtf8(source) });
					}
					else
						throw std::runtime_error("unsupported project record: " + kind);
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
			auto extension = source.extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
			const auto expected = record.Kind == AssetKind::Texture ? ".png" : record.Kind == AssetKind::Mesh ? ".fbx" : record.Kind == AssetKind::Audio ? ".wav" : ".ravenlevel";
			if (extension != expected && !(record.Kind == AssetKind::Scene && extension == ".ravenscene"))
				throw std::runtime_error("Incorrect source type for " + record.Id.ToString());
		}
		return result;
	}

	struct SceneProject::Impl
	{
		Impl(VulkanContext& renderer, std::filesystem::path root, bool watch, bool meshes, bool textures)
			: Root(std::filesystem::absolute(std::move(root)).lexically_normal()), Path(Root / "Project.ravenproject"),
			  Current(SceneProject::Decode(ReadProject(Path), Path)), Assets(renderer, Root, meshes, textures)
		{
			if (watch)
			{
				Watch = std::make_unique<SettledFileWatch<SceneProjectDefinition>>(Path,
					"project", ReadProject, SceneProject::Decode, Current.SourceBytes, true);
				Log(LogLevel::Info, "Watching project " + PathToUtf8(Path));
			}
			Log(LogLevel::Info, "Scene content root " + PathToUtf8(Root));
		}

		const std::filesystem::path Root;
		const std::filesystem::path Path;
		SceneProjectDefinition Current;
		AssetStore Assets;
		std::unique_ptr<SettledFileWatch<SceneProjectDefinition>> Watch;
	};

	std::filesystem::path SceneProject::DefaultContentRoot(bool development)
	{
		return development ? std::filesystem::path(ContentSourceConfig::Root) : Paths::GetContentPath();
	}

	SceneProject::SceneProject(VulkanContext& renderer, std::filesystem::path root, bool watch, bool meshes, bool textures)
		: m_Impl(std::make_unique<Impl>(renderer, std::move(root), watch, meshes, textures)) {}
	SceneProject::~SceneProject() = default;
	const AssetCatalog& SceneProject::GetCatalog() const noexcept { return m_Impl->Current.Catalog; }
	AssetStore& SceneProject::GetAssets() noexcept { return m_Impl->Assets; }
	const std::filesystem::path& SceneProject::GetContentRoot() const noexcept { return m_Impl->Root; }
	std::span<const SceneDescription> SceneProject::GetScenes() const noexcept { return m_Impl->Current.Scenes; }
	std::optional<SceneProjectDefinition> SceneProject::TakeUpdate()
	{
		return m_Impl->Watch ? m_Impl->Watch->Take() : std::nullopt;
	}
	bool SceneProject::IsCurrent(const SceneProjectDefinition& candidate) const { return ReadProject(m_Impl->Path) == candidate.SourceBytes; }
	bool SceneProject::IsWatching() const noexcept { return m_Impl->Watch != nullptr; }
	void SceneProject::Commit(SceneProjectDefinition& candidate) noexcept
	{
		m_Impl->Current.Catalog.Swap(candidate.Catalog);
		m_Impl->Current.Scenes.swap(candidate.Scenes);
		m_Impl->Current.SourceBytes.swap(candidate.SourceBytes);
	}
}
