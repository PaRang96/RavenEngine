# macOS / Linux contributor handoff

Status as of 2026-10-14. The goal is to **run the engine and perspective/orthographic samples currently verified on Windows through the same shared interfaces**. Each platform contributor implements and verifies the work below on the target OS. Check items only after recording verification results. Platform services and current sample checks can proceed alongside Windows ECS development; gameplay and reload paths receive their final platform checks once those features exist.

## Current baseline and ownership

Windows has C++20 Debug/Release builds, native windows/input, Vulkan initialization and GPU selection, swapchain recovery, perspective/orthographic cameras, depth testing, and mesh/texture uploads. PNG/JPEG loading, executable-relative Content paths, transparent ordering, culling, compatible draw batching, common asset IDs/catalogs, and texture reuse are also implemented. Windows builds were checked on 2026-10-14. Existing GPU evidence is in [TextureResources.md](TextureResources.md) and [AssetSystem.md](AssetSystem.md); those native/GPU checks were not rerun during the documentation assessment.

macOS has `CocoaWindow`/`CocoaInput` implementations of the current window/input/framebuffer interfaces and `MacPaths.cpp`. Earlier Debug rendering/input checks are recorded in [MacOSBuild.md](MacOSBuild.md). Linux has a merged X11/XCB backend, framebuffer/executable paths, and Debug/Release presets, but still needs native build/runtime verification. Both platforms lack the `Platform::FillRandomBytes` implementation required by the latest asset code. Add the implementation under the selected native folder; CMake now discovers that folder automatically. Complete that hook first, then verify the current shared code. Earlier macOS smoke results do not establish current full support.

| Owner | Main locations | Goal |
| --- | --- | --- |
| macOS contributor | `RavenEngine/Private/Platform/Mac/` | Connect the random-byte hook, then verify current Cocoa input/framebuffer/paths and both samples natively |
| Linux contributor | `RavenEngine/Private/Platform/Linux/` | Connect the random-byte hook, then verify the merged XCB backend, paths, and both samples natively |
| Both contributors | `cmake/RavenPlatform.cmake`, `cmake/RavenBuildSettings.cmake`, `CMakePresets.json`, build documentation | Place native sources in the selected platform folder; configure dependencies in RavenPlatform and shared language/runtime/PCH settings in RavenBuildSettings |

Reuse the shared renderer, math, samples, and image decoder. If a shared API needs to change, explain why and assess the impact with the Windows contributor first. Preserve other contributors' changes to shared CMake files.

## Port contract (2026-11-27)

Windows is the head platform, and platform code is only ever encapsulated (planning decision 21). The layer's shape is decided in [PlatformLayerDesign.md](PlatformLayerDesign.md): each area is one header in `RavenEngine/Private/Platform/`, every function lives in `Raven::Platform`, and a port implements the areas in its own folder. Shared code never names an OS: `scripts/CheckPlatformCode.cmake`, a step of Windows CI, fails on an OS macro or OS header outside the platform folders.

| Area | Header | A port implements | Windows | macOS and Linux today | Capability |
| --- | --- | --- | --- | --- | --- |
| Process | `PlatformProcess.hpp` | `StartBuildProcess`, `PollBuildProcess`, `DestroyBuildProcess`; `LoadGameLibrary`, `FindGameEntryPoint`, `UnloadGameLibrary`, `ImageContains` | `Win32BuildProcess.cpp`, `Win32GameModule.cpp` | `Fallback/FallbackBuildProcess.cpp`, `Fallback/FallbackGameModule.cpp` | `BuildProcesses`, `GameModuleReload` |
| Files | `PlatformFiles.hpp` | `ReplaceSavedFile`; `DirectoryWatch` | `Win32AtomicFile.cpp`, `Win32DirectoryWatch.cpp` | `Posix/PosixAtomicFile.cpp` (shared); `Fallback/PollDirectoryWatch.cpp` | `NativeDirectoryWatch` |
| Paths | `PlatformPaths.hpp` | `GetExecutablePath`, `GetUserDataDirectory` | `Win32Paths.cpp` | `Mac/MacPaths.cpp`, `Linux/LinuxPaths.cpp` | — |
| Random | `PlatformRandom.hpp` | `FillRandomBytes` | `Win32Random.cpp` | missing: connect it first (M4) | — |
| Input | `PlatformInput.hpp` | `GetNativeKeyName`; the window's input feeds `InputBuffer` | `Win32Input.cpp` | `Fallback/FallbackKeyNames.cpp` | `NativeKeyNames` |
| Window | `Public/Raven/Platform/Window.hpp` | `Window::Create` and the `Window` interface below | `Win32Window.cpp` | `Mac/CocoaWindow.mm`, `Linux/XcbWindow.cpp` | — |
| Capabilities | `PlatformCapabilities.hpp` | `Platform/<OS>/PlatformCapabilitiesOs.hpp`: the values above | all true | all false | — |

**Landing a backend for an area:**
1. Add the implementation under `Platform/<OS>/`, or under `Platform/Posix/` when macOS and Linux share it.
2. Remove that area's fallback from the OS's `raven_platform_fallbacks` list in `cmake/RavenPlatform.cmake`.
3. Set the capability in `Platform/<OS>/PlatformCapabilitiesOs.hpp`, in the same change.

Shared code then picks it up with no edit, because it branches only on the capability.

## Shared interfaces to check first

[Window.hpp](../RavenEngine/Public/Raven/Platform/Window.hpp) is the declaration authority. Backend headers and implementations must match return types, argument types, references, and `const` exactly.

```cpp
void PollEvents() override;
bool ShouldClose() const override;
FramebufferState GetFramebufferState() const override;
std::vector<const char*> GetRequiredVulkanInstanceExtensions() const override;
const InputState& GetInputState() const override;
VkSurfaceKHR CreateVulkanSurface(VkInstance instance) const override;
```

`Window::Create(const WindowDesc&)` creates the selected backend on each OS. Keep `RAVEN_API` on its public declaration and ensure backend objects are correctly destroyed through the virtual `Window` destructor. Pass the UTF-8 `WindowDesc::Title` string to the native title. `WindowDesc::Position` and `WindowDesc::Borderless` (2026-11-24) place a frameless topmost window for native-resolution benchmarks; Win32 implements them, and other backends may ignore them until a contributor needs the benchmark there. `RendererConfig` (present mode, GPU preference) and `ApplicationConfig::MaxFrameRate` are shared code; the frame cap sleeps with `std::this_thread::sleep_until` on every OS.

Follow [InputProcessing.md](InputProcessing.md) for Windows input ownership and execution order. Shared `InputState` provides mouse-button, cursor, and focus-recovery methods; private `InputBuffer` manages per-window pending input and published snapshots. Even when another window's poll processes an event, preserve that event's transitions until its own window calls `PollEvents()`. Windows uses this structure; Linux/macOS contributors must connect and verify their native implementations. Existing public and virtual signatures are preserved.

The required path hook in [PlatformPaths.hpp](../RavenEngine/Private/Platform/PlatformPaths.hpp) is:

```cpp
namespace Raven::Platform
{
	std::filesystem::path GetExecutablePath();
}
```

Return the **absolute path of the running host executable**, rather than the engine shared library or current working directory. Reuse shared [Paths.cpp](../RavenEngine/Private/Core/Paths.cpp) for the remaining Content/Shader path calculations.

Preserve these public entry points. Replacing an existing overload with a longer signature and default arguments can break already-built clients.

- `Application::Run(description, factory)` and `Application::Run(description, factory, config)`.
- `Images::Load(path)` and `Images::Load(path, ImageLoadOptions)`; options are passed by value.
- The public export of `Window::Create(const WindowDesc&)`.
- Before M6A: `ApplicationClient::OnFrame(VulkanContext&, const InputState&, const FrameTime&)` and the `ApplicationClientFactory` create/destroy callbacks. Keep Destroy `noexcept` and destroy objects in the module that created them.

See [EngineModules.md](EngineModules.md), [RuntimeUtilities.md](RuntimeUtilities.md), and [ImageLoading.md](ImageLoading.md) for contracts and the reasons for recent fixes. On every OS, a separate client using only public headers must link against the engine shared library. Verify public entry-point linking and calls; do not apply Windows export-name counts directly to other platforms.

## Hot reload since M5

M5 ([HotReloadDesign.md](HotReloadDesign.md)) moved every reload path onto shared code. Each platform supplies only the backends below.
- **Directory watching:** the reload service ([ReloadService.cpp](../RavenEngine/Private/Core/ReloadService.cpp)) watches assets, scenes, the project, game sources, the game DLL and shaders through `Platform::DirectoryWatch`.
  - Notifications are only wake-ups: the service compares sizes and write times, and reads contents every 2 s as an audit.
  - `Platform/Fallback/PollDirectoryWatch.cpp` works everywhere as a fallback. FSEvents or inotify only make detection faster.
- **Build processes:** shader watching ([ShaderLibrary.cpp](../RavenEngine/Private/Renderer/Shaders/ShaderLibrary.cpp)) and game builds run `cmake -P` through `Platform::StartBuildProcess` ([PlatformProcess.hpp](../RavenEngine/Private/Platform/PlatformProcess.hpp)), which takes UTF-8 arguments. Windows implements it in `Platform/Windows/Win32BuildProcess.cpp`; other systems get `Platform/Fallback/FallbackBuildProcess.cpp` with `Capabilities.BuildProcesses` false, so shader watching logs that it is off instead of failing. A POSIX backend replaces that fallback (port contract above). The generated `GameBuildConfig.hpp` and `ShaderWatchConfig.hpp` hold their paths as escaped UTF-8 (`raven_utf8_literal`), and `raven_capture_build_environment` records `CC`, `CXX` and `PATH` for non-MSVC toolchains.
- **Game modules:** the shared `GameModule` builds and activates the DLL. Your loader backend replaces `Fallback/FallbackGameModule.cpp` and sets `Capabilities.GameModuleReload`.
- **DXC on your host:** the watcher runs DXC twice per compile, a `-M -MF` dependency pass and then the compile. Check that your DXC build supports `-MF`.
- **Verify:** `scripts/Test-Windows3DLive.py` drives the Windows executable. Its cases are the checklist for a native port: shader stages, rejections, views, DLL, scene, PNG and FBX.

## New platform hook for common assets

UUIDv4 `AssetId`, `AssetRef<T>`, the in-memory `AssetCatalog`, and ID-based texture reuse are implemented. Reuse the shared generation/parsing/formatting and asset code. Windows Debug/Release public DLL calls and GPU reuse are verified; macOS/Linux contributors must connect their native random providers. Keep native UUID/GUID types out of shared asset headers.

The exact signature in [PlatformRandom.hpp](../RavenEngine/Private/Platform/PlatformRandom.hpp) is:

```cpp
namespace Raven::Platform
{
	void FillRandomBytes(std::span<std::uint8_t> bytes);
}
```

Fill the entire buffer with secure operating-system random bytes or throw. Shared `AssetId.cpp` sets UUID version/variant bits; platform code must not set them again or copy native GUID memory. Do not fall back to time, counters, or fixed values on failure.

The macOS contributor should implement an OS provider such as `SecRandomCopyBytes` in the private platform directory and connect its source and required Security framework in the CMake APPLE branch. The Linux contributor should handle partial reads, EINTR, and failures from a provider such as `getrandom`, and add the source to Linux selection. Currently only Windows connects `Win32Random.cpp` and `bcrypt`; other platforms need this hook to complete linking.

Generate an asset ID once for a new registration with `Generate()`, and read preserved strings with `Parse(std::string_view)`. Do not regenerate IDs on every load. Preserve types, value passing, and const qualifiers for `AssetCatalog::Register(const AssetRecord&)`, `Register(const TextureAssetDesc&)`, `GetTexture(AssetRef<TextureAsset>) const`, and `AssetResources::LoadTexture(AssetRef<TextureAsset>)`. `Parse` currently accepts UUIDv4 only; `ToString` uses the lowercase 36-character form. See [AssetSystem.md](AssetSystem.md) for ownership and validation scope.

## macOS contributor: work order

1. **Connect the random-byte hook required by current asset code**

   - [ ] Implement `Platform::FillRandomBytes` in a Mac private source and connect the source and required system dependencies in CMake's APPLE branch.
   - [ ] Call `AssetId::Generate` from a public client to verify linking, UUID version/variant bits, parsing round trips, and random-provider failure propagation.
   - [ ] Verify that existing `CocoaWindow.hpp` and `.mm` compile against the current public window signatures. `GetInputState` and `GetFramebufferState` already exist; check their compatibility with the latest code.

2. **Verify Cocoa input and drawable state**

   - [ ] Verify existing `CocoaInput.mm` key/modifier/mouse/wheel conversion against current shared `InputState`. Distinguish the 2026-10-10 input checks from the current snapshot ownership/publication contract.
   - [ ] Verify drawable pixel dimensions and persistent framebuffer revisions across Retina scaling, resize, minimize, and restore.
   - [ ] Process events on the window thread and ensure a close request does not destroy the native window before the Vulkan surface is released.

3. **Verify the existing executable-path hook**

   - [ ] Verify that `Raven::Platform::GetExecutablePath()` in the already-connected `MacPaths.cpp` returns the absolute host executable path.
   - [ ] Check Unicode and space-containing paths, different working directories, ContentRoot overrides, and relocation through the shared path checks.

4. **Verify Vulkan / MoltenVK execution**

   - [ ] Verify Metal surface creation, GPU enumeration, graphics/present queue selection, swapchain creation, and presentation on an actual Mac.
   - [ ] Check existing shared portability enumeration flags and portability subset device-extension handling. Coordinate additional requirements with the Windows contributor rather than duplicating the shared renderer.
   - [ ] Query `VkPhysicalDevicePortabilitySubsetFeaturesKHR` during device selection and log it once (M6B contract 8, [RenderingContentDesign.md](RenderingContentDesign.md#8-device-limits-and-bindless-classes-aa2-rnd-07)). It needs `VK_ENABLE_BETA_EXTENSIONS` for that translation unit only.
   - [ ] Verify current HLSL/SPIR-V, depth formats, and RGBA8 Linear/SRGB uploads on the target GPU. Record unsupported-capability errors.

5. **Verify builds and library placement**

   - [ ] Complete native builds with `macos-debug` / `macos-release`. Verify the Apple Silicon target architecture, C++23/Objective-C++23, ARC, and Cocoa/QuartzCore linkage.
   - [ ] Document reproducible DXC, `spirv-val`, and Vulkan loader/MoltenVK discovery/runtime settings. Keep developer-specific absolute paths out of sources.
   - [ ] Verify that the relocated sample finds the `RavenCore` shared library, `Content/Shaders`, and `Licenses/stb-LICENSE.txt`. Check shared-library runtime search paths.

After connecting the random-byte hook, use these build/run paths and record native results for the latest shared code.

```sh
cmake --preset macos-debug
cmake --build out/build/macos-debug
./out/build/macos-debug/RavenEngine/RavenEngine

cmake --preset macos-release
cmake --build out/build/macos-release
./out/build/macos-release/RavenEngine/RavenEngine
```

## Linux contributor: work order

1. **Check the X11/XCB target environment**

   - [ ] Record the distribution, desktop session, GPU, and use of X11 or XWayland. The current backend uses X11/XCB; there is no native Wayland backend.
   - [ ] Prepare and verify discovery of `pkg-config`, `xcb`, and `xcb-xkb` development dependencies. Track other window-system support separately.
   - [ ] Implement `Platform::FillRandomBytes` in a Linux private source and connect it in the CMake Linux branch. Verify partial reads, EINTR, and failures, plus public-client linking and UUID generation/parsing round trips.

2. **Verify the merged window backend**

   - [ ] Verify that `Private/Platform/Linux/XcbWindow.hpp` and `.cpp` compile with current public headers and match every shared window signature above.
   - [ ] Call the Linux `Window::Create(const WindowDesc&)` linked into `RavenCore` from a separate client. Verify that exactly one factory definition is linked.
   - [ ] Verify window creation/closing/event handling, UTF-8 titles, and input. Check that drawable dimensions and framebuffer revisions updated through map/unmap and configure events satisfy minimize/restore contracts.

3. **Verify executable paths**

   - [ ] Verify that `Raven::Platform::GetExecutablePath()` in `LinuxPaths.cpp` returns the absolute host executable path through `/proc/self/exe`. Reuse shared path calculations and file readers. See [proc_pid_exe(5)](https://man7.org/linux/man-pages/man5/proc_pid_exe.5.html) for Linux path behavior.
   - [ ] Check executable location, other working directories, Unicode/space-containing paths, case-sensitive Linux filenames, and ContentRoot overrides.

4. **Verify CMake and presets**

   - [ ] Verify that XCB sources and `PkgConfig::XCB` connect to `RavenCore` only when `CMAKE_SYSTEM_NAME` is Linux. Preserve other platforms' source selection.
   - [ ] Complete actual builds with the merged `linux-debug` / `linux-release` presets. Keep MSVC and Cocoa settings out of shared code.
   - [ ] Verify C++23 compiler and library support and discovery of Ninja, Vulkan, DXC, `spirv-val`, and XCB dependencies. Check sample/shared-library linking and runtime paths, plus Content/license staging.

5. **Verify Vulkan execution**

   - [ ] Verify XCB instance extensions and `CreateVulkanSurface()`, then exercise GPU selection through presentation.
   - [ ] Verify resize, unavailable drawable state, and out-of-date/suboptimal recovery through the shared renderer.
   - [ ] If hardware with separate graphics/present queue families is available, verify it too. Otherwise record it as unverified and report the GPU/queues actually tested.

Prepare dependencies and the random-byte hook before running these commands, then record actual results. Windows Debug/Release passed when the backend was merged, but the WSL environment at that time lacked Vulkan/XCB development dependencies, DXC, and `spirv-val`, so full Linux builds and execution were not verified.

```sh
cmake --preset linux-debug
cmake --build out/build/linux-debug
./out/build/linux-debug/RavenEngine/RavenEngine

cmake --preset linux-release
cmake --build out/build/linux-release
./out/build/linux-release/RavenEngine/RavenEngine
```

## GPU requirements

Since M3 the renderer needs the following, and reports an unsupported device otherwise:
- **Version:** Vulkan 1.2 with `timelineSemaphore`.
- **Extensions:** `VK_KHR_dynamic_rendering` and `VK_KHR_synchronization2`, with their features enabled.
- **Descriptor indexing for bindless textures:** update-after-bind, partially bound sampled-image arrays (4,096 elements).
- **Indirect drawing:** `multiDrawIndirect` and `drawIndirectFirstInstance`.
- **Queues:** one queue family with graphics *and* compute (the GPU-scene compute passes run on the graphics queue), plus a family that can present. The two may differ; the swapchain then uses concurrent sharing.
- **Optional:**
  - `drawIndirectCount` is queried but unused, kept for a future compaction pass ([GpuDrivenDesign.md](GpuDrivenDesign.md)).
  - `fillModeNonSolid` enables the wireframe debug view. Without it, `IsRenderViewAvailable(RenderView::Wireframe)` is false and F7 skips the view ([HotReloadDesign.md](HotReloadDesign.md#h3--debug-views-and-general-use-shaders-q3)).

Among devices that meet every requirement, a discrete GPU is preferred, then an integrated one, then any other; enumeration order breaks ties (owner decision, 2026-11-23). On Windows the loader already sorts devices by the OS GPU preference, and listing the Intel driver first with `VK_DRIVER_FILES` did not change that order, so the ranking path for an integrated-first order has not run.

| Device (Windows, 2026-11-23) | Vulkan | Queue families | Result |
| --- | --- | --- | --- |
| NVIDIA GeForce RTX 3070 Ti Laptop GPU (minimum spec) | 1.4 | 0: graphics+compute+transfer, present; 1: transfer; 2: compute+transfer, present; 3–5: video/optical flow | Selected by default; Debug smoke clean |
| Intel Iris Xe Graphics (same laptop, forced with `VK_LOADER_DRIVERS_SELECT=*igvk*`) | 1.3 | 0: graphics+compute+transfer, present; 1: compute, present | Debug smoke passed with no validation messages |

Both devices use one combined graphics/present family. The separate-family path (concurrent swapchain images, a separate present queue) has not run on any device.

## Shared verification criteria

- [ ] **Input:** Accumulate native events in each window's pending buffer. Publish its snapshot in that window's `PollEvents()`, then reset pending transient values. Preserve events processed earlier by another window until publication. Preserve both Pressed/Released and their `Events` entries for press/release in one poll; repeated key-down must not create a new Pressed transition. Name keys through `Platform::GetNativeKeyName` (UCKeyTranslate on macOS, xkbcommon on Linux), replacing `UnsupportedKeyNames.cpp`.
- [ ] **Keys and modifiers:** Translate native key identifiers to shared `Key` values, separating physical keys from text input. Check Shift/Control/Alt/Super, left/right modifiers, arrows, Home, R, and Space. Report native auto-repeat through `RepeatKey()` (macOS `isARepeat`, X11 detectable auto-repeat) and keep `InputState::Focused` current; see [InputProcessing.md](InputProcessing.md#focus-key-repeat-and-stable-key-ids-2026-11-23). Text input is not defined yet on any platform.
- [ ] **Mouse:** Verify button transitions, cursor position/delta, horizontal/vertical wheel accumulation, and held-state release on focus loss. Avoid a large delta from the first cursor position or focus recovery. Compare units, origin, and wheel directions with Windows and record them.
- [ ] **Framebuffer:** Report drawable pixels and `0 x 0` when minimized/unavailable. Increment Revision on size changes; getters must not consume or reset it. Check resize, scaling changes, minimize/restore, and closing while minimized.
- [ ] **Timing and renderer:** Verify zero DeltaSeconds on the first drawable frame and first frame after restore. Check the two cubes, ground slab, PNG quad UV/tint, perspective proportions, depth occlusion, and orbit/zoom/reset/pause against Windows. Preserve the shared Blender axes and matrix contract in [CoordinateSystem.md](CoordinateSystem.md).
- [ ] **Transparency and batching:** (The 2D Courtyard checks were dropped with the sample, planning decision 18.) Check the Gallery's sprite quads, F1 order switching, and culling. Verify transparent overlap order, compatible instancing, and per-frame buffer reuse after resize in both samples.
- [ ] **Assets:** Verify UUID generation/parsing, typed references, catalog diagnostics, same-ID texture reuse, and separation between renderer contexts. Preserve the current ownership contract: destroying an asset service does not release GPU textures.
- [ ] **Files and images:** Check executable-relative Content/Shader paths, relative/absolute ContentRoot overrides, and invalid-path/missing-file diagnostics. Load PNG/JPEG into RGBA8 for texture creation. Accept NPOT images, emit the default warning only in development builds, support explicit suppression, and verify no Release warning.
- [ ] **Public API and lifetime:** Independently compile public headers, link a separate client, call both Run/Load overloads, and call the Window factory. Destroy clients in their own modules before the renderer, and destroy GPU resources/surfaces before native windows. Check both normal shutdown and initialization failure.
- [ ] **Packaging and results:** Run Debug/Release from other working directories and relocated packages. Resolve Debug Vulkan core/synchronization warnings/errors. Distinguish tested environments from unverified cases.

*Since M6B's B5 (2026-11-27), sprites are render-world objects drawn from the bindless array ([RenderingContentDesign.md](RenderingContentDesign.md#b5--sprites-on-the-gpu-path-2026-11-27)); `CreateTexturedMesh`, the textured `DrawFrame` spans and the per-texture descriptor sets are gone, and the packaged shaders are `Sprite.vert.spv` and `Sprite.frag.spv`. The rest of this paragraph is the earlier contract.* Both samples submit opaque/transparent passes with shared `CreateTexturedMesh` and the four-argument `DrawFrame` for their camera type. Preserve the color API and earlier overloads. Set 0 binding 0 is a sampled image and binding 1 a sampler; descriptors remain context-owned from creation through shutdown. Package both Texture HLSL outputs and the PNGs under `Content/Textures`. Image-top UV is V=0; verify opaque depth testing, straight-alpha ordering, and instancing. See [TextureResources.md](TextureResources.md) for contracts and evidence. Keep platform porting separate from shared engine work tracked in [TODO.md](../TODO.md). The [roadmap](../ROADMAP.md#deferred-collision-and-physics) records the collision and physics deferral.

## Completion report

- [ ] Record OS/architecture, compiler/CMake/Ninja/Vulkan tool versions, GPU, and graphics/present queue families.
- [ ] Record actual Debug/Release configure/build/run commands, dependency settings, and shared-library placement.
- [ ] Record passed/failed/unverified results for the criteria above, validation logs, and sample screenshots. Keep generated reproduction files and temporary probes in `out/`; do not leave validation-only implementations in the engine.
- [ ] For shared-file changes, provide Windows preset regression results or identify the checks needed from the Windows contributor. Do not mark unfinished backends fully supported.

Windows evidence is under `out/validation/signature-*.log` and the existing per-feature validation logs. Because `out/` is ignored by Git, include verification summaries and the required logs/screenshots in the change report when handing work to another contributor.
