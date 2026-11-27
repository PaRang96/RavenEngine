#pragma once

#include "Raven/Renderer/Vulkan/VulkanTexturedDraw.hpp"
#include "Raven/Renderer/DebugLines.hpp"
#include "Raven/Renderer/Overlay.hpp"
#include "Raven/Renderer/SceneLighting.hpp"
#include "Renderer/Vulkan/VulkanFrameData.hpp"
#include "Raven/Renderer/Vulkan/VulkanTransparentDraw.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace Raven
{
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
		// Lit meshes are GPU-driven (docs/GpuDrivenDesign.md, G7); this queue holds the textured path.
		std::vector<QueuedTexturedDraw> OpaqueTexture;
		std::vector<QueuedTexturedDraw> Transparent;
		std::vector<TexturedInstanceData> TexturedInstances;
		std::vector<TexturedBatch> OpaqueBatches;
		std::vector<TexturedBatch> TransparentBatches;
		// Developer lines, borrowed for the frame (FD-32 collider view).
		std::span<const DebugLineVertex> DebugLines;
		// Screen-space 2D quads drawn last (docs/UIDesign.md, section 4), borrowed for the frame.
		std::span<const OverlayQuad> Overlay;
		// Texture regions written before drawing, such as new glyphs; pixels borrowed for the frame.
		struct TextureUpload
		{
			VkImage Image = VK_NULL_HANDLE;
			std::uint32_t X = 0;
			std::uint32_t Y = 0;
			std::uint32_t Width = 0;
			std::uint32_t Height = 0;
			std::span<const std::byte> Pixels;
		};
		std::vector<TextureUpload> Uploads;
	};

	void BuildVulkanDrawQueue(VulkanDrawQueue& queue, const Mat4& view, const Mat4& viewProjection,
		std::span<const VulkanTexturedDraw> opaqueDraws,
		std::span<const VulkanTransparentDraw> transparentDraws, const SceneLighting& lighting = {},
		float seconds = 0.0f, float delta = 0.0f);
}
