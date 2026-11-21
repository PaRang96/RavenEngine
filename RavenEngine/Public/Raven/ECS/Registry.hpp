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

		// Registry state a running view uses: the active-iteration counter and one owning
		// scene ID per entity slot (0 when no scene owns the entity).
		struct IterationState
		{
			std::uint32_t* Active = nullptr;
			const std::uint32_t* Owners = nullptr;
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
		template<ComponentType T>
		T* TryGet(Entity entity) noexcept
		{
			return static_cast<T*>(TryGetComponent(entity, ComponentTraits<T>::Info));
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

		// Type-erased access for generic callers and tools. Values address Info.Size bytes
		// laid out as the described component. A mismatched layout never matches a pool.
		RAVEN_API bool HasComponent(Entity entity, const ComponentInfo& info) const noexcept;
		RAVEN_API bool AddComponent(Entity entity, const ComponentInfo& info, const void* value);
		RAVEN_API bool ReadComponent(Entity entity, const ComponentInfo& info, void* value) const noexcept;
		RAVEN_API bool WriteComponent(Entity entity, const ComponentInfo& info, const void* value) noexcept;
		RAVEN_API bool RemoveComponent(Entity entity, const ComponentInfo& info) noexcept;
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
