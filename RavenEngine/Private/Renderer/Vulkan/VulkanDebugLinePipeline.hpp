#pragma once

#include "Renderer/Vulkan/VulkanDevice.hpp"

#include <cstdint>
#include <span>

namespace Raven
{
	// Developer debug lines (docs/GameServicesDesign.md, FD-32 collider view): DebugLineVertex pairs
	// from the frame's upload ring as a line list, depth-tested against the scene without writing
	// depth. Set 0 is the frame uniform.
	class VulkanDebugLinePipeline
	{
	public:
		VulkanDebugLinePipeline(const VulkanDevice& device, VkFormat colorFormat, VkFormat depthFormat,
			VkDescriptorSetLayout frameLayout, std::span<const std::uint32_t> vertexWords,
			std::span<const std::uint32_t> fragmentWords);
		~VulkanDebugLinePipeline();

		// Draws `vertexCount` vertices from `buffer` at `offset`.
		void Record(VkCommandBuffer commands, VkDescriptorSet frame, VkBuffer buffer, VkDeviceSize offset,
			std::uint32_t vertexCount) const;

		VulkanDebugLinePipeline(const VulkanDebugLinePipeline&) = delete;
		VulkanDebugLinePipeline& operator=(const VulkanDebugLinePipeline&) = delete;

	private:
		void Destroy() noexcept;

		VkDevice m_Device = VK_NULL_HANDLE;
		VkPipelineLayout m_Layout = VK_NULL_HANDLE;
		VkPipeline m_Pipeline = VK_NULL_HANDLE;
	};
}
