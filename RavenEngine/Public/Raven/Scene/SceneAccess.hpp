#pragma once

#include "Raven/Core/Api.hpp"
#include "Raven/ECS/Component.hpp"
#include "Raven/ECS/Entity.hpp"
#include "Raven/Math/Transform.hpp"

#include <array>
#include <span>
#include <string_view>
#include <tuple>
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
		RAVEN_API bool Read(ECS::Entity entity, ECS::Component& value) const noexcept;
		RAVEN_API bool Write(ECS::Entity entity, const ECS::Component& value) const noexcept;
		RAVEN_API std::vector<ECS::Entity> Query(std::span<const ECS::Component* const> components) const;
		RAVEN_API Mat4 GetWorldTransform(ECS::Entity entity) const;

		template<typename T> requires (!ECS::ComponentType<T>)
		bool Read(ECS::Entity, T&) const = delete;
		template<typename T> requires (!ECS::ComponentType<T>)
		bool Write(ECS::Entity, const T&) const = delete;

		template <ECS::ComponentType... T>
			requires(sizeof...(T) > 0)
		std::vector<ECS::Entity> Query() const
		{
			const std::tuple<T...> values;
			return std::apply(
				[this](const auto&... value)
				{
					const std::array<const ECS::Component*, sizeof...(T)> components{&value...};
					return Query(components);
				},
				values);
		}

	  private:
		friend class Scene;
		explicit SceneAccess(Scene& scene) noexcept : m_Scene(&scene)
		{
		}
		Scene* m_Scene = nullptr;
	};
}
