#include "Raven/ECS/Registry.hpp"

#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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

		// The first registration fixes a type's name and layout. The name is copied, so
		// pools never refer to a module's static storage.
		struct ComponentPool
		{
			std::string Name;
			std::size_t Size = 0;
			std::uint64_t LayoutHash = 0;
			std::unordered_map<std::uint32_t, std::vector<std::byte>> Values;
		};
		std::unordered_map<std::uint64_t, ComponentPool> Components;

		// LayoutHash includes the type name, so an ID collision between different names
		// also fails here; AddComponent checks the name itself before creating a pool.
		const ComponentPool* FindPool(const ComponentInfo& info) const noexcept
		{
			const auto found = Components.find(info.Id);
			if (found == Components.end())
				return nullptr;
			const auto& pool = found->second;
			return pool.Size == info.Size && pool.LayoutHash == info.LayoutHash ? &pool : nullptr;
		}

		const std::vector<std::byte>* FindValue(std::uint32_t index, const ComponentInfo& info) const noexcept
		{
			const auto* pool = FindPool(info);
			if (!pool)
				return nullptr;
			const auto value = pool->Values.find(index);
			return value == pool->Values.end() ? nullptr : &value->second;
		}

		// Mutable lookups reuse the const ones; this object itself is never const.
		ComponentPool* FindPool(const ComponentInfo& info) noexcept
		{
			return const_cast<ComponentPool*>(std::as_const(*this).FindPool(info));
		}

		std::vector<std::byte>* FindValue(std::uint32_t index, const ComponentInfo& info) noexcept
		{
			return const_cast<std::vector<std::byte>*>(std::as_const(*this).FindValue(index, info));
		}
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

		for (auto& [id, pool] : impl.Components)
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

	bool Registry::HasComponent(Entity entity, const ComponentInfo& info) const noexcept
	{
		return IsAlive(entity) && std::as_const(*m_Impl).FindValue(entity.m_Index, info);
	}

	bool Registry::AddComponent(Entity entity, const ComponentInfo& info, const void* value)
	{
		if (!IsAlive(entity) || info.Name.empty() || info.Size == 0 || !value)
			return false;
		auto [entry, created] = m_Impl->Components.try_emplace(info.Id);
		auto& pool = entry->second;
		if (created)
		{
			pool.Name = info.Name;
			pool.Size = info.Size;
			pool.LayoutHash = info.LayoutHash;
		}
		else if (pool.Name != info.Name || pool.Size != info.Size || pool.LayoutHash != info.LayoutHash)
		{
			return false;
		}
		if (pool.Values.contains(entity.m_Index))
			return false;
		std::vector<std::byte> bytes(info.Size);
		std::memcpy(bytes.data(), value, info.Size);
		return pool.Values.try_emplace(entity.m_Index, std::move(bytes)).second;
	}

	bool Registry::ReadComponent(Entity entity, const ComponentInfo& info, void* value) const noexcept
	{
		const auto* bytes = IsAlive(entity) && value ? std::as_const(*m_Impl).FindValue(entity.m_Index, info) : nullptr;
		if (!bytes)
			return false;
		std::memcpy(value, bytes->data(), info.Size);
		return true;
	}

	bool Registry::WriteComponent(Entity entity, const ComponentInfo& info, const void* value) noexcept
	{
		auto* bytes = IsAlive(entity) && value ? m_Impl->FindValue(entity.m_Index, info) : nullptr;
		if (!bytes)
			return false;
		std::memcpy(bytes->data(), value, info.Size);
		return true;
	}

	bool Registry::RemoveComponent(Entity entity, const ComponentInfo& info) noexcept
	{
		if (!IsAlive(entity))
			return false;
		auto* pool = m_Impl->FindPool(info);
		return pool && pool->Values.erase(entity.m_Index) != 0;
	}
}
