#pragma once

#include "Platform/AudioBackend.hpp"

#include <cstddef>
#include <filesystem>
#include <span>

namespace Raven
{
	PcmClip DecodeWav(std::span<const std::byte> bytes, const std::filesystem::path& path);
}
