#include "Renderer/Vulkan/VulkanTexture.hpp"

#include "Renderer/Vulkan/VulkanBuffer.hpp"
#include "Renderer/Vulkan/VulkanPhysicalDevice.hpp"
#include "Renderer/Vulkan/VulkanUploadBatch.hpp"
#include "Raven/Core/Log.hpp"

#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace Raven
{
	namespace
	{
		void CheckTextureResult(VkResult result, const char* operation)
		{
			CheckVulkanResult(result, operation);
		}
	}

	VulkanTexture::VulkanTexture(const VulkanDevice& device, const VulkanPhysicalDevice& physicalDevice,
		const TextureDesc& description, VulkanUploadBatch& uploads)
		: m_Device(device.Get()), m_Memory(&device.GetMemory()), m_Extent{ description.Width, description.Height }
	{
		if (m_Device == VK_NULL_HANDLE || physicalDevice.Handle == VK_NULL_HANDLE ||
			!physicalDevice.QueueFamilies.Graphics.has_value() || device.GetGraphicsQueue() == VK_NULL_HANDLE)
			throw std::invalid_argument("A device and graphics queue are required for texture creation");
		if (description.Width == 0 || description.Height == 0 ||
			description.Width > physicalDevice.Properties.limits.maxImageDimension2D ||
			description.Height > physicalDevice.Properties.limits.maxImageDimension2D)
			throw std::invalid_argument("Texture dimensions are zero or exceed maxImageDimension2D");
		const auto pixelCount = std::uint64_t{ description.Width } * description.Height;
		if (pixelCount > std::numeric_limits<std::size_t>::max() / 4 || (!description.Pixels.empty() &&
			(!description.Pixels.data() || description.Pixels.size() != static_cast<std::size_t>(pixelCount) * 4)))
			throw std::invalid_argument("Texture pixels must be empty or contain exactly Width * Height * 4 RGBA8 bytes");
		switch (description.ColorSpace)
		{
		case TextureColorSpace::Linear:
			m_Format = VK_FORMAT_R8G8B8A8_UNORM;
			break;
		case TextureColorSpace::SRGB:
			m_Format = VK_FORMAT_R8G8B8A8_SRGB;
			break;
		default:
			throw std::invalid_argument("Invalid texture color space");
		}

		VkFormatProperties formatProperties{};
		vkGetPhysicalDeviceFormatProperties(physicalDevice.Handle, m_Format, &formatProperties);
		constexpr VkFormatFeatureFlags required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
			VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
		if ((formatProperties.optimalTilingFeatures & required) != required)
			throw std::runtime_error("RGBA8 texture format does not support sampling with linear filtering");
		constexpr VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT |
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		VkImageFormatProperties imageProperties{};
		CheckTextureResult(vkGetPhysicalDeviceImageFormatProperties(physicalDevice.Handle, m_Format,
			VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0, &imageProperties),
			"vkGetPhysicalDeviceImageFormatProperties (texture)");
		if (description.Width > imageProperties.maxExtent.width ||
			description.Height > imageProperties.maxExtent.height)
			throw std::invalid_argument("Texture dimensions exceed the selected image format limits");

		try
		{
			VkImageCreateInfo imageInfo{};
			imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageInfo.imageType = VK_IMAGE_TYPE_2D;
			imageInfo.format = m_Format;
			imageInfo.extent = { m_Extent.width, m_Extent.height, 1 };
			imageInfo.mipLevels = 1;
			imageInfo.arrayLayers = 1;
			imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageInfo.usage = usage;
			imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			m_Image = m_Memory->CreateImage(imageInfo, false, m_Allocation);

			VkImageViewCreateInfo viewInfo{};
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = m_Image;
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = m_Format;
			viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			viewInfo.subresourceRange.levelCount = 1;
			viewInfo.subresourceRange.layerCount = 1;
			VkImageView view = VK_NULL_HANDLE;
			CheckTextureResult(vkCreateImageView(m_Device, &viewInfo, nullptr, &view), "vkCreateImageView (texture)");
			m_View = view;

			// Everything that can fail comes before the first recorded command, so a failure never
			// leaves the batch naming a destroyed image.
			std::shared_ptr<const VulkanBuffer> staging;
			if (!description.Pixels.empty())
			{
				staging = std::make_shared<const VulkanBuffer>(device, physicalDevice, description.Pixels,
					VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
				uploads.Keep(staging);
			}
			const auto commands = uploads.Record();
			const auto barrier2 = device.GetCommands().PipelineBarrier2;
			VkImageMemoryBarrier2KHR barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR;
			barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE_KHR;
			barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT_KHR;
			barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR;
			barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = m_Image;
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.layerCount = 1;
			VkDependencyInfoKHR dependency{};
			dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR;
			dependency.imageMemoryBarrierCount = 1;
			dependency.pImageMemoryBarriers = &barrier;
			barrier2(commands, &dependency);

			if (staging)
			{
				VkBufferImageCopy copy{};
				copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				copy.imageSubresource.layerCount = 1;
				copy.imageExtent = imageInfo.extent;
				vkCmdCopyBufferToImage(commands, staging->Get(), m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
			}
			else
			{
				// Created cleared: the GPU writes transparent black, and no zero buffer is sent.
				const VkClearColorValue clear{};
				vkCmdClearColorImage(commands, m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1,
					&barrier.subresourceRange);
			}

			// The batch's closing barrier makes the writes visible; this one sets the sampled layout.
			barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT_KHR;
			barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR;
			barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR;
			barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT_KHR;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier2(commands, &dependency);
		}
		catch (...)
		{
			Destroy();
			throw;
		}
	}

	void VulkanTexture::SetDescriptorSet(VkDescriptorSet binding)
	{
		if (binding == VK_NULL_HANDLE || m_DescriptorSet != VK_NULL_HANDLE)
			throw std::invalid_argument("A texture binding must be valid and assigned only once");
		m_DescriptorSet = binding;
	}

	void VulkanTexture::SwapImage(VulkanTexture& other) noexcept
	{
		using std::swap;
		swap(m_Image, other.m_Image);
		swap(m_Allocation, other.m_Allocation);
		swap(m_View, other.m_View);
		swap(m_Format, other.m_Format);
		swap(m_Extent, other.m_Extent);
	}

	VulkanTexture::~VulkanTexture()
	{
		Destroy();
	}

	void VulkanTexture::Destroy() noexcept
	{
		if (m_View != VK_NULL_HANDLE)
		{
			vkDestroyImageView(m_Device, m_View, nullptr);
			m_View = VK_NULL_HANDLE;
		}
		if (m_Image != VK_NULL_HANDLE)
		{
			m_Memory->DestroyImage(m_Image, m_Allocation);
			m_Image = VK_NULL_HANDLE;
		}
	}
}
