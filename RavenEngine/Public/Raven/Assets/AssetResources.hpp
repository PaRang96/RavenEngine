#pragma once

#include "Raven/Assets/AssetRef.hpp"
#include "Raven/Core/Api.hpp"
#include "Raven/Renderer/MeshHandle.hpp"

#include <filesystem>
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

		// Imports one static mesh from FBX and reuses its renderer-scoped handle.
		RAVEN_API MeshHandle LoadMesh(AssetRef<MeshAsset> asset);

		// Load the staged mesh, then watch the source on a worker.
		RAVEN_API void WatchFbx(AssetRef<MeshAsset> asset, const std::filesystem::path& source);
		// Between frames on the rendering thread. Returns true if any mesh was replaced.
		RAVEN_API bool ApplyMeshReloads();

		// Load the staged baseline, then watch the source on a worker. Each texture ID can be watched once.
		RAVEN_API void WatchPng(AssetRef<TextureAsset> asset, const std::filesystem::path& source);
		RAVEN_API bool IsWatching(AssetRef<TextureAsset> asset) const noexcept;

		// Between frames on the rendering thread. Returns true if a replacement was published.
		RAVEN_API bool ApplyTextureReload();

		AssetResources(const AssetResources&) = delete;
		AssetResources& operator=(const AssetResources&) = delete;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
