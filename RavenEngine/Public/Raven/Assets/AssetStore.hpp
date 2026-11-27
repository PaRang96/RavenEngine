#pragma once

#include "Raven/Assets/AssetRecord.hpp"
#include "Raven/Assets/AssetRef.hpp"
#include "Raven/Renderer/MeshHandle.hpp"
#include "Raven/Renderer/TextureHandle.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <utility>

namespace Raven
{
	class AssetCatalog;
	class VulkanContext;
	class VulkanTexture;
	class AudioPlayer;
	struct ManagedAsset;
	struct SoundEvent;

	// Host-owned lease. It must die before the renderer; it is never retained in ECS data.
	class AssetLease
	{
	public:
		AssetLease() = default;
		RAVEN_API AssetId GetId() const noexcept;
		RAVEN_API AssetKind GetKind() const noexcept;
		RAVEN_API MeshHandle GetMesh() const;
		RAVEN_API TextureHandle GetTextureHandle() const;
		RAVEN_API const VulkanTexture& GetTexture() const;
		// A sound-event asset's parsed contents; null for other kinds.
		RAVEN_API const SoundEvent* GetSoundEvent() const noexcept;

	private:
		friend class AssetStore;
		friend class AudioPlayer;
		explicit AssetLease(std::shared_ptr<ManagedAsset> asset) : m_Asset(std::move(asset)) {}
		std::shared_ptr<ManagedAsset> m_Asset;
	};

	class AssetCatalogUpdate
	{
	public:
		RAVEN_API AssetCatalogUpdate();
		RAVEN_API ~AssetCatalogUpdate();
		RAVEN_API AssetCatalogUpdate(AssetCatalogUpdate&&) noexcept;
		RAVEN_API AssetCatalogUpdate& operator=(AssetCatalogUpdate&&) noexcept;
		RAVEN_API bool IsCurrent() const;
		// Call only after IsCurrent at the publication boundary; no new GPU work in between.
		RAVEN_API void Commit() noexcept;

	private:
		friend class AssetStore;
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};

	struct AssetStoreStats
	{
		std::size_t CachedEntries = 0;
		std::size_t ResidentAssets = 0;
		std::size_t Watchers = 0;
	};

	struct AssetReloadResult
	{
		bool Mesh = false;
		bool Texture = false;
	};

	// Main/render-thread cache of weak entries. Leases own resources, not the cache.
	// The renderer outlives this service and every lease.
	class AssetStore
	{
	public:
		RAVEN_API AssetStore(VulkanContext& renderer, std::filesystem::path contentRoot,
			bool watchMeshes = false, bool watchTextures = false);
		RAVEN_API ~AssetStore();
		RAVEN_API AssetLease Acquire(const AssetCatalog& catalog, AssetId id, AssetKind kind);
		// Why `id` cannot be acquired as `kind` because of the data (unregistered ID, wrong kind or
		// missing file), or null when Acquire may proceed. Decode and GPU failures still throw.
		RAVEN_API const char* FindReferenceError(const AssetCatalog& catalog, AssetId id, AssetKind kind) const;
		RAVEN_API AssetCatalogUpdate PrepareCatalog(const AssetCatalog& catalog);
		RAVEN_API AssetReloadResult ApplyReloads();
		RAVEN_API void Collect();
		RAVEN_API AssetStoreStats GetStats() const noexcept;

		AssetStore(const AssetStore&) = delete;
		AssetStore& operator=(const AssetStore&) = delete;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
