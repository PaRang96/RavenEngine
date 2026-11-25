#pragma once

#include "Raven/Renderer/TextureHandle.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace Raven
{
	// The shared GPU 2D path, feed 1 (docs/UIDesign.md, section 4): screen-space quads drawn over the
	// scene in one instanced draw, in submission order, with alpha blending. UI panels, text, images
	// and on-screen diagnostics all use this one format; M6B's world-space sprites will share it.
	namespace OverlayFlags
	{
		// A filled rectangle, with optional rounded corners, border and soft edge.
		inline constexpr std::uint32_t Solid = 0;
		// The rectangle tinted by a texture region.
		inline constexpr std::uint32_t Image = 1;
		// A glyph from a multi-channel signed distance field atlas (MTSDF).
		inline constexpr std::uint32_t Glyph = 2;
	}

	// One instance. Positions are framebuffer pixels from the top-left corner; colors are linear RGBA
	// with straight alpha.
	struct OverlayQuad
	{
		float Rect[4]{};         // x, y, width, height
		float Uv[4]{};           // u0, v0, u1, v1 of the texture region
		float Color[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
		float BorderColor[4]{};
		float Clip[4]{ -1.0e9f, -1.0e9f, 1.0e9f, 1.0e9f }; // left, top, right, bottom; outside is discarded
		float CornerRadius = 0.0f;
		float BorderWidth = 0.0f;
		// Solid quads: edge blur in pixels, for shadows; zero gives a one-pixel antialiased edge.
		float Softness = 0.0f;
		// Glyphs: the distance field's range in framebuffer pixels at this quad's size.
		float DistanceRange = 0.0f;
		// The bindless element of the texture (VulkanContext::GetBindlessIndex).
		std::uint32_t Texture = 0;
		std::uint32_t Flags = OverlayFlags::Solid;
		std::uint32_t Reserved[2]{};
	};
	static_assert(sizeof(OverlayQuad) == 112);

	// Tightly packed RGBA8 rows copied into a texture region before the frame draws, through the
	// frame's upload ring, so a new glyph never stalls the GPU.
	struct OverlayTextureUpload
	{
		TextureHandle Texture;
		std::uint32_t X = 0;
		std::uint32_t Y = 0;
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::span<const std::byte> Pixels;
	};

	struct OverlayFrame
	{
		std::span<const OverlayQuad> Quads;
		std::span<const OverlayTextureUpload> Uploads;
	};
}
