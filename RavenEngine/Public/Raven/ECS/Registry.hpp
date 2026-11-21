#pragma once

#include <cstddef>

#include "Raven/Core/Api.hpp"
#include "Raven/ECS/Component.hpp"
#include "Raven/ECS/Entity.hpp"

#include <memory>
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

		// Typed access copies whole component values; no reference to storage escapes.
		template<ComponentType T>
		bool Has(Entity entity) const noexcept
		{
			return HasComponent(entity, ComponentTraits<T>::Info);
		}
		template<ComponentType T>
		bool Has(Entity entity, const T&) const noexcept
		{
			return Has<T>(entity);
		}
		template<ComponentType T>
		bool Add(Entity entity, const T& value)
		{
			return AddComponent(entity, ComponentTraits<T>::Info, &value);
		}
		template<ComponentType T>
		bool Read(Entity entity, T& value) const noexcept
		{
			return ReadComponent(entity, ComponentTraits<T>::Info, &value);
		}
		template<ComponentType T>
		bool Write(Entity entity, const T& value) noexcept
		{
			return WriteComponent(entity, ComponentTraits<T>::Info, &value);
		}
		template<ComponentType T>
		bool Remove(Entity entity) noexcept
		{
			return RemoveComponent(entity, ComponentTraits<T>::Info);
		}
		template<ComponentType T>
		bool Remove(Entity entity, const T&) noexcept
		{
			return Remove<T>(entity);
		}

		// Type-erased access for generic callers and tools. Values address Info.Size bytes
		// laid out as the described component. A mismatched layout never matches a pool.
		RAVEN_API bool HasComponent(Entity entity, const ComponentInfo& info) const noexcept;
		RAVEN_API bool AddComponent(Entity entity, const ComponentInfo& info, const void* value);
		RAVEN_API bool ReadComponent(Entity entity, const ComponentInfo& info, void* value) const noexcept;
		RAVEN_API bool WriteComponent(Entity entity, const ComponentInfo& info, const void* value) noexcept;
		RAVEN_API bool RemoveComponent(Entity entity, const ComponentInfo& info) noexcept;

        Registry(const Registry &) = delete;
        Registry &operator=(const Registry &) = delete;

        Registry(Registry &&) = delete;
        Registry &operator=(Registry &&) = delete;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
