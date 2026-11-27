#include "Platform/PlatformFiles.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <condition_variable>
#include <mutex>
#include <vector>

namespace Raven::Platform
{
	struct DirectoryWatch::Impl
	{
		std::vector<HANDLE> Handles;
		bool Available = false;

		~Impl()
		{
			for (std::size_t index = 1; index < Handles.size(); ++index)
				FindCloseChangeNotification(Handles[index]);
			if (!Handles.empty())
				CloseHandle(Handles.front());
		}
	};

	DirectoryWatch::DirectoryWatch(std::span<const WatchDirectory> directories)
		: m_Impl(std::make_unique<Impl>())
	{
		if (directories.empty() || directories.size() >= MAXIMUM_WAIT_OBJECTS)
			return;
		m_Impl->Handles.reserve(directories.size() + 1);
		const HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!stop)
			return;
		m_Impl->Handles.push_back(stop);
		constexpr DWORD changes = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
			FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_CREATION;
		for (const auto& directory : directories)
		{
			const auto handle = FindFirstChangeNotificationW(directory.Path.c_str(), directory.Subtree, changes);
			if (handle == INVALID_HANDLE_VALUE)
				return;
			m_Impl->Handles.push_back(handle);
		}
		m_Impl->Available = true;
	}

	DirectoryWatch::~DirectoryWatch() = default;

	bool DirectoryWatch::IsAvailable() const noexcept
	{
		return m_Impl->Available;
	}

	bool DirectoryWatch::Wait(std::stop_token stop, std::chrono::milliseconds timeout)
	{
		if (!IsAvailable())
		{
			std::mutex mutex;
			std::unique_lock lock(mutex);
			std::condition_variable_any timer;
			timer.wait_for(lock, stop, timeout, [] { return false; });
			return false;
		}
		const std::stop_callback cancel(stop, [event = m_Impl->Handles.front()] { SetEvent(event); });
		const auto count = static_cast<DWORD>(m_Impl->Handles.size());
		const auto result = WaitForMultipleObjects(count, m_Impl->Handles.data(), FALSE,
			static_cast<DWORD>(timeout.count()));
		if (result == WAIT_FAILED)
		{
			m_Impl->Available = false;
			return true;
		}
		if (result <= WAIT_OBJECT_0 || result >= WAIT_OBJECT_0 + count)
			return false;
		if (!FindNextChangeNotification(m_Impl->Handles[result - WAIT_OBJECT_0]))
			m_Impl->Available = false;
		return true;
	}
}
