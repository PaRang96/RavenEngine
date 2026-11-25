#include "Platform/PlatformKeyNames.hpp"

namespace Raven::Platform
{
	// macOS and Linux name keys through their M4 backends (UCKeyTranslate, xkbcommon). Until then
	// every key uses the shared English name.
	std::string GetNativeKeyName(Key)
	{
		return {};
	}
}
