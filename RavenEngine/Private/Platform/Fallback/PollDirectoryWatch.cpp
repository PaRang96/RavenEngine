#include "Platform/PlatformFiles.hpp"

#include <condition_variable>
#include <mutex>

namespace Raven::Platform
{
	struct DirectoryWatch::Impl {};

	DirectoryWatch::DirectoryWatch(std::span<const WatchDirectory>) {}
	DirectoryWatch::~DirectoryWatch() = default;
	bool DirectoryWatch::IsAvailable() const noexcept { return false; }

	bool DirectoryWatch::Wait(std::stop_token stop, std::chrono::milliseconds timeout)
	{
		std::mutex mutex;
		std::unique_lock lock(mutex);
		std::condition_variable_any timer;
		timer.wait_for(lock, stop, timeout, [] { return false; });
		return false;
	}
}
