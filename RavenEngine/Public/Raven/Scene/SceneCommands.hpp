#pragma once

#include "Raven/Core/Api.hpp"
#include "Raven/ECS/Component.hpp"
#include "Raven/ECS/Entity.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Raven
{
	class Scene;

	// Deferred structural changes (docs/DataModelDesign.md, FD-14). Systems record them; the engine
	// applies the buffer after each fixed step and at the end of Update, in record order. Create
	// returns the new entity's persistent ID at once, so it can be stored in components before the
	// entity exists; later commands in the same buffer may address it. A command whose entity is
	// gone by then is skipped and logged once (FD-06). Values are copied when recorded.
	class SceneCommands
	{
	public:
		RAVEN_API explicit SceneCommands(Scene& scene) noexcept;
		RAVEN_API ~SceneCommands();
		SceneCommands(const SceneCommands&) = delete;
		SceneCommands& operator=(const SceneCommands&) = delete;

		RAVEN_API ECS::EntityRef Create(std::string_view label = {});
		// Destroys the entity and its descendants.
		RAVEN_API void Destroy(ECS::EntityRef entity);
		// A null parent detaches the entity to the scene root.
		RAVEN_API void SetParent(ECS::EntityRef child, ECS::EntityRef parent = {});
		template<ECS::ComponentType T>
		void Add(ECS::EntityRef entity, const T& value)
		{
			static_assert(!ECS::HostOwned<T>, "Host-owned components change through Scene, for example SetParent");
			AddComponent(entity, ECS::ComponentTraits<T>::Info, &value);
		}
		template<ECS::ComponentType T>
		void Remove(ECS::EntityRef entity)
		{
			static_assert(!ECS::HostOwned<T>, "Host-owned components change through Scene, for example SetParent");
			RemoveComponent(entity, ECS::ComponentTraits<T>::Info);
		}
		RAVEN_API void AddComponent(ECS::EntityRef entity, const ECS::ComponentInfo& info, const void* value);
		RAVEN_API void RemoveComponent(ECS::EntityRef entity, const ECS::ComponentInfo& info);
		RAVEN_API std::size_t GetPendingCount() const noexcept;

		// Engine side: applies and clears the buffer, outside any view.
		RAVEN_API void Apply();
		// Drops the pending commands and records into another scene from now on.
		RAVEN_API void Reset(Scene& scene) noexcept;

	private:
		enum class Kind : std::uint8_t { Create, Destroy, SetParent, Add, Remove };
		struct Command
		{
			Kind Type = Kind::Create;
			std::uint64_t Entity = 0;
			// SetParent: the parent ID. Add and Remove: the index into m_Types.
			std::uint64_t Other = 0;
			// Create: the index into m_Labels. Add: the offset into m_Values.
			std::size_t Data = 0;
		};
		// Copied from the module's static info, so nothing recorded points into the module.
		struct ComponentType
		{
			std::uint64_t Id = 0;
			std::string Name;
			std::size_t Size = 0;
			std::size_t Alignment = 0;
			std::uint64_t LayoutHash = 0;
		};
		std::size_t FindType(const ECS::ComponentInfo& info);

		Scene* m_Scene;
		std::vector<Command> m_Commands;
		std::vector<std::string> m_Labels;
		std::vector<std::byte> m_Values;
		std::vector<ComponentType> m_Types;
	};
}
