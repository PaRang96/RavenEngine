#include "Platform/PlatformPaths.hpp"

#include <mach-o/dyld.h>

#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <vector>

namespace Raven
{
    std::filesystem::path GetPlatformExecutablePath()
    {
        std::uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::vector<char> buffer(size);
        if (_NSGetExecutablePath(buffer.data(), &size) != 0)
            throw std::runtime_error("Could not get the macOS executable path");

        return std::filesystem::canonical(buffer.data());
    }

    std::filesystem::path GetPlatformUserDataDirectory()
    {
        if (const char* home = std::getenv("HOME"); home && *home)
            return std::filesystem::path(home) / "Library" / "Application Support";
        throw std::runtime_error("HOME does not name a user data folder");
    }
}
