#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Platform area: processes and loadable images (docs/PlatformLayerDesign.md). Implemented per OS
// under Platform/<OS>/, with Platform/Fallback/ where an OS has none yet; check
// Capabilities.BuildProcesses and Capabilities.GameModuleReload before relying on either.
namespace Raven::Platform
{
	// Background builds: game modules and live shader compiles. Arguments are UTF-8; output and
	// errors go to `log`.
	struct BuildProcess;
	BuildProcess* StartBuildProcess(const std::filesystem::path& executable,
		const std::vector<std::string>& arguments, const std::filesystem::path& log);
	bool PollBuildProcess(BuildProcess& process, int& exitCode);
	// Cancels only this build's process tree if still running.
	void DestroyBuildProcess(BuildProcess* process) noexcept;

	// Game modules (docs/EngineModules.md, module contract).
	using GameEntryPoint = void (*)();
	void* LoadGameLibrary(const std::filesystem::path& path);
	GameEntryPoint FindGameEntryPoint(void* library, const char* name);
	void UnloadGameLibrary(void* library) noexcept;
	// Whether `address` lies inside the loaded image `library`. Activation checks that each
	// component's ComponentInfo is the new module's own copy, not one an earlier generation's image
	// supplied through symbol binding (AA2-HR-02).
	bool ImageContains(void* library, const void* address) noexcept;
}
