#pragma once

// Platform area: capabilities (docs/PlatformLayerDesign.md). What this build's platform backends
// implement, known at compile time, in the spirit of Unreal's FPlatformProperties and O3DE's
// platform traits. Shared code branches on these instead of testing an OS macro or waiting for a
// fallback to throw. Each OS states its values in Platform/<OS>/PlatformCapabilitiesOs.hpp, which
// RavenPlatform.cmake puts on the include path; a backend that gains a feature updates its value
// in the same change (docs/PlatformHandoff.md, port contract).
namespace Raven::Platform
{
	struct PlatformCapabilities
	{
		// For logs and reports, such as "Windows".
		const char* Name = "";
		// StartBuildProcess runs builds (PlatformProcess.hpp): automatic game builds, live shaders.
		bool BuildProcesses = false;
		// LoadGameLibrary loads and replaces game modules while running.
		bool GameModuleReload = false;
		// DirectoryWatch notifies instead of being polled (PlatformFiles.hpp).
		bool NativeDirectoryWatch = false;
		// GetNativeKeyName names keys by the active layout (PlatformInput.hpp).
		bool NativeKeyNames = false;
	};
}

#include "PlatformCapabilitiesOs.hpp"
