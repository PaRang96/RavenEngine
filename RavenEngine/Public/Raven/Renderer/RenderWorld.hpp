#pragma once

#include "Raven/Core/Api.hpp"
#include "Raven/Assets/AssetRef.hpp"
#include "Raven/Components/MaterialComponent.hpp"
#include "Raven/ECS/Entity.hpp"
#include "Raven/Renderer/MeshHandle.hpp"

#include <cstddef>
#include <functional>
#include <memory>

namespace Raven
{
	class Scene;
	class VulkanTexture;

	// Host-owned mirror of one scene's lit meshes for GPU-driven rendering
	// (docs/GpuDrivenDesign.md, G1). Sync follows change ticks and the Registry's lifecycle
	// journal, so its cost grows with changed objects, not scene size. It stores values and
	// handles only, so it survives game-module replacement together with the Registry.
	class RenderWorld
	{
	public:
		// Maps a material to a texture of the drawing context; null selects white. Called only
		// for new objects and changed materials.
		using TextureResolver = std::function<const VulkanTexture*(ECS::Entity, const ECS::MaterialComponent&)>;
		// Maps a mesh reference to geometry of the drawing context; a null handle draws nothing.
		// Called only for new objects and changed mesh components.
		using MeshResolver = std::function<MeshHandle(ECS::Entity, AssetRef<MeshAsset>)>;

		RAVEN_API RenderWorld();
		RAVEN_API ~RenderWorld();

		// After Scene::UpdateWorldTransforms, on the rendering thread, outside view iteration.
		// A different scene, or a journal resync, rebuilds the world once. If it throws, the
		// world is cleared and the next Sync rebuilds.
		RAVEN_API void Sync(Scene& scene, const TextureResolver& resolveTexture, const MeshResolver& resolveMesh);
		// Drops every object and forgets the scene; the next Sync rebuilds.
		RAVEN_API void Clear() noexcept;
		RAVEN_API std::size_t GetObjectCount() const noexcept;

		RenderWorld(const RenderWorld&) = delete;
		RenderWorld& operator=(const RenderWorld&) = delete;

		// Renderer-side state (Renderer/RenderWorldState.hpp).
		struct State;
		State& GetState() noexcept { return *m_State; }

	private:
		void SyncUnchecked(Scene& scene, const TextureResolver& resolveTexture, const MeshResolver& resolveMesh);

		std::unique_ptr<State> m_State;
	};
}
