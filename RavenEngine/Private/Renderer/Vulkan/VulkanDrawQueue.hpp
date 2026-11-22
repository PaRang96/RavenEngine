#pragma once

#include "Raven/Renderer/Vulkan/VulkanDraw.hpp"
#include "Raven/Renderer/SceneLighting.hpp"
#include "Renderer/Vulkan/VulkanMeshInstance.hpp"
#include "Renderer/Vulkan/VulkanFrameData.hpp"
#include "Raven/Renderer/Vulkan/VulkanTransparentDraw.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace Raven
{
	struct VulkanMeshData;

	// Lit-pipeline draws that share mesh data and texture become one instanced draw (R5). There
	// is one lit pipeline per frame and the lit flag is per-instance data, so neither splits a batch.
	struct LitBatchKey
	{
		const VulkanMeshData* Mesh;
		// Null selects the white texture while recording.
		const VulkanTexture* Texture;
		bool operator==(const LitBatchKey&) const = default;
	};

	// Open-addressing table from key to batch index, reused across frames (R5).
	class LitBatchTable
	{
	public:
		void Clear() noexcept;
		// Returns the batch for the key, or stores nextBatch and returns it for a new key.
		std::uint32_t Find(LitBatchKey key, std::uint32_t nextBatch);

	private:
		struct Slot
		{
			LitBatchKey Key{};
			std::uint32_t Batch = Empty;
		};
		static constexpr std::uint32_t Empty = ~0u;
		void Grow();
		std::vector<Slot> m_Slots;
		std::size_t m_Count = 0;
	};

	struct LitBatch
	{
		LitBatchKey Key;
		std::uint32_t FirstInstance;
		std::uint32_t InstanceCount;
	};

	struct QueuedColorDraw
	{
		const VulkanDraw* Draw;
		std::uint32_t Batch;
	};

	struct QueuedTexturedDraw
	{
		const VulkanTexturedDraw* Draw;
		Mat4 ModelViewProjection;
		float ViewDepth = 0.0f;
		std::int32_t SortLayer = 0;
		std::int32_t SortOrder = 0;
		std::size_t SubmissionIndex = 0;
	};

	struct TexturedInstanceData
	{
		Mat4 ClipFromLocal;
		Vec4 Tint;
	};

	static_assert(std::is_standard_layout_v<TexturedInstanceData>);
	static_assert(std::is_trivially_copyable_v<TexturedInstanceData>);
	static_assert(sizeof(TexturedInstanceData) == 80);
	static_assert(offsetof(TexturedInstanceData, ClipFromLocal) == 0);
	static_assert(offsetof(TexturedInstanceData, Tint) == 64);

	struct TexturedBatch
	{
		const VulkanTexturedMesh* Mesh;
		const VulkanTexture* Texture;
		std::uint32_t FirstInstance;
		std::uint32_t InstanceCount;
	};

	// CPU storage is reused; borrowed submissions are used only during the current DrawFrame.
	struct VulkanDrawQueue
	{
		FrameUniformData Frame{};
		// Visible lit-pipeline draws in submission order, with the batch each joined.
		std::vector<QueuedColorDraw> Color;
		// Batches in first-seen order; each owns a contiguous range of MeshInstances.
		std::vector<LitBatch> LitBatches;
		std::vector<MeshInstanceData> MeshInstances;
		// Cleared every frame: keys name mesh data and textures that may be released afterwards.
		LitBatchTable LitBatchIndex;
		std::vector<QueuedTexturedDraw> OpaqueTexture;
		std::vector<QueuedTexturedDraw> Transparent;
		std::vector<TexturedInstanceData> TexturedInstances;
		std::vector<TexturedBatch> OpaqueBatches;
		std::vector<TexturedBatch> TransparentBatches;
	};

	void BuildVulkanDrawQueue(VulkanDrawQueue& queue, const Mat4& view, const Mat4& viewProjection,
		std::span<const VulkanDraw> colorDraws, std::span<const VulkanTexturedDraw> opaqueDraws,
		std::span<const VulkanTransparentDraw> transparentDraws, const SceneLighting& lighting = {});
}
