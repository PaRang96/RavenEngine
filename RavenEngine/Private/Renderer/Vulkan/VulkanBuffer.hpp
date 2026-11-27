#pragma once

#include "Renderer/Vulkan/VulkanDevice.hpp"
#include "Renderer/Vulkan/VulkanMemory.hpp"

#include <cstddef>
#include <span>

namespace Raven
{
	struct VulkanPhysicalDevice;

	class VulkanBuffer
	{
	public:
		// Persistent buffers stay mapped for their lifetime (docs/RendererDesign.md, R4). Device-local
		// buffers are never mapped and are filled by transfer commands (R6).
		enum class Mapping
		{
			PerWrite,
			Persistent,
			DeviceLocal
		};

		VulkanBuffer(const VulkanDevice& device,
			const VulkanPhysicalDevice& physicalDevice,
			VkDeviceSize size, VkBufferUsageFlags usage, Mapping mapping = Mapping::PerWrite);
		VulkanBuffer(const VulkanDevice& device,
			const VulkanPhysicalDevice& physicalDevice,
			std::span<const std::byte> data, VkBufferUsageFlags usage);
		~VulkanBuffer();

		VkBuffer Get() const { return m_Buffer; }
		VkDeviceSize GetSize() const { return m_Size; }
		// Host-coherent mapped memory of a persistent buffer; null otherwise.
		std::byte* GetMapped() const noexcept { return m_Mapped; }

		// The caller completes all GPU use before overwriting this buffer.
		void Write(std::span<const std::byte> data);

		VulkanBuffer(const VulkanBuffer&) = delete;
		VulkanBuffer& operator=(const VulkanBuffer&) = delete;

	private:
		void Destroy() noexcept;

		// Device outlives this owner; GPU use completes before destruction.
		const VulkanMemory* m_Memory = nullptr;
		VkBuffer m_Buffer = VK_NULL_HANDLE;
		VulkanMemory::Allocation m_Allocation;
		VkDeviceSize m_Size = 0;
		std::byte* m_Mapped = nullptr;
		bool m_DeviceLocal = false;
	};
}
