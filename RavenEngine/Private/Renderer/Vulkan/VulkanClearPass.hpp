#pragma once

#include "Raven/Renderer/Vulkan/VulkanDraw.hpp"
#include "Raven/Renderer/Vulkan/VulkanTexturedDraw.hpp"

#include "Renderer/Vulkan/VulkanDevice.hpp"
#include "Renderer/Vulkan/VulkanBuffer.hpp"
#include "Renderer/Vulkan/VulkanMesh.hpp"
#include "Renderer/Vulkan/VulkanDepthAttachment.hpp"
#include "Renderer/Vulkan/VulkanSwapchain.hpp"
#include "Renderer/Vulkan/VulkanColorPipeline.hpp"
#include "Renderer/Vulkan/VulkanTexturePipeline.hpp"
#include "Renderer/Vulkan/VulkanDrawQueue.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <memory>
#include <span>
#include <vector>

namespace Raven
{
	class VulkanClearPass
	{
	public:
		VulkanClearPass(const VulkanDevice& device, const VulkanPhysicalDevice& physicalDevice,
			const VulkanSwapchain& swapchain,
			std::uint32_t graphicsFamily, VkDescriptorSetLayout textureLayout,
			const std::filesystem::path& shaderDirectory);
		~VulkanClearPass();

		// False requests swapchain recreation. The caller waits for idle before destruction.
		bool Draw(const VulkanSwapchain& swapchain, VkQueue graphics, VkQueue present,
			const VulkanDrawQueue& queue);

		VulkanClearPass(const VulkanClearPass&) = delete;
		VulkanClearPass& operator=(const VulkanClearPass&) = delete;

	private:
		struct Frame
		{
			VkCommandBuffer Commands = VK_NULL_HANDLE;
			VkSemaphore ImageAvailable = VK_NULL_HANDLE;
			VkFence InFlight = VK_NULL_HANDLE;
			std::unique_ptr<VulkanBuffer> TexturedInstances;
		};

		void UploadInstances(Frame& frame, std::span<const TexturedInstanceData> instances);
		void Record(VkCommandBuffer commands, std::uint32_t imageIndex,
			const VulkanDrawQueue& queue, VkBuffer instanceBuffer);
		void Destroy() noexcept;

		// These owners outlive the pass and its per-frame buffers.
		const VulkanDevice& m_DeviceOwner;
		const VulkanPhysicalDevice& m_PhysicalDevice;
		VkDevice m_Device = VK_NULL_HANDLE;
		VkRenderPass m_RenderPass = VK_NULL_HANDLE;
		std::optional<VulkanColorPipeline> m_Pipeline;
		std::optional<VulkanTexturePipeline> m_TexturePipeline;
		std::optional<VulkanTexturePipeline> m_TransparentPipeline;
		VkCommandPool m_CommandPool = VK_NULL_HANDLE;
		VkExtent2D m_Extent{};
		std::vector<VkFramebuffer> m_Framebuffers;
		// A separate depth image for each framebuffer avoids overlap between in-flight frames.
		std::vector<std::unique_ptr<VulkanDepthAttachment>> m_DepthAttachments;
		std::array<Frame, 2> m_Frames{};
		// Reacquiring an image makes its presentation semaphore safe to reuse.
		std::vector<VkSemaphore> m_RenderFinished;
		std::size_t m_FrameIndex = 0;
	};
}
