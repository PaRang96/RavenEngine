#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include "Platform/AtomicFile.hpp"

#include <chrono>
#include <system_error>
#include <thread>

namespace Raven::Platform
{
	void ReplaceSavedFile(const std::filesystem::path& temporary, const std::filesystem::path& target)
	{
		for (int attempt = 0; ; ++attempt)
		{
			if (MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
				return;
			const auto error = GetLastError();
			if ((error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED) || attempt == 20)
				throw std::system_error(static_cast<int>(error), std::system_category(), "Replace saved file");
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}
}
