# RavenEngine v1 TODO

The goal follows [ROADMAP.md](ROADMAP.md): a C++23 engine that can build and run small 2D and 3D games using Vulkan, ECS, and reloadable game code, shaders, and assets. ECS and hot reload are joint design goals. Fast iteration means reducing the time from a developer's edit to observing the result. Games are authored through code and data files in v1; a complete visual editor remains outside v1, but ownership and execution boundaries must accommodate future editor editing and play sessions. This is the working implementation order; the roadmap summarizes the wider scope. Write and maintain project documentation in English.

Work on Windows and shared engine code first. macOS and Linux contributors can complete their backends and native verification in parallel. Final platform completion must exercise the game and reload behavior once it exists. Keep shared contracts independent of operating-system headers.

Perspective and orthographic rendering use the agreed Blender world axes and shared transform/view math. Entity lifecycle, the narrow joint ECS/game-module reload example, and reusable Transform/StaticMesh/Sprite components are implemented. Perspective colored geometry and Orthographic textured sprites render from GameInstance/Registry with shared resources. ViewFromPoseRH is present. Concrete components have separate headers under Public/Raven/Components, while ECS infrastructure stays under Public/Raven/ECS. CameraComponent retains projection settings, and the renderer accepts a temporary CameraView at the prepared swapchain extent. Both samples still use their local cameras. The next change is to connect one ECS camera to OrthographicSample. The existing DLL remains compatible through a temporary sample-only movement bridge until that replacement path works. Scene ownership is deferred at the owner's request. Preserve the renderer/cache and retained-state boundary before selecting general query/storage optimizations. Blender/FBX remains the planned 3D asset direction.

Engine implementation uses small, reviewable snippets suggested by the assistant and applied by the owner. As clarified on 2026-11-01, the earlier general direct-edit authorization no longer applies to engine implementation: edit engine files only when the owner explicitly requests that specific change. Explicit CMake requests and the requested camera rollback do not authorize other engine edits. Check an item only after its completion criteria are demonstrated.

## Evaluation and agreed direction as of 2026-11-12

Planning follows the [shared Windows assessment and camera implementation record](https://chatgpt.com/share/6ae34d47-0d78-83e8-914c-7070fe80712d). The current projection component and renderer handoff have focused Windows Debug evidence. Explicit sample camera selection and game-module camera integration remain open. macOS/Linux implementation and native verification remain assigned to other contributors.

Next Windows completion criterion: run an interactive sample whose reloadable game module directly edits ECS transforms and camera settings, whose selected ECS camera drives either projection, and whose entities, values and camera selection survive compatible code replacement while renderer/cache resources remain stable. Complete the six camera/integration steps in 3.1 before expanding unrelated subsystems.

The Windows rendering and common asset/cache foundations remain implemented. The joint ECS/reload example now adds a host-owned GameInstance, minimal copied component values, a standalone RavenGame DLL, execution-copy replacement, conservative generated compatibility checks, source-content polling and background builds. Compatible logic changes retain Registry/Entity identity and component values; build/validation failures preserve the active code. General ECS queries/systems, scene ownership/persistence, runtime resource handles, imported meshes/lighting, physics, audio and playable games remain pending. The one moving ECS-backed sprite is a boundary demonstration, not the complete scene milestone.

Current Windows Debug/Release builds and joint ECS/reload evidence are recorded in [EngineModules.md](docs/EngineModules.md), separately from the historical renderer checks below. There are no registered CTest tests; the owner's deferral remains in force. Focused probes under ignored `out/validation/game-reload/` are evidence, not a tracked regression suite available from a clean checkout. These samples are demonstrations, not playable games.

The indexed triangle milestone is implemented and verified. Debug and Release each passed 243 indexed draws and presentations, including resize, minimize/restore, animation, and construction-failure cleanup. Debug reported no core or synchronization validation warnings/errors. Evidence: `out/validation/indexed-debug.log`, `indexed-release.log`, and `out/screenshots/ravenengine-indexed-triangle.jpg`.

The input baseline already includes keyboard transitions/events, modifiers, mouse buttons/movement, and vertical/horizontal wheel handling. Finish the remaining behavior when needed by samples; do not repeat completed input work.

Perspective validation: Debug/Release builds and shader validation pass; a temporary probe passes 59 math/projection checks. Computer use confirms animation, horizontal/vertical camera orbit, wheel zoom, Home reset, maximized/portrait proportions, minimize/restore, and normal shutdown. Debug has no core/synchronization validation diagnostics; Release also launches from `out/`. Evidence: `out/validation/perspective-*.log` and `out/screenshots/ravenengine-perspective.jpg`. No test sources/framework were added; CTest wiring remains pending.

Later rendering and asset validation is recorded in [TextureResources.md](docs/TextureResources.md), [CoordinateSystem.md](docs/CoordinateSystem.md), and [AssetSystem.md](docs/AssetSystem.md). The asset milestone records 3,417 public-client checks and 25 frames per Windows configuration; batching records matching GPU captures and preserved ordering. Those records do not establish current macOS/Linux support.

## Joint ECS and game-code reload baseline

Status: implemented for the narrow Windows development example. See [EngineModules.md](docs/EngineModules.md) for exact supported contracts, commands, evidence and limits.

Component.hpp supplies the abstract Component parent, virtual type/size access and synchronous field copying through `RAVEN_COMPONENT(FullyQualifiedType, Fields...);`. Registry accepts Component references and retains only approved copied values, excluding virtual-table pointers and borrowed views. Custom value structs require an explicit ComponentValue declaration; empty/tag components are supported. Transform, StaticMesh, Sprite, Camera, Position and Velocity inherit this parent without handwritten name strings or payload wrappers. Samples still use local renderer camera values pending ECS integration; animation/input bookkeeping remains local; F1 edits Sprite ordering in Registry. OrthographicSample owns five Transform/Sprite entities and gathers matching runtime entities; PerspectiveSample owns three Transform/StaticMesh entities. General scenes and queries remain pending.

1. [x] Retain host-owned GameInstance/Registry data across game-code replacement, without a singleton. Scene ownership is a later extension.
2. [x] Build, load, validate and replace a separate Windows game DLL using execution copies while keeping RavenCore, Registry and Entity handles alive. Replacement occurs between synchronous game calls.
3. [x] Connect minimal component add/read/write/remove and destruction cleanup. Repeated replacements preserve values while changed code updates them. General query/storage design remains open.
4. [x] Add source-content polling and background builds for the supported sample dependencies. Apply successful stable builds; retain active code on build/contract failure. Generated contracts require a host rebuild/restart for shared-definition/build changes, without manual layout counters. Extending the dependency set requires extending these checks.

First end-to-end completion: replace gameplay code during one running session, preserve Registry/Entity identity and existing component values, observe the new update behavior, and keep the previous game active on build or pre-activation validation failure. A DLL with an exported empty function alone does not satisfy this criterion. Step 4 then automates that demonstrated behavior.

Initial state preservation covers compatible logic edits. Generated shared-definition/build fingerprints conservatively reject unsupported contracts until a host rebuild/restart; there is no automatic state reset or C++ layout migration. Watcher events, source extensions and equal `sizeof` are not compatibility proofs. New code can still contain execution errors, so retaining old code on a failed build does not guarantee recovery from a native crash after activation.

Reuse the existing application loop, host-side client, public input/frame data, renderer, and asset cache. Keep persistent storage and its cleanup independent of unloaded game-module code. Preserve renderer/cache initialization across code swaps instead of recreating sample clients and GPU resources for every reload. Extend existing files where responsibilities remain cohesive; separate host module management from reloadable game implementation and put native loading/watching behind platform backends. Balance responsibilities and lifetimes rather than targeting a particular file count.

Verification after removing the sample-only component layer is complete: Debug/Release builds, 137 component/reload checks, 50 frames of each sample and 100 changed-DLL rendering frames pass per configuration. See [EngineModules.md](docs/EngineModules.md) for evidence and coverage limits.

Current component milestone: SpriteComponent and component-driven Orthographic rendering are implemented on the common virtual parent. Runtime creation, transform/fade/texture/blend/order edits, component/entity removal, slot reuse and shared resources pass focused Debug/Release GPU checks. The existing DLL still drives visible movement through a temporary Position-delta bridge, including after compatible replacement. Registry retains values rather than live objects; entity removal does not invoke a component destructor. Camera pose math, projection settings and the renderer handoff are implemented. Selected-camera rendering in the samples, direct Transform game-code integration and a free-fly controller remain planned. See [EngineModules.md](docs/EngineModules.md) for evidence and limits. Scene ownership is not an immediate dependency. Later work includes 3.2, playable 2D (4), imported/lit 3D gameplay (5), shader/asset reload (6), and broader game-code reload coverage (7). Platform work proceeds in parallel.

## Verified foundation

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
- [x] Separate concrete component headers under Public/Raven/Components, retain Component/Entity/Registry under ECS, and reserve a CameraComponent header without implementation.
- [ ] Complete the six-step ECS camera/integration milestone below: one Transform per entity, projection settings in CameraComponent, shared camera preparation, both samples, direct game-module edits and free-fly control.
- [ ] Define safe entity/component changes during updates using the existing snapshot/copy API; expand storage/queries only when the concrete rendering path requires it.
- [ ] Validate entity/component lifetime and iteration behavior with focused checks; revisit the CTest deferral before deciding how these checks are retained.
- [ ] Add a minimal scene owner when grouping and lifetime requirements justify it. Deferred; it does not block component integration.

Completion of the component/rendering work: both samples render through explicitly selected Transform/CameraComponent entities, support perspective and orthographic camera settings, and handle invalid/stale selection as defined below. Reloadable game code directly controls transforms and camera settings for the mesh/sprite paths, including a useful perspective controller. Repeated compatible replacement preserves entity identity, component values and camera selection; failed builds/rejected replacements retain active code. Preserve sprite ordering, one shared quad, shared meshes and the existing catalog/cache across updates and reloads. Scene ownership remains deferred. File-based scene authoring, hierarchy, scene switching and resource release follow in 3.2.

#### Implemented StaticMesh milestone and remaining component plan

TransformComponent and StaticMeshComponent live in separate headers under `Public/Raven/Components/`. PerspectiveSample owns three mesh entities, shares one GPU mesh and assembles existing draw submissions from Registry copies. MeshHandle identifies its renderer and unrecycled slot; deleting an entity does not delete shared geometry. Static geometry can have animated transforms. This first path supports unlit colored meshes, not imported/textured mesh materials. See [EngineModules.md](docs/EngineModules.md) for validation and resource-lifetime limits.

The component foundation uses a virtual parent with generated type/size/copy boilerplate and an explicit retained-value policy. SpriteComponent extends that pattern in its own `Public/Raven/Components/SpriteComponent.hpp` header, alongside SpriteBlendMode. OrthographicSample owns five entities and dynamically gathers every matching Transform/Sprite pair using its retained quad/catalog/cache. Transform provides pose/scale; Sprite provides appearance and transparent ordering. Invalid/null texture references and unknown blend modes are skipped; non-null unregistered references and load failures use the asset service's exception path. Removing a component/entity removes its draw. Cleanup and constructor rollback affect only the sample's own entity handles, including after slot reuse.

#### Next implementation target: CameraComponent

Status: camera pose math, CameraComponent projection settings and the VulkanContext camera handoff are implemented. Both samples still use their local renderer camera values. The complete milestone remains **ECS camera rendering with perspective/orthographic support and retained state across compatible game-code reload**.

**Next change: migrate OrthographicSample to one sample-owned ECS camera.** Its fixed orientation makes this a smaller first integration than migrating the Perspective orbit controller. See step 3.

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
- Give each controlled entity one designated movement/controller update in this milestone. Game/controller updates write retained state before camera preparation and draw gathering; both gathering and preparation read it. PerspectiveSample::UpdateDraws currently writes mesh rotations: move that animation to the update phase and disable/replace it for module-controlled entities. Do not let the host sample controller run over the module's camera edits later in OnFrame. Controllers read current Transform/settings; local yaw/pitch/distance caches must not overwrite external edits after an idle frame, camera switch or reload. Keep only input bookkeeping or distinct controller intent such as an orbit pivot; resynchronize any derived caches on handoff.
- Selection does not transfer ownership. Samples clean up only their own entities and roll them back on initialization failure. Route input to an explicit camera entity through a narrow host binding available to the synchronous game update; reuse the selected handle when controlling that view. A controller may deliberately target another camera, but must not invent a second active-camera flag, scan for the first camera, or retain module-static selection/pose.
- Use the existing Component parent, reviewed ComponentValue fields and RAVEN_COMPONENT macro for every retained field. Reuse GetEntities snapshots, Read copies and checked writes; add only access/iteration needed by this example. Keep derived caches temporary and introduce persistent matrix caching only when measurement justifies it.
- Define recoverable view failure: a missing Transform/Camera component, null/foreign/stale selected entity or invalid active camera settings/pose skips that view's world rendering and reports a useful diagnostic. Do not select an arbitrary entity or a recycled slot. A zero-sized/minimized viewport skips camera projection and world rendering until drawable again.
- Initialize entities/default settings separately from game updates. Compatible replacement preserves values unless gameplay deliberately changes them. Adding/changing shared camera or update-binding definitions requires a host rebuild/restart through the generated compatibility contract; routine compatible game-code edits then use runtime reload without manual layout counters.

1. [x] **Confirm and reuse the camera math contract.** Reuse the existing right-handed Z-up world, column-vector math, Vulkan depth [0, 1] and positive-height viewport Y convention. Preserve the existing projection rules: finite values, perspective 0 < vertical FOV < pi and 0 < near < far; orthographic height > 0 and 0 <= near < far. The Transform-to-camera view conversion uses the basis and scale rules above and shares the existing projection functions.

   Completion: known points, translation/rotation, clipping and portrait/landscape aspect work in both modes; camera scale does not change the view, including zero/negative scale. Invalid pose/active projection values and degenerate inputs follow documented behavior.

2. [x] **Add CameraComponent projection settings.** The component registers its mode, both size controls and shared clipping distances through RAVEN_COMPONENT. `GetCameraView` combines the active projection with the sibling Transform pose. The renderer receives the resulting matrices as a temporary CameraView.

   Verified on Windows Debug: Registry Add/Read/Write retains both size controls across a mode switch. A mesh and camera share one Transform, and duplicate Transform addition is rejected. Both projections map near/far to Vulkan depth 0/1, and invalid active settings, invalid poses and unknown modes are rejected. See the [current camera evidence](docs/EngineModules.md#camera-pose-projection-and-renderer-handoff).

3. [ ] **Connect a selected ECS camera to rendering.** Resolve the selected entity through its Registry and prepare the camera from the actual drawable viewport.

   - [x] Route both existing camera types and the public CameraViewBuilder through `VulkanContext::Impl::DrawFrameWithView`. The renderer supplies the prepared swapchain extent and computes `Projection * View` once. An empty view skips drawing. A temporary public client submitted a Registry-backed view through this path.
   - [ ] **Next: migrate OrthographicSample.** Create one sample-owned entity with TransformComponent and an orthographic CameraComponent, using the current camera defaults. Use its entity handle as the selected camera and destroy it with the sample, including on constructor rollback. Route the sample through CameraViewBuilder and call `GetCameraView` with the supplied aspect. Move pan, zoom and Home reset to component writes and retire the local `m_Camera` in the same change. Preserve F1 ordering and the existing game movement bridge. Return an empty view and report a diagnostic for a missing, stale or invalid camera.
   - [ ] Exercise explicit selection between cameras and recover by selecting a valid camera. Keep selection separate from ownership. Reuse the selected entity when introducing the game controller binding in step 5.

   Completion: switching between valid cameras changes the view; resize/minimize/restore uses matching projection/submission dimensions; camera preparation and gathering do not mutate Registry values. Missing components and null/foreign/destroyed/reused entity handles are diagnosed. Selecting another caller's camera does not make it sample-owned, and selecting a valid camera restores rendering.

4. [ ] **Migrate PerspectiveSample and add snapshot-based helpers.** Reuse the Orthographic camera handoff and retire the local Perspective `m_Camera`. Preserve orbit, zoom and reset using current component values and explicit controller ownership. Move mesh animation out of draw gathering. Add world-to-screen, screen-to-world and picking-ray helpers consuming the same camera snapshot and viewport, with explicit pixel origin and depth conventions.

   Completion: both samples retain expected images, depth, transparency and controls in Debug/Release; camera edits survive an idle manual/free-fly frame and switching cameras does not apply stale controller pose. Both projections pass screen/world round trips and picking-ray checks. Preserve shared geometry, the sprite quad and cached textures without recreating renderer/cache resources.

5. [ ] **Complete direct game-module integration.** Let reloadable code edit Transform and CameraComponent directly through explicit entity bindings while retaining Velocity where needed. Disable/replace host animation and controls for module-controlled entities so the subsequent OnFrame only prepares/gathers their values. Once the replacement works, remove the legacy SampleGame::Position definition, initialization, SyncLegacyMovement and previous-position bookkeeping. Keep --game usable during migration; do not reapply defaults on update/reload.

   Completion: module transform/camera edits survive the following host frame, including rotation; repeated compatible replacements preserve Registry/Entity identity, values and host camera selection. Failed builds and incompatible/pre-activation-invalid replacements retain active code; creation/removal, cleanup and shutdown preserve resource stability.

6. [ ] **Add one useful perspective controller.** Implement free-fly movement and mouse look through the existing input/frame path, with frame-rate-independent movement and controller behavior in reloadable game code. Keep movement separate from projection settings; distinguish FOV zoom from camera translation.

   Completion: navigate the perspective sample at different frame rates and after pause/minimize/restore, then change controller behavior through compatible game-code reload while preserving the current camera/world state.

Validation and workflow gate:

- [ ] Extend focused checks under ignored out/ only for the changed contracts, with recorded commands/inputs so the integration checks can be rerun. Exercise shared Transform composition, camera scale, read-only gathering, controller handoff/idle edits, matching viewport/matrix snapshots, projection helpers, camera/entity lifetime, repeated reload, retained values, failed builds/rejected replacements, resource counts/reuse and shutdown alongside native Windows Debug/Release rendering checks.
- [ ] Record edit-to-visible-change time through source polling, background build, validation/replacement and the next rendered result. Document which game-code edits reload and which shared-definition/build changes require a host rebuild/restart.
- [ ] Record the new milestone's evidence separately from historical renderer/reload checks. CTest remains deferred unless separately agreed; ignored probes do not become a tracked clean-checkout regression suite. Shader/asset runtime reload remains milestone 6 and is not completed by game-DLL reload.

After this foundation, add reusable orbit/follow controllers, smoothing, shake and camera transitions/blending when gameplay needs them. Visibility layers, simultaneous views and render-to-texture follow renderer support; physical lens/exposure/depth-of-field/jitter follow their consuming systems. A complete editor, arbitrary C++ state migration, generalized plugin loading, broad query optimization, particle/VFX systems and extensive new rendering features stay outside this milestone. Scene ownership remains deferred until a concrete reset/session/world-isolation requirement defines its responsibilities.

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
- [ ] Consolidate window dimensions around `GetFramebufferState()` and review the standalone `GetWidth()`/`GetHeight()` accessors across backends.
- [ ] Have swapchain preparation report whether the current framebuffer is usable, so frame drawing can use one readiness check while preserving minimized-window behavior.
- [ ] Keep mesh handle creation and lookup beside the mesh storage in `VulkanContext::Impl`.
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

The joint ECS/reload boundary and Transform/StaticMesh/Sprite rendering are implemented. The camera component and renderer handoff are ready for the Orthographic sample migration, followed by Perspective, direct game-module edits and a reloadable free-fly controller. Keep the legacy movement bridge until its replacement works. Preserve retained state and renderer/cache resources, and measure the edit/build/observe loop before broadening scope.
