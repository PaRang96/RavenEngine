# M5 hot reload completeness design

Status:
- **Done on Windows, 2026-11-23.**
  - Designed and decided by the owner (Q1–Q7).
  - Implemented in one pass and reviewed by the owner (D1–D3), with the gaps found in that review fixed.
  - Committed as `8536ac0`. Windows CI passed (x64-debug and x64-release).
- Sections H1–H7 below are the agreed design. Where the code differs, [Implementation notes](#implementation-notes-2026-11-23) says how and why, and the [M5 exit record](#m5-exit-record-2026-11-23) has the measurements.
- macOS and Linux follow through the same platform interfaces (directory watching, build processes, module loading) as their M4 backends land.

## Reviewer guide

M5 is one commit, `8536ac0` ("Complete hot reload with a shared reload service (M5)"). Earlier milestones have their own records: [ECSDesign.md](ECSDesign.md) (M1), [RendererDesign.md](RendererDesign.md) (M2), [GpuDrivenDesign.md](GpuDrivenDesign.md) (M3), and [InputProcessing.md](InputProcessing.md) and [PlatformHandoff.md](PlatformHandoff.md) for the Windows M4 items in `869ea05`.

**Where the code is, by decision:**

| Area | Files |
| --- | --- |
| H1 Reload service, records | [ReloadService.hpp/.cpp](../RavenEngine/Private/Core/ReloadService.cpp), [ImporterVersions.hpp](../RavenEngine/Private/Core/ImporterVersions.hpp), [SettledFileWatch.hpp](../RavenEngine/Private/Assets/SettledFileWatch.hpp) (typed adapter), consumers in `AssetStore.cpp`, `AssetResources.cpp`, `SceneSession.cpp`, `SceneProject.cpp`, and the records file in `Application.cpp` |
| H2 Shader programs and checks | [ShaderLibrary.hpp/.cpp](../RavenEngine/Private/Renderer/Shaders/ShaderLibrary.cpp), [ShaderInterface.hpp/.cpp](../RavenEngine/Private/Renderer/Vulkan/ShaderInterface.cpp) (vertex and compute), `FragmentShaderInterface.cpp` (fragment), [CompileShader.cmake](../cmake/CompileShader.cmake), publication in `VulkanContext.cpp` (`ApplyShaderReloads`, `UpdateLitBuild`), `VulkanGpuScene.cpp` (`ReplacePipelines`, expected layouts) |
| H3 Debug views | [RenderView.hpp](../RavenEngine/Public/Raven/Renderer/RenderView.hpp), `VulkanClearPass.*`, `VulkanColorPipeline.*` (line mode), `Unlit`/`Normals`/`Wireframe.frag.hlsl`, F7 in `PerspectiveSample.cpp`, `fillModeNonSolid` in `VulkanPhysicalDevice.cpp`/`VulkanDevice.cpp` |
| H4 System policy | `GameModule.cpp` (`RunPhase`, `LogSystemChanges`), `GameSystemStats::Suspended` in `GameModule.hpp` |
| H5 DLL loop | `GameModule.cpp` (worker on the service, phase timing), [ReloadTrace.hpp](../RavenEngine/Private/Core/ReloadTrace.hpp), [BuildGame.cmake.in](../cmake/BuildGame.cmake.in) (configure skip, phase timing), `Game/CMakeLists.txt` and `RavenBuildSettings.cmake` (PCH) |
| Scripts | `Test-Windows3DLive.py` (new cases), `Benchmark-Windows3D.py` (`--view`), `Test-Windows3D.ps1` (PowerShell 5.1) |

**Worth the closest look:**
1. **Concurrency in `ReloadService.cpp`.**
   - The watch thread and two workers share per-subscription state under one mutex each, plus a service mutex. A thread never holds a state mutex while taking the service mutex.
   - A subscription's destructor waits for its running import, so a consumer is never called after destruction.
   - Consumers are called under their state mutex (`Accept`, `Changed`) and must not call back into their own subscription there.
2. **The supersede and rebase rules** in `Service::Run`.
   - An import is accepted only if its inputs still match the snapshot it read.
   - When an import replaces its inputs (a shader's includes), the new contents are taken right after that check, and dropped if the import was superseded.
3. **`GameModule`'s DLL watch pause.** The linker fails if anything holds the DLL open, so the watch pauses while a build runs.
4. **The configure skip** in `BuildGame.cmake.in`. It must still configure for any change other than a `.cpp` written after the last completed build, because configuring is what rebuilds an edit that keeps its timestamp.
5. **Shader interface expectations** come from C++ `offsetof` (`VulkanGpuScene::GetBufferLayouts`, `LitVertexInterface` in `VulkanContext.cpp`). Changing a GPU struct in C++ updates them automatically; changing the fixed vertex or fragment interface does not.

**How to reproduce** (Visual Studio x64 developer shell, Python 3, Vulkan SDK layers):

```powershell
cmake --build out/build/x64-debug
ctest --test-dir out/build/x64-debug --output-on-failure
python scripts/Test-Windows3DLive.py x64-release
python scripts/Benchmark-Windows3D.py scale review --meshes 50000 --view wireframe --repeats 1
```

Set `RAVEN_RELOAD_TRACE=1` for per-phase timing, and `RAVEN_FRAME_TRACE=<file.csv>` for frame timing plus the `.systems.csv` and `.reloads.csv` files beside it.

**Evidence that is not tracked:** some results came from temporary probes in the ignored `out/m5-probe/` folder, and they are not in the repository:
- the DLL save-to-active timings
- the system-policy check
- the record and thread-count measurement
- the equal-timestamp game edit

Each result below says where it came from. The tracked live script reproduces every reload path except the system policy.

## Goals and fixed constraints

- **Exit (roadmap):** code, every shader stage and supported assets reload with recorded timings and failure recovery.
- **Roadmap items:**
  - Reload vertex, fragment and compute shaders, with include tracking.
  - Record each asset's source, dependencies and build outputs, and reimport when a dependency changes.
  - Set a policy for added or removed systems.
  - Cut DLL save-to-present from about 3.5 s.
  - Extend last-good recovery and diagnostics to every new path.
- **Owner request (2026-11-23):** prepare general-use shaders such as unlit and wireframe. They are covered by H3.
- ECS and hot reload stay joint goals:
  - Reload must keep host-owned world state.
  - Renderer and cache state must survive game-DLL replacement.
  - Nothing retained may hold a module pointer.
- **No throwaway work.** Steps may be larger if that keeps every piece in the final design.
- Keep tracked tests minimal. Verify with the smoke script, live sessions and focused probes.
- 3D is the primary target, and the RTX 3070 Ti laptop is the minimum spec for every budget.

## Where reload stands after M3

| Path | Today | Gap |
| --- | --- | --- |
| Lit and textured fragment shaders | `FragmentShaderWatch` runs one worker thread per watched shader. Each compiles with the build's DXC and spirv-val, checks a fixed interface, and publishes a pipeline between frames. Release save-to-visible is 234–289 ms. | Vertex and compute shaders never reload. The only tracked include is the hard-wired `Raven/Material.hlsli` (G4). `GpuScene.hlsli` edits are ignored. |
| PNG and FBX | `AssetStore` gives each watched asset its own `SettledFileWatch` thread and directory watch. The worker decodes settled bytes, and the store publishes behind the same handle. | Threads grow with the asset count. Nothing records what an asset was built from, so a dependency cannot trigger a reimport. |
| Scene and project files | `SceneSession` and `SceneProject` have their own watchers. | They are a third copy of the same settle-and-audit logic. |
| Game DLL | `GameModule` watches sources and the DLL, builds through `BuildGame.cmake`, then loads a copy and activates its system table. Save-to-present is about 3.4–3.6 s. | No timing breakdown exists. Earlier records suggest the compile and link take about 2.2 s, and detection, settle, verification, copy and load take the rest. |
| Systems | Each DLL generation registers its whole table. The host replaces it atomically, and per-system statistics stay keyed by name. | Added, removed and reordered systems are accepted silently. A system that throws is logged and called again every frame. |
| Component layout | Any edit to a Public or game header changes the contract hash, so the host must be rebuilt and restarted. | Field migration was moved here from M1. Host sample code compiles the 3D components (`PerspectiveLevel.cpp`, and `OrthographicLevel.cpp` uses `GameComponents.hpp`), so their layout cannot change under a running host. See H6. |

## How M5 serves the v1 goals

| Decision | Hot reload | ECS | Performance | Cross-platform | Future editor |
| --- | --- | --- | --- | --- | --- |
| H1 One reload service with dependency records | Every path shares settle, audit, last-good and diagnostics | — | Fixed thread count, whatever the asset count | One `DirectoryWatch` consumer per platform | The records are what an asset browser shows |
| H2 Shader programs for every stage | Vertex, compute and include edits reload | — | Compute culling stays reloadable without a restart | Same DXC flags on every host | — |
| H3 Debug views (wireframe, unlit, normals) | Their shaders reload like any other | — | Same indirect commands, one extra pipeline per view | Wireframe needs `fillModeNonSolid` (MoltenVK has it) | Editor viewport modes |
| H4 System table policy | Added and removed systems are reported; a failing system is contained | Systems stay stateless functions over components | — | — | Play-session reset reuses it |
| H5 DLL loop profiling and fixes | Shorter edit → observe loop | — | — | Timing hooks are platform-neutral | — |
| H6 Field migration (deferred) | Removes most restarts later | Needs a runtime field table | — | — | Editor component editing needs the same table |

## Decisions

### H1 — One reload service with dependency records (Q1, Q4)

A host-owned `ReloadService` replaces the per-asset and per-shader watcher threads.

- **Watching:** one thread owns one `DirectoryWatch` over the source roots:
  - the Content source root
  - the engine shader directory
  - the game sources and CMake inputs the DLL builder already watches
- **Settling:**
  - A file counts as changed only after its bytes stay unchanged for 100 ms.
  - A pass reads an input's contents only when its size or write time changed.
  - A 2 s content audit catches missed notifications, and edits that keep both size and timestamp (D3; see [Implementation notes](#implementation-notes-2026-11-23)).
- **Dependency records:** each consumer (an asset ID, a shader program, a scene, the project, the game module) has one in-memory record. It holds:
  - every input path, with the content hash of its last successful import
  - the importer version (Q4 A+)
  - the inputs that triggered the last import
  - what was produced (as the consumer describes it), and when
  - import, publish, failure and superseded counts and times, and the last diagnostic
- **Invalidation:** a settled input change marks every consumer whose record lists that input.
  - Dependents reimport on a small worker pool (two threads), or through an external compiler process.
  - A result superseded during its import is dropped, as today: the latest save wins.
- **Publication is unchanged:** results still publish where they do now:
  - `AssetStore` between frames, behind the same handle
  - the renderer at its pipeline publication point
  - `SceneSession` at its attempt boundary
  - `GameModule` between gameplay updates

  In-flight frames keep their resources until timeline retirement.
- **Dependencies by type:**
  - PNG: its source file.
  - FBX: its source file. External texture references join when M6 imports multi-object FBX files with materials.
  - HLSL: the source plus every include, from DXC's `-MF` dependency list. DXC writes that list instead of compiling, so it runs as a separate pass before each watched compile. It is read even when the compile fails, so a failed program still follows its includes. This replaces the G4 header special case.
  - Scene: its file. Its asset references keep their own records.
  - The project is its own consumer. `SceneSession::UpdateProject` rebuilds the active scene only when a project edit changes that scene's source. That keeps the existing rule that a metadata edit leaves the running level alone, so the scene's watch does not list the project. *(Corrected in review, 2026-11-23: the draft listed the project as a scene input.)*
- **Builds without watching:** the service exists only in watching launches. `--no-watch` and player builds create no thread and load staged outputs as today.
- **Build outputs (Q4, decided A+):** records live in memory only, but their key is cache-ready.
  - **Today's loading is unchanged:** PNG and FBX keep decoding from source at load, and shaders keep the build's SPIR-V at startup.
  - **The cache-ready key:** the content hash of every input, plus an importer version. Each importer (PNG, FBX, scene, project, HLSL) has its own version in `ImporterVersions.hpp`, bumped whenever its output can change for the same input. A shader cache must also key on the toolchain.
  - **Adding the cache later:** a persistent derived-data cache (like Unreal's DDC) then only saves and loads these records. Nothing is rewritten.
  - **It is built at the first of these triggers:**
    - GPU texture compression or mip generation
    - mesh clustering or LOD generation
    - many shader permutations from M6 material shaders
    - editor work
    - M7 cooking
    - a measured slow load

### H2 — Shader programs for every stage (Q2)

- **Program table:** every engine shader is one program entry: source, stage profile, entry point and interface kind. This covers nine programs:
  - `Color.vert`/`.frag`
  - the three H3 view fragments
  - `Texture.vert`/`.frag`
  - `Cull.comp` and `Scatter.comp`

  Compiling uses `CompileShader.cmake` with the same DXC and spirv-val flags as the build, after the `-MF` dependency pass.
  - `FragmentShaderWatch` is deleted. Its compile, scratch and stop logic moves into the program table.
- **Pipelines:** a graphics pipeline is rebuilt from its (vertex, fragment) pair when either stage changes. A compute pipeline is rebuilt from its compute words.
  - Both publish at the existing frame-thread point.
  - The frame that recorded with the old pipeline keeps it until its timeline value completes.
  - Swapchain recreation reuses the accepted words, as the fragment path does now.
- **Interface checks (Q2):** a new SPIR-V reader, `ShaderInterface.cpp`, checks vertex and compute shaders with no new dependency. `FragmentShaderInterface.cpp` keeps checking fragment shaders. A candidate that fails a check is rejected, and last-good stays active.
  - **Vertex:** descriptor bindings match the fixed set layouts (frame set 1, scene set 2). Inputs match the fixed vertex layout. Outputs match the fragment inputs at the same locations and widths.
  - **Fragment:** the M3 check, now accepting a shader that reads only some of the inputs and skips the texture and sampler. Everything it declares must still match.
  - **Compute:**
    - Bindings match the GPU-scene set layout.
    - The `LocalSize` execution mode equals the 64 threads the dispatch assumes.
    - The storage-buffer member offsets and array strides equal the C++ record layouts, so a `GpuScene.hlsli` struct edit that the engine is not built for is rejected.
- **Header edits:** an edit to a shared header recompiles every dependent program on the two reload workers, one compiler process each. The renderer publishes the dependents of one header change together, so a vertex/fragment pair never mixes versions.
- **Launch flags:** watching launches watch every engine shader. `--watch-shader` is kept for explicit non-default launches and now enables the whole table.
- **Not recoverable:** a shader that hangs the GPU (for example, an endless loop in `Cull.comp`) can cause device loss. Device loss stays fatal in v1.

### H3 — Debug views and general-use shaders (Q3)

Unlit shading already exists: `MaterialComponent::Lit = false` makes `RavenApplyLighting` return the albedo in the single lit pipeline. What is missing is a way to *look* at the scene differently, plus a path to more material shaders later.

- **M5 adds renderer debug views:** a renderer-wide setting with `Shaded` (the default), `Unlit` (base color only), `Normals` and `Wireframe`.
  - **Wireframe:** a `VK_POLYGON_MODE_LINE` variant of the lit pipeline, drawn depth-tested over the shaded image.
    - It needs the optional `fillModeNonSolid` feature, which is enabled when present. MoltenVK reports it.
    - Without the feature, the view reports itself unavailable instead of failing.
  - **Unlit and Normals:** fragment variants written against `Raven/Material.hlsli`.
  - **Drawing:** every view issues the same GPU-culled indirect commands with a different pipeline, so cost stays one extra draw per mesh range.
  - **Reload:** the view shaders are ordinary program entries, so they reload and are checked like every other shader.
  - **Samples:** a sample key cycles the views. When the M6 key bindings land, that key becomes a binding.
- **Per-material shaders move to M6:** a material referencing a `ShaderAsset` (the type is already declared), drawn with one pipeline per shader.
  - The G5 command array already supports it. Commands are grouped per (shader, mesh), and each shader draws its sub-range with `vkCmdDrawIndexedIndirect(offset, count)`, so nothing in M3 or M5 is rewritten.
  - It is triggered by the first game that needs a second material shader.

### H4 — System table policy (Q5)

- **Changes are allowed:** a new module generation may add, remove, rename, reorder or move systems between phases.
  - Activation logs one line per difference from the previous table.
  - Statistics survive for systems whose name is unchanged. A renamed system counts as a removal plus an addition.
- **Failing systems are suspended:** a system that throws is skipped from then on, with one error line, until the next module activation (a rebuilt DLL or F5). Today the error is logged and the system is called again every frame, which floods the log and keeps mutating state halfway.
  - The world stays as the failing call left it. No rollback of component values is attempted.
  - Suspended systems are listed in the per-system statistics.
- **No rollback to the previous DLL:** rollback would mean keeping the old library loaded after activation, and the world may already depend on the new code.

### H5 — DLL save-to-present (Q6)

- **Step 1 adds opt-in phase timing, not a probe that is thrown away.**
  - It is enabled by a `RAVEN_RELOAD_TRACE` environment variable, like `RAVEN_FRAME_TRACE`, so ordinary builds do no extra work (owner, 2026-11-23).
  - The one summary line per reload (H7) stays on.
  - Player builds have no build loop and no reload timing.
  - When enabled, the engine log and `AutoBuild.log` give each phase's duration:
    - settle (the reload service's line for the game sources)
    - the wait before the build starts
    - lock, contract and input hashing, child configure (or its skip), compile and link, and artifact verification (`AutoBuild.log`)
    - the hand-off to the frame thread
    - stamp, copy, load and system registration; the next frame presents the result
- **Fixes are chosen from that breakdown.** Of the candidates, in order of expected value:
  - *Landed:* start the build as soon as the reload service settles the change. This replaced the 500 ms poll and 350 ms debounce.
  - *Landed:* give the game target the standard-library PCH it lacked.
  - *Already in place:* incremental linking for Debug game builds; CMake's Debug defaults already use `/INCREMENTAL`.
  - *Landed, not in the original list:* skip the child configure for `.cpp`-only edits.
  - *Not needed:* skip rehashing unchanged inputs, and drop process launches. Hashing measured 31–53 ms, and the breakdown showed no other launch worth removing.
- **Target:** Q6 set ≤ 1.5 s for Debug and Release, provisionally. After the breakdown, the review set Debug ≤ 1.5 s and Release ≤ 2.0 s (D1), because the optimized build alone takes 1.4–1.7 s.

### H6 — Field-level component migration (Q7)

- **Decided: deferred until after M6's generic component serialization.** Migration needs a runtime table of field names, offsets and types for each component, and generic serialization needs the same table. Building it once in M6 avoids writing it twice.
- **Why not M5:** host code compiles today's components.
  - `PerspectiveLevel.cpp` uses the 3D interaction components, and `OrthographicLevel.cpp` uses `GameComponents.hpp`.
  - Changing such a layout under a running host is unsafe, whatever the migration does.
  - Only components the host never compiles could migrate, and the 3D sample has none.
- **After M6:** the host stops compiling game components. Pools whose layout hash changes are then migrated by field name: matching name and type copy, new fields take their default values, and removed fields are dropped. A type change rejects the module, and the host keeps the last-good module and world.

### H7 — Recovery and diagnostics

- **Every replacement path keeps last-good:** the shader programs, assets, scene and project, and the DLL.
- **Records:** [ImporterVersions.hpp](../RavenEngine/Private/Core/ImporterVersions.hpp) gives each importer (PNG, FBX, scene, project, HLSL) its own version, so the cache key is complete. `<frame trace>.reloads.csv` writes every record at shutdown.
- **Log output:** each reload attempt writes one line with:
  - the consumer
  - the triggering input
  - the result (published, rejected, or superseded)
  - its import and publish times

  A rejection adds the compiler, decoder or interface-check diagnostic, capped at 8 KiB as now. As built:
  - Assets and the legacy asset service: `Replaced PNG <path>; trigger <file>; import … ms, publish … ms`. FBX is the same.
  - Scenes: `Reloaded scene <path>; …`
  - The project: `Published project metadata; …`
  - Shaders, one line per published group: `<group> shader reload presented: <programs>; trigger <includes>; compile …, pipelines …`
  - The DLL: `Game module activated, generation N, … ms after the change was seen, activation … ms`
  - Rejections: `Cannot reload <consumer>: <diagnostic>`
  - Superseded imports: `Reload superseded: <consumer>`
- **On-screen display** of diagnostics waits for text rendering, which v1 does not have.

## Proposed budgets (minimum-spec laptop, Release unless noted)

These are the targets as proposed before implementation. Final targets and results are in the [M5 exit record](#m5-exit-record-2026-11-23). Two targets changed:
- **DLL:** D1 set Release ≤ 2.0 s.
- **Idle CPU:** the 0.29 ms baseline came from an earlier machine state. The exit check therefore compares against the pre-M5 build measured the same day.

| Measure | Before M5 | Proposed target |
| --- | ---: | ---: |
| Fragment shader, save to visible | 234–289 ms | ≤ 300 ms |
| Vertex or compute shader, save to visible | not reloadable | ≤ 300 ms |
| Header edit, every dependent program published | lit fragment only | ≤ 500 ms |
| PNG / FBX reimport, save to visible | 667 / 133 ms | no regression |
| DLL one-file `.cpp` edit, save to present (Debug and Release) | ~3.5 s | ≤ 1.5 s, confirmed after step 1 (Q6) |
| Longest frame during any reload | ≤ 100 ms (M3 DLL check) | ≤ 100 ms on every path |
| Idle process CPU with everything watched | 0.29 ms/frame | ≤ 0.3 ms/frame |
| Reload threads | 1 per watched asset or shader, plus the game worker | fixed: 1 watch thread + 2 workers + the game builder |

The PNG figure includes the screenshot sampling delay ([DevelopmentReload.md](DevelopmentReload.md#measured-windows-results)).

## Verification

- **Live script:** `scripts/Test-Windows3DLive.py` (the default-launch check) gained cases for:
  - a vertex-shader edit and a compute-shader edit, each restored
  - an edit to each shared header (`Raven/Material.hlsli` already, and `GpuScene.hlsli` through the stride case)
  - one rejected case per new interface check: vertex/fragment mismatch, wrong `numthreads`, and a struct stride that does not match the C++ layout
  - each debug view, cycled with F7

  It runs with Python and needs no PowerShell 7. `scripts/Test-Windows3D.ps1 -DefaultLaunch` only forwards to it.
- **Live Release session:** every reload path, and wireframe on the 50k stress scene inside the M3 FPS budget.
- **A system added, removed and made to throw:** checked with a temporary probe. No tracked script covers the system policy.
- **Measurements:** the H5 phase breakdown before and after the fixes, the budget table above, and the reload thread count. All are recorded in the exit record.
- **Tracked tests:** none planned. No tracked test covers the watchers, and none is needed for them. If Q7 brings migration forward, it gets one `RegistryTests` case, because it changes a Registry contract.

## Migration steps

All seven steps were done on 2026-11-23, in one commit.

1. **H5 instrumentation:** record the DLL phase breakdown. The owner sets the target (Q6).
2. **H1:** the reload service and dependency records. PNG, FBX, scene, project and game-source detection move onto it, and the per-asset `SettledFileWatch` threads are deleted.
3. **H2:** the shader program table for every stage, DXC dependency files, the per-stage checks and pipeline republishing. `FragmentShaderWatch` and the G4 header special case are deleted.
4. **H3:** the debug views.
5. **H4:** the system table diff and failing-system suspension.
6. **H5 fixes:** chosen from step 1's breakdown, then measured again.
7. **H7 audit and exit check:** every path, the budgets and Debug validation.

## Implementation notes (2026-11-23)

Where the implementation differs from, or adds to, the decisions above:

- **DLL timing (H5) starts when the engine sees the change, not at the file's write time.** On the test laptop's D: drive, file timestamps read 1.5 s ahead of the system clock, so the external probe measures save-to-active instead. Detailed timing is opt-in through `RAVEN_RELOAD_TRACE` (owner, 2026-11-23). It adds one line per reload, plus one per build in `AutoBuild.log`.
- **The DLL loop fixes that landed:**
  - The game target uses a standard-library PCH that includes `<format>`. A one-file compile fell from 1.75 s to 1.0 s. Adding the engine's public headers to the PCH measured no further gain, so they stay out.
  - The child project's configure (about 250 ms) is skipped when the only changed inputs are game `.cpp` files written after the last completed build. Configuration stamps each source's content into its compile command, which is what rebuilds an edit that keeps its timestamp, so any other change still configures. An equal-timestamp edit was checked: it configured and reloaded.
  - The 500 ms source poll and the 350 ms debounce are gone. The reload service settles a change in about 120 ms, and the build starts at once.
  - `BuildGame.cmake` now declares `cmake_minimum_required`, because script mode otherwise rejects `IN_LIST`.
- **A pass reads contents only when an input's size or write time changed.** The first service version re-read every input's contents on any notification in any watched folder. During a game build the DLL's folder changes constantly, so it re-read every asset, shader and game source every 40 ms.
  - The game build slowed to 5.2 s in Release.
  - Three of four Release live checks showed one frame of 0.7–1.7 s during a game build; the pre-M5 baseline, run the same day, showed none above 113 ms.
  - The fix: passes compare file sizes and write times first. The 2 s audit still reads every input's contents, so an edit that keeps its timestamp is caught within 2 s. A paused subscription's folder also leaves the watch set.
- **The game DLL's watch pauses while a build runs.** The first live check found that reading `RavenGame.dll` on each directory notification made the linker fail (`LNK1104`). The old watcher never read the DLL during a build. `ReloadSubscription::SetPaused` restores that rule.
- **DXC lists dependencies in a separate pass.** With `-MF` or `-MD`, DXC writes the list and does not compile. `CompileShader.cmake` therefore runs a `-MF` pass first, only for the watcher. Each program is compiled once when watching starts, so its includes are known. That is about 100 ms per program, in the background on the two workers.
- **Header edits publish together.** `ShaderLibrary::TakeReady` returns nothing while any watched program is still settling or compiling. A rejected program counts as settled, so it never blocks the others.
- **The fragment check accepts a subset of the interface.** In Release, DXC's `-O3` removes the inputs, texture and sampler a shader does not read. The M3 check demanded all of them, so the first Release live check rejected the unlit, normals and wireframe shaders. Vulkan allows a fragment shader to ignore vertex outputs, so the check now requires only that everything declared matches.
- **Unlit shading already existed** as `MaterialComponent::Lit = false`. The Unlit view instead ignores lighting for every lit mesh.
- **Wireframe** draws the shaded image, then the same indirect commands with a `VK_POLYGON_MODE_LINE` pipeline. The lines are depth-tested and biased toward the camera, light on dark materials and dark on light ones. Textured and transparent draws are unchanged in every view.
- **Directory watching limit:** Win32 waits on at most 63 directories at once. A project whose watched inputs span more directories falls back to 40 ms polling. Today's content uses fewer than ten.
- **A failed compile at startup** is reported once and keeps the build's SPIR-V.

## M5 exit record (2026-11-23)

Measured on the minimum-spec laptop (RTX 3070 Ti Laptop GPU), Windows only. macOS and Linux follow through their M4 backends.

| Measure | Before M5 | Target | Result | Status |
| --- | ---: | ---: | ---: | --- |
| Fragment shader, save to visible (Release, layer-free) | 234–289 ms | ≤ 300 ms | 248–432 ms; median 268 ms | Met at the median; the first edit of a run reads 432 ms |
| Vertex shader, save to visible | not reloadable | ≤ 300 ms | 270–281 ms | Met |
| Compute shader, save to visible | not reloadable | ≤ 300 ms | 218–251 ms | Met |
| Header edit, every dependent published | lit fragment only | ≤ 500 ms | 337 ms (`Material.hlsli`, Release); 326–389 ms (Debug probe) | Met |
| PNG / FBX reimport, save to visible | 667 / 133 ms | no regression | 640 / 141–173 ms | Met |
| DLL one-file `.cpp` edit, save to active (Debug) | ~3.5 s | ≤ 1.5 s | 1.3–1.6 s (probe, 8 edits) | Met at the median |
| DLL one-file `.cpp` edit, save to active (Release) | ~3.5 s | ≤ 2.0 s (D1) | 1.63–1.87 s (probe, 5 edits; median 1.69 s) | Met under D1 |
| Longest frame during any reload (Release, layer-free) | 49.7 ms (DLL) | ≤ 100 ms | ≤ 61 ms on every path | Met |
| Longest frame, validation and screenshot layers | — | — | one 437 ms frame during a Release game build in one of two runs; cause not found | Tracked (D2) |
| Idle process CPU, everything watched (Release) | 0.36–0.49 ms/frame (pre-M5 baseline, same day) | no regression | 0.46–0.68 ms/frame | Within run-to-run noise |
| Reload threads | 1 per watched file + game worker (8 in a Collection session) | fixed | 3 service threads + the game builder for 17 consumers, measured in a Collection session | Met |
| Wireframe at 50k lit meshes, 20% animated | — | inside the M3 FPS budget (≥ 60) | 59.9 FPS (display cap), GPU p95 6.31 ms against 5.98 ms shaded; CPU preparation unchanged (3.2 ms p95) | Met |

**Where each result came from:**
- **Tracked live script:** the shader, header, asset, frame-time and idle rows (`Test-Windows3DLive.py x64-release`; layer-free pass unless noted).
- **Temporary probes:** the DLL rows and the thread count, both from `out/m5-probe/`.
- **Tracked benchmark script:** the wireframe row.

**DLL phase breakdown (H5), Debug, one-file `InteractionGame.cpp` edit,** from `RAVEN_RELOAD_TRACE` (all values in ms):

| Phase | Before the fixes | After the fixes |
| --- | ---: | ---: |
| Wait from the change being seen to the build starting | 536–541 | 2 |
| Build: lock + hash / configure / compile and link / verify | 2 + 31 / 245 / 1,750 / 59 (total 2,030–2,170) | 2 + 46 / skipped / 920–1,000 / 54–89 (total 1,120–1,460) |
| Hand-off, stamp, copy, load, register | 0–24, 10–11, 22–63, 0, 0 | 0–24, 8–11, 17–46, 0, 0 |
| Change seen to active | about 2,700 | 1,250–1,530 |
| Save to active (external probe) | 2,680–2,820 | 1,430–1,640 |

The settle before "change seen" adds about 120 ms.

In Release, the same edit builds in 1,420–1,700 ms and is active 1.63–1.87 s after the save. Before the fixes it took about 3.5 s, and 3.4–4.7 s with the first version of the reload service (see the implementation notes).

**Benchmark environment:** today the NVIDIA GPU also drove an external 3840 × 2160 monitor at 60 Hz. FIFO presentation was therefore capped at 60 FPS and GPU time rose for both views, so these runs are not comparable with M3's 126.5 FPS. Shaded and wireframe ran back to back under the same conditions.

### What was verified

- **Tracked live check** (`scripts/Test-Windows3DLive.py x64-release`, before the review fixes): every case passed with validation and screenshot layers, then again without layers, plus idle samples and a relocated `--no-watch` launch. The cases:
  - fragment, vertex and compute reloads with visual checks, and each restored
  - `Material.hlsli` and `GpuScene.hlsli` edits
  - rejections of a syntax error, an incompatible fragment input, a vertex/fragment mismatch, wrong `numthreads`, and a struct stride the C++ code does not write (`Color.vert` and `Cull.comp` both rejected)
  - all four render views via F7
  - the game rule edit, a failed game build and recovery
  - scene recovery and a malformed scene, PNG and FBX replacement
- **Debug live check** (`x64-debug`, final code including the review fixes): every case passed with validation and screenshot layers. Frames over 100 ms came only from startup, the goal-completion sound and level switches, none during a game build. The review fixes changed only reload records, log lines and docs; the Release check was not repeated after them.
- **Validation:** core and synchronization validation reported no messages in the layered passes or the Debug shader probe.
- **System policy (temporary Debug probe):** an added system that throws logged one failure and stayed suspended. The next generation logged the move of another system and started the suspended one again. Its removal was logged and no further failures followed.
- **Equal-timestamp game edit:** configured, rebuilt and reloaded (temporary Debug probe, 1.6–1.75 s).
- **Builds and tests:** Debug and Release build; `ctest` passes in both. `scripts/Test-Windows3D.ps1` now also passes under Windows PowerShell 5.1. Windows CI passed on `8536ac0`.
- **Records and summaries (temporary Debug probe, confirmed by the Debug live check):**
  - A PNG replacement logged its own name as trigger, with its import and publish times.
  - A `Material.hlsli` edit logged `trigger Material.hlsli` for the three fragment shaders whose output changed.
  - The records file listed all 17 consumers with their importer versions and outputs.

### Known limits

- **Edits that keep both size and timestamp** (accepted, D3) are now found by the 2 s audit instead of at once. The equal-size shader case took about 2.3 s, against 0.27 s before, and was still published correctly. This is the cost of reading only changed inputs. Waking passes per directory would restore the old latency; it needs `Platform::DirectoryWatch` to report which directory changed.
- **The goal-completion frame** (about 110 ms when the completion sound first plays) predates M5. The baseline shows it too.

## Out of scope for M5

- Per-material shaders (M6, see H3).
- A persistent derived-data cache (see Q4).
- Field migration (see H6).
- Recovery from native crashes or GPU hangs.
- Reloading `RavenCore` or host code. MSVC Edit and Continue stays separate.
- On-screen diagnostics.

## Owner decisions

Q1–Q7 were decided before implementation; D1–D3 in the owner's review of the implementation.

| # | Question | Options | Decision |
| --- | --- | --- | --- |
| Q1 | Watching architecture | **A:** one reload service with dependency records for shaders, assets, scenes, the project and game sources. Fixed thread count, one copy of the settle logic, and the records the editor and a disk cache need. **B:** extend each watcher separately, adding vertex/compute watchers and keeping per-asset threads. Smaller first diff, but four copies of the logic and threads that grow with the asset count. **C** (considered in review): a shared service for assets and shaders only, with scene, project and game-source watching kept separate. | **Decided: A** (owner, 2026-11-23) |
| Q2 | Shader interface checks for vertex and compute | **A:** reflect and check every stage against the fixed layouts, including `numthreads` and struct strides. Catches mismatches that validation layers miss in Release. **B:** SPIR-V validation only, relying on pipeline creation. No parser work, but a stride mismatch silently corrupts the GPU scene. | **Decided: A** (owner, 2026-11-23) |
| Q3 | General-use shaders | **A:** debug views (wireframe, unlit, normals) in M5, and per-material shaders in M6 on the same command layout. Gives you the views now, and builds nothing twice. **B:** per-material shader assets in M5 as well. The general solution sooner, but no current game needs a second material shader, and it overlaps M6 material serialization. **C:** debug views only, with no per-material plan. Least work, but leaves the "other general-use shaders" request open. | **Decided: A** (owner, 2026-11-23) |
| Q4 | Asset build outputs | **A:** in-memory dependency records only. Nothing on disk to invalidate, and current load times need no cache. **B:** a persistent derived-data cache now. Faster cold starts and ready for packaging, but cache invalidation and storage before anything is slow. **A+** (refined in review): A, with records keyed by input hashes plus an importer version, so the cache only persists them later. | **Decided: A+** (owner, 2026-11-23) |
| Q5 | Failing systems | **A:** suspend a throwing system until the next module activation. No log flood, and no repeated half-updates. **B:** keep today's behavior (log and call every frame). Recovers by itself if the failure was transient. **C:** roll back to the previous DLL. Restores working code, but the old library must stay loaded and the world may depend on new code. **B2** (considered in review): log once but keep calling, which stops the flood but not the repeated partial updates. Today's flood is about 165 lines per second per failing `Update` system on the laptop. | **Decided: A** (owner, 2026-11-23) |
| Q6 | DLL save-to-present target | **A:** ≤ 1.5 s, confirmed after step 1's breakdown. Likely reachable with PCH, incremental linking and faster detection. **B:** ≤ 1.0 s. Feels immediate, but may need hot-patching beyond module replacement. **C:** set no number until step 1 lands. | **Decided: A**, provisional; revisited after step 1's measurement (owner, 2026-11-23) |
| Q7 | Field migration | **A:** after M6 generic serialization, sharing its field table. Built once, and it covers the 3D components. **B:** in M5, for host-unseen game components only. Available sooner, but the 3D sample has no such components, and the field table would be written twice. **C:** out of v1, so component edits keep requiring a restart. | **Decided: A** (owner, 2026-11-23) |
| D1 | Release DLL target (review) | **A:** Debug ≤ 1.5 s, Release ≤ 2.0 s. **B:** 1.5 s for both. **C:** compile the game DLL less optimized in Release development builds. | **Decided: A** (owner, 2026-11-23) |
| D2 | The single 437 ms stall (review) | **A:** keep it as a tracked TODO and investigate if it recurs. **B:** investigate now. | **Decided: A** (owner, 2026-11-23) |
| D3 | Edits that keep their timestamp (review) | **A:** accept detection by the 2 s audit. **B:** per-directory wake-ups through `Platform::DirectoryWatch`. | **Decided: A** (owner, 2026-11-23) |
