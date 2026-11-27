#pragma once

#include <filesystem>

// Platform area: paths (docs/PlatformLayerDesign.md). Implemented per OS under Platform/<OS>/.
namespace Raven::Platform
{
	std::filesystem::path GetExecutablePath();
	// The OS per-user data folder (docs/GameServicesDesign.md, FD-33): %LOCALAPPDATA% on Windows,
	// ~/Library/Application Support on macOS, $XDG_DATA_HOME or ~/.local/share on Linux. Throws when
	// the platform cannot name one.
	std::filesystem::path GetUserDataDirectory();
}
