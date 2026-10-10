#include "Platform/PlatformPaths.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <stdexcept>
#include <system_error>
#include <vector>

namespace Raven
{
	std::filesystem::path GetPlatformExecutablePath()
	{
		std::vector<wchar_t> buffer(MAX_PATH);
		for (;;)
		{
			const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
				static_cast<DWORD>(buffer.size()));
			if (length == 0)
				throw std::system_error(static_cast<int>(GetLastError()),
					std::system_category(), "Cannot locate the executable");
			if (length < buffer.size())
				return std::filesystem::path(buffer.begin(), buffer.begin() + length);
			if (buffer.size() >= 32768)
				throw std::runtime_error("Executable path exceeds the Windows path limit");
			buffer.resize(buffer.size() < 16384 ? buffer.size() * 2 : 32768);
		}
	}
}
