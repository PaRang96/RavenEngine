# RavenEngine TODO

This is the actionable task list for the current milestones in [ROADMAP.md](ROADMAP.md). The roadmap holds the goals, the agreed technical direction and each milestone's exit criteria. Completed-work evidence lives in the subsystem docs and [MilestoneHistory.md](docs/MilestoneHistory.md). Do not keep validation write-ups here. Summarize the result in one line and link to the record.

Working agreement: the assistant suggests small, reviewable engine snippets and the owner applies them. Engine files change only when the owner explicitly requests that specific change (clarified 2026-11-01). Documentation and CMake edits do not imply engine-edit approval. Check an item only when its result has been demonstrated.

Priority: 3D first. 2D runtime testing needs an explicit owner request. macOS and Linux contributors work on M4 in parallel.

## M0 — Safety net and baseline (one item left)

### Repository state
- [x] Commit the outstanding working tree in four commits on master: vendored ufbx with its license, the feature work (built in isolation), CTest/CI with the guard tests, and the roadmap/design docs (2026-11-21).
- [x] Update the local `AGENTS.md` summary so it matches the new roadmap, including the minimum-spec rule (2026-11-20).

### CTest
Test policy (owner, 2026-11-20): keep tests minimal. Add a tracked test only to guard behavior that an upcoming change rewrites. Areas that no milestone is changing rely on the existing smoke and benchmark scripts. Add a test to an area when a milestone starts changing it.

- [x] Enable CTest (`RAVEN_BUILD_TESTS`, on by default) with automatic enrollment: each `Tests/**/<Name>Tests.cpp` becomes one test that links RavenCore. `Tests/Gpu/` gets the `gpu` label, and `Tests/Support/` is shared. Build and test presets exist for every configure preset. Probe tests verified enrollment, labels and DLL lookup on Windows Debug (2026-11-20).
- [x] `RegistryTests` (CPU, runs in CI) guards the M1 storage rewrite: entity lifecycle, stale and foreign handles, slot reuse, Add/Read/Write/Remove, tags, destroy erasing values, and 1,000 entities with interleaved removal. Uses `Tests/Support/Check.hpp`; no framework.
- [x] `HierarchyTests` (`Tests/Gpu`, local only) guards the M1 cached world transforms: nested composition with reflected scale, the draw pass matching direct composition, parent moves, reparent/detach keeping the local transform, cycle and foreign-entity rejection, and cascade destroy. It is GPU-labeled only because `Scene` needs an `AssetStore`, which needs a renderer.
- [x] Test presets now fail when no tests are found. Both tests pass in `x64-debug` and `x64-release` (2026-11-21).

Not planned unless a milestone changes the area: math/projection, scene binary codec, assets/AssetStore, game-module compatibility, and GPU tests in CTest. The existing `scripts/Test-Windows3D.ps1` smoke and `scripts/Benchmark-Windows3D.py` stay the manual checks for rendering and reload.

### CI
- [x] The first run of `.github/workflows/windows.yml` passed on `15e413a`: x64-debug and x64-release built with the runner's Visual Studio 2022 toolset, and `RegistryTests` passed ([run 44762913227](https://github.com/PaRang96/RavenEngine/actions/runs/44762913227), 2026-11-21).
- [x] Workflow time is 1.6–3.5 minutes per job, mostly Vulkan SDK installation, so no build caching is needed.

### Performance baseline
- [x] Record the 4,000-mesh 3D baseline on the minimum-spec RTX GPU: 114.9 FPS, with gameplay ECS + world + gather at 3.91 ms p95, giving the M1 target ≤ 1.96 ms. See [ECSDesign.md](docs/ECSDesign.md#m0-3d-baseline-record-2026-11-21) (2026-11-21). Under the minimal-test policy, the 500/2,000-mesh runs were dropped.
- [x] Root CMake skips `Tests/` when a source snapshot omits it. This fixes the benchmark and smoke snapshot builds.
- [x] `Tests/Benchmarks/EcsBenchmark.cpp` measures Registry iteration, the rotator query, world transforms and random reads at 10k/50k/100k entities. The Release baseline on the minimum-spec laptop and the resulting M1 budgets are in [ECSDesign.md](docs/ECSDesign.md#performance-budgets) (2026-11-21).

### Build hygiene
- [x] Set `/W4` (MSVC) and `-Wall -Wextra` (Clang/GCC) on every target through one shared CMake function, with warnings as errors only in CI. Remove the game DLL's local `/WX` so it follows the same rule. Keep third-party code excluded (planning decision 3). A /W4 probe on 2026-11-24 found five warnings to resolve:
  - Fix the two shadowed names: `FragmentShaderInterface.cpp:152` (C4457) and `VulkanInstance.cpp:121` (C4456).
  - Define `_CRT_SECURE_NO_WARNINGS` for the two `getenv` calls (C4996, `ReloadTrace.hpp:20`, `RavenEngine.cpp:149`).
  - Disable C4324, the intended `alignas` padding in `VulkanDrawQueue.hpp:19`.
  - Done 2026-11-24 (`raven_apply_warnings`, `RAVEN_WARNINGS_AS_ERRORS` set in CI): Debug and Release build with no warnings locally on VS 18; CI's VS 2022 toolset runs with warnings as errors on the next push.

## M1 — Data-oriented ECS (done 2026-11-21)

- [x] Write the design note: [ECSDesign.md](docs/ECSDesign.md) (2026-11-20).
- [x] Owner review (2026-11-20): D1 keeps an empty, non-virtual `Component` base; D6 approved; D8 deferred to a Debug check; budgets come from the M0 baseline.
- [x] Benchmark the chosen storage against the current Registry, using the M0 micro-benchmarks (each step's results are in [EngineModules.md](docs/EngineModules.md#retained-ecs-state-and-game-reload)).
- [x] Make `Component` an empty, non-virtual base, with the macro generating static information and typed IDs. Template the Registry API on `T`. Applied 2026-11-21; see [EngineModules.md](docs/EngineModules.md#retained-ecs-state-and-game-reload).
- [x] Add dense sparse-set pools behind the current Read/Write API, so the samples don't change. Applied 2026-11-21. At 100k entities, Transform read+write is 2.1 ms and a random read 15 ns, which meets the ≤ 50 ns budget. See [EngineModules.md](docs/EngineModules.md#retained-ecs-state-and-game-reload).
- [x] Add views with borrowed references, `TryGet`/`Single`/`Count` and per-slot scene IDs, then port `SceneAccess::Query` and the gameplay loops. Applied 2026-11-21. A 100k-entity Transform view takes 0.11 ms, which meets the ≤ 0.3 ms budget. Change ticks move to the world-transform step.
- [x] Add the host system schedule: registered systems, FixedUpdate/Update phases and per-system timing. Applied 2026-11-21. The command buffer is deferred until gameplay first spawns or despawns at runtime.
- [ ] Run Courtyard (2D, fixed step) once on request, to confirm player movement through the FixedUpdate system. It is built but not run.
- [x] Add change ticks, a cached world-transform component and the hierarchy update, and port draw gathering. Applied 2026-11-21. Picking stays live. 50k entities with 20% changed take 0.52–0.57 ms; the 4,000-mesh M1 stages take 0.37 ms p95 (target ≤ 1.96 ms).
- [x] M1 exit check (step 7): every budget passed together, including a 50k-match view at 0.18–0.20 ms. Compatible and rejected reloads keep state in the smoke test and in a live Release session, and Vulkan validation is clean at 4,000 meshes. See [the exit record](docs/ECSDesign.md#m1-exit-record-2026-11-21).

## M2 — Vulkan 1.2 renderer foundation (done 2026-11-23)

- [x] Write the design note: [RendererDesign.md](docs/RendererDesign.md) (2026-11-21).
- [x] Owner review (2026-11-21): image tolerance, 32-bit indices, device-local geometry and the budgets are approved as proposed.
- [x] Step 1: reference screenshots from master `05a1f31` (six scenes; frame 0 is bit-for-bit deterministic) and `scripts/Compare-Captures.py` with the approved tolerance. Applied 2026-11-22. References are local under `out/captures/m2-reference`.
- [x] Step 2 (R1): Vulkan 1.2 instance, required features and extensions, SPIR-V 1.5 shaders, clear unsupported-device errors. Applied 2026-11-22; captures are bit-identical. See [RendererDesign.md](docs/RendererDesign.md#migration-steps).
- [x] Step 3 (R2): dynamic rendering and synchronization2; the render pass and framebuffers are gone, frames use explicit layout transitions, and pipelines survive resizes. Applied 2026-11-22; captures are bit-identical. See [RendererDesign.md](docs/RendererDesign.md#migration-steps).
- [x] Step 4 (R3): timeline-semaphore frame pacing and one retirement queue, replacing the per-slot fences and keep-alive lists. Applied 2026-11-22; captures are bit-identical and the 4,000-mesh benchmark is unchanged. See [RendererDesign.md](docs/RendererDesign.md#migration-steps).
- [x] Step 5 (R4): persistently mapped per-frame rings, the frame uniform (camera and lighting, set 1, vertex stage only), and 112-byte lit instances with the clip transform in the vertex shader. Applied 2026-11-22 with the revised image tolerance. See [RendererDesign.md](docs/RendererDesign.md#migration-steps).
- [x] Step 6 (R5): instanced lit-mesh batching by (mesh, texture); 4,000 meshes now take 7 draws. Applied 2026-11-22. See [RendererDesign.md](docs/RendererDesign.md#migration-steps).
- [x] Step 7 (R6): device-local vertex/index pools behind `MeshHandle`, with 32-bit indices, staging uploads and range retirement. Applied 2026-11-22. See [RendererDesign.md](docs/RendererDesign.md#migration-steps).
- [x] Step 8 (R7): `GetFramebufferState()` is the only window size accessor, `PrepareSwapchain()` is the one readiness check, and device loss has one error path. The mesh-handle item was reviewed with no change. Applied 2026-11-22. See [RendererDesign.md](docs/RendererDesign.md#migration-steps).
- [x] Step 9: M2 exit check, 2026-11-23. Every budget is met except renderer preparation + record/submit (0.88–1.01 ms against 0.8 ms), which is recorded without optimization because M3 removes that code. The 10k scene takes 7 draws and passes Debug validation. See [the exit record](docs/RendererDesign.md#m2-exit-record-2026-11-23).

## M3 — GPU-driven rendering at scale (done 2026-11-23)

- [x] Write the design note: [GpuDrivenDesign.md](docs/GpuDrivenDesign.md) (2026-11-22).
- [x] Owner decisions Q1–Q7 (2026-11-22/23): render world from change ticks, an in-memory lifecycle journal, bindless lit materials behind an engine header, fixed-count indirect commands, sprites moved to M6, Hi-Z only if measured, lit meshes only from the render world.
- [x] Steps 0–7, 2026-11-23: M2 exit, stress scenes, lifecycle journal, bindless lit materials, render world with GPU culling and indirect draws, the reload-spike measurement, and the exit check. All exit targets are met. See [the exit record](docs/GpuDrivenDesign.md#m3-exit-record-2026-11-23).

## M4 — Platform contributor queue (parallel)

See [PlatformHandoff.md](docs/PlatformHandoff.md) for the interfaces and verification steps.

- [ ] macOS/Linux `Platform::FillRandomBytes`.
- [ ] macOS/Linux game-module backend, replacing `UnsupportedGameModule.cpp`.
- [ ] macOS/Linux native directory watch, or a confirmed `PollDirectoryWatch` fallback.
- [ ] macOS/Linux native verification of audio through the shared device layer that M6C adds, replacing `UnsupportedAudio.cpp`. No native audio backend is written (planning decision 6, provisional).
- [ ] MoltenVK portability enumeration and subset handling (coordinate with M2).
- [ ] A Linux CI job with the build and CPU tests; macOS follows (planning decision 4). It needs the Linux `FillRandomBytes` and non-MSVC branches in the game CMake.
- [ ] Native Debug/Release runs of the current 3D samples and reload paths. The parity pass is evaluated after M6A, which breaks the module contract and replaces the scene format.

Windows-side M4 items:

- [x] Input focus state, key repeat and stable key IDs (2026-11-23). See [InputProcessing.md](docs/InputProcessing.md#focus-key-repeat-and-stable-key-ids-2026-11-23).
- [x] Require compute on the graphics queue family, document GPU requirements and verify the Intel Iris Xe (2026-11-23). See [PlatformHandoff.md](docs/PlatformHandoff.md#gpu-requirements).
- [ ] Manual checks: foreground focus and auto-repeat with a physical keyboard, a non-US layout, and mixed-DPI movement (needs one monitor at a different scale).
- [x] Prefer a discrete GPU, then an integrated one, in device selection (owner decision, 2026-11-23). See [PlatformHandoff.md](docs/PlatformHandoff.md#gpu-requirements).
- [ ] Owner decision: a development option that forces separate graphics and present families on the NVIDIA GPU, to exercise that path.

## M5 — Hot reload completeness (done 2026-11-23)

- [x] Write the design note: [HotReloadDesign.md](docs/HotReloadDesign.md) (2026-11-23).
- [x] Owner review: Q1 A, Q2 A, Q3 A, Q4 A+, Q5 A, Q6 A (provisional), Q7 A (2026-11-23).
- [x] Step 1: opt-in DLL phase timing (`RAVEN_RELOAD_TRACE`) and the measured breakdown (2026-11-23).
- [x] Step 2: the reload service and dependency records (H1); PNG, FBX, scene, project, game sources and the DLL moved onto it (2026-11-23).
- [x] Step 3: shader programs for every stage, DXC-reported includes and vertex/compute interface checks (H2) (2026-11-23).
- [x] Step 4: debug views on F7: shaded, unlit, normals, wireframe (H3) (2026-11-23).
- [x] Step 5: system table diff and failing-system suspension (H4) (2026-11-23).
- [x] Step 6: DLL loop fixes: game PCH, configure skip, no poll/debounce wait (H5) (2026-11-23).
- [x] Step 7: exit check (2026-11-23). See the [M5 exit record](docs/HotReloadDesign.md#m5-exit-record-2026-11-23).
- [x] Owner review decisions (2026-11-23): D1 Release DLL target ≤ 2.0 s and Debug ≤ 1.5 s; D2 track the stall; D3 accept the 2 s audit for edits that keep their timestamp.
- [ ] Investigate the single 437 ms frame seen during a Release game build with validation layers if it recurs (D2). It did not recur in two later passes.
- [x] Owner review of the whole M5 implementation (2026-11-23); gaps found in review were fixed: importer versions, complete records and summaries, the records file, the measured thread count, the PowerShell 5.1 smoke script and stale docs.

## Pre-M6 hardening (current)

Planning decision 1 (2026-11-24); see [ROADMAP.md](ROADMAP.md#pre-m6-hardening). Each item is a small engine snippet that needs the owner's go-ahead. Evidence and the assessment's step lists are in [ArchitectureAssessment.md](docs/ArchitectureAssessment.md).

- [x] ARCH-RT-01: a non-throwing normal matrix, so a zero-scale lit object is hidden instead of exiting (`Transform.hpp:164-180`, called from `RenderWorld.cpp:209-210`). Public `TryNormalMatrix` (one copy of the math; `NormalMatrix` throws on top of it for authored data). `MeshMaterials.md` updated. The transactional `Sync` reset waits for M6-1. *Done 2026-11-24; an ignored probe hid zero and 1e-14 scales and showed the object again at scale 2.*
- [x] ARCH-RT-02: one non-throwing asset lookup for data errors; a bad texture ID draws white, and a bad sprite is skipped. *Done 2026-11-24: `Scene::TryUseAsset` over `AssetCatalog::Find` and `AssetStore::FindReferenceError`; the probe checked nil, unregistered, missing-file and wrong-kind IDs, one log line each.*
- [x] ARCH-RT-03: a missing ancestor makes the entity a detached root in both world-transform paths, and the changed flag propagates. *Done 2026-11-24; the probe removed a root's Transform and destroyed a parent outside the Scene, and cache and live compose agreed. Porting `MoveSprites` to `SceneAccess` moved to M6-7: it needs a 2D run and does not lower this risk.*
- [x] ARCH-RT-04: wrap the game's system registration while the DLL is loaded, and add `catch(...)` next to the `std::exception` catch in `GameModule.cpp`. *Done 2026-11-24; a probe module that throws a `std::runtime_error` and an `int` left the host running with both messages reported.*
- [x] ARCH-PERF-01: present-mode (FIFO, Mailbox, Immediate) and `MaxFrameRate` launch options, FIFO with no extra cap by default, the 30 FPS minimized throttle kept (planning decision 2). Delete the source patch from `scripts/Windows3DHighRefresh.py`, re-run the M3 exit numbers on the stock host, and measure the sleep-cap precision. *Done 2026-11-24, with `--gpu` and `--borderless` so the patch could go entirely. Stock host: 165 FPS at 50k animated and 163.7 at 100k static under FIFO; a 120 cap gives 109 FPS ([record](docs/GpuDrivenDesign.md#m3-exit-record-2026-11-23)).*
- [x] The M0 warning level (above, under Build hygiene).
- [x] ARCH-BLD-02: debug info in the Debug game DLL. *Done 2026-11-24: `/Zi` through the target property on CMake 3.25+, explicit `/Zi /FS` on 3.24; the PDB now names the game sources and functions ([EngineModules.md](docs/EngineModules.md)).*
- [ ] A debugger breakpoint in game code across one auto-build reload (manual, Visual Studio attached).
- [x] ARCH-ECS-01 step 0: a structural-change case (spawn, despawn, reparent) in `EcsBenchmark`, recorded on the minimum-spec laptop, so M6-3 rests on a measurement. *Done 2026-11-24: at 100k entities, spawning 1k takes 17.6 ms, reparenting 1k 17.7 ms and despawning 1k 378 ms ([record](docs/ECSDesign.md#structural-change-record-2026-11-24-pre-m6)).*
- [x] Add the missing `#include <utility>` for `std::exchange` in `VulkanMesh.cpp:131`. MSVC accepts it through another header; g++ 13 and clang 18 reject it (Linux syntax check, 2026-11-24).

## M6 — design note (next)

- [x] Owner review of [ArchitectureAssessment.md](docs/ArchitectureAssessment.md) and [FutureDecisions.md](docs/FutureDecisions.md) (2026-11-24): the "decide now" items are recorded as [planning decisions 1–6](ROADMAP.md#planning-decisions-2026-11-24). The other assessment questions go to the M6 note or to the stage that needs them.
- [ ] Write the M6 design note: FD-06 to FD-17 (the assessment's D-A to D-I), starting with the game project layout and contract (FD-17), and the stage exit checklists.
- [ ] Owner review of the note before any M6A code.

M7 (quality and release) is tracked in [ROADMAP.md](ROADMAP.md#m7--quality-and-release). Move its tasks here when work starts.
