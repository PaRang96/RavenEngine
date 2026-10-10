#include "Renderer/Vulkan/VulkanQueueFamilies.hpp"
#include <stdexcept>
#include <string>
#include <vector>

namespace Raven
{
	VulkanQueueFamilies FindVulkanQueueFamilies(
		VkPhysicalDevice device, VkSurfaceKHR surface)
	{
		std::uint32_t count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);

		if (count == 0)
			return {};

		std::vector<VkQueueFamilyProperties> families(count);
		vkGetPhysicalDeviceQueueFamilyProperties(
			device, &count, families.data());

		VulkanQueueFamilies result{};

		for (std::uint32_t i = 0; i < count; ++i)
		{
			if (families[i].queueCount == 0)
				continue;

			const bool graphics =
				(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;

			VkBool32 present = VK_FALSE;
			const VkResult status = vkGetPhysicalDeviceSurfaceSupportKHR(
				device, i, surface, &present);

			if (status != VK_SUCCESS)
			{
				throw std::runtime_error(
					"Could not query Vulkan presentation support: " +
					std::to_string(status));
			}

			// Prefer one family that can perform both operations.
			if (graphics && present == VK_TRUE)
				return { i, i };

			if (graphics && !result.Graphics.has_value())
				result.Graphics = i;

			if (present == VK_TRUE && !result.Present.has_value())
				result.Present = i;
		}

		return result;
	}
}
