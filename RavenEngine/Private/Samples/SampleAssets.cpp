#include "SampleAssets.hpp"

namespace Raven
{
	SampleAssets::SampleAssets(VulkanContext& renderer, bool development, bool watchMesh, bool watchTexture)
		: m_Project(renderer, SceneProject::DefaultContentRoot(development || watchMesh || watchTexture),
			development, watchMesh, development || watchTexture)
	{
	}

	const VulkanTexture& SampleAssets::LoadTexture(AssetRef<TextureAsset> asset)
	{
		auto found = m_Shared.find(asset.Id);
		if (found == m_Shared.end())
			found = m_Shared.emplace(asset.Id, m_Project.GetAssets().Acquire(GetCatalog(), asset.Id, AssetKind::Texture)).first;
		return found->second.GetTexture();
	}

	AssetReloadResult SampleAssets::ApplyReloads() { return m_Project.GetAssets().ApplyReloads(); }

	AssetRef<AudioAsset> SampleAssets::GetWinSound()
	{
		return { AssetId::Parse("ef451c5a-02db-4d81-89a1-ec2ea98ed719") };
	}

	AssetRef<AudioAsset> SampleAssets::GetLossSound()
	{
		return { AssetId::Parse("0b47e26c-7486-45d8-968c-f7507a8feb29") };
	}
}
