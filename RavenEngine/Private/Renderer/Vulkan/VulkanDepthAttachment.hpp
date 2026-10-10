#pragma once

#include "Renderer/Vulkan/VulkanDevice.hpp"
#include "Renderer/Vulkan/VulkanPhysicalDevice.hpp"

namespace Raven
{
	class VulkanDepthAttachment
	{
	public:
		static VkFormat SelectFormat(const VulkanPhysicalDevice& physicalDevice);

		VulkanDepthAttachment(const VulkanDevice& device, const VulkanPhysicalDevice& physicalDevice,
			VkExtent2D extent, VkFormat format);
		~VulkanDepthAttachment();

		VkImageView GetView() const { return m_View; }

		VulkanDepthAttachment(const VulkanDepthAttachment&) = delete;
		VulkanDepthAttachment& operator=(const VulkanDepthAttachment&) = delete;

	private:
		void Destroy() noexcept;

		VkDevice m_Device = VK_NULL_HANDLE;
		VkImage m_Image = VK_NULL_HANDLE;
		VkDeviceMemory m_Memory = VK_NULL_HANDLE;
		VkImageView m_View = VK_NULL_HANDLE;
	};
}
