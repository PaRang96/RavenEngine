#include "Raven/Scene/Scene.hpp"
#include "Raven/Components/TransformComponent.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace Raven
{
	namespace
	{
		// The generic forms of the compile-time HostOwned checks (docs/DataModelDesign.md, FD-07).
		bool IsHostOwned(const ECS::ComponentInfo& info) noexcept
		{
			return info.Id == ECS::ComponentTraits<ECS::WorldTransformComponent>::Info.Id ||
				info.Id == ECS::ComponentTraits<ECS::ParentComponent>::Info.Id;
		}
	}

	ECS::Entity SceneAccess::Find(std::string_view id) const
	{
		return m_Scene ? m_Scene->FindEntity(std::string(id)) : ECS::Entity{};
	}

	ECS::Entity SceneAccess::Resolve(ECS::EntityRef id) const noexcept
	{
		return m_Scene ? m_Scene->Resolve(id) : ECS::Entity{};
	}

	ECS::EntityRef SceneAccess::GetRef(ECS::Entity entity) const noexcept
	{
		return m_Scene ? m_Scene->GetRef(entity) : ECS::EntityRef{};
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
		return IsAlive(entity) && !IsHostOwned(info) && m_Scene->m_Registry.WriteComponent(entity, info, value);
	}

	std::vector<ECS::Entity> SceneAccess::Query(std::span<const ECS::ComponentInfo* const> components) const
	{
		std::vector<ECS::Entity> result;
		if (!m_Scene)
			return result;
		std::vector<ECS::Detail::PoolView> pools;
		pools.reserve(components.size());
		for (const auto* component : components)
		{
			if (!component)
				return result;
			pools.push_back(m_Scene->m_Registry.GetPool(*component));
			if (pools.back().Count == 0)
				return result;
		}
		// Scene order: walk the scene's entities and test pool membership directly.
		for (const auto entity : m_Scene->m_Entities)
			if (m_Scene->m_Registry.IsAlive(entity) &&
				std::all_of(pools.begin(), pools.end(), [entity](const ECS::Detail::PoolView& pool)
							{ return pool.Find(entity) != ECS::Detail::PoolView::NoIndex; }))
				result.push_back(entity);
		return result;
	}

	void* SceneAccess::TryGetComponent(ECS::Entity entity, const ECS::ComponentInfo& info) const noexcept
	{
		if (!m_Scene || m_Scene->m_Registry.GetOwner(entity) != m_Scene->m_SceneId || IsHostOwned(info))
			return nullptr;
		return m_Scene->m_Registry.TryGetComponent(entity, info);
	}

	const void* SceneAccess::FindComponent(ECS::Entity entity, const ECS::ComponentInfo& info) const noexcept
	{
		if (!m_Scene || m_Scene->m_Registry.GetOwner(entity) != m_Scene->m_SceneId)
			return nullptr;
		return std::as_const(m_Scene->m_Registry).TryGetComponent(entity, info);
	}

	ECS::Detail::ViewSource SceneAccess::GetViewSource() const noexcept
	{
		return m_Scene ? ECS::Detail::ViewSource{ &m_Scene->m_Registry, m_Scene->m_SceneId } : ECS::Detail::ViewSource{};
	}

	Mat4 SceneAccess::GetWorldTransform(ECS::Entity entity) const
	{
		if (!m_Scene)
			throw std::invalid_argument("No scene bound to game update");
		return m_Scene->GetWorldTransform(entity);
	}
}
