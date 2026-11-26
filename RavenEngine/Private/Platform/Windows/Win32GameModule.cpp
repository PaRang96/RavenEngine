#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include "Platform/GameModulePlatform.hpp"

#include <stdexcept>
#include <system_error>

namespace Raven::Platform
{
	namespace
	{
		[[noreturn]] void Fail(const char* operation)
		{
			throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), operation);
		}
	}

	void* LoadGameLibrary(const std::filesystem::path& path)
	{
		auto library = LoadLibraryExW(path.c_str(), nullptr,
			LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (!library)
			Fail("Load game library");
		return library;
	}

	GameEntryPoint FindGameEntryPoint(void* library, const char* name)
	{
		auto entry = GetProcAddress(static_cast<HMODULE>(library), name);
		if (!entry)
			Fail(name);
		return reinterpret_cast<GameEntryPoint>(entry);
	}

	void UnloadGameLibrary(void* library) noexcept
	{
		if (library)
			FreeLibrary(static_cast<HMODULE>(library));
	}
}
