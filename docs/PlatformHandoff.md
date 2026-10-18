# macOS / Linux contributor handoff

Status as of 2026-10-14. The goal is to **run the engine and perspective/orthographic samples currently verified on Windows through the same shared interfaces**. Each platform contributor implements and verifies the work below on the target OS. Check items only after recording verification results. Platform services and current sample checks can proceed alongside Windows ECS development; gameplay and reload paths receive their final platform checks once those features exist.

## Current baseline and ownership

Windows has C++20 Debug/Release builds, native windows/input, Vulkan initialization and GPU selection, swapchain recovery, perspective/orthographic cameras, depth testing, and mesh/texture uploads. PNG/JPEG loading, executable-relative Content paths, transparent ordering, culling, compatible draw batching, common asset IDs/catalogs, and texture reuse are also implemented. Windows builds were checked on 2026-10-14. Existing GPU evidence is in [TextureResources.md](TextureResources.md) and [AssetSystem.md](AssetSystem.md); those native/GPU checks were not rerun during the documentation assessment.

macOS has `CocoaWindow`/`CocoaInput` implementations of the current window/input/framebuffer interfaces and `MacPaths.cpp`. Earlier Debug rendering/input checks are recorded in [MacOSBuild.md](MacOSBuild.md). Linux has a merged X11/XCB backend, framebuffer/executable paths, and Debug/Release presets, but still needs native build/runtime verification. Both platforms lack the `Platform::FillRandomBytes` implementation and CMake connection required by the latest asset code. Complete that hook first, then verify the current shared code. Earlier macOS smoke results do not establish current full support.

| Owner | Main locations | Goal |
| --- | --- | --- |
| macOS contributor | `RavenEngine/Private/Platform/Mac/` | Connect the random-byte hook, then verify current Cocoa input/framebuffer/paths and both samples natively |
| Linux contributor | `RavenEngine/Private/Platform/Linux/` | Connect the random-byte hook, then verify the merged XCB backend, paths, and both samples natively |
| Both contributors | `RavenEngine/CMakeLists.txt`, `CMakePresets.json`, build documentation | Coordinate per-OS source selection, dependencies, and shared-library runtime configuration |

Reuse the shared renderer, math, samples, and image decoder. If a shared API needs to change, explain why and assess the impact with the Windows contributor first. Preserve other contributors' changes to shared CMake files.

## Shared interfaces to check first

[Window.hpp](../RavenEngine/Public/Raven/Platform/Window.hpp) is the declaration authority. Backend headers and implementations must match return types, argument types, references, and `const` exactly.

```cpp
void PollEvents() override;
bool ShouldClose() const override;
std::uint32_t GetWidth() const override;
std::uint32_t GetHeight() const override;
FramebufferState GetFramebufferState() const override;
std::vector<const char*> GetRequiredVulkanInstanceExtensions() const override;
const InputState& GetInputState() const override;
VkSurfaceKHR CreateVulkanSurface(VkInstance instance) const override;
```

`Window::Create(const WindowDesc&)` creates the selected backend on each OS. Keep `RAVEN_API` on its public declaration and ensure backend objects are correctly destroyed through the virtual `Window` destructor. Pass the UTF-8 `WindowDesc::Title` string to the native title.

Follow [InputProcessing.md](InputProcessing.md) for Windows input ownership and execution order. Shared `InputState` provides mouse-button, cursor, and focus-recovery methods; private `InputBuffer` manages per-window pending input and published snapshots. Even when another window's poll processes an event, preserve that event's transitions until its own window calls `PollEvents()`. Windows uses this structure; Linux/macOS contributors must connect and verify their native implementations. Existing public and virtual signatures are preserved.

The required path hook in [PlatformPaths.hpp](../RavenEngine/Private/Platform/PlatformPaths.hpp) is:

```cpp
namespace Raven
{
	std::filesystem::path GetPlatformExecutablePath();
}
```

Return the **absolute path of the running host executable**, rather than the engine shared library or current working directory. Reuse shared [Paths.cpp](../RavenEngine/Private/Core/Paths.cpp) for the remaining Content/Shader path calculations.

Preserve these public entry points. Replacing an existing overload with a longer signature and default arguments can break already-built clients.

- `Application::Run(description, factory)` and `Application::Run(description, factory, config)`.
- `Images::Load(path)` and `Images::Load(path, ImageLoadOptions)`; options are passed by value.
- The public export of `Window::Create(const WindowDesc&)`.
- `ApplicationClient::OnFrame(VulkanContext&, const InputState&, const FrameTime&)` and the `ApplicationClientFactory` create/destroy callbacks. Keep Destroy `noexcept` and destroy objects in the module that created them.

See [EngineModules.md](EngineModules.md), [RuntimeUtilities.md](RuntimeUtilities.md), and [ImageLoading.md](ImageLoading.md) for contracts and the reasons for recent fixes. On every OS, a separate client using only public headers must link against the engine shared library. Verify public entry-point linking and calls; do not apply Windows export-name counts directly to other platforms.

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

   - [ ] Verify that `Raven::GetPlatformExecutablePath()` in the already-connected `MacPaths.cpp` returns the absolute host executable path.
   - [ ] Check Unicode and space-containing paths, different working directories, ContentRoot overrides, and relocation through the shared path checks.

4. **Verify Vulkan / MoltenVK execution**

   - [ ] Verify Metal surface creation, GPU enumeration, graphics/present queue selection, swapchain creation, and presentation on an actual Mac.
   - [ ] Check existing shared portability enumeration flags and portability subset device-extension handling. Coordinate additional requirements with the Windows contributor rather than duplicating the shared renderer.
   - [ ] Verify current HLSL/SPIR-V, depth formats, and RGBA8 Linear/SRGB uploads on the target GPU. Record unsupported-capability errors.

5. **Verify builds and library placement**

   - [ ] Complete native builds with `macos-debug` / `macos-release`. Verify the Apple Silicon target architecture, C++20/Objective-C++20, ARC, and Cocoa/QuartzCore linkage.
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

   - [ ] Verify that `Raven::GetPlatformExecutablePath()` in `LinuxPaths.cpp` returns the absolute host executable path through `/proc/self/exe`. Reuse shared path calculations and file readers. See [proc_pid_exe(5)](https://man7.org/linux/man-pages/man5/proc_pid_exe.5.html) for Linux path behavior.
   - [ ] Check executable location, other working directories, Unicode/space-containing paths, case-sensitive Linux filenames, and ContentRoot overrides.

4. **Verify CMake and presets**

   - [ ] Verify that XCB sources and `PkgConfig::XCB` connect to `RavenCore` only when `CMAKE_SYSTEM_NAME` is Linux. Preserve other platforms' source selection.
   - [ ] Complete actual builds with the merged `linux-debug` / `linux-release` presets. Keep MSVC and Cocoa settings out of shared code.
   - [ ] Verify discovery of C++20, Ninja, Vulkan, DXC, `spirv-val`, and XCB dependencies. Check sample/shared-library linking and runtime paths, plus Content/license staging.

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

## Shared verification criteria

- [ ] **Input:** Accumulate native events in each window's pending buffer. Publish its snapshot in that window's `PollEvents()`, then reset pending transient values. Preserve events processed earlier by another window until publication. Preserve both Pressed/Released and KeyEvents for press/release in one poll; repeated key-down must not create a new Pressed transition.
- [ ] **Keys and modifiers:** Translate native key identifiers to shared `Key` values, separating physical keys from text input. Check Shift/Control/Alt/Super, left/right modifiers, arrows, Home, R, and Space. Text input and a separate repeat API are also unfinished on Windows; coordinate new shared contracts separately.
- [ ] **Mouse:** Verify button transitions, cursor position/delta, horizontal/vertical wheel accumulation, and held-state release on focus loss. Avoid a large delta from the first cursor position or focus recovery. Compare units, origin, and wheel directions with Windows and record them.
- [ ] **Framebuffer:** Report drawable pixels and `0 x 0` when minimized/unavailable. Increment Revision on size changes; getters must not consume or reset it. Check resize, scaling changes, minimize/restore, and closing while minimized.
- [ ] **Timing and renderer:** Verify zero DeltaSeconds on the first drawable frame and first frame after restore. Check the two cubes, ground slab, PNG quad UV/tint, perspective proportions, depth occlusion, and orbit/zoom/reset/pause against Windows. Preserve the shared Blender axes and matrix contract in [CoordinateSystem.md](CoordinateSystem.md).
- [ ] **2D, transparency, and batching:** Check `--orthographic` pan/zoom, depth-independent sprite size, F1 order switching, and culling. Verify transparent overlap order, compatible instancing, and per-frame buffer reuse after resize in both samples.
- [ ] **Assets:** Verify UUID generation/parsing, typed references, catalog diagnostics, same-ID texture reuse, and separation between renderer contexts. Preserve the current ownership contract: destroying an asset service does not release GPU textures.
- [ ] **Files and images:** Check executable-relative Content/Shader paths, relative/absolute ContentRoot overrides, and invalid-path/missing-file diagnostics. Load PNG/JPEG into RGBA8 for texture creation. Accept NPOT images, emit the default warning only in development builds, support explicit suppression, and verify no Release warning.
- [ ] **Public API and lifetime:** Independently compile public headers, link a separate client, call both Run/Load overloads, and call the Window factory. Destroy clients in their own modules before the renderer, and destroy GPU resources/surfaces before native windows. Check both normal shutdown and initialization failure.
- [ ] **Packaging and results:** Run Debug/Release from other working directories and relocated packages. Resolve Debug Vulkan core/synchronization warnings/errors. Distinguish tested environments from unverified cases.

Both samples submit opaque/transparent passes with shared `CreateTexturedMesh` and the four-argument `DrawFrame` for their camera type. Preserve the color API and earlier overloads. Set 0 binding 0 is a sampled image and binding 1 a sampler; descriptors remain context-owned from creation through shutdown. Package both Texture HLSL outputs and the PNGs under `Content/Textures`. Image-top UV is V=0; verify opaque depth testing, straight-alpha ordering, and instancing. See [TextureResources.md](TextureResources.md) for contracts and evidence. Keep platform porting separate from implementing ECS, physics, audio, shader/asset reload, and game-module reload; those are shared-engine tasks in [TODO.md](../TODO.md).

## Completion report

- [ ] Record OS/architecture, compiler/CMake/Ninja/Vulkan tool versions, GPU, and graphics/present queue families.
- [ ] Record actual Debug/Release configure/build/run commands, dependency settings, and shared-library placement.
- [ ] Record passed/failed/unverified results for the criteria above, validation logs, and sample screenshots. Keep generated reproduction files and temporary probes in `out/`; do not leave validation-only implementations in the engine.
- [ ] For shared-file changes, provide Windows preset regression results or identify the checks needed from the Windows contributor. Do not mark unfinished backends fully supported.

Windows evidence is under `out/validation/signature-*.log` and the existing per-feature validation logs. Because `out/` is ignored by Git, include verification summaries and the required logs/screenshots in the change report when handing work to another contributor.
