#pragma once

#include "Raven/Renderer/Vulkan/VulkanTexturedDraw.hpp"

#include "Renderer/Vulkan/VulkanDevice.hpp"
#include "Renderer/Vulkan/VulkanBuffer.hpp"
#include "Renderer/Vulkan/VulkanMesh.hpp"
#include "Renderer/Vulkan/VulkanDepthAttachment.hpp"
#include "Renderer/Vulkan/VulkanSwapchain.hpp"
#include "Renderer/Vulkan/VulkanColorPipeline.hpp"
#include "Renderer/Vulkan/VulkanTexturePipeline.hpp"
#include "Renderer/Vulkan/VulkanDrawQueue.hpp"
#include "Renderer/Vulkan/VulkanFrameTimeline.hpp"
#include "Renderer/Vulkan/VulkanFrameData.hpp"
#include "Renderer/Vulkan/VulkanGpuScene.hpp"

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
	public:
		struct TexturedPipelines;

		// Creates lit pipelines for given attachment formats; usable on a worker thread.
		class LitPipelineFactory
		{
		public:
			std::shared_ptr<const VulkanColorPipeline> Create(std::span<const std::uint32_t> vertex,
				std::span<const std::uint32_t> fragment) const;

		private:
			friend class VulkanClearPass;
			LitPipelineFactory(const VulkanDevice& device, VkFormat colorFormat, VkFormat depthFormat,
				VkDescriptorSetLayout textureLayout, VkDescriptorSetLayout frameLayout, VkDescriptorSetLayout sceneLayout);
			const VulkanDevice& m_Device;
			VkFormat m_ColorFormat;
			VkFormat m_DepthFormat;
			VkDescriptorSetLayout m_TextureLayout;
			VkDescriptorSetLayout m_FrameLayout;
			VkDescriptorSetLayout m_SceneLayout;
		};

		// Pipelines depend only on attachment formats, so a recreated pass can keep them.
		struct Pipelines
		{
			std::shared_ptr<const VulkanColorPipeline> Lit;
			std::shared_ptr<const TexturedPipelines> Textured;
			VkFormat ColorFormat = VK_FORMAT_UNDEFINED;
			VkFormat DepthFormat = VK_FORMAT_UNDEFINED;
		};

		struct TexturedPipelines
		{
			TexturedPipelines(const VulkanDevice& device, VkFormat colorFormat, VkFormat depthFormat,
				VkDescriptorSetLayout textureLayout, const VulkanShaderModule& vertex, const VulkanShaderModule& fragment);
			VulkanTexturePipeline Opaque;
			VulkanTexturePipeline Transparent;
		};

		VulkanClearPass(const VulkanDevice& device, const VulkanPhysicalDevice& physicalDevice,
			const VulkanSwapchain& swapchain,
			std::uint32_t graphicsFamily, VkDescriptorSetLayout textureLayout, VkDescriptorSetLayout bindlessLayout,
			VkDescriptorSetLayout frameLayout, VulkanGpuScene& scene, std::span<const std::uint32_t> litVertex, std::span<const std::uint32_t> litFragment,
			std::span<const std::uint32_t> texturedVertex, std::span<const std::uint32_t> texturedFragment,
			VulkanFrameTimeline& timeline, const Pipelines* reuse = nullptr);
		LitPipelineFactory GetLitPipelineFactory() const;
		Pipelines GetPipelines() const;
		bool IsCurrent(const LitPipelineFactory& factory) const noexcept;
		void UseLitPipeline(std::shared_ptr<const VulkanColorPipeline> pipeline) noexcept;
		bool ReplaceTexturedPipelines(std::span<const std::uint32_t> vertex,
			std::span<const std::uint32_t> fragment, const std::function<bool()>& canPublish = {});
		~VulkanClearPass();

		// False requests swapchain recreation. The caller waits for idle before destruction.
		// The GPU scene was planned for this frame; lit meshes come only from it (G7).
		bool Draw(const VulkanSwapchain& swapchain, VkQueue graphics, VkQueue present,
			const VulkanDrawQueue& queue, const VulkanMeshPool& meshes, VkDescriptorSet bindlessTextures,
			FrameMetrics* metrics);

		VulkanClearPass(const VulkanClearPass&) = delete;
		VulkanClearPass& operator=(const VulkanClearPass&) = delete;

	private:
		struct Frame
		{
			VkCommandBuffer Commands = VK_NULL_HANDLE;
			VkSemaphore ImageAvailable = VK_NULL_HANDLE;
			// Timeline value signaled by this slot's last submission; 0 before the first one.
			std::uint64_t Submitted = 0;
			// Frame uniform and instances (R4). Only this slot's submissions read it, so waiting for
			// the slot covers its previous contents.
			VulkanUploadRing Ring;
			// Names the ring's frame uniform; rewritten when the ring is replaced.
			VkDescriptorSet FrameSet = VK_NULL_HANDLE;
			std::int64_t TimestampFrame = 0;
		};

		// Returns the ring offset of the textured instances.
		VkDeviceSize Upload(Frame& frame, const VulkanDrawQueue& queue);
		void Record(Frame& frame, std::uint32_t imageIndex, const VulkanDrawQueue& queue,
			const VulkanMeshPool& meshes, VkDescriptorSet bindlessTextures, VkDeviceSize texturedOffset, bool timestamps);
		void PrepareTimestampQueries();
		void ReadTimestamps(Frame& frame, FrameMetrics& metrics);
		void Destroy() noexcept;

		// These owners outlive the pass and its per-frame buffers.
		const VulkanDevice& m_DeviceOwner;
		const VulkanPhysicalDevice& m_PhysicalDevice;
		VulkanFrameTimeline& m_Timeline;
		VkDevice m_Device = VK_NULL_HANDLE;
		VkFormat m_ColorFormat = VK_FORMAT_UNDEFINED;
		VkFormat m_DepthFormat = VK_FORMAT_UNDEFINED;
		std::shared_ptr<const VulkanColorPipeline> m_LitPipeline;
		VkDescriptorSetLayout m_TextureLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_BindlessLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_FrameLayout = VK_NULL_HANDLE;
		// Owned by the context; outlives the pass.
		VulkanGpuScene& m_Scene;
		std::shared_ptr<const TexturedPipelines> m_TexturedPipelines;
		VkCommandPool m_CommandPool = VK_NULL_HANDLE;
		VkDescriptorPool m_FramePool = VK_NULL_HANDLE;
		VkExtent2D m_Extent{};
		// Swapchain images rendered to with dynamic rendering; the swapchain outlives the pass.
		std::vector<VkImage> m_ColorImages;
		std::vector<VkImageView> m_ColorViews;
		// A separate depth image for each swapchain image avoids overlap between in-flight frames.
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
