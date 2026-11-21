#include "Raven/ECS/Registry.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
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
                throw std::overflow_error("Registry identity exhausted");

            return ++lastId;
        }

		// Owns contiguous component values aligned for their type. Components are trivially
		// copyable, so values relocate with memcpy when the buffer grows.
		class ValueBuffer
		{
		public:
			ValueBuffer() = default;
			ValueBuffer(const ValueBuffer&) = delete;
			ValueBuffer& operator=(const ValueBuffer&) = delete;
			~ValueBuffer() { Release(); }

			void Configure(std::size_t stride, std::size_t alignment) noexcept
			{
				m_Stride = stride;
				m_Alignment = std::max(alignment, alignof(std::max_align_t));
			}

			std::byte* At(std::size_t index) noexcept { return m_Data + index * m_Stride; }
			const std::byte* At(std::size_t index) const noexcept { return m_Data + index * m_Stride; }

			void Reserve(std::size_t count)
			{
				if (count <= m_Capacity)
					return;
				const std::size_t capacity = std::max(count, std::max<std::size_t>(16, m_Capacity * 2));
				auto* data = static_cast<std::byte*>(::operator new(capacity * m_Stride, std::align_val_t{ m_Alignment }));
				if (m_Data)
					std::memcpy(data, m_Data, m_Capacity * m_Stride);
				Release();
				m_Data = data;
				m_Capacity = capacity;
			}

		private:
			void Release() noexcept
			{
				if (m_Data)
					::operator delete(m_Data, std::align_val_t{ m_Alignment });
				m_Data = nullptr;
				m_Capacity = 0;
			}

			std::byte* m_Data = nullptr;
			std::size_t m_Capacity = 0;
			std::size_t m_Stride = 0;
			std::size_t m_Alignment = alignof(std::max_align_t);
		};
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

		// One sparse set per component type. A paged sparse array maps an entity index to a
		// dense position; dense arrays hold the owning entity indices and the values in the
		// same order. Removal moves the last element into the gap, so dense order is unstable.
		// The first registration fixes the name and layout; the name is copied, so pools
		// never refer to a module's static storage.
		struct ComponentPool
		{
			static constexpr std::uint32_t PageShift = 12;
			static constexpr std::uint32_t PageSize = 1u << PageShift;
			static constexpr std::uint32_t NoIndex = std::numeric_limits<std::uint32_t>::max();

			std::string Name;
			std::size_t Size = 0;
			std::uint64_t LayoutHash = 0;
			std::vector<std::unique_ptr<std::uint32_t[]>> Pages;
			std::vector<std::uint32_t> Entities;
			ValueBuffer Values;

			std::uint32_t Find(std::uint32_t entity) const noexcept
			{
				const std::size_t page = entity >> PageShift;
				return page < Pages.size() && Pages[page] ? Pages[page][entity & (PageSize - 1)] : NoIndex;
			}

			const std::byte* Get(std::uint32_t entity) const noexcept
			{
				const auto dense = Find(entity);
				return dense == NoIndex ? nullptr : Values.At(dense);
			}

			std::byte* Get(std::uint32_t entity) noexcept
			{
				return const_cast<std::byte*>(std::as_const(*this).Get(entity));
			}

			void Insert(std::uint32_t entity, const void* value)
			{
				const std::size_t page = entity >> PageShift;
				if (page >= Pages.size())
					Pages.resize(page + 1);
				if (!Pages[page])
				{
					Pages[page] = std::make_unique<std::uint32_t[]>(PageSize);
					std::fill_n(Pages[page].get(), PageSize, NoIndex);
				}
				const auto dense = static_cast<std::uint32_t>(Entities.size());
				Values.Reserve(Entities.size() + 1);
				Entities.push_back(entity);
				std::memcpy(Values.At(dense), value, Size);
				Pages[page][entity & (PageSize - 1)] = dense;
			}

			bool Erase(std::uint32_t entity) noexcept
			{
				const auto dense = Find(entity);
				if (dense == NoIndex)
					return false;
				const auto last = static_cast<std::uint32_t>(Entities.size() - 1);
				if (dense != last)
				{
					const auto moved = Entities[last];
					std::memcpy(Values.At(dense), Values.At(last), Size);
					Entities[dense] = moved;
					Pages[moved >> PageShift][moved & (PageSize - 1)] = dense;
				}
				Entities.pop_back();
				Pages[entity >> PageShift][entity & (PageSize - 1)] = NoIndex;
				return true;
			}
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

		// Mutable lookups reuse the const ones; this object itself is never const.
		ComponentPool* FindPool(const ComponentInfo& info) noexcept
		{
			return const_cast<ComponentPool*>(std::as_const(*this).FindPool(info));
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
			pool.Erase(entity.m_Index);

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
		if (!IsAlive(entity))
			return false;
		const auto* pool = std::as_const(*m_Impl).FindPool(info);
		return pool && pool->Find(entity.m_Index) != Impl::ComponentPool::NoIndex;
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
			pool.Values.Configure(info.Size, info.Alignment);
		}
		else if (pool.Name != info.Name || pool.Size != info.Size || pool.LayoutHash != info.LayoutHash)
		{
			return false;
		}
		if (pool.Find(entity.m_Index) != Impl::ComponentPool::NoIndex)
			return false;
		pool.Insert(entity.m_Index, value);
		return true;
	}

	bool Registry::ReadComponent(Entity entity, const ComponentInfo& info, void* value) const noexcept
	{
		const auto* pool = IsAlive(entity) && value ? std::as_const(*m_Impl).FindPool(info) : nullptr;
		const auto* bytes = pool ? pool->Get(entity.m_Index) : nullptr;
		if (!bytes)
			return false;
		std::memcpy(value, bytes, info.Size);
		return true;
	}

	bool Registry::WriteComponent(Entity entity, const ComponentInfo& info, const void* value) noexcept
	{
		auto* pool = IsAlive(entity) && value ? m_Impl->FindPool(info) : nullptr;
		auto* bytes = pool ? pool->Get(entity.m_Index) : nullptr;
		if (!bytes)
			return false;
		std::memcpy(bytes, value, info.Size);
		return true;
	}

	bool Registry::RemoveComponent(Entity entity, const ComponentInfo& info) noexcept
	{
		if (!IsAlive(entity))
			return false;
		auto* pool = m_Impl->FindPool(info);
		return pool && pool->Erase(entity.m_Index);
	}
}
