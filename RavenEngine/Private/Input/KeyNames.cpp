#include "Input/InputRouter.hpp"

namespace Raven
{
	std::string_view GetFallbackKeyName(Key key) noexcept
	{
		static constexpr std::string_view letters[]{ "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
			"N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z" };
		static constexpr std::string_view digits[]{ "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" };
		static constexpr std::string_view functions[]{ "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10",
			"F11", "F12" };
		static constexpr std::string_view highFunctions[]{ "F13", "F14", "F15", "F16", "F17", "F18", "F19", "F20",
			"F21", "F22", "F23", "F24" };
		static constexpr std::string_view numpad[]{ "Numpad 1", "Numpad 2", "Numpad 3", "Numpad 4", "Numpad 5",
			"Numpad 6", "Numpad 7", "Numpad 8", "Numpad 9", "Numpad 0" };

		const auto value = static_cast<std::uint16_t>(key);
		if (key >= Key::A && key <= Key::Z)
			return letters[value - static_cast<std::uint16_t>(Key::A)];
		if (key >= Key::Digit1 && key <= Key::Digit0)
			return digits[value - static_cast<std::uint16_t>(Key::Digit1)];
		if (key >= Key::F1 && key <= Key::F12)
			return functions[value - static_cast<std::uint16_t>(Key::F1)];
		if (key >= Key::F13 && key <= Key::F24)
			return highFunctions[value - static_cast<std::uint16_t>(Key::F13)];
		if (key >= Key::Numpad1 && key <= Key::Numpad0)
			return numpad[value - static_cast<std::uint16_t>(Key::Numpad1)];

		switch (key)
		{
		case Key::Enter: return "Enter";
		case Key::Escape: return "Escape";
		case Key::Backspace: return "Backspace";
		case Key::Tab: return "Tab";
		case Key::Space: return "Space";
		case Key::Minus: return "-";
		case Key::Equal: return "=";
		case Key::LeftBracket: return "[";
		case Key::RightBracket: return "]";
		case Key::Backslash: return "\\";
		case Key::NonUSHash: return "#";
		case Key::Semicolon: return ";";
		case Key::Apostrophe: return "'";
		case Key::Grave: return "`";
		case Key::Comma: return ",";
		case Key::Period: return ".";
		case Key::Slash: return "/";
		case Key::CapsLock: return "Caps Lock";
		case Key::PrintScreen: return "Print Screen";
		case Key::ScrollLock: return "Scroll Lock";
		case Key::Pause: return "Pause";
		case Key::Insert: return "Insert";
		case Key::Home: return "Home";
		case Key::PageUp: return "Page Up";
		case Key::Delete: return "Delete";
		case Key::End: return "End";
		case Key::PageDown: return "Page Down";
		case Key::Right: return "Right";
		case Key::Left: return "Left";
		case Key::Down: return "Down";
		case Key::Up: return "Up";
		case Key::NumLock: return "Num Lock";
		case Key::NumpadDivide: return "Numpad /";
		case Key::NumpadMultiply: return "Numpad *";
		case Key::NumpadSubtract: return "Numpad -";
		case Key::NumpadAdd: return "Numpad +";
		case Key::NumpadEnter: return "Numpad Enter";
		case Key::NumpadDecimal: return "Numpad .";
		case Key::NonUSBackslash: return "\\";
		case Key::Application: return "Menu";
		case Key::Power: return "Power";
		case Key::NumpadEqual: return "Numpad =";
		case Key::KeypadComma: return "Numpad ,";
		case Key::LeftControl: return "Left Ctrl";
		case Key::LeftShift: return "Left Shift";
		case Key::LeftAlt: return "Left Alt";
		case Key::LeftSuper: return "Left Super";
		case Key::RightControl: return "Right Ctrl";
		case Key::RightShift: return "Right Shift";
		case Key::RightAlt: return "Right Alt";
		case Key::RightSuper: return "Right Super";
		default: return "Unknown";
		}
	}
}
