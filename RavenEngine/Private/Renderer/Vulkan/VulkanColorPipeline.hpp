#pragma once

#include "Renderer/Vulkan/VulkanDevice.hpp"

#include <cstdint>
#include <span>

namespace Raven
{
	class VulkanColorPipeline
	{
	public:
		VulkanColorPipeline(const VulkanDevice& device, VkRenderPass renderPass,
			VkDescriptorSetLayout textureLayout, std::span<const std::uint32_t> vertexWords,
			std::span<const std::uint32_t> fragmentWords);
		~VulkanColorPipeline();

		VkPipeline Get() const { return m_Pipeline; }
		void Bind(VkCommandBuffer commands, VkDescriptorSet texture) const;

		VulkanColorPipeline(const VulkanColorPipeline&) = delete;
		VulkanColorPipeline& operator=(const VulkanColorPipeline&) = delete;

	private:
		void Destroy() noexcept;

		VkDevice m_Device = VK_NULL_HANDLE;
		VkPipelineLayout m_Layout = VK_NULL_HANDLE;
		VkPipeline m_Pipeline = VK_NULL_HANDLE;
	};
}
