#pragma once

#include "Raven/Platform/Input.hpp"

#import <Cocoa/Cocoa.h>

namespace Raven
{
    // Returns true when AppKit's default key/scroll handling should be skipped.
    bool ApplyCocoaInputEvent(InputState& input, NSEvent* event, NSView* view);
    void ReleaseCocoaInput(InputState& input);
}
