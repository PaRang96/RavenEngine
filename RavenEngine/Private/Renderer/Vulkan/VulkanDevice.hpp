#pragma once

#include <vulkan/vulkan.h>

namespace Raven
{
	struct VulkanPhysicalDevice;

	class VulkanDevice
	{
	public:
		explicit VulkanDevice(const VulkanPhysicalDevice& physicalDevice);
		~VulkanDevice();

		VulkanDevice(const VulkanDevice&) = delete;
		VulkanDevice& operator=(const VulkanDevice&) = delete;

		VkDevice Get() const { return m_Device; }
		VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
		VkQueue GetPresentQueue() const { return m_PresentQueue; }

	private:
		VkDevice m_Device = VK_NULL_HANDLE;
		VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
		VkQueue m_PresentQueue = VK_NULL_HANDLE;
	};
}
