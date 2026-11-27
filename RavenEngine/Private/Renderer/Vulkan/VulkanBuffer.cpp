#include "Renderer/Vulkan/VulkanBuffer.hpp"

#include "Renderer/Vulkan/VulkanPhysicalDevice.hpp"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

namespace Raven
{
	VulkanBuffer::VulkanBuffer(const VulkanDevice& device,
		const VulkanPhysicalDevice& physicalDevice,
		std::span<const std::byte> data, VkBufferUsageFlags usage)
		: VulkanBuffer(device, physicalDevice, static_cast<VkDeviceSize>(data.size()), usage)
	{
		Write(data);
	}

	VulkanBuffer::VulkanBuffer(const VulkanDevice& device,
		const VulkanPhysicalDevice& physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage, Mapping mapping)
		: m_Memory(&device.GetMemory()), m_Size(size), m_DeviceLocal(mapping == Mapping::DeviceLocal)
	{
		if (device.Get() == VK_NULL_HANDLE || physicalDevice.Handle == VK_NULL_HANDLE ||
			size == 0 || usage == 0)
			throw std::runtime_error("Invalid Vulkan buffer parameters");
		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = m_Size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		const auto access = mapping == Mapping::DeviceLocal ? VulkanMemory::Access::Device
			: mapping == Mapping::Persistent ? VulkanMemory::Access::HostMapped : VulkanMemory::Access::Host;
		m_Buffer = m_Memory->CreateBuffer(bufferInfo, access, m_Allocation);
		m_Mapped = m_Allocation.Mapped;
	}

	VulkanBuffer::~VulkanBuffer()
	{
		Destroy();
	}

	void VulkanBuffer::Write(std::span<const std::byte> data)
	{
		if (data.size() > m_Size)
			throw std::invalid_argument("Buffer upload exceeds its capacity");
		if (m_DeviceLocal)
			throw std::logic_error("Device-local buffers are filled by transfer commands");
		if (data.empty())
			return;
		if (m_Mapped)
		{
			std::memcpy(m_Mapped, data.data(), data.size());
			return;
		}
		auto* mapped = m_Memory->Map(m_Allocation);
		std::memcpy(mapped, data.data(), data.size());
		m_Memory->Unmap(m_Allocation);
	}

	void VulkanBuffer::Destroy() noexcept
	{
		if (m_Buffer != VK_NULL_HANDLE)
			m_Memory->DestroyBuffer(m_Buffer, m_Allocation);
		m_Buffer = VK_NULL_HANDLE;
		m_Mapped = nullptr;
	}
}
