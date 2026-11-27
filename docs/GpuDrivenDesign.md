# M3 GPU-driven rendering design

Status:
- Drafted 2026-11-22. The owner decided Q1–Q7 on 2026-11-22/23; see [Owner decisions](#owner-decisions).
- **Implemented and exit-checked 2026-11-23**; see the [M3 exit record](#m3-exit-record-2026-11-23).
- Each decision below states how it serves the v1 goals in [ROADMAP.md](../ROADMAP.md).

## Goals and fixed constraints

- **Exit (roadmap, on the minimum-spec laptop):**
  - 50,000 lit meshes, 20% animated, at ≥ 60 FPS at 2560 × 1440, with CPU frame preparation ≤ 4 ms.
  - 100,000 static meshes at ≥ 60 FPS.
  - A DLL edit to the animating system reloads without frame-time spikes above 100 ms.
- The laptop's display is 2560 × 1440 at 165 Hz, so the existing benchmark already renders at the exit resolution.
- ECS and hot reload stay joint goals. Renderer state must survive game-DLL replacement, must hold no module pointers, and must never require component layout versions.
- Every Vulkan feature needs a documented fallback or a clear unsupported-device error, including on MoltenVK (M4).
- 3D is the primary target. 2D runs only on an explicit owner request.
- **No throwaway work (owner, 2026-11-22).** Do not write code in M3 that a later step or the next milestone replaces. A step may be larger if that keeps every piece in the final design.
- Keep tracked tests minimal. Verify with the existing smoke, capture comparison, live session and benchmark scripts, plus focused checks.

## Where M2 leaves the cost

4,000-mesh benchmark after M2 step 7 (auto FIFO, 2560 × 1440, p95):

| Stage | 4,000 meshes | Scales with | Linear estimate at 50,000 |
| --- | ---: | --- | ---: |
| ECS systems | 0.03 ms | animated entities | ~0.4 ms |
| World transforms (incremental, D7) | 0.13 ms | changed entities | ~1.6 ms |
| Scene gather into `VulkanDraw` spans | 0.18 ms | **all** meshes | ~2.2 ms |
| Renderer preparation: validation, culling, batching, instance records | 0.73 ms | **all** meshes | ~9 ms |
| Record/submit | 0.15 ms | batches (7) | ~0.15 ms |
| CPU frame preparation (total) | 1.17 ms | | **~13 ms** |
| GPU | 0.38 ms | visible meshes | ~5 ms |

- The CPU estimate exceeds the 4 ms budget about threefold. The GPU estimate fits.
- Three costs scale with the total object count, not with what changed:
  - rebuilding every draw each frame
  - frustum-testing every object on the CPU
  - writing 112 bytes per visible object every frame
- M3 makes CPU cost proportional to **changed** objects, and the GPU does the per-object work.

**M2 exit is still open.** Step 9 has not run. The M2 budget "renderer preparation + record/submit ≤ 0.8 ms" currently reads 0.88–1.01 ms. Optimizing it would mean tuning CPU culling and batching code that M3 removes. Under the no-throwaway rule, the proposal is to record the miss with that reason at the exit check. That remains the owner's call at the check.

## How M3 serves the v1 goals

| Decision | Performance at scale | ECS | Hot reload | Cross-platform | Future editor |
| --- | --- | --- | --- | --- | --- |
| G1 Render world, change ticks and lifecycle journal | CPU work ∝ changed objects | ECS stays the single source of truth; the journal serves every future resource service | Host-owned; survives DLL reload untouched | Pure CPU code | One render world per scene view |
| G2 Resident object buffer + dirty scatter | Upload ∝ changed objects | Ticks decide what is dirty | Reloaded systems simply produce new ticks | Plain storage buffers | — |
| G3 Mesh table over the R6 pools | Indirect draws need it | `MeshHandle` keeps its identity | FBX reload rewrites one table entry | — | — |
| G4 Bindless lit materials behind an engine header | Batches collapse to one per mesh | `MaterialComponent` unchanged | PNG reload rewrites one array element; shaders reload against one header | Per-texture fallback hidden by the header | — |
| G5 Compute culling and fixed-count indirect draws | Culling leaves the CPU; recording stays constant | — | Engine compute shader; reloadable in M5 | One path on every device | — |
| G6 Measured stress scenes | Proves the exit targets | Moving objects driven by a reloadable system | Measures the reload spike | — | — |
| G7 Public API: lit meshes only from the render world | Deletes the per-frame gather | — | — | — | Same API for every view |

## Decisions

### G1 — A host-owned render world (Q1, Q2)

**What it holds**
- `RenderWorld` is renderer-side state in the host. It has one **object slot** per renderable entity: an entity → slot map, and slot → entity, mesh index, material index, pass and flags, plus a CPU copy of the GPU record.
- Each object has a **pass** field. M3 accepts only "opaque". Transparent objects and sprites join in M6 without an API change (Q5, Q7).

**How it stays in sync**
- A host step, `SyncRenderWorld`, runs after `Scene::UpdateWorldTransforms()` and before drawing. It handles two kinds of change:
  - **Value changes** come from change ticks. For each mesh, the step compares the transform, mesh and material ticks with the tick saved at the last sync, then rewrites only changed objects. The walk is O(n) in 4-byte tick comparisons, which costs tens of microseconds at 50k.
  - **Additions and removals** come from the **lifecycle journal** below. There are no rescans and no callbacks.

**Ownership**
- The render world sits next to the `Scene`/`SceneSession` it mirrors.
- A DLL replacement leaves both the Registry and the render world untouched.
- A scene switch rebuilds the render world once.

#### The lifecycle journal (Q2, a Registry contract addition)

The Registry gains a general, opt-in journal of structural events. It serves the render world and every later resource service, such as network connectors, audio emitters or physics bodies. Each of those keeps plain settings in a component and owns the real resource in a host service keyed by entity.

- **Entries:** `Added(entity, type)` or `Removed(entity, type)`, about 16 bytes. The entity includes its generation, so a reused slot is never confused with the old entity. Destroying an entity writes `Removed` for each subscribed type it had. Entries hold no values by default; a type may opt in to copying the removed value.
- **Opt-in per type:** a service subscribes to the component types it needs. Only subscribed types record, so the rest cost nothing.
- **In memory only:** the journal is never saved and never part of scene files. It is not history, undo, or network replay.
- **Reading and trimming:**
  - Each subscriber has a read position and processes entries at its own point in the frame. The render sync runs before drawing, so a removed object is never drawn.
  - Entries are dropped once every subscriber has read past them.
  - A subscriber that falls behind a cap loses its backlog and is flagged for a one-time rescan, so memory cannot run away and no ghost survives.
- **New subscribers** scan their pools once, then follow the journal from that point.
- **Events tell what happened; pools tell what is true now.** A consumer checks current state before acting, because an `Added` may already have been followed by a `Removed`.
- **Threading:** structural changes and journal writes stay on the main thread, as today. A service may hand slow work, such as socket I/O, to its own thread after reading.
- **Disable/enable** is modeled as a tag component (for example `DisabledComponent`), so it is an ordinary add/remove event. The render world hides a disabled object's slot instead of freeing it.
- **Follow-up, outside M3:** views that exclude a component (for example, "everything except disabled").
- It does not depend on the deferred Commands work (D5). Commands will feed the same journal when they arrive.
- The contract is recorded in `docs/ECSDesign.md` when it is implemented, with one `RegistryTests` case covering add, remove, destroy, multiple subscribers, trimming and overflow.

### G2 — GPU-resident object data with dirty-range scatter

- **Layout:** one device-local storage buffer holds a 128-byte `ObjectData` per slot (12.8 MB at 100,000 objects):
  - world rows (48 B)
  - normal-matrix columns (48 B)
  - tint (16 B)
  - mesh index, material index and flags (12 B)
  - padding (4 B)
- **Uploads:** each frame, the sync writes `(slot, ObjectData)` records for changed objects into the frame's upload ring (R4), and a small compute pass scatters them into the object buffer.
  - With 20% of 50k animated, that is about 1.3 MB per frame.
  - The current path would upload 5.6 MB per frame for 50k visible objects.
- Normal matrices are computed on the CPU only for changed lit objects.
- **Synchronization:** one object buffer serves all frames. Frame N + 1's scatter is ordered after frame N's vertex reads by a synchronization2 barrier on the same queue. A grown buffer is retired through the R3 timeline.

### G3 — Mesh table over the R6 pools

- A storage buffer holds one 32-byte entry per mesh slot: `firstIndex`, `indexCount`, `vertexOffset` and local bounds.
- A `MeshHandle` keeps its identity, and its index is the table index.
- An FBX reload already writes a new pool range behind the same handle (R6). M3 adds a table-entry rewrite in frame order, and the old range is retired as today.

### G4 — Bindless lit materials behind an engine header (Q3)

- **Textures:** one `UPDATE_AFTER_BIND`, `PARTIALLY_BOUND` sampled-image array, indexed by texture slot.
  - A texture reload or release rewrites or clears one element.
  - A released slot is reused only after R3 retirement.
- **Materials:** a material table (base color, texture index, lit flag) replaces per-draw texture binds for lit meshes. Identical materials share an entry, and `MaterialComponent` is unchanged.
- **Engine header:** lit fragment shaders call engine functions from `Raven/Material.hlsli`, for example `RavenSampleBaseColor` and `RavenApplyLighting`, instead of declaring the arrays themselves.
  - The header starts minimal and grows only when a material feature needs it.
  - The existing lit shaders are ported once.
  - The fragment interface check moves to the new layout. Live reload and the rejection of incompatible shaders stay.
  - **The header is a reload dependency of every watched lit shader**, so editing it reloads them. This is a small special case until M5 adds general include tracking. *M5 replaced it with DXC-reported includes for every program ([HotReloadDesign.md](HotReloadDesign.md#h2--shader-programs-for-every-stage-q2)).*
  - **Raw declarations are allowed** as a documented advanced path, with the layout marked "may change between versions". A shader that declares the same bindings passes the same check.
- **Fallback:** if a device lacks update-after-bind or non-uniform indexing (some MoltenVK configurations), the engine batches per texture with a fixed-size array. The header hides the difference, so shaders are identical on every platform. A device with neither path gets a clear unsupported-device error.
- **Scope:** the textured and sprite path keeps its per-texture descriptors and is untouched in M3 (Q5).
- **Version 2 of the fragment interface (M6B B4, 2026-11-27; [RenderingContentDesign.md, contract 1](RenderingContentDesign.md#1-fragment-abi-v2-and-the-material-record-aa2-rnd-01-fd-19)).** Additive to version 1:
  - **Inputs:** `RavenFragmentInput` gains `nointerpolation uint Material` (location 7) and `float3 WorldPosition` (location 8); locations 0–6 are unchanged.
  - **Resources a fragment shader may read:** the frame uniform (set 1, `Raven/Frame.hlsli`, now with `Time`: seconds and frame delta), the material records (set 2 binding 2) and their values (set 2 binding 7, `float4`), and the sampler set as `SamplerState RavenSamplers[2]` (0 clamps, 1 repeats) at set 0 binding 1, replacing the single sampler.
  - **Accessors:** `RavenMaterialTexture(input, slot, uv)` samples a texture slot with that slot's sampler, `RavenMaterialValue(input, index)` reads a value, and `RavenGetFrame()` returns time, delta and camera position. `RavenSampleBaseColor` now reads slot 0 through them.
  - **One check for every stage:** `ShaderInterface` gained the fragment stage, textures and samplers; a fragment shader may read any subset of the inputs and resources. `FragmentShaderInterface` was removed. Version 2 becomes part of the game contract when B4 lets games author material shaders.

### G5 — Compute frustum culling and fixed-count indirect draws (Q4)

**Each frame, after the scatter:**
1. Copy the CPU-written template commands into the frame's indirect buffer. There is one `VkDrawIndexedIndirectCommand` per unique mesh, with `instanceCount = 0` and `firstInstance` at that mesh's region.
2. A culling compute pass runs one thread per live slot. It transforms the mesh's local bounds by the object's world matrix, tests them against the frustum (planes from the frame uniform), and appends the slot to its mesh's region with an atomic increment of `instanceCount`.
3. A barrier makes the commands visible to indirect reads.
4. One `vkCmdDrawIndexedIndirect` with `drawCount` equal to the mesh count draws everything.
5. The vertex shader reads `visible[gl_InstanceIndex]`, then the object data and the material.

**Fixed count on every device:**
- Empty commands cost almost nothing at today's mesh variety.
- Command order is fixed, which keeps GPU captures readable.
- **The layout is the one compaction would need.** `vkCmdDrawIndexedIndirectCount` can be added later as a compaction pass and a count buffer, with nothing rewritten. It is triggered only by a measurement: empty commands costing more than about 0.1 ms of GPU time, or a scene with more than about 1,000 unique meshes. The fixed-count path stays as the fallback, which MoltenVK needs.
- **Required device features:** `multiDrawIndirect` and `drawIndirectFirstInstance`. Device selection checks both and gives a clear error if either is missing. MoltenVK support for the second is confirmed in M4.

**Other properties:**
- **Region sizes** follow each mesh's object count, which the sync maintains on structural change, so appends never overflow.
- **Determinism:** append order varies between frames. Opaque, depth-tested meshes render identically except at exactly coincident surfaces, the same caveat as R5.

### G6 — Stress scenes and measurements

- Extend `RavenSceneTool create-benchmark` to 10k, 50k and 100k meshes. Keep the generated scenes local under `out/`.
- **Stress sample:** 50,000 meshes, 20% moved by a system that the reloadable game DLL registers.
- **Measurements:**
  - CPU stages
  - GPU time per pass (scatter, cull and draw)
  - upload bytes
  - frame times
  - the reload spike from a scripted DLL edit to the animating system, against the 100 ms target
- **First step:** measure the M2 path at 10k and 50k, which replaces the linear estimates above with real numbers. It uses only the scene-tool extension, which stays.

### G7 — Public API and scope (Q5, Q6, Q7)

**Public API (Q7):**
- Lit meshes come only from the render world.
- The lit-mesh spans leave `DrawFrame`. That removes the samples' per-frame gathering loop and the step 6 CPU lit batching.
- `DrawFrame` keeps its spans for textured, transparent and debug draws. Transparent draws keep CPU back-to-front sorting. *(M6B's B5, 2026-11-27: the textured and transparent spans left `DrawFrame` when sprites joined the render world.)*

**Sprites (Q5):** moved to M6 and done in M6B's B5 ([RenderingContentDesign.md](RenderingContentDesign.md#b5--sprites-on-the-gpu-path-2026-11-27)). They move once, getting the bindless switch, GPU culling and the textured shader port together, with a single 2D verification. Until then, sprites keep per-texture descriptors and 2D performance is unchanged.

**Hi-Z occlusion (Q6):** built only if the 100k measurement misses its GPU-time target. The culling pass leaves room for it as one extra test.

**Out of scope for M3:**
- multithreaded recording
- a render graph
- LOD
- meshlets and mesh shaders
- new shading features
- GPU-driven transparency

## Proposed budgets (minimum-spec laptop, Release, 2560 × 1440)

| Measure | M2 (4k) | M3 target |
| --- | ---: | ---: |
| FPS, 50k lit meshes, 20% animated | — | ≥ 60 (exit); aim ≥ 120 |
| FPS, 100k static | — | ≥ 60 |
| CPU frame preparation p95, 50k, 20% animated | 1.17 ms at 4k | ≤ 4 ms |
| Renderer CPU (sync + records + record/submit) p95, 50k | 0.88 ms at 4k | ≤ 1.5 ms, flat for static scenes |
| Draw commands | 7 at 4k | one per unique mesh, plus immediate-path batches |
| Upload bytes per frame | 112 B × visible | 132 B × changed + uniform |
| GPU p95, 50k | 0.38 ms at 4k | ≤ 8 ms |
| DLL reload frame spike | — | ≤ 100 ms |

## Verification

- **Image comparison:** the six existing captures against the M2 references, with the approved tolerance.
- **Validation:** the Debug 3D smoke with core and synchronization validation, and a Debug validation run of the 50k scene.
- **Live Release session:**
  - DLL, scene, PNG, FBX and fragment-shader reloads
  - the ported lit shader
  - a reload triggered by editing the header
  - the incompatible-shader rejection
- **Benchmarks:** the stress benchmark and the reload-spike measurement, recorded in this document.
- **Tracked test:** one `RegistryTests` case for the lifecycle journal, which is a new Registry contract.

## Migration steps

0. *(Done 2026-11-23.)* **M2 exit check** (M2 step 9).
1. *(Done 2026-11-23.)* **G6 baseline:** extend the scene tool to 10k/50k/100k and measure the M2 path ([RendererDesign.md](RendererDesign.md#m2-exit-record-2026-11-23)).
2. *(Done 2026-11-23.)* **Lifecycle journal (G1, Q2):** the Registry addition, its `ECSDesign.md` record and the `RegistryTests` case.
3. *(Done 2026-11-23, together with step 4.)* **G4:** bindless texture array, material table, the engine header with reload dependency, the ported lit shader and interface check. Lit batches drop the texture from their key.
4. *(Done 2026-11-23.)* **G1 + G2 + G3 + G5 + G7 together:** the render world and sync, the resident object buffer and scatter, the mesh table, compute culling and fixed-count indirect draws.
   - Lit spans leave `DrawFrame`, and the samples' lit gathering and CPU lit batching are deleted.
   - This is one step, so no temporary CPU-culled path is ever written.
5. *(Done 2026-11-23.)* **G6 stress sample:** the benchmark scenes are the stress sample. Their rotators run in the reloadable game DLL, and `Benchmark-Windows3D.py reload --meshes 50000` measures the reload spike.
6. **Conditional, only if measured:** compaction with `DrawIndexedIndirectCount`, and Hi-Z. Not needed; see the exit record.
7. *(Done 2026-11-23.)* **Exit check.**

## M3 exit record (2026-11-23)

Measured on the minimum-spec laptop, Release, 2560 × 1440 on the 165 Hz panel, one run per case:
- Rotators move 20% of the meshes, through a system registered by the reloadable game DLL.
- A 4K 60 Hz external monitor was attached during these runs, which capped the FIFO cases at about 60 FPS. The uncapped NVIDIA mailbox case is the reference.

| Measure | M2 path | Target | M3 | Result |
| --- | ---: | ---: | ---: | --- |
| FPS, 50k lit meshes, 20% animated | 85.8 | ≥ 60 (aim ≥ 120) | 126.5 | Met |
| CPU frame preparation p95, 50k, 20% animated | 12.34 ms | ≤ 4 ms | 3.53 ms (FIFO 3.18 ms) | Met |
| FPS, 100k static | — | ≥ 60 | 121.3 (CPU preparation 1.71 ms) | Met |
| FPS, 100k, 20% animated (no target) | 43.2 | — | 78.0 (CPU preparation 6.66 ms) | — |
| Renderer CPU (preparation + record/submit) p95, 50k | 8.74 ms | ≤ 1.5 ms, flat for static scenes | 0.53 ms; 0.16 ms at 100k static | Met |
| Draw commands | 7 | one per unique mesh + immediate batches | 6 (2 indirect mesh commands + 4 textured batches) | Met |
| GPU p95, 50k | 3.18 ms | ≤ 8 ms | 2.15 ms (upload + culling 0.17 ms) | Met |
| DLL reload frame spike, 50k | — | ≤ 100 ms | longest frame 46.5 ms; save-to-present 3.08 s | Met |
| 4,000 meshes (M2 exit case) | 193 FPS, CPU 1.35–1.54 ms | — | 275 FPS, CPU 0.41 ms | — |

**Stock-host re-run (2026-11-24, planning decision 2).** The numbers above came from a source-patched benchmark host. The same cases were re-run on the unpatched host through its launch options (`--present`, `--borderless`, `--max-fps`), Release, 2560 × 1440 on the 165 Hz panel with no external monitor attached, one run per case. Evidence is under ignored `out/benchmark-scale-ep3o8psa`, `-26cez9d1` and `-nqwspep_`.

| Case | FPS | Frame p50 / p95 | CPU preparation p95 | GPU p95 |
| --- | ---: | ---: | ---: | ---: |
| 50k, 20% animated, FIFO (default) | 165.0 | 6.06 / 6.28 ms | 3.05 ms | 2.48 ms |
| 50k, 20% animated, Mailbox | 126.5 | 7.81 / 9.07 ms | 3.87 ms | 2.16 ms |
| 100k static, FIFO (default) | 163.7 | 4.98 / 8.75 ms | 1.53 ms | 5.02 ms |
| 100k static, Mailbox | 119.1 | 8.34 / 9.15 ms | 2.00 ms | 4.02 ms |
| 50k, 20% animated, Mailbox, `--max-fps 120` | 109.0 | 9.08 / 10.03 ms | 3.59 ms | 2.50 ms |

- Every M3 target is met on the stock host. With the default FIFO and no cap it reaches the panel's 165 Hz, which the old fixed 120 FPS sleep prevented.
- Mailbox runs slower than FIFO on this panel at both sizes. The cause was not investigated; FIFO is the default.
- **Sleep-cap precision:** a 120 FPS cap (8.33 ms) delivered 109 FPS: `std::this_thread::sleep_until` overshoots by about 0.75 ms at the median and 1.7 ms at p95. The cap is optional and off by default. A high-resolution waitable timer would tighten it if a caller needs an exact cap.

- **Images:** the six captures match the M2 references exactly as at M2 exit: three bit-identical, and 1, 2 and 1 edge outliers in Assemblies, Gallery and Puzzle. GPU culling, bindless textures and indirect draws reproduce the CPU path's images.
- **Validation:**
  - The Debug 3D smoke with core and synchronization validation reported nothing.
  - `Benchmark-Windows3D.py validate --meshes 50000` passed Vulkan validation.
- **Live Release session:** passed. It covers:
  - DLL, scene, PNG, FBX and lit-shader reloads, with the lit shader ported to `Raven/Material.hlsli`
  - a reload triggered by editing the header, then restored
  - an incompatible shader with an extra stage input, rejected with the last good shader kept
- **Tracked test:** `RegistryTests` covers the lifecycle journal.
- **What remains on the CPU at 50k:** world transforms (1.5–1.7 ms p95) and the render-world sync inside scene gather (1.1–1.3 ms), both ECS-side. A faster `UpdateWorldTransforms` would be the next lever if the budget tightens; nothing in M3 needs it.
- **Not run:** Courtyard (2D), which needs an owner request. Its path is unchanged apart from the shared upload ring and the descriptor refactor, and the 3D captures cover the textured demonstration draws. MoltenVK and Linux (M4).

### Deviations from the decisions above

- **Steps 3 and 4 landed as one change.** On its own, bindless (step 3) would have needed a temporary material path on the CPU instance records, which the no-throwaway rule rules out.
- **Per-texture fallback (G4) not built.** M3 requires descriptor indexing (including update-after-bind sampled images), `multiDrawIndirect` and `drawIndirectFirstInstance`, and reports a missing one as an unsupported-device error. No device here lacks them. If MoltenVK needs the fallback, M4 builds it behind the same header.
- **Uploads:** a full upload (new world, scene switch, journal resync or buffer growth) copies whole buffers. Incremental frames scatter only dirty records with a compute pass. Material, mesh-table and command uploads are small copies every frame.
- **Removed-value copies (Q2)** and **compaction or Hi-Z (G5, Q6)** were not needed. There are only two unique meshes in the stress scenes, and GPU time is 2.2 ms at 50k and 4.0 ms at 100k static.
- **GPU timing:** the benchmark records total GPU time and the upload-plus-culling prepass (`gpu_cull_ns`). The visible count comes from a delayed readback of the indirect commands.

## Owner decisions

| # | Question | Decision |
| --- | --- | --- |
| Q1 | Where does GPU object state come from? | **A** (2026-11-22): a host-owned render world synced from change ticks. |
| Q2 | How are additions and removals detected? | **C with limits** (2026-11-22): an opt-in, in-memory lifecycle journal per component type, as described in G1. |
| Q3 | Shader interface for bindless materials | **A** (2026-11-22): bindless lit materials behind `Raven/Material.hlsli`, with the header as a reload dependency and raw declarations as an advanced path. The per-texture fallback is hidden by the header. |
| Q4 | Indirect command layout | **A** (2026-11-22): one fixed-count command per mesh on every device, laid out for later compaction, which is added only if measured. |
| Q5 | Sprites in M3? | **B** (2026-11-22): untouched in M3 and moved to M6 in one pass. The decision applied the no-throwaway rule. |
| Q6 | Hi-Z occlusion | **A** (2026-11-22): only if the 100k measurement misses on GPU time. |
| Q7 | Public API | **A** (2026-11-23): lit meshes come only from the render world. `DrawFrame` keeps textured, transparent and debug spans. Objects carry a pass field, so transparent objects and sprites can join in M6. |
