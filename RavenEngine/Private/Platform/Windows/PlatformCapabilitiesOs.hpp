#pragma once

// Windows, the head platform (planning decision 21): every capability has a native backend.
namespace Raven::Platform
{
	inline constexpr PlatformCapabilities Capabilities{
		.Name = "Windows",
		.BuildProcesses = true,
		.GameModuleReload = true,
		.NativeDirectoryWatch = true,
		.NativeKeyNames = true,
	};
}
