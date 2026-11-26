#include "Platform/GameModulePlatform.hpp"
#include <stdexcept>

namespace Raven::Platform
{
	void* LoadGameLibrary(const std::filesystem::path&)
	{ throw std::runtime_error("Game module replacement currently requires Windows"); }
	GameEntryPoint FindGameEntryPoint(void*, const char*) { return nullptr; }
	void UnloadGameLibrary(void*) noexcept {}
}
