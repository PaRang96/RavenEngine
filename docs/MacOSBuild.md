# Building on macOS

Status as of 2026-10-14: Cocoa window/input/framebuffer support, executable paths, and build presets are implemented. The latest shared asset code requires `Platform::FillRandomBytes`, which is not yet implemented or connected on macOS. The contributor must add that hook and its CMake source/system dependencies, then verify current Debug/Release builds and both perspective/orthographic samples. The 2026-10-10 execution/input records below describe an earlier revision and do not establish current complete build support. Follow the [platform handoff](PlatformHandoff.md) for remaining work.

## Dependencies and configuration

Install Xcode Command Line Tools, CMake 3.24 or newer, Ninja, Vulkan development libraries, DXC with SPIR-V support (`dxc`), and SPIRV-Tools (`spirv-val`).

The [official macOS Vulkan SDK](https://vulkan.lunarg.com/doc/view/latest/mac/getting_started.html) includes DXC. Homebrew's `shaderc`/`glslang` alone do not provide the DXC command used by this project.

After installing the SDK, set the environment variables to its actual location. `VULKAN_SDK` points to the SDK's `macOS` directory; the tools directory is lowercase `bin`.

```sh
export VULKAN_SDK="/path/to/VulkanSDK/<version>/macOS"
export PATH="$VULKAN_SDK/bin:$PATH"
cmake --preset macos-debug
cmake --build out/build/macos-debug
```

If the build already uses Homebrew Vulkan loader/headers, you can specify only the DXC path.

```sh
cmake --preset macos-debug -DRAVEN_DXC_EXECUTABLE="$VULKAN_SDK/bin/dxc"
cmake --build out/build/macos-debug
```

`RAVEN_DXC_EXECUTABLE-NOTFOUND` is a CMake dependency-discovery failure before C++ compilation. Install `dxc` or point the option to its actual executable, then configure again. Do not substitute `glslc`: its command-line options differ and shader compilation will fail.

The earlier macOS validation workspace prepared SDK 1.4.363.0 under `out/tools/vulkan-sdk/1.4.363.0` without a system installation. If that local path exists in your environment, run the following from the repository root to use its DXC.

```sh
cmake --preset macos-debug \
    -DRAVEN_DXC_EXECUTABLE="$PWD/out/tools/vulkan-sdk/1.4.363.0/macOS/bin/dxc"
cmake --build out/build/macos-debug
```

`out/` is ignored by Git. A fresh checkout or deletion of `out/` requires installing and configuring the tools again. Release uses the `macos-release` preset and build directory.

The executable is generated at `out/build/macos-debug/RavenEngine/RavenEngine`.

```sh
./out/build/macos-debug/RavenEngine/RavenEngine
```

## Validation layer lookup

Debug uses `VK_LAYER_KHRONOS_validation`. Homebrew's layer manifest specifies only a library name, so layer enumeration can succeed while dylib lookup fails, producing `vkCreateInstance failed: -6` (`VK_ERROR_LAYER_NOT_PRESENT`). CMake locates the installed validation dylib and adds its directory to Debug `BUILD_RPATH`. This supports development runs from the build directory; installed package paths still need separate configuration.

## Earlier native and input validation

On 2026-10-10, the Debug sample ran on an Apple M4 Pro without environment variables. Logs confirmed GPU selection, a 2560 × 1440 swapchain, and the first perspective frame. Those startup logs contained no Vulkan warnings/errors. The macOS instance extensions also include `VK_KHR_get_physical_device_properties2`, required by portability subset under Vulkan 1.0.

`CocoaInput.mm` translates keyboard/mouse input into shared `InputState`. Keys use physical key codes instead of input-language or IME text, and repeated key-down does not create a new `Pressed` transition. Left/right Shift/Control/Option/Command map to the corresponding shared modifier keys. Focus loss, minimization, and closing release held keys and buttons.

Mouse coordinates use actual pixels with the content area's top-left origin, matching Windows. Horizontal wheel values are positive to the right and vertical values positive upward; macOS natural-scroll reversal is normalized to those directions. Precise trackpad deltas map 10 points to one wheel step while preserving fractional values. This scale converts to the engine's step unit; see [Apple's scrollingDeltaY documentation](https://developer.apple.com/documentation/appkit/nsevent/scrollingdeltay) for AppKit delta units.

A temporary input-conversion probe passed 47 checks covering Space repeat/short taps, arrows/Home/R, left/right modifiers, wheel accumulation/reversal, Retina cursor coordinates, dragging/releasing outside the content area, and focus cleanup. Its log is `out/validation/cocoa-input-probe.log`. In the actual Debug window, two paused screenshots matched, and scroll zoom and R reset were observed. Native verification used a temporary `out/validation/RavenInputCheck.app` bundle. Multiple input devices/keyboard layouts and remaining resize/minimize/restore checks are tracked in the [platform handoff](PlatformHandoff.md).
