#pragma once

#include <vulkan/vulkan.h>

namespace Raven
{
	class VulkanInstance;

	class VulkanDebugMessenger
	{
	public:
		explicit VulkanDebugMessenger(const VulkanInstance& instance);
		~VulkanDebugMessenger();

		VulkanDebugMessenger(const VulkanDebugMessenger&) = delete;
		VulkanDebugMessenger& operator=(const VulkanDebugMessenger&) = delete;

	private:
		VkInstance m_Instance = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT m_Messenger = VK_NULL_HANDLE;
		PFN_vkDestroyDebugUtilsMessengerEXT m_Destroy = nullptr;
	};
}