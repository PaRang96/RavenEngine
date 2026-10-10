# RavenEngine v1 TODO

This replaces the temporary triangle checklist. The goal follows [ROADMAP.md](ROADMAP.md): a C++20 engine that can build and run small 2D and 3D games using Vulkan, ECS, and reloadable game code, shaders, and assets. Author games through code and data files. A visual editor is outside v1.

Work on Windows and shared engine code first. macOS implementation and verification belong to the macOS contributor; Linux is a later platform milestone. Keep shared contracts independent of operating-system headers.

Target a perspective camera and simple 3D geometry first, following the Blender/FBX direction. Add orthographic projection when starting the 2D sample; both projections use the same world axes and transform/view math.

Each implementation still follows the agreed workflow: propose a small snippet, receive owner approval, implement, and validate. Check an item only after its completion criteria are demonstrated. This list is a plan, not approval to implement every item.

## Evaluation as of 2026-10-10

The codebase has a working Windows rendering foundation. The perspective milestone completed on 2026-10-10 adds reusable transform/view/projection math, a camera, uploaded mesh ownership, and depth-tested draw submissions. Geometry and animation now live in a small sample with two independently transformed cubes and a ground slab. Reusable engine code now builds as `RavenCore.dll`, with the sample in `RavenEngine.exe` behind an application client interface. Public path/file utilities now resolve executable-relative content, and runtime shaders relocate with the executable/DLL. Context-owned RGBA8 textures now upload synchronously with image/view/sampler ownership and verified GPU readback. Public PNG/JPEG loading now provides owned RGBA8 CPU images and verified texture-upload integration. There is no scene/game framework, visible texture sampling path, ECS, asset identifier system, physics, audio, or runtime reload yet. Automated CTest targets remain absent.

The indexed triangle milestone is implemented and verified. Debug and Release each passed 243 indexed draws and presentations, including resize, minimize/restore, animation, and construction-failure cleanup. Debug reported no core or synchronization validation warnings/errors. Evidence: `out/validation/indexed-debug.log`, `indexed-release.log`, and `out/screenshots/ravenengine-indexed-triangle.jpg`.

The input baseline already includes keyboard transitions/events, modifiers, mouse buttons/movement, and vertical/horizontal wheel handling. Finish the remaining behavior when needed by samples; do not repeat completed input work.

Perspective validation: Debug/Release builds and shader validation pass; a temporary probe passes 59 math/projection checks. Computer use confirms animation, horizontal/vertical camera orbit, wheel zoom, Home reset, maximized/portrait proportions, minimize/restore, and normal shutdown. Debug has no core/synchronization validation diagnostics; Release also launches from `out/`. Evidence: `out/validation/perspective-*.log` and `out/screenshots/ravenengine-perspective.jpg`. No test sources/framework were added; CTest wiring remains pending.

## Verified foundation

- [x] Windows C++20 Debug/Release builds, Git, and development configurations.
- [x] Basic logging and top-level exception reporting.
- [x] Native Win32 window/event loop and shared input/framebuffer contracts.
- [x] Monotonic elapsed time and frame delta, with zero delta on initial/resumed drawable frames.
- [x] Vulkan instance, debug messenger, surface, physical/logical device, and queues with explicit ownership.
- [x] Swapchain, frame resources, submission/presentation, and resize/minimize/restore recovery.
- [x] HLSL compilation through DXC, SPIR-V validation/loading, and initial graphics pipeline.
- [x] Frame-delta triangle rotation and aspect correction through push constants.
- [x] Uploaded Vulkan buffer ownership and persistent vertex/index buffers used by indexed draws.

These checks establish the tested Windows baseline on the RTX 3070 Ti Laptop GPU. They do not establish full renderer completion or other-platform support.

## 1. Reusable geometry, transforms, and perspective camera — next

- [x] Agree and document world axes: right-handed, +X right, +Y reference forward, +Z up, XY ground plane; see [CoordinateSystem.md](docs/CoordinateSystem.md). This records the design decision, not an implemented math subsystem.
- [x] Define matrix layout/multiplication order and CPU/HLSL agreement using the agreed world axes.
- [x] Add model/view/projection transforms and a perspective camera; define field of view and near/far planes, and update aspect ratio through framebuffer resize.
- [x] Add depth resources/testing and recreate size-dependent attachments correctly for the initial 3D sample.
- [x] Introduce reusable geometry ownership and object draw submissions with explicit GPU lifetime rules. Context-owned meshes remain alive until shutdown after GPU completion.
- [x] Move sample geometry and animation out of `VulkanContext` into sample code.
- [x] Draw simple 3D geometry with independent object position, rotation, and scale through one perspective camera.
- [x] Separate reusable engine code and the sample executable into CMake targets as this boundary becomes concrete. `RavenCore.dll` owns the engine; `RavenEngine.exe` owns the sample through create/frame/destroy callbacks. Debug/Release builds, 17 temporary DLL client checks per configuration, and native smoke checks pass; see [EngineModules.md](docs/EngineModules.md).
- [ ] Add CTest wiring and focused transform/projection tests; make the current rendering smoke checks repeatable.

Completion: an input-controlled perspective camera and at least two independently transformed 3D objects render in Debug/Release with correct depth occlusion and perspective scaling. Verify near/far clipping and aspect ratio, resize/minimize/restore, and shutdown without validation diagnostics. The renderer context no longer owns the sample's animation or geometry choices.

## 2. Asset loading and textured 2D rendering

- [ ] Add orthographic projection for 2D using the existing transform/view math; define the visible region and resize behavior.
- [ ] Define asset roots, identifiers, and file/path access; use relocatable runtime paths instead of absolute build-tree shader paths. Runtime roots, public path/file utilities, and shader relocation are implemented and verified; asset identifiers remain. See [RuntimeUtilities.md](docs/RuntimeUtilities.md).
- [ ] Define initial texture/mesh/shader formats and dependency/build rules, with useful missing/malformed-file diagnostics. Initial PNG/JPEG-to-RGBA8 loading, a pinned private decoder, staged license, and file/decode/limit diagnostics are implemented and verified; see [ImageLoading.md](docs/ImageLoading.md). Mesh import and asset identifiers/dependency/build contracts remain pending.
- [x] Add image, image-view, and sampler ownership, texture uploads, and required layout/synchronization handling. Initial single-mip RGBA8 Linear/SRGB textures pass Debug/Release GPU readback, failure cleanup, and public-client checks; see [TextureResources.md](docs/TextureResources.md). Descriptors and visible sampling remain below.
- [ ] Add descriptor ownership and frame-safe camera/material data updates.
- [ ] Render textured quads with tint, transparency, camera transforms, and explicit draw ordering.
- [ ] Batch compatible sprites while preserving ordering and separating incompatible textures/materials.

Completion: load and render a textured 2D scene from files, with overlapping transparent sprites and multiple textures. Run it from different working directories and verify resize and resource cleanup with validation.

## 3. ECS and scenes

- [ ] Define entity identities with generation/stale-handle detection and documented component lifetime rules.
- [ ] Implement component storage, queries, and safe entity/component changes during system updates.
- [ ] Add transform components and parent/child propagation with cycle rejection and defined parent-destruction behavior.
- [ ] Add camera/render components and build renderer submissions from scene data.
- [ ] Define scene create/update/destroy behavior and scene switching without leftover resources.
- [ ] Choose a scene data format; serialize, load, and reload scenes with validation of references and malformed data.
- [ ] Add focused entity, component, hierarchy, and scene round-trip/failure tests.

Completion: load a scene from data, create/remove entities at runtime, reject stale references, and switch scenes repeatedly while rendering remains valid.

## 4. Playable 2D sample and core game services

- [ ] Expose input, timing, scene access, and resource handles to game code through stable engine APIs.
- [ ] Replace the fixed 16 ms sleep with explicit pacing; define simulation updates and bounded catch-up behavior for physics.
- [ ] Preserve input transitions across simulation/render rates and pause/resume.
- [ ] Add audio loading/playback and 2D collision/physics behind engine APIs; connect them to ECS lifecycle.
- [ ] Build a small playable 2D game with movement, collisions, sound, scene loading, and restart.

Completion: the 2D game runs through the reusable engine, can restart without leaks, and behaves consistently at different frame rates and after minimize/restore.

## 5. 3D rendering and gameplay

- [ ] Extend the perspective camera, 3D transforms, and depth path from milestone 1 to asset-loaded scenes.
- [ ] Load reusable 3D meshes through the asset system.
- [ ] Add materials, textures, normal handling, and basic lighting.
- [ ] Add 3D collision/physics and verify its integration with transforms, ECS, and audio.
- [ ] Build a small playable 3D game with camera control, lit objects, collisions, sound, and restart.

Completion: the 3D game loads a scene and meshes from files, shows correct depth/lighting and camera behavior, and survives resize, scene changes, and restart without validation diagnostics.

## 6. Shader and asset reload

- [ ] Watch asset sources/dependencies and coalesce changes into reload requests.
- [ ] Compile/import replacements without blocking normal game updates unnecessarily.
- [ ] Apply replacements at a safe frame boundary and retire old GPU resources only after their use completes.
- [ ] Keep the last valid shader/asset active on compile/import failure and report the affected path and error.
- [ ] Test successful reload, repeated edits, missing dependencies, failed replacements, and recovery.

Completion: edit a shader and texture while a sample runs, observe the changes without restarting, introduce a deliberate failure, and recover while the game keeps running.

## 7. Runtime game-code reload

- [ ] Define a versioned engine/game module API, state ownership, allocation/free responsibilities, and ABI/toolchain constraints.
- [ ] Build game code as a separate module and add loading/unloading through platform backends.
- [ ] Define module initialization/update/shutdown and remove callbacks or references before unloading code.
- [ ] Detect/build changed game code and reload it while preserving compatible game state.
- [ ] Reject failed builds, invalid modules, or incompatible versions while keeping the running game usable.
- [ ] Define state migration/reset behavior for incompatible changes and test repeated reload/shutdown.

Completion: change gameplay code during a running sample and observe the new behavior without restarting the host. Compatible state persists; failed builds and incompatible modules produce clear diagnostics and a defined recovery path.

## 8. Platform completion

Windows and shared contracts remain the immediate scope. Schedule this milestone after the samples establish the behavior each backend must support.

macOS/Linux 담당자별 작업 순서, 필수 인터페이스와 검증 기준은 [플랫폼 협업 작업 목록](docs/PlatformHandoff.md)에 정리했습니다.

- [ ] Finish Win32 key translations, repeat/text-input behavior, and exposed focus state; verify mixed-DPI monitor movement.
- [ ] Verify shared file/path, watcher, and module-loading behavior needed by the samples.
- [ ] macOS contributor: update Cocoa to the current window/input/framebuffer contracts and implement/verify platform services and Vulkan requirements.
- [ ] Choose the Linux window backend, implement the same contracts/services, and add build presets.
- [ ] Run both samples and all reload paths in Debug/Release on Windows, Linux, and macOS.
- [ ] Verify supported GPU/queue-family configurations beyond the currently tested single-family device; document requirements and unsupported-capability errors.

Completion: the same scene/assets/game logic run on all roadmap platforms through shared interfaces, with platform verification supplied by the responsible contributor.

## 9. Quality, documentation, and packaging

Carry validation forward during every milestone; this section is the final release gate.

- [ ] Add automated builds and CPU tests for supported platforms; run native Vulkan smoke/validation checks where GPU environments are available.
- [ ] Enable useful compiler warnings, static analysis, and supported sanitizer configurations; resolve actionable findings.
- [ ] Verify assertions and failure reporting; add crash diagnostics and a lightweight native smoke checklist.
- [ ] Profile Release CPU/GPU frame times, memory use, batching, and reload costs; define sample budgets and resolve measured bottlenecks.
- [ ] Review graphics-error and presentation/resource-completion handling, including the current device-idle recreation/shutdown approach.
- [ ] Verify the debugger breakpoint workflow and document the distinction between debugger Edit and Continue and runtime module reload.
- [ ] Refresh README/setup instructions with tested tool versions, architecture, API contracts, asset workflow, reload behavior, and platform ownership.
- [ ] Package the engine host, game modules, shaders, assets, and required runtime dependencies with appropriate licenses.
- [ ] Verify a clean checkout build and a relocated packaged run without developer-specific paths.

Completion: a documented release package runs the 2D and 3D games on supported platforms, with reproducible builds, recorded checks, and clear requirements.

## v1 completion checklist

- [ ] Both playable samples use reusable rendering, ECS/scenes, file-based assets, input, audio, and physics.
- [ ] Shader, asset, and game-code reload work and recover from intentional failures.
- [ ] Both samples pass platform, resize/minimize/restore, restart, scene-switching, and shutdown checks.
- [ ] Automated tests/builds and native validation evidence are recorded.
- [ ] Relocatable packages and documentation let another developer build, run, and modify a game.

The perspective camera, transform math, depth-tested sample, and engine/sample DLL boundary are implemented and verified. The remaining work in milestone 1 is repeatable CTest checks. Orthographic projection follows with the asset/2D milestone.
