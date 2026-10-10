#pragma once

#include "Raven/Platform/Window.hpp"

namespace Raven
{
    class CocoaWindow : public Window
    {
    public:
        explicit CocoaWindow(const WindowDesc &desc);
        ~CocoaWindow() override;

        CocoaWindow(const CocoaWindow &) = delete;
        CocoaWindow &operator=(const CocoaWindow &) = delete;

        void PollEvents() override;
        bool ShouldClose() const override;
        std::uint32_t GetWidth() const override;
        std::uint32_t GetHeight() const override;
        std::vector<const char *> GetRequiredVulkanInstanceExtensions() const override;
        VkSurfaceKHR CreateVulkanSurface(VkInstance instance) const override;
        const InputState& GetInputState() const override;

    private:
        // Cocoa types stay in the Objective-C++ implementation.
        void *m_WindowHandle = nullptr;
        InputState m_Input{};
    };
}
