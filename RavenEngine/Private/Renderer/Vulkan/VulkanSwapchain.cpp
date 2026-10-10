#include "Renderer/Vulkan/VulkanSwapchain.hpp"
#include "Renderer/Vulkan/VulkanDevice.hpp"
#include "Renderer/Vulkan/VulkanPhysicalDevice.hpp"
#include "Raven/Platform/Window.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <string>

namespace Raven
{
	namespace
	{
		void CheckSwapchain(VkResult result, const char* operation)
		{
			if (result != VK_SUCCESS)
				throw std::runtime_error(
					std::string(operation) + ": " + std::to_string(result));
		}

		template<typename T, typename Query>
		std::vector<T> EnumerateSwapchainValues(Query query, const char* operation)
		{
			std::vector<T> values;
			VkResult result = VK_SUCCESS;
			do
			{
				std::uint32_t count = 0;
				CheckSwapchain(query(&count, nullptr), operation);
				values.resize(count);
				if (count == 0)
					return values;

				result = query(&count, values.data());
				if (result != VK_INCOMPLETE)
					CheckSwapchain(result, operation);
				values.resize(count);
			}
			while (result == VK_INCOMPLETE);
			return values;
		}

		VkExtent2D ChooseSwapchainExtent(
			const VkSurfaceCapabilitiesKHR& capabilities,
			const FramebufferState& framebuffer)
		{
			if (capabilities.currentExtent.width !=
				std::numeric_limits<std::uint32_t>::max())
				return capabilities.currentExtent;

			return {
				std::clamp(framebuffer.Width,
					capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
				std::clamp(framebuffer.Height,
					capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
			};
		}

		VkCompositeAlphaFlagBitsKHR ChooseSwapchainAlpha(VkCompositeAlphaFlagsKHR supported)
		{
			for (const auto mode : {
				VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
				VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
				VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
				VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR })
			{
				if ((supported & mode) != 0)
					return mode;
			}
			throw std::runtime_error("Surface has no supported composite alpha mode");
		}
	}

	VulkanSwapchain::VulkanSwapchain(const VulkanDevice& device,
		const VulkanPhysicalDevice& physicalDevice,
		VkSurfaceKHR surface, const FramebufferState& framebuffer)
		: m_Device(device.Get())
	{
		if (m_Device == VK_NULL_HANDLE || physicalDevice.Handle == VK_NULL_HANDLE ||
			surface == VK_NULL_HANDLE || !physicalDevice.QueueFamilies.IsComplete())
			throw std::runtime_error("Swapchain requires a device, surface, and complete queues");

		if (!framebuffer.IsDrawable())
			return;

		VkSurfaceCapabilitiesKHR capabilities{};
		CheckSwapchain(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
			physicalDevice.Handle, surface, &capabilities), "Query surface capabilities");

		m_Extent = ChooseSwapchainExtent(capabilities, framebuffer);
		if (m_Extent.width == 0 || m_Extent.height == 0)
			return;

		const auto formats = EnumerateSwapchainValues<VkSurfaceFormatKHR>(
			[&](std::uint32_t* count, VkSurfaceFormatKHR* values)
			{
				return vkGetPhysicalDeviceSurfaceFormatsKHR(
					physicalDevice.Handle, surface, count, values);
			}, "Query surface formats");

		const auto modes = EnumerateSwapchainValues<VkPresentModeKHR>(
			[&](std::uint32_t* count, VkPresentModeKHR* values)
			{
				return vkGetPhysicalDeviceSurfacePresentModesKHR(
					physicalDevice.Handle, surface, count, values);
			}, "Query presentation modes");

		if (formats.empty() ||
			std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_FIFO_KHR) == modes.end())
			throw std::runtime_error("Surface has no supported FIFO swapchain configuration");

		auto format = formats.front();
		for (const auto& candidate : formats)
		{
			if (candidate.format == VK_FORMAT_B8G8R8A8_SRGB &&
				candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				format = candidate;
				break;
			}
		}
		if (format.format == VK_FORMAT_UNDEFINED ||
			(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0)
			throw std::runtime_error("Surface does not support color attachment rendering");
		m_Format = format.format;

		std::uint32_t imageCount = capabilities.minImageCount;
		if (imageCount < std::numeric_limits<std::uint32_t>::max())
			++imageCount;
		if (capabilities.maxImageCount != 0)
			imageCount = std::min(imageCount, capabilities.maxImageCount);

		const std::array<std::uint32_t, 2> families{
			physicalDevice.QueueFamilies.Graphics.value(),
			physicalDevice.QueueFamilies.Present.value()
		};

		VkSwapchainCreateInfoKHR createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = surface;
		createInfo.minImageCount = imageCount;
		createInfo.imageFormat = m_Format;
		createInfo.imageColorSpace = format.colorSpace;
		createInfo.imageExtent = m_Extent;
		createInfo.imageArrayLayers = 1;
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (families[0] != families[1])
		{
			createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			createInfo.queueFamilyIndexCount = static_cast<std::uint32_t>(families.size());
			createInfo.pQueueFamilyIndices = families.data();
		}
		createInfo.preTransform = capabilities.currentTransform;
		createInfo.compositeAlpha = ChooseSwapchainAlpha(capabilities.supportedCompositeAlpha);
		createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
		createInfo.clipped = VK_TRUE;

		const VkResult result = vkCreateSwapchainKHR(
			m_Device, &createInfo, nullptr, &m_Swapchain);
		if (result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			m_Swapchain = VK_NULL_HANDLE;
			return;
		}
		CheckSwapchain(result, "Create swapchain");

		try
		{
			m_Images = EnumerateSwapchainValues<VkImage>(
				[&](std::uint32_t* count, VkImage* values)
				{
					return vkGetSwapchainImagesKHR(m_Device, m_Swapchain, count, values);
				}, "Query swapchain images");
			if (m_Images.empty())
				throw std::runtime_error("Swapchain returned no images");

			m_ImageViews.resize(m_Images.size(), VK_NULL_HANDLE);
			for (std::size_t i = 0; i < m_Images.size(); ++i)
			{
				VkImageViewCreateInfo viewInfo{};
				viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				viewInfo.image = m_Images[i];
				viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
				viewInfo.format = m_Format;
				viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				viewInfo.subresourceRange.levelCount = 1;
				viewInfo.subresourceRange.layerCount = 1;
				VkImageView view = VK_NULL_HANDLE;
				CheckSwapchain(vkCreateImageView(
					m_Device, &viewInfo, nullptr, &view), "Create swapchain image view");
				m_ImageViews[i] = view;
			}
		}
		catch (...)
		{
			Destroy();
			throw;
		}
	}

	VulkanSwapchain::~VulkanSwapchain()
	{
		Destroy();
	}

	void VulkanSwapchain::Destroy() noexcept
	{
		for (VkImageView view : m_ImageViews)
		{
			if (view != VK_NULL_HANDLE)
				vkDestroyImageView(m_Device, view, nullptr);
		}
		m_ImageViews.clear();
		m_Images.clear();
		if (m_Swapchain != VK_NULL_HANDLE)
		{
			vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
			m_Swapchain = VK_NULL_HANDLE;
		}
	}
}
