#include "PlayerContent.hpp"

#include "Assets/AssetPath.hpp"
#include "Raven/Core/Files.hpp"
#include "Raven/Scene/SceneDocument.hpp"
#include "Samples/SampleAssets.hpp"

#include <algorithm>
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
		require(SampleAssets::GetUVGrid().Id, AssetKind::Texture);
		require(SampleAssets::GetUVGridAlternate().Id, AssetKind::Texture);
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
			if (found->Type != SceneType::Perspective)
				throw std::invalid_argument("Player packaging currently supports Perspective scenes: " + id.ToString());
			auto bytes = Files::ReadBinary(ResolveAssetPath(source, found->SourcePath), 4 * 1024 * 1024);
			const auto scene = SceneDocument::Decode(bytes);
			for (const auto& mesh : scene.Meshes)
				require(mesh.Asset.Id, AssetKind::Mesh);
			for (const auto& [name, material] : scene.Materials)
				if (material.BaseColorTexture.IsValid())
					require(material.BaseColorTexture.Id, AssetKind::Texture);
			if (scene.WinSound.IsValid())
				require(scene.WinSound.Id, AssetKind::Audio);
			selected.push_back({ &*found, std::move(bytes) });
		}

		std::ostringstream manifest;
		manifest << "raven-project 1\n";
		for (const auto& id : requiredAssets)
			WriteAssetRecord(manifest, project.Catalog, project.Catalog.Get(id));
		for (const auto& scene : selected)
			manifest << "scene " << scene.Description->Id.ToString() << " perspective "
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
