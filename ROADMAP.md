# RavenEngine Roadmap

## Goal

RavenEngine v1 is a **cross-platform C++23 game engine on Vulkan** with:

1. **Cross-platform runtime:** Windows, Linux and macOS run the same scenes, assets and game code through shared interfaces.
2. **ECS:** game state lives in a data-oriented entity/component registry that game systems iterate efficiently.
3. **Hot reload:** game code, shaders and assets reload during play, keep compatible state, and recover from failed edits.
4. **High performance at scale:** a GPU-driven renderer draws tens of thousands of dynamic objects at interactive frame rates.

ECS and hot reload are joint design goals. Component values are retained by the host as plain bytes, so reloadable game code can be replaced without losing the world. Fast iteration means shortening the edit → build → observe loop. Games are authored with code and data files in v1. A full visual editor is outside v1, but ownership boundaries must allow future editor and play sessions.

Fixed conventions: right-handed world, +X right, +Y forward, +Z up (Blender). Blender/FBX is the primary 3D authoring path. See [CoordinateSystem.md](docs/CoordinateSystem.md). 3D is the development target. 2D games are not a current target (owner, 2026-11-26, planning decision 18): the engine keeps sprites as world quads, the overlay and the UI, but builds no 2D sample or 2D-only feature.

Check an item only when its behavior is shown in a sample, a tracked test, or a recorded measurement. Detailed evidence for completed work is in the subsystem docs under `docs/` and in [MilestoneHistory.md](docs/MilestoneHistory.md).

An architecture assessment of the codebase against this goal (2026-11-23) proposes fixes and an M6 order: [ArchitectureAssessment.md](docs/ArchitectureAssessment.md). A forward-looking companion lists the technical decisions the remaining milestones will force, with each option's trade-offs: [FutureDecisions.md](docs/FutureDecisions.md). Both are proposals. Only what the owner adopts into this roadmap is decided; the adopted parts are listed under [Planning decisions](#planning-decisions-2026-11-24). A second assessment at revision 7617e21 (2026-11-26) reviews M6A, M6C and the M6B draft, follows up every earlier finding, and proposes review amendments and B0 contracts before M6B step B2: [ArchitectureAssessment-2026-11-26.md](docs/ArchitectureAssessment-2026-11-26.md). A companion code-quality assessment of the same revision lists slop, slow paths and readability problems with small fixes, sequenced against that assessment's phases and the M6B steps: [CodeQualityAssessment-2026-11-26.md](docs/CodeQualityAssessment-2026-11-26.md). Both are proposals for owner review.

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
| 4 | Linux CI (FD-04) | No Linux job before M4. M4 delivers Linux, including its CI build and CPU tests. Amended 2026-11-26 (assessment Q2, AA2-PLAT-01): a non-blocking Linux compile-only job is added now. On every push it compiles RavenCore, RavenEngine and RavenSceneTool past errors, with no link, run or tests; a red result never blocks. The repository is public, so it costs CI time, not money. M4 still delivers linking, running, CPU tests and the full CI build. | Now (compile-only), M4 |
| 5 | Shape of M6 (FD-01) | Three stages, M6A, M6B and M6C, each with its own exit and review. Three corrections to the proposal: the contract narrowing moves into M6A; the game project layout (FD-17) is decided in the M6 note and lands at M6-7; the sample game starts after M6-7. | [M6A–M6C](#m6a--host-and-data-model) |
| 6 | Audio (FD-30) | *Confirmed for what is built (owner, 2026-11-27, joint review item 4): the engine mixer, sound-event assets, buses and the miniaudio device layer on Windows. The device layer on macOS and Linux stays provisional until M4 plays sound there; a failure would revisit only that layer, behind `AudioDevice`.* Originally provisional, open to revision: an engine-owned mixer in shared code (voices with IDs, stop-oldest), over one vendored device layer for all three platforms (for example miniaudio). Its license, size and Linux backends are checked before vendoring. M4 then verifies audio natively instead of writing backends. Device layer decided 2026-11-25: miniaudio 0.11.26 behind the private `AudioDevice` interface ([UIAudioLibraries.md](docs/UIAudioLibraries.md#hiding-the-device-layer-owner-question-2026-11-25)). | M6C, M4 |
| 7 | Review amendments (2026-11-26 assessments, Q12; decided 2026-11-26) | Implemented directly before B2 and folded into the one joint review of M6A and M6C; nothing is pushed before that review. Scope: the high and medium defects, the small script and game fixes, the build and platform settings including the host compile fix, and the fixed-step context group (AA2-GP-02/03/05). The structural group (AA2-ECS-01/03, PERF-02, RND-05/07, CORE-02) lands with the B-step that touches the same files. Four commits: script and game fixes; high defects; medium defects and build settings; the one contract-hash commit. Each passes Debug and Release with warnings as errors and the CPU tests. | Before M6B B2 |
| 8 | Game-driven scene flow (2026-11-26 assessment Q10, AA2-CORE-01; decided 2026-11-26) | Now, as a review amendment in the contract-hash commit of decision 7: `GameServices::RequestScene` and `RequestRestart` (last request wins, applied between frames, remaining fixed steps skipped once a request is pending) and a `GetSceneTransition()` state that B6's asynchronous loading keeps. Samples' completion flow moves to the requests, and the stale F3 hints are corrected. Arena keeps its in-scene retry. Carrying data into the next scene is a later item, when a game first needs it. | Before M6B B2 |
| 9 | Stage exits and the M4 contract (2026-11-26 assessment Q13, AA2-PLAT-01; decided 2026-11-26) | Every stage closes with a Windows exit. What macOS and Linux still owe is its platform exit, appended once to M4's list, so platform work stays contained in M4 behind the platform interfaces. M4 ports against the public contract at the end of M6B, not M6A, because C1–C3 and B2/B5 change it. | Roadmap, M4 |
| 10 | v1 scope exclusions (2026-11-26 assessment Q3–Q5; decided 2026-11-26) | Skeletal animation, player save games and networking are not in v1. Each keeps a designed route, listed under [Not in v1](#not-in-v1-2026-11-26). | Roadmap |
| 11 | Minimum player CPU (2026-11-26 assessment Q1, AA2-PLAT-03; decided 2026-11-26) | Every x86-64 CPU (SSE2) on Windows, Linux and Intel Macs for v1. Every Jolt instruction-set option is set explicitly in `cmake/RavenJolt.cmake`, so no OS silently requires AVX2, and cross-platform determinism is explicitly off. Apple Silicon is ARM and unaffected. Trigger to revisit: a physics measurement on the laptop that shows a real gain from SSE4.2 or AVX2. | Review amendments, M4 |
| 12 | Bare-collider friction (2026-11-26 code-quality assessment Q1, CQ-GP-01; decided 2026-11-26) | A collider without a `RigidBodyComponent` uses the component's default friction, 0.5, instead of Jolt's 0.2, so there is one default everywhere. Arena's crate push is checked by hand and retuned in ArenaTool if needed; the documented determinism hash is re-recorded. | Review amendments |
| 13 | Arena fall-out (2026-11-26 code-quality assessment Q2, CQ-GAME-11; decided 2026-11-26) | Falling out of the arena ends the run through the shared game-over path; the swarm preview respawns the craft instead. The walls and scene stay as they are. The route out is confirmed by hand first. | Review amendments |
| 14 | One file per Arena screen (2026-11-26 code-quality assessment Q4, CQ-GAME-06; decided 2026-11-26) | Each screen gets one file holding its system and its drawing: `Hud.cpp`, `PauseMenu.cpp` and `Bindings.cpp`. `ArenaUi.cpp`/`.hpp` keep only the shared styles, screen models and F6 preview registration. The owner's reason: splitting made hot reload faster, so the one-file-per-object-or-screen rule is applied strictly. | Review amendments |
| 15 | Sprite sheets in v1 (2026-11-26 assessment Q8, AA2-RND-03; decided 2026-11-26) | `SpriteComponent` gains `UvRect`, `Size` and `Pivot` in M6B's contract-break window, with defaults that reproduce today's output (whole texture, today's size, centred pivot). B5's GPU sprite record carries them. The engine offers the capability; games decide how to animate with it. | M6B B2 window, B5 |
| 16 | GPU memory sub-allocation (2026-11-26 code-quality assessment Q3, CQ-VK-05; decided 2026-11-26) | Vendor AMD's Vulkan Memory Allocator (VMA, MIT, vendor-neutral over standard Vulkan) unmodified when B3 or B6 starts, with its settings in `cmake/RavenVma.cmake`, a swap script, and access only through a Raven seam. It removes the per-resource driver allocations (allocation cap, load hitches, waste) and gives the memory budget AA2-RND-08 needs. The download needs the owner's approval at that time. | M6B B3/B6 |
| 17 | Lighting (M6B FD-26; decided 2026-11-26) | v1 keeps one sun plus ambient light. The renderer's target after v1 is physically based materials plus global illumination, reached through directional shadows first. M6B keeps that path open without building it: four textures fit the PBR set, tangents and an HDR target come with PBR, and lighting stays behind one function in `Material.hlsli` ([RenderingContentDesign.md](docs/RenderingContentDesign.md#fd-26--lighting-in-v1)). | After v1; constraints in M6B |
| 18 | No 2D work (M6B 2D-run decision; decided 2026-11-26) | "We can make things in 2D, but no value for now." No 2D run, no 2D sample and no 2D-only feature. The Courtyard sample and the 2D-only `Collision2D` helpers are removed with the review amendments (the public header in the contract-hash commit). Sprites stay as 3D world quads, with the overlay and UI; M6B's B1 becomes a Samples Gallery sprite baseline. | Review amendments, M6B |
| 19 | Performance off the laptop (2026-11-26 assessment Q11, FD-39; decided 2026-11-26) | Budgets are enforced only on the minimum-spec Windows laptop. On macOS and Linux, M4 checks that the samples run at interactive rates and records the numbers, with no budget gate. Per-OS targets can be set later from those numbers. | M4 |
| 20 | CMake floor (2026-11-26 assessment AA2-BLD-01; decided 2026-11-27) | Every CMake file requires 3.28, the version Ubuntu 24.04 LTS ships, so local builds, CI and every OS build Raven under the same policies; CMake has no LTS releases of its own. The 3.24 paths and `CMP0141` guards are gone, CI prints its CMake version, and the Windows machines use Visual Studio's bundled CMake. |
| 21 | Platforms (owner, 2026-11-27, joint review) | Windows is the head platform: every milestone is built, measured and accepted on Windows first. Other platforms follow, so platform-specific code is only ever encapsulated behind Raven's platform interfaces (`Platform/<OS>/`, backends selected in CMake), and macOS and Linux catch up by implementing those interfaces, never by changing shared code. The owner decides platform direction; everything else in the joint review that is technical is delegated. The layer's shape (owner, 2026-11-27, option C of [PlatformLayerDesign.md](docs/PlatformLayerDesign.md)): named areas in `Raven::Platform`, a constexpr capabilities header per OS, Windows/Mac/Linux/Posix/Fallback folders selected only by `RavenPlatform.cmake`, a CI check that keeps OS macros and headers inside `Platform/`, and a port contract table, after reviewing Unreal's HAL, Godot's platform ports and O3DE's PAL. | Every milestone; M4 |

## Not in v1 (2026-11-26)

Owner decision 10. Each item stays possible later without rework; its trigger is when it is reconsidered.

| Feature | What v1 keeps | Trigger |
| --- | --- | --- |
| Skeletal animation (skinned characters) | M6B's B0 contracts record how compute skinning fits, as text only, and the 128-byte object record does not grow. No skinning code is added. | A game needs animated characters. |
| Player save games (slots or checkpoints beyond profiles) | Profiles, key bindings and the developer quick-save. The planned route is lossless scene capture plus runtime-only fields. | A game needs mid-level saves. |
| Physically based materials and global illumination (target direction, decision 17) | Sun plus ambient light. M6B's material record, texture count and `Material.hlsli` lighting function keep the path open. | After v1, starting with directional shadows. |
| Networking (prediction, rollback, lockstep) | The per-tick input records and seeded random streams, which keep lockstep and replay open. | Multiplayer comes into scope. |

## Where the engine stands (2026-11-23)

Verified on Windows only, on the minimum-spec RTX 3070 Ti Laptop (presentation and device checks also used its Intel iGPU):

- **Platform:** Win32 window, input with focus state, key repeat and stable HID key IDs, DPI awareness, frame clock and pacing, files/paths, atomic files, directory watch. Audio plays through the engine mixer over the private `AudioDevice` interface, with miniaudio as the backend (M6C C8, 2026-11-25). Device selection prefers a discrete GPU.
  - macOS (Cocoa) and Linux (XCB) windows and input are merged.
  - They have no random-byte hook, game-module loader or native watcher yet, and no current native verification (M4). Audio builds the shared miniaudio backend there, unverified.
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

Run the same engine on Linux and macOS. Platform contributors own native implementation and verification and can start now. M4 ports against the public contract as it stands at the end of M6B, and it closes every stage's platform exit (planning decision 9): the list below collects what each stage still owes on macOS and Linux.

- [ ] macOS/Linux: random-byte hook for `AssetId::Generate`.
- [ ] macOS/Linux: native game-module load/unload/replace (`dlopen`), with build-toolchain capture for game-only builds.
- [ ] macOS/Linux: native directory watching (FSEvents/inotify), or a confirmed polling fallback.
- [ ] macOS/Linux: native verification of audio through the shared miniaudio device layer, added in M6C on 2026-11-25 (planning decision 6). No native audio backend is written.
- [ ] Shader toolchain (DXC/spirv-val) on all hosts, plus the MoltenVK portability subset.
- [ ] Platform exit of M5: code, every shader stage and supported assets reload on macOS and Linux with recorded timings and failure recovery.
- [ ] Platform exit of M6: the small 3D game and the stress sample run on macOS and Linux using only engine APIs.
- [ ] CI builds and CPU tests on Windows, Linux and macOS. *A non-blocking Linux compile-only job lands before M4 (planning decision 4, amended 2026-11-26); M4 adds linking, running and the CPU tests. A read-only g++ 13/clang 18 syntax check of the shared sources on 2026-11-24 passed 65 of 67 files; see the [pre-M6 list](#pre-m6-hardening) for the one real defect.*
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

**Exit (Windows):** code, every shader stage and supported assets reload with recorded timings and failure recovery. The platform exit is owed by [M4](#m4--cross-platform-parity) (planning decision 9).

**Exit status (2026-11-23):** met on Windows. One unexplained 437 ms frame during a game build with validation layers is tracked (D2). macOS and Linux follow through their M4 backends. See the [M5 exit record](docs/HotReloadDesign.md#m5-exit-record-2026-11-23).

### Pre-M6 hardening

Small fixes that no later step replaces, so that game-written data cannot end the session before M6 makes such writes routine (planning decision 1). The tasks are in [TODO.md](TODO.md#pre-m6-hardening-current); evidence for each is in [ArchitectureAssessment.md](docs/ArchitectureAssessment.md).

**Exit:** a zero scale, a bad texture ID, a destroyed parent or a throwing system registration written by game code no longer exits the host. The M3 exit numbers are reproduced on the unpatched host through the launch options. The warning level is on every target (closing M0). The Debug game DLL has debug info. The structural-change benchmark case is recorded.

### M6 — Gameplay services and samples

M6 runs in three stages with their own exits and reviews (planning decision 5). One design note comes first and records FD-06 to FD-17, starting with the game project layout (FD-17): [DataModelDesign.md](docs/DataModelDesign.md), decided 2026-11-24. The step numbers are the assessment's ([section 6](docs/ArchitectureAssessment.md)).

**Exit for M6 as a whole (Windows):** a small 3D game and the stress sample run using only engine APIs. The 2D sample was dropped (planning decision 18). The platform exit is owed by [M4](#m4--cross-platform-parity) (planning decision 9).

### M6A — Host and data model · *done on Windows 2026-11-27 (owner review)*

*Implemented on Windows 2026-11-24; accepted in the owner's joint review of M6A, M6C and the review amendments on 2026-11-27. Decisions and results: [DataModelDesign.md](docs/DataModelDesign.md#m6a-exit-record-2026-11-24).*

- [x] M6-1: engine-owned frame pipeline. `OnFrame` splits into update and render with no shim, PostUpdate/Extract move into Application inside a recoverable error boundary (data errors recoverable, `bad_alloc`/`logic_error` fatal), the render-world `Sync` resets transactionally, and the GPU-scene generation counter becomes process-wide.
- [x] M6-2: a typed field table for components, with type tags in `LayoutHash`; the module table registers components. This is the base of serialization and migration.
- [x] M6-3: hierarchy as a host-written `ParentComponent` with child lists, depth buckets and incremental spawn/destroy/reparent.
- [x] M6-4: persistent entity IDs, `EntityRef`, and `AssetRef<MeshAsset>` resolved on change, so `MeshHandle` leaves components.
- [x] M6-5: the scene/asset seam; `Pick` becomes a free function.
- [x] M6-6: generalize component and asset serialization into one chunked scene format that keeps unknown sections, plus a tolerant `raven-project 2`, a converter and regenerated scenes, with round-trip tests in CI.
- [x] M6-7: the one module-contract break. Expose stable scene access, ECS views and runtime resource handles to game code: `SystemContext` becomes scene access, input, time and picks, with an engine play-state gate and a writable camera. Sample components leave RavenCore for the layout chosen in FD-17, and deferred structural changes (Commands, from M1) go through Scene. The 2D Courtyard part needs an owner-requested 2D run.
- [x] M6-8: migrate game components by field name across layout changes, using the field table, now that the host no longer compiles game components. *Moved from M1 via M5 (owner decision Q7, 2026-11-23); see [HotReloadDesign.md](docs/HotReloadDesign.md#h6--field-level-component-migration-q7).*
- [x] Narrow the module contract to the engine's public surface: generate the watched-input list from CMake, and stop hashing game headers. *Moved from the assessment's M6-10 into M6A, because this exit needs it (planning decision 5).*

**Exit:** a game defines a component the host never compiles, authors it in a scene file, and edits one of its fields while running, with no restart. The save/load round trip runs in CI. Spawning and despawning 1k entities per frame among 100k stays within budget. M6B changes the public contract again, so M4 ports against the end of M6B (planning decision 9).

### M6B — Rendering and content

Decide FD-18 to FD-23 (materials, shader interface, transparency and sprites, textures, asset loading, multi-object FBX) before it starts. Design note: [RenderingContentDesign.md](docs/RenderingContentDesign.md) (draft, 2026-11-26), with five owner decisions and the rest delegated.

- [ ] M6-9: draw grouping by (pipeline, mesh), material identity with texture leases, and per-material shaders: a material references a shader asset, and GPU-culled commands are drawn with one pipeline per shader. *Owner decision Q3, 2026-11-23; see [HotReloadDesign.md](docs/HotReloadDesign.md#h3--debug-views-and-general-use-shaders-q3).*
- [ ] M6-9: move sprites, and later transparent objects, onto the render world, GPU culling and bindless textures in one pass, checked against a Samples Gallery baseline (the 2D run was dropped, planning decision 18). *Moved from M3 (owner decision Q5, 2026-11-22).*
- [ ] M6-10: a per-frame upload batch, and import of multi-object FBX files with stable sub-asset identities.
- [ ] Asynchronous asset resolution with placeholders.

**Exit:** sprites and one custom material shader run on the GPU-driven path. A multi-object FBX imports with stable IDs. The Gallery's sprites match their baseline captures.

### M6C — Game services and the sample game · *done on Windows 2026-11-27 (owner review)*

Design: [GameServicesDesign.md](docs/GameServicesDesign.md), decided 2026-11-24 (FD-29, FD-31 to FD-34, FD-46 to FD-48). FD-47 (movement and control) is confirmed for its built part (owner, 2026-11-27), with the crowd backend decided at its own milestone; FD-31 (text and UI) is approved in full (owner, 2026-11-27; first for the early deliverable only), with the full stack planned in [UIDesign.md](docs/UIDesign.md). Direction: keep the ability to build an RTS, a real-time action game and other genres, without building RTS-specific systems now.

- [x] C1: input mapping contexts that consume input, one ordered event list, the developer context, the action map, key listening and layout-aware key names (FD-29).
- [x] C2: per-user data path, field-table records in text and binary forms, crash-safe writes and the host-owned profile store (FD-33).
- [x] C3: step-rate setting, control phase, input stream, simulation RNG, deterministic spawn IDs and render interpolation (FD-34, FD-47, FD-48).
- [x] C4: Jolt Physics as a host-owned physics world with components as the source of truth, plus the physics iteration set (FD-32).
- [x] C5: picking on Jolt for game and editor (FD-34).
- [x] C6: `MoverComponent` and the character backend (FD-47).
- [x] C7: the shared GPU 2D path, MSDF fonts and the thin UI cut (FD-31). *Built 2026-11-25 on the shared job system ([JobSystemDesign.md](docs/JobSystemDesign.md)): FreeType, HarfBuzz, msdfgen, msdf-atlas-gen, Yoga and ICU vendored unmodified; runtime MTSDF text for every script the engine fonts cover; the immediate UI over Yoga; the F6 preview mode ([record](docs/GameServicesDesign.md#c7--the-2d-and-ui-early-deliverable-2026-11-25)).*
- [x] C8: audio voices: the engine mixer over one shared device layer, replacing the single-voice `AudioPlayer` and the native XAudio2 backend (planning decision 6, provisional). *Device layer done 2026-11-25: miniaudio 0.11.26 behind `AudioDevice` (decision A1), the mixer core and the device lifecycle; XAudio2 removed ([record](docs/GameServicesDesign.md#c8-first-part--the-audio-device-layer-2026-11-25)). Mixer done 2026-11-25: voices, concurrency, virtual voices, spatial panning, buses and ducking, WAV/FLAC/MP3/Ogg (A2 owner, A3–A6 delegated; [record](docs/GameServicesDesign.md#c8-second-part--the-engine-mixer-2026-11-25)). Sound-event assets done 2026-11-25 ([record](docs/GameServicesDesign.md#c8-third-part--sound-event-assets-2026-11-25)); streaming follows.*
- [x] C9: the arena sample game (`Games/Arena`, FD-46) and the developer quick-save. *Both parts built 2026-11-25, the second with the HUD, menus, key bindings and sounds ([record](docs/GameServicesDesign.md#c9-second-part--hud-menus-key-bindings-and-sounds-2026-11-25)).*
- [x] C10: the determinism check and the exit check with every measurement. *Done 2026-11-25, accepted 2026-11-27: every exit item passes ([results](docs/GameServicesDesign.md#m6c-exit-checklist-results-2026-11-25)).*
- [ ] Add world-to-screen and screen-to-world helpers once a caller needs them, plus reusable camera controllers as gameplay requires them.
- [ ] Keep the 3D samples and Arena working through the reusable engine, on Windows now and on macOS and Linux through M4.

**Exit:** the M6 exit above, checked through the [M6C exit checklist](docs/GameServicesDesign.md#m6c-exit-checklist).

### M7 — Quality and release

- [ ] Static analysis and sanitizer configurations where toolchains support them. Add crash diagnostics.
- [ ] Verify the assertion path and the debugger breakpoint workflow. Document Edit and Continue versus runtime reload.
- [ ] Refresh the README with tested tool versions, architecture, module contracts and asset workflow.
- [ ] Package the host, game modules, shaders, assets and licenses per platform. Verify a clean-machine run.

**Exit:** another developer can clone, build, run, modify and package a game on any supported platform from the documentation alone.

## Out of scope for v1

A full visual editor, arbitrary C++ state migration beyond field-level component migration, generalized plugin loading, particle/VFX systems, a render graph, and simultaneous multi-view rendering (unless M3 needs one).

## Collision and physics

3D collision and physics come from Jolt Physics in M6C ([FD-32](docs/GameServicesDesign.md#fd-32--collision-and-physics)). 2D collision is not planned (planning decision 18); Courtyard's swept XZ helpers are removed with it. Crowd movement and navigation for many units are a later milestone ([FD-47](docs/GameServicesDesign.md#fd-47--movement-and-control)).
