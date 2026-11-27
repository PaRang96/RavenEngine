#pragma once

#include "Raven/Platform/Input.hpp"

#include <string>

// Platform area: input (docs/PlatformLayerDesign.md). Implemented per OS under Platform/<OS>/,
// with Platform/Fallback/ where an OS has no native names yet (Capabilities.NativeKeyNames).
namespace Raven::Platform
{
	// The label the active keyboard layout prints on a physical key, in UTF-8 (for example "A"
	// for the Q position on AZERTY), or empty when the platform cannot name it. Callers fall back
	// to the shared English names (docs/GameServicesDesign.md, FD-29).
	std::string GetNativeKeyName(Key key);
}
