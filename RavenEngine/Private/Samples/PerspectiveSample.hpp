#pragma once

#include "SampleAssets.hpp"

#include "Raven/Core/ApplicationClient.hpp"
#include "Raven/ECS/Entity.hpp"
#include "Raven/Renderer/PerspectiveCamera.hpp"
#include "Raven/Renderer/Vulkan/VulkanDraw.hpp"
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

	class PerspectiveSample final : public ApplicationClient
	{
	public:
		PerspectiveSample(VulkanContext& renderer, GameInstance& instance);
		~PerspectiveSample() override;
		void OnFrame(VulkanContext& renderer,
			const InputState& input, const FrameTime& frameTime) override;
		void Update(const InputState& input, const FrameTime& frameTime);

		const PerspectiveCamera& GetCamera() const { return m_Camera; }
		std::span<const VulkanDraw> GetDraws() const { return m_Draws; }
		std::span<const VulkanTexturedDraw> GetTexturedDraws() const { return m_TexturedDraws; }
		std::span<const VulkanTransparentDraw> GetTransparentDraws() const { return m_TransparentDraws; }

	private:
		void ResetCamera();
		void UpdateDraws();

		VulkanContext& m_Renderer;
		GameInstance& m_Game;
		SampleAssets m_Assets;
		const MeshHandle m_Cube;
		std::array<ECS::Entity, 3> m_MeshEntities{};
		std::vector<VulkanDraw> m_Draws;
		const VulkanTexturedMesh& m_Quad;
		const VulkanTexture& m_Texture;
		const VulkanTexture& m_AlternateTexture;
		std::array<VulkanTexturedDraw, 2> m_TexturedDraws;
		std::array<VulkanTransparentDraw, 2> m_TransparentDraws;
		PerspectiveCamera m_Camera;
		float m_Yaw = 0.0f;
		float m_Pitch = 0.0f;
		float m_Distance = 0.0f;
		double m_Angle = 0.0;
		bool m_Animate = true;
	};
}
