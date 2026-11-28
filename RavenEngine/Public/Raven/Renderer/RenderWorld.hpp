#pragma once

#include "Raven/Core/Api.hpp"
#include "Raven/Assets/AssetRecord.hpp"
#include "Raven/Assets/AssetRef.hpp"
#include "Raven/Components/MaterialComponent.hpp"
#include "Raven/ECS/Entity.hpp"
#include "Raven/Renderer/MeshHandle.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace Raven
{
	class AssetLease;
	class JobSystem;
	class Scene;

	// Host-owned mirror of one scene's lit meshes and sprites for GPU-driven rendering
	// (docs/GpuDrivenDesign.md, G1; docs/RenderingContentDesign.md, B0 contract 4). Sync follows change
	// ticks and the Registry's lifecycle journal, so its cost grows with changed objects, not scene size.
	// It stores values and handles only, so it survives game-module replacement together with the Registry.
	class RenderWorld
	{
	public:
		// Leases a scene asset: a material file or a texture. Null when the reference cannot be used:
		// a material then falls back to the entity's inline values and a texture samples white
		// (FD-06). Called only for new material keys and for material files that reloaded; the world
		// keeps copies of the leases it uses (docs/RenderingContentDesign.md, B0 contract 6).
		using AssetResolver = std::function<const AssetLease*(AssetId, AssetKind)>;
		// The drawing context's pipeline key for a leased material shader (B0 contract 7); 0 draws with
		// the engine's lit pipeline. Called for new materials and when a material or its shader reloads.
		using ShaderResolver = std::function<std::uint32_t(const AssetLease&)>;
		// Maps a mesh reference to geometry of the drawing context; a null handle draws nothing.
		// Called only for new objects and changed mesh components.
		using MeshResolver = std::function<MeshHandle(ECS::Entity, AssetRef<MeshAsset>)>;

		RAVEN_API RenderWorld();
		RAVEN_API ~RenderWorld();

		// After Scene::UpdateWorldTransforms, on the rendering thread, outside view iteration.
		// A different scene, or a journal resync, rebuilds the world once. If it throws, the
		// world is cleared and the next Sync rebuilds. Sprites draw on spriteMesh, the drawing context's
		// quad (VulkanContext::GetSpriteMesh); a different one rebuilds, and a null one draws none. With
		// workers (the host's job system), objects that only moved are written on them; the result is
		// the same either way.
		RAVEN_API void Sync(Scene& scene, const AssetResolver& resolveAsset, const MeshResolver& resolveMesh,
			const ShaderResolver& resolveShader = {}, MeshHandle spriteMesh = {}, JobSystem* workers = nullptr);
		// Drops every object and forgets the scene; the next Sync rebuilds.
		RAVEN_API void Clear() noexcept;
		RAVEN_API std::size_t GetObjectCount() const noexcept;
		// Sprites in the world, drawable or not.
		RAVEN_API std::size_t GetSpriteCount() const noexcept;

		RenderWorld(const RenderWorld&) = delete;
		RenderWorld& operator=(const RenderWorld&) = delete;

		// Renderer-side state (Renderer/RenderWorldState.hpp).
		struct State;
		State& GetState() noexcept { return *m_State; }

	private:
		void SyncUnchecked(Scene& scene, const AssetResolver& resolveAsset, const MeshResolver& resolveMesh,
			const ShaderResolver& resolveShader, MeshHandle spriteMesh, JobSystem* workers);

		std::unique_ptr<State> m_State;
	};
}
