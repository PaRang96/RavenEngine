#pragma once

#include "Raven/Scene/SceneProject.hpp"

#include <map>

namespace Raven
{
	// Sample adapter: project services own metadata and scoped resources.
	class SampleAssets
	{
	public:
		explicit SampleAssets(VulkanContext& renderer, bool development = false,
			bool watchMesh = false, bool watchTexture = false);
		const AssetCatalog& GetCatalog() const noexcept { return m_Project.GetCatalog(); }
		SceneProject& GetProject() noexcept { return m_Project; }
		// The diagnostic quads are application-owned and keep their explicit leases.
		const VulkanTexture& LoadTexture(AssetRef<TextureAsset> asset);
		AssetReloadResult ApplyReloads();

		static AssetRef<TextureAsset> GetUVGrid()
		{
			return { AssetId::Parse("7a590cee-ec82-4a27-927f-860e64619c19") };
		}
		static AssetRef<TextureAsset> GetUVGridAlternate()
		{
			return { AssetId::Parse("a1552d51-3e07-483c-aabd-366e71785a34") };
		}
		static AssetRef<AudioAsset> GetWinSound();
		static AssetRef<AudioAsset> GetLossSound();

	private:
		SceneProject m_Project;
		std::map<AssetId, AssetLease> m_Shared;
	};
}
