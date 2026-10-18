# RavenEngine Roadmap

This checklist defines **v1**: a C++20 engine that can build and run small 2D and 3D games on Windows, Linux, and macOS using Vulkan, ECS, and reloadable game code, shaders, and assets. Games are authored with code and data files; a visual editor is outside the v1 scope. Check an item only when its behavior is demonstrated in a sample or focused test.

The agreed world convention follows Blender: **right-handed, +X right, +Y reference forward, +Z up, XY ground plane**. Blender is the primary authoring tool, FBX is the primary planned 3D asset format, and a Blender live bridge is a future integration goal. See [the coordinate contract](docs/CoordinateSystem.md) for camera, import, and renderer boundaries and the remaining math decisions.

Rendering targets a **perspective camera first**, with shared model/view/projection math, simple 3D geometry, and depth testing. That initial sample and the following orthographic 2D rendering sample are now implemented using the same world and transform/view conventions. This order supports the Blender/FBX direction; both playable 2D and 3D samples remain v1 goals.

## Current Status — 2026-10-14

RavenEngine has a working Windows rendering foundation and is ready to begin its scene/game framework. `RavenCore.dll` owns reusable engine code; `RavenEngine.exe` contains perspective and orthographic samples. Shared transforms, depth, opaque/transparent textures, draw ordering, frustum culling, compatible textured instancing, relocatable content, UUIDv4 asset references, an in-memory catalog and texture reuse are implemented. ECS, scene persistence/lifecycle, resource release handles, imported meshes, lighting, physics, audio, playable games and runtime reload remain pending.

The current Windows Debug/Release builds were checked successfully on 2026-10-14; CTest reports zero registered tests. Earlier native/GPU validation records below were reviewed, not rerun in that assessment. Most rendering evidence is from an RTX 3070 Ti Laptop GPU. CTest integration remains deferred at the owner's request; revisit it when defining entity/component lifetime checks. Temporary probes under ignored `out/` do not provide a regression suite from a clean checkout.

Cocoa window/input/framebuffer and executable-path implementations, macOS presets and earlier Debug smoke evidence exist. Linux X11/XCB code, executable paths and presets are merged, with native verification pending. Both platforms still need the random-byte hook introduced by the asset system before current shared-code support can be considered complete. See [PlatformHandoff.md](docs/PlatformHandoff.md).

The working order is minimal ECS/renderable scene, persistent asset/scene data and resource lifetime, playable 2D, imported/lit 3D gameplay, shader/asset reload, then game-code reload. Platform contributors can work in parallel. [TODO.md](TODO.md) is the detailed working list; its immediate next task is entity creation/destruction, generation-based identity, slot reuse and stale-reference rejection.

## Historical milestone evidence

The following records describe each milestone when validated. Later milestones supersede earlier triangle implementations, shader paths, sample ownership and export counts; use the current summary and linked subsystem contracts for present behavior.

The Windows Vulkan bootstrap created a native Win32 window and clear-frame path. A focused native probe presented 407 frames per configuration on an NVIDIA GeForce RTX 3070 Ti Laptop GPU (`graphics=0`, `present=0`) with clean Debug core/synchronization validation. Resize, minimize/restore, shutdown, injected acquire/present recovery and construction rollback passed. The first clear frame is recorded at `out/screenshots/ravenengine-clear-frame.jpg`. Per-monitor v2 DPI awareness and DPI-message handling were implemented; mixed-DPI monitor movement remains unverified.

Resize and shutdown currently wait for device idle. Strict presentation-completion fencing remains a future extension-based improvement; unextended Vulkan has no present fence ([Khronos guidance](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)).

`FrameClock` provides monotonic elapsed seconds and frame delta. The first drawable frame and first frame after restore receive zero delta; total elapsed time includes minimized time. Debug/Release builds, focused clock checks, and native minimize/restore timing pass. Separate 10-second clear-loop samples after a one-second warm-up measure **59.3 FPS in Debug** (593 successful presentations) and **59.7 FPS in Release** (597), with the existing 16 ms sleep and FIFO presentation. Neither sample reports presentation failures or warning/error diagnostics; Debug runs with synchronization validation enabled.

The initial vertex/fragment shaders are written in HLSL and compiled by DXC through the `RavenShaders` build dependency. Both configurations generate SPIR-V for Vulkan 1.0 and validate it with `spirv-val`; shader source changes rebuild the affected stage. Outputs are under each build's `RavenEngine/Shaders/<config>/` directory. `VulkanShaderModule` loads aligned words with file-size/magic checks and RAII ownership. `VulkanTrianglePipeline` owns the graphics pipeline/layout; the existing frame loop binds it, sets dynamic viewport/scissor, and draws three vertices generated by `SV_VertexID`.

The first triangle is demonstrated in Debug and Release: focused native checks present 230 triangle frames each, exercise two resize dimensions and three minimize/restore cycles, and verify minimized startup plus normal/minimized shutdown. Debug has no core or synchronization validation warnings/errors. Missing/malformed shader files and injected module/layout/pipeline failures verify rejection and rollback; failed pipeline recreation leaves the context able to retry. The running Debug executable is visually confirmed in `out/screenshots/ravenengine-first-triangle.jpg`. Development builds use a CMake-supplied absolute shader output directory; relocatable asset paths remain packaging work.

The triangle now rotates at 90 degrees per second using frame delta. `VulkanContext` preserves a bounded angle across swapchain recreation; each draw supplies an 8-byte vertex-stage push constant containing angle and aspect ratio. HLSL uses explicit offsets `0`/`4` and corrects landscape/portrait proportions. Debug/Release builds and focused checks each present 243 frames, verify timing, wrapping, minimized skip, zero-delta resume, resize phase preservation, and existing cleanup/recovery paths. Debug has no core or synchronization validation warnings/errors. Computer use confirms changing orientation, maximization resizing, and rendering after minimize/restore in the actual Debug executable. Screenshots are under `out/screenshots/ravenengine-rotation-*.jpg` and diagnostics under `out/validation/animation-<config>.log`.

`VulkanBuffer` now owns a buffer and its host-visible coherent allocation with one-time initial upload and rollback on failure. Debug/Release builds and focused checks verify four payload sizes, two GPU copy/readbacks, invalid parameter rejection, and six injected resource/memory-selection failures on the RTX 3070 Ti Laptop GPU. Buffer-before-memory destruction and absence of leftover mappings are verified, with no Debug core or synchronization validation warnings/errors. Results are saved in `out/validation/buffer-<config>.log`. The subsequent indexed triangle milestone is implemented and verified; see [TODO.md](TODO.md) for its evidence and the current working priorities.

On 2026-10-10, the perspective sample replaces the hardcoded triangle draw path with 3D color vertices, shared matrix/camera math, context-owned meshes, and per-object draw submissions. Two cubes animate independently above an XY ground slab; each swapchain framebuffer has a separate depth attachment. Debug/Release builds and SPIR-V validation pass. A temporary CPU probe passes 59 transform/projection checks, and computer use verifies camera controls, depth occlusion, resize, minimize/restore, and normal shutdown. Debug reports no core/synchronization validation warnings/errors. See [the coordinate contract](docs/CoordinateSystem.md) for math/controls and `out/validation/perspective-*.log` and `out/screenshots/ravenengine-perspective.jpg` for evidence. No test sources or framework were added to the project.

The engine/sample build boundary is also implemented: `RavenCore.dll` owns reusable engine code and `RavenEngine.exe` owns the perspective sample. `Application` dispatches drawable frames through an application client interface; caller-owned create/destroy callbacks preserve module allocation ownership. Explicit exports and matching MSVC dynamic runtimes are verified in Debug/Release. A temporary probe passes 17 DLL client checks per configuration, and computer use verifies the running sample with no Debug core/synchronization validation diagnostics. See [the module contract](docs/EngineModules.md) and `out/validation/dll-*.log`. The engine DLL remains loaded for application lifetime; game-code reload is still a separate milestone.

Runtime path/file utilities now replace the absolute build-tree shader lookup with executable-relative `Content/Shaders`, including a startup content-root override. Debug/Release builds, 61 utility checks per configuration, relocated Unicode packages, missing/malformed-file diagnostics, and follow-up buffer/long-file checks pass. See [RuntimeUtilities.md](docs/RuntimeUtilities.md) for the current contract, evidence, and validation limits.

RGBA8 texture ownership/upload is implemented through `VulkanContext::CreateTexture`. Debug/Release verify nine GPU readbacks, 21 injected construction/upload failures, and 36 public-client texture uploads per configuration, with clean Debug core/synchronization validation. The following rendering milestone adds immutable descriptors, separate UV meshes, and opaque HLSL sampling. Both configurations pass 13 public checks with 24 frames and 72 textures, plus 13 GPU pixel checks across nine readbacks and 73 descriptor bindings. UV orientation, tint, SRGB decoding, transforms, and depth occlusion are verified. See [TextureResources.md](docs/TextureResources.md).

Public PNG/JPEG loading now returns owned RGBA8 CPU images through `Images::Load`. The pinned decoder is private to RavenCore and its license is staged beside the application. Debug/Release each pass 109 CPU/public-DLL checks, 19 valid fixture cases, 15 failure cases, concurrent decoding, 39 decoded-image uploads, and eight existing perspective frames, with clean Debug core/synchronization validation. See [ImageLoading.md](docs/ImageLoading.md) for the pixel contract, limits, and evidence. The current sample loads two staged PNGs and displays opaque and transparent textured quads alongside colored geometry.

Straight-alpha transparency, explicit layer/order, automatic camera-depth sorting, and conservative per-instance frustum culling are implemented. Visibility/ordering prepares a borrowed draw queue before Vulkan recording; meshes cache static local bounds. Debug/Release each pass 42 checks including 13 GPU pixel readbacks and six public-entry-point frames. Debug core/synchronization validation is clean. See [TextureResources.md](docs/TextureResources.md) for responsibilities, limits, and validation evidence. Orthographic projection and the 2D sample subsequently pass 38 focused checks and 13 public frame calls per configuration; see [CoordinateSystem.md](docs/CoordinateSystem.md). Adjacent compatible textured draws now use frame-safe instancing while preserving both pass boundaries and ordering. Five uncompressed GPU captures in each configuration match the previous individual-draw renderer byte for byte.

Common UUIDv4 asset identities, typed persistent references, an immutable in-memory catalog, and ID-based texture reuse are now implemented. Windows Debug/Release each pass 3,417 public-client checks and present 25 frames; seven asset headers compile independently. API tracing verifies repeated requests do not allocate additional textures/descriptors and preserves the 257-opaque/4-transparent two-draw batching path. Both native samples remain valid with clean core/synchronization diagnostics. Existing 24 DLL exports are retained with 12 asset exports added. Persistent metadata, FBX sub-assets, dependency/build outputs and runtime handle/reload contracts remain pending; see [AssetSystem.md](docs/AssetSystem.md).

## 0. Project Foundation

- [x] Create the C++20 CMake executable and Windows build presets.
- [x] Initialize Git and ignore local build output and `AGENTS.md`.
- [x] Make the initial Git commit.
- [x] Add repository-local VS Code build and debug configurations.
- [ ] Verify an `F5` session stops at a breakpoint and document the VS Code workflow.
- [ ] Refresh the README and document exact CMake, Ninja, MSVC, and Vulkan SDK versions and setup.
- [x] Separate reusable engine code into the `RavenCore` shared-library target and sample code into `RavenEngine`.
- [ ] Add CTest and automated builds; retain the explicit CTest deferral until revisited with the owner.
- [x] Add basic thread-safe logging and top-level exception reporting.
- [ ] Verify the assertion path and establish a consistent formatting policy.

## 1. Platform Layer

- [x] Move the shared `Window` contract to [`Window.hpp`](RavenEngine/Public/Raven/Platform/Window.hpp) without exposing Win32 types.
- [x] Implement native Win32 window creation, close handling, event polling, and framebuffer size. Verify opening, resizing, and closing a window.
- [x] Add a platform-independent input snapshot with Win32 keyboard transitions, mouse position/delta, mouse buttons, and focus/capture recovery.
- [x] Populate Win32 keyboard events/modifiers and vertical/horizontal wheel state through the shared input snapshot.
- [ ] Finish remaining key translations and define remaining repeat/text-input behavior as required by the samples.
- [x] Define drawable framebuffer state through a shared snapshot of pixel dimensions and persistent resize revision. Verify Win32 resize, minimized `0 x 0`, restore, and close while minimized with Vulkan validation enabled.
- [ ] Complete window state needed by rendering: expose focus state and verify mixed-DPI monitor movement. Per-monitor v2 awareness and DPI-message size handling are implemented in the Win32 backend.
- [x] Add an engine clock with monotonic elapsed time and frame delta. Verify initial/reset delta, elapsed time during pauses, normal timing after resume, and native minimize/restore behavior.
- [ ] Replace the fixed 16 ms sleep with explicit frame pacing.
- [x] Add platform-independent file and path APIs without exposing OS types to `Core/`. Public filesystem-based queries/readers and the Windows executable-path backend are verified; see [RuntimeUtilities.md](docs/RuntimeUtilities.md). Other platform backends remain their own milestone.
- [x] Merge Cocoa implementations of the current window/input/framebuffer and executable-path interfaces, with macOS presets and earlier Debug smoke evidence.
- [x] Merge the Linux X11/XCB window backend, executable paths and Debug/Release presets.
- [ ] Implement and connect the asset system's `Platform::FillRandomBytes` hook on macOS and Linux.
- [ ] Verify both current rendering samples and shared contracts in native Debug/Release on macOS and Linux; merged code and historical smoke checks do not establish current full support.

## 2. Vulkan Bootstrap — implemented baseline

- [x] Create a Vulkan instance with validation and a debug messenger enabled in debug builds.
- [x] Create and destroy a Win32 Vulkan surface through explicit RAII ownership.
- [x] Validate requested instance extensions and report missing Vulkan capabilities clearly. Verify Windows startup/shutdown and the named missing-extension error.
- [x] Select a physical device with graphics and presentation support and required swapchain capabilities.
- [x] Create the logical device and retrieve graphics/presentation queues. Verify queue handles and normal shutdown on Windows.
- [x] Create swapchain images and image views with explicit format, present-mode, and extent selection. Verify creation/destruction on Windows; separate-family presentation remains unverified on hardware.
- [x] Recreate the swapchain when the framebuffer is resized or restored, and defer rendering while minimized. Verify native resize/minimize/restore and injected out-of-date/suboptimal recovery.
- [x] Submit and present a color clear each frame without validation errors; own every Vulkan object with explicit lifetime rules. Verify 407 frames in Debug and Release and resource-construction rollback.
- [x] Confirm the blue clear color visually and record a screenshot of the first rendered frame on Windows.
- [x] Move Vulkan orchestration out of `Application` into a renderer-owned context. Verify Windows Debug/Release startup and shutdown, with device-idle waiting and dependency-ordered destruction before the window is destroyed.

## 3. Rendering Core

- [x] Define the initial renderer frame boundary through `VulkanContext::DrawFrame()` and keep rendering work out of the platform input layer.
- [x] Add the current command submission, frame synchronization, GPU buffer/image ownership, immutable texture descriptors and color/texture pipelines.
- [ ] Add runtime resource handles and a scene-sharing/release policy, including safe GPU retirement before scene switching or resource replacement.
- [x] Compile the initial HLSL vertex/fragment shaders to SPIR-V using DXC through CMake. Validate Debug/Release binaries and incremental source dependencies for Vulkan 1.0.
- [x] Load SPIR-V shader modules and create a graphics pipeline for the first triangle. Verify Debug/Release drawing, resize/minimize/restore, resource-creation rollback, and capture the running Debug executable.
- [x] Animate the triangle using frame delta and vertex-stage push constants. Verify rotation timing, aspect correction, resize phase preservation, and pause/resume with validation and computer use.
- [x] Add a Vulkan buffer owner with compatible memory selection, one-time upload, and partial-construction cleanup. Verify uploaded bytes and GPU copy/readback in Debug/Release with validation.
- [x] Move triangle geometry into vertex/index buffers and submit indexed draws with explicit vertex layout and resource lifetime. Verify Debug/Release indexed drawing and cleanup with validation.
- [x] Add shared model/view/projection math and a perspective camera; validate simple 3D geometry, depth testing, clipping, and resize behavior first.
- [x] Add orthographic projection and render textured 2D sprites with a camera and batching. Orthographic projection, pan/zoom, transparent sprites, frustum selection, and adjacent compatible draw instancing are implemented and verified.
- [ ] Extend the perspective/depth path to imported 3D meshes, materials, and basic lighting.
- [x] Handle viewport resizing, swapchain/depth/pipeline recreation, minimized rendering and tested acquire/present recovery.
- [ ] Review remaining graphics-error/device-loss and presentation-completion behavior, including the device-idle recreation/shutdown approach.

## 4. ECS and Scene Model — current focus

- [ ] Start with entity identity/lifecycle: invalid values, registry ownership, index/generation identity, creation/destruction, slot reuse and stale-reference rejection.
- [ ] Define component ownership/invalidation, storage, queries and safe mutation during updates.
- [ ] Add transforms, camera/sprite components and a minimal scene that generates existing renderer submissions.
- [ ] Drive the orthographic sample from ECS data and create/move/remove sprites at runtime while preserving texture reuse and ordering.
- [ ] Add hierarchy and scene switching after defining resource sharing/release and persistent asset metadata in the next phase of [TODO.md](TODO.md).
- [ ] Serialize/load scenes and explicitly reload them from data files; validate references, failure paths and repeated switching. Automatic source watching belongs to the reload milestone.

## 5. Assets and Reloading

- [x] Define UUIDv4 identities, typed references, runtime paths, the in-memory catalog and ID-based texture reuse.
- [x] Load PNG/JPEG textures and compile/validate/stage HLSL shaders through the existing build path.
- [ ] Persist asset metadata and define source/dependency/build-output records, keeping asset identity separate from runtime resource handles.
- [ ] Import reusable meshes through the planned Blender/FBX path, including sub-asset identity and coordinate conversion.
- [ ] Watch source files and reload shaders and assets at a safe frame boundary.
- [ ] Report import/compile errors while keeping the last valid resource active.

## 6. Game Modules and Code Reloading

- [ ] Define versioned engine/game APIs and decide which side owns persistent state.
- [ ] Load and unload a game module dynamically on each supported OS.
- [ ] Reload changed game code without restarting the host; preserve scene and gameplay state when compatible.
- [ ] Handle failed reloads and incompatible module versions cleanly.

## 7. Gameplay Services

- [x] Expose input snapshots and frame timing to game code through `ApplicationClient::OnFrame`.
- [ ] Expose scene access and runtime resource handles to game code through stable engine APIs.
- [ ] Add audio and 2D/3D physics behind documented engine APIs.
- [ ] Connect input, audio, and physics to ECS systems and verify behavior in sample games.

## 8. Quality and Release

- [ ] Add focused subsystem tests, Vulkan validation runs, performance profiling, and crash diagnostics.
- [ ] Add compiler warnings, static analysis, and sanitizer configurations where each toolchain supports them.
- [ ] Maintain a lightweight manual smoke checklist for native window, input, and Vulkan integration paths.
- [ ] Build and run on Windows, Linux, and macOS in automated checks.
- [ ] Ship a small 2D game and a small 3D game that exercise scenes, assets, and both reload paths.
- [ ] Document setup, architecture, module contracts, asset workflow, and release packaging.

## Next Review Point

Review the entity identity/lifecycle contract and its first small snippet with the owner. After creation/destruction and stale-reference checks work, proceed to component storage, transform/camera/sprite components, and an ECS-backed orthographic sample. Existing rendering and texture reuse are sufficient to start; persistent metadata, FBX import and broader materials follow later.

Before scene switching, define persistent asset/scene data and runtime resource sharing/release. Current mesh/texture references remain renderer-owned until shutdown, and destroying an asset service does not unload them. Preserve that contract until an explicit replacement is implemented and validated.

Keep remaining input work, platform hooks and validation debt visible. Revisit the CTest deferral for entity/component lifetime checks; do not mark absent automated checks complete. Follow the small-snippet, owner-typed implementation workflow recorded in [TODO.md](TODO.md).
