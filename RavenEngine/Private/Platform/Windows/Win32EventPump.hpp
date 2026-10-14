#pragma once

namespace Raven::Win32EventPump
{
	// The message queue and quit request belong to the calling UI thread.
	void PollEvents();
	bool IsQuitRequested();
}
