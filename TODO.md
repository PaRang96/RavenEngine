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
- [ ] Confirm the first green run of `.github/workflows/windows.yml` (x64-debug/x64-release build plus `ctest -LE gpu` on `windows-2025` with Vulkan SDK 1.4.309.0). The runner has Visual Studio 2022 (MSVC 19.4x), while local builds use Visual Studio 2026 (19.51), so check C++23 compatibility there or move to a newer runner image.
- [ ] Cache build outputs if the workflow takes longer than 15 minutes.

### Performance baseline
- [x] Record the 4,000-mesh 3D baseline on the minimum-spec RTX GPU: 114.9 FPS, with gameplay ECS + world + gather at 3.91 ms p95, giving the M1 target ≤ 1.96 ms. See [ECSDesign.md](docs/ECSDesign.md#m0-3d-baseline-record-2026-11-21) (2026-11-21). Under the minimal-test policy, the 500/2,000-mesh runs were dropped.
- [x] Root CMake skips `Tests/` when a source snapshot omits it. This fixes the benchmark and smoke snapshot builds.
- [x] `Tests/Benchmarks/EcsBenchmark.cpp` measures Registry iteration, the rotator query, world transforms and random reads at 10k/50k/100k entities. The Release baseline on the minimum-spec laptop and the resulting M1 budgets are in [ECSDesign.md](docs/ECSDesign.md#performance-budgets) (2026-11-21).

### Build hygiene
- [ ] Set `/W4` (MSVC) and `-Wall -Wextra` (Clang/GCC) on engine targets, and fix actionable warnings. Keep third-party code excluded.

## M1 — Data-oriented ECS (next, design first)

- [x] Write the design note: [ECSDesign.md](docs/ECSDesign.md) (2026-11-20).
- [x] Owner review (2026-11-20): D1 keeps an empty, non-virtual `Component` base; D6 approved; D8 deferred to a Debug check; budgets come from the M0 baseline.
- [ ] Benchmark the chosen storage against the current Registry, using the M0 micro-benchmarks.
- [ ] Make `Component` an empty, non-virtual base, with the macro generating static information and typed IDs. Template the Registry API on `T`.
- [ ] Add dense sparse-set pools behind the current Read/Write API, so the samples don't change.
- [ ] Add views with borrowed references, `Single<T>`, and the Debug no-candidate-scene check. Then port `SceneAccess::Query` and the rotator/gameplay systems.
- [ ] Add the command buffer for structural changes, and the host system schedule with phases.
- [ ] Add a cached world-transform component and a hierarchy update system. Port draw gathering and picking to it.

## M2 — Vulkan 1.2 renderer foundation (after M1)

- [ ] Raise `apiVersion` and DXC `-fspv-target-env` to Vulkan 1.2. Check required features and extensions at device selection, with clear errors.
- [ ] Adopt dynamic rendering and synchronization2, and replace the render-pass/framebuffer objects.
- [ ] Use timeline semaphores for frame pacing, with persistently mapped per-frame ring buffers.
- [ ] Split per-frame camera/lighting into a UBO, and shrink `MeshInstanceData` (currently 176 bytes, with lighting copied per object).
- [ ] Batch lit meshes by (mesh, material) with instancing ([VulkanClearPass.cpp](RavenEngine/Private/Renderer/Vulkan/VulkanClearPass.cpp) currently issues one draw per mesh).
- [ ] Put shared vertex/index pools behind MeshHandle.
- [ ] Carried over: consolidate window size access around `GetFramebufferState()`. Make swapchain preparation report usability through one readiness check. Keep mesh-handle creation and lookup beside the mesh storage. Review device-loss handling and device-idle recreation/shutdown.

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
