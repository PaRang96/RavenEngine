#pragma once

#include <vulkan/vulkan.h>

namespace Raven
{
	class Window;
	class VulkanInstance;

	class VulkanSurface
	{
	public:
		VulkanSurface(const VulkanInstance& instance, const Window& window);
		~VulkanSurface();

		VulkanSurface(const VulkanSurface&) = delete;
		VulkanSurface& operator=(const VulkanSurface&) = delete;

		VkSurfaceKHR Get() const { return m_Surface; }

	private:
		VkInstance m_Instance = VK_NULL_HANDLE;
		VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
	};
}