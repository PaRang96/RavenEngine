#pragma once

#include "Raven/Core/Api.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

#include "Raven/Platform/Input.hpp"

namespace Raven
{
	struct WindowPosition
	{
		std::int32_t X = 0;
		std::int32_t Y = 0;
	};

	struct WindowDesc
	{
		std::string Title = "RavenEngine";
		std::uint32_t Width = 1280;
		std::uint32_t Height = 720;
		// Top-left corner in virtual-screen pixels; unset lets the system place the window.
		std::optional<WindowPosition> Position;
		// No frame, and kept above other windows, so Width x Height is the whole window (for
		// measurements that must cover one display). Windows only so far; other backends ignore it.
		bool Borderless = false;
	};

	struct FramebufferState
	{
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::uint64_t Revision = 0;

		bool IsDrawable() const
		{
			return Width != 0 && Height != 0;
		}
	};

	class Window
	{
	public:

		virtual ~Window() = default;

		virtual void PollEvents() = 0;
		virtual bool ShouldClose() const = 0;

		// The only window size accessor (docs/RendererDesign.md, R7). Read on the window thread
		// after PollEvents().
		// Dimensions are drawable pixels; minimized/unavailable is 0 x 0.
		// Revision advances on size changes and is never consumed/reset.
		virtual FramebufferState GetFramebufferState() const = 0;

		virtual std::vector<const char*> 
			GetRequiredVulkanInstanceExtensions() const = 0;
		// Read on the window thread after this window's PollEvents().
		virtual const InputState& GetInputState() const = 0;
		// The display's scale for UI: 1 at 96 DPI on Windows. Backends that cannot tell return 1.
		virtual float GetContentScale() const { return 1.0f; }

		virtual VkSurfaceKHR CreateVulkanSurface(VkInstance instance) const = 0;

		RAVEN_API static std::unique_ptr<Window>
			Create(const WindowDesc& desc = WindowDesc());

	private:

	};
}
