#pragma once

// Linux follows Windows through these interfaces (M4). Until a backend lands, Platform/Fallback/
// provides it and the value here stays false; the backend's change sets it.
namespace Raven::Platform
{
	inline constexpr PlatformCapabilities Capabilities{
		.Name = "Linux",
		.BuildProcesses = false,
		.GameModuleReload = false,
		.NativeDirectoryWatch = false,
		.NativeKeyNames = false,
	};
}
