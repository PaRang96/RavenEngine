#pragma once

#include "Raven/Core/Api.hpp"

#include <filesystem>

namespace Raven::Paths
{
	RAVEN_API std::filesystem::path GetExecutablePath();
	RAVEN_API std::filesystem::path GetExecutableDirectory();
	// The OS per-user data folder: %LOCALAPPDATA%, ~/Library/Application Support, or
	// $XDG_DATA_HOME (~/.local/share). Throws when the platform cannot name one.
	RAVEN_API std::filesystem::path GetUserDataDirectory();

	// Relative paths are normalized; rooted paths and lexical parent escapes are rejected.
	RAVEN_API std::filesystem::path GetContentPath(
		const std::filesystem::path& relativePath = {});
	RAVEN_API std::filesystem::path GetShaderPath(
		const std::filesystem::path& relativePath = {});
}
