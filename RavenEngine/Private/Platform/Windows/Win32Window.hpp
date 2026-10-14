#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <Windows.h>
#include "Raven/Platform/Window.hpp"
#include "Platform/InputBuffer.hpp"

namespace Raven
{
	class Win32Window : public Window
	{
	public:
		explicit Win32Window(const WindowDesc& desc);
		~Win32Window() override;

		Win32Window(const Win32Window&) = delete;
		Win32Window& operator=(const Win32Window&) = delete;

		void PollEvents() override;
		bool ShouldClose() const override;
		std::uint32_t GetWidth() const override;
		std::uint32_t GetHeight() const override;
		FramebufferState GetFramebufferState() const override;
		std::vector<const char*> GetRequiredVulkanInstanceExtensions() const override;
		VkSurfaceKHR CreateVulkanSurface(VkInstance instance) const override;
		const InputState& GetInputState() const override;

	private:
		static LRESULT CALLBACK WindowProc(
			HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
		
		void UpdateFramebufferSize(HWND handle, bool minimized);
		void UpdateMouseCapture(HWND handle);
		void ReleaseMouseCapture(HWND handle);

		HWND m_Handle = nullptr;
		std::uint32_t m_Width = 0;
		std::uint32_t m_Height = 0;
		std::uint64_t m_FramebufferRevision = 0;
		bool m_ShouldClose = false;
		InputBuffer m_Input{};
	};
}
