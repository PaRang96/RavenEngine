#include "Renderer/Vulkan/VulkanDevice.hpp"
#include "Renderer/Vulkan/VulkanPhysicalDevice.hpp"

#include <array>
#include <stdexcept>
#include <string>

namespace Raven
{
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

		VkDeviceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
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
	}

	VulkanDevice::~VulkanDevice()
	{
		if (m_Device != VK_NULL_HANDLE)
			vkDestroyDevice(m_Device, nullptr);
	}
}
