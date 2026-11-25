#include "Audio/SoundEvent.hpp"
#include "PlayerContent.hpp"

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
			const auto source = ManifestPath(asset.SourcePath);
			const auto path = std::quoted(source);
			switch (asset.Kind)
			{
			case AssetKind::Texture:
				manifest << "texture " << asset.Id.ToString() << ' ' << path << ' '
					<< (catalog.GetTexture({asset.Id}).ColorSpace == TextureColorSpace::SRGB ? "srgb" : "linear") << '\n';
				break;
			case AssetKind::Mesh:
				manifest << "mesh " << asset.Id.ToString() << ' ' << path << '\n';
				break;
			case AssetKind::Audio:
				manifest << "audio " << asset.Id.ToString() << ' ' << path << '\n';
				break;
			case AssetKind::Sound:
				manifest << "sound " << asset.Id.ToString() << ' ' << path << '\n';
				break;
			default:
				throw std::invalid_argument("This kind cannot be included as a player asset: " + asset.Id.ToString());
			}
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
		// A scene's dependencies are its AssetRef fields, tagged with their asset type (FD-08).
		const std::pair<std::uint64_t, AssetKind> kinds[]{
			{ ECS::Detail::HashText(AssetTypeTraits<TextureAsset>::Name), AssetKind::Texture },
			{ ECS::Detail::HashText(AssetTypeTraits<MeshAsset>::Name), AssetKind::Mesh },
			{ ECS::Detail::HashText(AssetTypeTraits<AudioAsset>::Name), AssetKind::Audio },
			{ ECS::Detail::HashText(AssetTypeTraits<SoundAsset>::Name), AssetKind::Sound } };

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
			const auto scene = SceneFile::Decode(bytes);
			for (const auto& component : scene.Components)
				for (std::size_t field = 0; field < component.Fields.size(); ++field)
				{
					const auto& description = component.Fields[field];
					const auto kind = std::find_if(std::begin(kinds), std::end(kinds),
						[&](const auto& entry) { return entry.first == description.Tag; });
					if (description.Kind != ECS::FieldKind::AssetRef || kind == std::end(kinds))
						continue;
					const auto count = std::size_t{ description.Extent[0] } * description.Extent[1] * component.Entities.size();
					const auto* values = component.Values.data() + component.FieldOffset(field);
					for (std::size_t item = 0; item < count; ++item)
					{
						AssetId asset;
						std::memcpy(&asset, values + item * sizeof(AssetId), sizeof(AssetId));
						if (asset.IsValid())
							require(asset, kind->second);
					}
				}
			selected.push_back({ &*found, std::move(bytes) });
		}

		// A sound event plays its clips, so the player needs them too.
		for (const auto& id : std::set<AssetId>(requiredAssets))
		{
			const auto& record = project.Catalog.Get(id);
			if (record.Kind != AssetKind::Sound)
				continue;
			const auto path = ResolveAssetPath(source, record.SourcePath);
			for (const auto& clip : ParseSoundEvent(Files::ReadBinary(path, 1024 * 1024), path).Clips)
				require(clip, AssetKind::Audio);
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
