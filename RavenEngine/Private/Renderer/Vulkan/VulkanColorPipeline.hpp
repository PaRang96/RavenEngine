#pragma once

#include "Renderer/Vulkan/VulkanDevice.hpp"

#include <cstdint>
#include <span>

namespace Raven
{
	class VulkanColorPipeline
	{
	public:
		// Lines draws triangle edges over the depth the filled pass wrote: the wireframe debug view
		// (docs/HotReloadDesign.md, H3). It needs the fillModeNonSolid device feature. Blend composites
		// straight alpha over the scene, depth-tested without writing depth: blended sprites (B0 contract 4).
		enum class Mode { Fill, Lines, Blend };

		// Set 0 is the bindless texture array (G4), set 1 the frame uniform (R4) and set 2 the GPU
		// scene (G5). Vertices come from the shared pool; objects come from the scene set.
		VulkanColorPipeline(const VulkanDevice& device, VkFormat colorFormat, VkFormat depthFormat,
			VkDescriptorSetLayout textureLayout, VkDescriptorSetLayout frameLayout, VkDescriptorSetLayout sceneLayout,
			std::span<const std::uint32_t> vertexWords, std::span<const std::uint32_t> fragmentWords,
			Mode mode = Mode::Fill, bool cutout = false);
		~VulkanColorPipeline();

		VkPipeline Get() const { return m_Pipeline; }
		void Bind(VkCommandBuffer commands, VkDescriptorSet texture) const;
		// Binding set 0 later leaves this set in place, since every lit pipeline shares the layout.
		void BindFrame(VkCommandBuffer commands, VkDescriptorSet frame) const;
		void BindScene(VkCommandBuffer commands, VkDescriptorSet scene) const;

		VulkanColorPipeline(const VulkanColorPipeline&) = delete;
		VulkanColorPipeline& operator=(const VulkanColorPipeline&) = delete;

	private:
		void Destroy() noexcept;

		VkDevice m_Device = VK_NULL_HANDLE;
		VkPipelineLayout m_Layout = VK_NULL_HANDLE;
		VkPipeline m_Pipeline = VK_NULL_HANDLE;
	};
}
