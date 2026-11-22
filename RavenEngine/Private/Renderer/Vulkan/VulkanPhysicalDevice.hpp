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
		// Required in M3 and recorded at selection (docs/RendererDesign.md, R1).
		bool DescriptorIndexing = false;
		bool DrawIndirectCount = false;
	};

	VulkanPhysicalDevice SelectVulkanPhysicalDevice(
		VkInstance instance, VkSurfaceKHR surface);
}
