#pragma once

#include "SampleAssets.hpp"

#include "Raven/Core/ApplicationClient.hpp"
#include "Raven/Renderer/OrthographicCamera.hpp"
#include "Raven/Renderer/Vulkan/VulkanTexturedDraw.hpp"
#include "Raven/Renderer/Vulkan/VulkanTransparentDraw.hpp"

#include <array>

namespace Raven
{
	class VulkanContext;
	struct FrameTime;
	struct InputState;

	class OrthographicSample final : public ApplicationClient
	{
	public:
		explicit OrthographicSample(VulkanContext& renderer);
		void OnFrame(VulkanContext& renderer,
			const InputState& input, const FrameTime& frameTime) override;

	private:
		void Update(const InputState& input, const FrameTime& frameTime);

		SampleAssets m_Assets;
		const VulkanTexturedMesh& m_Quad;
		const VulkanTexture& m_Texture;
		const VulkanTexture& m_AlternateTexture;
		std::array<VulkanTexturedDraw, 3> m_OpaqueDraws;
		std::array<VulkanTransparentDraw, 2> m_TransparentDraws;
		OrthographicCamera m_Camera;
	};
}
