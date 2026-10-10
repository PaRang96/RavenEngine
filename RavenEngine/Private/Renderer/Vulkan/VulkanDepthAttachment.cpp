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

		std::uint32_t FindDepthMemory(VkPhysicalDevice physicalDevice, std::uint32_t allowedTypes)
		{
			VkPhysicalDeviceMemoryProperties properties{};
			vkGetPhysicalDeviceMemoryProperties(physicalDevice, &properties);
			for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index)
				if ((allowedTypes & (std::uint32_t{ 1 } << index)) != 0 &&
					(properties.memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
					return index;
			throw std::runtime_error("No compatible device-local depth memory");
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
		: m_Device(device.Get())
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
			VkImage image = VK_NULL_HANDLE;
			CheckDepthResult(vkCreateImage(m_Device, &imageInfo, nullptr, &image), "vkCreateImage (depth)");
			m_Image = image;

			VkMemoryRequirements requirements{};
			vkGetImageMemoryRequirements(m_Device, m_Image, &requirements);
			VkMemoryAllocateInfo allocation{};
			allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocation.allocationSize = requirements.size;
			allocation.memoryTypeIndex = FindDepthMemory(physicalDevice.Handle, requirements.memoryTypeBits);
			VkDeviceMemory memory = VK_NULL_HANDLE;
			CheckDepthResult(vkAllocateMemory(m_Device, &allocation, nullptr, &memory), "vkAllocateMemory (depth)");
			m_Memory = memory;
			CheckDepthResult(vkBindImageMemory(m_Device, m_Image, m_Memory, 0), "vkBindImageMemory (depth)");

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
			vkDestroyImage(m_Device, m_Image, nullptr);
		if (m_Memory != VK_NULL_HANDLE)
			vkFreeMemory(m_Device, m_Memory, nullptr);
	}
}
