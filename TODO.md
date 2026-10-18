# RavenEngine v1 TODO

The goal follows [ROADMAP.md](ROADMAP.md): a C++20 engine that can build and run small 2D and 3D games using Vulkan, ECS, and reloadable game code, shaders, and assets. Author games through code and data files. A visual editor is outside v1. This is the working implementation order; the roadmap summarizes the wider scope. Write and maintain project documentation in English.

Work on Windows and shared engine code first. macOS and Linux contributors can complete their backends and native verification in parallel. Final platform completion must exercise the game and reload behavior once it exists. Keep shared contracts independent of operating-system headers.

Perspective and orthographic rendering are implemented using the agreed Blender world axes and shared transform/view math. The current priority is a minimal ECS-backed scene using the existing renderer and texture cache. Blender/FBX remains the planned 3D asset direction.

For engine implementation, propose small, reviewable snippets and explain each change. The owner reviews and types implementation changes by hand unless explicitly asking the assistant to edit them. Check an item only after its completion criteria are demonstrated. Approval of this plan or documentation updates does not authorize implementation of every item.

## Evaluation as of 2026-10-14

The codebase has a working Windows rendering foundation sufficient to begin a small game framework. `RavenCore.dll` owns reusable engine code; `RavenEngine.exe` contains perspective and orthographic rendering samples behind an application client interface. Shared math, depth testing, opaque/transparent textures, explicit ordering, frustum culling, and adjacent compatible draw instancing are implemented. Runtime content paths are relocatable. UUIDv4 asset identities, typed references, an immutable in-memory catalog, image loading, and ID-based texture reuse are implemented; persistent metadata, mesh import, dependency/build rules, and runtime resource handles remain pending. There is no ECS, scene lifecycle/serialization, physics, audio, playable game, or runtime reload yet.

On 2026-10-14, the current Windows Debug and Release builds pass and CTest lists zero registered tests. Existing native/GPU validation records below were reviewed, not rerun in that assessment. The rendering samples are demonstrations, not playable games. CTest integration remains deferred at the owner's request; revisit the deferral when defining entity/component lifetime checks. Temporary probes under ignored `out/` are historical evidence, not a regression suite available from a clean checkout.

The indexed triangle milestone is implemented and verified. Debug and Release each passed 243 indexed draws and presentations, including resize, minimize/restore, animation, and construction-failure cleanup. Debug reported no core or synchronization validation warnings/errors. Evidence: `out/validation/indexed-debug.log`, `indexed-release.log`, and `out/screenshots/ravenengine-indexed-triangle.jpg`.

The input baseline already includes keyboard transitions/events, modifiers, mouse buttons/movement, and vertical/horizontal wheel handling. Finish the remaining behavior when needed by samples; do not repeat completed input work.

Perspective validation: Debug/Release builds and shader validation pass; a temporary probe passes 59 math/projection checks. Computer use confirms animation, horizontal/vertical camera orbit, wheel zoom, Home reset, maximized/portrait proportions, minimize/restore, and normal shutdown. Debug has no core/synchronization validation diagnostics; Release also launches from `out/`. Evidence: `out/validation/perspective-*.log` and `out/screenshots/ravenengine-perspective.jpg`. No test sources/framework were added; CTest wiring remains pending.

Later rendering and asset validation is recorded in [TextureResources.md](docs/TextureResources.md), [CoordinateSystem.md](docs/CoordinateSystem.md), and [AssetSystem.md](docs/AssetSystem.md). The asset milestone records 3,417 public-client checks and 25 frames per Windows configuration; batching records matching GPU captures and preserved ordering. Those records do not establish current macOS/Linux support.

## Immediate next step: entity identity and lifecycle

- [ ] Agree a small `Entity` value and registry contract: invalid values, index/generation identity, registry ownership, and generation exhaustion behavior.
- [ ] Add creation, destruction, liveness checks, and slot reuse; a destroyed entity must never become valid again when its slot is reused.
- [ ] Verify default/invalid entities, multiple live entities, stale references after reuse, repeated destruction, and registry lifetime boundaries.

Propose the identity contract and a small first snippet before component storage. Keep asset UUIDs separate from runtime entity identity. The first visible milestone is the orthographic sample driven by ECS transforms and sprites, with runtime creation/removal while texture reuse, transparency, culling, and batching remain valid.

Working order: minimal ECS/renderable scene (3.1), persistent asset/scene data and resource lifetime (3.2), playable 2D game (4), imported/lit 3D gameplay (5), shader/asset reload (6), then game-code reload (7). Platform work proceeds in parallel. Persistent asset metadata, FBX import, and broader materials are not prerequisites for starting 3.1.

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

## 1. Reusable geometry, transforms, and perspective camera — implemented baseline

- [x] Agree and document world axes: right-handed, +X right, +Y reference forward, +Z up, XY ground plane; see [CoordinateSystem.md](docs/CoordinateSystem.md). Shared math and both camera types now implement this convention.
- [x] Define matrix layout/multiplication order and CPU/HLSL agreement using the agreed world axes.
- [x] Add model/view/projection transforms and a perspective camera; define field of view and near/far planes, and update aspect ratio through framebuffer resize.
- [x] Add depth resources/testing and recreate size-dependent attachments correctly for the initial 3D sample.
- [x] Introduce reusable geometry ownership and object draw submissions with explicit GPU lifetime rules. Context-owned meshes remain alive until shutdown after GPU completion.
- [x] Move sample geometry and animation out of `VulkanContext` into sample code.
- [x] Draw simple 3D geometry with independent object position, rotation, and scale through one perspective camera.
- [x] Separate reusable engine code and the sample executable into CMake targets as this boundary becomes concrete. `RavenCore.dll` owns the engine; `RavenEngine.exe` owns the sample through create/frame/destroy callbacks. Debug/Release builds, 17 temporary DLL client checks per configuration, and native smoke checks pass; see [EngineModules.md](docs/EngineModules.md).
- [ ] Add CTest wiring and focused transform/projection tests; make the current rendering smoke checks repeatable. Deferred at the owner's request; revisit at the entity/component lifetime milestone without marking this work complete.

Completion: an input-controlled perspective camera and at least two independently transformed 3D objects render in Debug/Release with correct depth occlusion and perspective scaling. Verify near/far clipping and aspect ratio, resize/minimize/restore, and shutdown without validation diagnostics. The renderer context no longer owns the sample's animation or geometry choices.

## 2. Asset loading and textured 2D rendering — implemented baseline

- [x] Add orthographic projection for 2D using the existing transform/view math; define the visible region and resize behavior. Fixed vertical world extent, aspect-dependent width, camera pan/zoom, depth-invariant sprite size, transparency and frustum selection are verified in Debug/Release; see [CoordinateSystem.md](docs/CoordinateSystem.md).
- [x] Define asset roots, identifiers, and file/path access; use relocatable runtime paths instead of absolute build-tree shader paths. Runtime paths, UUIDv4 identifiers, typed asset references, an immutable in-memory catalog and ID-based texture reuse are implemented and verified in Debug/Release. See [RuntimeUtilities.md](docs/RuntimeUtilities.md) and [AssetSystem.md](docs/AssetSystem.md).
- [x] Load PNG/JPEG images into owned RGBA8 pixels with file/decode/size diagnostics, a pinned private decoder, and staged license; see [ImageLoading.md](docs/ImageLoading.md).
- [x] Compile HLSL into validated SPIR-V through CMake and stage runtime shader outputs. This is build-time shader processing; source watching and runtime replacement remain in milestone 6.
- [x] Add image, image-view, and sampler ownership, texture uploads, and required layout/synchronization handling. Initial single-mip RGBA8 Linear/SRGB textures pass Debug/Release GPU readback, failure cleanup, and public-client checks; see [TextureResources.md](docs/TextureResources.md). Initial descriptors and sampling are also implemented.
- [x] Own immutable texture descriptors and update current draw transforms/tints safely. Color draws use push constants; textured instances update per-frame buffers after fence completion. General material data remains in milestone 5.
- [x] Render textured quads with tint, transparency, camera transforms, and explicit draw ordering. Perspective/orthographic opaque/straight-alpha quads use signed layer/order, automatic camera-depth sorting, and per-instance frustum selection; see [TextureResources.md](docs/TextureResources.md) and [CoordinateSystem.md](docs/CoordinateSystem.md).
- [x] Batch compatible sprites while preserving ordering and separating incompatible textures/materials. Adjacent textured submissions sharing a mesh/texture are instanced within separate opaque/transparent passes; per-frame buffers retain capacity and update after fence completion. Debug/Release GPU captures match individual draws exactly; see [TextureResources.md](docs/TextureResources.md).

Completion: render the orthographic sample using file-loaded textures, overlapping transparent sprites and multiple textures; verify different working directories, resize and renderer shutdown. This rendering baseline is implemented. Sample objects and catalog registrations are still authored in C++; scene files and persistent asset metadata belong to 3.2.

## 3. ECS and scenes — current focus

### 3.1 Minimal ECS and a renderable scene

- [ ] Complete the entity identity/lifecycle contract and registry described in the immediate next step.
- [ ] Define component ownership, reference invalidation, and cleanup when entities are destroyed.
- [ ] Implement component storage, queries, and safe entity/component changes during system updates.
- [ ] Add transform, camera, and sprite components; retain typed asset references and resolve resources through the existing shared catalog/cache.
- [ ] Add a minimal scene owner and build existing renderer submissions from its components.
- [ ] Drive the orthographic sample from ECS data and demonstrate runtime entity creation, movement and removal.
- [ ] Validate entity/component lifetime and iteration behavior with focused checks; revisit the CTest deferral before deciding how these checks are retained.

Completion: the orthographic sample renders through a scene, rejects stale entity references, and creates/removes sprites while preserving the existing rendering behavior. Reuse a scene-shared mesh and the existing catalog/cache for this milestone; per-entity GPU allocation is unnecessary. File-based scene authoring, hierarchy, scene switching and resource release follow in 3.2.

### 3.2 Persistent assets, scene files, and resource lifetime

- [ ] Choose and version a persistent asset metadata/catalog format; preserve UUIDs, typed references, Content-relative paths and texture settings across runs.
- [ ] Define source/dependency/build-output records for textures, meshes and shaders. Integrate the existing image loader and HLSL build path; mesh import follows in milestone 5.
- [ ] Define runtime resource handles separately from persistent asset references, including ownership, sharing, invalid/stale use and release behavior.
- [ ] Implement resource reuse/release across scene lifetimes and defer GPU destruction until outstanding work completes. Current renderer-owned references remain valid only until renderer shutdown; a service destructor does not unload its textures.
- [ ] Add parent/child transform propagation with cycle rejection and defined parent-destruction behavior.
- [ ] Choose and version a scene format; serialize/load scenes with validation of asset references and malformed data.
- [ ] Implement scene switching/restart and explicit scene reload after the resource-lifetime contract is in place. Automatic source watching remains in milestone 6.
- [ ] Verify asset/scene round trips, load failures, shared resources, repeated switching and shutdown without accumulating unused GPU resources.

Completion: load scene and asset metadata from files, retain stable asset references across runs, and repeatedly switch/restart scenes with bounded resource use and valid GPU lifetimes.

## 4. Playable 2D sample and core game services

- [x] Expose the input snapshot and frame timing through `ApplicationClient::OnFrame`.
- [ ] Expose scene access and runtime resource handles to game code through stable engine APIs.
- [ ] Replace the fixed 16 ms sleep with explicit pacing; define simulation updates and bounded catch-up behavior for physics.
- [ ] Preserve input transitions across simulation/render rates and pause/resume.
- [ ] Add audio loading/playback and 2D collision/physics behind engine APIs; connect them to ECS lifecycle.
- [ ] Build a small playable 2D game with movement, collisions, sound, scene loading, and restart.

Completion: the 2D game runs through the reusable engine, can restart without leaks, and behaves consistently at different frame rates and after minimize/restore.

## 5. 3D rendering and gameplay

- [ ] Extend the perspective camera, 3D transforms, and depth path from milestone 1 to asset-loaded scenes.
- [ ] Load reusable 3D meshes through the asset system, following the Blender/FBX direction and defining imported sub-asset identities, coordinate conversion and dependency/build outputs.
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

Windows and shared contracts remain the immediate implementation scope. Contributors can finish platform services and validate current rendering samples in parallel; the final platform gate follows once gameplay and reload behavior exists.

The [platform contributor handoff](docs/PlatformHandoff.md) records macOS/Linux work order, required interfaces, and verification criteria.

- [ ] Finish Win32 key translations, repeat/text-input behavior, and exposed focus state; verify mixed-DPI monitor movement.
- [ ] Verify shared file/path, watcher, and module-loading behavior needed by the samples.
- [x] Merge Cocoa window/input/framebuffer and executable-path implementations, plus macOS Debug/Release presets. Earlier Debug rendering/input checks are recorded in [MacOSBuild.md](docs/MacOSBuild.md); current shared-code compatibility still needs verification.
- [x] Merge the Linux X11/XCB backend, executable-path implementation and Debug/Release presets. Native Linux build/runtime verification remains pending.
- [ ] macOS and Linux contributors: implement `Platform::FillRandomBytes` and connect each native source/dependency in CMake for `AssetId::Generate`.
- [ ] Verify current input snapshot timing, framebuffer, file/path and Vulkan behavior in both rendering samples on each platform after the new random-byte hook is connected.
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

Perspective/orthographic cameras, transform math, depth-tested samples, engine/sample DLL boundary, textured rendering, ordering, frustum culling, compatible draw batching and the common asset/cache baseline are implemented and verified on Windows. The next work is entity identity/lifecycle, then a minimal ECS-backed orthographic scene. CTest remains deferred; revisit that decision for entity/component lifetime checks.
