#include "Platform/PlatformPaths.hpp"

#include <mach-o/dyld.h>

#include <cstdint>
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
}
