#pragma once

#include "Renderer/Vulkan/VulkanDevice.hpp"

#include <cstdint>
#include <vector>

namespace Raven
{
	class VulkanTexture;

	// One immutable set per texture. Pools grow in pages and survive swapchain recreation.
	class VulkanTextureDescriptors
	{
	public:
		explicit VulkanTextureDescriptors(const VulkanDevice& device);
		~VulkanTextureDescriptors();

		VkDescriptorSetLayout GetLayout() const { return m_Layout; }
		VkDescriptorSet CreateBinding(const VulkanTexture& texture);

		VulkanTextureDescriptors(const VulkanTextureDescriptors&) = delete;
		VulkanTextureDescriptors& operator=(const VulkanTextureDescriptors&) = delete;

	private:
		struct Page
		{
			VkDescriptorPool Pool = VK_NULL_HANDLE;
			std::uint32_t Used = 0;
		};

		void AddPage();
		void Destroy() noexcept;

		static constexpr std::uint32_t SetsPerPage = 64;
		VkDevice m_Device = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_Layout = VK_NULL_HANDLE;
		std::vector<Page> m_Pages;
	};
}
