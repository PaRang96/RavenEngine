#pragma once

#include "Raven/Core/Api.hpp"
#include "Raven/ECS/Component.hpp"
#include "Raven/ECS/Entity.hpp"
#include "Raven/ECS/Registry.hpp"
#include "Raven/Math/Transform.hpp"

#include <array>
#include <span>
#include <string_view>
#include <vector>

namespace Raven
{
	class Scene;

	// Borrowed for one synchronous game update. Never retain across scene replacement.
	// Permits copied or borrowed component access to entities this scene owns, not
	// entity/resource ownership changes.
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

		// Borrowed access to entities owned by this scene (docs/ECSDesign.md, D4). Views,
		// references and pointers are valid only during the current synchronous update.
		// Views visit the scene's entities in unspecified order; Query keeps authored order.
		template<ECS::ViewComponent... T>
			requires(sizeof...(T) > 0)
		ECS::View<T...> View() const noexcept
		{
			return ECS::View<T...>(GetViewSource());
		}
		template<ECS::ComponentType T>
		T* TryGet(ECS::Entity entity) const noexcept
		{
			return static_cast<T*>(TryGetComponent(entity, ECS::ComponentTraits<T>::Info));
		}
		// The first scene-owned component of type T, or null; use for single-instance state.
		template<ECS::ComponentType T>
		T* Single() const
		{
			T* result = nullptr;
			View<T>().Each([&result](ECS::Entity, T& value)
			{
				result = &value;
				return false;
			});
			return result;
		}
		template<ECS::ComponentType T>
		std::size_t Count() const
		{
			std::size_t count = 0;
			View<const T>().Each([&count](ECS::Entity, const T&) { ++count; });
			return count;
		}

		RAVEN_API void* TryGetComponent(ECS::Entity entity, const ECS::ComponentInfo& info) const noexcept;
		RAVEN_API bool ReadComponent(ECS::Entity entity, const ECS::ComponentInfo& info, void* value) const noexcept;
		RAVEN_API bool WriteComponent(ECS::Entity entity, const ECS::ComponentInfo& info, const void* value) const noexcept;
		RAVEN_API std::vector<ECS::Entity> Query(std::span<const ECS::ComponentInfo* const> components) const;

	  private:
		friend class Scene;
		RAVEN_API ECS::Detail::ViewSource GetViewSource() const noexcept;
		explicit SceneAccess(Scene& scene) noexcept : m_Scene(&scene)
		{
		}
		Scene* m_Scene = nullptr;
	};
}
