#pragma once

#include <vulkan/vulkan.h>
#include <vector>

namespace Raven
{
	class VulkanInstance
	{
	public:
		explicit VulkanInstance(const std::vector<const char*> extensions);
		~VulkanInstance();

		VulkanInstance(const VulkanInstance&) = delete;
		VulkanInstance& operator=(const VulkanInstance&) = delete;

		VkInstance Get() const { return m_Instance; }

	private:
		VkInstance m_Instance = VK_NULL_HANDLE;
	};
}