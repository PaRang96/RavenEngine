#pragma once

#include "Raven/Core/Api.hpp"
#include "Raven/ECS/Entity.hpp"
#include "Raven/Renderer/CameraView.hpp"

#include <limits>
#include <optional>

namespace Raven
{
	class Scene;
	class VulkanContext;

	struct PickHit
	{
		ECS::Entity Entity;
		float Distance = 0.0f;
	};

	// The closest static mesh of the scene that the ray hits within maxDistance. Only meshes the
	// scene already leases can be hit; picking never loads an asset. A free function, so Scene
	// itself never depends on the renderer (docs/DataModelDesign.md, M6-5).
	RAVEN_API std::optional<PickHit> Pick(const Scene& scene, const VulkanContext& renderer, const Ray& ray,
		float maxDistance = std::numeric_limits<float>::max());
}
