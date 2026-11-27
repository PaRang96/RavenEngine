#pragma once

#include "Raven/Renderer/DebugLines.hpp"
#include "Raven/Renderer/Overlay.hpp"
#include "Raven/Renderer/SceneLighting.hpp"
#include "Renderer/Vulkan/VulkanFrameData.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace Raven
{
	// One frame's CPU-side inputs besides the GPU scene; borrowed spans are used only during the current
	// DrawFrame. Lit meshes and sprites come from the render world (docs/GpuDrivenDesign.md, G7;
	// docs/RenderingContentDesign.md, B0 contract 4).
	struct VulkanDrawQueue
	{
		FrameUniformData Frame{};
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

	// The frame uniform: camera, lighting and the renderer's clock.
	void BuildVulkanDrawQueue(VulkanDrawQueue& queue, const Mat4& view, const Mat4& viewProjection,
		const SceneLighting& lighting = {}, float seconds = 0.0f, float delta = 0.0f);
}
