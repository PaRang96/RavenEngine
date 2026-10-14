#include "Raven/Assets/AssetResources.hpp"

#include "Raven/Assets/AssetCatalog.hpp"
#include "Raven/Assets/Image.hpp"
#include "Raven/Core/Paths.hpp"
#include "Raven/Renderer/Vulkan/VulkanContext.hpp"

#include <filesystem>
#include <map>

namespace Raven
{
	struct AssetResources::Impl
	{
		explicit Impl(const AssetCatalog& catalog, VulkanContext& renderer)
			: Catalog(catalog), Renderer(renderer), ContentRoot(Paths::GetContentPath())
		{
		}

		const VulkanTexture& LoadTexture(AssetRef<TextureAsset> asset);

		const AssetCatalog& Catalog;
		VulkanContext& Renderer;
		const std::filesystem::path ContentRoot;
		std::map<AssetId, const VulkanTexture*> Textures;
	};

	const VulkanTexture& AssetResources::Impl::LoadTexture(AssetRef<TextureAsset> asset)
	{
		const auto& description = Catalog.GetTexture(asset);
		if (const auto found = Textures.find(asset.Id); found != Textures.end())
			return *found->second;

		// Allocate cache metadata before CreateTexture publishes a context-owned GPU resource.
		const auto entry = Textures.try_emplace(asset.Id, nullptr).first;
		try
		{
			const ImageData image = Images::Load(ContentRoot / description.SourcePath);
			const auto& texture = Renderer.CreateTexture(
				{ image.Width, image.Height, image.Pixels, description.ColorSpace });
			entry->second = &texture;
			return texture;
		}
		catch (...)
		{
			Textures.erase(entry);
			throw;
		}
	}

	AssetResources::AssetResources(const AssetCatalog& catalog, VulkanContext& renderer)
		: m_Impl(std::make_unique<Impl>(catalog, renderer))
	{
	}

	AssetResources::~AssetResources() = default;

	const VulkanTexture& AssetResources::LoadTexture(AssetRef<TextureAsset> asset)
	{
		return m_Impl->LoadTexture(asset);
	}
}
