#pragma once

#include "Renderer/Vulkan/VulkanDevice.hpp"

#include <filesystem>

namespace Raven
{
	enum class TexturePipelineMode
	{
		Opaque,
		StraightAlpha
	};

	class VulkanTexturePipeline
	{
	public:
		VulkanTexturePipeline(const VulkanDevice& device, VkRenderPass renderPass,
			VkDescriptorSetLayout textureLayout, const std::filesystem::path& shaderDirectory,
			TexturePipelineMode mode = TexturePipelineMode::Opaque);
		~VulkanTexturePipeline();

		void Bind(VkCommandBuffer commands, VkDescriptorSet texture) const;

		VulkanTexturePipeline(const VulkanTexturePipeline&) = delete;
		VulkanTexturePipeline& operator=(const VulkanTexturePipeline&) = delete;

	private:
		void Destroy() noexcept;

		VkDevice m_Device = VK_NULL_HANDLE;
		VkPipelineLayout m_Layout = VK_NULL_HANDLE;
		VkPipeline m_Pipeline = VK_NULL_HANDLE;
	};
}
