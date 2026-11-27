#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>

namespace Raven
{
	// GPU memory for buffers, textures and attachments (docs/RenderingContentDesign.md, FD-24): one
	// allocator sub-allocates them from large blocks, which removes the per-resource allocation cap
	// and its load hitches and gives the memory totals. It is AMD's Vulkan Memory Allocator; only
	// VulkanMemory.cpp includes it, and no other header names its types.
	class VulkanMemory
	{
	public:
		// Which memory a resource lives in; each matches the memory properties the renderer used before.
		enum class Access
		{
			// Device-local; filled by transfer commands.
			Device,
			// Host-visible and coherent, mapped on demand.
			Host,
			// Host-visible and coherent, mapped for the allocation's lifetime (rings, readback).
			HostMapped,
		};

		// One allocation; empty when its handle is null. Freed only through the owner that made it.
		struct Allocation
		{
			void* Handle = nullptr;
			std::byte* Mapped = nullptr;
		};

		struct Stats
		{
			std::size_t Allocations = 0;
			std::size_t Blocks = 0;
			VkDeviceSize AllocatedBytes = 0;
			VkDeviceSize BlockBytes = 0;
		};

		VulkanMemory(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device);
		~VulkanMemory();

		// Creates and binds; on failure nothing is left allocated and the call throws.
		VkBuffer CreateBuffer(const VkBufferCreateInfo& info, Access access, Allocation& allocation) const;
		// Attachments ask for their own block, which drivers place best.
		VkImage CreateImage(const VkImageCreateInfo& info, bool dedicated, Allocation& allocation) const;
		void DestroyBuffer(VkBuffer buffer, Allocation& allocation) const noexcept;
		void DestroyImage(VkImage image, Allocation& allocation) const noexcept;
		std::byte* Map(const Allocation& allocation) const;
		void Unmap(const Allocation& allocation) const noexcept;
		Stats GetStats() const noexcept;

		VulkanMemory(const VulkanMemory&) = delete;
		VulkanMemory& operator=(const VulkanMemory&) = delete;

	private:
		void* m_Allocator = nullptr;
	};
}
