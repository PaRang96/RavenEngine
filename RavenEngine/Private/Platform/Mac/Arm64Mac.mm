#define VK_USE_PLATFORM_METAL_EXT
<<<<<<<< HEAD:RavenEngine/Private/Platform/Mac/CocoaWindow.mm
#include "CocoaWindow.hpp"
========
#include "Platform/Mac/Arm64Mac.hpp"
>>>>>>>> 3579b68a4d362ea2ae212dfa9f33eba707387aab:RavenEngine/Private/Platform/Mac/Arm64Mac.mm

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

#include <stdexcept>
#include <string>
#include <cstdint>

// Keep native state here so the public header remains usable from C++.
@interface RavenNativeWindow : NSWindow <NSWindowDelegate>
@property(nonatomic) BOOL shouldClose;
@property(nonatomic) BOOL escapeDown;
- (void)updateDrawableSize;
@end

@implementation RavenNativeWindow
- (BOOL)windowShouldClose:(NSWindow *)sender
{
    self.shouldClose = YES;
    self.escapeDown = NO;
    // Keep the native window alive until the Vulkan surface is destroyed.
    return NO;
}

- (void)windowWillClose:(NSNotification *)notification
{
    self.shouldClose = YES;
    self.escapeDown = NO;
}

- (void)windowDidResignKey:(NSNotification *)notification
{
    self.escapeDown = NO;
}

- (void)sendEvent:(NSEvent *)event
{
    constexpr unsigned short EscapeKeyCode = 53;
    if ((event.type == NSEventTypeKeyDown || event.type == NSEventTypeKeyUp) &&
        event.keyCode == EscapeKeyCode)
    {
        self.escapeDown = event.type == NSEventTypeKeyDown;
        return;
    }
    [super sendEvent:event];
}

- (void)updateDrawableSize
{
    NSView *view = self.contentView;
    CAMetalLayer *layer = (CAMetalLayer *)view.layer;
    layer.contentsScale = self.backingScaleFactor;
    layer.drawableSize = [view convertRectToBacking:view.bounds].size;
}

- (void)windowDidResize:(NSNotification *)notification
{
    [self updateDrawableSize];
}

- (void)windowDidChangeBackingProperties:(NSNotification *)notification
{
    [self updateDrawableSize];
}
@end

namespace Raven
{
    CocoaWindow::CocoaWindow(const WindowDesc &desc)
    {
        @autoreleasepool
        {
            if (![NSThread isMainThread])
                throw std::runtime_error("Cocoa windows must be created on the main thread");

            NSString *title = [NSString stringWithUTF8String:desc.Title.c_str()];
            if (!title)
                throw std::runtime_error("Invalid UTF-8 window title");

            [NSApplication sharedApplication];
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
            [NSApp finishLaunching];

            NSRect frame = NSMakeRect(0, 0, desc.Width, desc.Height);
            RavenNativeWindow *window = [[RavenNativeWindow alloc]
                initWithContentRect:frame
                          styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                     NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
                            backing:NSBackingStoreBuffered
                              defer:NO];
            if (!window)
                throw std::runtime_error("Could not create Cocoa window");

            window.releasedWhenClosed = NO;
            window.title = title;
            NSView *view = window.contentView;
            CAMetalLayer *layer = [CAMetalLayer layer];
            view.layer = layer;
            view.wantsLayer = YES;
            layer.delegate = (id<CALayerDelegate>)view;
            window.delegate = window;
            [window updateDrawableSize];
            [window center];
            [window makeKeyAndOrderFront:nil];
            [NSApp activate];
            m_WindowHandle = (__bridge_retained void *)window;
        }
    }

    CocoaWindow::~CocoaWindow()
    {
        @autoreleasepool
        {
            RavenNativeWindow *window = (__bridge_transfer RavenNativeWindow *)m_WindowHandle;
            window.delegate = nil;
            [window close];
            m_WindowHandle = nullptr;
        }
    }

    void CocoaWindow::PollEvents()
    {
        m_Input.ClearTransientState();
        @autoreleasepool
        {
            NSEvent *event;
            while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                             untilDate:[NSDate distantPast]
                                                inMode:NSDefaultRunLoopMode
                                               dequeue:YES]))
            {
                [NSApp sendEvent:event];
            }
            [NSApp updateWindows];
        }
    }

    bool CocoaWindow::ShouldClose() const
    {
        return ((__bridge RavenNativeWindow *)m_WindowHandle).shouldClose;
    }

    std::uint32_t CocoaWindow::GetWidth() const
    {
        NSView *view = ((__bridge RavenNativeWindow *)m_WindowHandle).contentView;
        return static_cast<std::uint32_t>([view convertRectToBacking:view.bounds].size.width);
    }

    std::uint32_t CocoaWindow::GetHeight() const
    {
        NSView *view = ((__bridge RavenNativeWindow *)m_WindowHandle).contentView;
        return static_cast<std::uint32_t>([view convertRectToBacking:view.bounds].size.height);
    }

    std::vector<const char *> CocoaWindow::GetRequiredVulkanInstanceExtensions() const
    {
        return {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_EXT_METAL_SURFACE_EXTENSION_NAME,
            VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        };
    }

    VkSurfaceKHR CocoaWindow::CreateVulkanSurface(VkInstance instance) const
    {
        auto createMetalSurface = reinterpret_cast<PFN_vkCreateMetalSurfaceEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateMetalSurfaceEXT"));
        if (!createMetalSurface)
            throw std::runtime_error("vkCreateMetalSurfaceEXT is unavailable");

        RavenNativeWindow *window = (__bridge RavenNativeWindow *)m_WindowHandle;
        VkMetalSurfaceCreateInfoEXT createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
        createInfo.pLayer = (CAMetalLayer *)window.contentView.layer;

        VkSurfaceKHR surface = VK_NULL_HANDLE;
        const VkResult result = createMetalSurface(instance, &createInfo, nullptr, &surface);
        if (result != VK_SUCCESS)
            throw std::runtime_error("vkCreateMetalSurfaceEXT failed: " + std::to_string(result));
        return surface;
    }

    const InputState& CocoaWindow::GetInputState() const
    {
        return m_Input;
    }

    std::unique_ptr<Window> Window::Create(const WindowDesc &desc)
    {
        return std::make_unique<CocoaWindow>(desc);
    }
}
