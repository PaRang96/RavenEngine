# Milestone history

This file archives the detailed validation records that `TODO.md` and `ROADMAP.md` held until 2026-11-20, when both were restructured around the engine goals. The text is unchanged except that headings moved down one level and links were adjusted for this folder. It is historical evidence, not current status. Current milestones are in [ROADMAP.md](../ROADMAP.md) and the active work is in [TODO.md](../TODO.md).

Validation artifacts mentioned below live under ignored `out/` folders on the machine that produced them. They are not a tracked regression suite.

## Archived ROADMAP.md


This checklist defines **v1**: a C++23 engine that can build and run small 2D and 3D games on Windows, Linux, and macOS using Vulkan, ECS, and reloadable game code, shaders, and assets. ECS and hot reload are joint design goals; fast iteration means shortening the developer edit/build/observe cycle. Games are authored with code and data files in v1. A complete visual editor remains outside v1, but the architecture must accommodate future editor editing and play sessions with explicit ownership. Check an item only when its behavior is demonstrated in a sample or focused test.

The agreed world convention follows Blender: **right-handed, +X right, +Y reference forward, +Z up, XY ground plane**. Blender is the primary authoring tool, FBX is the primary planned 3D asset format, and a Blender live bridge is a future integration goal. See [the coordinate contract](CoordinateSystem.md) for camera, import, and renderer boundaries and the remaining math decisions.

Rendering targets a **perspective camera first**, with shared model/view/projection math, simple 3D geometry, and depth testing. That initial sample and the following orthographic 2D rendering sample are now implemented using the same world and transform/view conventions. This order supports the Blender/FBX direction; both playable 2D and 3D samples remain v1 goals.

### Current Status — 2026-11-19

Current owner direction: 3D is the primary development view. Upcoming implementation and Windows Debug/Release runtime validation focus on the Perspective/3D path. 2D runtime testing requires an explicit owner request. Existing 2D validation records remain historical evidence.

Planning follows the [shared Windows assessment and camera implementation record](https://chatgpt.com/share/6ae34d47-0d78-83e8-914c-7070fe80712d). The Windows samples now exercise ECS cameras, ground picking, and state-preserving gameplay reload for sprite movement and an opt-in free-fly camera. macOS/Linux remain assigned to other contributors.

RavenEngine has a working Windows rendering and asset foundation. The host owns GameInstance, Registry identities, and copied component values across compatible RavenGame DLL replacements. Source polling starts background builds, and failed builds or incompatible candidates keep the active code. Shared definitions and build-contract changes require a host rebuild and restart. Versioned asset/scene metadata, host-owned Scene/SceneSession services and scoped resource sharing/release now serve both samples. Hierarchical 3D scenes, full affine propagation, binary authored save/load and reusable reloadable interaction gameplay are verified. General ECS queries/systems, broader component/asset serialization and import dependency/build outputs remain open. Perspective and Gallery share retained mesh materials, imported normals and UVs, ambient and directional lighting, and live level and asset edits.

The 2026-10-14 assessment recorded successful Windows Debug/Release builds and zero registered CTest tests. Earlier native/GPU validation records below were reviewed, not rerun in that assessment. Most rendering evidence is from an RTX 3070 Ti Laptop GPU. CTest integration remains deferred at the owner's request; revisit it when defining entity/component lifetime checks. Temporary probes under ignored `out/` do not provide a regression suite from a clean checkout.

Cocoa window/input/framebuffer and executable-path implementations, macOS presets and earlier Debug smoke evidence exist. Linux X11/XCB code, executable paths and presets are merged, with native verification pending. Both platforms still need the random-byte hook introduced by the asset system before current shared-code support can be considered complete. See [PlatformHandoff.md](PlatformHandoff.md).

Transform/StaticMesh drives Perspective geometry imported from Blender FBX files, and Transform/Sprite drives Orthographic textured sprites with one shared quad and cached textures. Both samples use selected ECS cameras. Orthographic restarts and switches between two file-authored levels while retaining its renderer, texture cache, and game module. [TODO.md](#archived-todomd) records the remaining work.

Current Windows Debug/Release build and joint ECS/reload evidence is recorded in [EngineModules.md](EngineModules.md), including repeated replacement, retained component state and build-failure recovery. Retained data and cleanup remain in RavenCore; GameInstance does not require a process-wide singleton. Temporary probes remain distinct from a tracked regression suite.

The component assessment identified and corrected three foundation gaps: Registry operations now accept the abstract Component parent, borrowed/unreviewed field types are rejected, and empty tag components are supported. Component supplies virtual type/size access and synchronous data copying; its macro generates the common implementation. Registry retains names/bytes without retaining polymorphic objects, callbacks or virtual-table pointers. Transform, StaticMesh, Sprite, Camera and Velocity inherit this parent. MeshHandle is declared independently of Vulkan draw submissions. The parent has no rendering/gameplay data or per-frame behavior. General queries, component enumeration/factories, owning fields, property reflection and further mesh/game-DLL integration remain future work. Current evidence and architectural limits are recorded in [EngineModules.md](EngineModules.md).

### Windows milestone status

- [x] **ECS camera rendering and gameplay hot reload.** Windows verifies Orthographic sprite movement and opt-in Perspective free-fly with retained entities, camera selection, and component state. Failed builds and incompatible DLLs preserve active code. See the [validation record](EngineModules.md#windows-gameplay-hot-reload-validation).
- [x] **Playable Windows 2D sample.** Courtyard runs at a fixed simulation rate with a file-authored wall and triggers, win and loss cues, and restart. Debug and Release verify level switching, rejection of invalid levels, and stable resource use. See [level validation](EngineModules.md#file-authored-orthographic-levels) and [Courtyard gameplay](EngineModules.md#courtyard-gameplay).
- [x] **Live Windows content edits for the sample assets.** Courtyard reloads its wall PNG and textured fragment shader. Perspective swaps its mesh bindings and reloads single-object FBX files behind stable handles. Debug and Release verify last-good behavior and bounded GPU lifetimes for the supported replacements. See [wall texture reload](AssetSystem.md#live-courtyard-wall-reload), [shader reload](EngineModules.md#live-courtyard-shader-reload), and [FBX reload](AssetSystem.md#live-perspective-mesh-reload).
- [x] **DLL-controlled Perspective mesh.** Windows Debug and Release verify game-code and FBX replacement in the same session. Hold T in free-fly to rotate the bound cube. Reload preserves the entity, pose, camera selection, and mesh handle. Failed builds, incompatible DLLs, and malformed FBX preserve the last good result and recover on valid input. See [Windows 3D gameplay validation](#archived-todomd).
- [x] **Playable Windows 3D sample.** Perspective and Gallery use shared materials and lighting with a beacon objective, completion audio, restart and live editing. Windows Debug/Release verification is recorded in [mesh materials and lighting](MeshMaterials.md).
- [x] **Cleanup before persistent asset work.** Removed redundant level/sample/watch state and the pointer-only mesh-queue wrapper, kept runtime mesh handles outside authored definitions, corrected the instance-data header name and dependencies, and reused the FBX normal transform. Windows Debug/Release builds, focused state/resource checks and live-content smoke checks pass with unchanged sample pixels. Existing launch options remain supported. See the [cleanup record and validation limits](#archived-todomd).
- [x] **Reusable live scenes and assets.** A versioned project catalog drives asset registration and scene order. Both samples use host-owned scene lifecycle services and scoped resource leases; managed handles reject stale generations and retire native resources safely. Windows Debug/Release exercise data-only asset/scene additions, live edits and recovery, bounded shared/exclusive switching and DLL state retention. See [the workflow and verification record](SceneWorkflow.md).

Each entity has at most one Transform. Mesh, Sprite and Camera are sibling components that use it; an entity with both mesh and camera shares that same pose. CameraComponent retains only projection mode, vertical FOV in radians, full orthographic height and shared clipping distances. Keeping both size controls preserves mode-switch settings. It contains no Transform/position/target/up copy, aspect ratio, active flag, matrices or controller behavior. An unrotated camera looks along local +Y with +Z up, using the existing Transform rotation order; camera views use position/rotation and ignore Scale.

Use one camera-preparation path from Registry copies and the render target's actual drawable viewport to a temporary camera snapshot. Share that snapshot across culling, ordering, drawing and world/screen/ray helpers; keep ECS lookup outside the Vulkan backend. Existing camera APIs may remain one-way compatibility adapters into common math/submission code, while migrated samples retire their authoritative local camera copies. Start with a small host Registry/camera selection binding and the existing full-window viewport, without a view manager or multi-view framework. Selection never transfers entity ownership.

Give controlled entities one designated controller before read-only camera preparation and draw gathering. Perspective mesh animation runs in the update phase. Free-fly uses the same selected camera handle for the synchronous DLL update, drawing and picking.

Follow this foundation with reusable orbit/follow controllers, smoothing, shake and transitions/blending as gameplay requires them. Visibility layers, simultaneous views and render-to-texture follow renderer support; physical lens, exposure, depth of field and jitter follow their consuming systems. The Orthographic sample has level-local ownership for restart and switching. General scene/session ownership follows concrete world-isolation requirements. A full editor, arbitrary C++ migration, generalized plugin loading, broad query optimization and extensive new rendering work remain outside this milestone. Shader/asset runtime reload keeps its separate completion gate.

### Historical milestone evidence

The following records describe each milestone when validated. Later milestones supersede earlier triangle implementations, shader paths, sample ownership and export counts; use the current summary and linked subsystem contracts for present behavior.

The Windows Vulkan bootstrap created a native Win32 window and clear-frame path. A focused native probe presented 407 frames per configuration on an NVIDIA GeForce RTX 3070 Ti Laptop GPU (`graphics=0`, `present=0`) with clean Debug core/synchronization validation. Resize, minimize/restore, shutdown, injected acquire/present recovery and construction rollback passed. The first clear frame is recorded at `out/screenshots/ravenengine-clear-frame.jpg`. Per-monitor v2 DPI awareness and DPI-message handling were implemented; mixed-DPI monitor movement remains unverified.

Resize and shutdown currently wait for device idle. Strict presentation-completion fencing remains a future extension-based improvement; unextended Vulkan has no present fence ([Khronos guidance](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)).

`FrameClock` provides monotonic elapsed seconds and frame delta. The first drawable frame and first frame after restore receive zero delta; total elapsed time includes minimized time. Debug/Release builds, focused clock checks, and native minimize/restore timing pass. Separate 10-second clear-loop samples after a one-second warm-up measure **59.3 FPS in Debug** (593 successful presentations) and **59.7 FPS in Release** (597), with the existing 16 ms sleep and FIFO presentation. Neither sample reports presentation failures or warning/error diagnostics; Debug runs with synchronization validation enabled.

The initial vertex/fragment shaders are written in HLSL and compiled by DXC through the `RavenShaders` build dependency. Both configurations generate SPIR-V for Vulkan 1.0 and validate it with `spirv-val`; shader source changes rebuild the affected stage. Outputs are under each build's `RavenEngine/Shaders/<config>/` directory. `VulkanShaderModule` loads aligned words with file-size/magic checks and RAII ownership. `VulkanTrianglePipeline` owns the graphics pipeline/layout; the existing frame loop binds it, sets dynamic viewport/scissor, and draws three vertices generated by `SV_VertexID`.

The first triangle is demonstrated in Debug and Release: focused native checks present 230 triangle frames each, exercise two resize dimensions and three minimize/restore cycles, and verify minimized startup plus normal/minimized shutdown. Debug has no core or synchronization validation warnings/errors. Missing/malformed shader files and injected module/layout/pipeline failures verify rejection and rollback; failed pipeline recreation leaves the context able to retry. The running Debug executable is visually confirmed in `out/screenshots/ravenengine-first-triangle.jpg`. Development builds use a CMake-supplied absolute shader output directory; relocatable asset paths remain packaging work.

The triangle now rotates at 90 degrees per second using frame delta. `VulkanContext` preserves a bounded angle across swapchain recreation; each draw supplies an 8-byte vertex-stage push constant containing angle and aspect ratio. HLSL uses explicit offsets `0`/`4` and corrects landscape/portrait proportions. Debug/Release builds and focused checks each present 243 frames, verify timing, wrapping, minimized skip, zero-delta resume, resize phase preservation, and existing cleanup/recovery paths. Debug has no core or synchronization validation warnings/errors. Computer use confirms changing orientation, maximization resizing, and rendering after minimize/restore in the actual Debug executable. Screenshots are under `out/screenshots/ravenengine-rotation-*.jpg` and diagnostics under `out/validation/animation-<config>.log`.

`VulkanBuffer` now owns a buffer and its host-visible coherent allocation with one-time initial upload and rollback on failure. Debug/Release builds and focused checks verify four payload sizes, two GPU copy/readbacks, invalid parameter rejection, and six injected resource/memory-selection failures on the RTX 3070 Ti Laptop GPU. Buffer-before-memory destruction and absence of leftover mappings are verified, with no Debug core or synchronization validation warnings/errors. Results are saved in `out/validation/buffer-<config>.log`. The subsequent indexed triangle milestone is implemented and verified; see [TODO.md](#archived-todomd) for its evidence and the current working priorities.

On 2026-10-10, the perspective sample replaces the hardcoded triangle draw path with 3D color vertices, shared matrix/camera math, context-owned meshes, and per-object draw submissions. Two cubes animate independently above an XY ground slab; each swapchain framebuffer has a separate depth attachment. Debug/Release builds and SPIR-V validation pass. A temporary CPU probe passes 59 transform/projection checks, and computer use verifies camera controls, depth occlusion, resize, minimize/restore, and normal shutdown. Debug reports no core/synchronization validation warnings/errors. See [the coordinate contract](CoordinateSystem.md) for math/controls and `out/validation/perspective-*.log` and `out/screenshots/ravenengine-perspective.jpg` for evidence. No test sources or framework were added to the project.

The original engine/sample build boundary put reusable code in RavenCore.dll and the retained client in RavenEngine.exe. Its historical validation includes 17 DLL client checks per configuration and native smoke checks with clean Debug Vulkan diagnostics; see `out/validation/dll-*.log`. The subsequent joint ECS/reload milestone adds a separately replaceable RavenGame.dll while retaining those engine/host lifetimes; see [the current module contract](EngineModules.md).

Runtime path/file utilities now replace the absolute build-tree shader lookup with executable-relative `Content/Shaders`, including a startup content-root override. Debug/Release builds, 61 utility checks per configuration, relocated Unicode packages, missing/malformed-file diagnostics, and follow-up buffer/long-file checks pass. See [RuntimeUtilities.md](RuntimeUtilities.md) for the current contract, evidence, and validation limits.

RGBA8 texture ownership/upload is implemented through `VulkanContext::CreateTexture`. Debug/Release verify nine GPU readbacks, 21 injected construction/upload failures, and 36 public-client texture uploads per configuration, with clean Debug core/synchronization validation. The following rendering milestone adds immutable descriptors, separate UV meshes, and opaque HLSL sampling. Both configurations pass 13 public checks with 24 frames and 72 textures, plus 13 GPU pixel checks across nine readbacks and 73 descriptor bindings. UV orientation, tint, SRGB decoding, transforms, and depth occlusion are verified. See [TextureResources.md](TextureResources.md).

Public PNG/JPEG loading now returns owned RGBA8 CPU images through `Images::Load`. The pinned decoder is private to RavenCore and its license is staged beside the application. Debug/Release each pass 109 CPU/public-DLL checks, 19 valid fixture cases, 15 failure cases, concurrent decoding, 39 decoded-image uploads, and eight existing perspective frames, with clean Debug core/synchronization validation. See [ImageLoading.md](ImageLoading.md) for the pixel contract, limits, and evidence. The current sample loads two staged PNGs and displays opaque and transparent textured quads alongside colored geometry.

Straight-alpha transparency, explicit layer/order, automatic camera-depth sorting, and conservative per-instance frustum culling are implemented. Visibility/ordering prepares a borrowed draw queue before Vulkan recording; meshes cache static local bounds. Debug/Release each pass 42 checks including 13 GPU pixel readbacks and six public-entry-point frames. Debug core/synchronization validation is clean. See [TextureResources.md](TextureResources.md) for responsibilities, limits, and validation evidence. Orthographic projection and the 2D sample subsequently pass 38 focused checks and 13 public frame calls per configuration; see [CoordinateSystem.md](CoordinateSystem.md). Adjacent compatible textured draws now use frame-safe instancing while preserving both pass boundaries and ordering. Five uncompressed GPU captures in each configuration match the previous individual-draw renderer byte for byte.

Common UUIDv4 asset identities, typed persistent references, an immutable in-memory catalog, and ID-based texture reuse are now implemented. Windows Debug/Release each pass 3,417 public-client checks and present 25 frames; seven asset headers compile independently. API tracing verifies repeated requests do not allocate additional textures/descriptors and preserves the 257-opaque/4-transparent two-draw batching path. Both native samples remain valid with clean core/synchronization diagnostics. Existing 24 DLL exports are retained with 12 asset exports added. Persistent metadata, FBX sub-assets, dependency/build outputs and runtime handle/reload contracts remain pending; see [AssetSystem.md](AssetSystem.md).

### 0. Project Foundation

- [x] Create the CMake executable and Windows build presets. The current compiler baseline is C++23.
- [x] Initialize Git and ignore local build output and `AGENTS.md`.
- [x] Make the initial Git commit.
- [x] Add repository-local VS Code build and debug configurations.
- [ ] Verify an `F5` session stops at a breakpoint and document the VS Code workflow.
- [ ] Refresh the README and document exact CMake, Ninja, MSVC, and Vulkan SDK versions and setup.
- [x] Separate reusable engine code into the `RavenCore` shared-library target and sample code into `RavenEngine`.
- [ ] Add CTest and automated builds; retain the explicit CTest deferral until revisited with the owner.
- [x] Add basic thread-safe logging and top-level exception reporting.
- [ ] Verify the assertion path and establish a consistent formatting policy.

### 1. Platform Layer

- [x] Move the shared `Window` contract to [`Window.hpp`](../RavenEngine/Public/Raven/Platform/Window.hpp) without exposing Win32 types.
- [x] Implement native Win32 window creation, close handling, event polling, and framebuffer size. Verify opening, resizing, and closing a window.
- [x] Add a platform-independent input snapshot with Win32 keyboard transitions, mouse position/delta, mouse buttons, and focus/capture recovery.
- [x] Populate Win32 keyboard events/modifiers and vertical/horizontal wheel state through the shared input snapshot.
- [ ] Finish remaining key translations and define remaining repeat/text-input behavior as required by the samples.
- [x] Define drawable framebuffer state through a shared snapshot of pixel dimensions and persistent resize revision. Verify Win32 resize, minimized `0 x 0`, restore, and close while minimized with Vulkan validation enabled.
- [ ] Complete window state needed by rendering: expose focus state and verify mixed-DPI monitor movement. Per-monitor v2 awareness and DPI-message size handling are implemented in the Win32 backend.
- [x] Add an engine clock with monotonic elapsed time and frame delta. Verify initial/reset delta, elapsed time during pauses, normal timing after resume, and native minimize/restore behavior.
- [x] Pace frames against a deadline with a 120 Hz drawable cap and a 30 Hz minimized cap.
- [x] Add platform-independent file and path APIs without exposing OS types to `Core/`. Public filesystem-based queries/readers and the Windows executable-path backend are verified; see [RuntimeUtilities.md](RuntimeUtilities.md). Other platform backends remain their own milestone.
- [x] Merge Cocoa implementations of the current window/input/framebuffer and executable-path interfaces, with macOS presets and earlier Debug smoke evidence.
- [x] Merge the Linux X11/XCB window backend, executable paths and Debug/Release presets.
- [ ] Implement and connect the asset system's `Platform::FillRandomBytes` hook on macOS and Linux.
- [ ] Verify both current rendering samples and shared contracts in native Debug/Release on macOS and Linux; merged code and historical smoke checks do not establish current full support.

### 2. Vulkan Bootstrap — implemented baseline

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

### 3. Rendering Core

- [x] Define the initial renderer frame boundary through `VulkanContext::DrawFrame()` and keep rendering work out of the platform input layer.
- [x] Add the current command submission, frame synchronization, GPU buffer/image ownership, immutable texture descriptors and color/texture pipelines.
- [x] Add renderer-scoped value handles for existing color meshes; shared geometry stays alive until renderer shutdown.
- [ ] Extend runtime resource handles and define a scene-sharing/release policy, including safe GPU retirement before scene switching or resource replacement.
- [x] Compile the initial HLSL vertex/fragment shaders to SPIR-V using DXC through CMake. Validate Debug/Release binaries and incremental source dependencies for Vulkan 1.0.
- [x] Load SPIR-V shader modules and create a graphics pipeline for the first triangle. Verify Debug/Release drawing, resize/minimize/restore, resource-creation rollback, and capture the running Debug executable.
- [x] Animate the triangle using frame delta and vertex-stage push constants. Verify rotation timing, aspect correction, resize phase preservation, and pause/resume with validation and computer use.
- [x] Add a Vulkan buffer owner with compatible memory selection, one-time upload, and partial-construction cleanup. Verify uploaded bytes and GPU copy/readback in Debug/Release with validation.
- [x] Move triangle geometry into vertex/index buffers and submit indexed draws with explicit vertex layout and resource lifetime. Verify Debug/Release indexed drawing and cleanup with validation.
- [x] Add shared model/view/projection math and a perspective camera; validate simple 3D geometry, depth testing, clipping, and resize behavior first.
- [x] Add orthographic projection and render textured 2D sprites with a camera and batching. Orthographic projection, pan/zoom, transparent sprites, frustum selection, and adjacent compatible draw instancing are implemented and verified.
- [x] Render unlit single-object FBX meshes with the Perspective depth path. See [static FBX meshes](AssetSystem.md#static-fbx-meshes).
- [x] Add imported normals, UVs, retained materials, optional textures, ambient and directional light for both authored 3D levels. See [mesh materials](MeshMaterials.md).
- [x] Handle viewport resizing, swapchain/depth/pipeline recreation, minimized rendering and tested acquire/present recovery.
- [ ] Review remaining graphics-error/device-loss and presentation-completion behavior, including the device-idle recreation/shutdown approach.

### 4. ECS and Scene Model

- [x] Implement the entity identity/lifecycle baseline: registry ownership, index/generation identity, creation/destruction, slot reuse and stale-reference rejection. Retaining repeatable lifetime checks remains pending.
- [x] Validate the narrow Windows GameInstance/game-DLL/minimal-component example before expanding the general ECS storage/query design.
- [ ] Extend component ownership/invalidation and safe mutation rules as needed; choose general storage/query behavior against the concrete component-rendering path.
- [x] Add reusable Transform/StaticMesh components and generate Perspective colored-mesh submissions directly from GameInstance/Registry.
- [x] Add SpriteComponent and generate orthographic submissions from Transform/Sprite data using one shared quad and cached textures.
- [x] Exercise runtime sprite creation, movement, fading, texture/blend/order edits and removal while preserving resource reuse and ownership.
- [x] Separate Transform, StaticMesh, Sprite and Camera component headers under Public/Raven/Components. Keep Component, Entity and Registry under ECS.
- [x] Implement CameraComponent projection settings and prepare a CameraView from the entity's Transform and active projection. Use local +Y forward/+Z up and ignore camera scale.
- [x] Add the renderer handoff. The prepared swapchain extent goes to CameraViewBuilder, and existing camera overloads converge on the same draw routine.
- [x] Migrate OrthographicSample to its own selected ECS camera, including component-based pan, zoom and reset and selection failure handling.
- [x] Exercise explicit Orthographic camera switching with F2 and keep entity ownership separate from selection.
- [x] Migrate PerspectiveSample to an ECS camera and make draw gathering read-only. Ground picking uses the same camera matrices and viewport as rendering.
- [ ] Add the remaining world/screen conversions when a concrete caller needs them.
- [x] Migrate sprite movement to direct Transform writes and give the DLL an explicit Perspective camera binding. The host preserves standalone orbit and uses read-only preparation for the DLL-controlled pose.
- [x] Verify Windows free-fly and sprite hot reload, failed build and incompatible DLL recovery, and record edit-to-render times.
- [x] Give the game DLL control of one imported mesh Transform through an explicit entity binding.
- [x] Load, restart, and switch two file-authored Orthographic levels with sample-owned entities. Verify rejected candidates preserve the active level.
- [x] Provide reusable host-owned scene entities, selection/bindings, data-driven switching, persistent metadata and scoped resource retirement for both existing codecs. See [SceneWorkflow.md](SceneWorkflow.md).
- [x] Add hierarchical 3D scene authoring, stable entity IDs, full affine world transforms and explicit validated/atomic save/load with flat-level compatibility.
- [x] Automatically watch active scene files and project metadata during development, with transactional publication and last-good recovery in both samples.
- [ ] Add general scene serialization beyond the existing supported codecs.

### 5. Assets and Reloading

- [x] Define UUIDv4 identities, typed references, runtime paths, the in-memory catalog and ID-based texture reuse.
- [x] Load PNG/JPEG textures and compile/validate/stage HLSL shaders through the existing build path.
- [x] Replace Courtyard's wall PNG during play with last-good recovery and bounded GPU use. See [the Windows workflow and timings](AssetSystem.md#live-courtyard-wall-reload).
- [x] Persist UUID/type/path/texture settings and scene order, keeping authored identity separate from generation-checked runtime handles and lease ownership.
- [ ] Extend metadata with import dependencies and build-output records for additional asset/shader pipelines.
- [x] Import single-object static FBX meshes from Blender into the Perspective sample, with coordinate and unit conversion. See [static FBX meshes](AssetSystem.md#static-fbx-meshes).
- [x] Switch mesh sources on running Perspective entities and verify live FBX replacement behind stable handles on Windows. See [live Perspective mesh reload](AssetSystem.md#live-perspective-mesh-reload).
- [ ] Extend FBX import to multi-object files with stable sub-asset identities.
- [x] Reload Courtyard's textured fragment shader while playing on Windows. Replace both textured passes together, retain the last good shader through failed edits, and verify GPU lifetime. See [live shader reload](EngineModules.md#live-courtyard-shader-reload).
- [ ] Extend source watching and frame-boundary replacement to other shaders and assets.
- [ ] Extend last-good recovery and diagnostics to each newly supported import and compile path.

### 6. Game Modules and Code Reloading

The Windows ECS and reload baseline covers sprite movement, free-fly camera control, and an explicitly bound imported mesh.

- [x] Establish the minimal retained-state/update contract under the owner's direct-edit authorization. Reuse application/client/export definitions and separate host management from reloadable code.
- [x] Demonstrate Windows DLL replacement with preserved entity/component data, then automate successful stable builds through source polling.
- [x] Generate conservative compatibility checks for the supported dependency set; reject unsupported changes until the host is rebuilt/restarted, without manual layout counters.
- [x] Preserve directly controlled sprite Transform values and camera pose, lens, and selection across compatible Windows replacements. Verify that failed builds and incompatible DLLs keep the active code.
- [x] Verify compatible DLL replacement and rejection recovery for the controlled mesh in Windows Debug and Release.
- [ ] Implement and verify native game-module load, unload, and replacement on macOS and Linux.
- [ ] Extend state preservation to broader scene/gameplay ownership when those contracts exist; arbitrary C++ migration remains outside the baseline.

### 7. Gameplay Services

- [x] Complete Windows Courtyard with fixed 60 Hz gameplay, a file-authored wall and triggers, a full win and loss, and F3 restart. Compatible DLL reload retains live state. See [Courtyard gameplay](EngineModules.md#courtyard-gameplay).
- [x] Expose frame input and timing and buffer keyboard transitions for fixed gameplay.
- [x] Resolve Courtyard movement with shared swept XZ collision and play outcome cues through cached PCM WAV one-shots on Windows.
- [x] Verify reloadable Perspective free-fly with frame-rate-independent movement, mouse look, and retained camera pose and FOV on Windows.
- [ ] Add reusable orbit/follow controllers, smoothing, shake and transitions/blending as later gameplay needs them; keep behavior outside CameraComponent.
- [ ] Expose scene access and runtime resource handles to game code through stable engine APIs.
- [ ] Add native audio playback on the other supported platforms.

### 8. Quality and Release

- [ ] Add focused subsystem tests, Vulkan validation runs, performance profiling, and crash diagnostics.
- [ ] Add compiler warnings, static analysis, and sanitizer configurations where each toolchain supports them.
- [ ] Maintain a lightweight manual smoke checklist for native window, input, and Vulkan integration paths.
- [ ] Make focused Windows camera/reload checks rerunnable with recorded commands, including lifetime, state preservation, failed replacement, resource stability and shutdown; keep CTest deferred unless separately agreed.
- [x] Record Windows edit-to-visible-change measurements for the supported gameplay, wall PNG, textured fragment shader, and FBX reload paths. Document when a host rebuild and restart are required.
- [ ] Extend timing and recovery measurements as reload coverage expands.
- [ ] Build and run on Windows, Linux, and macOS in automated checks.
- [ ] Ship a small 2D game and a small 3D game that exercise scenes, assets, and game-code, shader, and asset reload.
- [ ] Document setup, architecture, module contracts, asset workflow, and release packaging.

### Deferred collision and physics

General-purpose 2D collision, 3D collision, and physics simulation are deferred with no scheduled milestone. Courtyard's swept XZ wall checks and trigger overlaps cover its current gameplay. The 2D and 3D sample milestones and the v1 release can proceed with the existing services. Revisit the scope when a concrete gameplay mechanic needs additional collision or simulation behavior.

### Hierarchical 3D milestone — 2026-11-19

- [x] Scene-owned hierarchy/identities, local-preserving reparent/detach, cascade destruction and invalid-parent rejection.
- [x] Shared full world matrices for 3D rendering, normals, bounds, camera position and picking; explicit rigid camera scale/reflection policy.
- [x] Version 3 authored writer/read round trips, validated atomic save, version 1/2 compatibility and live save recovery without F3.
- [x] Shared-asset Assemblies demonstration with DLL/host-controlled arms and attached descendants.
- [x] Windows Debug/Release 3D native and stock checks, Vulkan validation, reload/failure recovery, window lifecycle and bounded resource retirement. Temporary inputs restored; existing workspace edits preserved.

See [passed checks, gaps and evidence](#archived-todomd) and [format/save policy](SceneWorkflow.md#hierarchical-3d-authoring). CTest, tracked regression coverage, native macOS/Linux execution, general component serialization, multi-camera authoring, editor UI and prefabs remain open. No 2D runtime validation was performed for this milestone.

```powershell
# Debug
Set-Location -LiteralPath 'D:\RavenEngine'
& 'D:\RavenEngine\out\build\x64-debug\RavenEngine\RavenEngine.exe' --watch-game --free-fly --watch-mesh

# Release
Set-Location -LiteralPath 'D:\RavenEngine'
& 'D:\RavenEngine\out\build\x64-release\RavenEngine\RavenEngine.exe' --watch-game --free-fly --watch-mesh

# Game-only builds while running; do not rebuild the loaded host
cmake -P 'D:\RavenEngine\out\build\x64-debug\RavenEngine\GameBuild\BuildGame.cmake'
cmake -P 'D:\RavenEngine\out\build\x64-release\RavenEngine\GameBuild\BuildGame.cmake'
```

### Completed Windows milestone: reusable reloadable 3D gameplay

- [x] Borrowed SceneAccess with authored-ID lookup, scoped component queries and guarded copied-value read/write for DLL gameplay.
- [x] Reusable switches, rotators, doors, goals and retained attempt state, with gameplay rules/animation in the DLL and ownership/rendering in the host.
- [x] Closest exact 3D mesh picking with occlusion, full affine models and presented-camera depth limits.
- [x] A complete two-switch assembly/door puzzle with goal, one completion notification and restart, available as the fourth F4 scene.
- [x] Machine-readable versioned binary scenes, explicit bounded field encoding, validated atomic saves, migrated existing 3D content and a usable save/export tool/API.
- [x] Automatic development level reload without F3; compatible DLL/asset edits retain progress, invalid/superseded edits retain the active result, and valid edits recover.
- [x] Windows Debug/Release native and visible 3D checks, reload/failure recovery, resource/window lifecycle, restored inputs and preserved workspace edits.

See [current workflow, binary schema, complete commands, timings and checks](InteractionGame.md) and [the detailed milestone record](#archived-todomd). Local ignored probes are not a tracked regression suite. CTest, native macOS/Linux execution, full physics, editor UI, general component serialization and additional rendering features remain deferred. No 2D runtime tests were run.

### Next Review Point

Puzzle and Collection run through the same Windows 3D host with scoped gameplay and retained reload state. The [default development launch](DevelopmentReload.md) includes lit shader, game code, scene/project, PNG and FBX reload. The [Windows x64 player build command](PlayerBuild.md) ships selected scenes and required assets in a relocatable folder, with platform prerequisites documented. Verify the folder on a separate clean Windows machine and continue native platform verification in parallel.

Keep files cohesive by responsibility and lifetime. Reuse existing files for related small definitions, while separating retained host/module management from reloadable game implementation and native services. Preserve renderer/cache initialization across code replacement; repeatedly recreating sample clients can accumulate renderer-owned resources.

The file-driven scene path now supports shared and exclusive resource sets. Managed handles expire on final lease release, while submitted frames retain native data until their fences complete. Legacy borrowed-reference creation remains pinned to renderer lifetime. Keep these contracts explicit as scene schemas and game services expand.

Keep remaining input work, platform hooks and validation debt visible. Revisit the CTest deferral for entity/component lifetime checks; do not mark absent automated checks complete. Suggest small, reviewable engine snippets for the owner to apply, as recorded in [TODO.md](#archived-todomd). Edit engine files only when the owner explicitly requests that specific change.

## Archived TODO.md


The goal follows [ROADMAP.md](#archived-roadmapmd): a C++23 engine that can build and run small 2D and 3D games using Vulkan, ECS, and reloadable game code, shaders, and assets. ECS and hot reload are joint design goals. Fast iteration means reducing the time from a developer's edit to observing the result. Games are authored through code and data files in v1; a complete visual editor remains outside v1, but ownership and execution boundaries must accommodate future editor editing and play sessions. This is the working implementation order; the roadmap summarizes the wider scope. Write and maintain project documentation in English.

Work on Windows and shared engine code first. macOS and Linux contributors can complete their backends and native verification in parallel. Final platform completion must exercise the game and reload behavior once it exists. Keep shared contracts independent of operating-system headers.

Perspective colored meshes and Orthographic sprites render from GameInstance/Registry with shared resources. Both samples select ECS cameras and prepare their views at the drawable swapchain extent. Perspective supports ground picking, standalone orbit and opt-in reloadable free-fly. The game DLL moves the Orthographic sprite through its existing Transform. The 2D sample loads, restarts, and switches two file-authored levels. Perspective loads Blender-authored FBX meshes and can switch or reload their sources during play.

Engine implementation uses small, reviewable snippets suggested by the assistant and applied by the owner. As clarified on 2026-11-01, the earlier general direct-edit authorization no longer applies to engine implementation: edit engine files only when the owner explicitly requests that specific change. Explicit CMake requests and the requested camera rollback do not authorize other engine edits. Check an item only after its completion criteria are demonstrated.

### Current state as of 2026-11-19

Current owner direction: 3D is the primary development view. Upcoming implementation and Windows Debug/Release runtime validation focus on the Perspective/3D path. 2D runtime testing requires an explicit owner request. Existing 2D validation records remain historical evidence.

The Windows host retains GameInstance, Registry identities, and copied component values while replacing compatible RavenGame DLL code. The verified gameplay examples are Orthographic sprite movement and Perspective free-fly. Build and compatibility failures keep the active DLL. Shared definitions and build-contract changes require a host rebuild and restart. See the [Windows gameplay reload validation](EngineModules.md#windows-gameplay-hot-reload-validation).

Courtyard meets the Windows playable 2D sample milestone with fixed-step movement, a file-authored wall and triggers, win and loss audio, and restart. Its wall PNG and textured fragment shader can reload during play. Perspective imports normals and UVs from single-object Blender FBX meshes and renders lit materials, switches their bindings with F6, and hot-reloads geometry behind stable mesh handles. See [Courtyard gameplay](EngineModules.md#courtyard-gameplay), [wall texture reload](AssetSystem.md#live-courtyard-wall-reload), [shader reload](EngineModules.md#live-courtyard-shader-reload), and [FBX reload](AssetSystem.md#live-perspective-mesh-reload).

The [playable Windows 3D milestone](#next-windows-milestone-playable-3d-sample) now has a verified DLL-controlled imported mesh. Two authored levels now exercise shared mesh materials, ambient and directional lighting, live editing, beacon completion and restart. Versioned project metadata, reusable host-owned scenes, scoped asset sharing/release, and live editing now serve both samples. Hierarchical 3D scenes, binary authored save/load and reusable DLL-driven 3D interaction gameplay are now verified. General component/asset serialization, import dependencies and broader ECS queries remain open. See [the current workflow](SceneWorkflow.md). macOS/Linux contributors can continue native backend work in parallel. The Windows Debug/Release evidence comes from the documented sample runs and focused local probes. CTest remains deferred, and ignored `out/validation/` artifacts are local evidence.

Earlier camera direction is recorded in the [shared Windows assessment](https://chatgpt.com/share/6ae34d47-0d78-83e8-914c-7070fe80712d).

The indexed triangle milestone is implemented and verified. Debug and Release each passed 243 indexed draws and presentations, including resize, minimize/restore, animation, and construction-failure cleanup. Debug reported no core or synchronization validation warnings/errors. Evidence: `out/validation/indexed-debug.log`, `indexed-release.log`, and `out/screenshots/ravenengine-indexed-triangle.jpg`.

The input baseline already includes keyboard transitions/events, modifiers, mouse buttons/movement, and vertical/horizontal wheel handling. Finish the remaining behavior when needed by samples; do not repeat completed input work.

Perspective validation: Debug/Release builds and shader validation pass; a temporary probe passes 59 math/projection checks. Computer use confirms animation, horizontal/vertical camera orbit, wheel zoom, Home reset, maximized/portrait proportions, minimize/restore, and normal shutdown. Debug has no core/synchronization validation diagnostics; Release also launches from `out/`. Evidence: `out/validation/perspective-*.log` and `out/screenshots/ravenengine-perspective.jpg`. No test sources/framework were added; CTest wiring remains pending.

Later rendering and asset validation is recorded in [TextureResources.md](TextureResources.md), [CoordinateSystem.md](CoordinateSystem.md), and [AssetSystem.md](AssetSystem.md). The asset milestone records 3,417 public-client checks and 25 frames per Windows configuration; batching records matching GPU captures and preserved ordering. Those records do not establish current macOS/Linux support.

### Joint ECS and game-code reload baseline

Status: implemented for the narrow Windows development example. See [EngineModules.md](EngineModules.md) for exact supported contracts, commands, evidence and limits.

Component.hpp supplies the abstract Component parent, virtual type/size access and synchronous field copying through `RAVEN_COMPONENT(FullyQualifiedType, Fields...);`. Registry accepts Component references and retains only approved copied values, excluding virtual-table pointers and borrowed views. Custom value structs require an explicit ComponentValue declaration; empty/tag components are supported. Transform, StaticMesh, Sprite, Camera and Velocity inherit this parent without handwritten name strings or payload wrappers. OrthographicSample owns the active level's Transform/Sprite entities and two ECS cameras. F1 edits Sprite ordering in Registry and F2 changes the selected camera. PerspectiveSample owns three Transform/StaticMesh entities and one ECS camera. General scenes and queries remain pending.

1. [x] Retain host-owned GameInstance/Registry data across game-code replacement, without a singleton. Scene ownership is a later extension.
2. [x] Build, load, validate and replace a separate Windows game DLL using execution copies while keeping RavenCore, Registry and Entity handles alive. Replacement occurs between synchronous game calls.
3. [x] Connect minimal component add/read/write/remove and destruction cleanup. Repeated replacements preserve values while changed code updates them. General query/storage design remains open.
4. [x] Add source-content polling and background builds for the supported sample dependencies. Apply successful stable builds; retain active code on build/contract failure. Generated contracts require a host rebuild/restart for shared-definition/build changes, without manual layout counters. Extending the dependency set requires extending these checks.

First end-to-end completion: replace gameplay code during one running session, preserve Registry/Entity identity and existing component values, observe the new update behavior, and keep the previous game active on build or pre-activation validation failure. A DLL with an exported empty function alone does not satisfy this criterion. Step 4 then automates that demonstrated behavior.

Initial state preservation covers compatible logic edits. Generated shared-definition/build fingerprints conservatively reject unsupported contracts until a host rebuild/restart; there is no automatic state reset or C++ layout migration. Watcher events, source extensions and equal `sizeof` are not compatibility proofs. New code can still contain execution errors, so retaining old code on a failed build does not guarantee recovery from a native crash after activation.

Reuse the existing application loop, host-side client, public input/frame data, renderer, and asset cache. Keep persistent storage and its cleanup independent of unloaded game-module code. Preserve renderer/cache initialization across code swaps instead of recreating sample clients and GPU resources for every reload. Extend existing files where responsibilities remain cohesive; separate host module management from reloadable game implementation and put native loading/watching behind platform backends. Balance responsibilities and lifetimes rather than targeting a particular file count.

Verification after removing the sample-only component layer is complete: Debug/Release builds, 137 component/reload checks, 50 frames of each sample and 100 changed-DLL rendering frames pass per configuration. See [EngineModules.md](EngineModules.md) for evidence and coverage limits.

Current component milestone: the game DLL writes the moving sprite's Transform directly. Both samples render from ECS cameras, and the opt-in Perspective camera accepts reloadable movement and lens input. See [EngineModules.md](EngineModules.md#windows-gameplay-hot-reload-validation) for retained state, resource and reload checks. Scene ownership is deferred.

### Verified foundation

- [x] Windows C++23 Debug/Release builds, Git, and development configurations.
- [x] Basic logging and top-level exception reporting.
- [x] Native Win32 window/event loop and shared input/framebuffer contracts.
- [x] Monotonic elapsed time and frame delta, with zero delta on initial/resumed drawable frames.
- [x] Vulkan instance, debug messenger, surface, physical/logical device, and queues with explicit ownership.
- [x] Swapchain, frame resources, submission/presentation, and resize/minimize/restore recovery.
- [x] HLSL compilation through DXC, SPIR-V validation/loading, and initial graphics pipeline.
- [x] Frame-delta triangle rotation and aspect correction through push constants.
- [x] Uploaded Vulkan buffer ownership and persistent vertex/index buffers used by indexed draws.

These checks establish the tested Windows baseline on the RTX 3070 Ti Laptop GPU. They do not establish full renderer completion or other-platform support.

### 1. Reusable geometry, transforms, and perspective camera — implemented baseline

- [x] Agree and document world axes: right-handed, +X right, +Y reference forward, +Z up, XY ground plane; see [CoordinateSystem.md](CoordinateSystem.md). Shared math and both camera types now implement this convention.
- [x] Define matrix layout/multiplication order and CPU/HLSL agreement using the agreed world axes.
- [x] Add model/view/projection transforms and a perspective camera; define field of view and near/far planes, and update aspect ratio through framebuffer resize.
- [x] Add depth resources/testing and recreate size-dependent attachments correctly for the initial 3D sample.
- [x] Introduce reusable geometry ownership and object draw submissions with explicit GPU lifetime rules. Context-owned meshes remain alive until shutdown after GPU completion.
- [x] Move sample geometry and animation out of `VulkanContext` into sample code.
- [x] Draw simple 3D geometry with independent object position, rotation, and scale through one perspective camera.
- [x] Separate reusable engine code and the sample executable into CMake targets as this boundary becomes concrete. `RavenCore.dll` owns the engine; `RavenEngine.exe` owns the sample through create/frame/destroy callbacks. Debug/Release builds, 17 temporary DLL client checks per configuration, and native smoke checks pass; see [EngineModules.md](EngineModules.md).
- [ ] Add CTest wiring and focused transform/projection tests; make the current rendering smoke checks repeatable. Deferred at the owner's request; revisit at the entity/component lifetime milestone without marking this work complete.

Completion: an input-controlled perspective camera and at least two independently transformed 3D objects render in Debug/Release with correct depth occlusion and perspective scaling. Verify near/far clipping and aspect ratio, resize/minimize/restore, and shutdown without validation diagnostics. The renderer context no longer owns the sample's animation or geometry choices.

### 2. Asset loading and textured 2D rendering — implemented baseline

- [x] Add orthographic projection for 2D using the existing transform/view math; define the visible region and resize behavior. Fixed vertical world extent, aspect-dependent width, camera pan/zoom, depth-invariant sprite size, transparency and frustum selection are verified in Debug/Release; see [CoordinateSystem.md](CoordinateSystem.md).
- [x] Define asset roots, identifiers, and file/path access; use relocatable runtime paths instead of absolute build-tree shader paths. Runtime paths, UUIDv4 identifiers, typed asset references, an immutable in-memory catalog and ID-based texture reuse are implemented and verified in Debug/Release. See [RuntimeUtilities.md](RuntimeUtilities.md) and [AssetSystem.md](AssetSystem.md).
- [x] Load PNG/JPEG images into owned RGBA8 pixels with file/decode/size diagnostics, a pinned private decoder, and staged license; see [ImageLoading.md](ImageLoading.md).
- [x] Compile HLSL into validated SPIR-V through CMake and stage runtime shader outputs. This is build-time shader processing; source watching and runtime replacement remain in milestone 6.
- [x] Add image, image-view, and sampler ownership, texture uploads, and required layout/synchronization handling. Initial single-mip RGBA8 Linear/SRGB textures pass Debug/Release GPU readback, failure cleanup, and public-client checks; see [TextureResources.md](TextureResources.md). Initial descriptors and sampling are also implemented.
- [x] Own immutable texture descriptors and update current draw transforms/tints safely. Mesh and sprite instances update per-frame buffers after fence completion. Mesh materials and scene lighting are covered in milestone 5.
- [x] Render textured quads with tint, transparency, camera transforms, and explicit draw ordering. Perspective/orthographic opaque/straight-alpha quads use signed layer/order, automatic camera-depth sorting, and per-instance frustum selection; see [TextureResources.md](TextureResources.md) and [CoordinateSystem.md](CoordinateSystem.md).
- [x] Batch compatible sprites while preserving ordering and separating incompatible textures/materials. Adjacent textured submissions sharing a mesh/texture are instanced within separate opaque/transparent passes; per-frame buffers retain capacity and update after fence completion. Debug/Release GPU captures match individual draws exactly; see [TextureResources.md](TextureResources.md).

Completion: render the orthographic sample using file-loaded textures, overlapping transparent sprites and multiple textures; verify different working directories, resize and renderer shutdown. This rendering baseline is implemented. Orthographic entities are authored in level files. The sample texture catalog is registered in C++, with persistent asset metadata tracked in 3.2.

### 3. ECS and scenes

#### 3.1 Minimal ECS and component-driven rendering

- [x] Implement the entity identity/lifecycle baseline: registry ownership, index/generation handles, creation, destruction, liveness checks, and slot reuse. Existing temporary checks are not a tracked regression suite.
- [x] Complete the narrow Windows joint ECS/game-module reload example before expanding the general component storage/query design.
- [x] Define initial component ownership and destruction cleanup: engine-owned copied values; no borrowed component pointers escape. Extend this contract before introducing reference-returning or owning component APIs.
- [x] Define an abstract Component parent with virtual destruction/type access; derive all existing components from it and use `RAVEN_COMPONENT(FullyQualifiedType, Fields...);` for generated names and field-only storage.
- [x] Make the parent usable through Registry add/read/write/remove, reject borrowed/unreviewed fields, support empty tags and separate MeshHandle from Vulkan draw submissions. See the component assessment and verification in EngineModules.md.
- [x] Add reusable Transform/StaticMesh components with value mesh handles and direct per-entity transform/tint fields.
- [x] Generate Perspective colored-mesh submissions directly from GameInstance/Registry, sharing geometry and handling runtime additions/removals and invalid handles in focused GPU checks.
- [x] Add SpriteComponent with a typed texture reference, tint, opaque/alpha selection and transparent layer/order fields.
- [x] Generate sprite submissions from Transform/Sprite data while preserving one shared quad and cached textures.
- [x] Drive the orthographic sample from ECS data and demonstrate runtime creation, movement, fading, texture/blend/order changes and removal.
- [x] Place Transform, StaticMesh, Sprite, and Camera under Public/Raven/Components. Keep Component, Entity, and Registry under ECS.
- [x] Render both samples from selected ECS cameras with shared view preparation, direct game-module sprite Transform writes, and opt-in reloadable Perspective free-fly. See the [camera implementation and validation](#ecs-camera-and-gameplay-integration).
- [x] Give the game DLL direct control of one explicitly bound Perspective mesh Transform. See the [Windows 3D gameplay validation](#next-windows-milestone-playable-3d-sample).
- [ ] Define safe entity/component changes during updates using the existing snapshot/copy API; expand storage/queries only when the concrete rendering path requires it.
- [ ] Validate entity/component lifetime and iteration behavior with focused checks; revisit the CTest deferral before deciding how these checks are retained.
- [ ] Add a minimal scene owner when grouping and lifetime requirements justify it. Deferred; it does not block component integration.

Completion of the component/rendering work: both samples render through explicitly selected Transform/CameraComponent entities, support perspective and orthographic camera settings, and handle invalid/stale selection as defined below. Reloadable game code directly controls transforms and camera settings for the mesh/sprite paths, including a useful perspective controller. Repeated compatible replacement preserves entity identity, component values and camera selection; failed builds/rejected replacements retain active code. Preserve sprite ordering, one shared quad, shared meshes and the existing catalog/cache across updates and reloads. Scene ownership remains deferred. File-based scene authoring, hierarchy, scene switching and resource release follow in 3.2.

##### StaticMesh and Sprite rendering

TransformComponent and StaticMeshComponent live under `Public/Raven/Components/`. PerspectiveSample owns three mesh entities, shares the imported cube between the cube and floor at startup, and assembles draw submissions from Registry copies. MeshHandle identifies its renderer and unrecycled slot. Geometry replacement keeps that handle and retires superseded buffers after the frames that reference them complete. Imported normals and UVs now feed the shared material and lighting path. See [static FBX import and replacement](AssetSystem.md#static-fbx-meshes).

The component foundation uses a virtual parent with generated type/size/copy boilerplate and an explicit retained-value policy. SpriteComponent extends that pattern in its own `Public/Raven/Components/SpriteComponent.hpp` header, alongside SpriteBlendMode. OrthographicSample owns the active level's entities and gathers Transform/Sprite pairs using its retained quad, catalog, and cache. Transform provides pose/scale; Sprite provides appearance and transparent ordering. Invalid/null texture references and unknown blend modes are skipped; non-null unregistered references and load failures use the asset service's exception path. Removing a component/entity removes its draw. Cleanup and constructor rollback affect only the sample's own entity handles, including after slot reuse.

##### ECS camera and gameplay integration

Both samples use ECS cameras. Perspective ground picking and opt-in free-fly run on Windows. Remaining screen conversion helpers follow a concrete caller. DLL control of a mesh is tracked in the [3D milestone](#next-windows-milestone-playable-3d-sample).

The [Windows reload validation](EngineModules.md#windows-gameplay-hot-reload-validation) records the tested behavior and the build/restart boundary.

Responsibility and lifetime contract, refined after the duplication review:

| Data | Authoritative source | Use |
| --- | --- | --- |
| Entity position, rotation and scale | One TransformComponent on the entity | Mesh/sprite models; camera pose uses position/rotation |
| Projection mode, vertical FOV, orthographic height, near/far | CameraComponent on the same camera entity | Camera preparation |
| Selected camera | Small host view binding: Registry plus Entity | View selection and explicit routing to its controller |
| Drawable viewport | Prepared render target's actual extent | Projection, screen helpers and rendering |
| View/projection matrices | Temporary camera snapshot prepared from the above | Culling, transparent ordering, drawing and projection/ray helpers |

A mesh entity has Transform + StaticMesh; a sprite entity has Transform + Sprite; a camera entity has Transform + Camera. An entity with both mesh and camera still has one Transform used by both. These are sibling components deriving from the existing Component parent; mesh/sprite/camera components neither contain nor inherit another Transform. Separate entities have independent Transform values. Registry already rejects adding the same component type twice to one entity; temporary Read copies and derived draw matrices are snapshots rather than additional authored state.

- Keep CameraComponent as one simple projection-settings component with a mode enum, two separate size controls and shared clipping distances. FOV and orthographic height describe different projections; retaining both preserves settings when switching modes. Validate/use the active mode's size control. Do not add position, target/up, Transform copies, aspect ratio, derived width/matrices, active flags, controller behavior or Vulkan resources to it.
- Define an unrotated camera to look along local +Y with local +Z up. Use Transform's existing radians and Rz * Ry * Rx rotation order to rotate those basis directions. Build the view from position/rotation only and ignore camera Scale; do not invert the full scaled model matrix. Camera scale must not change FOV, orthographic height, handedness or view validity. Imported-camera basis conversion and future parent transforms remain separate contracts.
- Resolve Registry copies through one shared camera-preparation path into a temporary immutable snapshot. Reuse existing projection functions and the same snapshot for culling, depth ordering, drawing and world/screen/ray helpers. Keep the Vulkan backend independent of Registry/Entity lookup. Existing PerspectiveCamera/OrthographicCamera APIs may remain as one-way compatibility adapters into the common math/submission path; migrated samples stop retaining authoritative local camera copies.
- Use the actual drawable extent after render-target preparation, matching the renderer's current swapchain-based aspect calculation. The host view binding selects a Registry/camera and borrows that frame's viewport; it does not own the Registry or cache a second window size. If the target extent changes, prepare a matching camera snapshot before culling/submission. Start with the current full-window view and a small binding/value handoff, without a view manager, multi-view renderer or render graph.
- Give each controlled entity one designated controller. The DLL writes the bound free-fly camera before the host prepares the view. Standalone Perspective applies orbit input. Mesh animation runs before read-only draw gathering.
- Selection does not transfer ownership. Samples clean up only their own entities and roll them back on initialization failure. Route input to an explicit camera entity through a narrow host binding available to the synchronous game update; reuse the selected handle when controlling that view. A controller may deliberately target another camera, but must not invent a second active-camera flag, scan for the first camera, or retain module-static selection/pose.
- Use the existing Component parent, reviewed ComponentValue fields and RAVEN_COMPONENT macro for every retained field. Reuse GetEntities snapshots, Read copies and checked writes; add only access/iteration needed by this example. Keep derived caches temporary and introduce persistent matrix caching only when measurement justifies it.
- Define recoverable view failure: a missing Transform/Camera component, null/foreign/stale selected entity or invalid active camera settings/pose skips that view's world rendering and reports a useful diagnostic. Do not select an arbitrary entity or a recycled slot. A zero-sized/minimized viewport skips camera projection and world rendering until drawable again.
- Initialize entities/default settings separately from game updates. Compatible replacement preserves values unless gameplay deliberately changes them. Adding/changing shared camera or update-binding definitions requires a host rebuild/restart through the generated compatibility contract; routine compatible game-code edits then use runtime reload without manual layout counters.

1. [x] **Confirm and reuse the camera math contract.** Reuse the existing right-handed Z-up world, column-vector math, Vulkan depth [0, 1] and positive-height viewport Y convention. Preserve the existing projection rules: finite values, perspective 0 < vertical FOV < pi and 0 < near < far; orthographic height > 0 and 0 <= near < far. The Transform-to-camera view conversion uses the basis and scale rules above and shares the existing projection functions.

   Completion: known points, translation/rotation, clipping and portrait/landscape aspect work in both modes; camera scale does not change the view, including zero/negative scale. Invalid pose/active projection values and degenerate inputs follow documented behavior.

2. [x] **Add CameraComponent projection settings.** The component registers its mode, both size controls and shared clipping distances through RAVEN_COMPONENT. `GetCameraView` combines the active projection with the sibling Transform pose. The renderer receives the resulting matrices as a temporary CameraView.

   Verified on Windows Debug: Registry Add/Read/Write retains both size controls across a mode switch. A mesh and camera share one Transform, and duplicate Transform addition is rejected. Both projections map near/far to Vulkan depth 0/1, and invalid active settings, invalid poses and unknown modes are rejected. See the [current camera evidence](EngineModules.md#camera-pose-projection-and-renderer-handoff).

3. [x] **Connect a selected ECS camera to rendering.** Resolve the selected entity through its Registry and prepare the camera from the actual drawable viewport.

   - [x] Route both existing camera types and the public CameraViewBuilder through `VulkanContext::Impl::DrawFrameWithView`. The renderer supplies the prepared swapchain extent and computes `Projection * View` once. An empty view skips drawing. A temporary public client submitted a Registry-backed view through this path.
   - [x] **Migrate OrthographicSample.** The selected camera handle resolves to TransformComponent and CameraComponent for input and view preparation. Pan, zoom and Home write the selected entity. Invalid or missing camera values skip drawing with a diagnostic. F1 still changes transparent ordering.
   - [x] Exercise explicit selection between two Orthographic cameras. F2 changes selection independently of ownership, preserves each camera’s settings, and can switch away from a destroyed camera. Cleanup destroys only the sample’s own entities.

   Windows Debug/Release checks cover Orthographic controls and selection and Perspective rendering, orbit, free-fly, ground picking, resize and minimize/restore. See the [validation record](EngineModules.md#windows-gameplay-hot-reload-validation).

4. **Perspective migration and snapshot-based helpers.**
   - [x] Render through the selected ECS camera, preserve orbit/zoom/reset, and move mesh animation out of draw gathering.
   - [x] Add ScreenPointToRay for both projections using the presented view and drawable extent. Verify Perspective ground picking across resize.
   - [ ] Add world-to-screen and depth-based screen-to-world when a caller needs them, then verify round trips for both projections.

   Keep matching matrices and viewport dimensions across rendering and screen queries. Preserve shared geometry, the sprite quad and cached textures across controller updates.

5. **Direct game-module integration.**
   - [x] Update the moving sprite's Transform directly and retain its Velocity and bounce behavior. Remove the Position component and SyncLegacyMovement bridge.
   - [x] Bind the selected Perspective camera explicitly to the DLL in free-fly mode. The DLL controls pose and FOV, and the host reads the result for drawing and picking.
   - [x] Bind one imported Perspective mesh to the DLL for direct Transform writes. The host still owns the entity and gathers its draws.

   Windows checks verified that sprite and camera edits survive the host frame, compatible replacement preserves selected camera/entity identity and retained values, and rejected candidates preserve the active behavior.

6. [x] **Add one useful perspective controller.** The opt-in DLL controller supports free-fly movement, right-drag mouse look and separate wheel FOV control.

   Verified frame-rate-independent movement at 30 and 60 updates per second, native minimize/restore and compatible controller edits while the sample was running.

Validation and workflow gate:

- [ ] Extend focused checks under ignored out/ only for the changed contracts, with recorded commands/inputs so the integration checks can be rerun. Exercise shared Transform composition, camera scale, read-only gathering, controller handoff/idle edits, matching viewport/matrix snapshots, projection helpers, camera/entity lifetime, repeated reload, retained values, failed builds/rejected replacements, resource counts/reuse and shutdown alongside native Windows Debug/Release rendering checks.
- [x] Record edit-to-visible-change time through polling, background build, validation, replacement and rendering. The [validation record](EngineModules.md#windows-gameplay-hot-reload-validation) also describes shared-definition and build changes that require a host rebuild and restart.
- [x] Record the Windows sprite and camera reload evidence separately from historical checks. The probes are ignored local artifacts. CTest, shader/asset reload and native macOS/Linux game-module validation remain separate.

After this foundation, add reusable orbit/follow controllers, smoothing, shake and camera transitions/blending when gameplay needs them. Visibility layers, simultaneous views and render-to-texture follow renderer support; physical lens/exposure/depth-of-field/jitter follow their consuming systems. A complete editor, arbitrary C++ state migration, generalized plugin loading, broad query optimization, particle/VFX systems and extensive new rendering features stay outside this milestone. The Orthographic sample owns the entities for its active level. General scene and session ownership follows concrete world-isolation requirements.

#### 3.2 Persistent assets, scene files, and resource lifetime

- [x] Load the original Orthographic layout and Courtyard from versioned level files. Keep the authored sprite, camera, and movement bindings together.
- [x] Restart with F3 and switch with F4. Validate the candidate before retiring the outgoing entities. Retain the current level when its replacement fails.
- [x] Verify both Windows presets, repeated switches and restarts with stable resources, and level state retention across compatible DLL reload. See [the format, controls, and validation](EngineModules.md#file-authored-orthographic-levels).

- [x] Clean up the current sample, asset-watch and mesh-rendering internals before persistent asset work (2026-11-19).

The cleanup keeps material assignments local to decoding and resolved mesh handles local to loading. Level construction reads the decoded definition by reference instead of copying its source bytes and meshes. The sample uses its active watcher as the level-path source and draw submissions as the texture-reference source. Courtyard and mesh materials share PNG watcher registration, and upload diagnostics use the watcher's own path. The mesh queue stores draw pointers directly; `VulkanMeshInstance.hpp` names the unchanged instance layout. FBX import computes the normal matrix once per mesh and reports invalid attributes consistently. Every current command-line option still has a supported caller, so all remain available.

Windows Debug and Release builds and focused native checks passed: imported normals/UV seams, reflected and nonuniform normal transforms, compatible DLL state retention and rotation, F6, legacy-level defaults, rejected levels and recovery, repeated F3/F4 with shared resources, and balanced GPU resource creation/destruction. Stock live runs passed level/material/PNG/FBX edits, malformed-save recovery, resize/minimize/restore, standalone orbit, Courtyard completion and wall PNG reload. Perspective and Gallery pixels match the saved pre-cleanup captures in both presets. Validation inputs were restored and unrelated workspace files matched their original hashes. Evidence is under ignored `out/validation/code-cleanup/`; these local probes are not a tracked regression suite. Failed-game-build, incompatible-DLL and endpoint-audio coverage remains the preceding milestone's evidence. Native macOS/Linux verification and CTest remain open.

- [x] Persist a versioned project catalog with UUID/type/Content-relative source/texture settings and ordered scene records. Both samples register their existing assets and audio through data; new FBX/PNG/scene additions need no sample C++ edits or host rebuild in development.
- [ ] Define source, dependency, and build-output records for the existing image loader, static FBX importer, and HLSL build path.
- [x] Separate persistent asset UUIDs from generation-checked managed MeshHandle/TextureHandle values and explicit host AssetLease ownership. Preserve legacy context-lifetime borrowed APIs.
- [x] Share resources through a weak AssetStore cache and scene/application leases. Stop workers and release the last managed owner; retain native data through submitted frame fences and free/reuse descriptor bindings. Legacy AssetResources remains pinned.
- [x] Add scene-owned parent/child transform propagation, stable authored IDs, cycle rejection and defined reparent/detach/cascade behavior. See [hierarchical 3D validation](#hierarchical-3d-scene-validation).
- [x] Extend the 3D codec to version 3 hierarchy and explicit validated/atomic authored save/load with typed asset references.
- [ ] Generalize component/asset serialization beyond the supported sample codecs.
- [x] Switch supported scene codecs across shared and exclusive resource sets with bounded cache, watcher, runtime-slot and descriptor counts. SceneSession handles source/catalog publication and F3/F4; both samples use it.
- [x] Verify the supported file-authoring path, failed candidates, identity-preserving asset/catalog replacement, superseded snapshots, repeated switching and shutdown in Windows Debug/Release. See [SceneWorkflow.md](SceneWorkflow.md).
- [x] Verify supported 3D writer/read round trips, flat-file compatibility and atomic-save/live-reload recovery.
- [ ] Add general component/asset serialization round trips when a broader schema is implemented.

The current Windows milestone loads project/scene data from files and keeps authoring live in both sample families. Scene owns entities, names, selection/bindings and asset leases; the host retains the session across compatible DLL replacement. Development sources resolve from the configured source Content root, including an absolute shader source path. Ordinary runtime uses automatically staged Content. Valid level saves restart attempts; catalog resource edits and PNG/FBX/DLL replacement preserve applicable identities and state. Invalid or missing saves retain the last good result and corrected dependencies recover automatically. See [format, ownership, launch commands and validation](SceneWorkflow.md).

Windows Debug/Release native probes pass 100 managed-resource cycles and 60 scene switches per family, including stale handles, descriptor reuse, constructor rollback, Unicode source paths, superseded candidates, data-only new assets/scenes, invalid/missing metadata/scene/PNG/FBX recovery, and owned-only destruction. Both families retain camera/entity/resource/outcome state through compatible DLL reload, incompatible rejection and valid recovery. Stock sessions in both presets show new files and scenes without rebuilding and keep pixels through a real failed game build. Stock Debug/Release audio/control checks passed, including both beacon cues, Courtyard completion and orbit. Both presets passed automatic 2D scene and metadata recovery, F2/Home controls, live wall PNG and shader replacement with invalid-edit retention and exact pixel recovery. The final shader watcher uses an absolute configured source path; all new public headers compile independently. Validation inputs and unrelated workspace content are checked against saved hashes. Evidence is local under ignored `out/validation/scene-workflow/`; CTest stays deferred and native macOS/Linux execution remains open.

The supported Windows workflow meets the initial file-driven authoring and bounded-lifetime gate. Broader 3.2 completion still includes import dependency/build-output records and general component/asset serialization beyond the supported 3D writer.

#### Hierarchical 3D scene validation

Windows Debug and Release builds and 3D-only native probes pass: nested local/world propagation, nonuniform scale/reflection/shear, inverse-transpose normals, transformed AABBs, rigid parented-camera rays, cycle/self/stale/cross-scene rejection, local-preserving detach/reparent, cascade destruction and binding cleanup. Version 1/2 flat files remain readable; version 3 writer round trips retain authored IDs, parents, local transforms, materials/assets, lighting, camera settings and gameplay/audio bindings. Invalid saves, locked-destination replacement failure, Unicode paths and temporary-file cleanup pass. Live saves publish once, restart authored state, reject invalid/superseded candidates and recover without F3.

Both configurations verify real DLL-controlled child rotation and descendant movement, host-controlled second-arm propagation, compatible/incompatible DLL replacement, retained camera pose/selection, hierarchy, completion tint and mesh handles, malformed FBX retention/recovery, 30 scene switches with bounded slots/descriptors and zero remaining scene-owned assets/workers/entities after retirement. The renderer's one pinned white-texture binding remains its baseline. Stock 3D sessions show root/lighting edits, FBX changes and last-good pixels, DLL-only speed edits, intentional failed game builds and valid recovery, T/F6, ground picking, standalone orbit/Home, resize/minimize/restore and normal shutdown. Vulkan core/synchronization validation reports no warnings/errors. Screenshots and native sources/logs are local under ignored `out/validation/scene-hierarchy/`; they are not a tracked regression suite.

Temporary production level/FBX/game-source edits were restored. Existing workspace edits were preserved. The Perspective codec now retains material names for saving, separates file parsing/writing from runtime behavior, and removes its unused adapter constructor. All existing command-line options still have callers and remain supported. No 2D runtime tests were run; CTest stays deferred. Native macOS/Linux execution, general component/asset serialization, multi-camera authoring, prefabs, editor UI, multi-object FBX/import dependencies, broader physics and additional rendering features remain outside this milestone. Atomic saves promise visibility on the supported local filesystem, not power-loss durability or concurrent-writer conflict resolution.

Use F4 twice after launch for Assemblies. Hold T to rotate the DLL-controlled arm; Space toggles host animation. Full workflow and save API: [SceneWorkflow.md](SceneWorkflow.md#hierarchical-3d-authoring).

```powershell
# Debug
Set-Location -LiteralPath 'D:\RavenEngine'
& 'D:\RavenEngine\out\build\x64-debug\RavenEngine\RavenEngine.exe' --watch-game --free-fly --watch-mesh

# Release
Set-Location -LiteralPath 'D:\RavenEngine'
& 'D:\RavenEngine\out\build\x64-release\RavenEngine\RavenEngine.exe' --watch-game --free-fly --watch-mesh

# Game-only builds while running; do not rebuild the loaded host
cmake -P 'D:\RavenEngine\out\build\x64-debug\RavenEngine\GameBuild\BuildGame.cmake'
cmake -P 'D:\RavenEngine\out\build\x64-release\RavenEngine\GameBuild\BuildGame.cmake'
```

### 4. Playable 2D sample and core game services

- [x] Complete the Windows Courtyard attempt with WASD, an authored wall, courier and beacon triggers, distinct outcome sounds, restart, and level switching.
- [x] Pace frames against a deadline and run Orthographic gameplay at 60 Hz with bounded catch-up. Preserve keyboard transitions until consumed and clear pending input on minimize and successful level replacement.
- [x] Resolve player intent with the shared swept XZ collision API and write the existing ECS Transform. Keep authored colliders and attempt state with the level.
- [x] Cache PCM WAV clips by asset ID in a host-owned service and play one outcome cue through the private Windows backend.
- [x] Verify Windows Debug and Release, including native endpoint playback, 30/60/144 Hz schedules, repeated restarts, rejected solids, and Debug player-speed reload. See [Courtyard gameplay](EngineModules.md#courtyard-gameplay).
- [ ] Expose scene access and runtime resource handles to game code through stable engine APIs.
- [ ] Bring Courtyard through the v1 scene, platform, and packaging gates.

Completion: the 2D game runs through the reusable engine, can restart without leaks, and behaves consistently at different frame rates and after minimize/restore.

### 5. 3D rendering and gameplay

- [x] Load Blender-authored cube and sphere meshes through the static FBX importer and asset cache. Verify unit conversion, triangle winding, and the Perspective sample on Windows. See [static FBX meshes](AssetSystem.md#static-fbx-meshes).
- [x] Swap Perspective mesh sources with F6 and reload either static FBX while retaining its handle and retiring old buffers after their frame fences. See [live Perspective mesh reload](AssetSystem.md#live-perspective-mesh-reload).

#### Completed Windows milestone: reusable reloadable 3D interaction game

Owner-approved direction: build a complete 3D switch/assembly/door puzzle with a clear goal and restart. Human-readable level information is not required. Use a versioned binary scene format with explicit field encoding, stable authored IDs and asset references, bounded validation and atomic saves; never serialize native C++ object layouts, runtime handles, pointers or GPU/DLL state. Author through save/export APIs or tools. Saving valid scene data must remain live in development without F3.

- [x] Expose safe authored-ID scene access and focused component queries to the game DLL.
- [x] Add reusable switch, rotator, door and retained attempt-state components/systems; keep ownership and rendering in the host and gameplay rules/animation in the DLL.
- [x] Pick interactable 3D objects using the same world transforms and presented camera as rendering.
- [x] Implement the binary codec, writer and a migration path for existing 3D scene content that preserves authored identities, hierarchy, materials/assets and gameplay bindings.
- [x] Build the complete interaction puzzle with completion and restart. Preserve progress across compatible DLL/asset replacement; authored level saves restart the attempt automatically.
- [x] Verify Windows Debug/Release 3D gameplay, live binary saves, malformed/truncated/unsupported-version recovery, code/asset reload, retained state and resource/window lifetime with Vulkan validation.

Keep full physics, editor UI and new rendering features deferred. Runtime testing remains 3D-only; CTest remains deferred. This milestone is implemented and verified on the Windows 3D path.

Current evidence: Windows Debug/Release builds and native/stock 3D checks pass with clean Vulkan core/synchronization validation. Binary migrations reproduce the authored values exactly, 512 checksum-repaired mutations exercise bounded parsing, and malformed/truncated/version/count/reference/atomic-save failures retain the last good result. Scene-scoped queries reject stale/foreign entities; exact triangle picking covers occlusion, affine transforms and camera depth limits. The same reusable systems drive two switches and independent scenes, reversible arms/door gating and one completion notification. DLL speed edits preserve progress, real failed builds and incompatible candidates retain active code, and valid source/FBX/PNG saves recover. Thirty-two scene switches keep slots/descriptors bounded; scene-owned entities, workers and CPU/GPU mesh data retire correctly. Window lifecycle, restart, legacy T/F6 and orbit/Home pass. Final public headers compile independently and final rebuilt host/DLL sessions complete the puzzle again.

RavenSceneTool supports live binary position/rotation/scale, lighting and rotator edits, validation and explicit legacy import. The runtime text parser was removed from the 3D path, old staged 3D text files were removed, and bounded file reading now has one public declaration shared by the tool and engine. All existing runtime command-line options still have callers. Temporary validation inputs were restored and unrelated workspace edits were preserved. Current binary schema, save/export examples, complete launch/game-only commands, timings, reproducible local probe commands and gaps are recorded in [InteractionGame.md](InteractionGame.md). Local evidence is under ignored `out/validation/interaction-game/`; tracked regression coverage, CTest and native macOS/Linux execution remain open. No 2D runtime tests were performed.

#### Completed Windows milestone: playable 3D sample

1. [x] **Bind one imported mesh to reloadable gameplay.** Hold T in Perspective free-fly to turn the explicitly bound cube entity about Z. The DLL is the only writer of that rotation in this mode. The host retains entity ownership, gathers draws from Registry copies, and animates the sphere. F6 still swaps mesh handles between the two object entities.
2. [x] **Verify the combined path.** Rebuilt the Windows Debug and Release hosts for the new GameBindings contract. Stock runs with `--watch-game --free-fly --watch-mesh` showed T-driven rotation, unchanged idle pixels through DLL replacement, and new geometry after a watched `Cube.fbx` edit. Both presets kept the last good result through a failed build, incompatible DLL, and malformed FBX, then recovered with valid edits.
3. [x] **Finish the authored 3D attempt.** Perspective uses the staged level and plays Courtyard's win cue once when the beacon is reached. Windows Debug and Release verify completion retention across compatible DLL reload and a new cue after restart.
4. [x] **Reload level edits automatically during development.** With `--watch-game --free-fly`, settled saves of the staged Perspective level restart the attempt automatically. Rejected or missing files preserve the active attempt. F3 still restarts manually. See [live level and audio validation](EngineModules.md#live-perspective-level-editing-and-completion-audio).
5. [x] **Light two authored levels.** MaterialComponent and LightingComponent drive the shared mesh path with imported normals, UVs, optional PNG textures, ambient and directional light. F4 selects Perspective or Gallery. Windows Debug/Release verify live edits, old-level defaults, failed-edit recovery, retained state, normal transforms, resource lifetime, audio and window behavior. See [mesh materials and lighting](MeshMaterials.md).

Focused Vulkan probes in both presets measured 0.15 radians per 100 ms at the original rate and 0.45 radians after the logic edit. The probes checked the drawn model against the ECS pose, retained entity IDs, pose, camera selection and lens, and confirmed that 36 cube indices changed to 672 sphere indices behind the same renderer mesh handle and object. Standalone orbit, F6 swapping, and Courtyard win, loss, restart, and level switching passed in both presets. Temporary source edits were restored and FBX saves used isolated copies. Local logs, captures, and probes are under ignored `out/validation/perspective-mesh-gameplay/`. Native macOS and Linux and tracked regression coverage remain open.

Multi-object FBX sub-asset identities and import dependencies follow a concrete asset requirement. General scene serialization, switching across resource sets, and release remain in [persistent assets and scene lifetime](#32-persistent-assets-scene-files-and-resource-lifetime).

The milestone is complete when a Windows player can load the authored scene, play to a clear outcome with lit geometry and audio, restart, and observe compatible code and FBX edits during play. Verify state retention, recovery, and Vulkan validation in both Windows presets.

#### Completed Windows milestone: second objective and repeatable smoke

- [x] Separate 3D scene instantiation from gameplay component setup. Route free-fly, mesh rotation, and objective updates through scoped scene access.
- [x] Make scene queries check registry liveness directly while iterating the scene's own entities. Preserve ownership checks on public reads and writes.
- [x] Add Collection as the fifth 3D scene. The game DLL retains pickup state and gates the existing exit goal until all three pickups are collected. The binary codec still reads and writes the earlier four scenes byte-for-byte.
- [x] Verify Windows Debug and Release with the [repeatable 3D smoke check](InteractionGame.md#repeatable-windows-3d-smoke), including a fresh Debug build from a source-only snapshot. The normal sample also verified Collection completion and restart. A watched Debug build changed the rule during play, kept partial progress through a compile failure, and recovered after the valid source was restored.

### 6. Shader and asset reload

- [x] Reload Courtyard's wall PNG during play on Windows. Retain the attempt and the last good texture through bad saves. Verify GPU lifetime, Debug and Release, and save-to-present timing. See the [launch and validation record](AssetSystem.md#live-courtyard-wall-reload).
- [x] Reload Courtyard's textured fragment shader on Windows. Verify the opaque and transparent passes, rejected edits, state retention, and GPU cleanup. See [live shader reload](EngineModules.md#live-courtyard-shader-reload).
- [x] Reload Perspective's single-object FBX sources with background decoding, stable handles, frame-fence retirement, and recovery from failed imports and uploads. See [live mesh reload](AssetSystem.md#live-perspective-mesh-reload).
- [x] Make game code, scene/project, PNG, FBX and lit fragment watching the default Windows 3D development workflow. Verify last-good recovery, retained ECS progress, worker pipeline preparation, and a staged `--no-watch` launch. See [validation and Release timings](DevelopmentReload.md#measured-windows-results).
- [ ] Extend source and dependency watching and background compile/import to more shaders and assets.
- [ ] Verify frame-boundary publication, GPU retirement, and last-good recovery for each newly supported replacement path.

### 7. Runtime game-code reload

Initial module build/load/replacement and minimal ECS state preservation begin in the immediate joint milestone, before the full scene/game framework. This section also tracks the remaining reload scope and final validation.

- [x] Establish the initial engine/game update contract, retained ownership, matching-toolchain constraints and generated compatibility rejection. Direct implementation was authorized by the owner on 2026-10-19.
- [x] Build a standalone Windows game DLL and native loading/unloading backend; other OS backends remain in platform completion.
- [x] Define an update-only module lifecycle with host-owned initialization/cleanup and no retained module callbacks, objects or threads.
- [x] Poll/build changed game code and reload it while preserving compatible minimal component state on Windows.
- [x] Reject failed builds, invalid modules, or incompatible contracts while retaining the active game code on Windows.
- [x] Verify direct sprite Transform writes and explicit Perspective camera control across compatible reloads. See the [Windows gameplay reload validation](EngineModules.md#windows-gameplay-hot-reload-validation).
- [x] Verify Windows DLL replacement, rejection, and recovery on the [imported mesh gameplay path](#next-windows-milestone-playable-3d-sample).
- [ ] Extend the current rejection/manual-restart policy when supporting broader game state or migration. Initial repeated replacement and shutdown are already verified; arbitrary C++ migration is outside this baseline.

Completion: change gameplay code during a running sample and observe the new behavior without restarting the host. Compatible state persists; failed builds and incompatible modules produce clear diagnostics and a defined recovery path.

### 8. Platform completion

Windows and shared contracts remain the immediate implementation scope. Contributors can finish platform services and validate current rendering samples in parallel; the final platform gate follows once gameplay and reload behavior exists.

The [platform contributor handoff](PlatformHandoff.md) records macOS/Linux work order, required interfaces, and verification criteria.

- [ ] Finish Win32 key translations, repeat/text-input behavior, and exposed focus state; verify mixed-DPI monitor movement.
- [ ] Verify shared file/path, watcher, and module-loading behavior needed by the samples.
- [x] Merge Cocoa window/input/framebuffer and executable-path implementations, plus macOS Debug/Release presets. Earlier Debug rendering/input checks are recorded in [MacOSBuild.md](MacOSBuild.md); current shared-code compatibility still needs verification.
- [x] Merge the Linux X11/XCB backend, executable-path implementation and Debug/Release presets. Native Linux build/runtime verification remains pending.
- [ ] macOS and Linux contributors: implement `Platform::FillRandomBytes` and connect each native source/dependency in CMake for `AssetId::Generate`.
- [ ] Verify current input snapshot timing, framebuffer, file/path and Vulkan behavior in both rendering samples on each platform after the new random-byte hook is connected.
- [ ] Run both samples and all reload paths in Debug/Release on Windows, Linux, and macOS.
- [ ] Verify supported GPU/queue-family configurations beyond the currently tested single-family device; document requirements and unsupported-capability errors.

Completion: the same scene/assets/game logic run on all roadmap platforms through shared interfaces, with platform verification supplied by the responsible contributor.

### 9. Quality, documentation, and packaging

Carry validation forward during every milestone; this section is the final release gate.

- [ ] Add automated builds and CPU tests for supported platforms; run native Vulkan smoke/validation checks where GPU environments are available.
- [ ] Enable useful compiler warnings, static analysis, and supported sanitizer configurations; resolve actionable findings.
- [ ] Verify assertions and failure reporting; add crash diagnostics and a lightweight native smoke checklist.
- [ ] Profile Release CPU/GPU frame times, memory use, batching, and reload costs; define sample budgets and resolve measured bottlenecks.
- [ ] Review graphics-error and presentation/resource-completion handling, including the current device-idle recreation/shutdown approach.
- [ ] Consolidate window dimensions around `GetFramebufferState()` and review the standalone `GetWidth()`/`GetHeight()` accessors across backends.
- [ ] Have swapchain preparation report whether the current framebuffer is usable, so frame drawing can use one readiness check while preserving minimized-window behavior.
- [ ] Keep mesh handle creation and lookup beside the mesh storage in `VulkanContext::Impl`.
- [ ] Verify the debugger breakpoint workflow and document the distinction between debugger Edit and Continue and runtime module reload.
- [ ] Refresh README/setup instructions with tested tool versions, architecture, API contracts, asset workflow, reload behavior, and platform ownership.
- [ ] Package the engine host, game modules, shaders, assets, and required runtime dependencies with appropriate licenses.
- [ ] Verify a clean checkout build and a relocated packaged run without developer-specific paths.

Completion: a documented release package runs the 2D and 3D games on supported platforms, with reproducible builds, recorded checks, and clear requirements.

### v1 completion checklist

- [ ] Both playable samples use reusable rendering, ECS/scenes, file-based assets, input, and audio.
- [ ] Shader, asset, and game-code reload work and recover from intentional failures.
- [ ] Both samples pass platform, resize/minimize/restore, restart, scene-switching, and shutdown checks.
- [ ] Automated tests/builds and native validation evidence are recorded.
- [ ] Relocatable packages and documentation let another developer build, run, and modify a game.

The Windows examples verify retained sprite movement and reloadable Perspective camera control. See the [validation record](EngineModules.md#windows-gameplay-hot-reload-validation) for the measured edit-to-render loop and reproduction steps.

### Deferred work

General-purpose 2D collision, 3D collision, and physics simulation are unscheduled. See the [roadmap deferral](#archived-roadmapmd).
