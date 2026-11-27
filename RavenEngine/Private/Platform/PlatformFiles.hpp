#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <span>
#include <stop_token>

// Platform area: files (docs/PlatformLayerDesign.md). Implemented per OS under Platform/<OS>/,
// shared POSIX code under Platform/Posix/, and the polling watch under Platform/Fallback/.
namespace Raven::Platform
{
	// Replaces `target` with `temporary` atomically, after flushing it to disk, so a crash leaves
	// either the old file or the new one.
	void ReplaceSavedFile(const std::filesystem::path& temporary, const std::filesystem::path& target);

	struct WatchDirectory
	{
		std::filesystem::path Path;
		bool Subtree = false;
	};

	// Directory change notifications. They are wakeups only: the caller still validates what it
	// observes. Without a native watch (Capabilities.NativeDirectoryWatch), IsAvailable is false and
	// callers poll.
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
