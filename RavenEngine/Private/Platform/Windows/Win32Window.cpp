#define VK_USE_PLATFORM_WIN32_KHR

#include <vulkan/vulkan.h>
#include "Platform/Windows/Win32Window.hpp"
#include "Platform/Windows/Win32EventPump.hpp"
#include "Platform/Windows/Win32Input.hpp"
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
		Win32EventPump::PollEvents();
		m_Input.Publish();
	}

	bool Win32Window::ShouldClose() const
	{
		return m_ShouldClose || Win32EventPump::IsQuitRequested();
	}
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
		return m_Input.GetState();
	}

	void Win32Window::UpdateMouseCapture(HWND handle)
	{
		if (m_Input.HasMouseButtonDown())
		{
			if (GetCapture() != handle)
				SetCapture(handle);
		}
		else
		{
			ReleaseMouseCapture(handle);
		}
	}

	void Win32Window::ReleaseMouseCapture(HWND handle)
	{
		if (GetCapture() == handle)
			ReleaseCapture();
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

				self->m_Input.SetCursorPosition(x, y);
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
				self->m_Input.SetMouseButtonDown(MouseButton::Left, true);
				self->UpdateMouseCapture(handle);
				return 0;
			case WM_LBUTTONUP:
				self->m_Input.SetMouseButtonDown(MouseButton::Left, false);
				self->UpdateMouseCapture(handle);
				return 0;
			case WM_RBUTTONDOWN:
				self->m_Input.SetMouseButtonDown(MouseButton::Right, true);
				self->UpdateMouseCapture(handle);
				return 0;
			case WM_RBUTTONUP:
				self->m_Input.SetMouseButtonDown(MouseButton::Right, false);
				self->UpdateMouseCapture(handle);
				return 0;
			case WM_MBUTTONDOWN:
				self->m_Input.SetMouseButtonDown(MouseButton::Middle, true);
				self->UpdateMouseCapture(handle);
				return 0;
			case WM_MBUTTONUP:
				self->m_Input.SetMouseButtonDown(MouseButton::Middle, false);
				self->UpdateMouseCapture(handle);
				return 0;
			case WM_XBUTTONDOWN:
			{
				const MouseButton button =
					GET_XBUTTON_WPARAM(wParam) == XBUTTON1
					? MouseButton::X1 : MouseButton::X2;
				self->m_Input.SetMouseButtonDown(button, true);
				self->UpdateMouseCapture(handle);
				return TRUE;
			}
			case WM_XBUTTONUP:
			{
				const MouseButton button =
					GET_XBUTTON_WPARAM(wParam) == XBUTTON1
					? MouseButton::X1 : MouseButton::X2;
				self->m_Input.SetMouseButtonDown(button, false);
				self->UpdateMouseCapture(handle);
				return TRUE;
			}
			case WM_KEYDOWN:
			case WM_KEYUP:
			case WM_SYSKEYDOWN:
			case WM_SYSKEYUP:
			{
				const Key key = TranslateWin32Key(wParam, lParam);
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
				self->m_Input.HandleFocusLost();
				self->ReleaseMouseCapture(handle);
				return 0;
			case WM_CAPTURECHANGED:
				self->m_Input.ReleaseAllMouseButtons();
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
