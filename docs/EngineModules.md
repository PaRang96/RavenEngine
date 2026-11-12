# Engine and sample modules

The Windows build separates reusable engine code from sample code:

| Target | Output | Responsibility |
| --- | --- | --- |
| `RavenCore` | `RavenCore.dll` and import library `RavenCore.lib` | Application loop, platform backend, input, logging, and Vulkan resource ownership |
| `RavenEngine` | `RavenEngine.exe` | Entry point, window description, perspective/orthographic samples, camera controls, and animation |
| `RavenGame` | `RavenGame.dll` | Replaceable sample movement code; standalone child build links the completed engine import library |
| `RavenShaders` | Validated SPIR-V under `Shaders/<config>/` | Build dependency of `RavenCore` |
| `RavenContent` | Runtime copies under `Content/` beside the sample | Stages validated shaders and sample PNGs before the engine/sample build |

The DLL and executable are emitted together under `out/build/x64-debug/RavenEngine/` or `out/build/x64-release/RavenEngine/`. Existing executable paths and launch configurations remain valid. The `.lib` is an import library used when linking the sample; the engine implementation remains in the DLL, which is required at runtime. Shader lookup now uses executable-relative `Content/Shaders`; see [RuntimeUtilities.md](RuntimeUtilities.md) for relocation and content-root overrides.

## CMake layout

The build entry points keep target names, output directories and preset commands stable. Build rules are grouped by responsibility:

| File | Responsibility |
| --- | --- |
| `CMakeLists.txt` | Project, host debugger policy, tooling, Vulkan discovery and loading shared defaults |
| `cmake/RavenBuildSettings.cmake` | Shared language/runtime/export defaults and the optional private standard-library PCH helper |
| `RavenEngine/CMakeLists.txt` | RavenCore/RavenEngine target setup, interfaces, compile definitions and module ordering |
| `cmake/RavenSourceDiscovery.cmake` | Cached folder discovery shared by enrollment and game compatibility/build checks |
| `cmake/RavenSources.cmake` | Assign discovered public/common/sample files to their owning target |
| `cmake/RavenGameSources.cmake` | Game folder discovery shared by compilation and content-based rebuild checks |
| `cmake/RavenPlatform.cmake` | Select the native folder from the shared discovery result; configure platform dependencies/settings |
| `cmake/RavenContent.cmake` | Single registrations for shaders/runtime files; derives source entries, compilation, validation, outputs, dependencies and staging |
| `cmake/RavenBuildEnvironment.cmake` | Capture selected compiler/SDK environment variables once at host configuration |
| `cmake/RavenGameModule.cmake` | Optional standalone game build, host contract, generated environment and build configuration |
| `RavenEngine/Private/Game/CMakeLists.txt` | Independent game-DLL project linking the completed engine import library |

These included modules configure the existing targets in the engine source/binary directory scope. They do not introduce extra libraries or move build outputs. Shader outputs and staging lists are generated from registrations. Presets inherit common output paths, platform toolchains, architecture and Debug/Release settings; unused C compiler entries are removed from the C++ projects.

Keep shared build helpers under `cmake/`: `GameContract.cmake` fingerprints its `.cmake` and `.in` files, and the runtime source poller watches that directory. Extracted rules therefore remain covered by the rebuild/restart contract. The standalone project and generated `GameBuild/BuildGame.cmake` continue to avoid rebuilding the loaded host; use the generated script during a running session.

### Automatic C++ enrollment - 2026-11-01

Ordinary file additions/removals require no CMake source-list edit. Public .hpp headers belong to RavenCore. Common Private .cpp/.hpp files belong to RavenCore; Private/Samples belongs to RavenEngine; Private/Game belongs only to the standalone RavenGame project. Shared files directly under Private/Platform are included once, while RavenPlatform selects exactly one native subfolder. Mac .mm files are included only with that backend. Legacy .h sample headers are also discovered. Follow the repository's .cpp/.hpp naming convention for new code.

RavenSourceDiscovery scans each folder once per CMake invocation and caches that result only for that invocation. The host reuses its Private scan for common code, sample code and native backend selection, and reuses its Public scan for the compatibility contract. Generated out/, Content and ThirdParty trees are outside C++ source discovery. Shader profiles/entry points, platform dependencies and runtime staging retain explicit registrations because those files need additional build metadata.

CONFIGURE_DEPENDS checks filename changes at build time and regenerates the build system when needed. This intentionally uses scoped discovery for the current Ninja workflow; CMake documents its per-build check cost and generator limitations in the [file command reference](https://cmake.org/cmake/help/v3.24/command/file.html#filesystem). Ordinary source-body edits continue through compiler dependency tracking. Public/game header and build-rule changes remain covered by the conservative host-rebuild/restart contract.

### Where to make a build change

| Change | Edit once |
| --- | --- |
| Add an engine/sample C++ file or header | Place it under the owning Public/Private folder; `cmake/RavenSources.cmake` discovers it automatically |
| Add a native backend file | Place it in Private/Platform/Windows, Mac or Linux; only the selected folder is compiled |
| Add a platform dependency | Configure it once in `cmake/RavenPlatform.cmake` |
| Add a game C++ file or header | Place it in Private/Game; the standalone target and build-input check use the same discovered set |
| Add a shader | Add one `raven_add_shader(source, profile, entry)` call in `cmake/RavenContent.cmake` |
| Add a runtime file | Add one `raven_stage_file(project_relative_path)` call in `cmake/RavenContent.cmake`; supply an optional destination for an alias |
| Change language/CRT/export defaults | Change `cmake/RavenBuildSettings.cmake`; both projects include it |
| Disable engine/sample PCH | Configure with `-DRAVEN_USE_PCH=OFF`; the game target does not use PCH |
| Change common build paths/toolchain/profile settings | Change the corresponding hidden base in `CMakePresets.json` |

A shader registration adds the source to the engine target, marks it as a source-only HLSL file, creates its compile/validation rule and derives its SPIR-V/staged output name. Runtime-file registration supplies both dependencies and source/destination lists for the generic StageContent template; that template contains no asset names. One generated staging script restores missing runtime shaders, textures and licenses, while identical files retain their timestamps.

The game project remains an independent build entry point so runtime builds cannot rebuild the loaded host. It shares its defaults and source discovery instead of maintaining another file list. Each game source has its own content-derived command key: an equal-timestamp body edit changes that source's command without changing an unchanged sibling's command. Compatibility fingerprints include the configured language standard, compiler option and runtime settings.

## Automatic enrollment and verified build requests

Automatic .cpp/.hpp enrollment remains a workflow requirement. Add code under its owning engine/sample/game/native folder and build; do not maintain parallel per-file source lists. Compiler settings, shader entry-point metadata and asset staging remain centralized in their existing helpers.

The generated GameBuild/BuildGame.cmake is the shared entry point for the full-build game target, manual requests and runtime source watching. It acquires a process-scoped lock before inspecting the child cache or build record. A second request waits, then rechecks the completed result; it does not configure or compile concurrently in the same child directory.

A successful host configuration records only selected compiler/SDK environment variables in GameBuild/BuildEnvironment.cmake. The game-build process restores that snapshot when compilation is required. Runtime rebuilds therefore work without inheriting a Visual Studio developer shell, and the engine process's own environment is unchanged. Configure the full host through CMake Tools or a Visual Studio developer environment; reconfigure after changing the selected toolchain.

The standalone target generates GameOutputs.txt from CMake target metadata: its runtime DLL and, in Debug/RelWithDebInfo, its linker PDB. The last completed build record contains hashes of inputs, child cache and expected artifact contents. Reuse requires all three to match. Missing/corrupted artifacts trigger a clean rebuild of only the standalone child. The PDB is included in that target's additional clean outputs because ordinary Ninja clean does not remove it automatically. No host binary has a build/clean edge in the child.

The previous completed record stays intact until a replacement succeeds and all expected artifacts exist. It cannot certify a failed edit because the input/cache/output hashes must still match. This preserves the previous valid DLL and avoids cleaning unchanged sibling sources when recovering from a compiler error. New records are written to a temporary file and renamed under the build lock.

For explicit child-only recovery, run from the repository root:

```powershell
cmake -DRAVEN_GAME_FORCE_REBUILD=ON -P out/build/x64-debug/RavenEngine/GameBuild/BuildGame.cmake
```

VS Code's default build task now delegates to CMake Tools and the active build preset. It no longer initializes a hardcoded Visual Studio installation or unconditionally configures before every build. The debug configuration resolves the active launch target with cmake.getLaunchTargetPath, which performs no additional build, and uses that one build task as its pre-launch step. Shared-definition changes still require the existing host rebuild/restart contract; use only the game script while the host is running.

Build-workflow hardening validation on 2026-11-01: production Windows Debug/Release builds pass. Isolated Debug and Release runners pass 54 and 50 focused checks respectively, covering automatic source/header enrollment and removal, DLL/PDB/manifest repair, explicit recovery, transactional compile-failure recovery, equal-timestamp edits and two concurrent requests. Both concurrent callers succeed and only one performs compilation. Native automatic-watcher probes also pass in both configurations with developer include/library variables removed. Every standalone request preserves host DLL/executable hashes and timestamps. The editor task was validated against the installed CMake Tools task schema; its non-building launch-path command is present. The current 40 production translation units and all production C++/header contents remain unchanged.

The verified production game-only fast path, with no developer-shell inheritance, has local medians of 0.045 seconds in Debug and 0.038 seconds in Release (seven requests each). D: is exFAT; one fixture produced a conservative timestamp-driven sibling recompile despite an unchanged compiler command. The checks distinguish command reuse from that filesystem effect, and content-derived keys still detect equal-timestamp edits. This storage limitation can add incremental work; it does not require manual source enrollment.

Retained evidence is under ignored `out/validation/build-workflow/`: results.json, results-release.json, production-timings.json, watcher-run.log, release-watcher-run.log and the production build logs. Cleanup removes temporary source snapshots, probe projects, runners and generated validation builds; historical logs and result summaries remain. These are focused local probes, not a tracked regression suite. CTest remains deferred; native macOS/Linux and an interactive F5/debugger session were not rerun for this build-tooling change.

## Standard-library precompiled headers - 2026-11-01

RAVEN_USE_PCH defaults to ON for RavenCore and RavenEngine. The shared helper precompiles stable standard-library headers privately; it contains no project headers. Editing a component or engine header therefore does not invalidate the PCH itself. Each target builds its own artifact because export/compile definitions differ, as required by [CMake's PCH reuse rules](https://cmake.org/cmake/help/v3.24/command/target_precompile_headers.html#reusing-precompile-headers). The shared defaults use C++23 for the engine, sample and game DLL. MSVC uses its C++23 mode, currently the preview flag on MSVC 19.51. The macOS backend also uses C++23 for Cocoa Objective-C++ files, with a mapping for older CMake versions. Reconfigure, rebuild and restart the host after a language mode change.

The standalone game target keeps ordinary per-source compilation and its content-derived command keys. There is no extra PCH build on the game-only reload path. Configure with -DRAVEN_USE_PCH=OFF to verify ordinary includes or compare performance.

## Build iteration performance

### Folder enrollment and PCH measurements - 2026-11-01

The final paired comparison alternates baseline and optimized builds in isolated source copies. Both compile the same 40 production translation units; the optimized host adds two private PCH compilation entries. The baseline restores the disconnected RavenSources include before timing, so the comparison uses a complete build. Developer-environment setup and source-timestamp settling are outside the measured build request. Source-edit rows use three runs per configuration; unchanged rows use seven. Both use Windows x64 Debug/MSVC/Ninja with --parallel 4.

| Build request | Before (seconds) | After (seconds) |
| --- | ---: | ---: |
| Perspective sample source edit | 4.794 | 1.464 |
| Vulkan renderer source edit | 4.144 | 1.561 |
| Game source edit, standalone request | 2.522 | 2.158 |
| Full build, no changes | 0.368 | 0.367 |
| Standalone game request, no changes | 0.048 | 0.051 |

These are local warm-build measurements, not timing guarantees. PCH reduces repeated standard-library parsing for the engine/sample translation units; the one-source game target remains outside PCH. Automatic folder discovery keeps unchanged-build time close to the explicit-list baseline.

Validation: Windows Debug/Release production builds pass with PCH enabled. A fresh Debug build with RAVEN_BUILD_GAME_MODULE=OFF and RAVEN_USE_PCH=OFF also passes. In an isolated copy, 38 focused checks cover the exact production translation-unit set, CameraView header discovery in the CMake codemodel, automatic core/sample/public/game additions and removals, duplicate/foreign-backend exclusion, equal-size/equal-timestamp source changes, unchanged sibling objects, no-change stamps/outputs, failed game builds/recovery, matching module loads and shared-header contract rejection/restoration. Every standalone request preserves the host DLL/executable hashes and timestamps. A newly created probe source's timestamp is settled before its first compilation so coarse volume timestamps do not falsely report a sibling rebuild.

Retained measurements and logs are under ignored `out/validation/cmake-enrollment/`, including paired-timings.json, enrollment-results.json, debug/release-build.log and no-game-no-pch.log. Native sample-only probes also passed 50 Perspective and 50 Orthographic frames per configuration, covering runtime component/resource edits, removal, slot reuse, stale mesh handles and cleanup. Debug logs contain no Vulkan warning/error diagnostics. Temporary source copies, probe projects and runners have been removed. These results are task-local historical evidence; CTest remains deferred and native macOS/Linux validation remains pending.

## Exported interface

`Public/Raven/Core/Api.hpp` defines `RAVEN_API`. The engine build defines `RAVEN_CORE_EXPORTS` privately; consumers see DLL imports on Windows. Automatic export of all symbols is disabled. Export annotations are on individual functions so private renderer types and containers do not become exported classes.

The initial seven exported functions are `Application::Run`, `Log`, and the `VulkanContext` constructor, destructor, `PrepareSwapchain`, `CreateMesh`, and `DrawFrame`. Other Vulkan owners and backend implementations remain internal to the DLL. Headers such as math, camera, vertex data, input, and frame time describe the values borrowed by these calls.

The path/file milestone adds six exports: `Paths::GetExecutablePath`, `GetExecutableDirectory`, `GetContentPath`, `GetShaderPath`, and `Files::ReadBinary`/`ReadText`. `Application::Run` accepts an `ApplicationConfig` with `ContentRoot` through a three-argument overload. The original two-argument overload is retained and delegates using `ApplicationConfig{}`.

The texture milestone adds `VulkanContext::CreateTexture`, bringing the total at that milestone to 14 explicit exports. `TextureDesc` is public CPU data; the complete `VulkanTexture` owner remains private. Creation synchronously consumes the input span and returns a context-owned opaque reference. See [TextureResources.md](TextureResources.md).

The image-loading milestone adds `Images::Load`, bringing the total to 15 explicit exports. `ImageData` owns a standard pixel vector returned by value; the private decoder has internal linkage and is not exposed to clients. The image can be loaded before an application/device exists and passed to `CreateTexture`. See [ImageLoading.md](ImageLoading.md).

The development-diagnostic follow-up adds an `Images::Load(path, ImageLoadOptions)` overload and retains the original `Images::Load(path)` export. The one-argument overload delegates using default options. Both signatures are declared and defined consistently, bringing the DLL to 16 explicit exports. `WarnIfNonPowerOfTwo` defaults to `true`, while RavenCore's private development-build definition controls whether it logs a warning.

The signature audit restores the original two-argument `Application::Run` DLL entry point alongside the configured overload and exports the public `Window::Create(const WindowDesc&)` factory. That audit brought the DLL to 18 explicit exports and retained all 16 prior symbols. Adding a default argument does not preserve the binary symbol of a shorter overload; keep an existing entry point when extending these APIs.

The texture-rendering milestone adds `CreateTexturedMesh` and a three-argument `DrawFrame` overload. Both configurations retain all 18 prior symbols and expose 20 exports in total. The existing color vertex/draw types and APIs are unchanged; UV geometry uses its own opaque mesh type. Texture descriptors remain context-owned private resources. See [TextureResources.md](TextureResources.md) for lifetime rules and Debug/Release rendering evidence.

The transparency milestone adds only a four-argument `DrawFrame` export and `VulkanTransparentDraw` CPU data. Both configurations retain all 20 prior symbols and expose 21 exports in total. The existing color/textured draw layouts and both older overloads are preserved. Visibility selection, draw sorting, mesh bounds, and Vulkan pipelines remain private to the engine. See [TextureResources.md](TextureResources.md) for ordering and culling rules.

The orthographic milestone adds matching two-, three- and four-argument `DrawFrame` overloads for `OrthographicCamera`. Both configurations preserve all 21 prior symbols and expose 24 total exports. The plain camera value stays header-only. Both camera types now adapt to the [shared camera handoff](#camera-pose-projection-and-renderer-handoff). See [CoordinateSystem.md](CoordinateSystem.md) for extent, resizing and validation rules.

Both targets use the dynamic MSVC runtime: `/MDd` in Debug and `/MD` in Release. This is a C++ interface for modules built together with matching architecture, compiler/STL, runtime, and build configuration. Rebuild consumers when shared declarations change. It is not the future versioned game-module ABI or a compiler-independent plugin SDK.

The common asset milestone adds 12 explicit exports: three `AssetId` functions, six `AssetCatalog` functions (constructor/destructor, both registration overloads and both lookup functions), and three `AssetResources` functions. Debug/Release retain all 24 prior exports and expose 36 in total. Typed `AssetRef<T>` and asset metadata remain public values, with the catalog/cache implementation private. Public-DLL clients check exact function/member-pointer signatures and resource lifecycle. No existing application, image-loading or renderer signature changes. See [AssetSystem.md](AssetSystem.md).

## Public and private source trees

`RavenEngine/Public/Raven/` contains the engine's client-facing headers. Include them through the `Raven/` prefix, for example `Raven/Core/Application.hpp` and `Raven/Renderer/Vulkan/VulkanContext.hpp`. CMake shares only the `Public` include root with clients.

`RavenEngine/Private/` contains engine `.cpp` files, internal headers, platform backends, and shaders. `Private/Samples/` contains the retained sample executable. It also includes the shared sample component definitions from `Private/Game/`; those definitions are not general public engine APIs. `Private/Game/` has a standalone build for replaceable game code. Public include visibility remains limited to the engine's `Public` directory.

`FrameTime` is public callback data; `FrameClock` is an internal clock implementation. `VulkanDraw` is public submission data and forward-declares the borrowed `VulkanMesh`. The complete mesh class and GPU buffers remain private. `VulkanContext` owns a private implementation defined in its `.cpp`, preserving GPU resource order while removing internal Vulkan headers and container layout from the client interface.

The source-tree cleanup retained existing application/renderer entry points. The later component assessment replaces the internal byte-span Registry entry points with exported Component-reference operations. Rebuild RavenCore, host and game together after this shared-API change; cross-version binary compatibility is not supported by the current C++ interface.

## Client lifecycle and ownership

`ApplicationClient` supplies `OnFrame(renderer, input, frameTime)`. The sample implements it through `PerspectiveSample` and `OrthographicSample`. `Application` has no dependency on a concrete sample.

The caller supplies `ApplicationClientFactory`:

- `Create(renderer)` allocates and initializes a client in the caller's module.
- `Destroy(client)` destroys that client in the same module and must not throw.
- Optional `CreateGameClient(renderer, instance)` receives the retained GameInstance and takes precedence over `Create`.

One create callback and `Destroy` are required and checked before creating a window. A null client is rejected. The factory and client code must remain loaded throughout `Run`. Existing two-field factory initializers remain source-compatible; the factory and ApplicationConfig layouts have grown, so rebuild consumers together with RavenCore.

`Run` establishes its content root, then creates the window, renderer, GameInstance, optional GameModule, and host client in that order. Cleanup reverses this order. A custom-deleter `unique_ptr` invokes the caller's destroy function exactly once on normal exit or frame failure. Renderer cleanup waits for GPU completion; the window outlives it. Factory exceptions unwind the earlier owners. Only one application run may be active at a time; independent GameInstance values do not require a singleton.

Clients borrow the renderer and input during callbacks. `CreateMesh` returns renderer-owned resources valid until shutdown. Both sample factories receive GameInstance. Cameras and animation/input bookkeeping stay in sample members. OrthographicSample owns five Transform/Sprite entities, with F1 editing Sprite ordering in Registry; PerspectiveSample owns three Transform/StaticMesh entities. Cleanup removes only each sample's own entity handles. GPU resource ownership is unchanged.

Frame timing and minimized-window behavior are preserved: event polling precedes `OnFrame`, a nondrawable framebuffer skips it, and the first drawable frame after startup or restore receives zero delta.

## Component file organization - 2026-11-01

`Public/Raven/ECS/` contains the reusable Component, Entity and Registry infrastructure. `Public/Raven/Components/` contains separate TransformComponent, StaticMeshComponent, SpriteComponent and CameraComponent headers. SpriteBlendMode stays with SpriteComponent, and CameraProjectionMode stays with CameraComponent. The extraction preserves Raven::ECS type names, retained-field declarations and behavior, and removes the combined RenderComponents.hpp header. Consumers include the specific components they use; CMake discovers these headers through the Public folder rule in cmake/RavenSources.cmake.

Validation: Windows x64 Debug and Release builds pass for RavenCore, RavenEngine and the existing RavenGame module. Each of the four prepared headers also passes an independent MSVC C++20 syntax check with repeated inclusion. The three moved component bodies match the pre-move definitions exactly. Logs are under ignored `out/validation/component-layout/{debug-build.log,release-build.log,standalone-headers.log}`. Rendering was not rerun for this file reorganization; no tracked tests or CTest integration were added.

## Static meshes on entities — 2026-10-19

`Public/Raven/Components/TransformComponent.hpp` and `StaticMeshComponent.hpp` define their respective components, both derived from the abstract Component parent. TransformComponent directly holds position, Euler rotation in radians and scale, composing `T * Rz * Ry * Rx * S`; its default matrix is identity. StaticMeshComponent directly holds a runtime MeshHandle and RGBA tint. These are concrete fields, not part of Component. Static describes immutable mesh geometry; entity transforms can still animate. This first path uses the existing unlit ColorVertex meshes; textured/imported mesh assets and materials remain separate work.

`VulkanContext::CreateMeshHandle(vertices, indices)` creates renderer-owned geometry and returns a copied value identifying its context and slot. `ResolveMesh(handle)` returns a borrowed resource or null for null/foreign/expired-context handles. Context identities are not reused and mesh slots are not recycled. Handles are runtime identities, not persistent asset references, and do not release geometry when copied or removed. GPU resources live until renderer shutdown after pending work completes. Do not store the resolved pointer in Registry data or serialize the handle for later runs.

PerspectiveSample creates two cubes and a ground entity, all sharing one cube mesh. Its animation writes rotation into TransformComponent; rendering reads Transform/StaticMesh copies from the Registry snapshot and resolves valid handles into existing VulkanDraw submissions. Missing components or invalid handles are skipped. Runtime entities using the same components join this path without changing the fixed sample geometry list. The sample cleans up only its own entities, including rollback on construction failure. Texture draws and camera controls keep their existing path; no Scene class is required.

Initial StaticMesh validation, before the metadata macro refactor: Windows Debug/Release engine/sample targets and the existing temporary RenderProbe build successfully. The sample-only probe exercises both native samples and checks mesh sharing, identity transforms/Y rotation, component transform/tint values reaching draws, runtime creation, component/entity removal, invalid handles and cleanup. A second renderer rejects a handle retained from the destroyed first context. Debug reports no Vulkan core/synchronization diagnostics. Evidence: `out/validation/game-reload/{debug,release}-static-mesh-build.log`, `*-static-mesh-probe-build.log` and `*-static-mesh-render.log`. These are programmatic GPU checks; visual/resize/minimize coverage was not rerun. No tracked tests or CTest integration were added.

The owner requested component implementation before further game-DLL work. The initial StaticMesh checks did not rebuild or load RavenGame. Later component refactors rebuild the existing game target to check migrated callers. The virtual-parent change also checks retained fields through one existing DLL replacement/unload; integrating the mesh path with replaceable gameplay remains future work.

## Sprites on entities — 2026-10-19

SpriteComponent and SpriteBlendMode live in `Public/Raven/Components/SpriteComponent.hpp`; SpriteComponent derives from the same virtual Component parent. It contains AssetRef<TextureAsset>, RGBA Tint, SpriteBlendMode and signed SortLayer/SortOrder. The macro supplies the common type/size/copy implementation. Default mode is Alpha, default tint is white, and a null texture reference produces no draw. Pose and size remain in Transform; the shared quad lies in local XZ, so Scale.X/Scale.Z control width/height. No texture pointer, resource owner or renderer callback enters Registry storage.

OrthographicSample initializes five Transform/Sprite entities and gathers matching entities from the Registry snapshot each frame. All sprites share one quad; texture references resolve through the existing catalog/cache. Opaque mode uses the opaque depth-tested path. Alpha uses the existing straight-alpha path, sorted by layer/order then camera depth; higher layer/order draws later. SortLayer/SortOrder apply to Alpha only. Tint alpha supports fading. Runtime additions and edits update submissions without a fixed draw array or per-entity geometry allocation. Missing Transform/Sprite, null textures and unknown blend modes are skipped. A non-null unregistered asset or load failure propagates through the existing asset-service exception path. Registry slot order is not creation order and does not replace explicit transparent ordering.

F1 edits the blue sprite's SortOrder in Registry. Camera controls remain local pending ECS camera selection/render-view integration. Cleanup and initialization rollback destroy only the five owned handles; removing an owned entity and reusing its slot does not let the sample destroy the replacement. Other callers own their runtime entities. Renderer-owned resources remain valid until context shutdown; removing a Sprite does not release its texture or quad.

The existing game DLL still updates Position/Velocity. A temporary sample-only SyncLegacyMovement applies its Position delta to the first sprite's Transform.Position before gathering draws. Applying deltas preserves direct Transform edits rather than overwriting the pose every frame. This keeps --game/--watch-game behavior compatible without changing DLL source or build/loader code. The planned game-code migration will update Transform directly and remove this bridge; it is not a general component synchronization system.

Sprites are suitable visual primitives for future smoke/VFX: many puffs can reuse a quad/texture while a system updates transforms and tint alpha. Emitters, particle lifetime/pooling, automatic camera-facing billboards, flipbook/UV animation and additive blending are not implemented by this milestone.

Validation: Windows Debug/Release engine/sample/existing-game builds and focused native probes pass. Each configuration presents 50 Perspective and 50 Orthographic sample frames, exercising sprite creation, pose/fade/texture/blend/order changes, shared resources, missing/invalid components, removal, slot reuse and cleanup. A further 100 frames verify existing DLL movement reaches rendered sprite transforms and changes after compatible replacement while retaining one host client. Debug has no Vulkan core/synchronization diagnostics. Logs: `out/validation/game-reload/{debug,release}-sprite-build.log`, `*-sprite-probe-build.log` and `*-sprite-render.log`. These are programmatic GPU checks; manual visual/resize/minimize checks, watcher recovery and non-Windows execution were not rerun. No tracked tests or CTest integration were added.

### Camera pose, projection and renderer handoff

The `ViewFromPoseRH(position, rotation)` helper converts the entity Transform pose to a view matrix. It uses local +Y forward/+Z up and Rz * Ry * Rx radians, builds orientation at the origin and applies negative-position translation. Scale is excluded from its inputs, and non-finite pose/result values are rejected.

`CameraComponent.hpp` defines two projection modes and retains only projection settings. Its `GetProjection(aspect)` validates the active settings through the existing perspective or orthographic math. `GetCameraView(transform, camera, aspect)` prepares a temporary `CameraView` with `View` and `Projection` fields. The pose remains in Transform.

`VulkanContext::DrawFrame(CameraViewBuilder, ...)` is the public entry point for a caller that prepares a view. The six existing camera overloads remain available and use an internal adapter to supply a builder. Both paths enter `Impl::DrawFrameWithView`, which calls the builder synchronously after preparing a drawable swapchain. The builder receives the actual width and height and can return `std::nullopt` to skip drawing. The renderer owns GPU resources and the caller owns camera selection. The draw routine reads the two matrices, computes `Projection * View` once and forwards both the view and the product to the draw queue.

Validation on 2026-11-12: the Windows `x64-debug` build passed. A temporary public client passed 35 checks against RavenCore covering Registry Add/Read/Write, retained settings across a projection mode switch, one Transform shared by mesh and camera, near/far depth, aspect, scale independence and invalid inputs. Its builder received the prepared drawable extent, skipped the missing-camera view and presented a frame from a valid Registry camera. It also checked rejection of a destroyed handle after slot reuse. The previous Debug smoke check presented a frame in both samples through the existing overloads. This is focused Debug evidence for the current component and handoff. The older 241-check camera-foundation logs describe the reverted prototype. Release and native macOS/Linux were outside this audit, and no test suite was added.

Both samples still use local camera objects. The [next change](../TODO.md#next-implementation-target-cameracomponent) is to give OrthographicSample a selected camera entity and move its pan, zoom and reset to the entity components. Selection switching, Perspective migration and direct game-module camera control follow.

## Retained ECS state and game reload — 2026-10-19

Application owns a GameInstance containing a Registry. The Registry owns entity slots and minimal component storage in RavenCore. Entity values retain registry/index/generation identity. Every component derives from the abstract Component class in Component.hpp. Its interface is a virtual destructor, GetTypeName, GetDataSize, CopyDataTo and CopyDataFrom; its constructors/assignment operators are protected. Registry add/read/write/remove accept Component references, so generic callers can use the parent. No reference is retained after a synchronous operation. Component has no renderer dependency, per-frame callback or concrete game state. Entity destruction erases retained fields before recycling a slot. GetEntities returns a snapshot; stale handles still require liveness checks. The map-backed storage implementation is unchanged; general queries/storage optimization remains open.

Derive from Component, declare direct fields, then put the shared macro in the public section using the qualified type identifier and every retained field:

```cpp
namespace MyGame
{
	struct Health : Raven::ECS::Component
	{
		int Current = 100;
		RAVEN_COMPONENT(MyGame::Health, Current);
	};
}

// Inside gameplay code:
MyGame::Health health;
health.Current = 80;
Raven::ECS::Component& base = health;
base.GetTypeName(); // Virtual call; returns "MyGame::Health".
registry.Add(entity, base);
registry.Read(entity, base);
health.Current = 65;
registry.Write(entity, base);
registry.Remove<MyGame::Health>(entity);
```

The macro stringifies the qualified identifier into TypeName, supplies mutable/const GetFields tuples and implements the virtual type/size/copy interface. Use the canonical qualified spelling; aliases are not separate component identities. It replaces the earlier global metadata-only declaration and ComponentTraits. ComponentType requires a concrete mutable Component descendant with its own declaration and matching mutable/const field lists. Typed Registry arguments that inherit a parent's declaration without declaring their own component are rejected. A Component reference uses the declared virtual interface.

Each field must be approved by ComponentValue as well as standard-layout, trivially copyable/destructible and nonvolatile. Arithmetic/enumeration types are allowed; fixed arrays are checked recursively. Approved engine values are Vec3, Vec4, Mat4, Entity, AssetId, AssetRef<T> and MeshHandle. Other class types are rejected by default, including std::string_view, std::span, reference wrappers, pointer wrappers and owning containers. For a custom value struct, explicitly specialize `Raven::ECS::ComponentValue<MyValue> : std::true_type` after reviewing that it contains only self-contained values/IDs. Opt-in never bypasses structural checks. An opted-in class's hidden members require review because this declaration checks the field contract without inspecting its nested implementation. Members must represent values; borrowed/reference members and resource cleanup in retained data remain unsupported.

Polymorphic components are not trivially copyable or aggregates. Add allocates engine-owned storage and synchronously copies the listed fields into it. Read/Write use the virtual copy interface directly, without allocating a temporary byte array on the stack or heap. Buffer-size checks happen before any field changes; failed reads leave the destination unchanged. Remove uses packed field size rather than sizeof(component). Registry owns copied name strings and bytes, with no virtual-table pointers, component objects or retained module callbacks. The caller's virtual code runs only during the synchronous call, while its module remains loaded. Replacement occurs between game updates; local component objects must be destroyed before their module unloads. Deleting a local derived object through Component* invokes its derived destructor, but entity destruction only erases retained bytes. Owning component fields and retained polymorphic object APIs are not implemented.

An empty declaration such as `RAVEN_COMPONENT(MyGame::Selected);` inside a Component-derived Selected type defines a tag. Its data size is zero; Add/Read/Write/Remove and entity destruction still check presence and entity identity. Empty copying performs no memory access. This does not add a concrete tag type to the engine.

List every retained field once in the macro, in a consistent order. Adding a retained field requires updating this declaration. This is an in-process value-copy contract, not a persistent file format or automatic layout migration. No manual layout-version counter, per-component storage implementation, new lifecycle callback or global Registry is introduced.

TransformComponent exposes Position/Rotation/Scale/GetMatrix directly; StaticMeshComponent exposes Mesh/Tint. The existing sample Position/Velocity structs expose Location/Linear. Component keys now include their full qualified type identifiers. Renaming a type changes its key; names and equal byte sizes alone are not layout compatibility proofs. The existing generated shared-header/build contract remains responsible for rejecting unsupported code replacement until a host/game rebuild and restart.

Historical metadata-only refactor validation on 2026-10-19: Debug/Release engine, host, existing game DLL and temporary probe callers built. Component-only checks covered generated names, namespace separation, copied fields, removal and stale slot reuse. An expected compilation failure rejected std::string. Both native samples passed focused GPU checks, with clean Debug core/synchronization diagnostics. Logs remain under ignored `out/validation/game-reload/`: `{debug,release}-component-macro-build.log`, `*-component-macro-probe-build.log`, `*-component-macro-probe.log`, `*-component-macro-render.log` and `component-macro-invalid.log`. That evidence predates the virtual Component parent; it is not the current verification record. The probes remain temporary evidence; no tracked test suite or CTest integration was added.

Historical virtual-parent validation on 2026-10-19: Debug/Release builds and focused inheritance, virtual destruction, field copying, entity lifecycle and GPU checks passed. Retained movement fields survived reloading the same DLL and its final unload. An expected compilation failure rejected std::string. Logs remain at `out/validation/game-reload/{debug,release}-component-base-probe-build.log`, `*-component-base-probe.log`, `*-component-base-render.log` and `component-base-invalid.log`. This record predates the assessment fixes below.

### Component architecture assessment — 2026-10-19

The current engine is a Windows prototype with working rendering/assets and a narrow ECS/game-reload path. The virtual parent and retained value storage serve different lifetimes:

| Part | Responsibility |
| --- | --- |
| Component-derived object | A caller-owned typed view/copy with virtual type/data access; its destructor handles that local object |
| Registry inside GameInstance | Retained entity identities and copied values; removing an entity erases data, without invoking a component destructor |
| Game DLL | Replaceable synchronous update code; no live component object or cleanup callback is left in retained state |
| Renderer | Shared GPU resource ownership; component handles identify resources without owning or releasing them |

The assessment reproduced three problems before editing: Registry rejected Component references, std::string_view/std::span fields changed their observed values when the original memory changed without Registry::Write, and empty fields were rejected. Evidence is `out/validation/game-reload/component-assessment-before.log`. The fixes add the base-reference API, default-deny custom value policy and tag support. MeshHandle now lives in `Public/Raven/Renderer/MeshHandle.hpp`; StaticMeshComponent includes math and that identity without importing Vulkan draw submissions. No new concrete component, query/storage optimization, scene owner or DLL workflow was added.

The following remain deliberate limits: field lists are manual; type names/equal sizes do not prove layout compatibility; shared-definition/build edits require a host/game rebuild and restart; there is no owning-component cleanup, property reflection, component factory/enumeration or general ECS query API. Registry access belongs to one session/game thread; entity snapshots permit same-thread structural changes, not concurrent access. Future editor/jobs must queue mutations to that owner or define a separate synchronization contract. Define resource sharing/release and safe GPU retirement before adding repeated scene switching or runtime resource replacement. macOS/Linux still need their documented random-byte/module backends and native validation. Review this foundation before continuing Sprite/Camera implementation or further mesh/game-DLL integration.

Assessment verification: Windows Debug/Release engine, host, existing game DLL and focused probes build; changed Registry/module sources also compile with /W4 /WX. Checks pass for operations through Component references, virtual destruction, independent copies, reviewed custom values/arrays, tags, buffer-size failure, stale handles and cleanup. One changed-logic DLL replacement preserves state; an incompatible candidate is rejected while the active code continues; values remain readable after final unload. Both samples pass 50 frames per configuration, with clean Debug Vulkan core/synchronization diagnostics. Separate expected compile failures reject owning and borrowed-view fields. Logs: `out/validation/game-reload/{debug,release}-component-assessment-build.log`, `*-component-assessment-probe.log`, `*-component-assessment-render.log`, `InvalidComponent-assessment.log` and `InvalidBorrowedComponent-assessment.log`. Watcher recovery, manual visual/resize/minimize checks and non-Windows execution were not rerun. Probes remain ignored temporary evidence; no tracked test suite or CTest integration was added.

Sample control state uses ordinary class members. No separate sample-component file or Registry wrapper helpers are needed. OrthographicSample initializes Transform/Sprite entities and keeps Position/Velocity only for the temporary first-sprite compatibility bridge described above. All sample factory paths still receive GameInstance. Only the movement update in RavenGame.dll is currently replaceable.

The implemented ownership boundary is:

| Part | Lifetime and responsibility |
| --- | --- |
| RavenCore | Remains loaded; implements Registry, engine services, rendering and GPU resource cleanup |
| Host and GameInstance | The host owns a GameInstance for each running game session and retains its Registry/game data across game-code replacement; scene-specific data belongs to the scene/Registry |
| Game module | Supplies replaceable game update code; does not own retained data or leave cleanup callbacks, virtual objects, or other references into unloaded code |

A GameInstance per running game does not require a process-wide singleton. Its retained implementation and destruction must remain available after a game module is unloaded. Future editor editing/play sessions are a design assumption, not a commitment to ship a complete visual editor in v1. The current application still runs one client/window; multiple concurrent play sessions are not implemented by this plan.

Application polls the GameModule between updates, then calls its active update function with borrowed GameInstance/InputState/FrameTime before the host client's OnFrame. Polling/builds continue while minimized; gameplay/rendering pause. The retained OrthographicSample initializes resources/entities once, bridges legacy movement into Transform and gathers Transform/Sprite values for drawing. Only the movement function lives in RavenGame.dll. No module-owned virtual objects, destructors, callbacks, threads or retained pointers may survive an update. The explicit ComponentValue policy and structural checks protect copied values; custom opt-ins still require reviewed value/ID-only members.

For the initial scene and reload example, share the existing mesh and asset catalog/cache. Current meshes, textures and immutable descriptors remain renderer-owned until shutdown. Sample constructors allocate resources and create per-client caches, so recreating clients on every code replacement can accumulate unused renderer-owned resources. Preserve this initialization across game-code swaps. Destroying a client or AssetResources service does not individually release GPU resources. Persistent asset references, runtime entity identity and future resource handles have separate responsibilities. Define sharing/release, stale-handle behavior and safe GPU retirement before repeated scene switching or resource replacement.

Game callback aliases remain in ApplicationClient.hpp; Api.hpp holds separate engine/game export macros. GameModule.cpp owns replacement and polling/build orchestration. Win32GameModule.cpp contains native loading and a hidden child process/job with redirected diagnostics; shutdown cancels only its own build tree. Game implementation and state definitions remain in Private/Game. Application.cpp coordinates lifetimes and frame calls.

Engine implementation follows assistant-suggested, owner-applied snippets as clarified on 2026-11-01. The earlier general direct-edit authorization is withdrawn for engine code; a specific engine file edit requires an explicit owner request.

## Reload scope

`RavenCore.dll` and the host remain loaded. GameModule reserves a unique execution-copy directory beside the game output, copies the DLL, loads it by absolute path, checks the fixed C-linkage contract query, then obtains the typed update function. A valid candidate replaces the old function only between synchronous calls. The previous library and its execution copy are released after activation. Failed copying/loading, missing exports, null updates and contract mismatches leave the active module intact. The build output is never loaded, so the linker can overwrite it.

Run `RavenEngine.exe --game` for explicit/finished-output reload or `--watch-game` for source builds. F5 calls Reload. Poll observes external output contents/metadata after two stable samples, including replacements with equal size/timestamps. The source watcher compares file contents every 500 ms and waits for a stable sample before building. It watches Public headers, game .cpp/.hpp files, game/root CMake rules, presets and cmake helpers. Edits during a build queue another build; a source snapshot changed during compilation is not activated. This is a polling watcher, not a native notification backend.

CMake generates a SHA-256 contract from all Public/game .hpp definitions, relevant build files, compiler/version, pointer size, configuration and C++ flags. Game .cpp bodies are excluded so logic can change. Any contract change, including a header comment, conservatively requires rebuilding/restarting the host; there is no automatic reset or migration. A separate content-derived build key forces compilation even when timestamps are equal. Component names must be unique and kept stable; byte-size checks alone do not prove layout compatibility. No developer-maintained layout counter is needed.

The standalone child project under each build's RavenEngine/GameBuild links RavenCore.lib as a file, with no dependency that can rebuild or clean RavenCore. Full builds, manual requests and the runtime watcher share the generated GameBuild/BuildGame.cmake recipe and process lock. It hashes supported inputs, the generated recipe/environment, engine import library, child cache and expected runtime artifacts. Unchanged verified results skip child configure/build; changed inputs rebuild incrementally, and missing/corrupted outputs trigger child-only recovery. The last completed record is replaced atomically only after a successful build and artifact verification. Automatic builds run asynchronously and log to GameBuild/AutoBuild.log; their compiler environment comes from the host configuration snapshot. Use the script instead of rebuilding the full engine while it runs. Only Windows/MSVC/single-config Ninja supports this path; other native module backends remain pending.

This is a matching-toolchain development interface, not a sandbox or compiler-independent plugin ABI. The supported sample dependency set is shared .hpp definitions and fixed CMake rules; adding private/external headers, libraries, generator flags or other dependencies requires extending the contract inputs and rebuilding the host first. Do not use a cpp-only edit to introduce persistent type definitions or change ABI-affecting pragmas/macros. Arbitrary layout migration and recovery from native code crashes remain outside scope. The DLL's loader initialization runs before its query can be checked, so load only trusted local build output. Shipping packages should omit this development module/build workflow.

Build or pre-activation validation failures should retain the previous working module. This does not guarantee recovery from arbitrary execution errors in newly activated native code. Automatic migration of arbitrary C++ object layouts is outside the first example. End-to-end completion requires actual changed behavior with retained ECS state and repeated replacement, not just a DLL export check.

## Joint ECS/reload validation on 2026-10-19

These reload results precede the StaticMesh component integration above; they do not establish reload coverage for that new rendering path.

Windows Debug/Release engine, host and standalone game builds pass. The modified Registry, GameModule and Win32 module/process implementations also compile independently with `/W4 /WX /utf-8`. Temporary public-client probes pass **133 checks per configuration**, including 24 live replacements, retained identity/value updates, component removal and destruction/slot reuse, rejection of wrong contracts/missing exports/null updates/damaged DLLs, continued old behavior and execution-copy cleanup.

Automatic-build probes pass **14 state/identity checks per configuration** across initial build, source-body replacement, intentional compilation failure, recovery, shared-header contract rejection and restoration. Runtime builds leave both the RavenCore DLL hash and write time unchanged. Explicit configure/content-derived compilation corrected a reproduced equal/coarse-timestamp recovery failure; DLL content observation also detects equal-size/timestamp output replacement.

Native Application probes execute **100 rendered frames per configuration** on the RTX 3070 Ti Laptop GPU. Finished-output polling changes the moving sprite's update behavior while keeping one host client and its existing renderer/cache alive; the entity remains alive during client destruction. Debug reports no Vulkan core/synchronization warnings or errors. These are programmatic frame/behavior checks, not a new visual review or a complete rerun of resize/minimize/input coverage.

Historical validation logs are under ignored `out/validation/game-reload/`: `debug-probe.log`, `release-probe.log`, `debug-watch.log`, `release-watch.log`, `debug-render.log` and `release-render.log`. Build failures are intentional cases in the watcher probe; source files are restored afterward. No tracked test framework or CTest registration was added. This establishes the narrow Windows sample, not general ECS queries, a complete scene, arbitrary dependency/layout migration, other OS module backends, or shader/asset reload.

After removing the sample-only layer, Windows Debug/Release engine, host, standalone game and temporary probe builds pass. Per configuration, the public-client probe passes **137 checks**, including 24 live replacements and four intentional rejection paths. The rendering probe passes **50 perspective + 50 orthographic frames**, checking perspective orbit/zoom/order/pause/resume, local controls, unchanged sprite data under orthographic camera input, and sprite cleanup. A separate **100-frame** run observes changed DLL behavior with one retained host client and live entity state during cleanup. RavenCore's SHA-256 hash is unchanged across each rendering run; Debug reports no Vulkan core/synchronization warnings or errors.

Current cleanup evidence is under ignored `out/validation/game-reload/`: `{debug,release}-cleanup-build.log`, `{debug,release}-cleanup-probe-build.log`, `{debug,release}-cleanup-probe.log` and `{debug,release}-cleanup-render.log`. These are native GPU/programmatic checks, not a fresh visual review or a rerun of resize/minimize coverage. The source-watcher build/failure-recovery checks above were not rerun for this sample cleanup. No engine implementation, tracked test suite or CTest integration was added during this verification.

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

At the time of the signature audit, the former `Arm64Mac` header had an invalid `IsKeyDown` override, lacked the required input/framebuffer overrides, and had no executable-path implementation. The historical compiler evidence is in `out/validation/signature-before-mac-header.log`; these interface/path findings have since been addressed by `CocoaWindow`, `CocoaInput` and `MacPaths.cpp`. Earlier native macOS Debug evidence is recorded in [MacOSBuild.md](MacOSBuild.md). As of 2026-10-14, macOS/Linux still lack the asset system's `Platform::FillRandomBytes` implementation and CMake hookup, and need current Debug/Release native verification. They are not included in the passing Windows build/header counts. Follow [PlatformHandoff.md](PlatformHandoff.md) for remaining work.
