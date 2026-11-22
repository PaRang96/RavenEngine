#pragma once

#include <cstddef>

#include "Raven/Core/Api.hpp"
#include "Raven/ECS/Component.hpp"
#include "Raven/ECS/Entity.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace Raven
{
	class Scene;
	class SceneAccess;
}

namespace Raven::ECS
{
	namespace Detail
	{
		struct EntityAccess
		{
			static constexpr std::uint32_t Index(Entity entity) noexcept { return entity.m_Index; }
		};

		// Borrowed description of one component pool. Valid until the next structural change.
		struct PoolView
		{
			static constexpr std::uint32_t PageShift = 12;
			static constexpr std::uint32_t PageMask = (1u << PageShift) - 1;
			static constexpr std::uint32_t NoIndex = 0xffffffffu;

			const Entity* Entities = nullptr;
			std::byte* Values = nullptr;
			// Change tick per dense element, stamped by mutable access.
			std::uint32_t* Ticks = nullptr;
			std::size_t Count = 0;
			std::size_t Stride = 0;
			const std::uint32_t* const* Pages = nullptr;
			std::size_t PageCount = 0;

			std::uint32_t Find(Entity entity) const noexcept
			{
				const auto index = EntityAccess::Index(entity);
				const std::size_t page = index >> PageShift;
				return page < PageCount && Pages[page] ? Pages[page][index & PageMask] : NoIndex;
			}

			template<typename T>
			T& At(std::uint32_t dense) const noexcept
			{
				return *std::launder(reinterpret_cast<T*>(Values + dense * Stride));
			}
		};

		// Registry state a running view uses: the active-iteration counter, one owning
		// scene ID per entity slot (0 when no scene owns the entity) and the change tick
		// that mutable access stamps.
		struct IterationState
		{
			std::uint32_t* Active = nullptr;
			const std::uint32_t* Owners = nullptr;
			std::uint32_t Tick = 0;
		};

		// Where a view reads from. Scene 0 visits every entity in the registry.
		struct ViewSource
		{
			Registry* Target = nullptr;
			std::uint32_t Scene = 0;
		};
	}

	template<typename T>
	concept ViewComponent = ComponentType<std::remove_const_t<T>>;

	// Lifecycle journal (docs/GpuDrivenDesign.md, G1). A subscriber to one component type learns
	// which entities gained or lost it. Entries live in memory only and are dropped once every
	// subscriber has read them; value changes stay on change ticks.
	enum class LifecycleEvent : std::uint8_t
	{
		Added,
		Removed
	};

	struct LifecycleEntry
	{
		Entity Target;
		LifecycleEvent Event = LifecycleEvent::Added;
	};

	struct LifecycleBatch
	{
		// Valid until the next structural change or the next ReadLifecycle call on the registry.
		// Entries describe what happened in order; check current state before acting on one.
		std::span<const LifecycleEntry> Entries;
		// True on the first read and after the backlog overflowed: rescan the component's
		// entities once, then follow the entries.
		bool Resync = false;
	};

	template<typename... T>
	class View;

    class Registry
    {
    public:
        RAVEN_API Registry();
        RAVEN_API ~Registry();

		// Throws std::logic_error while a view iterates.
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

		// Borrowed access (docs/ECSDesign.md, D4). References, pointers and views are valid
		// only during the current synchronous update, until the next structural change.
		// Create/Add/Remove/Destroy are rejected while a view iterates.
		template<ViewComponent... T>
			requires (sizeof...(T) > 0)
		ECS::View<T...> View() noexcept;
		// TryGet<const T> reads without marking the component changed.
		template<ViewComponent T>
		T* TryGet(Entity entity) noexcept
		{
			const auto& info = ComponentTraits<std::remove_const_t<T>>::Info;
			if constexpr (std::is_const_v<T>)
				return static_cast<T*>(std::as_const(*this).TryGetComponent(entity, info));
			else
				return static_cast<T*>(TryGetComponent(entity, info));
		}
		template<ComponentType T>
		const T* TryGet(Entity entity) const noexcept
		{
			return static_cast<const T*>(TryGetComponent(entity, ComponentTraits<T>::Info));
		}
		// The first component of type T, or null; use for single-instance state.
		template<ComponentType T>
		T* Single() noexcept;
		template<ComponentType T>
		std::size_t Count() noexcept
		{
			return GetPool(ComponentTraits<T>::Info).Count;
		}
		RAVEN_API bool IsIterating() const noexcept;

		// Lifecycle journal. Only subscribed types record. A subscriber that leaves more than
		// maxBacklog entries unread loses them and is told to resync. Returns 0 when the type's
		// layout conflicts with its pool.
		static constexpr std::size_t DefaultLifecycleBacklog = std::size_t{ 1 } << 20;
		template<ComponentType T>
		std::uint32_t SubscribeLifecycle(std::size_t maxBacklog = DefaultLifecycleBacklog)
		{
			return SubscribeLifecycle(ComponentTraits<T>::Info, maxBacklog);
		}
		RAVEN_API std::uint32_t SubscribeLifecycle(const ComponentInfo& info,
			std::size_t maxBacklog = DefaultLifecycleBacklog);
		RAVEN_API void UnsubscribeLifecycle(std::uint32_t subscription) noexcept;
		// Returns the entries recorded since this subscriber's last read. Unknown subscriptions
		// return an empty batch.
		RAVEN_API LifecycleBatch ReadLifecycle(std::uint32_t subscription) noexcept;

		// Change ticks (docs/ECSDesign.md, D4/D7). Add, Write, mutable TryGet and mutable view
		// access stamp the current tick on the element; reads do not. A consumer remembers
		// the tick AdvanceChangeTick returned and later processes elements stamped after it.
		RAVEN_API std::uint32_t GetChangeTick() const noexcept;
		// Closes the current tick and returns it; later stamps use the next value.
		RAVEN_API std::uint32_t AdvanceChangeTick() noexcept;

		// Type-erased access for generic callers and tools. Values address Info.Size bytes
		// laid out as the described component. A mismatched layout never matches a pool.
		RAVEN_API bool HasComponent(Entity entity, const ComponentInfo& info) const noexcept;
		RAVEN_API bool AddComponent(Entity entity, const ComponentInfo& info, const void* value);
		RAVEN_API bool ReadComponent(Entity entity, const ComponentInfo& info, void* value) const noexcept;
		RAVEN_API bool WriteComponent(Entity entity, const ComponentInfo& info, const void* value) noexcept;
		RAVEN_API bool RemoveComponent(Entity entity, const ComponentInfo& info) noexcept;
		// The mutable form stamps the change tick; the const form does not.
		RAVEN_API void* TryGetComponent(Entity entity, const ComponentInfo& info) noexcept;
		RAVEN_API const void* TryGetComponent(Entity entity, const ComponentInfo& info) const noexcept;

        Registry(const Registry &) = delete;
        Registry &operator=(const Registry &) = delete;

        Registry(Registry &&) = delete;
        Registry &operator=(Registry &&) = delete;

    private:
		friend class ::Raven::Scene;
		friend class ::Raven::SceneAccess;
		template<typename... T>
		friend class ::Raven::ECS::View;

		// An empty view when no compatible pool exists.
		RAVEN_API Detail::PoolView GetPool(const ComponentInfo& info) noexcept;
		RAVEN_API Detail::IterationState GetIterationState() noexcept;
		// Scene ownership marks entities so scene-scoped views skip foreign ones.
		RAVEN_API std::uint32_t AllocateSceneId();
		RAVEN_API void SetOwner(Entity entity, std::uint32_t scene) noexcept;
		RAVEN_API std::uint32_t GetOwner(Entity entity) const noexcept;

        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

	// Visits entities that have every listed component; const-qualify read-only types.
	// Borrowed for one synchronous update. Iteration starts from the smallest pool, and
	// order is unspecified because removal reorders dense storage.
	template<typename... T>
	class View
	{
		static_assert(sizeof...(T) > 0 && (ViewComponent<T> && ...));

	public:
		explicit View(Detail::ViewSource source) noexcept : m_Source(source) {}

		// Calls function(Entity, T&...) for each match. Return false to stop early.
		template<typename Function>
		void Each(Function&& function) const
		{
			Run(function, std::index_sequence_for<T...>{});
		}

	private:
		static constexpr std::size_t Size = sizeof...(T);

		template<typename Function, std::size_t... I>
		void Run(Function& function, std::index_sequence<I...>) const
		{
			if (!m_Source.Target)
				return;
			const std::array<Detail::PoolView, Size> pools
			{
				m_Source.Target->GetPool(ComponentTraits<std::remove_const_t<T>>::Info)...
			};
			std::size_t driver = 0;
			for (std::size_t pool = 0; pool < Size; ++pool)
			{
				if (pools[pool].Count == 0)
					return;
				if (pools[pool].Count < pools[driver].Count)
					driver = pool;
			}

			const auto state = m_Source.Target->GetIterationState();
			struct Guard
			{
				std::uint32_t* Active;
				~Guard() { --*Active; }
			};
			++*state.Active;
			const Guard guard{ state.Active };

			const auto& source = pools[driver];
			for (std::uint32_t dense = 0; dense < source.Count; ++dense)
			{
				const Entity entity = source.Entities[dense];
				if (m_Source.Scene != 0 && state.Owners[Detail::EntityAccess::Index(entity)] != m_Source.Scene)
					continue;
				std::array<std::uint32_t, Size> positions{};
				bool matched = true;
				for (std::size_t pool = 0; pool < Size && matched; ++pool)
				{
					positions[pool] = pool == driver ? dense : pools[pool].Find(entity);
					matched = positions[pool] != Detail::PoolView::NoIndex;
				}
				if (!matched)
					continue;
				(MarkChanged<T>(pools[I], positions[I], state.Tick), ...);
				if constexpr (std::is_same_v<std::invoke_result_t<Function&, Entity, T&...>, bool>)
				{
					if (!std::invoke(function, entity, pools[I].template At<T>(positions[I])...))
						return;
				}
				else
				{
					std::invoke(function, entity, pools[I].template At<T>(positions[I])...);
				}
			}
		}

		template<typename U>
		static void MarkChanged(const Detail::PoolView& pool, std::uint32_t dense, std::uint32_t tick) noexcept
		{
			if constexpr (!std::is_const_v<U>)
				pool.Ticks[dense] = tick;
		}

		Detail::ViewSource m_Source;
	};

	template<ViewComponent... T>
		requires (sizeof...(T) > 0)
	ECS::View<T...> Registry::View() noexcept
	{
		return ECS::View<T...>(Detail::ViewSource{ this, 0 });
	}

	template<ComponentType T>
	T* Registry::Single() noexcept
	{
		T* result = nullptr;
		View<T>().Each([&result](Entity, T& value)
		{
			result = &value;
			return false;
		});
		return result;
	}
}
