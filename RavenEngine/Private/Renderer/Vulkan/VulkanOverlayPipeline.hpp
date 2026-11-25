#pragma once

#include "Renderer/Vulkan/VulkanDevice.hpp"

#include <cstdint>
#include <span>

namespace Raven
{
	// The shared GPU 2D path, feed 1 (docs/UIDesign.md, section 4): OverlayQuad instances from the
	// frame's upload ring, drawn as triangle strips over the scene with straight alpha blending and no
	// depth test. Set 0 is the bindless texture array; the push constant is the framebuffer size.
	class VulkanOverlayPipeline
	{
	public:
		VulkanOverlayPipeline(const VulkanDevice& device, VkFormat colorFormat, VkFormat depthFormat,
			VkDescriptorSetLayout bindlessLayout, std::span<const std::uint32_t> vertexWords,
			std::span<const std::uint32_t> fragmentWords);
		~VulkanOverlayPipeline();

		// Draws `count` quads from `buffer` at `offset`.
		void Record(VkCommandBuffer commands, VkDescriptorSet bindless, VkBuffer buffer, VkDeviceSize offset,
			std::uint32_t count, VkExtent2D extent) const;

		VulkanOverlayPipeline(const VulkanOverlayPipeline&) = delete;
		VulkanOverlayPipeline& operator=(const VulkanOverlayPipeline&) = delete;

	private:
		void Destroy() noexcept;

		VkDevice m_Device = VK_NULL_HANDLE;
		VkPipelineLayout m_Layout = VK_NULL_HANDLE;
		VkPipeline m_Pipeline = VK_NULL_HANDLE;
	};
}
