#pragma once

#include <vulkan/vulkan.h>

namespace Raven
{
	struct VulkanPhysicalDevice;

	// Extension commands loaded for this device (docs/RendererDesign.md, R2).
	struct VulkanDeviceCommands
	{
		PFN_vkCmdBeginRenderingKHR BeginRendering = nullptr;
		PFN_vkCmdEndRenderingKHR EndRendering = nullptr;
		PFN_vkCmdPipelineBarrier2KHR PipelineBarrier2 = nullptr;
	};

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
		const VulkanDeviceCommands& GetCommands() const { return m_Commands; }

	private:
		VkDevice m_Device = VK_NULL_HANDLE;
		VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
		VkQueue m_PresentQueue = VK_NULL_HANDLE;
		VulkanDeviceCommands m_Commands;
	};
}
