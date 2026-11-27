#pragma once

#include "Renderer/Vulkan/VulkanDevice.hpp"
#include "Renderer/Vulkan/VulkanMemory.hpp"
#include "Renderer/Vulkan/VulkanPhysicalDevice.hpp"

namespace Raven
{
	class VulkanDepthAttachment
	{
	public:
		static VkFormat SelectFormat(const VulkanPhysicalDevice& physicalDevice);

		VulkanDepthAttachment(const VulkanDevice& device, const VulkanPhysicalDevice& physicalDevice,
			VkExtent2D extent, VkFormat format);
		~VulkanDepthAttachment();

		VkImage GetImage() const { return m_Image; }
		VkImageView GetView() const { return m_View; }

		VulkanDepthAttachment(const VulkanDepthAttachment&) = delete;
		VulkanDepthAttachment& operator=(const VulkanDepthAttachment&) = delete;

	private:
		void Destroy() noexcept;

		VkDevice m_Device = VK_NULL_HANDLE;
		const VulkanMemory* m_Memory = nullptr;
		VkImage m_Image = VK_NULL_HANDLE;
		VulkanMemory::Allocation m_Allocation;
		VkImageView m_View = VK_NULL_HANDLE;
	};
}
