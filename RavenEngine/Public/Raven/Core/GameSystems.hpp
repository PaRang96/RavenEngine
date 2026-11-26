#pragma once

#include "Raven/Assets/AssetRef.hpp"
#include "Raven/Audio/Sound.hpp"
#include "Raven/Core/Api.hpp"
#include "Raven/ECS/Registry.hpp"
#include "Raven/Core/SimulationRandom.hpp"
#include "Raven/Input/InputMap.hpp"
#include "Raven/Input/InputStream.hpp"
#include "Raven/Physics/PhysicsQueries.hpp"
#include "Raven/Scene/SceneAccess.hpp"
#include "Raven/Scene/ScenePicking.hpp"
#include "Raven/UI/Ui.hpp"

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
	class UserDataStore;
	struct InputState;
	struct FrameTime;

	// Host-scheduled game phases (docs/ECSDesign.md, D6). They run only while the engine session
	// plays (docs/DataModelDesign.md, FD-12).
	enum class SystemPhase : std::uint8_t
	{
		// Once per drawable frame, before the fixed steps: controllers read input, own local view
		// state (camera rig, selection, hover) and emit input-stream records. They never write
		// simulation state (docs/GameServicesDesign.md, FD-47).
		Control,
		// Once per fixed gameplay step, at the project's fixed-step rate.
		FixedUpdate,
		// Once per drawable frame, after any fixed steps.
		Update,
	};

	// Where game-driven scene flow stands (GameServices::RequestScene and the rest).
	enum class SceneTransition : std::uint8_t
	{
		// No request is pending, and the last one succeeded.
		Idle,
		// A request waits for the next frame (with asynchronous loading, M6B B6: its scene loads).
		Loading,
		// The last request failed: the scene that was playing keeps playing, and the log says why.
		Failed,
	};

	// Engine services a system may call during its update. Implemented by the engine.
	class GameServices
	{
	public:
		// Scene flow: menus, levels and retries (docs/GameServicesDesign.md, review amendments). A
		// request is applied between frames, before the next frame's fixed steps; the last request
		// wins. Once one is pending, the frame's remaining fixed steps are skipped, so no tick runs on
		// a scene the game has already left. Nothing carries over into the new scene yet.
		virtual void RequestScene(AssetRef<SceneAsset> scene) noexcept = 0;
		// The next scene in the project's scene order; after the last scene, the first.
		virtual void RequestNextScene() noexcept = 0;
		// The current scene again, as authored.
		virtual void RequestRestart() noexcept = 0;
		virtual SceneTransition GetSceneTransition() const noexcept = 0;
		// Sounds (docs/GameServicesDesign.md, C8). A bad reference is logged once and returns an empty
		// handle. Calls with a handle whose sound ended do nothing, so handles may be kept in components.
		virtual SoundHandle PlaySound(AssetRef<AudioAsset> sound, const SoundParams& params) = 0;
		SoundHandle PlaySound(AssetRef<AudioAsset> sound) { return PlaySound(sound, SoundParams{}); }
		// Plays a sound-event asset: one of its clips at random, with its volume and pitch ranges, bus,
		// limits and spatial settings (docs/GameServicesDesign.md, C8). Designers tune the event file.
		virtual SoundHandle PlaySound(AssetRef<SoundAsset> sound, const SoundOverrides& overrides) = 0;
		SoundHandle PlaySound(AssetRef<SoundAsset> sound) { return PlaySound(sound, SoundOverrides{}); }
		// Fades out over fadeSeconds; even zero fades for about 1 ms, so a stop never clicks.
		virtual void StopSound(SoundHandle sound, float fadeSeconds) noexcept = 0;
		virtual void StopSounds() noexcept = 0;
		virtual bool IsSoundPlaying(SoundHandle sound) const noexcept = 0;
		// Changes glide over 10 ms and take effect at the end of the frame.
		virtual void SetSoundVolume(SoundHandle sound, float volume) noexcept = 0;
		virtual void SetSoundPitch(SoundHandle sound, float pitch) noexcept = 0;
		virtual void SetSoundPosition(SoundHandle sound, Vec3 position) noexcept = 0;
		// Linear bus volumes, 0 to 4, for example from an options menu. Kept across scene restarts.
		virtual void SetBusVolume(AudioBus bus, float volume) noexcept = 0;
		virtual float GetBusVolume(AudioBus bus) const noexcept = 0;
		// Launch options for the game: `--name` on the command line, asked for as "name".
		virtual bool HasOption(std::string_view name) const noexcept = 0;
		// Benchmarks: a value the frame trace records as benchmark_phase.
		virtual void SetTracePhase(double value) noexcept = 0;
		// Per-user data that outlives a play session (docs/GameServicesDesign.md, FD-33).
		virtual UserDataStore& GetUserData() = 0;
		// A game pause, for example behind a pause menu: from the next frame, fixed steps and physics
		// stop while Control and Update keep running. Stream records emitted meanwhile are dropped.
		// Kept across scene restarts and module reloads; the developer pause (F8) is separate.
		virtual void SetSimulationPaused(bool paused) noexcept = 0;
		virtual bool IsSimulationPaused() const noexcept = 0;

	protected:
		~GameServices() = default;
	};

	// Borrowed for one system call; retain nothing from it (docs/DataModelDesign.md, FD-12).
	// Systems see only engine concepts: the scene being played, input, time, the picks of
	// the presented frame, deferred structural changes and engine services.
	struct SystemContext
	{
		SceneAccess World;
		// Raw input without what a mapping context consumed (docs/GameServicesDesign.md, FD-29).
		// Fixed steps see keyboard and mouse buttons; pointer motion and scroll stay per frame.
		const InputState& Input;
		// Mapped actions for this frame or fixed step, plus context, listening and rebinding control.
		InputActions& Actions;
		// Controllers emit records; fixed steps read the records of their tick (FD-47).
		InputStream& Stream;
		const FrameTime& Time;
		// Left-click hits on the presented frame, delivered once to the next update.
		std::span<const PickHit> Picks;
		// Collision queries, and during fixed steps the previous step's contacts (FD-32).
		const PhysicsQueries& Physics;
		SceneCommands& Commands;
		GameServices& Services;
		// Seeded when the scene starts; fixed-step code draws its random numbers here (FD-48).
		SimulationRandom& Random;
		// The game's UI context (docs/UIDesign.md). Declare UI in the Control or Update phase, once
		// per frame; fixed steps run a varying number of times per frame and must not declare UI.
		UI::Ui& Ui;
	};

	// A UI preview entry (docs/UIDesign.md, "Visual editing and preview", layer 1): one screen drawn
	// with sample data, shown alone in the developer preview mode (F6) without playing.
	using PreviewFunction = void (*)(UI::Ui& ui);

	struct PreviewDesc
	{
		// Static text in the module; the host copies it during registration.
		const char* Name = nullptr;
		PreviewFunction Run = nullptr;
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
		// Registers a UI preview entry; the host keeps its function only for the loaded generation.
		RAVEN_API void Preview(const PreviewDesc& preview);
		const std::vector<PreviewDesc>& GetPreviews() const noexcept { return m_Previews; }

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

		// Declares an input mapping context and its actions (docs/GameServicesDesign.md, FD-29). The
		// host copies them; a duplicate or invalid declaration rejects the module.
		RAVEN_API void InputContext(const InputContextDesc& context);
		RAVEN_API void Action(const ActionDesc& action);
		const std::vector<RegisteredInputContext>& GetInputContexts() const noexcept { return m_InputContexts; }
		const std::vector<RegisteredAction>& GetActions() const noexcept { return m_Actions; }

	private:
		std::vector<SystemDesc> m_Systems;
		std::vector<PreviewDesc> m_Previews;
		std::vector<ECS::ComponentSchema> m_Components;
		std::vector<RegisteredInputContext> m_InputContexts;
		std::vector<RegisteredAction> m_Actions;
	};

	using RegisterSystemsFunction = void (*)(SystemTable& systems);
	inline constexpr char RegisterSystemsEntryPoint[] = "RavenRegisterSystems";

	// Every game module exports this; the host checks it before registering systems.
	using GetGameContractFunction = const char* (*)() noexcept;
	inline constexpr char GameContractEntryPoint[] = "RavenGetGameContract";
}
