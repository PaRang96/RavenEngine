#pragma once

#include <cstddef>

#include "Raven/Core/Api.hpp"
#include "Raven/ECS/Component.hpp"
#include "Raven/ECS/Entity.hpp"

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace Raven::ECS
{
    class Registry
    {
    public:
        RAVEN_API Registry();
        RAVEN_API ~Registry();

        RAVEN_API Entity Create();

        RAVEN_API bool Destroy(Entity entity) noexcept;

        RAVEN_API bool IsAlive(Entity entity) const noexcept;

		// A snapshot permits structural changes during updates; stale handles still fail.
		RAVEN_API std::vector<Entity> GetEntities() const;

		RAVEN_API bool Has(Entity entity, const Component& value) const noexcept;

		// Base references support generic callers; no component object is retained.
		RAVEN_API bool Add(Entity entity, const Component& value);
		RAVEN_API bool Read(Entity entity, Component& value) const noexcept;
		RAVEN_API bool Write(Entity entity, const Component& value) noexcept;
		RAVEN_API bool Remove(Entity entity, const Component& value) noexcept;

		// Reject typed callers that inherit a parent's declaration without declaring
		// their own component. Component& itself selects the non-template overload.
		template<typename T> requires (!ComponentType<T>)
		bool Has(Entity, const T&) const = delete;
		template<typename T> requires (!ComponentType<T>)
		bool Add(Entity, const T&) = delete;
		template<typename T> requires (!ComponentType<T>)
		bool Read(Entity, T&) const = delete;
		template<typename T> requires (!ComponentType<T>)
		bool Write(Entity, const T&) = delete;
		template<typename T> requires (!ComponentType<T>)
		bool Remove(Entity, const T&) = delete;

		template<ComponentType T>
		bool Remove(Entity entity) noexcept
		{
			return RemoveComponent(entity, T::TypeName,
				Detail::ComponentFields<decltype(std::declval<T&>().GetFields())>::Size);
		}

        Registry(const Registry &) = delete;
        Registry &operator=(const Registry &) = delete;

        Registry(Registry &&) = delete;
        Registry &operator=(Registry &&) = delete;

    private:
		RAVEN_API bool RemoveComponent(Entity, std::string_view, std::size_t) noexcept;

        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
