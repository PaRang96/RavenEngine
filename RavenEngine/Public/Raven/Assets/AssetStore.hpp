#pragma once

#include "Raven/Assets/AssetRecord.hpp"
#include "Raven/Assets/AssetRef.hpp"
#include "Raven/Renderer/MeshHandle.hpp"
#include "Raven/Renderer/TextureHandle.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <utility>

namespace Raven
{
	class AssetCatalog;
	class JobSystem;
	class VulkanContext;
	class VulkanTexture;
	class AudioPlayer;
	struct ManagedAsset;
	struct SoundEvent;
	struct MaterialDefinition;
	struct MaterialShader;

	// Host-owned lease. It must die before the renderer; it is never retained in ECS data.
	class AssetLease
	{
	public:
		AssetLease() = default;
		RAVEN_API AssetId GetId() const noexcept;
		RAVEN_API AssetKind GetKind() const noexcept;
		// Loading and failed assets have no contents: no mesh or texture handle, no clip, event, material
		// or shader. Their GetRevision changes when the contents arrive.
		RAVEN_API AssetLoadState GetLoadState() const noexcept;
		bool IsReady() const noexcept { return GetLoadState() == AssetLoadState::Ready; }
		RAVEN_API MeshHandle GetMesh() const;
		RAVEN_API TextureHandle GetTextureHandle() const;
		RAVEN_API const VulkanTexture& GetTexture() const;
		// A sound-event asset's parsed contents; null for other kinds.
		RAVEN_API const SoundEvent* GetSoundEvent() const noexcept;
		// A material asset's parsed contents; null for other kinds.
		RAVEN_API const MaterialDefinition* GetMaterial() const noexcept;
		// A material shader asset's compiled program and parameters; null for other kinds.
		RAVEN_API const MaterialShader* GetMaterialShader() const noexcept;
		// Changes with every reload committed to the asset, and when a background load arrives; holders
		// compare it to notice new contents.
		RAVEN_API std::uint64_t GetRevision() const noexcept;

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
		// Background loads not yet published.
		std::size_t Loading = 0;
	};

	struct AssetReloadResult
	{
		bool Mesh = false;
		bool Texture = false;
		bool Material = false;
		// Background loads published.
		std::size_t Loaded = 0;
	};

	// Main/render-thread cache of weak entries. Leases own resources, not the cache.
	// The renderer outlives this service and every lease.
	class AssetStore
	{
	public:
		RAVEN_API AssetStore(VulkanContext& renderer, std::filesystem::path contentRoot,
			bool watchMeshes = false, bool watchTextures = false);
		RAVEN_API ~AssetStore();
		// Loads on the calling thread; an asset still loading in the background is finished first. Scenes
		// preload what they reference this way.
		RAVEN_API AssetLease Acquire(const AssetCatalog& catalog, AssetId id, AssetKind kind);
		// For an ID first used at runtime (FD-22): an asset not resident yet is returned Loading and decoded
		// on the workers at background priority; ApplyReloads publishes it. Without workers it loads like
		// Acquire. Reference errors throw as Acquire's do.
		RAVEN_API AssetLease Request(const AssetCatalog& catalog, AssetId id, AssetKind kind);
		// The engine's shared workers for Request; they must outlive this store.
		RAVEN_API void UseWorkers(JobSystem* workers) noexcept;
		// Why `id` cannot be acquired as `kind` because of the data (unregistered ID, wrong kind or
		// missing file), or null when Acquire may proceed. Decode and GPU failures still throw.
		RAVEN_API const char* FindReferenceError(const AssetCatalog& catalog, AssetId id, AssetKind kind) const;
		RAVEN_API AssetCatalogUpdate PrepareCatalog(const AssetCatalog& catalog);
		// On the main thread between frames: publishes watched files' reloads and finished background loads.
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
