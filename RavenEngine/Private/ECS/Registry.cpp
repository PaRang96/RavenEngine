#include "Raven/ECS/Registry.hpp"

#include "Raven/Core/Log.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
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

		// A view's references stay valid only while pools keep their shape.
		bool RejectWhileIterating(std::uint32_t active, const char* operation) noexcept
		{
			if (active == 0)
				return false;
			try
			{
				Log(LogLevel::Error, std::string("Registry ") + operation +
					" rejected while a view iterates; defer structural changes until it returns");
			}
			catch (...)
			{
			}
			return true;
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

			std::byte* Data() const noexcept { return m_Data; }

			void Swap(ValueBuffer& other) noexcept
			{
				std::swap(m_Data, other.m_Data);
				std::swap(m_Capacity, other.m_Capacity);
				std::swap(m_Stride, other.m_Stride);
				std::swap(m_Alignment, other.m_Alignment);
			}
			std::byte* At(std::size_t index) const noexcept { return m_Data + index * m_Stride; }

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
		// Owning scene ID per slot, parallel to Slots; 0 when no scene owns the entity.
		std::vector<std::uint32_t> Owners;
        std::uint32_t FirstFree = NoSlot;
		std::uint32_t LastSceneId = 0;
		// Stamped on elements by mutable access; see AdvanceChangeTick.
		std::uint32_t ChangeTick = 1;
		// Number of views currently iterating; structural changes wait until it is zero.
		std::uint32_t Iterating = 0;

		// One journal per subscribed component type (docs/GpuDrivenDesign.md, G1). Base is the
		// sequence number of Entries[0]; subscriber cursors are sequence numbers.
		struct LifecycleJournal
		{
			std::string Name;
			std::size_t Size = 0;
			std::uint64_t LayoutHash = 0;
			std::vector<LifecycleEntry> Entries;
			std::uint64_t Base = 0;
			std::vector<std::uint32_t> Subscribers;

			std::uint64_t End() const noexcept { return Base + Entries.size(); }
		};
		struct LifecycleSubscriber
		{
			std::uint64_t ComponentId = 0;
			std::uint64_t Cursor = 0;
			std::size_t MaxBacklog = 0;
			bool Resync = true;
		};
		// Node-based maps keep journal addresses stable for the pools that point at them.
		std::unordered_map<std::uint64_t, LifecycleJournal> Journals;
		std::unordered_map<std::uint32_t, LifecycleSubscriber> Subscribers;
		std::uint32_t LastSubscription = 0;

		// One sparse set per component type. A paged sparse array maps an entity index to a
		// dense position; dense arrays hold the owning entities and the values in the same
		// order. Removal moves the last element into the gap, so dense order is unstable.
		// The first registration fixes the name and layout; the name is copied, so pools
		// never refer to a module's static storage.
		struct ComponentPool
		{
			using Lookup = Detail::PoolView;

			std::string Name;
			std::size_t Size = 0;
			std::uint64_t LayoutHash = 0;
			std::vector<std::uint32_t*> Pages;
			std::vector<Entity> Entities;
			std::vector<std::uint32_t> Ticks;
			ValueBuffer Values;
			// Set while the type has lifecycle subscribers.
			LifecycleJournal* Journal = nullptr;

			ComponentPool() = default;
			ComponentPool(const ComponentPool&) = delete;
			ComponentPool& operator=(const ComponentPool&) = delete;
			~ComponentPool()
			{
				for (auto* page : Pages)
					delete[] page;
			}

			Detail::PoolView GetView() noexcept
			{
				return { Entities.data(), Values.Data(), Ticks.data(), Entities.size(), Size, Pages.data(), Pages.size() };
			}

			std::uint32_t Find(std::uint32_t entity) const noexcept
			{
				const std::size_t page = entity >> Lookup::PageShift;
				return page < Pages.size() && Pages[page] ? Pages[page][entity & Lookup::PageMask] : Lookup::NoIndex;
			}

			std::byte* Get(std::uint32_t entity) const noexcept
			{
				const auto dense = Find(entity);
				return dense == Lookup::NoIndex ? nullptr : Values.At(dense);
			}

			void Insert(Entity entity, const void* value, std::uint32_t tick)
			{
				const auto index = Detail::EntityAccess::Index(entity);
				const std::size_t page = index >> Lookup::PageShift;
				if (page >= Pages.size())
					Pages.resize(page + 1, nullptr);
				if (!Pages[page])
				{
					auto lookup = std::make_unique<std::uint32_t[]>(Lookup::PageMask + 1);
					std::fill_n(lookup.get(), Lookup::PageMask + 1, Lookup::NoIndex);
					Pages[page] = lookup.release();
				}
				const auto dense = static_cast<std::uint32_t>(Entities.size());
				// Reserve everything first so the pushes below cannot fail halfway.
				const auto grown = std::max<std::size_t>(16, Entities.size() * 2);
				if (Entities.size() == Entities.capacity())
					Entities.reserve(grown);
				if (Ticks.size() == Ticks.capacity())
					Ticks.reserve(grown);
				Values.Reserve(Entities.size() + 1);
				Entities.push_back(entity);
				Ticks.push_back(tick);
				std::memcpy(Values.At(dense), value, Size);
				Pages[page][index & Lookup::PageMask] = dense;
			}

			bool Erase(std::uint32_t entity) noexcept
			{
				const auto dense = Find(entity);
				if (dense == Lookup::NoIndex)
					return false;
				const auto last = static_cast<std::uint32_t>(Entities.size() - 1);
				if (dense != last)
				{
					const auto moved = Detail::EntityAccess::Index(Entities[last]);
					std::memcpy(Values.At(dense), Values.At(last), Size);
					Entities[dense] = Entities[last];
					Ticks[dense] = Ticks[last];
					Pages[moved >> Lookup::PageShift][moved & Lookup::PageMask] = dense;
				}
				Entities.pop_back();
				Ticks.pop_back();
				Pages[entity >> Lookup::PageShift][entity & Lookup::PageMask] = Lookup::NoIndex;
				return true;
			}
		};
		std::unordered_map<std::uint64_t, ComponentPool> Components;
		// Field tables by component ID, copied out of the declaring module (FD-08).
		std::unordered_map<std::uint64_t, ComponentSchema> Schemas;

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

		// Drops entries every subscriber has read.
		void Trim(LifecycleJournal& journal) noexcept
		{
			auto oldest = journal.End();
			for (const auto id : journal.Subscribers)
				oldest = std::min(oldest, Subscribers.at(id).Cursor);
			const auto read = static_cast<std::size_t>(oldest - journal.Base);
			if (read == 0)
				return;
			journal.Entries.erase(journal.Entries.begin(), journal.Entries.begin() + static_cast<std::ptrdiff_t>(read));
			journal.Base = oldest;
		}

		// Structural changes are noexcept, so a failed append or an overflow turns into a resync.
		void Record(LifecycleJournal& journal, Entity entity, LifecycleEvent event) noexcept
		{
			bool overflowed = false;
			try
			{
				journal.Entries.push_back({ entity, event });
			}
			catch (...)
			{
				for (const auto id : journal.Subscribers)
					Subscribers.at(id).MaxBacklog = 0;
			}
			for (const auto id : journal.Subscribers)
			{
				auto& subscriber = Subscribers.at(id);
				if (journal.End() - subscriber.Cursor > subscriber.MaxBacklog)
				{
					subscriber.Cursor = journal.End();
					subscriber.Resync = true;
					overflowed = true;
				}
			}
			if (overflowed)
				Trim(journal);
		}

		void Record(ComponentPool& pool, Entity entity, LifecycleEvent event) noexcept
		{
			if (pool.Journal)
				Record(*pool.Journal, entity, event);
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
		if (impl.Iterating != 0)
			throw std::logic_error("Registry Create rejected while a view iterates");

        if (impl.FirstFree != Impl::NoSlot)
        {
            const std::uint32_t index = impl.FirstFree;
            auto &slot = impl.Slots[index];

            impl.FirstFree = slot.NextFree;
            slot.NextFree = Impl::NoSlot;
            slot.Alive = true;
			impl.Owners[index] = 0;

            return Entity(impl.RegistryId, index, slot.Generation);
        }

        if (impl.Slots.size() >= Impl::NoSlot)
            throw std::overflow_error("Entity slot capacity exhausted");

        const auto index =
            static_cast<std::uint32_t>(impl.Slots.size());

		impl.Owners.push_back(0);
		try
		{
			impl.Slots.push_back({1, Impl::NoSlot, true});
		}
		catch (...)
		{
			impl.Owners.pop_back();
			throw;
		}

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
		if (RejectWhileIterating(impl.Iterating, "Destroy"))
			return false;
        auto &slot = impl.Slots[entity.m_Index];

		for (auto& [id, pool] : impl.Components)
			if (pool.Erase(entity.m_Index))
				impl.Record(pool, entity, LifecycleEvent::Removed);

        slot.Alive = false;
		impl.Owners[entity.m_Index] = 0;

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

	bool Registry::IsIterating() const noexcept
	{
		return m_Impl->Iterating != 0;
	}

	ComponentSchema MakeComponentSchema(const ComponentInfo& info, std::span<const std::byte> defaults)
	{
		if (info.Name.empty() || info.Size == 0 || info.Alignment == 0 || defaults.size() != info.Size ||
			(info.FieldCount != 0 && !info.Fields))
			throw std::logic_error("Component registration needs a name, a size, defaults and its field table");
		const std::span<const FieldInfo> fields(info.Fields, info.FieldCount);
		std::size_t end = 0;
		for (std::size_t index = 0; index < fields.size(); ++index)
		{
			const auto& field = fields[index];
			const std::size_t count = std::size_t{ field.Extent[0] } * field.Extent[1];
			if (field.Name.empty() || field.Size == 0 || count == 0 || field.Size % count != 0 ||
				field.Offset < end || field.Offset + std::size_t{ field.Size } > info.Size)
				throw std::logic_error("Component field table does not describe the type: " + std::string(info.Name));
			for (std::size_t other = 0; other < index; ++other)
				if (fields[other].Name == field.Name)
					throw std::logic_error("Component field table repeats a name: " + std::string(info.Name));
			end = field.Offset + std::size_t{ field.Size };
		}
		ComponentSchema schema{ info.Id, std::string(info.Name), info.Size, info.Alignment, info.LayoutHash, info.Flags,
			{}, std::vector<std::byte>(defaults.begin(), defaults.end()) };
		schema.Fields.reserve(fields.size());
		for (const auto& field : fields)
			schema.Fields.push_back({ std::string(field.Name), field.Offset, field.Size, field.Kind, field.Flags,
				field.Extent, field.Tag, {} });
		for (std::size_t index = 0; index < info.AliasCount; ++index)
		{
			const auto alias = info.Aliases[index];
			const auto separator = alias.find('=');
			if (separator == std::string_view::npos)
			{
				schema.OldNames.emplace_back(alias);
				continue;
			}
			const auto old = alias.substr(0, separator);
			const auto current = alias.substr(separator + 1);
			const auto field = std::find_if(schema.Fields.begin(), schema.Fields.end(),
				[&](const FieldSchema& candidate) { return candidate.Name == current; });
			if (old.empty() || field == schema.Fields.end())
				throw std::logic_error("Component alias '" + std::string(alias) + "' names no field of " +
					std::string(info.Name));
			field->OldNames.emplace_back(old);
		}
		return schema;
	}

	bool Registry::RegisterComponent(const ComponentInfo& info, std::span<const std::byte> defaults)
	{
		return RegisterSchema(MakeComponentSchema(info, defaults));
	}

	bool Registry::RegisterSchema(const ComponentSchema& schema)
	{
		auto& schemas = m_Impl->Schemas;
		if (const auto existing = schemas.find(schema.Id); existing != schemas.end())
			return IsSameLayout(existing->second, schema);
		schemas.emplace(schema.Id, schema);
		return true;
	}

	bool IsSameLayout(const ComponentSchema& left, const ComponentSchema& right) noexcept
	{
		return left.Id == right.Id && left.Name == right.Name && left.Size == right.Size &&
			left.Alignment == right.Alignment && left.LayoutHash == right.LayoutHash;
	}

	const FieldSchema* ComponentSchema::FindField(std::string_view name) const noexcept
	{
		for (const auto& field : Fields)
			if (field.Name == name)
				return &field;
		for (const auto& field : Fields)
			if (std::find(field.OldNames.begin(), field.OldNames.end(), name) != field.OldNames.end())
				return &field;
		return nullptr;
	}

	bool IsSameFieldType(const FieldSchema& left, const FieldSchema& right) noexcept
	{
		return left.Kind == right.Kind && left.Flags == right.Flags && left.Extent == right.Extent &&
			left.Tag == right.Tag && left.Size == right.Size;
	}

	namespace
	{
		// The registered schema a new one replaces: the same ID, or a registered type the new one
		// names as an old name. Null when the new schema is new.
		const ComponentSchema* FindPredecessor(const std::unordered_map<std::uint64_t, ComponentSchema>& schemas,
			const ComponentSchema& next) noexcept
		{
			if (const auto found = schemas.find(next.Id); found != schemas.end())
				return &found->second;
			for (const auto& [id, schema] : schemas)
				if (std::find(next.OldNames.begin(), next.OldNames.end(), schema.Name) != next.OldNames.end())
					return &schema;
			return nullptr;
		}

		// For each new field, the old field it takes its value from.
		std::vector<const FieldSchema*> MapFields(const ComponentSchema& from, const ComponentSchema& to)
		{
			std::vector<const FieldSchema*> sources;
			for (const auto& field : to.Fields)
			{
				const FieldSchema* source = nullptr;
				for (const auto& old : from.Fields)
					if (old.Name == field.Name ||
						std::find(field.OldNames.begin(), field.OldNames.end(), old.Name) != field.OldNames.end())
						source = &old;
				sources.push_back(source);
			}
			return sources;
		}
	}

	std::string Registry::CheckMigration(std::span<const ComponentSchema> schemas) const
	{
		const auto& registered = m_Impl->Schemas;
		for (const auto& next : schemas)
		{
			const auto* current = FindPredecessor(registered, next);
			if (!current || IsSameLayout(*current, next))
				continue;
			if (current->Id == next.Id && current->Name != next.Name)
				return "Component " + next.Name + " has the ID of " + current->Name;
			// A renamed type moves its old pool to the new ID, which must not hold values already.
			if (current->Id != next.Id && m_Impl->Components.contains(next.Id))
				return "Component " + next.Name + " already has values, so " + current->Name + " cannot be renamed to it";
			const auto sources = MapFields(*current, next);
			for (std::size_t index = 0; index < sources.size(); ++index)
				if (sources[index] && !IsSameFieldType(*sources[index], next.Fields[index]))
					return "Component field " + next.Name + "::" + next.Fields[index].Name +
						" changed type; rename it to keep both, or rebuild and restart the host";
		}
		return {};
	}

	std::vector<ComponentMigration> Registry::Migrate(std::span<const ComponentSchema> schemas)
	{
		auto& impl = *m_Impl;
		if (impl.Iterating != 0)
			throw std::logic_error("Components cannot migrate while a view iterates");
		if (const auto error = CheckMigration(schemas); !error.empty())
			throw std::logic_error(error);

		// Build every converted pool first, so a failed allocation changes nothing.
		struct Plan
		{
			const ComponentSchema* Next;
			std::uint64_t From;
			ValueBuffer Values;
			ComponentMigration Report;
		};
		std::deque<Plan> plans;
		for (const auto& next : schemas)
		{
			const auto* current = FindPredecessor(impl.Schemas, next);
			if (!current || IsSameLayout(*current, next))
				continue;
			auto& plan = plans.emplace_back();
			plan.Next = &next;
			plan.From = current->Id;
			plan.Report.Name = next.Name;
			const auto sources = MapFields(*current, next);
			for (std::size_t index = 0; index < sources.size(); ++index)
			{
				if (!sources[index])
					plan.Report.Added.push_back(next.Fields[index].Name);
				else if (sources[index]->Name != next.Fields[index].Name)
					plan.Report.Renamed.push_back(sources[index]->Name + " -> " + next.Fields[index].Name);
			}
			for (const auto& old : current->Fields)
				if (std::find(sources.begin(), sources.end(), &old) == sources.end())
					plan.Report.Removed.push_back(old.Name);
			const auto pool = impl.Components.find(current->Id);
			if (pool == impl.Components.end())
				continue;
			const auto count = pool->second.Entities.size();
			plan.Report.Elements = count;
			plan.Values.Configure(next.Size, next.Alignment);
			plan.Values.Reserve(count);
			for (std::size_t dense = 0; dense < count; ++dense)
			{
				auto* target = plan.Values.At(dense);
				const auto* source = pool->second.Values.At(dense);
				std::memcpy(target, next.Defaults.data(), next.Size);
				for (std::size_t index = 0; index < sources.size(); ++index)
					if (sources[index])
						std::memcpy(target + next.Fields[index].Offset, source + sources[index]->Offset,
							next.Fields[index].Size);
			}
		}
		impl.Schemas.reserve(impl.Schemas.size() + plans.size());

		// Commit: node moves and swaps only.
		std::vector<ComponentMigration> reports;
		for (auto& plan : plans)
		{
			const auto& next = *plan.Next;
			if (auto node = impl.Components.extract(plan.From))
			{
				auto& pool = node.mapped();
				pool.Values.Swap(plan.Values);
				pool.Name = next.Name;
				pool.Size = next.Size;
				pool.LayoutHash = next.LayoutHash;
				// Every converted element counts as changed for tick consumers.
				std::fill(pool.Ticks.begin(), pool.Ticks.end(), impl.ChangeTick);
				node.key() = next.Id;
				impl.Components.insert(std::move(node));
			}
			if (auto journal = impl.Journals.extract(plan.From))
			{
				journal.mapped().Name = next.Name;
				journal.mapped().Size = next.Size;
				journal.mapped().LayoutHash = next.LayoutHash;
				journal.key() = next.Id;
				impl.Journals.insert(std::move(journal));
				for (auto& [subscription, subscriber] : impl.Subscribers)
					if (subscriber.ComponentId == plan.From)
						subscriber.ComponentId = next.Id;
			}
			impl.Schemas.erase(plan.From);
			impl.Schemas.insert_or_assign(next.Id, next);
			reports.push_back(std::move(plan.Report));
		}
		return reports;
	}

	const ComponentSchema* Registry::FindSchema(std::uint64_t id) const noexcept
	{
		const auto found = m_Impl->Schemas.find(id);
		return found == m_Impl->Schemas.end() ? nullptr : &found->second;
	}

	std::vector<const ComponentSchema*> Registry::GetSchemas() const
	{
		std::vector<const ComponentSchema*> schemas;
		schemas.reserve(m_Impl->Schemas.size());
		for (const auto& [id, schema] : m_Impl->Schemas)
			schemas.push_back(&schema);
		std::sort(schemas.begin(), schemas.end(), [](const ComponentSchema* left, const ComponentSchema* right)
		{
			return left->Name < right->Name;
		});
		return schemas;
	}

	bool Registry::HasComponent(Entity entity, const ComponentInfo& info) const noexcept
	{
		if (!IsAlive(entity))
			return false;
		const auto* pool = std::as_const(*m_Impl).FindPool(info);
		return pool && pool->Find(entity.m_Index) != Detail::PoolView::NoIndex;
	}

	bool Registry::AddComponent(Entity entity, const ComponentInfo& info, const void* value)
	{
		if (!IsAlive(entity) || info.Name.empty() || info.Size == 0 || !value ||
			RejectWhileIterating(m_Impl->Iterating, "Add"))
			return false;
		auto [entry, created] = m_Impl->Components.try_emplace(info.Id);
		auto& pool = entry->second;
		if (created)
		{
			pool.Name = info.Name;
			pool.Size = info.Size;
			pool.LayoutHash = info.LayoutHash;
			pool.Values.Configure(info.Size, info.Alignment);
			if (const auto journal = m_Impl->Journals.find(info.Id); journal != m_Impl->Journals.end() &&
				journal->second.LayoutHash == info.LayoutHash && journal->second.Size == info.Size)
				pool.Journal = &journal->second;
		}
		else if (pool.Name != info.Name || pool.Size != info.Size || pool.LayoutHash != info.LayoutHash)
		{
			return false;
		}
		if (pool.Find(entity.m_Index) != Detail::PoolView::NoIndex)
			return false;
		pool.Insert(entity, value, m_Impl->ChangeTick);
		m_Impl->Record(pool, entity, LifecycleEvent::Added);
		return true;
	}

	bool Registry::ReadComponent(Entity entity, const ComponentInfo& info, void* value) const noexcept
	{
		const auto* bytes = value ? TryGetComponent(entity, info) : nullptr;
		if (!bytes)
			return false;
		std::memcpy(value, bytes, info.Size);
		return true;
	}

	bool Registry::WriteComponent(Entity entity, const ComponentInfo& info, const void* value) noexcept
	{
		auto* bytes = value ? TryGetComponent(entity, info) : nullptr;
		if (!bytes)
			return false;
		std::memcpy(bytes, value, info.Size);
		return true;
	}

	bool Registry::RemoveComponent(Entity entity, const ComponentInfo& info) noexcept
	{
		if (!IsAlive(entity) || RejectWhileIterating(m_Impl->Iterating, "Remove"))
			return false;
		auto* pool = m_Impl->FindPool(info);
		if (!pool || !pool->Erase(entity.m_Index))
			return false;
		m_Impl->Record(*pool, entity, LifecycleEvent::Removed);
		return true;
	}

	std::uint32_t Registry::SubscribeLifecycle(const ComponentInfo& info, std::size_t maxBacklog)
	{
		auto& impl = *m_Impl;
		if (info.Name.empty() || info.Size == 0)
			return 0;
		const auto existing = impl.Components.find(info.Id);
		if (existing != impl.Components.end() && (existing->second.Name != info.Name ||
			existing->second.Size != info.Size || existing->second.LayoutHash != info.LayoutHash))
			return 0;
		if (impl.LastSubscription == std::numeric_limits<std::uint32_t>::max())
			throw std::overflow_error("Lifecycle subscriptions exhausted");
		auto [entry, created] = impl.Journals.try_emplace(info.Id);
		auto& journal = entry->second;
		if (created)
		{
			journal.Name = info.Name;
			journal.Size = info.Size;
			journal.LayoutHash = info.LayoutHash;
		}
		else if (journal.Name != info.Name || journal.Size != info.Size || journal.LayoutHash != info.LayoutHash)
		{
			return 0;
		}
		const auto id = impl.LastSubscription + 1;
		try
		{
			journal.Subscribers.push_back(id);
			try
			{
				impl.Subscribers.emplace(id, Impl::LifecycleSubscriber{ info.Id, journal.End(), maxBacklog, true });
			}
			catch (...)
			{
				journal.Subscribers.pop_back();
				throw;
			}
		}
		catch (...)
		{
			if (journal.Subscribers.empty())
				impl.Journals.erase(entry);
			throw;
		}
		impl.LastSubscription = id;
		if (existing != impl.Components.end())
			existing->second.Journal = &journal;
		return id;
	}

	void Registry::UnsubscribeLifecycle(std::uint32_t subscription) noexcept
	{
		auto& impl = *m_Impl;
		const auto found = impl.Subscribers.find(subscription);
		if (found == impl.Subscribers.end())
			return;
		const auto componentId = found->second.ComponentId;
		impl.Subscribers.erase(found);
		auto& journal = impl.Journals.at(componentId);
		std::erase(journal.Subscribers, subscription);
		if (!journal.Subscribers.empty())
		{
			impl.Trim(journal);
			return;
		}
		if (const auto pool = impl.Components.find(componentId); pool != impl.Components.end())
			pool->second.Journal = nullptr;
		impl.Journals.erase(componentId);
	}

	LifecycleBatch Registry::ReadLifecycle(std::uint32_t subscription) noexcept
	{
		auto& impl = *m_Impl;
		const auto found = impl.Subscribers.find(subscription);
		if (found == impl.Subscribers.end())
			return {};
		auto& subscriber = found->second;
		auto& journal = impl.Journals.at(subscriber.ComponentId);
		// Entries handed out by earlier reads are dropped here, which is why a batch is valid only
		// until the next read.
		impl.Trim(journal);
		const auto first = static_cast<std::size_t>(subscriber.Cursor - journal.Base);
		const LifecycleBatch batch{ std::span<const LifecycleEntry>(journal.Entries).subspan(first), subscriber.Resync };
		subscriber.Cursor = journal.End();
		subscriber.Resync = false;
		return batch;
	}

	const void* Registry::TryGetComponent(Entity entity, const ComponentInfo& info) const noexcept
	{
		const auto* pool = IsAlive(entity) ? std::as_const(*m_Impl).FindPool(info) : nullptr;
		return pool ? pool->Get(entity.m_Index) : nullptr;
	}

	void* Registry::TryGetComponent(Entity entity, const ComponentInfo& info) noexcept
	{
		auto* pool = IsAlive(entity) ? m_Impl->FindPool(info) : nullptr;
		const auto dense = pool ? pool->Find(entity.m_Index) : Detail::PoolView::NoIndex;
		if (dense == Detail::PoolView::NoIndex)
			return nullptr;
		pool->Ticks[dense] = m_Impl->ChangeTick;
		return pool->Values.At(dense);
	}

	std::uint32_t Registry::GetChangeTick() const noexcept
	{
		return m_Impl->ChangeTick;
	}

	std::uint32_t Registry::AdvanceChangeTick() noexcept
	{
		return m_Impl->ChangeTick++;
	}

	Detail::PoolView Registry::GetPool(const ComponentInfo& info) noexcept
	{
		auto* pool = m_Impl->FindPool(info);
		return pool ? pool->GetView() : Detail::PoolView{};
	}

	Detail::IterationState Registry::GetIterationState() noexcept
	{
		return { &m_Impl->Iterating, m_Impl->Owners.data(), m_Impl->ChangeTick };
	}

	std::uint32_t Registry::AllocateSceneId()
	{
		if (m_Impl->LastSceneId == std::numeric_limits<std::uint32_t>::max())
			throw std::overflow_error("Registry scene identity exhausted");
		return ++m_Impl->LastSceneId;
	}

	void Registry::SetOwner(Entity entity, std::uint32_t scene) noexcept
	{
		if (IsAlive(entity))
			m_Impl->Owners[entity.m_Index] = scene;
	}

	std::uint32_t Registry::GetOwner(Entity entity) const noexcept
	{
		return IsAlive(entity) ? m_Impl->Owners[entity.m_Index] : 0;
	}
}
