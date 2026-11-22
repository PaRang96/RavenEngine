#include "Renderer/Vulkan/VulkanDevice.hpp"
#include "Renderer/Vulkan/VulkanPhysicalDevice.hpp"

#include <array>
#include <stdexcept>
#include <string>

namespace Raven
{
	void CheckVulkanResult(VkResult result, const char* operation)
	{
		if (result == VK_SUCCESS)
			return;
		if (result == VK_ERROR_DEVICE_LOST)
			throw VulkanDeviceLostError(std::string("The GPU device was lost during ") + operation +
				" (driver reset, GPU fault or device removal). Rendering cannot continue; restart the application.");
		throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(result));
	}

	VulkanDevice::VulkanDevice(const VulkanPhysicalDevice& physicalDevice)
	{
		if (physicalDevice.Handle == VK_NULL_HANDLE ||
			!physicalDevice.QueueFamilies.IsComplete())
		{
			throw std::runtime_error(
				"Vulkan device requires a physical device "
				"and complete queue families");
		}

		const std::uint32_t graphicsFamily =
			physicalDevice.QueueFamilies.Graphics.value();
		const std::uint32_t presentFamily =
			physicalDevice.QueueFamilies.Present.value();

		const std::array<std::uint32_t, 2> families{
			graphicsFamily, presentFamily
		};

		const std::uint32_t queueInfoCount =
			graphicsFamily == presentFamily ? 1u : 2u;

		const float priority = 1.0f;
		std::array<VkDeviceQueueCreateInfo, 2> queueInfos{};

		for (std::uint32_t i = 0; i < queueInfoCount; ++i)
		{
			queueInfos[i].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueInfos[i].queueFamilyIndex = families[i];
			queueInfos[i].queueCount = 1;
			queueInfos[i].pQueuePriorities = &priority;
		}

		// Features required at selection (docs/RendererDesign.md, R1).
		VkPhysicalDeviceSynchronization2FeaturesKHR synchronization2{};
		synchronization2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
		synchronization2.synchronization2 = VK_TRUE;
		VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRendering{};
		dynamicRendering.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
		dynamicRendering.pNext = &synchronization2;
		dynamicRendering.dynamicRendering = VK_TRUE;
		VkPhysicalDeviceVulkan12Features vulkan12{};
		vulkan12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
		vulkan12.pNext = &dynamicRendering;
		vulkan12.timelineSemaphore = VK_TRUE;

		VkDeviceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.pNext = &vulkan12;
		createInfo.queueCreateInfoCount = queueInfoCount;
		createInfo.pQueueCreateInfos = queueInfos.data();
		createInfo.enabledExtensionCount =
			static_cast<std::uint32_t>(physicalDevice.Extensions.size());
		createInfo.ppEnabledExtensionNames = physicalDevice.Extensions.data();

		const VkResult result = vkCreateDevice(
			physicalDevice.Handle, &createInfo, nullptr, &m_Device);

		if (result != VK_SUCCESS)
		{
			throw std::runtime_error(
				"vkCreateDevice failed: " + std::to_string(result));
		}

		vkGetDeviceQueue(m_Device, graphicsFamily, 0, &m_GraphicsQueue);
		vkGetDeviceQueue(m_Device, presentFamily, 0, &m_PresentQueue);

		// The instance targets Vulkan 1.2, so these come from their KHR extensions.
		const auto load = [this](const char* name)
		{
			const auto function = vkGetDeviceProcAddr(m_Device, name);
			if (!function)
			{
				vkDestroyDevice(m_Device, nullptr);
				m_Device = VK_NULL_HANDLE;
				throw std::runtime_error(std::string("Vulkan device does not provide ") + name);
			}
			return function;
		};
		m_Commands.BeginRendering = reinterpret_cast<PFN_vkCmdBeginRenderingKHR>(load("vkCmdBeginRenderingKHR"));
		m_Commands.EndRendering = reinterpret_cast<PFN_vkCmdEndRenderingKHR>(load("vkCmdEndRenderingKHR"));
		m_Commands.PipelineBarrier2 = reinterpret_cast<PFN_vkCmdPipelineBarrier2KHR>(load("vkCmdPipelineBarrier2KHR"));
		m_Commands.QueueSubmit2 = reinterpret_cast<PFN_vkQueueSubmit2KHR>(load("vkQueueSubmit2KHR"));
	}

	VulkanDevice::~VulkanDevice()
	{
		if (m_Device != VK_NULL_HANDLE)
			vkDestroyDevice(m_Device, nullptr);
	}
}
