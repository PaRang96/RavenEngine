#pragma once

#include "Raven/Assets/Image.hpp"

#include <cstddef>
#include <filesystem>
#include <span>

namespace Raven
{
	ImageData DecodePngSource(std::span<const std::byte> bytes, const std::filesystem::path& path);
}
