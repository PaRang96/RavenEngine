# RavenEngine v1 TODO

The goal follows [ROADMAP.md](ROADMAP.md): a C++20 engine that can build and run small 2D and 3D games using Vulkan, ECS, and reloadable game code, shaders, and assets. ECS and hot reload are joint design goals. Fast iteration means reducing the time from a developer's edit to observing the result. Games are authored through code and data files in v1; a complete visual editor remains outside v1, but ownership and execution boundaries must accommodate future editor editing and play sessions. This is the working implementation order; the roadmap summarizes the wider scope. Write and maintain project documentation in English.

Work on Windows and shared engine code first. macOS and Linux contributors can complete their backends and native verification in parallel. Final platform completion must exercise the game and reload behavior once it exists. Keep shared contracts independent of operating-system headers.

Perspective and orthographic rendering use the agreed Blender world axes and shared transform/view math. Entity lifecycle, the narrow joint ECS/game-module reload example, and reusable Transform/StaticMesh/Sprite components are implemented. Perspective colored geometry and Orthographic textured sprites render from GameInstance/Registry with shared resources. CameraComponent is next, followed by migrating game code directly to Transform. The existing DLL remains compatible through a temporary sample-only movement bridge. Scene ownership is deferred at the owner's request. Preserve the renderer/cache and retained-state boundary before selecting general query/storage optimizations. Blender/FBX remains the planned 3D asset direction.

The usual implementation workflow is small, reviewable snippets typed by the owner. On 2026-10-19 the owner explicitly authorized the assistant to create/edit implementation files until further instruction. That authorization covers the current joint ECS/reload work; revert to snippet approval when withdrawn. Check an item only after its completion criteria are demonstrated.

## Evaluation and agreed direction as of 2026-10-19

The Windows rendering and common asset/cache foundations remain implemented. The joint ECS/reload example now adds a host-owned GameInstance, minimal copied component values, a standalone RavenGame DLL, execution-copy replacement, conservative generated compatibility checks, source-content polling and background builds. Compatible logic changes retain Registry/Entity identity and component values; build/validation failures preserve the active code. General ECS queries/systems, scene ownership/persistence, runtime resource handles, imported meshes/lighting, physics, audio and playable games remain pending. The one moving ECS-backed sprite is a boundary demonstration, not the complete scene milestone.

Current Windows Debug/Release builds and joint ECS/reload evidence are recorded in [EngineModules.md](docs/EngineModules.md), separately from the historical renderer checks below. There are no registered CTest tests; the owner's deferral remains in force. Focused probes under ignored `out/validation/game-reload/` are evidence, not a tracked regression suite available from a clean checkout. These samples are demonstrations, not playable games.

The indexed triangle milestone is implemented and verified. Debug and Release each passed 243 indexed draws and presentations, including resize, minimize/restore, animation, and construction-failure cleanup. Debug reported no core or synchronization validation warnings/errors. Evidence: `out/validation/indexed-debug.log`, `indexed-release.log`, and `out/screenshots/ravenengine-indexed-triangle.jpg`.

The input baseline already includes keyboard transitions/events, modifiers, mouse buttons/movement, and vertical/horizontal wheel handling. Finish the remaining behavior when needed by samples; do not repeat completed input work.

Perspective validation: Debug/Release builds and shader validation pass; a temporary probe passes 59 math/projection checks. Computer use confirms animation, horizontal/vertical camera orbit, wheel zoom, Home reset, maximized/portrait proportions, minimize/restore, and normal shutdown. Debug has no core/synchronization validation diagnostics; Release also launches from `out/`. Evidence: `out/validation/perspective-*.log` and `out/screenshots/ravenengine-perspective.jpg`. No test sources/framework were added; CTest wiring remains pending.

Later rendering and asset validation is recorded in [TextureResources.md](docs/TextureResources.md), [CoordinateSystem.md](docs/CoordinateSystem.md), and [AssetSystem.md](docs/AssetSystem.md). The asset milestone records 3,417 public-client checks and 25 frames per Windows configuration; batching records matching GPU captures and preserved ordering. Those records do not establish current macOS/Linux support.

## Joint ECS and game-code reload baseline

Status: implemented for the narrow Windows development example. See [EngineModules.md](docs/EngineModules.md) for exact supported contracts, commands, evidence and limits.

Component.hpp supplies the abstract Component parent, virtual type/size access and synchronous field copying through `RAVEN_COMPONENT(FullyQualifiedType, Fields...);`. Registry accepts Component references and retains only approved copied values, excluding virtual-table pointers and borrowed views. Custom value structs require an explicit ComponentValue declaration; empty/tag components are supported. Transform, StaticMesh, Sprite, Position and Velocity inherit this parent without handwritten name strings or payload wrappers. Cameras and animation/input bookkeeping remain local sample members; F1 edits Sprite ordering in Registry. OrthographicSample owns five Transform/Sprite entities and gathers matching runtime entities; PerspectiveSample owns three Transform/StaticMesh entities. General scenes and queries remain pending.

1. [x] Retain host-owned GameInstance/Registry data across game-code replacement, without a singleton. Scene ownership is a later extension.
2. [x] Build, load, validate and replace a separate Windows game DLL using execution copies while keeping RavenCore, Registry and Entity handles alive. Replacement occurs between synchronous game calls.
3. [x] Connect minimal component add/read/write/remove and destruction cleanup. Repeated replacements preserve values while changed code updates them. General query/storage design remains open.
4. [x] Add source-content polling and background builds for the supported sample dependencies. Apply successful stable builds; retain active code on build/contract failure. Generated contracts require a host rebuild/restart for shared-definition/build changes, without manual layout counters. Extending the dependency set requires extending these checks.

First end-to-end completion: replace gameplay code during one running session, preserve Registry/Entity identity and existing component values, observe the new update behavior, and keep the previous game active on build or pre-activation validation failure. A DLL with an exported empty function alone does not satisfy this criterion. Step 4 then automates that demonstrated behavior.

Initial state preservation covers compatible logic edits. Generated shared-definition/build fingerprints conservatively reject unsupported contracts until a host rebuild/restart; there is no automatic state reset or C++ layout migration. Watcher events, source extensions and equal `sizeof` are not compatibility proofs. New code can still contain execution errors, so retaining old code on a failed build does not guarantee recovery from a native crash after activation.

Reuse the existing application loop, host-side client, public input/frame data, renderer, and asset cache. Keep persistent storage and its cleanup independent of unloaded game-module code. Preserve renderer/cache initialization across code swaps instead of recreating sample clients and GPU resources for every reload. Extend existing files where responsibilities remain cohesive; separate host module management from reloadable game implementation and put native loading/watching behind platform backends. Balance responsibilities and lifetimes rather than targeting a particular file count.

Verification after removing the sample-only component layer is complete: Debug/Release builds, 137 component/reload checks, 50 frames of each sample and 100 changed-DLL rendering frames pass per configuration. See [EngineModules.md](docs/EngineModules.md) for evidence and coverage limits.

Current component milestone: SpriteComponent and component-driven Orthographic rendering are implemented on the common virtual parent. Runtime creation, transform/fade/texture/blend/order edits, component/entity removal, slot reuse and shared resources pass focused Debug/Release GPU checks. The existing DLL still drives visible movement through a temporary Position-delta bridge, including after compatible replacement. Registry retains values rather than live objects; entity removal does not invoke a component destructor. CameraComponent and direct game-code migration to Transform remain next. See [EngineModules.md](docs/EngineModules.md) for evidence and limits. Scene ownership is not an immediate dependency. Later work includes 3.2, playable 2D (4), imported/lit 3D gameplay (5), shader/asset reload (6), and broader game-code reload coverage (7). Platform work proceeds in parallel.

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

### 3.1 Minimal ECS and component-driven rendering

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
- [ ] Add CameraComponent with explicit active-camera selection and one authoritative pose.
- [ ] Define safe entity/component changes during updates using the existing snapshot/copy API; expand storage/queries only when the concrete rendering path requires it.
- [ ] Validate entity/component lifetime and iteration behavior with focused checks; revisit the CTest deferral before deciding how these checks are retained.
- [ ] Add a minimal scene owner when grouping and lifetime requirements justify it. Deferred; it does not block component integration.

Completion of the component/rendering work: the orthographic sample renders from Registry components using an explicitly selected CameraComponent, rejects stale camera/entity references, and creates/removes sprites while preserving texture reuse, ordering and compatible game-code replacement. Game movement updates Transform directly after the planned migration. Reuse one shared quad and the existing catalog/cache; per-entity GPU allocation is unnecessary. Scene ownership remains deferred. File-based scene authoring, hierarchy, scene switching and resource release follow in 3.2.

#### Implemented StaticMesh milestone and remaining component plan

TransformComponent and StaticMeshComponent now live in `Public/Raven/ECS/RenderComponents.hpp`. PerspectiveSample owns three mesh entities, shares one GPU mesh and assembles existing draw submissions from Registry copies. MeshHandle identifies its renderer and unrecycled slot; deleting an entity does not delete shared geometry. Static geometry can have animated transforms. This first path supports unlit colored meshes, not imported/textured mesh materials. See [EngineModules.md](docs/EngineModules.md) for validation and resource-lifetime limits.

The component foundation uses a virtual parent with generated type/size/copy boilerplate and an explicit retained-value policy. SpriteComponent now extends that pattern in the same shared header. OrthographicSample owns five entities and dynamically gathers every matching Transform/Sprite pair using its retained quad/catalog/cache. Transform provides pose/scale; Sprite provides appearance and transparent ordering. Invalid/null texture references and unknown blend modes are skipped; non-null unregistered references and load failures use the asset service's exception path. Removing a component/entity removes its draw. Cleanup and constructor rollback affect only the sample's own entity handles, including after slot reuse.

The remaining component plan is:

1. Add CameraComponent to the existing shared header, initially wrapping OrthographicCamera. Select the active camera explicitly and keep one source for its pose.
2. Write input-driven camera edits to that component and render using its retained values. Keep input bookkeeping local.
3. When game-code integration resumes, replace Position with Transform.Position in game updates and retain Velocity. Remove legacy Position initialization, SyncLegacyMovement and its previous-position bookkeeping. Until then the bridge applies only legacy movement deltas, preserving direct Transform edits and the existing --game behavior.
4. Reuse GetEntities snapshots/Read copies, checked mutations and stale-handle rejection. A missing/stale active camera should report an error through the sample's existing exception path. Add general queries only when this path needs them.
5. Extend the existing temporary probes only for the changed behavior. CTest integration remains deferred unless separately agreed.

#### Next implementation target: CameraComponent

Status: planned; SpriteComponent is implemented and verified. CameraComponent moves authoritative camera state into the same Registry workflow as sprites, enabling camera edits and explicit camera switching while preparing for future editor/play sessions.

- Add CameraComponent beside the existing render components, initially holding the existing OrthographicCamera value. Review its self-contained members and explicitly approve that value through ComponentValue; use the shared component macro.
- Keep position/target/up, zoom and clipping settings in that one camera value. Do not duplicate pose in a Transform or retain a second authoritative camera in OrthographicSample.
- Keep an explicit active-camera Entity handle in the sample. Select the camera by handle; avoid a global camera or competing per-camera active flags.
- Read/modify/write the selected component for pan, zoom and Home reset; read a fresh copy for drawing. Preserve the existing input behavior, sprites, shared quad and texture cache.
- Track ownership separately from selection. The sample cleans up the camera it creates; selecting another caller's camera does not transfer ownership. Roll back owned entities if initialization fails.
- Reject a missing Camera component or null/foreign/stale active handle through the existing exception path. Never silently substitute a local camera or select a recycled entity slot.

Completion: Registry camera edits change the rendered view; explicit switching between two valid camera entities works; controls retain their behavior; stale selection fails clearly; cleanup preserves caller-owned cameras. Use focused component/native checks for this behavior. CameraComponent remains unchecked until implemented and demonstrated.

After this task, migrate existing game movement directly to Transform and remove the temporary sprite movement bridge. Perspective camera integration, particle/VFX systems, general queries and Scene ownership remain separate work.

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

Initial module build/load/replacement and minimal ECS state preservation begin in the immediate joint milestone, before the full scene/game framework. This section also tracks the remaining reload scope and final validation.

- [x] Establish the initial engine/game update contract, retained ownership, matching-toolchain constraints and generated compatibility rejection. Direct implementation was authorized by the owner on 2026-10-19.
- [x] Build a standalone Windows game DLL and native loading/unloading backend; other OS backends remain in platform completion.
- [x] Define an update-only module lifecycle with host-owned initialization/cleanup and no retained module callbacks, objects or threads.
- [x] Poll/build changed game code and reload it while preserving compatible minimal component state on Windows.
- [x] Reject failed builds, invalid modules, or incompatible contracts while retaining the active game code on Windows.
- [ ] Extend the current rejection/manual-restart policy when supporting broader game state or migration. Initial repeated replacement and shutdown are already verified; arbitrary C++ migration is outside this baseline.

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

The joint ECS/reload boundary and Transform/StaticMesh/Sprite rendering are implemented alongside the renderer, asset/cache and entity-lifecycle foundations. CameraComponent is next, followed by direct game-code migration to Transform and removal of the legacy movement bridge. The mesh path's reload integration remains pending, and Scene ownership is deferred. Keep the proven retained-state boundary while evaluating query/storage choices. CTest integration remains deferred; choose a retained regression-check approach with the owner rather than silently adding a test framework.
