#pragma once

#include "SampleAssets.hpp"
#include "Raven/Scene/SceneSession.hpp"

#include "Raven/Audio/AudioPlayer.hpp"
#include "Raven/Core/ApplicationClient.hpp"
#include "Raven/Renderer/Vulkan/VulkanTexturedDraw.hpp"
#include "Raven/Renderer/Vulkan/VulkanTransparentDraw.hpp"

#include <chrono>
#include <cstddef>
#include <optional>
#include <memory>
#include <span>
#include <vector>

namespace Raven
{
	class OrthographicLevel;
	class VulkanContext;
	struct FrameTime;
	struct InputState;

	class OrthographicSample final : public ApplicationClient
	{
	public:
		OrthographicSample(VulkanContext& renderer, GameInstance& instance, bool watchWall = false, bool watchLevel = false);
		~OrthographicSample() override;
		GameBindings GetGameBindings() const noexcept override;
		bool UsesFixedGameplayStep() const noexcept override { return true; }
		bool PrepareFrame(const InputState& input, const FrameTime& frameTime) override;
		void OnGameplayStep(const GameUpdateResult& result) override;
		void OnFrame(VulkanContext& renderer,
			const InputState& input, const FrameTime& frameTime) override;

		std::span<const VulkanTexturedDraw> GetOpaqueDraws() const { return m_OpaqueDraws; }
		std::span<const VulkanTransparentDraw> GetTransparentDraws() const { return m_TransparentDraws; }

	private:
		OrthographicLevel& GetLevel() const noexcept;
		void Update(const InputState& input, const FrameTime& frameTime);
		void UpdateDraws();

		GameInstance& m_Game;
		SampleAssets m_Assets;
		AudioPlayer m_Audio;
		const VulkanTexturedMesh& m_Quad;
		std::vector<VulkanTexturedDraw> m_OpaqueDraws;
		std::vector<VulkanTransparentDraw> m_TransparentDraws;
		std::unique_ptr<SceneSession> m_Session;
		bool m_CameraViewUnavailable = false;
		std::optional<std::chrono::duration<double, std::milli>> m_WallUpload;
	};
}
