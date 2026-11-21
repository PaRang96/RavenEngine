#pragma once

#include "Raven/Assets/AssetStore.hpp"
#include "Raven/Core/ApplicationClient.hpp"
#include "Raven/ECS/Registry.hpp"
#include "Raven/Renderer/CameraView.hpp"

#include <map>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Raven
{
	namespace ECS { struct TransformComponent; }

	// Host-owned entities, selected camera, gameplay bindings and asset leases for one scene.
	// Registry and store outlive the scene; game modules borrow only values/handles.
	class Scene
	{
	public:
		RAVEN_API Scene(ECS::Registry& registry, AssetStore& store);
		RAVEN_API virtual ~Scene();
		RAVEN_API ECS::Entity CreateEntity(const std::string& name = {});
		RAVEN_API ECS::Entity FindEntity(const std::string& name) const;
		// Names are immutable authored identities scoped to this scene, independent of handles.
		RAVEN_API std::string GetAuthoredId(ECS::Entity entity) const;
		RAVEN_API ECS::Entity GetParent(ECS::Entity entity) const;
		// Reparent/detach preserves local TRS. A null parent detaches to the scene root.
		RAVEN_API void SetParent(ECS::Entity entity, ECS::Entity parent = {});
		// Destroys the owned subtree, including descendants of an externally destroyed root.
		RAVEN_API void DestroyEntity(ECS::Entity entity);
		RAVEN_API Mat4 GetWorldTransform(ECS::Entity entity) const;

		// Host PostUpdate (docs/ECSDesign.md, D7). Refreshes WorldTransformComponent for owned
		// entities whose Transform changed since the previous call, whose ancestry changed, or
		// that have no world yet. Parents are processed before children. Throws when a live
		// entity's ancestor has no TransformComponent. Not callable while a view iterates.
		RAVEN_API void UpdateWorldTransforms();

		// Full affine position; rotation-only ancestor chain ignores scale/reflection for axes.
		RAVEN_API CameraView GetCameraView(ECS::Entity entity, float aspect) const;
		RAVEN_API const AssetLease& UseAsset(const AssetCatalog& catalog, AssetId id, AssetKind kind);
		RAVEN_API const AssetLease& GetAsset(AssetId id) const;
		const std::vector<ECS::Entity>& GetEntities() const noexcept { return m_Entities; }
		ECS::Entity GetSelectedCamera() const noexcept { return m_SelectedCamera; }
		// Selection retains an owned handle even if its entity later becomes stale.
		RAVEN_API void SelectCamera(ECS::Entity camera);
		SceneAccess GetAccess() noexcept { return SceneAccess(*this); }
		GameBindings GetBindings() const noexcept { return m_Bindings; }
		struct PickHit { ECS::Entity Entity; float Distance; };
		RAVEN_API std::optional<PickHit> Pick(const VulkanContext& renderer, const Ray& ray,
			float maxDistance = std::numeric_limits<float>::max()) const;
		RAVEN_API void SetBindings(GameBindings bindings);

		Scene(const Scene&) = delete;
		Scene& operator=(const Scene&) = delete;

	protected:
		ECS::Registry& m_Registry;

	private:
		friend class SceneAccess;
		struct EntityHash
		{
			std::size_t operator()(ECS::Entity entity) const noexcept;
		};
		bool OwnsLiveEntity(ECS::Entity entity) const noexcept;
		void RequireOwned(ECS::Entity entity) const;
		Mat4 ComposeTransform(ECS::Entity entity, bool rotationOnly) const;
		Mat4 ComposeTransformUnchecked(ECS::Entity entity, bool rotationOnly) const;
		void RebuildWorldOrder();
		std::vector<ECS::Entity> m_Entities;
		// The index also records membership. Roots have a null parent.
		std::unordered_map<ECS::Entity, ECS::Entity, EntityHash> m_Parents;
		std::map<std::string, ECS::Entity> m_Names;
		std::map<AssetId, AssetLease> m_Assets;
		AssetStore& m_Store;
		ECS::Entity m_SelectedCamera;
		GameBindings m_Bindings;
		// Marks owned entities in the registry so scene-scoped views skip foreign ones.
		std::uint32_t m_SceneId = 0;
		// Owned entities with parents before children, each parent as an index into the
		// order. Rebuilt after create, destroy or reparent.
		static constexpr std::uint32_t NoWorldParent = 0xffffffffu;
		std::vector<ECS::Entity> m_WorldOrder;
		std::vector<std::uint32_t> m_WorldParents;
		std::vector<std::uint8_t> m_WorldChanged;
		bool m_WorldOrderDirty = true;
		bool m_WorldFull = true;
		std::uint32_t m_WorldTick = 0;
	};
}
