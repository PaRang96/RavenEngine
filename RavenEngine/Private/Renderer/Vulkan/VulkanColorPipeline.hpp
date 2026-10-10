#pragma once

#include "Renderer/Vulkan/VulkanDevice.hpp"
#include "Raven/Math/Transform.hpp"

#include <cstddef>
#include <filesystem>

namespace Raven
{
	struct ColorPushConstants
	{
		Mat4 ClipFromLocal;
		Vec4 Tint;
	};

	static_assert(std::is_standard_layout_v<ColorPushConstants>);
	static_assert(std::is_trivially_copyable_v<ColorPushConstants>);
	static_assert(sizeof(ColorPushConstants) == 80);
	static_assert(offsetof(ColorPushConstants, ClipFromLocal) == 0);
	static_assert(offsetof(ColorPushConstants, Tint) == 64);

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
