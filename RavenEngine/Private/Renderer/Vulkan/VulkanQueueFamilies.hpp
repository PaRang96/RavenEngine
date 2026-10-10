#pragma once

#include <cstdint>
#include <optional>
#include <vulkan/vulkan.h>

namespace Raven
{
	struct VulkanQueueFamilies
	{
		std::optional<std::uint32_t> Graphics;
		std::optional<std::uint32_t> Present;

		bool IsComplete() const
		{
			return Graphics.has_value() && Present.has_value();
		}
	};

	VulkanQueueFamilies FindVulkanQueueFamilies(
		VkPhysicalDevice device, VkSurfaceKHR surface);
}
