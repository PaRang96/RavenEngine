#include "Platform/GameModulePlatform.hpp"
#include <stdexcept>

namespace Raven::Platform
{
	void* LoadGameLibrary(const std::filesystem::path&)
	{ throw std::runtime_error("Game module replacement currently requires Windows"); }
	GameEntryPoint FindGameEntryPoint(void*, const char*) { return nullptr; }
	void UnloadGameLibrary(void*) noexcept {}
	GameBuildProcess* StartGameBuild(const std::filesystem::path&,
		const std::vector<std::wstring>&, const std::filesystem::path&)
	{ throw std::runtime_error("Automatic game builds currently require Windows"); }
	bool PollGameBuild(GameBuildProcess&, int&) { return false; }
	void DestroyGameBuild(GameBuildProcess*) noexcept {}
}
