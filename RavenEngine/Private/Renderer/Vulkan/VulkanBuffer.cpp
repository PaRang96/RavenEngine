#include "Renderer/Vulkan/VulkanBuffer.hpp"

#include "Renderer/Vulkan/VulkanPhysicalDevice.hpp"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

namespace Raven
{
	namespace
	{
		void CheckBufferResult(VkResult result, const char* operation)
		{
			if (result != VK_SUCCESS)
				throw std::runtime_error(std::string(operation) +
					" failed: " + std::to_string(result));
		}

		std::uint32_t FindUploadMemoryType(VkPhysicalDevice physicalDevice,
			std::uint32_t allowedTypes)
		{
			VkPhysicalDeviceMemoryProperties properties{};
			vkGetPhysicalDeviceMemoryProperties(physicalDevice, &properties);

			constexpr VkMemoryPropertyFlags required =
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
				VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

			for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index)
			{
				const bool allowed =
					(allowedTypes & (std::uint32_t{ 1 } << index)) != 0;
				const auto flags = properties.memoryTypes[index].propertyFlags;

				if (allowed && (flags & required) == required)
					return index;
			}

			throw std::runtime_error("No compatible host-visible coherent buffer memory");
		}
	}

	VulkanBuffer::VulkanBuffer(const VulkanDevice& device,
		const VulkanPhysicalDevice& physicalDevice,
		std::span<const std::byte> data, VkBufferUsageFlags usage)
		: VulkanBuffer(device, physicalDevice, static_cast<VkDeviceSize>(data.size()), usage)
	{
		Write(data);
	}

	VulkanBuffer::VulkanBuffer(const VulkanDevice& device,
		const VulkanPhysicalDevice& physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage)
		: m_Device(device.Get()), m_Size(size)
	{
		if (m_Device == VK_NULL_HANDLE || physicalDevice.Handle == VK_NULL_HANDLE ||
			size == 0 || usage == 0)
			throw std::runtime_error("Invalid Vulkan buffer parameters");

		try
		{
			VkBufferCreateInfo bufferInfo{};
			bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufferInfo.size = m_Size;
			bufferInfo.usage = usage;
			bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VkBuffer buffer = VK_NULL_HANDLE;
			CheckBufferResult(vkCreateBuffer(m_Device, &bufferInfo, nullptr, &buffer),
				"vkCreateBuffer");
			m_Buffer = buffer;

			VkMemoryRequirements requirements{};
			vkGetBufferMemoryRequirements(m_Device, m_Buffer, &requirements);

			VkMemoryAllocateInfo allocation{};
			allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocation.allocationSize = requirements.size;
			allocation.memoryTypeIndex = FindUploadMemoryType(
				physicalDevice.Handle, requirements.memoryTypeBits);

			VkDeviceMemory memory = VK_NULL_HANDLE;
			CheckBufferResult(vkAllocateMemory(m_Device, &allocation, nullptr, &memory),
				"vkAllocateMemory");
			m_Memory = memory;

			CheckBufferResult(vkBindBufferMemory(m_Device, m_Buffer, m_Memory, 0),
				"vkBindBufferMemory");
		}
		catch (...)
		{
			Destroy();
			throw;
		}
	}

	VulkanBuffer::~VulkanBuffer()
	{
		Destroy();
	}

	void VulkanBuffer::Write(std::span<const std::byte> data)
	{
		if (data.size() > m_Size)
			throw std::invalid_argument("Buffer upload exceeds its capacity");
		if (data.empty())
			return;
		void* mapped = nullptr;
		CheckBufferResult(vkMapMemory(m_Device, m_Memory, 0,
			static_cast<VkDeviceSize>(data.size()), 0, &mapped), "vkMapMemory");
		std::memcpy(mapped, data.data(), data.size());
		vkUnmapMemory(m_Device, m_Memory);
	}

	void VulkanBuffer::Destroy() noexcept
	{
		if (m_Buffer != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(m_Device, m_Buffer, nullptr);
			m_Buffer = VK_NULL_HANDLE;
		}

		if (m_Memory != VK_NULL_HANDLE)
		{
			vkFreeMemory(m_Device, m_Memory, nullptr);
			m_Memory = VK_NULL_HANDLE;
		}
	}
}
