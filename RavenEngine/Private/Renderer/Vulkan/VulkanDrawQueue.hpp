#pragma once

#include "Raven/Renderer/Vulkan/VulkanDraw.hpp"
#include "Raven/Renderer/Vulkan/VulkanTransparentDraw.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace Raven
{
	struct QueuedColorDraw
	{
		const VulkanDraw* Draw;
		Mat4 ModelViewProjection;
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
		std::vector<QueuedColorDraw> Color;
		std::vector<QueuedTexturedDraw> OpaqueTexture;
		std::vector<QueuedTexturedDraw> Transparent;
		std::vector<TexturedInstanceData> TexturedInstances;
		std::vector<TexturedBatch> OpaqueBatches;
		std::vector<TexturedBatch> TransparentBatches;
	};

	void BuildVulkanDrawQueue(VulkanDrawQueue& queue, const Mat4& view, const Mat4& viewProjection,
		std::span<const VulkanDraw> colorDraws, std::span<const VulkanTexturedDraw> opaqueDraws,
		std::span<const VulkanTransparentDraw> transparentDraws);
}
