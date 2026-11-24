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
		// The live entity a persistent reference names, or null (docs/DataModelDesign.md, FD-09).
		RAVEN_API ECS::Entity Resolve(ECS::EntityRef id) const noexcept;
		RAVEN_API ECS::EntityRef GetRef(ECS::Entity entity) const noexcept;
		RAVEN_API bool IsAlive(ECS::Entity entity) const noexcept;
		RAVEN_API Mat4 GetWorldTransform(ECS::Entity entity) const;

		// Copies whole values of entities owned by this scene.
		template<ECS::ComponentType T>
		bool Read(ECS::Entity entity, T& value) const noexcept
		{
			return ReadComponent(entity, ECS::ComponentTraits<T>::Info, &value);
		}
		// Host-owned components (WorldTransform, Parent) are read-only here (FD-07).
		template<ECS::ComponentType T>
		bool Write(ECS::Entity entity, const T& value) const noexcept
		{
			static_assert(!ECS::HostOwned<T>, "This component is written only by the host");
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
		// Views visit the scene's entities in unspecified order; Query keeps Scene::GetEntities order.
		template<ECS::ViewComponent... T>
			requires(sizeof...(T) > 0)
		ECS::View<T...> View() const noexcept
		{
			static_assert(((std::is_const_v<T> || !ECS::HostOwned<std::remove_const_t<T>>) && ...),
				"View host-owned components as const");
			return ECS::View<T...>(GetViewSource());
		}
		// TryGet<const T> reads without marking the component changed.
		template<ECS::ViewComponent T>
		T* TryGet(ECS::Entity entity) const noexcept
		{
			const auto& info = ECS::ComponentTraits<std::remove_const_t<T>>::Info;
			if constexpr (std::is_const_v<T>)
				return static_cast<T*>(FindComponent(entity, info));
			else
			{
				static_assert(!ECS::HostOwned<T>, "Use TryGet<const T> for host-owned components");
				return static_cast<T*>(TryGetComponent(entity, info));
			}
		}
		// The first scene-owned component of type T, or null; use for single-instance state.
		template<ECS::ComponentType T>
		T* Single() const
		{
			static_assert(!ECS::HostOwned<T>, "This component is written only by the host");
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

		// The mutable form marks the component changed; FindComponent does not. The mutable
		// and write forms refuse host-owned components.
		RAVEN_API void* TryGetComponent(ECS::Entity entity, const ECS::ComponentInfo& info) const noexcept;
		RAVEN_API const void* FindComponent(ECS::Entity entity, const ECS::ComponentInfo& info) const noexcept;
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
