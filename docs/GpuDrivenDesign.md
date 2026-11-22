# M3 GPU-driven rendering design (draft for owner review)

Status: **draft, 2026-11-22. Not approved; no M3 code exists.** This note proposes how M3 moves per-object work from the CPU to the GPU. Each decision states how it serves the v1 goals in [ROADMAP.md](../ROADMAP.md). Questions that need an owner decision are collected in [Owner decisions](#owner-decisions).

## Goals and fixed constraints

- **Exit (roadmap, proposed targets on the minimum-spec laptop):** 50,000 lit meshes, 20% animated, at ≥ 60 FPS at 2560 × 1440, with CPU frame preparation ≤ 4 ms. 100,000 static meshes at ≥ 60 FPS. A DLL edit to the animating system reloads without frame-time spikes above 100 ms.
- The laptop's display is 2560 × 1440 at 165 Hz, so the existing benchmark already renders at the exit resolution.
- ECS and hot reload stay joint goals:
  - Renderer state must survive game-DLL replacement.
  - It must hold no module pointers.
  - It must never require component layout versions.
- Every Vulkan feature needs a documented fallback or a clear unsupported-device error, including on MoltenVK (M4).
- 3D is the primary target. 2D runs only on an explicit owner request.
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
- Three of the costs scale with the total object count, not with what changed:
  - rebuilding every draw each frame
  - frustum-testing every object on the CPU
  - writing 112 bytes per visible object every frame
- M3 makes CPU cost proportional to **changed** objects, and the GPU does the per-object work it is good at.

**M2 exit is still open.** Step 9 (budgets, image comparison, validation, live session, the 10k scene) has not run. The M2 budget "renderer preparation + record/submit ≤ 0.8 ms" currently reads 0.88 ms (FIFO) to 1.01 ms (mailbox), so it may need either a short fix or an owner decision at that check. M3 removes that preparation cost altogether.

## How M3 serves the v1 goals

| Decision | Performance at scale | ECS | Hot reload | Cross-platform | Future editor |
| --- | --- | --- | --- | --- | --- |
| G1 Render world synced from change ticks | CPU work ∝ changed objects | Reads ECS through views and ticks; no new component types | Host-owned; survives DLL reload untouched | Pure CPU code | One render world per scene view, so edit and play views can coexist |
| G2 Resident object buffer + dirty scatter | Upload ∝ changed objects | Ticks decide what is dirty | Reloaded systems simply produce new ticks | Plain storage buffers | — |
| G3 Mesh table over the R6 pools | Indirect draws need it | `MeshHandle` keeps its identity | FBX reload rewrites one table entry | — | — |
| G4 Bindless textures and a material table | Batches collapse to one per mesh | `MaterialComponent` unchanged | PNG reload rewrites one descriptor in place | Descriptor indexing has a fixed-array fallback | — |
| G5 Compute culling and indirect draws | Culling leaves the CPU; recording stays constant | — | Engine compute shader; reloadable in M5 | Fixed-count path works everywhere | — |
| G6 Measured stress scenes | Proves the exit targets | Moving objects driven by a reloadable system | Measures the reload spike | — | — |
| G7 Scope limits (sprites, Hi-Z) | Only where measured | — | — | — | — |

## Decisions

### G1 — A host-owned render world, synced from change ticks

- `RenderWorld` is renderer-side state in the host. It holds one **object slot** per renderable entity (entity → slot map, slot → entity), plus each slot's mesh index, material index, flags and a CPU copy of the GPU record.
- A host step, `SyncRenderWorld(SceneAccess, RenderWorld&)`, runs after `Scene::UpdateWorldTransforms()` and before drawing:
  - It walks `View<const WorldTransformComponent, const StaticMeshComponent>` and compares each element's change ticks with the tick saved by the last sync. Only changed entities are rewritten.
  - Material changes are handled the same way, through `MaterialComponent` ticks.
  - The walk is O(n) in 4-byte tick comparisons, which costs tens of microseconds at 50k. It does not copy or test every object.
- **Removals and additions:** the Registry has no removal events, and runtime spawning is deferred (D5). Proposed: each pool exposes a structural revision counter, bumped by `Add`/`Remove`/`Destroy`. When the revision of a watched pool changes, the sync reconciles the whole slot table, which is O(n) but only on structural change. When it doesn't change, the sync stays incremental. This is a small Registry addition and needs no hooks or module callbacks.
- Slots are freed by swap-with-last, so the object buffer stays dense and the culling dispatch covers exactly the live count.
- **Ownership:**
  - The render world is owned next to the `Scene`/`SceneSession` that it mirrors, and it borrows the `VulkanContext` resources it names.
  - A DLL replacement leaves both the Registry and the render world untouched.
  - A scene switch rebuilds the render world once.
- **Why this serves the goal:** it is the only part of the CPU that must touch every object, and it touches 4 bytes per object, not 112. Holding the state in the host, not in game code, keeps the reload contract unchanged.

### G2 — GPU-resident object data with dirty-range scatter

- One device-local storage buffer holds a 128-byte `ObjectData` per slot:
  - world rows (48 B)
  - normal-matrix columns (48 B)
  - tint (16 B)
  - mesh index, material index, flags (12 B)
  - padding (4 B)
- At 100,000 objects this is 12.8 MB.
- Each frame, the sync writes `(slot, ObjectData)` records for changed objects into the frame's upload ring (R4). A small compute pass scatters them into the object buffer.
  - With 20% of 50k animated, that is about 10,000 × 132 B ≈ 1.3 MB per frame, written once.
  - Today, 50k visible objects would mean 5.6 MB per frame.
- Normal matrices are computed on the CPU only for changed lit objects, as R4 already does for visible ones.
- **Synchronization:**
  - One object buffer is shared by all frames. The scatter of frame N + 1 is ordered after frame N's vertex reads by a synchronization2 barrier on the same queue.
  - The scatter writes only slots whose values changed, so no double buffering is needed.
  - The R3 timeline retires a replaced buffer when the buffer grows.
- **Why:** the object count costs GPU memory once, and per-frame bandwidth follows motion, not scene size.

### G3 — Mesh table over the R6 pools

- A small storage buffer holds one 32-byte entry per mesh slot: `firstIndex`, `indexCount`, `vertexOffset` and local bounds (center and half extent).
- `MeshHandle` keeps its context, index and generation. Its index is the table index.
- An FBX reload already writes a new pool range behind the same handle (R6). M3 adds a table-entry rewrite in frame order; the old range is retired as today.
- **Why:** indirect commands and culling need geometry and bounds on the GPU, and R6 already made every lit mesh a range in two shared buffers.

### G4 — Bindless textures and a material table

- **Textures:** one `UPDATE_AFTER_BIND`, `PARTIALLY_BOUND` sampled-image array (Vulkan 1.2 descriptor indexing, which the device reports), indexed by texture slot.
  - Texture reloads and releases rewrite or clear one element.
  - Released slots are reused only after R3 retirement.
- **Materials:** a material table (base color, texture index, lit flag) replaces per-draw descriptor binds. `MaterialComponent` is unchanged; the sync maps it to a material index.
  - Identical materials share an entry, so the table stays small.
- **Batching:** with textures bindless, batches collapse to **one per mesh**, and pipelines differ only for opaque versus transparent.
- **Reload impact (owner decision Q3):**
  - The lit fragment shader interface changes: it receives a material index instead of a bound texture.
  - The fragment interface check (`FragmentShaderInterface`) moves to the new interface, so reloadable lit shaders keep live reload with last-good recovery.
  - Existing user-edited fragment shaders must be ported once.
- **Fallback:**
  - If update-after-bind or non-uniform indexing is missing (some MoltenVK configurations), use a fixed-size array (4,096) updated between frames, with uniform indexing per batch.
  - A device that has neither gets a clear unsupported-device error.
- **Why:** per-object material differences no longer split draws, which is what lets the draw count follow unique meshes at 100k.

### G5 — Compute frustum culling and indirect draws

- **Per frame, after the scatter:**
  1. Copy the CPU-written template indirect commands (one `VkDrawIndexedIndirectCommand` per mesh with `instanceCount = 0` and `firstInstance` set to that mesh's region) into the frame's indirect buffer.
  2. A culling compute pass runs one thread per live slot. It transforms the mesh's local bounds by the object's world matrix, tests the frustum (planes from the frame uniform), and appends the slot index to its mesh's region of a visible-instance buffer with an atomic increment of that command's `instanceCount`.
  3. A barrier makes the commands visible to indirect reads.
  4. One `vkCmdDrawIndexedIndirect` with `drawCount` = mesh count draws everything.
  5. The vertex shader reads `visible[gl_InstanceIndex]`, then the object data and the material.
- **Fixed count first (owner decision Q4):** with one command per mesh, the count is the number of unique meshes, so empty commands cost almost nothing. This path works on every device, including MoltenVK. `vkCmdDrawIndexedIndirectCount` (reported available) is added only if measurements show empty commands costing time with many unique meshes.
- **Region sizes:** each mesh's region holds that mesh's object count, maintained by the sync on structural change, so appends never overflow.
- **Determinism:** atomic append order varies between frames. Opaque, depth-tested meshes render identically except at exactly coincident surfaces, the same caveat as R5. The capture tolerance covers edge pixels, and coincident geometry in the samples would show up as a capture failure.
- **CPU after M3:**
  - the render-world sync (tick comparisons plus changed objects)
  - writing changed records
  - a constant number of commands
- **The immediate path stays.** The `DrawFrame` spans keep working for textured, transparent and debug draws. Transparent draws keep CPU back-to-front sorting.
- **Why:** this is the step that removes culling and per-object instance preparation from the CPU and keeps recording constant at any object count.

### G6 — Stress scenes and measurements

- Extend `RavenSceneTool create-benchmark` to 10k, 50k and 100k meshes. Keep the generated scenes local under `out/`, not in `Content/`.
- **Stress sample:** 50,000 meshes, 20% moved by a system registered by the reloadable game DLL.
- **Measurements:**
  - The benchmark records CPU stages, GPU time split into scatter, cull and draw (timestamps per pass), upload bytes and frame times.
  - A scripted DLL edit to the animating system measures the reload spike against the 100 ms target.
- **First step:** measure the M2 path at 10k and 50k before changing anything. This replaces the linear estimates above with real numbers and confirms where the time goes.

### G7 — Scope limits

- **Sprites (owner decision Q5):**
  - The roadmap puts sprites on the same path.
  - They are the 2D path and need an owner-requested 2D run.
  - Proposed: do it last in M3, after the 3D exit measurements pass, or move it to M6 if 3D needs all of M3's time.
- **Hi-Z occlusion (owner decision Q6):** add it only if the 100k measurement misses its target on GPU time. The benchmark grid has little occlusion, so Hi-Z would add a depth pre-pass and a pyramid without proof of benefit.
- **Out of scope for M3:**
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

- Image comparison of the six existing captures against the M2 references, with the approved tolerance.
- The Debug 3D smoke with core and synchronization validation, and a Debug validation run of the 50k scene.
- The live Release session: DLL, scene, PNG, FBX and fragment-shader reloads, including the ported lit shader and the incompatible-shader rejection.
- The stress benchmark and reload-spike measurement above, recorded in this document.
- A tracked test only for behavior M3 rewrites that the scripts cannot see: the render-world sync's handling of add, remove and change ticks (CPU, no GPU), if G1 is approved.

## Migration steps

0. **M2 exit check** (M2 step 9), still pending.
1. **G6 baseline:** extend the scene tool to 10k/50k/100k and measure the M2 path. No renderer change.
2. **G4:** bindless texture array, material table, ported lit fragment shader and interface check. CPU batching keys drop the texture.
3. **G1 + G2 + G3:**
   - The render world, resident object buffer, dirty scatter and mesh table.
   - Draws still come from a CPU-culled list of slot indices, which proves the data path and removes per-object instance preparation.
   - The Registry gets its structural revision counter (owner decision Q2).
4. **G5:** compute culling and fixed-count indirect draws; CPU culling for scene meshes is removed.
5. **G6 stress sample:** reloadable animating system, reload-spike measurement.
6. **Conditional:** `DrawIndexedIndirectCount` (only if measured useful), Hi-Z (only if 100k misses on GPU time).
7. **Sprites** onto the path (only with an owner 2D request; otherwise moved to M6).
8. **Exit check.**

## Owner decisions

| # | Question | Recommendation |
| --- | --- | --- |
| Q1 | Where does GPU object state come from? | A host-owned render world synced from change ticks (G1). The alternative, writing GPU records from game systems, would put renderer contracts into reloadable code. |
| Q2 | How are removals detected? | A per-pool structural revision counter in the Registry, with full reconcile only when it changes. The alternative is per-type remove hooks (D1/D6), which are not built yet and add callback lifetimes. |
| Q3 | Accept a new lit fragment shader interface for bindless materials? | Yes. Live reload and the interface check stay, and existing shaders are ported once. |
| Q4 | Fixed-count indirect commands per mesh, or count-compacted commands? | Fixed count first. It works everywhere; add indirect count only if measured useful. |
| Q5 | Sprites in M3? | Last in M3 with your 2D request, or move them to M6. |
| Q6 | Hi-Z occlusion? | Only if the 100k measurement misses on GPU time. |
| Q7 | Public API | The scene path moves from `DrawFrame` spans to the render world. Immediate spans stay for textured, transparent and debug draws. |
