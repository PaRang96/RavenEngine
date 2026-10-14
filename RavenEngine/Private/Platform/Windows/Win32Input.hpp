#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include "Raven/Platform/Input.hpp"

namespace Raven
{
	Key TranslateWin32Key(WPARAM virtualKey, LPARAM keyData);
}
