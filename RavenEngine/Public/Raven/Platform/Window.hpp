#pragma once

#include "Raven/Core/Api.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

#include "Raven/Platform/Input.hpp"

namespace Raven
{
	struct WindowDesc
	{
		std::string Title = "RavenEngine";
		std::uint32_t Width = 1280;
		std::uint32_t Height = 720;
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

		virtual std::uint32_t GetWidth() const = 0;
		virtual std::uint32_t GetHeight() const = 0;

		// Read on the window thread after PollEvents().
		// Dimensions are drawable pixels; minimized/unavailable is 0 x 0.
		// Revision advances on size changes and is never consumed/reset.
		virtual FramebufferState GetFramebufferState() const = 0;

		virtual std::vector<const char*> 
			GetRequiredVulkanInstanceExtensions() const = 0;
		virtual const InputState& GetInputState() const = 0;

		virtual VkSurfaceKHR CreateVulkanSurface(VkInstance instance) const = 0;

		RAVEN_API static std::unique_ptr<Window>
			Create(const WindowDesc& desc = WindowDesc());

	private:

	};
}
