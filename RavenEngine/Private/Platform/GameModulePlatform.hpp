#pragma once

#include <filesystem>


namespace Raven::Platform
{
	using GameEntryPoint = void (*)();
	void* LoadGameLibrary(const std::filesystem::path& path);
	GameEntryPoint FindGameEntryPoint(void* library, const char* name);
	void UnloadGameLibrary(void* library) noexcept;
	// Whether `address` lies inside the loaded image `library`. Activation checks that each
	// component's ComponentInfo is the new module's own copy, not one an earlier generation's image
	// supplied through symbol binding (AA2-HR-02).
	bool ImageContains(void* library, const void* address) noexcept;
}
