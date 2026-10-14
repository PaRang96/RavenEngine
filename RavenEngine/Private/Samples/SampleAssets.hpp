#pragma once

#include "Raven/Assets/AssetCatalog.hpp"
#include "Raven/Assets/AssetResources.hpp"

namespace Raven
{
	class SampleAssets
	{
	public:
		explicit SampleAssets(VulkanContext& renderer);
		const VulkanTexture& LoadTexture(AssetRef<TextureAsset> asset);

		static AssetRef<TextureAsset> GetUVGrid();
		static AssetRef<TextureAsset> GetUVGridAlternate();

	private:
		AssetCatalog m_Catalog;
		AssetResources m_Resources;
	};
}
