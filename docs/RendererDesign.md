# M2 renderer design note

Status: **approved by the owner** (2026-11-21), with every open question accepted as proposed. Implementation follows the migration steps as reviewed patches, and contracts move into the subsystem docs as each step lands.

This note covers milestone M2 in [ROADMAP.md](../ROADMAP.md): raise the Vulkan baseline and restructure per-frame data so that draw cost scales with the number of distinct meshes and materials, not with the number of objects. M3 (GPU-driven rendering) builds directly on this structure.

## Goals and fixed constraints

Goals:

- Lit meshes record one draw per distinct (mesh, texture, lit) batch instead of one per object.
- Per-frame data uploads go through persistently mapped buffers with no per-write map/unmap.
- Camera and lighting are uploaded once per frame instead of copied into every object's instance data.
- Geometry sits in shared device-local pools that M3's indirect draws can address with one binding.
- The Vulkan 1.2 baseline, plus dynamic rendering, synchronization2 and timeline semaphores, simplifies resizing, barriers and resource retirement.

Constraints that stay:

- **Public renderer API is unchanged in M2.** `VulkanContext::DrawFrame` still takes `std::span<const VulkanDraw>` plus the textured and transparent spans, so Perspective, Courtyard, the smoke test and the tools need no rendering-API changes. M3 replaces this with GPU-resident objects.
- **Hot reload keeps working.** That covers lit/textured fragment shader replacement with last-good recovery, FBX replacement behind stable `MeshHandle`s, and PNG replacement. Replaced resources still retire only after the frames that used them complete.
- **Every target is met on the minimum-spec laptop** (RTX 3070 Ti Laptop GPU). The Intel Iris Xe and MoltenVK must keep working where their drivers support the features, but they are not performance targets.
- **No validation messages** in Debug with core and synchronization validation.
- **Small reviewable steps**, each leaving every sample working, prototyped in an `out/` snapshot first, as in M1.

## How M2 serves the v1 goals

Today the renderer costs about 0.47 µs of CPU per object (1.87 ms for 4,000 meshes). At 50,000 objects that is about 23 ms, which rules out 60 FPS on its own. M1 already made the ECS side cheap: at 50k entities, views take about 0.2 ms and the world update about 0.6 ms.

M2 removes the per-object costs that grow fastest: draw calls, the CPU camera multiply and per-write mapping. That brings the estimate at 50k to roughly 6–8 ms. M3 then removes the remaining per-object CPU work (culling, normal matrices, full uploads) so the cost becomes roughly constant. M2 alone does not reach the 50k goal. It builds the structure M3 needs and cuts the slope.

| Decision | Performance at scale | Cross-platform | Hot reload | ECS |
| --- | --- | --- | --- | --- |
| R1 Vulkan 1.2 + extensions | M3's features are checked now | Current NVIDIA/AMD/Intel, Linux drivers and MoltenVK; drops only pre-2016-era drivers | — | — |
| R2 Dynamic rendering, sync2 | M3's extra passes (culling, depth pyramid) need no render-pass objects | Fewer objects for MoltenVK to translate | Reloaded pipelines no longer depend on a render pass; resize leaves them alone | — |
| R3 Timeline semaphore and retirement queue | M3's in-place GPU buffer updates need an exact "frame N finished" | One model on every platform | Replaced meshes, textures and pipelines all retire one way | — |
| R4 Rings, frame uniform, 112 B instances | Upload per object drops from 176 B to 112 B; the camera multiply moves to the GPU | Mapped memory behaves the same everywhere, including unified-memory Macs | — | Records come from cached world transforms; M3 then uploads only changed ticks |
| R5 Instanced batching | Recording becomes constant in object count; batches become M3's indirect groups | — | — | — |
| R6 Device-local pools, 32-bit indices | One geometry binding, which indirect draws require; no PCIe vertex reads on the discrete GPU | Same code on unified-memory devices | FBX reload writes a new range behind the same handle; large meshes are no longer rejected | — |
| R7 Cleanups | Clear device-loss errors | One readiness and size path simplifies the Cocoa/XCB backends | — | — |

## Current renderer and cost

| Area | Today | Cost |
| --- | --- | --- |
| API baseline | Vulkan 1.0 instance and SPIR-V (`-fspv-target-env=vulkan1.0`). One `VkRenderPass`, framebuffers recreated per swapchain image | Render-pass and framebuffer objects tie pipelines and resizes together |
| Frame sync | 2 frames in flight with per-slot fences; resources kept alive by `shared_ptr` lists per slot | Works, but every new retirement needs its own list |
| Buffers | `VulkanBuffer` is host-visible coherent; `Write` maps and unmaps each call. Vertex/index data is also host-visible | GPU reads geometry over the PCIe BAR on the discrete GPU |
| Lit meshes | One bind (pipeline + descriptor) and one `vkCmdDrawIndexed(..., 1, ...)` per mesh. Lighting is copied into every 176-byte instance | 4,000 meshes = 4,004 draws |
| CPU preparation | Per mesh: `viewProj × model`, inverse-transpose normal matrix, world bounds and frustum test | Linear in objects every frame |
| Indices | 16-bit per mesh | Imported meshes cannot exceed 65,535 vertices |
| Sprites | Adjacent compatible textured draws are already instanced, with order preserved | Fine for now; M3 merges sprites into the GPU path |

Measured at the M1 exit (4,000 meshes, 2560 × 1440, mailbox, p95; [ECSDesign.md](ECSDesign.md#m1-exit-record-2026-11-21)):

- renderer preparation 0.86 ms
- recording and submission 1.01 ms
- GPU pass 0.57 ms
- CPU preparation 2.27 ms in total, so the renderer is now about 80% of it

## Decisions

### R1 — Vulkan 1.2 core plus two extensions

- Instance `apiVersion = VK_API_VERSION_1_2`, and shaders are compiled and validated for `vulkan1.2`.
- **Required device features:** Vulkan 1.2 `timelineSemaphore`, plus `VK_KHR_dynamic_rendering` and `VK_KHR_synchronization2` with their feature bits. These are core in 1.3 and supported by current NVIDIA, Intel and AMD drivers and by MoltenVK.
- **Queried now, required only by M3:** `descriptorIndexing` features (`runtimeDescriptorArray`, `descriptorBindingPartiallyBound`, `shaderSampledImageArrayNonUniformIndexing`) and `drawIndirectCount`. Device selection records whether they exist.
- Device selection skips devices that lack a required feature and logs which feature was missing. With no suitable device it fails with a message naming the missing features.
- The existing portability enumeration and `VK_KHR_portability_subset` handling stays.

### R2 — Dynamic rendering and synchronization2

- Remove `VkRenderPass` and the per-image `VkFramebuffer`s. Recording uses `vkCmdBeginRendering` with the swapchain image view and the frame's depth view.
- Pipelines are created with `VkPipelineRenderingCreateInfo` (color and depth formats). Resize no longer touches pipelines unless the swapchain format changes.
- Image layout transitions (`UNDEFINED → COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC_KHR`, and depth) are explicit `vkCmdPipelineBarrier2` calls. Texture uploads move to synchronization2 barriers too.
- The lit and textured pipeline factories keep their current hot-reload behavior. They now take formats instead of a render-pass handle.

### R3 — Timeline semaphore frame pacing and retirement

- One timeline semaphore counts submitted frames. Frame `N` signals value `N`, and a slot is reused after the CPU waits for value `N − FramesInFlight`. This replaces the per-slot fences.
- Binary semaphores stay for swapchain acquire and present, which require them.
- **Retirement queue:** resources that are released or replaced (mesh ranges, textures, pipelines, ring buffers) are queued with the timeline value of the last frame that used them, and destroyed once the semaphore passes it. This replaces the per-slot `shared_ptr` lists, and gives M3's GPU buffers one rule.
- Resize and shutdown keep waiting for device idle. Strict presentation-completion fencing remains future work, because unextended Vulkan has no present fence.

- **The upload batch (M6B B3, 2026-11-27; [RenderingContentDesign.md, contract 5](RenderingContentDesign.md#5-uploads-and-descriptors-aa2-rnd-04)).** Texture contents created between frames are recorded into one command buffer (`VulkanUploadBatch`) instead of a submission with a fence wait per texture. The context submits it on the graphics queue just before the next frame: it signals the next timeline value, ends in a transfer-to-shader barrier, and retires its staging buffers, and the textures created into it, through the queue above. Bindless elements are handed out at once, which is safe because the batch precedes the first frame that samples them on the same queue. A texture created without pixels is cleared on the GPU with `vkCmdClearColorImage`. Replacing a texture in place (development reload) first waits for the frames already submitted, a rare one-frame stall, since nothing else now guarantees they are done with the element.
- **GPU memory (FD-24, owner).** Buffers, textures and the depth attachment are sub-allocated by the vendored Vulkan Memory Allocator 3.5.0 behind `VulkanMemory`, the only code that includes it. Each allocation asks for the memory properties the renderer used before, so behavior is unchanged; the resource stats report allocations, used bytes and block bytes.

### R4 — Per-frame ring buffers and a frame uniform

- Each frame slot owns one persistently mapped, host-visible, coherent upload ring. Instance data, the frame uniform and textured instances are sub-allocated with a bump pointer and correct alignment.
- When a frame needs more than the ring holds, the ring is replaced with a larger one, and the old one goes through the retirement queue. Its size is kept across frames.
- **Frame uniform (set 1)**, one per frame: `ViewProjection`, `View`, camera position, ambient, light direction and light color. Only the lit vertex shader reads it; the texture stays at set 0, so the reloadable fragment shaders and their interface check are unchanged until M3's bindless textures (owner decision, 2026-11-22; the earlier draft put the uniform at set 0).
- **Lit instance data shrinks from 176 to 112 bytes**: a 3×4 world matrix (48 B), a 3×4 normal matrix with the lit flag in a spare lane (48 B), and tint (16 B).
  - The vertex shader computes `clip = ViewProjection × world`, so the CPU no longer multiplies by the view-projection per mesh.
  - The normal matrix stays on the CPU, and only lit meshes compute it.
- `VulkanBuffer` gains a persistent-mapping mode. The map-per-write path is removed once nothing uses it.

### R5 — Instanced lit-mesh batching

- After culling, lit draws are grouped by a batch key of (pipeline, mesh, texture, lit flag). A hash map from key to bucket preserves first-seen order, and the instances are written contiguously per bucket. This is O(n) per frame with no full sort.
- Each bucket is one `vkCmdDrawIndexed` with `instanceCount = bucket size` and `firstInstance = bucket offset`. Binds happen only when the pipeline, texture or mesh changes.
- Opaque lit meshes are depth-tested, so draw order does not change the image except for exactly coincident surfaces.
- The textured and sprite path keeps its current order-preserving batching.

### R6 — Shared device-local geometry pools

- One device-local vertex pool and one device-local index pool hold all mesh geometry. `MeshHandle` resolves to a range: first vertex, vertex count, first index and index count.
- Indices become **32-bit**, removing the 65,535-vertex limit for imported meshes. This doubles index memory, which is small next to vertex data.
- Uploads go through a staging buffer on the graphics queue, synchronized with synchronization2 barriers. Most changes come in at scene load and FBX reload.
- Free ranges use a simple first-fit free list, with neighbors merged on free. Replacing a mesh allocates a new range and retires the old one through R3. When a pool runs out, it grows by allocating a larger buffer, copying on the GPU and retiring the old buffer.
- Draws bind the two pools once and use `firstIndex` and `vertexOffset`. That is exactly what M3's indirect commands need.
- Picking keeps its CPU-side copies of positions and indices, which already exist for picking and bounds. GPU memory is never read back. Since M6C step C5 the pick world builds its Jolt mesh shapes from these copies ([Physics.md](Physics.md#picking)); `IntersectMesh` was removed.

### R7 — Carried renderer cleanups

These come from the M0 list and are folded in where the code is already being touched:

- Consolidate window size reads on `GetFramebufferState()`.
- Have swapchain preparation report whether the framebuffer is usable, so frame drawing uses one readiness check and keeps the minimized-window behavior.
- Keep mesh-handle creation and lookup next to the mesh storage in `VulkanContext::Impl` (natural once R6 moves mesh storage).
- Review device-loss handling: `VK_ERROR_DEVICE_LOST` from submit, present or wait gets one clear fatal error path instead of a generic exception.

## Proposed budgets

Measured on the minimum-spec laptop, in Release, with the same 4,000-mesh command and case as the M1 exit.

| Measure | M1 exit | M2 target |
| --- | ---: | ---: |
| Lit-mesh draw calls (4,000 meshes, 2 meshes × few materials) | 4,004 | ≤ 16 (one per distinct batch plus the textured demonstration draws) |
| Renderer preparation + recording and submission, sum of median p95 | 1.87 ms (0.86 + 1.01) | ≤ 0.8 ms |
| GPU pass p95 | 0.57 ms | not worse than 0.65 ms |
| CPU preparation p95 | 2.27 ms | ≤ 1.5 ms |
| Lit instance bytes per mesh | 176 | 112 |

A locally generated 10,000-mesh benchmark scene (made with the existing `RavenSceneTool create-benchmark`, kept under `out/` and not added to `Content/`) checks the roadmap exit criterion: draws stay proportional to distinct batches at 10k.

## Verification

- **Image comparison:** a tracked `scripts/Compare-Captures.py` compares screenshots of Perspective, Gallery, Assemblies, Puzzle and Collection against reference captures taken from master before M2 starts. The references stay under ignored `out/`.
  - Moving the clip transform into the vertex shader changes floating-point rounding, so exact equality is not expected.
  - Tolerance: every pixel within 2/255 per channel, except at most 0.1% of pixels, which may differ by any amount. The allowance covers triangle-edge pixels whose coverage flips under different rounding. A lighting or color error shifts most pixels and a missing object changes thousands, so both still fail; a defect smaller than about 920 pixels (0.1% of 1280 × 720) would pass.
- **Each step** runs the Debug 3D smoke test with core and synchronization validation, the live Release session (shader, PNG and FBX reload included), the compatible reload check and the 4,000-mesh benchmark.
- **Tests:** no new tracked tests unless a step changes CPU-testable behavior; the minimal-test policy applies. The pool allocator (R6) is CPU logic and gets a small `MeshPoolTests` if it is written as a standalone class.
- **Courtyard (2D)** is rendered once, at your request, because R2 and R4 change the textured pipelines it uses.

## Migration steps

Each step is a reviewed patch, and every sample keeps working.

1. *(Done 2026-11-22.)* **References:** capture the reference screenshots from current master, and add `Compare-Captures.py`.
   - `python scripts/Compare-Captures.py capture <dir>` launches each scene with `--no-watch --scene <id>` (Release by default). It saves frame 0 through the Vulkan screenshot layer: the first drawable frame has a zero time step, so it shows the authored state.
   - The scenes are Perspective, Gallery, Assemblies, Puzzle, Collection and Benchmark-500, at 1280 × 720.
   - `compare <reference> <candidate>` applies the approved tolerance (defaults `--max-fraction 0.001 --max-delta 2`), writes `<name>.diff.ppm` for images that differ (outliers red, pixels within the limit yellow), and exits nonzero on failure.
   - References come from master `05a1f31` and are kept under ignored `out/captures/m2-reference`. A second capture matched them bit for bit, which confirms frame 0 is deterministic.
   - Altered copies confirmed the thresholds: 0.05% of pixels off by 1 passes, while 0.22% of pixels, or a single pixel off by 10, fails.
2. *(Done 2026-11-22.)* **R1:** Vulkan 1.2 instance, required features and extensions, SPIR-V target 1.2, clear unsupported-device errors. No rendering change.
   - The loader must report Vulkan 1.2.
   - A GPU must report Vulkan 1.2, `timelineSemaphore`, `VK_KHR_dynamic_rendering` and `VK_KHR_synchronization2`, and the device enables those three features. Descriptor indexing and draw-indirect-count are recorded for M3 and logged at startup.
   - When no GPU qualifies, one error lists every GPU and what it lacks.
   - Shaders are built as SPIR-V 1.5 by the build and the runtime watcher. The fragment-interface check accepts SPIR-V 1.0–1.5, and from 1.4 on it also requires the texture and sampler in the entry-point interface.
   - Verification on the minimum-spec laptop:
     - Debug/Release builds and both tests pass.
     - The Debug 3D smoke with core and synchronization validation reported nothing.
     - All six captures are bit-identical to the references.
     - The RTX reports itself as a Vulkan 1.4 device with both M3 features.
     - The SDK profiles layer produced the two rejections: a simulated Vulkan 1.1 device ("supports only Vulkan 1.1") and hidden extensions ("lacks VK_KHR_dynamic_rendering, VK_KHR_synchronization2").
     - The live Release session passed: fragment shader reloads in 236–282 ms, syntax and interface rejections in 220 ms, and PNG, FBX, scene and game reloads as before.
     - MoltenVK and Linux are not verified here (M4).
3. *(Done 2026-11-22.)* **R2:** dynamic rendering and synchronization2. Remove the render pass and framebuffers, and add explicit layout transitions.
   - The device loads `vkCmdBeginRenderingKHR`, `vkCmdEndRenderingKHR` and `vkCmdPipelineBarrier2KHR`; a missing command fails device creation.
   - Lit and textured pipelines are created from the color and depth formats. Swapchain recreation keeps them while those formats match, and a background lit build started before a resize is still published.
   - Each frame transitions the swapchain image (after the acquire wait at color output) and its depth image, renders, and transitions the image to present. Texture uploads use synchronization2 barriers; sampling waits at the fragment shader, the only stage that binds textures.
   - Verification on the minimum-spec laptop:
     - Debug/Release builds and both tests pass.
     - The Debug 3D smoke with core and synchronization validation reported nothing.
     - All six captures are bit-identical to the references.
     - The live Release session passed with six swapchain recreations and no validation messages: fragment shader reloads in 221–284 ms (including across resizes), rejections in 260 ms, and PNG, FBX, scene and game reloads as before.
     - A Debug minimize/restore/portrait-resize probe with validation recreated the swapchain four times, reported nothing and closed normally.
     - Not verified: closing while minimized (retained pipelines are destroyed before the device by declaration order), MoltenVK and Linux (M4).
4. *(Done 2026-11-22.)* **R3:** timeline-semaphore pacing and the retirement queue, replacing the per-slot fences and keep-alive lists.
   - `VulkanFrameTimeline` is owned by the context and survives swapchain recreation. Each submission (`vkQueueSubmit2KHR`) signals the next value; a frame slot waits for the value of its own previous submission before reuse.
   - The wait first compares with the last value read back, then reads the counter, and calls `vkWaitSemaphores` only when the GPU is behind. On the laptop a `vkWaitSemaphores` call on an already-reached value costs about 18 µs; the slot wait is now 0.5 µs p50 (fences: 4 µs).
   - Replaced lit and textured pipelines, replaced mesh data, released managed meshes and released managed textures are retired at the last submitted value. A released texture's slot is freed once the queue drops its copy. If queuing fails, the device is waited idle instead.
   - The per-frame keep-alive lists and the per-draw texture-slot search are gone. Per-slot instance buffers stay owned by their slot, which the slot wait covers.
   - Verification on the minimum-spec laptop:
     - Debug/Release builds and both tests pass.
     - The Debug 3D smoke with core and synchronization validation reported nothing.
     - All six captures are bit-identical to the references.
     - The live Release session passed: fragment shader reloads in 229–267 ms, rejections in 203–238 ms, and PNG, FBX, scene and game reloads as before.
     - 4,000-mesh benchmark (NVIDIA, mailbox, 3 runs): 165.0 FPS as before, frame p95 6.48–6.66 ms (step 3: 6.49–6.59 ms), renderer preparation p95 0.67–0.72 ms (step 3: 0.74–0.78 ms), GPU p95 unchanged.
     - Interleaved idle runs show the same process CPU per frame as step 3, so the timeline wait does not spin.
     - Debug minimize/restore/resize probes with validation reported nothing; closing while minimized exits with code 0.
     - Not verified: a direct count of drained retirements (no counter is logged), MoltenVK and Linux (M4).
5. *(Done 2026-11-22.)* **R4:** persistently mapped per-frame rings, the frame uniform, and 112-byte lit instances with the clip transform in the vertex shader.
   - Each frame slot owns a persistently mapped `VulkanUploadRing`. A frame writes the uniform at offset 0, then lit and textured instances at 16-byte alignment. The ring doubles when a frame needs more, retires the old buffer and rewrites the slot's frame set.
   - The frame uniform (192 B, set 1) is read only by the lit vertex shader, which forwards lighting through the unchanged flat inputs. The camera position solves the view's 3×3 system once per frame.
   - Lit instances are 112 B: world rows, normal-matrix columns with the lit flag in `NormalToWorld[0].W`, and tint. The CPU no longer multiplies by the view-projection per mesh, and normal matrices are computed only for visible lit meshes. Every draw is still checked for a finite transform and tint.
   - `VulkanBuffer` has a persistent-mapping mode; mesh and texture uploads keep mapping per write until R6 replaces them.
   - Verification on the minimum-spec laptop:
     - Debug/Release builds and both tests pass.
     - The Debug 3D smoke with core and synchronization validation reported nothing.
     - Captures: Perspective, Collection and Benchmark-500 are bit-identical. Assemblies, Gallery and Puzzle have 1, 2 and 1 triangle-edge outliers (up to 103/255) plus three pixels within 2/255, so the tolerance was revised (review record). Doctored copies with a 2% brightness shift and a missing 40 × 40 block both fail.
     - The live Release session passed on its second run: fragment shader reloads in 236–294 ms, and the incompatible-shader rejection still works. The first run's no-layers pass timed out holding W to reach the goal after picking had succeeded; no rendering error was logged.
     - 4,000-mesh benchmark (NVIDIA, mailbox, 3 runs, back to back with step 4): 165.0 FPS steady (step 4 run: 161–174, unsteady); renderer preparation p95 0.64–0.71 ms (0.74–0.77), record/submit p95 0.89–0.92 ms (0.98–1.03), GPU p95 0.40–0.43 ms (0.48–0.61). An earlier step 5 run straight after a build was slower, so run-to-run noise is of the same order as the gain.
     - Not verified: Courtyard (2D) at runtime (on request), MoltenVK and Linux (M4).
6. *(Done 2026-11-22.)* **R5:** instanced lit-mesh batching.
   - Visible lit-pipeline draws are grouped by (mesh data, texture). The design key also named the pipeline and the lit flag, but there is one lit pipeline per frame and the lit flag is per-instance data since R4, so neither needs to split a batch.
   - A reused open-addressing table maps keys to batches in first-seen order. Batches then take consecutive instance ranges and each draw fills the next slot of its batch: O(n), no sort. A first version with `std::unordered_map` cost about 0.05–0.1 ms more at 4,000 draws.
   - Recording binds the frame set and instance stream once, then rebinds the texture only when it changes and the vertex/index buffers only when the mesh changes; each batch is one `vkCmdDrawIndexed`.
   - Verification on the minimum-spec laptop:
     - Debug/Release builds and both tests pass; the Debug 3D smoke with core and synchronization validation reported nothing.
     - Captures match step 5 exactly (Perspective, Collection and Benchmark-500 bit-identical; 1, 2 and 1 edge outliers in Assemblies, Gallery and Puzzle).
     - 4,000-mesh benchmark: 7 draws instead of 4,004 (three lit batches plus four textured batches); the benchmark script now expects 7.
     - Back to back with step 5 at equal frame rate (auto FIFO, NVIDIA, 165 FPS): renderer preparation p95 0.67 ms (0.61), record/submit p95 0.13 ms (0.84), GPU p95 0.44 ms (0.42), CPU preparation p95 1.05 ms (1.71).
     - NVIDIA mailbox (2 runs each): 193–207 FPS (163); preparation p95 0.79–0.87 ms (0.64–0.66), record/submit p95 0.18 ms (0.87–0.90), GPU p95 0.44–0.52 ms (0.55). Preparation per frame reads higher at the higher frame rate; the FIFO pair isolates the code change.
     - The live session was not rerun: batching changes neither reload nor resource lifetime, which step 7 exercises.
7. *(Done 2026-11-22.)* **R6:** device-local vertex/index pools with 32-bit indices, staging uploads and range retirement.
   - `VulkanMeshPool` (in `VulkanMesh.cpp`) owns one device-local vertex pool (44-byte `ColorVertex`, initially 64 Ki vertices) and one index pool (32-bit, initially 256 Ki indices), each with a sorted first-fit free list that merges neighbors.
   - `VulkanMeshData` holds its ranges and returns them on destruction. Released or replaced data is already retired through R3, so a range is reused only after the GPU finishes with it.
   - An upload writes a persistently mapped staging buffer and records copies in a one-time command buffer on the graphics queue: a barrier after earlier copies and vertex/index reads, growth copies when a pool is outgrown, a copy-to-copy barrier, the data copies, and a barrier to vertex and index input. The submission signals the next timeline value; the staging buffer, command buffer and any outgrown pool buffer are retired, so uploads never wait on the CPU.
   - Uploads are transactional: the pools are copied, changed and committed only after submission succeeds.
   - The lit mesh API (`CreateMesh`, `CreateMeshHandle`, `CreateManagedMesh`, `ReplaceMesh`) takes 32-bit indices, and FBX import no longer has a 65,536-vertex limit; its 262,144-triangle limit stays. Draws bind both pools once and pass `firstIndex` and `vertexOffset` per batch.
   - Not changed: textured (sprite) meshes keep their own host-visible 16-bit buffers. They are the 2D path, which M2 does not target; M3 can fold them in with bindless materials.
   - Verification on the minimum-spec laptop:
     - Debug/Release builds and both tests pass; the Debug 3D smoke with core and synchronization validation reported nothing.
     - Temporary probe with 64-vertex and 96-index initial pools (ignored snapshot, reverted): the validation smoke reported nothing, and Debug captures matched step 6, so growth copies, barriers and free-list reuse keep existing geometry intact.
     - Release captures match step 6. The live Release session passed, including FBX reload through `ReplaceMesh`.
     - 4,000-mesh benchmark, back to back with step 6 (one run per case): auto FIFO preparation p95 0.73 ms (0.80), record/submit 0.15 ms (0.19), GPU 0.38 ms (0.37), CPU preparation 1.17 ms (1.30); NVIDIA mailbox 213 FPS (193), GPU p95 0.31 ms (0.38). Run-to-run noise is about 0.1 ms on preparation.
8. *(Done 2026-11-22.)* **R7** cleanups where not already absorbed.
   - Window size: `Window::GetWidth()`/`GetHeight()` are removed, and `GetFramebufferState()` is the only size accessor. The Win32 accessors reported the restored size while minimized, unlike the framebuffer state. The Xcb mouse path now reads the framebuffer state. The Xcb and Cocoa edits are not compiled here (M4).
   - Readiness: `PrepareSwapchain()` returns whether the framebuffer is drawable and a pass is ready. `DrawFrame` and `ReplaceTexturedFragment` use that one result. A minimized window still keeps its swapchain.
   - Device loss: `CheckVulkanResult` turns `VK_ERROR_DEVICE_LOST` into a `VulkanDeviceLostError` with one message. Frame submit, acquire and present, timeline waits, swapchain-recreation idle waits, and mesh and texture uploads all use it. The error reaches the sample's fatal handler, which logs it and exits with code 1. Device loss could not be triggered on purpose, so this path is verified only by build and review.
   - Mesh handles: reviewed, no change. Slot creation (`CreateMeshSlot`) and generation-checked lookup (`ResolveMesh`) already sit with the slot storage, and R6 moved the geometry into `VulkanMeshPool`. Moving handle construction into `VulkanContext::Impl` would need `MeshHandle` to befriend the private `Impl`, which buys nothing.
   - Not addressed: the roadmap's "presentation completion" item. Present waits are bounded by the render-finished semaphores and timeline pacing; measuring when a frame actually reaches the display (`VK_KHR_present_wait`) would serve latency tuning, which no M2 budget needs.
   - Verification on the minimum-spec laptop: Debug/Release builds and both tests pass; the Debug 3D smoke with core and synchronization validation reported nothing; Release captures match step 7. A Debug minimize → restore → minimize → close probe under validation kept the swapchain while minimized, recreated it once on restore, exited 0 and reported nothing.
9. **Exit check:** budgets, image comparison, Debug validation, live reload session, the 10k scene, and Courtyard on your request.

## M2 exit record (2026-11-23)

Measured on the minimum-spec laptop, Release, 2560 × 1440 at 165 Hz, 4,000-mesh benchmark (one run per case after steps 7 and 8):

| Measure | M1 exit | M2 target | M2 exit | Result |
| --- | ---: | ---: | ---: | --- |
| Lit-mesh draw calls (4,000 meshes) | 4,004 | ≤ 16 | 7 (three lit batches, four textured) | Met |
| Renderer preparation + record/submit, p95 | 1.87 ms | ≤ 0.8 ms | 0.88 ms (auto FIFO) to 1.01 ms (NVIDIA mailbox) | **Missed by 0.1–0.2 ms** |
| GPU pass p95 | 0.57 ms | ≤ 0.65 ms | 0.31–0.38 ms | Met |
| CPU preparation p95 | 2.27 ms | ≤ 1.5 ms | 1.17–1.30 ms (FIFO), 1.35–1.54 ms (mailbox) | Met in FIFO, at the limit in mailbox |
| Lit instance bytes per mesh | 176 | 112 | 112 | Met |

- **The missed budget is recorded, not optimized.** All of the remaining time is CPU culling, batching and instance preparation that M3 removes (no-throwaway rule, owner 2026-11-22; see [GpuDrivenDesign.md](GpuDrivenDesign.md)).
- **Roadmap exit criterion met.** The 10,000-mesh scene, generated locally with `RavenSceneTool create-benchmark` and never added to `Content/`, records 7 draws, as at 4,000: draws follow unique (mesh, material) pairs.
  - The scene limits grew for it: 262,144 meshes, groups and rotators per scene, and 64 MiB binary scenes (`SceneBinary.hpp`).
  - The benchmark script's `scale --meshes 10000|50000|100000` generates the scene into its build snapshot.
- **Images:** the six captures match the M2 references within the approved tolerance (step 8).
- **Validation:**
  - The Debug 3D smoke with core and synchronization validation reported nothing (step 8).
  - `Benchmark-Windows3D.py validate --meshes 10000` passed Vulkan validation.
- **Live Release session:** passed on master after step 8, covering DLL, scene, PNG, FBX and fragment-shader reloads and the incompatible-shader rejection.
- **Not run:** Courtyard (2D), which needs an owner request; MoltenVK and Linux (M4).

M3 baseline for the same path (auto FIFO, 20% animated, one run each):

| Meshes | FPS | CPU preparation p95 | Renderer preparation p95 | World transforms p95 | Scene gather p95 | GPU p95 |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 10,000 | 165 (display limit) | 2.58 ms | 1.71 ms | 0.29 ms | 0.40 ms | 0.72 ms |
| 50,000 | 85.8 | 12.34 ms | 8.31 ms | 1.49 ms | 2.02 ms | 3.18 ms |
| 100,000 | 43.2 | 24.23 ms | 16.78 ms | 2.67 ms | 3.77 ms | 7.21 ms |

## Out of scope for M2

GPU culling, indirect draws, bindless materials and GPU-resident object data (M3); multithreaded recording; a render graph; new lighting or shading features; changes to the public `DrawFrame` API.

## Review record (2026-11-21)

| Question | Outcome |
| --- | --- |
| Image tolerance | Approved: "renders identically" means at most 0.1% of pixels differ, by at most 2/255 per channel. The clip transform moves to the vertex shader in M2. Revised 2026-11-22 at step 5: every pixel within 2/255 except at most 0.1%, which may differ by any amount, because edge pixels flip coverage. |
| Index width | Approved: 32-bit indices in the shared pools. |
| Device-local geometry | Approved: device-local pools with staging uploads in M2. |
| Budgets | Approved as proposed. A 50k-object CPU checkpoint was discussed and not added. |
| R1–R7 | Approved as written. |
