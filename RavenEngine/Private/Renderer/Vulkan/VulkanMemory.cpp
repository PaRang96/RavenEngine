#include "Renderer/Vulkan/VulkanMemory.hpp"

#include "Renderer/Vulkan/VulkanDevice.hpp"

// The one implementation of the vendored allocator (ThirdParty/VulkanMemoryAllocator, FD-24). The
// renderer links the Vulkan loader, so the allocator calls its exported functions directly, and it
// targets the instance's Vulkan 1.2.
#define VMA_STATIC_VULKAN_FUNCTIONS 1
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#define VMA_VULKAN_VERSION 1002000
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <stdexcept>

namespace Raven
{
	namespace
	{
		VmaAllocator Get(void* allocator) noexcept
		{
			return static_cast<VmaAllocator>(allocator);
		}

		VmaAllocationCreateInfo Describe(VulkanMemory::Access access)
		{
			VmaAllocationCreateInfo info{};
			info.usage = VMA_MEMORY_USAGE_UNKNOWN;
			if (access == VulkanMemory::Access::Device)
				info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			else
			{
				info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
				if (access == VulkanMemory::Access::HostMapped)
					info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
			}
			return info;
		}
	}

	VulkanMemory::VulkanMemory(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device)
	{
		VmaAllocatorCreateInfo info{};
		info.instance = instance;
		info.physicalDevice = physicalDevice;
		info.device = device;
		info.vulkanApiVersion = VK_API_VERSION_1_2;
		VmaAllocator allocator = VK_NULL_HANDLE;
		CheckVulkanResult(vmaCreateAllocator(&info, &allocator), "vmaCreateAllocator");
		m_Allocator = allocator;
	}

	VulkanMemory::~VulkanMemory()
	{
		vmaDestroyAllocator(Get(m_Allocator));
	}

	VkBuffer VulkanMemory::CreateBuffer(const VkBufferCreateInfo& info, Access access, Allocation& allocation) const
	{
		const auto create = Describe(access);
		VkBuffer buffer = VK_NULL_HANDLE;
		VmaAllocation handle = VK_NULL_HANDLE;
		VmaAllocationInfo result{};
		CheckVulkanResult(vmaCreateBuffer(Get(m_Allocator), &info, &create, &buffer, &handle, &result), "vmaCreateBuffer");
		allocation.Handle = handle;
		allocation.Mapped = static_cast<std::byte*>(result.pMappedData);
		return buffer;
	}

	VkImage VulkanMemory::CreateImage(const VkImageCreateInfo& info, bool dedicated, Allocation& allocation) const
	{
		auto create = Describe(Access::Device);
		if (dedicated)
			create.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
		VkImage image = VK_NULL_HANDLE;
		VmaAllocation handle = VK_NULL_HANDLE;
		CheckVulkanResult(vmaCreateImage(Get(m_Allocator), &info, &create, &image, &handle, nullptr), "vmaCreateImage");
		allocation.Handle = handle;
		allocation.Mapped = nullptr;
		return image;
	}

	void VulkanMemory::DestroyBuffer(VkBuffer buffer, Allocation& allocation) const noexcept
	{
		vmaDestroyBuffer(Get(m_Allocator), buffer, static_cast<VmaAllocation>(allocation.Handle));
		allocation = {};
	}

	void VulkanMemory::DestroyImage(VkImage image, Allocation& allocation) const noexcept
	{
		vmaDestroyImage(Get(m_Allocator), image, static_cast<VmaAllocation>(allocation.Handle));
		allocation = {};
	}

	std::byte* VulkanMemory::Map(const Allocation& allocation) const
	{
		void* mapped = nullptr;
		CheckVulkanResult(vmaMapMemory(Get(m_Allocator), static_cast<VmaAllocation>(allocation.Handle), &mapped),
			"vmaMapMemory");
		return static_cast<std::byte*>(mapped);
	}

	void VulkanMemory::Unmap(const Allocation& allocation) const noexcept
	{
		vmaUnmapMemory(Get(m_Allocator), static_cast<VmaAllocation>(allocation.Handle));
	}

	VulkanMemory::Stats VulkanMemory::GetStats() const noexcept
	{
		VmaTotalStatistics totals{};
		vmaCalculateStatistics(Get(m_Allocator), &totals);
		const auto& all = totals.total.statistics;
		return { all.allocationCount, all.blockCount, all.allocationBytes, all.blockBytes };
	}
}
