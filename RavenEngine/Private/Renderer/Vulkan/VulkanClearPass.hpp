#pragma once

#include "Raven/Renderer/Vulkan/VulkanDraw.hpp"

#include "Renderer/Vulkan/VulkanDevice.hpp"
#include "Renderer/Vulkan/VulkanMesh.hpp"
#include "Renderer/Vulkan/VulkanDepthAttachment.hpp"
#include "Renderer/Vulkan/VulkanSwapchain.hpp"
#include "Renderer/Vulkan/VulkanColorPipeline.hpp"

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
			std::uint32_t graphicsFamily, const std::filesystem::path& shaderDirectory);
		~VulkanClearPass();

		// False requests swapchain recreation. The caller waits for idle before destruction.
		bool Draw(const VulkanSwapchain& swapchain, VkQueue graphics, VkQueue present,
			const Mat4& viewProjection, std::span<const VulkanDraw> draws);

		VulkanClearPass(const VulkanClearPass&) = delete;
		VulkanClearPass& operator=(const VulkanClearPass&) = delete;

	private:
		struct Frame
		{
			VkCommandBuffer Commands = VK_NULL_HANDLE;
			VkSemaphore ImageAvailable = VK_NULL_HANDLE;
			VkFence InFlight = VK_NULL_HANDLE;
		};

		void Record(VkCommandBuffer commands, std::uint32_t imageIndex,
			const Mat4& viewProjection, std::span<const VulkanDraw> draws);
		void Destroy() noexcept;

		VkDevice m_Device = VK_NULL_HANDLE;
		VkRenderPass m_RenderPass = VK_NULL_HANDLE;
		std::optional<VulkanColorPipeline> m_Pipeline;
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
