# RavenEngine Architecture Assessment (2026-11-23)

Status: proposal for owner review. Nothing in this document is decided until the owner approves it, item by item, in the usual way. Reviewed revision: `f52c40b` on `master`. Engine sources were not changed while it was written.

## 1. Purpose and method

This document assesses the RavenEngine codebase and architecture against the v1 goal in `ROADMAP.md`. It lists what the implementation team should fix, in which order, and why. It is written so that any single finding can be picked up and acted on by itself.

**The v1 goal used as the yardstick:**

1. A shared cross-platform runtime on Windows, Linux and macOS.
2. A data-oriented ECS.
3. Hot reload of game code, shaders and assets. Compatible state is kept and last-good recovery applies.
4. GPU-driven rendering of 50k+ dynamic objects at interactive rates on the minimum-spec laptop (i7-12700H, RTX 3070 Ti Laptop).

ECS and hot reload are a joint goal: the host owns the component bytes, and the game DLL can be replaced. Future editor and play sessions must remain possible.

**Method:**

- **Panel.** Nine panelists reviewed the code, each through one lens: ECS and data model; hot reload and the module boundary; Vulkan renderer and the GPU-driven path; assets, scenes and content; cross-platform; build, CI and tooling; core runtime and public API; performance and scalability; roadmap, docs and process. A tenth reviewer, the lead and chief architect, ran the discussion and wrote this synthesis.
- **Two discussion rounds.**
  - Round 1: independent papers with file:line evidence.
  - Round 2: answers to cross-questions, endorsements, challenges, self-revisions and new findings.
  - The lead re-read the code to settle disputes. Re-checked during synthesis: `GameModule.cpp:360-430`, `RenderWorld.cpp:186-288` and the `RenderWorld` instance in `scripts/Windows3DSmoke.cpp:164`.
- **Round 3: adversarial verification.**
  - Fifteen independent verifiers, one per finding rated critical or high after round 2, re-read the code and tried to refute each finding: its mechanism, its file:line evidence, its severity and its fix steps.
  - A separate citation checker spot-checked 105 file:line citations in the remaining findings and sections.
  - All 15 findings were confirmed in substance, each with corrections: wrong or incomplete line ranges, overstated claims, missed call sites and missed partial state. Several fix steps were refined where the verifiers showed they would not work as written, would add throwaway code, or would re-list existing M4 items.
  - The corrections are applied in the findings below. Each verified finding carries a **Verification** line, and section 9.3 summarises the results.
- **Read-only review** of commit HEAD on `master` (`f52c40b`). Nothing was built, run or edited.
  - Some panelists read local, ignored build outputs under `out/`: `build.ninja`, `CMakeCache.txt`, benchmark `result.json` files, leftover `.raven-game-*` directories. Those are noted where used.
- **Verification state.** Everything recorded as working is verified on Windows only. Linux and macOS code has not been compiled since early October (see ARCH-PLAT-02).
- **What happened to panel findings:**
  - Findings refuted in discussion were dropped.
  - Duplicates were merged under one stable `ARCH-<AREA>-NN` ID, and each finding lists the panel IDs it came from.
  - Severities reflect the round-2 debate and then the round-3 verification. Where verification changed a severity, the finding's Verification line gives the reason. After verification no finding is rated critical.

**Severity scale:**

| Severity | Meaning |
|---|---|
| Critical | Blocks a v1 goal, or causes a crash or data loss in normal use |
| High | Will force rework, or materially risks a goal |
| Medium | Real but contained |
| Low | Polish |

**Kinds:** defect, architecture-gap, doc-drift, process.

**Timing labels:**

| Label | Meaning |
|---|---|
| now | Before or independent of any M6 work |
| before M6 | Lands before the first M6 implementation step |
| M6-n | A numbered step of the M6 sequence in section 6 |
| M4 | The parallel platform track |
| M7 | Quality and release |

## 2. Executive summary

The engine's core is in good shape and is honestly documented. The Registry sparse-set core, the compile-time component contract, the all-or-nothing module swap, the transactional game build, the single timeline retirement and the resident GPU scene are each well built.

The risk is in three places:

1. **Between subsystems.** The engine has no host-owned frame pipeline. Sample code owns PostUpdate, Extract, scene instantiation and reload ordering.
2. **In the data model M6 will build on.** There is no typed field table, no persistent identity, and sample gameplay is baked into the public API.
3. **In error handling.** Game-written data can throw from host code and end the session (a logged exit from `main`), which loses the retained world that hot reload exists to protect. No current content or game code triggers these paths, but nothing guards them either.

Cross-platform has made the least progress. The work to fix it is small and well understood.

After the round-3 verification no finding is critical. Five findings are high: four M6 architecture gaps (ARCH-CORE-01, ARCH-CORE-02, ARCH-ECS-03 and ARCH-ASSET-01) and one error-boundary defect (ARCH-RT-01). The other error-boundary defects are medium, because their triggers are latent today.

### Verdict per goal

| Goal | Status (one line) | Rating |
|---|---|---|
| 1. Cross-platform runtime | Public API is OS-clean, but Linux/macOS executables do not link (one missing symbol, already tracked for M4), the platform folders have had no contributor commit since October and later owner edits were not compiled, and the hot-reload seams and game-module CMake are Windows/MSVC-shaped in shared code. Every gate fails loudly. No ECS gameplay sample can run off Windows. | At risk |
| 2. Data-oriented ECS | Registry, views, change ticks and the lifecycle journal are solid and meet the M1 budgets. Structural changes cost O(N), a documented simplification that no runtime path triggers yet, and the metadata and identity that M6 serialization needs are missing. | On track |
| 3. Hot reload with last-good recovery | M5 works on Windows. Several host paths still end the session on game-written data errors and lose the retained world; the triggers are latent (no current content or game code reaches them). H6 migration has no type-safe compatibility key yet. | At risk |
| 4. GPU-driven 50k+ at interactive rates | The steady-state path is sound and measured. The pacing-independent numbers (CPU preparation p95 3.53 ms, GPU p95 2.15 ms) imply the stock host also meets the 60 FPS target, but the recorded FPS figures come from a source-patched host. 100% dynamic and spawn/despawn are unmeasured, and one spawn would restamp and re-upload every object. | On track (FPS evidence must be re-run on the stock host) |
| Editor and play-session readiness | Entity handles carry a registry ID and scenes are scoped, but there is no engine session/world object, no persistent entity identity, and SystemContext hands game code the raw Registry. | At risk |

### Top fixes in priority order

1. **Close the host exception boundary (now, a handful of S snippets).** ARCH-RT-01 (steps 1-3) to ARCH-RT-04 now; the ARCH-RT-05 boundary and the transactional `Sync` reset (ARCH-RT-01 step 4) land with ARCH-CORE-01 at M6-1.
   - A zero or near-zero scale on a lit mesh, an invalid texture ID, or a missing parent written by game code must never end the process. No current content triggers these, so they are latent, but the fixes are small and none is replaced later.
   - The registration catch must run while the DLL is still loaded.
2. **Land production presentation options and re-run the M3 exit cases on the stock host (now).** ARCH-PERF-01. The recorded goal-4 FPS figures cannot be reproduced from HEAD without patching sources. The pacing-independent CPU and GPU numbers already show the goal is met; what is missing is a stock-host measurement and a harness that does not depend on exact source text.
3. **Restore the build safety net before M6 touches `Component.hpp` (now).**
   - Warning level on all targets (ARCH-BLD-01).
   - Debug info in the game DLL (ARCH-BLD-02).
   - Reload-recipe checks wired into CI (ARCH-BLD-03).
4. **Write the M6 step-0 design note with explicit owner decisions D-A to D-I before any M6 code, as was done for M1-M5.** ARCH-DOC-01. It settles the hierarchy representation, field-table origin, persistent identity, bindings replacement, per-world Registry, the scene-format conversion and material identity. The new asks are an explicit exit checklist and scheduling the two orphaned preconditions (the D5 command buffer and "the host stops compiling game components").
5. **Build the engine-owned frame pipeline (M6-1).** ARCH-CORE-01: split `OnFrame` (no temporary shim), move PostUpdate/Extract into Application inside the recoverable boundary, absorb the replacement-time Extract in `PrepareFrame`, and make the GPU-scene generation counter process-wide first (ARCH-RND-03).
6. **Make the cross-platform seams real on the Windows side, and, with an owner yes, add an informational Linux compile job now rather than in M4.** ARCH-PLAT-01 and ARCH-PLAT-02. The other platform items stay with the M4 contributors as already listed.
7. **Measure, then fix, the structural-change cost on the chosen hierarchy representation before D5 Commands.** ARCH-ECS-01.
8. **Generate the typed field table from `RAVEN_COMPONENT` and register components through the module table.** ARCH-ECS-02. Then follow with persistent identity, the generic scene format and relocation of the sample components (ARCH-ECS-03, ARCH-ASSET-01, ARCH-CORE-02).

## 3. Strengths to preserve

The team should not regress these. Several later recommendations deliberately build on them.

- **Registry storage and views.**
  - Paged sparse sets with 4,096-entry pages and memcpy relocation, justified by the trivially-copyable rule (`Registry.cpp:51-96`).
  - Insert reserves before it pushes, and erase swaps the last element into the gap (`Registry.cpp:204-236`).
  - Views are driven by the smallest pool and use an RAII iteration guard (`Registry.hpp:288-304`).
  - Change ticks respect const access (`Registry.hpp:334-339`).
- **The compile-time component contract.**
  - `RAVEN_COMPONENT` checks every listed field against `sizeof`/`alignof` (`Component.hpp:170-172`), and fields must satisfy `ComponentValue` (`Component.hpp:103-107`).
  - It is the right base for a typed field table that needs no change to user declarations.
- **Retained state never points into a module.**
  - Pools copy name, size and layout hash and compare by value (`Registry.cpp:150-158, 242-249, 414-433`).
  - System names are copied while the module is loaded (`GameModule.cpp:297-303`).
  - Function pointers are cleared before `FreeLibrary` (`GameModule.cpp:205-208`).
  - Entity handles carry a registry ID and a generation (`Entity.hpp:36-38`), so stale and foreign handles are rejected.
- **The all-or-nothing generation swap.** Contract check, system validation and a source-revision recheck all finish before `Active.swap` (`GameModule.cpp:384-401`). A throwing system is suspended until the next generation (`GameModule.cpp:343-351`).
- **A transactional game build.** `BuildGame.cmake.in` takes a process lock, uses content-hashed stamps, and commits `BuildInputs.sha256` by rename only after the outputs verify (`BuildGame.cmake.in:35, 51-60, 177-186`). Builds run in a kill-on-close job object (`Win32GameModule.cpp:122-128`).
- **One reload service.** `ReloadService` has a clean Import/Accept/Discard/Changed protocol and safe subscription teardown (`ReloadService.cpp:543-575`). Records are keyed by input hash plus importer version, ready for a later derived-data cache.
- **One shader recipe** for build and live watch: DXC with `-WX`, then `spirv-val` (`CompileShader.cmake:20-30`, `ShaderLibrary.cpp:154-158`). Reloaded shaders are checked against C++ layouts.
- **One GPU retirement mechanism.** `VulkanFrameTimeline` (`VulkanFrameTimeline.hpp:12-55`; `Retire` at :31, `Collect` at :33) retires everything. Render-finished semaphores are per swapchain image, and the upload is written before acquire (`VulkanClearPass.cpp:460-461`).
- **A lean, correct GPU-driven core.**
  - A resident 128-byte object buffer is updated by dirty scatter.
  - Fixed-count indirect draws.
  - A released mesh culls to `IndexCount` 0 instead of crashing (`VulkanGpuScene.cpp:301-312`, `Cull.comp.hlsl:28-30`).
- **Transactional content publication.**
  - `SceneSession` prepares, re-checks currency, then commits (`SceneSession.cpp:87-112`).
  - Reload swaps resource contents into stable handles and bindless elements (`AssetStore.cpp:64-67`, `VulkanContext.cpp:782-805`).
  - Persistent UUID identity is kept separate from generation-checked runtime handles.
- **An OS-clean public API.**
  - Only `Api.hpp` branches on `_WIN32`.
  - Keys are USB HID usages, so they are safe to persist.
  - Cursor and framebuffer values are reported in drawable pixels on all backends.
- **Process discipline.**
  - Design notes carry numbered owner decisions.
  - Exit records are measured on the minimum-spec laptop.
  - Unverified work is recorded honestly ("not compiled on this machine").
  - Third-party code is vendored at pinned commits with SHA-256 records.
  - Tests enroll automatically, and `noTestsAction=error` is set.

## 4. Findings by area

Each finding states its panel sources so that the round-1 papers can be traced. File paths are relative to the repository root. `Private/...` and `Public/...` are under `RavenEngine/`.

---

### 4.1 Runtime robustness and error boundaries (RT)

The shared root cause: host stages run on data written by game code, and they throw instead of degrading. `Application::Run` calls `client->OnFrame` with no try (`Application.cpp:143`). The only handler is in `main`, which catches `std::exception`, logs and returns 1 (`Private/Samples/RavenEngine.cpp:170-174`). Each of RT-01 to RT-03 therefore ends the session with a logged, orderly exit and loses the retained Registry; nothing on disk is corrupted, and the scene files reload on restart. RT-04 is different: it can crash through a call into an unmapped image. The throws are raised in host code after the game systems return, so per-system suspension never sees them.

#### ARCH-RT-01: Zero or tiny scale on a lit mesh throws out of `RenderWorld::Sync` and ends the process

- **Severity:** high
- **Kind:** defect
- **Sources:** PERF-1, CORE-2 (part), endorsed by ECS, RENDER, ASSETS, DOCS. Lead re-verified.
- **Evidence:**
  - `Public/Raven/Math/Transform.hpp:171-173`: `NormalMatrix` throws on determinant 0. Lines 179-180 also throw when `1/det` overflows. For a uniform scale s, det = s^3: between about 1.1e-15 and 1.4e-13 per axis the determinant is a nonzero denormal and the overflow check throws; below about 1.1e-15 it rounds to exactly 0 and lines 172-173 throw. Products of nested small scales reach the same range.
  - `Private/Renderer/RenderWorld.cpp:20-27`: `IsDrawable` checks only finite and affine values, so a singular world matrix is not hidden.
  - `RenderWorld.cpp:205-210`: `Write` calls `NormalMatrix(world)` for every drawable object whose material has `Lit == true` (`:209`). The default `MaterialComponent` has `Lit = false` (`Public/Raven/Components/MaterialComponent.hpp:13`), so the "lit mesh" qualifier is load-bearing; the sample scenes do mark their materials lit (`Private/Tools/SampleScenes.cpp:45-46, 97, 161`).
  - `RenderWorld.cpp:193-201` and `:213`: `GroupCounts` and `Group` are mutated before the throw, and `MarkDirty` comes after it. When `Write` is reached through `Add` (`RenderWorld.cpp:124-131`), `Objects`, `Entities` and `SlotOfIndex` already hold the new slot. Partial state is left behind.
  - `RenderWorld.cpp:272, 276, 287`: lifecycle batches are consumed and `state.Since` is advanced before `Rebuild` (`:281`) or `ApplyTicks`, so an abort loses both Added/Removed entries and tick changes.
  - `Private/Scene/Scene.cpp:338-339`: collapsed axes are an expected input of the world-transform pass; the world matrix is kept with a zero column.
  - Call path: `PerspectiveSample::OnFrame` (`:142`) calls `Update`, which calls `UpdateDraws` (`:293`), which calls `m_RenderWorld.Sync` (`:307`). `Application.cpp:143` calls `OnFrame` with no try; the only handler is `RavenEngine.cpp:170-174`.
  - `Private/Scene/SceneDocument.cpp:61-62, 114`: authored data is guarded. The trigger is runtime writes only.
  - `docs/MeshMaterials.md:50` documents the throw as an earlier design choice ("Singular or unsupported normal transforms reject lit submissions"). This contradicts `docs/TextureResources.md:97`, which lists zero scale as supported on the textured bounds path.
- **Why it matters:** Goal 3. Tweening `Scale` to 0 to hide or pop an object is ordinary gameplay, and nothing stops game code from writing it. No code in the repository writes a zero or tiny scale at runtime today: the only gameplay "hide", in the game module's `Private/Game/InteractionGame.cpp:38-40`, marks a pickup `Collected` and dims its `Tint`; it does not touch the scale. The trigger is plausible future gameplay, not current behavior. The cost is an orderly process exit that loses the retained session; nothing is corrupted. The exception is raised in host code after the system returned, so system suspension does not catch it.
- **Fix:**
  1. Add a non-throwing normal-matrix helper and keep the throwing `NormalMatrix` for `SceneDocument` validation. The owner chooses its home deliberately, because `Public/` is the client ABI surface:
     - a public `bool TryNormalMatrix(const Mat4&, Mat4& out) noexcept` in `Transform.hpp` (reusable, but a public API addition and a contract-hash change), or
     - a `noexcept` helper in `RenderWorld.cpp`'s anonymous namespace (only `RenderWorld` needs it; the public math API stays unchanged).
  2. In `RenderWorld::Write`, compute the group and the normal matrix before mutating `GroupCounts`. When the helper fails, set `GpuObjectHidden` (already used for non-drawable objects). A collapsed object rasterizes nothing, so this is visually exact; for a tiny nonzero scale it is visually equivalent.
  3. Update `docs/MeshMaterials.md:50` in the same commit, so the documentation describes hide-on-collapse instead of "reject lit submissions".
  4. Separately, and sequenced with ARCH-RT-05 rather than as part of this fix: in `RenderWorld::Sync`, assign `state.Since` only after `Rebuild` or `ApplyTicks` returns, and on any exception reset `state.Source` so the next `Sync` rebuilds (through `Clear()`, which unsubscribes and resubscribes cleanly), then rethrow to the boundary. After steps 1-2 a zero scale no longer throws at all; step 4 guards other throws such as the RT-02 resolver, and helps only once the boundary exists. Assigning `Since` late is safe because `Sync` makes no Registry writes.
- **Effort:** S
- **Timing:** steps 1-3 now; step 4 at M6-1 with ARCH-RT-05
- **Panel discussion:** the panel debated critical versus high, because authored zero scale is rejected. Round 2 resolved it as **critical**, on the premise that the remaining trigger is a normal gameplay write already in use for pickups. Verification refuted that premise (pickups are dimmed, not scaled) and showed the throw is documented earlier design, so the evidence-based rating is high. It stays first in the fix order because the fix is small and the consequence is loss of the whole session. It stays above ARCH-RT-02 and ARCH-RT-03 (both medium) because its trigger is a correct gameplay write, while theirs need a game-side data mistake or API misuse.
- **Verification:** confirmed with corrections; severity critical -> high because no code writes a zero or tiny scale at runtime, the throw was documented design, and the cost is an orderly exit without corruption, the same class of host throw as ARCH-RT-02.

#### ARCH-RT-02: An invalid, wrong-kind or never-leased missing texture `AssetRef` written by game code throws out of `Sync` and the 2D sprite gather

- **Severity:** medium
- **Kind:** defect
- **Source:** ASSET-9 (new in round 2)
- **Evidence:**
  - `Private/Samples/PerspectiveSample.cpp:307-312`: the resolver calls `level.UseAsset(...).GetTexture()` inside `Sync` with no try.
  - `Scene::UseAsset` (`Private/Scene/Scene.cpp:374-384`) calls `AssetStore::Acquire`, which first calls the catalog.
  - `Private/Assets/AssetCatalog.cpp:69-73`: throws `invalid_argument` on a nil ID and `out_of_range` on an unregistered ID.
  - `Private/Assets/AssetStore.cpp:162-165`: throws on a wrong kind or a missing file, for IDs the scene has not leased yet. For an ID the scene has already leased (for example a mesh ID used by a mesh in that scene), the wrong-kind throw is `Scene.cpp:379-380` ("Incorrect scene asset type").
  - `Private/Renderer/RenderWorld.cpp:170-172` (`FindMaterial`): a null texture already maps to white element 0, so a safe fallback exists.
  - `Private/Samples/OrthographicSample.cpp:196`: the same `UseAsset` call runs per sprite.
  - The termination path ends at the top-level catch in `Private/Samples/RavenEngine.cpp:170-174`, which logs and returns 1. No catch exists in Core/Application, Samples or Game on this path.
  - Reachability: `SceneAccess::Write<T>` is public (`SceneAccess.hpp:39-42`), `MaterialComponent` and `SpriteComponent` hold `AssetRef<TextureAsset>`, and `AssetRef`'s `Id` is a raw public `AssetId` (`AssetRef.hpp:21`), so the type does not stop a mesh ID being stored there. No game code writes either field today (no matches in `Private/Game`).
  - Authored data cannot trigger this: `PerspectiveLevel.cpp:38-45` checks each material texture's kind and file, and `:52-54` leases the textures before any entity is published; `OrthographicLevel.cpp:272-276` leases every sprite texture at load. Deleting the PNG of an already leased texture does not crash: the lease stays cached in `Scene::m_Assets`, and `AssetStore::ApplyReloads` catches publish failures (`AssetStore.cpp:208-232`).
  - The throw happens inside `Add`/`Write` after `Objects`, `Entities` and `SlotOfIndex` are updated and `GroupCounts` incremented (`RenderWorld.cpp:124-131, 193-203`). Even a caller that caught it would leave RenderWorld half-written, so the resolver itself has to stop throwing.
- **Why it matters:** Goal 3. A game system that writes an unregistered ID, puts a mesh ID in `BaseColorTexture`, or names a texture the scene never leased whose file is missing ends the session. The throw comes after the game system has returned, so per-system suspension does not help. The defect is latent: no current content or code triggers it. M6 "runtime resource handles for game code" (`ROADMAP.md:149`) makes game-written `AssetRef`s routine, so it is worth fixing before M6.
- **Fix:**
  1. Add one shared, non-throwing engine lookup, for example `Scene::TryUseAsset` or a validation step in `AssetStore`, that returns `nullptr` on data errors: nil or unregistered ID, wrong kind, missing file, and arguably decode failure. Renderer and resource failures from `Load` (GPU upload, out-of-memory, device lost) still throw. It does not wrap `UseAsset` in a broad catch. Log once per (AssetId, reason).
  2. The perspective resolver, the current sprite gather and the future M6 sprite path all call that lookup; the object samples white and the sprite is skipped. No sample-local try/catch goes into `OrthographicSample`'s CPU sprite gather, because sprites move to the GPU path in M6 and a sample-local guard there would be throwaway.
  3. State that the placeholder persists until the material changes again: `FindMaterial` runs only on add or a material change (`RenderWorld.cpp:202-203`), so adding the missing file or registering the ID later does not show the texture without another write. That is acceptable for a placeholder and is not last-good recovery; tie it to the catalog or asset reload notification if recovery is wanted.
  4. Make "data error gives a placeholder; only programmer errors throw" the written policy of the engine Extract stage (ARCH-CORE-01). This is documentation and must not block steps 1-2.
- **Effort:** S
- **Timing:** steps 1-3 now; step 4 with ARCH-CORE-01
- **Verification:** confirmed with corrections; severity high -> medium because authored content cannot trigger it, a deleted PNG of a leased texture is already handled, and only game code writing a bad ID at runtime can reach it, which no current code does.

#### ARCH-RT-03: `UpdateWorldTransforms` throws on a missing ancestor after advancing its tick state, and game code can reach that state through the raw Registry

- **Severity:** medium
- **Kind:** defect
- **Sources:** CORE-2, ECS-5 (folded in)
- **Evidence:**
  - `Public/Raven/Core/GameSystems.hpp:28`: `SystemContext` exposes `GameInstance& Game`. `Public/Raven/Core/GameInstance.hpp:17` hands out a mutable `Registry&` (`:18` is the const overload). `GameModule.cpp:317` builds that context for every system, and in the Perspective sample the Scene uses the same registry (`PerspectiveSample.cpp:95`).
  - `Private/Game/RavenGame.cpp:24, 146`: the sample game already runs an unscoped `registry.View<...>()` through it. That use is not on the risky path: `MoveSprites` only writes `TransformComponent` values, and it runs only in the Orthographic sample, which never calls `UpdateWorldTransforms` (the only caller is `PerspectiveSample.cpp:304`).
  - `Private/ECS/Registry.cpp:363-387`: `Registry::Destroy` erases every component with no owner or scene check, and does not update Scene parents, names, bindings or order. `SceneAccess` has no `Remove` or `Destroy`, so only the raw Registry exposure can cause this.
  - `Scene.cpp:241-243`: externally destroyed entities stay in the order on purpose.
  - `Scene.cpp:290-292`: `m_WorldFull` is cleared and `m_WorldTick` advanced before the loop.
  - `Scene.cpp:329-331`: throws "Scene transform ancestor has no TransformComponent" when a child is processed (own tick changed, no world yet, or a full rebuild).
  - `Scene.cpp:317-324`: when a parent's world is dropped through the `local == NoIndex` branch, `m_WorldChanged[parent]` is never set. Static children whose own tick did not change keep a frozen, stale world silently and never throw, so moving children end the session while static children go stale.
  - `Scene.cpp:207-223`: the same missing-ancestor throw (`:217`) exists in `ComposeTransformUnchecked`, which backs the live `GetWorldTransform` used by picking, the camera and `SceneAccess`. `UpdateWorldTransforms` also calls it for non-finite or collapsed values (`Scene.cpp:336-337`).
  - `UpdateDraws` (`PerspectiveSample.cpp:304`) and `Application.cpp` have no try, so the exception unwinds to `RavenEngine.cpp:170`, which logs and returns 1.
- **Why it matters:** Goal 3, and play sessions. A game mistake such as destroying a parent or removing its Transform through `context.Game` ends the session with a logged exit, although system exceptions are otherwise suspended per system (`GameModule.cpp:328-332`). Even if a caller caught the throw, entities after the failing index whose ticks are at or below the new `m_WorldTick` would be skipped next time. The hazard is latent: no game code calls `Destroy` or `Remove` today.
- **Fix:**
  1. Keep the detection, change the reaction: treat a missing ancestor world as a detached root, in **both** the cached pass and `ComposeTransformUnchecked`, so the cached world and the live `GetWorldTransform` agree; the non-finite fallback at `Scene.cpp:336-337` then no longer throws from inside the loop. When a slot's world is removed in the `local == NoIndex` branch, set `m_WorldChanged` for it so all descendants re-root consistently. Log once per entity; this needs a small host-owned set of reported entities, cleared on reload or a new generation. If anything still throws (only allocation remains), set `m_WorldFull = true` so the next pass recomputes everything; no transactional rollback.
  2. Port `MoveSprites` (`RavenGame.cpp:146`) to the scene-scoped `SceneAccess` view. It is the only use of `context.Game`. This is sound under the no-throwaway rule but does not lower this risk, because `MoveSprites` cannot remove an ancestor; steps 1 and 3 carry the fix.
  3. Remove `GameInstance& Game` from `SystemContext` in the single contract break (ARCH-CORE-02). This is the real mitigation.
  4. Route D5 Commands through Scene (ARCH-ECS-01).
- **Effort:** S (steps 1-2, including the reported-entity set)
- **Timing:** steps 1-2 now; steps 3-4 at M6-7
- **Panel discussion:** ECS proposed dropping the descendant's `WorldTransform` instead. The lead chose "detached root" because it needs no structural change inside the transform pass, and the object stays visible so the developer sees the problem. Either choice is logged. Verification added that the same semantics must apply to the live compose path, and that the changed flag must propagate so static children do not go silently stale.
- **Verification:** confirmed with corrections; severity high -> medium because nothing triggers it today, the result is an orderly logged exit rather than a crash, and the trigger needs game code to misuse an API that step 3 removes (ARCH-RT-04, a real use-after-unmap, is also medium).

#### ARCH-RT-04: The candidate game DLL is unloaded before the registration catch handler runs, and non-std exceptions escape `Activate`

- **Severity:** medium
- **Kind:** defect
- **Source:** HR-1. Lead re-verified.
- **Evidence:**
  - `Private/Core/GameModule.cpp:366`: `auto candidate = std::make_unique<LoadedGame>();` is declared inside the `try` that opens at `:360`.
  - `GameModule.cpp:390`: `registerSystems(table)` is the only call into game code that can throw.
  - `GameModule.cpp:205-208`: `~LoadedGame` calls `FreeLibrary`, which runs during unwinding before the handler.
  - `GameModule.cpp:426-430`: only `catch (const std::exception&)` calls `error.what()`, a virtual call into the now unmapped image. There is no `catch(...)`.
  - `docs/EngineModules.md:477`: promises recovery on this path.
- **Why it matters:** Goal 3. A failed registration should leave the active generation intact. Instead it can crash the host.
- **Fix:**
  1. Wrap the module call and rethrow a host-owned exception while the DLL is still loaded, for both `std::exception` and `...`.
  2. Add `catch (...) { Report("Game module activation failed"); return false; }`.
  3. Optionally declare `candidate` before the `try`.
  4. Verify once with an ignored probe DLL that throws. No tracked test.
- **Effort:** S
- **Timing:** now (bundle with RT-01 to RT-03)
- **Panel discussion:** proposed as high. RELOAD re-rated it to medium: the trigger needs game code to throw from registration, and today's registration cannot (`RavenGame.cpp:182-190`). LoadSystems and LogSystemChanges never touch module memory after unload. The lead accepts **medium** but keeps it in "now", because the fix is one snippet and ARCH-ECS-02 widens this exact path.

#### ARCH-RT-05: There is no recoverable host-stage boundary or recovery policy

- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** CORE-2 step 2, PERF-1 step 4, CORE round-2 recovery definition
- **Evidence:**
  - `Application.cpp:139-143`: `game->Update` (139-140) is followed by `gameplayTime.End()` and `client->OnFrame` (143), with no stage between them and no try.
  - The actual host PostUpdate and Extract run inside client code: `PerspectiveSample::OnFrame` (`PerspectiveSample.cpp:142`) calls `Update`, which calls `UpdateDraws` (`:293`). `UpdateDraws` runs `UpdateWorldTransforms` (`:304`) and `RenderWorld::Sync` (`:307-312`).
  - The same `UpdateDraws` also runs from `PrepareFrame` when a scene is replaced (`PerspectiveSample.cpp:126-131`, called from `Application.cpp:108`).
  - Process termination is `main`'s catch in `Private/Samples/RavenEngine.cpp:170-174` (log, return 1). It catches `std::exception` only.
  - `RenderWorld.cpp:269-287`: `Sync` is not transactional. Batches are consumed at 270-274 and `Since` advanced at 276, before `Rebuild` (281) or `ApplyTicks` (287).
  - `GameModule.cpp:343-351` (try/catch at 324-335, including `catch(...)`): system suspension until the next activation is the existing precedent.
  - The recovery pieces already exist: `RenderWorld::Clear()` (`RenderWorld.cpp:235-244`) is noexcept, resets `Source` and bumps `Generation`; `VulkanGpuScene.cpp:289` forces a full upload on a generation change; `DrawFrame(..., RenderWorld* world, ...)` accepts nullptr (`VulkanContext.cpp:876-887`, `VulkanGpuScene.cpp:283-288`); the Registry view counter is released by an RAII guard (`Registry.hpp:297-304`), so unwinding out of a view leaves no stuck iteration state. `Scene::InvalidateWorld` is new; the fields it sets (`m_WorldOrderDirty`, `m_WorldFull`) exist.
- **Why it matters:** Goal 3 and future editor play sessions. After RT-01 to RT-03, data errors no longer reach the boundary. What remains are game-reachable data errors nobody has found yet, plus genuine engine bugs. An engine invariant bug or `bad_alloc` ending the process with a logged error is ordinary engine behavior, not a v1 goal violation, so the boundary's value is defense in depth. Game system throws are already contained, and reloaded inputs already recover last-good.
- **Fix:**
  - The tiered policy, landing with ARCH-CORE-01 S2 in the host pipeline (landing it in sample code before CORE-01 would be throwaway):
    - **Tier 0 (prevention):** RT-01 to RT-03.
    - **Recoverable categories:** data and game-caused errors only. `std::bad_alloc`, `catch(...)` of non-std types, and `std::logic_error` from engine invariants (for example "World transforms cannot be updated while a view iterates", `Scene.cpp:286-287`) are **fatal**: log and terminate, as today. Retrying a full rebuild after `bad_alloc` allocates more than an incremental sync and would walk into Tier 2 with the heap in an unknown state, and continuing after an invariant violation would hide engine bugs.
    - **Tier 1 (boundary):** when PostUpdate or Extract throws a recoverable error, log the stage and message (deduplicated). Invalidate derived state with a new `Scene::InvalidateWorld()` (sets order-dirty and full) and `RenderWorld::Clear()`. `Clear` bumps the generation, so the GPU scene does a full upload. Draw this frame without the lit world, then rebuild next frame. `Scene::InvalidateWorld` is private or host-only, so game code cannot force full world recomputes every frame.
    - **Tier 2 (no silent retry):** if the rebuild fails again, suspend the stage. Draw with `world = nullptr`, so lit meshes visibly disappear, and set a metrics flag. Resume on the next generation event: module activation, scene replacement or asset reload.
    - **Coverage:** the boundary wraps both the per-frame PostUpdate/Extract and the scene-replacement path in `PrepareFrame` (ARCH-CORE-01 S1/S2 absorbs the latter), or a throw during a scene swap still ends the process.
  - The Registry is never rolled back.
- **Effort:** S, but only as an increment on ARCH-CORE-01 S2; today there is no host-owned stage to put the try in
- **Timing:** M6-1 (with ARCH-CORE-01)
- **Panel discussion:** PERF proposed drawing the last uploaded GPU scene while suspended. The lead chose CORE's "draw without the lit world". A failed `Sync` may have mutated RenderWorld state that the next `DrawFrame` would upload, and a visible failure cannot be mistaken for success. Round 2 listed allocation failures as recoverable; verification showed they should be fatal, and that position is adopted.
- **Verification:** confirmed with corrections; severity high -> medium because once RT-01 to RT-03 land, only engine bugs and unknown data errors reach the boundary, and ending the process on those is ordinary engine behavior rather than a v1 goal violation.

---

### 4.2 Host architecture and public API (CORE)

#### ARCH-CORE-01: The engine has no host frame pipeline; sample clients own PostUpdate, Extract and world ownership

- **Severity:** high
- **Kind:** architecture-gap
- **Sources:** CORE-1, ECS-4, CORE-11, PERF question
- **Evidence:**
  - `Private/Core/Application.cpp:65-69`: Application owns only the `ApplicationPathsScope` (`:65`), Window, `VulkanContext`, `GameInstance` and `GameModule`.
  - `Application.cpp:104-143`: PrepareFrame, systems, then `OnFrame`. There is no PostUpdate or Extract stage.
  - `Private/Samples/PerspectiveSample.hpp:49-53`: the sample owns `SampleAssets` (whose `SceneProject` exposes the `AssetStore`, `SceneProject.hpp:43`), `AudioPlayer`, `SceneSession` and `RenderWorld`.
  - `PerspectiveSample.cpp:139-142, 293, 296-313`: OnFrame applies asset reloads and runs sample updates (ECS writes at `:248, :264, :291`; `:264` writes `StaticMeshComponent` through `PerspectiveLevel.cpp:198-199`). Only then does it run the "Host PostUpdate" (`UpdateWorldTransforms`, `RenderWorld::Sync`) and `DrawFrame` (`:149`).
  - `PerspectiveSample.cpp:126-131`: `PrepareFrame` also runs `UpdateDraws` (`:130`), and so PostUpdate and Extract, after a scene replacement, before the game systems run. This is a second Extract call site.
  - `Public/Raven/Core/ApplicationClient.hpp:40-46`: the only virtuals are `GetGameBindings`, `UsesFixedGameplayStep`, `PrepareFrame`, `OnGameplayStep` and `OnFrame`. The only `OnFrame` implementers are `PerspectiveSample.cpp:135` and `OrthographicSample.cpp:81`.
  - `OrthographicSample.cpp:173, 196`: the 2D client never calls `UpdateWorldTransforms`, walks all registry entities and builds matrices from the local transform.
  - `scripts/Windows3DSmoke.cpp:161-176`: a third hand-built copy of the pipeline. It drives the renderer directly and does not implement `ApplicationClient`.
  - `docs/ECSDesign.md:148-153`: plans host PostUpdate and Extract phases.
- **Why it matters:**
  - The M6 exit ("using only engine APIs") cannot be met while these are sample code.
  - D5 Commands have no flush point.
  - The RT-05 boundary has no home.
  - Editor and play sessions need an engine object they can instantiate twice.
  - Moving PostUpdate naively either lags sample writes by a frame or runs Extract after `DrawFrame`, so `OnFrame` must be split.
  - Nothing is broken today: game systems run before the sample's PostUpdate, so current ordering is correct. The gap blocks the M6 exit and the editor/play-session direction.
- **Fix** (each step is a move, not a rewrite):
  - **S0.** Land ARCH-RND-03 (process-wide RenderWorld generation) first.
  - **S1.** Add an engine type owning one `SceneSession` and one `RenderWorld`, with `PostUpdate()` (world transforms) and `Extract(resolver)` (`RenderWorld::Sync`). Move the body of `PerspectiveSample::UpdateDraws` into it. The sample stays its owner. No behavior change.
  - **S2.** Replace `OnFrame` with `OnUpdate(input, time)` and `OnRender(renderer, input, time)` on `ApplicationClient`, and move both overriding clients (`PerspectiveSample`, `OrthographicSample`) in the same small step. No temporary `OnFrame` shim: it would be throwaway, and a default `OnFrame` that calls both hooks could not put the engine PostUpdate/Extract between them anyway. The loop becomes Poll/F5, PrepareFrame, FixedUpdate/Update systems, OnUpdate, session PostUpdate+Extract inside the RT-05 boundary, then OnRender.
  - **S1/S2, replacement-time Extract:** the `UpdateDraws` call in `PrepareFrame` (`PerspectiveSample.cpp:130`) is removed. Scene replacement only rebinds the session, and the frame's single engine Extract, which always precedes `OnRender`, syncs the new scene. Extract then runs exactly once per frame, and ARCH-RND-02's "re-sync before any draw after a replacement" invariant holds by construction.
  - **S3.** Application owns the session plus project and `AssetStore` when the config names a project. Clients borrow it. Rebase `Windows3DSmoke` on it; the smoke does not implement `ApplicationClient`, so it belongs only to this step.
  - **S4.** The 2D client adopts PostUpdate. Sprites join Extract in M6.
  - **S5.** The generic SceneDocument-to-ECS instantiator comes only after the generic scene format (ARCH-ASSET-01). Writing it against today's puzzle sections would be throwaway.
  - **Design rule for the session note:** one process-scoped `GameModule` drives several `GameInstance`s. Per-world run state (`Suspended` and stats, `GameModule.cpp:147-155, 232`) moves to a per-instance schedule state when play sessions arrive.
- **Effort:** M overall (each step S)
- **Timing:** S0-S2 at M6-1; S3-S4 during M6; S5 after M6-6
- **Panel discussion:** DOCS challenged CORE's single "L, step 0" bundle. Resolved by splitting it as above. Verification removed the temporary `OnFrame` shim from S2 and added the `PrepareFrame` Extract site.
- **Verification:** confirmed with corrections; severity high -> high because the gap blocks the M6 "engine APIs only" exit and the editor/play-session direction, and the RT-05 boundary has nowhere to live without it, although nothing is broken today.

#### ARCH-CORE-02: Sample gameplay concepts are part of the public API, the game ABI and the contract hash

- **Severity:** high
- **Kind:** architecture-gap
- **Sources:** CORE-3, ECS-6, HR-2 (relocation part), PROC-1, ASSET-2 (relocation part)
- **Evidence:**
  - `Public/Raven/Core/ApplicationClient.hpp:17-30`: `GameBindings{Camera, Player, Mesh, World, Interactions}` and `GameUpdateResult{PlayerDisplacement}`.
  - `Public/Raven/Core/GameSystems.hpp:26-33`: `SystemContext` embeds both, so they are in every game DLL's ABI.
  - `Public/Raven/Scene/Scene.hpp:52,56,83` and `Scene.cpp:395-403` (`SetBindings`): Scene stores and validates these bindings, and `Scene.cpp:203-205` clears them when entities are destroyed.
  - `Public/Raven/Components/InteractionComponents.hpp:14-65`: puzzle components (Switch, Rotator, Door, Goal, Pickup, Attempt) in `Raven::ECS`.
  - `Public/Raven/Scene/SceneDocument.hpp:42-88`: the public `SwitchDefinition`, `RotatorDefinition`, `DoorDefinition`, `GoalDefinition` structs and their vectors are sample concepts in the public API too.
  - `Private/Samples/PerspectiveLevel.cpp:5, 150-162`: the host compiles them, adds `RotatorComponent` and `AttemptComponent`, and derives the Mesh binding from `RotatorComponent`.
  - `OrthographicLevel.cpp:5`: includes `Game/GameComponents.hpp` (which already lives in `Private/Game`). `OrthographicLevel::Step` (`OrthographicLevel.cpp:347-378`) with `OverlapsPlayer` (`:339-345`): host code does XZ gameplay collision and win/loss.
  - `Application.cpp:132-140`: the host loop passes `client->GetGameBindings()` and routes the result into `OnGameplayStep`; `OrthographicSample.cpp:74` consumes `result.PlayerDisplacement`.
  - `Public/Raven/Scene/SceneProject.hpp:17`: `SceneType {Perspective, Orthographic}`.
  - `cmake/GameContract.cmake:5-12`: every `.hpp` under `Public` and `Private/Game` is hashed, plus all cmake files, the CMakeLists files and `CMakePresets.json` (`:6-12`), so any edit forces a host restart. Moving components into `Private/Game` does not by itself take their edits out of the contract hash.
  - `docs/HotReloadDesign.md:224`: after M6, "the host stops compiling game components". `ROADMAP.md:151` names this as the precondition of field migration, and `docs/ECSDesign.md:173` (D9) states the direction ("over time become components or singletons"). Neither schedules the relocation itself: the gap is real but already acknowledged.
- **Why it matters:** the M6 exit ("using only engine APIs") and H6 migration (`ROADMAP.md:151` cannot start until this is done). Every new binding edits a public header and forces a restart. Relocation alone does not end the restarts, because `Private/Game` headers are also hashed (`GameContract.cmake:8`); that needs H6 migration plus narrowing the contract (ARCH-HR-04 step 3, M6-10). This is a planning and architecture gap, not a runtime bug.
- **Fix:** one planned contract break.
  1. **Now:** freeze `GameBindings` and `GameUpdateResult`. No new fields.
  2. **M6-7:** `SystemContext` becomes `{ SceneAccess World; const InputState& Input; const FrameTime& Time; std::span<const PickHit> Picks; }`.
     - The camera is the Scene's selected camera. Only the **selection** (which entity is the camera) is read-only through `SceneAccess`; the camera's Transform and `CameraComponent` stay writable, because in free-fly the DLL is their sole writer (`RavenGame.cpp:64-115`, `docs/EngineModules.md:467`).
     - Player and Mesh become game-defined tag components authored in the scene, found with `View`/`Single`.
     - **Gating replacement:** today `GetGameBindings` returns empty bindings unless free-fly is on (`PerspectiveSample.cpp:104-112`) or the level is Playing (`OrthographicSample.cpp:48-53`). That is how the host stops game systems driving the camera in orbit mode or the player after a win or loss. An engine-defined replacement is required, such as a session or mode component, or the play state on Scene; the M6 note (D-B) picks one.
     - Interactions become the generic per-frame `Picks` span. `PickHit` is nested in Scene today (`Scene.hpp:53`); move it to a lighter header so `SystemContext` does not pull `Scene.hpp` into the system ABI.
     - `PlayerDisplacement` disappears: Courtyard's step moves into a FixedUpdate game system. That turns `OrthographicLevel`'s colliders, bounds and solids into game-defined components and changes the 2D text codec; verifying it needs a 2D runtime run, which requires an explicit owner request (ARCH-RND-06).
     - Removing `GameInstance& Game` breaks `MoveSprites` (`RavenGame.cpp:146`, through `context.Game.GetRegistry()`), so it is ported to `SceneAccess` views in the same break (ARCH-RT-03 step 2 may already have done this).
     - Host reactions such as sounds go through engine-defined request components.
     - Rule: **the host reads only engine-defined components.**
  3. Move `InteractionComponents.hpp`, the `SceneDocument` puzzle sections and their `Validate` rules into the game. `GameComponents.hpp` is already in `Private/Game`; the work there is removing the host include (`OrthographicLevel.cpp:5`) and the host gameplay that runs on it.
  4. Replace `SceneType` with a project-level codec or game field.
- **Effort:** M
- **Timing:** freeze now; break at M6-7, after the field table (M6-2) and generic format (M6-6)
- **Panel discussion:** HR-2 originally proposed generic named entity slots plus an opaque result component. HR-2 withdrew it in round 2. Rejected because it adds a second persistent identity system that serialization, play-session cloning and the editor would all have to persist and remap, and authored tags would later replace it (no-throwaway). PROC-1 originally ordered "relocate first". Corrected: the host can only stop compiling these components once it can instantiate them generically, so relocation follows the field table and the generic format. Verification kept the camera writable and added the gating replacement.
- **Verification:** confirmed with corrections; severity high -> high because the M6 exit and the `ROADMAP.md:151` field-migration item cannot proceed until this is done, although it is a planning gap rather than a runtime bug.

#### ARCH-CORE-03: `Scene` cannot exist without a window, a Vulkan device and a renderer-bound `AssetStore`

- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** ECS-7, CORE-4, BLD-5, PROC-5 item 4
- **Evidence:**
  - `Public/Raven/Scene/Scene.hpp:24`: `Scene(ECS::Registry&, AssetStore&)`. `Scene.hpp:54`: `Pick` takes `const VulkanContext&`.
  - `Public/Raven/Assets/AssetStore.hpp:74`: `AssetStore(VulkanContext&, ...)`.
  - `Tests/Support/TestWorld.hpp:11-27`: tests that use Scene are GPU-dependent.
  - `Tests/Gpu/HierarchyTests.cpp:1-3`: GPU-labelled for that reason only, so CI (`-LE gpu`) never runs it.
- **Why it matters:** the M6 serialization round-trip tests (ROADMAP.md:150) and headless tools would be GPU-only.
- **Fix:**
  1. In the M6 serialization note, define a narrow asset-lease provider interface that Scene calls at `Scene.cpp:383`. Alternatively, make `AssetStore` constructible with a null uploader.
  2. Make `Pick` a free function taking Scene and renderer (`ScenePicking.cpp` exists).
  3. Move the non-rendering half of `HierarchyTests` to CPU.
- **Effort:** M
- **Timing:** M6-5 (before the generic format, so its round trip runs in CI)

#### ARCH-CORE-04: Engine and sample hotkeys are hard-coded and cannot be consumed; there is no binding-context or platform-service pattern for M6 input

- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** CORE-12, PLAT-7
- **Evidence:**
  - `Application.cpp:100-101`: F5 reloads unconditionally.
  - `PerspectiveSample.cpp:122, 254-277` and `OrthographicSample.cpp:62,107,119,127`: F1-F7 and other keys are read directly from `InputState`.
  - `Private/Core/FixedGameplayStep.hpp:17-28`: every key, including developer hotkeys, reaches gameplay input.
  - `GameSystems.hpp:29`: only raw `InputState`.
  - `Public/Raven/Platform/Window.hpp:34-58`: no cursor mode, content scale or text input.
  - `docs/InputProcessing.md:55, 62-66`: key names and text input are undecided.
- **Why it matters:** M6 key bindings and key listening, an owner high-priority item, and editor sessions. A game binding on F5 or F7 would trigger a reload or a view change.
- **Fix:** in the M6 key-binding design note.
  1. Binding contexts in priority order (engine-dev, editor, game) that can consume keys. Engine-dev is disabled in player builds.
  2. An action-state view in `SystemContext` next to raw keys.
  3. Query-style platform services (key display names) as private free functions with a shared fallback, mirroring `CreateAudioBackend`/`UnsupportedAudio`, so every OS keeps linking.
  4. Batch window capabilities (cursor mode, content scale, text input) into one public `Window` change when the first caller lands, with documented no-ops on unported backends.
  5. Until then, add no new hard-coded hotkeys in host code.
- **Effort:** M
- **Timing:** M6 key-binding design (can run in parallel)

#### ARCH-CORE-05: Dead unmanaged resource API is still public and inside the contract hash

- **Severity:** low
- **Kind:** defect
- **Sources:** CORE-7, ASSET-7. Callers verified repo-wide, including `scripts/`, `Tests/` and `Tools/`.
- **Evidence:**
  - `Public/Raven/Assets/AssetResources.hpp:16-48`: no callers outside its own `.cpp`.
  - `AssetResources.cpp:68, 123, 151, 190` are the only callers of public `CreateTexture`, `CreateMeshHandle`, `ReplaceMesh` and `ReplaceTexture`.
  - `VulkanContext.hpp:55-57, 102, 121-126`: `CreateMesh`, `ReplaceTexturedFragment` and the two camera-only `DrawFrame` overloads have no callers. All callers use the builder overload: `PerspectiveSample.cpp:149`, `OrthographicSample.cpp:88`, `Windows3DSmoke.cpp:172`.
  - `Private/Assets/PngFileWatch.hpp:11-21`: survives only for `AssetResources`.
  - `Private/Audio/AudioPlayer.cpp:56`: `Play(AssetRef)` is unused and bypasses `ResolveAssetPath`.
  - `VulkanContext.cpp:573-577`: `ReplaceTexture` relies on an implicit queue drain (ARCH-RND-04).
- **Why it matters:** no-throwaway and API surface. M6 must not offer game code two asset entry points. Keep `Impl::CreateTexture`, which makes the white texture (`VulkanContext.cpp:228`).
- **Fix:**
  1. Delete `AssetResources` and the `PngFileWatch` class. Keep `DecodePngSource`.
  2. Delete `CreateMesh`, `CreateMeshHandle`, `ReplaceMesh`, public `CreateTexture`, `ReplaceTexture` and `ReplaceTexturedFragment`.
  3. Delete the two camera `DrawFrame` overloads and the `Impl::DrawFrame` template.
  4. Delete `AudioPlayer::Play(AssetRef)`.
  5. Update the docs in the same commits (DOC Phase B1).
  6. `CreateTexturedMesh` stays until sprites move to the render world.
- **Effort:** S (2-3 snippets)
- **Timing:** now

#### ARCH-CORE-06: Logging, assertion and error-signalling policy is underdeveloped

- **Severity:** low
- **Kind:** process
- **Source:** CORE-8
- **Evidence:**
  - `Public/Raven/Core/Assert.hpp:7-18`: `RAVEN_ASSERT` has zero uses.
  - `Private/Core/Log.cpp:13-32`: `std::cerr` is the only sink.
  - There is no `[[nodiscard]]` under `Public/`.
  - `PerspectiveLevel.cpp:107, 118, 132, 139, 149, 155, 159`: ignored bool results from `Add`.
- **Fix:**
  1. Add `[[nodiscard]]` on bool-returning Registry and SceneAccess mutators, and fix the call sites.
  2. One sink registration hook.
  3. A one-paragraph error policy: exceptions for authoring and IO, bool or optional on ECS hot paths, asserts for invariants. This must agree with RT-05 Tier 0.
- **Effort:** S
- **Timing:** step 1 now; sinks and policy in M7

#### ARCH-CORE-07: Layering cycles (Core and Scene headers; ECS befriends Scene and renderer)

- **Severity:** low
- **Kind:** architecture-gap
- **Source:** CORE-9
- **Evidence:**
  - `ApplicationClient.hpp:5` includes `SceneAccess.hpp`, while `Scene.hpp:4` includes `ApplicationClient.hpp`.
  - `Registry.hpp:21-23, 239-242`: friends `Scene`, `SceneAccess` and `RenderWorldSync`.
  - `Window.hpp:9` includes `<vulkan/vulkan.h>`.
- **Fix:** with ARCH-CORE-02 and ARCH-ECS-02, not as separate churn.
  1. Remove `GameBindings` from `ApplicationClient.hpp`.
  2. Replace the Registry friends with one private `Detail` accessor.
  3. Move `WindowDesc` to its own header and forward-declare the Vulkan handles.
- **Effort:** S
- **Timing:** during M6

#### ARCH-CORE-08: The fixed-step clock drops time silently and exposes no interpolation factor

- **Severity:** low
- **Kind:** architecture-gap
- **Source:** CORE-13
- **Evidence:**
  - `Private/Core/FixedGameplayStep.hpp:13-14, 29`: 1/60 s steps, and the accumulator is clamped to 4 steps with no log.
  - `FixedGameplayStep.hpp:32-34`: no remainder or alpha is exposed.
- **Fix:**
  1. Count dropped seconds in `FrameMetrics` and log once.
  2. Expose the step remainder.
  3. No interpolation until a game needs it.
- **Effort:** S
- **Timing:** during M6

#### ARCH-CORE-09: There is no writable user-data location

- **Severity:** low
- **Kind:** architecture-gap
- **Source:** CORE-14
- **Evidence:**
  - `Private/Core/Paths.cpp:70-98`: only executable and content paths.
  - `docs/PlayerBuild.md:9`: the player needs a writable install directory.
- **Fix:** add `Paths::GetUserDataPath(relative)` behind a Platform function (LocalAppData, `XDG_DATA_HOME`, Application Support) with a shared fallback. Use it for M6 saves, the bindings file and the log sink.
- **Effort:** S
- **Timing:** M6 (decide in the serialization note)

---

### 4.3 ECS and data model (ECS)

#### ARCH-ECS-01: Any structural change sends the whole world through an O(N) rebuild; destroy is O(k*N)

- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** ECS-1, PERF-3, ASSET-4 (merged)
- **Evidence:**
  - `Private/Scene/Scene.cpp:114, 176, 200`: `CreateEntity`, `SetParent` and `DestroyEntity` set `m_WorldOrderDirty`.
  - `Scene.cpp:239-282`: `RebuildWorldOrder` builds a depth `unordered_map` over every entity, stable-sorts with hash lookups inside the comparator, builds a second `unordered_map` of positions, and sets `m_WorldFull = true` (`:281`).
  - `Scene.cpp:290, 323, 349-350`: with `full` set, the skip at `:323` never fires, so every world is recomposed; existing worlds are restamped (`:349-350`) and new ones added (`:344`).
  - `RenderWorld.cpp:109-111`: `ApplyTicks` then calls `Write` for every mesh with a newer world tick, and `Write` marks every object dirty, so all records are re-uploaded.
  - `Scene.cpp:185-189, 199-201`: the cascade scans all of `m_Parents` per removed entity (O(k*N)), and the `contains` lambda is a linear find, so the `erase_if` calls on `m_Entities` and `m_Names` are also O(N*k).
  - `Public/Raven/Scene/Scene.hpp:78`: the hierarchy is `unordered_map<Entity, Entity> m_Parents` with no child links (`:76` is `m_Entities`, `:79` is `m_Names`).
  - `docs/ECSDesign.md:133-142` (D5; "destroy through Scene" at `:142`): Commands are deferred by owner decision (`ROADMAP.md:42, 75`). `docs/ECSDesign.md:187` states the full reprocess after a hierarchy change as a deliberate design choice, so the cost is documented but has never been budgeted.
  - No current runtime path triggers it: `SceneAccess` exposes no `Create`, `Destroy` or `SetParent`, so only level load and tests restructure the world.
- **Why it matters:** goal 4 and the M6 game, once gameplay spawns per frame.
  - An **extrapolated** cost, not a measurement: with about 100 ns per Write (consistent with `docs/GpuDrivenDesign.md:243`, render-world sync 1.1-1.3 ms for about 10k changed objects) and about 57 ns per compose (inferred from 0.57 ms in the 20%-changed case), one spawn in the 50k stress layout (about 100.5k entities: row, pivot, mesh) costs about 100k composes (5.7 ms) plus 50k Writes (5.2 ms): **about 11 ms or more per structural frame**. This may be low, because the order rebuild's hash maps and the sort with lookups in its comparator at about 100k entities are not counted.
  - The upload is about 6.4 MB (50k records of 128 B, plus slot indices). It goes through the dirty-scatter path (`VulkanGpuScene.cpp:317-326`), not the engine's "full upload" path, which `VulkanGpuScene.cpp:289-290` picks only for a new world, a generation change or growth.
  - Reparenting (attach or detach) has the same cost.
  - Against the 4 ms CPU budget on the minimum-spec laptop this would break goal 4 once M6 gameplay spawns. The measured goal-4 targets (50k at 20% animated, 100k static) are met today.
- **Fix:** decide the representation first (owner decision D-A), measure, then implement once.
  - **Hierarchy truth:** a single host-owned `ParentComponent { Entity Parent; }`. It becomes `EntityRef` at M6-4; at runtime `EntityRef` stores the resolved Entity, so the change is mechanical.
  - **Write policy (part of D-A):** a host-owned component that game modules can mutate through mutable views would silently desynchronize the Scene runtime index. Two options:
    - (a) **Host-only writes** (recommended). `ParentComponent` is written only by `Scene::SetParent`; the serializer and a future editor instantiate through `SetParent`; `SceneAccess` offers it only through const access, and the raw Registry leaves `SystemContext` at M6-7 (ARCH-CORE-02). Strength: no per-frame reconciliation cost, and cycles are validated at the single write site. Until M6-7 the rule is documented for game code.
    - (b) **Any writer, reconciled by tick.** Scene re-derives its index from `ParentComponent` ticks every frame. Strength: generic editor and field-table writes need no special path. Cost: a dense scan of the Parent ticks every frame, plus cycle validation when a changed tick is applied.
  - **Derived runtime index**, Scene-owned and never serialized: child lists, depth, and the parent-first order as **depth buckets** (one dense vector per depth plus each entity's bucket index).
  - **Steps:**
    0. First add a structural-change case to EcsBenchmark (spawn and despawn 1k per frame at 100k, and reparent a 1k subtree) and run it on the minimum-spec laptop, to confirm the cost and justify the priority. It is a benchmark case, not CTest.
    1. Add `ParentComponent` under the chosen write policy.
    2. Add child lists so `DestroyEntity` walks only the subtree. `m_Entities` and `m_Names` removal becomes swap-erase.
    3. Spawn appends to the bucket for its depth without setting `m_WorldFull`.
    4. Reparent moves one subtree between buckets and marks only that subtree for recompute.
    5. Validate cycles at every write site: in `SetParent` (`Scene.cpp:168-174`) and at load under option (a); additionally when a changed `ParentComponent` tick is applied under option (b).
    6. Keep the full rebuild for scene load only.
    7. Re-run the step-0 cases and record the result.
- **Effort:** M
- **Timing:** M6-3, before D5 Commands (step 0 can run now)
- **Panel discussion:** three options were on the table:
  - Optimize Scene side tables (ECS-1 round 1). Rejected: if M6 later moves the hierarchy to components, that optimization is thrown away.
  - Make Parent, FirstChild, NextSibling and PrevSibling all components (PERF round 2). Rejected: sibling links are derived data. Each reparent would write and stamp 3-5 entities, links must be repaired on every swap-erase and clone, and redundant data would be serialized.
  - The ECS hybrid (Parent as component, everything else a Scene runtime index). **Chosen.** It survives M6 serialization, clone and remap, because the field table carries Parent. ASSETS confirmed that the on-disk format (a parent persistent ID per entity) does not depend on this choice. PERF's depth-bucket order cache is adopted inside the hybrid.
  - Verification showed that round 2's steps 1 ("written only by `SetParent`") and 5 ("validate cycles on tick application") contradicted each other; the write policy is now an explicit part of D-A.
- **Verification:** confirmed with corrections; severity high -> medium because the cost is a documented, deliberately deferred simplification that no runtime path triggers today, and the 11 ms figure is an extrapolation; it still gates D5 Commands at M6-3.

#### ARCH-ECS-02: There is no runtime field table, and `LayoutHash` cannot detect field type changes

- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** ECS-2, PROC-4, HR-2 (registration part), ASSET-1 (field-table part)
- **Evidence:**
  - `Public/Raven/ECS/Component.hpp:54-61`: `ComponentInfo` is only Id, Name, Size, Alignment and LayoutHash.
  - `Component.hpp:127-131, 174-179`: `HashLayout` folds only `sizeof`/`alignof` per field; `Info` adds `sizeof(T)`, the field-name text and the type name. `float` to `int32_t`, or `Vec3` to `float[3]` (Vec3 is three floats, `Transform.hpp:11-16`), keeps the same hash.
  - `Component.hpp:189`: field names exist only as one stringified text (`#__VA_ARGS__`).
  - `Private/ECS/Registry.cpp:414-433`: `AddComponent` captures pool metadata lazily on first Add (`:419-430`). A mismatch returns false with no log (`:431-433`). `FindPool` (`Registry.cpp:242-249`) then returns nullptr on a hash mismatch, hiding the type silently.
  - `Registry.cpp:427-429`: on a layout mismatch the pool is created without its journal, so an existing subscriber silently gets nothing. `Registry.cpp:477-479, 490-492` refuse the subscription by returning 0, and `ReadLifecycle(0)` then returns an empty batch forever (`Registry.cpp:543-545`), so the effect is still silent.
  - `Public/Raven/Core/GameSystems.hpp:51-59`: `SystemTable` is the only module registration surface (`:61-62` are the entry-point alias and name).
  - `docs/HotReloadDesign.md:224`: an explicitly post-M6 design statement ("After M6: ..."), promising "A type change rejects the module". H6 (`:219`) already schedules the field table for M6, so this finding refines a planned item; the new information is that the current `LayoutHash` carries no type information and so cannot deliver that promise.
  - `docs/ECSDesign.md:76, 93`: `:76` claims a field table that does not exist. `:93` says the hash covers field sizes and offsets; offsets are not hashed explicitly but follow from the hashed sizes and alignments under the natural-layout `static_assert` (`Component.hpp:170-172`). The real doc gap is type information and the field table.
  - Today's exposure: `GameContract.cmake` hashes every `.hpp` under `Public/` and `Private/Game`, so any component-header change is rejected at `GameModule.cpp:385-386` before `LayoutHash` is consulted. The current silent-reinterpret hole is a component declared in a game `.cpp`, which the contract deliberately excludes. None exists: `RAVEN_COMPONENT` appears in `.cpp` files only in `Tests/RegistryTests.cpp` and `EcsBenchmark.cpp`.
- **Why it matters:**
  - Goal 3 and M6 serialization, migration and editor inspection.
  - Today, a same-size type change in a component declared in a game `.cpp` would silently reinterpret retained bytes; once the coarse contract stops covering game headers (H6), every component is exposed.
  - Without a module-side registration path, the field table would be built twice.
- **Fix:** one source, copied by value.
  1. Have `RAVEN_COMPONENT` emit a `constexpr` array of `{name, offset, size, TypeTag}`. Default values come from **per-field default bytes, or are captured at runtime from a static `T{}`**, not from a constexpr `std::bit_cast` of the whole object: Clang rejects that in constant evaluation when `T` has padding (allowed today, for example a bool next to a float), and it would need every field type (`AssetId`, `AssetRef`, `Entity`, `MeshHandle`) to be constexpr-constructible, which is not verified. `TypeTag` comes from a trait over `ComponentValue`. That set is not closed (any arithmetic or enum type, and `AssetRef<T>` for any `T`), so the tag encodes an enum's underlying type and the `AssetRef` target; otherwise a change of enum type or `AssetRef<T>` target keeps the same hash. No change to user declarations.
  2. Fold the type tags into `LayoutHash`.
  3. Add `std::span<const FieldInfo> Fields` and default bytes to `ComponentInfo`. The Registry copies both into `ComponentPool` at pool creation; that copy becomes the "old layout" for migration.
  4. Grow `SystemTable` into a module table with `table.Component<T>()` next to `Add(SystemDesc)`. Use one export, not a second `RavenRegisterComponents`. `Activate` validates the descriptors against live pools before the swap (`GameModule.cpp:389-401`).
  5. Add a `RAVEN_VALUE(...)` macro for opted-in value structs.
  6. Log at Error, once per (Id, LayoutHash), in `FindPool`'s mismatch branch (Id found, hash differs; `Registry.cpp:248`). That branch is not the hot path for correct code, and it is the one site that also catches modules that only read or view a mismatched type. Also log at the Add and subscription sites (`Registry.cpp:431-433, 477-479, 490-492`).
  7. Add a tracked guard: a `static_assert` that a same-size type change alters `LayoutHash`, next to the hash definition or in `RegistryTests.cpp`. It is acceptable under the minimal-tests rule because M6-2 rewrites `LayoutHash`.
- **Effort:** M
- **Timing:** logging (step 6) now; steps 1-5 and 7 at M6-2
- **Constraint:** on Linux this is safe only with the game-module visibility flags (ARCH-PLAT-03). Descriptors must be copied by value and never keyed by the address of `Info`.
- **Verification:** confirmed with corrections; severity high -> medium because the contract hash already rejects every component-header change today, the only current hole (components in game `.cpp` files) has no instance, and the field table is already scheduled by H6, so this is an M6-2 design input plus doc drift and missing logs.

#### ARCH-ECS-03: Component data has no persistent entity references or mesh asset identity

- **Severity:** high
- **Kind:** architecture-gap
- **Sources:** ECS-3, ASSET-1 (identity part); amended by ASSETS round 2
- **Evidence:**
  - `Public/Raven/Components/StaticMeshComponent.hpp:11-12`: `MeshHandle Mesh; // Runtime identity only`. The AssetId is lost from component data after instantiation (`PerspectiveLevel.cpp:52, 70-71`); only the sample's `m_Definition` keeps each mesh's `Asset` id, which `InitializeGameplay` reads (`PerspectiveLevel.cpp:91-99`).
  - `SpriteComponent.hpp:20` and `MaterialComponent.hpp:12` use `AssetRef<TextureAsset>`, so the models are inconsistent.
  - `Public/Raven/Components/InteractionComponents.hpp:12, 35`: `SceneReference = std::array<char,129>`, four of them in `DoorComponent`.
  - `Private/Game/InteractionGame.cpp:109, 120, 138` with `SceneAccess.cpp:11`: each reference per frame constructs a `std::string` (a heap allocation only for names longer than the SSO capacity, 15 characters on MSVC) and does an O(log n) `std::map<std::string>` lookup with string compares (`Scene.hpp:79`).
  - `Component.hpp:40-43`: `Entity` (`:40`), `AssetId` (`:41`), `AssetRef` (`:42`) and `MeshHandle` (`:43`) are approved field types; `Entity` and `MeshHandle` have no persistence semantics.
  - `Scene.cpp:148-155`: `GetAuthoredId` is a linear reverse scan. Its only caller runs on a user pick (`PerspectiveSample.cpp:203`), so it is not a hot path; it is evidence that no reverse lookup exists.
  - The only engine-core readers of `StaticMeshComponent::Mesh` are `RenderWorld.cpp:193` and `ScenePicking.cpp:18`. Sample code also writes it (`PerspectiveLevel.cpp:70-71`) and swaps the Mesh values of two entities (`PerspectiveLevel.cpp:197`, `SwapMeshSources`).
  - Lease maps and the asset cache are keyed by `AssetId` alone (`AssetStore.cpp:123`, `Scene.cpp:374-384`). `docs/EngineModules.md:183` and `SceneWorkflow.md:88` already say `MeshHandle` must not be serialized.
- **Why it matters:** generic serialization, multi-object FBX, and editor clone/remap. ROADMAP M6 lists generic component serialization and stable multi-object FBX sub-asset identities, and neither can be done generically while components hold `MeshHandle` and fixed char-array names. Without this, serialization needs special codecs for these fields, and a later identity fix would throw them away. Nothing breaks today; the rating reflects that this must be sequenced ahead of an M6 exit item.
- **Fix:**
  1. A scene-scoped persistent entity ID (owner decision D-D on width and location), stored **per entity** (a slot-indexed array or a component) so Entity-to-ID lookups for serialization do not scan, plus an ID-to-Entity map. Give `m_Names` a transparent comparator (`std::map<std::string, Entity, std::less<>>`) so `SceneAccess::Find` looks up a `string_view` without constructing a string; a hash map is not required, since `Find` runs a handful of times per frame.
  2. An `EntityRef` field type: it stores the resolved Entity at runtime and serializes as the persistent ID. The field table tags it.
  3. `StaticMeshComponent` holds `AssetRef<MeshAsset>` only, with **one AssetId per sub-mesh** (ARCH-ASSET-02).
     - `RenderWorld` and Pick resolve AssetRef to MeshHandle through the scene lease map. This is **not** like textures (`RenderWorld.cpp:170`), which resolve only on add or a material change: `Write` calls `FindGroup(mesh.Mesh)` on every write, including the transform-only writes from `ApplyTicks` (`RenderWorld.cpp:94-112, 193`), so resolving through a `std::function` and the `std::map<AssetId, AssetLease>` on each write would add cost for the about 10k animated objects per frame at 50k. **Requirement:** resolve only on add or when the `StaticMeshComponent` tick changes, and keep the resolved runtime `MeshHandle` (widened by pipeline) as the render-world group key (ARCH-RND-01).
     - `MeshHandle` then leaves the `ComponentValue` allow-list. This does not contradict ROADMAP M6 item 1 ("Expose ... runtime resource handles to game code"): handles are exposed through scene and asset APIs, not through component fields.
  4. Classify field types as persistent or runtime-only. Generic serialization rejects runtime-only types.
  5. Port Rotator, Door and Goal to `EntityRef` when they move into the game (ARCH-CORE-02).
  6. Migration scope includes the sample writers `PerspectiveLevel.cpp:71` and `:197`, listed so the owner can apply them as snippets.
- **Effort:** M
- **Timing:** M6-4
- **Panel discussion:**
  - ASSETS challenged two parts of ECS-3, and both challenges were accepted:
    - The sub-asset index: both lease maps and the catalog are keyed by AssetId alone (`AssetStore.cpp:123`, `Scene.cpp:374-384`), so an index adds a second key dimension everywhere.
    - A runtime-tagged `MeshHandle` field: resolving it would require a host Write that stamps ticks, and the field table would carry a "skip" class forever.
  - With one Registry per world (owner decision D-G), the play-world clone needs only an `m_Registry` rewrite of Entity fields. Persistent IDs are still needed for disk and the editor.
  - Round 2 said the group key "can become the mesh AssetId". Verification of ARCH-RND-01 showed that keying by AssetId conflicts with procedural meshes, and verification of this finding showed per-write resolution is too costly; resolving on tick change and keeping the runtime `MeshHandle` key satisfies both.
- **Verification:** confirmed with corrections; severity high -> high because generic serialization and stable sub-asset identities in M6 cannot be built without it, and codecs written for the current fields would be thrown away; medium would also be defensible, since nothing breaks today.

#### ARCH-ECS-04: H6 migration has no defined change-stamp or journal semantics, because journals and pools are keyed by `LayoutHash`

- **Severity:** medium
- **Kind:** architecture-gap
- **Source:** ECS-12
- **Evidence:**
  - `Registry.cpp:427-429`: a journal attaches only on matching `LayoutHash`.
  - `Registry.cpp:437`: only `Insert` stamps the current tick.
  - `docs/HotReloadDesign.md` H6 describes field copying only.
- **Why it matters:** migrated components would be invisible to tick consumers, and journal subscribers would silently stop receiving events.
- **Fix:** write into the H6 design:
  1. Migration rebuilds the pool through `Insert`, so every element is stamped as changed.
  2. The journal is carried over to the new `LayoutHash`, and its subscribers get `Resync = true`.
  3. Migration runs at activation, before the engine PostUpdate.
- **Effort:** S
- **Timing:** M6-8 design (the logging part is in ARCH-ECS-02 step 6, now)

#### ARCH-ECS-05: A failed journal append loses an event, and every later record then forces a resync

- **Severity:** low
- **Kind:** defect
- **Source:** ECS-8
- **Evidence:**
  - `Registry.cpp:278-282`: sets `MaxBacklog = 0` permanently on `push_back` failure.
  - `Registry.cpp:286`: a caught-up subscriber is not flagged, so the dropped event is lost, and every later record exceeds a backlog of 0.
  - `docs/ECSDesign.md:342`: says "a failed append returns Resync".
- **Fix:** in the catch block, set `Resync = true` and `Cursor = journal.End()` per subscriber, and leave `MaxBacklog` unchanged.
- **Effort:** S
- **Timing:** now (opportunistic)

#### ARCH-ECS-06: API sharp edges: `Single` always stamps, mutable views stamp untouched elements, unqualified type names can alias pools

- **Severity:** low
- **Kind:** defect
- **Source:** ECS-9 (refined)
- **Evidence:**
  - `Registry.hpp:351-361` and `SceneAccess.hpp:72-81`: `Single<T>` uses a mutable view and is used read-only at `InteractionGame.cpp:65`.
  - `Registry.hpp:321`: mutable views stamp every visited element. `InteractionGame.cpp:95` therefore stamps settled rotators every frame, which costs a compose and a Write each.
  - `Component.hpp:149, 187-188`: an unqualified `RAVEN_COMPONENT(Tag)` compiles. Same-named types in different namespaces with identical layouts share one pool. With different layouts, the second type is silently invisible.
  - All 17 current declarations are fully qualified, so this is latent.
- **Fix:**
  1. `Single<const T>` on Registry and SceneAccess.
  2. Document "iterate const, Write on change" at the view API.
  3. `static_assert(TypeName.find("::") != npos)` in the macro.
  4. Plan a rename/alias table in the M6 serializer.
- **Effort:** S
- **Timing:** now / M6

#### ARCH-ECS-07: `uint32` change ticks have no wraparound handling

- **Severity:** low
- **Kind:** defect
- **Source:** ECS-11
- **Evidence:**
  - `Registry.cpp:118, 579-582`: `ChangeTick++` with no check.
  - `RenderWorld.cpp:108-110` and `Scene.cpp:323`: plain `>` comparisons.
  - About 2 advances per frame: roughly 207 days at 120 FPS, days with several worlds at high FPS.
- **Why it matters:** after a wrap, static objects compare as changed every frame, which means a full 50k rewrite indefinitely.
- **Fix:**
  1. On wrap, zero every pool's ticks, reset to 1 and bump a `TickEpoch`.
  2. Scene and RenderWorld force their existing full path when the epoch changes.
  3. Make the tick type a single alias.
- **Effort:** S
- **Timing:** before M7, or with ARCH-PERF-02

---

### 4.4 Hot reload and module boundary (HR)

#### ARCH-HR-01: The DLL watch is not coordinated with manual or full builds, and F5 ignores an active build

- **Severity:** medium
- **Kind:** defect
- **Source:** HR-3 (revised in round 2)
- **Evidence:**
  - `Private/Core/GameModule.cpp:509-510`: the DLL output is always subscribed. `GameModule.cpp:523, 557`: the watch is paused only around self-started builds.
  - `Application.cpp:100-101` and `GameModule.cpp:603-606`: F5 calls `Activate()` even while building.
  - `docs/HotReloadDesign.md:305`: reading the DLL on notifications caused LNK1104.
  - `GameModule.cpp:525-543, 565-569`: after a failed auto-build there is no retry until the next source edit, so a transient LNK1104 is sticky.
  - `Private/Samples/RavenEngine.cpp:162`: the `--game` hint invites building the GameBuild child directly, which bypasses `BuildInputs.sha256`.
  - `cmake/BuildGame.cmake.in:177-186` and `cmake/RavenGameModule.cmake:31-36`: every supported build path commits the stamp only after verification.
- **Why it matters:** reload reliability in the documented `--game` workflow.
  - Confusing link failures that stick.
  - A half-written DLL being *loaded* was **refuted**: the stamp, digest and contract checks reject it (`GameModule.cpp:363-386`).
- **Fix:**
  1. Watch `GameBuild/BuildInputs.sha256` instead of `RavenGame.dll`.
  2. Change the `RavenEngine.cpp:162` hint to name `cmake -P .../BuildGame.cmake`.
  3. While `IsBuilding()`, F5 logs "build in progress" and returns.
- **Effort:** S
- **Timing:** now

#### ARCH-HR-02: ReloadService cost and memory grow with total watched bytes, and the watch silently degrades to polling

- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** HR-4, PERF-8, PLAT-8 (merged)
- **Evidence:**
  - `Private/Core/ReloadService.cpp:186, 346, 355, 369`: full-content snapshots are retained and copied per job.
  - `ReloadService.cpp:26-39, 313-317, 335`: a 2 s audit re-reads every input and FNV-hashes it byte by byte. The per-pass `ReadCache` holds everything.
  - `AssetStore.cpp:108, 113`: resident assets keep source bytes twice. `AssetStore.cpp:32` makes a third, transient copy.
  - `Private/Platform/Windows/Win32DirectoryWatch.cpp:31-32, 43-44`: at 64 or more directories (63 directories plus the stop event exactly fill `MAXIMUM_WAIT_OBJECTS`, which is 64), or one failed open, the watch is unavailable. `ReloadService.cpp:384` then polls every 40 ms with no log line.
  - Today's content is 3.5 MB in 5 folders. A generated 50k scene is 10.5 MB and is watched while active.
  - ASSETS estimates 50-300 MB of watched content for the M6 game.
- **Why it matters:** goal 3 at real content scale. Each audit hashes 50-300 MB, roughly 0.1-0.5 s of a core per 2 s, plus about 2x source bytes in RAM. The M5 idle budget never measured this.
- **Fix:**
  - **Now:**
    1. State and `ReadCache` keep size, mtime and digest only; the worker re-reads at dispatch.
    2. Log once, with the reason, when the DirectoryWatch is unavailable.
    3. Replace the byte-wise FNV with a word-wise non-cryptographic hash *before* any disk cache keys on it.
    4. `ManagedAsset` keeps a digest instead of `Bytes` when not watching, and the extra copy in `DecodeAsset` is removed.
  - **Before M6 content growth:**
    5. Change the DirectoryWatch contract to "recursive root, reports changed relative paths". Win32 uses one `ReadDirectoryChangesW` per root; inotify adds subdirectory watches inside the backend; FSEvents is natively recursive.
    6. The audit becomes a budgeted round-robin safety net, and binary assets rely on size and mtime.
- **Effort:** M
- **Timing:** steps 1-4 now; steps 5-6 before M6 content growth (M6-10 at the latest)
- **Panel discussion:** PLAT-8's "collapse sibling directories near the limit" was dropped as throwaway, because the recursive-root contract replaces it.

#### ARCH-HR-03: Player builds load the game through the development reload path; execution copies leak

- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** HR-5, CORE-6 (merged and re-timed)
- **Evidence:**
  - `GameModule.cpp:366-376`: every load creates `.raven-game-N` beside the executable.
  - `docs/PlayerBuild.md:9`: requires a writable install directory.
  - `Application.cpp:100-101`: F5 is not gated by `WatchGameModule`.
  - `RavenEngine.cpp:59-62`: the player sets no-watch but still constructs `GameModule`.
  - `docs/EngineModules.md:494`: says shipping packages omit the development module.
  - `GameModule.cpp:209-214`: cleanup happens only in destructors. 13 leftover `.raven-game-*` directories were counted in x64-debug and 44 in x64-release, plus about 98 `%TEMP%/raven-shader-*` directories.
  - `Private/Platform/GameModulePlatform.hpp:9-11`: the platform seam is only Load, Find and Unload.
- **Why it matters:** M7 packaging from read-only installs, and unbounded development disk use.
- **Fix:**
  1. **Now:** at `GameModule` construction, sweep stale `.raven-game-*` directories. Put shader scratch under one per-process parent, swept the same way.
  2. **Before M7:** extract a shared `LoadGeneration(imagePath)` (contract check, register, `LoadSystems`) used by an in-place mode (player and `--no-watch`: no copy, `Reload`/`Poll` are no-ops) and the replaceable mode. Promote it to a separate class only when the editor needs one.
  3. **Before M7:** gate F5 on `config.WatchGameModule`. This also becomes part of ARCH-CORE-04's engine-dev context.
- **Effort:** S
- **Timing:** sweep now; steps 2-3 before M7
- **Panel discussion:** CORE-6 argued that M4 dlopen backends would copy the dev model and that the class split must precede M4. Refuted: the copy is shared code, and the backend implements only Load, Find and Unload. Development builds must keep the copy on every OS: Linux dlopen deduplicates by path and inode, and rewriting a mapped `.so` can raise SIGBUS. Re-timed to "before M7". M4 contributors only need to be told the seam is Load, Find and Unload.

#### ARCH-HR-04: The game contract is too coarse, and the watched game inputs are kept in four hand-maintained lists

- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** HR-6, HR-10
- **Evidence:**
  - `GameModule.cpp:98-101, 121-132`: two C++ lists of fixed roots.
  - `cmake/RavenGameSources.cmake:3-4` and `cmake/GameContract.cmake:6-12`: a third and a fourth list.
  - `GameContract.cmake:9-10`: any `.hpp` in Public or `Private/Game`, comments included, forces a host restart.
  - `GameContract.cmake:6-10`: hashes every `.cmake` and `.in` under `cmake/`, including `CompileShader.cmake`, `RavenContent.cmake`, `RavenPlayer.cmake`, `StageContent.cmake.in`, `ShaderWatchConfig.hpp.in`, `ContentSourceConfig.hpp.in` and `RavenPlatform.cmake`, which cannot affect the game ABI.
  - `cmake/BuildGame.cmake.in:42-43`: the same input function also drives needless reconfigures.
  - `GameContract.cmake:22-26`: only global values are hashed directly (compiler ID and version, pointer size, build type, `CMAKE_CXX_FLAGS(_CONFIG)`, the standard and the runtime); target-level compile options such as `/W4` and the PCH are not.
- **Why it matters:** fast iteration. M6 per-material shader work will edit the shader and content recipes, and each edit currently invalidates a running host. Moving the game out of tree means touching four lists.
- **Fix:**
  1. **Now:** replace the blanket `cmake/` scan with an allowlist: `RavenBuildSettings.cmake`, `RavenGameSources.cmake`, `GameContract.cmake`, `RavenSourceDiscovery.cmake`, `BuildGame.cmake.in`, the root files and `Private/Game/CMakeLists.txt`. Mirror it in `GameModule.cpp`.
  2. **Before M6-10:** generate the watched-input list once from CMake (a `GameInputs.txt` beside `GameBuildConfig.hpp`) and have the C++ read it.
  3. **M6-10:** after relocation (ARCH-CORE-02) and activation-time descriptor validation (ARCH-ECS-02), drop `Private/Game` headers from the contract. Keep Public headers and the ABI-relevant build files.
- **Effort:** M overall (step 1 S)
- **Timing:** step 1 now; step 2 before M6-10; step 3 at M6-10
- **Panel discussion:** narrowing via compiler dependency output was **rejected**:
  - The host bakes its header set at build time, so a newly included header that changed would be missed.
  - The PCH can hide includes.
  - It adds per-toolchain parsing work for M4.
  - Owner decision D-F (in-tree game versus separate project roots) sets the final shape of step 2.

#### ARCH-HR-05: Activation hashes and copies the DLL on the frame thread

- **Severity:** low
- **Kind:** defect
- **Source:** HR-8
- **Evidence:**
  - `GameModule.cpp:363, 376-377`: hash, copy and re-hash inside `Poll()`.
  - `docs/HotReloadDesign.md:344`: stamp takes 8-11 ms and copy 17-46 ms per activation.
  - `GameModule.cpp:539, 569`: the worker already stamps.
- **Fix:** the worker stages and verifies the execution copy, and publishes the staged path. `Activate` then only loads, checks and swaps.
- **Effort:** S
- **Timing:** during M6

---

### 4.5 Renderer and GPU-driven path (RND)

#### ARCH-RND-01: Draw groups are keyed by mesh only, looked up linearly, retired only by a rebuild, and drawn with one pipeline

- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** RND-2, PERF-6 (group parts)
- **Evidence:**
  - `Private/Renderer/RenderWorldState.hpp:83-85`: a group is one `MeshHandle` (three `uint64` values, `Public/Raven/Renderer/MeshHandle.hpp`), plus `GroupCounts`.
  - `RenderWorld.cpp:153-162`: linear `FindGroup` comparing 24-byte keys, with `push_back`. `Remove` (`:137`) only decrements `GroupCounts`; groups at count 0 are removed only when `Rebuild` clears everything (`RenderWorld.cpp:60-61`). Until then, `Plan` still resolves each dead group and emits a zero-instance command for it every frame (`VulkanGpuScene.cpp:298-316`). Groups do not pile up across FBX reloads, because `ReplaceMesh` keeps the handle.
  - `RenderWorld.cpp:193`: `FindGroup` runs on every Write, including transform-only rewrites from `ApplyTicks` (`:109-111`) and `Reconcile` (`:88`), even when the mesh did not change.
  - `Private/Renderer/Vulkan/VulkanGpuScene.cpp:465-479` (`RecordDraw`): one pipeline and one `vkCmdDrawIndexedIndirect` with `drawCount = m_GroupCount`.
  - `Cull.comp.hlsl:28, 47-48`: indexes `Meshes[object.Group]` and `Commands[object.Group]`.
  - `GpuScene.hlsli:26-34` and `VulkanGpuScene.hpp:72-80`: `MeshData.Padding` is unused (always written as 0, `VulkanGpuScene.cpp:306`).
  - `ROADMAP.md:153-154`: M6 groups commands per (shader, mesh), and transparency later moves to the render world. The one-pipeline, mesh-only key is therefore an already-planned M6 item, not a new gap.
  - `docs/GpuDrivenDesign.md:251`: the recorded stress scenes have two unique meshes; `GpuDrivenDesign.md:140` sets about 1,000 unique meshes as the trigger to revisit grouping.
- **Why it matters:** M6 per-material shaders, multi-object FBX and goal 4 once mesh variety grows.
  - At 500 groups, the linear lookup alone adds an **unmeasured estimate** of 1.3-2.5 ms at 10k changed objects per frame, which would break the 4 ms CPU budget (measured at 3.53 ms).
  - The 6-12 ms full-rewrite estimate is the `Rebuild` path, which runs only on scene bind or a lifecycle resync (`RenderWorld.cpp:281-284`): a one-time hitch, not a per-frame cost.
  - Nothing triggers either today. The FindGroup-on-every-Write cost is the only latent hot-path part; the rest is design input for M6.
- **Fix:**
  1. **Now:** inside `Write`, call `FindGroup` only when `added || !(state.Groups[object.Group] == mesh.Mesh)`. This exact guard needs no new parameters. (Passing a `meshChanged` flag from `ApplyTicks` would be coarse: the `StaticMeshComponent` tick is also stamped by `Tint` writes and by any mutable view or `TryGet`, and `Reconcile` is a second caller.) After ARCH-ECS-03 the comparison uses the handle resolved on add or StaticMesh tick change.
  2. Hash-map lookup. The key starts as the runtime `MeshHandle` and widens to (pipeline, `MeshHandle`). It does not become the mesh AssetId: procedural meshes have no asset, and ARCH-ECS-03 resolves `AssetRef` to the handle only on change.
  3. Retire groups at count 0 through a free list. `Plan` skips free slots, or gives them empty commands, so group IDs stay stable for the shader indexing.
  4. Store a `CommandIndex` in the spare `MeshData.Padding`, so group IDs stay stable while `Plan` buckets commands contiguously per pipeline each frame. `Cull` reads `Commands[mesh.CommandIndex]`. This is more than a two-line shader change: `Plan`'s per-group region and template building (`VulkanGpuScene.cpp:295-316`), the `GetDrawCount`/`m_GroupCount` usage and `ReadVisible`'s per-group readback (`VulkanGpuScene.cpp:504-517`) all move from group order to command order. The `GetBufferLayouts` offset check (`VulkanGpuScene.cpp:211-213`) still works if `Padding` is renamed, because only the offset matters.
  5. `RecordDraw` issues one indirect call per pipeline range.
  6. State explicitly that transparent render-world objects use a separate sorted path.
- **Effort:** M
- **Timing:** step 1 now; design in the M6 note; steps 2-6 at M6-9 (before sprites)
- **Panel discussion:** none in round 2. Verification dropped the AssetId-key option from step 2; this agrees with the ARCH-ECS-03 verification, which requires resolution only on change.
- **Verification:** confirmed with corrections; severity high -> medium because the performance figures are unmeasured estimates, current scenes have two unique meshes against a documented revisit trigger of about 1,000, and the pipeline-aware grouping is already an M6 roadmap item.

#### ARCH-RND-02: Render-world material identity and texture lifetime are unsafe under M6 asset changes

- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** RND-1, RND-9, ASSET-11, PERF-6 (material part)
- **Evidence:**
  - `RenderWorldState.hpp:80-81`: append-only. `RenderWorld.cpp:164-183`: linear exact-float compare with append. `RenderWorld.cpp:59`: cleared only on Rebuild.
  - `RenderWorld.cpp:172`: stores the raw bindless index and holds no lease.
  - `VulkanTextureDescriptors.cpp:181-192, 196-216`: freed elements are reused immediately and are never reset. `:57-58` uses `UPDATE_UNUSED_WHILE_PENDING`.
  - Safe today only because scene leases only grow (`Scene.cpp:374-384`) and the sample re-syncs before any draw after a scene replacement (`PerspectiveSample.cpp:126-130`).
  - No game system writes `MaterialComponent`. Gameplay recolors through `Tint` (`InteractionGame.cpp:39, 89, 147`; `RenderWorld.cpp:204`).
- **Why it matters:**
  - Animated materials would grow the table without bound, with quadratic lookup.
  - Once leases are released while a scene lives (ARCH-ASSET-03 step 3), or a session draws before re-syncing, materials sample a destroyed or reused element without any error.
- **Fix:**
  1. **Now:** document `StaticMeshComponent::Tint` as the per-frame animatable color. Read `MaterialComponent` only through const access in `Sync`.
  2. **Now:** `ReleaseBinding` writes white element 0 into a freed element. This is legal there because release follows timeline retirement (`VulkanContext.cpp:558-563`).
  3. **M6 material design (owner decision D-I):** key materials by (material or texture AssetId, overrides). Each entry holds the lease or refcount for its texture, and freed entries go to a free list with dirty-range uploads. Today only the tail is uploaded (`VulkanGpuScene.cpp:291-292`).
  4. Only after step 3 may lease lifetimes be derived from the lifecycle journal.
- **Effort:** M
- **Timing:** steps 1-2 now; steps 3-4 at M6-9
- **Panel discussion:** RND-1's "hash map over raw `GpuMaterialData` bytes" stopgap was withdrawn by RENDER and PERF as throwaway, because M6 re-keys materials. RND-1 was re-rated from high to medium, because the growth path is latent today.

#### ARCH-RND-03: The GPU scene's upload identity (raw pointer plus per-world generation) is open to ABA

- **Severity:** medium
- **Kind:** defect
- **Source:** RND-5. Lead verified the trigger is live in the smoke harness.
- **Evidence:**
  - `VulkanGpuScene.cpp:289-292`: a full upload happens only if the pointer or generation differs; otherwise the material upload is skipped from the previous count.
  - `RenderWorldState.hpp:67`: `Generation` starts at 1. `RenderWorld.cpp:235-244`: `Clear` derives from the world's own counter, so each new world reaches the same sequence.
  - `scripts/Windows3DSmoke.cpp:164`: creates a fresh `RenderWorld world;` inside `Render()`, which is called three times. The smoke checks only `has_value()`, so wrong materials would go unnoticed.
  - The codebase already has the right pattern: monotonic `Registry::AllocateSceneId` (`Registry.cpp:595-600`).
- **Why it matters:** ARCH-CORE-01 and editor sessions will create and destroy RenderWorlds. Objects would draw stale or wrong materials.
- **Fix:**
  1. Draw RenderWorld generations from a process-wide atomic counter in the constructor, `Clear` and `Rebuild`.
  2. `VulkanGpuScene` compares only the generation.
  3. When `RND-01` and this regrouping touch `VulkanGpuScene`, split its members into per-world resident state (objects, materials, uploaded identity) and per-view state (visible, commands, meshes, readback). This is a member regrouping only; no render-to-target API in v1.
- **Effort:** S
- **Timing:** steps 1-2 now (precondition of ARCH-CORE-01 S0); step 3 at M6-9

#### ARCH-RND-04: Texture uploads drain the queue synchronously, and descriptor-swap safety depends on that implicit drain

- **Severity:** medium
- **Kind:** architecture-gap
- **Source:** RND-4 (corrected in round 2)
- **Evidence:**
  - `VulkanTexture.cpp:81-94`: `vkQueueSubmit` with a fence, then `vkWaitForFences`, on the graphics queue (`:223`). Frames use the same queue (`VulkanContext.cpp:640-642`, `VulkanClearPass.cpp:507`).
  - `AssetStore.cpp:216-222` and `SceneSession.cpp:97-103`: no frame runs between prepare and commit.
  - `VulkanContext.cpp:794-805`: rewrites live bindless elements.
  - `Public/Raven/Renderer/Vulkan/VulkanContext.hpp:84`: wrongly extends the "prepared uploads complete earlier graphics work" claim to meshes. Mesh uploads are non-blocking timeline submissions ordered by barriers (`VulkanMesh.cpp:253-254, 273-292`).
  - `docs/TextureResources.md:27`: "an initial synchronous path".
- **Why it matters:**
  - Correctness today is spec-guaranteed: a fence signal's first scope covers all earlier submissions on the same queue. This is **not a current bug**.
  - Every texture load costs a full GPU drain, so N textures cause N hitches on M6 scene and FBX loads.
  - Batching or async uploads would silently break the descriptor-swap invariant.
- **Fix:**
  1. Add one renderer-owned per-frame upload batch: a staging ring plus one command buffer signalling the timeline. Route texture and mesh uploads through it; a multi-mesh FBX becomes one submission.
  2. At `SwapTextureContents`, wait explicitly on the timeline for the last submitted frame before rewriting descriptors. Reloads are rare, so a one-frame stall fits the budget.
  3. Correct the `VulkanContext.hpp:84` comment in the same snippet (see DOC Phase B2).
  4. Record the contract in `RendererDesign.md`.
  5. `ReplaceTexture` is deleted with ARCH-CORE-05 rather than fixed.
- **Effort:** M
- **Timing:** M6-10 (before multi-object FBX)

#### ARCH-RND-05: Device selection never checks the limits behind the 4,096-entry bindless array; every texture has its own sampler and allocation

- **Severity:** medium
- **Kind:** defect
- **Sources:** PLAT-4, RND-3 (re-rated)
- **Evidence:**
  - `VulkanTextureDescriptors.hpp:20`: `BindlessCapacity = 4096`.
  - `VulkanPhysicalDevice.cpp:68-73, 215-227`: requires the feature booleans only. A grep finds no reads of `maxPerStageDescriptorUpdateAfterBindSampledImages` or `maxDescriptorSetUpdateAfterBindSampledImages`.
  - `VulkanPhysicalDevice.cpp:13-14, 237-238`: enables `VK_KHR_portability_subset` but never queries its features.
  - `VulkanTexture.cpp:192, 219` and `VulkanBuffer.cpp:78-88`: per-resource `vkAllocateMemory` and `VkSampler`. Only the legacy set-0 path uses per-texture samplers (`VulkanTextureDescriptors.cpp:238-257`).
  - `VulkanTextureDescriptors.cpp:95-103`: one clamp, no-mip bindless sampler.
  - `ROADMAP.md:24` and `docs/PlatformHandoff.md:174-177`: promise a clear unsupported-device error.
- **Why it matters:** goal 1 on macOS. MoltenVK limits depend on the argument-buffer tier, and low-limit devices would fail late in shared renderer code.
- **Fix:**
  1. Chain `VkPhysicalDeviceVulkan12Properties` and reject devices whose update-after-bind sampled-image limits are below `BindlessCapacity`, with a clear reason.
  2. Query and log the portability-subset features once.
  3. Delete per-texture samplers with the M6 sprite move (they serve only set-0); do not cache them.
  4. In the M6 material design, give bindless a small fixed sampler set (clamp and repeat, linear and mip).
  5. Revisit allocation (VMA) only if `GetResourceStats` shows counts approaching limits.
- **Effort:** S
- **Timing:** steps 1-2 now; steps 3-4 at M6-9
- **Panel discussion:** VMA was **not adopted**. The project has 7 asset records, an M6 game plausibly has tens to low hundreds of textures, and geometry is pooled. The quoted NVIDIA limits are not recorded for this laptop. A new dependency and a multi-file rewrite have no measured need.

#### ARCH-RND-06: The 2D path has not been run through four renderer rewrites

- **Severity:** medium
- **Kind:** process
- **Sources:** PROC-5 (step 1), RENDER blind spot
- **Evidence:**
  - `TODO.md:46` is unchecked since M1. `RendererDesign.md:201, 255` and `GpuDrivenDesign.md:244`: Courtyard is "Not run".
  - `OrthographicSample.cpp:30, 88`: uses legacy spans and unmanaged `CreateTexturedMesh`, with no RenderWorld and no world transforms. It compiles in CI, but runtime behavior has not been seen since M1.
  - `ROADMAP.md:153`: M6 sprites would be the first 2D run, so failures could not be separated from M2/M3 changes.
- **Fix:**
  1. One owner-requested Courtyard run before M6-9.
  2. Add 2D captures to the reference set.
- **Effort:** S
- **Timing:** before M6-9 (owner request required)

---

### 4.6 Performance and evidence (PERF)

#### ARCH-PERF-01: Goal-4 FPS evidence comes from a source-patched host; the shipped host is FIFO-only with a fixed 120 FPS sleep

- **Severity:** medium
- **Kind:** process
- **Sources:** RND-6, PERF-2, CORE-5 (merged; CORE added the window-placement scope)
- **Evidence:**
  - `Private/Core/Application.cpp:154-157`: after a comment that "FIFO presentation can already block", an unconditional `std::this_thread::sleep_until(frameStart + frameBudget)`. The budget is 1/120 s for drawable frames and 1/30 s for non-drawable frames (`:156`).
  - `Private/Renderer/Vulkan/VulkanSwapchain.cpp:103-108, 110-112, 158`: present modes are enumerated (103-108), FIFO is required or the code throws (110-112), and `presentMode` is hard-coded to FIFO (158).
  - `scripts/Windows3DHighRefresh.py:92-144` (`edit_snapshot`): patches `Application.cpp` (sleep only when not drawable), `VulkanSwapchain.cpp` (mailbox through `RAVEN_SCALE_PRESENT`), `VulkanPhysicalDevice.cpp` (vendor filter) and `Win32Window.cpp` (`WS_EX_TOPMOST`, `WS_POPUP`, explicit x/y/size) by exact-count string replacement. `Benchmark-Windows3D.py:281-285` builds the scale snapshot with it, and line 93 of the script says these switches stay in the disposable host only "until the production presentation options land".
  - The harness is not broken today: every anchor still matches exactly once at HEAD (`Application.cpp:157`, `VulkanSwapchain.cpp:7, 158`, `VulkanPhysicalDevice.cpp:3, 137-138`, `Win32Window.cpp:7, 81-85`). Any edit to those lines breaks it.
  - `docs/GpuDrivenDesign.md:224`: 126.5 FPS, above the stock 120 FPS cap; `:220` names the uncapped mailbox case as the reference. The M3 FIFO numbers were also captured on the patched host (`:220, 225`: FIFO capped at about 60 FPS by the external 4K 60 Hz monitor, FIFO CPU preparation 3.18 ms). There is no stock-host M3 measurement at all.
  - Adapter selection is not part of the gap for the reference case: since 869ea05 the stock host ranks discrete GPUs first (`VulkanPhysicalDevice.cpp:124-135`), so it already picks the RTX on the test laptop. The harness vendor filter matters only for the `intel-*` cases.
  - A grep finds no waitable timer and no `timeBeginPeriod`. `Application.hpp:12-22` and `Window.hpp:15-20` have no presentation, pacing or placement options.
- **Why it matters:** the recorded goal-4 FPS figure comes only from a patched snapshot, and the harness depends on exact source text. Goal 4 itself is not at risk: the pacing-independent evidence (CPU preparation p95 3.53 ms, or 3.18 ms under FIFO, and GPU p95 2.15 ms) shows a frame well inside both 60 and 120 FPS, so the stock host's cap still meets the ">= 60" target and roughly the "aim >= 120". What is unverified is the stock FPS figure, which the coarse `sleep_until` timer granularity on Windows could pace well below 120 FPS (not verified from code). That justifies the mandatory stock re-run.
- **Fix:**
  1. A public `RendererConfig` (no Vulkan or OS headers) passed to a new `VulkanContext(const Window&, const RendererConfig&)` (the constructor is in the public header `Public/Raven/Renderer/Vulkan/VulkanContext.hpp:48`). It holds `PresentMode {Fifo, Mailbox, Immediate}`, default FIFO, with FIFO fallback and a log line. Adapter options are scoped to what the benchmark's `intel-*` cases need, or deferred; `Auto` is the current discrete-first ranking, and a vendor/name filter is not added speculatively.
  2. `ApplicationConfig` gains `RendererConfig Renderer`, `MaxFrameRate` and `IdleFrameRate = 30`. The default pacing is an owner decision (Q11): either keep today's 120 FPS cap, or default `MaxFrameRate = 0` (no limiter), which is safe only because FIFO paces; a Mailbox or Immediate request then runs uncapped unless `MaxFrameRate` is set, which is intended for benchmarks and must be documented. Any change of the shipped default requires the step-5 re-run. The limiter uses a private `Platform::SleepUntil`: a Win32 high-resolution waitable timer, with `sleep_until` as the shared fallback. Add it to the platform-handoff list, because macOS and Linux follow it (the fallback keeps them linking).
  3. `WindowDesc` gains an optional position and a borderless style. `Window.hpp` already includes `<vulkan/vulkan.h>` (`:9`, ARCH-CORE-07), so these fields go into a header that already breaks the no-Vulkan-headers rule; `RendererConfig` must not.
  4. `RavenEngine.cpp` maps CLI flags to these. Delete `edit_snapshot` from the harness, and replace the `RAVEN_SCALE_*` environment variables in `Benchmark-Windows3D.py` with the CLI flags.
  5. Re-run the M3 exit cases on the stock host and record them. This re-run is mandatory.
- **Effort:** S-M. The change touches public `WindowDesc`, `ApplicationConfig`, a new public `RendererConfig`, the public `VulkanContext` constructor, the swapchain, `Win32Window`, a new platform timer, the sample CLI and `Benchmark-Windows3D.py`, as owner-applied snippets.
- **Timing:** now
- **Panel discussion:** none in round 2. Verification removed the `Benchmark3D.md:40-51, 63` citation: it is historical pre-M1 data from a 2,000-mesh scene (commit a99fbfa), whose roughly 27 ms frame p95 is dominated by a 24.574 ms frame-slot fence wait, not by the sleep, and it is not stock-host evidence for the M3 path.
- **Verification:** confirmed with corrections; severity high -> medium because the pacing-independent CPU and GPU numbers show goal 4 is met on the stock host too, so only the reproducibility of one FPS figure and the harness's dependence on source text are at stake.

#### ARCH-PERF-02: Steady-state CPU cost grows with total objects; the design note understates the walk cost

- **Severity:** medium
- **Kind:** doc-drift (plus performance)
- **Source:** PERF-4 (corrected in round 2)
- **Evidence:**
  - Local benchmark records (ignored `out/benchmark-scale-*/1-nvidia-mailbox/result.json`):
    - 100k static: CPU preparation p95 1.71 ms, world transforms 1.10 ms, render-world sync 0.50 ms. With nothing changing, both are pure walk cost.
    - 50k/20%: sync 1.29 ms, about 0.25 ms walk plus about 1.04 ms for 10k Writes.
    - World 1.69 ms: about 0.55 ms walk plus about 1.14 ms for 20k composes.
  - `RenderWorld.cpp:94-112`: `SlotOfIndex` and two sparse Finds per mesh before any tick compare.
  - `Scene.cpp:301-324`: two Finds per entity even when skipping. `m_WorldChanged.assign(N)` runs every frame.
  - `docs/GpuDrivenDesign.md:66`: "tens of microseconds at 50k" (actual: about 250 us).
- **Why it matters:** about 0.47 ms of headroom remains against the 4 ms budget at 50k/20%.
- **Fix:**
  1. `ApplyTicks` walks the dense Ticks arrays of worlds, meshes and materials sequentially, resolving slots only on a hit. About 0.2 ms recovered.
  2. After ARCH-ECS-01's child links exist, `UpdateWorldTransforms` first scans Transform ticks densely and returns early when clean. Up to about 0.5 ms more.
  3. Correct `GpuDrivenDesign.md:66` with the measured split, and copy the stage numbers into the tracked doc, because `out/` is ignored.
- **Effort:** S
- **Timing:** step 1 and doc before M6; step 2 after M6-3
- **Panel discussion:** PERF's "off by 40x" was withdrawn after the lead's challenge, because it counted Writes as walk cost. The doc is off by about 5-10x. Re-rated from high to medium.

#### ARCH-PERF-03: The "dynamic objects" ceiling at 100% animated has never been measured

- **Severity:** medium
- **Kind:** architecture-gap
- **Source:** PERF-5 (re-rated)
- **Evidence:**
  - `Private/Tools/SampleScenes.cpp:60`: `animate = index % 5 == 0`.
  - `GpuDrivenDesign.md:225-227`: two data pairs give about 0.25-0.27 us per changed object.
  - `VulkanGpuScene.cpp:320-326, 379`: dirty records are copied twice.
  - There is no job system in `Private/Core`.
- **Why it matters:** the goal promises dynamic objects. A linear *projection* puts 50k at 100% at 9-14 ms of CPU, which is still interactive but leaves no gameplay headroom.
- **Fix:**
  1. Add `--animated-percent` to `RavenSceneTool create-benchmark`, and record 50k at 50% and 100%.
  2. Write dirty records straight into the upload ring.
  3. Only if the target is missed, write a short note for one host-owned task pool (fixed workers, parallel-for over ranges, main-thread join). It could also serve `AssetStore` decoding and `ReloadService` workers.
  4. No ad hoc `std::thread` parallel-for, because it would be throwaway.
- **Effort:** M
- **Timing:** steps 1-2 before M6; step 3 during M6 only if needed

#### ARCH-PERF-04: Benchmark methodology is too thin for exit decisions

- **Severity:** medium
- **Kind:** process
- **Source:** PERF-7
- **Evidence:**
  - `GpuDrivenDesign.md:218-220`: one run per case, with an external monitor attached.
  - `scripts/Benchmark-Windows3D.py:118`: requires every mesh to be visible, so culling never rejects anything.
  - `GpuDrivenDesign.md:251`: the stress scenes have two unique meshes.
  - `Tests/Benchmarks/EcsBenchmark.cpp:191-206`: no `Sync` or structural cases.
- **Fix:** the M6 exit benchmark matrix, kept outside CTest:
  - **Run conditions:** Release, stock host (after ARCH-PERF-01), AC power, monitor state recorded, 3 alternating repeats, median p95 plus min-max range. Mailbox-uncapped for throughput and FIFO for the shipped default.
  - **Cases:**
    1. 50k at 20% animated.
    2. 50k at 100% animated.
    3. 100k static.
    4. 50k with about half the grid outside the frustum (visible count in a band).
    5. 50k with 256 meshes, 64 materials, and later 4 shaders.
    6. 100 spawns plus 100 despawns per frame, and a 1k burst.
    7. Material churn for 60 s, recording the material count.
    8. The 50k reload spike.
  - **EcsBenchmark micro cases:** `Sync` with 0% and 20% changed, create and destroy 1k per frame at 100k, reparent a 1k subtree.
- **Effort:** S
- **Timing:** before M6 (cases are added as their features land)

#### ARCH-PERF-05: Small per-frame overheads

- **Severity:** low
- **Kind:** defect
- **Source:** PERF-9
- **Evidence:**
  - `GameModule.cpp:337-339`: a `std::map<std::string>` lookup per system call.
  - `VulkanContext.cpp:612-619`: `OwnsResource` is a linear scan per textured draw.
  - `VulkanClearPass.cpp:420`: the indirect commands are read back every frame.
- **Fix:**
  1. Cache the stats pointer in `LoadedSystem`.
  2. Remove `OwnsResource` with the M6 sprite move.
  3. Record the readback only when metrics are active.
- **Effort:** S
- **Timing:** later (OwnsResource at M6-9)

#### ARCH-PERF-06: Picking is O(scene entities) per click with live ancestor composes

- **Severity:** low
- **Kind:** architecture-gap
- **Source:** PERF-10
- **Evidence:**
  - `Private/Scene/ScenePicking.cpp:13-21`: a full `m_Entities` loop with copied `Read`.
  - `Scene.cpp:208-236`: live compose up the hash map (the ancestor walk in `ComposeTransformUnchecked` is `:208-224`; `:226-236` are the `ComposeTransform` and `GetWorldTransform` wrappers).
  - `PerspectiveSample.cpp:200`: the only sample caller. The smoke harness also calls it (`scripts/Windows3DSmoke.cpp:248`).
- **Fix:**
  1. Measure one pick at 50k.
  2. If it takes more than about 1 ms, use a scene-scoped `View<const StaticMeshComponent, const WorldTransformComponent>`. Use the cached world, or keep the live compose for the final candidate only. The owner decided picking "stays live", so this needs owner input.
- **Effort:** S
- **Timing:** during M6, after measurement

---

### 4.7 Assets, scenes and content (ASSET)

#### ARCH-ASSET-01: The only 3D scene format is the sample's fixed, positional schema

- **Severity:** high
- **Kind:** architecture-gap
- **Source:** ASSET-2 (format part; the relocation part is ARCH-CORE-02)
- **Evidence:**
  - `Public/Raven/Scene/SceneDocument.hpp:42-89`: Switches, Rotators, Doors, Goals, Collection, Beacon and WinSound are hard-coded records. The puzzle-section code is in `Private/Scene/SceneDocument.cpp`: Encode at `:220-253` and Decode at `:335-369`.
  - `SceneDocument.cpp` lives under `Private/Scene`, which `cmake/RavenSources.cmake:11` does not exclude from RavenCore.
  - `SceneDocument.cpp:170-262` (Encode) and `:265-373` (Decode): fields are read and written in a fixed order. Decode rejects any version other than 1 or 2 (`:273`) and rejects trailing bytes (`:370`), so a field cannot be added and a decoder cannot skip an unknown one.
  - `SceneDocument.cpp:258` (written) and `:364` (branched on): the version only says whether Collection is present.
  - `SceneDocument.cpp:115-167`: `Validate` enforces the puzzle game's rules (rotators feeding doors, the collection's single exit goal) inside the engine.
  - `SceneProject.cpp:71, 83-86, 100-105, 121-122`: only `raven-project 1` is accepted, each scene line requires `perspective` or `orthographic`, unknown record kinds (`:100-101`) and extra fields (`:103-105`) are rejected, and the extension check hard-codes `.ravenlevel`/`.ravenscene` as scene types (`:121-122`). Only the sample hosts use `SceneType` (`OrthographicSample.cpp:38`, `PerspectiveSample.cpp:92`).
  - `Private/Samples/LevelFile.hpp:22-69`: a second text codec. It is sample code that `RavenSources.cmake:11` keeps out of RavenCore, not an engine codec, and it is used by both the 2D reader (`Private/Samples/OrthographicLevel.cpp:186`) and the legacy 3D text importer (`Private/Tools/LegacyScene.cpp:29-31, 55-57, 72`).
- **Why it matters:** a new game cannot author its own components into scenes without editing RavenCore, and fields cannot be added or skipped. This conflicts with the M6 exit ("a small 3D game ... using only engine APIs") and with the field migration that depends on it. It is an already-scheduled roadmap item (`ROADMAP.md:150`), not a new discovery; the finding fixes its design and order.
- **Fix:**
  1. A chunked, self-describing container: one section per component `TypeName`, with field-name-keyed records driven by the ARCH-ECS-02 field table. Unknown fields are skipped, and new fields take their defaults.
  2. A component section whose `TypeName` is not registered (a game DLL failed to load, or a component was removed) is **preserved as opaque bytes** and written back unchanged on re-save, so last-good hot reload never silently drops data; the editor can offer to delete it explicitly. The alternative, rejecting the file, is safer against stale data but blocks loading while a DLL is broken. The M6 note records the choice.
  3. A Scene-owned section for persistent IDs and names. The hierarchy rides on `ParentComponent`, which does not exist yet: today the hierarchy is the private `Scene::m_Parents` map (`Scene.hpp:78`) and, in files, the `SceneDocument::Parents` string map (`SceneDocument.hpp:76`). This step depends on ARCH-ECS-01 (M6-3) adding the component.
  4. Converter-then-delete (owner decision D-H), using the existing `RavenSceneTool import` pattern (`RavenSceneTool.cpp:61-65`, `LegacyScene.cpp`):
     - Regenerate the six tool-generated scenes (Puzzle, Collection, Benchmark-100/500/2000/4000; built by `create-puzzle`, `create-collection` and `create-benchmark` from `SampleScenes.cpp`) from the rewritten `SampleScenes.cpp`.
     - Convert only the three hand-authored 3D scenes (Perspective, Gallery, Assemblies) and the two `.ravenlevel` files (Courtyard, Orthographic). Converting the 2D files depends on sprites and their components being in the generic format, which the M6 sprite move schedules.
     - The owner confirms that Puzzle and Collection were not hand-edited after generation.
     - The interaction components still live in RavenCore at M6-6 (they move at M6-7, ARCH-CORE-02). Their `TypeName`s must stay stable through that move, or the rename is recorded in the serializer's alias table (ARCH-ECS-06 step 4); the tool-generated scenes can also simply be regenerated in the M6-7 commit.
  5. One commit replaces the content files. The next deletes the RVN3DSCN reader, the puzzle sections and `LegacyScene`. Once the 2D files are converted, the `.ravenlevel` text path (`LevelFile.hpp`, the reader at `OrthographicLevel.cpp:186`, and the extension check at `SceneProject.cpp:121`) is deleted in the same way, or the commit states why it is kept.
  6. `raven-project 2` replaces `SceneType` with a codec or game field. Version 2 is designed to **skip and log unknown record kinds and extra fields**, the same rule as the scene container, so ARCH-ASSET-02 can add its `model`/`submesh` records at M6-10 without a `raven-project 3` and without making the first version-2 decoder partly throwaway.
  7. Verify the conversion once with an ignored probe: decode old, convert, encode new, decode new, semantic dump.
  8. The tracked CPU round-trip test targets the **new** codec only, at codec level, so it does not wait on ARCH-CORE-03. This test is already scheduled by ROADMAP.md:150.
- **Effort:** L
- **Timing:** M6-6
- **Panel discussion:** none in round 2 beyond the format/relocation split. Verification asked for ARCH-CORE-02's relocation to come before M6-6, so regenerated files do not name RavenCore `TypeName`s that later change. The lead's order is kept, because the host can only stop compiling these components once it can instantiate them generically (see ARCH-CORE-02); stable `TypeName`s or the alias table resolve the concern without reordering.
- **Verification:** confirmed with corrections; severity high -> high because a new game cannot author its own components into scenes without editing RavenCore, which blocks the M6 exit and the field-migration item.

#### ARCH-ASSET-02: Multi-object FBX is structurally blocked by the catalog shape

- **Severity:** medium
- **Kind:** architecture-gap
- **Source:** ASSET-3 (option resolved)
- **Evidence:**
  - `Private/Assets/FbxMesh.cpp:29-43`: rejects more than one mesh node. `FbxMesh.cpp:63-66`: bakes `geometry_to_world`.
  - `Public/Raven/Assets/AssetRecord.hpp:24-29` and `SceneProject.cpp:94-98`: one ID maps to one file.
  - `Private/Assets/AssetId.cpp:52-53`: v4 UUIDs only.
  - `AssetStore.cpp:109-114`: one watch and one full decode per resident asset.
  - `AssetStore.cpp:33-45, 106-107`: dispatch by extension, and `Model` cannot be acquired.
- **Why it matters:** the M6 "stable sub-asset identities" item would be bolted on and later replaced.
- **Fix:** **Option A** (owner decision D-E).
  1. `raven-project 2` (designed at M6-6 to skip and log unknown records, ARCH-ASSET-01 step 6) adds `model <uuid> "path.fbx"` and `submesh <uuid> <model-uuid> "node/path"` lines with generated v4 IDs, written by a new `RavenSceneTool import-model` command. A missing node is a load error with last-good recovery. An unknown node is ignored and logged.
  2. `AssetStore` keys watch and decode by source path and fans out to sub-assets, so there is one decode per file.
  3. Keep baked-transform mesh mode for existing single-object assets. Model import emits node transforms as entities through the generic format.
- **Effort:** M
- **Timing:** M6-10 (after M6-4, M6-6 and the ARCH-RND-04 upload batch)
- **Panel discussion:** Option B (v5 IDs derived from node paths) was **rejected**. A node rename in the DCC tool silently orphans every reference and leaves nowhere to record a remap, so the editor would later have to replace B with a stored mapping. Re-rated from high to medium: once A is chosen, the remaining work is the scheduled M6 item.

#### ARCH-ASSET-03: Asset acquisition is synchronous and lazy, sometimes inside `RenderWorld::Sync`; scene leases never shrink

- **Severity:** medium
- **Kind:** architecture-gap
- **Source:** ASSET-5 (narrowed)
- **Evidence:**
  - `PerspectiveSample.cpp:307-312` and `RenderWorld.cpp:170, 202-203`: the resolver runs for added objects and changed materials.
  - `Scene.cpp:374-384`: returns the cached lease early, otherwise `AssetStore::Acquire`.
  - `AssetStore.cpp:160-176, 237-241`: read, decode and fence-waited upload, plus a stat and `Collect` per Acquire call.
  - `OrthographicSample.cpp:196`: per sprite.
  - `Scene.hpp:80`: `m_Assets` only grows.
  - `SceneAccess.hpp:29-100`: game code cannot acquire by `AssetRef`.
- **Why it matters:** the first time a game writes a texture ID the scene has never used, a synchronous decode and upload run mid-frame. Leases grow until the scene dies.
- **Fix:** one resolution policy in M6.
  1. Components hold `AssetRef` only (ARCH-ECS-03).
  2. The host resolves in the engine stage. Missing assets are decoded on `ReloadService` workers, uploaded at the frame boundary, and shown as a placeholder until resident.
  3. Derive lease refcounts from the lifecycle journal **only after** ARCH-RND-02 steps 2-3.
  4. Move the stat and `Collect` out of the Acquire cache-hit path.
- **Effort:** M
- **Timing:** M6
- **Panel discussion:**
  - "Stat plus Collect on every frame or cache hit" was **refuted**: Scene caches the lease, so the cost is per (scene, asset) miss.
  - "Collect during Sync retires what Sync references" was **refuted**: the scene's leases keep referenced textures alive.

#### ARCH-ASSET-04: Audio has no game-facing path and a single voice

- **Severity:** medium
- **Kind:** architecture-gap
- **Source:** ASSET-10
- **Evidence:**
  - `Private/Audio/AudioPlayer.cpp:70-80` and `Platform/Windows/Win32Audio.cpp:68-70`: `Stop()` before every `Play`.
  - `PerspectiveSample.cpp:283-284` and `OrthographicSample.cpp:76-78`: only host sample code plays sounds.
  - `AssetStore.cpp:109`: audio is never watched.
  - `ROADMAP.md:32, 125`: audio appears only as one-shots and an M4 backend task.
- **Why it matters:** the M6 game cannot trigger overlapping sounds through engine APIs. M4 backends written against the single-voice contract would be rewritten.
- **Fix:** an audio section in the M6 note before M4 audio backends are written.
  1. A pooled-voice contract: play returns a voice ID, stop by ID, stop-oldest policy.
  2. Game requests go through a host-drained request component.
  3. Watch WAV files like PNG and FBX.
  4. Tell M4 contributors about step 1 now.
- **Effort:** M
- **Timing:** M6 design (contract now)

#### ARCH-ASSET-05: Player packaging hard-codes sample dependencies and ships raw sources

- **Severity:** low
- **Kind:** architecture-gap
- **Source:** ASSET-8
- **Evidence:**
  - `Private/Tools/PlayerContent.cpp:7, 67-68, 80-90`: `SampleAssets` include, Perspective-only, hand-walked dependencies.
  - `PlayerContent.cpp:104-111`: ships FBX and PNG, so ufbx and stb run in players.
- **Fix:**
  1. Compute the dependency closure from the field table by walking `AssetRef` fields.
  2. For M7, cook the serialized `DecodedAsset` keyed by the existing reload record and importer version (HotReloadDesign Q4).
- **Effort:** S
- **Timing:** M6 (closure), M7 (cook)

#### ARCH-ASSET-06: Case-mismatched asset paths work on Windows and macOS but fail on Linux; case aliases duplicate assets

- **Severity:** low
- **Kind:** defect
- **Source:** PLAT-10
- **Evidence:**
  - `Private/Assets/AssetPath.hpp:14-15`: `weakly_canonical` without a case check.
  - `SceneProject.cpp:116` resolves every record.
  - `docs/AssetSystem.md:24`: aliases are not merged.
  - Latent today: all 18 records match the disk case.
- **Fix:** an OS-neutral check comparing the resolved relative tail with the record path, rejecting case-only differences. It fires on Windows, where authoring happens.
- **Effort:** S
- **Timing:** before M6

---

### 4.8 Cross-platform (PLAT)

#### ARCH-PLAT-01: The hot-reload seams and game-module pipeline are Windows/MSVC-shaped in shared code; the handoff understates the M4 work

- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** PLAT-1, BLD-2, HR-7 (toolchain part), PROC-2 (seam part)
- **Evidence:**
  - `Private/Renderer/Shaders/ShaderLibrary.cpp:267, 276-279`: `ShaderLibrary::Watch` has `#ifdef _WIN32` (`:267`) in core renderer code, with an `#else` throw (`:276-279`). It compiles on GCC but throws "Live shader compilation currently requires Windows" at runtime, even for a POSIX contributor who has implemented `StartBuildProcess`. It is reached only through `--watch-shader` / `VulkanContext::WatchShaders` (`VulkanContext.cpp:832-835`), because the default mode enables watching only when `GameBuild::Enabled` is true (`RavenEngine.cpp:98-104`).
  - `Private/Platform/BuildProcess.hpp:10-11`: `std::vector<std::wstring>` arguments. The callers build wide strings: `ShaderLibrary.cpp:154-159` and `GameModule.cpp:554` (`{ L"-P", (directory / "BuildGame.cmake").wstring() }`).
  - `Private/Platform/UnsupportedGameModule.cpp:11-15`: build-process stubs live in the loader stub file. The real implementation is in `Win32GameModule.cpp:105-189`. `cmake/RavenPlatform.cmake:46-53` adds that stub file for every non-Windows build, so "your loader backend replaces `UnsupportedGameModule.cpp`" would also delete the build-process stubs, a seam a contributor has to discover alone.
  - `cmake/GameBuildConfig.hpp.in:6-10` and `ShaderWatchConfig.hpp.in:5-10`: `wchar_t` constants. These compile on POSIX; this is hygiene.
  - `cmake/RavenGameModule.cmake:4, 13-14, 22`: the `RAVEN_GAME_DEFAULT` is WIN32/MSVC/Ninja only (`:3-7`), a `FATAL_ERROR` fires unless Windows, MSVC and Ninja (`:13-14`), and `:22` calls `raven_capture_msvc_environment`.
  - `cmake/RavenBuildEnvironment.cmake:3-13`: captures MSVC `INCLUDE`/`LIB` variables only.
  - `Private/Game/CMakeLists.txt:3, 24, 32`: a WIN32 gate, `/W4 /WX /utf-8` and `$<TARGET_PDB_FILE>`.
  - `RavenEngine.cpp:109, 151-153` with `Application.cpp:70-71` and `GameModule.cpp:590-591`: `gameEnabled` leads to `config.GameModulePath`, Application constructs a `GameModule`, and that constructor throws when `GameBuild::Enabled` is false. So `--free-fly`, `--game` and `--watch-game` cannot run off Windows.
  - `docs/PlatformHandoff.md:58, 62-63`: "Each platform supplies only the backends below" and "your loader backend replaces `UnsupportedGameModule.cpp`". The handoff does not tell contributors to avoid shared edits (`:15` gives them the shared CMake files, and `:17` asks them to coordinate shared API changes); the understatement is `:58` with `:62-63`.
- **Why it matters:** the M4 exit (`ROADMAP.md:131`, "every sample and every reload path on all three platforms") and the M6 exit (`:157/:159`, Courtyard and the 3D game on all platforms) require edits to shared core and CMake that the handoff does not mention. A Linux compile job will **not** catch these, because the game module defaults OFF off Windows and the rest compiles. Every gate fails loudly with an explicit "currently requires Windows" message, nothing is broken on Windows, and `ROADMAP.md:145` already says macOS and Linux follow through their M4 backends: this is planning debt with cheap seam fixes, not a hazard to the v1 goals.
- **Fix:** Windows-side snippets. Steps 1-4 and the MSVC half of step 5 are verifiable by the current Windows CI.
  1. `BuildProcess` takes UTF-8 `std::vector<std::string>`; the Win32 backend converts. Change both call sites (`ShaderLibrary.cpp:154-159`, `GameModule.cpp:554`).
  2. Move the Win32 process code to `Win32BuildProcess.cpp` and its stubs to `UnsupportedBuildProcess.cpp`, and update the explicit non-Windows source list in `RavenPlatform.cmake:47-52`.
  3. Delete the `ShaderLibrary` `#ifdef`, but keep an explicit up-front check on the platform side, such as a `Platform::SupportsBuildProcesses()` query or a probe that `Watch()` makes before subscribing. Without it, `Watch()` would succeed, create nine `Program` subscriptions each with its own scratch directory, and fail each first compile on a ReloadService worker (`StartPaused` false with no baseline, `ShaderLibrary.cpp:126-132`), giving repeated logged failures instead of today's one synchronous error at startup.
  4. Move the config constants to UTF-8, and route every use as a path (for example `GameBuild::BuildDirectory`, `ModuleFilename`, `ShaderWatchConfig::SourceDirectory`) through the existing `PathFromUtf8` (`Core/PathText.hpp`). On MSVC, `std::filesystem::path(const char*)` uses the ANSI code page even with `/utf-8`, so install paths with non-ASCII characters would otherwise regress on Windows.
  5. Make the game-module CMake compiler-neutral, in two parts:
     - **Now, MSVC-guard refactor only:** a shared `raven_apply_warnings(target)` (ARCH-BLD-01) with its MSVC branch; environment capture becomes a dispatcher with an `if(MSVC)` branch; emit the PDB line only for MSVC.
     - **With or after the informational Linux job (ARCH-PLAT-02), or by the M4 contributor:** the GCC/Clang branches, removing the WIN32 gate in `Game/CMakeLists.txt:3`, and relaxing `RavenGameModule.cmake:13-14` (with the default at `:4`) to "single-config Ninja" once a loader backend exists. Writing these branches from Windows without a job that compiles them would repeat the "not compiled on this machine" pattern.
  6. Rewrite PlatformHandoff's "Hot reload since M5" scope (`:58, 62-63`): the POSIX build process (posix_spawn into a process group, killpg on destroy) as a separate seam from the loader, the dlopen loader and visibility flags (ARCH-PLAT-03), DXC `-MF`.
- **Effort:** M
- **Timing:** steps 1-4 and the MSVC part of step 5 now / before M6; the non-MSVC part of step 5 with ARCH-PLAT-02; the backends are M4 contributor work
- **Panel discussion:** none in round 2. Verification split step 5, because the non-MSVC branches cannot be verified by Windows CI.
- **Verification:** confirmed with corrections; severity high -> medium because every gate fails loudly, nothing is broken or silently wrong on Windows, and the roadmap already routes macOS and Linux through M4, so the defects are an understated handoff and a few cheap seams.

#### ARCH-PLAT-02: Linux and macOS have no compile signal; they do not link, and Windows-only code accumulates unseen

- **Severity:** medium
- **Kind:** process
- **Sources:** PLAT-3, BLD-1, PROC-2 (CI part)
- **Evidence:**
  - `.github/workflows/windows.yml` is the only workflow (the `.github/workflows` directory holds only this file). Line 19 is `runs-on: windows-2025` and line 24 the `[x64-debug, x64-release]` matrix.
  - `Private/Assets/AssetId.cpp:26` calls `Platform::FillRandomBytes` (declared in `Private/Platform/PlatformRandom.hpp:9`). Its only definition is `Platform/Windows/Win32Random.cpp:12`, so executables and tests fail to link off Windows. It is the only `Platform::` symbol without a non-Windows definition; BuildProcess, GameModulePlatform, AudioBackend, AtomicFile, DirectoryWatch and PlatformPaths all have one. On Linux, no CMake file sets `--no-undefined` or `-z defs`, so `libRavenCore.so` itself still links; on macOS, ld64 defaults to `-undefined error` for dylibs, so `libRavenCore.dylib` itself fails too. Acknowledged in `docs/PlatformHandoff.md:9` since 2026-10-14 (f8e93b6) and already tracked at `TODO.md:74` and `ROADMAP.md:122`.
  - Git history: the last contributor (non-owner) commits to `Private/Platform/Mac` and `Linux` are dated 2026-10-10. Owner commits after that also touched these folders without compiling them: a99fbfa (2026-11-21) and 6b12ed6 (2026-11-22, "The Xcb and Cocoa edits are not compiled on this machine").
  - `cmake/RavenBuildSettings.cmake:52-59`: the private PCH (`RAVEN_USE_PCH`, ON by default) force-includes std headers on MSVC, GCC and Clang alike, masking missing includes on every compiler.
  - More Windows-only code that no CI sees: `ShaderLibrary.cpp:267` `#ifdef _WIN32` and the `std::wstring` `BuildProcess` API (ARCH-PLAT-01).
  - Cross-platform CI is already an M4 item (`ROADMAP.md:127`, "CI builds and CPU tests on Windows, Linux and macOS").
- **Why it matters:** goal 1. M6 will add serialization, migration and shader code to shared code. Each addition can add drift that a later contributor has to unwind. Nothing is broken on the primary target, and the only known break is documented and tracked; the evidence shows unverified drift, not a compile break beyond that symbol. The finding's contribution is a timing argument: an informational job now bounds the drift cheaply.
- **Fix:** one new owner decision (Q10): run an informational Linux job **now** rather than in M4. The random-byte hook (`ROADMAP.md:122`), three-OS CI (`ROADMAP.md:127`) and the macOS job remain the existing M4 items and are not re-listed here.
  1. Add an **informational, non-required** `ubuntu-24.04` job (GCC 13):
     - install the Vulkan SDK through the same action (provides DXC and `spirv-val`, which a RavenCore-only build still needs, because `add_dependencies(RavenCore RavenContent)` compiles the shaders) plus `libxcb`/`xcb-xkb` dev packages;
     - configure `linux-debug` with `RAVEN_USE_PCH=OFF`;
     - build `--target RavenCore` until Linux `FillRandomBytes` exists, then the full build and `ctest -LE gpu`. The interim RavenCore-only signal is narrower than a full build: it skips the samples, tools and test sources, where some Windows-only constructs live (for example `Samples/RavenEngine.cpp:146` `#ifdef _MSC_VER`).
  2. Only code that this Linux job compiles and checks may land from the Windows side. Linux `FillRandomBytes` (getrandom with EINTR and partial-read handling) is already specified in `PlatformHandoff.md:82, 135`, so it may land with the job, or stay with the M4 contributor (owner choice in Q10). The macOS implementation stays with the M4 contributor or the macOS job; the handoff names `SecRandomCopyBytes` plus the Security framework (`PlatformHandoff.md:82, 90`), and any change of provider is an owner decision with a handoff update.
  3. Make the job required only once it is green, and never in a way that blocks a Windows milestone.
  - Optional hygiene, not part of the fix: `find_package(Threads REQUIRED)` and `Threads::Threads` fix no observed defect (not a break on glibc 2.34+).
- **Effort:** S
- **Timing:** now (subject to owner decision)
- **Panel discussion:**
  - DOCS's "M5-Win" naming proposal was withdrawn: ROADMAP.md:133/145 already says "done on Windows" and "follow through M4", and the rename would conflict with the owner's platforms-follow-Windows framing.
  - The `std::jthread` concern was reframed as a toolchain floor (ARCH-PLAT-06), not a code change.
  - Round 2 proposed landing both `FillRandomBytes` implementations from the Windows side, with macOS using `getentropy` to avoid the Security framework, and a `macos-15` job after it. Verification withdrew these: landing macOS code that no job compiles repeats the "not compiled on this machine" pattern, `getentropy` departs from the documented handoff, and the macOS job and random-byte hook are existing M4 items.
- **Verification:** confirmed with corrections; severity high -> medium because nothing is broken on Windows, the only known break is one documented and tracked symbol, and three-OS CI is already a scheduled M4 item, so the gap is process timing rather than an architectural defect.

#### ARCH-PLAT-03: ELF module semantics are unspecified; GNU unique symbols would bind generation N to generation 1's `ComponentInfo`

- **Severity:** medium
- **Kind:** architecture-gap
- **Source:** PLAT-2 (re-rated, premise corrected)
- **Evidence:**
  - `Public/Raven/Core/Api.hpp:10, 20`: `visibility("default")` macros.
  - No visibility settings, `-fno-gnu-unique` or `-Bsymbolic` in any CMake file (grep).
  - `Component.hpp:174-179`: a class-template static `Info`, odr-used by `const&` (`Registry.hpp:134-159`). GCC emits these as `gnu_unique_object`, which marks the module NODELETE and resolves later generations to the first copy.
  - `Registry.cpp:150-158, 424-433`: identity is by value, so this is harmless today. The contract forces a restart on any layout change.
- **Why it matters:** once H6 relaxes the contract, generation N would pass generation 1's Size and `LayoutHash` with new-size values: an over-read or truncation on Linux/GCC only. Copying descriptors at activation does not protect against this, because the module's own registration code would read the unified `Info`.
  - Correction: the round-1 claim that `RavenCore.so` exports the interaction components' `Info` is wrong. Samples compile into the `RavenEngine` executable, which exports nothing.
- **Fix:**
  1. Set the game module to `CXX_VISIBILITY_PRESET hidden` with `VISIBILITY_INLINES_HIDDEN`, plus `-fno-gnu-unique` for GCC, passed through `BuildGame.cmake.in`. The visibility properties are no-ops on MSVC and can land now with the MSVC part of ARCH-PLAT-01 step 5. The GCC-only `-fno-gnu-unique` lands with the non-MSVC part of that step, with or after the informational Linux job, so it is compiled where it is written.
  2. Use hidden visibility for RavenCore too, so missing `RAVEN_API` annotations show up off Windows.
  3. The POSIX loader uses `dlopen(RTLD_NOW|RTLD_LOCAL)`, and after `dlclose` probes with `RTLD_NOLOAD`, logging a warning if the module is still resident.
  4. Record in HotReloadDesign that the host never relies on a generation's statics being reset, and that H6's narrowed contract must not be enabled on a platform whose loader lacks step 1.
  5. Document that macOS cannot unload dylibs containing TLV or Objective-C.
  6. On the first Linux build, check with `readelf --dyn-syms -W` for `UNIQUE`.
- **Effort:** S
- **Timing:** visibility flags now; `-fno-gnu-unique` with the Linux job or M4; hard precondition before H6 is enabled on Linux

#### ARCH-PLAT-04: The cross-platform verification checklist is a Win32 PostMessage driver

- **Severity:** medium
- **Kind:** architecture-gap
- **Source:** PLAT-5
- **Evidence:**
  - `scripts/Test-Windows3DLive.py:19, 31, 110, 523, 535`: user32, `PostMessageW`, hard-coded `.exe`/`.dll` names.
  - `docs/PlatformHandoff.md:65`: calls this script "the checklist for a native port".
  - `RavenEngine/CMakeLists.txt:44-53`: `Windows3DSmoke` is built only when the game is enabled.
- **Fix:**
  1. Split the script into an OS-neutral case list and runner plus a per-OS driver module.
  2. Derive binary names from the platform.
  3. An engine-side replay input source is an optional owner decision.
- **Effort:** M
- **Timing:** M4

#### ARCH-PLAT-05: The macOS and Linux input backends lag the current input contract

- **Severity:** low
- **Kind:** defect
- **Source:** PLAT-6 (re-rated)
- **Evidence:**
  - `CocoaWindow.mm:158` and `XcbWindow.cpp:331`: the live `InputState` is mutated directly instead of through `InputBuffer` (`InputBuffer.hpp:9-29`), which breaks with several windows (`PlatformHandoff.md:195`).
  - `CocoaInput.mm:195-200` and `XcbWindow.cpp:391-407, 514-519`: repeats are dropped, never reported through `RepeatKey`.
  - `XcbWindow.cpp:525-527`: FOCUS_OUT only. Neither backend calls `SetFocused`.
  - `XcbWindow.cpp:466-477`: duplicates `SetCursorPosition`.
  - The gap came from 869ea05, and it is already listed as unchecked M4 work at `PlatformHandoff.md:195-196`.
- **Why it matters:** on Linux and macOS, `WasKeyRepeated` is always false and `Focused` always true. Behavior is degraded, not broken.
- **Fix:** M4 contributor task. Keep it listed in the refreshed handoff (DOC Phase A8).
- **Effort:** S per backend
- **Timing:** M4

#### ARCH-PLAT-06: No macOS toolchain floor is recorded

- **Severity:** low
- **Kind:** process
- **Sources:** BLD-1 (jthread part), PLAT round-2 answer
- **Evidence:**
  - `CMakePresets.json:58-69`: `macos-base` sets only `/usr/bin/clang++`.
  - `docs/MacOSBuild.md:7`: "C++23 support", no version.
  - `stop_token` is part of internal interfaces (`DirectoryWatch.hpp:24`, `ReloadService.hpp:71`) and `stop_callback` (`ReloadService.cpp:383`). libc++ kept these experimental before roughly LLVM 18-20.
- **Fix:**
  1. Record Xcode 16+ in `MacOSBuild.md`, and pin the macOS CI image to `macos-15`.
  2. Do not rewrite to `std::thread` plus `stop_source`, which would be throwaway.
- **Effort:** S
- **Timing:** now (docs) / with the macOS job

#### ARCH-PLAT-07: The POSIX atomic save is atomic but not durable

- **Severity:** low
- **Kind:** defect
- **Source:** PLAT-9
- **Evidence:**
  - `Private/Platform/PosixAtomicFile.cpp:5-8`: rename without fsync.
  - `Private/Core/Files.cpp:66-72`: only `flush()`.
  - `Win32AtomicFile.cpp:17`: Windows uses `MOVEFILE_WRITE_THROUGH`.
- **Fix:** fsync the temporary file (`F_FULLFSYNC` on macOS) before rename, then fsync the parent directory.
- **Effort:** S
- **Timing:** M4

---

### 4.9 Build, CI and tooling (BLD)

#### ARCH-BLD-01: Warning-level asymmetry, and the M0 warning item has stayed open through M1-M5

- **Severity:** medium
- **Kind:** defect
- **Sources:** BLD-3, PROC-6
- **Evidence:**
  - `CMakeLists.txt:1`: CMake 3.24, with no warning flag on any engine target (grep). Local `build.ninja:95` shows no `/W` on RavenCore, so the effective level is MSVC's default `/W1`.
  - `Private/Game/CMakeLists.txt:24`: `/W4 /WX /utf-8` on RavenGame only.
  - About 17 of 53 public headers are compiled at `/W4` (through game includes).
  - `RavenEngine/CMakeLists.txt:14`: Vulkan includes are not SYSTEM.
  - `ROADMAP.md:64` and `TODO.md:35`: the item is open.
- **Why it matters:** the first agreed M6 step edits `Component.hpp`, which is compiled at `/W4 /WX` only in the game. New warnings would surface as failed hot-reload builds needing a host fix and restart, while the full build reports success.
- **Fix:**
  1. A `raven_apply_warnings(target)` function: `/W4 /utf-8` on MSVC now. Its GCC/Clang branch (`-Wall -Wextra -Wpedantic`) lands with or after the informational Linux job, as the non-MSVC part of ARCH-PLAT-01 step 5, so it is compiled where it is written. Apply it to RavenCore, RavenEngine, RavenSceneTool, the tests and RavenGame.
  2. Make the Vulkan include SYSTEM, or drop it.
  3. Fix warnings one folder per snippet.
  4. `/WX` in CI only, through `RAVEN_WARNINGS_AS_ERRORS`.
  5. A `RavenPublicHeaders` object library: one TU per public header, at the game's warning level. It will expose that `Window.hpp:9` pulls `<vulkan/vulkan.h>` into game TUs.
  6. The owner can count `/W4` warnings in an ignored configure first.
- **Effort:** M
- **Timing:** now, and before M6-2
- **Panel discussion:** `/permissive-` was dropped from the proposal. `-std:c++23preview` already implies it on MSVC, so there is no extra fix-up (documented behavior, not built).

#### ARCH-BLD-02: Debug builds of the game DLL carry no compiler debug info; the debugger workflow is unverified

- **Severity:** medium
- **Kind:** defect
- **Sources:** BLD-10, HR-9
- **Evidence:**
  - `CMakeLists.txt:1-9`: `if(POLICY CMP0141)` sets `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT`. With CMake 3.25+ the policy is NEW, and `/Zi` leaves the default Debug flags.
  - `Private/Game/CMakeLists.txt:1`: the child requires 3.24, so CMP0141 is OLD there and the variable is ignored.
  - `BuildGame.cmake.in` passes the host's `CMAKE_CXX_FLAGS_DEBUG`, which has no `/Zi`.
  - Local `GameBuild/CMakeCache.txt:31` and `build.ninja:54`: no `/Zi` or `/ZI`.
  - Local `RavenGame.pdb` references game sources only as object names.
  - Locally, the cache was seeded by a different CMake than the one recorded, and engine C++ TUs also lack `/Z` flags.
  - `docs/EngineModules.md:91`: no debugger session has been rerun. ROADMAP M7 schedules the breakpoint check last.
- **Why it matters:** fast iteration. Breakpoints in game code cannot bind, and HR-9's PDB-lock question cannot be tested until this is fixed.
- **Fix:**
  1. Raise `cmake_minimum_required` to 3.25 in the root and in `Game/CMakeLists.txt`, drop the policy guard, and update `MacOSBuild.md:7`.
  2. Set `MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>"` on RavenGame (`/Zi`, not `/ZI`). Expect one host restart, because the contract hashes flags.
  3. The owner reconfigures `x64-debug` from scratch with one CMake.
  4. Run one auto-build reload with the Visual Studio debugger attached and record the result. If the PDB lock blocks the link, give each build a unique PDB name and sweep old ones like ARCH-HR-03.
- **Effort:** S
- **Timing:** now

#### ARCH-BLD-03: The GPU-free reload-recipe checks and `Raven3DSmoke` are never built or run in CI

- **Severity:** medium
- **Kind:** process
- **Source:** BLD-4 (confirmed as wiring only)
- **Evidence:**
  - `RavenEngine/CMakeLists.txt:45`: `Raven3DSmoke` is `EXCLUDE_FROM_ALL`.
  - `scripts/Test-Windows3D.ps1:24-93`: the edited-body build and the "failed build preserves last-good DLL" check need no GPU, and line 29 builds the smoke.
  - `Test-Windows3D.ps1:45-63`: brittle string surgery on generated text. It fails loudly.
  - `.github/workflows/windows.yml:50-51`: CI runs only `RegistryTests`.
- **Fix:**
  1. Add a `-BuildOnly` switch that returns after line 93, and run it in CI on `x64-debug` (an estimated 30-90 s).
  2. `BuildGame.cmake.in` accepts `-DRAVEN_GAME_ROOT_OVERRIDE`, `_BUILD_DIR_OVERRIDE` and `_OUTPUT_OVERRIDE`, replacing the string surgery.
  3. No new tracked test; this is the verification AGENTS.md already requires when the build cache changes.
- **Effort:** S
- **Timing:** now

#### ARCH-BLD-04: The engine shader program table is duplicated in CMake and C++, and there is no cook path for M6 shader assets

- **Severity:** medium
- **Kind:** architecture-gap
- **Source:** BLD-6 (direction confirmed by RENDER)
- **Evidence:**
  - `cmake/RavenContent.cmake:49-60` and `ShaderLibrary.cpp:39-47`: the same nine programs in two lists.
  - `RavenContent.cmake:95-101`: only build-compiled SPIR-V is staged.
- **Fix:**
  1. Keep one data file (`Programs.txt`: name, profile, entry), read by CMake and used to generate `ShaderPrograms.hpp`.
  2. The engine programs stay build-time code.
  3. M6 per-material shaders are fragment-only programs against `Raven/Material.hlsli`; `LitVertex` stays engine-owned.
  4. Cook them with the same `CompileShader.cmake`, so players load SPIR-V only and no DXC ships.
- **Effort:** S
- **Timing:** step 1 before M6; steps 3-4 in the M6 design (D-I)

#### ARCH-BLD-05: RavenCore target-depends on shader compilation and content staging

- **Severity:** low
- **Kind:** defect
- **Source:** BLD-7 (clarified)
- **Evidence:**
  - `cmake/RavenContent.cmake:124`: `add_dependencies(RavenCore RavenContent)`.
  - `StageContent.cmake.in:1`: staging goes to `$<TARGET_FILE_DIR:RavenEngine>`, which is also the tests' directory (`Tests/CMakeLists.txt:26`, inside `raven_add_test_executable` at `:20-27`).
  - Local `build.ninja:91, 1072, 1110`: tests get staging only transitively.
- **Fix:** in one snippet, move the edge to RavenEngine **and** to `raven_add_test_executable` and `Raven3DSmoke`. Otherwise a test-only build loses its shaders.
- **Effort:** S
- **Timing:** any time

#### ARCH-BLD-06: Release builds embed developer paths; dead x86 presets; CI actions pinned by tag

- **Severity:** low
- **Kind:** defect
- **Source:** BLD-8
- **Evidence:**
  - `cmake/ShaderWatchConfig.hpp.in:5-10`, `GameBuildConfig.hpp.in:8-10`, `ContentSourceConfig.hpp.in:5`.
  - `CMakePresets.json:48-56, 97-112, 148-149`.
  - `.github/workflows/windows.yml:30, 36`.
- **Fix:**
  1. Delete the x86 presets.
  2. Pin actions to commit SHAs.
  3. Guard the development configs out of player builds.
- **Effort:** S
- **Timing:** steps 1-2 now; step 3 in M7

#### ARCH-BLD-07: There is no sanitizer policy

- **Severity:** low
- **Kind:** process
- **Source:** BLD-11
- **Evidence:**
  - `ROADMAP.md:50`.
  - MSVC ASan conflicts with `/ZI` and `/RTC1`.
- **Fix:** when the Linux job exists, add `RAVEN_SANITIZE` (ASan and UBSan on RavenCore and the tests) as one matrix entry running `ctest -LE gpu`. Revisit MSVC ASan before M6-8 if Linux CI is still absent.
- **Effort:** S
- **Timing:** with ARCH-PLAT-02

---

### 4.10 Documentation and process (DOC)

#### ARCH-DOC-01: M6 depends on unscheduled steps and has a vague exit

- **Severity:** medium
- **Kind:** process
- **Sources:** PROC-1 (order corrected), DOCS and CORE round 2
- **Evidence:**
  - `ROADMAP.md:147-159`: the M6 heading (`:147`), a flat item list (`:149-157`) and a vague exit (`:159`: "a small 3D game, the stress sample and the 2D sample run on all platforms using only engine APIs"), naming no sample and giving no checklist.
  - `ROADMAP.md:75`: the M1 command buffer is deferred "until gameplay first spawns or despawns at runtime" (owner, 2026-11-21). It has an event trigger but no milestone, and the M6 list does not include it; `TODO.md` moves M6 tasks in only "when work starts".
  - `docs/HotReloadDesign.md:224`: "After M6: the host stops compiling game components". `ROADMAP.md:151` makes field migration depend on it, but no ROADMAP item moves game components or samples out of the host, so the precondition is unscheduled. `HotReloadDesign.md:221` confirms the host compiles them today.
  - `RavenEngine/Private/Game/InteractionGame.cpp:38-40` (game-module code): the only gameplay that could despawn sets `Collected = true` and dims the `Tint`, the designed behavior per `docs/InteractionGame.md:80`, so nothing yet triggers the deferred command buffer.
  - `PerspectiveLevel.cpp:2` includes `Assets/AssetPath.hpp` and `PerspectiveSample.cpp:2` includes `Core/FrameTrace.hpp`, both engine-private headers under `RavenEngine/Private/`, which conflicts with the "only engine APIs" exit.
  - The project already writes a design note with numbered owner decisions before each milestone's code: M1-M5 each have a "Write the design note" item in `TODO.md`, and AGENTS.md states the rule for M1.
- **Why it matters:** the M6 exit cannot be checked, and two preconditions of M6 items have no owner. Building M6 items in list order risks building some of them twice; that is a projection, and the existing note-first process already guards against much of it. The new value here is narrow: an exit checklist and scheduling the orphaned preconditions.
- **Fix:**
  1. Write the M6 design note (owner decisions D-A to D-I, section 8) before any M6 code, as was done for M1-M5. Use the sequence in section 6.
  2. Schedule the two orphaned preconditions explicitly: the command buffer (`ROADMAP.md:75`, M6-7 in section 6) and "the host stops compiling game components" (`HotReloadDesign.md:224`), including the step that removes the samples' `Private/` header includes (`PerspectiveLevel.cpp:2`, `PerspectiveSample.cpp:2`). Without that step, the "engine APIs only" exit and the H6 field-migration item cannot be met.
  3. Replace "a small 3D game" with a named, scoped sample and an exit checklist. Core items: one material shader, one bound action, a save/load round trip, engine APIs only (with the step above). Two candidate items add scope beyond the current M6 list and are owner proposals, not fixed parts of the exit (Q16): spawn/despawn through Commands (pulls the deferred M1 command buffer into M6) and overlapping sounds (from ARCH-ASSET-04; audio is not an M6 item in `ROADMAP.md`).
- **Effort:** S (the note)
- **Timing:** before M6
- **Panel discussion:** verification credited the existing note-first practice and moved the two scope-adding checklist items into an owner question.
- **Verification:** confirmed with corrections; severity high -> medium because the project already writes a design note with owner decisions before every milestone, so what remains is an S-effort docs and process gap (exit checklist, two orphaned preconditions) that matters only before M6 starts.

#### ARCH-DOC-02: Entry-point and subsystem docs contradict the code

- **Severity:** medium
- **Kind:** doc-drift
- **Sources:** PROC-3, PROC-7, PROC-8, ECS-10, RND-7, ASSET-6, HR-7 (doc part), PLAT-6 (doc part), BLD-9, CORE-10, PERF-4 (doc part)
- **Evidence:** listed per item in the docs pass below.
- **Why it matters:** the M7 exit ("from the documentation alone") and onboarding of M4 contributors.
- **Fix:** one docs pass in two phases.

**Phase A (now, `*.md` only, one docs commit):**

- **A1.** `TODO.md:50, 89`: retitle M2 "(next)" and M5 "(current)" as done.
  - Check `ROADMAP.md:90` as completed in M3 (`ObjectData.Material/Group`).
  - Link `docs/SpriteBatchingResearch.md` (no inbound links) from the M6 sprite item, marked historical.
- **A2.** `ROADMAP.md:105`: fixed-count indirect everywhere (Q4).
- **A3.** `README.md`: fix the false lines.
  - `:13` and `:23` describe a virtual component parent; the base is empty and non-virtual.
  - `:59`: every program is watched now.
  - `:95` says "no registered CTest tests".
  - Link the design notes and the tracked current-state section at `ROADMAP.md:40-50`. Do not copy the gitignored AGENTS.md block. The full rewrite stays in M7.
- **A4.** `docs/ECSDesign.md`:
  - `:76` field table and ComponentRef: planned.
  - `:93`: the hash covers names plus sizeof/alignof.
  - `:118` `Values<T>`: not implemented.
  - `:210` declared system access: deferred, to agree with `:155`.
  - `:332` per-type hooks: not supported, consistent with `EngineModules.md:374`.
- **A5.**
  - `docs/TextureResources.md:25, 67, 100, 104, 116`: add a "Superseded by RendererDesign.md (M2)" banner and correct the lifetimes.
  - `docs/DevelopmentReload.md:27`: dynamic rendering, not a render pass.
  - `GpuDrivenDesign.md` G4: mark the non-bindless fallback as an open owner decision (section 8, Q12).
- **A6.**
  - `docs/AssetSystem.md:65, 91, 111`: move to MilestoneHistory.
  - `AssetSystem.md:61`: AssetStore leases; AssetResources is "legacy, unused".
  - `docs/InteractionGame.md:62`: 64 MiB and 262,144 (`SceneBinary.hpp:15-17`).
- **A7.**
  - `docs/EngineModules.md:60`: the game uses a PCH (`Game/CMakeLists.txt:27`).
  - `:568`: the settle worker was replaced by ReloadService.
  - `:117` and `:543`: historical, with dates.
  - Add one line stating the current per-target warning flags; update it in the ARCH-BLD-01 commit.
- **A8.** `docs/PlatformHandoff.md`:
  - `:3, :7`: the date, and C++23.
  - `:9`: the input claim.
  - `:58, 62-63`: "only the backends below" understates the work; shader watch is a compile-time `#ifdef`, the build-process stubs share the loader stub file, and the full ARCH-PLAT-01/03 scope.
  - Add `Platform::SleepUntil` to the backend list once ARCH-PERF-01 lands.
  - The input-contract delta (ARCH-PLAT-05).
  - Point `MacOSBuild.md:3` at the handoff and record the Xcode floor.
  - `BuildProcess` is documented as-is, with "UTF-8 change proposed", until ARCH-PLAT-01 step 1 lands.
- **A9.** `TODO.md:23`: replace the "not planned" test list with the accepted guards (section 6) once the M6 note is approved.

**Phase B (in the same commit as the code it describes):**

- **B1.** AssetResources removal text (AssetSystem, SceneWorkflow:92, HotReloadDesign:19, README:57, TextureResources:162, PlatformHandoff:84) lands only in the ARCH-CORE-05 deletion commit.
- **B2.** Comments in Public headers are **engine source edits**: they need explicit owner approval, and they change the contract hash (`GameContract.cmake:5-12`). Each one rides with the first approved snippet touching that header:
  - `RenderWorld.hpp:17-18`
  - `StaticMeshComponent.hpp:11`
  - `AssetStore.hpp:70`
  - `VulkanContext.hpp:84`
- **B3.** `GpuDrivenDesign.md:66` takes the measured split from ARCH-PERF-02.
- **B4.** The local, gitignored AGENTS.md lines on SceneAccess Has/Add/Remove and on hooks are fixed by the owner together with A4.

- **Effort:** S
- **Timing:** Phase A now; Phase B with the code

#### ARCH-DOC-03: Verification baselines exist on one machine only

- **Severity:** low
- **Kind:** process
- **Source:** PROC-5 (step 3)
- **Evidence:** `TODO.md:54` and `docs/RendererDesign.md:150`: references are under ignored `out/captures/m2-reference`.
- **Fix:** a small tracked manifest of per-capture SHA-256 hashes, so other machines can detect drift.
- **Effort:** S
- **Timing:** before M6-9

## 5. Cross-cutting themes

1. **The engine lacks its own host layer.** Frame-pipeline stages, world ownership, scene instantiation and reload ordering live in sample clients and are copied in the smoke harness. Many findings trace back to this:
   - the missing exception boundary (RT-05),
   - the M6 "engine APIs only" exit (CORE-01, CORE-02),
   - the RenderWorld ABA becoming live (RND-03),
   - the Commands flush point (ECS-01).

   Building the session layer first, as moves rather than rewrites, makes several later steps routine.
2. **M6's data model must be decided before it is coded.** The field table, persistent identity, the hierarchy representation, the generic scene format, removing sample concepts and material identity are mutually dependent (ECS-01 to ECS-04, ASSET-01/02, CORE-02, RND-01/02). Under the no-throwaway rule, their order matters more than any single fix. Section 6 gives one order, and section 8 lists the decisions.
3. **Host stages must never throw on data written by the game.** The rule for every host stage: data errors produce placeholders or hidden objects plus a once-per-item log; only programmer errors throw; the boundary rebuilds derived state and then suspends (RT-01 to RT-05). Apply the same rule to every M6 stage: async asset resolution, Commands and migration.
4. **Implicit invariants that M6 will break.** Several things are correct today only by accident of current usage:
   - texture uploads drain the queue (RND-04);
   - scene leases only grow (RND-02, ASSET-03);
   - only one RenderWorld exists (RND-03);
   - all structural changes go through Scene (RT-03);
   - layout edits always force a restart (PLAT-03, ECS-02).

   Each must be made explicit before the M6 change that removes it.
5. **A Windows-only signal.** Platform code has not compiled since October, the game module is MSVC-only, the verification harness is Win32-only, and warnings differ between the engine and the game. A compile job alone will not catch the seam problems (PLAT-01), and the seam fixes alone will not stop new drift (PLAT-02). Both are needed.
6. **Evidence and documentation drift away from the code.** Goal-4 FPS numbers come from a patched host (PERF-01), a design-note cost claim is off by roughly an order of magnitude (PERF-02), and README contradicts the core ECS contract (DOC-02). The fix is process: exit records come from the stock binary, and a milestone commit updates every doc that states a contract it changes.
7. **Small, durable steps.** Nearly every recommendation decomposes into S snippets that the existing Windows CI can verify. The few M/L items (CORE-01, ECS-01/02/03, ASSET-01) are deliberately ordered so that each snippet is a move or an addition, never a temporary structure.

## 6. Prioritized fix plan

### 6.1 Table

| Priority | ID | Fix | Severity | Effort | Timing | Depends on |
|---|---|---|---|---|---|---|
| 1 | ARCH-RT-01 (steps 1-3) | Non-throwing normal matrix (owner picks public or private), hide singular objects, update `MeshMaterials.md:50` | high | S | now | none |
| 2 | ARCH-RT-02 | Shared non-throwing asset lookup for data errors gives a white placeholder or skipped sprite | medium | S | now | none |
| 3 | ARCH-RT-03 | Missing ancestor becomes a detached root in both world paths, changed flag propagates; port `MoveSprites` to `SceneAccess` | medium | S | now | none |
| 4 | ARCH-RT-04 | Wrap the registration call, add `catch(...)` | medium | S | now | none |
| 5 | ARCH-PERF-01 | Presentation options, harness cleanup, stock-host M3 re-run | medium | S-M | now | owner decision Q11 (default pacing) |
| 6 | ARCH-BLD-02 | Game debug info (CMake 3.25, `/Zi`), debugger reload check | medium | S | now | none |
| 7 | ARCH-BLD-01 | `raven_apply_warnings` on all targets, CI-only `/WX` | medium | M | now (before M6-2) | none |
| 8 | ARCH-BLD-03 | Reload-recipe `-BuildOnly` and smoke compile in CI | medium | S | now | none |
| 9 | ARCH-RND-03 | Process-wide RenderWorld generation | medium | S | now | none |
| 10 | ARCH-PLAT-01 | UTF-8 `BuildProcess`, file split, `#ifdef` replaced by an up-front platform check, MSVC-guard refactor of the game CMake (non-MSVC branches with the Linux job) | medium | M | now / before M6 | ARCH-BLD-01 (shared function); ARCH-PLAT-02 for the non-MSVC branches |
| 11 | ARCH-PLAT-02 | Informational Linux compile job now (Linux `FillRandomBytes` with it, if the owner chooses) | medium | S | now (owner yes) | owner decision Q10 |
| 12 | ARCH-PLAT-03 | Hidden visibility and `-fno-gnu-unique` for the game module | medium | S | now / M4 | ARCH-PLAT-01 step 5 (the GCC flag with its non-MSVC part, ARCH-PLAT-02) |
| 13 | ARCH-HR-01 | Watch the build record; fix the `--game` hint; F5 respects building | medium | S | now | none |
| 14 | ARCH-HR-04 (step 1) | Contract allowlist for `cmake/` inputs | low (part of a medium) | S | now | none |
| 15 | ARCH-HR-03 (step 1) | Sweep stale execution copies and shader scratch | medium | S | now | none |
| 16 | ARCH-HR-02 (steps 1-4) | Digests not bytes; log watch fallback; word-wise hash | medium | S | now | none |
| 17 | ARCH-CORE-05 | Delete the unmanaged resource API | low | S | now | none |
| 18 | ARCH-ECS-02 (step 6) | Log the silent layout mismatches, including `FindPool`'s mismatch branch | medium | S | now | none |
| 19 | ARCH-RND-05 (steps 1-2) | Bindless limit check, portability-subset log | medium | S | now | none |
| 20 | ARCH-RND-01 (step 1) | `FindGroup` only when added or the stored group mesh differs | medium (part) | S | now | none |
| 21 | ARCH-RND-02 (steps 1-2) | Document Tint; const material reads; white-reset freed element | medium | S | now | none |
| 22 | ARCH-DOC-02 (Phase A) | Docs pass, `.md` only | medium | S | now | none |
| 23 | ARCH-PERF-02 (step 1) | Dense tick walk in `ApplyTicks` | medium | S | before M6 | none |
| 24 | ARCH-PERF-03, ARCH-PERF-04 | Benchmark matrix, 50%/100% animated, single dirty copy | medium | S-M | before M6 | ARCH-PERF-01 |
| 25 | ARCH-DOC-01 | M6 design note with D-A to D-I, the exit checklist and the two orphaned preconditions scheduled | medium | S | before M6 | owner (Q16 for checklist scope) |
| 26 | ARCH-CORE-01 (S0-S2) + ARCH-RT-05 + ARCH-RT-01 (step 4) | Session type, `OnUpdate`/`OnRender` split with no shim, `PrepareFrame` Extract absorbed, engine PostUpdate/Extract, tiered boundary (data errors recoverable, `bad_alloc`/`logic_error` fatal), `Sync` transactional reset | high / medium | M | M6-1 | ARCH-RND-03, RT-01 to RT-04 |
| 27 | ARCH-ECS-02 | Typed field table, type tags in `LayoutHash`, module table registers components | medium | M | M6-2 | ARCH-BLD-01, M6 note (D-C) |
| 28 | ARCH-ECS-01 | Structural benchmark case first; `ParentComponent` under the chosen write policy, child lists, depth buckets, incremental spawn/destroy/reparent | medium | M | M6-3 (benchmark case now) | M6-1, D-A (incl. write policy), tick test |
| 29 | ARCH-ECS-03 | Persistent entity ID stored per entity, `EntityRef`, `AssetRef<MeshAsset>` resolved on change, `MeshHandle` out of components | high | M | M6-4 | M6-2, D-D |
| 30 | ARCH-CORE-03 | Scene/asset seam; `Pick` as a free function; HierarchyTests partly CPU | medium | M | M6-5 | M6-4 |
| 31 | ARCH-ASSET-01 | Generic chunked scene format (unknown sections kept), tolerant `raven-project 2`, converter, regeneration, delete the old readers | high | L | M6-6 | M6-2 to M6-5, D-H |
| 32 | ARCH-CORE-02 + D5 Commands | Relocate sample components; single `SystemContext` break with an engine play-state gate and a writable camera; Commands through Scene | high | M | M6-7 | M6-1, M6-3, M6-6, D-B, D-G; 2D run for Courtyard |
| 33 | ARCH-HR-04 (steps 2-3) | Generated input list; narrow the contract | medium | M | step 2 before M6-10; step 3 at M6-10 | M6-7, D-F |
| 34 | ARCH-ECS-04 + H6 | Field migration with stamp and resync rules | medium | M | M6-8 | M6-2, M6-7, ARCH-PLAT-03 on Linux |
| 35 | ARCH-RND-01/02, ARCH-RND-05 (3-4), ARCH-BLD-04 | (pipeline, mesh) groups with `CommandIndex`; material identity and leases; sprites; per-material shaders | medium | M | M6-9 | M6-6, D-I, ARCH-RND-06 2D run |
| 36 | ARCH-RND-04, ARCH-ASSET-02 | Per-frame upload batch; multi-object FBX (option A) | medium | M | M6-10 | M6-4, M6-6, D-E |
| 37 | ARCH-ASSET-03 | Async asset resolution with placeholders; journal-derived leases | medium | M | M6 | M6-4, M6-9 |
| 38 | ARCH-CORE-04, ARCH-ASSET-04, ARCH-CORE-09 | Key-binding contexts, audio voices, user-data path | medium / low | M | M6 (parallel) | M6 note |
| 39 | ARCH-HR-02 (steps 5-6) | Recursive-root DirectoryWatch contract, budgeted audit | medium | M | before M6-10 | step 1-4 |
| 40 | ARCH-HR-03 (steps 2-3), ARCH-BLD-06 (step 3), ARCH-ASSET-05 (cook) | In-place player load, F5 gating, release paths, cook | medium / low | S-M | before M7 | none |
| 41 | ARCH-PLAT-04, ARCH-PLAT-05, ARCH-PLAT-07 | Platform contributor work (alongside the existing M4 items) | medium / low | S-M | M4 | ARCH-PLAT-01 |
| 42 | Remaining lows | ECS-05/06/07, CORE-06/07/08, HR-05, PERF-05/06, BLD-05/07, DOC-03, ASSET-06, PLAT-06 | low | S | opportunistic / as listed | as listed |

### 6.2 Recommended sequencing

**Phase 0: now, before any M6 code.**

- Close the error boundary (priorities 1-4) and land the presentation options with the stock re-run (5). ARCH-RT-01 step 4 (the transactional `Sync` reset) waits for the ARCH-RT-05 boundary at M6-1.
- Restore the build safety net (6-8) and land the process-wide RenderWorld generation (9).
- Make the cross-platform seams real (10, 12), and add the Linux job if the owner agrees (11). The non-MSVC branches of the game CMake (ARCH-PLAT-01 step 5) and the GCC-only `-fno-gnu-unique` (ARCH-PLAT-03) land with or after that job, so they are compiled where they are written.
- Add the structural-change benchmark case (ARCH-ECS-01 step 0) so the M6-3 priority rests on a measurement.
- Do the small reload and renderer hygiene items (13-21) and the Phase A docs pass (22).
- All of these are S snippets that are independent of M6 decisions, and none is replaced later.
- The warning level must land before M6-2, because the field table edits `Component.hpp`.

**Phase 1: the M6 design note (ARCH-DOC-01).** Record D-A to D-I and the exit checklist, as was done for M1-M5, and schedule the two orphaned preconditions (the D5 command buffer and "the host stops compiling game components").

**Phase 2: M6 in this dependency order.**

| Step | Content |
|---|---|
| M6-1 | Engine frame pipeline and boundary (CORE-01 S0-S2, RT-05, RT-01 step 4) |
| M6-2 | Typed field table and module component registration (ECS-02) |
| M6-3 | Hierarchy as `ParentComponent` plus incremental index (ECS-01) |
| M6-4 | Persistent identity (ECS-03) |
| M6-5 | Scene/asset seam (CORE-03) |
| M6-6 | Generic scene format with converter-then-delete (ASSET-01) |
| M6-7 | Relocate sample components and remove the samples' `Private/` includes, single `SystemContext` contract break, D5 Commands through Scene (CORE-02) |
| M6-8 | H6 migration (ECS-04) |
| M6-9 | Render-world grouping and material identity, then sprites, then per-material shaders (RND-01, RND-02, BLD-04), after the owner-requested 2D run (RND-06) |
| M6-10 | Upload batch, then multi-object FBX (RND-04, ASSET-02); narrow the game contract (HR-04 step 3) |

Notes on the order:

- Key bindings (CORE-04), audio (ASSET-04) and the user-data path (CORE-09) run in parallel once the note is approved. CORE-01 S3-S4 (Application owns the session) can follow M6-1 at any time.
- If gameplay needs spawning earlier, Commands may move up to right after M6-3, because they need only M6-1 and M6-3.
- `GameBindings` stays frozen from now until M6-7.

**M4 (parallel, platform contributors).**

- Backends against the seams made real in Phase 0, alongside the M4 items already in `ROADMAP.md`: the dlopen loader with ARCH-PLAT-03 flags, the non-MSVC game CMake branches if not landed with the Linux job, the POSIX `Platform::SleepUntil` if a high-resolution timer is wanted (ARCH-PERF-01), POSIX build process, input delta, inotify/FSEvents against the recursive-root contract, durable POSIX saves, the OS-neutral live-test runner.
- Tell contributors now:
  - the loader seam is Load, Find and Unload only;
  - the audio backend contract is changing to pooled voices;
  - H6 must not be enabled on Linux without the visibility flags.

**M7.** In-place player load and F5 gating (HR-03), release path hygiene (BLD-06), cooked assets (ASSET-05), log sinks and error policy (CORE-06), tick epoch (ECS-07) if not done earlier.

### 6.3 Tracked tests under the minimal-test policy

The policy: add a test only when it guards behavior that an upcoming change rewrites. The panel accepts **two new tracked tests and one conditional**:

1. **CPU change-tick case in `RegistryTests`** (from BLD-5).
   - Covers: Add, Write, mutable `TryGet` and non-const views stamp; const access does not; `AdvanceChangeTick` ordering.
   - Why: the contract is rewritten or newly relied on by ECS-01, PERF-02, Commands and H6, and today only the GPU-labelled `HierarchyTests` touches ticks, so CI never checks it.
   - When: before M6-3.
2. **`LayoutHash` type-change guard plus migration case in `RegistryTests`** (from PROC-4 and ECS-02).
   - The type-change half is a compile-time `static_assert` on the constexpr hash. The migration half is H6 behavior: name and type match copies, new fields take defaults, type changes are rejected, and migrated elements are stamped.
   - When: lands with M6-2 and grows at M6-8.
3. **Conditional: `Tests/Gpu/RenderWorldTests.cpp`** (from RND-8).
   - Covers bookkeeping only: add and remove with swap-dirty, group change, material change, `GroupCounts` sum, and the zero-scale hidden case from RT-01.
   - Added just before M6-9. It becomes CPU if the M6-5 seam lets `RenderWorld::Sync` run without a renderer.

Also:

- The new generic-codec CPU round-trip test is already scheduled by ROADMAP.md:150, so it is not an extra test.
- **Rejected:** a round-trip test of the old RVN3DSCN codec (it guards code M6-6 deletes; verify the converter with an ignored probe instead) and a separate zero-scale line in `HierarchyTests` (the defect is in RenderWorld; folded into item 3).
- Benchmark additions (ARCH-PERF-03/04, ARCH-ECS-01 cases) are not CTest tests.
- CI wiring of existing scripts (ARCH-BLD-03) adds no tests.

## 7. Suggested roadmap and TODO adjustments (proposals for the owner)

These are proposals only. None is decided.

1. **M0:** move "Set /W4 and -Wall -Wextra" to an explicit "before M6" gate, and add "Debug game DLL has debug info; debugger reload verified" (ARCH-BLD-01, ARCH-BLD-02).
2. **M3 record:** add "re-run exit cases on the stock host after production presentation options" (ARCH-PERF-01). Add the presentation options as a scheduled item; the harness comment already expects it.
3. **M6:** replace the flat list with the dependency order in section 6.2. Add explicit items for:
   - the engine-owned frame pipeline and boundary,
   - "host stops compiling game components" (the H6 precondition),
   - D5 Commands,
   - the hierarchy representation,
   - persistent entity identity,
   - the Scene/asset seam,
   - the audio voice contract,
   - user-data paths,
   - key-binding contexts.
4. **M6 exit:** replace "a small 3D game" with a named sample plus a checklist (ARCH-DOC-01; checklist scope per Q16), and record the benchmark matrix (ARCH-PERF-04) as the performance exit.
5. **M4:**
   - Optionally pull an informational Linux compile-only CI job forward to now, without making it a gate for Windows milestones.
   - Rewrite the game-module task to "loader backend plus visibility flags; the shared seams are already portable" (after ARCH-PLAT-01; the non-MSVC CMake branches come with the Linux job or the M4 contributor).
   - Add the Xcode floor.
   - Resolve the G4 non-bindless fallback decision.
6. **M5 record:** note the known recovery gaps fixed in Phase 0 (RT-01 to RT-04), so the "last-good recovery" claim matches the code.
7. **M7:** add in-place player loading, release path hygiene and cooked assets. Keep "Refresh the README" but note that Phase A corrections land now.
8. **TODO.md:** fix the stale headings, replace the "not planned" test list with the accepted guards in section 6.3 once the M6 note is approved, and add the owner-requested Courtyard run before M6-9.

## 8. Open questions for the owner

Each question lists the options and the panel's recommendation.

- **Q1 (D-A). Hierarchy representation.**
  - (a) Scene side tables plus a child index.
  - (b) Parent, FirstChild and NextSibling as components.
  - (c) Hybrid: `ParentComponent` as the serialized truth, child lists and depth buckets as a Scene runtime index.
  - *Recommendation:* (c). It survives serialization and clone without serializing derived data, and the index code is identical regardless of where Parent lives.
  - *Sub-decision, write policy for (c):* (i) host-only writes through `Scene::SetParent`, with const-only access for game code (no per-frame reconciliation; cycles checked at one write site), or (ii) any writer, with Scene reconciling its index from Parent ticks every frame (generic editor writes need no special path, at the cost of a dense tick scan). *Recommendation:* (i) (ARCH-ECS-01).
- **Q2 (D-B). Replacement for GameBindings and GameUpdateResult.**
  - (a) Tag and singleton components, plus engine concepts (selected camera, `Picks` span), with gameplay result logic moved into systems. Camera selection is read-only, but the camera's Transform and `CameraComponent` stay writable for free-fly. An engine-defined play-state or mode (a session component, or the play state on Scene) replaces today's empty-binding gating.
  - (b) Generic named entity slots plus an opaque result.
  - *Recommendation:* (a), as one contract break at M6-7, with GameBindings frozen until then. The note must choose the play-state form.
- **Q3 (D-C). Field-table origin.**
  - (a) constexpr only.
  - (b) A separate `RavenRegisterComponents` export.
  - (c) constexpr table generated by `RAVEN_COMPONENT`, registered through the existing module table (`table.Component<T>()`), copied by value at activation and into pools at creation.
  - *Recommendation:* (c). One export, one contract change, and safe under ELF unique symbols once the PLAT-03 flags are set.
- **Q4 (D-D). Persistent entity ID.**
  - (a) A `u64` in a Scene-owned table.
  - (b) A UUID component.
  - *Recommendation:* (a) a `u64`, scene-scoped and Scene-owned, mapped from authored names at load. It is cheaper and enough for v1. Revisit if cross-scene references are needed.
- **Q5 (D-E). Sub-asset IDs for multi-object FBX.**
  - (a) Generated v4 IDs stored as `submesh` lines in `Project.ravenproject` v2.
  - (b) v5 IDs derived from node paths.
  - *Recommendation:* (a). Renames stay recoverable, and `AssetId::Parse` keeps v4-only.
- **Q6 (D-F). Game project layout for v1.**
  - (a) One in-tree game under `Private/Game`.
  - (b) Engine plus separate game project roots.
  - *Recommendation:* decide before M6-7. Either way, generate the watched-input list once from CMake. (b) aligns with M7's "clone, build, modify and package a game", but costs more now.
- **Q7 (D-G). Editor and play worlds; the Commands route.**
  - (a) One Registry per world, with scene IDs for scenes inside a world.
  - (b) A shared Registry with scene IDs.
  - *Recommendation:* (a). Isolation by construction, cheap per-pool clone, per-Registry ticks and journals. Commands go through Scene in all cases.
- **Q8 (D-H). Scene format conversion.**
  - (a) A one-time tool converter, then delete the old reader.
  - (b) Keep a legacy runtime decoder.
  - *Recommendation:* (a). Regenerate the six tool-generated scenes and convert the five hand-authored files. Please confirm that Puzzle and Collection were not hand-edited after generation.
- **Q9 (D-I). Material identity and shader cooking.**
  - *Recommendation:* key render-world materials by (asset identity, overrides), with entries holding texture leases. Per-material shaders are fragment-only, cooked by `CompileShader.cmake`, so no DXC ships in players. Sprites move after the grouping infrastructure and before shader assets.
- **Q10. Linux CI timing.** This is the one new platform decision; the macOS job and the random-byte hook stay the existing M4 items.
  - (a) Add an informational compile-only Linux job now, and land Linux `FillRandomBytes` from the Windows side with it, because the job compiles and checks it. Strength: a full build and `ctest -LE gpu` signal from the first week.
  - (b) Same job, but Linux `FillRandomBytes` is left to M4 contributors; the job builds RavenCore only until then. Strength: no platform code is written from Windows. The interim signal is narrower (samples, tools and tests are skipped).
  - (c) Wait for M4. Strength: no new CI cost now; drift keeps accumulating.
  - *Recommendation:* (a) or (b). Never a required check that blocks Windows milestones. macOS `FillRandomBytes` stays with the M4 contributor in every option.
- **Q11. Default frame pacing.** Removing the fixed 120 FPS sleep changes stock behavior for every user.
  - (a) No limiter under FIFO by default (`MaxFrameRate = 0`), an optional `MaxFrameRate`, and an idle throttle kept at 30 FPS. Strength: FIFO already paces, and the stock host reaches the panel refresh. Mailbox or Immediate then run uncapped unless `MaxFrameRate` is set, which must be documented.
  - (b) Keep today's 120 FPS cap as the default, with the new options available. Strength: no shipped behavior change. The stock re-run then measures a capped host.
  - *Recommendation:* (a), with FIFO as the default present mode. Either way, re-record the M3 numbers on the stock host afterwards (ARCH-PERF-01).
- **Q12. MoltenVK scope.**
  - (a) Require argument-buffer MoltenVK with update-after-bind; Intel Macs and lower-limit configurations get a clear rejection.
  - (b) Build the G4 per-texture fallback in M4.
  - *Recommendation:* (a). A fallback would be a second batching path that M6 grouping must carry. If (b), design it before ARCH-RND-01 lands.
- **Q13. Picking semantics.**
  - *Recommendation:* measure first (ARCH-PERF-06). If slow, allow picking against cached world transforms, with live compose only for the final candidate.
- **Q14. 2D run.** An owner-requested Courtyard run is needed before M6-9 (ARCH-RND-06). Please schedule it.
- **Q15. Docs that live in engine headers.** Approve that the comment corrections listed in DOC Phase B2 ride with the first approved snippet touching each header (each causes one host restart).
- **Q16. M6 exit checklist scope (ARCH-DOC-01).** The core checklist is a named sample, one material shader, one bound action, a save/load round trip and engine APIs only.
  - (a) Also require spawn/despawn through Commands and overlapping sounds. Strength: the named sample exercises the deferred command buffer and the M4 audio contract before v1. Cost: pulls the deferred M1 command buffer and an audio contract (not an M6 item today) into M6.
  - (b) Keep the core checklist only, and add Commands or audio when the named sample needs them. Strength: M6 scope stays the current roadmap list. Cost: the D5 flush point and pooled voices may stay unexercised until later.
  - *Recommendation:* (b) plus spawn/despawn if the named sample spawns at runtime, which the D5 trigger (`ROADMAP.md:75`) then requires anyway. Audio voices follow the ARCH-ASSET-04 contract decision.

## 9. Disputed or unverified items left open, and scope

### 9.1 Unverified (nothing was built or run)

- **GNU unique-symbol behavior (ARCH-PLAT-03)** was reasoned from GCC and glibc semantics, not compiled. Check it on the first Linux build with `readelf --dyn-syms -W`.
- **The size of the `/W4` warning backlog (ARCH-BLD-01)** is unknown. That `-std:c++23preview` implies `/permissive-` is based on documented MSVC behavior.
- **The libc++ version** that makes `jthread`/`stop_token` non-experimental is approximate (LLVM 18-20). The Xcode 16+ floor is the panel's best estimate.
- **The correctness of the uncompiled Xcb and Cocoa edits** (6b12ed6) was checked only by reading; the types look consistent.
- **The 50k at 100% animated CPU cost (9-14 ms)** is a projection from two data points.
- **The FindGroup cost at 500 groups (1.3-2.5 ms)** and the 6-12 ms `Rebuild` hitch are unmeasured estimates.
- **The structural-change cost (about 11 ms per spawn at 50k, ARCH-ECS-01)** is an extrapolation from unit costs that are not recorded in the repository, and it leaves out the order rebuild's own hash-map and sort cost. ARCH-ECS-01 step 0 measures it.
- **Device limits on the RTX 3070 Ti Laptop** (`maxSamplerAllocationCount`, `maxMemoryAllocationCount`) are not recorded in the repository.
- **Whether a debugger-held PDB blocks the next game link (HR-9)** cannot be tested until ARCH-BLD-02 is fixed.
- **Windows sleep granularity** under the current 120 FPS limiter was not measured, and there is no stock-host M3 measurement at all (ARCH-PERF-01).
- **The M6 content size (50-300 MB watched)** is an estimate.
- **Whether Puzzle and Collection scenes were hand-edited** after generation needs the owner's confirmation.
- **Linux build breaks beyond `FillRandomBytes`** (missing standard includes hidden by the PCH) are unknown until a `RAVEN_USE_PCH=OFF` build runs.

### 9.2 Settled by the lead where panelists disagreed

| Topic | Decision | Finding |
|---|---|---|
| PERF-1 severity | critical in round 2; lowered to **high** by verification (no runtime code writes a zero scale, the throw was documented design, the cost is an orderly exit) | ARCH-RT-01 |
| HR-1 severity | medium, but in the "now" bucket | ARCH-RT-04 |
| Missing-ancestor reaction | detached root over dropping the world | ARCH-RT-03 |
| Suspended-stage rendering | draw without the lit world over reusing the last GPU scene | ARCH-RT-05 |
| Hierarchy representation | hybrid with depth buckets | ARCH-ECS-01 |
| Bindings replacement | components over named slots | ARCH-CORE-02 |
| Mesh identity | one AssetId per sub-mesh, no `MeshHandle` in components | ARCH-ECS-03 |
| Player loader | function-level split, timed before M7 | ARCH-HR-03 |
| Directory-watch scaling | recursive-root contract over sibling collapse | ARCH-HR-02 |
| VMA | deferred | ARCH-RND-05 |
| Material-hash stopgap | dropped as throwaway | ARCH-RND-02 |
| Old-codec test | dropped | section 6.3 |
| "M5-Win" naming | dropped | ARCH-PLAT-02 |
| CORE-01 bundle | split into steps | ARCH-CORE-01 |
| Relocation before the generic format (verification request) | kept after M6-6; stable `TypeName`s or the alias table instead | ARCH-ASSET-01, ARCH-CORE-02 |
| Render-world group key | runtime `MeshHandle` widened by pipeline, resolved from `AssetRef` only on change; AssetId key dropped | ARCH-RND-01, ARCH-ECS-03 |

### 9.3 Verification pass results

Round 3 ran one independent adversarial verifier per finding rated critical or high after round 2, plus a citation checker over 105 citations elsewhere. All 15 findings were confirmed in substance with corrections. The citation checker found 11 problems (wrong line ranges, a nonexistent range in `VulkanFrameTimeline.hpp`, an off-by-one directory limit, an incomplete hash list, a misattributed handoff specification); all are fixed in place.

| ID | Verdict | Severity (panel -> final) | Main correction |
|---|---|---|---|
| ARCH-RT-01 | confirmed with corrections | critical -> high | Pickups are dimmed, not scaled; no runtime code writes a zero scale. The throw was documented design (`MeshMaterials.md:50`). Owner chooses public or private non-throwing helper; the `Sync` reset moves to M6-1 with RT-05. |
| ARCH-RT-02 | confirmed with corrections | high -> medium | Authored data and deleted leased PNGs are already safe; only a bad or never-leased ID written by game code ends the session. Fix is one shared non-throwing lookup for data errors only, no sample-local guard in the sprite gather. |
| ARCH-RT-03 | confirmed with corrections | high -> medium | Orderly exit, not a crash; latent. Detached-root semantics must also cover `ComposeTransformUnchecked`, and the changed flag must propagate so static children do not go stale. |
| ARCH-RT-05 | confirmed with corrections | high -> medium | PostUpdate/Extract run in client code, including the `PrepareFrame` replacement path. `bad_alloc`, non-std exceptions and engine `logic_error`s are fatal, not recoverable. Effort S only on top of CORE-01. |
| ARCH-CORE-01 | confirmed with corrections | high -> high | Second Extract site in `PrepareFrame`; the smoke does not implement `ApplicationClient`. The temporary `OnFrame` shim is dropped. |
| ARCH-CORE-02 | confirmed with corrections | high -> high | Camera transform stays writable; an engine play-state gate replaces the empty-binding gating. `Private/Game` headers are also hashed, so relocation alone does not end restarts. |
| ARCH-ECS-01 | confirmed with corrections | high -> medium | The 11 ms is an extrapolation and the cost is documented design with no runtime trigger. The upload is dirty-scatter, not "full". Write policy for `ParentComponent` must be decided; benchmark first. |
| ARCH-ECS-02 | confirmed with corrections | high -> medium | Today's hole is components in game `.cpp` files (none exist). Default bytes cannot come from constexpr `bit_cast`; type tags must encode enum and `AssetRef` targets; log in `FindPool`'s mismatch branch. |
| ARCH-ECS-03 | confirmed with corrections | high -> high | Mesh resolution must not run per write; resolve on tick change. Persistent ID stored per entity; `std::less<>` instead of a hash map; sample writers listed. |
| ARCH-RND-01 | confirmed with corrections | high -> medium | Performance figures are unmeasured estimates; two unique meshes today; grouping by pipeline is already an M6 item. Exact `FindGroup` guard needs no new flag; step 4 is more than a shader change. |
| ARCH-PERF-01 | confirmed with corrections | high -> medium | Pacing-independent numbers show goal 4 is met on the stock host; the `Benchmark3D.md` citation was unrelated historical data; adapter selection is not part of the gap. Effort S-M. |
| ARCH-ASSET-01 | confirmed with corrections | high -> high | `SceneDocument.hpp:203-250` does not exist; `LevelFile.hpp` is sample code. `raven-project 2` must tolerate unknown records; unknown component sections are preserved; the `.ravenlevel` path is deleted too. |
| ARCH-PLAT-01 | confirmed with corrections | high -> medium | `RavenGameModule.cmake:42-52` does not exist (gate at `:4, 13-14`); missed call site `GameModule.cpp:554`. Keep an up-front platform check; UTF-8 paths through `PathFromUtf8`; non-MSVC CMake branches wait for the Linux job. |
| ARCH-PLAT-02 | confirmed with corrections | high -> medium | macOS `libRavenCore.dylib` also fails to link; the handoff specifies `SecRandomCopyBytes`, not `getentropy`. Scoped to one new decision (Linux job now); macOS `FillRandomBytes` stays with M4. |
| ARCH-DOC-01 | confirmed with corrections | high -> medium | `InteractionGame.cpp` is game-module code in `Private/Game`; recoloring is designed behavior. Credits the existing note-first practice; checklist scope additions become owner question Q16. |

### 9.4 Refuted and dropped

- A half-written DLL can be loaded through F5 (HR-3).
- `AssetStore::Acquire` stat and Collect costs are per frame (ASSET-5).
- `CollectResources` during `Sync` retires referenced textures (ASSET-5).
- The texture-swap path is a current Vulkan correctness bug (RND-4; the fence scope covers it).
- `RavenCore.so` exports the interaction components' `Info` (PLAT-2(c)).
- M4 dlopen backends would import the copy-then-load model (CORE-6).
- A missing `Threads::Threads` breaks the link on current Ubuntu (PLAT-3).
- The doc's tick-walk estimate is off by 40x (PERF-4; the correct figure is about 5-10x).
- Pickups are hidden by scaling them to zero (round-2 premise of ARCH-RT-01, refuted in round 3: `Private/Game/InteractionGame.cpp:38-40` dims the `Tint`).
- A deleted PNG of a texture already in use ends the session (round-2 claim in ARCH-RT-02, refuted in round 3: the lease stays cached and `AssetStore::ApplyReloads` catches publish failures).
- The platform handoff tells contributors to avoid shared edits (round-2 wording in ARCH-PLAT-01, refuted in round 3: `PlatformHandoff.md:15, 17`).

### 9.5 Out of scope or only partly covered

- **Importer security and fuzzing** of malformed FBX, PNG and scene inputs. Only noted: ufbx and stb run in shipping players (ARCH-ASSET-05).
- **Full concurrency audit.** RENDER checked renderer workers and found no defect (null pipeline cache, worker joined before device idle, all submits on the main thread). `ReloadService` and `AssetStore` threading was reviewed only at the level of documented contracts.
- **Device-lost and out-of-memory recovery.** `VulkanDeviceLostError` ends the run, documented as R7 and accepted for v1. OOM during GPU buffer growth was not examined.
- **DXC redistribution licensing.** Moot if per-material shaders are cooked (Q9).
- **The editor itself.** Out of scope for v1 per ROADMAP. Only readiness was assessed.
- **Owner decisions** (section 8) are not settled by this document.
