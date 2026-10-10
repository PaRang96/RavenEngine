#pragma once

#include "Raven/Core/ApplicationClient.hpp"
#include "Raven/Renderer/PerspectiveCamera.hpp"
#include "Raven/Renderer/Vulkan/VulkanDraw.hpp"

#include <array>
#include <span>

namespace Raven
{
	class VulkanContext;
	struct FrameTime;
	struct InputState;

	class PerspectiveSample final : public ApplicationClient
	{
	public:
		explicit PerspectiveSample(VulkanContext& renderer);
		void OnFrame(VulkanContext& renderer,
			const InputState& input, const FrameTime& frameTime) override;
		void Update(const InputState& input, const FrameTime& frameTime);

		const PerspectiveCamera& GetCamera() const { return m_Camera; }
		std::span<const VulkanDraw> GetDraws() const { return m_Draws; }

	private:
		void ResetCamera();
		void UpdateDraws();

		const VulkanMesh& m_Cube;
		std::array<VulkanDraw, 3> m_Draws;
		PerspectiveCamera m_Camera;
		float m_Yaw = 0.0f;
		float m_Pitch = 0.0f;
		float m_Distance = 0.0f;
		double m_Angle = 0.0;
		bool m_Animate = true;
	};
}
