#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Raven::Platform
{
	using GameEntryPoint = void (*)();
	void* LoadGameLibrary(const std::filesystem::path& path);
	GameEntryPoint FindGameEntryPoint(void* library, const char* name);
	void UnloadGameLibrary(void* library) noexcept;

	struct GameBuildProcess;
	GameBuildProcess* StartGameBuild(const std::filesystem::path& executable,
		const std::vector<std::wstring>& arguments, const std::filesystem::path& log);
	bool PollGameBuild(GameBuildProcess& process, int& exitCode);
	// Cancels only this build's process tree if still running.
	void DestroyGameBuild(GameBuildProcess* process) noexcept;
}
