#pragma once

#include "Renderer/RenderWorldState.hpp"
#include "Renderer/Vulkan/VulkanBuffer.hpp"
#include "Renderer/Vulkan/VulkanDevice.hpp"
#include "Renderer/Vulkan/VulkanMesh.hpp"
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

	// Bindings of the scene set that material shaders read (Shaders/GpuScene.hlsli).
	inline constexpr std::uint32_t RavenSceneMaterialsBinding = 2;
	inline constexpr std::uint32_t RavenSceneMaterialValuesBinding = 7;
	class VulkanFrameTimeline;
	class VulkanUploadRing;
	struct VulkanMeshData;
	struct VulkanPhysicalDevice;

	// GPU side of a RenderWorld (docs/GpuDrivenDesign.md, G2/G3/G5): resident object and material
	// buffers, a resident per-group mesh table, compute culling into fixed-count indirect commands
	// laid out contiguously per pipeline, and one indirect draw per pipeline range
	// (docs/RenderingContentDesign.md, B0 contract 3). Blended sprites are culled and sorted back to
	// front on the CPU and drawn by one instanced draw (B0 contract 4). The context owns one; it
	// follows whichever world it is given.
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
		// The mesh table is resolved again only when the world's groups or meshRevision (bumped by
		// the context when mesh contents are swapped or released) changed. The camera orders the
		// blended sprites: ascending layer and order, then far to near, then first appearance.
		VkDeviceSize Plan(RenderWorld::State* world, const MeshResolver& resolveMesh, std::uint64_t meshRevision,
			const Mat4& view, const Mat4& viewProjection);
		// Writes the planned records into the slot's ring after its Begin, and names the ring and
		// buffers in the slot's set. The slot's previous frame has completed.
		void Upload(std::size_t slot, VulkanUploadRing& ring);
		// Outside rendering: apply uploads, then cull into the indirect commands.
		void RecordPrepare(VkCommandBuffer commands, std::size_t slot, const Mat4& viewProjection) const;
		// Inside rendering, with the lit pipeline's bindless and frame sets: every pipeline range with
		// the pipeline pipelineFor gives it, skipping ranges it gives none. Material shaders (B4) pick
		// one per range; debug views draw the meshes with one.
		using PipelineFor = std::function<const VulkanColorPipeline*(const DrawPipelineKey&)>;
		void RecordDraw(VkCommandBuffer commands, std::size_t slot, const PipelineFor& pipelineFor,
			VkDescriptorSet textures, VkDescriptorSet frame, VkBuffer vertices, VkBuffer indices) const;
		// The visible blended sprites, back to front in one draw (B0 contract 4).
		void RecordBlended(VkCommandBuffer commands, std::size_t slot, const VulkanColorPipeline& pipeline,
			VkDescriptorSet textures, VkDescriptorSet frame, VkBuffer vertices, VkBuffer indices) const;
		// After rendering: copy the commands for the visible-count readback.
		void RecordReadback(VkCommandBuffer commands, std::size_t slot) const;
		// The frame was submitted: its upload is now part of the GPU state.
		void Commit(std::size_t slot) noexcept;
		// Visible meshes of the slot's previous submitted frame (sprites are not counted); read after the
		// slot's wait.
		std::uint32_t ReadVisible(std::size_t slot) const noexcept;
		std::uint32_t GetDrawCount() const noexcept { return m_CommandCount; }
		// Commands of opaque and cut-out sprites, which debug views do not draw again.
		std::uint32_t GetSpriteCommandCount() const noexcept { return m_SpriteCommands; }
		std::uint32_t GetBlendedCount() const noexcept { return static_cast<std::uint32_t>(m_BlendSlots.size()); }
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
			// The group's indirect command (B0 contract 3).
			std::uint32_t CommandIndex = 0;
			Vec4 Center;
			Vec4 HalfExtent;
		};

		// A device-local buffer that grows by replacement; the old one is retired (R3).
		struct Storage
		{
			std::shared_ptr<const VulkanBuffer> Buffer;
			VkDeviceSize Capacity = 0;
		};

		// A run of commands that share one pipeline.
		struct CommandRange
		{
			DrawPipelineKey Pipeline;
			std::uint32_t First = 0;
			std::uint32_t Count = 0;
		};

		struct Slot
		{
			VkDescriptorSet Set = VK_NULL_HANDLE;
			std::shared_ptr<const VulkanBuffer> Readback;
			std::uint32_t ReadbackGroups = 0;
			// Which of the read-back commands draw sprites.
			std::vector<std::uint8_t> ReadbackSprites;
			// Ring offsets of this frame's upload.
			VkDeviceSize MeshOffset = 0;
			VkDeviceSize TemplateOffset = 0;
			VkDeviceSize ObjectOffset = 0;
			VkDeviceSize SlotOffset = 0;
			VkDeviceSize MaterialOffset = 0;
			VkDeviceSize ValuesOffset = 0;
			VkDeviceSize BlendOffset = 0;
			VkBuffer Ring = VK_NULL_HANDLE;
		};

		// One blended sprite in this frame's sort, as two unsigned keys: layer and order, then far-to-near
		// depth and first appearance.
		struct BlendEntry
		{
			std::uint64_t Primary;
			std::uint64_t Secondary;
			std::uint32_t Slot;
		};

		struct ComputePipelines;
		std::shared_ptr<const ComputePipelines> CreatePipelines(std::span<const std::uint32_t> scatterWords,
			std::span<const std::uint32_t> cullWords) const;
		void PlanBlended(const RenderWorld::State& world, const MeshResolver& resolveMesh, const Mat4& view,
			const Mat4& viewProjection);
		// Whether this frame records uploads and culling; a world without objects needs none.
		bool Prepares() const noexcept { return m_World && (m_ObjectCount != 0 || m_CommandCount != 0); }
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
		// The materials' values (B0 contract 1); filled with B4's material shaders.
		Storage m_Values;
		Storage m_Meshes;
		// The command templates; copied over the commands every frame, since Cull accumulates.
		Storage m_Templates;
		Storage m_Commands;
		std::array<Slot, FrameSlots> m_Slots{};

		// The current frame's plan. The mesh table, command order, ranges and templates persist
		// between plans and are rebuilt only when their revisions change.
		RenderWorld::State* m_World = nullptr;
		std::vector<MeshEntry> m_MeshTable;
		std::vector<std::uint32_t> m_CommandGroups;
		// Per command: whether it draws sprites.
		std::vector<std::uint8_t> m_CommandSprites;
		std::vector<CommandRange> m_Ranges;
		std::vector<VkDrawIndexedIndirectCommand> m_TemplateRecords;
		bool m_UploadMeshes = false;
		bool m_UploadTemplates = false;
		std::vector<std::uint32_t> m_DirtySlots;
		std::vector<GpuObjectData> m_DirtyRecords;
		std::uint32_t m_ObjectCount = 0;
		std::uint32_t m_GroupSlots = 0;
		std::uint32_t m_CommandCount = 0;
		std::uint32_t m_SpriteCommands = 0;
		std::uint64_t m_MeshRevision = 0;
		std::uint32_t m_MaterialCount = 0;
		bool m_AllMaterials = false;
		bool m_UploadValues = false;
		std::uint64_t m_UploadedValuesRevision = ~0ull;
		std::vector<std::uint32_t> m_MaterialSlots;
		std::vector<GpuMaterialData> m_MaterialRecords;
		// This frame's blended sprites, back to front, after the culled instance regions in Visible.
		std::vector<BlendEntry> m_BlendEntries;
		std::vector<std::uint32_t> m_BlendSlots;
		std::uint32_t m_BlendBase = 0;
		VulkanMeshPool::Ranges m_BlendMesh{};
		bool m_Full = false;
		VkDeviceSize m_PlannedBytes = 0;

		// What the GPU holds after the last committed frame.
		const RenderWorld::State* m_UploadedWorld = nullptr;
		std::uint64_t m_UploadedGeneration = 0;
		// Set when the material buffer was replaced but the frame filling it has not been committed.
		bool m_PendingMaterials = true;
		// What the resident mesh table and templates were built from; a replaced buffer forgets.
		std::uint64_t m_UploadedLayout = ~0ull;
		std::uint64_t m_UploadedCounts = ~0ull;
		std::uint64_t m_UploadedMeshRevision = ~0ull;
		// Set when a buffer was replaced but the frame filling it has not been committed.
		bool m_PendingFull = true;
	};
}
