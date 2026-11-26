#include "Audio/SoundEvent.hpp"
#include "PlayerContent.hpp"

#include "Assets/AssetDependencies.hpp"
#include "Assets/AssetKinds.hpp"
#include "Assets/AssetPath.hpp"
#include "Raven/Core/Files.hpp"
#include "Raven/ECS/Component.hpp"
#include "Raven/Scene/SceneFile.hpp"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace Raven
{
	namespace
	{
		struct SelectedScene
		{
			const SceneDescription* Description;
			std::vector<std::byte> Bytes;
		};

		std::string ManifestPath(const std::filesystem::path& path)
		{
			const auto text = path.lexically_normal().generic_u8string();
			return { reinterpret_cast<const char*>(text.data()), text.size() };
		}

		void WriteAssetRecord(std::ostream& manifest, const AssetCatalog& catalog, const AssetRecord& asset)
		{
			const auto* kind = FindAssetKind(asset.Kind);
			if (!kind || !kind->SceneDependency || kind->Keyword.empty())
				throw std::invalid_argument("This kind cannot be included as a player asset: " + asset.Id.ToString());
			manifest << kind->Keyword << ' ' << asset.Id.ToString() << ' ' << std::quoted(ManifestPath(asset.SourcePath));
			// A texture's record also carries its color space.
			if (asset.Kind == AssetKind::Texture)
				manifest << ' ' << (catalog.GetTexture({ asset.Id }).ColorSpace == TextureColorSpace::SRGB ? "srgb" : "linear");
			manifest << '\n';
		}
	}

	void StagePlayerContent(const SceneProjectDefinition& project, const std::filesystem::path& source,
		const std::filesystem::path& destination, std::span<const AssetId> scenes, std::span<const AssetId> extras)
	{
		if (scenes.empty())
			throw std::invalid_argument("The player profile needs at least one scene");

		std::set<AssetId> requiredAssets;
		const auto require = [&](AssetId id, AssetKind kind)
		{
			if (project.Catalog.Get(id).Kind != kind)
				throw std::invalid_argument("Incorrect player asset kind for " + id.ToString());
			requiredAssets.insert(id);
		};
		requiredAssets.insert(extras.begin(), extras.end());

		std::vector<SelectedScene> selected;
		std::set<AssetId> selectedIds;
		for (const auto& id : scenes)
		{
			if (!selectedIds.insert(id).second)
				throw std::invalid_argument("Duplicate player scene: " + id.ToString());
			const auto found = std::ranges::find(project.Scenes, id, &SceneDescription::Id);
			if (found == project.Scenes.end())
				throw std::invalid_argument("Unknown player scene: " + id.ToString());
			auto bytes = Files::ReadBinary(ResolveAssetPath(source, found->SourcePath), std::size_t{ 1 } << 30);
			// A scene's dependencies are its AssetRef fields, tagged with their asset type (FD-08).
			ForEachAssetRef(SceneFile::Decode(bytes), require);
			selected.push_back({ &*found, std::move(bytes) });
		}

		// A sound event plays its clips, so the player needs them too.
		for (const auto& id : std::set<AssetId>(requiredAssets))
		{
			const auto& record = project.Catalog.Get(id);
			if (record.Kind != AssetKind::Sound)
				continue;
			const auto path = ResolveAssetPath(source, record.SourcePath);
			ExpandSoundClips(ParseSoundEvent(Files::ReadBinary(path, 1024 * 1024), path), require);
		}

		std::ostringstream manifest;
		manifest << "raven-project 2\n";
		for (const auto& id : requiredAssets)
			WriteAssetRecord(manifest, project.Catalog, project.Catalog.Get(id));
		for (const auto& scene : selected)
			manifest << "scene " << scene.Description->Id.ToString() << ' '
				<< std::quoted(ManifestPath(scene.Description->SourcePath)) << '\n';

		if (!std::filesystem::create_directory(destination))
			throw std::invalid_argument("Player content destination must be a new directory");
		for (const auto& id : requiredAssets)
		{
			const auto& relative = project.Catalog.Get(id).SourcePath;
			const auto target = destination / relative.lexically_normal();
			std::filesystem::create_directories(target.parent_path());
			std::filesystem::copy_file(ResolveAssetPath(source, relative), target,
				std::filesystem::copy_options::skip_existing);
		}
		for (const auto& scene : selected)
		{
			const auto target = destination / scene.Description->SourcePath.lexically_normal();
			std::filesystem::create_directories(target.parent_path());
			Files::WriteAtomic(target, scene.Bytes);
		}
		const auto text = manifest.str();
		const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
		const auto projectPath = destination / "Project.ravenproject";
		SceneProject::Decode(bytes, projectPath);
		Files::WriteAtomic(projectPath, bytes);
	}
}
