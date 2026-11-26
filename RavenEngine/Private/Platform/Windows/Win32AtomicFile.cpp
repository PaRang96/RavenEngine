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
		// The new contents reach the disk before the rename, so a power loss right after a save
		// leaves the old file or the new one, never an empty one. MOVEFILE_WRITE_THROUGH then makes
		// the rename itself durable.
		const HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Open saved file to flush");
		const bool flushed = FlushFileBuffers(file) != 0;
		const auto flushError = GetLastError();
		CloseHandle(file);
		if (!flushed)
			throw std::system_error(static_cast<int>(flushError), std::system_category(), "Flush saved file");
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
