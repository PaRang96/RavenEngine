#pragma once

#include "Raven/Assets/AssetStore.hpp"
#include "Raven/ECS/Registry.hpp"
#include "Raven/Renderer/CameraView.hpp"
#include "Raven/Scene/SceneAccess.hpp"

#include <map>
#include <limits>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Raven
{
	namespace ECS { struct TransformComponent; }
	class JobSystem;

	// Host-owned entities, their hierarchy and persistent IDs, and asset leases for one scene.
	// Registry and store outlive the scene; game modules borrow only values and handles.
	class Scene
	{
	public:
		RAVEN_API Scene(ECS::Registry& registry, AssetStore& store);
		// A scene without assets, for tools and CPU tests (docs/DataModelDesign.md, M6-5): every
		// lease request is a data error, and nothing here needs a window or a GPU.
		RAVEN_API explicit Scene(ECS::Registry& registry);
		RAVEN_API virtual ~Scene();
		// Every entity gets a random persistent ID (docs/DataModelDesign.md, FD-09). A loader passes
		// the stored ID instead; a null or already used ID is rejected. Names are optional labels.
		RAVEN_API ECS::Entity CreateEntity(const std::string& name = {});
		RAVEN_API ECS::Entity CreateEntity(const std::string& name, ECS::EntityRef id);
		RAVEN_API ECS::Entity FindEntity(const std::string& name) const;
		// The label given at creation, or empty.
		RAVEN_API std::string GetAuthoredId(ECS::Entity entity) const;
		// The live owned entity with this ID, or null.
		RAVEN_API ECS::Entity Resolve(ECS::EntityRef id) const noexcept;
		// The persistent ID of an owned entity; null for entities this scene does not own.
		RAVEN_API ECS::EntityRef GetRef(ECS::Entity entity) const noexcept;
		// A fresh ID no entity of this scene uses; CreateEntity(name, id) claims it later.
		RAVEN_API ECS::EntityRef NewRef();
		// Restarts the simulation ID sequence from a seed, so entities created in fixed steps get the
		// same IDs in every run with that seed (docs/GameServicesDesign.md, FD-48). IDs already in use
		// are skipped.
		RAVEN_API void SeedIds(std::uint64_t seed) noexcept;
		// The simulation sequence's current state; SeedIds(state) continues it, for world snapshots.
		RAVEN_API std::uint64_t GetIdState() const noexcept;
		// Which sequence NewRef and CreateEntity draw from. The host selects the seeded simulation
		// sequence only while a fixed step runs; at all other times IDs come from a second, OS-seeded
		// presentation sequence, so cosmetic spawns in Control or Update never shift the IDs the
		// simulation gets (review amendment, AA2-GP-03).
		RAVEN_API void UseSimulationIds(bool simulation) noexcept;
		RAVEN_API bool IsUsingSimulationIds() const noexcept;
		RAVEN_API ECS::Entity GetParent(ECS::Entity entity) const;
		// Reparent/detach preserves local TRS. A null parent detaches to the scene root. Writes
		// ParentComponent (FD-07), so it is rejected while a view iterates. Costs the moved subtree.
		RAVEN_API void SetParent(ECS::Entity entity, ECS::Entity parent = {});
		// Destroys the owned subtree, including descendants of an externally destroyed root.
		// Costs the destroyed subtree, not the scene.
		RAVEN_API void DestroyEntity(ECS::Entity entity);
		RAVEN_API Mat4 GetWorldTransform(ECS::Entity entity) const;

		// Host PostUpdate (docs/ECSDesign.md, D7). Refreshes WorldTransformComponent for owned
		// entities whose Transform changed since the previous call, whose ancestry changed, or
		// that have no world yet. Parents are processed before children. An entity whose
		// ancestor was destroyed or has no TransformComponent is placed as a detached root and
		// logged once. Not callable while a view iterates. With workers (the host's job system), depths
		// holding many entities are split over them; the results are the same either way.
		RAVEN_API void UpdateWorldTransforms(JobSystem* workers = nullptr);

		// Full affine position; rotation-only ancestor chain ignores scale/reflection for axes.
		RAVEN_API CameraView GetCameraView(ECS::Entity entity, float aspect) const;
		// Loads now: scene load preloads every reference this way.
		RAVEN_API const AssetLease& UseAsset(const AssetCatalog& catalog, AssetId id, AssetKind kind);
		// Non-throwing form for game-written references: a data error (nil or unregistered ID,
		// wrong kind, missing file) returns null and logs once per asset, and a skipped asset stays
		// skipped until the scene reloads. An asset the scene does not hold yet loads in the background
		// (docs/RenderingContentDesign.md, FD-22): the lease is Loading until then, and a failed decode
		// leaves it Failed, logged once.
		RAVEN_API const AssetLease* TryUseAsset(const AssetCatalog& catalog, AssetId id, AssetKind kind);
		RAVEN_API const AssetLease& GetAsset(AssetId id) const;
		// True for a live entity this scene owns.
		RAVEN_API bool Owns(ECS::Entity entity) const noexcept;
		// A component of an owned entity, read without marking it changed; null otherwise.
		template<ECS::ComponentType T>
		const T* FindComponent(ECS::Entity entity) const noexcept
		{
			return Owns(entity) ? std::as_const(m_Registry).TryGet<T>(entity) : nullptr;
		}
		// The scene's lease of an asset, or null when it holds none. Never loads.
		RAVEN_API const AssetLease* FindAsset(AssetId id) const noexcept;
		// Creation order until an entity is destroyed; destruction moves the last entity into the gap.
		const std::vector<ECS::Entity>& GetEntities() const noexcept { return m_Entities; }
		SceneAccess GetAccess() noexcept { return SceneAccess(*this); }
		// Unique among the scenes of one registry: a new scene at a freed one's address differs.
		std::uint32_t GetSceneId() const noexcept { return m_SceneId; }

		Scene(const Scene&) = delete;
		Scene& operator=(const Scene&) = delete;

	protected:
		ECS::Registry& m_Registry;

	private:
		friend class SceneAccess;
		friend class SceneCommands;
		friend class SceneSerializer;
		friend class RenderWorld;
		struct EntityHash
		{
			std::size_t operator()(ECS::Entity entity) const noexcept;
		};
		static constexpr std::uint32_t NoNode = 0xffffffffu;
		// Runtime hierarchy index for one owned entity, kept at its registry slot index
		// (docs/DataModelDesign.md, FD-07). Parent mirrors ParentComponent; children form a list.
		struct Node
		{
			// Null when the slot is not owned by this scene. An externally destroyed entity
			// keeps its node until DestroyEntity or slot reuse removes it.
			ECS::Entity Entity;
			ECS::Entity Parent;
			std::uint64_t Id = 0;
			std::uint32_t FirstChild = NoNode;
			std::uint32_t NextSibling = NoNode;
			std::uint32_t PreviousSibling = NoNode;
			std::uint32_t Depth = 0;
			// Positions in m_Depths[Depth] and m_Entities.
			std::uint32_t DepthPosition = 0;
			std::uint32_t EntityPosition = 0;
		};
		const Node* FindNode(ECS::Entity entity) const noexcept;
		bool OwnsLiveEntity(ECS::Entity entity) const noexcept;
		void RequireOwned(ECS::Entity entity) const;
		ECS::Entity Adopt(ECS::Entity entity, const std::string& name, std::uint64_t id);
		std::uint64_t NextId();
		void Link(std::uint32_t child, std::uint32_t parent) noexcept;
		void Unlink(std::uint32_t child) noexcept;
		void SetSubtreeDepth(std::uint32_t root, std::uint32_t depth);
		void RemoveFromDepth(std::uint32_t index) noexcept;
		void Release(std::uint32_t index) noexcept;
		Mat4 ComposeTransform(ECS::Entity entity, bool rotationOnly) const;
		Mat4 ComposeTransformUnchecked(ECS::Entity entity, bool rotationOnly) const;
		void ReportDetachedRoot(ECS::Entity entity) const;
		std::vector<ECS::Entity> m_Entities;
		std::vector<Node> m_Nodes;
		// Node indices by depth; parents always sit at a smaller depth than their children.
		std::vector<std::vector<std::uint32_t>> m_Depths;
		std::unordered_map<std::uint64_t, std::uint32_t> m_Ids;
		// Kept between calls, so spawning and despawning leaves allocate nothing for their walks.
		std::vector<std::pair<std::uint32_t, std::uint32_t>> m_DepthWalk;
		std::vector<std::uint32_t> m_RemoveWalk;
		std::uint64_t m_IdState = 0;
		std::uint64_t m_PresentationIdState = 0;
		bool m_SimulationIds = false;
		// Entities already logged as detached roots, so a broken hierarchy logs once.
		mutable std::unordered_set<ECS::Entity, EntityHash> m_ReportedDetached;
		std::unordered_map<std::string, ECS::Entity> m_Names;
		std::unordered_map<std::uint32_t, std::string> m_Labels;
		std::map<AssetId, AssetLease> m_Assets;
		std::set<AssetId> m_ReportedAssets;
		AssetStore* m_Store = nullptr;
		// Marks owned entities in the registry so scene-scoped views skip foreign ones.
		std::uint32_t m_SceneId = 0;
		// What the world pass reads per node, kept compact and parallel to m_Nodes so the pass
		// streams 24 bytes per entity instead of the whole node.
		static constexpr std::uint8_t WorldChanged = 1;  // recomputed or dropped in the current pass
		static constexpr std::uint8_t WorldForced = 2;   // reparented or new: recompute next pass
		static constexpr std::uint8_t WorldPresent = 4;  // had a world after the last pass
		struct WorldSlot
		{
			ECS::Entity Entity;
			std::uint32_t Parent = NoNode;
			std::uint8_t Flags = 0;
		};
		std::vector<WorldSlot> m_World;
		bool m_WorldFull = true;
		std::uint32_t m_WorldTick = 0;
	};
}
