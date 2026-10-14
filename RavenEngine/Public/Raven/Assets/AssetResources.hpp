#pragma once

#include "Raven/Assets/AssetRef.hpp"
#include "Raven/Core/Api.hpp"

#include <memory>

namespace Raven
{
	class AssetCatalog;
	class VulkanContext;
	class VulkanTexture;

	class AssetResources
	{
	public:
		// Synchronous, rendering-thread service. Catalog and renderer must outlive this object.
		RAVEN_API AssetResources(const AssetCatalog& catalog, VulkanContext& renderer);
		RAVEN_API ~AssetResources();

		// Reuses a context-owned resource; its reference remains valid until renderer shutdown.
		RAVEN_API const VulkanTexture& LoadTexture(AssetRef<TextureAsset> asset);

		AssetResources(const AssetResources&) = delete;
		AssetResources& operator=(const AssetResources&) = delete;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
