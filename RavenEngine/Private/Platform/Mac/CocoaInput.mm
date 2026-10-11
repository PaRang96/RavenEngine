#include "Platform/Mac/CocoaInput.hpp"

#include <Carbon/Carbon.h>
#include <IOKit/hidsystem/IOLLEvent.h>

#include <cmath>

namespace Raven
{
    namespace
    {
        Key TranslateKey(unsigned short code)
        {
            // NSEvent key codes identify physical keys, independent of the IME.
            switch (code)
            {
            case kVK_ANSI_A: return Key::A;
            case kVK_ANSI_B: return Key::B;
            case kVK_ANSI_C: return Key::C;
            case kVK_ANSI_D: return Key::D;
            case kVK_ANSI_E: return Key::E;
            case kVK_ANSI_F: return Key::F;
            case kVK_ANSI_G: return Key::G;
            case kVK_ANSI_H: return Key::H;
            case kVK_ANSI_I: return Key::I;
            case kVK_ANSI_J: return Key::J;
            case kVK_ANSI_K: return Key::K;
            case kVK_ANSI_L: return Key::L;
            case kVK_ANSI_M: return Key::M;
            case kVK_ANSI_N: return Key::N;
            case kVK_ANSI_O: return Key::O;
            case kVK_ANSI_P: return Key::P;
            case kVK_ANSI_Q: return Key::Q;
            case kVK_ANSI_R: return Key::R;
            case kVK_ANSI_S: return Key::S;
            case kVK_ANSI_T: return Key::T;
            case kVK_ANSI_U: return Key::U;
            case kVK_ANSI_V: return Key::V;
            case kVK_ANSI_W: return Key::W;
            case kVK_ANSI_X: return Key::X;
            case kVK_ANSI_Y: return Key::Y;
            case kVK_ANSI_Z: return Key::Z;
            case kVK_ANSI_0: return Key::Digit0;
            case kVK_ANSI_1: return Key::Digit1;
            case kVK_ANSI_2: return Key::Digit2;
            case kVK_ANSI_3: return Key::Digit3;
            case kVK_ANSI_4: return Key::Digit4;
            case kVK_ANSI_5: return Key::Digit5;
            case kVK_ANSI_6: return Key::Digit6;
            case kVK_ANSI_7: return Key::Digit7;
            case kVK_ANSI_8: return Key::Digit8;
            case kVK_ANSI_9: return Key::Digit9;
            case kVK_Return: return Key::Enter;
            case kVK_Escape: return Key::Escape;
            case kVK_Delete: return Key::Backspace;
            case kVK_Tab: return Key::Tab;
            case kVK_Space: return Key::Space;
            case kVK_ANSI_Minus: return Key::Minus;
            case kVK_ANSI_Equal: return Key::Equal;
            case kVK_ANSI_LeftBracket: return Key::LeftBracket;
            case kVK_ANSI_RightBracket: return Key::RightBracket;
            case kVK_ANSI_Backslash: return Key::Backslash;
            case kVK_ANSI_Semicolon: return Key::Semicolon;
            case kVK_ANSI_Quote: return Key::Apostrophe;
            case kVK_ANSI_Grave: return Key::Grave;
            case kVK_ANSI_Comma: return Key::Comma;
            case kVK_ANSI_Period: return Key::Period;
            case kVK_ANSI_Slash: return Key::Slash;
            case kVK_CapsLock: return Key::CapsLock;
            case kVK_F1: return Key::F1;
            case kVK_F2: return Key::F2;
            case kVK_F3: return Key::F3;
            case kVK_F4: return Key::F4;
            case kVK_F5: return Key::F5;
            case kVK_F6: return Key::F6;
            case kVK_F7: return Key::F7;
            case kVK_F8: return Key::F8;
            case kVK_F9: return Key::F9;
            case kVK_F10: return Key::F10;
            case kVK_F11: return Key::F11;
            case kVK_F12: return Key::F12;
            case kVK_F13: return Key::F13;
            case kVK_F14: return Key::F14;
            case kVK_F15: return Key::F15;
            case kVK_F16: return Key::F16;
            case kVK_F17: return Key::F17;
            case kVK_F18: return Key::F18;
            case kVK_F19: return Key::F19;
            case kVK_F20: return Key::F20;
            case kVK_Help: return Key::Insert;
            case kVK_Home: return Key::Home;
            case kVK_PageUp: return Key::PageUp;
            case kVK_ForwardDelete: return Key::Delete;
            case kVK_End: return Key::End;
            case kVK_PageDown: return Key::PageDown;
            case kVK_LeftArrow: return Key::Left;
            case kVK_RightArrow: return Key::Right;
            case kVK_UpArrow: return Key::Up;
            case kVK_DownArrow: return Key::Down;
            case kVK_ANSI_KeypadClear: return Key::NumLock;
            case kVK_ANSI_KeypadDivide: return Key::NumpadDivide;
            case kVK_ANSI_KeypadMultiply: return Key::NumpadMultiply;
            case kVK_ANSI_KeypadMinus: return Key::NumpadSubtract;
            case kVK_ANSI_KeypadPlus: return Key::NumpadAdd;
            case kVK_ANSI_KeypadEnter: return Key::NumpadEnter;
            case kVK_ANSI_Keypad0: return Key::Numpad0;
            case kVK_ANSI_Keypad1: return Key::Numpad1;
            case kVK_ANSI_Keypad2: return Key::Numpad2;
            case kVK_ANSI_Keypad3: return Key::Numpad3;
            case kVK_ANSI_Keypad4: return Key::Numpad4;
            case kVK_ANSI_Keypad5: return Key::Numpad5;
            case kVK_ANSI_Keypad6: return Key::Numpad6;
            case kVK_ANSI_Keypad7: return Key::Numpad7;
            case kVK_ANSI_Keypad8: return Key::Numpad8;
            case kVK_ANSI_Keypad9: return Key::Numpad9;
            case kVK_ANSI_KeypadDecimal: return Key::NumpadDecimal;
            case kVK_ANSI_KeypadEquals: return Key::NumpadEqual;
            case kVK_ISO_Section: return Key::NonUSBackslash;
            case kVK_JIS_Yen: return Key::International3;
            case kVK_JIS_Underscore: return Key::International1;
            case kVK_JIS_KeypadComma: return Key::KeypadComma;
            case kVK_JIS_Eisu: return Key::Lang2;
            case kVK_JIS_Kana: return Key::Lang1;
            case kVK_Control: return Key::LeftControl;
            case kVK_Shift: return Key::LeftShift;
            case kVK_Option: return Key::LeftAlt;
            case kVK_Command: return Key::LeftSuper;
            case kVK_RightControl: return Key::RightControl;
            case kVK_RightShift: return Key::RightShift;
            case kVK_RightOption: return Key::RightAlt;
            case kVK_RightCommand: return Key::RightSuper;
            default: return Key::Unknown;
            }
        }

        void UpdateModifierPair(InputState& input, NSEventModifierFlags flags,
            NSEventModifierFlags genericMask, NSUInteger leftMask, NSUInteger rightMask,
            Key left, Key right, Key changedKey)
        {
            bool leftDown = false;
            bool rightDown = false;
            if (flags & genericMask)
            {
                if (flags & (leftMask | rightMask))
                {
                    leftDown = (flags & leftMask) != 0;
                    rightDown = (flags & rightMask) != 0;
                }
                else
                {
                    // Some synthesized events omit the device-specific flags.
                    leftDown = input.IsKeyDown(left);
                    rightDown = input.IsKeyDown(right);
                    if (changedKey == left)
                        leftDown = !leftDown;
                    else if (changedKey == right)
                        rightDown = !rightDown;
                    else if (!leftDown && !rightDown)
                        leftDown = true;
                }
            }
            input.SetKeyDown(left, leftDown);
            input.SetKeyDown(right, rightDown);
        }

        void UpdateModifiers(InputState& input, NSEventModifierFlags flags,
            Key changedKey = Key::Unknown)
        {
            UpdateModifierPair(input, flags, NSEventModifierFlagShift,
                NX_DEVICELSHIFTKEYMASK, NX_DEVICERSHIFTKEYMASK,
                Key::LeftShift, Key::RightShift, changedKey);
            UpdateModifierPair(input, flags, NSEventModifierFlagControl,
                NX_DEVICELCTLKEYMASK, NX_DEVICERCTLKEYMASK,
                Key::LeftControl, Key::RightControl, changedKey);
            UpdateModifierPair(input, flags, NSEventModifierFlagOption,
                NX_DEVICELALTKEYMASK, NX_DEVICERALTKEYMASK,
                Key::LeftAlt, Key::RightAlt, changedKey);
            UpdateModifierPair(input, flags, NSEventModifierFlagCommand,
                NX_DEVICELCMDKEYMASK, NX_DEVICERCMDKEYMASK,
                Key::LeftSuper, Key::RightSuper, changedKey);
            input.SetKeyDown(Key::CapsLock, (flags & NSEventModifierFlagCapsLock) != 0);
        }

        void UpdateButton(ButtonState& state, bool down)
        {
            if (state.Down == down)
                return;
            state.Down = down;
            if (down)
                state.Pressed = true;
            else
                state.Released = true;
        }

        bool HasMouseButtonDown(const InputState& input)
        {
            for (const auto& button : input.MouseButtons)
                if (button.Down)
                    return true;
            return false;
        }

        void UpdateCursor(InputState& input, NSView* view, NSPoint point)
        {
            const NSPoint pixel = [view convertPointToBacking:point];
            const NSRect bounds = [view convertRectToBacking:view.bounds];
            const auto x = static_cast<std::int32_t>(std::lround(pixel.x - NSMinX(bounds)));
            const auto y = static_cast<std::int32_t>(std::lround(view.flipped
                ? pixel.y - NSMinY(bounds) : NSMaxY(bounds) - pixel.y));
            if (input.HasCursorPosition)
            {
                input.CursorDeltaX += x - input.CursorX;
                input.CursorDeltaY += y - input.CursorY;
            }
            input.CursorX = x;
            input.CursorY = y;
            input.HasCursorPosition = true;
        }
    }

    void ReleaseCocoaInput(InputState& input)
    {
        input.ReleaseAllKeys();
        for (auto& button : input.MouseButtons)
            UpdateButton(button, false);
        input.HasCursorPosition = false;
    }

    bool ApplyCocoaInputEvent(InputState& input, NSEvent* event, NSView* view)
    {
        switch (event.type)
        {
        case NSEventTypeKeyDown:
        case NSEventTypeKeyUp:
        {
            UpdateModifiers(input, event.modifierFlags);
            const Key key = TranslateKey(event.keyCode);
            input.SetKeyDown(key, event.type == NSEventTypeKeyDown);
            return key != Key::Unknown;
        }
        case NSEventTypeFlagsChanged:
            UpdateModifiers(input, event.modifierFlags, TranslateKey(event.keyCode));
            return true;
        case NSEventTypeMouseMoved:
        case NSEventTypeLeftMouseDragged:
        case NSEventTypeRightMouseDragged:
        case NSEventTypeOtherMouseDragged:
        case NSEventTypeLeftMouseDown:
        case NSEventTypeLeftMouseUp:
        case NSEventTypeRightMouseDown:
        case NSEventTypeRightMouseUp:
        case NSEventTypeOtherMouseDown:
        case NSEventTypeOtherMouseUp:
        case NSEventTypeScrollWheel:
            break;
        default:
            return false;
        }

        if (!view)
            return false;
        const NSPoint point = [view convertPoint:event.locationInWindow fromView:nil];
        // Leave title-bar controls alone, but retain drags/releases outside content.
        if (!NSPointInRect(point, view.bounds) && !HasMouseButtonDown(input))
            return false;
        UpdateCursor(input, view, point);

        switch (event.type)
        {
        case NSEventTypeScrollWheel:
        {
            // Convert trackpad points to wheel-like steps; right/up are positive.
            const float scale = event.hasPreciseScrollingDeltas ? 0.1f : 1.0f;
            const float direction = event.directionInvertedFromDevice ? -1.0f : 1.0f;
            input.AddScroll(-static_cast<float>(event.scrollingDeltaX) * scale * direction,
                static_cast<float>(event.scrollingDeltaY) * scale * direction);
            return true;
        }
        case NSEventTypeLeftMouseDown:
        case NSEventTypeLeftMouseUp:
            UpdateButton(input.MouseButtons[static_cast<std::size_t>(MouseButton::Left)],
                event.type == NSEventTypeLeftMouseDown);
            break;
        case NSEventTypeRightMouseDown:
        case NSEventTypeRightMouseUp:
            UpdateButton(input.MouseButtons[static_cast<std::size_t>(MouseButton::Right)],
                event.type == NSEventTypeRightMouseDown);
            break;
        case NSEventTypeOtherMouseDown:
        case NSEventTypeOtherMouseUp:
            if (event.buttonNumber >= 2 && event.buttonNumber <= 4)
                UpdateButton(input.MouseButtons[static_cast<std::size_t>(event.buttonNumber)],
                    event.type == NSEventTypeOtherMouseDown);
            break;
        default:
            break;
        }
        return false;
    }
}
