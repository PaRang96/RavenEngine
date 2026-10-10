#pragma once

#include "Renderer/Vulkan/VulkanDevice.hpp"

#include <cstddef>
#include <span>

namespace Raven
{
	struct VulkanPhysicalDevice;

	class VulkanBuffer
	{
	public:
		VulkanBuffer(const VulkanDevice& device,
			const VulkanPhysicalDevice& physicalDevice,
			std::span<const std::byte> data, VkBufferUsageFlags usage);
		~VulkanBuffer();

		VkBuffer Get() const { return m_Buffer; }
		VkDeviceSize GetSize() const { return m_Size; }

		VulkanBuffer(const VulkanBuffer&) = delete;
		VulkanBuffer& operator=(const VulkanBuffer&) = delete;

	private:
		void Destroy() noexcept;

		// Device outlives this owner; GPU use completes before destruction.
		VkDevice m_Device = VK_NULL_HANDLE;
		VkBuffer m_Buffer = VK_NULL_HANDLE;
		VkDeviceMemory m_Memory = VK_NULL_HANDLE;
		VkDeviceSize m_Size = 0;
	};
}
