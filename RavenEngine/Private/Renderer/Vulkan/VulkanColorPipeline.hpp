#pragma once

#include "Renderer/Vulkan/VulkanDevice.hpp"
#include "Renderer/Vulkan/VulkanDrawConstants.hpp"

#include <cstddef>
#include <filesystem>

namespace Raven
{
	using ColorPushConstants = DrawPushConstants;

	class VulkanColorPipeline
	{
	public:
		VulkanColorPipeline(const VulkanDevice& device, VkRenderPass renderPass,
			const std::filesystem::path& shaderDirectory);
		~VulkanColorPipeline();

		VkPipeline Get() const { return m_Pipeline; }
		void Bind(VkCommandBuffer commands, const ColorPushConstants& constants) const;

		VulkanColorPipeline(const VulkanColorPipeline&) = delete;
		VulkanColorPipeline& operator=(const VulkanColorPipeline&) = delete;

	private:
		void Destroy() noexcept;

		VkDevice m_Device = VK_NULL_HANDLE;
		VkPipelineLayout m_Layout = VK_NULL_HANDLE;
		VkPipeline m_Pipeline = VK_NULL_HANDLE;
	};
}
