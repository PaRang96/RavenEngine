#pragma once

#include "Raven/ECS/Registry.hpp"

namespace Raven
{
	class GameInstance
	{
	public:
		GameInstance() = default;
		~GameInstance() = default;
		GameInstance(const GameInstance&) = delete;
		GameInstance& operator=(const GameInstance&) = delete;
		GameInstance(GameInstance&&) = delete;
		GameInstance& operator=(GameInstance&&) = delete;

		ECS::Registry& GetRegistry() noexcept { return m_Registry; }
		const ECS::Registry& GetRegistry() const noexcept { return m_Registry; }

	private:
		ECS::Registry m_Registry;
	};
}
