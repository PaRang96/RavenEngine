# RavenEngine TODO

This is the actionable task list for the current milestones in [ROADMAP.md](ROADMAP.md). The roadmap holds the goals, the agreed technical direction and each milestone's exit criteria. Completed-work evidence lives in the subsystem docs and [MilestoneHistory.md](docs/MilestoneHistory.md). Do not keep validation write-ups here. Summarize the result in one line and link to the record.

Working agreement: the assistant suggests small, reviewable engine snippets and the owner applies them. Engine files change only when the owner explicitly requests that specific change (clarified 2026-11-01). Documentation and CMake edits do not imply engine-edit approval. Check an item only when its result has been demonstrated.

Priority: 3D first. 2D runtime testing needs an explicit owner request. macOS and Linux contributors work on M4 in parallel.

## M0 — Safety net and baseline (current)

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
- [ ] Set `/W4` (MSVC) and `-Wall -Wextra` (Clang/GCC) on engine targets, and fix actionable warnings. Keep third-party code excluded.

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

## M2 — Vulkan 1.2 renderer foundation (next)

- [x] Write the design note: [RendererDesign.md](docs/RendererDesign.md) (2026-11-21).
- [x] Owner review (2026-11-21): image tolerance, 32-bit indices, device-local geometry and the budgets are approved as proposed.
- [x] Step 1: reference screenshots from master `05a1f31` (six scenes; frame 0 is bit-for-bit deterministic) and `scripts/Compare-Captures.py` with the approved tolerance. Applied 2026-11-22. References are local under `out/captures/m2-reference`.
- [x] Step 2 (R1): Vulkan 1.2 instance, required features and extensions, SPIR-V 1.5 shaders, clear unsupported-device errors. Applied 2026-11-22; captures are bit-identical. See [RendererDesign.md](docs/RendererDesign.md#migration-steps).
- [x] Step 3 (R2): dynamic rendering and synchronization2; the render pass and framebuffers are gone, frames use explicit layout transitions, and pipelines survive resizes. Applied 2026-11-22; captures are bit-identical. See [RendererDesign.md](docs/RendererDesign.md#migration-steps).
- [ ] Step 4 (R3): timeline-semaphore frame pacing and one retirement queue, replacing the per-slot fences and keep-alive lists.
- [ ] Step 5 (R4): persistently mapped per-frame rings, the frame uniform (camera and lighting), and 112-byte lit instances with the clip transform in the vertex shader.
- [ ] Step 6 (R5): instanced lit-mesh batching by (pipeline, mesh, texture, lit). [VulkanClearPass.cpp](RavenEngine/Private/Renderer/Vulkan/VulkanClearPass.cpp) currently issues one draw per mesh.
- [ ] Step 7 (R6): device-local vertex/index pools behind `MeshHandle`, with 32-bit indices, staging uploads and range retirement.
- [ ] Step 8 (R7): consolidate window size access around `GetFramebufferState()`, make swapchain preparation report usability through one readiness check, keep mesh-handle creation and lookup beside the mesh storage, and give device loss one clear error path.
- [ ] Step 9: M2 exit check (budgets, image comparison, Debug validation, live session, a local 10k scene, Courtyard on request).

## M4 — Platform contributor queue (parallel)

See [PlatformHandoff.md](docs/PlatformHandoff.md) for the interfaces and verification steps.

- [ ] macOS/Linux `Platform::FillRandomBytes`.
- [ ] macOS/Linux game-module backend, replacing `UnsupportedGameModule.cpp`.
- [ ] macOS/Linux native directory watch, or a confirmed `PollDirectoryWatch` fallback.
- [ ] macOS/Linux audio backend, replacing `UnsupportedAudio.cpp`.
- [ ] MoltenVK portability enumeration and subset handling (coordinate with M2).
- [ ] Native Debug/Release runs of the current 3D samples and reload paths.

## Later milestones

M3 (GPU-driven rendering), M5 (reload completeness), M6 (gameplay services) and M7 (quality and release) are tracked in [ROADMAP.md](ROADMAP.md#milestones). Move their tasks here when work starts.
