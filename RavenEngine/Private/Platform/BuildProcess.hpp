#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Raven::Platform
{
	struct BuildProcess;
	BuildProcess* StartBuildProcess(const std::filesystem::path& executable,
		const std::vector<std::wstring>& arguments, const std::filesystem::path& log);
	bool PollBuildProcess(BuildProcess& process, int& exitCode);
	// Cancels only this build's process tree if still running.
	void DestroyBuildProcess(BuildProcess* process) noexcept;
}
