#pragma once

#include "Renderer/Vulkan/VulkanDevice.hpp"

#include <cstdint>
#include <map>
#include <vector>

namespace Raven
{
	class VulkanTexture;

	// One immutable set per texture for the textured and sprite path, plus one element of a
	// bindless array that lit shaders index (docs/GpuDrivenDesign.md, G4). Pools grow in pages and
	// survive swapchain recreation.
	class VulkanTextureDescriptors
	{
	public:
		// Elements of the bindless array; the first texture created (the context's white) is element 0.
		static constexpr std::uint32_t BindlessCapacity = 4096;

		explicit VulkanTextureDescriptors(const VulkanDevice& device);
		~VulkanTextureDescriptors();

		VkDescriptorSetLayout GetLayout() const { return m_Layout; }
		// Set 0 of the lit pipeline: the bindless array and the shared sampler.
		VkDescriptorSetLayout GetBindlessLayout() const { return m_BindlessLayout; }
		VkDescriptorSet GetBindlessSet() const { return m_BindlessSet; }
		// Also assigns the texture its bindless element.
		VkDescriptorSet CreateBinding(VulkanTexture& texture);
		// The texture's frames have completed (R3), so its element can be reused.
		void ReleaseBinding(const VulkanTexture& texture) noexcept;
		std::size_t GetBindingCount() const noexcept { return m_Bindings.size(); }
		std::size_t GetPoolCount() const noexcept { return m_Pages.size(); }
		// Points target's set and element at contents' image. The caller completes earlier GPU use first.
		void WriteBinding(const VulkanTexture& target, const VulkanTexture& contents) const noexcept;

		VulkanTextureDescriptors(const VulkanTextureDescriptors&) = delete;
		VulkanTextureDescriptors& operator=(const VulkanTextureDescriptors&) = delete;

	private:
		struct Page
		{
			VkDescriptorPool Pool = VK_NULL_HANDLE;
			std::uint32_t Used = 0;
		};

		void AddPage();
		void WriteSet(VkDescriptorSet set, const VulkanTexture& contents) const noexcept;
		void WriteElement(std::uint32_t element, const VulkanTexture& contents) const noexcept;
		void Destroy() noexcept;

		static constexpr std::uint32_t SetsPerPage = 64;
		VkDevice m_Device = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_Layout = VK_NULL_HANDLE;
		std::vector<Page> m_Pages;
		std::map<VkDescriptorSet, std::size_t> m_Bindings;
		VkDescriptorSetLayout m_BindlessLayout = VK_NULL_HANDLE;
		VkDescriptorPool m_BindlessPool = VK_NULL_HANDLE;
		VkDescriptorSet m_BindlessSet = VK_NULL_HANDLE;
		// Every texture's sampler is linear and clamped, so one shared sampler matches them.
		VkSampler m_Sampler = VK_NULL_HANDLE;
		std::vector<std::uint32_t> m_FreeElements;
		std::uint32_t m_NextElement = 0;
	};
}
