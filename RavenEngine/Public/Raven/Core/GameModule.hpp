#pragma once

#include "Raven/Core/Api.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace Raven
{
	class GameInstance;
	struct InputState;
	struct FrameTime;

	// Development-only, synchronous game code. Call from the game thread between updates.
	// Reloadable code must not retain pointers, callbacks, threads or owning C++ objects.
	class GameModule
	{
	public:
		RAVEN_API explicit GameModule(const std::filesystem::path& path, bool autoBuild = false);
		RAVEN_API ~GameModule();
		GameModule(const GameModule&) = delete;
		GameModule& operator=(const GameModule&) = delete;
		GameModule(GameModule&&) = delete;
		GameModule& operator=(GameModule&&) = delete;

		// A rejected candidate leaves the active module and game data intact.
		RAVEN_API bool Reload();
		RAVEN_API void Poll();
		RAVEN_API void Update(GameInstance&, const InputState&, const FrameTime&);
		RAVEN_API std::uint64_t GetGeneration() const noexcept;
		RAVEN_API bool IsBuilding() const noexcept;
		RAVEN_API std::string GetLastError() const;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
