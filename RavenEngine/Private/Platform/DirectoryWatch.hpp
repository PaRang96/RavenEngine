#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <span>
#include <stop_token>

namespace Raven::Platform
{
	struct WatchDirectory
	{
		std::filesystem::path Path;
		bool Subtree = false;
	};

	// Notifications are wakeups only. The caller still validates observed contents.
	class DirectoryWatch
	{
	public:
		explicit DirectoryWatch(std::span<const WatchDirectory> directories);
		~DirectoryWatch();
		bool IsAvailable() const noexcept;
		bool Wait(std::stop_token stop, std::chrono::milliseconds timeout);

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
