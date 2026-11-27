# RavenEngine TODO

This is the actionable task list for the current milestones in [ROADMAP.md](ROADMAP.md). The roadmap holds the goals, the agreed technical direction and each milestone's exit criteria. Completed-work evidence lives in the subsystem docs and [MilestoneHistory.md](docs/MilestoneHistory.md). Do not keep validation write-ups here. Summarize the result in one line and link to the record.

Working agreement: the assistant suggests small, reviewable engine snippets and the owner applies them. Engine files change only when the owner explicitly requests that specific change (clarified 2026-11-01). Documentation and CMake edits do not imply engine-edit approval. Check an item only when its result has been demonstrated.

Priority: 3D only. No 2D work for now (owner, 2026-11-26; Courtyard is removed). macOS and Linux contributors work on M4 in parallel.

## M0 — Safety net and baseline (done 2026-11-24)

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
- Dropped 2026-11-26: running Courtyard once on request. Courtyard is removed (owner: no 2D work for now).
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
- [ ] macOS/Linux native verification of audio through the shared miniaudio device layer (added 2026-11-25, replacing `UnsupportedAudio.cpp`). No native audio backend is written (planning decision 6, provisional).
- [ ] MoltenVK portability enumeration and subset handling (coordinate with M2).
- [ ] A Linux CI job with the build and CPU tests; macOS follows (planning decision 4). It needs the Linux `FillRandomBytes` and non-MSVC branches in the game CMake.
- [ ] Native Debug/Release runs of the current 3D samples and reload paths. The parity pass is evaluated after M6A, which breaks the module contract and replaces the scene format.

Windows-side M4 items:

- [x] Input focus state, key repeat and stable key IDs (2026-11-23). See [InputProcessing.md](docs/InputProcessing.md#focus-key-repeat-and-stable-key-ids-2026-11-23).
- [x] Require compute on the graphics queue family, document GPU requirements and verify the Intel Iris Xe (2026-11-23). See [PlatformHandoff.md](docs/PlatformHandoff.md#gpu-requirements).
- [ ] Manual checks: foreground focus and auto-repeat with a physical keyboard, a non-US layout, and mixed-DPI movement (needs one monitor at a different scale).
- [x] Prefer a discrete GPU, then an integrated one, in device selection (owner decision, 2026-11-23). See [PlatformHandoff.md](docs/PlatformHandoff.md#gpu-requirements).
- [ ] A development option that forces separate graphics and present families on the NVIDIA GPU. *Owner decision (2026-11-24, FD-34): skipped for now; kept open until hardware needs that path.*

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

## M6 — design note (done 2026-11-24)

- [x] Owner review of [ArchitectureAssessment.md](docs/ArchitectureAssessment.md) and [FutureDecisions.md](docs/FutureDecisions.md) (2026-11-24): the "decide now" items are recorded as [planning decisions 1–6](ROADMAP.md#planning-decisions-2026-11-24). The other assessment questions go to the M6 note or to the stage that needs them.
- [x] Write the M6 design note: FD-06 to FD-17 (the assessment's D-A to D-I), starting with the game project layout and contract (FD-17), and the stage exit checklists: [DataModelDesign.md](docs/DataModelDesign.md) (2026-11-24).
- [x] Decisions FD-06 to FD-17, Q16, the M6A exit checklist and the 2D-run timing (2026-11-24). The owner delegated the choice to business alignment and future expandability; every recommendation was adopted, with two refinements (FD-08 metadata outside the layout hash, FD-10 deterministic output). See [Owner decisions](docs/DataModelDesign.md#owner-decisions).

## M6C — Game services and the sample game (current)

Design and step order: [GameServicesDesign.md](docs/GameServicesDesign.md#step-order). Exit: [M6C exit checklist](docs/GameServicesDesign.md#m6c-exit-checklist). Each new dependency is added only after the owner approves its exact source and release.

Order (owner, 2026-11-25): the owner's review of C1–C6 and a commit, then C9 without the HUD and sounds, then C7 and C8, then C10. C7 and C8 are a breakpoint: discuss and research them with the owner before any UI or audio work.

- [x] Decisions FD-29, FD-31 to FD-34 and FD-46 to FD-48 (2026-11-24). FD-47 is provisional until its measurements; FD-31 covers the early deliverable only.
- [ ] Owner review of [UIDesign.md](docs/UIDesign.md) sections 1, 2 and 4, before any UI code (C7).
- [ ] Library and design decisions U1–U5 and A1–A6 for C7 and C8, one at a time: [UIAudioLibraries.md](docs/UIAudioLibraries.md) (research, 2026-11-25). *A1 decided 2026-11-25: miniaudio as the first backend behind a private `AudioDevice` interface. U1 decided 2026-11-25: our own UI stack. U4 decided 2026-11-25: every script, not only Korean and English, which adds U5 (Unicode text processing). UI API model (c) approved 2026-11-25, with a UMG-like visual designer later and a preview mode first ([UIDesign.md](docs/UIDesign.md#decision-c-owner-2026-11-25)). Ownership (section 2) approved 2026-11-25, with a threads-and-processes study ([section 8](docs/UIDesign.md#8-threads-and-processes-owner-request-2026-11-25)). Rendering (section 4) approved 2026-11-25. U2 decided 2026-11-25: target D (MTSDF plus a hinted-bitmap path). U3 decided 2026-11-25: Yoga 3.2.1 plus our own grid, list, scroll and scale containers. U5 decided 2026-11-25: ICU4C 79.1 unmodified (`common` in C7, `i18n` with string tables), adopted only if it builds unmodified. A2 decided 2026-11-25 (owner): our own mixer; A3–A6 delegated the same day and built with it ([record](docs/GameServicesDesign.md#c8-second-part--the-engine-mixer-2026-11-25)). Job system decided (delegated) and built 2026-11-25 ([JobSystemDesign.md](docs/JobSystemDesign.md)).*
- [x] C1: input contract (FD-29). *Done 2026-11-25: mapping contexts, ordered events, developer context, key listening, key names; `InputMappingTests`, smoke and live checks pass ([record](docs/GameServicesDesign.md#c1--input-contract-2026-11-25)).*
- [x] C2: user data and the profile store (FD-33). *Done 2026-11-25: keyed records, text and checksummed binary forms, `.bak` recovery, OS folders, saved bindings; `UserDataTests`, smoke and live checks pass ([record](docs/GameServicesDesign.md#c2--user-data-and-the-profile-store-2026-11-25), [UserData.md](docs/UserData.md)).*
- [x] C3: fixed-step services, input stream and determinism level 1 (FD-34, FD-47, FD-48). *Done 2026-11-25: `fixed-step` rate, dropped-time log, Control phase, input stream, seeded RNG and IDs, opt-in render interpolation; `FixedStepTests`, smoke and live checks pass ([record](docs/GameServicesDesign.md#c3--fixed-step-services-input-stream-and-determinism-level-1-2026-11-25)).*
- [x] C4: Jolt physics and the iteration set (FD-32). *Done 2026-11-25: Jolt 5.7.0 vendored unmodified, collider and rigid-body components, `PhysicsWorld` sync and write-back, ordered contacts, queries, F9/F10/Shift+F8, the Physics sample; measurements 1 and 2 recorded; `PhysicsTests`, smoke, live and a validated live run of the sample pass ([record](docs/GameServicesDesign.md#c4--jolt-physics-and-the-iteration-set-2026-11-25), [Physics.md](docs/Physics.md)).*
- [x] C5: picking on Jolt (FD-34). *Done 2026-11-25: `PickWorld`, a never-stepped Jolt system with shared exact mesh shapes synced only when a pick runs; `ScenePicking.cpp` and `IntersectMesh` removed; measurement 4 recorded; tests, smoke and live mouse picks pass ([record](docs/GameServicesDesign.md#c5--picking-on-jolt-2026-11-25)).*
- [x] C6: movement and the character backend (FD-47). *Done 2026-11-25: `MoverComponent` on Jolt virtual characters with inner bodies, a grid for character-vs-character collision (500 movers 3.1 ms instead of 22 ms), walkers in the Physics sample; measurement 3 recorded except the arena drones (C9); tests, smoke and live pass ([record](docs/GameServicesDesign.md#c6--movement-and-the-character-backend-2026-11-25)).*
- [ ] C7: 2D path, MSDF fonts and the thin UI cut (FD-31); record measurement 7. *Built 2026-11-25, pending the owner's review: six text and layout libraries vendored unmodified (ICU builds unmodified with MSVC), the shared 2D overlay, runtime MTSDF text with bidi, script runs, font fallback and ICU line breaking, the UI core with focus and input withholding, the F6 preview mode and on-screen diagnostics; measurement 7 within budget ([record](docs/GameServicesDesign.md#c7--the-2d-and-ui-early-deliverable-2026-11-25)). Not built yet: the small-text bitmap path, grid/list/scroll containers, a worker for layout, IME and gamepad navigation.*
- [ ] C8: audio mixer over one shared device layer (planning decision 6). *Device layer done 2026-11-25: miniaudio 0.11.26 vendored unmodified, `AudioDevice` with the miniaudio and null backends, the mixer core, the device lifecycle with silent fallback; XAudio2 removed; build, tests, smoke, live and a loopback-recorded harness pass ([record](docs/GameServicesDesign.md#c8-first-part--the-audio-device-layer-2026-11-25)). Mixer done 2026-11-25: 64 voices with concurrency groups, ranking and virtual voices, pitch, spatial panning and attenuation, six buses with ducking, sound handles in `GameServices`, and WAV/FLAC/MP3/Ogg decoding (stb_vorbis 1.25 vendored unmodified) ([record](docs/GameServicesDesign.md#c8-second-part--the-engine-mixer-2026-11-25)). Sound-event assets (`.ravensound`, hot-reloaded) done 2026-11-25 ([record](docs/GameServicesDesign.md#c8-third-part--sound-event-assets-2026-11-25)). Next: streaming for long music.*
- [ ] C9: the arena game and the developer quick-save (FD-46, FD-33). *First part done 2026-11-25: `Games/Arena` (craft through the input stream, drones, cells, platform, crates, runs, best score in the profile, Esc pause through a game-requested simulation pause), the Ctrl+F5/Ctrl+F9 quick-save, and measurement 3 completed ([record](docs/GameServicesDesign.md#c9--arena-game-and-the-developer-quick-save-first-part-2026-11-25)). Second part done 2026-11-25: HUD, pause menu, key-binding screen with saved rebinding, five preview entries and six sound events ([record](docs/GameServicesDesign.md#c9-second-part--hud-menus-key-bindings-and-sounds-2026-11-25)).*
- [ ] C10: the determinism check and the exit check; record measurements 5 and 6. *Done 2026-11-25, pending the owner's review: the `DeterminismCheck` test, physics rebuilt on every scene start, the game PCH with the engine's game headers, Arena split by object; measurement 5 meets the M5 targets and measurement 6 shows no regression; every exit item passes ([record](docs/GameServicesDesign.md#c10--determinism-check-and-the-exit-check-2026-11-25), [exit results](docs/GameServicesDesign.md#m6c-exit-checklist-results-2026-11-25)).*

## M6B — Rendering and content (next)

Design: [RenderingContentDesign.md](docs/RenderingContentDesign.md) (draft, 2026-11-26). Step order B1–B8 there. The owner's decisions are settled (2026-11-26); B1 starts after the review amendments and the B0 contracts below.

- [x] Owner questions from [ArchitectureAssessment-2026-11-26.md](docs/ArchitectureAssessment-2026-11-26.md) (Q1–Q13), [CodeQualityAssessment-2026-11-26.md](docs/CodeQualityAssessment-2026-11-26.md) (Q1–Q4) and this note (FD-18, FD-19, FD-23, FD-26, the 2D run), settled one at a time on 2026-11-26. Records: [planning decisions 4 and 7–19](ROADMAP.md#planning-decisions-2026-11-24) and [Not in v1](ROADMAP.md#not-in-v1-2026-11-26).
  - Q12 decided 2026-11-26: the amendments are implemented directly and folded into the joint review ([planning decision 7](ROADMAP.md#planning-decisions-2026-11-24)).
  - Q10 decided 2026-11-26: game-driven scene flow lands now, as a review amendment (planning decision 8).
  - Q2 decided 2026-11-26: a non-blocking Linux compile-only CI job is added now (planning decision 4, amended).
  - Q13 decided 2026-11-26: Windows exits per stage, platform exits collected in M4, and M4 ports against the end of M6B (planning decision 9).
  - Q3, Q4 and Q5 decided 2026-11-26: skeletal animation, player save games and networking are not in v1 (planning decision 10, [Not in v1](ROADMAP.md#not-in-v1-2026-11-26)).
  - Q1 decided 2026-11-26: every x86-64 CPU (SSE2) on every OS for v1 (planning decision 11).
  - CQ Q1 decided 2026-11-26: a collider without a `RigidBodyComponent` uses the component's default friction 0.5 instead of Jolt's 0.2, landing with CQ-GP-01 in the review amendments (planning decision 12). The documented determinism hash is re-recorded and Physics.md states the default; Arena's crate feel is not reviewed, since Arena is not a release target (owner, 2026-11-27).
  - CQ Q2 decided 2026-11-26: falling out of the arena ends the run (planning decision 13).
  - CQ Q4 decided 2026-11-26: one file per Arena screen, `Hud.cpp`, `PauseMenu.cpp` and `Bindings.cpp` (planning decision 14).
  - Q7 removed 2026-11-26 (owner): what pauses behind a pause menu is gameplay design, not engine design. The engine offers per-bus pause and volume as features (AA2-MEDIA-07), with no engine-imposed policy; each game, Arena included, chooses in its own code.
  - Q6 removed 2026-11-26 by the same rule: launch languages and shipped fonts are each game's product choice. The engine keeps its OFL Noto Sans, Arabic and KR set as defaults and loads fonts as assets for any script (AA2-MEDIA-05); the ICU data filter fix stays a technical item.
  - Q8 decided 2026-11-26: sprite sheets are in v1 (planning decision 15).
  - Q9 delegated 2026-11-26 (no on-screen change for existing content, and the indirect path draws in group order with depth): `Opaque` sprites stay as today, with real depth and no alpha test; a new `Cutout` mode is also depth-ordered; sort layer and order apply only to blended sprites, which stay CPU-sorted.
  - CQ Q3 decided 2026-11-26: vendor VMA when B3 or B6 starts (planning decision 16).
  - FD-18 decided 2026-11-26: shared `.ravenmaterial` files referenced by ID, per-object tint kept ([RenderingContentDesign.md](docs/RenderingContentDesign.md#fd-18--material-model)).
  - FD-19 decided 2026-11-26 after research: pixel shaders plus a vertex offset hook with declared maximum displacement, four textures and up to 16 named values per shader, build-time compiling for players and a background compile queue in development ([RenderingContentDesign.md](docs/RenderingContentDesign.md#fd-19--custom-material-shaders)).
  - FD-23 decided 2026-11-26, storage revised: `.ravenasset` sidecars for imported files, IDs in the headers of Raven's own formats, and project settings only in `Project.ravenproject`, landing at the start of B2 as `raven-project 3` ([RenderingContentDesign.md](docs/RenderingContentDesign.md#fd-23--multi-object-fbx)).
  - FD-26 decided 2026-11-26: v1 keeps sun plus ambient; PBR plus global illumination is the target after v1, and M6B keeps the path open (planning decision 17).
  - The 2D run decided 2026-11-26: no 2D work at all for now. Courtyard and the 2D-only `Collision2D` helpers are removed with the review amendments, and B1 becomes a Samples Gallery sprite baseline (planning decision 18).
  - Q11 decided 2026-11-26: budgets are enforced only on the laptop; macOS and Linux record their numbers in M4 with no budget gate (planning decision 19).
- [ ] Review amendments for M6A and M6C, implemented directly and folded into the one joint review (planning decision 7): the trimmed Phase 1 list of the architecture assessment (section 7.1), the code-quality fixes for now (CQ-GAME-01, CQ-BLD-01 and its section 8 Phase 1 rows), scene flow (decision 8), the Linux compile job (decision 4), the Jolt instruction-set settings (decision 11), bare-collider friction (decision 12), Arena fall-out (decision 13), one file per Arena screen (decision 14) and the Courtyard removal (decision 18). Nothing is pushed before the review.
  - Landed locally 2026-11-26/27, in reviewable commits (the plan of four grew into a series): Arena and harness fixes (CQ-GAME-01/02/05/11, AA2-GP-04, GP-05 step 3, decisions 13 and 14, CQ-BLD-01); the high defects (AA2-RT-01, MEDIA-01 with `AudioVoicesTests`, the PLAT-01 host compile fix); kinematic drift and friction (AA2-GP-01, CQ-GP-01, decision 12), CQ-GP-13, deferred saves and `FlushFileBuffers` (GP-05 steps 1 and 5), `--seed` (CQ-CORE-15), the SSE2 floor (decision 11) and the Linux compile job (decision 4); failed candidates and assets (AA2-RT-03, RT-05), module reload (AA2-HR-03, HR-05), developer-key collisions and the Samples split (AA2-GP-12, CQ-GAME-06 steps 1-3); scene flow and the Courtyard removal (AA2-CORE-01 steps 1-5, decision 18, the contract-hash commit); capacity as data errors (AA2-RT-02), glyph placement (MEDIA-03 step 1), shader program classification (CQ-RND-01/03 step 1), decoded-audio bounds (MEDIA-02) and the UI content box (CQ-MEDIA-01 step 1).
  - Landed locally 2026-11-27: the mover inner-body filter (AA2-GP-06, with its benchmark), the UI filter's element set (MEDIA-04 step 1), scene-command rules (AA2-ECS-02 steps 3-6), one field matcher (CQ-DATA-02), the asset-kind table (CQ-DATA-01 steps 1-5), the vendored-file manifests and check (AA2-BLD-02), hidden symbols (PLAT-04 steps 1-3), the UTF-8 build-process seam (PLAT-02 steps 4-8), the module image guard and contract (HR-02 steps 2-3), the fixed-step context group (AA2-GP-02 steps 1-4 and 7, AA2-GP-03, GP-05 step 4; contract-hash commit), physics and picking recovery (AA2-RT-04, with `Scene::GetSceneId`), and the build worker's named state (CQ-CORE-02). Records in [GameServicesDesign.md](docs/GameServicesDesign.md#review-amendments-to-the-engine-services-2026-11-26).
  - AA2-BLD-01 decided 2026-11-27 (owner): CMake 3.28 is the floor ([planning decision 20](ROADMAP.md#planning-decisions-2026-11-24)); the laptop's PATH now leads with Visual Studio's CMake 4.3.1.
  - Not checked by hand yet: a manual `BuildGame.cmake` build offered through the build stamp. Arena's crate feel and fall-out route are dropped: Arena is not a release target, so its quality is not reviewed (owner, 2026-11-27).
- [ ] Joint review of M6A, M6C and the review amendments, item by item (owner, from 2026-11-27): 1 FD-47, 2 FD-48, 3 FD-31 and the UIDesign.md review, 4 audio (planning decision 6), 5 coding rules R1–R15, 6 acceptance and the push.
  - Item 1 decided 2026-11-27: FD-47 confirmed for its built part (control phase, input stream, `MoverComponent`, character backend for tens to a few hundred characters); the crowd backend is decided at its own milestone ([GameServicesDesign.md](docs/GameServicesDesign.md#fd-47--movement-and-control)).
- [ ] B0 contracts in this note before B2 (architecture assessment, AA2-DOC-07 and AA2-RND-01..07), now including the FD-19 vertex offset hook and the FD-23 sidecar format.
- [ ] Adopt the proposed coding rules R1–R15 of the code-quality assessment (section 7) into AGENTS.md, as part of the joint review.
- [ ] M6-9: find and remove the 0.27 ms render-world sync cost that M6A added at 50k animated ([exit record](docs/DataModelDesign.md#m6a-exit-record-2026-11-24), item 4).

## M6A — Host and data model (implemented 2026-11-24, pending review)

Order and decisions per step: [DataModelDesign.md](docs/DataModelDesign.md#step-order-and-what-each-step-uses). Exit: [M6A exit checklist](docs/DataModelDesign.md#m6a-exit-checklist).

- [x] The CPU change-tick case, in `HierarchyTests` (it needs a `Scene`, which runs on the CPU since M6-5).
- [x] M6-1: engine frame pipeline, the tiered error rule (FD-06) and the frame boundary; transactional `Sync` reset; process-wide GPU-scene generation.
- [x] M6-2: field table from `RAVEN_COMPONENT` with type tags in `LayoutHash` (plus its `static_assert`), registered through the module table (FD-08).
- [x] M6-3: persistent ID table and `EntityRef` first (FD-09), then `ParentComponent`, host-owned components and the incremental child index (FD-07).
- [x] M6-4: `EntityRef` in components, `AssetRef<MeshAsset>` replacing `MeshHandle` (FD-09).
- [x] M6-5: scene/asset seam; `Pick` as a free function; `HierarchyTests` to CPU.
- [x] M6-6: generic chunked format with per-section field lists, unknown sections kept, aliases, `dump`/`apply`, `raven-project 2`; six scenes regenerated (generators moved to `SamplesTool`), five converted, old readers deleted; `SceneFileTests` (FD-10, FD-11, FD-16).
- [ ] `RavenSceneTool upgrade` and `prune` (FD-16, FD-11), when a rename is first retired.
- [x] M6-7: `Games/Samples` and `Games/Courtyard` with one generic host (FD-17), the `SystemContext` break and session play state (FD-12), Commands (FD-14), and the 2D Courtyard run.
- [ ] Narrow the contract to the engine's public surface, with one CMake-generated input list (FD-17).
- [x] M6-8: field migration with the migration case in `RegistryTests`.
- [x] M6A exit check against the checklist, in the [exit record](docs/DataModelDesign.md#m6a-exit-record-2026-11-24). The owner accepted item 3 (spawn plus despawn: 1.6–2.0 ms at 100k) and item 4 (0.3–0.5 ms steady-state cost within budget, followed up in M6-9). Item 5 measured: the 100k load is 0.66 s, against 1.67 s for the pre-M6 reader.

M7 (quality and release) is tracked in [ROADMAP.md](ROADMAP.md#m7--quality-and-release). Move its tasks here when work starts.
