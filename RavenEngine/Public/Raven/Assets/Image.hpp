#pragma once

#include "Raven/Core/Api.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace Raven
{
	struct ImageLoadOptions
	{
		// Applies in development builds; disable for deliberately non-power-of-two assets.
		bool WarnIfNonPowerOfTwo = true;
	};

	struct ImageData
	{
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		// Tightly packed RGBA8, top row first, with straight (unassociated) alpha.
		std::vector<std::byte> Pixels;
	};

	namespace Images
	{
		// Decodes PNG/JPEG into owned CPU pixels. Color-space selection belongs to TextureDesc.
		RAVEN_API ImageData Load(const std::filesystem::path& path);
		RAVEN_API ImageData Load(const std::filesystem::path& path, ImageLoadOptions options);
	}
}
