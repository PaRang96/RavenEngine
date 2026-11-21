#pragma once

#include "Raven/Core/Api.hpp"
#include "Raven/ECS/Component.hpp"
#include "Raven/ECS/Entity.hpp"
#include "Raven/Math/Transform.hpp"

#include <array>
#include <span>
#include <string_view>
#include <vector>

namespace Raven
{
	class Scene;

	// Borrowed for one synchronous game update. Never retain across scene replacement.
	// This view permits copied component access, not entity/resource ownership changes.
	class SceneAccess
	{
	  public:
		SceneAccess() = default;
		explicit operator bool() const noexcept
		{
			return m_Scene != nullptr;
		}
		RAVEN_API ECS::Entity Find(std::string_view authoredId) const;
		RAVEN_API bool IsAlive(ECS::Entity entity) const noexcept;
		RAVEN_API Mat4 GetWorldTransform(ECS::Entity entity) const;

		// Copies whole values of entities owned by this scene.
		template<ECS::ComponentType T>
		bool Read(ECS::Entity entity, T& value) const noexcept
		{
			return ReadComponent(entity, ECS::ComponentTraits<T>::Info, &value);
		}
		template<ECS::ComponentType T>
		bool Write(ECS::Entity entity, const T& value) const noexcept
		{
			return WriteComponent(entity, ECS::ComponentTraits<T>::Info, &value);
		}
		template<ECS::ComponentType... T>
			requires(sizeof...(T) > 0)
		std::vector<ECS::Entity> Query() const
		{
			const std::array<const ECS::ComponentInfo*, sizeof...(T)> components{ &ECS::ComponentTraits<T>::Info... };
			return Query(components);
		}

		RAVEN_API bool ReadComponent(ECS::Entity entity, const ECS::ComponentInfo& info, void* value) const noexcept;
		RAVEN_API bool WriteComponent(ECS::Entity entity, const ECS::ComponentInfo& info, const void* value) const noexcept;
		RAVEN_API std::vector<ECS::Entity> Query(std::span<const ECS::ComponentInfo* const> components) const;

	  private:
		friend class Scene;
		explicit SceneAccess(Scene& scene) noexcept : m_Scene(&scene)
		{
		}
		Scene* m_Scene = nullptr;
	};
}
