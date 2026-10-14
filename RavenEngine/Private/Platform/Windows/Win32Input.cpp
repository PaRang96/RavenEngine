#include "Platform/Windows/Win32Input.hpp"

namespace Raven
{
	namespace
	{
		// Used only when the message has no physical scan code.
		Key TranslateScanlessKey(WPARAM virtualKey, bool extended)
		{
			if (virtualKey >= VK_F1 && virtualKey <= VK_F12)
				return static_cast<Key>(static_cast<std::uint16_t>(Key::F1) + virtualKey - VK_F1);
			if (virtualKey >= VK_NUMPAD0 && virtualKey <= VK_NUMPAD9)
			{
				if (virtualKey == VK_NUMPAD0)
					return Key::Numpad0;
				return static_cast<Key>(static_cast<std::uint16_t>(Key::Numpad1) + virtualKey - VK_NUMPAD1);
			}

			switch (virtualKey)
			{
			case VK_LSHIFT: return Key::LeftShift;
			case VK_RSHIFT: return Key::RightShift;
			case VK_LCONTROL: return Key::LeftControl;
			case VK_RCONTROL: return Key::RightControl;
			case VK_LMENU: return Key::LeftAlt;
			case VK_RMENU: return Key::RightAlt;
			case VK_CONTROL: return extended ? Key::RightControl : Key::LeftControl;
			case VK_MENU: return extended ? Key::RightAlt : Key::LeftAlt;
			case VK_LWIN: return Key::LeftSuper;
			case VK_RWIN: return Key::RightSuper;
			case VK_ESCAPE: return Key::Escape;
			case VK_RETURN: return extended ? Key::NumpadEnter : Key::Enter;
			case VK_BACK: return Key::Backspace;
			case VK_TAB: return Key::Tab;
			case VK_SPACE: return Key::Space;
			case VK_CAPITAL: return Key::CapsLock;
			case VK_NUMLOCK: return Key::NumLock;
			case VK_SCROLL: return Key::ScrollLock;
			case VK_MULTIPLY: return Key::NumpadMultiply;
			case VK_ADD: return Key::NumpadAdd;
			case VK_SUBTRACT: return Key::NumpadSubtract;
			case VK_DECIMAL: return Key::NumpadDecimal;
			case VK_DIVIDE: return Key::NumpadDivide;
			case VK_INSERT: return Key::Insert;
			case VK_HOME: return Key::Home;
			case VK_PRIOR: return Key::PageUp;
			case VK_DELETE: return Key::Delete;
			case VK_END: return Key::End;
			case VK_NEXT: return Key::PageDown;
			case VK_LEFT: return Key::Left;
			case VK_RIGHT: return Key::Right;
			case VK_UP: return Key::Up;
			case VK_DOWN: return Key::Down;
			case VK_APPS: return Key::Application;
			default: return Key::Unknown;
			}
		}
	}

	Key TranslateWin32Key(WPARAM virtualKey, LPARAM keyData)
	{
		const UINT flags = HIWORD(keyData);
		const UINT scanCode = LOBYTE(flags);
		const bool extended = (flags & KF_EXTENDED) != 0;

		// Pause/PrintScreen have alternate scan sequences; F24 shares a
		// scan code with a language key. Windows identifies these explicitly.
		if (virtualKey == VK_PAUSE || virtualKey == VK_CANCEL)
			return Key::Pause;
		if (virtualKey == VK_SNAPSHOT)
			return Key::PrintScreen;
		if (virtualKey >= VK_F13 && virtualKey <= VK_F24)
			return static_cast<Key>(static_cast<std::uint16_t>(Key::F13) + virtualKey - VK_F13);

		if (scanCode == 0)
			return TranslateScanlessKey(virtualKey, extended);

		if (extended)
		{
			switch (scanCode)
			{
			case 0x1C: return Key::NumpadEnter;
			case 0x1D: return Key::RightControl;
			case 0x35: return Key::NumpadDivide;
			case 0x37: return Key::PrintScreen;
			case 0x38: return Key::RightAlt;
			case 0x45: return Key::NumLock;
			case 0x46: return Key::Pause;
			case 0x47: return Key::Home;
			case 0x48: return Key::Up;
			case 0x49: return Key::PageUp;
			case 0x4B: return Key::Left;
			case 0x4D: return Key::Right;
			case 0x4F: return Key::End;
			case 0x50: return Key::Down;
			case 0x51: return Key::PageDown;
			case 0x52: return Key::Insert;
			case 0x53: return Key::Delete;
			case 0x5B: return Key::LeftSuper;
			case 0x5C: return Key::RightSuper;
			case 0x5D: return Key::Application;
			case 0x5E: return Key::Power;
			default: return Key::Unknown;
			}
		}

		if (scanCode >= 0x02 && scanCode <= 0x0A)
			return static_cast<Key>(static_cast<std::uint16_t>(Key::Digit1) + scanCode - 0x02);
		if (scanCode >= 0x3B && scanCode <= 0x44)
			return static_cast<Key>(static_cast<std::uint16_t>(Key::F1) + scanCode - 0x3B);
		if (scanCode >= 0x64 && scanCode <= 0x6E)
			return static_cast<Key>(static_cast<std::uint16_t>(Key::F13) + scanCode - 0x64);

		// Physical key positions stay stable across layouts and NumLock.
		switch (scanCode)
		{
		case 0x01: return Key::Escape;
		case 0x0B: return Key::Digit0;
		case 0x0C: return Key::Minus;
		case 0x0D: return Key::Equal;
		case 0x0E: return Key::Backspace;
		case 0x0F: return Key::Tab;
		case 0x10: return Key::Q;
		case 0x11: return Key::W;
		case 0x12: return Key::E;
		case 0x13: return Key::R;
		case 0x14: return Key::T;
		case 0x15: return Key::Y;
		case 0x16: return Key::U;
		case 0x17: return Key::I;
		case 0x18: return Key::O;
		case 0x19: return Key::P;
		case 0x1A: return Key::LeftBracket;
		case 0x1B: return Key::RightBracket;
		case 0x1C: return Key::Enter;
		case 0x1D: return Key::LeftControl;
		case 0x1E: return Key::A;
		case 0x1F: return Key::S;
		case 0x20: return Key::D;
		case 0x21: return Key::F;
		case 0x22: return Key::G;
		case 0x23: return Key::H;
		case 0x24: return Key::J;
		case 0x25: return Key::K;
		case 0x26: return Key::L;
		case 0x27: return Key::Semicolon;
		case 0x28: return Key::Apostrophe;
		case 0x29: return Key::Grave;
		case 0x2A: return Key::LeftShift;
		case 0x2B: return Key::Backslash;
		case 0x2C: return Key::Z;
		case 0x2D: return Key::X;
		case 0x2E: return Key::C;
		case 0x2F: return Key::V;
		case 0x30: return Key::B;
		case 0x31: return Key::N;
		case 0x32: return Key::M;
		case 0x33: return Key::Comma;
		case 0x34: return Key::Period;
		case 0x35: return Key::Slash;
		case 0x36: return Key::RightShift;
		case 0x37: return Key::NumpadMultiply;
		case 0x38: return Key::LeftAlt;
		case 0x39: return Key::Space;
		case 0x3A: return Key::CapsLock;
		case 0x45: return Key::NumLock;
		case 0x46: return Key::ScrollLock;
		case 0x47: return Key::Numpad7;
		case 0x48: return Key::Numpad8;
		case 0x49: return Key::Numpad9;
		case 0x4A: return Key::NumpadSubtract;
		case 0x4B: return Key::Numpad4;
		case 0x4C: return Key::Numpad5;
		case 0x4D: return Key::Numpad6;
		case 0x4E: return Key::NumpadAdd;
		case 0x4F: return Key::Numpad1;
		case 0x50: return Key::Numpad2;
		case 0x51: return Key::Numpad3;
		case 0x52: return Key::Numpad0;
		case 0x53: return Key::NumpadDecimal;
		case 0x54: return Key::PrintScreen;
		case 0x56: return Key::NonUSBackslash;
		case 0x57: return Key::F11;
		case 0x58: return Key::F12;
		case 0x59: return Key::NumpadEqual;
		default: return Key::Unknown;
		}
	}
}
