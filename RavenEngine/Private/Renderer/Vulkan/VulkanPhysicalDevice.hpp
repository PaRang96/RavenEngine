#pragma once

#include "Renderer/Vulkan/VulkanQueueFamilies.hpp"

#include <vector>

namespace Raven
{
	struct VulkanPhysicalDevice
	{
		VkPhysicalDevice Handle = VK_NULL_HANDLE;
		VulkanQueueFamilies QueueFamilies{};
		VkPhysicalDeviceProperties Properties{};
		std::vector<const char*> Extensions;
	};

	VulkanPhysicalDevice SelectVulkanPhysicalDevice(
		VkInstance instance, VkSurfaceKHR surface);
}
