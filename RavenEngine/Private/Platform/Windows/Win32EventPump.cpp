#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include "Platform/Windows/Win32EventPump.hpp"

namespace Raven::Win32EventPump
{
	namespace
	{
		thread_local bool QuitRequested = false;
	}

	void PollEvents()
	{
		MSG message{};
		while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
		{
			if (message.message == WM_QUIT)
			{
				QuitRequested = true;
				continue;
			}
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
	}

	bool IsQuitRequested()
	{
		return QuitRequested;
	}
}
