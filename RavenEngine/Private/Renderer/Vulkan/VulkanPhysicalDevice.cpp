#include "Renderer/Vulkan/VulkanPhysicalDevice.hpp"

#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

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

		struct FeatureSupport
		{
			bool TimelineSemaphore = false;
			bool DynamicRendering = false;
			bool Synchronization2 = false;
			bool DescriptorIndexing = false;
			bool DrawIndirectCount = false;
			bool MultiDrawIndirect = false;
			bool DrawIndirectFirstInstance = false;
			bool FillModeNonSolid = false;
		};

		// Extension feature structures are chained only when the extension is present.
		FeatureSupport QueryFeatures(VkPhysicalDevice device, bool dynamicRenderingExtension,
			bool synchronization2Extension)
		{
			VkPhysicalDeviceSynchronization2FeaturesKHR synchronization2{};
			synchronization2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
			VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRendering{};
			dynamicRendering.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
			VkPhysicalDeviceVulkan12Features vulkan12{};
			vulkan12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
			void* chain = nullptr;
			if (synchronization2Extension)
			{
				synchronization2.pNext = chain;
				chain = &synchronization2;
			}
			if (dynamicRenderingExtension)
			{
				dynamicRendering.pNext = chain;
				chain = &dynamicRendering;
			}
			vulkan12.pNext = chain;
			VkPhysicalDeviceFeatures2 features{};
			features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
			features.pNext = &vulkan12;
			vkGetPhysicalDeviceFeatures2(device, &features);
			return {
				vulkan12.timelineSemaphore == VK_TRUE,
				dynamicRendering.dynamicRendering == VK_TRUE,
				synchronization2.synchronization2 == VK_TRUE,
				// The bindless texture array (docs/GpuDrivenDesign.md, G4).
				vulkan12.descriptorIndexing == VK_TRUE && vulkan12.runtimeDescriptorArray == VK_TRUE &&
					vulkan12.descriptorBindingPartiallyBound == VK_TRUE &&
					vulkan12.shaderSampledImageArrayNonUniformIndexing == VK_TRUE &&
					vulkan12.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE &&
					vulkan12.descriptorBindingUpdateUnusedWhilePending == VK_TRUE,
				vulkan12.drawIndirectCount == VK_TRUE,
				// One fixed-count indirect draw whose commands start at their mesh's region (G5).
				features.features.multiDrawIndirect == VK_TRUE,
				features.features.drawIndirectFirstInstance == VK_TRUE,
				features.features.fillModeNonSolid == VK_TRUE,
			};
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

		// Devices that can present but miss a Vulkan 1.2 requirement, for the final error.
		std::vector<std::string> rejected;
		// Prefer a discrete GPU, then an integrated one; enumeration order breaks ties.
		// Order alone is no preference: on hybrid laptops it depends on the driver.
		const auto rank = [](VkPhysicalDeviceType type)
		{
			switch (type)
			{
			case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return 2;
			case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 1;
			default: return 0;
			}
		};
		std::optional<VulkanPhysicalDevice> best;

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
			bool hasDynamicRendering = false;
			bool hasSynchronization2 = false;

			for (const auto& extension : extensions)
			{
				if (std::strcmp(extension.extensionName,
					VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
					hasSwapchain = true;

				if (std::strcmp(extension.extensionName,
					PortabilitySubset) == 0)
					hasPortabilitySubset = true;

				if (std::strcmp(extension.extensionName,
					VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) == 0)
					hasDynamicRendering = true;

				if (std::strcmp(extension.extensionName,
					VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME) == 0)
					hasSynchronization2 = true;
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

			// docs/RendererDesign.md, R1: Vulkan 1.2 with timeline semaphores, plus dynamic
			// rendering and synchronization2.
			const std::string name = selected.Properties.deviceName;
			if (selected.Properties.apiVersion < VK_API_VERSION_1_2)
			{
				rejected.push_back(name + " supports only Vulkan " +
					std::to_string(VK_API_VERSION_MAJOR(selected.Properties.apiVersion)) + "." +
					std::to_string(VK_API_VERSION_MINOR(selected.Properties.apiVersion)));
				continue;
			}
			const auto support = QueryFeatures(device, hasDynamicRendering, hasSynchronization2);
			std::string missing;
			const auto require = [&missing](bool present, const char* feature)
			{
				if (!present)
					missing += (missing.empty() ? "" : ", ") + std::string(feature);
			};
			require(support.TimelineSemaphore, "timelineSemaphore");
			require(hasDynamicRendering && support.DynamicRendering, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
			require(hasSynchronization2 && support.Synchronization2, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
			require(support.DescriptorIndexing, "descriptor indexing for bindless textures");
			require(support.MultiDrawIndirect, "multiDrawIndirect");
			require(support.DrawIndirectFirstInstance, "drawIndirectFirstInstance");
			if (!missing.empty())
			{
				rejected.push_back(name + " lacks " + missing);
				continue;
			}

			selected.Extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
			selected.Extensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
			selected.Extensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
			if (hasPortabilitySubset)
				selected.Extensions.push_back(PortabilitySubset);
			selected.DescriptorIndexing = support.DescriptorIndexing;
			selected.DrawIndirectCount = support.DrawIndirectCount;
			selected.FillModeNonSolid = support.FillModeNonSolid;

			if (!best || rank(selected.Properties.deviceType) > rank(best->Properties.deviceType))
				best = std::move(selected);
		}

		if (best)
			return std::move(*best);

		if (!rejected.empty())
		{
			std::string message = "No Vulkan GPU meets RavenEngine's Vulkan 1.2 requirements:";
			for (const auto& reason : rejected)
				message += "\n  " + reason;
			throw std::runtime_error(message);
		}
		throw std::runtime_error(
			"No Vulkan GPU supports graphics with compute, presentation, "
			"and swapchain rendering");
	}
}