#pragma once

#include "Audio/AudioPlayer.hpp"
#include "Raven/Core/Application.hpp"
#include "Raven/Core/GameSystems.hpp"
#include "Raven/Core/SimulationRandom.hpp"
#include "Raven/Renderer/CameraView.hpp"
#include "Raven/Renderer/DebugLines.hpp"
#include "Raven/Renderer/Vulkan/VulkanTexturedDraw.hpp"
#include "Raven/Renderer/Vulkan/VulkanTransparentDraw.hpp"
#include "Raven/Scene/SceneCommands.hpp"
#include "Raven/Scene/SceneProject.hpp"
#include "Raven/Scene/SceneSession.hpp"
#include "UI/UiContext.hpp"

#include <chrono>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace Raven
{
	class GameInstance;
	class PickWorld;
	class RenderWorld;
	class VulkanContext;
	struct InputState;
	struct FrameTime;

	// Developer actions pressed this frame, from the engine's developer input context
	// (docs/GameServicesDesign.md, FD-29). Default bindings: F3, F4, F7, F8 and Shift+F8.
	struct DeveloperRequests
	{
		bool RestartScene = false;
		bool NextScene = false;
		bool CycleRenderView = false;
		bool TogglePlay = false;
		// Restart the scene from its file and play (FD-32 iteration set).
		bool RestartPlay = false;
	};

	// The engine's play session for one game root (docs/DataModelDesign.md, FD-12, FD-17). It owns the
	// project, the scene session, the deferred commands and the services game systems call, and
	// draws the active scene: the entity tagged ActiveCameraComponent, the scene's lighting, its
	// lit meshes from the render world and its sprites. Developer requests restart the scene, open
	// the next one, cycle the render view, and pause or resume play.
	class GameSession final : private GameServices
	{
	public:
		GameSession(VulkanContext& renderer, GameInstance& game, const ApplicationConfig& config);
		~GameSession();
		GameSession(const GameSession&) = delete;
		GameSession& operator=(const GameSession&) = delete;

		// Before gameplay. True when a new scene replaced the active one.
		bool Prepare(const DeveloperRequests& requests, const FrameTime& time);
		// Restarts the scene, opens the next one, or applies pending scene and project edits.
		bool Advance(bool restart = false, bool next = false);
		// A game scene-flow request is pending: the frame's remaining fixed steps are skipped.
		bool HasSceneRequest() const noexcept { return m_SceneRequest.has_value(); }
		SceneSession& GetSession() noexcept { return *m_Session; }
		SceneCommands& GetCommands() noexcept { return m_Commands; }
		GameServices& GetServices() noexcept { return *this; }
		bool IsPlaying() const noexcept { return m_Playing; }
		Scene& GetScene() const noexcept;
		const AssetCatalog& GetCatalog() const noexcept;
		// The context for one phase (review amendment, AA2-GP-02/03). A fixed step gets no picks, no
		// UI and the seeded random numbers, and the scene draws IDs from its simulation sequence until
		// EndFixedStep; Control and Update get the presented frame's picks, the live UI, and the
		// OS-seeded presentation random numbers and IDs.
		SystemContext MakeContext(SystemPhase phase, const InputState& input, InputActions& actions,
			InputStream& stream, const FrameTime& time);
		// After a fixed step's systems and commands: back to the presentation streams.
		void EndFixedStep() noexcept;
		// Applies deferred structural changes; called after each fixed step and after Update.
		void ApplyCommands();
		// After a failed Gameplay stage: drops what the failed systems recorded, so the buffer is
		// empty between frames (FD-14) and nothing reaches the next frame or a module activation.
		void DropCommands() noexcept;
		// True when the frame was presented. Left-button presses in `input` (the game's view, after
		// input contexts) are picked against the presented frame.
		bool Render(VulkanContext& renderer, RenderWorld& world, const InputState& input,
			std::span<const DebugLineVertex> debugLines = {});
		double GetTracePhase() const noexcept { return m_TracePhase; }
		// The project's fixed-step rate when the session started.
		double GetFixedStepRate() const noexcept;
		std::uint64_t GetSimulationSeed() const noexcept { return m_SimulationSeed; }
		// The developer quick-save (docs/GameServicesDesign.md, FD-33): a snapshot of the live world,
		// its random numbers, ID sequence and fixed tick, kept in memory. Between frames only.
		void QuickSave(std::uint64_t tick);
		// Replaces the world with the snapshot and returns its tick; empty, with one log line, when
		// there is none, it belongs to another scene or it no longer loads.
		std::optional<std::uint64_t> QuickLoad();
		bool IsSimulationPaused() const noexcept override { return m_SimulationPaused; }
		// The host's user-data store; systems reach it through GameServices::GetUserData.
		void SetUserData(UserDataStore* store) noexcept { m_UserData = store; }
		// The host's physics world, which systems query through SystemContext::Physics.
		void SetPhysics(const PhysicsQueries* physics) noexcept { m_Physics = physics; }
		// The host's text stack and UI contexts (docs/UIDesign.md): the game's, which systems declare into,
		// and the developer overlay's. Without them, systems get a UI that draws nothing.
		void SetUi(Text::TextSystem* text, UI::UiContext* game, UI::UiContext* developer);
		// The host's pick world (FD-34). Without one, nothing is picked.
		void SetPicking(PickWorld* picking) noexcept { m_Picking = picking; }
		// The closest static mesh of the active scene along a ray, as drawn, within maxDistance.
		std::optional<PickHit> Pick(const Ray& ray, float maxDistance);
		const PhysicsLimits& GetPhysicsLimits() const noexcept;

	private:
		SoundHandle PlaySound(AssetRef<AudioAsset> sound, const SoundParams& params) override;
		SoundHandle PlaySound(AssetRef<SoundAsset> sound, const SoundOverrides& overrides) override;
		void StopSound(SoundHandle sound, float fadeSeconds) noexcept override { m_Audio.Stop(sound, fadeSeconds); }
		void StopSounds() noexcept override;
		bool IsSoundPlaying(SoundHandle sound) const noexcept override { return m_Audio.IsPlaying(sound); }
		void SetSoundVolume(SoundHandle sound, float volume) noexcept override { m_Audio.SetVolume(sound, volume); }
		void SetSoundPitch(SoundHandle sound, float pitch) noexcept override { m_Audio.SetPitch(sound, pitch); }
		void SetSoundPosition(SoundHandle sound, Vec3 position) noexcept override { m_Audio.SetPosition(sound, position); }
		void SetBusVolume(AudioBus bus, float volume) noexcept override { m_Audio.SetBusVolume(bus, volume); }
		float GetBusVolume(AudioBus bus) const noexcept override { return m_Audio.GetBusVolume(bus); }
		bool HasOption(std::string_view name) const noexcept override;
		void SetTracePhase(double value) noexcept override { m_TracePhase = value; }
		UserDataStore& GetUserData() override;
		void SetSimulationPaused(bool paused) noexcept override;
		void RequestScene(AssetRef<SceneAsset> scene) noexcept override;
		void RequestNextScene() noexcept override;
		void RequestRestart() noexcept override;
		SceneTransition GetSceneTransition() const noexcept override;

		void GatherSprites();
		OverlayFrame BuildOverlay();
		void ReportCamera(const std::string& message);

		VulkanContext& m_Renderer;
		GameInstance& m_Game;
		std::vector<std::string> m_Options;
		SceneProject m_Project;
		AudioPlayer m_Audio;
		std::unique_ptr<SceneSession> m_Session;
		SceneCommands m_Commands;
		const VulkanTexturedMesh& m_Quad;
		std::vector<VulkanTexturedDraw> m_OpaqueSprites;
		std::vector<VulkanTransparentDraw> m_TransparentSprites;
		std::vector<PickHit> m_Picks;
		std::vector<PickHit> m_PendingPicks;
		std::string m_CameraProblem;
		// The last reported lighting problem, and the sprite count skipped in the last gather.
		std::string m_LightingProblem;
		std::size_t m_SkippedSprites = 0;
		bool m_Playing = true;
		double m_TracePhase = 0.0;
		UserDataStore* m_UserData = nullptr;
		const PhysicsQueries* m_Physics = nullptr;
		PickWorld* m_Picking = nullptr;
		Text::TextSystem* m_Text = nullptr;
		UI::UiContext* m_Ui = nullptr;
		UI::UiContext* m_DeveloperUi = nullptr;
		std::vector<OverlayQuad> m_Overlay;
		// Sound-event variations and ranges are presentation, never simulation: their own generator.
		std::minstd_rand m_SoundRandom{ std::random_device{}() };
		// Seeds the random numbers and runtime entity IDs whenever a scene starts (FD-48).
		std::uint64_t m_SimulationSeed = 0;
		SimulationRandom m_Random;
		// Control and Update draw from this one, so cosmetic randomness never shifts the simulation.
		SimulationRandom m_PresentationRandom{ (std::uint64_t{ std::random_device{}() } << 32) ^ std::random_device{}() };
		// The phase whose systems are running, while one runs.
		bool m_InFixedStep = false;
		void StartSimulation() noexcept;
		struct Snapshot
		{
			AssetId Scene;
			std::vector<std::byte> Bytes;
			std::uint64_t Random = 0;
			std::uint64_t Ids = 0;
			std::uint64_t Tick = 0;
			std::size_t Entities = 0;
		};
		std::optional<Snapshot> m_QuickSave;
		// The game's scene-flow request, applied by the next Prepare (the last request wins).
		struct SceneRequest
		{
			enum class Kind : std::uint8_t { Scene, Next, Restart } Type = Kind::Restart;
			AssetId Scene;
		};
		std::optional<SceneRequest> m_SceneRequest;
		SceneTransition m_Transition = SceneTransition::Idle;
		bool m_SimulationPaused = false;
		bool m_MeshReloadAwaitingPresent = false;
		bool m_TextureReloadAwaitingPresent = false;
		std::chrono::steady_clock::time_point m_ReloadApplied{};
	};
}
