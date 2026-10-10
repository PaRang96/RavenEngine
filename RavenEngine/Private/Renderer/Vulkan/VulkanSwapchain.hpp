#pragma once

#include <vector>
#include <vulkan/vulkan.h>

namespace Raven
{
	class VulkanDevice;
	struct VulkanPhysicalDevice;
	struct FramebufferState;

	class VulkanSwapchain
	{
	public:
		VulkanSwapchain(const VulkanDevice& device,
			const VulkanPhysicalDevice& physicalDevice,
			VkSurfaceKHR surface, const FramebufferState& framebuffer);
		~VulkanSwapchain();

		VulkanSwapchain(const VulkanSwapchain&) = delete;
		VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

		VkSwapchainKHR Get() const { return m_Swapchain; }
		VkFormat GetFormat() const { return m_Format; }
		VkExtent2D GetExtent() const { return m_Extent; }
		const std::vector<VkImage>& GetImages() const { return m_Images; }
		const std::vector<VkImageView>& GetImageViews() const { return m_ImageViews; }

	private:
		void Destroy() noexcept;

		VkDevice m_Device = VK_NULL_HANDLE;
		VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
		VkFormat m_Format = VK_FORMAT_UNDEFINED;
		VkExtent2D m_Extent{};
		// These images belong to the swapchain.
		std::vector<VkImage> m_Images;
		std::vector<VkImageView> m_ImageViews;
	};
}
