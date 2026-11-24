#include "Raven/Scene/SceneCommands.hpp"

#include "Raven/Core/Log.hpp"
#include "Raven/ECS/Registry.hpp"
#include "Raven/Scene/Scene.hpp"

#include <cstring>
#include <set>
#include <stdexcept>
#include <utility>

namespace Raven
{
	SceneCommands::SceneCommands(Scene& scene) noexcept
		: m_Scene(&scene)
	{
	}

	SceneCommands::~SceneCommands() = default;

	ECS::EntityRef SceneCommands::Create(std::string_view label)
	{
		const auto id = m_Scene->NewRef();
		m_Labels.emplace_back(label);
		m_Commands.push_back({ Kind::Create, id.Id, 0, m_Labels.size() - 1 });
		return id;
	}

	void SceneCommands::Destroy(ECS::EntityRef entity)
	{
		m_Commands.push_back({ Kind::Destroy, entity.Id });
	}

	void SceneCommands::SetParent(ECS::EntityRef child, ECS::EntityRef parent)
	{
		m_Commands.push_back({ Kind::SetParent, child.Id, parent.Id });
	}

	std::size_t SceneCommands::FindType(const ECS::ComponentInfo& info)
	{
		for (std::size_t index = 0; index < m_Types.size(); ++index)
			if (m_Types[index].Id == info.Id && m_Types[index].LayoutHash == info.LayoutHash)
				return index;
		m_Types.push_back({ info.Id, std::string(info.Name), info.Size, info.Alignment, info.LayoutHash });
		return m_Types.size() - 1;
	}

	void SceneCommands::AddComponent(ECS::EntityRef entity, const ECS::ComponentInfo& info, const void* value)
	{
		if (!value || info.Size == 0)
			throw std::invalid_argument("A component command needs a value");
		const auto type = FindType(info);
		const auto offset = m_Values.size();
		const auto* bytes = static_cast<const std::byte*>(value);
		m_Values.insert(m_Values.end(), bytes, bytes + info.Size);
		m_Commands.push_back({ Kind::Add, entity.Id, type, offset });
	}

	void SceneCommands::RemoveComponent(ECS::EntityRef entity, const ECS::ComponentInfo& info)
	{
		m_Commands.push_back({ Kind::Remove, entity.Id, FindType(info) });
	}

	std::size_t SceneCommands::GetPendingCount() const noexcept
	{
		return m_Commands.size();
	}

	void SceneCommands::Reset(Scene& scene) noexcept
	{
		m_Scene = &scene;
		m_Commands.clear();
		m_Labels.clear();
		m_Values.clear();
		m_Types.clear();
	}

	void SceneCommands::Apply()
	{
		if (m_Commands.empty())
			return;
		auto& scene = *m_Scene;
		auto& registry = scene.m_Registry;
		if (registry.IsIterating())
			throw std::logic_error("Scene commands are applied outside views");
		// Swap the buffer out first, so a failing command cannot be applied twice.
		auto commands = std::exchange(m_Commands, {});
		auto labels = std::exchange(m_Labels, {});
		auto values = std::exchange(m_Values, {});
		auto types = std::exchange(m_Types, {});
		std::set<std::string> reported;
		const auto report = [&reported](const std::string& message)
		{
			if (reported.insert(message).second)
				Log(LogLevel::Warning, "Scene command skipped: " + message);
		};
		for (const auto& command : commands)
		{
			const ECS::EntityRef target{ command.Entity };
			if (command.Type == Kind::Create)
			{
				try
				{
					scene.CreateEntity(labels[command.Data], target);
				}
				catch (const std::invalid_argument& error)
				{
					report(error.what());
				}
				continue;
			}
			const auto entity = scene.Resolve(target);
			if (entity.IsNull())
			{
				report("its entity no longer exists");
				continue;
			}
			switch (command.Type)
			{
			case Kind::Destroy:
				scene.DestroyEntity(entity);
				break;
			case Kind::SetParent:
			{
				const ECS::EntityRef parentRef{ command.Other };
				const auto parent = scene.Resolve(parentRef);
				if (!parentRef.IsNull() && parent.IsNull())
				{
					report("its parent no longer exists");
					break;
				}
				try
				{
					scene.SetParent(entity, parent);
				}
				catch (const std::invalid_argument& error)
				{
					report(error.what());
				}
				break;
			}
			case Kind::Add:
			case Kind::Remove:
			{
				const auto& type = types[command.Other];
				const ECS::ComponentInfo info{ type.Id, type.Name, type.Size, type.Alignment, type.LayoutHash };
				const bool applied = command.Type == Kind::Add
					? registry.AddComponent(entity, info, values.data() + command.Data)
					: registry.RemoveComponent(entity, info);
				if (!applied)
					report((command.Type == Kind::Add ? "could not add " : "could not remove ") + type.Name);
				break;
			}
			case Kind::Create:
				break;
			}
		}
	}
}
