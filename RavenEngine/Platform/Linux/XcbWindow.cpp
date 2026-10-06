#define VK_USE_PLATFORM_XCB_KHR

#include <xcb/xcb.h>
// xkb.h uses the C++ keyword "explicit" as a struct field name.
#define explicit explicit_
#include <xcb/xkb.h>
#undef explicit
#include <vulkan/vulkan.h>
#include "XcbWindow.hpp"

#include <linux/input-event-codes.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

namespace Raven
{
	namespace
	{
		// X11 keycodes are evdev codes offset by 8.
		constexpr std::uint32_t EvdevKeycodeOffset = 8;

		struct FreeDeleter
		{
			void operator()(void* pointer) const { std::free(pointer); }
		};

		template <typename T>
		using XcbReply = std::unique_ptr<T, FreeDeleter>;

		xcb_atom_t InternAtom(xcb_connection_t* connection, const char* name)
		{
			const xcb_intern_atom_cookie_t cookie = xcb_intern_atom(
				connection, 0, static_cast<std::uint16_t>(std::strlen(name)), name);
			XcbReply<xcb_intern_atom_reply_t> reply(
				xcb_intern_atom_reply(connection, cookie, nullptr));
			return reply ? reply->atom : XCB_ATOM_NONE;
		}

		// Physical key mapping, independent of the active keyboard layout.
		Key TranslateKey(xcb_keycode_t keycode)
		{
			if (keycode < EvdevKeycodeOffset)
				return Key::Unknown;

			switch (keycode - EvdevKeycodeOffset)
			{
			case KEY_A: return Key::A;
			case KEY_B: return Key::B;
			case KEY_C: return Key::C;
			case KEY_D: return Key::D;
			case KEY_E: return Key::E;
			case KEY_F: return Key::F;
			case KEY_G: return Key::G;
			case KEY_H: return Key::H;
			case KEY_I: return Key::I;
			case KEY_J: return Key::J;
			case KEY_K: return Key::K;
			case KEY_L: return Key::L;
			case KEY_M: return Key::M;
			case KEY_N: return Key::N;
			case KEY_O: return Key::O;
			case KEY_P: return Key::P;
			case KEY_Q: return Key::Q;
			case KEY_R: return Key::R;
			case KEY_S: return Key::S;
			case KEY_T: return Key::T;
			case KEY_U: return Key::U;
			case KEY_V: return Key::V;
			case KEY_W: return Key::W;
			case KEY_X: return Key::X;
			case KEY_Y: return Key::Y;
			case KEY_Z: return Key::Z;

			case KEY_1: return Key::Digit1;
			case KEY_2: return Key::Digit2;
			case KEY_3: return Key::Digit3;
			case KEY_4: return Key::Digit4;
			case KEY_5: return Key::Digit5;
			case KEY_6: return Key::Digit6;
			case KEY_7: return Key::Digit7;
			case KEY_8: return Key::Digit8;
			case KEY_9: return Key::Digit9;
			case KEY_0: return Key::Digit0;

			case KEY_ENTER:      return Key::Enter;
			case KEY_ESC:        return Key::Escape;
			case KEY_BACKSPACE:  return Key::Backspace;
			case KEY_TAB:        return Key::Tab;
			case KEY_SPACE:      return Key::Space;
			case KEY_MINUS:      return Key::Minus;
			case KEY_EQUAL:      return Key::Equal;
			case KEY_LEFTBRACE:  return Key::LeftBracket;
			case KEY_RIGHTBRACE: return Key::RightBracket;
			case KEY_BACKSLASH:  return Key::Backslash;
			case KEY_SEMICOLON:  return Key::Semicolon;
			case KEY_APOSTROPHE: return Key::Apostrophe;
			case KEY_GRAVE:      return Key::Grave;
			case KEY_COMMA:      return Key::Comma;
			case KEY_DOT:        return Key::Period;
			case KEY_SLASH:      return Key::Slash;
			case KEY_CAPSLOCK:   return Key::CapsLock;

			case KEY_F1:  return Key::F1;
			case KEY_F2:  return Key::F2;
			case KEY_F3:  return Key::F3;
			case KEY_F4:  return Key::F4;
			case KEY_F5:  return Key::F5;
			case KEY_F6:  return Key::F6;
			case KEY_F7:  return Key::F7;
			case KEY_F8:  return Key::F8;
			case KEY_F9:  return Key::F9;
			case KEY_F10: return Key::F10;
			case KEY_F11: return Key::F11;
			case KEY_F12: return Key::F12;
			case KEY_F13: return Key::F13;
			case KEY_F14: return Key::F14;
			case KEY_F15: return Key::F15;
			case KEY_F16: return Key::F16;
			case KEY_F17: return Key::F17;
			case KEY_F18: return Key::F18;
			case KEY_F19: return Key::F19;
			case KEY_F20: return Key::F20;
			case KEY_F21: return Key::F21;
			case KEY_F22: return Key::F22;
			case KEY_F23: return Key::F23;
			case KEY_F24: return Key::F24;

			case KEY_SYSRQ:      return Key::PrintScreen;
			case KEY_SCROLLLOCK: return Key::ScrollLock;
			case KEY_PAUSE:      return Key::Pause;
			case KEY_INSERT:     return Key::Insert;
			case KEY_HOME:       return Key::Home;
			case KEY_PAGEUP:     return Key::PageUp;
			case KEY_DELETE:     return Key::Delete;
			case KEY_END:        return Key::End;
			case KEY_PAGEDOWN:   return Key::PageDown;
			case KEY_RIGHT:      return Key::Right;
			case KEY_LEFT:       return Key::Left;
			case KEY_DOWN:       return Key::Down;
			case KEY_UP:         return Key::Up;

			case KEY_NUMLOCK:    return Key::NumLock;
			case KEY_KPSLASH:    return Key::NumpadDivide;
			case KEY_KPASTERISK: return Key::NumpadMultiply;
			case KEY_KPMINUS:    return Key::NumpadSubtract;
			case KEY_KPPLUS:     return Key::NumpadAdd;
			case KEY_KPENTER:    return Key::NumpadEnter;
			case KEY_KP1:        return Key::Numpad1;
			case KEY_KP2:        return Key::Numpad2;
			case KEY_KP3:        return Key::Numpad3;
			case KEY_KP4:        return Key::Numpad4;
			case KEY_KP5:        return Key::Numpad5;
			case KEY_KP6:        return Key::Numpad6;
			case KEY_KP7:        return Key::Numpad7;
			case KEY_KP8:        return Key::Numpad8;
			case KEY_KP9:        return Key::Numpad9;
			case KEY_KP0:        return Key::Numpad0;
			case KEY_KPDOT:      return Key::NumpadDecimal;
			case KEY_KPEQUAL:    return Key::NumpadEqual;
			case KEY_KPCOMMA:    return Key::KeypadComma;

			case KEY_102ND:   return Key::NonUSBackslash;
			case KEY_COMPOSE: return Key::Application;
			case KEY_POWER:   return Key::Power;

			case KEY_RO:               return Key::International1;
			case KEY_KATAKANAHIRAGANA: return Key::International2;
			case KEY_YEN:              return Key::International3;
			case KEY_HENKAN:           return Key::International4;
			case KEY_MUHENKAN:         return Key::International5;
			case KEY_KPJPCOMMA:        return Key::International6;
			case KEY_HANGEUL:          return Key::Lang1;
			case KEY_HANJA:            return Key::Lang2;
			case KEY_KATAKANA:         return Key::Lang3;
			case KEY_HIRAGANA:         return Key::Lang4;
			case KEY_ZENKAKUHANKAKU:   return Key::Lang5;

			case KEY_LEFTCTRL:   return Key::LeftControl;
			case KEY_LEFTSHIFT:  return Key::LeftShift;
			case KEY_LEFTALT:    return Key::LeftAlt;
			case KEY_LEFTMETA:   return Key::LeftSuper;
			case KEY_RIGHTCTRL:  return Key::RightControl;
			case KEY_RIGHTSHIFT: return Key::RightShift;
			case KEY_RIGHTALT:   return Key::RightAlt;
			case KEY_RIGHTMETA:  return Key::RightSuper;

			default: return Key::Unknown;
			}
		}

		void UpdateButton(ButtonState& state, bool down)
		{
			if (down)
			{
				if (!state.Down)
					state.Pressed = true;
			}
			else if (state.Down)
			{
				state.Released = true;
			}

			state.Down = down;
		}

		bool TranslateMouseButton(xcb_button_t button, MouseButton& out)
		{
			switch (button)
			{
			case XCB_BUTTON_INDEX_1: out = MouseButton::Left;   return true;
			case XCB_BUTTON_INDEX_2: out = MouseButton::Middle; return true;
			case XCB_BUTTON_INDEX_3: out = MouseButton::Right;  return true;
			case 8:                  out = MouseButton::X1;     return true;
			case 9:                  out = MouseButton::X2;     return true;
			default:                 return false;
			}
		}

		void EnableDetectableAutoRepeat(xcb_connection_t* connection)
		{
			XcbReply<xcb_xkb_use_extension_reply_t> extension(
				xcb_xkb_use_extension_reply(
					connection,
					xcb_xkb_use_extension(
						connection,
						XCB_XKB_MAJOR_VERSION,
						XCB_XKB_MINOR_VERSION),
					nullptr));
			if (!extension || !extension->supported)
				return;

			// Without this, held keys produce release/press pairs.
			// If unsupported, HandleKeyRelease filters the pairs instead.
			constexpr std::uint32_t flag =
				XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT;
			XcbReply<xcb_xkb_per_client_flags_reply_t> flags(
				xcb_xkb_per_client_flags_reply(
					connection,
					xcb_xkb_per_client_flags(
						connection, XCB_XKB_ID_USE_CORE_KBD,
						flag, flag, 0, 0, 0),
					nullptr));
		}
	}

	XcbWindow::XcbWindow(const WindowDesc& desc)
		: m_Width(desc.Width), m_Height(desc.Height)
	{
		int screenIndex = 0;
		m_Connection = xcb_connect(nullptr, &screenIndex);
		if (xcb_connection_has_error(m_Connection))
		{
			xcb_disconnect(m_Connection);
			throw std::runtime_error("Could not connect to the X server");
		}

		xcb_screen_iterator_t screens =
			xcb_setup_roots_iterator(xcb_get_setup(m_Connection));
		for (int i = 0; i < screenIndex && screens.rem > 0; ++i)
			xcb_screen_next(&screens);

		const xcb_screen_t* screen = screens.rem > 0 ? screens.data : nullptr;
		if (!screen)
		{
			xcb_disconnect(m_Connection);
			throw std::runtime_error("Could not find an X screen");
		}

		m_Handle = xcb_generate_id(m_Connection);

		const std::uint32_t valueMask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
		const std::uint32_t values[] = {
			screen->black_pixel,
			XCB_EVENT_MASK_KEY_PRESS |
			XCB_EVENT_MASK_KEY_RELEASE |
			XCB_EVENT_MASK_BUTTON_PRESS |
			XCB_EVENT_MASK_BUTTON_RELEASE |
			XCB_EVENT_MASK_POINTER_MOTION |
			XCB_EVENT_MASK_STRUCTURE_NOTIFY |
			XCB_EVENT_MASK_FOCUS_CHANGE
		};

		xcb_create_window(
			m_Connection, XCB_COPY_FROM_PARENT, m_Handle, screen->root,
			0, 0,
			static_cast<std::uint16_t>(desc.Width),
			static_cast<std::uint16_t>(desc.Height),
			0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
			valueMask, values);

		xcb_change_property(
			m_Connection, XCB_PROP_MODE_REPLACE, m_Handle,
			XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
			static_cast<std::uint32_t>(desc.Title.size()), desc.Title.data());

		const xcb_atom_t netWmName = InternAtom(m_Connection, "_NET_WM_NAME");
		const xcb_atom_t utf8String = InternAtom(m_Connection, "UTF8_STRING");
		if (netWmName != XCB_ATOM_NONE && utf8String != XCB_ATOM_NONE)
		{
			xcb_change_property(
				m_Connection, XCB_PROP_MODE_REPLACE, m_Handle,
				netWmName, utf8String, 8,
				static_cast<std::uint32_t>(desc.Title.size()), desc.Title.data());
		}

		// Instance and class names, each NUL-terminated.
		constexpr char WindowClass[] = "RavenEngine\0RavenEngine";
		xcb_change_property(
			m_Connection, XCB_PROP_MODE_REPLACE, m_Handle,
			XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8,
			sizeof(WindowClass), WindowClass);

		m_WmProtocols = InternAtom(m_Connection, "WM_PROTOCOLS");
		m_WmDeleteWindow = InternAtom(m_Connection, "WM_DELETE_WINDOW");
		if (m_WmProtocols != XCB_ATOM_NONE && m_WmDeleteWindow != XCB_ATOM_NONE)
		{
			xcb_change_property(
				m_Connection, XCB_PROP_MODE_REPLACE, m_Handle,
				m_WmProtocols, XCB_ATOM_ATOM, 32, 1, &m_WmDeleteWindow);
		}

		EnableDetectableAutoRepeat(m_Connection);

		xcb_map_window(m_Connection, m_Handle);
		xcb_flush(m_Connection);
	}

	XcbWindow::~XcbWindow()
	{
		std::free(m_PendingEvent);

		if (m_Connection)
		{
			if (m_Handle)
				xcb_destroy_window(m_Connection, m_Handle);
			xcb_disconnect(m_Connection);
		}
	}

	void XcbWindow::PollEvents()
	{
		m_Input.ClearTransientState();

		while (xcb_generic_event_t* event = NextEvent())
		{
			HandleEvent(event);
			std::free(event);
		}

		if (xcb_connection_has_error(m_Connection))
			m_ShouldClose = true;
	}

	bool XcbWindow::ShouldClose() const { return m_ShouldClose; }
	std::uint32_t XcbWindow::GetWidth() const { return m_Width; }
	std::uint32_t XcbWindow::GetHeight() const { return m_Height; }

	std::vector<const char*> XcbWindow::GetRequiredVulkanInstanceExtensions() const
	{
		return {
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_XCB_SURFACE_EXTENSION_NAME
		};
	}

	VkSurfaceKHR XcbWindow::CreateVulkanSurface(VkInstance instance) const
	{
		VkXcbSurfaceCreateInfoKHR createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
		createInfo.connection = m_Connection;
		createInfo.window = m_Handle;

		VkSurfaceKHR surface = VK_NULL_HANDLE;
		const VkResult result =
			vkCreateXcbSurfaceKHR(instance, &createInfo, nullptr, &surface);
		if (result != VK_SUCCESS)
			throw std::runtime_error(
				"vkCreateXcbSurfaceKHR failed: " + std::to_string(result));

		return surface;
	}

	const InputState& XcbWindow::GetInputState() const
	{
		return m_Input;
	}

	xcb_generic_event_t* XcbWindow::NextEvent()
	{
		if (m_PendingEvent)
		{
			xcb_generic_event_t* event = m_PendingEvent;
			m_PendingEvent = nullptr;
			return event;
		}

		return xcb_poll_for_event(m_Connection);
	}

	void XcbWindow::HandleKeyRelease(const xcb_key_release_event_t* release)
	{
		// Fallback auto-repeat filter: a repeat is a release immediately
		// followed by a press of the same key with the same timestamp.
		xcb_generic_event_t* next = xcb_poll_for_queued_event(m_Connection);
		if (next)
		{
			if ((next->response_type & ~0x80) == XCB_KEY_PRESS)
			{
				const auto* press =
					reinterpret_cast<const xcb_key_press_event_t*>(next);
				if (press->detail == release->detail &&
					press->time == release->time)
				{
					std::free(next);
					return;
				}
			}

			m_PendingEvent = next;
		}

		m_Input.SetKeyDown(TranslateKey(release->detail), false);
	}

	void XcbWindow::HandleEvent(const xcb_generic_event_t* event)
	{
		switch (event->response_type & ~0x80)
		{
		case XCB_CLIENT_MESSAGE:
		{
			const auto* message =
				reinterpret_cast<const xcb_client_message_event_t*>(event);
			if (message->type == m_WmProtocols &&
				message->data.data32[0] == m_WmDeleteWindow)
				m_ShouldClose = true;
			break;
		}
		case XCB_DESTROY_NOTIFY:
			m_ShouldClose = true;
			break;
		case XCB_CONFIGURE_NOTIFY:
		{
			const auto* configure =
				reinterpret_cast<const xcb_configure_notify_event_t*>(event);
			m_Width = configure->width;
			m_Height = configure->height;
			break;
		}
		case XCB_MOTION_NOTIFY:
		{
			const auto* motion =
				reinterpret_cast<const xcb_motion_notify_event_t*>(event);
			const std::int32_t x = motion->event_x;
			const std::int32_t y = motion->event_y;

			if (m_Input.HasCursorPosition)
			{
				m_Input.CursorDeltaX += x - m_Input.CursorX;
				m_Input.CursorDeltaY += y - m_Input.CursorY;
			}
			else
			{
				m_Input.HasCursorPosition = true;
			}

			m_Input.CursorX = x;
			m_Input.CursorY = y;
			break;
		}
		case XCB_BUTTON_PRESS:
		case XCB_BUTTON_RELEASE:
		{
			// X11 grabs the pointer implicitly while a button is held,
			// so no explicit capture is needed.
			const auto* buttonEvent =
				reinterpret_cast<const xcb_button_press_event_t*>(event);
			const bool down =
				(event->response_type & ~0x80) == XCB_BUTTON_PRESS;

			// Buttons 4-7 are wheel steps and only send press events.
			switch (buttonEvent->detail)
			{
			case XCB_BUTTON_INDEX_4: if (down) m_Input.AddScroll(0.0f, 1.0f);  return;
			case XCB_BUTTON_INDEX_5: if (down) m_Input.AddScroll(0.0f, -1.0f); return;
			case 6:                  if (down) m_Input.AddScroll(-1.0f, 0.0f); return;
			case 7:                  if (down) m_Input.AddScroll(1.0f, 0.0f);  return;
			default: break;
			}

			MouseButton button{};
			if (TranslateMouseButton(buttonEvent->detail, button))
				UpdateButton(
					m_Input.MouseButtons[static_cast<std::size_t>(button)], down);
			break;
		}
		case XCB_KEY_PRESS:
		{
			const auto* press =
				reinterpret_cast<const xcb_key_press_event_t*>(event);
			m_Input.SetKeyDown(TranslateKey(press->detail), true);
			break;
		}
		case XCB_KEY_RELEASE:
			HandleKeyRelease(
				reinterpret_cast<const xcb_key_release_event_t*>(event));
			break;
		case XCB_FOCUS_OUT:
			m_Input.ReleaseAllKeys();
			for (ButtonState& state : m_Input.MouseButtons)
				UpdateButton(state, false);
			m_Input.HasCursorPosition = false;
			break;
		default:
			break;
		}
	}

	std::unique_ptr<Window> Window::Create(const WindowDesc& desc)
	{
		return std::make_unique<XcbWindow>(desc);
	}
}
