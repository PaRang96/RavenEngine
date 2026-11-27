#include "Raven/Scene/SceneProject.hpp"

#include "Assets/AssetKinds.hpp"
#include "Assets/ContentIndex.hpp"
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
#include <set>
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

		// The project text, then a NUL and the content fingerprint: the reload service compares these
		// bytes, so a new or edited sidecar or asset file republishes the project (B0 contract 9).
		std::vector<std::byte> ReadProjectInputs(const std::filesystem::path& path)
		{
			auto bytes = ReadProject(path);
			bytes.push_back(std::byte{ 0 });
			const auto fingerprint = ContentFingerprint(path.parent_path());
			bytes.insert(bytes.end(), fingerprint.begin(), fingerprint.end());
			return bytes;
		}

		// Development writes the sidecars of new imported files.
		SceneProjectDefinition DecodeDevelopment(std::span<const std::byte> bytes, const std::filesystem::path& path)
		{
			return SceneProject::Decode(bytes, path, true);
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
		return Decode(bytes, path, false);
	}

	// raven-project 2 lists every asset; 3 (docs/RenderingContentDesign.md, B0 contract 9) keeps only
	// project settings and the scene order, takes asset identity from sidecars and file headers, and
	// lists assets only as a packaged player's generated index (`content index`). Both are read, as
	// the version policy asks (docs/DataModelDesign.md, FD-10). Bytes after a NUL are the content
	// fingerprint the reload service compares (ReadProjectInputs) and are not project text.
	SceneProjectDefinition SceneProject::Decode(std::span<const std::byte> bytes, const std::filesystem::path& path,
		bool createSidecars)
	{
		SceneProjectDefinition result;
		result.SourceBytes.assign(bytes.begin(), bytes.end());
		const auto textEnd = std::find(bytes.begin(), bytes.end(), std::byte{ 0 });
		const auto* text = bytes.empty() ? "" : reinterpret_cast<const char*>(bytes.data());
		std::istringstream file(std::string(text, static_cast<std::size_t>(textEnd - bytes.begin())));
		std::string line;
		int version = 0;
		bool indexed = false;
		std::vector<AssetId> order;
		std::size_t lineNumber = 0;
		const auto registerAsset = [&](std::string_view keyword, std::istringstream& input)
		{
			std::string identity, source, setting;
			if (!(input >> identity))
				throw std::runtime_error("expected asset UUID");
			const auto id = AssetId::Parse(identity);
			if (keyword == "scene")
			{
				if (!(input >> std::quoted(source)))
					throw std::runtime_error("expected scene UUID and path");
				result.Catalog.Register({ id, AssetKind::Scene, PathFromUtf8(source) });
				if (version == 2)
					order.push_back(id);
			}
			else if (keyword == "texture")
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
				result.Catalog.Register({ id, FindAssetKindByKeyword(keyword)->Kind, PathFromUtf8(source) });
			}
		};
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
				if (version == 0)
				{
					if (kind != "raven-project" || !(input >> version) || (version != 2 && version != 3))
						throw std::runtime_error("expected raven-project 3 (or 2)");
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
				else if (version == 3 && kind == "scene")
				{
					// The scene order (F4 and the startup scene); the files carry their IDs.
					std::string identity;
					if (!(input >> identity))
						throw std::runtime_error("expected scene UUID");
					order.push_back(AssetId::Parse(identity));
				}
				else if (version == 3 && kind == "content")
				{
					std::string mode;
					if (!(input >> mode) || mode != "index")
						throw std::runtime_error("expected content index");
					indexed = true;
				}
				else if (version == 3 && kind == "asset")
				{
					std::string keyword;
					if (!indexed || !(input >> keyword) || !FindAssetKindByKeyword(keyword))
						throw std::runtime_error("asset records belong to a packaged index, after 'content index'");
					registerAsset(keyword, input);
				}
				else if (version == 2 && FindAssetKindByKeyword(kind))
					registerAsset(kind, input);
				else
				{
					// Tolerant (FD-10): records a later version adds are skipped by this reader.
					Log(LogLevel::Warning, "Project line " + std::to_string(lineNumber) + ": unknown record '" + kind +
						"' skipped");
					continue;
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
		if (version == 0 || file.bad())
			throw std::runtime_error("project needs a supported header");

		const auto root = path.parent_path();
		if (version == 3 && !indexed)
		{
			auto content = ScanContent(root, createSidecars);
			for (const auto& problem : content.Problems)
				Log(LogLevel::Warning, "Content: " + problem);
			for (auto& asset : content.Assets)
			{
				if (asset.Kind == AssetKind::Texture)
					result.Catalog.Register(TextureAssetDesc{ asset.Id, asset.Source,
						asset.ColorSpace.value_or(TextureColorSpace::SRGB) });
				else
					result.Catalog.Register({ asset.Id, asset.Kind, asset.Source });
			}
		}
		// The ordered scenes first, then any other scene in path order, so every scene can be opened.
		std::set<AssetId> listed;
		for (const auto& id : order)
		{
			const auto* record = result.Catalog.Find(id);
			if (!record || record->Kind != AssetKind::Scene)
				throw std::runtime_error("The scene order names " + id.ToString() + ", which is no scene in the content");
			if (listed.insert(id).second)
				result.Scenes.push_back({ id, record->SourcePath });
		}
		std::vector<SceneDescription> unlisted;
		for (const auto& record : result.Catalog.GetRecords())
			if (record.Kind == AssetKind::Scene && !listed.contains(record.Id))
				unlisted.push_back({ record.Id, record.SourcePath });
		std::sort(unlisted.begin(), unlisted.end(),
			[](const SceneDescription& left, const SceneDescription& right) { return left.SourcePath < right.SourcePath; });
		result.Scenes.insert(result.Scenes.end(), unlisted.begin(), unlisted.end());
		if (result.Scenes.empty())
			throw std::runtime_error("project needs at least one scene");
		for (const auto& record : result.Catalog.GetRecords())
		{
			const auto source = ResolveAssetPath(root, record.SourcePath);
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
			  Current(SceneProject::Decode(ReadProjectInputs(Path), Path, watch)), Assets(renderer, Root, meshes, textures),
			  FixedStepRate(Current.FixedStepRate), Physics(Current.Physics)
		{
			if (watch)
			{
				// No retry after a failure: the inputs include the content fingerprint, so any content
				// change (a fixed scene, a new file) re-imports through the reload service's audit. A retry
				// would rescan the content and repeat its warnings several times a second.
				Watch = std::make_unique<SettledFileWatch<SceneProjectDefinition>>(Path,
					"project", ImporterVersion::Project, ReadProjectInputs, DecodeDevelopment, Current.SourceBytes);
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
	bool SceneProject::IsCurrent(const SceneProjectDefinition& candidate) const { return ReadProjectInputs(m_Impl->Path) == candidate.SourceBytes; }
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
