#pragma once

#include "Raven/Renderer/RenderView.hpp"

#include "Renderer/Vulkan/VulkanDevice.hpp"
#include "Renderer/Vulkan/VulkanBuffer.hpp"
#include "Renderer/Vulkan/VulkanMesh.hpp"
#include "Renderer/Vulkan/VulkanDepthAttachment.hpp"
#include "Renderer/Vulkan/VulkanSwapchain.hpp"
#include "Renderer/Vulkan/VulkanColorPipeline.hpp"
#include "Renderer/Vulkan/VulkanDebugLinePipeline.hpp"
#include "Renderer/Vulkan/VulkanOverlayPipeline.hpp"
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
		// The lit meshes' shaders: one vertex shader and a fragment shader per view (H3).
		struct LitShaders
		{
			std::span<const std::uint32_t> Vertex;
			std::span<const std::uint32_t> Shaded;
			std::span<const std::uint32_t> Unlit;
			std::span<const std::uint32_t> Normals;
			std::span<const std::uint32_t> Wireframe;
		};

		// Every view's pipeline, published and retired together (docs/HotReloadDesign.md, H3).
		struct LitPipelines
		{
			std::shared_ptr<const VulkanColorPipeline> Shaded;
			std::shared_ptr<const VulkanColorPipeline> Unlit;
			std::shared_ptr<const VulkanColorPipeline> Normals;
			// Null when the device lacks fillModeNonSolid.
			std::shared_ptr<const VulkanColorPipeline> Lines;
		};

		// Creates lit pipelines for given attachment formats; usable on a worker thread.
		class LitPipelineFactory
		{
		public:
			std::shared_ptr<const LitPipelines> Create(const LitShaders& shaders) const;
			// A material shader's shaded pipeline (B0 contract 7).
			std::shared_ptr<const VulkanColorPipeline> CreateMaterial(std::span<const std::uint32_t> vertex,
				std::span<const std::uint32_t> fragment) const;
			// A sprite pipeline (B0 contract 4): Fill for opaque sprites, Fill with cutout, or Blend.
			std::shared_ptr<const VulkanColorPipeline> CreateSprite(std::span<const std::uint32_t> vertex,
				std::span<const std::uint32_t> fragment, VulkanColorPipeline::Mode mode, bool cutout) const;
			VkFormat GetColorFormat() const noexcept { return m_ColorFormat; }
			VkFormat GetDepthFormat() const noexcept { return m_DepthFormat; }

		private:
			friend class VulkanClearPass;
			LitPipelineFactory(const VulkanDevice& device, VkFormat colorFormat, VkFormat depthFormat,
				VkDescriptorSetLayout textureLayout, VkDescriptorSetLayout frameLayout, VkDescriptorSetLayout sceneLayout,
				bool lines);
			const VulkanDevice& m_Device;
			VkFormat m_ColorFormat;
			VkFormat m_DepthFormat;
			VkDescriptorSetLayout m_TextureLayout;
			VkDescriptorSetLayout m_FrameLayout;
			VkDescriptorSetLayout m_SceneLayout;
			bool m_Lines;
		};

		// The sprites' pipelines (B0 contract 4), from one vertex and one fragment program; they share the
		// lit pipelines' layout, so the scene's sets stay bound between them.
		struct SpritePipelines
		{
			std::shared_ptr<const VulkanColorPipeline> Opaque;
			std::shared_ptr<const VulkanColorPipeline> Cutout;
			std::shared_ptr<const VulkanColorPipeline> Blend;
		};

		// Pipelines depend only on attachment formats, so a recreated pass can keep them.
		struct Pipelines
		{
			std::shared_ptr<const LitPipelines> Lit;
			std::shared_ptr<const SpritePipelines> Sprites;
			std::shared_ptr<const VulkanDebugLinePipeline> DebugLines;
			std::shared_ptr<const VulkanOverlayPipeline> Overlay;
			VkFormat ColorFormat = VK_FORMAT_UNDEFINED;
			VkFormat DepthFormat = VK_FORMAT_UNDEFINED;
		};

		VulkanClearPass(const VulkanDevice& device, const VulkanPhysicalDevice& physicalDevice,
			const VulkanSwapchain& swapchain,
			std::uint32_t graphicsFamily, VkDescriptorSetLayout bindlessLayout,
			VkDescriptorSetLayout frameLayout, VulkanGpuScene& scene, const LitShaders& lit,
			std::span<const std::uint32_t> spriteVertex, std::span<const std::uint32_t> spriteFragment,
			std::span<const std::uint32_t> debugLineVertex, std::span<const std::uint32_t> debugLineFragment,
			std::span<const std::uint32_t> overlayVertex, std::span<const std::uint32_t> overlayFragment,
			VulkanFrameTimeline& timeline, const Pipelines* reuse = nullptr);
		LitPipelineFactory GetLitPipelineFactory() const;
		Pipelines GetPipelines() const;
		bool IsCurrent(const LitPipelineFactory& factory) const noexcept;
		void UseLitPipelines(std::shared_ptr<const LitPipelines> pipelines) noexcept;
		void ReplaceSpritePipelines(std::span<const std::uint32_t> vertex, std::span<const std::uint32_t> fragment);
		void ReplaceDebugLinePipeline(std::span<const std::uint32_t> vertex, std::span<const std::uint32_t> fragment);
		void ReplaceOverlayPipeline(std::span<const std::uint32_t> vertex, std::span<const std::uint32_t> fragment);
		// Material pipelines by key minus one; null entries draw with the shaded lit pipeline.
		void SetMaterialPipelines(std::vector<std::shared_ptr<const VulkanColorPipeline>> pipelines) noexcept
		{
			m_MaterialPipelines = std::move(pipelines);
		}
		~VulkanClearPass();

		// False requests swapchain recreation. The caller waits for idle before destruction.
		// The GPU scene was planned for this frame; lit meshes come only from it (G7).
		bool Draw(const VulkanSwapchain& swapchain, VkQueue graphics, VkQueue present,
			const VulkanDrawQueue& queue, const VulkanMeshPool& meshes, VkDescriptorSet bindlessTextures,
			FrameMetrics* metrics, RenderView view = RenderView::Shaded);

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

		struct RingOffsets
		{
			VkDeviceSize DebugLines = 0;
			VkDeviceSize Overlay = 0;
			std::vector<VkDeviceSize> Uploads;
		};
		RingOffsets Upload(Frame& frame, const VulkanDrawQueue& queue);
		void Record(Frame& frame, std::uint32_t imageIndex, const VulkanDrawQueue& queue,
			const VulkanMeshPool& meshes, VkDescriptorSet bindlessTextures, const RingOffsets& offsets, bool timestamps,
			RenderView view);
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
		std::shared_ptr<const LitPipelines> m_LitPipelines;
		std::vector<std::shared_ptr<const VulkanColorPipeline>> m_MaterialPipelines;
		VkDescriptorSetLayout m_BindlessLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_FrameLayout = VK_NULL_HANDLE;
		// Owned by the context; outlives the pass.
		VulkanGpuScene& m_Scene;
		std::shared_ptr<const SpritePipelines> m_SpritePipelines;
		std::shared_ptr<const VulkanDebugLinePipeline> m_DebugLines;
		std::shared_ptr<const VulkanOverlayPipeline> m_Overlay;
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
