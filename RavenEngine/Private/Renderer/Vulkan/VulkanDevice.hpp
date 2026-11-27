#pragma once

#include <vulkan/vulkan.h>

#include <memory>
#include <stdexcept>

namespace Raven
{
	// VK_ERROR_DEVICE_LOST from any submit, present or wait (docs/RendererDesign.md, R7). The context
	// cannot recover, so the error propagates to the application's fatal handler with one message.
	class VulkanDeviceLostError : public std::runtime_error
	{
	public:
		using std::runtime_error::runtime_error;
	};

	// Throws VulkanDeviceLostError for device loss and std::runtime_error for any other failure.
	void CheckVulkanResult(VkResult result, const char* operation);

	struct VulkanPhysicalDevice;
	class VulkanMemory;

	// Extension commands loaded for this device (docs/RendererDesign.md, R2 and R3).
	struct VulkanDeviceCommands
	{
		PFN_vkCmdBeginRenderingKHR BeginRendering = nullptr;
		PFN_vkCmdEndRenderingKHR EndRendering = nullptr;
		PFN_vkCmdPipelineBarrier2KHR PipelineBarrier2 = nullptr;
		PFN_vkQueueSubmit2KHR QueueSubmit2 = nullptr;
	};

	class VulkanDevice
	{
	public:
		VulkanDevice(const VulkanPhysicalDevice& physicalDevice, VkInstance instance);
		~VulkanDevice();

		VulkanDevice(const VulkanDevice&) = delete;
		VulkanDevice& operator=(const VulkanDevice&) = delete;

		VkDevice Get() const { return m_Device; }
		VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
		VkQueue GetPresentQueue() const { return m_PresentQueue; }
		const VulkanDeviceCommands& GetCommands() const { return m_Commands; }
		// GPU memory for every buffer, texture and attachment of this device (FD-24).
		const VulkanMemory& GetMemory() const { return *m_Memory; }

	private:
		VkDevice m_Device = VK_NULL_HANDLE;
		VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
		VkQueue m_PresentQueue = VK_NULL_HANDLE;
		VulkanDeviceCommands m_Commands;
		// Released before the device, after every allocation.
		std::unique_ptr<VulkanMemory> m_Memory;
	};
}
