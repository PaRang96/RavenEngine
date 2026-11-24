#pragma once

#include "Raven/Assets/AssetRef.hpp"
#include "Raven/Core/Api.hpp"
#include "Raven/ECS/Registry.hpp"
#include "Raven/Scene/SceneAccess.hpp"
#include "Raven/Scene/ScenePicking.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Raven
{
	class SceneCommands;
	struct InputState;
	struct FrameTime;

	// Host-scheduled game phases (docs/ECSDesign.md, D6). Both run only while the engine session
	// plays (docs/DataModelDesign.md, FD-12).
	enum class SystemPhase : std::uint8_t
	{
		// Once per fixed gameplay step.
		FixedUpdate,
		// Once per drawable frame, after any fixed steps.
		Update,
	};

	// Engine services a system may call during its update. Implemented by the engine.
	class GameServices
	{
	public:
		// Replaces the current one-shot; a bad reference is logged and ignored.
		virtual void PlaySound(AssetRef<AudioAsset> sound) = 0;
		virtual void StopSounds() noexcept = 0;
		// Launch options for the game: `--name` on the command line, asked for as "name".
		virtual bool HasOption(std::string_view name) const noexcept = 0;
		// Benchmarks: a value the frame trace records as benchmark_phase.
		virtual void SetTracePhase(double value) noexcept = 0;

	protected:
		~GameServices() = default;
	};

	// Borrowed for one system call; retain nothing from it (docs/DataModelDesign.md, FD-12).
	// Systems see only engine concepts: the scene being played, input, time, the picks of
	// the presented frame, deferred structural changes and engine services.
	struct SystemContext
	{
		SceneAccess World;
		const InputState& Input;
		const FrameTime& Time;
		// Left-click hits on the presented frame, delivered once to the next update.
		std::span<const PickHit> Picks;
		SceneCommands& Commands;
		GameServices& Services;
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

		// Declares a component the module defines (docs/DataModelDesign.md, FD-08). The host copies
		// its field table and defaults, and registers them before the systems run.
		template<ECS::ComponentType T>
		void Component()
		{
			std::array<std::byte, sizeof(T)> defaults;
			if (!ECS::Detail::CaptureDefaults<T>(defaults.data()))
				throw std::logic_error(std::string("Component fields are not at their natural offsets: ") +
					std::string(T::TypeName));
			AddComponent(ECS::ComponentTraits<T>::Info, defaults);
		}
		RAVEN_API void AddComponent(const ECS::ComponentInfo& info, std::span<const std::byte> defaults);
		const std::vector<ECS::ComponentSchema>& GetComponents() const noexcept { return m_Components; }

	private:
		std::vector<SystemDesc> m_Systems;
		std::vector<ECS::ComponentSchema> m_Components;
	};

	using RegisterSystemsFunction = void (*)(SystemTable& systems);
	inline constexpr char RegisterSystemsEntryPoint[] = "RavenRegisterSystems";

	// Every game module exports this; the host checks it before registering systems.
	using GetGameContractFunction = const char* (*)() noexcept;
	inline constexpr char GameContractEntryPoint[] = "RavenGetGameContract";
}
