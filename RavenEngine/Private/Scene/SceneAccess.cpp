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

	bool SceneAccess::Read(ECS::Entity entity, ECS::Component& value) const noexcept
	{
		return IsAlive(entity) && m_Scene->m_Registry.Read(entity, value);
	}

	bool SceneAccess::Write(ECS::Entity entity, const ECS::Component& value) const noexcept
	{
		return IsAlive(entity) && m_Scene->m_Registry.Write(entity, value);
	}

	std::vector<ECS::Entity> SceneAccess::Query(std::span<const ECS::Component* const> components) const
	{
		std::vector<ECS::Entity> result;
		if (!m_Scene)
			return result;
		for (const auto entity : m_Scene->m_Entities)
			if (m_Scene->m_Registry.IsAlive(entity) &&
				std::all_of(components.begin(), components.end(), [this, entity](const auto* component)
							{ return component && m_Scene->m_Registry.Has(entity, *component); }))
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
