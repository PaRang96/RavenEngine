#pragma once

#include "Raven/Renderer/TextureDesc.hpp"
#include "Renderer/Vulkan/VulkanDevice.hpp"

namespace Raven
{
	struct VulkanPhysicalDevice;

	class VulkanTexture
	{
	public:
		VulkanTexture(const VulkanDevice& device, const VulkanPhysicalDevice& physicalDevice,
			const TextureDesc& description);
		~VulkanTexture();

		VkImage GetImage() const { return m_Image; }
		VkImageView GetView() const { return m_View; }
		VkSampler GetSampler() const { return m_Sampler; }
		VkFormat GetFormat() const { return m_Format; }
		VkExtent2D GetExtent() const { return m_Extent; }

		VulkanTexture(const VulkanTexture&) = delete;
		VulkanTexture& operator=(const VulkanTexture&) = delete;

	private:
		void Destroy() noexcept;

		// The context completes GPU use before destroying this owner and its device.
		VkDevice m_Device = VK_NULL_HANDLE;
		VkImage m_Image = VK_NULL_HANDLE;
		VkDeviceMemory m_Memory = VK_NULL_HANDLE;
		VkImageView m_View = VK_NULL_HANDLE;
		VkSampler m_Sampler = VK_NULL_HANDLE;
		VkFormat m_Format = VK_FORMAT_UNDEFINED;
		VkExtent2D m_Extent{};
	};
}
