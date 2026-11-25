#pragma once

#include <filesystem>

namespace Raven
{
	std::filesystem::path GetPlatformExecutablePath();
	// The OS per-user data folder (docs/GameServicesDesign.md, FD-33): %LOCALAPPDATA% on Windows,
	// ~/Library/Application Support on macOS, $XDG_DATA_HOME or ~/.local/share on Linux. Throws when
	// the platform cannot name one.
	std::filesystem::path GetPlatformUserDataDirectory();
}
