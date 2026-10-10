#include "Renderer/Vulkan/VulkanTexture.hpp"

#include "Renderer/Vulkan/VulkanBuffer.hpp"
#include "Renderer/Vulkan/VulkanPhysicalDevice.hpp"
#include "Raven/Core/Log.hpp"

#include <limits>
#include <stdexcept>
#include <string>

namespace Raven
{
	namespace
	{
		void CheckTextureResult(VkResult result, const char* operation)
		{
			if (result != VK_SUCCESS)
				throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(result));
		}

		std::uint32_t FindTextureMemory(VkPhysicalDevice physicalDevice, std::uint32_t allowedTypes)
		{
			VkPhysicalDeviceMemoryProperties properties{};
			vkGetPhysicalDeviceMemoryProperties(physicalDevice, &properties);
			for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index)
				if ((allowedTypes & (std::uint32_t{ 1 } << index)) != 0 &&
					(properties.memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
					return index;
			throw std::runtime_error("No compatible device-local texture memory");
		}

		class TextureUpload
		{
		public:
			TextureUpload(VkDevice device, VkQueue queue, std::uint32_t family)
				: m_Device(device), m_Queue(queue)
			{
				try
				{
					VkCommandPoolCreateInfo poolInfo{};
					poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
					poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
					poolInfo.queueFamilyIndex = family;
					VkCommandPool pool = VK_NULL_HANDLE;
					CheckTextureResult(vkCreateCommandPool(m_Device, &poolInfo, nullptr, &pool),
						"vkCreateCommandPool (texture upload)");
					m_Pool = pool;

					VkCommandBufferAllocateInfo allocation{};
					allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
					allocation.commandPool = m_Pool;
					allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
					allocation.commandBufferCount = 1;
					CheckTextureResult(vkAllocateCommandBuffers(m_Device, &allocation, &m_CommandBuffer),
						"vkAllocateCommandBuffers (texture upload)");
					VkFenceCreateInfo fenceInfo{};
					fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
					VkFence fence = VK_NULL_HANDLE;
					CheckTextureResult(vkCreateFence(m_Device, &fenceInfo, nullptr, &fence),
						"vkCreateFence (texture upload)");
					m_Fence = fence;
					VkCommandBufferBeginInfo beginInfo{};
					beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
					beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
					CheckTextureResult(vkBeginCommandBuffer(m_CommandBuffer, &beginInfo),
						"vkBeginCommandBuffer (texture upload)");
				}
				catch (...)
				{
					Destroy();
					throw;
				}
			}

			~TextureUpload() { Destroy(); }
			TextureUpload(const TextureUpload&) = delete;
			TextureUpload& operator=(const TextureUpload&) = delete;

			VkCommandBuffer Get() const { return m_CommandBuffer; }

			void SubmitAndWait()
			{
				CheckTextureResult(vkEndCommandBuffer(m_CommandBuffer), "vkEndCommandBuffer (texture upload)");
				VkSubmitInfo submission{};
				submission.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
				submission.commandBufferCount = 1;
				submission.pCommandBuffers = &m_CommandBuffer;
				CheckTextureResult(vkQueueSubmit(m_Queue, 1, &submission, m_Fence), "vkQueueSubmit (texture upload)");
				m_Submitted = true;
				const VkResult result = vkWaitForFences(m_Device, 1, &m_Fence, VK_TRUE,
					std::numeric_limits<std::uint64_t>::max());
				if (result == VK_SUCCESS)
					m_Submitted = false;
				CheckTextureResult(result, "vkWaitForFences (texture upload)");
			}

		private:
			void Destroy() noexcept
			{
				// A failed fence wait must not release a pending command pool or staging buffer.
				if (m_Submitted)
				{
					const VkResult result = vkQueueWaitIdle(m_Queue);
					if (result != VK_SUCCESS)
						Log(LogLevel::Error, "vkQueueWaitIdle failed during texture upload cleanup: " +
							std::to_string(result));
					m_Submitted = false;
				}
				if (m_Fence != VK_NULL_HANDLE)
					vkDestroyFence(m_Device, m_Fence, nullptr);
				if (m_Pool != VK_NULL_HANDLE)
					vkDestroyCommandPool(m_Device, m_Pool, nullptr);
			}

			VkDevice m_Device;
			VkQueue m_Queue;
			VkCommandPool m_Pool = VK_NULL_HANDLE;
			VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
			VkFence m_Fence = VK_NULL_HANDLE;
			bool m_Submitted = false;
		};
	}

	VulkanTexture::VulkanTexture(const VulkanDevice& device, const VulkanPhysicalDevice& physicalDevice,
		const TextureDesc& description)
		: m_Device(device.Get()), m_Extent{ description.Width, description.Height }
	{
		if (m_Device == VK_NULL_HANDLE || physicalDevice.Handle == VK_NULL_HANDLE ||
			!physicalDevice.QueueFamilies.Graphics.has_value() || device.GetGraphicsQueue() == VK_NULL_HANDLE)
			throw std::invalid_argument("A device and graphics queue are required for texture creation");
		if (description.Width == 0 || description.Height == 0 ||
			description.Width > physicalDevice.Properties.limits.maxImageDimension2D ||
			description.Height > physicalDevice.Properties.limits.maxImageDimension2D)
			throw std::invalid_argument("Texture dimensions are zero or exceed maxImageDimension2D");
		const auto pixelCount = std::uint64_t{ description.Width } * description.Height;
		if (pixelCount > std::numeric_limits<std::size_t>::max() / 4 ||
			!description.Pixels.data() || description.Pixels.size() != static_cast<std::size_t>(pixelCount) * 4)
			throw std::invalid_argument("Texture pixels must contain exactly Width * Height * 4 RGBA8 bytes");
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
			VkImage image = VK_NULL_HANDLE;
			CheckTextureResult(vkCreateImage(m_Device, &imageInfo, nullptr, &image), "vkCreateImage (texture)");
			m_Image = image;

			VkMemoryRequirements requirements{};
			vkGetImageMemoryRequirements(m_Device, m_Image, &requirements);
			VkMemoryAllocateInfo allocation{};
			allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocation.allocationSize = requirements.size;
			allocation.memoryTypeIndex = FindTextureMemory(physicalDevice.Handle, requirements.memoryTypeBits);
			VkDeviceMemory memory = VK_NULL_HANDLE;
			CheckTextureResult(vkAllocateMemory(m_Device, &allocation, nullptr, &memory),
				"vkAllocateMemory (texture)");
			m_Memory = memory;
			CheckTextureResult(vkBindImageMemory(m_Device, m_Image, m_Memory, 0), "vkBindImageMemory (texture)");

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

			VkSamplerCreateInfo samplerInfo{};
			samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			samplerInfo.magFilter = VK_FILTER_LINEAR;
			samplerInfo.minFilter = VK_FILTER_LINEAR;
			samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerInfo.maxLod = 0.0f;
			VkSampler sampler = VK_NULL_HANDLE;
			CheckTextureResult(vkCreateSampler(m_Device, &samplerInfo, nullptr, &sampler), "vkCreateSampler (texture)");
			m_Sampler = sampler;

			const VulkanBuffer staging(device, physicalDevice, description.Pixels, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
			TextureUpload upload(m_Device, device.GetGraphicsQueue(), physicalDevice.QueueFamilies.Graphics.value());
			VkImageMemoryBarrier barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = m_Image;
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.layerCount = 1;
			vkCmdPipelineBarrier(upload.Get(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);

			VkBufferImageCopy copy{};
			copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copy.imageSubresource.layerCount = 1;
			copy.imageExtent = imageInfo.extent;
			vkCmdCopyBufferToImage(upload.Get(), staging.Get(), m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(upload.Get(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);
			upload.SubmitAndWait();
		}
		catch (...)
		{
			Destroy();
			throw;
		}
	}

	VulkanTexture::~VulkanTexture()
	{
		Destroy();
	}

	void VulkanTexture::Destroy() noexcept
	{
		if (m_Sampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(m_Device, m_Sampler, nullptr);
			m_Sampler = VK_NULL_HANDLE;
		}
		if (m_View != VK_NULL_HANDLE)
		{
			vkDestroyImageView(m_Device, m_View, nullptr);
			m_View = VK_NULL_HANDLE;
		}
		if (m_Image != VK_NULL_HANDLE)
		{
			vkDestroyImage(m_Device, m_Image, nullptr);
			m_Image = VK_NULL_HANDLE;
		}
		if (m_Memory != VK_NULL_HANDLE)
		{
			vkFreeMemory(m_Device, m_Memory, nullptr);
			m_Memory = VK_NULL_HANDLE;
		}
	}
}
