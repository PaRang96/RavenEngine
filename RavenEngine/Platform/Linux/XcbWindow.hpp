#pragma once

#include <xcb/xcb.h>
#include "../Window.hpp"

namespace Raven
{
	class XcbWindow : public Window
	{
	public:
		explicit XcbWindow(const WindowDesc& desc);
		~XcbWindow() override;

		XcbWindow(const XcbWindow&) = delete;
		XcbWindow& operator=(const XcbWindow&) = delete;

		void PollEvents() override;
		bool ShouldClose() const override;
		std::uint32_t GetWidth() const override;
		std::uint32_t GetHeight() const override;
		std::vector<const char*> GetRequiredVulkanInstanceExtensions() const override;
		VkSurfaceKHR CreateVulkanSurface(VkInstance instance) const override;
		const InputState& GetInputState() const override;

	private:
		xcb_generic_event_t* NextEvent();
		void HandleEvent(const xcb_generic_event_t* event);
		void HandleKeyRelease(const xcb_key_release_event_t* event);

		xcb_connection_t* m_Connection = nullptr;
		xcb_window_t m_Handle = 0;
		xcb_atom_t m_WmProtocols = XCB_ATOM_NONE;
		xcb_atom_t m_WmDeleteWindow = XCB_ATOM_NONE;

		// Event read ahead while filtering synthetic key repeats.
		xcb_generic_event_t* m_PendingEvent = nullptr;

		std::uint32_t m_Width = 0;
		std::uint32_t m_Height = 0;
		bool m_ShouldClose = false;
		InputState m_Input{};
	};
}
