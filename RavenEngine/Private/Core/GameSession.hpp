#pragma once

#include "Raven/Audio/AudioPlayer.hpp"
#include "Raven/Core/Application.hpp"
#include "Raven/Core/GameSystems.hpp"
#include "Raven/Renderer/Vulkan/VulkanTexturedDraw.hpp"
#include "Raven/Renderer/Vulkan/VulkanTransparentDraw.hpp"
#include "Raven/Scene/SceneCommands.hpp"
#include "Raven/Scene/SceneProject.hpp"
#include "Raven/Scene/SceneSession.hpp"

#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Raven
{
	class GameInstance;
	class RenderWorld;
	class VulkanContext;
	struct InputState;
	struct FrameTime;

	// The engine's play session for one game root (docs/DataModelDesign.md, FD-12, FD-17). It owns the
	// project, the scene session, the deferred commands and the services game systems call, and
	// draws the active scene: the entity tagged ActiveCameraComponent, the scene's lighting, its
	// lit meshes from the render world and its sprites. Developer keys: F3 restarts the scene,
	// F4 opens the next one, F7 cycles the render view and F8 pauses or resumes play.
	class GameSession final : private GameServices
	{
	public:
		GameSession(VulkanContext& renderer, GameInstance& game, const ApplicationConfig& config);
		~GameSession();
		GameSession(const GameSession&) = delete;
		GameSession& operator=(const GameSession&) = delete;

		// Before gameplay. True when a new scene replaced the active one.
		bool Prepare(const InputState& input, const FrameTime& time);
		// Restarts the scene, opens the next one, or applies pending scene and project edits.
		bool Advance(bool restart = false, bool next = false);
		SceneSession& GetSession() noexcept { return *m_Session; }
		SceneCommands& GetCommands() noexcept { return m_Commands; }
		GameServices& GetServices() noexcept { return *this; }
		bool IsPlaying() const noexcept { return m_Playing; }
		Scene& GetScene() const noexcept;
		const AssetCatalog& GetCatalog() const noexcept;
		// The context for one phase; picks are those of the last presented frame.
		SystemContext MakeContext(const InputState& input, const FrameTime& time);
		// Applies deferred structural changes; called after each fixed step and after Update.
		void ApplyCommands();
		// True when the frame was presented.
		bool Render(VulkanContext& renderer, RenderWorld& world, const InputState& input);
		double GetTracePhase() const noexcept { return m_TracePhase; }

	private:
		void PlaySound(AssetRef<AudioAsset> sound) override;
		void StopSounds() noexcept override;
		bool HasOption(std::string_view name) const noexcept override;
		void SetTracePhase(double value) noexcept override { m_TracePhase = value; }

		void GatherSprites();
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
		bool m_Playing = true;
		double m_TracePhase = 0.0;
		bool m_MeshReloadAwaitingPresent = false;
		bool m_TextureReloadAwaitingPresent = false;
		std::chrono::steady_clock::time_point m_ReloadApplied{};
	};
}
