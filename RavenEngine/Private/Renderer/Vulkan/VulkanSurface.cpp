#include "Renderer/Vulkan/VulkanSurface.hpp"

#include "Renderer/Vulkan/VulkanInstance.hpp"
#include "Raven/Platform/Window.hpp"

namespace Raven
{
	VulkanSurface::VulkanSurface(
		const VulkanInstance& instance, const Window& window)
		: m_Instance(instance.Get()),
		  m_Surface(window.CreateVulkanSurface(m_Instance))
	{
	}

	VulkanSurface::~VulkanSurface()
	{
		if (m_Surface != VK_NULL_HANDLE)
			vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
	}
}