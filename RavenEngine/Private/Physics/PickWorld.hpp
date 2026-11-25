#pragma once

#include "Raven/Assets/AssetId.hpp"
#include "Raven/Core/Api.hpp"
#include "Raven/Math/Transform.hpp"
#include "Raven/Renderer/CameraView.hpp"
#include "Raven/Scene/ScenePicking.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>

namespace Raven
{
	class Scene;

	namespace ECS
	{
		class Registry;
	}

	// The CPU triangles of one version of a mesh asset. Owner identifies that version, so a reloaded
	// mesh is noticed, and keeps the spans valid while the pick world copies them.
	struct PickMesh
	{
		std::shared_ptr<const void> Owner;
		std::span<const Vec3> Vertices;
		std::span<const std::uint32_t> Indices;
	};

	// The current triangles of a mesh asset the scene holds, or an empty PickMesh. Never loads.
	using PickMeshResolver = std::function<PickMesh(AssetId mesh)>;

	struct PickStats
	{
		std::uint32_t Bodies = 0;
		std::uint32_t Shapes = 0;
		// The latest pick: bringing the bodies up to date, the ray query, and the mesh shapes built
		// during that sync.
		std::int64_t SyncNanoseconds = 0;
		std::int64_t QueryNanoseconds = 0;
		std::int64_t ShapeNanoseconds = 0;
	};

	// Picking on Jolt (docs/GameServicesDesign.md, FD-34). Every entity with a StaticMeshComponent and
	// a world transform gets a query-only body in a Jolt system of its own that is never simulated, so
	// renderables never use the simulation's body limit. Each mesh asset has one exact triangle shape,
	// shared by every entity that draws it and rebuilt when the mesh reloads; a non-unit scale wraps it
	// per entity. Bodies are brought up to date only when a pick runs, from the world transforms as
	// drawn, and the body capacity grows with the scene.
	class PickWorld final
	{
	public:
		RAVEN_API PickWorld();
		RAVEN_API ~PickWorld();

		// Forgets the scene; the next pick rebuilds from the components.
		RAVEN_API void Clear() noexcept;
		// The closest mesh hit within maxDistance, both triangle faces counted. Throws
		// std::invalid_argument for a non-finite or negative distance or a non-finite ray.
		RAVEN_API std::optional<PickHit> Pick(Scene& scene, ECS::Registry& registry, const PickMeshResolver& meshes,
			const Ray& ray, float maxDistance);
		RAVEN_API PickStats GetStats() const noexcept;

		PickWorld(const PickWorld&) = delete;
		PickWorld& operator=(const PickWorld&) = delete;

	private:
		friend class PickWorldSync;
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
