#pragma once

#include "Raven/Core/Api.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <span>
#include <vector>

namespace Raven::Files
{
	RAVEN_API std::vector<std::byte> ReadBinary(const std::filesystem::path& path);
	// Checks the opened file length before allocating the read buffer.
	RAVEN_API std::vector<std::byte> ReadBinary(const std::filesystem::path& path, std::size_t maxBytes);
	// Returns the original bytes, without newline conversion or character decoding.
	RAVEN_API std::string ReadText(const std::filesystem::path& path);
	// Same-directory exclusive temporary file followed by replacement; throws without truncating the destination.
	// Atomic visibility on the supported local filesystem, not a power-loss durability guarantee.
	RAVEN_API void WriteAtomic(const std::filesystem::path& path, std::span<const std::byte> bytes);
}
