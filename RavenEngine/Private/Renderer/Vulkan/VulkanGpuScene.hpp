#pragma once

#include "Renderer/RenderWorldState.hpp"
#include "Renderer/Vulkan/VulkanBuffer.hpp"
#include "Renderer/Vulkan/VulkanDevice.hpp"
#include "Renderer/Vulkan/ShaderInterface.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace Raven
{
	class VulkanColorPipeline;
	class VulkanFrameTimeline;
	class VulkanUploadRing;
	struct VulkanMeshData;
	struct VulkanPhysicalDevice;

	// GPU side of a RenderWorld (docs/GpuDrivenDesign.md, G2/G3/G5): resident object and material
	// buffers, a per-mesh table, compute culling into fixed-count indirect commands, and one
	// indirect draw for every lit mesh. The context owns one; it follows whichever world it is given.
	class VulkanGpuScene
	{
	public:
		using MeshResolver = std::function<const VulkanMeshData*(MeshHandle)>;
		static constexpr std::size_t FrameSlots = 2;

		VulkanGpuScene(const VulkanDevice& device, const VulkanPhysicalDevice& physicalDevice,
			VulkanFrameTimeline& timeline, std::span<const std::uint32_t> scatterWords,
			std::span<const std::uint32_t> cullWords);
		~VulkanGpuScene();

		// Set 2 of the lit pipeline and set 0 of the culling passes.
		VkDescriptorSetLayout GetLayout() const noexcept { return m_Layout; }

		// What reloaded shaders must match (docs/HotReloadDesign.md, H2): the scene buffers as
		// this class writes them, bound at the given set, and each compute pass's interface.
		static std::vector<ShaderBufferLayout> GetBufferLayouts(std::uint32_t set);
		static ShaderInterface GetScatterInterface();
		static ShaderInterface GetCullInterface();
		// Replaces both compute pipelines; frames in flight keep the previous ones (R3).
		void ReplacePipelines(std::span<const std::uint32_t> scatterWords, std::span<const std::uint32_t> cullWords);

		// Plans this frame's upload and returns the ring bytes it needs. A null world draws nothing.
		VkDeviceSize Plan(RenderWorld::State* world, const MeshResolver& resolveMesh);
		// Writes the planned records into the slot's ring after its Begin, and names the ring and
		// buffers in the slot's set. The slot's previous frame has completed.
		void Upload(std::size_t slot, VulkanUploadRing& ring);
		// Outside rendering: apply uploads, then cull into the indirect commands.
		void RecordPrepare(VkCommandBuffer commands, std::size_t slot, const Mat4& viewProjection) const;
		// Inside rendering, with the lit pipeline's bindless and frame sets.
		void RecordDraw(VkCommandBuffer commands, std::size_t slot, const VulkanColorPipeline& pipeline,
			VkDescriptorSet textures, VkDescriptorSet frame, VkBuffer vertices, VkBuffer indices) const;
		// After rendering: copy the commands for the visible-count readback.
		void RecordReadback(VkCommandBuffer commands, std::size_t slot) const;
		// The frame was submitted: its upload is now part of the GPU state.
		void Commit(std::size_t slot) noexcept;
		// Visible objects of the slot's previous submitted frame; read after the slot's wait.
		std::uint32_t ReadVisible(std::size_t slot) const noexcept;
		std::uint32_t GetDrawCount() const noexcept { return m_GroupCount; }
		VkDeviceSize GetPlannedBytes() const noexcept { return m_PlannedBytes; }
		std::uint32_t GetObjectCount() const noexcept { return m_ObjectCount; }

		VulkanGpuScene(const VulkanGpuScene&) = delete;
		VulkanGpuScene& operator=(const VulkanGpuScene&) = delete;

	private:
		struct MeshEntry
		{
			std::uint32_t IndexCount = 0;
			std::uint32_t FirstIndex = 0;
			std::int32_t VertexOffset = 0;
			std::uint32_t Padding = 0;
			Vec4 Center;
			Vec4 HalfExtent;
		};

		// A device-local buffer that grows by replacement; the old one is retired (R3).
		struct Storage
		{
			std::shared_ptr<const VulkanBuffer> Buffer;
			VkDeviceSize Capacity = 0;
		};

		struct Slot
		{
			VkDescriptorSet Set = VK_NULL_HANDLE;
			std::shared_ptr<const VulkanBuffer> Readback;
			std::uint32_t ReadbackGroups = 0;
			// Ring offsets of this frame's upload.
			VkDeviceSize MeshOffset = 0;
			VkDeviceSize TemplateOffset = 0;
			VkDeviceSize ObjectOffset = 0;
			VkDeviceSize SlotOffset = 0;
			VkDeviceSize MaterialOffset = 0;
			VkBuffer Ring = VK_NULL_HANDLE;
		};

		struct ComputePipelines;
		std::shared_ptr<const ComputePipelines> CreatePipelines(std::span<const std::uint32_t> scatterWords,
			std::span<const std::uint32_t> cullWords) const;
		bool Reserve(Storage& storage, VkDeviceSize bytes, VkBufferUsageFlags usage);
		void Destroy() noexcept;

		const VulkanDevice& m_DeviceOwner;
		const VulkanPhysicalDevice& m_PhysicalDevice;
		VulkanFrameTimeline& m_Timeline;
		VkDevice m_Device = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_Layout = VK_NULL_HANDLE;
		VkDescriptorPool m_Pool = VK_NULL_HANDLE;
		VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
		std::shared_ptr<const ComputePipelines> m_Pipelines;
		Storage m_Objects;
		Storage m_Visible;
		Storage m_Materials;
		Storage m_Meshes;
		Storage m_Commands;
		std::array<Slot, FrameSlots> m_Slots{};

		// The current frame's plan.
		RenderWorld::State* m_World = nullptr;
		std::vector<MeshEntry> m_MeshTable;
		std::vector<VkDrawIndexedIndirectCommand> m_Templates;
		std::vector<std::uint32_t> m_DirtySlots;
		std::vector<GpuObjectData> m_DirtyRecords;
		std::uint32_t m_ObjectCount = 0;
		std::uint32_t m_GroupCount = 0;
		std::uint32_t m_MaterialCount = 0;
		std::uint32_t m_FirstMaterial = 0;
		bool m_Full = false;
		VkDeviceSize m_PlannedBytes = 0;

		// What the GPU holds after the last committed frame.
		const RenderWorld::State* m_UploadedWorld = nullptr;
		std::uint64_t m_UploadedGeneration = 0;
		std::uint32_t m_UploadedMaterials = 0;
		// Set when a buffer was replaced but the frame filling it has not been committed.
		bool m_PendingFull = true;
	};
}
