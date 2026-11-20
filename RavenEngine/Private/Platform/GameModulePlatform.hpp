#pragma once

#include <filesystem>


namespace Raven::Platform
{
	using GameEntryPoint = void (*)();
	void* LoadGameLibrary(const std::filesystem::path& path);
	GameEntryPoint FindGameEntryPoint(void* library, const char* name);
	void UnloadGameLibrary(void* library) noexcept;

}
