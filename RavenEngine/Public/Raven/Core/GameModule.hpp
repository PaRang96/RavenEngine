#pragma once

#include "Raven/Core/Api.hpp"
#include "Raven/Core/GameSystems.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Raven
{
	class GameInstance;
	struct InputState;
	struct FrameTime;

	// Accumulated host-side timing for one registered system name, across generations.
	struct GameSystemStats
	{
		std::string Name;
		SystemPhase Phase = SystemPhase::Update;
		std::uint64_t Calls = 0;
		std::uint64_t Nanoseconds = 0;
		// The system threw and is skipped until the next module activation (docs/HotReloadDesign.md, H4).
		bool Suspended = false;
	};

	// Development-only, synchronous game code. Call from the game thread between updates.
	// Reloadable code must not retain pointers, callbacks, threads or owning C++ objects.
	class GameModule
	{
	public:
		// The module's components are registered in game's Registry, which must outlive the module.
		RAVEN_API GameModule(GameInstance& game, const std::filesystem::path& path, bool autoBuild = false);
		RAVEN_API ~GameModule();
		GameModule(const GameModule&) = delete;
		GameModule& operator=(const GameModule&) = delete;
		GameModule(GameModule&&) = delete;
		GameModule& operator=(GameModule&&) = delete;

		// A rejected candidate leaves the active module and game data intact.
		RAVEN_API bool Reload();
		RAVEN_API void Poll();
		// Run the registered systems of one phase in order with the given context.
		RAVEN_API void FixedUpdate(SystemContext& context);
		RAVEN_API void Update(SystemContext& context);
		RAVEN_API std::vector<GameSystemStats> GetSystemStats() const;
		RAVEN_API std::uint64_t GetGeneration() const noexcept;
		RAVEN_API bool IsBuilding() const noexcept;
		RAVEN_API std::string GetLastError() const;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
