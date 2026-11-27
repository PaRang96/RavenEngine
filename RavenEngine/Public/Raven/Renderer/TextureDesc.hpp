#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace Raven
{
	// Which part of the bindless array a managed texture takes (docs/RenderingContentDesign.md, B0
	// contract 8): engine textures (glyph pages, UI) have a reserve content cannot use up.
	enum class TextureUse
	{
		Content,
		Engine,
	};

	enum class TextureColorSpace
	{
		Linear,
		SRGB
	};

	struct TextureDesc
	{
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		// Rows are tightly packed RGBA8 bytes; creation consumes the entire span.
		std::span<const std::byte> Pixels;
		TextureColorSpace ColorSpace = TextureColorSpace::SRGB;
	};
}
