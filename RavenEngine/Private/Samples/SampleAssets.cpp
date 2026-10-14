#include "SampleAssets.hpp"

namespace Raven
{
	SampleAssets::SampleAssets(VulkanContext& renderer)
		: m_Resources(m_Catalog, renderer)
	{
		m_Catalog.Register(TextureAssetDesc{ GetUVGrid().Id, "Textures/UVGrid.png", TextureColorSpace::SRGB });
		m_Catalog.Register(TextureAssetDesc{ GetUVGridAlternate().Id,
			"Textures/UVGridAlternate.png", TextureColorSpace::SRGB });
	}

	const VulkanTexture& SampleAssets::LoadTexture(AssetRef<TextureAsset> asset)
	{
		return m_Resources.LoadTexture(asset);
	}

	AssetRef<TextureAsset> SampleAssets::GetUVGrid()
	{
		return { AssetId::Parse("7a590cee-ec82-4a27-927f-860e64619c19") };
	}

	AssetRef<TextureAsset> SampleAssets::GetUVGridAlternate()
	{
		return { AssetId::Parse("a1552d51-3e07-483c-aabd-366e71785a34") };
	}
}
