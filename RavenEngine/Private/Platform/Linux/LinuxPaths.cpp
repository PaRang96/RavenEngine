#include "Platform/PlatformPaths.hpp"

#include <cstdlib>
#include <stdexcept>

namespace Raven::Platform
{
	std::filesystem::path GetExecutablePath()
	{
		return std::filesystem::read_symlink("/proc/self/exe");
	}

	std::filesystem::path GetUserDataDirectory()
	{
		// The XDG base directory rule: an absolute $XDG_DATA_HOME, else ~/.local/share.
		if (const char* data = std::getenv("XDG_DATA_HOME"); data && *data == '/')
			return data;
		if (const char* home = std::getenv("HOME"); home && *home)
			return std::filesystem::path(home) / ".local" / "share";
		throw std::runtime_error("Neither XDG_DATA_HOME nor HOME names a user data folder");
	}
}
