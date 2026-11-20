#include "Platform/GameModulePlatform.hpp"
#include "Platform/BuildProcess.hpp"
#include <stdexcept>

namespace Raven::Platform
{
	void* LoadGameLibrary(const std::filesystem::path&)
	{ throw std::runtime_error("Game module replacement currently requires Windows"); }
	GameEntryPoint FindGameEntryPoint(void*, const char*) { return nullptr; }
	void UnloadGameLibrary(void*) noexcept {}
	BuildProcess* StartBuildProcess(const std::filesystem::path&,
		const std::vector<std::wstring>&, const std::filesystem::path&)
	{ throw std::runtime_error("Background build processes currently require Windows"); }
	bool PollBuildProcess(BuildProcess&, int&) { return false; }
	void DestroyBuildProcess(BuildProcess*) noexcept {}
}
