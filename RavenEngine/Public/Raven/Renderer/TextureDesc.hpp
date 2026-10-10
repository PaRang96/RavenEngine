#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace Raven
{
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
