#pragma once

#include "Raven/Core/Api.hpp"

#include <filesystem>

namespace Raven::Paths
{
	RAVEN_API std::filesystem::path GetExecutablePath();
	RAVEN_API std::filesystem::path GetExecutableDirectory();

	// Relative paths are normalized; rooted paths and lexical parent escapes are rejected.
	RAVEN_API std::filesystem::path GetContentPath(
		const std::filesystem::path& relativePath = {});
	RAVEN_API std::filesystem::path GetShaderPath(
		const std::filesystem::path& relativePath = {});
}
