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
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace Raven
{
	struct FrameMetrics;

	class VulkanClearPass
	{
		struct RenderPass;

	public:
		class LitPipelineFactory
		{
		public:
			std::shared_ptr<const VulkanColorPipeline> Create(std::span<const std::uint32_t> vertex,
				std::span<const std::uint32_t> fragment) const;

		private:
			friend class VulkanClearPass;
			LitPipelineFactory(const VulkanDevice& device, std::shared_ptr<const RenderPass> pass,
				VkDescriptorSetLayout textureLayout);
			const VulkanDevice& m_Device;
			std::shared_ptr<const RenderPass> m_Pass;
			VkDescriptorSetLayout m_TextureLayout;
		};

		VulkanClearPass(const VulkanDevice& device, const VulkanPhysicalDevice& physicalDevice,
			const VulkanSwapchain& swapchain,
			std::uint32_t graphicsFamily, VkDescriptorSetLayout textureLayout,
			std::span<const std::uint32_t> litVertex, std::span<const std::uint32_t> litFragment,
			std::span<const std::uint32_t> texturedVertex, std::span<const std::uint32_t> texturedFragment);
		LitPipelineFactory GetLitPipelineFactory() const;
		bool IsCurrent(const LitPipelineFactory& factory) const noexcept;
		void UseLitPipeline(std::shared_ptr<const VulkanColorPipeline> pipeline) noexcept;
		bool ReplaceTexturedPipelines(std::span<const std::uint32_t> vertex,
			std::span<const std::uint32_t> fragment, const std::function<bool()>& canPublish = {});
		~VulkanClearPass();
		void CollectCompletedResources() noexcept;

		// False requests swapchain recreation. The caller waits for idle before destruction.
		bool Draw(const VulkanSwapchain& swapchain, VkQueue graphics, VkQueue present,
			const VulkanDrawQueue& queue, const VulkanTexture& white,
			std::span<const std::shared_ptr<const VulkanTexture>> textures, FrameMetrics* metrics);

		VulkanClearPass(const VulkanClearPass&) = delete;
		VulkanClearPass& operator=(const VulkanClearPass&) = delete;

	private:
		struct TexturedPipelines
		{
			TexturedPipelines(const VulkanDevice& device, VkRenderPass renderPass, VkDescriptorSetLayout textureLayout,
				const VulkanShaderModule& vertex, const VulkanShaderModule& fragment);
			VulkanTexturePipeline Opaque;
			VulkanTexturePipeline Transparent;
		};

		struct Frame
		{
			VkCommandBuffer Commands = VK_NULL_HANDLE;
			VkSemaphore ImageAvailable = VK_NULL_HANDLE;
			VkFence InFlight = VK_NULL_HANDLE;
			std::unique_ptr<VulkanBuffer> TexturedInstances;
			std::unique_ptr<VulkanBuffer> MeshInstances;
			// Retain the exact pipelines and mesh buffers referenced by this slot's submission.
			std::shared_ptr<const VulkanColorPipeline> LitPipeline;
			std::shared_ptr<const TexturedPipelines> Textured;
			std::vector<std::shared_ptr<const VulkanMeshData>> ColorMeshes;
			std::vector<std::shared_ptr<const VulkanTexture>> Textures;
			std::int64_t TimestampFrame = 0;
		};

		void UploadInstances(std::unique_ptr<VulkanBuffer>& buffer, std::span<const std::byte> bytes);
		void Record(Frame& frame, std::uint32_t imageIndex, const VulkanDrawQueue& queue,
			const VulkanTexture& white, bool timestamps);
		void PrepareTimestampQueries();
		void ReadTimestamps(Frame& frame, FrameMetrics& metrics);
		void Destroy() noexcept;

		// These owners outlive the pass and its per-frame buffers.
		const VulkanDevice& m_DeviceOwner;
		const VulkanPhysicalDevice& m_PhysicalDevice;
		VkDevice m_Device = VK_NULL_HANDLE;
		std::shared_ptr<RenderPass> m_RenderPass;
		std::shared_ptr<const VulkanColorPipeline> m_LitPipeline;
		VkDescriptorSetLayout m_TextureLayout = VK_NULL_HANDLE;
		std::shared_ptr<const TexturedPipelines> m_TexturedPipelines;
		VkCommandPool m_CommandPool = VK_NULL_HANDLE;
		VkExtent2D m_Extent{};
		std::vector<VkFramebuffer> m_Framebuffers;
		// A separate depth image for each framebuffer avoids overlap between in-flight frames.
		std::vector<std::unique_ptr<VulkanDepthAttachment>> m_DepthAttachments;
		std::array<Frame, 2> m_Frames{};
		VkQueryPool m_Timestamps = VK_NULL_HANDLE;
		std::uint32_t m_TimestampBits = 0;
		bool m_TimestampsChecked = false;
		// Reacquiring an image makes its presentation semaphore safe to reuse.
		std::vector<VkSemaphore> m_RenderFinished;
		std::size_t m_FrameIndex = 0;
	};
}
