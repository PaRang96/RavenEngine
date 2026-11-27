#pragma once

#include "Raven/Renderer/TextureDesc.hpp"
#include "Renderer/Vulkan/VulkanDevice.hpp"
#include "Renderer/Vulkan/VulkanMemory.hpp"

namespace Raven
{
	struct VulkanPhysicalDevice;
	class VulkanUploadBatch;

	class VulkanTexture
	{
	public:
		// The contents are recorded into the upload batch, which keeps the staging buffer; the caller
		// keeps the texture alive in the batch too (B0 contract 5).
		// With mipmaps, a texture created from pixels gets its full mip chain, generated on the GPU in
		// the batch (FD-21); a cleared texture, or a format that cannot blit, keeps one level.
		VulkanTexture(const VulkanDevice& device, const VulkanPhysicalDevice& physicalDevice,
			const TextureDesc& description, VulkanUploadBatch& uploads, bool mipmaps = false);
		~VulkanTexture();

		VkImage GetImage() const { return m_Image; }
		VkDevice GetDevice() const { return m_Device; }
		VkDescriptorSet GetDescriptorSet() const { return m_DescriptorSet; }
		// A borrowed immutable binding owned by the context's descriptor pools.
		void SetDescriptorSet(VkDescriptorSet binding);
		// Same device, with all earlier GPU use completed. Each texture keeps its binding.
		void SwapImage(VulkanTexture& other) noexcept;
		// Element of the bindless array; kept by SwapImage like the descriptor set.
		std::uint32_t GetBindlessIndex() const noexcept { return m_BindlessIndex; }
		void SetBindlessIndex(std::uint32_t element) noexcept { m_BindlessIndex = element; }
		VkImageView GetView() const { return m_View; }
		VkFormat GetFormat() const { return m_Format; }
		VkExtent2D GetExtent() const { return m_Extent; }
		std::uint32_t GetLevels() const { return m_Levels; }

		VulkanTexture(const VulkanTexture&) = delete;
		VulkanTexture& operator=(const VulkanTexture&) = delete;

	private:
		void Destroy() noexcept;

		// The context completes GPU use before destroying this owner and its device.
		VkDevice m_Device = VK_NULL_HANDLE;
		const VulkanMemory* m_Memory = nullptr;
		VkImage m_Image = VK_NULL_HANDLE;
		VulkanMemory::Allocation m_Allocation;
		VkImageView m_View = VK_NULL_HANDLE;
		VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;
		std::uint32_t m_BindlessIndex = 0;
		VkFormat m_Format = VK_FORMAT_UNDEFINED;
		VkExtent2D m_Extent{};
		std::uint32_t m_Levels = 1;
	};
}
