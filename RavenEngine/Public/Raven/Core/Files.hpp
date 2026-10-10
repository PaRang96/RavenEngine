#pragma once

#include "Raven/Core/Api.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace Raven::Files
{
	RAVEN_API std::vector<std::byte> ReadBinary(const std::filesystem::path& path);
	// Returns the original bytes, without newline conversion or character decoding.
	RAVEN_API std::string ReadText(const std::filesystem::path& path);
}
