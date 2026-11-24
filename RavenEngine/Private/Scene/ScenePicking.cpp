#include "Raven/Scene/ScenePicking.hpp"
#include "Raven/Components/StaticMeshComponent.hpp"
#include "Raven/Renderer/Vulkan/VulkanContext.hpp"
#include "Raven/Scene/Scene.hpp"

#include <cmath>
#include <stdexcept>

namespace Raven
{
	std::optional<PickHit> Pick(const Scene& scene, const VulkanContext& renderer, const Ray& ray, float maxDistance)
	{
		if (!std::isfinite(maxDistance) || maxDistance < 0)
			throw std::invalid_argument("Picking distance must be finite and nonnegative");
		std::optional<PickHit> result;
		for (const auto entity : scene.GetEntities())
		{
			const auto* mesh = scene.FindComponent<ECS::StaticMeshComponent>(entity);
			if (!mesh)
				continue;
			const auto* lease = scene.FindAsset(mesh->Mesh.Id);
			if (!lease || lease->GetKind() != AssetKind::Mesh)
				continue;
			if (const auto distance = renderer.IntersectMesh(lease->GetMesh(), scene.GetWorldTransform(entity), ray);
				distance && *distance <= maxDistance && (!result || *distance < result->Distance))
				result = PickHit{ entity, *distance };
		}
		return result;
	}
}
