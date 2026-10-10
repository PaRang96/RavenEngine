#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Raven
{
	enum class Key : std::uint16_t
	{
		Unknown = 0x00,

		A = 0x04, B, C, D, E, F, G, H, I, J, K, L, M,
		N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

		Digit1 = 0x1E, Digit2, Digit3, Digit4, Digit5,
		Digit6, Digit7, Digit8, Digit9, Digit0,

		Enter = 0x28, Escape, Backspace, Tab, Space,
		Minus = 0x2D, Equal, LeftBracket, RightBracket, Backslash,
		NonUSHash = 0x32, Semicolon, Apostrophe, Grave,
		Comma, Period, Slash, CapsLock,

		F1 = 0x3A, F2, F3, F4, F5, F6,
		F7, F8, F9, F10, F11, F12,

		PrintScreen = 0x46, ScrollLock, Pause,
		Insert = 0x49, Home, PageUp, Delete, End, PageDown,
		Right, Left, Down, Up,

		NumLock = 0x53, NumpadDivide, NumpadMultiply,
		NumpadSubtract, NumpadAdd, NumpadEnter,
		Numpad1 = 0x59, Numpad2, Numpad3, Numpad4, Numpad5,
		Numpad6, Numpad7, Numpad8, Numpad9,
		Numpad0, NumpadDecimal,

		NonUSBackslash = 0x64, Application, Power, NumpadEqual,

		F13 = 0x68, F14, F15, F16, F17, F18,
		F19, F20, F21, F22, F23, F24,

		KeypadComma = 0x85, KeypadEqualSign,
		International1 = 0x87, International2, International3,
		International4, International5, International6,
		International7, International8, International9,
		Lang1 = 0x90, Lang2, Lang3, Lang4, Lang5,
		Lang6, Lang7, Lang8, Lang9,

		LeftControl = 0xE0, LeftShift, LeftAlt, LeftSuper,
		RightControl, RightShift, RightAlt, RightSuper
	};

	enum class MouseButton : std::uint8_t
	{
		Left, Right, Middle, X1, X2, Count,
	};

	enum class Modifier : std::uint8_t
	{
		None = 0,
		Shift = 1 << 0,
		Control = 1 << 1,
		Alt = 1 << 2,
		Super = 1 << 3,
	};

	constexpr Modifier operator|(Modifier left, Modifier right)
	{
		return static_cast<Modifier>(
			static_cast<std::uint8_t>(left) |
			static_cast<std::uint8_t>(right));
	}

	struct ButtonState
	{
		bool Down = false;
		bool Pressed = false;
		bool Released = false;
	};

	enum class KeyTransition : std::uint8_t
	{
		Pressed,
		Released
	};

	struct KeyEvent
	{
		Key Code = Key::Unknown;
		KeyTransition Transition = KeyTransition::Pressed;
		Modifier Modifiers = Modifier::None;
	};

	struct InputState
	{
		static constexpr std::size_t KeyCount = 256;
		static constexpr std::size_t MouseButtonCount =
			static_cast<std::size_t>(MouseButton::Count);

		std::array<ButtonState, KeyCount> Keys{};
		std::array<ButtonState, MouseButtonCount> MouseButtons{};

		std::vector<KeyEvent> KeyEvents{};

		std::int32_t CursorX = 0;
		std::int32_t CursorY = 0;
		std::int32_t CursorDeltaX = 0;
		std::int32_t CursorDeltaY = 0;
		bool HasCursorPosition = false;

		float ScrollX = 0.0f;
		float ScrollY = 0.0f;

		ButtonState GetKeyState(Key key) const
		{
			const auto index = static_cast<std::size_t>(key);
			if (index == 0 || index >= Keys.size())
				return {};
			return Keys[index];
		}

		ButtonState GetMouseButtonState(MouseButton button) const
		{
			const auto index = static_cast<std::size_t>(button);
			if (index >= MouseButtons.size())
				return {};
			return MouseButtons[index];
		}

		bool IsKeyDown(Key key) const
		{
			return GetKeyState(key).Down;
		}

		Modifier GetModifiers() const
		{
			Modifier modifiers = Modifier::None;

			if (IsKeyDown(Key::LeftShift) ||
				IsKeyDown(Key::RightShift))
				modifiers = modifiers | Modifier::Shift;

			if (IsKeyDown(Key::LeftControl) ||
				IsKeyDown(Key::RightControl))
				modifiers = modifiers | Modifier::Control;

			if (IsKeyDown(Key::LeftAlt) ||
				IsKeyDown(Key::RightAlt))
				modifiers = modifiers | Modifier::Alt;

			if (IsKeyDown(Key::LeftSuper) ||
				IsKeyDown(Key::RightSuper))
				modifiers = modifiers | Modifier::Super;

			return modifiers;
		}

		void SetKeyDown(Key key, bool down)
		{
			const auto index = static_cast<std::size_t>(key);
			if (key == Key::Unknown || index >= Keys.size())
				return;

			ButtonState& state = Keys[index];
			if (state.Down == down)
				return;

			state.Down = down;

			// Preserve both transitions if a key is pressed and
			// released during the same poll.
			if (down)
				state.Pressed = true;
			else
				state.Released = true;

			// Capture modifiers after applying this transition.
			KeyEvents.push_back({
				key,
				down ? KeyTransition::Pressed : KeyTransition::Released,
				GetModifiers()
			});
		}

		void ReleaseAllKeys()
		{
			for (std::size_t i = 0; i < Keys.size(); ++i)
			{
				ButtonState& state = Keys[i];
				if (!state.Down)
					continue;

				state.Down = false;
				state.Released = true;

				KeyEvents.push_back({
					static_cast<Key>(i),
					KeyTransition::Released,
					Modifier::None
				});
			}
		}

		bool WasKeyPressed(Key key) const
		{
			return GetKeyState(key).Pressed;
		}

		bool WasKeyReleased(Key key) const
		{
			return GetKeyState(key).Released;
		}

		bool WasKeyPressed(Key key, Modifier exactModifiers) const
		{
			for (const KeyEvent& event : KeyEvents)
			{
				if (event.Code == key &&
					event.Transition == KeyTransition::Pressed &&
					event.Modifiers == exactModifiers)
				{
					return true;
				}
			}
			return false;
		}

		void AddScroll(float x, float y)
		{
			ScrollX += x;
			ScrollY += y;
		}

		void ClearTransientState()
		{
			for (ButtonState& state : Keys)
			{
				state.Pressed = false;
				state.Released = false;
			}
			for (ButtonState& state : MouseButtons)
			{
				state.Pressed = false;
				state.Released = false;
			}

			KeyEvents.clear();
			CursorDeltaX = 0;
			CursorDeltaY = 0;
			ScrollX = 0.0f;
			ScrollY = 0.0f;
		}
	};
}
