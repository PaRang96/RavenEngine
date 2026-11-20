#pragma once

#include "Raven/Assets/Image.hpp"

#include <span>

namespace Raven::Images
{
	std::vector<std::byte> ReadEncoded(const std::filesystem::path& path);
	ImageData Decode(std::span<const std::byte> bytes, const std::filesystem::path& source,
		ImageLoadOptions options = {});
}
