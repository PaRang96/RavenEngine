#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Raven::Platform
{
	struct BuildProcess;
	// Whether this platform runs background builds (game modules, live shader compiles). The
	// fallback backend answers false until its OS implements them.
	bool SupportsBuildProcesses() noexcept;
	// Arguments are UTF-8. Output and errors go to `log`.
	BuildProcess* StartBuildProcess(const std::filesystem::path& executable,
		const std::vector<std::string>& arguments, const std::filesystem::path& log);
	bool PollBuildProcess(BuildProcess& process, int& exitCode);
	// Cancels only this build's process tree if still running.
	void DestroyBuildProcess(BuildProcess* process) noexcept;
}
