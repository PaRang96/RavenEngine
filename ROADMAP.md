# RavenEngine Roadmap

## Goal

RavenEngine v1 is a **cross-platform C++23 game engine on Vulkan** with:

1. **Cross-platform runtime:** Windows, Linux and macOS run the same scenes, assets and game code through shared interfaces.
2. **ECS:** game state lives in a data-oriented entity/component registry that game systems iterate efficiently.
3. **Hot reload:** game code, shaders and assets reload during play, keep compatible state, and recover from failed edits.
4. **High performance at scale:** a GPU-driven renderer draws tens of thousands of dynamic objects at interactive frame rates.

ECS and hot reload are joint design goals. Component values are retained by the host as plain bytes, so reloadable game code can be replaced without losing the world. Fast iteration means shortening the edit → build → observe loop. Games are authored with code and data files in v1. A full visual editor is outside v1, but ownership boundaries must allow future editor and play sessions.

Fixed conventions: right-handed world, +X right, +Y forward, +Z up (Blender). Blender/FBX is the primary 3D authoring path. See [CoordinateSystem.md](docs/CoordinateSystem.md). 3D is the primary development target; the 2D path stays supported but secondary.

Check an item only when its behavior is shown in a sample, a tracked test, or a recorded measurement. Detailed evidence for completed work is in the subsystem docs under `docs/` and in [MilestoneHistory.md](docs/MilestoneHistory.md).

An architecture assessment of the codebase against this goal (2026-11-23) proposes fixes and an M6 order: [ArchitectureAssessment.md](docs/ArchitectureAssessment.md). A forward-looking companion lists the technical decisions the remaining milestones will force, with each option's trade-offs: [FutureDecisions.md](docs/FutureDecisions.md). Both are proposals. Only what the owner adopts into this roadmap is decided; the adopted parts are listed under [Planning decisions](#planning-decisions-2026-11-24).

## Agreed technical direction (2026-11-20)

| Topic | Decision |
| --- | --- |
| Performance target | GPU-driven rendering for 50k+ objects: GPU-resident instance data, compute culling, indirect draws, bindless materials. |
| ECS access | Dense, typed component storage. Systems may borrow component spans/views that are valid only during one synchronous update call. The copy-based Read/Write API stays for one-off access. Retained storage stays plain bytes, so DLL reload keeps working. |
| Vulkan baseline | Vulkan 1.2 core (descriptor indexing, timeline semaphores, buffer device address where useful) plus dynamic rendering and synchronization2 as extensions. Add the portability path for MoltenVK. Every feature needs a documented fallback or a clear unsupported-device error. |
| Testing | Start CTest and a Windows CI build now, before the ECS and renderer rewrites. Keep tests minimal: add a tracked test only for behavior an upcoming change rewrites. Rendering and reload stay covered by the existing smoke and benchmark scripts. |
| Minimum spec | The test laptop (Intel Core i7-12700H, NVIDIA GeForce RTX 3070 Ti Laptop GPU) is both the test machine and the minimum supported spec. Every performance target must be met on it. Faster hardware is not a substitute for meeting a target here. Its Intel iGPU is not part of the minimum spec. |

## Planning decisions (2026-11-24)

The owner reviewed the "decide now" items of [FutureDecisions.md](docs/FutureDecisions.md#3-decision-calendar) against the code. Each decision was checked against the current sources before it was adopted.

| # | Topic | Decision | Lands in |
| --- | --- | --- | --- |
| 1 | What comes next (FD-02) | A short pre-M6 fix list, then the M6 design note. The other "now" items of the assessment wait for the M6 step that touches them, or stay opportunistic. | [Pre-M6 hardening](#pre-m6-hardening) |
| 2 | Frame pacing (FD-03) | FIFO by default with no extra frame cap. Launch options select the present mode (FIFO, Mailbox, Immediate) and an optional `MaxFrameRate`. The 30 FPS throttle while minimized stays. Benchmarks use the options, and the source patch in `scripts/Windows3DHighRefresh.py` is deleted. The sleep-cap precision is measured when `MaxFrameRate` lands. | Pre-M6 hardening |
| 3 | Warning policy (FD-05) | `/W4` (MSVC) and `-Wall -Wextra` (GCC/Clang) on every engine, sample, tool, test and game target; warnings are errors only in CI. The game DLL follows the same rule, so its local `/WX` is removed. Third-party code is excluded. | M0 |
| 4 | Linux CI (FD-04) | No Linux job before M4. M4 delivers Linux, including its CI build and CPU tests. | M4 |
| 5 | Shape of M6 (FD-01) | Three stages, M6A, M6B and M6C, each with its own exit and review. Three corrections to the proposal: the contract narrowing moves into M6A; the game project layout (FD-17) is decided in the M6 note and lands at M6-7; the sample game starts after M6-7. | [M6A–M6C](#m6a--host-and-data-model) |
| 6 | Audio (FD-30) | Provisional, open to revision: an engine-owned mixer in shared code (voices with IDs, stop-oldest), over one vendored device layer for all three platforms (for example miniaudio). Its license, size and Linux backends are checked before vendoring. M4 then verifies audio natively instead of writing backends. | M6C, M4 |

## Where the engine stands (2026-11-23)

Verified on Windows only, on the minimum-spec RTX 3070 Ti Laptop (presentation and device checks also used its Intel iGPU):

- **Platform:** Win32 window, input with focus state, key repeat and stable HID key IDs, DPI awareness, frame clock and pacing, files/paths, atomic files, directory watch, XAudio2 one-shots. Device selection prefers a discrete GPU.
  - macOS (Cocoa) and Linux (XCB) windows and input are merged.
  - They have no random-byte hook, game-module loader, native watcher or audio backend yet, and no current native verification (M4).
- **Vulkan (M2, M3 done):** Vulkan 1.2 with dynamic rendering, synchronization2 and timeline retirement, plus persistent per-frame rings and a frame uniform.
  - Lit meshes come from a host-owned render world, are culled on the GPU into fixed-count indirect draws, and use bindless textures behind `Raven/Material.hlsli`.
  - 50k lit meshes with 20% animated ran at 126.5 FPS ([GpuDrivenDesign.md](docs/GpuDrivenDesign.md)).
  - Debug views: shaded, unlit, normals and wireframe (F7).
- **ECS (M1 done):** components derive from an empty `Component` base and live in sparse-set pools keyed by a compile-time type ID.
  - Scene-scoped views hand out borrowed references; change ticks and cached world transforms are in place.
  - The game DLL registers named systems in `FixedUpdate`/`Update`.
  - Deferred structural commands wait for a first runtime spawn/despawn need. See [ECSDesign.md](docs/ECSDesign.md).
- **Scenes and assets:** a versioned project catalog, a binary 3D scene codec with hierarchy and atomic saves, SceneSession switching, AssetStore leases, PNG/JPEG, static single-object FBX and PCM WAV. See [SceneWorkflow.md](docs/SceneWorkflow.md) and [AssetSystem.md](docs/AssetSystem.md).
- **Hot reload (M5 done on Windows):** one reload service watches the game sources and DLL, scenes, the project, PNG and FBX assets, and every engine shader stage with its includes. Each input keeps an in-memory dependency record.
  - Shaders are checked against the engine's interfaces, and every path keeps its last-good result.
  - A one-file game edit is active in 1.3–1.6 s in Debug and about 1.7 s in Release.
  - A throwing game system is suspended until the next reload.
  - Changing shared component definitions still requires a host rebuild and restart. See [HotReloadDesign.md](docs/HotReloadDesign.md) and [DevelopmentReload.md](docs/DevelopmentReload.md).
- **Samples:** Perspective, Gallery, Assemblies, Puzzle and Collection (3D), Courtyard (2D), benchmark scenes with 100–100,000 meshes, and a Windows player build ([PlayerBuild.md](docs/PlayerBuild.md)).
- **Quality:** CTest with `RegistryTests` (CPU) and `HierarchyTests` (GPU-labeled), a Windows CI build and CPU tests, and tracked smoke, live-reload and benchmark scripts. Warnings at `/W4` (or `-Wall -Wextra`) on every target, as errors in CI; no sanitizer configuration yet.

## Milestones

The milestones are ordered by dependency. M1 and M2 can proceed in parallel after M0. M4 platform work runs in parallel by platform contributors; its parity pass is evaluated after M6A, which breaks the module contract and replaces the scene format. [TODO.md](TODO.md) holds the actionable task list for the current milestones.

Order from here: M6A in the order of the [M6 design note](docs/DataModelDesign.md#step-order-and-what-each-step-uses) (decided 2026-11-24), then M6B, with M6C alongside. The pre-M6 debugger check stays a manual owner item.

### M0 — Safety net and baseline · *done 2026-11-24*

Make the existing behavior repeatable before the ECS and renderer change underneath it.

- [x] Commit the outstanding working tree as reviewed, logically grouped commits.
- [x] Add CTest with a minimal set of tests that guard the M1 rewrite: `RegistryTests` (CPU) and `HierarchyTests` (GPU-labeled). Other areas get tests only when a milestone changes them.
- [x] Add a Windows CI workflow: configure plus Debug/Release builds plus CPU tests.
- [x] Make the 3D benchmark and smoke scripts a tracked, documented baseline, with results recorded on the minimum-spec machine ([ECSDesign.md](docs/ECSDesign.md#performance-budgets)).
- [x] Set a compiler warning level for every target and fix actionable warnings (planning decision 3). *`/W4` or `-Wall -Wextra` on every target; CI builds with warnings as errors and passed on `ae4423a`.* *A /W4 probe build on 2026-11-24 found five warnings: two `getenv` deprecations, one alignment padding and two shadowed names.*

**Exit:** `ctest --test-dir out/build/x64-debug` passes locally and in CI. The 4,000-mesh benchmark baseline is recorded in the repository; the 500/2,000-mesh runs were dropped under the minimal-test policy. The warning level landed on 2026-11-24.

### M1 — Data-oriented ECS · *done 2026-11-21*

Replace per-entity heap storage with dense typed pools and real systems while keeping the reload contract. Design: [ECSDesign.md](docs/ECSDesign.md) (approved 2026-11-20).

- [x] Give each component a stable compile-time type ID derived from `TypeName`, so lookups no longer use strings.
- [x] Store each component type in dense pools (sparse sets; the benchmark did not require archetypes). Pools stay host-owned and reload-safe.
- [x] Add views over one or more component types that yield borrowed references, valid only during the current synchronous update, scene-scoped through per-slot scene IDs. Iteration cost scales with matching entities, not total entities.
- [ ] Add deferred structural changes (create/destroy/add/remove) through a command buffer applied at defined sync points. *Deferred until gameplay first spawns or despawns at runtime (owner, 2026-11-21); views reject structural changes meanwhile. Scheduled at M6-7 (planning decision 5).*
- [x] Add a host-owned system schedule with update phases (`FixedUpdate`, `Update`). The game DLL registers its systems on each load, and they are cleared before unload, so no callback outlives the module.
- [x] Store world transforms as a cached component updated by a hierarchy update with change tracking. Rendering reads it; picking stays live because it runs per click.
- [ ] Optional: migrate components by field name when layouts change, so routine component edits no longer need a host restart. *Moved to M6, after generic serialization (owner decision Q7, 2026-11-23).*

**Exit:** iterating Transform over 100k entities and running the hierarchy update over 50k entities meet budgets set in M0. Compatible DLL reload keeps every entity, value and system order. Existing samples behave the same.

**Exit result (2026-11-21):** met on the minimum-spec laptop. Every M0 budget passed, and the 4,000-mesh M1 stages fell from 3.91 to 0.38 ms p95 (162.8 FPS). Compatible and rejected reloads keep state, both in the smoke test and in a live Release session, and Vulkan validation is clean at 4,000 meshes. Only the 3D samples were run; the 2D Courtyard fixed-step path is built but not run. See [the exit record](docs/ECSDesign.md#m1-exit-record-2026-11-21).

### M2 — Vulkan 1.2 renderer foundation · *done 2026-11-23*

Raise the API baseline and restructure frame data for scale. CPU-side mesh instancing is the first win. Design: [RendererDesign.md](docs/RendererDesign.md) (approved 2026-11-21).

- [x] Target Vulkan 1.2 for the instance and SPIR-V. Use dynamic rendering, synchronization2 and timeline semaphores. Add portability enumeration for MoltenVK. Report unsupported devices with clear errors.
- [x] Move per-frame uploads to persistently mapped ring buffers sized per frame in flight.
- [ ] Move camera and lighting into a per-frame uniform buffer, and shrink instance data to a transform plus material/object indices. *Camera and lighting moved, and instances shrank to 112 bytes (step 5). Material/object indices come with M3's bindless materials and resident object data.*
- [x] Batch lit meshes by (mesh, material) with instanced draws. Transparent draws keep their sorted path.
- [x] Consolidate meshes into shared vertex/index pools, so geometry is ready for indirect drawing.
- [x] Review device loss, presentation completion and the swapchain readiness check. Fold in the pending framebuffer-state and mesh-handle cleanups. *Presentation-completion timing was reviewed and left out; see [RendererDesign.md](docs/RendererDesign.md#migration-steps) step 8.*

**Exit:** 10k lit meshes using a few unique meshes record draws in proportion to the number of unique (mesh, material) pairs. The current samples render identically, and Debug validation is clean.

**Exit result (2026-11-23):** met. The 10k scene takes 7 draws, the samples render within the approved tolerance, and Debug validation is clean at 10k. One budget was recorded as missed instead of optimized: renderer preparation + record/submit read 0.88–1.01 ms against 0.8 ms, and M3 removes that code. See [the exit record](docs/RendererDesign.md#m2-exit-record-2026-11-23).

### M3 — GPU-driven rendering at scale · *done 2026-11-23*

Move object processing to the GPU to reach the performance goal. Design: [GpuDrivenDesign.md](docs/GpuDrivenDesign.md) (owner decisions Q1–Q7, 2026-11-22/23).

- [x] Keep a GPU-resident object/transform buffer updated incrementally from ECS changes, uploading dirty ranges only.
- [x] Make materials and textures bindless through descriptor indexing. *Lit path; sprites follow in M6 (Q5).*
- [x] Add compute frustum culling that writes indirect draw arguments. Use draw-indirect-count where supported, with a fixed-count fallback that zeroes culled instance counts (for example on MoltenVK).
- [x] Add Hi-Z occlusion culling if the measurements justify it. *Not justified: GPU p95 is 2.2 ms at 50k and 4.0 ms at 100k static.*
- [x] Record GPU and CPU timings per stage in the benchmark build, and add a stress sample with thousands of moving objects driven by reloadable ECS systems.

**Exit (proposed targets, measured on the minimum-spec machine):** 50,000 lit meshes with 20% animated run at ≥ 60 FPS at 2560 × 1440 on the RTX 3070 Ti Laptop, with CPU frame preparation ≤ 4 ms. 100,000 static meshes stay ≥ 60 FPS. A DLL edit to the animating system reloads without frame-time spikes above 100 ms.

**Exit result (2026-11-23):** met on the minimum-spec laptop.
- 50k lit meshes with 20% animated run at 126.5 FPS at 2560 × 1440, with CPU frame preparation of 3.53 ms p95.
- 100k static meshes run at 121.3 FPS.
- A DLL edit to the animating system peaks at a 46.5 ms frame.

Captures match M2, and Debug validation is clean at 50k. See [the exit record](docs/GpuDrivenDesign.md#m3-exit-record-2026-11-23).

### M4 — Cross-platform parity

Run the same engine on Linux and macOS. Platform contributors own native implementation and verification and can start now.

- [ ] macOS/Linux: random-byte hook for `AssetId::Generate`.
- [ ] macOS/Linux: native game-module load/unload/replace (`dlopen`), with build-toolchain capture for game-only builds.
- [ ] macOS/Linux: native directory watching (FSEvents/inotify), or a confirmed polling fallback.
- [ ] macOS/Linux: native verification of audio through the shared device layer that M6C adds (planning decision 6). No native audio backend is written.
- [ ] Shader toolchain (DXC/spirv-val) on all hosts, plus the MoltenVK portability subset.
- [ ] CI builds and CPU tests on Windows, Linux and macOS. *The Linux job lands with M4, not before (planning decision 4). A read-only g++ 13/clang 18 syntax check of the shared sources on 2026-11-24 passed 65 of 67 files; see the [pre-M6 list](#pre-m6-hardening) for the one real defect.*
- [ ] Finish Win32 key translation, repeat/text input and focus state, and verify mixed-DPI movement. Bring the other backends to the same input contract. *Windows focus state, key repeat and stable key IDs landed on 2026-11-23 ([InputProcessing.md](docs/InputProcessing.md#focus-key-repeat-and-stable-key-ids-2026-11-23)). Text input waits for a caller. Mixed DPI and real-keyboard checks remain.*
- [ ] Verify queue-family configurations beyond the single-family device, and document GPU requirements. *Requirements documented, discrete GPUs preferred and the Intel Iris Xe verified on 2026-11-23 ([PlatformHandoff.md](docs/PlatformHandoff.md#gpu-requirements)). No tested device has separate graphics and present families.*

**Exit:** every sample and every reload path runs in Debug/Release on all three platforms, with native verification recorded by the responsible contributor.

### M5 — Hot reload completeness and iteration speed · *done on Windows 2026-11-23 (owner review)*

Design: [HotReloadDesign.md](docs/HotReloadDesign.md) (owner decisions Q1–Q7, 2026-11-23).

- [x] Reload every shader stage (vertex, fragment and compute), with dependency tracking for includes. *All nine engine programs, DXC-reported includes, and vertex/compute interface checks (H2).*
- [x] Record each asset's source, dependencies and build outputs for images, FBX and HLSL. Reimport when a dependency changes. *Records stay in memory, keyed by input hashes plus an importer version, so a disk cache can be added later without rework. The cache is built at its first trigger: texture compression or mips, mesh clustering or LOD, shader permutations, editor work, M7 cooking or a measured slow load (owner decision Q4, 2026-11-23).*
- [x] Reload systems registered by the DLL, and decide the policy for added or removed systems. *Changes are logged; a throwing system is suspended until the next activation (H4).*
- [x] Reduce DLL save-to-present from about 3.5 s to a target set after profiling the build (for example below 1.5 s for a one-file edit). *Targets set in review (D1, 2026-11-23): Debug ≤ 1.5 s, Release ≤ 2.0 s. Measured Debug 1.3–1.6 s and Release 1.63–1.87 s (median 1.69 s); see the [exit record](docs/HotReloadDesign.md#m5-exit-record-2026-11-23).*
- [x] Extend last-good recovery and diagnostics to every newly supported replacement path.

**Exit:** code, every shader stage and supported assets reload on all platforms with recorded timings and failure recovery.

**Exit status (2026-11-23):** met on Windows. One unexplained 437 ms frame during a game build with validation layers is tracked (D2). macOS and Linux follow through their M4 backends. See the [M5 exit record](docs/HotReloadDesign.md#m5-exit-record-2026-11-23).

### Pre-M6 hardening

Small fixes that no later step replaces, so that game-written data cannot end the session before M6 makes such writes routine (planning decision 1). The tasks are in [TODO.md](TODO.md#pre-m6-hardening-current); evidence for each is in [ArchitectureAssessment.md](docs/ArchitectureAssessment.md).

**Exit:** a zero scale, a bad texture ID, a destroyed parent or a throwing system registration written by game code no longer exits the host. The M3 exit numbers are reproduced on the unpatched host through the launch options. The warning level is on every target (closing M0). The Debug game DLL has debug info. The structural-change benchmark case is recorded.

### M6 — Gameplay services and samples

M6 runs in three stages with their own exits and reviews (planning decision 5). One design note comes first and records FD-06 to FD-17, starting with the game project layout (FD-17): [DataModelDesign.md](docs/DataModelDesign.md), decided 2026-11-24. The step numbers are the assessment's ([section 6](docs/ArchitectureAssessment.md)).

**Exit for M6 as a whole:** a small 3D game, the stress sample and the 2D sample run on all platforms using only engine APIs.

### M6A — Host and data model

- [ ] M6-1: engine-owned frame pipeline. `OnFrame` splits into update and render with no shim, PostUpdate/Extract move into Application inside a recoverable error boundary (data errors recoverable, `bad_alloc`/`logic_error` fatal), the render-world `Sync` resets transactionally, and the GPU-scene generation counter becomes process-wide.
- [ ] M6-2: a typed field table for components, with type tags in `LayoutHash`; the module table registers components. This is the base of serialization and migration.
- [ ] M6-3: hierarchy as a host-written `ParentComponent` with child lists, depth buckets and incremental spawn/destroy/reparent.
- [ ] M6-4: persistent entity IDs, `EntityRef`, and `AssetRef<MeshAsset>` resolved on change, so `MeshHandle` leaves components.
- [ ] M6-5: the scene/asset seam; `Pick` becomes a free function.
- [ ] M6-6: generalize component and asset serialization into one chunked scene format that keeps unknown sections, plus a tolerant `raven-project 2`, a converter and regenerated scenes, with round-trip tests in CI.
- [ ] M6-7: the one module-contract break. Expose stable scene access, ECS views and runtime resource handles to game code: `SystemContext` becomes scene access, input, time and picks, with an engine play-state gate and a writable camera. Sample components leave RavenCore for the layout chosen in FD-17, and deferred structural changes (Commands, from M1) go through Scene. The 2D Courtyard part needs an owner-requested 2D run.
- [ ] M6-8: migrate game components by field name across layout changes, using the field table, now that the host no longer compiles game components. *Moved from M1 via M5 (owner decision Q7, 2026-11-23); see [HotReloadDesign.md](docs/HotReloadDesign.md#h6--field-level-component-migration-q7).*
- [ ] Narrow the module contract to the engine's public surface: generate the watched-input list from CMake, and stop hashing game headers. *Moved from the assessment's M6-10 into M6A, because this exit needs it (planning decision 5).*

**Exit:** a game defines a component the host never compiles, authors it in a scene file, and edits one of its fields while running, with no restart. The save/load round trip runs in CI. Spawning and despawning 1k entities per frame among 100k stays within budget. This is the stable contract M4 ports against.

### M6B — Rendering and content

Decide FD-18 to FD-23 (materials, shader interface, transparency and sprites, textures, asset loading, multi-object FBX) before it starts.

- [ ] M6-9: draw grouping by (pipeline, mesh), material identity with texture leases, and per-material shaders: a material references a shader asset, and GPU-culled commands are drawn with one pipeline per shader. *Owner decision Q3, 2026-11-23; see [HotReloadDesign.md](docs/HotReloadDesign.md#h3--debug-views-and-general-use-shaders-q3).*
- [ ] M6-9: move sprites, and later transparent objects, onto the render world, GPU culling and bindless textures in one pass, with an owner-requested 2D run. *Moved from M3 (owner decision Q5, 2026-11-22).*
- [ ] M6-10: a per-frame upload batch, and import of multi-object FBX files with stable sub-asset identities.
- [ ] Asynchronous asset resolution with placeholders.

**Exit:** sprites and one custom material shader run on the GPU-driven path. A multi-object FBX imports with stable IDs. The owner-requested 2D run passes.

### M6C — Game services and the sample game

Decide FD-29 and FD-31 to FD-34 (input bindings, text, collision scope, save data, smaller services) before it starts. The services run alongside M6A and M6B once the M6 note is approved; the sample game starts after M6-7, so it is written against the final `SystemContext`.

- [ ] Key bindings and key listening on top of the shared input contract: an action map storing stable `Key`/`MouseButton` values, capture of the next pressed input, and layout-aware key names. *Owner requirement, 2026-11-23; see [InputProcessing.md](docs/InputProcessing.md#planned-key-bindings-and-key-listening).*
- [ ] Audio voices: the engine mixer over one shared device layer, replacing the single-voice `AudioPlayer` and the native XAudio2 backend (planning decision 6, provisional).
- [ ] A per-user data path for saves and settings.
- [ ] Add world-to-screen and screen-to-world helpers once a caller needs them, plus reusable camera controllers as gameplay requires them.
- [ ] After M6-7: the small 3D sample game.
- [ ] Keep the 3D samples and Courtyard (2D) working on all platforms through the reusable engine.

**Exit:** the M6 exit above.

### M7 — Quality and release

- [ ] Static analysis and sanitizer configurations where toolchains support them. Add crash diagnostics.
- [ ] Verify the assertion path and the debugger breakpoint workflow. Document Edit and Continue versus runtime reload.
- [ ] Refresh the README with tested tool versions, architecture, module contracts and asset workflow.
- [ ] Package the host, game modules, shaders, assets and licenses per platform. Verify a clean-machine run.

**Exit:** another developer can clone, build, run, modify and package a game on any supported platform from the documentation alone.

## Out of scope for v1

A full visual editor, arbitrary C++ state migration beyond field-level component migration, generalized plugin loading, particle/VFX systems, a render graph, and simultaneous multi-view rendering (unless M3 needs one).

## Deferred collision and physics

General-purpose 2D collision, 3D collision and physics simulation are deferred with no scheduled milestone. Courtyard's swept XZ checks and trigger overlaps cover its current gameplay. Revisit the scope when a concrete mechanic needs more.
