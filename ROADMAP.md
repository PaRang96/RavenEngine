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

## Agreed technical direction (2026-11-20)

| Topic | Decision |
| --- | --- |
| Performance target | GPU-driven rendering for 50k+ objects: GPU-resident instance data, compute culling, indirect draws, bindless materials. |
| ECS access | Dense, typed component storage. Systems may borrow component spans/views that are valid only during one synchronous update call. The copy-based Read/Write API stays for one-off access. Retained storage stays plain bytes, so DLL reload keeps working. |
| Vulkan baseline | Vulkan 1.2 core (descriptor indexing, timeline semaphores, buffer device address where useful) plus dynamic rendering and synchronization2 as extensions. Add the portability path for MoltenVK. Every feature needs a documented fallback or a clear unsupported-device error. |
| Testing | Start CTest and a Windows CI build now, before the ECS and renderer rewrites. Keep tests minimal: add a tracked test only for behavior an upcoming change rewrites. Rendering and reload stay covered by the existing smoke and benchmark scripts. |
| Minimum spec | The test laptop (Intel Core i7-12700H, NVIDIA GeForce RTX 3070 Ti Laptop GPU) is both the test machine and the minimum supported spec. Every performance target must be met on it. Faster hardware is not a substitute for meeting a target here. Its Intel iGPU is not part of the minimum spec. |

## Where the engine stands (2026-11-21)

Verified on Windows only, on the minimum-spec RTX 3070 Ti Laptop (earlier presentation checks also used its Intel iGPU):

- **Platform:** Win32 window, input, DPI awareness, frame clock and pacing, files/paths, atomic files, directory watch, XAudio2 one-shots. macOS (Cocoa) and Linux (XCB) windows/input are merged. They have no random-byte hook, game-module loader, native watcher or audio backend yet, and no current native verification.
- **Vulkan:** a 1.0 renderer with an explicit-lifetime device/swapchain, resize/minimize recovery, depth, color/textured/lit pipelines, managed mesh/texture handles with fence retirement, sprite instancing and CPU frustum culling. Lit meshes use one draw call per object, so 4,000 meshes = 4,004 draws. See [Benchmark3D.md](docs/Benchmark3D.md).
- **ECS (M1 done):** components derive from an empty `Component` base and live in sparse-set pools keyed by a compile-time type ID. Scene-scoped views hand out borrowed references; change ticks and cached world transforms are in place. The game DLL registers named systems in `FixedUpdate`/`Update`. Deferred structural commands wait for a first runtime spawn/despawn need. See [ECSDesign.md](docs/ECSDesign.md).
- **Scenes and assets:** a versioned project catalog, a binary 3D scene codec with hierarchy and atomic saves, SceneSession switching, AssetStore leases, PNG/JPEG, static single-object FBX and PCM WAV. See [SceneWorkflow.md](docs/SceneWorkflow.md) and [AssetSystem.md](docs/AssetSystem.md).
- **Hot reload:** the game DLL (Windows), scene/project files, PNG, FBX and lit/textured fragment shaders reload with last-good recovery. Save-to-present for a DLL edit is about 3.4–3.6 s. Changing shared component definitions requires a host rebuild and restart. See [EngineModules.md](docs/EngineModules.md) and [DevelopmentReload.md](docs/DevelopmentReload.md).
- **Samples:** Perspective, Gallery, Assemblies, Puzzle and Collection (3D), Courtyard (2D), benchmark scenes with 100–4,000 meshes, and a Windows player build ([PlayerBuild.md](docs/PlayerBuild.md)).
- **Quality:** CTest with `RegistryTests` (CPU) and `HierarchyTests` (GPU-labeled), a Windows CI build and CPU tests, and tracked smoke/benchmark scripts. No warning/sanitizer policy yet.

## Milestones

The milestones are ordered by dependency. M1 and M2 can proceed in parallel after M0. M4 platform work runs in parallel with M1–M3 by platform contributors and closes after M3 and M5. [TODO.md](TODO.md) holds the actionable task list for the current milestones.

### M0 — Safety net and baseline · *one item left*

Make the existing behavior repeatable before the ECS and renderer change underneath it.

- [x] Commit the outstanding working tree as reviewed, logically grouped commits.
- [x] Add CTest with a minimal set of tests that guard the M1 rewrite: `RegistryTests` (CPU) and `HierarchyTests` (GPU-labeled). Other areas get tests only when a milestone changes them.
- [x] Add a Windows CI workflow: configure plus Debug/Release builds plus CPU tests.
- [x] Make the 3D benchmark and smoke scripts a tracked, documented baseline, with results recorded on the minimum-spec machine ([ECSDesign.md](docs/ECSDesign.md#performance-budgets)).
- [ ] Set a compiler warning level for engine targets and fix actionable warnings.

**Exit:** `ctest --test-dir out/build/x64-debug` passes locally and in CI. The 4,000-mesh benchmark baseline is recorded in the repository; the 500/2,000-mesh runs were dropped under the minimal-test policy. Only the warning level remains.

### M1 — Data-oriented ECS · *done 2026-11-21*

Replace per-entity heap storage with dense typed pools and real systems while keeping the reload contract. Design: [ECSDesign.md](docs/ECSDesign.md) (approved 2026-11-20).

- [x] Give each component a stable compile-time type ID derived from `TypeName`, so lookups no longer use strings.
- [x] Store each component type in dense pools (sparse sets; the benchmark did not require archetypes). Pools stay host-owned and reload-safe.
- [x] Add views over one or more component types that yield borrowed references, valid only during the current synchronous update, scene-scoped through per-slot scene IDs. Iteration cost scales with matching entities, not total entities.
- [ ] Add deferred structural changes (create/destroy/add/remove) through a command buffer applied at defined sync points. *Deferred until gameplay first spawns or despawns at runtime (owner, 2026-11-21); views reject structural changes meanwhile.*
- [x] Add a host-owned system schedule with update phases (`FixedUpdate`, `Update`). The game DLL registers its systems on each load, and they are cleared before unload, so no callback outlives the module.
- [x] Store world transforms as a cached component updated by a hierarchy update with change tracking. Rendering reads it; picking stays live because it runs per click.
- [ ] Optional: migrate components by field name when layouts change, so routine component edits no longer need a host restart. *Moved to M5.*

**Exit:** iterating Transform over 100k entities and running the hierarchy update over 50k entities meet budgets set in M0. Compatible DLL reload keeps every entity, value and system order. Existing samples behave the same.

**Exit result (2026-11-21):** met on the minimum-spec laptop. Every M0 budget passed, and the 4,000-mesh M1 stages fell from 3.91 to 0.38 ms p95 (162.8 FPS). Compatible and rejected reloads keep state, both in the smoke test and in a live Release session, and Vulkan validation is clean at 4,000 meshes. Only the 3D samples were run; the 2D Courtyard fixed-step path is built but not run. See [the exit record](docs/ECSDesign.md#m1-exit-record-2026-11-21).

### M2 — Vulkan 1.2 renderer foundation · *next*

Raise the API baseline and restructure frame data for scale. CPU-side mesh instancing is the first win. Design: [RendererDesign.md](docs/RendererDesign.md) (approved 2026-11-21).

- [ ] Target Vulkan 1.2 for the instance and SPIR-V. Use dynamic rendering, synchronization2 and timeline semaphores. Add portability enumeration for MoltenVK. Report unsupported devices with clear errors.
- [ ] Move per-frame uploads to persistently mapped ring buffers sized per frame in flight.
- [ ] Move camera and lighting into a per-frame uniform buffer, and shrink instance data to a transform plus material/object indices.
- [ ] Batch lit meshes by (mesh, material) with instanced draws. Transparent draws keep their sorted path.
- [ ] Consolidate meshes into shared vertex/index pools, so geometry is ready for indirect drawing.
- [ ] Review device loss, presentation completion and the swapchain readiness check. Fold in the pending framebuffer-state and mesh-handle cleanups.

**Exit:** 10k lit meshes using a few unique meshes record draws in proportion to the number of unique (mesh, material) pairs. The current samples render identically, and Debug validation is clean.

### M3 — GPU-driven rendering at scale

Move object processing to the GPU to reach the performance goal.

- [ ] Keep a GPU-resident object/transform buffer updated incrementally from ECS changes, uploading dirty ranges only.
- [ ] Make materials and textures bindless through descriptor indexing.
- [ ] Add compute frustum culling that writes indirect draw arguments. Use draw-indirect-count where supported, with a fixed-count fallback that zeroes culled instance counts (for example on MoltenVK).
- [ ] Add Hi-Z occlusion culling if the measurements justify it.
- [ ] Move sprites onto the same instance and culling path.
- [ ] Record GPU and CPU timings per stage in the benchmark build, and add a stress sample with thousands of moving objects driven by reloadable ECS systems.

**Exit (proposed targets, measured on the minimum-spec machine):** 50,000 lit meshes with 20% animated run at ≥ 60 FPS at 2560 × 1440 on the RTX 3070 Ti Laptop, with CPU frame preparation ≤ 4 ms. 100,000 static meshes stay ≥ 60 FPS. A DLL edit to the animating system reloads without frame-time spikes above 100 ms.

### M4 — Cross-platform parity

Run the same engine on Linux and macOS. Platform contributors own native implementation and verification and can start now.

- [ ] macOS/Linux: random-byte hook for `AssetId::Generate`.
- [ ] macOS/Linux: native game-module load/unload/replace (`dlopen`), with build-toolchain capture for game-only builds.
- [ ] macOS/Linux: native directory watching (FSEvents/inotify), or a confirmed polling fallback.
- [ ] macOS/Linux: an audio backend for the existing one-shot service.
- [ ] Shader toolchain (DXC/spirv-val) on all hosts, plus the MoltenVK portability subset.
- [ ] CI builds and CPU tests on Windows, Linux and macOS.
- [ ] Finish Win32 key translation, repeat/text input and focus state, and verify mixed-DPI movement. Bring the other backends to the same input contract.
- [ ] Verify queue-family configurations beyond the single-family device, and document GPU requirements.

**Exit:** every sample and every reload path runs in Debug/Release on all three platforms, with native verification recorded by the responsible contributor.

### M5 — Hot reload completeness and iteration speed

- [ ] Reload every shader stage (vertex, fragment and compute), with dependency tracking for includes.
- [ ] Record each asset's source, dependencies and build outputs for images, FBX and HLSL. Reimport when a dependency changes.
- [ ] Reload systems registered by the DLL, and decide the policy for added or removed systems.
- [ ] Reduce DLL save-to-present from about 3.5 s to a target set after profiling the build (for example below 1.5 s for a one-file edit).
- [ ] Extend last-good recovery and diagnostics to every newly supported replacement path.

**Exit:** code, every shader stage and supported assets reload on all platforms with recorded timings and failure recovery.

### M6 — Gameplay services and samples

- [ ] Expose stable scene access, ECS views and runtime resource handles to game code.
- [ ] Generalize component and asset serialization beyond the current sample codecs, with round-trip tests.
- [ ] Import multi-object FBX files with stable sub-asset identities.
- [ ] Add world-to-screen and screen-to-world helpers once a caller needs them, plus reusable camera controllers as gameplay requires them.
- [ ] Keep the 3D samples and Courtyard (2D) working on all platforms through the reusable engine.

**Exit:** a small 3D game, the stress sample and the 2D sample run on all platforms using only engine APIs.

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
