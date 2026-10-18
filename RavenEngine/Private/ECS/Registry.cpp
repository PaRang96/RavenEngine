#include "Raven/ECS/Registry.hpp"

#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
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

        if (slot.Generation ==
            std::numeric_limits<std::uint32_t>::max())
            return true;
        ++slot.Generation;
        slot.NextFree = impl.FirstFree;
        impl.FirstFree = entity.m_Index;
        return true;
    }
}
