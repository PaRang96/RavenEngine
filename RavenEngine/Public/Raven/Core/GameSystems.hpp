#pragma once

#include "Raven/Core/Api.hpp"
#include "Raven/Core/ApplicationClient.hpp"

#include <cstdint>
#include <vector>

namespace Raven
{
	class GameInstance;
	struct InputState;
	struct FrameTime;

	// Host-scheduled game phases (docs/ECSDesign.md, D6).
	enum class SystemPhase : std::uint8_t
	{
		// Once per fixed gameplay step, only for clients that use fixed stepping.
		FixedUpdate,
		// Once per drawable frame, after any fixed steps.
		Update,
	};

	// Borrowed for one system call; retain nothing from it. Bindings.World is the
	// scene-scoped access. FixedUpdate systems report player intent through Result.
	struct SystemContext
	{
		GameInstance& Game;
		const InputState& Input;
		const FrameTime& Time;
		GameBindings Bindings;
		GameUpdateResult& Result;
	};

	// Exceptions are caught and logged by the host per system.
	using SystemFunction = void (*)(SystemContext& context);

	struct SystemDesc
	{
		// Static text in the module; the host copies it during registration.
		const char* Name = nullptr;
		SystemPhase Phase = SystemPhase::Update;
		// Lower runs first within a phase; equal orders keep registration order.
		std::int32_t Order = 0;
		SystemFunction Run = nullptr;
	};

	// Filled by the module's RavenRegisterSystems after its contract is accepted. The host
	// keeps the function pointers only for that loaded generation and clears them before
	// the module unloads. A rejected candidate leaves the active systems in place.
	class SystemTable
	{
	public:
		RAVEN_API void Add(const SystemDesc& system);
		const std::vector<SystemDesc>& GetSystems() const noexcept { return m_Systems; }

	private:
		std::vector<SystemDesc> m_Systems;
	};

	using RegisterSystemsFunction = void (*)(SystemTable& systems);
	inline constexpr char RegisterSystemsEntryPoint[] = "RavenRegisterSystems";
}
