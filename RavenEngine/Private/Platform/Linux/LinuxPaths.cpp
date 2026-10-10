#include "Platform/PlatformPaths.hpp"

namespace Raven
{
	std::filesystem::path GetPlatformExecutablePath()
	{
		return std::filesystem::read_symlink("/proc/self/exe");
	}
}
