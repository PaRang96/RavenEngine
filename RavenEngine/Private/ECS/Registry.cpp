#include "Raven/ECS/Registry.hpp"

#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>
#include <map>
#include <string>
#include <unordered_map>

namespace Raven::ECS
{
    namespace
    {
        std::uint64_t AllocateRegistryId()
        {
            static std::mutex mutex;
            static std::uint64_t lastId = 0;

            const std::scoped_lock lock(mutex);

            if (lastId == std::numeric_limits<std::uint64_t>::max())
                throw std::overflow_error("Registry idnetity exhausted");

            return ++lastId;
        }
    }

    struct Registry::Impl
    {
        static constexpr std::uint32_t NoSlot =
            std::numeric_limits<std::uint32_t>::max();

        struct Slot
        {
            std::uint32_t Generation = 1;
            std::uint32_t NextFree = NoSlot;
            bool Alive = false;
        };

        const std::uint64_t RegistryId = AllocateRegistryId();
        std::vector<Slot> Slots;
        std::uint32_t FirstFree = NoSlot;

		struct ComponentPool
		{
			std::size_t Size = 0;
			std::unordered_map<std::uint32_t, std::vector<std::byte>> Values;
		};
		std::map<std::string, ComponentPool, std::less<>> Components;
    };

    Registry::Registry()
        : m_Impl(std::make_unique<Impl>())
    {
    }

    Registry::~Registry() = default;

    Entity Registry::Create()
    {
        auto &impl = *m_Impl;

        if (impl.FirstFree != Impl::NoSlot)
        {
            const std::uint32_t index = impl.FirstFree;
            auto &slot = impl.Slots[index];

            impl.FirstFree = slot.NextFree;
            slot.NextFree = Impl::NoSlot;
            slot.Alive = true;

            return Entity(impl.RegistryId, index, slot.Generation);
        }

        if (impl.Slots.size() >= Impl::NoSlot)
            throw std::overflow_error("Entity slot capacity exhausted");

        const auto index =
            static_cast<std::uint32_t>(impl.Slots.size());

        impl.Slots.push_back({1, Impl::NoSlot, true});

        return Entity(impl.RegistryId, index, 1);
    }

    bool Registry::IsAlive(Entity entity) const noexcept
    {
        const auto &impl = *m_Impl;

        if (entity.m_Registry != impl.RegistryId ||
            entity.m_Index >= impl.Slots.size())
            return false;

        const auto &slot = impl.Slots[entity.m_Index];

        return slot.Alive && slot.Generation == entity.m_Generation;
    }

    bool Registry::Destroy(Entity entity) noexcept
    {
        if (!IsAlive(entity))
            return false;

        auto &impl = *m_Impl;
        auto &slot = impl.Slots[entity.m_Index];

		for (auto& [name, pool] : impl.Components)
			pool.Values.erase(entity.m_Index);

        slot.Alive = false;

        if (slot.Generation ==
            std::numeric_limits<std::uint32_t>::max())
            return true;
        ++slot.Generation;
        slot.NextFree = impl.FirstFree;
        impl.FirstFree = entity.m_Index;
        return true;
    }

	std::vector<Entity> Registry::GetEntities() const
	{
		std::vector<Entity> entities;
		for (std::size_t index = 0; index < m_Impl->Slots.size(); ++index)
		{
			const auto& slot = m_Impl->Slots[index];
			if (slot.Alive)
				entities.push_back(Entity(m_Impl->RegistryId, static_cast<std::uint32_t>(index), slot.Generation));
		}
		return entities;
	}

	bool Registry::Add(Entity entity, const Component& component)
	{
		if (!IsAlive(entity))
			return false;
		const auto name = component.GetTypeName();
		const auto size = component.GetDataSize();
		if (name.empty())
			return false;
		const auto existing = m_Impl->Components.find(name);
		if (existing != m_Impl->Components.end() &&
			(existing->second.Size != size || existing->second.Values.contains(entity.m_Index)))
			return false;
		std::vector<std::byte> bytes(size);
		if (!component.CopyDataTo(bytes))
			return false;
		auto entry = m_Impl->Components.try_emplace(std::string(name)).first;
		auto& pool = entry->second;
		pool.Size = size;
		return pool.Values.try_emplace(entity.m_Index, std::move(bytes)).second;
	}

	bool Registry::Read(Entity entity, Component& component) const noexcept
	{
		if (!IsAlive(entity))
			return false;
		const auto pool = m_Impl->Components.find(component.GetTypeName());
		if (pool == m_Impl->Components.end() || pool->second.Size != component.GetDataSize())
			return false;
		const auto value = pool->second.Values.find(entity.m_Index);
		if (value == pool->second.Values.end())
			return false;
		return component.CopyDataFrom(value->second);
	}

	bool Registry::Write(Entity entity, const Component& component) noexcept
	{
		if (!IsAlive(entity))
			return false;
		const auto pool = m_Impl->Components.find(component.GetTypeName());
		if (pool == m_Impl->Components.end() || pool->second.Size != component.GetDataSize())
			return false;
		const auto value = pool->second.Values.find(entity.m_Index);
		if (value == pool->second.Values.end())
			return false;
		return component.CopyDataTo(value->second);
	}

	bool Registry::Remove(Entity entity, const Component& component) noexcept
	{
		return RemoveComponent(entity, component.GetTypeName(), component.GetDataSize());
	}

	bool Registry::RemoveComponent(Entity entity, std::string_view name, std::size_t size) noexcept
	{
		if (!IsAlive(entity))
			return false;
		const auto pool = m_Impl->Components.find(name);
		return pool != m_Impl->Components.end() && pool->second.Size == size &&
			pool->second.Values.erase(entity.m_Index) != 0;
	}
}
