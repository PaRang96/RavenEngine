#pragma once

#include "Raven/Platform/Input.hpp"

#include <string>

namespace Raven::Platform
{
	// The label the active keyboard layout prints on a physical key, in UTF-8 (for example "A"
	// for the Q position on AZERTY), or empty when the platform cannot name it. Callers fall back
	// to the shared English names (docs/GameServicesDesign.md, FD-29).
	std::string GetNativeKeyName(Key key);
}
