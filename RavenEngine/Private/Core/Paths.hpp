#pragma once

#include <filesystem>

namespace Raven
{
	// Only Application can establish a process-wide content root for its lifetime.
	class ApplicationPathsScope
	{
	public:
		explicit ApplicationPathsScope(const std::filesystem::path& contentRoot);
		~ApplicationPathsScope();

		ApplicationPathsScope(const ApplicationPathsScope&) = delete;
		ApplicationPathsScope& operator=(const ApplicationPathsScope&) = delete;
	};
}
