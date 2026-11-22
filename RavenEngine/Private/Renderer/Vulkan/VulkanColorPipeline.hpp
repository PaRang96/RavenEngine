#pragma once

#include "Renderer/Vulkan/VulkanDevice.hpp"

#include <cstdint>
#include <span>

namespace Raven
{
	class VulkanColorPipeline
	{
	public:
		// Set 0 is the texture, set 1 the frame uniform read by the vertex stage (R4).
		VulkanColorPipeline(const VulkanDevice& device, VkFormat colorFormat, VkFormat depthFormat,
			VkDescriptorSetLayout textureLayout, VkDescriptorSetLayout frameLayout,
			std::span<const std::uint32_t> vertexWords, std::span<const std::uint32_t> fragmentWords);
		~VulkanColorPipeline();

		VkPipeline Get() const { return m_Pipeline; }
		void Bind(VkCommandBuffer commands, VkDescriptorSet texture) const;
		// Binding set 0 later leaves this set in place, since every lit pipeline shares the layout.
		void BindFrame(VkCommandBuffer commands, VkDescriptorSet frame) const;

		VulkanColorPipeline(const VulkanColorPipeline&) = delete;
		VulkanColorPipeline& operator=(const VulkanColorPipeline&) = delete;

	private:
		void Destroy() noexcept;

		VkDevice m_Device = VK_NULL_HANDLE;
		VkPipelineLayout m_Layout = VK_NULL_HANDLE;
		VkPipeline m_Pipeline = VK_NULL_HANDLE;
	};
}
