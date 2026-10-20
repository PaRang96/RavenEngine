#pragma once

#include "SampleAssets.hpp"

#include "Raven/Core/ApplicationClient.hpp"
#include "Raven/ECS/Entity.hpp"
#include "Raven/Renderer/OrthographicCamera.hpp"
#include "Raven/Renderer/Vulkan/VulkanTexturedDraw.hpp"
#include "Raven/Renderer/Vulkan/VulkanTransparentDraw.hpp"

#include <array>
#include <span>
#include <vector>

namespace Raven
{
	class VulkanContext;
	struct FrameTime;
	struct InputState;

	class OrthographicSample final : public ApplicationClient
	{
	public:
		OrthographicSample(VulkanContext& renderer, GameInstance& instance);
		~OrthographicSample() override;
		void OnFrame(VulkanContext& renderer,
			const InputState& input, const FrameTime& frameTime) override;

		std::span<const VulkanTexturedDraw> GetOpaqueDraws() const { return m_OpaqueDraws; }
		std::span<const VulkanTransparentDraw> GetTransparentDraws() const { return m_TransparentDraws; }

	private:
		void Update(const InputState& input, const FrameTime& frameTime);
		void SyncLegacyMovement();
		void UpdateDraws();

		GameInstance& m_Game;
		std::array<ECS::Entity, 5> m_SpriteEntities{};
		// Temporary adapter for the existing game DLL's Position/Velocity update.
		Vec3 m_PreviousGamePosition{};
		SampleAssets m_Assets;
		const VulkanTexturedMesh& m_Quad;
		std::vector<VulkanTexturedDraw> m_OpaqueDraws;
		std::vector<VulkanTransparentDraw> m_TransparentDraws;
		OrthographicCamera m_Camera;
	};
}
