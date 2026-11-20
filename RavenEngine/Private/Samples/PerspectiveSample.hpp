#pragma once

#include "SampleAssets.hpp"
#include "PerspectiveLevel.hpp"
#include "Raven/Scene/SceneSession.hpp"
#include "Raven/Audio/AudioPlayer.hpp"

#include "Raven/Core/ApplicationClient.hpp"
#include "Raven/ECS/Entity.hpp"
#include "Raven/Renderer/Vulkan/VulkanDraw.hpp"
#include "Raven/Renderer/Vulkan/VulkanTexturedDraw.hpp"
#include "Raven/Renderer/Vulkan/VulkanTransparentDraw.hpp"

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace Raven
{
	class VulkanContext;
	struct FrameTime;
	struct InputState;

	class PerspectiveSample final : public ApplicationClient
	{
	public:
		PerspectiveSample(VulkanContext& renderer, GameInstance& instance, bool freeFly, bool watchMesh,
			bool watchLevel = false, std::optional<AssetId> initialScene = {});
		~PerspectiveSample() override;
		GameBindings GetGameBindings() const noexcept override;
		bool PrepareFrame(const InputState& input, const FrameTime& frameTime) override;
		void OnFrame(VulkanContext& renderer,
			const InputState& input, const FrameTime& frameTime) override;
		void Update(const InputState& input, const FrameTime& frameTime);

		std::span<const VulkanDraw> GetDraws() const { return m_Draws; }
		std::span<const VulkanTexturedDraw> GetTexturedDraws() const { return m_TexturedDraws; }
		std::span<const VulkanTransparentDraw> GetTransparentDraws() const { return m_TransparentDraws; }

	private:
		PerspectiveLevel& GetLevel() const noexcept;
		void UpdateCamera(const InputState& input, const FrameTime& frameTime);
		void UpdateDraws();

		VulkanContext& m_Renderer;
		GameInstance& m_Game;
		const bool m_FreeFly;
		SampleAssets m_Assets;
		AudioPlayer m_Audio;
		std::unique_ptr<SceneSession> m_Session;
		std::vector<VulkanDraw> m_Draws;
		Scene::WorldTransformStorage m_DrawWorldTransforms;
		std::vector<ECS::Entity> m_PendingInteractions;
		std::vector<ECS::Entity> m_Interactions;
		ECS::Entity m_BenchmarkRotor;
		const VulkanTexturedMesh& m_Quad;
		std::array<VulkanTexturedDraw, 2> m_TexturedDraws;
		std::array<VulkanTransparentDraw, 2> m_TransparentDraws;
		bool m_CameraViewUnavailable = false;
		bool m_MeshReloadAwaitingPresent = false;
		double m_Angle = 0.0;
		bool m_Animate = true;
	};
}
