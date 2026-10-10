#include "Renderer/Vulkan/VulkanPhysicalDevice.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

namespace Raven
{
	namespace
	{
		constexpr const char* PortabilitySubset =
			"VK_KHR_portability_subset";

		void Check(VkResult result, const char* operation)
		{
			if (result != VK_SUCCESS)
			{
				throw std::runtime_error(
					std::string(operation) + ": " +
					std::to_string(result));
			}
		}

		template<typename T, typename Query>
		std::vector<T> Enumerate(Query query, const char* operation)
		{
			std::vector<T> values;
			VkResult result = VK_SUCCESS;

			do
			{
				std::uint32_t count = 0;
				Check(query(&count, nullptr), operation);

				values.resize(count);
				if (count == 0)
					return values;

				result = query(&count, values.data());
				if (result != VK_INCOMPLETE)
					Check(result, operation);

				values.resize(count);
			}
			while (result == VK_INCOMPLETE);

			return values;
		}
	}

	VulkanPhysicalDevice SelectVulkanPhysicalDevice(
		VkInstance instance, VkSurfaceKHR surface)
	{
		const auto devices = Enumerate<VkPhysicalDevice>(
			[instance](std::uint32_t* count, VkPhysicalDevice* values)
			{
				return vkEnumeratePhysicalDevices(instance, count, values);
			},
			"Could not enumerate Vulkan physical devices");

		if (devices.empty())
			throw std::runtime_error("No Vulkan physical devices available");

		for (VkPhysicalDevice device : devices)
		{
			const auto queues = FindVulkanQueueFamilies(device, surface);
			if (!queues.IsComplete())
				continue;

			const auto extensions = Enumerate<VkExtensionProperties>(
				[device](std::uint32_t* count, VkExtensionProperties* values)
				{
					return vkEnumerateDeviceExtensionProperties(
						device, nullptr, count, values);
				},
				"Could not enumerate Vulkan device extensions");

			bool hasSwapchain = false;
			bool hasPortabilitySubset = false;

			for (const auto& extension : extensions)
			{
				if (std::strcmp(extension.extensionName,
					VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
					hasSwapchain = true;

				if (std::strcmp(extension.extensionName,
					PortabilitySubset) == 0)
					hasPortabilitySubset = true;
			}

			if (!hasSwapchain)
				continue;

			std::uint32_t formatCount = 0;
			Check(vkGetPhysicalDeviceSurfaceFormatsKHR(
				device, surface, &formatCount, nullptr),
				"Could not query Vulkan surface formats");

			std::uint32_t presentModeCount = 0;
			Check(vkGetPhysicalDeviceSurfacePresentModesKHR(
				device, surface, &presentModeCount, nullptr),
				"Could not query Vulkan presentation modes");

			if (formatCount == 0 || presentModeCount == 0)
				continue;

			VkSurfaceCapabilitiesKHR capabilities{};
			Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
				device, surface, &capabilities),
				"Could not query Vulkan surface capabilities");

			if ((capabilities.supportedUsageFlags &
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0)
				continue;

			VulkanPhysicalDevice selected{};
			selected.Handle = device;
			selected.QueueFamilies = queues;
			vkGetPhysicalDeviceProperties(device, &selected.Properties);

			selected.Extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
			if (hasPortabilitySubset)
				selected.Extensions.push_back(PortabilitySubset);

			return selected;
		}

		throw std::runtime_error(
			"No Vulkan GPU supports graphics, presentation, "
			"and swapchain rendering");
	}
}