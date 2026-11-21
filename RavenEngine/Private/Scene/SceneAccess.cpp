#include "Raven/Scene/Scene.hpp"

#include <algorithm>
#include <stdexcept>

namespace Raven
{
	ECS::Entity SceneAccess::Find(std::string_view id) const
	{
		return m_Scene ? m_Scene->FindEntity(std::string(id)) : ECS::Entity{};
	}

	bool SceneAccess::IsAlive(ECS::Entity entity) const noexcept
	{
		return m_Scene && m_Scene->OwnsLiveEntity(entity);
	}

	bool SceneAccess::ReadComponent(ECS::Entity entity, const ECS::ComponentInfo& info, void* value) const noexcept
	{
		return IsAlive(entity) && m_Scene->m_Registry.ReadComponent(entity, info, value);
	}

	bool SceneAccess::WriteComponent(ECS::Entity entity, const ECS::ComponentInfo& info, const void* value) const noexcept
	{
		return IsAlive(entity) && m_Scene->m_Registry.WriteComponent(entity, info, value);
	}

	std::vector<ECS::Entity> SceneAccess::Query(std::span<const ECS::ComponentInfo* const> components) const
	{
		std::vector<ECS::Entity> result;
		if (!m_Scene)
			return result;
		for (const auto entity : m_Scene->m_Entities)
			if (m_Scene->m_Registry.IsAlive(entity) &&
				std::all_of(components.begin(), components.end(), [this, entity](const auto* component)
							{ return component && m_Scene->m_Registry.HasComponent(entity, *component); }))
				result.push_back(entity);
		return result;
	}

	Mat4 SceneAccess::GetWorldTransform(ECS::Entity entity) const
	{
		if (!m_Scene)
			throw std::invalid_argument("No scene bound to game update");
		return m_Scene->GetWorldTransform(entity);
	}
}
