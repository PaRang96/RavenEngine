#define VK_USE_PLATFORM_WIN32_KHR

#include <vulkan/vulkan.h>
#include "Platform/Windows/Win32Window.hpp"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <windowsx.h>

namespace Raven
{
	namespace
	{
		constexpr wchar_t ClassName[] = L"RavenEngineWindow";

		void EnableDpiAwareness()
		{
			if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
			{
				const DWORD error = GetLastError();
				if (error != ERROR_ACCESS_DENIED)
					throw std::runtime_error(
						"Could not enable per-monitor DPI awareness: " +
						std::to_string(error));
			}

			// A previous call may have set the process mode already.
			if (!AreDpiAwarenessContextsEqual(
				GetThreadDpiAwarenessContext(),
				DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
				throw std::runtime_error("Window thread requires per-monitor v2 DPI awareness");
		}

		std::wstring ToWide(const std::string& text)
		{
			const int count = MultiByteToWideChar(
				CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1, nullptr, 0);

			if (count == 0)
				throw std::runtime_error("Invalid UTF-8 window title");

			std::wstring result(static_cast<std::size_t>(count), L'\0');
			if (MultiByteToWideChar(
				CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1,
				result.data(), count) == 0)
				throw std::runtime_error("Could not convert window title");

			return result;
		}

		Key TranslatePhysicalKey(LPARAM lParam)
		{
			const UINT flags = HIWORD(lParam);
			if ((flags & KF_EXTENDED) != 0)
				return Key::Unknown;
			
			switch (LOBYTE(flags))
			{
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
			
			case 0x1E: return Key::A;
			case 0x1F: return Key::S;
			case 0x20: return Key::D;
			case 0x21: return Key::F;
			case 0x22: return Key::G;
			case 0x23: return Key::H;
			case 0x24: return Key::J;
			case 0x25: return Key::K;
			case 0x26: return Key::L;
			
			case 0x2C: return Key::Z;
			case 0x2D: return Key::X;
			case 0x2E: return Key::C;
			case 0x2F: return Key::V;
			case 0x30: return Key::B;
			case 0x31: return Key::N;
			case 0x32: return Key::M;
			
			case 0x02: return Key::Digit1;
			case 0x03: return Key::Digit2;
			case 0x04: return Key::Digit3;
			case 0x05: return Key::Digit4;
			case 0x06: return Key::Digit5;
			case 0x07: return Key::Digit6;
			case 0x08: return Key::Digit7;
			case 0x09: return Key::Digit8;
			case 0x0A: return Key::Digit9;
			case 0x0B: return Key::Digit0;

			case 0x0C: return Key::Minus;
			case 0x0D: return Key::Equal;
			case 0x1A: return Key::LeftBracket;
			case 0x1B: return Key::RightBracket;
			case 0x27: return Key::Semicolon;
			case 0x28: return Key::Apostrophe;
			case 0x29: return Key::Grave;
			case 0x2B: return Key::Backslash;
			case 0x33: return Key::Comma;
			case 0x34: return Key::Period;
			case 0x35: return Key::Slash;

			default: return Key::Unknown;
			}
		}

		Key TranslateKey(WPARAM virtualKey, LPARAM lParam)
		{
			const Key physicalKey = TranslatePhysicalKey(lParam);
			if (physicalKey != Key::Unknown)
				return physicalKey;

			if (virtualKey == VK_SHIFT)
			{
				const UINT scanCode = LOBYTE(HIWORD(lParam));
				virtualKey = MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK_EX);
			}
			else if (virtualKey == VK_CONTROL || virtualKey == VK_MENU)
			{
				// Scan-code lookup can produce language keys on Korean layouts.
				const bool extended = (HIWORD(lParam) & KF_EXTENDED) != 0;
				if (virtualKey == VK_CONTROL)
					virtualKey = extended ? VK_RCONTROL : VK_LCONTROL;
				else
					virtualKey = extended ? VK_RMENU : VK_LMENU;
			}

			if (virtualKey >= VK_F1 && virtualKey <= VK_F12)
			{
				return static_cast<Key>(
					static_cast<std::uint16_t>(Key::F1) +
					static_cast<std::uint16_t>(virtualKey - VK_F1));
			}

			if (virtualKey >= VK_F13 && virtualKey <= VK_F24)
			{
				return static_cast<Key>(
					static_cast<std::uint16_t>(Key::F13) +
					static_cast<std::uint16_t>(virtualKey - VK_F13));
			}

			switch (virtualKey)
			{
			case VK_LSHIFT:   return Key::LeftShift;
			case VK_RSHIFT:   return Key::RightShift;
			case VK_LCONTROL: return Key::LeftControl;
			case VK_RCONTROL: return Key::RightControl;
			case VK_LMENU:    return Key::LeftAlt;
			case VK_RMENU:    return Key::RightAlt;
			case VK_LWIN:     return Key::LeftSuper;
			case VK_RWIN:     return Key::RightSuper;
			case VK_ESCAPE: return Key::Escape;
			case VK_RETURN: 
				return (HIWORD(lParam) & KF_EXTENDED) != 0
					? Key::NumpadEnter : Key::Enter;
			case VK_BACK:   return Key::Backspace;
			case VK_TAB:    return Key::Tab;
			case VK_SPACE:  return Key::Space;
			case VK_INSERT: return Key::Insert;
			case VK_HOME:   return Key::Home;
			case VK_PRIOR:  return Key::PageUp;
			case VK_DELETE: return Key::Delete;
			case VK_END:    return Key::End;
			case VK_NEXT:   return Key::PageDown;
			case VK_LEFT:   return Key::Left;
			case VK_RIGHT:  return Key::Right;
			case VK_UP:     return Key::Up;
			case VK_DOWN:   return Key::Down;
			default:        return Key::Unknown;
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

		void UpdateMouseButton(
			HWND handle,
			InputState& input,
			MouseButton button,
			bool down)
		{
			const auto index = static_cast<std::size_t>(button);
			if (index >= input.MouseButtons.size())
				return;

			UpdateButton(input.MouseButtons[index], down);

			if (down)
			{
				SetCapture(handle);
				return;
			}

			for (const ButtonState& state : input.MouseButtons)
			{
				if (state.Down)
					return;
			}

			if (GetCapture() == handle)
				ReleaseCapture();
		}
	}

	Win32Window::Win32Window(const WindowDesc& desc)
		: m_Width(desc.Width), m_Height(desc.Height)
	{
		EnableDpiAwareness();

		const HINSTANCE instance = GetModuleHandleW(nullptr);

		WNDCLASSW windowClass{};
		windowClass.lpfnWndProc = &Win32Window::WindowProc;
		windowClass.hInstance = instance;
		windowClass.lpszClassName = ClassName;
		windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		windowClass.hbrBackground = GetSysColorBrush(COLOR_WINDOW);

		if (!RegisterClassW(&windowClass) &&
			GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
			throw std::runtime_error("Could not register window class");

		RECT bounds{
			0, 0,
			static_cast<LONG>(desc.Width),
			static_cast<LONG>(desc.Height)
		};
		if (!AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, FALSE))
			throw std::runtime_error("Could not calculate window size");

		const std::wstring title = ToWide(desc.Title);
		m_Handle = CreateWindowExW(
			0, ClassName, title.c_str(), WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, CW_USEDEFAULT,
			bounds.right - bounds.left,
			bounds.bottom - bounds.top,
			nullptr, nullptr, instance, this);

		if (!m_Handle)
			throw std::runtime_error("Could not create window");

		ShowWindow(m_Handle, SW_SHOW);
		UpdateFramebufferSize(m_Handle, IsIconic(m_Handle) != FALSE);
	}

	Win32Window::~Win32Window()
	{
		if (m_Handle)
			DestroyWindow(m_Handle);
	}

	void Win32Window::PollEvents()
	{
		m_Input.ClearTransientState();

		MSG message{};
		while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
		{
			if (message.message == WM_QUIT)
			{
				m_ShouldClose = true;
				continue;
			}

			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
	}

	bool Win32Window::ShouldClose() const { return m_ShouldClose; }
	std::uint32_t Win32Window::GetWidth() const { return m_Width; }
	std::uint32_t Win32Window::GetHeight() const { return m_Height; }

	FramebufferState Win32Window::GetFramebufferState() const
	{
		return { m_Width, m_Height, m_FramebufferRevision };
	}

	void Win32Window::UpdateFramebufferSize(HWND handle, bool minimized)
	{
		std::uint32_t width = 0;
		std::uint32_t height = 0;

		if (!minimized)
		{
			RECT client{};
			if (GetClientRect(handle, &client))
			{
				width = static_cast<std::uint32_t>(client.right - client.left);
				height = static_cast<std::uint32_t>(client.bottom - client.top);
			}
		}

		if (width == m_Width && height == m_Height)
			return;

		m_Width = width;
		m_Height = height;
		++m_FramebufferRevision;
	}

	std::vector<const char*> Win32Window::GetRequiredVulkanInstanceExtensions() const
	{
		return {
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME
		};
	}

	VkSurfaceKHR Win32Window::CreateVulkanSurface(VkInstance instance) const
	{
		VkWin32SurfaceCreateInfoKHR createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
		createInfo.hinstance = GetModuleHandleW(nullptr);
		createInfo.hwnd = m_Handle;

		VkSurfaceKHR surface = VK_NULL_HANDLE;
		const VkResult result =
			vkCreateWin32SurfaceKHR(instance, &createInfo, nullptr, &surface);
		if (result != VK_SUCCESS)
			throw std::runtime_error(
				"vkCreateWin32SurfaceKHR failed: " + std::to_string(result));

		return surface;
	}

	const InputState& Win32Window::GetInputState() const
	{
		return m_Input;
	}

	LRESULT CALLBACK Win32Window::WindowProc(
		HWND handle, UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (message == WM_NCCREATE)
		{
			auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
			SetWindowLongPtrW(
				handle, GWLP_USERDATA,
				reinterpret_cast<LONG_PTR>(create->lpCreateParams));
		}

		auto* self = reinterpret_cast<Win32Window*>(
			GetWindowLongPtrW(handle, GWLP_USERDATA));

		if (self)
		{
			switch (message)
			{
			case WM_CLOSE:
			case WM_DESTROY:
				self->m_ShouldClose = true;
				return 0;
			case WM_SIZE:
				self->UpdateFramebufferSize(handle, wParam == SIZE_MINIMIZED);
				return 0;
			case WM_DPICHANGED:
			{
				const auto* bounds = reinterpret_cast<const RECT*>(lParam);
				SetWindowPos(
					handle, nullptr, bounds->left, bounds->top,
					bounds->right - bounds->left,
					bounds->bottom - bounds->top,
					SWP_NOZORDER | SWP_NOACTIVATE);

				self->UpdateFramebufferSize(handle, IsIconic(handle) != FALSE);
				return 0;
			}
			case WM_MOUSEMOVE:
			{
				const std::int32_t x = GET_X_LPARAM(lParam);
				const std::int32_t y = GET_Y_LPARAM(lParam);

				if (self->m_Input.HasCursorPosition)
				{
					self->m_Input.CursorDeltaX += x - self->m_Input.CursorX;
					self->m_Input.CursorDeltaY += y - self->m_Input.CursorY;
				}
				else
				{
					self->m_Input.HasCursorPosition = true;
				}

				self->m_Input.CursorX = x;
				self->m_Input.CursorY = y;
				return 0;
			}
			case WM_MOUSEWHEEL:
			case WM_MOUSEHWHEEL:
			{
				const float steps =
					static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / 
					static_cast<float>(WHEEL_DELTA);
				
				if (message == WM_MOUSEWHEEL)
					self->m_Input.AddScroll(0.0f, steps);
				else
					self->m_Input.AddScroll(steps, 0.0f);

				return 0;
			}
			case WM_LBUTTONDOWN:
				UpdateMouseButton(
					handle, self->m_Input, MouseButton::Left, true);
				return 0;
			case WM_LBUTTONUP:
				UpdateMouseButton(
					handle, self->m_Input, MouseButton::Left, false);
				return 0;
			case WM_RBUTTONDOWN:
				UpdateMouseButton(
					handle, self->m_Input, MouseButton::Right, true);
				return 0;
			case WM_RBUTTONUP:
				UpdateMouseButton(
					handle, self->m_Input, MouseButton::Right, false);
				return 0;
			case WM_MBUTTONDOWN:
				UpdateMouseButton(
					handle, self->m_Input, MouseButton::Middle, true);
				return 0;
			case WM_MBUTTONUP:
				UpdateMouseButton(
					handle, self->m_Input, MouseButton::Middle, false);
				return 0;
			case WM_XBUTTONDOWN:
			{
				const MouseButton button =
					GET_XBUTTON_WPARAM(wParam) == XBUTTON1
					? MouseButton::X1 : MouseButton::X2;
				UpdateMouseButton(handle, self->m_Input, button, true);
				return TRUE;
			}
			case WM_XBUTTONUP:
			{
				const MouseButton button =
					GET_XBUTTON_WPARAM(wParam) == XBUTTON1
					? MouseButton::X1 : MouseButton::X2;
				UpdateMouseButton(handle, self->m_Input, button, false);
				return TRUE;
			}
			case WM_KEYDOWN:
			case WM_KEYUP:
			case WM_SYSKEYDOWN:
			case WM_SYSKEYUP:
			{
				const Key key = TranslateKey(wParam, lParam);
				if (key == Key::Unknown)
					break;

				const bool down =
					message == WM_KEYDOWN || message == WM_SYSKEYDOWN;

				self->m_Input.SetKeyDown(key, down);

				if (message == WM_KEYDOWN || message == WM_KEYUP)
					return 0;

				break;
			}
			case WM_KILLFOCUS:
				self->m_Input.ReleaseAllKeys();

				for (ButtonState& state : self->m_Input.MouseButtons)
					UpdateButton(state, false);
				if (GetCapture() == handle)
					ReleaseCapture();
				self->m_Input.HasCursorPosition = false;
				return 0;
			case WM_CAPTURECHANGED:
				for (ButtonState& state : self->m_Input.MouseButtons)
					UpdateButton(state, false);
				return 0;
			}
		}

		return DefWindowProcW(handle, message, wParam, lParam);
	}

	std::unique_ptr<Window> Window::Create(const WindowDesc& desc)
	{
		return std::make_unique<Win32Window>(desc);
	}
}
