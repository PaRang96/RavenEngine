#pragma once

#include <cstdint>

#include "Raven/Renderer/RendererConfig.hpp"

#include "Renderer/Vulkan/VulkanQueueFamilies.hpp"

#include <vector>

namespace Raven
{
	// Elements of the bindless texture array (docs/GpuDrivenDesign.md, G4); selection rejects a
	// device whose descriptor limits cannot hold it.
	inline constexpr std::uint32_t BindlessTextureCapacity = 4096;

	struct VulkanPhysicalDevice
	{
		VkPhysicalDevice Handle = VK_NULL_HANDLE;
		VulkanQueueFamilies QueueFamilies{};
		VkPhysicalDeviceProperties Properties{};
		std::vector<const char*> Extensions;
		// Required in M3 and recorded at selection (docs/RendererDesign.md, R1).
		bool DescriptorIndexing = false;
		bool DrawIndirectCount = false;
		// Optional: line polygon mode for the wireframe debug view (docs/HotReloadDesign.md, H3).
		bool FillModeNonSolid = false;
	};

	VulkanPhysicalDevice SelectVulkanPhysicalDevice(
		VkInstance instance, VkSurfaceKHR surface, GpuPreference preference);
}
