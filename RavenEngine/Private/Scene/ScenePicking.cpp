#include "Raven/Scene/Scene.hpp"
#include "Raven/Components/StaticMeshComponent.hpp"
#include "Raven/Renderer/Vulkan/VulkanContext.hpp"

namespace Raven
{
	std::optional<Scene::PickHit> Scene::Pick(const VulkanContext& renderer, const Ray& ray,
											  float maxDistance) const
	{
		if (!std::isfinite(maxDistance) || maxDistance < 0)
			throw std::invalid_argument("Picking distance must be finite and nonnegative");
		std::optional<PickHit> result;
		for (const auto entity : m_Entities)
		{
			ECS::StaticMeshComponent mesh;
			if (!m_Registry.Read(entity, mesh))
				continue;
			if (const auto distance = renderer.IntersectMesh(mesh.Mesh, GetWorldTransform(entity), ray);
				distance && *distance <= maxDistance && (!result || *distance < result->Distance))
				result = PickHit{entity, *distance};
		}
		return result;
	}
}
