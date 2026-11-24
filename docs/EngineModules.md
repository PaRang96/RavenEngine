# Engine modules and game roots

Since M6A (2026-11-24, [DataModelDesign.md](DataModelDesign.md)) the engine and each game build separately. A game lives in its own folder under `Games/` and sees only `RavenEngine/Public`:

| Target | Output | Responsibility |
| --- | --- | --- |
| `RavenCore` | `RavenCore.dll` and import library `RavenCore.lib` | Application loop, the engine game session, platform backends, input, logging, ECS, scene/project services, scene files, asset ownership and Vulkan resources |
| `RavenEngine` | `RavenEngine.exe` | The generic host ([Private/Host](../RavenEngine/Private/Host/RavenEngine.cpp)): runs one game root with `--game <Name>` (default `Samples`) |
| `<Name>Game` | `<Name>.dll` | One game module per `Games/<Name>`, built by a standalone child project that links the completed engine import library |
| `<Name>Tool` | `<Name>Tool.exe` | Optional tools of a game root (`Games/<Name>/Tools/*.cpp`), for example the Samples scene generators |
| `RavenSceneTool` | `RavenSceneTool.exe` | Generic scene file commands: `validate`, `dump`, `apply`, `position`/`rotation`/`scale`, `stage-player` |
| `RavenShaders` | Validated SPIR-V under `Shaders/<config>/` | Build dependency of `RavenCore` |
| `RavenContent` | Runtime copies beside the executable | Stages engine shaders and licenses under `Content/`, and each game's content under `Games/<Name>/Content/` |

Everything is emitted under `out/build/<preset>/RavenEngine/`. Shader lookup uses executable-relative `Content/Shaders`; see [RuntimeUtilities.md](RuntimeUtilities.md).

## Game roots (M6A)

```
Games/<Name>/
    CMakeLists.txt     three lines: project, include RavenGame.cmake, raven_add_game(<Name>)
    Source/            the module: components, systems, RavenRegisterSystems
    Content/           Project.ravenproject, scenes and assets
    Tools/             optional host-built tools
```

- **Samples** holds the 3D samples (Perspective, Gallery, Assemblies, Puzzle, Collection) and the benchmark scenes. It flies freely by default and runs the orbit demo with `--orbit`. `SamplesTool create-puzzle|create-collection|create-benchmark` regenerates the tool-made scenes.
- **Courtyard** holds the 2D Courtyard and Orthographic levels: WASD, arrows/wheel/PageUp/PageDown/Home for the camera, F1 order, F2 camera.
- The engine host owns the developer keys: F3 restart scene, F4 next scene, F5 reload the module, F7 render view, F8 pause.
- Any other `--name` launch option is the game's, through `GameServices::HasOption("name")`.

**Writing game code.**
- Components derive from `ECS::Component`, list their fields with `RAVEN_COMPONENT`, and are declared to the host with `systems.Component<T>()` in `RavenRegisterSystems`.
- Systems receive a `SystemContext`:
  - `World`, the scene access;
  - `Input` and `Time`;
  - `Picks`, the left-click hits of the presented frame;
  - `Commands`, deferred spawns, despawns, reparenting and component adds or removes, applied after each fixed step and after `Update`;
  - `Services`: sound, launch options, and the benchmark trace value.
- Entities refer to each other through `EntityRef`, resolved with `World.Resolve`. Assets are referenced with `AssetRef<T>`.
- `WorldTransformComponent` and `ParentComponent` are host-owned: game code reads them, and changes the parent through `Commands.SetParent`.

**What reloads.** The module contract hashes only the engine's public surface: `RavenEngine/Public`, the game-facing CMake (`GameContract.cmake`, `RavenBuildSettings.cmake`, `RavenGame.cmake`, `RavenGameSources.cmake`) and the toolchain.
- **Game code:** any edit in a game root rebuilds and reloads the module. Component layout changes migrate retained values by field name ([HotReloadDesign.md](HotReloadDesign.md#h6--field-level-component-migration-q7)):
  - matching names and types copy;
  - new fields take their defaults;
  - removed fields are dropped;
  - `RAVEN_COMPONENT_ALIASES("Old::TypeName", "OldField=NewField")` carries renames;
  - a field that changes type is refused, and the last good module stays.
- **Engine public headers:** an edit still needs a host rebuild and restart, and the running host says so.

## CMake layout

| File | Responsibility |
| --- | --- |
| `CMakeLists.txt` | Project, host debugger policy, tooling, Vulkan discovery and loading shared defaults |
| `cmake/RavenBuildSettings.cmake` | Shared language/runtime/export defaults, the warning policy and the optional private standard-library PCH helper |
| `RavenEngine/CMakeLists.txt` | RavenCore/RavenEngine/RavenSceneTool target setup, interfaces, compile definitions and module ordering |
| `cmake/RavenSourceDiscovery.cmake` | Cached folder discovery shared by enrollment and game builds |
| `cmake/RavenSources.cmake` | Assign discovered public/private/host/tool files to their owning target |
| `cmake/RavenPlatform.cmake` | Select the native folder from the shared discovery result; configure platform dependencies/settings |
| `cmake/RavenContent.cmake` | Shader registration/validation, and staging of shaders, licenses and every game root's content |
| `cmake/RavenBuildEnvironment.cmake` | Capture selected compiler/SDK environment variables once at host configuration |
| `cmake/RavenGameModule.cmake` | Discover `Games/*`, the engine contract, one child build (`GameBuild/<Name>`), one `WatchedInputs.txt` and one `<Name>Game` target per game, and the game tools |
| `cmake/RavenGame.cmake` | `raven_add_game(<Name>)` for a game root's child project |
| `cmake/RavenGameSources.cmake` | A game root's `Source/` files, shared by compilation and content-based rebuild checks |
| `cmake/GameContract.cmake` | The engine contract inputs and their hash |

While the host runs, build a game only through `cmake -P out/build/<preset>/RavenEngine/GameBuild/<Name>/BuildGame.cmake`; full engine builds can try to overwrite loaded files.

### Where to make a build change

| Change | Edit once |
| --- | --- |
| Add an engine C++ file or header | Place it under `RavenEngine/Public` or `RavenEngine/Private`; `cmake/RavenSources.cmake` discovers it |
| Add a native backend file | Place it in Private/Platform/Windows, Mac or Linux; only the selected folder is compiled |
| Add a platform dependency | Configure it once in `cmake/RavenPlatform.cmake` |
| Add a game C++ file or header | Place it in `Games/<Name>/Source`; the child build and the build-input check use the same discovered set |
| Add a game | Create `Games/<Name>` with the three-line `CMakeLists.txt`, `Source/` and `Content/Project.ravenproject`, and reconfigure |
| Add a shader | Add one `raven_add_shader(source, profile, entry)` call in `cmake/RavenContent.cmake` |
| Add an engine runtime file | Add one `raven_stage_file(project_relative_path)` call in `cmake/RavenContent.cmake` |
| Change language/CRT/export defaults | Change `cmake/RavenBuildSettings.cmake`; every project includes it |
| Disable engine PCH | Configure with `-DRAVEN_USE_PCH=OFF` |
| Change the game the host runs by default | Configure with `-DRAVEN_DEFAULT_GAME=<Name>` |

The sections below are earlier records, kept for their evidence. Where they name `Private/Game`, `Private/Samples`, `RavenGame.dll`, `ApplicationClient` or `GameBindings`, they describe the layout before M6A.

### Automatic C++ enrollment - 2026-11-01

Ordinary file additions/removals require no CMake source-list edit. Public .hpp headers belong to RavenCore. Common Private .cpp/.hpp files belong to RavenCore; Private/Samples belongs to RavenEngine; Private/Game belongs only to the standalone RavenGame project. Shared files directly under Private/Platform are included once, while RavenPlatform selects exactly one native subfolder. Mac .mm files are included only with that backend. Legacy .h sample headers are also discovered. Follow the repository's .cpp/.hpp naming convention for new code.

RavenSourceDiscovery scans each folder once per CMake invocation and caches that result only for that invocation. The host reuses its Private scan for common code, sample code and native backend selection, and reuses its Public scan for the compatibility contract. Generated out/, Content and ThirdParty trees are outside C++ source discovery. Shader profiles/entry points, platform dependencies and runtime staging retain explicit registrations because those files need additional build metadata.

CONFIGURE_DEPENDS checks filename changes at build time and regenerates the build system when needed. This intentionally uses scoped discovery for the current Ninja workflow; CMake documents its per-build check cost and generator limitations in the [file command reference](https://cmake.org/cmake/help/v3.24/command/file.html#filesystem). Ordinary source-body edits continue through compiler dependency tracking. Public/game header and build-rule changes remain covered by the conservative host-rebuild/restart contract.

A shader registration adds the source to the engine target, marks it as a source-only HLSL file, creates its compile/validation rule and derives its SPIR-V/staged output name. Runtime-file registration supplies both dependencies and source/destination lists for the generic StageContent template; that template contains no asset names. One generated staging script restores missing runtime shaders, textures and licenses, while identical files retain their timestamps.

The game project remains an independent build entry point so runtime builds cannot rebuild the loaded host. It shares its defaults and source discovery instead of maintaining another file list. Each game source has its own content-derived command key: an equal-timestamp body edit changes that source's command without changing an unchanged sibling's command. Compatibility fingerprints include the configured language standard, compiler option and runtime settings.

## Automatic enrollment and verified build requests

Automatic .cpp/.hpp enrollment remains a workflow requirement. Add code under its owning engine/sample/game/native folder and build; do not maintain parallel per-file source lists. Compiler settings, shader entry-point metadata and asset staging remain centralized in their existing helpers.

The generated GameBuild/BuildGame.cmake is the shared entry point for the full-build game target, manual requests and runtime source watching. It acquires a process-scoped lock before inspecting the child cache or build record. A second request waits, then rechecks the completed result; it does not configure or compile concurrently in the same child directory.

A successful host configuration records only selected compiler/SDK environment variables in GameBuild/BuildEnvironment.cmake. The game-build process restores that snapshot when compilation is required. Runtime rebuilds therefore work without inheriting a Visual Studio developer shell, and the engine process's own environment is unchanged. Configure the full host through CMake Tools or a Visual Studio developer environment; reconfigure after changing the selected toolchain.

The standalone target generates GameOutputs.txt from CMake target metadata: its runtime DLL and, in Debug/RelWithDebInfo, its linker PDB. The last completed build record contains hashes of inputs, child cache and expected artifact contents. Reuse requires all three to match. Missing/corrupted artifacts trigger a clean rebuild of only the standalone child. The PDB is included in that target's additional clean outputs because ordinary Ninja clean does not remove it automatically. No host binary has a build/clean edge in the child.

Since 2026-11-24 the Debug and RelWithDebInfo game DLL compiles with `/Zi`: through `MSVC_DEBUG_INFORMATION_FORMAT` with CMake 3.25 or newer, and with explicit `/Zi /FS` on CMake 3.24, whose child project ignores that property. Before, its PDB named the game sources only as object files. The game follows the engine's warning policy (`/W4`, warnings as errors only in CI through `RAVEN_WARNINGS_AS_ERRORS`, which the game build passes to its child configure), so warnings appear in `AutoBuild.log` without failing a reload. A breakpoint session across a reload has not been run yet.

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

`FrameTime` is public callback data; `FrameClock` is an internal clock implementation. `RenderWorld` (M3) is the public, host-owned mirror of a scene's lit meshes; its GPU state is private. The complete mesh class and GPU buffers remain private. `VulkanContext` owns a private implementation defined in its `.cpp`, preserving GPU resource order while removing internal Vulkan headers and container layout from the client interface.

The source-tree cleanup retained existing application/renderer entry points. The later component assessment replaces the internal byte-span Registry entry points with exported Component-reference operations. Rebuild RavenCore, host and game together after this shared-API change; cross-version binary compatibility is not supported by the current C++ interface.

## Client lifecycle and ownership

`ApplicationClient` supplies `OnFrame(renderer, input, frameTime)`. The sample implements it through `PerspectiveSample` and `OrthographicSample`. `Application` has no dependency on a concrete sample.

The caller supplies `ApplicationClientFactory`:

- `Create(renderer)` allocates and initializes a client in the caller's module.
- `Destroy(client)` destroys that client in the same module and must not throw.
- Optional `CreateGameClient(renderer, instance)` receives the retained GameInstance and takes precedence over `Create`.

One create callback and `Destroy` are required and checked before creating a window. A null client is rejected. The factory and client code must remain loaded throughout `Run`. Existing two-field factory initializers remain source-compatible; the factory and ApplicationConfig layouts have grown, so rebuild consumers together with RavenCore.

`Run` establishes its content root, then creates the window, renderer, GameInstance, optional GameModule, and host client in that order. Cleanup reverses this order. A custom-deleter `unique_ptr` invokes the caller's destroy function exactly once on normal exit or frame failure. Renderer cleanup waits for GPU completion; the window outlives it. Factory exceptions unwind the earlier owners. Only one application run may be active at a time; independent GameInstance values do not require a singleton.

Clients borrow the renderer and input during callbacks. Legacy CreateMesh resources remain valid until renderer shutdown. Both sample factories receive GameInstance and create host-owned SceneSessions. Their Scene instances own the current entities, selected ECS cameras, gameplay bindings and managed asset leases. F1 edits Sprite ordering in Registry. Cleanup affects only owned handles; compatible game replacement preserves the session. The [scene workflow](SceneWorkflow.md) defines shared resource ownership and release.

Frame timing and minimized-window behavior are preserved: event polling precedes `OnFrame`, a nondrawable framebuffer skips it, and the first drawable frame after startup or restore receives zero delta.

## Component file organization - 2026-11-01

`Public/Raven/ECS/` contains the reusable Component, Entity and Registry infrastructure. `Public/Raven/Components/` contains separate TransformComponent, StaticMeshComponent, SpriteComponent and CameraComponent headers. SpriteBlendMode stays with SpriteComponent, and CameraProjectionMode stays with CameraComponent. The extraction preserves Raven::ECS type names, retained-field declarations and behavior, and removes the combined RenderComponents.hpp header. Consumers include the specific components they use; CMake discovers these headers through the Public folder rule in cmake/RavenSources.cmake.

Validation: Windows x64 Debug and Release builds pass for RavenCore, RavenEngine and the existing RavenGame module. Each of the four prepared headers also passes an independent MSVC C++20 syntax check with repeated inclusion. The three moved component bodies match the pre-move definitions exactly. Logs are under ignored `out/validation/component-layout/{debug-build.log,release-build.log,standalone-headers.log}`. Rendering was not rerun for this file reorganization; no tracked tests or CTest integration were added.

## Static meshes on entities — 2026-10-19

`Public/Raven/Components/TransformComponent.hpp` and `StaticMeshComponent.hpp` define their respective components, both derived from the empty Component base. TransformComponent directly holds position, Euler rotation in radians and scale, composing `T * Rz * Ry * Rx * S`; its default matrix is identity. StaticMeshComponent directly holds a runtime MeshHandle and RGBA tint. These are concrete fields, not part of Component. Static describes immutable mesh geometry; entity transforms can still animate. This first path uses the existing unlit ColorVertex meshes; textured/imported mesh assets and materials remain separate work. The current extension with imported normals, UVs, retained materials and lighting is documented in [MeshMaterials.md](MeshMaterials.md).

`VulkanContext::CreateMeshHandle(vertices, indices)` creates renderer-owned geometry and returns a copied value identifying its context and slot. `ResolveMesh(handle)` returns a borrowed resource or null for null/foreign/expired-context handles. Context identities are not reused and mesh slots are not recycled. Handles are runtime identities, not persistent asset references, and do not release geometry when copied or removed. GPU resources live until renderer shutdown after pending work completes. Do not store the resolved pointer in Registry data or serialize the handle for later runs.

PerspectiveSample creates two cubes and a ground entity, all sharing one cube mesh. Its animation writes rotation into TransformComponent; since M3, rendering syncs a `RenderWorld` from change ticks and the lifecycle journal, and the GPU culls and draws it. Invalid mesh handles draw nothing. Runtime entities using the same components join this path without changing the fixed sample geometry list. The sample cleans up only its own entities, including rollback on construction failure. Texture draws and camera controls keep their existing path; no Scene class is required.

Initial StaticMesh validation, before the metadata macro refactor: Windows Debug/Release engine/sample targets and the existing temporary RenderProbe build successfully. The sample-only probe exercises both native samples and checks mesh sharing, identity transforms/Y rotation, component transform/tint values reaching draws, runtime creation, component/entity removal, invalid handles and cleanup. A second renderer rejects a handle retained from the destroyed first context. Debug reports no Vulkan core/synchronization diagnostics. Evidence: `out/validation/game-reload/{debug,release}-static-mesh-build.log`, `*-static-mesh-probe-build.log` and `*-static-mesh-render.log`. These are programmatic GPU checks; visual/resize/minimize coverage was not rerun. No tracked tests or CTest integration were added.

The owner requested component implementation before further game-DLL work. The initial StaticMesh checks did not rebuild or load RavenGame. Later component refactors rebuild the existing game target to check migrated callers. The virtual-parent change also checks retained fields through one existing DLL replacement/unload; integrating the mesh path with replaceable gameplay remains future work.

## Sprites on entities — 2026-10-19

SpriteComponent and SpriteBlendMode live in `Public/Raven/Components/SpriteComponent.hpp`; SpriteComponent derives from the same Component base. It contains AssetRef<TextureAsset>, RGBA Tint, SpriteBlendMode and signed SortLayer/SortOrder. The macro supplies its type identity and field list. Default mode is Alpha, default tint is white, and a null texture reference produces no draw. Pose and size remain in Transform; the shared quad lies in local XZ, so Scale.X/Scale.Z control width/height. No texture pointer, resource owner or renderer callback enters Registry storage.

OrthographicSample initializes the active level's Transform/Sprite entities and gathers matching entities from the Registry snapshot each frame. All sprites share one quad; texture references resolve through the existing catalog/cache. Opaque mode uses the opaque depth-tested path. Alpha uses the existing straight-alpha path, sorted by layer/order then camera depth; higher layer/order draws later. SortLayer/SortOrder apply to Alpha only. Tint alpha supports fading. Runtime additions and edits update submissions without a fixed draw array or per-entity geometry allocation. Missing Transform/Sprite, null textures and unknown blend modes are skipped. A non-null unregistered asset or load failure propagates through the existing asset-service exception path. Registry slot order is not creation order and does not replace explicit transparent ordering.

F1 edits the blue sprite's SortOrder in Registry. Pan, zoom and Home update the selected camera’s components. Cleanup and initialization rollback destroy only the owned sprite and camera handles. Removing an owned entity and reusing its slot does not let the sample destroy the replacement. Other callers own their runtime entities. Renderer-owned resources remain valid until context shutdown; removing a Sprite does not release its texture or quad.

The Orthographic sample adds Velocity to the moving sprite. The game DLL writes its existing Transform directly and reverses Velocity.X at the existing bounds. OnFrame gathers the retained pose for drawing.

Sprites are suitable visual primitives for future smoke/VFX: many puffs can reuse a quad/texture while a system updates transforms and tint alpha. Emitters, particle lifetime/pooling, automatic camera-facing billboards, flipbook/UV animation and additive blending are not implemented by this milestone.

Validation: Windows Debug/Release engine/sample/existing-game builds and focused native probes pass. Each configuration presents 50 Perspective and 50 Orthographic sample frames, exercising sprite creation, pose/fade/texture/blend/order changes, shared resources, missing/invalid components, removal, slot reuse and cleanup. A further 100 frames verify existing DLL movement reaches rendered sprite transforms and changes after compatible replacement while retaining one host client. Debug has no Vulkan core/synchronization diagnostics. Logs: `out/validation/game-reload/{debug,release}-sprite-build.log`, `*-sprite-probe-build.log` and `*-sprite-render.log`. These are programmatic GPU checks; manual visual/resize/minimize checks, watcher recovery and non-Windows execution were not rerun. No tracked tests or CTest integration were added.

### Camera pose, projection and renderer handoff

The `ViewFromPoseRH(position, rotation)` helper converts the entity Transform pose to a view matrix. It uses local +Y forward/+Z up and Rz * Ry * Rx radians, builds orientation at the origin and applies negative-position translation. Scale is excluded from its inputs, and non-finite pose/result values are rejected.

`CameraComponent.hpp` defines two projection modes and retains only projection settings. Its `GetProjection(aspect)` validates the active settings through the existing perspective or orthographic math. `GetCameraView(transform, camera, aspect)` prepares a temporary `CameraView` with `View` and `Projection` fields. The pose remains in Transform.

`VulkanContext::DrawFrame(CameraViewBuilder, ...)` is the public entry point for a caller that prepares a view. The six existing camera overloads remain available and use an internal adapter to supply a builder. Both paths enter `Impl::DrawFrameWithView`, which calls the builder synchronously after preparing a drawable swapchain. The builder receives the actual width and height and can return `std::nullopt` to skip drawing. The renderer owns GPU resources and the caller owns camera selection. The draw routine reads the two matrices, computes `Projection * View` once and forwards both the view and the product to the draw queue.

Validation on 2026-11-12: the Windows `x64-debug` build passed. A temporary public client passed 35 checks against RavenCore covering Registry Add/Read/Write, retained settings across a projection mode switch, one Transform shared by mesh and camera, near/far depth, aspect, scale independence and invalid inputs. Its builder received the prepared drawable extent, skipped the missing-camera view and presented a frame from a valid Registry camera. It also checked rejection of a destroyed handle after slot reuse. The previous Debug smoke check presented a frame in both samples through the existing overloads. This is focused Debug evidence for the current component and handoff. The older 241-check camera-foundation logs describe the reverted prototype. Release and native macOS/Linux were outside this audit, and no test suite was added.

OrthographicSample owns two camera entities and stores the selected entity separately. F2 changes selection before camera input runs. Each camera retains its own Transform and projection settings across switches. Home restores the selected camera’s startup pose and projection. The primary camera starts at X = 0 and the second at X = 4, both at Y = -10 facing +Y. This restores the moving sprite’s initial depth visibility in the primary view. The callback reads the selected components and skips drawing for missing, stale or invalid camera values.

Validation on 2026-11-13: Windows x64 Debug and Release builds passed. A focused Debug client verified Vulkan presentation, startup depth, F2 selection before input, independent pan and zoom, Home on the second camera, and recovery by switching away from a destroyed camera. A replacement entity in a recycled slot and an unrelated entity survived sample cleanup. The Debug `--orthographic` and Release `--game` samples presented a frame, and Release loaded the game DLL. These checks used programmatic input and startup smoke runs. Manual controls, native macOS/Linux and live DLL replacement were outside this check.

PerspectiveSample owns an ECS camera and gathers meshes after its animation update. ScreenPointToRay uses the matrices and drawable extent returned by DrawFrame, and the sample intersects the ray against Z = 0 after rendering. Standalone orbit and reloadable free-fly share the same rendering and picking paths.

## File-authored Orthographic levels

Run the Windows sample with --orthographic, --game, or --watch-game. F1 toggles the bound alpha sprite's sort order. F2 switches cameras. Home resets the selected camera to its authored pose and lens. F3 restarts the current level from disk. F4 switches between Orthographic and Courtyard.

CMake stages the source files in Content/Levels beside the executable. To try an edit while running, edit the staged Content/Levels/Orthographic.ravenlevel, change the runner's position from -2 to -1 on X, then press F3. F4 loads the other file. The next host build stages the source copies again. Keep permanent edits in the repository's Content/Levels directory.

The text format uses one record per line, whitespace-delimited fields, and # comments. The first record is raven-level 1. Entity names are unique within the file. Records after the header can appear in any order.

| Record | Fields after the record name |
| --- | --- |
| camera | Name, position XYZ, rotation XYZ in radians, orthographic height, near plane, far plane |
| sprite | Name, texture UUID, opaque or alpha, position XYZ, rotation XYZ in radians, scale XYZ, tint RGBA, integer sort layer, integer sort order |
| cameras | Initial camera name and alternate camera name |
| moving | Sprite name and initial velocity XYZ |
| order | Alpha sprite name for the F1 sort-order toggle |
| player | Sprite name for DLL-controlled movement |
| goal | Sprite name for the goal |
| bounds | Player, moving sprite, or goal name and positive finite X and Z half-extents in world units |
| solid | Stationary sprite name and positive finite X and Z half-extents in world units |

Each level declares two distinct cameras and one cameras, moving, and order record. A playable level adds one player record, one goal record, and exactly three bounds records, one each for the player, moving sprite, and goal. These three bindings name distinct sprites. A level can add multiple solid records, each bound to a different stationary sprite. Each solid uses its sprite's authored position. Its X and Z half-extents are explicit world units, independent of rotation and visual scale. The loader rejects unknown solid names, duplicate records, invalid extents, and a player spawn inside a solid. Sprite textures use UUIDs from SampleAssets.cpp. The sample validates records, bindings, and texture files before publishing a new level. A failed F3 or F4 logs the file path and cause and keeps the current level. A successful replacement destroys only the previous level's entities. The retained GameInstance, Registry, renderer, quad, texture cache, and DLL continue to serve the host. F3 resets the level to its authored values. A compatible DLL reload preserves live values and camera selection.

Windows x64 Debug and Release builds passed. The native integration probe matched the original sprite transforms, tints, texture assignments and camera settings, and exercised F1, F2, Home, and both sprite bounce bounds. It verified 16 switches and 16 restarts per configuration. Old handles became stale, an unrelated entity survived, and the original quad and two texture objects were reused. Vulkan API traces showed 32 presentations and zero new image, buffer, sampler, or descriptor-set allocations in that loop. Malformed data, unknown camera and sprite references, an unknown UUID, and a missing texture file were rejected while the previous level kept rendering. Restoring the file recovered on the next restart.

Stock executable captures in both configurations showed the edited layout after F3, Courtyard after F4, and a pixel-identical original frame after restoring the file and switching back. A Debug integration probe used the same source watcher as --watch-game and changed MovementScale while Courtyard was active. DLL activation retained the level entity IDs, moving sprite pose and velocity, and alternate camera selection. The changed movement rendered, and F3 restored Courtyard's authored sprite and camera state. Test source and content edits were restored, and the native runs shut down cleanly.

Local probe sources, native logs, captures, and Vulkan API traces are under ignored out/validation/orthographic-levels/. Live DLL retention was checked in Debug. Native macOS/Linux were outside this milestone. The validation used the original shared texture set. Courtyard now has a dedicated wall PNG. See [live texture replacement](AssetSystem.md#live-courtyard-wall-reload). General scene serialization and resource retirement remain open.

## Courtyard gameplay

Launch the Windows sample with --game or --watch-game and press F4 to enter Courtyard. Move the cyan explorer with WASD, go around the purple wall, avoid the gold courier, and reach the green beacon. To win from spawn, follow the bottom edge to the right and then go up toward the beacon. To lose, go straight up from spawn into the courier's path. A win turns the explorer green and plays an ascending cue. A loss turns it red and plays a descending cue. The player stays in place until F3, and the courier keeps moving. F2 switches cameras, arrows pan, wheel or PageUp/PageDown zoom, and Home restores the selected camera. F4 returns to the original Orthographic level. Plain --orthographic displays both levels and provides camera controls. WASD movement and the moving sprite use the game DLL.

Application processes frame controls once and renders once per drawable frame. The Orthographic client advances gameplay in 1/60-second steps and runs at most four steps per frame after a stall. The keyboard snapshot holds the latest held state and buffers transitions in arrival order until the first tick consumes them. A press and release between ticks still moves the player for one tick. Mouse and wheel stay on the frame path. Minimize and successful level replacement clear pending simulation time and transitions, and the first restored frame starts at zero delta. Drawable frames have a 120 Hz cap and minimized frames have a 30 Hz cap. The loop sleeps only for the remainder of each frame budget. Perspective orbit and DLL free-fly use the frame update path. Rendering uses the latest simulated pose.

GameBindings passes nullable camera and player handles by value. The DLL supplies normalized player displacement in GameUpdateResult, and the level resolves it with ResolveMovementXZ before writing the existing Transform once per moving tick. The swept XZ box stops at the first wall and slides with any remaining tangent motion. Y, rotation, and visual scale are independent of collision. The beacon and courier use trigger overlap after player and courier movement each tick. Edge contact counts, and a courier hit takes precedence over reaching the beacon. The level returns an outcome only on the tick it finishes, which lets the host emit one cue.

Further collision and physics work is deferred. See the [roadmap deferral](../ROADMAP.md#deferred-collision-and-physics).

AudioPlayer caches mono or stereo 16-bit PCM WAV clips at 8–96 kHz by asset ID. The sample owns the service, catalog, and attempt cues across level replacements and DLL reloads. Its private XAudio2 backend owns one source voice at a time and keeps sample data alive until the voice is destroyed. A successful F3 or F4 stops the outgoing cue. Compatible DLL reload retains the current cue and attempt state. Audio failure logs a warning and leaves the attempt playable. Native playback is implemented for Windows.

To move the goal while running, change the beacon position in staged Content/Levels/Courtyard.ravenlevel beside the executable and press F3. For a quick test, move it to X = -1.7 and Z = -2.4, then walk right from spawn. Move the platform sprite to move its wall, or change its solid record to resize the collider. A negative solid extent rejects the replacement and the current attempt stays playable. Keep permanent edits in the repository's Content/Levels directory.

With --watch-game, change PlayerSpeed in the game DLL source while Courtyard is active. A compatible replacement uses the new speed and retains the player pose, courier pose and velocity, selected camera, and attempt result. F3 restores authored values and the initial camera. The shared GameUpdateResult signature requires a host rebuild and restart when first adopted. Subsequent game-body edits use the existing watcher.

Windows x64-debug and x64-release built and passed native-input playthroughs, restart and switch, live goal edits, invalid solid rollback, and minimized input reset. Standalone orbit and DLL free-fly passed mouse/wheel and picking checks. The cadence probe found identical poses and outcome ticks at simulated 30, 60, and 144 Hz render schedules. It checked a thin wall, sliding and corner contacts, simultaneous triggers, movement lock, short taps, and four-step catch-up. WASAPI loopback on the unmuted default Windows render endpoint captured one ascending and one descending cue per playthrough in each build. A 37-attempt loop per build had stable post-warmup handle counts, recovered from a missing WAV, and shut down cleanly. The Debug watcher changed the speed from 3 to 6 units per second, retained both active and finished state, and verified the authored reset after F3. Probe sources and logs are under ignored out/validation/courtyard-milestone/. Native macOS and Linux audio playback remains open.

## Live Courtyard shader reload

Build the Windows preset once. From the repository root run:

~~~powershell
.\out\build\x64-debug\RavenEngine\RavenEngine.exe --game --watch-shader
~~~

Use the corresponding x64-release path for Release. Press F4 and save an edit to [Texture.frag.hlsl](../RavenEngine/Private/Renderer/Shaders/Texture.frag.hlsl). The startup log prints the absolute source path configured by CMake, independent of the launch directory. Replace the return statement in PSMain with the following to see the change on opaque and transparent sprites:

~~~hlsl
float4 color = SurfaceTexture.Sample(SurfaceSampler, input.UV) * input.Tint;
return float4(color.bgr, color.a);
~~~

Save a syntax error to see the compiler diagnostic while the previous shader remains visible. Restore valid HLSL to recover. Keep the current PSMain entry, texture and sampler bindings, and stage inputs and output. The watcher checks the compiled SPIR-V against that fixed interface before pipeline creation. Use --watch-wall to watch the Courtyard PNG as well, and use --watch-game in place of --game to enable automatic game DLL builds.

The worker polls the shader contents every 40 ms and waits for 100 ms of stability. It takes an immutable source snapshot, runs the same CMake DXC and spirv-val recipe as a normal build in a private temporary directory, and checks the source again before handing off the result. The renderer builds an opaque and transparent pipeline from the same candidate and publishes both before recording the next frame. Each submitted frame holds its pipeline pair until its frame fence signals. The context retains the active SPIR-V across swapchain recreation. The normal staged shader provides the startup version, and live compilation writes only temporary outputs. The shader compiler and automatic game builds share the Windows process owner, with separate job and output handles for each build.

Stock Debug and Release captures verified changes in both passes during play and after a win. A syntax error and an incompatible descriptor binding kept both previous pixels visible. A temporarily missing source also recovered on the next valid save in both builds. The corrected save, minimize and restore, and resize showed the replacement. A focused probe verified entity identity, player and courier poses, courier velocity, the selected camera, and the completed result. F3, F4, wall PNG reload, and compatible DLL reload passed. A Debug run launched from the repository root and exercised the three watchers together. Temporary source edits were restored. Vulkan traces of 64 replacements per preset peaked at five live graphics pipelines including the color pipeline and returned to zero at shutdown. Core and synchronization validation reported no warnings or errors in the traced runs.

| Windows build | Save to first present, median / worst | Compile and validate, median / worst | Create both pipelines, median / worst | Longest reload frame |
| --- | ---: | ---: | ---: | ---: |
| x64-debug | 231.5 / 244.5 ms | 62.7 / 78.1 ms | 0.54 / 0.60 ms | 27.4 ms |
| x64-release | 240.3 / 267.0 ms | 63.9 / 88.1 ms | 0.30 / 0.37 ms | 27.5 ms |

Each timing run used 14 saves on the RTX 3070 Ti Laptop GPU without screenshot or API capture. Save-to-present starts when the file replacement completes and ends at the successful Vulkan present call. Reload frame time covers renderer work through present, including pipeline creation and any frame or presentation waits. The pipeline timings reflect the observed driver cache state. Validation scripts, captures, and logs are under ignored out/validation/live-shader/.

The Windows development workflow covers this one self-contained fragment shader and its existing interface. Shader include dependency watching, layout changes, other shader stages, and native macOS and Linux verification remain open. Driver-level pipeline allocation failures were not injected.

## Retained ECS state and game reload

Current contract as of 2026-11-21 (M1 step 2, [ECSDesign.md](ECSDesign.md) D1/D2).

Application owns a GameInstance containing a Registry. The Registry owns entity slots and component storage in RavenCore, and Entity values carry registry/index/generation identity. Every component derives from the empty, non-virtual `Component` struct in Component.hpp.

- A component is a standard-layout, trivially copyable and trivially destructible value with no virtual functions.
- Registry stores each component as a whole `T` value, in a pool keyed by its compile-time `TypeId`.
- Registry `Has`/`Add`/`Read`/`Write`/`Remove` are templates on the component type and copy whole values. No reference into storage escapes a call.
- Type-erased callers use `HasComponent`/`AddComponent`/`ReadComponent`/`WriteComponent`/`RemoveComponent` with a `ComponentInfo`.
- Entity destruction erases stored values before the slot is recycled, without running destructors.
- GetEntities returns a snapshot; stale handles still need liveness checks.
- Each component type is one sparse set (M1 step 3): a paged entity-index lookup (4,096 entries per page, allocated on demand), plus dense arrays of owning entity indices and of the values, aligned for the type. Add and Remove are O(1). Remove moves the last element into the gap, so dense order is not stable.
- Borrowed access (M1 step 4):
  - `View<A, const B>().Each(fn)` calls `fn(Entity, A&, const B&)` for entities with every listed component. It starts from the smallest pool, and returning `false` stops early.
  - `TryGet<T>(entity)` returns a pointer or null. `Single<T>()` returns the first component of a type, and `Count<T>()` counts them.
  - References and pointers are valid only during the current synchronous update, until the next structural change.
  - While a view iterates, `Add`/`Remove`/`Destroy` are rejected and logged, and `Create` throws `std::logic_error`. `Scene::DestroyEntity` throws before changing its own bookkeeping.
- Each slot records an owning scene ID set by `Scene::CreateEntity`. `SceneAccess` views, `TryGet`, `Single` and `Count` visit only entities the scene owns. Registry views visit everything.
- `SceneAccess::Query` still returns the scene's entities in authored order; it tests pool membership directly.
- Change ticks (M1 step 6):
  - `Add`, `Write`, mutable `TryGet` and non-const view types stamp the Registry's current tick. `Read`, `TryGet<const T>` and const view types do not.
  - `AdvanceChangeTick()` closes the current tick for a consumer.
- Cached world transforms (M1 step 6):
  - `Scene::UpdateWorldTransforms()` maintains `WorldTransformComponent` for owned entities with a Transform. It recomputes only changed entities and their descendants, parents first.
  - Perspective draw gathering calls it, then reads `View<const WorldTransformComponent, const StaticMeshComponent>`.
  - Gameplay `GetWorldTransform` and picking keep composing live values.

Derive from Component, declare direct fields, then put the shared macro in the public section using the qualified type identifier and every data member in declaration order:

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
registry.Add(entity, health);
registry.Read(entity, health);
health.Current = 65;
registry.Write(entity, health);
registry.Remove<MyGame::Health>(entity);

// Generic callers and tools:
const Raven::ECS::ComponentInfo& info = Raven::ECS::ComponentTraits<MyGame::Health>::Info;
registry.ReadComponent(entity, info, &health); // info.Name is "MyGame::Health"
```

The macro generates:

- `TypeName` (the stringified qualified identifier) and `TypeId` (its 64-bit FNV-1a hash).
- `FieldNames`.
- Mutable and const `GetFields` tuples.

Use the canonical qualified spelling; aliases are not separate component identities. `ComponentType` requires a mutable Component descendant with its own declaration and matching field lists. Typed calls with a type that inherits a parent's declaration without declaring its own are rejected.

`ComponentTraits<T>` checks each type at compile time and rejects these with a static assertion:

- virtual functions
- non-trivial copy or destruction
- a field list that doesn't cover every data member in declaration order (compared against `sizeof(T)` and `alignof(T)`)

`ComponentTraits<T>::Info` describes the type: id, name, size, alignment, and a layout hash of the name, field names and field sizes.

Each field must be approved by ComponentValue as well as standard-layout, trivially copyable/destructible and nonvolatile. Arithmetic/enumeration types are allowed; fixed arrays are checked recursively. Approved engine values are Vec3, Vec4, Mat4, Entity, AssetId, AssetRef<T> and MeshHandle. Other class types are rejected by default, including std::string_view, std::span, reference wrappers, pointer wrappers and owning containers. For a custom value struct, explicitly specialize `Raven::ECS::ComponentValue<MyValue> : std::true_type` after reviewing that it contains only self-contained values/IDs. Opt-in never bypasses structural checks. An opted-in class's hidden members require review because this declaration checks the field contract without inspecting its nested implementation. Members must represent values; borrowed/reference members and resource cleanup in retained data remain unsupported.

Components are aggregates, so `MyGame::Health{}` and designated initializers work.

- **Pool registration:** the first Add fixes a pool's name, size and layout hash. Registry copies the name, so no pool refers to a module's string storage. A later call with a different name, size or layout hash fails instead of reading or writing mismatched bytes.
- **Failure handling:** failed reads leave the destination unchanged.
- **Retained state:** Registry holds no virtual-table pointers, component objects or module callbacks. Local component objects must be destroyed before their module unloads.
- **Not supported:** owning component fields and per-type lifecycle hooks.

An empty declaration such as `RAVEN_COMPONENT(MyGame::Selected);` inside an empty Component-derived type defines a tag. It is stored as one byte. Add/Read/Write/Remove and entity destruction still check presence and entity identity.

List every data member once in the macro, in declaration order; the compile-time check enforces this. This is an in-process value contract, not a persistent file format or automatic layout migration. There is no manual layout-version counter.

TransformComponent exposes Position/Rotation/Scale/GetMatrix directly, StaticMeshComponent exposes Mesh/Tint, and SampleGame::Velocity exposes Linear. Component identity is the full qualified type identifier, so renaming a type changes its `TypeId`. The generated shared-header/build contract still rejects incompatible code replacement until a host/game rebuild and restart. The layout hash is a second guard, not a substitute.

M1 step 2 verification (2026-11-21):

- Windows Debug/Release builds pass, and `RegistryTests` and `HierarchyTests` pass in both.
- Every engine and game-DLL component passes the compile-time layout check.
- Compile probes rejected a virtual function, a missing field and an out-of-order field list.
- A Release `EcsBenchmark` at 100k entities measured Transform read+write at 5.5–6.2 ms (baseline 9.20 ms), the rotator query at 4.6–4.7 ms (5.96 ms) and a random typed read at 36–69 ns (78 ns). World transforms were unchanged.
- `python scripts/Benchmark-Windows3D.py reload` passed on the 500-mesh scene with a compatible DLL edit. Generation went from 1 to 2 and the rotor speed from 0.65 to 1.30 rad/s, with its phase retained and no scene recreation. Save-to-present was 3.17 s; the longest frame during reload was 29 ms. Local evidence: `out/benchmark-reload-6fw56bnj`.

M1 step 3 verification (2026-11-21):

- Windows Debug/Release builds pass, and both tests pass in both. `RegistryTests` covers interleaved removal across 1,000 entities.
- Release `EcsBenchmark` at 100k entities on an idle minimum-spec laptop, in two alternating rounds against the step 2 build:

  | Case | Step 2 | Step 3 |
  | --- | ---: | ---: |
  | Transform read+write | 5.0–5.3 ms | 2.1 ms |
  | Rotator query | 4.2–4.9 ms | 1.57 ms |
  | World transforms, draw pass | 9.1–9.7 ms | 4.9–5.3 ms |
  | Random typed read | 35–38 ns | 15–17 ns |

- The compatible DLL reload check passed again. Generation went from 1 to 2 and the rotor speed from 0.65 to 1.30 rad/s, with its phase retained and no scene recreation. Save-to-present was 2.94 s; the longest frame was 33 ms.

M1 step 4 verification (2026-11-21):

- Windows Debug/Release builds pass, and both tests pass in both. `RegistryTests` checks view membership, write-through, early stop, rejected structural changes, and `TryGet`/`Single`/`Count`. `HierarchyTests` checks that scene views skip other scenes and unowned entities.
- Release `EcsBenchmark` at 100k entities, in two alternating rounds against the step 3 build:
  - Mutable `View<Transform>` pass: 0.11 ms. The copy-based pass takes 2.1–2.3 ms.
  - Scene-scoped `View<Rotator, Transform>` over 10k matches: 0.09–0.13 ms. `Query` plus copies takes 1.5–1.6 ms.
  - `Query` itself improved from 1.5–1.6 to 0.83–0.91 ms.
- The Debug `Raven3DSmoke` run with Vulkan core and synchronization validation passed with no validation messages. It covered Puzzle and Collection completion through the view-based game code, foreign-entity exclusion, and compatible DLL replacement with retained progress.
  - `scripts/Test-Windows3D.ps1` needs PowerShell 7 (`pwsh`), which isn't installed on the laptop. Windows PowerShell 5.1 stops at its intentional failed-build step, so the remaining smoke executable step was run directly.
- The compatible DLL reload check passed: generation 1 to 2, rotor speed 0.65 to 1.30 rad/s, phase retained, no scene recreation. Save-to-present was 3.45 s; the longest frame was 32 ms.

M1 step 5 verification (2026-11-21):

- The game DLL registers MoveSprites and PlayerMovement (FixedUpdate) and FreeFly, MeshRotation and Interaction (Update).
- Windows Debug/Release builds pass and both tests pass in both.
- The Debug `Raven3DSmoke` run with Vulkan core and synchronization validation passed with no validation messages, driving the registered Update systems through Puzzle and Collection.
- The compatible reload check passed: generation 1 to 2, rotor speed 0.65 to 1.30 rad/s, phase retained, no scene recreation. Save-to-present was 2.99 s; the longest frame was 29.5 ms.
- Its `frames.systems.csv` accumulated FreeFly, MeshRotation and Interaction across both generations. At 500 meshes, Interaction averaged 5.1 µs per frame.
- The Orthographic/Courtyard fixed-step path was built but not run, following the 3D-only runtime testing direction.

M1 step 6 verification (2026-11-21):

- Windows Debug/Release builds pass, and both tests pass in both. `HierarchyTests` compares cached worlds with live composition after the first update, an idle update, a parent move, a view edit and a reparent.
- The Debug `Raven3DSmoke` run with Vulkan core and synchronization validation passed with no validation messages. Its hierarchy checks now use the cache: extreme parent scales, detach/reparent, an externally destroyed root rejected, and foreign entities left without a world.
- Release `EcsBenchmark` at 50k entities, where one pivot in five rotates and its mesh follows: the cached update took 0.52–0.57 ms. The previous draw pass recomputed everything in 2.7–2.8 ms. With nothing changed, the update took 0.15 ms.
- The compatible reload check passed: generation 1 to 2, rotor speed 0.65 to 1.30 rad/s, phase retained. Save-to-present was 4.19 s; the longest frame was 33.9 ms.
- The 4,000-mesh benchmark measured gameplay + world + gather at 0.37 ms p95, against 3.91 ms at M0, and 162.8 FPS. See [ECSDesign.md](ECSDesign.md#m1-step-6-3d-record-2026-11-21).

The component records below describe the earlier virtual parent and are historical.

Historical metadata-only refactor validation on 2026-10-19: Debug/Release engine, host, existing game DLL and temporary probe callers built. Component-only checks covered generated names, namespace separation, copied fields, removal and stale slot reuse. An expected compilation failure rejected std::string. Both native samples passed focused GPU checks, with clean Debug core/synchronization diagnostics. Logs remain under ignored `out/validation/game-reload/`: `{debug,release}-component-macro-build.log`, `*-component-macro-probe-build.log`, `*-component-macro-probe.log`, `*-component-macro-render.log` and `component-macro-invalid.log`. That evidence predates the virtual Component parent; it is not the current verification record. The probes remain temporary evidence; no tracked test suite or CTest integration was added.

Historical virtual-parent validation on 2026-10-19: Debug/Release builds and focused inheritance, virtual destruction, field copying, entity lifecycle and GPU checks passed. Retained movement fields survived reloading the same DLL and its final unload. An expected compilation failure rejected std::string. Logs remain at `out/validation/game-reload/{debug,release}-component-base-probe-build.log`, `*-component-base-probe.log`, `*-component-base-render.log` and `component-base-invalid.log`. This record predates the assessment fixes below.

### Component architecture assessment — 2026-10-19 (historical, virtual parent)

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

OrthographicSample initializes Transform/Sprite entities and the moving sprite's Velocity once. PerspectiveSample initializes its camera once and supplies its handle to the game update in free-fly mode. Both factory paths receive the host-owned GameInstance.

The implemented ownership boundary is:

| Part | Lifetime and responsibility |
| --- | --- |
| RavenCore | Remains loaded; implements Registry, engine services, rendering and GPU resource cleanup |
| Host and GameInstance | The host owns a GameInstance for each running game session and retains its Registry/game data across game-code replacement; scene-specific data belongs to the scene/Registry |
| Game module | Supplies replaceable game update code; does not own retained data or leave cleanup callbacks, virtual objects, or other references into unloaded code |

A GameInstance per running game does not require a process-wide singleton. Its retained implementation and destruction must remain available after a game module is unloaded. Future editor editing/play sessions are a design assumption, not a commitment to ship a complete visual editor in v1. The current application still runs one client/window; multiple concurrent play sessions are not implemented by this plan.

Application polls GameModule between updates and calls the active function with borrowed GameInstance, InputState, and FrameTime, plus a GameBindings value from the client. The host client then runs OnFrame. The DLL updates sprites with Velocity and runs camera and player control for the corresponding bound handles. In free-fly, the DLL is the sole writer of the selected camera pose after initialization. Polling/building continue while minimized. Gameplay and drawing pause, and the first drawable frame gets zero delta. Module-owned objects, callbacks, threads and retained pointers must end before unload.

Preserve the initialized renderer, project, active scene and asset service across game-code replacement. The scene path shares resources through AssetStore leases and releases unused managed resources behind generation-checked handles. Submitted frames retain native data until their fences complete. Legacy AssetResources and direct renderer creation remain pinned until context shutdown. Persistent asset UUIDs, runtime resource handles and entity identities have separate roles; see [ownership and retirement](SceneWorkflow.md#scene-and-resource-ownership).

Game callback aliases remain in ApplicationClient.hpp; Api.hpp holds separate engine/game export macros. GameModule.cpp owns replacement and polling/build orchestration. Win32GameModule.cpp contains native loading and a hidden child process/job with redirected diagnostics; shutdown cancels only its own build tree. Game implementation and state definitions remain in Private/Game. Application.cpp coordinates lifetimes and frame calls.

Engine implementation follows assistant-suggested, owner-applied snippets as clarified on 2026-11-01. The earlier general direct-edit authorization is withdrawn for engine code; a specific engine file edit requires an explicit owner request.

## Reload scope

`RavenCore.dll` and the host remain loaded. GameModule reserves a unique execution-copy directory beside the game output, copies the DLL, loads it by absolute path, checks the fixed C-linkage contract query, then calls `RavenRegisterSystems` and validates the system table (see [ECSDesign.md](ECSDesign.md#d6--systems-and-schedule)). A valid candidate replaces the old systems only between synchronous calls. The host drops a generation's system table before its library unloads. The previous library and its execution copy are released after activation. Failed copying/loading, missing exports, an invalid or empty system table, a registration exception and contract mismatches leave the active module intact. A registration exception, standard or not, is translated into a host-owned error while the candidate is still loaded, because its message and destructor live in the candidate's image (2026-11-24; checked with an ignored probe module that throws both kinds). Game code compiled with `/EHsc` assumes its `extern "C"` entry points do not throw, so MSVC warns (C4297) when `RavenRegisterSystems` throws directly. The build output is never loaded, so the linker can overwrite it.

The [default Windows launch](DevelopmentReload.md) enables source builds for Perspective in free-fly mode. The explicit `--game` and `--watch-game` commands still select Orthographic when used alone. Add `--free-fly` to select Perspective. F5 calls Reload. Since M5 the shared reload service ([HotReloadDesign.md](HotReloadDesign.md#h1--one-reload-service-with-dependency-records-q1-q4)) settles changes to the game sources and to the DLL output, including replacements with equal size and timestamps. Windows directory notifications wake it, and a periodic content audit catches missed changes. It watches Public headers, game C++ files, game and root CMake rules, presets, and CMake helpers. The game worker starts a build as soon as the sources settle, and pauses the DLL watch while the build runs so the linker can open the file. A source edit during a build queues a new build. The frame loop accepts a finished result between gameplay updates.

System policy (M5, [H4](HotReloadDesign.md#h4--system-table-policy-q5)):
- A new generation may add, remove, rename, reorder or re-phase systems. Activation logs one line per difference, and statistics continue for unchanged names.
- A system that throws is logged once and then skipped until the next activation (a rebuild or F5). What the failing call already wrote stays in the world. `GameSystemStats::Suspended` and the `suspended` column of the frame-trace system CSV report it.

Build loop (M5, [H5](HotReloadDesign.md#h5--dll-save-to-present-q6)):
- The game target precompiles the standard library, including `<format>`.
- `BuildGame.cmake` skips the child configure when only game `.cpp` files written after the last completed build changed. Any other change, including an edit that keeps its timestamp, still configures.
- `RAVEN_RELOAD_TRACE=1` logs the phases of each reload, and the recipe's phases in `AutoBuild.log`.

CMake generates a SHA-256 contract from all Public/game .hpp definitions, relevant build files, compiler/version, pointer size, configuration and C++ flags. Game .cpp bodies are excluded so logic can change. Any contract change, including a header comment, conservatively requires rebuilding/restarting the host; there is no automatic reset or migration. A separate content-derived build key forces compilation even when timestamps are equal. Component names must be unique and kept stable; byte-size checks alone do not prove layout compatibility. No developer-maintained layout counter is needed.

The standalone child project under each build's RavenEngine/GameBuild links RavenCore.lib as a file, with no dependency that can rebuild or clean RavenCore. Full builds, manual requests and the runtime watcher share the generated GameBuild/BuildGame.cmake recipe and process lock. It hashes supported inputs, the generated recipe/environment, engine import library, child cache and expected runtime artifacts. Unchanged verified results skip child configure/build; changed inputs rebuild incrementally, and missing/corrupted outputs trigger child-only recovery. The last completed record is replaced atomically only after a successful build and artifact verification. Automatic builds run asynchronously and log to GameBuild/AutoBuild.log; their compiler environment comes from the host configuration snapshot. Use the script instead of rebuilding the full engine while it runs. Only Windows/MSVC/single-config Ninja supports this path; other native module backends remain pending.

This is a matching-toolchain development interface, not a sandbox or compiler-independent plugin ABI. The supported sample dependency set is shared .hpp definitions and fixed CMake rules; adding private/external headers, libraries, generator flags or other dependencies requires extending the contract inputs and rebuilding the host first. Do not use a cpp-only edit to introduce persistent type definitions or change ABI-affecting pragmas/macros. Arbitrary layout migration and recovery from native code crashes remain outside scope. The DLL's loader initialization runs before its query can be checked, so load only trusted local build output. Shipping packages should omit this development module/build workflow.

Build or pre-activation validation failures should retain the previous working module. This does not guarantee recovery from arbitrary execution errors in newly activated native code. Automatic migration of arbitrary C++ object layouts is outside the first example. End-to-end completion requires actual changed behavior with retained ECS state and repeated replacement, not just a DLL export check.

## Windows gameplay hot reload validation

Build `x64-debug` and `x64-release` from a Visual Studio developer shell. Run these commands from the workspace after building:

```powershell
.\out\build\x64-debug\RavenEngine\RavenEngine.exe --watch-game
.\out\build\x64-debug\RavenEngine\RavenEngine.exe --watch-game --free-fly
```

Run the examples separately. In Orthographic, edit `MovementScale` in `RavenEngine/Private/Game/RavenGame.cpp` and observe the moving sprite. In Perspective, hold the right mouse button and drag to look, use WASD to move and Q/E for world-vertical travel. The wheel changes FOV. A left-click logs the ground hit from the presented camera view. Change `FreeFlySpeed` and repeat a movement input, or change the wheel multiplier and scroll. Source polling builds and activates compatible edits while retaining the existing camera. `--free-fly` without `--watch-game` also loads the DLL and supports F5. Standalone `--perspective` keeps orbit controls. Restore test edits when finished.

Shared headers and build-rule edits require a host rebuild and restart. Routine `.cpp` logic edits use the DLL watcher. While a host runs, use `cmake -P out/build/x64-debug/RavenEngine/GameBuild/BuildGame.cmake` for a manual game-only build. F5 reloads the resulting DLL.

To reproduce rejection in a running Debug watcher, save the original game source and shared definitions first. Add an intentional compile error to RavenGame.cpp and verify that the active movement still responds. Restore the game source, then make a reversible comment edit in a shared header. The resulting DLL has a different contract and the host reports rejection. Restore the header and make a valid game-body edit to verify recovery. The validation probe used a separately built incompatible DLL for the second case.

- Windows x64 Debug and Release builds and native runs passed. Programmatic input checked standalone orbit, free-fly strafe and right-drag, ground picking after resize, and minimize/restore without a pose jump. A Vulkan integration probe checked both bounce bounds, 30/60 Hz movement, FOV and pose independence, and the host frame's use of the updated sprite and camera.
- In the Debug integration probe, two compatible source edits changed both movement behaviors in the first tested frame after replacement. Registry/entity identity, both selected camera handles, camera pose/lens and sprite position/rotation/scale/velocity carried through activation. Submitted mesh, shared quad and cached texture object addresses stayed stable. A compiler error and an incompatible contract both kept generation 4 active and rendering. A valid edit activated generation 5, and restoring the original source activated generation 6. The sample entities were cleaned up at shutdown.
- Save to first verified rendered effect took **2.250 s** and **1.969 s** for the two compatible edits. Recovery took **2.312 s**. These are local Debug measurements with the existing polling and background-build path on the RTX 3070 Ti Laptop GPU system. The stock `--watch-game --free-fly` executable also rendered two wheel-response changes while preserving the prior lens and off-center ground hit across each replacement.

Local probe sources and logs are under ignored `out/validation/midterm-gameplay/`. `sample_smoke.py` drives native windows, `GameplayProbe.cpp` verifies retained state and renderer resources with the production sample classes, and `watch_probe.py` and `watch_live.py` exercise the two watcher paths. The intentional bad build log is `x64-debug/intentional-failure.log`. These checks used Windows. Live replacement timing and failure cases were measured in Debug.

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

## Live Perspective level editing and completion audio

Build a Windows preset once, then run from the repository root.

~~~powershell
Set-Location -LiteralPath 'D:\RavenEngine'
& 'D:\RavenEngine\out\build\x64-debug\RavenEngine\RavenEngine.exe' --watch-game --free-fly --watch-mesh
~~~

Use `x64-release` for the Release build; [SceneWorkflow.md](SceneWorkflow.md#complete-launch-commands) includes all complete 2D/3D commands. `--watch-game` enables automatic scene/project watching in both sample families. The log prints the source file, such as `D:\RavenEngine\Content\Levels\Perspective.ravenlevel`. A valid save restarts the authored attempt automatically, and F3 explicitly restarts the same file. Ordinary runs read staged Content beside the executable.

The settled-file worker polls contents every 40 ms and waits for 100 ms of stability. SceneSession takes the immutable source snapshot, runs the host codec, resolves scoped dependencies and prepares owned entities before replacing the active scene. It checks the bytes again before publication. Invalid or missing data preserves the attempt; dependency-blocked candidates retry and corrected data recovers automatically. F3 seeds the replacement watcher from the accepted bytes to avoid a duplicate reset. The original validation below used the earlier sample-specific parser worker and staged paths; the current shared session path and source roots are recorded in [SceneWorkflow.md](SceneWorkflow.md).

Perspective owns an AudioPlayer across level and game-DLL replacements. Reaching the beacon plays the existing Courtyard win WAV once. Compatible DLL activation preserves the completed level and cue. A successful level replacement stops any outgoing cue, clears completion through the new level, and lets the next completion play it again.

Windows Debug and Release builds and native runs passed. A state probe covered automatic position changes, malformed and empty files, invalid bindings, unknown asset IDs, missing-file recovery, manual restart without a duplicate reset, burst saves, and repeated replacement with stable renderer resources. Stock runs completed at a saved beacon position without F3 and retained completion through a compatible DLL replacement. Windows endpoint loopback captured exactly three ascending cues across the initial completion, automatic reload, and manual restart. Separate loopback runs verified that both restart paths cut off an outgoing cue. One save-to-activation observation was 0.190 seconds in Debug and 0.179 seconds in Release. These are local single-save observations. Native macOS/Linux playback and tracked regression coverage remain open. Local sources, logs, and endpoint recordings are under ignored `out/validation/live-perspective-level/`. Test level files and game DLL outputs were restored.

## Shared mesh materials and lighting

Perspective and Gallery now use the shared mesh material path. Their versioned level records, retained components, vertex and instance layouts, normal transforms, asset lifetimes, launch workflow and Windows Debug/Release evidence are documented in [MeshMaterials.md](MeshMaterials.md). Shared header changes require rebuilding and restarting the host once through the generated contract. Routine compatible game-body edits then use the existing DLL watcher.

## Reloadable 3D scene gameplay and binary authoring

The current 3D path provides borrowed SceneAccess and presented-frame interaction spans through GameBindings. Queries and copied component read/write are scoped to the active Scene; gameplay behavior and retained progress live in the DLL-driven ECS systems. The host owns scene/resource lifetimes, draw gathering and completion notifications. 3D level files now use the binary SceneDocument codec and RavenSceneTool save/export path. Earlier text-editing instructions and their dated validation records above are historical; use [InteractionGame.md](InteractionGame.md) for current file format, complete launch/authoring commands, ownership, recovery and validation.
