# Engine and sample modules

The Windows build separates reusable engine code from sample code:

| Target | Output | Responsibility |
| --- | --- | --- |
| `RavenCore` | `RavenCore.dll` and import library `RavenCore.lib` | Application loop, platform backend, input, logging, and Vulkan resource ownership |
| `RavenEngine` | `RavenEngine.exe` | Entry point, window description, perspective scene, camera controls, and animation |
| `RavenShaders` | Validated SPIR-V under `Shaders/<config>/` | Build dependency of `RavenCore` |
| `RavenContent` | Runtime copies under `Content/Shaders/` beside the sample | Stages validated shaders before the engine/sample build |

The DLL and executable are emitted together under `out/build/x64-debug/RavenEngine/` or `out/build/x64-release/RavenEngine/`. Existing executable paths and launch configurations remain valid. The `.lib` is an import library used when linking the sample; the engine implementation remains in the DLL, which is required at runtime. Shader lookup now uses executable-relative `Content/Shaders`; see [RuntimeUtilities.md](RuntimeUtilities.md) for relocation and content-root overrides.

## Exported interface

`Public/Raven/Core/Api.hpp` defines `RAVEN_API`. The engine build defines `RAVEN_CORE_EXPORTS` privately; consumers see DLL imports on Windows. Automatic export of all symbols is disabled. Export annotations are on individual functions so private renderer types and containers do not become exported classes.

The initial seven exported functions are `Application::Run`, `Log`, and the `VulkanContext` constructor, destructor, `PrepareSwapchain`, `CreateMesh`, and `DrawFrame`. Other Vulkan owners and backend implementations remain internal to the DLL. Headers such as math, camera, vertex data, input, and frame time describe the values borrowed by these calls.

The path/file milestone adds six exports: `Paths::GetExecutablePath`, `GetExecutableDirectory`, `GetContentPath`, `GetShaderPath`, and `Files::ReadBinary`/`ReadText`. `Application::Run` accepts an `ApplicationConfig` with `ContentRoot` through a three-argument overload. The original two-argument overload is retained and delegates using `ApplicationConfig{}`.

The texture milestone adds `VulkanContext::CreateTexture`, bringing the total at that milestone to 14 explicit exports. `TextureDesc` is public CPU data; the complete `VulkanTexture` owner remains private. Creation synchronously consumes the input span and returns a context-owned opaque reference. See [TextureResources.md](TextureResources.md).

The image-loading milestone adds `Images::Load`, bringing the total to 15 explicit exports. `ImageData` owns a standard pixel vector returned by value; the private decoder has internal linkage and is not exposed to clients. The image can be loaded before an application/device exists and passed to `CreateTexture`. See [ImageLoading.md](ImageLoading.md).

The development-diagnostic follow-up adds an `Images::Load(path, ImageLoadOptions)` overload and retains the original `Images::Load(path)` export. The one-argument overload delegates using default options. Both signatures are declared and defined consistently, bringing the DLL to 16 explicit exports. `WarnIfNonPowerOfTwo` defaults to `true`, while RavenCore's private development-build definition controls whether it logs a warning.

The signature audit restores the original two-argument `Application::Run` DLL entry point alongside the configured overload and exports the public `Window::Create(const WindowDesc&)` factory. The DLL now has 18 explicit exports and retains all 16 symbols from before this audit. Adding a default argument does not preserve the binary symbol of a shorter overload; keep an existing entry point when extending these APIs.

Both targets use the dynamic MSVC runtime: `/MDd` in Debug and `/MD` in Release. This is a C++ interface for modules built together with matching architecture, compiler/STL, runtime, and build configuration. Rebuild consumers when shared declarations change. It is not the future versioned game-module ABI or a compiler-independent plugin SDK.

## Public and private source trees

`RavenEngine/Public/Raven/` contains the engine's client-facing headers. Include them through the `Raven/` prefix, for example `Raven/Core/Application.hpp` and `Raven/Renderer/Vulkan/VulkanContext.hpp`. CMake shares only the `Public` include root with clients.

`RavenEngine/Private/` contains engine `.cpp` files, internal headers, platform backends, and shaders. Its include root belongs only to `RavenCore`. `Private/Samples/` contains the sample executable's own source and headers; that target receives only its local sample directory and the engine's public headers.

`FrameTime` is public callback data; `FrameClock` is an internal clock implementation. `VulkanDraw` is public submission data and forward-declares the borrowed `VulkanMesh`. The complete mesh class and GPU buffers remain private. `VulkanContext` owns a private implementation defined in its `.cpp`, preserving GPU resource order while removing internal Vulkan headers and container layout from the client interface.

Existing public function signatures and DLL exports are retained. The header paths and renderer class layout changed during this cleanup, so rebuild both the DLL and its consumers together.

## Client lifecycle and ownership

`ApplicationClient` supplies `OnFrame(renderer, input, frameTime)`. The sample implements it through `PerspectiveSample`. `Application` has no dependency on a concrete sample.

The caller supplies `ApplicationClientFactory` with two function pointers:

- `Create(renderer)` allocates and initializes a client in the caller's module.
- `Destroy(client)` destroys that client in the same module and must not throw.

Both callbacks are required and are checked before creating a window. A null client is rejected. The factory and client code must remain loaded throughout `Run`.

`Run` establishes its content root, then creates the window, renderer, and client. A custom-deleter `unique_ptr` invokes the caller's destroy function exactly once on normal exit or frame failure. The client is destroyed before the renderer, which waits for GPU completion before destroying its resources; the window outlives the renderer. Factory exceptions also unwind the renderer/window. The configured root remains available during cleanup and returns to the default afterward. Only one run may be active at a time; sequential runs remain supported.

Clients borrow the renderer and input during callbacks. `CreateMesh` copies the supplied CPU geometry into resources owned by the renderer and returns a borrowed mesh reference. That reference remains valid until renderer shutdown. Draw submissions are borrowed during frame recording. The sample owns its CPU scene/camera state, while the DLL owns GPU allocations and their destruction.

Frame timing and minimized-window behavior are preserved: event polling precedes `OnFrame`, a nondrawable framebuffer skips it, and the first drawable frame after startup or restore receives zero delta.

## Reload scope

`RavenCore.dll` is loaded normally with the executable and remains loaded for the application's lifetime. No runtime game-code reload is implemented by this split. A future `Game.dll` will need the separately planned versioned API, persistent-state ownership, reload lifecycle, and callback removal before unload.

## Validation on 2026-10-10

Windows Debug and Release configure/build successfully. PE inspection confirms seven explicit engine exports and sample imports for `Run`, `Log`, `CreateMesh`, and `DrawFrame`; the running Debug process loads the intended `RavenCore.dll`.

A temporary probe compiled from standard input passes 17 checks and renders eight frames in each configuration. It verifies missing callback rejection, null-client rejection, factory exceptions, mesh-validation exceptions across the DLL, frame exceptions, repeated application runs, exactly-once client destruction, renderer availability during client destruction, and initial zero delta. Probe binaries/objects are removed after validation; no test sources or framework are added to the project.

Computer use verifies perspective rendering, pause, camera orbit, wheel zoom, maximize, minimize/restore, and normal shutdown in Debug. Release verifies rendering, camera input, and normal shutdown from the `out/` working directory. Debug core/synchronization validation reports no warnings or errors. Evidence is under `out/validation/dll-*.log`.

After the public/private cleanup, both configurations rebuild successfully, including the moved HLSL shaders. All 13 public headers compile independently with only the public include root and Vulkan SDK headers. The same 17 client checks and eight rendered frames pass again in Debug and Release using only public engine headers plus sample-local headers. The DLL retains its seven exports. Native Debug rendering, camera input, maximize, minimize/restore, and normal shutdown are checked again with clean core/synchronization validation. Evidence is under `out/validation/public-private-*.log`; temporary probe executables and objects are removed.

After adding runtime utilities, both configurations build with 13 explicit exports, and all 15 public headers compile independently. The 17 client checks and eight rendered frames pass again per configuration. Utility probes pass 61 checks and 12 rendered frames per configuration, including relocated Unicode packages. Follow-up checks cover missing-shader startup cleanup, long file reads, and backend buffer/error behavior; see [RuntimeUtilities.md](RuntimeUtilities.md) for evidence and limits.

After adding texture ownership/upload, both configurations build, all 16 public headers compile independently, and the DLL has 14 explicit exports. Per configuration, production-source probes pass nine GPU byte readbacks and 21 injected resource/upload failures; public DLL clients pass 260 checks, eight frames, and 36 texture uploads. Debug core/synchronization validation is clean. See [TextureResources.md](TextureResources.md) and `out/validation/texture-*.log`.

## Signature audit on 2026-10-10

Two additional Windows/shared API defects are reproduced before correction: a client using the original two-argument `Application::Run` declaration fails to link, and a client calling public `Window::Create` fails to link because the factory is not exported. Both are corrected without changing the existing implementations' behavior.

Debug and Release builds pass. All 17 public headers and 21 Windows/shared private headers compile independently with `/W4 /WX`; public DLL clients check function/member-pointer types, pass 27 behavior checks, and render eight frames per configuration. Checks include both `Run` overloads, factory validation, direct public window creation, framebuffer/input access, renderer creation/texture upload, client cleanup, and content-root restoration. A separate client built using the original application declaration links and runs in each configuration. Debug Vulkan core/synchronization validation is clean. The DLL has 18 exports and preserves all 16 prior symbols. Evidence is under `out/validation/signature-*.log`; temporary probe binaries and objects are removed, and no tracked test source is added.

The Mac backend remains assigned to the Mac coworker under the owner's scope instruction. Its current header still declares `IsKeyDown(Key) const override`, although the shared `Window` interface has no such virtual method. It also lacks the required `const InputState& GetInputState() const` and `FramebufferState GetFramebufferState() const` overrides, leaving `Arm64Mac` abstract. A C++ header probe reproduces these compiler errors in `out/validation/signature-before-mac-header.log`. The Mac source selection additionally lacks an implementation of the shared `std::filesystem::path GetPlatformExecutablePath()` hook. These Mac findings remain open; the Mac backend is not included in the passing Windows build/header counts and has not been runtime-tested here.
