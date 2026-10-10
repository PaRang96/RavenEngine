#include "Renderer/Vulkan/VulkanDebugMessenger.hpp"

#include "Renderer/Vulkan/VulkanInstance.hpp"
#include "Raven/Core/Log.hpp"

#include <stdexcept>
#include <string>

namespace Raven
{
	namespace
	{
		VKAPI_ATTR VkBool32 VKAPI_CALL OnVulkanMessage(
			VkDebugUtilsMessageSeverityFlagBitsEXT severity,
			VkDebugUtilsMessageTypeFlagsEXT,
			const VkDebugUtilsMessengerCallbackDataEXT* data,
			void*)
		{
			if (data && data->pMessage)
			{
				const LogLevel level =
					(severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
					? LogLevel::Error : LogLevel::Warning;
				Log(level, data->pMessage);
			}
			return VK_FALSE;
		}
	}
	VulkanDebugMessenger::VulkanDebugMessenger(
		const VulkanInstance& instance)
		: m_Instance(instance.Get())
	{
		const auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
			vkGetInstanceProcAddr(m_Instance, "vkCreateDebugUtilsMessengerEXT"));
		m_Destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
			vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugUtilsMessengerEXT"));

		if (!create || !m_Destroy)
			throw std::runtime_error("Vulkan debug utils functions unavailable");

		VkDebugUtilsMessengerCreateInfoEXT info{};
		info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		info.messageSeverity =
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		info.messageType =
			VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		info.pfnUserCallback = OnVulkanMessage;

		const VkResult result =
			create(m_Instance, &info, nullptr, &m_Messenger);

		if (result != VK_SUCCESS)
			throw std::runtime_error(
				"Could not create Vulkan debug messenger: " +
				std::to_string(result));
	}

	Raven::VulkanDebugMessenger::~VulkanDebugMessenger()
	{
		if (m_Messenger != VK_NULL_HANDLE)
			m_Destroy(m_Instance, m_Messenger, nullptr);
	}
}
