#include "Renderer/Vulkan/VulkanDepthAttachment.hpp"

#include <array>
#include <stdexcept>
#include <string>

namespace Raven
{
	namespace
	{
		void CheckDepthResult(VkResult result, const char* operation)
		{
			if (result != VK_SUCCESS)
				throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(result));
		}

	}

	VkFormat VulkanDepthAttachment::SelectFormat(const VulkanPhysicalDevice& physicalDevice)
	{
		// Depth-only formats avoid introducing a stencil contract into this first pass.
		constexpr std::array formats{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM };
		for (const auto format : formats)
		{
			VkFormatProperties properties{};
			vkGetPhysicalDeviceFormatProperties(physicalDevice.Handle, format, &properties);
			if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
				return format;
		}
		throw std::runtime_error("No supported depth attachment format");
	}

	VulkanDepthAttachment::VulkanDepthAttachment(const VulkanDevice& device,
		const VulkanPhysicalDevice& physicalDevice, VkExtent2D extent, VkFormat format)
		: m_Device(device.Get()), m_Memory(&device.GetMemory())
	{
		if (m_Device == VK_NULL_HANDLE || physicalDevice.Handle == VK_NULL_HANDLE ||
			extent.width == 0 || extent.height == 0 ||
			(format != VK_FORMAT_D32_SFLOAT && format != VK_FORMAT_D16_UNORM))
			throw std::invalid_argument("Invalid Vulkan depth attachment parameters");

		try
		{
			VkImageCreateInfo imageInfo{};
			imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageInfo.imageType = VK_IMAGE_TYPE_2D;
			imageInfo.format = format;
			imageInfo.extent = { extent.width, extent.height, 1 };
			imageInfo.mipLevels = 1;
			imageInfo.arrayLayers = 1;
			imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			// An attachment takes its own block (FD-24).
			m_Image = m_Memory->CreateImage(imageInfo, true, m_Allocation);

			VkImageViewCreateInfo viewInfo{};
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = m_Image;
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = format;
			viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			viewInfo.subresourceRange.levelCount = 1;
			viewInfo.subresourceRange.layerCount = 1;
			VkImageView view = VK_NULL_HANDLE;
			CheckDepthResult(vkCreateImageView(m_Device, &viewInfo, nullptr, &view), "vkCreateImageView (depth)");
			m_View = view;
		}
		catch (...)
		{
			Destroy();
			throw;
		}
	}

	VulkanDepthAttachment::~VulkanDepthAttachment()
	{
		Destroy();
	}

	void VulkanDepthAttachment::Destroy() noexcept
	{
		if (m_View != VK_NULL_HANDLE)
			vkDestroyImageView(m_Device, m_View, nullptr);
		if (m_Image != VK_NULL_HANDLE)
			m_Memory->DestroyImage(m_Image, m_Allocation);
	}
}
