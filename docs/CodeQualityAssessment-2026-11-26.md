# RavenEngine Code Quality Assessment (2026-11-26)

Status: proposal for owner review. Reviewed revision 1633f31 on master (the code is identical to 7617e21; the later commit added only docs). Nothing was built or run.

## 1. Purpose and method

This document lists code-level problems in RavenEngine and says how to fix them. The implementation team can pick up any single finding and act on it.

**What the three lenses mean here**

- **Slop**: code that is padding, dead weight or generated-looking. This covers comments that restate or contradict the code, dead functions and members (proved by grep), copy-pasted blocks that drifted, wrappers that add nothing, magic numbers and duplicated tables. The seams around vendored libraries and the Platform/<OS> interfaces are required by project rules and are not slop.
- **Slow**: code that costs time where it matters. Every slow finding states its frequency: per frame, per entity per frame, per fixed step, per reload, per scene load, per asset load, startup or build time. A cost that was not measured is labeled "estimate" or "derived" with its arithmetic. Recorded measurements in docs/*.md are cited.
- **Readability**: functions too long or too nested to review safely, misleading names, hidden ownership or ordering rules, files doing several jobs, and drift from the AGENTS.md style.

**How the review ran**

- Nine panelists each reviewed one area: RND (renderer frame path), VK (Vulkan resources and renderer headers), CORE (core runtime and host), DATA (ECS, scene, assets, tools), GP (physics and input), MEDIA (audio, text, UI), API (public headers and compile cost), BLD (platform, CMake, scripts), GAME (games, tests, benchmarks). A lead (chief reviewer) wrote this document.
- Two discussion rounds. In round 1 each panelist wrote a paper. In round 2 they answered each other's questions, challenged findings, and revised or withdrew their own. The lead settled the remaining disputes, in some cases by reading code again (section 10 lists what was decided and why).
- Read-only. No file under D:\RavenEngine was changed. Nothing was built, run or measured for this review. Line numbers refer to revision 1633f31.

**Verification round (applied to this version)**

- Adversarial verifiers re-read the code for 51 findings and tried to refute each one. The six high findings (CQ-VK-01, CQ-DATA-01, CQ-DATA-02, CQ-BLD-01, CQ-GAME-01, CQ-X-01) were each verified alone. The 45 medium findings were verified in batches. Each verdict is confirmed, confirmed with corrections, overstated or refuted, with a recommended severity, corrections and problems in the recommended fix.
- Low findings were not verified one by one. They were covered only by the citation spot-checks below, so their severities are the panel's.
- Two citation checkers opened cited file:line ranges across the whole document and reported 31 problems (three reported by both). One report was a new defect, added as CQ-GP-13. One correction was checked again and not applied (CQ-BLD-02, section 10.4).
- A completeness critic compared file sizes against citation counts and proposed six new findings. Each was verified separately: five were added (CQ-CORE-15, CQ-CORE-16, CQ-RND-13, CQ-API-14, CQ-VK-12) and one was refuted as a duplicate of CQ-CORE-11. The critic's coverage gaps are listed in section 10.3.
- Each verified finding carries a **Verification** line. Results: 3 high findings remain (CQ-BLD-01, CQ-GAME-01 and the CQ-X-01 pattern); three former highs are now medium; 16 mediums were lowered to low; one finding (CQ-DATA-10) was dropped as a duplicate of AA2-ECS-02. Section 10.4 has the full table.

**Relation to the architecture assessment**

docs/ArchitectureAssessment-2026-11-26.md (IDs AA2-*) covers architecture. This document does not repeat its findings. Where a finding here overlaps, it names the AA2 ID and adds only code-level detail. Phase names (Phase 0 to Phase 4, and the † commit) are those of AA2 section 7.1:

- Phase 0: docs-only items, now.
- Phase 1: the M6A+M6C review amendments. Items marked † edit a contract input and land in one hash-changing commit.
- Phase 2: the baseline before B4.
- Phase 3: the contract-break window at the start of B2 (one host restart).
- Phase 4: during M6B.

The older docs/ArchitectureAssessment.md (ARCH-*) is history.

**Severity scale**

- high: a measurable cost on a goal-scale hot path, or slop/readability that has caused or is likely to cause a bug (a drifted duplicate, a misleading comment, a function nobody can safely change).
- medium: real but contained.
- low: polish.
- critical: blocks a v1 goal. No finding is critical.

**Terms**

- Contract hash: the hash GameContract.cmake computes over every Public .hpp plus RavenBuildSettings.cmake, RavenGame.cmake and RavenGameSources.cmake. Any byte change, comments included, forces a host rebuild and a rebuild of every game.
- No-throwaway: do not polish code that a scheduled step deletes or rewrites. Such items say "delete with Bn".
- Snippet: one small, reviewable change of one to three files, applied by the owner.

## 2. Executive summary

**Verdict.** The code is in good shape for its age. History narration and restating comments are nearly absent. Most comments explain why, and many cite a measurement. The frame loop meets its recorded budgets. The problems are concentrated in four patterns:

1. **Duplicated facts, some already diverged.** One fact is kept in several hand-written places. Some copies already disagree (the field-matching rule, physics state copies, the harness folder lists, the "new best" rule, the active-camera rule, file-size caps). Others still agree but will be edited next (asset-kind tables, shader-program groups, pipeline create-infos, Arena action names). One of the three high findings is of this kind (the harness folder lists broke a tracked check), and so is the CQ-X-01 pattern. The asset-kind, field-matching and pipeline duplicates were lowered to medium in verification because their failures are loud or statically preventable, but each has a concrete trigger in the next milestone step (B2 adds materials, B4 adds pipelines).
2. **Comments that claim a capability or cost the code lacks.** These are rarer than in most codebases, but worse than restating comments, because reviewers trust them.
3. **Long multi-job functions in files M6B edits next**: VulkanClearPass::Record, Application::Run, PhysicsWorld::Step, GameModule Watch, CheckFragmentShaderInterface.
4. **Reload-loop overhead.** About 100-170 ms (derived, partly unmeasured: 60-110 ms of output hashing, 25-39 ms of glob re-verification, 16-22 ms of frame-thread re-hashing) of redundant work sits in the 1.47-1.50 s game-edit loop, and the shader edit path runs a DXC dependency scan in series before every compile (CQ-RND-13).

One shipped-code crash path (Arena dereferences unchecked TryGet results) and one broken tracked check (the live reload harness cannot build its snapshot) need fixing now. Verification also found a small reproduction bug: about half of the logged simulation seeds cannot be passed back through --seed (CQ-CORE-15).

**Scorecard**

| Area | Slop | Speed | Readability |
|---|---|---|---|
| RND renderer frame path | good: almost no narration; a few misleading comments, thin wrappers | fair: within budget; sync is O(total) (AA2-PERF-02), dirty records copied twice, per-glyph barriers; the shader edit path runs DXC's dependency scan in series | fair: small classes clear; VulkanClearPass and VulkanContext::Impl have long multi-job functions and scattered program groups |
| VK Vulkan resources | fair: four pipeline copies that differ in style, nine result-check wrappers, two SPIR-V parsers, dead camera-type headers | good per frame, fair per asset load (per-texture pool, fence, submit, sampler; O(slots) bookkeeping) | fair: good why-comments; 112-147-line pipeline constructors, a 184-line fragment checker and magic SPIR-V numbers |
| CORE host and runtime | fair: hand-written FNV copies, four drifted key lists, ~6 dead members, a five-times-copied asset lookup | good frame loop; fair reload loop (DLL hashed three times on the frame thread, AA2-HR-06; 40 ms polling during imports) | fair, poor in Application.cpp (Run 444 lines, Watch 153-line state machine over nine locals); --seed cannot read half the logged seeds |
| DATA ECS, scene, assets | fair: clean core; asset-kind knowledge in six tables, field-matching rule twice | fair: views 3.5-4.0 ns per match (measured); instantiate hash-map bound (600 ms at 200k); interpolator hash lookups | fair: good borrow rules; 1,046-line four-job SceneFile.cpp, four parallel field structs |
| GP physics and input | fair: clean comments; Entry/Desired copy lists drifted into a wrong-state bug; slot maps x4 | fair: fine at measured scale; first pick 32 ms Release / 225 ms Debug is the full pick-world build (measured) | fair: excellent headers; Step 189 lines, contact logic split; a noexcept decomposition that can call a throwing Normalize |
| MEDIA audio, text, UI | fair: dead members, duplicated limits, bus names, ducking and alignment rules | good: audio thread real-time-safe; HUD and pause menu 0.117-0.167 ms p95 against their 0.3 ms budget, densest screen 0.288 ms p95 (measured) | fair: clean public Ui API; Edges Left/Right misnaming hides a latent RTL bug |
| API public headers | good: lean, one unused include, one dead camera helper | good: PCH closure has no OS, Vulkan or heavy containers | fair: label/name/authoredId vocabulary, misleading static_assert text, style drift |
| BLD platform, CMake, scripts | fair: drifted harness copies (one breaks a tracked check), repeated vendoring CMake, unchecked Yoga and msdf version pins | good per frame; fair per reload (outputs hashed twice, globs re-verified) | fair: hidden Windows.h include order, 141-line WindowProc, 191-line recipe |
| GAME games, tests, benchmarks | fair: helpers copied across games, dead Register lists, stale test headers | good: tiny at real scale; benchmark rotors ride on puzzle code | fair: Arena is a model, Samples and Courtyard break the one-file rule; unchecked TryGet can crash the host |

**Top fixes in priority order**

1. **CQ-GAME-01** (high): Arena dereferences TryGet results unchecked in six places. A routine dump/apply data edit ends the host with an access violation instead of skipping one system (FD-06). Fix now with null checks or a view-based FindCraft helper that skips and logs; keep skip-and-log as the documented pattern for data-dependent reads.
2. **CQ-BLD-01** (high): add Content to the Test-Windows3DLive.py snapshot (one line), and have the owner run it once on the laptop. The only tracked reload-recovery check cannot build today (verified by reading the generated Ninja rule).
3. **CQ-DATA-02** (medium): one field-matching function shared by live migration and scene/profile load, with FD-16's name-first rule, and registration that rejects ambiguous alias tables (rename chains, swaps, one old name claimed twice). Today both copies depend on declaration order, and a rename chain can silently reset player-profile values.
4. **CQ-DATA-01** (medium): one asset-kind table and one ForEachAssetRef before B2 adds Material. A missed row fails loudly but only at the packaged player's scene load.
5. **CQ-GP-01** (medium, S): add HasRigidBody to the physics rebuild test and replace hand-copied state with one struct. Jolt friction can silently disagree with the component.
6. **CQ-RND-01 step 1 and CQ-RND-03 step 1** (medium, S): make CheckProgram throw on an unclassified shader program, and correct the two comments that claim interface checks that do not exist (one snippet).
7. **CQ-CORE-15** (medium, S): parse --seed as unsigned, so every logged seed can be replayed.
8. **CQ-VK-01 then CQ-VK-03** (medium, medium): one GraphicsPipelineDesc and one shared SPIR-V parser before B4, as preparation, so material pipelines and the wider fragment interface are data edits.
9. **CQ-BLD-02, CQ-BLD-03** (medium): cut redundant reload-loop work in the build recipe: about 60-110 ms derived of output hashing and 25-39 ms measured of glob re-verification per game edit. CQ-BLD-03 needs a path check first. The frame-thread re-hash (CQ-CORE-01, 16-22 ms derived) is folded into AA2-HR-06 step 1.
10. **CQ-GAME-02, CQ-GAME-11** (medium): one new-best rule and a fall-out end to the run, with AA2-GP-05 step 3. Both are player-visible in the reference game.
11. **CQ-RND-13** (medium): time the shader compile phases, then run DXC's dependency scan beside the compile if it is 10% or more of the median edit.
12. **CQ-X-08** (low, sequencing): land public-header fixes in AA2 section 7.1's Phase 1 † commit and Phase 3 window, so the contract hash changes once per phase.

## 3. Patterns worth copying

These are the target shapes the fixes converge on.

- **Transactional commit: work on copies, publish by swap.** VulkanMesh.cpp:226 ("Everything that can fail before submission runs before the pools change"), :235-244, :295-304. Registry.cpp:554-643 (Migrate builds side buffers, then commits with node extract/insert and swaps only). SceneSession.cpp:58-112 (candidate prepared, re-checked, published with swaps). RenderWorld.cpp:269-282 (Sync catches, clears, rethrows) with RenderWorld.cpp:203-213 stating the ordering rule.
- **Timeline-semaphore retirement.** VulkanFrameTimeline.cpp:34-58, 60-80 with VulkanFrameTimeline.hpp:12-55: one semaphore counts frames, a monotonic deque retires shared_ptr resources, Reached() caches the counter, and Retire falls back to vkDeviceWaitIdle on allocation failure. Use it for every deferred destroy.
- **Create into a local, assign on success, catch(...) { Destroy(); throw; }.** VulkanTexture.cpp:181-183, 259-263; VulkanBuffer.cpp:70-73, 100-104; VulkanDepthAttachment.cpp:66-68, 93-97.
- **Shader interfaces derived from C++ layouts.** VulkanGpuScene.cpp:201-243 and the frame-uniform offsets at VulkanContext.cpp:361-363 and 377-379 are built with offsetof, so a reloaded shader whose buffer layout drifted is rejected with last-good kept. The stage-variable lists beside them are still hand-written {location, width} literals (CQ-VK-04 step 4 derives them). static_asserts pin every GPU-visible layout (RenderWorldState.hpp:36-38, 49-50; VulkanFrameData.hpp:28-33; VulkanDrawQueue.hpp:34-38; ColorVertex.hpp:16-22).
- **A clear per-frame protocol with one-line contracts.** VulkanGpuScene.hpp:48-63: Plan, Upload, RecordPrepare, RecordDraw, RecordReadback, Commit. Commit only after submit (VulkanClearPass.cpp:588-590).
- **Why-comments that cite a measurement.** SceneFile.cpp:933-934 records the 0.9 s it avoided. Scene.hpp:151-162 explains the 24-byte world slot (history 4.5 ms to 1.37 ms, DataModelDesign.md:650). AudioVoices.cpp:15-28 gives the perceptual reason for every audio constant.
- **Telemetry that costs nothing when off.** Application.cpp:48-63, 383-391; FrameTrace.hpp:55-80: a thread_local target set only when a capture exists, a null FrameSection does nothing, samples reserved once.
- **Typed error tiers and log-once latches.** StageBoundary (Application.cpp:72-128) keeps bad_alloc and logic_error fatal and logs a repeated failure once. GameModule.cpp:396-404 suspends a throwing system with one log line. AudioVoices.cpp:122-123, 433-435 and PhysicsWorld.cpp:698-706, 774-777 latch reports.
- **Vendored libraries behind one seam.** Jolt is reached only through Private/Physics (JoltSupport.hpp:27-91 is the single conversion and validation point). The audio device seam (AudioDevice.hpp:9-67) has a documented threading contract and a null-device fallback. TextSystem's pimpl (TextSystem.hpp:105-107) keeps ICU, HarfBuzz, msdfgen and FreeType in one TU.
- **Thin typed templates over one exported type-erased entry.** Registry::Has/Add/Read/Write (Registry.hpp:218-254), SceneAccess (SceneAccess.hpp:37-80), UserDataStore (UserData.hpp:35-60). Template bloat stays small and generic tools share the entry.
- **Fast inner loops.** View::Run (Registry.hpp:355-436) drives from the smallest pool, decides stamping from const-ness at compile time, and is measured at 3.5-4.0 ns per match (ECSDesign.md:329). AudioQueue (AudioMixer.hpp:13-50) is a lock-free SPSC ring with cache-line-separated indices.
- **Determinism written into code with the reason beside it.** PhysicsWorld.cpp:525-553 (Commit sorts additions by persistent ID at :534-537), :794-811 (two add batches in that sorted order), :1079-1080, 1123-1137 (ordered contacts), :991-1000 (Clear rebuilds Jolt's world). CharacterGrid (CharacterMovers.cpp:72-216) replaces Jolt's all-pairs checker through its public interface, measured 22.1 ms to 3.1 ms at 500 movers (GameServicesDesign.md:529-533).
- **Process hygiene.** Win32GameModule.cpp:27-58, 118-174: build processes start suspended in a KILL_ON_JOB_CLOSE job with an explicit handle list. BuildGame.cmake.in:35-40, 185-189: lock before reading stamps, commit stamps by write-then-rename after verification. BuildPlayer.py:89-111: atomic publish with rollback.
- **Game code shape.** ArenaGame.cpp:22-65 only registers. ArenaComponents.hpp:26-143 puts tuning on components with units. ArenaUi.cpp:178-217 draws each screen from a plain model shared by systems and F6 previews.
- **A tiny test harness.** Tests/Support/Check.hpp:1-67 and glob enrollment (Tests/CMakeLists.txt:1-51). Each test file opens with the contract it guards.

## 4. Hot-path map

"Cost class" is the growth rate. Measurements come from the docs cited. Everything else is labeled estimate or derived.

| Path | Frequency | Where | Cost class | Recorded measurement | Findings |
|---|---|---|---|---|---|
| RenderWorld::Sync, ApplyTicks | per frame | RenderWorld.cpp:103-122 | O(total meshes) | 0.90-0.93 ms at 50k animated (DataModelDesign.md:661) | AA2-PERF-02, CQ-RND-07 |
| RenderWorldSync::Write | per changed object per frame (~10k of 50k) | RenderWorld.cpp:196-231 | O(changed) | inside the above | CQ-RND-09 |
| GpuScene Plan + Upload | per frame | VulkanGpuScene.cpp:298-336, 365-404 | O(groups) + O(dirty), record copied twice | renderer CPU (preparation plus record/submit) 0.53 ms p95 at 50k (GpuDrivenDesign.md:228-230) | CQ-RND-04, CQ-RND-08, CQ-RND-11 |
| GPU scatter and cull | per frame, per object (GPU) | Scatter.comp.hlsl, Cull.comp.hlsl:47-48 | O(objects), one atomic per visible object | upload+cull 0.17 ms at 50k (GpuDrivenDesign.md:230) | CQ-RND-12 |
| Lit, overlay, debug draws | per frame | VulkanGpuScene.cpp:465-479; VulkanOverlayPipeline.cpp:142-153 | O(1) binds and draws | - | CQ-VK-01 |
| Glyph uploads | per frame with new glyphs, per glyph | VulkanClearPass.cpp:419-449 | 2 barriers + 1 copy per glyph | unmeasured | CQ-RND-05 |
| Whole frame CPU preparation | per frame | Application.cpp:387-673 | includes Update-phase game systems | 3.24-3.53 ms p95 against 3.5 ms (DataModelDesign.md:661; GpuDrivenDesign.md:225) | CQ-GAME-03 |
| Samples rotor loop (drives the 50k benchmark's 20% animation) | per rotor per frame | Interaction.cpp:90-108 | O(rotors) | Interaction system 22.4 us per frame (ECSDesign.md:293) on the 800-pivot workload (ECSDesign.md:256); 28 ns per rotor derived as an upper bound (the time also covers switches and doors); 0.28-0.4 ms at 50k derived | CQ-GAME-03 |
| Scene::UpdateWorldTransforms | per frame, twice with interpolation | Scene.cpp:510-593 | O(all nodes) | p50 1.37-1.39 ms at 50k animated (DataModelDesign.md:661) | AA2-PERF-01/02, AA2-ECS-07 |
| TransformInterpolator Apply/Restore | per moving entity per frame | TransformInterpolator.cpp:29-116 | about 5.7-6.5 hash lookups per entity per frame at 165 Hz with a 60 Hz step (derived) | unmeasured | CQ-DATA-05 |
| SceneAccess TryGet/Read/Write | per call, per entity per fixed step in games | SceneAccess.cpp:35-86; Registry.cpp:253-260 | O(1) with a hash lookup | Registry::Read 15.8-16.7 ns in random order at 100k (ECSDesign.md:332); SceneAccess::TryGet about 25 ns (estimate) | CQ-API-03 |
| View::Each | per system call | Registry.hpp:374-426 | O(smallest pool) | 3.5-4.0 ns per match (ECSDesign.md:329) | (strength) |
| PhysicsWorld::Step | per fixed step | PhysicsWorld.cpp:1002-1190 | Jolt update + O(entries) scans | 1.0/1.6 ms mean/p95 at 1,000 bodies (GameServicesDesign.md:428) | CQ-GP-01, -02, -05, -07, -08 |
| CharacterMovers::Update | per mover per fixed step | CharacterMovers.cpp:463-522 | O(movers) | 3.1 ms at 500 movers (GameServicesDesign.md:529-533) | CQ-GP-12, CQ-GAME-11 |
| PickWorld::Pick | per left-click | PickWorld.cpp:448-498 | O(entries) tick scan (AA2-PERF-02) plus a shape collection that short-circuits (about 2 compares on the two-mesh 50k scene; O(shapes x entries) only for unused shapes) | 0.39 ms unchanged sync, which is ApplyTicks; first pick 32.1 ms Release / 225 ms Debug at 50k, which is the full pick-world build (GameServicesDesign.md:471-477) | CQ-GP-03 |
| UI build, layout, draw | per frame, per element, per glyph | UiContext.cpp:395-538; TextSystem.cpp:696-732 | O(elements) allocations, 2 lookups per glyph | 0.053-0.175 ms mean, 0.117-0.288 ms p95; the 0.3 ms budget covers the HUD plus pause menu (0.117 and 0.167 ms p95) (GameServicesDesign.md:624-630; UIDesign.md:233) | CQ-MEDIA-02, CQ-MEDIA-03 |
| Audio mixer | per 256-frame block (audio thread) | AudioMixer.cpp:103-182 | O(64 + active voices x 256) | <1% of a core estimate | CQ-MEDIA-09 |
| AudioVoices handle calls | per handle call | AudioVoices.cpp:303-311 | O(sounds) scan | no caller today | CQ-MEDIA-04 |
| Host frame loop overhead | per frame | Application.cpp:401-441 | constant, small allocations | a few us estimate | CQ-CORE-09, CQ-X-06 |
| Game-edit reload loop | per game edit | BuildGame.cmake.in; GameModule.cpp:408-515 | hash phases O(DLL+PDB bytes) | 1.47-1.50 s Debug total (GameServicesDesign.md:265); hash 46 ms, verify 54-89 ms, stamp 8-11 ms, copy 17-46 ms (HotReloadDesign.md:349-350, M5 H5 probe on Samples' DLL); verify_globs 25-39 ms (child .ninja_log) | CQ-BLD-02, CQ-BLD-03, CQ-CORE-01, CQ-CORE-02 |
| ReloadService watcher | every 40 ms during any import, every 2 s otherwise | ReloadService.cpp:288-386 | O(watched files) stats per pass | about 5.4k stats/s from Arena's 108 matched game-source files while an import runs (about 0.65 s at launch), derived | CQ-CORE-06, CQ-CORE-03 |
| Scene load | per load, restart, quick-load | SceneFile.cpp:876-988 | hash maps, vector of vectors, triple label copies | instantiate 600 ms at 200k, decode 33 ms (DataModelDesign.md:662) | CQ-DATA-03, CQ-DATA-11 |
| Asset preload and decode | per unique asset per load | AssetStore.cpp:180-274; Image.cpp:89-101; PngFileWatch.cpp:37-76 | O(assets + slots) per Acquire; two full copies; byte-wise CRC | unmeasured | CQ-DATA-04, CQ-DATA-06, CQ-VK-11 |
| Texture and mesh creation | per asset load | VulkanTexture.cpp:124-264; VulkanMesh.cpp:220-306 | pool + fence + submit + wait per texture (AA2-RND-04); staging allocation + command buffer + submit per mesh, on a shared pool | unmeasured | CQ-VK-05, CQ-VK-06 |
| Shader reload | per shader edit | ShaderLibrary.cpp:156-211; CompileShader.cmake:9-34; VulkanContext.cpp:402-517 | cmake + DXC scan + DXC compile + spirv-val in series per program; interface checks | fragment save to visible 248-432 ms, median 268 ms, against 300 ms (HotReloadDesign.md:326); about 100 ms per program compile (HotReloadDesign.md:307) | CQ-RND-13, CQ-RND-01, CQ-RND-03, CQ-VK-03 |
| Host build | per `cmake --build` | RavenContent.cmake:137-143 | ~48 MB read to compare staged content, derived | unmeasured | CQ-BLD-07 |

## 5. Findings by area

Each finding gives Severity, Category, Frequency (slow findings only), Sources (round-1 panel IDs, or the verification source for findings added in verification), Evidence, Why it matters, Fix, Effort, Timing, Overlaps, Panel discussion and, for verified findings, Verification. Effort: S is under a day, M is a few days, L is longer. Low findings without a Verification line were covered only by citation spot-checks.

### 5.1 RND: renderer frame path

#### CQ-RND-01 Two default branches silently misroute any new engine shader program; group membership is spread over seven code sites plus the enum and the CMake table

- **Severity:** medium (revised from high).
- **Category:** readability, hidden ordering.
- **Sources:** RFP-1; verified by VK and RND in round 2.
- **Evidence:**
  - VulkanContext.cpp:459-473: ApplyShaderReloads ends in `else textured.push_back`. Any program not named goes to the textured group.
  - VulkanContext.cpp:474-499 with :345-352: publish('Textured') rebuilds the textured pipelines from the library's current words (WordsOf falls back to library.Get), then calls m_Shaders.Set for every candidate (:486-487). The misrouted program is recorded as published and no pipeline uses it.
  - VulkanContext.cpp:402-438 and :915: CheckProgram runs first, on the reload worker (through m_Library.m_Check, ShaderLibrary.cpp:191). Its `default:` (:433-436) is the intended route for LitFragment, UnlitFragment, NormalsFragment and WireframeFragment, and it also catches any unlisted program, validating it as a lit fragment shader. The lit checker requires one PSMain fragment entry (FragmentShaderInterface.cpp:119-123), so a new vertex or compute program is rejected on every reload with a misleading "fragment" message. A new fragment program that satisfies the lit interface (most likely a new F7 debug view) reaches the silent path.
  - Membership is repeated at ShaderLibrary.cpp:38-52 (program table), VulkanContext.cpp:298-303 (IsLitProgram), :441-446 (GetLitShaders), :552-554 and :564 (a positional array of 5 mapped by index to LitShaders, VulkanClearPass.hpp:37-44), and :271-277 (GetLitShaders() plus 6 m_Shaders.Get spans to the pass constructor). The ShaderProgram enum (ShaderLibrary.hpp:20-22) and the raven_add_shader rows in RavenContent.cmake:50-66 (AA2-HR-01 step 5, formerly ARCH BLD-04) are two more per-program sites.
- **Why it matters:** game material shaders never become ShaderProgram values (AA2-HR-01 step 5), so B4 is not the trigger. The trigger is the next engine program: B5 sprite or blend programs, or a new debug view. Forgetting one site gives a stale pipeline or a permanently rejected reload, with no clear error, in the edit-shader-see-result loop.
- **Fix:**
  1. Now, VulkanContext.cpp only: in CheckProgram, list the four lit fragment programs explicitly and make `default:` throw `std::logic_error("unclassified shader program")`. That throw runs on the reload worker, where ReloadService.cpp:428-431 catches it and logs a failed import. Do not throw in ApplyShaderReloads: it runs on the main thread inside DrawFrameWithView, outside publish's try (:479), and AA2-RT-01 notes that Render has no stage boundary and that logic_error is fatal. There, share one `Classify(program)` function with CheckProgram, and log and drop an unclassified program. Ship this with CQ-RND-03 step 1 (same function).
  2. With AA2-HR-01 steps 4-5: add a ShaderGroup tag (Lit, Textured, Compute, DebugLine, Overlay) to ProgramDesc and expose ShaderLibrary::GetGroup (ShaderLibrary.hpp/.cpp).
  3. Switch IsLitProgram, Classify and the reload classification on GetGroup. Build the lit word array from one constexpr list that also defines LitShaders' order (VulkanContext.cpp, VulkanClearPass.hpp).
  4. Fold the constructor's LitShaders struct and its 6 other span arguments into a PassShaders struct (VulkanClearPass.hpp/.cpp, VulkanContext.cpp). The struct lands with the B5 rename in CQ-RND-06.
- **Effort:** S.
- **Timing:** step 1 now (Phase 1 private item); steps 2-3 with AA2-HR-01; step 4 during B5.
- **Overlaps:** AA2-HR-01 (its step 4 must change these call sites, which it cites as name dispatch at VulkanContext.cpp:440-470; this finding adds the misrouting detail and the positional array), AA2-RND-06.
- **Panel discussion:** RND proposed high; VK and RND agreed in round 2 on medium because B4 adds no engine program and CheckProgram fails most new kinds loudly. The lead accepts medium; step 1 is cheap enough to land now regardless.
- **Verification:** confirmed with corrections; severity medium -> medium because nothing in B4 triggers it, but the failure is silent in the reload loop and cheap to fix.

#### CQ-RND-02 BuildVulkanDrawQueue also builds the frame uniform and the only lighting validation, so deleting the CPU textured path at B5 as written loses both; there is no B5 deletion checklist

- **Severity:** low (revised from medium in verification).
- **Category:** readability, hidden ordering.
- **Sources:** RND-13 (new in round 2), absorbing RFP-2 and RFP-10 step 1.
- **Evidence:**
  - VulkanDrawQueue.cpp:74-83: BuildVulkanDrawQueue calls lighting.Validate() and builds queue.Frame (FrameUniformData). The lit vertex shader reads it at set 1 and debug lines at set 0 (VulkanContext.cpp:361-363, 377-379).
  - grep `Validate()` over RavenEngine, Games and Tests: VulkanDrawQueue.cpp:78 is the only SceneLighting::Validate caller (definition SceneLighting.hpp:17).
  - VulkanContext.cpp:702-704: the comment "Visibility and ordering complete before acquisition" sits on the CPU queue build. VulkanGpuScene.cpp:450 builds a second ViewFrustum during recording, after acquire (VulkanClearPass.cpp:541, 546, 558). Today it cannot throw after acquire, because the identical ViewFrustum(viewProjection) at VulkanDrawQueue.cpp:89 runs first, before acquire.
  - A NaN camera is already rejected before acquire: Scene::GetCameraView throws on a non-finite view (Scene.cpp:600-609), GameSession catches it and skips the frame (GameSession.cpp:496-508), and DrawFrameWithView returns before Plan (VulkanContext.cpp:694-696). What remains after B5 is only float overflow of Projection*View.
  - If the call were deleted, queue.Frame stays value-initialized (`FrameUniformData Frame{}`, VulkanDrawQueue.hpp:51): an all-zero frame uniform, so the lit scene draws blank.
- **Why it matters:** B5 deletes more renderer code than any other step. A deleter who removes BuildVulkanDrawQueue as "the CPU sprite queue" leaves an all-zero frame uniform (a blank lit scene, obvious on the first B5 run and in the image checks) or drops the only lighting validation, until AA2-RT-01 step 1 adds SceneLighting::IsValid with a GameSession fallback. Without one list, B5 also leaves orphan helpers.
- **Fix:**
  1. Now, after or with AA2-RT-01 step 1 (which touches the same Validate rule), VulkanDrawQueue.hpp/.cpp: split out `FrameUniformData BuildFrameUniform(view, viewProjection, const SceneLighting&)` (Validate plus uniform) from the textured-queue builder. Both are still called from VulkanContext.cpp:704. Pure move.
  2. With it, VulkanGpuScene.hpp/.cpp and VulkanContext.cpp: Plan takes viewProjection and stores the cull constants, so the frustum is built once, before acquire. RecordPrepare drops its parameter. This is pre-B5 hardening (the frustum throw stays before acquire, consistent with AA2-RT-01 step 4's DataError rule) and removes a duplicate per-frame frustum build that costs almost nothing; it is not a performance fix.
  3. Do not record a firm B5 list in Phase 0. The B5 design is unsettled: AA2-RND-03 keeps owner questions Q8 and Q9 open, and its step 1 keeps blended sprites CPU-sorted back-to-front through a blend pipeline, which plausibly still needs CPU visibility (ViewFrustum::Intersects, possibly TransformBounds) and a blend pipeline (today VulkanTexturePipeline's Transparent mode). Record the candidate checklist below as input to AA2-RND-03 step 6's draft revision, after Q8 and Q9 are answered.
     - Candidate deletions: VulkanTexturedMesh.hpp/.cpp; VulkanTexturePipeline.hpp/.cpp and TexturePipelineMode (unless the blend pipeline is kept); Texture.vert/frag.hlsl; ShaderProgram TexturedVertex/TexturedFragment and their ShaderLibrary and RavenContent.cmake rows; CheckProgram cases at VulkanContext.cpp:409-414 and TexturedVertexInterface (:394-400); FragmentShaderKind::Textured; the textured group, publish('Textured') and the else branch (:454, 471-472, 495-499); ReplaceTexturedPipelines with its dead canPublish parameter (VulkanClearPass.hpp:107-108); the dead public ReplaceTexturedFragment (VulkanContext.cpp:936-944; VulkanContext.hpp:104); RecordTexturedBatches and TexturedPipelines; RingOffsets::Textured (VulkanClearPass.hpp:139); QueuedTexturedDraw, TexturedInstanceData and TexturedBatch (VulkanDrawQueue.hpp:18-46); the OwnsResource scans (VulkanContext.cpp:52-60, 72-78, 685-690); CreateTexturedMesh; Public VulkanTexturedDraw.hpp, VulkanTransparentDraw.hpp, TexturedVertex.hpp; GatherSprites, GameSession's QuadVertices and m_Quad (GameSession.cpp:37-42, :114) and the sprite vectors; Visibility.hpp TransformBounds and Intersects (unless AA2-RND-03 keeps CPU visibility); PrepareTransform (VulkanDrawQueue.cpp:20-23); per-texture samplers and set-0 descriptors; the OpaqueBatches/TransparentBatches metric terms (VulkanContext.cpp:712-714); the Interpolation Restore pass (CQ-CORE-12); the camera-only DrawFrame closure (CQ-VK-12, if not already deleted in Phase 3). AA2-CORE-04 step 4 already covers the textured mesh and draw entry points, and CQ-VK-05 the samplers.
     - Keep and re-home: ViewFrustum, FrameUniformData construction, SceneLighting::Validate, the blended-sprite back-to-front sort (AA2-RND-03 step 1), and the reason in the Cull.comp.hlsl:41 tolerance comment.
  4. At B5: delete in three snippets (shaders and program table; pipeline and pass members; context entry points and public headers in the contract window).
- **Effort:** S.
- **Timing:** steps 1-2 now, after or with AA2-RT-01 step 1; step 3 with AA2-RND-03 step 6, after Q8 and Q9; step 4 delete with B5.
- **Overlaps:** AA2-CORE-04 step 4, AA2-RND-03 (steps 1 and 6), AA2-PERF-01 step 5, AA2-RT-01.
- **Panel discussion:** RFP-2 originally claimed the GPU path's camera validation happened only by accident before acquire. GP and DATA showed the camera is validated by GetCameraView, so RND withdrew it to low and folded it here. The lead kept the merged finding at medium; verification lowered it.
- **Verification:** overstated; severity medium -> low because the residual risk is a one-time deletion step whose failure (a blank lit scene) the first B5 test run exposes, and AA2-RT-01 step 1 adds a second lighting validation.

#### CQ-RND-03 Overlay and debug-line fragment reloads are never interface-checked, and two comments say they are

- **Severity:** medium.
- **Category:** slop, stale comment.
- **Sources:** RFP-3; verified by VK and MEDIA.
- **Evidence:**
  - VulkanContext.cpp:424-426 ("pipeline creation checks the match", DebugLineFragment) and :430-432 ("pipeline creation checks both", OverlayFragment). Both cases break without a check.
  - VulkanOverlayPipeline.cpp (164 lines) and VulkanDebugLinePipeline.cpp (151 lines) contain no interface check, only VkResult and argument throws (:19, :103, :128 and :20, :92, :117). vkCreateGraphicsPipelines does not validate stage interfaces. With RAVEN_ENABLE_VALIDATION, a mismatch is logged by VulkanDebugMessenger.cpp:22-24 but the shader is still published, so the accurate claim is "not rejected", not "not detected".
  - Overlay.frag.hlsl:3-4, 9-20 redeclares the bindless set and the 8-location QuadOutput (including a uint2 at location 7) by hand. FragmentShaderInterface.hpp:9 supports only Textured and Lit kinds; neither fits the overlay or the debug line (float4 at location 0).
- **Why it matters:** a bad edit to the UI fragment shader, which every 2D, UI and text draw uses, is published instead of rejected. DXC and spirv-val still run, so only interface mismatches slip through, and the result is wrong UI output, not a crash. That is a hole in last-good recovery, and the comments tell reviewers it is covered. The debug-line fragment serves only the developer collider view (FD-32) and carries less risk.
- **Fix:**
  1. Now: correct both comments to say the fragment interface is unchecked (VulkanContext.cpp). Ship it in the same snippet as CQ-RND-01 step 1 (same function), so it needs no separate review round.
  2. After CQ-VK-03 steps 1-3: add FragmentShaderKind::Overlay first (locations 0-7 matching Overlay.vert outputs, set 0 bindings 0/1), then ::DebugLine (location 0, float4), on the shared SpirvModule. Wire both into CheckProgram.
- **Effort:** S.
- **Timing:** step 1 now, with CQ-RND-01 step 1; step 2 before B5 extends the overlay format to sprites.
- **Overlaps:** AA2-RND-03, CQ-VK-03, CQ-RND-10.
- **Panel discussion:** VK asked that step 2 land on the shared parser, not the current hex-literal checker. Accepted.
- **Verification:** confirmed; severity medium -> medium because the comments misstate a last-good safety property for the shader that every 2D draw uses.

#### CQ-RND-04 Dirty object records are copied twice per frame

- **Severity:** medium.
- **Category:** slow, redundant pass.
- **Frequency:** per frame, per dirty object (about 10k at 50k with 20% animated, derived).
- **Sources:** RFP-4; verified by VK.
- **Evidence:**
  - VulkanGpuScene.cpp:317-327: Plan copies each in-range dirty slot and its 128-byte GpuObjectData (static_assert at RenderWorldState.hpp:37) into m_DirtySlots and m_DirtyRecords.
  - VulkanGpuScene.cpp:374-379: Upload memcpys both vectors into the mapped ring (VulkanFrameData.cpp:51-60).
  - Call chain: VulkanContext.cpp:711 (Plan), then ClearPass::Draw (:716), then Upload (VulkanClearPass.cpp:541, through :309), on one thread. Between Plan and Upload there is only the timeline wait (:530), and nothing writes the world in between. grep shows the two vectors are used only in Plan, Upload and RecordPrepare (:433-440), so they can be replaced by a count.
  - The ring is the first HOST_VISIBLE|HOST_COHERENT memory type (VulkanBuffer.cpp:21-43). Both designs write the ring exactly once, so the ring's memory type does not change the saving.
  - Recorded total: renderer CPU (preparation plus record/submit) p95 is 0.53 ms at 50k (GpuDrivenDesign.md:228). Plan's copy is timed under RenderPrepare and Upload's under RecordSubmit (VulkanContext.cpp:682-715; VulkanClearPass.cpp:532-542). It is unverified which of the two counts toward the 3.24-3.42 ms "CPU preparation" p95 used as headroom below.
- **Why it matters:** derived: 10,000 x 128 B = about 1.28 MB written into the reused vector and read back from it per frame, through cached memory. The direct write still reads Objects[slot] scattered and still writes the ring once, so the saving is roughly 0.03-0.13 ms (estimate), probably nearer the low end. The 50k budget has 0.1-0.25 ms of headroom (3.24-3.42 ms p95 against 3.5 ms, DataModelDesign.md:661), and AA2-PERF-01 grows the same stream.
- **Fix:**
  1. Measure a baseline first with the 50k/20% benchmark, back to back with the after run, because run-to-run noise is about 0.1 ms (RendererDesign.md:224).
  2. VulkanFrameData.hpp/.cpp: add `VulkanUploadRing::Allocate(bytes)` returning {offset, std::span<std::byte>}, and implement Push on it. Contract comment: the span is write-only and must be filled front to back with whole-record memcpy.
  3. VulkanGpuScene.hpp/.cpp: Plan stores the filtered dirty count (the same `slot < m_ObjectCount` filter) instead of filling the two vectors; delete m_DirtySlots and m_DirtyRecords.
  4. Upload allocates the slot and record regions and writes `world->Objects[slot]` straight into the ring, slots first, then records, in increasing offset order, and asserts that it wrote exactly the count Plan stored. A mismatch makes Push/Allocate throw length_error, which is fatal per AA2-RT-02, so land CQ-RND-11's layout assertion in the same snippet. State the Plan-to-Upload no-mutation rule at VulkanGpuScene.hpp:48-52.
  5. Record the before and after numbers in GpuDrivenDesign.md.
- **Effort:** S.
- **Timing:** now, or the first B4 GpuScene snippet. Order with CQ-RND-11 and CQ-VK-06: see the fix plan.
- **Overlaps:** AA2-PERF-01, CQ-RND-11. AA2-RND-02 step 6 only edits the same Plan and Upload functions (it makes the mesh table persistent, not the object records).
- **Panel discussion:** VK answered RND's write-combining question; the direct write is also the better shape for that memory.
- **Verification:** confirmed with corrections; severity medium -> medium because headroom at 50k is thin (DataModelDesign.md:661), though the saving is an unmeasured estimate at the low end.

#### CQ-RND-05 Each glyph upload gets its own pair of layout barriers on the same atlas image

- **Severity:** medium.
- **Category:** slow, redundant pass.
- **Frequency:** per frame that finishes glyphs, per glyph (startup, screen changes, F6 locale switches, CJK text).
- **Sources:** RFP-5; endorsed by VK and MEDIA.
- **Evidence:**
  - TextSystem.cpp:608: one Uploads.push_back per glyph, inside Place (:580-618), which appends only to Pages.back() (:586-611); new pages only append. TakeUploads places every finished glyph with no per-frame cap (:742-751), and unpresented uploads stay at the front of the list (EndFrame :759-764), so uploads are ordered by page and same-image uploads are consecutive. GlyphGap=1 (:44, :582-583) and shelf packing keep regions disjoint.
  - Call chain: GameSession.cpp:453 -> VulkanContext::DrawFrame :969-980 (resolve into m_ResolvedUploads) -> DrawFrameWithView :708 (swap into m_DrawQueue.Uploads) -> VulkanClearPass::Draw :541/:558 -> Upload/Record.
  - VulkanClearPass.cpp:419-449: for every upload, a SHADER_READ->TRANSFER_DST barrier (:422-434), one 1-region vkCmdCopyBufferToImage (:435-440) and a barrier back (:441-448), each through the `transition` lambda (:381-388), so two separate vkCmdPipelineBarrier2 calls per glyph on the same atlas image. These are recorded before BeginRendering (:472), so they delay the whole frame's GPU work on those frames.
  - VulkanClearPass.cpp:302-308: RingOffsets::Uploads (VulkanClearPass.hpp:142) is a local returned by value with no reserve, so a frame with N uploads makes about log2(N)+1 heap allocations; frames with no uploads make none.
- **Why it matters:** estimate: 300 new glyphs (one CJK paragraph) record 600 image barriers, plausibly 0.2-1 ms of serialized GPU time. That is an upper bound for one frame: glyphs are generated asynchronously at Background priority (TextSystem.cpp:574-575), so a paragraph's uploads spread over several frames. On a frame that opens a new page, the synchronous 4 MB page creation (SubmitAndWait, TextSystem.cpp:612-615; AA2-MEDIA-03) probably costs more than the barriers. The hitch lands exactly when UI text appears. Unmeasured.
- **Fix:**
  1. VulkanClearPass.cpp/.hpp: extract `RecordTextureUploads(commands, offsets)`. Make it the shared helper AA2-RND-04 and CQ-VK-06 also use. This step is also CQ-RND-06 step 1.
  2. Inside it: collect one image barrier per distinct image into a single VkDependencyInfo (one vkCmdPipelineBarrier2 for all images), record one vkCmdCopyBufferToImage per image with N regions, then issue one barrier call back. This does not depend on the page-ordering invariant, which holds today only because TextSystem places append-only. Keep each region's own aligned ring offset (VulkanUploadRing::Alignment is 16 bytes, VulkanFrameData.hpp:57, which satisfies the RGBA8 bufferOffset rule) and keep the batch per frame, because unpresented uploads are re-listed (TextSystem.cpp:759-764).
  3. Trivial: `offsets.Uploads.reserve(queue.Uploads.size())` in Upload (a member vector would conflict with Upload returning RingOffsets by value, VulkanClearPass.cpp:276, 541).
  4. Record GPU time for a pseudo-locale switch before and after in UIDesign.md (metrics.Gpu comes from the existing timestamps, VulkanClearPass.cpp:333-353; no new instrumentation and no tracked test).
- **Effort:** S.
- **Timing:** now, ahead of CQ-RND-06 steps 1-3 (which reuse step 1); align with AA2-RND-04's batch. No dependency on CQ-RND-04: the glyph pixels still go through Push.
- **Overlaps:** AA2-MEDIA-03 (the synchronous page-creation stall, out of scope here and not merged), AA2-RND-04, CQ-RND-06.
- **Panel discussion:** MEDIA chose renderer-side batching over a 4 MB-per-page CPU mirror (about 100 MB for Hangul, derived). The lead agrees.
- **Verification:** confirmed with corrections; severity medium -> medium because the redundancy is real and user-visible as a text-appearance hitch, but contained and not at 50k scale.

#### CQ-RND-06 VulkanClearPass is misnamed and does every per-frame GPU job

- **Severity:** medium.
- **Category:** readability, long function.
- **Sources:** RFP-6.
- **Evidence:**
  - VulkanClearPass.cpp:355-519: Record is 165 lines: timestamps (:367-376), GPU-scene prepare (:374), begin barriers (:390-415), glyph uploads (:417-449), BeginRendering (:451-472), six draw record sites (:478-498: lit filled, lit wireframe overlay, textured opaque, textured transparent, debug lines, overlay; B5 deletes the two textured ones), readback (:500) and the present barrier (:502-515). It is linear and its barrier choices carry good why-comments (:390-391, :417-418, :502-503), so it is long rather than unreviewable.
  - VulkanClearPass.cpp:140-246: the constructor is 107 lines. VulkanClearPass.hpp:95-102 has 17 parameters: 6 shader spans (textured, debug-line and overlay vertex and fragment, hpp:99-101) plus a LitShaders struct (hpp:98) of 5 more spans (hpp:37-44).
  - VulkanClearPass.cpp:521-610: Draw is 90 lines (wait, upload, acquire, record, submit, commit, present).
  - Context side: VulkanContext.cpp:448-517 (ApplyShaderReloads, 70 lines), :674-740 (DrawFrameWithView, 67 lines).
  - The stale name repeats in the CheckClearResult/CheckClearCreation helpers (:16-28; CheckClearResult is a forwarding wrapper) and the message "A drawable swapchain is required for the clear pass" (:155). The only other user of the class is VulkanContext.cpp (:13, 120, 182, 190, 196, 203, 271, 441).
- **Why it matters:** every M6B rendering step (B4, B5, B6) edits this file, and barrier order in Record is load-bearing. The name hides where the frame is recorded.
- **Fix (each step one small snippet in this file):**
  1. RecordTextureUploads (CQ-RND-05 step 1, which lands first).
  2. Replace the `transition` lambda (:381-388) with a file-local `RecordImageBarriers(vk, commands, span)` in the anonymous namespace (:14-48), and extract RecordBeginAttachments and RecordPresentTransition (barriers at :390-415, :502-515) as free functions there that take m_ColorImages, m_DepthAttachments and the device's sync2 function table (vk.PipelineBarrier2) as parameters.
  3. Extract CreateFrameResources (:187-239: command pool, command buffers, frame descriptor pool and sets, semaphores) together with the per-image depth attachments (:182-185), so the constructor keeps only validation and pipeline selection.
  4. At B5, after the textured members are deleted: rename the class to VulkanFramePass, use the PassShaders struct (CQ-RND-01 step 4), rename or fold the CheckClear* helpers into CQ-VK-02's shared result helpers, fix the :155 message, and update the docs that name the class (HotReloadDesign.md:21; TextureResources.md:100, :116).
- **Effort:** S per step.
- **Timing:** steps 1-3 in Phase 2 or during B4, after CQ-RND-05 and after CQ-VK-01 steps 1-4 (see the fix plan); step 4 during B5.
- **Overlaps:** CQ-VK-01, CQ-RND-01, CQ-RND-05, CQ-VK-02.
- **Panel discussion:** RND and VK proposed opposite orders for this and CQ-VK-01. The lead decides: CQ-VK-01 steps 1-4 first (they touch LitPipelineFactory at :50-80 and the constructor's pipeline section), then steps 1-3 here (disjoint ranges). Rename once, at B5.
- **Verification:** confirmed with corrections; severity medium -> medium because every M6B step edits this file and the barrier order matters, though no bug has resulted; effort revised from M to S per step.

#### CQ-RND-07 The public RenderWorld comment says sync cost grows with changed objects; the code walks every mesh

- **Severity:** low.
- **Category:** slop, stale comment.
- **Sources:** RFP-7.
- **Evidence:**
  - Public/Raven/Renderer/RenderWorld.hpp:19-20: "its cost grows with changed objects, not scene size".
  - RenderWorld.cpp:103-122: ApplyTicks iterates every mesh every frame; measured 0.90-0.93 ms at 50k (DataModelDesign.md:661).
  - VulkanFrameData.hpp:16 names only the lit vertex shader as the uniform's reader; DebugLine.vert.hlsl:13 also reads it.
  - VulkanContext.cpp:233 prints "M3 features" (milestone narration in a runtime log). The same line prints the always-true DescriptorIndexing (CQ-VK-08).
- **Why it matters:** B4 planners read this comment for the cost model, and it contradicts the measurement AA2-PERF-02 schedules a fix for.
- **Fix:**
  1. RenderWorld.hpp:19-20: say lifecycle changes are O(changed) and value changes are found by a per-frame walk of every mesh until B4. Restore the claim in the B4 snippet that makes it true.
  2. Fix VulkanFrameData.hpp:16.
  3. Change the log text at VulkanContext.cpp:233 to "GPU-driven features", in the same snippet as CQ-VK-08 step 4.
- **Effort:** S.
- **Timing:** step 1 in the Phase 1 † commit (public header); steps 2-3 now.
- **Overlaps:** AA2-RND-10, AA2-PERF-02.
- **Panel discussion:** API notes RenderWorld.hpp leaves the contract-hashed Public tree in Phase 3 (AA2-CORE-03 steps 3-4, the RavenEngine/Host/Raven root; CQ-X-08), so the B4 restoration no longer changes the game contract.

#### CQ-RND-08 Seven scene descriptors are rewritten and a readback recorded every frame

- **Severity:** low.
- **Category:** slow, redundant pass.
- **Frequency:** per frame.
- **Sources:** RFP-8; verified by VK.
- **Evidence:** VulkanGpuScene.cpp:383-404 writes 7 descriptors unconditionally, while the frame set is rewritten only on ring replacement (VulkanClearPass.cpp:287-298). VulkanGpuScene.cpp:481-490 copies to the readback every frame (called at VulkanClearPass.cpp:500), though ReadVisible runs only with metrics (VulkanClearPass.cpp:533-539).
- **Why it matters:** microseconds (estimate). Listed so that B4's per-pipeline ranges copy the conditional form.
- **Fix:**
  1. Keep the last-written VkBuffer handles per slot and skip vkUpdateDescriptorSets when unchanged (VulkanGpuScene.hpp/.cpp).
  2. Pass a readback flag from Draw into Record and RecordReadback (VulkanClearPass.cpp, VulkanGpuScene.cpp).
- **Effort:** S.
- **Timing:** during B4, in the GpuScene snippets.
- **Overlaps:** AA2-RND-02 step 6.
- **Panel discussion:** none.

#### CQ-RND-09 Five copies of the finite/affine matrix check, with different behavior

- **Severity:** low.
- **Category:** slop, duplicate.
- **Sources:** RFP-10 (step 1 withdrawn under no-throwaway: ReplaceTexturedPipelines and its dead canPublish parameter are deleted at B5 and listed in CQ-RND-02).
- **Evidence:** RenderWorld.cpp:21-28 (IsDrawable returns false), Visibility.hpp:13-18 (TransformBounds throws), VulkanDrawQueue.cpp:20-23 (PrepareTransform throws), Transform.hpp:168 (TryNormalMatrix) and :193 (NormalMatrix re-checks). Related copy: JoltSupport.hpp:41-43, 80 (finite and length tests).
- **Why it matters:** a reviewer cannot tell which rule is canonical, and the copies already differ (skip versus throw).
- **Fix:**
  1. Move IsDrawable into a private Renderer/MatrixChecks.hpp, and have TransformBounds and PrepareTransform call it until B5 (per AA2-RT-01 step 2).
  2. Route NormalMatrix's duplicate test through TryNormalMatrix (Transform.hpp, public: Phase 3 with CQ-API-10's shared constant).
  3. At B5 delete TransformBounds, Intersects and PrepareTransform (CQ-RND-02).
- **Effort:** S.
- **Timing:** step 1 now; step 2 Phase 3; step 3 delete with B5.
- **Overlaps:** AA2-RT-01, CQ-API-10.
- **Panel discussion:** none beyond the withdrawal.

#### CQ-RND-10 Hand-copied HLSL declarations, an implicit frames-in-flight count, and magic literals

- **Severity:** low.
- **Category:** readability, duplicate.
- **Sources:** RFP-11 (step 5 moved to CQ-VK-04); MEDIA added the overlay flags.
- **Evidence:**
  - FrameData declared in Color.vert.hlsl:4-12 and DebugLine.vert.hlsl:3-11. QuadOutput in Overlay.vert.hlsl:22-34 and Overlay.frag.hlsl:9-20. RavenTextures/RavenSampler in Raven/Material.hlsli:10-11 and Overlay.frag.hlsl:3-4. FlagImage/FlagGlyph in Overlay.frag.hlsl:6-7 duplicate OverlayFlags (Overlay.hpp:14-22).
  - Scatter.comp.hlsl:22-28 hard-codes 128u, >>3, &7, 16u; VulkanGpuScene.cpp:440 repeats `* 8`. Cull.comp.hlsl:26 tests `Flags & 1u`.
  - Frames in flight: std::array<Frame, 2> (VulkanClearPass.hpp:176), literal 2 at VulkanClearPass.cpp:194, 213, 215, and VulkanGpuScene::FrameSlots = 2 (VulkanGpuScene.hpp:30), coupled only through `.at()` at run time.
  - Timestamp literal 3 at VulkanClearPass.cpp:328, 337, 340, 367, 370; clear color :457; 256-byte minimum VulkanGpuScene.cpp:261, 356; 64 KiB ring VulkanFrameData.cpp:41.
- **Why it matters:** each copy is a place to forget during M6B. The interface checker catches C++-to-HLSL drift but not HLSL-to-HLSL drift.
- **Fix:**
  1. Add Shaders/Raven/Frame.hlsli and Shaders/Overlay.hlsli (QuadOutput, bindless declaration, flags); include them from the four shaders. DXC includes are already followed (ShaderLibrary.cpp:197-211).
  2. Add RAVEN_OBJECT_STRIDE and RAVEN_OBJECT_HIDDEN to GpuScene.hlsli, and `constexpr ScatterThreadsPerRecord = sizeof(GpuObjectData) / 16`.
  3. Add `inline constexpr std::size_t FramesInFlight = 2` to VulkanFrameTimeline.hpp; use it in both classes with a static_assert.
  4. Name TimestampsPerFrame and the timestamp indices in VulkanClearPass.cpp.
- **Effort:** S.
- **Timing:** steps 1, 3, 4 now; step 2 with B2/B4 (AA2-RND-05 step 3).
- **Overlaps:** AA2-RND-05, AA2-RND-01, CQ-VK-04.
- **Panel discussion:** none.

#### CQ-RND-11 Ring sizes are computed apart from the pushes they must cover, in two classes

- **Severity:** low.
- **Category:** readability, hidden ordering.
- **Sources:** RFP-12.
- **Evidence:** VulkanClearPass.cpp:282-286 (a nested Align expression plus GetPlannedBytes) must match the push sequence at :300-309. VulkanGpuScene.cpp:328-336 must match Upload's pushes at :365-380. An undersized plan throws std::length_error (VulkanFrameData.cpp:54-55), which AA2-RT-02 classes as fatal.
- **Why it matters:** correct today, but each M6B ring user (B2 materials, B5 sprites, B6 uploads) must be added in two places, and a miss crashes on the first frame that uses it.
- **Fix:**
  1. Add a VulkanUploadRing::Layout accumulator (Add returns the aligned offset; Total) in VulkanFrameData.hpp, in the same snippet as CQ-RND-04 step 2 (Allocate).
  2. Build ClearPass::Upload's layout from the same list of spans it pushes.
  3. GpuScene::Plan records its pushes in the accumulator; Upload asserts the offsets match.
- **Effort:** S.
- **Timing:** before B5/B6 add ring users.
- **Overlaps:** AA2-RT-02, CQ-RND-04.
- **Panel discussion:** none.

#### CQ-RND-12 Lit HLSL carries frame constants as per-vertex varyings; cull uses one global atomic per visible object

- **Severity:** low.
- **Category:** slow, redundant pass.
- **Frequency:** per vertex and per visible object per frame (GPU).
- **Sources:** RND-14 (new in round 2).
- **Evidence:** Color.vert.hlsl declares three nointerpolation varyings at :35-37 and writes AmbientAndLit, LightDirection and LightColor into them at :55-57: 10 varying components (locations 3-5) for every vertex, because the frame set is vertex-only (VulkanFrameData.cpp:13-14). Raven/Material.hlsli:13-24 freezes them into RavenFragmentInput. Cull.comp.hlsl:47-48 does one InterlockedAdd per visible object; with two meshes, about 50k atomics land on two addresses per frame. Measured upload+cull is 0.17 ms at 50k (GpuDrivenDesign.md:230).
- **Why it matters:** small GPU cost today. The value is the ABI: once B4 publishes Material.hlsli v2 to designers, locations 3-5 become permanent weight in every material shader and in the checker.
- **Fix:**
  1. With AA2-RND-01 steps 1 and 3 (the first B4 snippet): expose ambient and light to the fragment stage through the frame set and drop the frame constants from locations 3-5 before v2 is recorded. AmbientAndLit.w is the per-material Lit flag (Color.vert.hlsl:55, material.Lit), not a frame constant, so it must still reach the fragment stage, either as a flat per-material value or folded into location 6's material path. Validate with one capture compare at M2 tolerance.
  2. Only if AA2-PERF-04 shows cull time rising: per-wave aggregation with one atomic per wave, behind a subgroup feature check (AA2-RND-07).
  3. Name RAVEN_OBJECT_HIDDEN with CQ-RND-10 step 2.
- **Effort:** S.
- **Timing:** step 1 during B4; step 2 later if measured.
- **Overlaps:** AA2-RND-01, AA2-RND-07, CQ-VK-03.
- **Panel discussion:** none.

#### CQ-RND-13 Every shader edit runs DXC's dependency scan in series before the compile: four process launches on the save-to-visible path, which has 32 ms of median headroom

- **Severity:** medium.
- **Category:** slow, redundant pass.
- **Frequency:** per shader edit, and per program at every develop launch; after B4 also per material-shader edit.
- **Sources:** completeness critic (CRIT-3).
- **Evidence:**
  - ShaderLibrary.cpp:156-185: Import always passes `-DDEPENDENCIES=` (:168), starts one `cmake -P CompileShader.cmake` (:173-174) and polls it every 2 ms (:176-181). Only after it exits does FollowIncludes run (:182-185).
  - cmake/CompileShader.cmake:9-19: when DEPENDENCIES is defined, a full `dxc ... -MF` pass runs first; :20-27 then runs the real DXC compile and :28-34 runs spirv-val. Every edit is cmake + dxc + dxc + spirv-val, strictly in series.
  - Recorded: "-MF ... writes the list and does not compile", so it is its own pass; about 100 ms per program in total (HotReloadDesign.md:307). Fragment save-to-visible is 248-432 ms, median 268 ms, against a 300 ms target, "Met at the median" (HotReloadDesign.md:326).
  - CompileShader.cmake has no phase timing, unlike BuildGame.cmake.in:12-31.
  - AA2-HR-01 covers worker blocking, startup compiles and lanes (its step 3 reuses the -MF pass for startup digests). No AA2 or CQ item covers the serial scan on the per-edit path.
- **Why it matters:** shader iteration (Goal 3) has 32 ms of headroom at the median, and B4 sends every designer material-shader edit down the same path. Estimate (unmeasured): the scan is 20-40 ms per edit, one DXC process start plus preprocessing. Running the scan beside the compile removes it from the latency without weakening the rule that a failed compile still follows its includes. The 432 ms first-edit outlier is not attributable to the scan, which runs on every edit; it points to cold process or disk caches.
- **Fix:**
  1. CompileShader.cmake: add RAVEN_RELOAD_TRACE phase timing (scan, compile, validate) with the BuildGame.cmake.in:17-25 macro shape. Record one measured edit in HotReloadDesign.md.
  2. Only if the scan is 10% of the median or more: add a SCAN_ONLY mode to CompileShader.cmake (the -MF pass alone), and have ShaderLibrary::Import start the scan and the compile as two BuildProcesses at once, wait for both, then FollowIncludes and check the exit code as today. Two files. If step 1 shows CMake interpreter startup is a large part of the time, start DXC directly for the scan process (still through Platform::StartBuildProcess) instead of a second `cmake -P`; decide from the step 1 data.
  3. When AA2-HR-01 step 1 makes process waits non-blocking, register both processes with the service instead of the 2 ms sleep loop.
  4. No new test; measure before and after in Release on the laptop.
- **Effort:** S.
- **Timing:** steps 1-2 before M6B; step 3 with AA2-HR-01 step 1.
- **Overlaps:** AA2-HR-01.
- **Panel discussion:** not discussed by the panel; added in verification.
- **Verification:** confirmed with corrections (critic proposal verified separately); severity medium (new) because it sits on the shader reload loop with little headroom, provided the cost stays labeled an estimate until step 1 measures it.

### 5.2 VK: Vulkan resources, pipelines and renderer headers

#### CQ-VK-01 Four graphics pipelines copy the same ~75-line create-info block, and the copies differ in style

- **Severity:** medium (revised from high in verification).
- **Category:** slop, duplicate.
- **Sources:** VKR-1; endorsed by RND.
- **Evidence:**
  - VulkanColorPipeline.cpp:13-149 (137-line constructor), VulkanTexturePipeline.cpp:13-159 (147), VulkanOverlayPipeline.cpp:13-135 (123), VulkanDebugLinePipeline.cpp:13-124 (112): 519 lines, with stages, vertex input, viewport, dynamic state, raster, multisample, blend, rendering info, layout and create-info line-for-line alike (the stage blocks Color :28-36 = Texture :26-34 = Overlay :24-32 = DebugLine :25-33). grep vkCreateGraphicsPipelines finds exactly these four files (VulkanGpuScene creates only compute pipelines).
  - The copies differ in style. None of these differences changes behavior today; each is a maintenance hazard for the next edit:
    - Overlay checks colorFormat but not depthFormat (VulkanOverlayPipeline.cpp:18) and still passes depthFormat (:110). Every caller passes the same m_DepthFormat that DebugLine and Color validate (VulkanClearPass.cpp:125, :134), and passing a matching depth format is required for dynamic-rendering compatibility, so the gap is only a missing precondition check (a nit).
    - Color and Texture create the layout into a local (VulkanColorPipeline.cpp:107-111; VulkanTexturePipeline.cpp:117-121); Overlay and DebugLine write m_Layout directly (VulkanOverlayPipeline.cpp:99-104; VulkanDebugLinePipeline.cpp:88-93). Both end with a null or valid handle on failure.
    - Overlay and DebugLine Destroy null their handles (:161-162, :148-149); Color and Texture do not (VulkanColorPipeline.cpp:176-182; VulkanTexturePipeline.cpp:174-180). No double-Destroy path exists: Destroy runs only from the destructor or from catch-then-rethrow in the constructor.
    - Texture takes VulkanShaderModule& where the others take SPIR-V spans; two unrelated render-state enums (VulkanColorPipeline.hpp:15, VulkanTexturePipeline.hpp:8-12); the straight-alpha blend block appears twice (Texture :98-107, Overlay :77-84).
  - VulkanClearPass.cpp:58-73: LitPipelineFactory::Create builds three or four VulkanColorPipeline objects (Lines only when FillModeNonSolid is available, :70-71), each with an identical 3-set layout (VulkanColorPipeline.cpp:102-111) and its own vertex module from the same words (:25).
- **Why it matters:** B4 builds one pipeline per shader (RenderingContentDesign.md:78) keyed by (material shader, blend class, cull mode) on the same vertex stage and layout (AA2-RND-02 step 1, ArchitectureAssessment-2026-11-26.md:896; the B4 row at RenderingContentDesign.md:211; AA2-RND-06). B5 needs a straight-alpha blend pipeline (AA2-RND-03 step 1). Without one helper, B4 becomes a fifth copy or N copies of the layout, and a fix to one copy misses the others. The code runs only at startup, shader reload and pipeline rebuild, so this is preparation for B4, not a hot-path cost.
- **Fix:**
  1. Add Private/Renderer/Vulkan/VulkanGraphicsPipeline.hpp/.cpp: `struct GraphicsPipelineDesc` (shader words, vertex bindings and attributes, topology, polygon mode, depth test/write/compare, depth bias, BlendMode {Opaque, StraightAlpha}, cull mode, color and depth formats, push-constant ranges, layout) and `CreateGraphicsPipeline(device, desc)`, with the failed-output rule and its comment written once. Write the depth-format rule once: either VK_FORMAT_UNDEFINED is allowed explicitly, or a defined format is required, so Overlay's missing check is decided rather than dropped by accident. Add a small VulkanPipelineLayout owner.
  2. Port VulkanDebugLinePipeline (the diff is mostly deletion).
  3. Port VulkanOverlayPipeline (it keeps its own push-constant range, VulkanOverlayPipeline.cpp:92-98, and set layouts), then VulkanColorPipeline, keeping their Bind/Record API.
  4. Share one pipeline layout and one vertex module across the lit pipelines. The vertex module is a local inside LitPipelineFactory::Create (pipelines do not need it after creation). The layout must not be owned by LitPipelineFactory or by VulkanClearPass: the factory is a non-owning value copied into LitPipelineBuild and run on a worker (VulkanContext.cpp:193-197, :560), and LitPipelines bundles are retired through m_Timeline and kept across swapchain and ClearPass recreation (m_RetainedPipelines, VulkanContext.cpp:202-203; IsCurrent at VulkanClearPass.cpp:86-90). Have each LitPipelines bundle co-own the layout through a `shared_ptr<const VulkanPipelineLayout>`, or give it a context-lifetime owner that outlives every retained bundle. This is cold-path hygiene: RecordDraw runs at most twice per frame (VulkanClearPass.cpp:481, :485), and separate layouts made from identical set layouts are already compatible, so bound sets already survive pipeline switches (VulkanColorPipeline.hpp:27). Binding sets once per frame in B4 is an API choice in RecordDraw, not something the shared layout unlocks.
  5. Do not port VulkanTexturePipeline: delete with B5.
  6. At B4, map the material pipeline key onto GraphicsPipelineDesc fields.
- **Effort:** M.
- **Timing:** steps 1-4 before B4 (Phase 2), before CQ-RND-06 steps 1-3 as planned; step 5 delete with B5; step 6 during B4.
- **Overlaps:** AA2-RND-06 (step 3 "Debug views keep the engine pipeline bundle" and fallback step 4 keep VulkanColorPipeline, so porting it is not throwaway), AA2-RND-02 step 1, AA2-RND-03 step 1, AA2-CORE-04 step 4.
- **Panel discussion:** VK lowered its own finding to medium because no drift is a live bug today. RND kept high and confirmed that B4 keeps VulkanColorPipeline (debug views and the lit fallback stay, AA2-RND-06 steps 3-4), so porting it is not throwaway. The lead kept high; verification lowered it, matching VK's own rating.
- **Verification:** confirmed with corrections; severity high -> medium because none of the four differences changes behavior and the code runs only at startup and rebuild, so "likely to cause a bug" is unproven; the B4 trigger keeps it a planned Phase 2 item.

#### CQ-VK-02 Nine per-file VkResult wrappers; one reachable site bypasses the device-lost message

- **Severity:** low (revised from medium in verification).
- **Category:** slop, duplicate.
- **Sources:** VKR-2, absorbing RFP-9.
- **Evidence:**
  - Pure forwarders: CheckTextureResult (VulkanTexture.cpp:16-19), CheckMeshResult (VulkanMesh.cpp:24-27), CheckSceneResult (VulkanGpuScene.cpp:46-49), CheckClearResult (VulkanClearPass.cpp:16-19).
  - Re-implementations that skip VulkanDeviceLostError: CheckBufferResult (VulkanBuffer.cpp:14-19), CheckDepthResult (VulkanDepthAttachment.cpp:11-15), CheckDescriptorResult (VulkanTextureDescriptors.cpp:15-19), Check (VulkanPhysicalDevice.cpp:16-24), CheckSwapchain (VulkanSwapchain.cpp:17-22).
  - Only one of the calls these five wrap can return VK_ERROR_DEVICE_LOST under the spec: vkCreateSwapchainKHR (VulkanSwapchain.cpp:170-175), which becomes "Create swapchain: -4" instead of the single device-lost message (VulkanDevice.cpp:10-18). vkCreateImageView (:199), vkGetSwapchainImagesKHR, buffer and image creation, memory allocation, binding and mapping, the descriptor calls, vkCreateSampler and the physical-device queries cannot.
  - Nothing catches VulkanDeviceLostError by type (grep: VulkanDevice.hpp:11 and VulkanDevice.cpp:15 only). Inside StageBoundary (Application.cpp:90-109) both error types are std::runtime_error and are logged as recoverable; outside it both are fatal. So the misclassification changes message text at one site and no behavior.
  - 21 inline `"... failed: " + std::to_string(result)` throws or logs (grep `to_string(result)`), including Win32Window.cpp:165, XcbWindow.cpp:369 and CocoaWindow.mm:209, and message formats differ ("X failed: N" vs "X: N"). All print raw enum values.
  - CheckClearCreation (VulkanClearPass.cpp:21-28) is useful but file-local; VulkanFrameData.cpp:19-24 and VulkanFrameTimeline.cpp:19-24 re-implement it. VulkanGpuScene.cpp:79-86 is similar but destroys a non-null pipeline on failure instead of nulling the handle (for pipelines the spec already nulls failed elements).
- **Why it matters:** raw numbers like -1000001004 need a header lookup, and nine wrappers invite a tenth. If device loss must stay fatal inside recoverable stages, the real fix is a VulkanDeviceLostError rethrow in StageBoundary, which belongs to AA2-RT-01; this wrapper sweep does not deliver it.
- **Fix:**
  1. VulkanDevice.hpp/.cpp: add `VulkanResultName(VkResult) noexcept`, using string_VkResult from vulkan/vk_enum_string_helper.h if the stripped CI SDK (windows.yml, stripdown: true) ships that header, otherwise a local switch over about 20 codes; CheckVulkanResult prints "<op> failed: <name> (<value>)"; move CheckClearCreation here as CheckVulkanCreation(result, handle, operation).
  2. Delete the four forwarders (VulkanTexture.cpp, VulkanMesh.cpp, VulkanGpuScene.cpp, VulkanClearPass.cpp).
  3. Convert the five re-implementations (VulkanSwapchain.cpp first, then the others).
  4. Remaining inline sites ride the snippet that already edits their file: the pipeline files with CQ-VK-01; VulkanFrameData.cpp:23 with CQ-RND-04; VulkanFrameTimeline.cpp:23 with CQ-RND-10 step 3; VulkanShaderModule.cpp:49 with CQ-VK-08; VulkanTexture.cpp:106 with CQ-VK-06; VulkanInstance/Device/DebugMessenger with CQ-VK-09 (M4). VulkanContext.cpp:745-751 (shutdown wait in a destructor) stays a log line, using VulkanResultName. Platform surface creators stay as they are.
- **Effort:** S.
- **Timing:** at low severity, steps 1-3 ride other snippets in these files rather than taking their own slot; step 4 as listed.
- **Overlaps:** AA2-RT-01 (the device-lost rethrow), AA2-RT-02.
- **Panel discussion:** RND wanted "convert when touched", VK a sweep. The agreed split put the five re-implementations first; verification showed only one of them is a reachable misclassification, and it changes only the message.
- **Verification:** confirmed with corrections; severity medium -> low because device loss is reachable at one site only, nothing catches the error by type, and the effect is message text, not behavior.

#### CQ-VK-03 Two independent SPIR-V parsers; the fragment checker is a 184-line function of magic numbers that B4 must extend

- **Severity:** medium.
- **Category:** readability, duplicate.
- **Sources:** VKR-3; endorsed by RND and BLD.
- **Evidence:**
  - ShaderInterface.cpp:16-148 has a Module parser (enums :16-32, Module :39-86, Parse :88-148) with plain enums and Reject("Incompatible shader: ...", :36). FragmentShaderInterface.cpp:16-63 re-parses the same stream with its own enums, Type lookup (:54-63) and Reject prefix ("Incompatible fragment shader: ", :51); its loop at :92-172 covers the same opcodes as ShaderInterface.cpp:93-144.
  - The two loops are not plain duplicates. The fragment loop whitelists while parsing: it rejects capabilities, extensions, decoration groups and extra execution modes, records Flat, checks the SPIR-V 1.0-1.5 version window (:78) and lists globals (:82). The vertex parser accepts all of these and stores decorations as maps.
  - Magic numbers: capabilities 5301/5302/5307 (FragmentShaderInterface.cpp:104-105); "SPV_EXT_descriptor_indexing" as seven hex words (:111-112); "PSMain" as 0x614d5350/0x00006e69 (:121-122).
  - The lit widths {3,2,3,4,3,3,1} and the location-6 special case are documented (:69: "Lit location 6 is a flat uint texture index (docs/GpuDrivenDesign.md, G4)"), but the flat rule at :228 (lit && location >= 3) and the width table have no comment naming RavenFragmentInput.
  - CheckFragmentShaderInterface spans :66-249 (184 lines). The header comment (FragmentShaderInterface.hpp:8) says "textured" but the function also checks Lit, and the comment sits on the enum.
  - AA2-RND-01 (ArchitectureAssessment-2026-11-26.md:860 for the checker evidence, fix step 4 at about :873): B4 must widen this checker for material inputs and bindings.
- **Why it matters:** with B4 the checker judges designer-authored shaders. A wrong rule rejects valid shaders (designer-visible) or accepts mismatched ones.
- **Fix:**
  1. (a) Move ShaderInterface.cpp's Module and Parse into Private/Renderer/Vulkan/SpirvModule.hpp/.cpp, with named constants and string-decoded extension and entry names. SpirvModule must record capabilities, extension names, execution modes, decoration groups and the version word, because the fragment checker whitelists them, so this is more than moving Module. Port ShaderInterface.cpp onto it.
  2. (b) Port CheckFragmentShaderInterface onto SpirvModule. (c) In a separate snippet, split it into CheckFragmentHeader, CheckFragmentDescriptors and CheckFragmentStageVariables (each under 50 lines). Name the flat rule and point at RavenFragmentInput. Add RAVEN_API to CheckShaderInterface and CheckFragmentShaderInterface in these snippets: RavenCore is a SHARED library (RavenEngine/CMakeLists.txt:1) with CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS OFF (RavenBuildSettings.cmake:47), so the step 5 test cannot link them otherwise (PhysicsWorld.hpp:48 is the precedent for PhysicsTests).
  3. Express the lit inputs as `std::vector<ShaderStageVariable>` with a Flat bit, beside the vertex interfaces. Leave texturedWidths alone: B5 deletes FragmentShaderKind::Textured (step 4), consistent with CQ-VK-04 step 4 and the no-throwaway rule.
  4. Fix the header comment. Delete FragmentShaderKind::Textured with B5.
  5. One new CPU test, Tests/ShaderInterfaceTests.cpp: run both checkers on the staged engine .spv files (expect accept), and on one copy with a location word patched in memory (expect reject). Allowed by the minimal-tests rule because steps 1-2 rewrite both checkers and B4 widens the interface next. CI stages shaders (DXC is installed, windows.yml:34-41, and RavenContent is a RavenCore dependency, RavenContent.cmake:143), so no gpu label is needed. Test accept/reject only, never SPIR-V bytes (CQ-BLD-12).
- **Effort:** M.
- **Timing:** steps 1-3 and 5 before B4 (Phase 2); AA2-RND-01's interface widening lands after them; step 4 delete with B5.
- **Overlaps:** AA2-RND-06, AA2-RND-01, CQ-RND-03, CQ-RND-12.
- **Panel discussion:** RND agreed the widening lands after this port. This is the only new test file proposed in the whole assessment.
- **Verification:** confirmed with corrections; severity medium -> medium because B4 turns this checker into the gate for designer-authored shaders.

#### CQ-VK-04 Vertex and uniform layouts are written twice, and OverlayQuad's grouped attributes are guarded only by sizeof

- **Severity:** medium.
- **Category:** readability, hidden ordering.
- **Sources:** VKR-4, absorbing RFP-11 step 5; endorsed by MEDIA.
- **Evidence:**
  - Each pipeline's attribute table and its ShaderInterface list repeat locations and widths: VulkanColorPipeline.cpp:42-48 vs VulkanContext.cpp:358; VulkanOverlayPipeline.cpp:35-44 vs :387; VulkanDebugLinePipeline.cpp:36-40 vs :375.
  - FrameUniformData offsets written twice: VulkanContext.cpp:361-363 and :377-379.
  - OverlayQuad attribute 5 reads a float4 starting at CornerRadius (CornerRadius, BorderWidth, Softness, DistanceRange) and attribute 6 reads uint2 at Texture (Texture, Flags) (VulkanOverlayPipeline.cpp:42-43). Only the shader says so (Overlay.vert.hlsl:16-19).
  - Overlay.hpp:44 asserts only sizeof == 112, with a Reserved[2] tail (:42). Inserting or reordering a field inside those groups and shrinking Reserved keeps the size and every offsetof the pipeline uses, and shifts what attributes 5 and 6 read. ColorVertex.hpp:19-22 asserts every offset. Overlay.hpp:13 says M6B's world-space sprites will share this struct, so an edit is likely soon.
  - The reload interface check never compares VkFormat against the C++ layout: ShaderInterface inputs carry only Location, Components and Integer (ShaderInterface.hpp:21-26). That is why step 4's single table matters.
- **Why it matters:** a field reorder in a public UI struct would draw wrong shapes with no build error and no reload rejection.
- **Fix:**
  1. Overlay.hpp: offsetof static_asserts for every field and one comment on the grouped fields (Phase 1 † commit, public).
  2. One comment at VulkanOverlayPipeline.cpp:42-43.
  3. A `FrameUniformLayout(set)` helper used at VulkanContext.cpp:361 and :377. Land it before or together with AA2-RND-01 step 3, which adds Time to FrameUniformData and updates both LitVertexInterface and DebugLineVertexInterface; otherwise that snippet edits the duplicated list twice.
  4. With CQ-VK-01: one constexpr {location, VkFormat, offset} table per vertex stream, deriving both the Vk attributes and the ShaderInterface inputs. The format-to-components mapping must treat UNORM formats as float components (the debug-line color is R8G8B8A8_UNORM read as a float4), not integer. Skip the textured pipeline (delete with B5).
- **Effort:** S.
- **Timing:** step 1 Phase 1 †; step 2 now; step 3 now, at the latest with AA2-RND-01 step 3; step 4 with CQ-VK-01.
- **Overlaps:** CQ-VK-01, CQ-RND-10, AA2-RND-01 step 3.
- **Panel discussion:** none.
- **Verification:** confirmed; severity medium -> medium because it is a latent but likely bug in a public struct that M6B extends, and the fix is cheap.

#### CQ-VK-05 A per-texture VkSampler, though one shared sampler already exists; one dedicated allocation per resource

- **Severity:** low (revised from medium).
- **Category:** slow, resource limit.
- **Frequency:** per texture or buffer created; fails at scale rather than costing per frame.
- **Sources:** VKR-5.
- **Evidence:**
  - VulkanTexture.cpp:209-220 creates a sampler per texture, while VulkanTextureDescriptors.hpp:61 says one shared sampler matches all textures and it exists (VulkanTextureDescriptors.cpp:95-105). WriteSet binds the per-texture one (:242-243), and CreateBinding rejects a texture whose GetSampler() is null (:150).
  - Every image, staging buffer and depth attachment is a separate vkAllocateMemory (VulkanTexture.cpp:187-194; VulkanBuffer.cpp:78-88; VulkanDepthAttachment.cpp:72-78). No sub-allocator.
  - Bindless capacity is 4,096 (VulkanTextureDescriptors.hpp:20). Spec minimums: maxSamplerAllocationCount 4000, maxMemoryAllocationCount 4096. Not queried on the laptop; device selection checks neither.
  - Content today: 2 textures in all games (Samples project file); M6B caps materials at 4 textures (RenderingContentDesign.md:80).
- **Why it matters:** the limits sit at or below the design capacity, so content growth would fail with an opaque driver error instead of a bindless-full DataError. Nothing near that scale is planned before M7.
- **Fix:**
  1. Now, net deletion (VulkanTexture.hpp/.cpp, VulkanTextureDescriptors.cpp): drop m_Sampler and GetSampler; WriteSet uses the shared sampler; remove the null-sampler check in CreateBinding in the same snippet.
  2. With AA2-RND-07 step 1: read both limits at selection and report live counts in GetResourceStats.
  3. Before B3 or B6: sub-allocate images and staging from large blocks (own block allocator, or a vendored allocator; see owner question Q3).
- **Effort:** M (S for step 1).
- **Timing:** step 1 now; step 2 with AA2-RND-07; step 3 before B3/B6.
- **Overlaps:** AA2-RND-07, AA2-RND-08, AA2-RT-02, AA2-CORE-04 step 4.
- **Panel discussion:** GAME showed content is two orders of magnitude below the limits. The lead lowers to low; step 1 is worth doing now anyway.

#### CQ-VK-06 Texture uploads pay their own pool, fence, allocation and submit, and meshes their own staging allocation and submit; the mesh path's better idiom is not shared

- **Severity:** low (revised from medium in verification).
- **Category:** slow, redundant pass.
- **Frequency:** per texture and per mesh at scene load, asset reload and glyph-page creation; B7 multi-object FBX multiplies it per file. Not per frame.
- **Sources:** VKR-6.
- **Evidence:**
  - Per texture: TextureUpload creates and destroys a command pool, command buffer and fence (VulkanTexture.cpp:35-73, 98-113), a staging buffer with its own allocation and map (:222), then vkQueueSubmit and an infinite fence wait (:81-95). It also repeats two format queries and the memory-properties query per texture (:151-165, :24). This half, including the glyph-page case, is AA2-RND-04's evidence and fix step 1 (ArchitectureAssessment-2026-11-26.md:941-973); it is listed here only for the code-level additions.
  - Per mesh: VulkanMeshPool uses one shared command pool (VulkanMesh.cpp:192-199) and no fence; it retires through the timeline (:278-304). Per mesh it pays one staging VulkanBuffer (create, allocate, bind, map; :227-229), one command-buffer allocation (:144-155, :232) and one vkQueueSubmit2 (:292).
  - CPU passes per mesh: ValidateMesh over vertices (3-arg std::hypot, :36-42) and then indices, PickPositions over vertices (:49-56), CalculateMeshBounds over indices into vertices (MeshBounds.hpp:19-35, called at :326), and the memcpy of both arrays (:230-231). Bounds deliberately cover only index-referenced vertices (MeshBounds.hpp:19, "Only referenced vertices contribute").
  - The mesh path is transactional and timeline-retired (:226, :295-304); the texture path blocks on a fence.
  - Content today is about two textures and two meshes. The one runtime case, a new glyph page (TextSystem.cpp:612-615), is rare and already covered by AA2-RND-04.
- **Why it matters:** estimate: 10-100 us per allocation or submit on Windows drivers; each mesh costs at least two such calls (staging vkAllocateMemory and vkQueueSubmit2), so a 20-part FBX is about 0.4-4 ms per load (derived) and a 100-part one about 2-20 ms. Unmeasured, per load. Two idioms mean the batch work converts both.
- **Fix:**
  1. With AA2-RND-04 step 1, by B3 (AA2-RND-04 step 2 has B3's mip chain record into the batch): generalize the mesh path into a `VulkanUploadBatch` (one command buffer per frame, staging from the ring through CQ-RND-04's Allocate, timeline retirement). Reuse CQ-RND-05's RecordTextureUploads for image regions.
  2. Move texture creation onto the batch, deleting TextureUpload (VulkanTexture.cpp:32-121). This removes the fence drain that the in-place descriptor rewrite relies on (VulkanContext.cpp:643-647: "The upload fence covers earlier draws"), so it must land with AA2-RND-04 step 3's explicit timeline wait in SwapTextureContents.
  3. Upload all sub-meshes of one FBX in the same batch (B7).
  4. Any time, low risk: replace ValidateMesh's 3-arg std::hypot with a squared-length test. Do not merge ValidateMesh, PickPositions and the bounds into one vertex pass: bounds and the finite-position check deliberately cover only index-referenced vertices, and CalculateMeshBounds is shared with VulkanTexturedMesh.cpp:36 until B5, so a merged vertex pass would widen bounds with unreferenced vertices and reject meshes that are valid today.
  5. Measure scene-load time before and after in the B8 record.
- **Effort:** M.
- **Timing:** steps 1-2 with AA2-RND-04, by B3; step 3 with B7; step 4 any time.
- **Overlaps:** AA2-RND-04 (the texture half and the batch), CQ-RND-04, CQ-RND-05.
- **Panel discussion:** GAME confirmed today's content is small, so the batch can wait for the AA2-RND-04 schedule.
- **Verification:** overstated; severity medium -> low because the texture half re-reports AA2-RND-04, the mesh path already shares its pool and has no fence, and the cost is per asset load with today's content of a few assets.

#### CQ-VK-07 Small Vulkan helpers re-implemented per file

- **Severity:** low.
- **Category:** slop, duplicate.
- **Sources:** VKR-7.
- **Evidence:** memory-type search three times (VulkanBuffer.cpp:21-43, VulkanTexture.cpp:21-30, VulkanDepthAttachment.cpp:17-26), each querying memory properties per allocation; Barrier twice (VulkanMesh.cpp:171-186, VulkanGpuScene.cpp:51-66); image views three times (VulkanTexture.cpp:197-207, VulkanDepthAttachment.cpp:81-91, VulkanSwapchain.cpp:190-201); command pools three times. The host-visible choice takes the first HOST_VISIBLE|COHERENT type for every use (VulkanBuffer.cpp:27-29), so the CPU-read readback (VulkanGpuScene.cpp:361-362) gets uncached memory.
- **Why it matters:** B3 and B6 add images, views, samplers and barriers; without one helper file they become copies five and six.
- **Fix:**
  1. Cache memory properties in VulkanPhysicalDevice; add one FindMemoryType in Private/Renderer/Vulkan/VulkanMemory.hpp/.cpp; add Mapping::Readback preferring HOST_CACHED; log the chosen memory types once at startup so the laptop's real layout is recorded.
  2. RecordMemoryBarrier in the same helper header.
  3. CreateImageView(device, image, format, aspect).
- **Effort:** S.
- **Timing:** before B3.
- **Overlaps:** CQ-X-03.
- **Panel discussion:** none.

#### CQ-VK-08 Dead or always-constant members

- **Severity:** low.
- **Category:** slop, dead code.
- **Sources:** VKR-8.
- **Evidence:** VulkanShaderModule path constructor (VulkanShaderModule.hpp:15, .cpp:29-32): grep `VulkanShaderModule[ (]` finds only span constructions. VulkanColorPipeline::Get (VulkanColorPipeline.hpp:25): no caller. VulkanTexture::GetFormat (VulkanTexture.hpp:29): grep `GetFormat()` finds only swapchain callers. VulkanPhysicalDevice::DescriptorIndexing (VulkanPhysicalDevice.hpp:18) is always true because selection rejects devices without it (VulkanPhysicalDevice.cpp:226-240), so the startup log can never print "no". TRANSFER_SRC on every texture (VulkanTexture.cpp:157-158) is unused today (grep vkCmdBlitImage|vkCmdCopyImage: none) but B3's mip blits need it.
- **Fix:**
  1. Delete the path constructor (ReadWords stays; ShaderLibrary.cpp:250 uses it).
  2. Delete VulkanColorPipeline::Get (or fold into CQ-VK-01 step 3) and VulkanTexture::GetFormat.
  3. Remove DescriptorIndexing and its log field, in the same snippet as CQ-RND-07 step 3.
  4. Add a comment at VulkanTexture.cpp:157 that TRANSFER_SRC is for B3's mips.
- **Effort:** S.
- **Timing:** now (step 4 with B3).
- **Overlaps:** AA2-CORE-04.
- **Panel discussion:** none.

#### CQ-VK-09 The pre-M2 bootstrap files copy the Enumerate loop three times, in a different style

- **Severity:** low.
- **Category:** readability, duplicate.
- **Sources:** VKR-9.
- **Evidence:** VulkanPhysicalDevice.cpp:82-106, VulkanSwapchain.cpp:24-44, VulkanInstance.cpp:12-49. Layer enumeration tests VkResult by truthiness at VulkanInstance.cpp:58, and :62 treats VK_INCOMPLETE as a hard failure. VulkanInstance copies its extension vector twice (VulkanInstance.hpp:11; .cpp:52, 94). Redundant qualification at VulkanDebugMessenger.cpp:61. Narrow 60-column wrapping in these files. SelectVulkanPhysicalDevice is 153 lines (:109-261), the swapchain constructor 133 (:77-209).
- **Fix:**
  1. One `EnumerateVulkan<T>` in Private/Renderer/Vulkan/VulkanEnumerate.hpp, used in the three files.
  2. Take extensions as std::span<const char* const>.
  3. Extract `EvaluateDevice(device, surface, rejected)` from device selection.
  4. Re-wrap only lines a snippet already touches.
- **Effort:** S.
- **Timing:** M4 (with AA2-RND-07 portability work).
- **Overlaps:** AA2-RND-07.
- **Panel discussion:** none.

#### CQ-VK-10 Renderer headers hide intent

- **Severity:** low.
- **Category:** readability, naming.
- **Sources:** VKR-10.
- **Evidence:** VulkanMeshPool exposes Range and Pool publicly only for .cpp helpers (VulkanMesh.hpp:47-62). VulkanMeshData interleaves access sections and carries CPU picking copies whose only reader is GameSession.cpp:401 (VulkanMesh.hpp:72-91). SceneLighting.hpp:25 calls `Normalize(Direction);` and discards the result purely to throw. VulkanContext.hpp:118 trailing whitespace; :150 uses std::pair without <utility>.
- **Fix:**
  1. Make Range and Pool private (VulkanMesh.hpp/.cpp).
  2. Reorder VulkanMeshData and note why the Pick arrays exist.
  3. SceneLighting.hpp:25: an explicit check with a comment (Phase 1 † commit with AA2-RT-01 step 1, which edits that header).
  4. VulkanContext.hpp fixes ride the Phase 3 move of the header out of the Public tree (AA2-CORE-03 steps 3-4, the Host root; CQ-X-08), after which they no longer change the game contract.
- **Effort:** S.
- **Timing:** step 3 Phase 1 †; steps 1-2 later; step 4 Phase 3.
- **Overlaps:** AA2-CORE-04, AA2-RT-01.
- **Panel discussion:** none.

#### CQ-VK-11 Texture and mesh slot bookkeeping is O(slots) on every create, release and frame

- **Severity:** low.
- **Category:** slow, O(total).
- **Frequency:** per texture create and release, per mesh create; plus an O(textures) walk per frame.
- **Sources:** VKR-11 (new in round 2; answers DATA's and VK's own questions).
- **Evidence:** ReserveResourceSlot is a linear first-free scan (VulkanContext.cpp:81-88; used at :595 and :613). CreateTextureSlot calls CollectResources then ReserveResourceSlot (:610-622). CollectResources walks every slot (:624-634) and runs every frame (:683), on every creation, on every ReleaseTexture (:844-856) and on every AssetStore::Acquire (AssetStore.cpp:270-274). The descriptor side already has a free list (VulkanTextureDescriptors.cpp:196-216).
- **Why it matters:** derived: loading 4,096 textures does about 17M slot checks, roughly 17-35 ms (estimate), and unloading repeats it. Today's content (a handful of textures) makes it negligible; the bindless capacity makes it reachable.
- **Fix:**
  1. VulkanContext.cpp: add m_PendingTextureReleases and m_FreeTextureSlots. Release pushes the index; CollectResources iterates only the pending list. Reserve both to m_Textures.size() in CreateTextureSlot, so the noexcept release path never allocates.
  2. ReserveResourceSlot pops a free list (skipping Generation==max slots); same for meshes.
  3. Ignored probe: time 4,096 1x1 texture creates and releases before and after, recorded with CQ-DATA-04.
  4. Leave the OwnsResource scans (:685-690): deleted with B5.
- **Effort:** S.
- **Timing:** before B6, with CQ-DATA-04.
- **Overlaps:** AA2-RT-03, AA2-RND-04, CQ-DATA-04.
- **Panel discussion:** VK proposed medium; the lead lowers it to low for the same reason as CQ-DATA-04 (no content near the scale). Do it together with CQ-DATA-04 so preload is O(N) end to end.

#### CQ-VK-12 AA2-CORE-04's dead camera-DrawFrame closure misses the two camera value types, the Impl template and three docs

- **Severity:** low.
- **Category:** slop, dead code.
- **Frequency:** build time (public headers in the contract hash).
- **Sources:** completeness critic (CRIT-6).
- **Evidence:**
  - AA2-CORE-04 step 1 (ArchitectureAssessment-2026-11-26.md:464) deletes "the camera-only DrawFrame overloads" (VulkanContext.hpp:123-128; VulkanContext.cpp:999-1009) but lists nothing they keep alive. AA2-CORE-04's evidence says AssetResources.cpp uses them; grep finds no caller at all (GameSession.cpp:489 uses the view-builder overload).
  - `git grep -n "PerspectiveCamera|OrthographicCamera"` over tracked files (excluding ThirdParty and docs) finds only the two headers, their includes at VulkanContext.hpp:10 and :13, the overload declarations (:123-128) and their definitions (VulkanContext.cpp:999-1009). Once the overloads go, Public/Raven/Renderer/PerspectiveCamera.hpp and OrthographicCamera.hpp have no users.
  - VulkanContext.cpp:131-134 (declaration) and :658-672 (definition): the `template<typename Camera> Impl::DrawFrame` adapter is called only from :1002 and :1008; its camera.GetView() at :667 is the only GetView() call on a camera type.
  - VulkanContext.hpp:119-122: only the first sentence ("Camera overloads draw only textured and transparent draws...") is camera-specific. The transparent-ordering rule and "All submitted resources must belong to this context" also describe the surviving DrawFrame(buildView, world, texturedDraws, transparentDraws, ...) at :141-145, whose own comment (:136-140) does not state them.
  - README.md:49-51, docs/EngineModules.md:176-178 and docs/TextureResources.md:71-73 describe the overloads as current. LookAtRH stays: Scene.cpp:605 and CameraView.hpp:121 use it.
- **Why it matters:** if AA2-CORE-04 step 1 lands as written, two public headers and an Impl template stay as dead weight in the contract hash, three docs still describe a removed API, and a second contract change is needed later.
- **Fix (extend AA2-CORE-04 step 1, in the same Phase 3 snippet):**
  1. Delete PerspectiveCamera.hpp, OrthographicCamera.hpp and their two includes (VulkanContext.hpp:10, 13), the camera-specific first sentence at :119-122 (move the ordering and same-context sentences onto the live overload's comment at :136-140), and Impl::DrawFrame (VulkanContext.cpp:131-134, 658-672).
  2. In the AA2-CORE-04 step 3 doc commit: change only the camera-overload sentences in README.md:49-51 and TextureResources.md:73 (EngineModules.md is already in that step's scope). Leave their CreateTexturedMesh and textured/transparent text to the B5 checklist (CQ-RND-02), so the docs are not rewritten twice. Leave MilestoneHistory.md unchanged (history). Correct AA2-CORE-04's "used only by AssetResources.cpp" evidence in the same doc commit.
- **Effort:** S.
- **Timing:** Phase 3 contract window, with AA2-CORE-04 step 1.
- **Overlaps:** AA2-CORE-04, CQ-X-08, CQ-RND-02.
- **Panel discussion:** not discussed by the panel; added in verification.
- **Verification:** confirmed with corrections (critic proposal verified separately); severity low (new) because it is dead public weight whose only cost is an avoidable second contract change.

### 5.3 CORE: core runtime and host

#### CQ-CORE-01 Activation hashes the game DLL three times on the frame thread; one hash is enough

- **Severity:** low (revised from medium in verification).
- **Category:** slow, redundant pass.
- **Frequency:** per game-module reload (every game edit), on the frame thread.
- **Sources:** HOST-1; corrected by BLD in round 2.
- **Evidence:**
  - GameModule.cpp:415 `Stamp(Path)` is hash 1; :429 `Stamp(Path) == before` is hash 2 and `Stamp(candidate->Copy)` hash 3. Stamp is stat + HashFile + stat (:67-77). HashFile reads 4 KB chunks through a per-byte helper (:37-57, helper :37-41).
  - GameModule.cpp:553-570 (the offer lambda), 603-616, 623, 653: the worker hashes the DLL before offering it and passes it as `expected`. Only the :623 offer is made after the build tree exited; the :653 offer (output settled, no host build) can stamp a DLL that an external build is still writing.
  - GameModule.cpp:427-431 with :498-499: the `copy` trace span also covers two hashes.
  - Measured, Debug, M5 H5 probe (Samples' RavenGame.dll after a one-file InteractionGame.cpp edit, not Arena): stamp 8-11 ms, copy 17-46 ms (HotReloadDesign.md:344-350). Arena.dll is 1,476,096 bytes, so the savings below are derived for a DLL whose size in that probe was not recorded.
  - The Debug game DLL links with /INCREMENTAL (Arena build.ninja:167). An incremental link patches the image through a mapping; LastWriteTime may not update until flush or close, and the size often stays the same. So size + mtime cannot prove an untorn copy.
  - This is the same defect as AA2-HR-06 ("Activation hashes, copies and re-hashes the DLL on the frame thread", GameModule.cpp:415-430), which AA2 rates low for the whole ~50 ms hitch. AA2-HR-06 step 1 moves staging and verification into the worker, and AA2-HR-04 step 5 replaces the byte-wise FNV.
- **Why it matters:** derived: dropping two of three hashes saves about 16-22 ms of a 25-57 ms frame-thread hitch per edit, which also causes a fixed-step catch-up burst. The code-level addition to AA2-HR-06 is only the rule "the staging function hashes once".
- **Fix (fold into AA2-HR-06 step 1, not a separate commit, because that step rewrites these Activate lines):**
  1. Expected path: hash the copy once and accept only if its digest equals expected->Digest. Equality proves the loaded bytes are the bytes the worker saw. A size+mtime check may stay as an early "changed since offer, skip" reject, never as proof. The :653 offer path depends on AA2-HR-03 step 2 (watch BuildInputs.sha256) landing first, so that an offer is never made while an external build writes.
  2. Say what AttemptedFile becomes. Today :481 sets it from hash 1 (`before`); it is compared at :410 and :712 and seeds the worker at :703. In the expected path it becomes *expected; in the F5 path, Path's {time, size} plus the copy digest.
  3. Keep today's split between a silent stale offer and an error: a mismatch before the copy returns false silently (:416-417), while a mismatch after it throws and is reported (:429-430). With the copy digest as the only check, make a digest mismatch a silent return (the candidate's destructor removes the copy, :214-225), so a stale offer does not become an error message.
  4. F5 path (manual, rare, outside the per-edit loop): keep today's three-check verification, or AA2-HR-03 step 1's IsBuilding refusal. Do not add a Build.lock probe: no lock function exists in the Platform layer (grep LockFileEx|TryLock|FileLock in RavenEngine: none), so it would need a new Platform header plus Win32 and POSIX backends.
  5. Split the trace span into `copy` and `verify`.
- **Effort:** S (inside AA2-HR-06 step 1).
- **Timing:** with AA2-HR-06 step 1, after AA2-HR-03 step 2.
- **Overlaps:** AA2-HR-06 (primary), AA2-HR-03, AA2-HR-04 step 5 (replaces this HashFile), CQ-BLD-02.
- **Panel discussion:** CORE's round-1 fix relied on a size+mtime check (SameMeta) to detect a linker still writing. BLD showed that is unsafe under /INCREMENTAL; CORE's own round-2 argument also rested on the copy's digest. The lead adopted BLD's form. A proposed shared build/host digest was dropped: the build uses SHA-256 and the host FNV, and the engine has no SHA-256 (grep SHA256|BCryptHash: none). This finding and CQ-BLD-02 are independent.
- **Verification:** overstated; severity medium -> low because AA2-HR-06 already reports this defect at low for the whole hitch and schedules the rewrite, so the single-hash rule belongs inside its step 1.

#### CQ-CORE-02 GameModule::Impl::Watch is a 153-line state machine held in nine mutable locals, and AA2-HR-03 is about to change it

- **Severity:** medium.
- **Category:** readability, long function.
- **Sources:** HOST-2.
- **Evidence:** GameModule.cpp:517-669 (153 lines). State lives in mutable locals: build, publishedFile/publishedRevision, observedSource, builtSource, trace, buildQueued, buildFailed and lastError (:521-530), checkSources and checkOutput (:595-596), sourcesSettled and outputSettled (:580-581), and a per-iteration `superseded` (:615). Three lambdas mutate them (:531-570): report (:531-538) and offer (:553-570) publish to the frame thread; observeSource (:539-552) increments SourceRevision and calls DiscardCandidate but does not publish. The build-finished branch (:603-628) sets and clears flags with no written transitions. The offer gate `!build && !buildQueued && !buildFailed` (:649-653) is the sticky failure AA2-HR-03 changes.
  - The state is a product of independent facts, not one of four exclusive states. A source edit during a build sets buildQueued while `build` is in flight; that combination is what `superseded` (:615) reads to discard the result, whatever its exit code. buildFailed stays true across the next build until a successful, unsuperseded build clears it (:620-627). An output change while failed offers nothing (:652), and F5 calls Activate directly without touching the worker's flags (:693-696).
  - HotReloadDesign.md:306 records an LNK1104 near this code; it came from the reload service reading the DLL during a link and was fixed by SetPaused (:607, :640), not by the flag transitions.
- **Why it matters:** AA2-HR-03 changes three transitions. Without the transitions written down, each edit risks blocked offers or a lost supersede.
- **Fix (GameModule.cpp):**
  1. Pure, behavior-preserving restructure: model the state as the existing process handle (`build`) plus `bool RebuildPending` plus `enum class LastBuild { None, Succeeded, Failed }`, with a short comment listing today's transitions against those: source change sets RebuildPending; not building and RebuildPending starts a build; a build that finishes with sources changed or RebuildPending set is superseded and discarded; exit 0 and not superseded sets Succeeded and offers; a non-zero exit sets Failed; only Succeeded clears Failed. A single four-value enum cannot represent "building with a rebuild pending" or "failed and building", so it would change behavior.
  2. Extract OnBuildFinished (:603-628), StartBuild (:635-648) and OfferIfIdle (:649-654) as Impl members over a small WatchState struct. The struct must hold build, trace, outputWatch, directory and the published and observed fields the lambdas capture today, so the snippet stays one file and compiles.
  3. Apply AA2-HR-03 steps 1-3 as edits to the named state (clearing the failure on a stamp change or F5 is AA2-HR-03 step 3, not current behavior), in its own snippet, so review can tell refactor from fix.
- **Effort:** S.
- **Timing:** Phase 1, immediately before AA2-HR-03.
- **Overlaps:** AA2-HR-03, AA2-HR-06.
- **Panel discussion:** none.
- **Verification:** confirmed with corrections; severity medium -> medium as preparation for the AA2-HR-03 edit; the proposed single enum was replaced because it cannot represent live state combinations.

#### CQ-CORE-03 Reload consumer callbacks run under the subscription's lock; the header does not repeat the documented "never call back into your subscription" rule, and nothing enforces it

- **Severity:** low (revised from medium in verification).
- **Category:** readability, hidden ordering.
- **Sources:** HOST-14 (new in round 2; covers the lead's lock-ownership blind spot). MEDIA audited audio and text threads in the same round and found them clean apart from CQ-MEDIA-12.
- **Evidence:**
  - ReloadService.cpp:452-528: `scoped_lock lock(state.Mutex)` is held across Consumer->Discard() (:457, :462) and Consumer->Accept(record) (:501) and across Log calls. ReloadService.cpp:339-359: the watcher calls Consumer->Changed() under the same mutex.
  - ReloadService.cpp:568-617: every ReloadSubscription method and the destructor lock m_State->Mutex (:568, 579, 589, 599, 605, 617), a plain std::mutex (:183).
  - Import runs unlocked (:425 vs :452), which is why ShaderLibrary's Import -> SetInputs (ShaderLibrary.cpp:209) is safe today. The same call from Accept would self-deadlock.
  - The rule is documented: docs/HotReloadDesign.md:30 says consumers are called under their state mutex (Accept, Changed) and must not call back into their own subscription there. Only the header (ReloadService.hpp:65-81) is silent; it says which thread runs each callback but not that the lock is held.
  - Current consumers use only leaf locks: ShaderLibrary.cpp:213-232; GameModule.cpp's WakeConsumer (:107-117); SettledFileWatch.hpp:73-90. No consumer violates the rule today.
  - AA2-PERF-03 already cites ReloadService.cpp:359, 457, 462, 501 and the undocumented header; its fix step 4 is this finding's step 1 plus the move of Accept and Discard outside the lock.
- **Why it matters:** B6 and AA2-HR-01/04 add consumers whose natural Accept would update their inputs. One such call deadlocks the reload worker; the subscription destructor then blocks, so the host hangs at scene change or exit with no message. The new content here, beyond AA2-PERF-03, is the debug guard and the leaf-lock note.
- **Fix:**
  1. With AA2-PERF-03 step 4 (an owner-applied engine snippet, not a docs-only item): ReloadService.hpp states that Accept, Discard and Changed run with the lock held, may take only leaf locks, must not call ReloadSubscription or block; Import runs unlocked and may call SetInputs.
  2. ReloadService.cpp, debug builds: store the callback thread in a per-state `std::atomic<std::thread::id>`, read without m_State->Mutex (otherwise the check itself deadlocks). A subscription method called from its own callback logs and aborts (a debug fatal check). A thrown std::logic_error would not reach the consumer as an error: Accept is called without a try in Service::Run (:501), so the exception leaves the worker jthread and calls std::terminate, and Discard, Changed and the subscription destructor are noexcept.
  3. The move of Accept and Discard outside the lock is AA2-PERF-03 step 4 (inside AA2-HR-04 steps 1-4), keeping the Importing handshake.
- **Effort:** S.
- **Timing:** step 1 with AA2-PERF-03 step 4; step 2 before B6.
- **Overlaps:** AA2-PERF-03 step 4 (primary), AA2-HR-04, AA2-HR-01, CQ-CORE-06.
- **Panel discussion:** none.
- **Verification:** overstated; severity medium -> low because the rule is already documented in HotReloadDesign.md:30, AA2-PERF-03 step 4 already prescribes the header comment, and no consumer violates it today.

#### CQ-CORE-04 The frame-trace CSV keeps 27 columns as two hand-aligned lists, and three planned fixes add columns

- **Severity:** low (revised from medium in verification).
- **Category:** readability, duplicate.
- **Sources:** HOST-4.
- **Evidence:** Application.cpp:679-682 (a 27-name header) and :686-692 (27 hand-ordered insertions); they line up today, but nothing ties name to field, and FrameMetrics (FrameTrace.hpp:19-51) is a third ordering. scripts/Benchmark-Windows3D.py:59-60 reads the file with csv.DictReader, so a consistent-count misordering lands values under the wrong keys silently. Application.cpp:676-739 is 64 lines of end-of-run I/O for three files, in Application::Run after the frame loop ends (once per run). :705 spells phases "fixed/control/update", while GameModule.cpp:154-162 PhaseName uses "Control/FixedUpdate/Update". Columns are coming from AA2-PERF-06 (restore_ns, step_world_ns), AA2-PERF-01 and CQ-GP-10 (physics step time).
- **Why it matters:** a mismatched addition reports physics time as UI time in an exit record, on the path that gates B4. The two lists are within 14 lines of each other and no misattribution has happened, so the risk becomes real only when the planned columns land.
- **Fix:**
  1. FrameTrace.hpp plus a new FrameTrace.cpp (only the header exists today): one constexpr table of {name, accessor} next to FrameMetrics, so declaration and column order are reviewed together, and `WriteFrameTrace(path, samples)` driven by it.
  2. Moving the systems and reloads writers beside it and sharing one PhaseName is AA2-CORE-02 step 4 ("Move ... the trace writers into their own Private/Core files"), scheduled during M6B. If done now, record it as delivering that part of AA2-CORE-02 step 4. Choose one phase spelling explicitly; no script parses .systems.csv (grep of scripts for systems.csv and the phase strings: none).
  3. Application.cpp keeps the calls. Name the 32,768-frame cap (samples.reserve(32768) at Application.cpp:384, capacity check at :390; already documented in DevelopmentReload.md:45, so the constant is for code clarity) and warn when capture stops (from CQ-CORE-13).
- **Effort:** S.
- **Timing:** now, before AA2-PERF-06 step 1. Step 1 is two files (FrameTrace.hpp/.cpp), step 3 one (Application.cpp).
- **Overlaps:** AA2-CORE-02 step 4, AA2-PERF-06, CQ-GP-10, CQ-CORE-12.
- **Panel discussion:** none.
- **Verification:** confirmed with corrections; severity medium -> low because the code runs once at exit, the two lists sit side by side, and no misattribution has occurred.

#### CQ-CORE-05 Application::Run's Gameplay stage encodes several modes in a 68-line lambda nested six blocks below Run's body

- **Severity:** medium.
- **Category:** readability, long function.
- **Sources:** HOST-5.
- **Evidence:** Run is 444 lines (Application.cpp:299-742; AA2-CORE-02). The Gameplay lambda (:518-585) covers scene replaced (:520-535, which also clears physics and picking, :520-524), held (:525-528, joined by `previewing`), F9 single step (:536-548), playing with catch-up (:550-575), simulation paused (:561-565), and preview (`previewing` suppresses Update at :576 and drives RunPreview at :582-583). Control (:554-559) and Update (:576-581) run in both Playing and SimulationPaused. The deepest statement (the Log at :544-545) is three blocks deep inside the lambda (if :525, else-if :536, if :539) and six below Run's body (while :388, if drawable :422, the lambda :518, then those three). The runStep lambda (:496-517) captures 9 locals by reference (gameplay, interpolator, session, physics, game, router, metrics, instance, determinism) and is called from inside the Gameplay lambda (:541, :571), which captures about 19.
  - Orderings: syncGameInput runs before Route (:397, partly explained at :346-347) and again after F5 (:418, inside :415-419), with no why-comment for the resync. Reset and Snap: only two adjacent Reset+Snap pairs exist (:527-528, :563-564). gameplay.Reset also runs without Snap at :547 (after the Snap at :543, which runs only when a step ran), :588 (failed stage) and :650 (not drawable). Restore after Render (:640-646) is stated by the comment at :599-602.
- **Why it matters:** AA2-CORE-01, AA2-GP-02/03, AA2-PERF-01 and AA2-RT-01 all land in this lambda.
- **Fix (with AA2-CORE-02 step 3, FramePipeline.hpp/.cpp plus Application.cpp):**
  1. Compute `enum class GameplayMode { Replaced, Held, SingleStep, Playing, SimulationPaused, Previewing }` once (or keep preview as an explicit flag beside the mode) and switch on it.
  2. Merge only the two real Reset+Snap pairs (:527-528, :563-564) into one HoldSimulation(). Do not use it at :547, :588 or :650 without deciding whether those paths should snap: adding a Snap there changes behavior.
  3. After AA2-CORE-02 step 2 moves PhysicsWorld, FixedGameplayStep and TransformInterpolator into the session-owned PlayWorld: make runStep a member RunFixedStep(). Before that move, the member would need about 9 references passed in.
  4. After AA2-PERF-01 removes the interpolation write-back (which changes the Snap/Restore calls): write the ordering rules as a comment block at the top of Frame(), including why input is resynced after F5.
- **Effort:** M.
- **Timing:** Phase 4, with AA2-CORE-02 steps 2-3; step 4 after AA2-PERF-01.
- **Overlaps:** AA2-CORE-02, AA2-PERF-01, CQ-CORE-14.
- **Panel discussion:** none.
- **Verification:** confirmed with corrections; severity medium -> medium because several AA2 fixes land in this lambda, which makes it a real review hazard; the "every Reset is followed by Snap" premise was false and step 2 was narrowed.

#### CQ-CORE-06 ReloadService polls every 40 ms with a full stat sweep while any import runs, because a finished import never wakes the watcher

- **Severity:** medium.
- **Category:** slow, O(total).
- **Frequency:** every 40 ms for the duration of every import (about 0.65 s for the 13-program background compile at develop launch, about 0.1 s per shader reload, derived from AA2-HR-01). It runs on the watcher thread only, never on the frame thread or the import workers.
- **Sources:** HOST-6; verified line by line by MEDIA.
- **Evidence:**
  - An Importing state is skipped but sets `busy |= Importing` (ReloadService.cpp:324-327); busy selects the 40 ms PollInterval over the 2 s AuditInterval (:384, :26-27). Run clears Importing and notifies only state.Idle (:528-529). m_Wake.request_stop() is called only from Add, Remove and Changed (:221, 231, 240); SetInputs and SetPaused reach it through Changed (:583, 594).
  - Each pass calls ReadMeta for every non-skipped subscription (:332): two filesystem calls per matched file (:135-136) plus enumeration of directory inputs (:147-158), with an is_regular_file check per entry.
  - The game-source input is three subtree directories filtered by IsSourceFile (.hpp, .cpp, .cmake, .in, CMakeLists.txt) (GameModule.cpp:79-84, 101-107), and ReadMeta stats only matching entries (ReloadService.cpp:150-151). For Arena that is 15 + 65 + 28 = 108 matched files (Games/Arena, RavenEngine/Public, cmake/; the other files are content or RavenIcuData.txt), so about 216 stats plus about 126 entry checks per pass, roughly 5.4k stats/s while an import runs (derived); about 16 passes and 3.5k stats over the 0.65 s launch compile (derived). The default Samples game has 101 matched files.
  - ShaderLibrary.cpp:38-52 lists 13 programs and :129-136 subscribes with no baseline, so every develop launch imports all 13 on the two workers.
  - Ordering: a directory notification that arrives during an import wakes a pass that skips the importing state; after the supersede at :459-469 only the 40 ms busy poll picks the change up, and a RetryFailures retry (:522-526) would otherwise wait up to 2 s.
  - Lock order permits a wake from Run: no path holds m_Mutex while taking a state->Mutex (Records :245-255, Directories :268-281 and the destructor :564-573 take them one after the other).
- **Why it matters:** background CPU that grows with M6B's watched content and import count. The game build is not an import (the game-source consumer is the instant WakeConsumer, GameModule.cpp:110-120, and the DLL subscription is paused during the build), so the 40 ms poll does not run during a game edit reload. At today's scale this is close to low; it matters as watched inputs grow.
- **Fix (ReloadService.cpp only; steps 1-2 are independent of AA2-HR-04 and can land before it):**
  1. In the same snippet: add Service::Wake() (request_stop without bumping the revision), call it at the end of Run after Importing is cleared and after releasing state.Mutex (the lock at :452 lasts to the end of Run; waking outside it keeps the leaf-lock rule of CQ-CORE-03), and remove `busy |= Importing`. Never land the removal alone: today the 40 ms poll is the only thing that picks up a save made during an import (supersede at :459-469, RetryFailures at :522-526).
  2. For unsettled changes, wait until the earliest StableSince + Settle instead of a fixed 40 ms. A mid-settle rewrite must re-arm the deadline: the directory notification already wakes the watcher and the meta compare at :335 catches it, so recompute the deadline on every pass, not once. Keep the 40 ms fallback when the directory watch is unavailable (:384 already handles this).
  3. With AA2-HR-04 step 1: share cached input contents instead of copying them. (Per changed or audited read, ReadSnapshot copies a shared include's path into the key at :96 and the content once per consuming subscription at :100; ReadSnapshot runs only when metadata differs, on the audit, on Rebase or while unsettled, :335-338. AA2-HR-04 owns this.)
  4. Check from outside, without editing production sources: count the watcher thread's file-system calls with Process Monitor or an ETW file-I/O trace during a develop launch, before and after. A counter inside ReloadService.cpp may only ride the requested fix snippet.
- **Effort:** S.
- **Timing:** before M6B; step 3 with AA2-HR-04 step 1.
- **Overlaps:** AA2-HR-04, AA2-HR-01, CQ-CORE-03.
- **Panel discussion:** MEDIA found the ordering constraint in step 1.
- **Verification:** confirmed with corrections; severity medium -> medium because the sweep grows with M6B's watched content, though the cost is bursty, off the frame thread, and nearer low at today's scale; file counts corrected from 126 to 108.

#### CQ-CORE-07 Stale and drifted comments: "Development-only" module, four different developer-key lists, an orphaned overlay comment

- **Severity:** low.
- **Category:** slop, stale comment.
- **Sources:** HOST-7 (the redundant pragma moved to CQ-BLD-11).
- **Evidence:** GameModule.hpp:29 says "Development-only", but RavenEngine.cpp:168-169 and Application.cpp:315-316 load it for players too. The developer key list exists four times and each differs from InputRouter.cpp:80-95 (14 bindings): RavenEngine.cpp:212-213 logs 10 keys and omits the F6 family; GameSession.hpp:34 and Application.hpp:35 list other subsets. Bindings are rebindable, so the hard-coded log can also be wrong at run time. Application.cpp:147-148 describes DrawDeveloperOverlay but sits above DeterminismCheck. JobSystem.cpp:13 states "about 10-20 microseconds" for SpinLimit, unmeasured.
- **Fix:**
  1. GameModule.hpp:29: describe both modes (Phase 1 † commit, on its own; AA2-HR-05 is not a † item in AA2 section 7.1).
  2. Add InputRouter::DescribeDeveloperBindings() built from the live table, logged after bindings load; delete the RavenEngine.cpp string; point the two comments at it.
  3. Move the orphaned comment onto DrawDeveloperOverlay.
  4. Label JobSystem.cpp:13 "unmeasured; see AA2-PERF-05".
- **Effort:** S.
- **Timing:** step 1 Phase 1 †; steps 2-4 now.
- **Overlaps:** AA2-HR-05, AA2-CORE-05, AA2-PERF-05, CQ-X-04.
- **Panel discussion:** none.

#### CQ-CORE-08 Members and exported APIs with no caller

- **Severity:** low.
- **Category:** slop, dead code.
- **Sources:** HOST-8; re-grepped by the lead.
- **Evidence:** GameModule::GetLastError (GameModule.hpp:59): grep finds only Win32 ::GetLastError calls. GameSession::GetSimulationSeed (GameSession.hpp:80) and UserDataService::GetRoot (UserData.hpp:38): only declarations. Paths::GetExecutablePath is exported but used only in Paths.cpp:80. ApplicationPathsScope's contentRoot is always `{}` (Application.cpp:307), so Paths.cpp:49-53 never runs. JobSystem::ParallelFor/SubmitRange: the only caller is TextSystem::Prepare (TextSystem.cpp:660), itself uncalled (AA2-MEDIA-08). Keep GameModule::IsBuilding: AA2-HR-03 step 1 calls it. PenetratesXZ is handled in CQ-API-07.
- **Fix:**
  1. Now: delete GetSimulationSeed and GetRoot; drop the ApplicationPathsScope parameter and its branches.
  2. Delete GetLastError in the snippet that moves GameModule.hpp to the host root (AA2-CORE-03 step 4), where it costs no extra contract change; un-export or document GetExecutablePath there too.
  3. Do not polish ParallelFor: AA2-PERF-05 replaces it, and MEDIA ports or deletes the one Prepare call in that commit.
- **Effort:** S.
- **Timing:** step 1 now; step 2 Phase 3; step 3 with AA2-PERF-05.
- **Overlaps:** AA2-CORE-03, AA2-CORE-04, AA2-PERF-05, AA2-MEDIA-08.
- **Panel discussion:** BLD and API moved PenetratesXZ to the Collision2D move at B5, so the contract changes once.

#### CQ-CORE-09 Small per-frame allocations in the host loop

- **Severity:** low.
- **Category:** slow, hot-path allocation.
- **Frequency:** per frame.
- **Sources:** HOST-9 items 1-2 (item 3 moved to CQ-GP-11), API-12 steps 1-2.
- **Evidence:** Application.cpp:430 builds `std::vector<std::string>` from GetPreviewNames() every frame (GameModule.cpp:739-746). It is read on a key press (:431-439), and :440-441 clamps preview.Entry against its size; that clamp only matters while preview.Entry >= 0 (lead read of :426-445). DrawDeveloperOverlay reads the current entry's name while previewing (:260-284, :636). Application.cpp:403 and :407 construct two InputState copies per frame; uiInput differs from raw only while listening (:408-412).
- **Why it matters:** a few microseconds per frame (estimate), and the host's patterns get copied.
- **Fix (Application.cpp only, no API change):**
  1. Fetch preview names only when PreviewNext was pressed or `preview.Entry >= 0`. Otherwise skip the call; the clamp is a no-op when Entry is -1.
  2. Hoist `routed` and a listening-only masked copy out of the loop as reused locals, and pass raw to the UI when not listening. Copy-assignment keeps the Events capacity, so neither allocates in steady state.
- **Effort:** S.
- **Timing:** now.
- **Overlaps:** AA2-PERF-09, CQ-X-06.
- **Panel discussion:** three fixes were proposed for GetPreviewNames: new GameModule accessors (HOST-9, chosen by API), a key-press gate (panel item API-12, not CQ-API-12) and a generation cache (MEDIA). API's argument that the clamp needs the count every frame does not hold: the clamp only acts while previewing. The lead picks CORE's refined gate: one private file, no API change, correct for both readers.

#### CQ-CORE-10 Log writes each line as about eight unbuffered console writes with the full absolute path

- **Severity:** low.
- **Category:** slow, hot-path allocation.
- **Frequency:** per log line, under the global log mutex (about 10 per reload, one per pick click).
- **Sources:** HOST-10; adjusted by BLD.
- **Evidence:** Log.cpp:48-50 inserts eight pieces into unit-buffered std::cerr. Log.cpp:49 prints the full build path. Log.cpp:27-30 has trailing whitespace and :37-43 redundant qualification. Callers mix `+ std::to_string` chains (GameModule.cpp:485-501, ReloadService.cpp:504-506) and std::format (Application.cpp:213, 282).
- **Why it matters:** each line holds the global mutex across several console writes, and the main thread takes the same mutex every frame (AA2-PERF-08).
- **Fix (Log.cpp only):**
  1. Build the line once with std::format and write it in one call.
  2. Trim the repo root at run time: derive it once from Log.cpp's own source_location by stripping RavenEngine\Private\Core\Log.cpp; print the remainder for paths under that root. No compiler flag.
  3. Fix the signature formatting. Do not churn call sites.
- **Effort:** S.
- **Timing:** Phase 4 with AA2-PERF-08 step 1, or before.
- **Overlaps:** AA2-PERF-08, AA2-CORE-05.
- **Panel discussion:** BLD rejected compiler flags (/d1trimfile is undocumented; source_location is evaluated in the caller's TU, so the flag would have to go into contract-hashed RavenBuildSettings.cmake). Accepted.

#### CQ-CORE-11 GameSession::Render does seven jobs, and the world-replaced reset is duplicated

- **Severity:** low.
- **Category:** readability, mixed abstraction.
- **Sources:** HOST-11; endorsed by MEDIA and GAME.
- **Evidence:** GameSession.cpp:456-554 (99 lines): camera selection, audio listener and m_Audio.Update(), lighting, sprite gather, DrawFrame with overlay, reload logging, picking. Audio updates only on drawable frames because Render runs inside `if (drawable)` (Application.cpp:421-647), undocumented. GameSession.cpp:186-190 (Advance) and :231-235 (QuickLoad) repeat the same five-line reset; AA2-MEDIA-01 must reorder StopAll at both.
- **Fix:**
  1. Now: extract `OnWorldReplaced()` and call it from both sites, before AA2-MEDIA-01.
  2. With AA2-RT-01 step 4: split Render into SelectCamera, UpdateAudio(camera, seconds) (called from Application with FrameClock's real delta; see CQ-MEDIA-04 discussion), DrawFrame and QueuePicks.
  3. GatherSprites: delete with B5.
- **Effort:** S.
- **Timing:** step 1 now; step 2 with AA2-RT-01 step 4.
- **Overlaps:** AA2-RT-01, AA2-MEDIA-01, AA2-MEDIA-07.
- **Panel discussion:** CORE recommended that AudioPlayer::Update take `double seconds` from the frame clock (real, unscaled), with pause as per-bus state, changing the public signature in the AA2-CORE-04 step 2 snippet that moves AudioPlayer.hpp private. In verification, the completeness critic proposed step 1 again as a new finding (CRIT-5); it was refuted as a duplicate of this step (section 10.4).

#### CQ-CORE-12 The interpolation Restore pass shares StageBoundary::PostUpdate's state

- **Severity:** low.
- **Category:** readability, hidden ordering.
- **Sources:** HOST-12; timing corrected by RND.
- **Evidence:** Application.cpp:608-614 and :640-646 both run under StageBoundary::PostUpdate, sharing m_States[PostUpdate] (:83-117, 127); recovery counts and "recovered" lines mix the two passes (:110-115); both report "World transform update" (:120-121).
- **Fix:** the Restore pass is deleted at B5 (AA2-PERF-01 step 5), not B4. If AA2-PERF-06 lands first, add a Restore stage name and trace section in the same one-line edit, because the pass survives through B4.
- **Effort:** S.
- **Timing:** delete with B5; otherwise with AA2-PERF-06 step 1.
- **Overlaps:** AA2-PERF-01, AA2-PERF-06, AA2-RT-01.
- **Panel discussion:** none.

#### CQ-CORE-13 Polish group

- **Severity:** low.
- **Category:** readability, magic number.
- **Sources:** HOST-13.
- **Evidence:** Application.cpp:384, 390 reserve 32,768 trace frames and stop silently (derived: about 199 s at 165 FPS). Files.cpp:60-71 has six one-line if statements against the house style (:60, 61, 64, 67, 69, 71). JobSystem.cpp:29 has a redundant outer max. UserData.cpp:299-312 encodes in O(rows^2 x size) (trivial at today's sizes). Application.cpp:207-208 keeps every tick's snapshot (derived: Arena 600 x 13,049 B ≈ 7.8 MB; a 4,000-entity scene would be ≈ 620 MB).
- **Fix:** name the trace cap and warn (with CQ-CORE-04); expand the Files.cpp statements; simplify JobSystem.cpp:29; UserData grouping with AA2-GP-05; bound determinism snapshots (keep hashes only past a budget) in the AA2-CORE-02 step 4 move.
- **Effort:** S.
- **Timing:** first three now; the rest with the named steps.
- **Overlaps:** AA2-GP-05, AA2-CORE-02.
- **Panel discussion:** none.

#### CQ-CORE-14 Raven3DSmoke compiles a second copy of GameSession.cpp

- **Severity:** low.
- **Category:** slop, duplicate.
- **Sources:** HOST-15 (new in round 2).
- **Evidence:** RavenEngine/CMakeLists.txt:48-57 adds Private/Core/GameSession.cpp to Raven3DSmoke (comment at :49, "the smoke compiles its own copy"; the second source at :51; link and dependencies at :56-57). GameSession.hpp:50 has no export. InputRouter.hpp:54 shows the per-member RAVEN_API precedent.
- **Why it matters:** a GameSession.cpp change that calls a non-exported RavenCore helper builds the engine but breaks only the smoke, which CI does not build (AA2-BLD-03).
- **Fix:**
  1. With AA2-CORE-02 step 3: export the FramePipeline entry points and the GameSession members the smoke calls, per member (class-level export risks C4275 from the non-exported GameServices base; unverified).
  2. Remove the second GameSession.cpp and its comment from the smoke target.
- **Effort:** S.
- **Timing:** Phase 4 with AA2-CORE-02 step 3.
- **Overlaps:** AA2-CORE-02, AA2-BLD-03.
- **Panel discussion:** none.

#### CQ-CORE-15 --seed cannot replay about half of the seeds the engine logs, because it parses into a signed 64-bit value

- **Severity:** medium.
- **Category:** readability, hidden contract (correctness of a developer workflow).
- **Frequency:** every reproduction attempt of a logged run.
- **Sources:** completeness critic (CRIT-1).
- **Evidence:**
  - GameSession.cpp:118-125: with no --seed, the seed is `(std::uint64_t{ device() } << 32) ^ device()` from std::random_device (32-bit result_type), so bit 63 is set in about half of all sessions (derived: P = 0.5). It is logged unsigned: "Simulation seed {}".
  - RavenEngine/Private/Host/RavenEngine.cpp:36-43: ParseInteger reads into `long long` with std::from_chars and throws "<option> expects whole numbers" on any error; a value above INT64_MAX gives errc::result_out_of_range.
  - RavenEngine.cpp:134-139: the comment says "--seed ... for reproducing a run", then `static_cast<std::uint64_t>(ParseInteger(argv[index], "--seed"))`. A logged seed of 2^63 or more is rejected with a misleading message; "-1" silently becomes UINT64_MAX.
  - RavenEngine.cpp:142-149: --determinism-check casts the same long long to uint32, so 4294967297 silently becomes 1 tick.
  - The contract is documented: GameServicesDesign.md:366 ("The seed comes from `--seed <n>`, or from the OS once per session, and is logged") and Application.hpp:32-34 (`std::optional<std::uint64_t> SimulationSeed`). ParseInteger's other callers, --max-fps (:68) and --borderless (:85-88), stay signed.
- **Why it matters:** level-1 determinism (FD-48) is debugged by rerunning a logged seed. About half of the logged seeds cannot be passed back, and the developer is told the seed is not a whole number. Nothing tests this path.
- **Fix (RavenEngine.cpp, one file, no contract change):**
  1. Add `std::uint64_t ParseUnsigned(std::string_view, const char* option, std::uint64_t max)` on std::from_chars into std::uint64_t (which does not accept a minus sign), rejecting anything above max with the message "expects 0 to <max>".
  2. Use it for --seed (max UINT64_MAX) and for --determinism-check (max UINT32_MAX, then the existing zero check). Keep ParseInteger for the signed options.
  3. No tracked test: the next manual replay of a high seed checks it.
- **Effort:** S.
- **Timing:** now (Phase 1 private item).
- **Overlaps:** none.
- **Panel discussion:** not discussed by the panel; added in verification.
- **Verification:** confirmed (critic proposal verified separately); severity medium (new) because it breaks the documented reproduction workflow for half of all runs, but is contained to one file.

#### CQ-CORE-16 The FindAsset-then-TryUseAsset lookup is copied five times in GameSession; the audio copies skip the kind check, so a wrong-kind sound reference is logged on every call although the comment says once

- **Severity:** low.
- **Category:** slop, duplicate.
- **Frequency:** per PlaySound call; per UI image per frame (texture resolver); per sprite per frame (GatherSprites, delete with B5).
- **Sources:** completeness critic (CRIT-2).
- **Evidence:**
  - Scene.cpp:627-644: TryUseAsset already does `m_Assets.find(id)`, returns the lease when the kind matches, and logs "incorrect asset type" or a missing reference once per ID through m_ReportedAssets.
  - GameSession.cpp:298-302 (UI texture resolver) and :425-428 (GatherSprites): `FindAsset(id); if (!lease || lease->GetKind() != Texture) lease = TryUseAsset(...)`, which does the same as TryUseAsset alone.
  - GameSession.cpp:331-341 (PlaySound(AudioAsset)) and :358-360 (the sound-event clip): `if (!lease) lease = TryUseAsset(...)` with no kind check, so a lease of another kind reaches AudioPlayer::Play, which throws internally and logs "Cannot play scene audio" at Warning on every call with no latch (AudioPlayer.cpp:77-89). The sound-event path (:350-352) gets GetSoundEvent() == nullptr on a wrong kind and returns {} silently, logged zero times. Both contradict GameSession.cpp:332-333 ("a reference it does not hold is a data error, logged once (FD-06)").
  - Reachability is narrow. LeaseAssets (GameSession.cpp:57-91) calls UseAsset, and AssetStore::Acquire (AssetStore.cpp:180-195, kind checks at :182, :194) rejects a kind mismatch, so a wrong-kind ID in a scene file rejects the scene at load. The per-call spam needs a runtime, game-constructed reference to an ID the scene already leases under another kind. The clip lookup at :358-360 is reachable with a wrong kind only when the clip ID is leased under another kind, because load leases event clips as Audio (:86-90).
  - grep `FindAsset(` in RavenEngine and Games: GameSession.cpp:88, 300, 338, 350, 358, 394 and 426, plus the Scene definition. Only :394 (Pick, which must never lease) and :88 need FindAsset's const, no-lease behavior.
- **Why it matters:** the comment tells reviewers a log-once guarantee that does not hold on the drifted path, and when AA2-RT-03 makes TryUseAsset catch decode failures, the FindAsset short-circuits are where callers silently miss that protection. There is no speed benefit: both do one hash lookup on a hit.
- **Fix:**
  1. GameSession.cpp: replace the pairs at :300-302, :338-340, :350-352 and :358-360 with a single `scene.TryUseAsset(catalog, id, kind)`. Leave GatherSprites (:426-428) alone: it is deleted with B5.
  2. In the same snippet, change :332-333 to name where the log-once lives (Scene::TryUseAsset).
  3. With AA2-RT-03, note in Scene.hpp:67-71 that TryUseAsset is the only lookup game-facing services may use; Pick keeps FindAsset because it must never lease. No test: the change is behavior-preserving for correct data.
- **Effort:** S.
- **Timing:** now (step 3 with AA2-RT-03).
- **Overlaps:** AA2-RT-03, CQ-DATA-01 (the kind tables these calls feed).
- **Panel discussion:** not discussed by the panel; added in verification.
- **Verification:** confirmed with corrections (critic proposal verified separately); severity medium (proposed) -> low because a wrong-kind ID in a scene file is already rejected at load, so the drift needs a runtime-constructed reference.

### 5.4 DATA: ECS, scene, assets and tools

#### CQ-DATA-01 Asset-kind knowledge is copied into six hand-kept tables; the two scene-dependency scans are parallel copies, and B2, B4 and B7 each add a row to all of them

- **Severity:** medium (revised from high in verification; schedule early in Phase 1).
- **Category:** slop, duplicate.
- **Sources:** DATA-1, absorbing API-1; endorsed by every panel in round 2.
- **Evidence:**
  - GameSession.cpp:59-63 and PlayerContent.cpp:73-77: two identical tag-to-AssetKind tables (Texture, Mesh, Audio, Sound). A repo-wide grep for `HashText(AssetTypeTraits` finds only these 8 lines plus Component.hpp:207.
  - GameSession.cpp:64-82 vs PlayerContent.cpp:90-107: the same AssetRef field scan. Differences:
    - Extent products: GameSession multiplies uint32 extents (:73), PlayerContent widens to size_t (:98). This cannot be observed: Extent comes from compile-time fixed arrays, SceneFile.cpp:78-84 CheckField computes the count in uint64 and rejects a malformed product before either scan runs, and GameSession.cpp:75 multiplies by the entity count in size_t.
    - Kind checks: GameSession keeps the first tag seen through std::map::emplace (:80) and relies on AssetStore::Acquire (AssetStore.cpp:182-183) and Scene::UseAsset (Scene.cpp:618-619) to reject a catalog mismatch; PlayerContent throws on any catalog mismatch (:65-69). The one remaining gap is an asset ID referenced under two different tags, which GameSession checks for its first tag only.
  - GameSession.cpp:83-91 vs PlayerContent.cpp:111-120: sound-event clip expansion. These are parallel logic, not copies: GameSession reads the leased, parsed SoundEvent (:88-90), while PlayerContent re-parses the file with a 1 MiB cap (:118) against AssetStore's 64 MiB cap (AssetStore.cpp:26).
  - More tables: SceneProject.cpp:92-99 (rejecting unknown records) and :106-125 (keyword to kind registration), PlayerContent.cpp:33-55 (kind to keyword, throwing default at :52-53), SceneProject.cpp:143-148 (kind to extension, where every unlisted kind falls to ".ravenscene" at :145-146, so a new Material or Shader record would fail with a misleading "Incorrect source type"), AssetStore.cpp:29-39 (KindName) and :41-61 (extension dispatch). The completeness critic found a sixth list: AssetCatalog.cpp:32-47, an exhaustive switch that already lists Material (not separately verified). Lowercase-extension code three times: AssetStore.cpp:45-49, SceneProject.cpp:143-144, AudioClip.cpp:26-28.
  - No ForEachAssetRef or AssetKinds exists. AssetRef.hpp:20-30 already defines Material, Model, Shader and other trait names, and AssetRecord.hpp:10-24 already has those enum values. M6B adds at least three kinds that must reach every table: B2 Material, B4 Shader and B7 Model sub-assets (RenderingContentDesign.md:58 FD-18, :209 B2); AA2-MEDIA-05 adds Font and AA2-RND-06 shader assets.
- **Why it matters:** if B2 adds Material to GameSession's table but not PlayerContent's, the editor works, but staging skips the unknown tag by design (PlayerContent.cpp:96), so `RavenSceneTool stage-player` omits the material; SceneProject's tolerant reader then skips the unknown record kind (:92-99), so it is missing from the player's catalog. The packaged player's scene load then throws out_of_range "Asset ID is not registered" (AssetCatalog.cpp:74, through Scene::UseAsset -> AssetStore::Acquire:182). The failure is loud, but it appears only in a packaged player: CI runs `ctest -LE gpu` only (windows.yml:51), has no stage-player step, and a stage-player step would stay green in exactly this failure (BLD, round 2). No bug has occurred yet, and the code runs per scene load and per staging run.
- **Fix:**
  1. Add Private/Assets/AssetKinds.hpp: one constexpr row per kind {AssetKind, project keyword, accepted extensions or an explicit "not file-backed" value, tag hash, display name} plus KindFromTag, KindFromKeyword, KeywordOf, LowercaseExtension and AcceptsSource. A kind without an extension makes the SceneProject.cpp:145-146 fallback throw an unknown-kind error instead of defaulting to ".ravenscene" (rule R1). Until Phase 3, a static_assert per row checks that HashText(AssetTypeTraits<X>::Name) equals its tag column.
  2. Add ForEachAssetRef(const SceneFile&, function(AssetId, AssetKind)) and ExpandSoundClips(const SoundEvent&, callback) in Private/Assets/AssetDependencies.cpp, with one size_t extent product. Policy, stated in the helper: an ID seen under two different tags is rejected in both consumers. Switch GameSession.cpp:57-91 to it. That factory is also where AA2-RT-05 step 3 reorders LeaseAssets before InstantiateScene (GameSession.cpp:107-110): land AA2-RT-05 step 3 first, or both in one snippet.
  3. Switch PlayerContent.cpp:72-120 to both helpers (PlayerContent parses the event file and passes the SoundEvent to ExpandSoundClips; its size cap is settled by CQ-DATA-09).
  4. Switch SceneProject.cpp:92-149, PlayerContent.cpp:33-55 and AssetCatalog.cpp:32-47 to the table.
  5. AssetStore.cpp:29-61 and AudioClip.cpp:26-28: only the shared LowercaseExtension and KindName helpers. Leave the decoder dispatch shape to B6 (async asset resolution reworks that load path; no-throwaway).
  6. Phase 3, with the AA2-ECS-01 FieldKind pin: add `static constexpr AssetKind Kind` beside each AssetTypeTraits Name (AssetRef.hpp:20-30) and generate the table from the traits. The field Tag stays HashText(Name), because it is persisted (CQ-X-02).
  7. B2 then adds Material as one row (B4 Shader and B7 Model likewise). No new test: after step 3 both consumers call the same two helpers.
- **Effort:** M.
- **Timing:** steps 1-5 now (Phase 1 private items, early, before B2); step 6 Phase 3.
- **Overlaps:** AA2-ECS-04 (the helper becomes its single change site), AA2-RT-05 step 3 (same GameSession function), AA2-MEDIA-05 (AssetKind::Font), AA2-RND-06 (shader assets), ARCH ASSET-05, CQ-CORE-16, CQ-DATA-09.
- **Panel discussion:** unanimous. API's version had the narrower table; DATA's is the primary.
- **Verification:** confirmed with corrections; severity high -> medium because the code runs per scene load and staging run, no bug has occurred, and the predicted failure is loud; it stays a high-priority medium because B2, B4 and B7 each add a row.

#### CQ-DATA-02 The field-matching rule is implemented twice and both copies depend on declaration order; an ambiguous alias table silently resets scene and profile values

- **Severity:** medium (raised to high by the panel; lowered in verification).
- **Category:** readability, duplicate.
- **Sources:** DATA-2, API-2 evidence; corrected by the lead and re-traced by DATA and API in round 2.
- **Evidence:**
  - Registry.cpp:516-529 MapFields (live migration) loops over the old fields for each new field and overwrites `source`, so the last old field matching by name or alias wins (:522-525).
  - Registry.cpp:483-492 FindField: exact name first, then aliases (FD-16's documented rule, DataModelDesign.md:374: match "the current names first, then the aliases"). SceneFile.cpp:797-817 MatchFields (scene load, and ApplyRecord for user data, SceneFile.cpp:1020-1040, called at :1027) iterates the stored fields, resolves each through FindField and overwrites source[] (:812). Targets that match nothing are never reported; only unknown or retyped stored fields are (:807, :814).
  - Scenario: new {Health; MaxHealth} with aliases "Current=Health" and "Health=MaxHealth" (a rename chain).
    - Migrate, old declared {Health; Current}: correct.
    - Migrate, old declared {Current; Health}: new Health and MaxHealth both take old Health; old Current appears only in the "removed" list that the activation log prints (GameModule.cpp:363).
    - Load of a file saved before the edit, either order: MaxHealth keeps its default. With stored order {Current; Health}, new Health also takes the wrong value (stored Health instead of Current).
    - After the chain is applied, the aliases stay until the unwritten `upgrade` retires them (DataModelDesign.md:628). A later unrelated edit that migrates the type can then shift values: old {MaxHealth; Health} migrating to new {MaxHealth; Health; Armor} gives new MaxHealth old Health, because the alias match comes last.
  - The profile path is real: UserDataService::Read (UserData.cpp:221-223) calls ApplyRecord, and Write then rewrites the record in the new layout (UserData.cpp:235-239), so a lost value persists.
  - MakeComponentSchema (Registry.cpp:442-459) accepts two fields claiming one old name. Migrate then fills both, load only the first field FindField returns.
  - Type aliases have two different ambiguities, not one drift. FindPredecessor (Registry.cpp:504-513) handles one new type whose OldNames name several registered types; the first in unordered_map order wins. Instantiate (SceneFile.cpp:915-923) handles several registered types claiming one stored type name; the last in name-sorted order wins. In Migrate, two new types claiming one old type both plan the same From ID, and the second extract finds no node (Registry.cpp:616), so that type silently gets no values.
  - The type comparison is written twice: Registry.cpp:494-498 (IsSameFieldType) and SceneFile.cpp:810-811. FindField and IsSameFieldType are public RAVEN_API (Public/Raven/ECS/Registry.hpp:150-155); only SceneFile.cpp and RegistryTests.cpp:131 call FindField.
  - This contradicts the AA2 section 3 strength "One field-matching rule" (ArchitectureAssessment-2026-11-26.md:127); correct that line rather than leave the two documents in conflict.
- **Why it matters:** two copies of one rule give different values for the same edit, and one path is player profiles (FD-33), which are persisted. Ordinary single renames (the only kind the tests and FD-16 exercise, for example RegistryTests.cpp:40 "Value=Points") match identically on both paths. The failure needs a rename chain or swap (an alias whose old name is also a current field name) or one old name claimed by two fields; both can be detected at registration. AA2-ECS-01 and AA2-ECS-04 are about to change this code again.
- **Fix (private files only for the matcher):**
  1. Add Private/ECS/FieldMatching.hpp with one per-target matcher `MatchSources(stored names and types, const ComponentSchema& target)` that follows FD-16's name-first rule (exact current name, then aliases). Reuse IsSameFieldType for the type comparison, or a FieldSchema/SceneFile::Field adapter until CQ-API-01 merges the field structs, so no third copy appears. Do not invent a new priority: an "alias claim wins" rule would reverse FD-16's recorded decision, and it corrupts data, because a file saved in the new layout while the chain aliases remain would load MaxHealth from stored Health. A name-only rule cannot resolve a chain or a swap, since a stored set may be in the old or the new layout (A=B, B=A is fully ambiguous).
  2. Make Registry.cpp MapFields and CheckMigration call it.
  3. Make SceneFile.cpp MatchFields call it; also log target fields left at default when the stored section had fields. Either FindField delegates to the matcher's rule, or FindField is retired in the Phase 3 public-API window; otherwise a third embodiment remains public.
  4. MakeComponentSchema rejects any alias whose old name is a current field name (this covers self-aliases, chains and swaps) and any old name claimed by two fields, with an error telling the developer to do such renames in two separate steps (rename, reload, then rename again). Registration rejects one old type name claimed by two types and one new type whose OldNames name several registered types.
  5. Record the rejection rule in DataModelDesign FD-16, and correct ArchitectureAssessment-2026-11-26.md:127.
  6. Extend the existing alias test (RegistryTests.cpp:40, 131-151): both declaration orders and the load path give the same result for an ordinary rename, and the ambiguous chain table is rejected at registration. Do not assert that a chain migrates. This guards exactly the rule being rewritten.
  7. Split out as its own item (unrelated to field matching): ToInfo(const ComponentSchema&) (the Instantiate site is SceneFile.cpp:932) and one pool-compatibility predicate in Registry.cpp, today written at :259, 686-687, 690, 736-737 and 749. The predicate must state whether it compares Name: :259 and :686-687 deliberately omit it, because LayoutHash includes the name (:251-252).
- **Effort:** S.
- **Timing:** now (Phase 1 private items), before AA2-ECS-01/04; step 7 any time.
- **Overlaps:** AA2-ECS-01, AA2-ECS-04, CQ-API-01.
- **Panel discussion:** DATA first claimed live migration was correct; the lead's spot-check showed it is order-dependent, and DATA and API confirmed. API preferred medium (rename chains are not routine); DATA raised to high, and the lead decided high. Verification returned it to API's medium and replaced the proposed priority rule, which reversed FD-16, with registration-time rejection.
- **Verification:** confirmed with corrections; severity high -> medium because ordinary renames match identically and the failing alias tables are statically detectable at registration, so the defect is contained and preventable.

#### CQ-DATA-03 Scene instantiate is dominated by hash maps, a vector of vectors and triple-copied labels

- **Severity:** medium.
- **Category:** slow, hot-path allocation.
- **Frequency:** per scene load, restart, scene reload, quick-load and each AA2-RT-05 retry.
- **Sources:** DATA-3.
- **Evidence:**
  - Measured at 200k entities, Release, median of 5 (DataModelDesign.md:662): instantiate 600 ms = creating entities about 200 ms (half labels), 420k adds about 135 ms, 200k parents about 75 ms, hierarchy ordering about 190 ms. Decode is 33 ms.
  - SceneFile.cpp:373-424 HierarchyOrder builds a count-sized unordered_map (:376-379) and a vector<vector<size_t>> (:380), with two index.find calls per ParentComponent row (:396-397). Instantiate (:876-988) builds two more 200k maps, rank (:891-894) and created (:895-896). Each section does one rank.find per row plus a std::sort (:935-941), then one created.find per row (:944).
  - Every load, restart and quick-load reaches InstantiateScene through SceneFile::Decode of Encode()d bytes (GameSession.cpp:107-109; quick-save at :197-200), and Encode always calls Normalize (:573-576; Normalize at :489-521), so rows and entities are ID-sorted in every written file. Decode does not enforce that order, so the fix keeps a fallback.
  - SceneFile.cpp:475-480 FieldOffset is O(field index), called per row per field (:958-959).
  - Labels: SceneFile.cpp:900 copies the label (only because it may be cleared on a duplicate) and :901 hashes it; Scene.cpp:170 hashes it again; Adopt emplaces it into m_Names (:207) and copies it into m_Labels (:208). Scene.hpp:144-145 stores the label twice.
- **Why it matters:** the 100k-entity goal scale and the restart/quick-load iteration loop. Estimate: the maps that steps 1-3 remove cost about 2.2M hash operations at 200k (derived: HierarchyOrder about 600k = 200k emplaces plus 2 x 200k finds; rank 200k emplaces plus about 620k finds; created 200k emplaces plus about 620k finds), and replacing them and 100k+ small vectors with flat vectors and dropping two label copies saves roughly 150-250 ms of 600 ms (estimate, against the measured 190 ms ordering bucket plus about 100 ms of labels).
- **Fix (measure each step with the DataModelDesign.md:662 method):**
  1. SceneFile.cpp: HierarchyOrder returns order plus a position-to-rank vector, and builds child lists as CSR (counts, prefix offsets, one fill). The hierarchy order AA2-ECS-07 relies on is kept.
  2. SceneFile.cpp: replace rank and created with vectors indexed by file position, mapping row IDs by a merge walk when sorted (std::is_sorted check), the map as fallback.
  3. SceneFile.cpp: fill a rank-indexed row vector per section instead of sorting, and compute stored field offsets once per section.
  4. SceneFile.cpp: bind a const reference to entity.Label, or to a static empty string in the duplicate case, instead of copying.
  5. Phase 3 (Scene.hpp is public until AA2-CORE-03 moves it): one try_emplace in Adopt and a transparent hash on m_Names, in the same snippet as CQ-API-11 step 2. "One label store" means keying m_Names by a string_view into m_Labels' strings (or the reverse), not deleting a map: m_Labels (index to name) is needed for GetAuthoredId and Capture.
  6. Only if adds still dominate: a private Registry entry that resolves and reserves a pool once per section.
  7. Split Instantiate into named phases while doing 1-4 (see CQ-DATA-07). The SceneFileTests round trip (SceneFileTests.cpp:67-152) guards behavior.
- **Effort:** M.
- **Timing:** steps 1-4 now; 5-6 Phase 3.
- **Overlaps:** AA2-ECS-07, AA2-RT-05, AA2-PERF-07, CQ-DATA-11, CQ-API-11.
- **Panel discussion:** none.
- **Verification:** confirmed with corrections; severity medium -> medium because the cost scales with entity count at the 100k goal and sits on the restart and quick-load loop, but is not per frame.

#### CQ-DATA-04 AssetStore::Acquire costs O(assets + texture slots) plus 4-6 weakly_canonical calls per unique asset

- **Severity:** low (revised from medium).
- **Category:** slow, O(total).
- **Frequency:** per unique asset per scene load (LeaseAssets) and per first runtime use.
- **Sources:** DATA-4.
- **Evidence:** GameSession.cpp:84-90 calls Scene::UseAsset per asset, which calls Acquire (Scene.cpp:613-625). AssetStore.cpp:182-186 calls catalog.Get twice, ResolveAssetPath, is_regular_file and Collect(). Collect (AssetStore.cpp:270-274) runs erase_if over the whole map plus the renderer's slot walk (CQ-VK-11). Load (:105) and FindReferenceError (:199-210) resolve the same path again. Each ResolveAssetPath calls weakly_canonical twice, one on the constant root (AssetPath.hpp:10-21; AssetStore.cpp:96, 140). Content today: 16 project records in Arena, 15 in Samples.
- **Why it matters:** derived: at 4,096 assets, about 8M map-node visits plus the slot walk and hundreds of canonicalizations. At today's N of about 15-50 it is negligible; the scale exists only in the synthetic AA2-PERF-04 variety case.
- **Fix:**
  1. Canonicalize Root once; add a ResolveAssetPath overload taking a canonical root (AssetStore.cpp, AssetPath.hpp).
  2. In Acquire, resolve once and pass the path into Load; drop the second catalog.Get.
  3. Stop calling Collect from Acquire (reuse a dead weak entry in place); call Collect once after LeaseAssets (AssetStore.cpp, GameSession.cpp).
  4. Measure LeaseAssets on the AA2-PERF-04 variety case (256 meshes, 64 materials), together with CQ-VK-11.
- **Effort:** S.
- **Timing:** before B6.
- **Overlaps:** AA2-HR-04, AA2-RT-03, ARCH ASSET-06, CQ-VK-11.
- **Panel discussion:** GAME showed asset counts are tiny; the lead lowers to low and keeps the cheap steps.

#### CQ-DATA-05 TransformInterpolator does about 6 hash lookups per moving entity per frame

- **Severity:** medium.
- **Category:** slow, hot-path lookup.
- **Frequency:** per moving entity per frame, plus per fixed step.
- **Sources:** DATA-5; RND answered the keying question.
- **Evidence:** entries live in an unordered_map keyed by persistent ID (TransformInterpolator.hpp:54-55). Apply (TransformInterpolator.cpp:84-105) does Resolve (an m_Ids find, Scene.cpp:274-281), TryGet<const TransformComponent> (a FindPool unordered_map find, Registry.cpp:247, 253-260) and Write (another FindPool). Restore (:107-116) repeats Resolve and Write. ForEachMoving (:29-51) does a TryGet<const InterpolateComponent> exclusion per body and per mover (:42, :48). The m_Entries[] lookups are in BeforeStep (:57) and AfterSteps (:70), and AfterSteps also runs an erase_if over the whole map (:81) on every stepping frame: O(entries), not O(moving). Apply's Same() early-out (:91) lets resting entries skip Apply and Restore, but they still pay the BeforeStep and AfterSteps lookups. Call sites: BeforeStep per step at Application.cpp:500, AfterSteps at :542 and :573, Apply at :612, Restore at :644. Clear runs on scene replace (:534).
- **Why it matters:** derived: every frame costs 5 lookups per moving entry (Apply 3, Restore 2); AfterSteps adds 1-2 only on frames that stepped, and BeforeStep 1-2 per fixed step. At 165 Hz with a 60 Hz step that averages about 5 + (60/165) x 2-4 = 5.7-6.5 lookups per moving entity per frame. At 20-40 ns each, the AA2-PERF-04 game-scale case (500 movers plus 2k bodies) would cost about 0.3-0.7 ms per frame (estimate), the same order as the 3.5 ms budget's headroom. Unmeasured, and smaller than AA2-PERF-01's second world pass.
- **Fix:** do not polish today's Apply/Restore (B4 replaces them; Restore is deleted at B5). These are implementation constraints for AA2-PERF-01 step 2 (which already requires keying the override by entity, not by render slot), not a separate ticket:
  1. Key entries by ECS::Entity (index plus generation), stored in a dense vector with an index-to-entry vector (the CQ-GP-04 EntitySlots shape), validated by one generation compare. The interpolator is cleared on every scene replace (Application.cpp:534), so the persistent ID buys nothing.
  2. Apply becomes a linear scan, and the stepping-frame erase_if becomes O(moving).
  3. Replace the per-entity exclusion TryGet with one PoolView membership test (Detail::PoolView::Find, Registry.hpp:56-61).
  4. Write blended matrices into the render-side array, not through SceneAccess::Write (AA2-PERF-01 steps 2-3).
  5. Measure with the AA2-PERF-04 generator and the AA2-PERF-06 column.
- **Effort:** S (inside AA2-PERF-01 step 2).
- **Timing:** during B4 with AA2-PERF-01 step 2.
- **Overlaps:** AA2-PERF-01 (primary), AA2-PERF-04, AA2-PERF-06, CQ-GP-04.
- **Panel discussion:** RND rejected both a render slot (slots swap-erase) and the persistent ID; key by entity, as AA2-PERF-01 already requires.
- **Verification:** confirmed with corrections; severity medium -> medium because the cost is per moving entity per frame at the AA2-PERF-04 scale, though unmeasured and filed as B4 implementation notes under AA2-PERF-01.

#### CQ-DATA-06 Asset decode makes two avoidable full copies, and every PNG load runs a byte-wise CRC

- **Severity:** low (revised from medium in verification).
- **Category:** slow, redundant pass.
- **Frequency:** per asset load and reload.
- **Sources:** DATA-6.
- **Evidence:** DecodeAsset copies the whole span into DecodedAsset::Bytes (AssetStore.cpp:41-44); Load passes a temporary from ReadAsset at :107, and :125 then moves Bytes into the asset, so the copy at :44 is avoidable. Image.cpp:89-101: stb decodes into its own buffer, then resize (which value-initializes, a zero-fill) plus memcpy into ImageData::Pixels (peak 2x, up to 2 x 256 MiB per Image.cpp:29). PngFileWatch.cpp:37-76: DecodePngSource (:85-88) always calls ValidatePng (:49-76), a table-driven byte-at-a-time CRC32 (:37-47) over every chunk, reached on every PNG load including player and --no-watch runs (AssetStore.cpp:52-53). stb_image itself skips CRCs (ThirdParty/stb/stb_image.h:4599, 5236, 5258), so this is the only chunk-integrity check. FbxMesh.cpp:155 assigns a same-typed std::vector<std::uint32_t> (FbxMesh.hpp:16) where a move would do (:148 is the local's declaration). Today's content has five PNGs, the largest 375 bytes, so every cost figure is a future M6B-content estimate.
- **Why it matters:** derived: a 64 MiB texture costs an extra 7-13 ms memcpy plus a zero-fill; a byte-wise CRC at an estimated 0.5-1 GB/s adds 20-40 ms to a 20 MB PNG, against stb inflate and unfiltering of hundreds of ms for the same file (estimate), so single-digit percentages. B6 moves decode to workers, which hides latency but not the memory peak or the pool time.
- **Fix:**
  1. Before B6 (not needed now with today's tiny PNGs): switch the CRC to slicing-by-8 (PngFileWatch.cpp), which keeps behavior. Do not restrict the full CRC to the reload path: that removes the only chunk-integrity check for staged and player content, and it would need a second decode function or flag, because the watch path reuses the same DecodeAsset through SettledFileWatch (AssetStore.cpp:131-133).
  2. With AA2-HR-04 step 2: move the read vector instead of copying (AssetStore.cpp, ManagedAsset.hpp).
  3. Let ImageData own the stb allocation, through the settings seam AA2-BLD-02 asks for (Image.cpp/.hpp). ImageData is public (Raven/Assets/Image.hpp:18-23, Pixels is std::vector<std::byte>), so this lands in the Phase 3 contract window, or after AA2-CORE-04 makes the type private, not merely during B6.
  4. FBX index move: delete with B7 (DecodeFbxMesh is rewritten).
- **Effort:** S.
- **Timing:** step 1 before B6; step 2 with AA2-HR-04; step 3 in the Phase 3 window or after AA2-CORE-04; step 4 with B7.
- **Overlaps:** AA2-HR-04, AA2-MEDIA-02, AA2-BLD-02, AA2-PERF-03, AA2-CORE-04.
- **Panel discussion:** none.
- **Verification:** confirmed with corrections; severity medium -> low because the path is per asset load, today's PNGs are at most 375 bytes, and the costs are a small fraction of stb decode (estimate).

#### CQ-DATA-07 SceneFile.cpp is a 1,046-line file doing four jobs

- **Severity:** low.
- **Category:** readability, long function.
- **Sources:** DATA-7.
- **Evidence:** binary helpers and decoder 32-204, text helpers 206-472, binary codec 475-632, text codec 634-790, matching and capture/instantiate 792-1046. Instantiate 876-988 (113 lines), FromText 677-790 (114 lines, a keyword if-chain). Unknown sections are hex-encoded through WriteElement with substr(1) (:669-672); hex decode is written twice (:462-470, :775-778), with a temporary std::string (small enough for the small-string buffer, so no heap allocation) and a ParseNumber call per byte.
- **Why it matters:** AA2-ECS-01, -04 and -05 all land in this file next.
- **Fix:**
  1. Move the text codec into SceneFileText.cpp behind the same header (one move snippet; folder discovery needs no CMake edit).
  2. One HexEncode/HexDecode pair without substr.
  3. Split Instantiate into phases with CQ-DATA-03, not before.
- **Effort:** S.
- **Timing:** steps 1-2 now, before the AA2-ECS-01 policy snippet; step 3 with CQ-DATA-03.
- **Overlaps:** AA2-ECS-01, AA2-ECS-04, AA2-ECS-05.
- **Panel discussion:** none.

#### CQ-DATA-08 The dead AssetResources closure is wider than AA2-CORE-04 lists, and ships an unreachable JPEG decoder

- **Severity:** low.
- **Category:** slop, dead code.
- **Sources:** DATA-8 (its public test-only overloads moved to CQ-API-07).
- **Evidence:** grep `Images::Load` finds only AssetResources.cpp:67 (dead per AA2-CORE-04), so Image.hpp:12-16, 26-30 and Image.cpp:43-58 die with it. grep `ReadEncoded` finds only the dead Load and the dead PngFileWatch class (PngFileWatch.cpp:91). grep `ReadFbxBytes` finds only AssetResources.cpp:109, 115, 122. DecodePngSource only wraps DecodePng (PngFileWatch.cpp:78-88). STBI_ONLY_JPEG (Image.cpp:15) compiles a decoder no path can reach (textures must be .png, SceneProject.cpp:145; AssetStore.cpp:52-53; PngFileWatch.cpp:51-55).
- **Fix:** extend AA2-CORE-04 step 1's list: delete Images::Load x2, ImageLoadOptions, ReadEncoded, ReadFbxBytes, the PngFileWatch class and the wrapper (keeping one DecodePngSource in ImageSource.hpp), remove STBI_ONLY_JPEG and fix "PNG/JPEG" at Image.cpp:39. Three snippets.
- **Effort:** S.
- **Timing:** Phase 3 with AA2-CORE-04 (Image.hpp is public).
- **Overlaps:** AA2-CORE-04, CQ-API-07.
- **Panel discussion:** none.

#### CQ-DATA-09 File-size limits are magic numbers repeated per call site, and the engine and RavenSceneTool disagree

- **Severity:** low.
- **Category:** slop, magic number.
- **Sources:** DATA-9; confirmed by BLD; MEDIA added two sites.
- **Evidence:** project file 4 MiB in the engine (SceneProject.cpp:26) vs 1 MiB in stage-player (RavenSceneTool.cpp:111). Sound event 64 MiB via the store (AssetStore.cpp:26) vs 1 MiB in staging (PlayerContent.cpp:118). Scene 1 GiB three times (SceneSession.cpp:21, RavenSceneTool.cpp:29, PlayerContent.cpp:88). 64 MiB at AssetStore.cpp:26, FbxMesh.cpp:20, Image.cpp:28, AudioClip.hpp:20, ReloadService.cpp:29, TextSystem.cpp:178, 192.
- **Why it matters:** a 1-4 MiB project runs in the engine but fails packaging.
- **Fix:**
  1. Private/Core/FileLimits.hpp naming MaxSceneBytes, MaxProjectBytes, MaxSoundEventBytes, MaxEncodedAssetBytes with reasons; use it in SceneSession.cpp, SceneProject.cpp, AssetStore.cpp.
  2. Use the same names in RavenSceneTool.cpp and PlayerContent.cpp.
  3. Point the decoders at MaxEncodedAssetBytes, or fold into AA2-HR-04 step 6. TextSystem's two sites go with CQ-BLD-06.
- **Effort:** S.
- **Timing:** now (step 3 with AA2-HR-04 step 6).
- **Overlaps:** AA2-HR-04, AA2-MEDIA-02, CQ-BLD-06.
- **Panel discussion:** none.

CQ-DATA-10 (generic SceneCommands Add/RemoveComponent bypassing the host-owned rule) was removed in verification as a duplicate of AA2-ECS-02; its one code-level addition is recorded in section 10.4 as an evidence addendum for AA2-ECS-02.

#### CQ-DATA-11 Lifecycle journal: one failed push permanently disables backlog tracking, and each entry does a hash lookup per subscriber

- **Severity:** low.
- **Category:** slow, hidden ordering.
- **Frequency:** per Add/Remove/Destroy of a subscribed type, per subscriber.
- **Sources:** DATA-14 (new in round 2).
- **Evidence:** Registry.cpp:282-306: on a push_back failure the catch sets MaxBacklog = 0 for every subscriber and never restores it, so every later read forces a full Resync for the rest of the session. Record is noexcept, so the bad_alloc never reaches the fatal tier. Registry.cpp:291-297: Subscribers.at(id) per subscriber per entry; derived: about 3 Transform subscribers x about 20 ns ≈ 60 ns per add, about 12 ms per 200k-entity load (inside the recorded 135 ms adds bucket).
- **Fix (Registry.cpp):**
  1. In the catch, set Cursor = End() and Resync = true per subscriber and leave MaxBacklog alone, with a one-line comment.
  2. Store LifecycleSubscriber pointers (node addresses are stable), keeping the id only for Unsubscribe.
- **Effort:** S.
- **Timing:** now, measured with CQ-DATA-03.
- **Overlaps:** AA2-PERF-02, CQ-DATA-03.
- **Panel discussion:** none.

#### CQ-DATA-12 Scene polish: third and fourth copies of the Euler order, an undocumented batch tail drop, and small nits

- **Severity:** low.
- **Category:** readability, duplicate.
- **Sources:** DATA-15, DATA-12 step 5.
- **Evidence:** Scene.cpp:478-479 (rotationOnly) spells RotationZ*RotationY*RotationX, a copy beside TransformComponent::GetMatrix (TransformComponent.hpp:18-22) and AppendLocalTransform (Scene.cpp:80-120); the public ViewFromPoseRH (CameraView.hpp:111) is a fourth copy (CQ-API-14); HierarchyTests guard only the world path. GetCameraView walks the ancestry twice per frame (Scene.cpp:600-601). SceneCommands.cpp:86-90 swaps the buffer out first, so a throw drops every later command; SceneCommands.hpp:17-21 does not say so. Nits: no why-comment on the pool re-fetch after Add/Remove (Scene.cpp:549-550, 582-583); a broken comment (TransformInterpolator.hpp:17-18); SceneSession.cpp:154-155 copies descriptions only to search them.
- **Fix:**
  1. Now: document the tail drop in SceneCommands.hpp; add the re-fetch comment; fix the broken comment; search the span directly.
  2. Phase 3 or B7: a constexpr EulerRotation(Vec3) in Transform.hpp used by GetMatrix, the rotationOnly path and ViewFromPoseRH (CQ-API-14 step 2); take camera orientation from the cached world matrix only if B4 touches camera selection anyway.
- **Effort:** S.
- **Timing:** step 1 now; step 2 Phase 3 or with B7.
- **Overlaps:** AA2-RT-01, CQ-API-10, CQ-API-14.
- **Panel discussion:** none.

### 5.5 GP: physics and input

#### CQ-GP-01 PhysicsWorld's Entry mirrors Desired through hand-copied field lists that drifted: HasRigidBody is never refreshed, so Jolt's friction can differ from the component

- **Severity:** medium (GP proposed high).
- **Category:** slop, duplicate.
- **Sources:** PHYS-1; verified independently by GAME, GP and the lead.
- **Evidence:**
  - PhysicsWorld.cpp:307-329 (Entry) and :602-611 (Desired) hold the same fields. Both value-initialize RigidBody to RigidBodyComponent{} (Friction 0.5, Motion Dynamic; PhysicsComponents.hpp:56-72).
  - :628-631: Evaluate copies the component only when HasRigidBody. :752-765: Build sets mFriction, mRestitution and mMotionQuality only when HasRigidBody, so a bare collider's body keeps Jolt's default `mFriction = 0.2f` (ThirdParty Jolt BodyCreationSettings.h:107) while Entry stores 0.5.
  - :779-789: Build copies every field. :944-950: Update copies Collider, RigidBody, velocities, Applied, Transform and World but never HasRigidBody. :865-867: the rebuild test compares motion, Sensor and Child but not HasRigidBody. Child cannot drift: a Child mismatch rebuilds the body, which goes through Build's full copy. HasRigidBody is the only mirrored field that can go stale.
  - Failure 1: add RigidBodyComponent{Motion=Static, Friction=0.5} to a bare collider. No rebuild (Static == Static), the friction compare at :908 sees 0.5 == 0.5, and Jolt keeps 0.2. A scene restart gives 0.5. Only friction diverges; restitution 0 and Discrete match Jolt's defaults (BodyCreationSettings.h:104, 108).
  - Failure 2: remove a Static RigidBody with Friction 0. HasRigidBody stays true, Jolt keeps 0, and :945 (`entry.RigidBody = desired.RigidBody`) stores the default Dynamic motion (:946-947 overwrite only the velocities), so the entity's next change rebuilds through :865 with friction 0.2 and ends and restarts its contacts. No stray write-back occurs meanwhile: WriteBack (:574) skips the static body, and a static sensor fails TryGet<RigidBody> at :584-586.
  - :323-324, 593-596, 786-787, 941-947: Entry::Linear/Angular always equal the RigidBody velocities. SameVector is exact equality (JoltSupport.hpp:46-49), so copying Desired whole behaves the same.
  - No shipped game adds or removes a RigidBody at run time (grep `Commands.(Add|Remove)` in Games: only CourtyardGame.cpp:166-167 and Screens.cpp:24; SampleSceneBuilder is an offline tool).
- **Why it matters:** this breaks FD-32's "components are the source of truth" until an unrelated edit or restart, and it sits in the code AA2-PERF-02 step 2 and AA2-GP-01 rewrite next, so the drift would spread.
- **Fix (PhysicsWorld.cpp):**
  1. Add `desired.HasRigidBody != entry.HasRigidBody` to the rebuild condition at :865-867. No change for unedited content. This also fixes Failure 2.
  2. First move Desired (a private nested struct of PhysicsWorldSync at :602, declared after Impl::Entry at :307) into the anonymous namespace or into Impl, so Entry can hold it. Then replace Entry's mirrored fields with one `Desired Built` member (not `Applied`, which clashes with the existing `Placement Applied` member at :318; Desired already carries `Place`, so `entry.Applied` uses become `entry.Built.Place`). Build, Update and WriteBack (:587-596, which writes entry.Transform and the RigidBody velocities) end with whole-struct assignments. Delete Entry::Linear/Angular.
  3. Document in Physics.md that a collider without a RigidBodyComponent uses Jolt's 0.2. Physics.md states no friction default today (its bare-collider rule is at :11), so this adds a sentence rather than correcting one. Whether to use 0.5 is owner question Q1.
  4. Add one case to the existing PhysicsTests kinematic/child block (near the kinematic-child case at Tests/PhysicsTests.cpp:175-179; the AA2-GP-01 case is not written yet): add a RigidBody to a static collider and compare a sliding box with a freshly built scene. Justified because AA2-PERF-02 step 2 rewrites this sync.
- **Effort:** S.
- **Timing:** steps 1-2 and 4 in Phase 1 with AA2-GP-01 (same function); step 3 docs now, the question in the joint review.
- **Overlaps:** AA2-GP-01, AA2-PERF-02 step 2.
- **Panel discussion:** GAME argued for medium by the AA2-GP-01 precedent (latent, no shipped trigger); GP kept high. The lead decides medium for consistency with AA2-GP-01, but keeps it in the first fix batch because it is S and lands in the same function.
- **Verification:** confirmed with corrections; severity medium -> medium because the defect is latent (no shipped runtime add or remove), consistent with AA2-GP-01.

#### CQ-GP-02 EndContacts scans every active and dormant pair per removed body

- **Severity:** low (revised from medium).
- **Category:** slow, O(total).
- **Frequency:** per removed collider or mover per fixed step; per entry on a lifecycle-resync rebuild.
- **Sources:** PHYS-2.
- **Evidence:** PhysicsWorld.cpp:348-363 iterates all ActivePairs then erase_ifs Dormant for one id. Reached from DropBody (:819), Remove (:834-837), rejection paths (:860, 870, 886) and CharacterMovers.cpp:321-322. Rebuild (:464-468) removes entries one at a time. The largest shipped burst is an Arena restart: at most 14 drones and 5 cells with tens of pairs (Run.cpp:29-32; ArenaComponents.hpp:78-80); --swarm never despawns.
- **Why it matters:** estimate: 1,000 despawns against 10,000 pairs is 10M node visits, 30-100 ms. That is what debris or projectile waves would produce. Shipped content is microseconds.
- **Fix:**
  1. EndContacts(id) only pushes the id to EndedIds.
  2. In Step, after both syncs, sort and unique EndedIds and do one pass over each pair set with binary_search, emitting the same End events. O(P + k log k).
  3. Optionally clear both sets in one go in Rebuild.
  4. The existing tests PhysicsTests.cpp:122-141 guard this; re-run PhysicsBenchmark.
- **Effort:** S.
- **Timing:** as its own snippet right after the CQ-GP-05 ContactTracker extraction (which stays behavior-neutral and does not add EndedIds), at the latest with AA2-PERF-02 step 2.
- **Overlaps:** AA2-PERF-02, AA2-RT-04, CQ-GP-05.
- **Panel discussion:** GP itself moved the timing out of the amendments; GAME argued low. Accepted.

#### CQ-GP-03 PickWorld: a shape collection that is O(shapes x entries) in the worst case on every pick, a 32 ms first-click build, a broad phase never re-optimized

- **Severity:** medium.
- **Category:** slow, O(total).
- **Frequency:** per pick; per scene start (first pick); per session for broad-phase widening.
- **Sources:** PHYS-3 (absorbs PHYS-10's GetMeshCount item).
- **Evidence:**
  - PickWorld.cpp:493-498: erase_if over Shapes with none_of over Entries on every pick. none_of stops at the first entry that uses the shape, so a used shape costs only the position of its first user: about 2 comparisons per pick in the measured 50k layout (two meshes, cube and sphere alternating; GameServicesDesign.md:469-470). The cost is O(shapes x entries) only for unused shapes, which scan every entry before they are erased, or for many distinct meshes whose first users sit late in Entries: derived roughly S x E/2, for example 1,000 meshes x 25k = 25M compares, about 15-25 ms per click (estimate). M6B's multi-object FBX makes that layout plausible.
  - Measured (GameServicesDesign.md:471-477; Physics.md:125): unchanged sync 0.39 ms, which is ApplyTicks' two Finds per entry (PickWorld.cpp:225-238, already AA2-PERF-02), not the shape collection. First pick 32.1 ms Release and 225 ms Debug at 50k, which is the full Rebuild of every body plus one OptimizeBroadPhase (:482-483, :255-256); Physics.md:125 documents it as expected. The erase_if plays no part in either number.
  - PickWorld.cpp:253-256: OptimizeBroadPhase only on adds over 256; the pick system is never Updated, so moved bodies only widen nodes (degradation plausible, unmeasured).
  - PickWorld.cpp:464-476: capacity growth calls RemoveAll with Registry nulled (:469) to skip unsubscribing, and restores it (:471-473).
  - PickWorld.cpp:172 GetMeshCount: no caller (grep over RavenEngine, Games, Tests, scripts and cmake finds only the definition).
  - Pick runs per left-press (GameSession.cpp:529-551); the public API exposes only SystemContext::Picks (GameSystems.hpp:99).
- **Why it matters:** the 225 ms Debug first-pick build lands on the developer's first click after every scene start, AA2-GP-08 may raise pick frequency, and the shape collection becomes O(shapes x entries) with M6B's many-mesh content.
- **Fix (PickWorld.cpp unless noted; batch with AA2-RT-04 steps 2-3, which edit the same Pick() region):**
  1. A Users count per MeshShape, counted by entry.Mesh, not entry.Shape (failed meshes keep a MeshShape with a null Shape to suppress repeated warnings, :302-304). Maintain it wherever entry.Mesh is assigned (Update at :360 and :388) and in Remove, and reset it on the capacity-growth RemoveAll (:470), which clears Entries but keeps Shapes; otherwise counts go stale. Erase at zero; delete the erase_if.
  2. Split RemoveAll into RemoveBodies() and Unsubscribe(); remove the Registry=nullptr trick.
  3. Delete GetMeshCount.
  4. With AA2-PERF-02 step 4: add PickWorld::Prepare called from scene activation in GameSession, so the 32 ms moves into the load, but only when picking is enabled (or tied to AA2-GP-08's pick-request API); otherwise every scene start pays about 32 ms at 50k plus pick-body memory even in games that never pick. Count moved bodies and optimize past a named fraction, after measuring query time over 600 animated picks.
- **Effort:** M.
- **Timing:** steps 1-3 now, in one snippet series with AA2-RT-04 steps 2-3; step 4 with AA2-PERF-02 step 4.
- **Overlaps:** AA2-PERF-02, AA2-GP-08, AA2-RT-04, CQ-GP-04.
- **Panel discussion:** none.
- **Verification:** overstated; severity medium -> medium, but for the future many-mesh case, the documented scene-start build and AA2-GP-08's possible higher pick frequency, not for the shipped content (about 2 compares per pick today).

#### CQ-GP-04 Four hand-rolled entity-to-slot maps and three lifecycle-subscription prologues, already drifted on the scene check

- **Severity:** medium.
- **Category:** slop, duplicate.
- **Sources:** PHYS-4; endorsed by RND and DATA.
- **Evidence:** IndexOf four times (PhysicsWorld.cpp:295-298, CharacterMovers.cpp:31-34, PickWorld.cpp:61-64, RenderWorld.cpp:30-33); NoSlot four times (grep `NoSlot = 0xffffffff`: RenderWorldState.hpp:56, PhysicsWorld.cpp:304, PickWorld.cpp:79, CharacterMovers.cpp:28). Reuse reconcile four times (PhysicsWorld.cpp:475-495, CharacterMovers.cpp:430-458, PickWorld.cpp:181-208, RenderWorld.cpp:79-99). Swap-remove four times with different side effects (PhysicsWorld.cpp:834-848, std::replace on m_Changed; CharacterMovers.cpp:334-345, m_OrderDirty; PickWorld.cpp:335-347, a copy instead of a move; RenderWorld.cpp:143-160, GroupCounts, MarkDirty and a DirtyFlags resize). Subscribe/ReadLifecycle/Resync three times (PhysicsWorld.cpp:1008-1027, PickWorld.cpp:448-462, RenderWorld.cpp:289-307). Drift: RenderWorld compares a SceneId (:289); PhysicsWorld.cpp:1008 and PickWorld.cpp:448 compare raw pointers (AA2-RT-04). The callers' tick advance differs on purpose: PhysicsWorld advances Since after its own write-back (:1182), while PickWorld (:478) and RenderWorld (:309) advance it before the sync.
- **Why it matters:** every change-tracking fix (AA2-PERF-02, AA2-RT-04, CQ-GP-02) has to be made three or four times, and the copies already disagree.
- **Fix:**
  1. Private/ECS/EntitySlots.hpp: Find, Insert, SwapRemove (returns the moved slot so callers fix their own queues, dirty marks and group counts), Clear. The helper owns only SlotOfIndex plus the entity vector, so RenderWorld's split payload (parallel Objects/Entities vectors, RenderWorld.cpp:151-153) fits without forcing an Entry-struct layout on a 50k hot path.
  2. LifecycleFeed<N> in the same header, modelled on RenderWorld's copy: compares a SceneId, ORs the N Resync flags, does not own the caller's tick, noexcept Reset and destructor, and an optional read-time co-component filter (CQ-GP-07). AA2-RT-04 step 3 (timing "now") stores and compares a SceneId by hand in PhysicsWorld and PickWorld; either fold that step into LifecycleFeed, or accept that this step replaces both hand fixes. The fix plan takes the second: AA2-RT-04 step 3 lands now as written, and this step replaces it.
  3. Adopt in PhysicsWorldSync (both helpers), then PickWorld (both), then CharacterMovers (EntitySlots only: it does not subscribe to lifecycle itself, but receives merged entries from PhysicsWorld::Step, :1044-1048). One file each.
  4. RenderWorld adopts it in its B4 sync rewrite, so RenderWorld.cpp is edited once. The B4 interpolation override (CQ-DATA-05) is a further consumer.
- **Effort:** M.
- **Timing:** steps 1-3 with AA2-PERF-02 step 2; step 4 during B4.
- **Overlaps:** AA2-RT-04 (step 3), AA2-PERF-02, AA2-PERF-09, CQ-DATA-05.
- **Panel discussion:** RND specified the interface needs from RenderWorld.
- **Verification:** confirmed with corrections; severity medium -> medium because the copies already diverged and the AA2-PERF-02 and AA2-RT-04 fixes would otherwise be made three or four times; RenderWorld adds a fourth copy of the reconcile and swap-remove patterns.

#### CQ-GP-05 PhysicsWorld::Step is 189 lines with eight phases; the FD-48 contact bookkeeping is split between Impl and Step

- **Severity:** medium.
- **Category:** readability, long function.
- **Sources:** PHYS-5.
- **Evidence:** PhysicsWorld.cpp:1002-1190: scene check and subscribe (1007-1020) with batch reads (1022-1027), body and mover sync (1028-1050), debug toggles (1052-1057 and 1062-1064), the simulation call (collisionSteps and System.Update, 1058-1061), errors (1065-1077), contact publication (1079-1174, 96 lines), write-back (1176-1180), the Since advance (1181-1182, which must follow both write-backs so physics' own writes are not read as edits), stats (1183-1189). Contact state lives in Impl (ActivePairs, Dormant and PendingEnds at 428-433; EndContacts at 348-363), but its begin, end and dormant rules are inline in Step (1089-1173). The 0.05 m dormant expansion at 1160 is unnamed. Other long functions: Update 852-951 (100), Build 709-792 (84), MakeShape 114-199 (86), Evaluate 619-696 (78). The :457 comment describes Rebuild but sits above GetMoverPools (:458). PhysicsWorldSync is constructed twice per step (1030, 1177); that costs only four GetPool calls per step, a readability point only.
- **Why it matters:** the contact rules are the subtlest determinism code in M6C, and CQ-GP-02, AA2-PERF-02 step 2, AA2-GP-01 and AA2-RT-04 step 1 all land in or around Step.
- **Fix (PhysicsWorld.cpp; a behavior-neutral extraction):**
  1. Extract a ContactTracker class (ActivePairs, Dormant, PendingEnds; End, Publish, Clear) and name DormantSeparation. Keep a callable End(id) for CharacterMovers, which ends contacts through Links.EndContacts (PhysicsWorld.cpp:365-368; CharacterMovers.cpp:321-322), and keep RemoveAll's special case of nulling EndContacts (:373-375). EndedIds belongs to CQ-GP-02's deferred-end design and is added in that step, not in this extraction.
  2. Split SyncBodies, ReportStepErrors and UpdateStats out of Step, keeping the Since advance as an explicit step with its comment; Step becomes about 50 lines.
  3. Move the :457 comment to Rebuild.
  4. Optional, owner-approved split only: move ContactTracker to Private/Physics/PhysicsContacts.cpp/.hpp.
- **Effort:** M.
- **Timing:** before AA2-PERF-02 step 2, after CQ-GP-01 step 1. Order with AA2-RT-04 step 1 (wrap Step's body in try/catch, timing "now"): wrapping the 189-line body first and splitting it later re-indents and then moves the same lines, so either split first or have AA2-RT-04 step 1 wrap the short post-split body. The fix plan lands AA2-RT-04 step 1 as written now and this split after it.
- **Overlaps:** AA2-PERF-02, AA2-GP-01, AA2-RT-04 step 1, CQ-GP-02.
- **Panel discussion:** none.
- **Verification:** confirmed with corrections; severity medium -> medium because the contact code is the subtle FD-48 determinism logic and several fixes land in or around Step.

#### CQ-GP-06 Shape, gravity, motion and reporting logic duplicated between PhysicsWorld and CharacterMovers; only body-limit reporting disagrees

- **Severity:** low (revised from medium in verification).
- **Category:** slop, duplicate.
- **Sources:** PHYS-6.
- **Evidence:**
  - The capsule is built in PhysicsWorld.cpp:154-176, 182-197 with error handling, where the pi/2 rotation and the collider offset are combined into one RotatedTranslatedShape (:172, :188-190), and in CharacterMovers.cpp:36-45 with `new JPH::CapsuleShape` and `.Create().Get()` with no error check. That cannot fail in practice: CharacterMovers::Evaluate (:275-277) rejects a non-positive or non-finite Radius and a negative or non-finite HalfHeight first. Both rotate pi/2 about X and fall back to a sphere when the half height is not positive.
  - Gravity: `System.SetGravity(0,0,-9.81f)` at PhysicsWorld.cpp:337 and `constexpr float Gravity = 9.81f` at CharacterMovers.cpp:29, used at :485 (grep SetGravity|GetGravity|9.81 over RavenEngine, Games and Tests: only these two). They agree.
  - Internal motion is derived twice (`internalMotion != Static` at :738-744, `motion != Static || Sensor` at :903-904); the two are logically equivalent. Offset scaling is byte-identical at :720-721 and :881-882. MakeShape has a third caller, MakeQueryShape (:1238-1244), which passes offset {}.
  - Two Report functions (PhysicsWorld.cpp:698-706, CharacterMovers.cpp:287-295) differ only in "Collider" versus "Mover". Both "back in the physics world" logs (PhysicsWorld.cpp:775-776, CharacterMovers.cpp:384-385) call GetAuthoredId with no hex-ID fallback, so an unnamed runtime entity logs "on ''" (Scene.cpp:267-272): a shared defect, not a disagreement.
  - The one real disagreement: three body-limit wordings (PhysicsWorld.cpp:731-732, :770; CharacterMovers.cpp:379) and two behaviors, once per world through ReportedLimit (PhysicsWorld.cpp:728-734, 767-772) versus once per mover through entry.Problem (CharacterMovers.cpp:377-380), against docs/Physics.md:57, which says "logged once".
- **Why it matters:** each new shape kind must be added twice, and a later edit to one gravity or capsule copy would drift silently. The copies are 2-10 lines each in one folder, on no hot path, and have caused no bug.
- **Fix:**
  1. A shared capsule helper in JoltSupport.hpp/.cpp that returns the inner capsule or sphere plus the pi/2 rotation (or takes the offset), so PhysicsWorld keeps one RotatedTranslatedShape for rotation and offset together. A helper that returns an already-rotated shape would make PhysicsWorld wrap a second RotatedTranslatedShape around every offset capsule. Passing errors back adds little.
  2. CharacterMovers uses m_System.GetGravity(); delete the constant.
  3. An InternalMotion(const Desired&) helper; MakeShape computes the offset itself (verify first that a default ColliderComponent's Offset is zero, so MakeQueryShape's `{}` is unchanged).
  4. ReportLeftOut/ReportBack helpers in Private/Physics, outside JoltSupport (which PickWorld shares and which holds only Jolt and math helpers today; it should not gain Scene and Log dependencies), taking the display name or ID as a parameter, with one hex-ID fallback; one body-limit text, per entity (a delegated technical choice: players and designers do not see it). Update Physics.md:57 in the same snippet.
- **Effort:** S.
- **Timing:** steps 1-3 now; step 4 in Phase 1.
- **Overlaps:** CQ-X-09.
- **Panel discussion:** none.
- **Verification:** confirmed with corrections; severity medium -> low because the capsule, gravity, offset and motion copies agree today and only the body-limit reporting differs.

#### CQ-GP-07 Per-step allocation and sort churn; every TransformComponent spawn is routed into the mover sync

- **Severity:** low.
- **Category:** slow, hot-path allocation.
- **Frequency:** per fixed step; proportional to spawns of any entity with a transform.
- **Sources:** PHYS-7; DATA supplied the filter.
- **Evidence:** PhysicsWorld.cpp:1044-1046 copies the lifecycle entries of four journals, including Transform (subscribed at :1017), each step. CharacterMovers.cpp:403-414 copies them again; :425-428 sorts with two GetRef calls per comparison before :440-447 discards non-movers. Fresh vectors per step at :1082, :1138 (move discards m_Contacts' buffer), :797 (the AddPending batch vector on every Commit), CharacterMovers.cpp:93 and :403 (the visit vector). PhysicsWorld.cpp:534-537 (Commit) sorts with two GetRef calls per comparison too, mirroring CharacterMovers.cpp:425-428. Per Overlap query, not per step: PhysicsWorld.cpp:1329 (the `ids` vector). count_if and GetCount twice per step (:1184-1186; CharacterMovers.cpp:244-253). AA2-PERF-09 lists only :962-963 and :1030.
- **Fix:**
  1. Filter the Transform batch at read time: keep an entry only if the entity is alive and in the Collider or Mover PoolView (an inline paged Find, a few ns). Destroys already arrive through the Collider and Mover journals (Registry.cpp:384-386). No journal API change; keep the subscription.
  2. Build (Id, Entity) pairs once and sort by Id, in both sorts (CharacterMovers.cpp:425-428 and PhysicsWorld.cpp:534-537).
  3. Keep the vectors as members; replace the move at :1138 with assign plus clear.
  4. Maintain the Rejected and Movers counts incrementally.
- **Effort:** S.
- **Timing:** with AA2-PERF-09, opportunistic now, and AA2-PERF-02 step 2.
- **Overlaps:** AA2-PERF-09, AA2-PERF-02, CQ-GP-04, CQ-X-06.
- **Panel discussion:** DATA explained why the journal itself should not filter (cost on the 100k spawn path).

#### CQ-GP-08 A lock per contact callback and one queue lock per Jolt job

- **Severity:** low.
- **Category:** slow, hot-path lock.
- **Frequency:** per contact callback (Jolt workers); per Jolt job per step.
- **Sources:** PHYS-8; CORE answered the range-submit question.
- **Evidence:** PhysicsWorld.cpp:217-241 takes m_Mutex per push_back on Jolt workers. JoltJobs.cpp:48-64: QueueJobs loops QueueJob, one Submit lock each (Submit at JobSystem.cpp:50-63, which calls notify_one only when a worker sleeps, :58-62). SubmitRange (JobSystem.cpp:65-81; private, JobSystem.hpp:93) already pushes N items under one lock, Frame priority only, with one shared context and an index. Shared-pool overhead measured at 0.05-0.15 ms per step (GameServicesDesign.md:632-640). Callback counts are not recorded.
- **Fix:**
  1. Measure first: add the raw callback count to PhysicsStats, record it for the 1,000 and 4,096 benchmark cases.
  2. Only past about 1k callbacks per step: per-thread buffers concatenated in Take() (Publish sorts anyway).
  3. With AA2-PERF-05: generalize SubmitRange to per-item contexts and any priority (each Item already carries a context, JobSystem.hpp:87-92), pushing N items under one lock and notifying min(count, sleeping), so the job system does not gain a third submit entry point. JoltJobs then needs no pooled buffer.
- **Effort:** S.
- **Timing:** step 1 now; steps 2-3 later.
- **Overlaps:** AA2-PERF-05, AA2-RT-04.
- **Panel discussion:** none.

#### CQ-GP-09 InputRouter::Press folds key listening into the context loop through a sentinel index

- **Severity:** low.
- **Category:** readability, mixed abstraction.
- **Sources:** PHYS-9.
- **Evidence:** InputRouter.cpp:186-256 (71 lines) loops to `index <= m_Contexts.size()` with a listenChecked flag (191-216). :203-208 masks with `~own` where own is None (a no-op). Find-action-by-name four times (:366-368, 413-414, 432-433, 471-473). :271's bounds check cannot fail because every change calls ClearRoutes (183, 421, 437, 461; 136-141).
- **Fix (InputRouter.cpp):** three explicit steps (engine contexts, listening, game contexts) through one RouteTo helper; write the chord directly at :208; a private FindAction; replace :271 with a comment. InputMappingTests (222 lines) guards the order.
- **Effort:** S.
- **Timing:** with AA2-GP-10.
- **Overlaps:** AA2-GP-10.
- **Panel discussion:** none.

#### CQ-GP-10 Dead and redundant items in physics

- **Severity:** low.
- **Category:** slop, dead code.
- **Sources:** PHYS-10.
- **Evidence:** PhysicsStats::StepNanoseconds (PhysicsWorld.hpp:32-33) written at PhysicsWorld.cpp:1006, 1188-1189 and never read (grep). `#include <thread>` at PhysicsWorld.cpp:42 with no use. The misplaced comment and redundant Entry velocities are in CQ-GP-05 and CQ-GP-01; GetMeshCount is in CQ-GP-03.
- **Fix:** feed StepNanoseconds into the AA2-PERF-06 trace column through CQ-CORE-04's table, or delete it with its two clock reads; delete the include.
- **Effort:** S.
- **Timing:** now (column with AA2-PERF-06).
- **Overlaps:** AA2-PERF-06, CQ-CORE-04.
- **Panel discussion:** none.

#### CQ-GP-11 Input stream records: a run-time check of a compile-time fact, one heap vector per record, and an O(all records) scan per consumer

- **Severity:** low.
- **Category:** slop, run-time check of a compile-time fact.
- **Sources:** PHYS-11, PHYS-13 (new), HOST-9 item 3.
- **Evidence:** InputStream.hpp:35-43: Emit loops over ComponentTraits<T>::Info.Fields per call and throws on an Entity field, though Info is constexpr (Component.hpp:382). A throw from a Control system suspends it, later than a compile error. FixedGameplayStep.hpp:134-146: each Stored owns a std::vector<std::byte>. InputStream.hpp:46-59: ForEach calls the virtual GetRecordCount in the loop condition and GetRecord per record, once per consumer type. Today Arena emits one record per frame (Craft.cpp:30); RTS bursts of tens to a few hundred per tick are plausible (derived).
- **Fix:**
  1. Phase 1 † commit with AA2-GP-04 step 2: `static_assert(!HasEntityField(ComponentTraits<T>::Info), ...)` via a consteval helper; read GetRecordCount once before the loop.
  2. With AA2-GP-02 step 3 (the per-tick list rebuild): one byte arena plus headers per queue, swapped as today; the InputState and action snapshot stay in a typed sidecar (they hold vectors). Bucket headers by Type at BeginStep and add `GetRecords(type)`. The borrowed Record::Data contract is unaffected (ForEach memcpys; Push touches only m_Pending).
- **Effort:** S.
- **Timing:** step 1 Phase 1 †; step 2 with AA2-GP-02 step 3.
- **Overlaps:** AA2-GP-04, AA2-GP-02.
- **Panel discussion:** GP owns the arena while FD-47 is provisional.

#### CQ-GP-12 Unnamed tuning and capacity constants, including designer-visible mover feel and a grid margin tied to the step rate

- **Severity:** low.
- **Category:** readability, magic number.
- **Sources:** PHYS-12; GAME amended step 2.
- **Evidence:** CharacterMovers.cpp:489 (stair step-up 0.4 m), :514 (stick-to-floor 0.5 m), :502 (0.1 m/s approach), unnamed. :80-81 Margin = 1.0 m per step: 60 m/s at 60 Hz, halving at 30 Hz. :92-103: cell size is the largest character's extent. :497-514 (the air branch, gravity added at :511) keeps vertical speed in the air with no clamp, so a fall passes 60 m/s after about 6.1 s (derived). PhysicsWorld.cpp:332 (16 MiB), :970 (8 workers), :1160 (0.05 m); PickWorld.cpp:115, 255. Counter-example: PhysicsWorld.cpp:1058-1059 states its rule beside the constant.
- **Fix:**
  1. A named-constant block in CharacterMovers.cpp (StairStepUp, StickToFloorDepth, GroundApproachSpeed) with one-line reasons.
  2. Clamp downward speed to a named TerminalSpeed (for example 50 m/s) and set the grid margin to TerminalSpeed x step seconds; cap cell size at a named multiple of the median extent, with oversized characters in a separate list. This is an independent engine improvement related to CQ-GAME-11 (it does not fix the soft-lock and does not wait for owner question Q2). It changes fall feel in every game only after about 5 s of free fall (derived: 50/9.81), so it is recorded as a delegated technical choice.
  3. Name the PhysicsWorld and PickWorld constants with reasons.
- **Effort:** S.
- **Timing:** steps 1-3 now (TerminalSpeed can land independently in CharacterMovers.cpp); the cell-size cap at the crowd milestone with AA2-GP-07.
- **Overlaps:** AA2-GP-07, AA2-GP-09, CQ-GAME-11, CQ-X-07.
- **Panel discussion:** whether StairStepUp becomes a component field is scope; ask only when a game needs another value.

#### CQ-GP-13 JoltSupport's Decompose is noexcept but calls the throwing Normalize, so a degenerate transform calls std::terminate

- **Severity:** medium (plausible; not reproduced).
- **Category:** correctness, error tier (FD-06).
- **Frequency:** per child collider decomposition (per fixed step on change) and per pick body build.
- **Sources:** citation checker (new defect found while checking CQ-GP and CQ-API-10 sites).
- **Evidence:**
  - JoltSupport.hpp:72-91: `inline bool Decompose(const Mat4& world, Placement& result) noexcept`. Its length checks (:79-81) reject a zero or non-finite axis, but :83 then calls `Normalize(axes[1] - x * Dot(x, axes[1]))`.
  - Transform.hpp:53-59: Normalize throws std::invalid_argument when the squared length is at most 1e-12 or non-finite.
  - An X axis (nearly) parallel to the Y axis passes the length checks and reaches the throw, for example a rotated child under a nearly flat, non-uniformly scaled parent. A throw inside a noexcept function calls std::terminate.
  - Callers: PhysicsWorld.cpp:679 (child colliders) and PickWorld.cpp:356 (every pick body). The comment at JoltSupport.hpp:70-71 says the function returns false for a zero or non-finite axis, so the reject path already exists.
- **Why it matters:** a degenerate content transform ends the process instead of being rejected with a logged reason, which breaks FD-06 ("Bad data never ends a session") and bypasses every stage boundary.
- **Fix:** JoltSupport.hpp only: replace the Normalize at :83 with an explicit squared-length check that returns false (the existing reject path), using CQ-API-10's shared MinNormalizeLengthSquared once it exists (until then, the same 1e-12 literal with a comment). No tracked test; check once with an ignored probe matrix whose X and Y columns are parallel.
- **Effort:** S.
- **Timing:** now (Phase 1 private item).
- **Overlaps:** CQ-API-10, AA2-RT-01.
- **Panel discussion:** not discussed by the panel; found by a citation checker.
- **Verification:** plausible, not reproduced (citation checker, new); severity medium (new) because a definite noexcept/throw mismatch turns bad data into process termination, though the trigger needs a degenerate transform.

### 5.6 MEDIA: audio, text and UI

#### CQ-MEDIA-01 UI::Edges names Left/Right but the engine applies them as Start/End; the image path takes the names literally and also ignores and overpaints the border

- **Severity:** medium.
- **Category:** readability, naming.
- **Sources:** MEDIA-1; verified against Yoga semantics by API.
- **Evidence:** Ui.hpp:44-50 `Edges { Left, Top, Right, Bottom }`, next to Style's Start/End offsets (Ui.hpp:76-79, comment at :73-74). UiContext.cpp:86-93 maps Padding.Left to YGEdgeStart and Right to YGEdgeEnd; :94 sets the border with YGEdgeAll; :428 lays out with YGDirectionRTL when right-to-left; :434-437 read physical positions. The text path swaps (:504) and subtracts the border inset (:503-507). The image path (:487-490) uses Padding.Left unswapped and ignores BorderWidth. Result for Padding {Left=20, Right=0} in RTL: Yoga reserves 20 px on the right, the image covers [X+20, X+W] and leaves a 20 px gap on the left. The image quad is also drawn after the bordered box quad (:483, then :486-497), so with BorderWidth > 0 a bordered image hides its own border, even in LTR. The bug is latent in shipped content: grep for "Image" over Games/ finds no Ui::Image call (the only other Image implementation is the null Ui in GameSession.cpp:279), and every game Padding uses All() or Symmetric() (ArenaUi.cpp:30, 58, 74, 90, 148, 155, 198); Margin is never set.
- **Why it matters:** RTL is a stated goal (the Arabic font, the Ctrl+F6 direction switch), the public names mislead every future widget, and the text and image paths have already drifted. No visible screen shows the bug today.
- **Fix:**
  1. Now, UiContext.cpp: ContentRect(const Element&) applying the RTL swap and the border inset, used for the image quad and the label.
  2. Phase 3: rename Edges to {Start, Top, End, Bottom}; All and Symmetric unchanged, so no game edits (verified: games use only All and Symmetric). Update the Style comment. Land it in the same Ui.hpp change as AA2-MEDIA-06's UiText API, so the public UI contract breaks once, not twice.
  3. No tracked test. Check with a temporary, ignored preview entry that draws an image with asymmetric padding and a border, under F6 and Ctrl+F6, because no preview entry or game screen uses Ui::Image.
- **Effort:** S.
- **Timing:** step 1 now (Phase 1); step 2 Phase 3 with AA2-MEDIA-06.
- **Overlaps:** AA2-MEDIA-05/06 (same header window).
- **Panel discussion:** API added the border detail.
- **Verification:** confirmed with corrections; severity medium -> medium because RTL is a stated goal and the public names actively mislead, although the bug is latent in shipped content.

#### CQ-MEDIA-02 The UI rebuilds its frame from fresh allocations, and a throw during layout leaks the Yoga tree every frame

- **Severity:** low (revised from medium in verification).
- **Category:** slow, hot-path allocation.
- **Frequency:** per frame; per element per frame; per Yoga measure call.
- **Sources:** MEDIA-2; RND and VK confirmed the GPU side is not the larger cost.
- **Evidence:** UiContext.cpp:401-402, 440 create and free a YGConfig per frame; :403-404 allocate two vectors; :411 allocates a node per element, freed with YGNodeFreeRecursive at :439, with no RAII. Shown() returns a new string (:188-191) and Add copies it again (:172); m_Elements.clear() frees them (:265). New nodes each frame defeat Yoga's measure cache, so every measure calls TextSystem::Layout (:132-146; TextSystem.cpp:679-694), which re-hashes the whole string and does an unordered_map find. TextSystem::Shape throws on non-UTF-8 text (TextSystem.cpp:274) and on bidi failure (:296); the throw passes through MeasureText and YGNodeCalculateLayout (:428), skips :439-440, and so leaks a whole Yoga tree plus its config every frame while that text is shown. Measured (GameServicesDesign.md:624-630): the HUD and pause menu, which the 0.3 ms budget covers (UIDesign.md:233; GameServicesDesign.md:630), are 0.117 and 0.167 ms p95; the key-binding screen (196 quads), which no stated budget covers, is 0.175 ms mean and 0.288 ms p95, about 0.89 us per quad (derived) for Yoga layout, text lookups and glyph emission together. The overlay upload is one 112 B memcpy per quad per frame (VulkanClearPass.cpp:305; Overlay.hpp:44), about 10-40 us at 2,000 quads (estimate).
- **Why it matters:** the leak on a bad string is the most valuable item. The allocation share of the measured time is unmeasured: derived, about 100-200 small allocations at 50-100 ns each is 10-30 us of the 175 us (estimate). The 2,000-quad case (about 1.8 ms, derived) is editor scale, which the code comment at :399-400 and GameServicesDesign.md:615 already defer to retained per-ID nodes and worker layout.
- **Fix (UiContext.hpp/.cpp):**
  1. Measure the key-binding screen first, so steps 2-4 are justified by a number; record it in GameServicesDesign.md measurement 7.
  2. RAII guard only: own the root with a unique_ptr whose deleter calls YGNodeFreeRecursive, and hold one YGConfig for the context lifetime (set the point scale only when it changes). This fixes the leak without retaining nodes. Do not reuse nodes across frames by index: a reused leaf keeps its measure cache unless marked dirty, so a leaf whose text changed would return a stale measurement unless every content change calls YGNodeMarkDirty; dirtying every leaf removes the gain, and index-keyed reuse is a half-step that the documented per-ID retained nodes replace (no-throwaway).
  3. Smaller than a text arena: an Add overload taking std::string&& removes the second copy of Shown()'s result (:172); short labels already fit in the small-string buffer.
  4. AA2-MEDIA-08's Prepare is timed during M6B in AA2; trial it there (fed only texts that changed since the previous frame), not now. If the gain on F6/Ctrl+F6/Alt+F6 switch frames is within noise, delete Prepare and correct the docs that describe it.
- **Effort:** S.
- **Timing:** steps 1-3 now; step 4 with AA2-MEDIA-08 during M6B.
- **Overlaps:** AA2-MEDIA-08, AA2-MEDIA-09, CQ-MEDIA-03.
- **Panel discussion:** retained Yoga nodes stay with the editor-sized tree work, as planned.
- **Verification:** overstated; severity medium -> low because the measured game screens are within their budget, the allocation share is a small estimated fraction, and the editor-scale case is already deferred; the per-frame leak on a throw is the item worth fixing now.

#### CQ-MEDIA-03 AppendQuads resolves every glyph through two hash lookups every frame

- **Severity:** low (revised from medium).
- **Category:** slow, hot-path lookup.
- **Frequency:** per visible glyph per frame.
- **Sources:** MEDIA-3.
- **Evidence:** TextSystem.cpp:701-706 calls Request (try_emplace, :567-578) per glyph to read Status; :711-713 looks up the same key again with operator[]; the key formula is written at :569 and :713. Glyphs are never evicted today, so the readiness pass is redundant after a block is ready.
- **Why it matters:** about 10 us of the measured 0.12-0.29 ms UI time today (estimate); larger only at editor scale.
- **Fix (TextSystem.cpp):** a constexpr GlyphKey helper; keep the entry pointers from the first pass in a reused vector (unordered_map nodes are stable); a ReadyEpoch per cached block lands with AA2-MEDIA-03's eviction.
- **Effort:** S.
- **Timing:** steps 1-2 opportunistically with CQ-MEDIA-08; the epoch with AA2-MEDIA-03.
- **Overlaps:** AA2-MEDIA-03.
- **Panel discussion:** MEDIA lowered its own finding.

#### CQ-MEDIA-04 Sound-handle operations and Play are linear scans over every live sound

- **Severity:** low (revised from medium).
- **Category:** slow, O(total).
- **Frequency:** per handle call; per Play.
- **Sources:** MEDIA-4.
- **Evidence:** AudioVoices.cpp:303-311 Find scans m_Sounds for every handle API (:142-182). Play (:81-140) runs Collect (O(releases x n), :438-454), a group scan, count_if and EraseStopped twice. Up to 576 entries: MaxSounds 512 (AudioVoices.hpp:27) plus AudioMixer::MaxVoices 64 (AudioMixer.hpp:65), the size used at AudioVoices.cpp:72 (reserve) and :236 (ranked array). grep `SetSound|IsSoundPlaying|StopSound` in Games: 0 hits; all 8 sound calls are PlaySound.
- **Why it matters:** derived: at 512 attached moving sounds, about 131k compares per frame, 0.3-1 ms (estimate). A per-drone hum would be 14 (121 in --swarm), microseconds.
- **Fix:** with the C8 streaming rewrite of AudioVoices, after AA2-MEDIA-01's test lands: a fixed slot array with the slot in the handle's low 10 bits and the random serial above; a free list; a live count; per-group live counts. Re-raise earlier only when a system updates more than about 50 attached sounds per frame.
- **Effort:** M.
- **Timing:** with C8 streaming.
- **Overlaps:** AA2-MEDIA-01, AA2-MEDIA-07.
- **Panel discussion:** GP, GAME and the lead agreed on low. Related, from CORE: AudioPlayer::Update should take the frame clock's real delta (CQ-CORE-11 step 2).

#### CQ-MEDIA-05 SoundParams limits, bus names, bus count and the ducking rule are literals duplicated across files

- **Severity:** low (revised from medium in verification).
- **Category:** slop, duplicate.
- **Sources:** MEDIA-5.
- **Evidence:** volume 0..4 at AudioVoices.cpp:39, 168 and SoundEvent.cpp:59-60 (Sound.hpp:53 documents it); pitch 0.25..4 at AudioVoices.cpp:40, 174 and SoundEvent.cpp:64-65 (Sound.hpp:55); distances, priority and fade at AudioVoices.cpp:44-47, 53 vs SoundEvent.cpp:80, 103-110; the concurrency cap 512 at SoundEvent.cpp:85-86 equals AudioVoices::MaxSounds (AudioVoices.hpp:27). AudioVoices.cpp:187 is a separate bus-volume clamp (SetBusVolume) that Sound.hpp does not document. Buses: the enum (Sound.hpp:13-21), a separate AudioBusCount = 6 (:22) and the name table at SoundEvent.cpp:71, which depends on enum order with no static_assert tying them; an out-of-range bus is remapped to Effects (AudioVoices.cpp:37-38). Ducking is tested twice (:324-325 inside BusGain :318-327; :475-476 inside UpdateBuses :471-485). One small drift exists and is harmless: the parser accepts MaxDistance == MinDistance (ReadFloat's low bound is inclusive, SoundEvent.cpp:103-105) despite its "beyond the minimum" message, and Sanitize then forces MinDistance x 1.001 (AudioVoices.cpp:45). All AudioBusCount users are private (grep: AudioMixer.hpp:153-154, AudioMixer.cpp:41, 53, 135, AudioVoices.hpp:108-109, AudioVoices.cpp:37, 186, 192, 471; no Games or Tests hits).
- **Why it matters:** these are designer-visible tuning limits. Raising max pitch in one place makes .ravensound files fail while code calls work. A seventh bus with AudioBusCount not bumped plays on Effects; with the name table not extended, the parser throws "unknown bus". The worse silent case is reordering the enum, which remaps every .ravensound bus name with no error. All of these are hypothetical today.
- **Fix:**
  1. Named limits and AudioBusNames with a static_assert against the enum. Consider public constexpr limits in Sound.hpp next to SoundParams (Phase 3, contract-hashed), since Sound.hpp already documents them to game authors in comments and game sliders could share them; otherwise a private Private/Audio/SoundLimits.hpp. The bus-volume clamp gets its own named constant. Either way one or two files per step.
  2. AudioVoices.cpp uses them; one IsDucked(AudioBus).
  3. SoundEvent.cpp uses them, with messages built from the constants (and the distance message corrected to "at least the minimum", or the bound made exclusive).
  4. Phase 3: derive AudioBusCount from the enum in Sound.hpp.
- **Effort:** S.
- **Timing:** steps 1-3 sequenced with AA2-MEDIA-01's change to AudioVoices (C8 streaming, next) and AA2-MEDIA-07's SetPaused bus mask, so AudioVoices.cpp is not edited three times in a row (Sanitize and BusGain survive both, so this is not throwaway); step 4 Phase 3.
- **Overlaps:** AA2-MEDIA-01, AA2-MEDIA-07, CQ-X-01.
- **Panel discussion:** none.
- **Verification:** confirmed with corrections; severity medium -> low because the limits have not drifted in a way that matters, there is no seventh bus, and the failure modes are a parse error or a remap, not corruption; the static_assert covers the one order-dependent table.

#### CQ-MEDIA-06 Dead members and two stale public comments

- **Severity:** low.
- **Category:** slop, dead code.
- **Sources:** MEDIA-6 (Font::Name kept; it gains a reader in CQ-MEDIA-12).
- **Evidence:** grep shows no caller for TextSystem::Stats/GetStats (TextSystem.hpp:95-103), TextBlock::Lines (written once at TextSystem.cpp:468), UiContext::GetElementCount and IsPseudoLocalized (UiContext.hpp:43, 56), and Entry::Interactive (written at UiContext.cpp:546-547; m_Order already holds the interactive IDs). AudioOutput's destructor (AudioOutput.cpp:23-28) repeats what jthread does, with a redundant notify_all. Stale: Sound.hpp:49 "later (decision A4)" (already done); AudioPlayer.hpp:25 describes the dead Play(AssetRef). Keep CommandSpace/FreeSpace and GetCounts: AA2-MEDIA-01 calls them.
- **Fix:** delete the dead items (TextSystem, UiContext); simplify or comment the AudioOutput destructor; fix Sound.hpp:49 in the Phase 1 † commit; fix AudioPlayer.hpp:25 with AA2-CORE-04 step 2.
- **Effort:** S.
- **Timing:** now; comments as noted.
- **Overlaps:** AA2-CORE-04, AA2-MEDIA-01, AA2-MEDIA-08.
- **Panel discussion:** none.

#### CQ-MEDIA-07 Text alignment is implemented twice, with WrapWidth doubling as a "wrap enabled" flag

- **Severity:** low.
- **Category:** readability, duplicate.
- **Sources:** MEDIA-7.
- **Evidence:** TextSystem.cpp:471-485 and UiContext.cpp:523-532 both apply `(Align == End) != rtl`. UiContext.cpp:222-223 maps two identical enums with a nested ternary. UiContext.cpp:419 passes 1.0f to mean "wrap on", decoded at :136-139; TextSystem.hpp:40-41 documents WrapWidth only as a width.
- **Fix:** add BoxWidth to TextStyle and align once in LayoutShaped; delete the UiContext copy; a Wrap bool in MeasureContext; static_assert the enumerators match and static_cast.
- **Effort:** S.
- **Timing:** now, or with AA2-MEDIA-05 step 2 (same struct).
- **Overlaps:** AA2-MEDIA-05.
- **Panel discussion:** none.

#### CQ-MEDIA-08 TextSystem.cpp is one 784-line Impl doing five jobs; UiContext duplicates its hit test

- **Severity:** low.
- **Category:** readability, long function.
- **Sources:** MEDIA-8.
- **Evidence:** TextSystem.cpp:171-637 covers font loading (63 lines), fallback, Shape (262-372, 111 lines), LayoutShaped (384-487, 104 lines; five vectors per call; re-sums advances after each break, :420-421), Generate (498-565), Place (580-618) and job plumbing. One-line local structs (:300, :427, :436) and an enum declared with a member (:134). UiContext::Draw (443-538, 96 lines) and BeginFrame (262-331, 70 lines); the hit test is written twice (:245-255, :280-286) and the copies diverged (AA2-MEDIA-04).
- **Fix:**
  1. Now, UiContext.cpp: one HitTest(x, y, order) used in both places (apply AA2-MEDIA-04 step 1 there); DrawBox/DrawImage/DrawLabel with CQ-MEDIA-01 step 1.
  2. Now, TextSystem.cpp: split LayoutShaped into BreakLines, PlaceLine and AlignLines; house-style braces.
  3. With AA2-MEDIA-03: move glyph atlas code to Private/Text/GlyphAtlas.cpp/.hpp.
  4. With AA2-MEDIA-05: move font loading to Private/Text/FontSet.cpp/.hpp.
- **Effort:** M.
- **Timing:** steps 1-2 now; 3-4 with the named steps.
- **Overlaps:** AA2-MEDIA-03, -04, -05.
- **Panel discussion:** none.

#### CQ-MEDIA-09 Mixer names collide: RampLeft means frames left, the stereo stride is hard-coded, BusGain reuses GainLeft

- **Severity:** low.
- **Category:** readability, naming.
- **Sources:** MEDIA-9.
- **Evidence:** AudioMixer.hpp:125-129 GainLeft/TargetLeft beside RampLeft; AudioMixer.cpp:156-160. `frame * 2` at :130-131, 177-178 beside MixerChannels at :24-26, 164, 172, with no static_assert. AudioMixer.hpp:81 and AudioVoices.cpp:482 overload GainLeft for bus gain. Two divides per frame per ramping voice (:156-159; about 6.1M/s worst case, derived, under 1% of a core).
- **Fix:** rename to RampFramesLeft and static_assert MixerChannels == 2 (or index by it); a BusGain field on Command; leave the ramp precompute to the C8 streaming rewrite.
- **Effort:** S.
- **Timing:** now; the loop restructure with C8 streaming.
- **Overlaps:** AA2-MEDIA-01.
- **Panel discussion:** none.

#### CQ-MEDIA-10 Audio decode loops and error texts are duplicated

- **Severity:** low.
- **Category:** slop, duplicate.
- **Sources:** MEDIA-11.
- **Evidence:** AudioClip.cpp:89-105 and :139-157 repeat the chunked decode loop and the 300-second check; the same "streaming, which is not built yet" message appears at :102-103 and :152-153; "audio clip is empty" three times (:66, 109, 162).
- **Why it matters:** AA2-MEDIA-02 must add a decoded-size bound to both loops; one could be missed.
- **Fix (AudioClip.cpp):** GrowDecoded(...) applying the budget and the one length message, used by both loops; a [[noreturn]] ThrowAudio helper. Drop "which is not built yet" when streaming lands.
- **Effort:** S.
- **Timing:** with AA2-MEDIA-02 (Phase 1).
- **Overlaps:** AA2-MEDIA-02.
- **Panel discussion:** none.

#### CQ-MEDIA-11 Unnamed magic numbers in text and UI layout

- **Severity:** low.
- **Category:** readability, magic number.
- **Sources:** MEDIA-12.
- **Evidence:** TextSystem.cpp:178, 192 (64 MiB twice), :525 (512 px glyph box, marks Failed), :766 (`Frame % 60`), :205 (8 variation axes). UiContext.cpp:144 (`* 1.2f` minimum line height), :473-480 (hover 0.12, press -0.15, focus ring white 0.9 alpha, at least 2 px).
- **Fix:** name MaxEngineFileBytes (or use CQ-DATA-09's constant), MaxGlyphBox, EvictEveryFrames, MaxVariationAxes in TextSystem.cpp; MinLineHeight, HoverShade, PressShade, FocusRingColor, FocusRingWidth in UiContext.cpp. They become the default style's values when style assets land.
- **Effort:** S.
- **Timing:** now, with CQ-BLD-06 (TextSystem.cpp) and CQ-MEDIA-02 (UiContext.cpp).
- **Overlaps:** CQ-DATA-09, CQ-BLD-06, CQ-X-07.
- **Panel discussion:** none.

#### CQ-MEDIA-12 Glyph failures are silent, and a throwing Submit leaves the glyph counter raised, so a fatal error hangs at exit

- **Severity:** low.
- **Category:** readability, silent failure.
- **Sources:** MEDIA-13 (new in round 2).
- **Evidence:** TextSystem.cpp:554-557 catch(...) sets Failed with no reason; :525-526 marks oversize glyphs Failed silently; :740-748 and :713-715 drop Failed glyphs with no log, so characters vanish. :567-578 Request increments Outstanding before Workers.Submit, which can throw bad_alloc (JobSystem.cpp:50-56), leaking the job; ~Impl then waits forever on Outstanding (:240).
- **Fix (TextSystem.cpp):** a short Reason on FinishedGlyph, logged once per key in TakeUploads with Font::Name and the glyph index (same snippet as AA2-MEDIA-03 step 1); hold the job in a unique_ptr and undo the increment and entry if Submit throws; name the 512 px limit.
- **Effort:** S.
- **Timing:** now, with AA2-MEDIA-03 step 1.
- **Overlaps:** AA2-MEDIA-03, AA2-RT-02, CQ-MEDIA-06.
- **Panel discussion:** none.

### 5.7 API: public headers and game-facing API

#### CQ-API-01 Four parallel field-descriptor structs; two hand-written type comparisons and five element-count products

- **Severity:** low (revised from medium in verification).
- **Category:** readability, duplicate.
- **Sources:** API-2.
- **Evidence:** Component.hpp:83-95 FieldInfo, Component.hpp:146-152 Detail::FieldType, Registry.hpp:121-132 FieldSchema (OldNames at :131) and SceneFile.hpp:38-48 SceneFile::Field (defaulted operator== at :47) repeat Kind, Flags, Extent[2] and Tag; three of them also repeat Size (Detail::FieldType has none). Member-by-member copies at Component.hpp:316-317, Registry.cpp:440-441, SceneFile.cpp:857 and :1010. The two hand-written type comparisons are Registry.cpp:496-497 (IsSameFieldType) and SceneFile.cpp:810-811 (CQ-DATA-02's subject). Extent products at SceneFile.cpp:69 (uint32, in a local ElementCount(const SceneFile::Field&) helper the other sites do not use), :80 (uint64, on purpose: CheckField validates untrusted file extents), Registry.cpp:427 (size_t), GameSession.cpp:73 (uint32) and PlayerContent.cpp:98 (size_t). The later uint32 products are safe only because CheckField has already proven count <= Size < 2^32; two 65537 extents wrap in uint32 to 131073, which can pass a `Size % count` test.
- **Why it matters:** copy-and-compare clutter that a reviewer must keep in step. No planned change multiplies it: AA2-ECS-01 pins FieldKind enumerator values and adds a format policy (ArchitectureAssessment-2026-11-26.md:495-517) without a field member, and AA2-ECS-04's FieldIsRuntime (:559-575) is a bit in the existing Flags byte that all four structs already carry.
- **Fix (lighter form, about two small snippets after CQ-DATA-02):**
  1. Make FieldType public in Component.hpp with operator== and a 64-bit `ElementCount()` (std::uint64_t, never uint32: a uint32 helper used by CheckField would reopen an overflow on untrusted scene and profile input). Keep the flat members of FieldInfo, FieldSchema and SceneFile::Field.
  2. Add one constexpr `TypeOf(const T& field)` that builds a FieldType from any of the three, used by the two comparisons and the five products (replacing SceneFile.cpp's local ElementCount). Embedding a `FieldType Type;` member instead would rename about 45 member accesses across SceneFile.cpp, Registry.cpp, GameSession.cpp, PlayerContent.cpp, Component.hpp and the tests, so each header step would drag in all its users; it is not proposed.
  3. A FieldInfo or ComponentInfo layout change is a game-contract change (every game rebuilds), so any such edit lands in the Phase 3 contract window.
  4. The near-identical AssetRef scans (GameSession.cpp:64-82, PlayerContent.cpp:90-107) are the better duplicate target; they are CQ-DATA-01 step 2 (AA2-ECS-04 also touches GameSession.cpp:65-82).
- **Effort:** S.
- **Timing:** Phase 3 format group, after CQ-DATA-02 and with the AA2-ECS-01 pin, before AA2-ECS-04.
- **Overlaps:** AA2-ECS-01, AA2-ECS-04, CQ-DATA-02, CQ-DATA-01.
- **Panel discussion:** DATA asked that the behavior rule land first (private), then the struct work.
- **Verification:** overstated; severity medium -> low because no planned change needs four struct edits, and the proposed embedded member and uint32 ElementCount were replaced by a lighter, overflow-safe form.

#### CQ-API-02 RAVEN_COMPONENT's static_assert message claims to check declaration order, which only the run-time check catches

- **Severity:** low (revised from medium).
- **Category:** readability, misleading message.
- **Sources:** API-3; DATA and API debated feasibility.
- **Evidence:** Component.hpp:362-364 compares only size and alignment but says "must list every data member, in declaration order". Component.hpp:306-323 assigns offsets in list order; only CaptureDefaults' address compare (:398-420) detects a misorder, at registration. The resulting throw (Registry.hpp:187-189, GameSystems.hpp:153-155) says "not at their natural offsets", pointing at alignas. GetFields is not constexpr (:431-432).
- **Why it matters:** no silent corruption (the run-time check catches every misorder), but the developer gets a rejected reload after a full build with a message naming the wrong cause.
- **Fix:**
  1. Phase 1 † commit: narrow the static_assert message to "must list every data member"; have CaptureDefaults report the first mismatched field index and say "listed out of declaration order, or alignas". Use the CQ-API-06 helper so the text exists once.
  2. Optional: an ignored MSVC probe of a constexpr order check guarded by constant-evaluability (DATA's sketch). Adopt only if the probe passes without changing the macro's expansion.
- **Effort:** S.
- **Timing:** Phase 1 †.
- **Overlaps:** AA2-HR-02, AA2-ECS-08 step 3, CQ-API-06.
- **Panel discussion:** DATA thought the constexpr check feasible behind a guard; API thought not without macro changes. The lead makes the message fix the required step and the check an optional probe.

#### CQ-API-03 Deterministic game code is pushed onto per-call type-erased TryGet

- **Severity:** low (revised from medium in verification).
- **Category:** slow, hot-path lookup.
- **Frequency:** per entity per fixed step, per component touched.
- **Sources:** API-4; GAME confirmed coverage.
- **Evidence:** the chain SceneAccess.hpp:69-80 -> SceneAccess.cpp:74-79 or :81-86 -> Registry::GetOwner (Registry.cpp:867-870, the first IsAlive) -> IsHostOwned (two ID compares) -> Registry::TryGetComponent (Registry.hpp:318-319; Registry.cpp:817-831, the second IsAlive) -> FindPool (an unordered_map find, Registry.cpp:253-260) -> a sparse lookup. Only the first exported call crosses the game/engine boundary; the second is inside the RavenCore DLL. View order is unspecified (Registry.hpp:354), so games build ID order themselves (InIdOrder, ArenaSystems.hpp:81-92: a View, an exported GetRef per entity, two vectors and an unreserved sort). Drones.cpp:50-56 runs per fixed step (ArenaGame.cpp:58, FixedUpdate) with three TryGets per drone. Run.cpp:29-32 builds two more InIdOrder lists (a View, a GetRef per entity and a sort each), but only in RunState::Starting, once per run start, and calls Commands.Destroy per entity, not TryGet. Measured: Registry::Read in random order is 15.8-16.7 ns at 100k (ECSDesign.md:332; EcsBenchmark.cpp:250-256); SceneAccess::TryGet adds GetOwner and a second call, so about 25 ns per TryGet is an estimate.
- **Why it matters:** at today's scale the cost is negligible: estimate 120 swarm drones (ArenaComponents.hpp:84; GameServicesDesign.md:887) x 3 x about 25 ns = about 9 us per fixed step against a recorded 2.5-2.8 ms swarm step (GameServicesDesign.md:888), and 14 drones in normal play. At a hypothetical 10k units, 10k x 3 x 25 ns = about 0.75 ms per fixed step plus a sort (estimate); that is RTS scale, which the owner wants kept possible but not focused on. The substance is AA2-ECS-08 step 6 (EachInIdOrder backed by a host-cached ID-sorted index, rated low there) and AA2-PERF-09 step 3 (the InIdOrder allocation churn); this entry is code-level detail for those IDs.
- **Fix (code-level notes for AA2-ECS-08 step 6):**
  1. `SceneAccess::EachInIdOrder<A, const B...>(fn)` with the View signature, resolving pools once per call (so the double IsAlive goes) and walking a host-cached ID-sorted index (SceneAccess.hpp, SceneAccess.cpp/Scene.cpp).
  2. Port Drones.cpp, which carries the RNG-order dependency that DeterminismCheck guards; delete InIdOrder there. Do not port Run.cpp's Destroy loops on an assumed determinism dependency: either show that destroy order affects later random-number order (through dense reorder into an unordered View that draws random numbers), or replace them with a plain View.
  3. Then delete SceneAccess::Query and Registry::GetEntities after recording their last benchmark rows (CQ-API-07 step 3). Port every caller first: EcsBenchmark.cpp:139, 164; Windows3DSmoke.cpp:338, 362; HierarchyTests.cpp:154 (access.Query<TransformComponent>()); RegistryTests.cpp:193 and :330 (registry.GetEntities()). Otherwise Registry::GetEntities stays as a test-only accessor.
  4. Optional: drop the second IsAlive through the AA2-CORE-03 step 7 RegistryAccess seam.
- **Effort:** M.
- **Timing:** with AA2-ECS-08 step 6, which AA2 places during M6B (Phase 4).
- **Overlaps:** AA2-ECS-08 step 6 (primary), AA2-PERF-09 step 3, AA2-ECS-06, CQ-GAME-01.
- **Panel discussion:** none.
- **Verification:** overstated; severity medium -> low because the cost is about 9 us per step at today's largest workload, the 0.75 ms figure applies only to a hypothetical 10k-unit game, and the fix restates AA2-ECS-08 step 6, which AA2 rates low.

#### CQ-API-04 Label vocabulary: GetAuthoredId returns a label, and SceneAccess::IsAlive means "owned and alive"

- **Severity:** low (narrowed from medium).
- **Category:** readability, naming.
- **Sources:** API-5, DATA-12 step 4.
- **Evidence:** Scene.hpp:33-37 (CreateEntity(name), FindEntity(name), GetAuthoredId returns "the label"), SceneAccess.hpp:29 (Find(authoredId)), SceneCommands.hpp:30 (Create(label)), SceneFile.hpp:35 (Label). "Id" elsewhere means the persistent EntityRef (Scene.hpp:31-48, :111). SceneAccess::IsAlive returns OwnsLiveEntity (SceneAccess.cpp:35-38), which is Scene::Owns (Scene.hpp:73-74). The private SceneCommands::ComponentType (SceneCommands.hpp:68-75) shadows the ECS concept used at :35, :41. grep GetAuthoredId: 0 game sites; `.IsAlive(` in Games: 1 site (Interaction.cpp:125).
- **Fix (Phase 3 renames group):** GetAuthoredId -> GetLabel; SceneAccess::IsAlive -> Owns (one game line); parameter renames to `label`; SceneCommands::ComponentType -> RecordedType; a two-line glossary in Scene.hpp and SceneAccess.hpp (label = optional authoring name; ID/EntityRef = persistent identity). FindEntity, Find and the SceneFile nested names stay.
- **Effort:** S.
- **Timing:** Phase 3.
- **Overlaps:** AA2-CORE-03, AA2-ECS-02.
- **Panel discussion:** the lead records the narrow rename as a delegated decision (section 9): it fixes the two actively misleading names for one game line; a full rename buys consistency at the cost of every tool and test.

#### CQ-API-05 Single/Count traps: const reads stamp, Count changes complexity by class, games re-implement const Single and First

- **Severity:** low.
- **Category:** readability, API trap.
- **Sources:** API-6, GTB-4's First/Single part, DATA-12 step 1.
- **Evidence:** Single<T> iterates a mutable View and stamps (SceneAccess.hpp:82-93; Registry.hpp:415). 9 of 15 game Single sites only read (Craft.cpp:37, 50, 83; Drones.cpp:41-42; Cells.cpp:40; Run.cpp:19; Screens.cpp:38, 165). First<T> copies in ArenaSystems.hpp:67-77, SamplesGame.cpp:52-62, CourtyardGame.cpp:46-56. Registry::Count is O(1) (Registry.hpp:280-284); SceneAccess::Count walks a View (SceneAccess.hpp:94-100), called per frame at Interaction.cpp:64, 69.
- **Fix (Phase 3 additions group, SceneAccess.hpp):**
  1. Constrain Single on ViewComponent and iterate View<T>(), with a static_assert that mutable T is not host-owned. Single<const T> returns const T* without stamping; Single<T> keeps its meaning. No game breaks.
  2. Add a variadic `First<T...>() const` returning an Entity.
  3. Document SceneAccess::Count as O(entities with T).
  4. Game-only follow-ups: switch the 9 read sites; delete the three First copies (with CQ-GAME-04).
- **Effort:** S.
- **Timing:** Phase 3; game ports right after.
- **Overlaps:** AA2-ECS-08 step 2, AA2-GP-01, CQ-GAME-04, CQ-API-12.
- **Panel discussion:** AA2-ECS-08 step 2 proposed Single returning const plus a new SingleMut, which silently changes 15 sites and breaks 6. The lead adopts API's non-breaking form, matching the TryGet<const T> convention, as a delegated decision.

#### CQ-API-06 The defaults-capture-and-throw block is duplicated in Registry::Register and SystemTable::Component

- **Severity:** low.
- **Category:** slop, duplicate.
- **Sources:** API-7.
- **Evidence:** Registry.hpp:183-191 and GameSystems.hpp:149-157 hold the same five lines and message. GameSystems.hpp:19-20 includes <stdexcept> and <string> only for this.
- **Fix:** `ECS::Detail::RequireDefaults<T>()` in Component.hpp, called from both; drop <stdexcept> from GameSystems.hpp. Same snippet as CQ-API-02 step 1.
- **Effort:** S.
- **Timing:** Phase 1 †.
- **Overlaps:** AA2-HR-02, CQ-API-02.
- **Panel discussion:** none.

#### CQ-API-07 Vestigial public surface not covered by AA2-CORE-04

- **Severity:** low.
- **Category:** slop, dead code.
- **Sources:** API-8, DATA-8 public part, DATA-12 step 3, HOST-8 PenetratesXZ.
- **Evidence:** Scene.hpp:30 `RAVEN_API virtual ~Scene()` and :90-91 protected m_Registry: grep `public Scene|: Scene\b` finds no subclass. Registry.hpp:223-227, 250-254 Has/Remove(Entity, const T&): callers only RegistryTests.cpp:209-275. Scene.hpp:9 <limits> unused. AssetStore.hpp:73 "Legacy AssetResources stays pinned". Collision2D.hpp:10-29 (with PenetratesXZ at :24, no caller) is used only by Courtyard; EngineModules.md:294 describes it through types that no longer exist. SceneAccess::Query and Registry::GetEntities: no engine or game caller, but they feed recorded benchmark rows (EcsBenchmark.cpp:139, 164, 176) and smoke checks (Windows3DSmoke.cpp:338, 362).
- **Fix:**
  1. Phase 3 deletions group: non-virtual ~Scene, private m_Registry, drop <limits>; delete the AssetStore.hpp:73 sentence with AA2-CORE-04; delete the value-deduced Has/Remove and port RegistryTests to Has<T>/Remove<T>.
  2. At B5: move Collision2D (PenetratesXZ deleted in the same snippet) into Games/Courtyard/Source and fix EngineModules.md:294. One contract change instead of two.
  3. Phase 4, after CQ-API-03: record the last Query/GetEntities benchmark numbers in ECSDesign.md, port the smoke checks and the test callers (HierarchyTests.cpp:154; RegistryTests.cpp:193, 330), delete both.
- **Effort:** S.
- **Timing:** as listed.
- **Overlaps:** AA2-CORE-04, AA2-CORE-03, CQ-DATA-08, CQ-GAME-09.
- **Panel discussion:** API and BLD moved Query/GetEntities out of Phase 3 so benchmark cases are not rewritten twice.

#### CQ-API-08 Style drift from AGENTS.md in contract-hashed headers and Registry.cpp

- **Severity:** low.
- **Category:** readability, style.
- **Sources:** API-9, DATA-11.
- **Evidence:** Entity.hpp:9-39 (27 space-indented lines), Registry.hpp:172-176, 208-212, 321-327, 348-350 (16 lines) and `Registry &operator=` (:322, 325), Registry.hpp:3 include order, SceneAccess.hpp:23, 110 (`  public:`), Window.hpp:49, 61, 73-74. Registry.cpp has 73 space-indented lines (for example 21-35, 108-121, 315-398) mixed with tabs and `auto &impl` (:324-382); git blame dates them to 4720b043 (2026-10-18), before the style rule.
- **Fix:** Registry.cpp now as one whitespace-only snippet (private). The four headers in Phase 3 as the first group, one whitespace-only snippet per file. No formatter.
- **Effort:** S.
- **Timing:** Registry.cpp now; headers Phase 3 first.
- **Overlaps:** AA2-CORE-03, CQ-X-08.
- **Panel discussion:** none.

#### CQ-API-09 Platform/Input.hpp mixes the game-facing snapshot with backend mutators and repeats its own tables

- **Severity:** low.
- **Category:** readability, mixed responsibility.
- **Sources:** API-10.
- **Evidence:** Input.hpp:180-436 carries 12 mutators beside the game queries, though games receive `const InputState&` (GameSystems.hpp:92). GetModifiers (:226-247) re-implements GetModifierOfKey (:133-144). `& 0x0F` (:94) unnamed; 256 written at :123 and as KeyCount (:182). Query scans of Events (:302-324).
- **Fix:** Phase 3: fold GetModifiers over GetModifierOfKey, name AllModifiers, use KeyCount in IsValid. With FD-29 gamepads: a Private InputRecorder and a move to Raven/Input/InputState.hpp.
- **Effort:** S.
- **Timing:** step 1 Phase 3; step 2 with the FD-29 gamepad step.
- **Overlaps:** AA2-GP-10.
- **Panel discussion:** none.

#### CQ-API-10 Normalize throws from a gameplay math header, so callers copy a magic 1e-12 guard

- **Severity:** low.
- **Category:** readability, API trap.
- **Sources:** API-11.
- **Evidence:** Transform.hpp:53-59 throws for squared length <= 1.0e-12f or non-finite. SamplesGame.cpp:107 copies the 1.0e-12f literal; CourtyardGame.cpp:103 uses an exact-zero check before Normalize at :113, safe only because its axis inputs are -1, 0 or 1; Arena avoids Normalize (Drones.cpp:71-73). JoltSupport's noexcept Decompose calls Normalize directly (CQ-GP-13). A throw suspends the system (GameModule.hpp:25). TryNormalMatrix exists beside NormalMatrix (Transform.hpp:166-199), but no TryNormalize. JoltSupport.hpp:41-43, 80 has its own finite and length tests.
- **Fix:** Phase 3: TryNormalize, NormalizeOr and one MinNormalizeLengthSquared in Transform.hpp, shared with Normalize and JoltSupport (including CQ-GP-13's check); port SamplesGame.cpp:107-108. Keep the throwing Normalize for authored-data validation.
- **Effort:** S.
- **Timing:** Phase 3.
- **Overlaps:** AA2-RT-01, CQ-RND-09, CQ-GP-13.
- **Panel discussion:** none.

#### CQ-API-11 Public API shapes that allocate on per-frame paths: string-building label lookup

- **Severity:** low.
- **Category:** slow, hot-path allocation.
- **Frequency:** per frame.
- **Sources:** API-12 step 3, DATA-12 step 2, GTB-3 step 5 (GetPreviewNames moved to CQ-CORE-09).
- **Evidence:** SceneAccess.cpp:20-23 builds a std::string for Scene::FindEntity(const std::string&) because m_Names (Scene.hpp:144) lacks heterogeneous lookup. SamplesGame.cpp:250 calls Find("benchmark-rotor-0") every frame; 17 characters exceed MSVC's 15-character SSO, so it allocates every frame inside the benchmark trace path. Verification estimated the whole lookup at about 0.1 us per frame (one heap allocation plus one unordered_map lookup, Scene.cpp:233-237), negligible against 3.5 ms.
- **Fix:**
  1. No game-side cache: a cached EntityRef would still need a Resolve each frame and saves about 0.1 us (CQ-GAME-03 dropped that step in verification).
  2. Phase 3: a transparent hash and equal_to on m_Names; string_view FindEntity and CreateEntity (Scene.hpp/.cpp, SceneAccess.cpp), in one snippet with CQ-DATA-03 step 5.
- **Effort:** S.
- **Timing:** step 2 Phase 3.
- **Overlaps:** AA2-PERF-09, CQ-DATA-03, CQ-GAME-03.
- **Panel discussion:** the panel kept both the engine and game fixes; verification of CQ-GAME-03 dropped the game-side cache as negligible.

#### CQ-API-12 The active-camera rule exists three times and has drifted

- **Severity:** low.
- **Category:** slop, duplicate.
- **Sources:** API-13 (new in round 2).
- **Evidence:** GameSession.cpp:461-474 uses the tagged camera, else the first CameraComponent. SamplesGame.cpp:40-50 and CourtyardGame.cpp:58-68 view only tagged cameras and return null otherwise (used at SamplesGame.cpp:78, 126, 200; CourtyardGame.cpp:152). Latent: every generator and level tags a camera (SampleSceneBuilder.cpp:74, ArenaTool.cpp:279).
- **Why it matters:** removing the tag by dump/apply would leave the host drawing from one camera while the game's camera systems do nothing, with no log line.
- **Fix:** Phase 3: RAVEN_API `SceneAccess::ActiveCamera() const` implementing the host rule; GameSession calls it; delete the two game helpers after the window.
- **Effort:** S.
- **Timing:** Phase 3 additions; game ports right after.
- **Overlaps:** CQ-API-05, CQ-GAME-04.
- **Panel discussion:** none.

#### CQ-API-13 An exception inside a UI scope leaves the container open for the rest of the frame; End() silently tolerates imbalance

- **Severity:** low.
- **Category:** readability, hidden ordering.
- **Sources:** API-14 (new in round 2).
- **Evidence:** UiContext.cpp:266 clears m_Scopes only in BeginFrame; :343-347 End pops only when non-empty and says nothing. GameModule.cpp:381-404 suspends a throwing system and runs the next one against the same UiContext, so later elements nest under the orphaned container with different IDs (:158, :169). Ui.hpp:105-126 is a pure interface with a protected non-virtual destructor.
- **Fix:**
  1. Now, UiContext.cpp: log once when End() runs on an empty stack or scopes remain open at Layout, naming the innermost open ID.
  2. Phase 3, Ui.hpp: a header-only, non-copyable UI::Scope whose destructor calls End. New game code uses it; Arena's balanced calls need no port.
- **Effort:** S.
- **Timing:** step 1 now; step 2 Phase 3 with AA2-MEDIA-05/06.
- **Overlaps:** AA2-MEDIA-05/06, CQ-MEDIA-08.
- **Panel discussion:** none.

#### CQ-API-14 The public ECS::GetCameraView free function has no caller and ignores parents, unlike Scene::GetCameraView; ViewFromPoseRH is a fourth copy of the Euler order

- **Severity:** low.
- **Category:** slop, dead code.
- **Frequency:** n/a (build time: in the game PCH).
- **Sources:** completeness critic (CRIT-4).
- **Evidence:**
  - Public/Raven/Components/CameraComponent.hpp:55-62: `inline CameraView GetCameraView(const TransformComponent&, const CameraComponent&, float aspect)` builds the view from the local TransformComponent through ViewFromPoseRH.
  - `git grep -n "GetCameraView("` (excluding ThirdParty and docs, including Tests and Games) finds only the Scene member (Scene.hpp:66, Scene.cpp:595, called at GameSession.cpp:498) and this definition. The free function has no caller.
  - Scene.cpp:595-611: the engine's real camera view composes the world transform and the parent rotation chain (GetWorldTransform plus ComposeTransform(entity, true)), so the two disagree for any parented camera, such as a camera rig.
  - CameraView.hpp:105-111: ViewFromPoseRH spells `RotationZ(rotation.Z) * RotationY(rotation.Y) * RotationX(rotation.X)`, a fourth hand copy of the order beside the three in CQ-DATA-12. Its only other caller is SamplesGame.cpp:103 (free-fly axes).
  - cmake/RavenGame.cmake:44-50 precompiles every Public/Raven/Components/*.hpp into each game's PCH, so every game TU sees the function.
- **Why it matters:** a game author who finds ECS::GetCameraView beside CameraComponent will use it for a world-to-screen helper; with a parented camera it silently disagrees with the rendered view. The extra Euler copy makes CQ-DATA-12's consolidation incomplete.
- **Fix:**
  1. Phase 3 contract window: delete ECS::GetCameraView from CameraComponent.hpp, and drop its CameraView.hpp include if nothing else in that header needs it. Before dropping the include, check that SamplesGame.cpp (which calls ViewFromPoseRH) still gets CameraView.hpp through another header: Scene.hpp:5, GameSession.hpp:7 and VulkanContext.hpp:19 include it, so the game PCH very likely still covers it. Name that check in the snippet.
  2. With CQ-DATA-12 step 2: route ViewFromPoseRH's orientation through the same EulerRotation helper. One public file each, in the CQ-X-08 ordered batch.
- **Effort:** S.
- **Timing:** Phase 3 contract window.
- **Overlaps:** CQ-DATA-12 (extends it), CQ-X-08.
- **Panel discussion:** not discussed by the panel; added in verification.
- **Verification:** confirmed (critic proposal verified separately); severity low (new) because it is a dead public helper with a trap, at no runtime cost.

### 5.8 BLD: platform backends, CMake and scripts

#### CQ-BLD-01 The harness scripts duplicate snapshot, launch, percentile and PPM code; the tracked live reload check omits the Content folder and cannot build

- **Severity:** high.
- **Category:** slop, duplicate.
- **Sources:** PLT-1; failure mode verified by BLD and GAME in round 2.
- **Evidence:**
  - scripts/Test-Windows3DLive.py:510-513: the snapshot copies the folder tuple (RavenEngine, Games, ThirdParty, cmake, scripts) at :511 plus CMakeLists.txt and CMakePresets.json (:512-513), but not the top-level Content folder. The script was last changed in 5f4df75 (M6A), before Content/Fonts/*.ttf and *-OFL.txt were added and tracked in 68e23e8 (2026-11-25).
  - scripts/Benchmark-Windows3D.py:187-190 has its own list that includes Content (and docs and BuildProfiles); GameServicesDesign.md:958 records that the benchmark snapshot failed without Content and was fixed there only.
  - cmake/RavenContent.cmake:80-96: raven_stage_file has no EXISTS guard. :115-118 stages Content/Fonts; :137-143 RavenContent DEPENDS on every source. out/build/x64-debug/build.ninja:1384 lists Content\Fonts\NotoSans.ttf as an explicit input with no rule for it, so configure succeeds and Ninja stops the snapshot build. StageContent's file(COPY_FILE) is also fatal on a missing source (StageContent.cmake.in:10). Only Tests/ is EXISTS-guarded (CMakeLists.txt:22-27). No out/live3d-* directory exists, so the check has not run since the fonts arrived. Not run (read-only review).
  - By reading, Content is the only folder the build needs that the live list omits: docs is referenced only as text written into PlayerInputs.txt (cmake/RavenPlatform.cmake:8; RavenPlayer.cmake:17), and BuildProfiles is not referenced by any CMake file. This is unbuilt and must be confirmed by the step 1 run. scripts/Test-Windows3D.ps1 copies only Games/Samples (:42), so it is not another stale full-repo list.
  - Duplicates: check() (Live:39-41, Bench:38-40); summary statistics with different units (Live:464-466 in ms, Bench:63-67 in ns/1e6, and only Bench checks for empty samples); the layer environment (Live:190-199, run_sample at :188; Compare-Captures.py:91-96); PPM parsing (Live:174-177 validates the header against the window client size by split(), Compare:46-55 uses a regex and a truncation check: similar, not identical); key pairs as literals (Live:127, 357, 361; Bench:81, 91, 106); the level list three times (Live:241, 484, 543). Benchmark imports the live script through runpy (Bench:9, 18; used at 101, 218, 223, 290). live() is 234 lines (Live:215-448; read_trace starts at :451).
- **Why it matters:** this is the tracked check for shader, scene, PNG, FBX and DLL reload with last-good recovery (HotReloadDesign.md:43, 340), a joint v1 goal. As written, the next reload regression surfaces as "build failed", and the cause is exactly the drifted-duplicate pattern. CI does not run it (AA2-BLD-03).
- **Fix:**
  1. Now: add "Content" to the folder tuple at Test-Windows3DLive.py:511. The verification is one run of `python scripts/Test-Windows3DLive.py x64-debug`, a GPU, windowed harness that only the owner can run on the laptop; report the result with the change.
  2. The actual bug guard: scripts/raven_harness.py (importable name) with one snapshot folder-list constant and build_snapshot, used by both scripts. In the same module, later: Sample, run_sample, save, check, percentiles(values, scale) that keeps the empty-input check, read_ppm (the live call site keeps its window-size comparison), the layer-environment helper and a KEYS table. Moving check() and KEYS is low-value consolidation and must not block the folder-list constant.
  3. Benchmark-Windows3D.py imports it; delete the runpy import.
  4. Compare-Captures.py uses the shared helpers.
  5. Low priority, after steps 2-4: split live() into one function per case group over a small context object (readability polish on a test harness).
- **Effort:** M (step 1 is a one-line change; step 2's folder constant is S).
- **Timing:** step 1 now (Phase 1); step 2's folder constant soon after; the rest of steps 2-4 before B4; step 5 later.
- **Overlaps:** AA2-BLD-03 (complements it).
- **Panel discussion:** none.
- **Verification:** confirmed with corrections; severity high -> high because the only reload-recovery check (a v1 goal) currently fails as "build failed", caused by a drifted duplicate list.

#### CQ-BLD-02 Each Debug game reload hashes about 15 MB of DLL and PDB twice in BuildGame.cmake

- **Severity:** medium (revised from high by the panel).
- **Category:** slow, redundant pass.
- **Frequency:** per game reload and per no-change build request, including every full host build: RavenGameModule.cmake:52-57 runs the recipe as an ALL custom target that depends on RavenCore.
- **Sources:** PLT-2; phase boundaries checked by CORE.
- **Evidence:** BuildGame.cmake.in:68-87 raven_game_output_digest SHA-256s the manifest and each listed output (:71, :80). RavenGame.cmake:57-60 writes the DLL, plus the linker PDB in Debug and RelWithDebInfo only (:60), into GameOutputs.txt; in Release only the DLL (about 300 KB) is hashed. It runs before the build (:89) and again in verify (:180). Sizes: Arena.dll 1,476,096 B and Arena.pdb 13,946,880 B (out/build/x64-debug/RavenEngine). Measured (H5, Samples Debug, pre-M6A): hash 46 ms, verify 54-89 ms (HotReloadDesign.md:349); the Samples PDB size behind those numbers was not recorded. The "hash" phase (:42-90; raven_trace_phase(lock) at :41) also includes the GameContract and RavenGameSources includes, two script-mode GLOB_RECURSE scans and 86 input SHA-256s (BuildInputs.list has 85 inputs plus the recipe); "verify" (:180-190) holds the output digest, one CMakeCache hash and two small write-and-rename pairs, so it is mostly output hashing. The stamp is a "|"-split string (:92-108); :145-164 sit unindented inside if(configure_needed) at :144-165.
- **Why it matters:** derived: about 45-80 ms from verify; the pre-build share (25-40 ms) is an estimate with no measured split behind it. Roughly 60-110 ms per Debug edit, 5-7% of a loop at its 1.5 s target; it grows with the PDB. Release iteration saves only the small DLL hash.
- **Fix (cmake/BuildGame.cmake.in; not a contract input, so only a recipe-digest reconfigure):**
  1. Key each output by path|size|mtime in the pre-build check; hash content only on a tuple mismatch. Take sub-second mtimes (`file(TIMESTAMP ... "%s%f" UTC)`, CMake 3.23+, fine under the 3.24 minimum) and `file(SIZE)`; the existing :125 uses whole seconds, and copying that would weaken the check. Safety argument, consistent with CQ-CORE-01's rejection of size+mtime as proof: the tuple is only compared against a tuple this recipe recorded itself, after link.exe exited, under Build.lock. A writer outside the recipe and the lock (a direct `cmake --build` of the child) could match a tuple mid-write; that writer is unsupported per EngineModules.md:73, and the snippet comment says so. Changing the stamp's third field changes its format: the first run after the change sees previous_output_digest differ (:96-100), sets repair_outputs and does one --clean-first rebuild, which is harmless; the snippet says so.
  2. In verify, hash only the DLL; record the PDB by tuple. The new stamp is still written last, by rename (:188-189), because AA2-HR-03 step 2 makes BuildInputs.sha256 the host's watched output.
  3. Extract raven_read_stamp, raven_write_stamp and raven_configure_needed; indent :145-164.
  4. A separate snippet in a second file (unrelated to the double hash): allow game_root, build_directory and RAVEN_GAME_OUTPUT overrides with `if(NOT DEFINED ...)`, so Test-Windows3D.ps1:44-61 passes -D values instead of text-replacing the recipe. RAVEN_GAME_OUTPUT sits inside an execute_process bracket literal (:150), so it first needs a variable.
  5. Measure with RAVEN_RELOAD_TRACE before and after; re-rate high if hash plus verify output time is 60 ms or more.
- **Effort:** S.
- **Timing:** before M6B, with CQ-BLD-03 (same file).
- **Overlaps:** AA2-HR-06, AA2-HR-03 (step 2, the stamp contract), CQ-CORE-01.
- **Panel discussion:** the original step 3 (the host reads the build's digest) was dropped: SHA-256 in CMake, FNV in the host, and the engine has no SHA-256. CQ-CORE-01 is independent.
- **Verification:** confirmed with corrections; severity medium -> medium because it is about 5-7% of the Debug loop, contained, with no bug risk. A suggested citation change to HotReloadDesign.md:347 was checked and not applied: grep shows the row at :349 (section 10.4).

#### CQ-BLD-03 Every game reload re-runs the child project's glob verification, which BuildGame.cmake mostly does already

- **Severity:** medium.
- **Category:** slow, redundant pass.
- **Frequency:** per game reload, before compile.
- **Sources:** BLD-11 (new in round 2).
- **Evidence:** out/build/x64-debug/RavenEngine/GameBuild/Arena/.ninja_log: cmake.verify_globs runs on every reload, 25-39 ms over the last 10 one-file reloads (one 60 ms outlier) (measured). VerifyGlobs.cmake:6, 28 walks Games/Arena/Source and RavenEngine/Public. RavenSourceDiscovery.cmake:7-9 adds CONFIGURE_DEPENDS whenever not in script mode; RavenSourceDiscovery.cmake is not a contract input (GameContract.cmake:9-13). BuildGame.cmake.in:45-49 rescans both folders, and :117-137 forces a configure when the entry count differs (:123) or when a changed or new entry is not a .cpp or is not newer than the stamp (:130). It does not catch a same-count .cpp swap: A.cpp deleted and B.cpp created after the last build (a git checkout across a rename, or save-as-and-delete). Removed paths are never examined. Today the child's verify_globs catches that case and re-runs cmake; without it, Ninja still lists A.cpp and fails ("missing and no known rule"), no stamp or inputs file is written, and every later request repeats the same skip and fails until the file count changes or RAVEN_GAME_FORCE_REBUILD is passed. The :113-116 comment ("Ninja rebuilds by timestamp") silently depends on CONFIGURE_DEPENDS for files Ninja has never seen.
- **Why it matters:** 25-39 ms measured, about 2-3% of the loop, serial before compile, growing with Public and game sources. Removing it without first closing the swap gap would introduce a stuck-build regression.
- **Fix (in this order):**
  1. BuildGame.cmake.in first: make any path absent from previous_entries force a configure, so only a changed digest of a known .cpp path may skip it. Comparing the path parts (strip "|digest" from both lists once) also covers removals, because the count is equal. Add a one-line comment at :113-116 that the recipe, not CONFIGURE_DEPENDS, now owns detecting added and removed sources.
  2. Only then, RavenSourceDiscovery.cmake: skip CONFIGURE_DEPENDS when RAVEN_DISCOVERY_STATIC is set.
  3. BuildGame.cmake.in: pass -DRAVEN_DISCOVERY_STATIC=ON in the configure command (:145-161), with a comment that the recipe rescans itself.
  4. Measure with RAVEN_RELOAD_TRACE and .ninja_log; record in HotReloadDesign.md, together with a manual same-count swap check (delete one .cpp and add one in the same settle window). While measuring, record whether each build restarts mspdbsrv; act only on a number. No tracked test, per the minimal-tests rule.
- **Effort:** S.
- **Timing:** before M6B, with CQ-BLD-02.
- **Overlaps:** AA2-HR-06, CQ-BLD-02.
- **Panel discussion:** none.
- **Verification:** confirmed with corrections; severity medium -> medium because the cost is measured, but the redundancy is not complete: the child check covers a same-count swap, so step 1 must land before the check is removed.

#### CQ-BLD-04 The Windows.h configuration is copied into six files, and in Win32Window.cpp it arrives after windows.h

- **Severity:** low (revised from medium in verification).
- **Category:** readability, hidden ordering.
- **Sources:** PLT-3; SDK header confirmed by VK and API.
- **Evidence:** Win32Window.cpp:1-3 defines VK_USE_PLATFORM_WIN32_KHR and includes <vulkan/vulkan.h> first; C:/VulkanSDK/1.4.357.0/Include/vulkan/vulkan.h:44-46 includes <windows.h> under that macro, before Win32Window.hpp:3-6 defines WIN32_LEAN_AND_MEAN and NOMINMAX. So in that TU the full windows.h is parsed and the min/max macros are live; NOMINMAX still applies to later includes, but LEAN has no effect. Full copies in six files: Win32Window.hpp:3-4, Win32Input.hpp:3-4, Win32EventPump.cpp:1-2, Win32GameModule.cpp:1-2, Win32AtomicFile.cpp:1-2, Win32Paths.cpp:3-4; Win32DirectoryWatch.cpp:3-5 guards NOMINMAX only; Win32Random.cpp includes Windows.h bare (:3) and works around max with parentheses (:16). RavenPlatform.cmake:4 already sets UNICODE/_UNICODE privately on RavenCore. A repo-wide grep finds no Windows.h include outside Private/Platform/Windows. No Windows file calls std::min/max today, and nothing uses headers that LEAN would drop (ShlObj.h is already included after LEAN in Win32Paths.cpp).
- **Why it matters:** no break today. The next std::min/max in Win32Window.cpp fails to compile in that TU only (a loud, immediate error), and that TU pulls the full windows.h.
- **Fix:**
  1. RavenPlatform.cmake:4: add WIN32_LEAN_AND_MEAN and NOMINMAX to RavenCore's private definitions, inside the if(WIN32) branch (:2-9) so Mac and Linux builds are unaffected.
  2. Delete the per-file defines in two snippets of three or four files. Verify with the Debug and Release warnings-as-errors build (CI-equivalent), naming Image.cpp and AudioClip.cpp (stb implementations) and Win32Random.cpp (it includes <bcrypt.h> after a now-LEAN Windows.h; bcrypt.h supplies NTSTATUS itself).
  3. Optionally drop the parentheses in Win32Random.cpp:16.
- **Effort:** S.
- **Timing:** now.
- **Overlaps:** CQ-X-03.
- **Panel discussion:** none.
- **Verification:** confirmed with corrections; severity medium -> low because the latent failure is a compile error confined to one TU and the cost is one full windows.h parse.

#### CQ-BLD-05 The vendored-library CMake and update scripts repeat version readers, folder-replace loops and Debug-optimization overrides, and three version pins are never checked

- **Severity:** medium.
- **Category:** slop, duplicate.
- **Sources:** PLT-4; MEDIA endorsed step 4.
- **Evidence:**
  - Version readers written twice per library for the three older libraries (RavenAudio.cmake:19-27 / UpdateMiniaudio.cmake:21-29; RavenVorbis.cmake:12-15 / UpdateStbVorbis.cmake:21-25; RavenJolt.cmake:15-17 / UpdateJolt.cmake:26-30). raven_check_version exists (RavenThirdParty.cmake:19-24), but Jolt (:16-19), miniaudio (:28-31) and stb_vorbis (:16-19) hand-roll their mismatch errors. A drifted reader fails loudly at configure time.
  - The larger gap: RavenYoga.cmake:8 sets RAVEN_YOGA_VERSION and RavenMsdf.cmake:9-10 pins msdfgen and msdf-atlas-gen, but neither file reads the vendored version or calls raven_check_version. RAVEN_YOGA_VERSION has no consumer (grep), RAVEN_MSDF_ATLAS_GEN_VERSION is never used, and RAVEN_MSDFGEN_VERSION is only injected as a define. The newer update scripts (UpdateFreeType/HarfBuzz/Msdf/Yoga.cmake) read no version at all. So for three libraries the pin is not enforced, and the vendored copy can drift from the documented version silently.
  - The replace loop five times (VendorCopy.cmake:13-19, UpdateJolt.cmake:35-41, UpdateMiniaudio.cmake:34-40, UpdateStbVorbis.cmake:30-36, UpdateIcu.cmake:88-94). Debug-optimization override twice (RavenJolt.cmake:47-51, RavenText.cmake:21-27).
  - RavenMiniaudio (RavenAudio.cmake:38-40) and RavenStbVorbis (RavenVorbis.cmake:21-29) bypass raven_third_party. Because the root CMakeLists.txt:6-7 sets EditAndContinue for MSVC Debug, both compile with /ZI, and miniaudio gets no /w (per-target raven_apply_warnings never applies to it). miniaudio.c, miniaudio.h and stb_vorbis.c have 0 non-ASCII bytes, so /utf-8 is only a guard. Five "# Builds RavenX." restating comments (RavenFreeType:11, RavenHarfBuzz:11, RavenMsdf:14, RavenYoga:11, RavenIcu:12).
- **Why it matters:** these files enforce the never-edit rule. The silent risk is the unchecked Yoga and msdf pins; each duplicated reader is a place where an update passes the script and fails the settings, and AA2-BLD-02 is about to add libraries.
- **Fix:**
  1. Put each version reader in a script-safe function that takes the library root as a parameter, in RavenThirdParty.cmake or a small per-library version file that both the settings file and the update script include (the settings files are not script-safe: RavenAudio.cmake:15-16 uses set(CACHE) and set_property(CACHE ...), ROOT paths come from PROJECT_SOURCE_DIR, which is empty under -P, and RavenText.cmake includes all the text settings). Use it with raven_check_version for Jolt, Audio and Vorbis, and add raven_check_version calls for Yoga, msdfgen and msdf-atlas-gen, reading their own version markers.
  2. Update scripts use raven_vendor_copy (VendorCopy.cmake). For ICU: raven_vendor_copy takes one source root and wipes the target folder (except README.md) on every call, while UpdateIcu copies from both ${source} and the generated ${work}/icudt*.dat; use raven_vendor_copy for the source paths, then copy the .dat afterwards with file(COPY), never a second raven_vendor_copy.
  3. A raven_optimize_debug_flags macro in RavenThirdParty.cmake, called from RavenJolt.cmake and RavenText.cmake.
  4. Route RavenMiniaudio and RavenStbVorbis through raven_third_party (PUBLIC definitions unchanged); delete the hand-rolled /w. This intentionally changes their Debug format from /ZI to /Zi and adds /utf-8 and /w to miniaudio; include both targets in the Debug and Release warnings-as-errors CI-equivalent check.
  5. Delete the restating comments.
- **Effort:** S.
- **Timing:** with AA2-BLD-02, before B7; step 1's missing checks may land earlier.
- **Overlaps:** AA2-BLD-02, AA2-PLAT-04.
- **Panel discussion:** none.
- **Verification:** confirmed with corrections; severity medium -> medium, but only because the unchecked Yoga and msdf pins are part of the finding; the duplicated readers alone fail loudly and would be low.

#### CQ-BLD-06 The ICU data file name is hard-coded outside the version pin

- **Severity:** low.
- **Category:** slop, magic number.
- **Sources:** PLT-5; MEDIA takes the TextSystem half.
- **Evidence:** RavenIcu.cmake:9 pins 79.1; RavenContent.cmake:114 stages the literal icudt79l.dat; TextSystem.cpp:177 opens the same literal; UpdateIcu.cmake:102-103 asks for a manual edit in Private/Text only. utypes.h:154 defines U_ICUDATA_NAME.
- **Why it matters:** on the next ICU major update, staging fails or ICU rejects the data at startup and no text renders.
- **Fix:**
  1. TextSystem.cpp:177: `std::string(U_ICUDATA_NAME) + ".dat"`, in the same snippet as naming the two 64 MiB literals (CQ-MEDIA-11).
  2. RavenContent.cmake:114: derive the name from RAVEN_ICU_VERSION's major part (RavenText.cmake is included before RavenContent, RavenEngine/CMakeLists.txt:34, 43). Delete the manual-edit clause in UpdateIcu.cmake.
- **Effort:** S.
- **Timing:** now.
- **Overlaps:** CQ-MEDIA-11, CQ-DATA-09.
- **Panel discussion:** none.

#### CQ-BLD-07 Every host build re-runs content staging and compares about 24 MB byte for byte

- **Severity:** low.
- **Category:** slow, redundant pass.
- **Frequency:** per host `cmake --build`, including no-op builds and every snapshot build.
- **Sources:** PLT-6.
- **Evidence:** RavenContent.cmake:137-142 is an always-out-of-date custom target; StageContent.cmake.in:6-11 compares about 72 files (fonts 13.3 MB, ICU 3.4 MB, game content 6.6 MB), reading about 48 MB (derived). :143 puts it in front of the RavenCore link. Three CONFIGURE_DEPENDS globs over vendored folders (RavenIcu.cmake:20, RavenMsdf.cmake:20, RavenYoga.cmake:14). The COMMENT at :140 is stale. No game-only build or develop launch relies on the re-run (BuildGame.cmake.in never touches RavenContent; development reads Games/<Name>/Content directly).
- **Why it matters:** estimate: 50-150 ms per no-op host build, unmeasured.
- **Fix:** per-file add_custom_command copies generated with foreach(... IN ZIP_LISTS ...), RavenContent depending on their outputs, StageContent.cmake.in deleted; move the dependency from RavenCore to RavenEngine and the tests (ARCH BLD-05); drop CONFIGURE_DEPENDS from the three vendored globs; fix the COMMENT. Measure a no-op build first.
- **Effort:** S.
- **Timing:** later.
- **Overlaps:** ARCH BLD-05, AA2-HR-01 step 5.
- **Panel discussion:** none.

#### CQ-BLD-08 Win32Window::WindowProc is 141 lines with eight near-identical mouse-button cases and an unexplained cursor asymmetry

- **Severity:** low.
- **Category:** readability, long function.
- **Sources:** PLT-7; GP answered the cursor question.
- **Evidence:** Win32Window.cpp:200-340; eight near-identical button cases (L, R, M and X, each down and up) at :259-305 (WM_MOUSEMOVE at :237 and the wheel cases at :245-246 differ). Only WM_RBUTTONDOWN seeds the cursor (:269). XBUTTON cases return TRUE (:295, 304), correct but uncommented. GetContentScale calls GetDpiForWindow per frame (:170-174) though WM_DPICHANGED is handled (:225-236). The only CursorDelta consumer, SamplesGame.cpp:89-99, skips the press frame, so nothing depends on the right-only seed.
- **Fix:** an OnMouseButton helper with a message-to-button map, where every press seeds the cursor, stated in one comment; comment the XBUTTON return; cache the DPI scale. Do not cache key names in the platform (layout dependency); if ever needed, the game caches by bindings revision. Arena's BindingRows/RowSlots pair calls GetBindings twice per row (Screens.cpp:67, 80): pass the result through, in CQ-GAME-05's snippet.
- **Effort:** S.
- **Timing:** later (before AA2-GP-10 adds devices).
- **Overlaps:** AA2-GP-10.
- **Panel discussion:** none.

#### CQ-BLD-09 Platform seam polish: a raw owning BuildProcess*, fallbacks that differ from Win32, an implicit stop-event index

- **Severity:** low.
- **Category:** readability, hidden ownership.
- **Sources:** PLT-8.
- **Evidence:** BuildProcess.hpp:10-14 returns a raw owning pointer; both callers spell the same unique_ptr (GameModule.cpp:520-521, ShaderLibrary.cpp:173-174). UnsupportedGameModule.cpp:9 returns nullptr where Win32 throws (Win32GameModule.cpp:91-97); :14 PollBuildProcess returns false forever. The fallback sleep appears twice (Win32DirectoryWatch.cpp:59-66, PollDirectoryWatch.cpp:14-21). "Handles[0] is the stop event" is implicit (Win32DirectoryWatch.cpp:21-24, 34-37, 67, 76).
- **Fix:** a BuildProcessHandle type with a deleter; the Unsupported backend throws like Win32 (with AA2-PLAT-02 step 6); a named StopEvent member or index; one shared fallback-sleep helper.
- **Effort:** S.
- **Timing:** before M6B, with AA2-PLAT-02 step 6.
- **Overlaps:** AA2-PLAT-02, AA2-HR-04.
- **Panel discussion:** none.

#### CQ-BLD-10 The Xcb and Cocoa input backends bypass InputBuffer and emit no key-repeat events

- **Severity:** low.
- **Category:** readability, duplicate.
- **Sources:** PLT-9.
- **Evidence:** Win32Window.hpp:42 uses InputBuffer; XcbWindow.hpp:42 and CocoaWindow.hpp:26 hold a bare InputState cleared at the start of PollEvents (XcbWindow.cpp:331, CocoaWindow.mm:158). XcbWindow.cpp:459-478 duplicates SetCursorPosition. XcbWindow.cpp:514-518 and CocoaInput.mm:200 never produce Repeated, so WasKeyRepeated is never true off Windows. Platform/Mac files use four-space indentation.
- **Fix:** add to the ARCH-PLAT-05 handoff line: hold an InputBuffer and Publish; use SetCursorPosition; call RepeatKey on a repeat press; re-indent Platform/Mac in the same M4 commit. No Windows change.
- **Effort:** S.
- **Timing:** M4.
- **Overlaps:** ARCH-PLAT-05, AA2-GP-10.
- **Panel discussion:** none.

#### CQ-BLD-11 Stale, misplaced and restating comments, and small dead settings

- **Severity:** low.
- **Category:** slop, stale comment.
- **Sources:** PLT-10, HOST-7 item 4.
- **Evidence:** RavenSources.cmake:1-2 mentions selecting "the sample". RavenBuildSettings.cmake:67 option text mentions "sample" builds (contract input). Compare-Captures.py:89 names the wrong project path (reads Games/Samples/Content/Project.ravenproject at :38). Windows3DSmoke.cpp:42 comment sits above the wrong function. RavenMsdf.cmake:4 and UpdateYoga.cmake:5 grammar. RavenPlatform.cmake:17 defines _UNICODE on Apple with no consumer. RavenEngine.cpp:205-207 `#pragma warning(suppress : 4996)` is redundant: raven_apply_warnings defines _CRT_SECURE_NO_WARNINGS (RavenBuildSettings.cmake:57-58) for this target (RavenEngine/CMakeLists.txt:61-63); it is the only #pragma warning in the engine. Window.hpp:49, 73-74 blank and empty sections. RavenPlayer.cmake:18-24 walks two lists by index where ZIP_LISTS exists.
- **Fix:** one private commit for all non-hashed items; RavenBuildSettings.cmake:67 and Window.hpp in the Phase 3 hash-only batch.
- **Effort:** S.
- **Timing:** now; hashed items Phase 3.
- **Overlaps:** AA2-CORE-03, AA2-PLAT-01, CQ-X-04.
- **Panel discussion:** none.

#### CQ-BLD-12 The Vulkan SDK and DXC version is pinned only in CI and differs from the reference laptop

- **Severity:** low.
- **Category:** readability, hidden ordering.
- **Sources:** BLD-12 (new in round 2).
- **Evidence:** .github/workflows/windows.yml:36-41 uses 1.4.309.0; the laptop's CMakeCache.txt:403 points at 1.4.357.0. CMakeLists.txt:18 `find_package(Vulkan REQUIRED)` has no version, although the renderer needs Vulkan 1.2+ features. No Windows SDK prerequisite version is recorded.
- **Why it matters:** CI validates shaders with a different DXC than the measured builds, and any SPIR-V test must not assume identical bytes (CQ-VK-03 step 5).
- **Fix:** a version floor with a one-line reason in find_package; record the tested SDK in docs/EngineModules.md prerequisites; align CI and laptop on the next update; keep SPIR-V tests semantic.
- **Effort:** S.
- **Timing:** later (next CI or SDK touch).
- **Overlaps:** AA2-BLD-01.
- **Panel discussion:** none.

### 5.9 GAME: games, tests and benchmarks

#### CQ-GAME-01 Arena dereferences TryGet results unchecked; a data edit ends the host instead of skipping one system

- **Severity:** high.
- **Category:** correctness, FD-06 violation (a reachable access violation).
- **Frequency:** one-time crash: the process dies on the first fixed step after a scene without the craft's Transform or Mover loads.
- **Sources:** GTB-1; reachability verified by DATA, CORE and GP.
- **Evidence:**
  - Run.cpp:24 `scene.TryGet<const ECS::TransformComponent>(craft)->Position`; :34 and :37 dereference the Transform and the Mover. Cells.cpp:45, Drones.cpp:47 and Craft.cpp:78 dereference the Transform (Craft.cpp:72-74 checks the Mover first, then dereferences the Transform unchecked). Run.cpp:33 dereferences CraftComponent safely: First<CraftComponent> in the same system proves it exists, and Commands are deferred.
  - The craft comes from First<CraftComponent> (ArenaSystems.hpp:67-77), which proves only CraftComponent exists. SceneAccess::TryGet is noexcept and returns null when the component is absent (SceneAccess.hpp:68-78).
  - Nothing adds a Transform or a Mover automatically: CreateEntity/Adopt create a bare node (Scene.cpp:159-231); SceneFile Instantiate (SceneFile.cpp:876, component loop at :912-970) adds only stored rows and reports and skips rows it cannot add; `RavenSceneTool apply` does FromText then Save with no validation (RavenSceneTool.cpp:132-142); the world pass treats a Transform-less entity as a normal state (Scene.cpp:521-549). Removing the craft's Mover is equally legal.
  - RunPhase catches only C++ exceptions (GameModule.cpp:377-404). No /EHa, SEH translation or __except exists in the build files (git grep outside ThirdParty for EHa, /EH, _set_se_translator and __except: none), and game code is compiled with /EHsc (docs/EngineModules.md:523), so a null dereference is an access violation that ends the host.
  - Contrasts: Samples and Courtyard use `scene.Read(entity, value)` returning bool (SamplesGame.cpp:81-84; CourtyardGame.cpp:98, 171-173), and the engine's own character backend reports a Mover/Transform mismatch as a problem instead of crashing (CharacterMovers.cpp:262).
- **Why it matters:** this violates FD-06, "Bad data never ends a session" (docs/DataModelDesign.md:18, :61). An ordinary designer edit crashes the editor host, bypassing last-good recovery. Arena is the reference game, so the pattern spreads. AA2-RT-01 (the Render stage outside the boundary) is an analogous crash path, not a duplicate.
- **Fix:**
  1. Now, game-only: skip and log. Either plain null checks at the six sites, or a small `std::optional<CraftState> FindCraft(const SceneAccess&)` in ArenaSystems.hpp/.cpp built on `View<const CraftComponent, const TransformComponent>`, plus a variant that also requires MoverComponent (used by RunSystem in Run.cpp and HitSystem in Craft.cpp). CraftState must carry the ECS::Entity, because Run.cpp:36 and :40 write the Transform and the Mover back to the craft. The helper is optional; do not over-abstract it. A system that finds no craft returns, logging once. This recovers as soon as the data is fixed, and matches FD-06 tier 1 (a placeholder plus one log line).
  2. Port Run.cpp and Cells.cpp (one snippet), then Drones.cpp and Craft.cpp (second snippet).
  3. One sentence in ArenaSystems.hpp: read optional components through Read/TryGet with a check, never `TryGet(...)->`; for data-dependent reads, skip and log.
  4. Phase 3 additions, for true invariants only: `SceneAccess::Get<const T>(Entity)` that throws a DataError naming the entity's ref, label and TypeName, through an exported out-of-line `[[noreturn]] ThrowMissing`, so the game header gains no includes. DataError does not exist yet (git grep DataError in RavenEngine: none), so this step also creates it, derived from std::exception so RunPhase's catch reports its message. A throwing system is suspended until the next module activation (the flag is cleared only at GameModule.cpp:475-477; the log says "Suspended until the next module reload"), so a designer who fixes the scene with dump/apply, a scene reload or Shift+F8 does not get the system back without a code reload. Therefore Get is not the pattern for data-dependent reads, unless it is paired with resetting Suspended on scene load. Document beside TryGet: TryGet means "may be absent"; Get means "absence is an invariant violation".
  5. No tracked test; check by hand with dump/apply removing the craft's Mover, and separately its Transform (Run, Cells, Drones and Hit all dereference it).
- **Effort:** S (step 4 adds the DataError type, counted in Phase 3).
- **Timing:** steps 1-3 now (game-only, Phase 1 batch); step 4 Phase 3.
- **Overlaps:** AA2-RT-01 (analogous crash path), CQ-API-03.
- **Panel discussion:** API thought Get unnecessary because Read->bool and view joins already exist. The lead adopted CORE's Get as delegated. Verification showed Get's suspension outlives a data fix, so skip-and-log is the documented pattern for data-dependent reads and Get is reserved for invariants (delegated choice 9).
- **Verification:** confirmed with corrections; severity high -> high because this is a reachable crash in the reference game that bypasses last-good recovery and is likely to be copied.

#### CQ-GAME-02 The "new best" rule is computed twice and has drifted: a tie shows "a new best!" without raising the best score

- **Severity:** medium.
- **Category:** slop, duplicate.
- **Sources:** GTB-2.
- **Evidence:** Craft.cpp:88-90 computes `best = run->Score > profile.BestScore` and raises BestScore and run->Best only on a strict win; Run.cpp:46 seeds run->Best from the profile at run start. Screens.cpp:177 computes `model.NewBest = model.Over && run->Score > 0 && run->Score >= run->Best`, and ArenaUi.cpp:101-102 shows "Score {} - a new best!" from it (HudModel::NewBest already exists, ArenaUi.hpp:39). Craft.cpp:96-97 logs "(best N)" for the same tie. Cells.cpp:42 adds score only while Playing, and grep for run->Best/NewBest finds no other writer, so nothing changes Score or Best between the hit and the HUD. On a tie (Score equal to the previous best, Score > 0) the banner says "a new best!", the profile still saves (Runs is incremented, Set/Save run), but BestScore does not change and the log says it is not a best. The >= exists because Craft.cpp:90 overwrites run->Best with Score on a real win; the ArenaComponents.hpp:112 comment ("raised when the run beats it") is the only hint that the two rules depend on each other.
- **Why it matters:** a player-visible mismatch in the reference game.
- **Fix (one snippet of two or three files, together with AA2-GP-05 step 3):** AA2-GP-05 step 3 makes HitSystem only set a RunOver result, moves the profile write to an Update system, and makes the best score enter the run at scene start, so the decision cannot sit in HitSystem. Choose one approach explicitly:
  - (recommended) Once AA2-GP-05 step 3 stops overwriting run->Best, keep run->Best as the start-of-run best and use one inline `IsNewBest(run)` helper (`Score > Best`) in ArenaSystems.hpp for both the log/profile path and HudSystem. No component field.
  - Or add `bool NewBest = false;` to ArenaRunComponent and its RAVEN_COMPONENT list (ArenaComponents.hpp:108-120, list at :119), set by the Update profile writer and read by HudSystem (field migration handles it).
- **Effort:** S.
- **Timing:** Phase 1 with AA2-GP-05 step 3.
- **Overlaps:** AA2-GP-05, CQ-GAME-11.
- **Panel discussion:** none.
- **Verification:** confirmed with corrections; severity medium -> medium because it is a real, player-visible drift in the reference game.

#### CQ-GAME-03 The 50k benchmark's 20% animation runs through the Samples puzzle system

- **Severity:** medium.
- **Category:** slow, hidden ordering.
- **Frequency:** per rotor per frame (10k at 50k, 20k at 100k).
- **Sources:** GTB-3; RND confirmed the budget definition.
- **Evidence:**
  - SamplesTool.cpp:87-92: one pivot in five gets a rotator with no switch (Rotator(pivot, {}, ...) leaves Switch null, SampleSceneBuilder.cpp:111-121). Interaction.cpp:88-108 animates them inside UpdateInteraction with three std::remainder calls per rotor (:98-99).
  - `ContinuousRotationSpeed = 1.0f` (Interaction.cpp:95) is not dead: it is the hot-reload benchmark's edit fixture. scripts/Benchmark-Windows3D.py:211-213 requires exactly one occurrence (otherwise it fails with "The benchmark's compatible speed edit moved"), :223 rewrites it to 2.0f to measure live reload, and :236-242 checks that the rotor rate doubled through the benchmark_phase that TraceSystem publishes (SamplesGame.cpp:248-252). InteractionSpeed (:22) is the documented DLL-edit knob (docs/InteractionGame.md:78, which names a stale file, InteractionGame.cpp). Interaction.cpp is also a fixture for scripts/Test-Windows3D.ps1:66-70 (`return collected == total;`) and Test-Windows3DLive.py:218, 379-381 (the pickup rule).
  - Hidden preconditions: an early return without AttemptComponent (Interaction.cpp:60-62), which exists only because SampleSceneBuilder::Finish creates `$attempt` (SampleSceneBuilder.cpp:190-191); Space toggles Paused (:74-75), which gates rotors (:88); the system runs only in free flight (SamplesGame.cpp:199). The door logic reads rotor->Progress in the same frame (Interaction.cpp:113-117).
  - UpdateInteraction is 101 lines (Interaction.cpp:57-157), up to five levels deep, seven jobs.
  - The CPU preparation measure is work_ns minus waits (Benchmark-Windows3D.py:147-148, 154), so it includes Update-phase systems (Application.cpp:576-581; GpuDrivenDesign.md:219).
  - Measured: the Interaction system at 22.4 us per frame (ECSDesign.md:293) on the 800-pivot workload (ECSDesign.md:256), so 28 ns per rotor derived as an upper bound (the time also covers the switch and door views). Derived 0.28 ms at 50k (GpuDrivenDesign.md:27 estimates about 0.4 ms). No later per-system record exists; M6A shrank the component, so 28 ns is likely an upper bound (unverified).
  - TraceSystem calls Find("benchmark-rotor-0") every frame (SamplesGame.cpp:248-252): one std::string build (17 characters, one heap allocation past MSVC's SSO, estimated) plus one unordered_map lookup (SceneAccess.cpp:20-23; Scene.cpp:233-237), about 0.1 us per frame (estimate), negligible.
- **Why it matters:** an innocent puzzle edit, or pressing Space during a run, silently stops the goal-4 workload and changes recorded numbers. The loop is about 8-11% of the 3.5 ms budget (derived).
- **Fix:**
  1. Move rotor logic into Rotators.cpp as its own RotatorSystem that reads Paused only if AttemptComponent exists, registered before the door logic (order < 20) so doors keep reading this frame's Progress. Keep a named speed constant in Rotators.cpp, and in the same snippet move the benchmark script's path and edit rule (Benchmark-Windows3D.py:211-213, 223) to it. Move the Test-Windows3D.ps1 and Test-Windows3DLive.py fixtures with whichever Interaction.cpp code they target. This step owns the Rotators.cpp split; CQ-GAME-06 step 2 splits only the rest of Interaction.cpp.
  2. Wrap each axis only when |v| > pi. remainder(x, 2pi) returns x exactly for |x| <= pi, so output is bit-identical.
  3. Split the rest with CQ-GAME-06.
  4. Read the system's time at 50k from frames.systems.csv before and after; record the A/B in GpuDrivenDesign.md.
- **Effort:** S.
- **Timing:** Phase 2 (before the B4 baseline).
- **Overlaps:** AA2-PERF-04, AA2-PERF-07, AA2-CORE-01 step 4, CQ-API-11, CQ-GAME-06.
- **Panel discussion:** none. Verification dropped the proposed step 5 (cache the rotor's EntityRef): it adds game-side state to save about 0.1 us, and a cached EntityRef still needs a Resolve each frame.
- **Verification:** confirmed with corrections; severity medium -> medium because the goal-4 workload is coupled to puzzle code; the claim that ContinuousRotationSpeed is a no-op was refuted (it is the reload benchmark's edit fixture).

#### CQ-GAME-04 Small helpers are copied across three games and two tools; the builders' Register<> lists are dead

- **Severity:** low (revised from medium in verification).
- **Category:** slop, duplicate.
- **Sources:** GTB-4 (its First/Single part moved to CQ-API-05); verified by DATA.
- **Evidence:** First<T> three times (ArenaSystems.hpp:67-77, SamplesGame.cpp:52-62, CourtyardGame.cpp:46-56). ActiveCamera twice (SamplesGame.cpp:40-50, CourtyardGame.cpp:58-68). Frame delta clamp three times (ArenaSystems.hpp:45-49, SamplesGame.cpp:29-33, CourtyardGame.cpp:28-31). Drift: Samples applies the frame clamp inside a FixedUpdate system (PatrolSystem, SamplesGame.cpp:178-182), and so do Courtyard's FixedUpdate systems MoveSprites and Player (registered at CourtyardGame.cpp:233-234; using the clamped Delta at :78, :100), while Arena uses StepSeconds (ArenaSystems.hpp:40-43). In FixedUpdate, DeltaSeconds is the fixed step (Application.cpp:498, 505; FixedGameplayStep.hpp:80, 165, default 1/60 s), so the 0.1 s clamp is a no-op unless a project sets a rate below 10 Hz. KeyStep twice (SamplesGame.cpp:69-72, CourtyardGame.cpp:33-36); Pose twice (SamplesTool.cpp:29, ArenaTool.cpp:152-159); builder Mesh twice, drifted (SampleSceneBuilder.cpp:47, ArenaTool.cpp:176). Register lists at SampleSceneBuilder.cpp:15-26 (constructor :12-27) and ArenaTool.cpp:166-172 (constructor :164-173): typed Add registers on first use (Registry.hpp:229-234), Registry::GetSchemas sorts by name (Registry.cpp:652-663), Capture skips empty pools (SceneFile.cpp:850-851) and Normalize sorts again before writing (:491-492, 574-575). grep over Games/*/Tools finds no AddComponent, FindSchema, GetSchemas, scene load or Apply; the tools only build and CaptureScene (ArenaTool.cpp:203, SampleSceneBuilder.cpp:223), so the Register comment's rationale ("files can name types that no entity has yet") does not apply and the calls change nothing.
- **Why it matters:** polish; no realistic bug at the default 60 Hz step.
- **Fix:**
  1. Tools now: delete both Register lists. Verify without regenerating tracked scenes for a byte-compare, which cannot work: each Scene seeds its ID sequence from OS random bytes (Scene.cpp:132-135) and the tools never call SeedIds (grep: only GameSession.cpp:237, 323), so every regeneration changes every entity ID. Compare `RavenSceneTool dump` output with the IDs masked, or seed IDs in an ignored probe. Do not commit regenerated scenes (the churned IDs add diff noise).
  2. PatrolSystem uses the fixed step's seconds, with CQ-GAME-06. Courtyard's MoveSprites and Player follow with Courtyard's split (AA2-GP-02 step 2 or B5), under no-throwaway.
  3. After Phase 3 (CQ-API-05, CQ-API-12): delete the First and ActiveCamera copies in each game. Leave Pose and Mesh per tool (8 lines, no shared tools library).
- **Effort:** S.
- **Timing:** step 1 now; step 2 with CQ-GAME-06 (Courtyard later); step 3 after Phase 3.
- **Overlaps:** AA2-ECS-08, AA2-PERF-09, CQ-API-05, CQ-API-12, CQ-GAME-06.
- **Panel discussion:** none.
- **Verification:** confirmed with corrections; severity medium -> low because the drift cannot change behavior at the default step and the rest is polish.

#### CQ-GAME-05 Arena action names are spelled in three places

- **Severity:** medium.
- **Category:** readability, duplicate.
- **Sources:** GTB-5.
- **Evidence:** ArenaGame.cpp:45-50 registers "MoveX", "MoveY", "Jump", "Turn", "Pause", "Resume"; ArenaSystems.hpp:32-37 builds ActionIds from a second copy; Screens.cpp:58-61 BindingTargets is a third. ActionId hashes a string_view and ActionDesc::Name is one (InputMap.hpp:28-31, 78-86). Positional booleans: ArenaGame.cpp:44, ArenaUi.cpp:181.
- **Why it matters:** a typo or rename fails silently: GetAxis returns 0, the bindings row is empty, Rebind targets nothing.
- **Fix:** constexpr string_view names in ArenaSystems.hpp used by all three sites (two snippets); designated initializers for the two aggregates; pass GetBindings results through in the BindingRows/RowSlots pair (Screens.cpp:67 in RowSlots, :80 in BindingRows; a trivial cost, once per row while the bindings screen draws; CQ-BLD-08).
- **Effort:** S.
- **Timing:** now.
- **Overlaps:** CQ-X-01.
- **Panel discussion:** none.
- **Verification:** confirmed with corrections; severity medium -> medium because Arena is the reference game new games copy and a mismatch fails silently (low would also be reasonable).

#### CQ-GAME-06 Samples and Courtyard break the one-file-per-object rule and use a different input style

- **Severity:** medium.
- **Category:** readability, long function.
- **Sources:** GTB-6 (Arena step withdrawn).
- **Evidence:** SamplesGame.cpp:24-253 (the anonymous namespace) holds 7 systems (FreeFly, OrbitCamera, MeshRotation, Patrol, Interaction, Demo, Trace) and 6 helpers (Delta, FreeFlight, ActiveCamera, First, MovementAxis, KeyStep). DemoSystem (:205-245) does four jobs: the Space animate toggle (:210-211), the O/F1 sort-order toggle (:212-221), the F6 mesh swap (:224-233) and the orbit animation (:234-244). Interaction.cpp:57-157 is one 101-line UpdateInteraction covering the announcement, pickups (through UpdatePickups, :27-54), the Space pause, switches (:76-87), rotators (:90-108), doors (:109-123), goals (:125-145) and completion (:146-156). CourtyardGame.cpp:23-215 holds all four systems; PlayerSystem (:92-145) uses a decltype(tag) lambda (:117-127, used at :128 and :134); the hard-coded ±3.0f bounds at :83-85 belong to MoveSpritesSystem (:76-89), the courier's bounce. Space has two owners split by mode (DemoSystem's Animate toggle only matters in orbit mode, :234; Interaction.cpp:74-75 runs only in free flight, SamplesGame.cpp:199-200; the mode is fixed per process by --orbit, :37), a readability problem, not a live key collision. Raw keys in Samples and Courtyard versus mapped actions in Arena (Craft.cpp:23, 29). Delta, First<Tag>, ActiveCamera and KeyStep are copied verbatim between SamplesGame.cpp:30-33/52-62/40-50/69-72 and CourtyardGame.cpp:28-31/46-56/58-68/33-36 (CQ-GAME-04). Arena's Screens.cpp layout is the recorded owner-requested split (GameServicesDesign.md:957; commit 52c034b), not a violation. AGENTS.md states the one-file rule for game code in general.
- **Why it matters:** the owner asked for this layout "from the first version", and AA2-GP-12 is about to edit the same DemoSystem code.
- **Fix:**
  1. First snippet: a shared Samples helper header (for example SampleSystems.hpp, as Arena has ArenaSystems.hpp) for Delta, FreeFlight, ActiveCamera, First, MovementAxis and KeyStep, so the split files do not copy them again.
  2. Land AA2-GP-12 step 1's port to mapped actions (Space, O, F1 and F6) as its own snippet first; it also settles the two Space owners. Then the pure moves, so each diff either changes behavior or moves code, never both.
  3. Samples: FreeFly.cpp, OrbitCamera.cpp, Demo.cpp, Patrol.cpp; keep the 4-line benchmark TraceSystem with the code it observes (or one Benchmark.cpp), not a file of its own, per AGENTS.md's warning against excessive splitting. SamplesGame.cpp only registers.
  4. Interaction.cpp (after CQ-GAME-03 step 1 moves the rotators): Switches, Doors, Pickups, Goals, Attempt (one or two files per snippet). Keep the implicit order inside UpdateInteraction as explicit priorities: readyToExit is computed from pickups (:73) before the switches toggle, the switch toggles come before the rotators, then the doors, then the goals; all share the Paused and Completed gates, and each new system needs the FreeFlight check. Measure before and after, because the rotators carry the 50k benchmark animation (CQ-GAME-03).
  5. Courtyard: Player.cpp, Courier.cpp, Camera.cpp, Order.cpp with a Touches<T>() template and the bounce bounds on VelocityComponent or a courier component, only with the owner-requested 2D run or B5. AA2-GP-02 step 2 already covers Courtyard's PlayerSystem port to Actions; only the file split is new here.
- **Effort:** M.
- **Timing:** Samples in Phase 1 with AA2-GP-12 (after its action port), except the Interaction.cpp split (step 4), which waits for CQ-GAME-03 step 1 in Phase 2; Courtyard with the 2D run or B5.
- **Overlaps:** AA2-GP-12 (step 1), AA2-GP-02, AA2-CORE-01, AA2-DOC-06, CQ-GAME-03, CQ-GAME-04.
- **Panel discussion:** whether the owner wants the one-file rule applied to Arena's screens is owner question Q4.
- **Verification:** confirmed with corrections; severity medium -> medium because the owner asked for this layout and AA2-GP-12 edits the same code, though no behavior bug follows from the layout (low would also be defensible).

#### CQ-GAME-07 Tuning and math constants baked into game code

- **Severity:** low.
- **Category:** slop, magic number.
- **Sources:** GTB-7.
- **Evidence:** DroneComponent::Speed = 4.0f (ArenaComponents.hpp:48) is always overwritten (Drones.cpp:32). Code-only tuning at Drones.cpp:16, 26-28, 49, 66-67; ArenaSystems.cpp:33-35 (1.5 m repeated from Drones.cpp:66); Craft.cpp:77, 82; Cells.cpp:19-24. Unnamed literal at Camera.cpp:24 (100.0f = 10 m squared; the guard itself has a why-comment at :23). Unexplained guards at Cells.cpp:42 and Craft.cpp:70 (they protect restart from pending deferred Destroys). 2π spelled three ways across 12 sites. PatrolSystem uses std::cos/sin/sqrt in FixedUpdate (SamplesGame.cpp:186-191) against GameServicesDesign.md:367.
- **Fix:** move drone speeds, hunt grace and restart time onto ArenaRulesComponent with migration defaults; name the 1.5 m margin and the camera's 10 m snap distance once; why-comments at the two Cells/Craft guards; a TwoPi constant in Raven/Math in Phase 3; port PatrolSystem to SimulationMath with CQ-GAME-06.
- **Effort:** S.
- **Timing:** Arena items now; TwoPi Phase 3; Samples with CQ-GAME-06.
- **Overlaps:** CQ-X-07.
- **Panel discussion:** none.

#### CQ-GAME-08 Test and benchmark headers describe finished rewrites; test include paths climb out of the tree

- **Severity:** low.
- **Category:** slop, stale comment.
- **Sources:** GTB-8; BLD corrected the count.
- **Evidence:** RegistryTests.cpp:1-2 (guards the finished M1 rewrite); SceneFileTests.cpp:1-3 (M6-6 history); EcsBenchmark.cpp:1-2, 28-29, 262. The local AGENTS.md lists TestWorld.hpp and "RegistryTests (cpu) and HierarchyTests (gpu)", while there are 7 test files, an empty Tests/Gpu and only Check.hpp in Support. 10 `../RavenEngine/Private/` includes in 5 files (FixedStepTests, UserDataTests, InputMappingTests, PhysicsTests, Benchmarks/PhysicsBenchmark).
- **Fix:**
  1. Rewrite each header to name the contract guarded now and the upcoming change it protects against (for example RegistryTests: field tables and migration for AA2-ECS-01 and B2).
  2. Update the local AGENTS.md testing paragraph (local-only file).
  3. Add the Private include directory in raven_add_test_executable (Tests/CMakeLists.txt:20-28), as RavenSceneTool does, and drop the prefixes (two snippets).
  4. Phase 3: a RAVEN_TEST_API alias of RAVEN_API in Raven/Core/Api.hpp to mark test-only exports among the 60 in Private headers.
- **Effort:** S.
- **Timing:** steps 1-3 now; step 4 Phase 3.
- **Overlaps:** AA2-DOC-06, AA2-CORE-03, CQ-X-09.
- **Panel discussion:** none.

#### CQ-GAME-09 Benchmarks duplicate timing and statistics, mislabel a column, and pack 16 order-dependent cases into one 238-line function

- **Severity:** low.
- **Category:** readability, long function.
- **Sources:** GTB-9.
- **Evidence:** EcsBenchmark.cpp:51-65 duplicates :68-84. PhysicsBenchmark.cpp:76-85 and :173-179 repeat mean and p95. PhysicsBenchmark.cpp:262 prints "first_step_ms" but :79, 266 write the maximum of 300 steps. EcsBenchmark.cpp:92-329 RunSize: 16 cases on one registry; churn (:265-300) changes dense order before reparent and migration; read+write times a GetEntities allocation (:139). Cube mesh in PhysicsTests.cpp:283-291 and PhysicsBenchmark.cpp:194-198.
- **Fix:** Tests/Support/Timing.hpp (Median, Summary{Mean, P95, Max}); rename the column to max_ms or record the first step; split RunSize into per-group functions over a fixture built per group, recording before/after numbers once in ECSDesign.md; move CubeMesh to Tests/Support.
- **Effort:** S.
- **Timing:** Phase 2, before the AA2-PERF-04 cases land.
- **Overlaps:** AA2-PERF-04, AA2-PERF-07, CQ-API-07.
- **Panel discussion:** none.

#### CQ-GAME-10 Content asset IDs repeated as raw string literals across tools

- **Severity:** low.
- **Category:** slop, magic number.
- **Sources:** GTB-10.
- **Evidence:** texture ID "7a590cee-..." at SamplesTool.cpp:78, :159 and SampleSceneBuilder.cpp:198, while SamplesTool.cpp:25-27 names its other IDs. ArenaTool.cpp:135-149 clip UUIDs live only inside sound text, apart from the WAV names (:95-132), and a third time in Project.ravenproject:8-14. ArenaTool.cpp:3-4 says the clips are registered by the tool; it never writes that file.
- **Fix:** one named AssetId in SampleSceneBuilder.hpp; a {file, clip id, synth, peak} table in ArenaTool driving WAVs and sound bodies; fix the header comment.
- **Effort:** S.
- **Timing:** later (next tool touch).
- **Overlaps:** none.
- **Panel discussion:** none.

#### CQ-GAME-11 Arena's walls can likely be crossed, and an escaped craft falls forever with unbounded speed; the run never ends

- **Severity:** medium (derived, not run).
- **Category:** correctness, gameplay defect (a missing fall-out and a missing terminal speed).
- **Frequency:** per run in which the player leaves the arena; per fixed step afterwards.
- **Sources:** GTB-11 (new in round 2).
- **Evidence:** ArenaTool.cpp:216-217: the floor is centred at z -0.25 and 0.5 m tall (top at z = 0), 29 m wide (±14.5 m). :226-230: walls are centred at z 0.6 and 1.2 m tall (top at 1.2 m), 0.5 m thick at ±14.75 m (inner face 14.5, outer 15.0); the north and south walls run to x = ±15.25 (length 2*half+2.5 at :227-228). Mesh() adds a static box collider by default (:175-177), so the walls block. No floor or other collider lies outside them. The deck at :238 spans x 11.5-14.0 with its top at 1.1 m, leaving a 0.5 m gap to the east wall. JumpSpeed 6 m/s (ArenaComponents.hpp:29) is written into DesiredVelocity.Z (Craft.cpp:57), CharacterMovers adds it on the ground (:506-507) with gravity 9.81 (:29): apex 36/19.62 = 1.83 m (derived). The craft stays above 1.2 m from about 0.25 s to 0.97 s, about 5 m horizontally at Speed 7 (derived), against about 1.4 m needed (0.5 m wall plus the 0.9 m capsule diameter, Radius 0.45 at ArenaTool.cpp:264). The escape is likely easier than a jump: from the deck the wall top is only 0.1 m higher, the 0.5 m gap is narrower than the capsule, and mWalkStairsStepUp is 0.4 m (CharacterMovers.cpp:489), so walking east off the deck may step straight onto the wall top (derived, not run). In the air, CharacterMovers.cpp:509-511 keeps vertical speed and adds gravity at :511 with no clamp. The run ends only through HitSystem (Craft.cpp:81); grep `RunState::Over|Position.Z <|Kill|Fall|FallOut` in Games/Arena/Source finds only that site plus the readers at Run.cpp:67 and Screens.cpp:176. Drones cannot end it: they are gravity movers (Drones.cpp:25-30) that steer only in XY (:71-73). The pause menu offers only Resume and Bindings (ArenaUi.cpp:115-118), so the player has no in-game restart.
- **Why it matters:** a player-visible soft-lock in the reference game, recoverable only by developer keys. The unbounded fall speed has no consequence today (the CharacterGrid margin only matters near other characters, and none follow the craft), but coordinates grow without bound.
- **Fix:**
  1. Verify by hand first, in a Debug run: walk east off the deck onto the wall top, then jump from the deck toward the east wall, before and after the fix.
  2. Game: end the run below a named FallOutZ (a new ArenaRulesComponent field) through one shared EndRun or RunOver-result helper, which AA2-GP-05 step 3 introduces when HitSystem stops owning the Over path (today score, best and profile save live in Craft.cpp:81-97). A second copy of the Over path would add a third site for the new-best rule (CQ-GAME-02). In swarm mode HitSystem never ends a run (Craft.cpp:70; Run.cpp:53 logs "no game over"), so a fall-out there respawns the craft at CraftComponent::Spawn, as Run.cpp:33-40 does.
  3. Owner question Q2: fall-out ends the run (recommended in either case), plus an optional choice of wall height.
  4. Separately, an independent engine improvement that does not fix the soft-lock: the TerminalSpeed clamp (CQ-GP-12 step 2), in CharacterMovers.cpp, which can land without waiting for Q2.
- **Effort:** S.
- **Timing:** step 1 first (confirm the derivation); step 2 in Phase 1 with AA2-GP-05 step 3, after the Q2 answer; step 4 any time.
- **Overlaps:** CQ-GP-12, CQ-GAME-02, AA2-GP-05.
- **Panel discussion:** listed as unverified in section 10 until step 1 runs.
- **Verification:** confirmed with corrections; severity medium -> medium because it is a player-visible soft-lock in the reference game that has not been run, not a goal-scale hot path.

## 6. Cross-cutting patterns

#### CQ-X-01 Duplicated tables and rules, some already diverged

- **Severity:** high (as a pattern; individual findings carry their own ratings, and this entry carries no separate fix-plan work).
- **Category:** slop, duplicate.
- **Sites, already diverged (the copies disagree in behavior today):**
  - Field matching: CQ-DATA-02 (Registry.cpp:516-529 last-match-wins vs SceneFile.cpp:797-817 name-then-alias; the type comparison twice at Registry.cpp:494-498 and SceneFile.cpp:810-811).
  - Asset-dependency scans: CQ-DATA-01 (GameSession.cpp:64-82 keeps the first tag through std::map::emplace and relies on UseAsset's kind check, while PlayerContent.cpp:65-69 throws on any catalog mismatch; the extent-product width difference cannot be observed, because SceneFile.cpp:78-84 rejects an overflowing product first).
  - Physics state copies: CQ-GP-01 (PhysicsWorld.cpp:779-789 copies every field in Build; :944-950 leaves out HasRigidBody, and the rebuild test at :865-867 does not compare it).
  - Body-limit reporting: CQ-GP-06 (once per world vs once per mover, against Physics.md:57).
  - Pipeline create-infos: CQ-VK-01 (style differences only, no behavior change today).
  - "New best" rule: CQ-GAME-02 (Craft.cpp:88 `>` vs Screens.cpp:177 `>=`).
  - Active-camera rule: CQ-API-12.
  - Harness folder lists: CQ-BLD-01 (Test-Windows3DLive.py:511 has no Content; Benchmark-Windows3D.py:187 does).
  - File-size caps: CQ-DATA-09 (a sound event is read at 64 MiB through AssetStore.cpp:26 and at 1 MiB in PlayerContent.cpp:118).
  - Developer key lists: CQ-CORE-07.
- **Sites, duplicated but not yet diverged:** shader-program groups (CQ-RND-01: seven code sites plus the ShaderProgram enum and the RavenContent.cmake:50-66 rows, which AA2-HR-01 step 5 tracks; consistent today, the hazard is the next engine program); vertex and uniform layouts (CQ-VK-04); HLSL declarations (CQ-RND-10); sound limits and buses (CQ-MEDIA-05); text alignment (CQ-MEDIA-07); Arena action names (CQ-GAME-05); the ICU data name (CQ-BLD-06); field structs (CQ-API-01).
- **Fix (one rule for all, rule R1):** one table or one function, then convert callers in snippets of one to three files, landed before the milestone step that adds the next row (B2 for materials, B4 for programs and pipelines, B5 for sprite programs). This is work inside the listed findings; it has no fix-plan row of its own, so it is not double-counted. Where the copies already disagree, the unifying snippet states which behavior wins and why. The open choices this implies: the sound-event size cap (1 MiB vs 64 MiB, CQ-DATA-09; delegated); GameSession's silent first-tag-wins vs PlayerContent's throw (CQ-DATA-01 step 2 rejects a second tag in both; delegated); and a bare collider's friction (Jolt's 0.2 vs the component default 0.5, CQ-GP-01), which is designer-visible and therefore owner question Q1.
- **Overlaps:** AA2-HR-01 (program table), AA2-ECS-04 and AA2-ECS-01 (field matching and the asset-ref helper), AA2-GP-01 and AA2-PERF-02 step 2 (the PhysicsWorld mirror).
- **Verification:** confirmed with corrections; severity high -> high because drifted copies have already produced real divergence (friction, the tie banner, the broken live-check snapshot, the staging cap), and B2, B4 and B5 each add a row to several of these tables.

#### CQ-X-02 Two hash families are mixed: persisted values and in-memory digests use the same hand-written FNV, and the persisted uses are not named

- **Severity:** low (revised from medium in verification).
- **Category:** readability, hidden invariant.
- **Sources:** HOST-3, DATA-10, MEDIA-10; freeze list from DATA.
- **Evidence:**
  - FNV-1a 64 written out at five sites: Application.cpp:202-204, GameModule.cpp:34-57, ReloadService.cpp:31-40, TextSystem.cpp:50-59, and UiContext.cpp:22-30 and :158-161, beside the canonical ECS::Detail::HashText/HashNumber (Component.hpp:117-139; grep for the FNV constants). TextSystem's Hash(text, seed) equals HashText(text, HashOffset ^ seed), and UiContext's Mix(hash, text) equals HashText(text, hash), so reusing HashText keeps their values.
  - Persisted or compared against stored data (must never change):
    1. The AssetRef field Tag = HashText(AssetTypeTraits<T>::Name) (Component.hpp:207), stored in every scene and profile field descriptor (SceneFile.cpp:857, 1010) and compared on load (:810-811). The two Tag-to-AssetKind tables built from it (GameSession.cpp:60-63, PlayerContent.cpp:74-77) are consumers too (CQ-DATA-01 step 6). A change drops every stored texture, mesh and sound reference.
    2. The .ravendata checksum (UserData.cpp:26-29, 79-80, 326; duplicated in RavenSceneTool.cpp:52).
    3. User-data key IDs = HashText(key) (UserData.cpp:113-119); a mismatch makes the record unreadable (:174-178).
  - Not persisted: TypeId = HashText(TypeName). Scene files store the type name (SceneFile.cpp:604, :644), and the loader recomputes HashText(component.Type) at :915 with the same function; UserData.cpp:213 is an in-memory FindSchema(info.Id). TypeId is compiled into the game DLL, and the contract hash already forces a rebuild when it changes; it only has to match the loader's function, which step 5 addresses.
  - Informational only: LayoutHash is written to scenes (SceneFile.cpp:605, 644, 855) but only read, never compared on load (:172, :728). The determinism hash is compared within one process (Application.cpp:218-227); 09c983c4ab39958e is only a doc baseline (GameServicesDesign.md:957).
  - In-memory, free to change: GameModule DLL stamps, UI IDs, text cache keys, ActionId and sound names (compiled into game DLLs but not persisted; bindings are stored by name, InputBindingsFile.cpp:31, 58). ReloadService input hashes are in-memory today, but ReloadService.hpp:42-43 says the input hashes plus ImporterVersion are "what a future derived-data cache would store", so any persisted key must include a digest version.
  - Component.hpp:120 already says "64-bit FNV-1a; stable across compilers, configurations and module generations". The gap is that it does not name the persisted uses or say "file format".
  - The engine's stable hash lives in ECS::Detail but serves Sound.hpp:46, InputMap.hpp:29, UserData and the tools. RavenSceneTool.cpp:40-57 also re-implements the user-data header layout (UserData.cpp:20-22, 74-78), and has drifted: RavenSceneTool.cpp:46-55 checks only the magic and the checksum, while UserData.cpp:76-78 also rejects any Version other than 1, so the tool would decode a future-version payload.
  - AA2-HR-04 step 5 plans a faster in-house hash in ReloadService and GameModule, neither of which calls HashText.
- **Why it matters:** the persisted set is a file-format invariant documented only as "stable". A "faster hash" edit to HashText would need someone to edit a contract-hashed public header against that comment; the duplicates are slop either way.
- **Fix:**
  1. Now: Private/Core/ContentDigest.hpp (ContentDigest(span), ContentDigestFile(path), HashCombine). Point ReloadService.cpp:31-40 and GameModule.cpp:34-57 at it, and also the determinism capture hash (Application.cpp:202-204), which digests whole-scene byte buffers on every fixed step during the check and is in-memory (only the doc baseline moves). Fix the ReloadService.hpp:49 comment and say that any persisted key must include a digest version. AA2-HR-04 step 5 then changes only this body.
  2. Now: TextSystem.cpp and UiContext.cpp call ECS::Detail::HashText (identical values); delete Hash and Mix; the snippet states that UI IDs and text cache keys are not persisted. The layout-key combine (`key * 31`) moves to HashCombine with AA2-MEDIA-09.
  3. Now: expose UnwrapUserData(span) from Private/Core/UserData.hpp with RAVEN_API (RavenSceneTool links the RavenCore shared library, RavenEngine/CMakeLists.txt:39; the existing pattern is UserData.hpp:28), including the Version check, and use it in RavenSceneTool.cpp:42-57.
  4. Phase 3 format group: public Raven/Core/StableHash.hpp with StableHash(text) and StableHash(number, seed), commented "file-format function; never change", naming the three persisted uses. Component.hpp includes it (values unchanged). Update the call sites, including the two Tag tables. A new public header changes the contract hash, so it waits for the window; steps 1-3 do not wait for it.
  5. Phase 3: Registry::FindSchemaByName owns the HashText(name) shortcut used at SceneFile.cpp:915.
- **Effort:** S.
- **Timing:** steps 1-3 now (before AA2-HR-04 step 5); steps 4-5 Phase 3.
- **Overlaps:** AA2-HR-04 step 5, AA2-ECS-01, AA2-ECS-08 step 3, AA2-MEDIA-09, CQ-DATA-01.
- **Panel discussion:** CORE and GP proposed low, since AA2-HR-04 step 5 names only non-HashText sites and the determinism hash is not persisted. DATA kept medium because the freeze list includes the AssetRef Tag and key IDs. The lead decided medium; verification returned it to CORE's and GP's low.
- **Verification:** overstated; severity medium -> low because TypeId is not persisted, Component.hpp:120 already marks the function stable, and AA2-HR-04 step 5 touches no HashText caller, so the hazard needs an edit against an existing comment.

#### CQ-X-03 Small helpers re-implemented per file, some with different behavior

- **Severity:** medium.
- **Category:** slop, duplicate.
- **Sites, drifted:** VkResult wrappers (CQ-VK-02: the device-lost mapping differs at one reachable site); IndexOf/NoSlot/lifecycle prologue x4 (CQ-GP-04: the scene-identity check differs); the frame clamp per game (CQ-GAME-04: applied to a fixed step in Samples and Courtyard); Report functions and body-limit texts (CQ-GP-06); hit test (CQ-MEDIA-08); finite/affine checks x5 (CQ-RND-09: skip vs throw).
- **Sites, pure duplicates:** memory type, barrier, image view, command pool (CQ-VK-07); First<T> and ActiveCamera per game (CQ-GAME-04); Euler order x4 (CQ-DATA-12, CQ-API-14); FNV at five sites (CQ-X-02); Windows.h defines in six files plus one guard (CQ-BLD-04); vendored version readers and replace loops (CQ-BLD-05); harness helpers (CQ-BLD-01); hex decode (CQ-DATA-07); lowercase extension x3, behaviorally identical (CQ-DATA-01); FindAsset-then-TryUseAsset x5 (CQ-CORE-16).
- **Fix:** one shared helper per subsystem in the subsystem's existing helper header (or one new small header), converted file by file. Windows.h settings go into the Windows backend's compile definitions in cmake/RavenPlatform.cmake (or one Platform/Windows header), never a core helper header. When the copies differ, the shared helper adopts the correct behavior and the snippet says which copy was wrong. No-throwaway applies: helpers whose only callers a B-step deletes (the CPU textured and sprite parts of VulkanClearPass, Collision2D-era code) are deleted with that step, not unified now. As in CQ-X-01, land each helper before the step that adds the next copy.
- **Verification:** confirmed with corrections; severity medium -> medium because some copies already disagree (device-lost mapping, the scene check, the fixed-step clamp); the pure duplicates are listed separately.

#### CQ-X-04 Stale or misleading comments, some claiming capabilities or costs the code lacks

- **Severity:** medium (earned by CQ-RND-03 and CQ-RND-07; most other sites are low).
- **Category:** slop, misleading comment.
- **Sites claiming a capability or cost:** RenderWorld.hpp:19-20 cost model (CQ-RND-07); VulkanContext.cpp:424-426, 430-432 "pipeline creation checks" (CQ-RND-03); VulkanContext.cpp:702-704 "visibility complete before acquisition" (CQ-RND-02); GameModule.hpp:29 "Development-only" (CQ-CORE-07); JobSystem.cpp:13 unmeasured timing (CQ-CORE-07); Component.hpp:364 "declaration order" (CQ-API-02); GameSession.cpp:332-333 "logged once" (CQ-CORE-16); Physics.md:57 "logged once" (CQ-GP-06).
- **Sites that are stale, misplaced or wrong-path:** FragmentShaderInterface.hpp:8 "textured" (CQ-VK-03); three developer-key lists (CQ-CORE-07); AssetStore.hpp:73 "Legacy AssetResources" (CQ-API-07); Sound.hpp:49 "later (A4)" and AudioPlayer.hpp:25 (CQ-MEDIA-06); ArenaTool.cpp:3-4 (CQ-GAME-10); test headers naming finished rewrites (CQ-GAME-08); RavenSources.cmake:1-2 "the sample" wording and Compare-Captures.py:89 wrong path (CQ-BLD-11); PhysicsWorld.cpp:457 misplaced (CQ-GP-05); VulkanContext.hpp:119-122 camera-overload comment (CQ-VK-12).
- **Fix:** every snippet that changes behavior updates the comment that describes it in the same commit (rule R3 in section 7). Fix the existing sites with their findings. Public-header comment fixes (RenderWorld.hpp, GameModule.hpp, Sound.hpp, Component.hpp) go in the Phase 1 † commit; AssetStore.hpp:73 is the exception and goes with the Phase 3 deletions group (CQ-API-07, AA2-CORE-04), which wins because it deletes the sentence with the code it describes.
- **Note:** history narration is nearly absent everywhere (one runtime log string, VulkanContext.cpp:233; the two test headers in CQ-GAME-08). A grep for milestone and "now we/previously" patterns in Private, Public and Games comments finds only explanatory references. That is a real strength.
- **Verification:** confirmed with corrections; severity medium -> medium, earned by CQ-RND-03 (an unchecked last-good path described as checked) and CQ-RND-07 (the cost model B4 planners read); retitled because several sites are stale rather than claims.

#### CQ-X-05 Long multi-job functions; split each with its next edit

- **Severity:** medium.
- **Category:** readability, long function.
- **Sites that M6B B-steps edit next:** Application::Run 444 lines, Gameplay lambda 68 (CQ-CORE-05); PhysicsWorld::Step 189 (CQ-GP-05); CheckFragmentShaderInterface 184 (CQ-VK-03); VulkanClearPass::Record 165, constructor 107 (CQ-RND-06); GameModule Watch 153 (CQ-CORE-02); TextSystem Shape 111, LayoutShaped 104 (CQ-MEDIA-08); SceneFile Instantiate 113 (CQ-DATA-07); GameSession::Render 99 (CQ-CORE-11); UiContext::Draw 96 (CQ-MEDIA-08).
- **Other long functions (not B-step targets; the RenderingContentDesign.md B4/B5 rows at :211-212 do not name them):** EcsBenchmark::RunSize 238 (CQ-GAME-09); Test-Windows3DLive.py live() about 234 (CQ-BLD-01); SelectVulkanPhysicalDevice 153 (CQ-VK-09); Win32Window::WindowProc 141 (CQ-BLD-08); SceneFile::FromText 114 (CQ-DATA-07); Samples UpdateInteraction 101 (CQ-GAME-03, CQ-GAME-06). All lengths were re-measured by brace matching in verification.
- **Fix:** extract only parts that no scheduled step deletes, and do the split in the same commit series as the fix that next touches the function (named in each finding's Timing). Never split a function that a B-step deletes. VulkanClearPass::Record and its constructor are named explicitly: split only the parts B5 keeps, and only in the steps CQ-RND-06 lists (its steps 1-3 touch parts that survive B5); do not split the textured batches B5 rewrites.
- **Verification:** confirmed with corrections; severity medium -> medium because Application::Run at 444 lines sits on the frame path; retitled because several listed functions are not M6B targets.

#### CQ-X-06 Small per-frame and per-step allocations and lookups

- **Severity:** low.
- **Category:** slow, hot-path allocation.
- **Sites:** preview names and InputState copies (CQ-CORE-09); SceneAccess::Find string (CQ-API-11); UI strings, Yoga tree and config (CQ-MEDIA-02); glyph lookups (CQ-MEDIA-03); per-record heap vectors (CQ-GP-11); per-step physics vectors and sorts (CQ-GP-07); interpolator hash lookups (CQ-DATA-05); TryGet chains (CQ-API-03); RingOffsets::Uploads (CQ-RND-05); log formatting (CQ-CORE-10); GetRecentProblems string copies (AA2-PERF-09).
- **Fix:** track these as one inventory appended to AA2-PERF-09, not as separate tickets. Only CQ-DATA-05 scales with goal-sized counts at the AA2-PERF-04 game scale; CQ-API-03 matters only at a hypothetical RTS scale and CQ-MEDIA-02 only at editor scale. The rest are fixed opportunistically when their file is touched.

#### CQ-X-07 Designer-visible tuning written as unnamed literals in code

- **Severity:** low.
- **Category:** readability, magic number.
- **Sites:** mover step-up 0.4 m, stick-to-floor 0.5 m, grid margin, terminal speed (CQ-GP-12); bare-collider friction 0.2 vs 0.5 (CQ-GP-01); UI hover, press and focus-ring values (CQ-MEDIA-11); drone speeds, grace and restart times (CQ-GAME-07); sound limits and bus list (CQ-MEDIA-05).
- **Fix:** name each with a one-line reason now. Promote to data only when a game asks (that is a scope question for the owner at that time).

#### CQ-X-08 Public-header fixes join AA2's one-commit-per-phase batches

- **Severity:** low (revised from medium in verification; a sequencing note, not a ranked fix).
- **Category:** readability, process.
- **Sources:** lead pattern P9; batch list owned by API.
- **Mechanism:** AA2 section 7.1 already defines the Phase 1 † commit (ArchitectureAssessment-2026-11-26.md:2219) and the Phase 3 contract window (:443, 469, 539). GameContract.cmake:6-16 hashes every Public .hpp plus four CMake files byte for byte (:28-33), so a comment or whitespace edit changes the contract, and GameModule.cpp:437-438 then rejects the DLL ("Game contract changed: rebuild and restart the host"). Each contract change needs a host reconfigure and rebuild, a host restart, and a rebuild of every game module (EngineModules.md:536). This entry lists only the CQ items that join those batches; it is developer-time sequencing, not part of the 1.5 s reload loop, and has no effort of its own.
- **Phase 1 † commit** (comments and must-fix-now items only): RenderWorld.hpp:19-20 (CQ-RND-07); GameModule.hpp:29 (CQ-CORE-07, on its own: AA2-HR-05 is not a † item in AA2 section 7.1); Sound.hpp:49 (CQ-MEDIA-06); Component.hpp:364 message and RequireDefaults (CQ-API-02, CQ-API-06); Overlay.hpp offset asserts (CQ-VK-04 step 1); SceneLighting.hpp:25 (CQ-VK-10, with AA2-RT-01 step 1, which is a † item); InputStream.hpp static_assert and loop count (CQ-GP-11, with AA2-GP-04 step 2, which is a † item).
- **Phase 3 window**, in commit order:
  1. Whitespace only: Entity.hpp, Registry.hpp, SceneAccess.hpp, Window.hpp (CQ-API-08); RavenBuildSettings.cmake:67 text (CQ-BLD-11).
  2. Deletions: AA2-CORE-04 steps 1-3 plus CQ-DATA-08's closure and CQ-VK-12's camera-type closure; AssetStore.hpp:73; value-deduced Has/Remove with the RegistryTests port; Scene's virtual destructor, protected member and <limits> (CQ-API-07); ECS::GetCameraView (CQ-API-14).
  3. Tier moves, per AA2-CORE-03 steps 3-4: RenderWorld.hpp, VulkanContext.hpp and GameModule.hpp move to the new RavenEngine/Host/Raven root, a PUBLIC include directory of RavenCore outside the contract-hashed Public tree (matching CQ-CORE-08 step 2). VulkanTexturedDraw.hpp and VulkanTransparentDraw.hpp stay in Public: B5 deletes them in the commit that already changes the contract (no-throwaway), and the moved VulkanContext.hpp can keep including them by their Public path. The moved headers are included by RavenCore .cpp files, Private headers (RenderWorldState.hpp, GameSession.hpp, VulkanDrawQueue.hpp, VulkanClearPass.hpp), each other and scripts/Windows3DSmoke.cpp; no file under Games/ or Tests/ includes them. VulkanContext.hpp alone has 7 includers, so split each move into one or two includer groups per snippet with the move last (the one-to-three-files rule), or accept it explicitly as a mechanical exception. GameModule::GetLastError is deleted in the GameModule.hpp move (CQ-CORE-08).
  4. Format and structs: AA2-ECS-01 pin (if it missed Phase 1), CQ-API-01 FieldType, CQ-X-02 StableHash.hpp, CQ-DATA-01 step 6 AssetTypeTraits::Kind, AA2-ECS-02's host-owned flag. CQ-DATA-02 must already have landed.
  5. Additions: SceneAccess Single<const T>, First<T...>, ActiveCamera, Get<const T> with DataError (CQ-API-05, CQ-API-12, CQ-GAME-01); TryNormalize/NormalizeOr/MinNormalizeLengthSquared and TwoPi (CQ-API-10, CQ-GAME-07); Input.hpp folding (CQ-API-09); Ui.hpp Edges rename and UI::Scope with AA2-MEDIA-06's UiText API (CQ-MEDIA-01, CQ-API-13); AudioBusCount derivation and any public sound limits (CQ-MEDIA-05); transparent m_Names hash with one label store (CQ-API-11, CQ-DATA-03 step 5); SceneAccess::Count documentation (CQ-API-05); RAVEN_TEST_API (CQ-GAME-08); EulerRotation (CQ-DATA-12, CQ-API-14 step 2); ImageData owning the stb allocation (CQ-DATA-06 step 3, unless AA2-CORE-04 has made it private).
  6. Renames: GetAuthoredId to GetLabel, SceneAccess::IsAlive to Owns, RecordedType, glossary (CQ-API-04).
  7. Game ports right after: delete First and ActiveCamera copies, switch read-only Single sites.
- **Out of the window on purpose:** Query and Registry::GetEntities (Phase 4 with CQ-API-03); Collision2D and PenetratesXZ (B5); Input.hpp backend split (FD-29 gamepads); AA2-CORE-04 step 4 (B5); VulkanTexturedDraw.hpp and VulkanTransparentDraw.hpp (deleted at B5).
- **Verification:** confirmed with corrections; severity medium -> low because the mechanism is AA2 section 7.1's and the entry is sequencing, not a code defect; the move destination was corrected to AA2-CORE-03's Host root and the two B5-deleted headers were removed from the moves.

#### CQ-X-09 Docs that the fixes make stale have no owner

- **Severity:** low.
- **Category:** slop, stale comment.
- **Sources:** GTB-12.
- **Sites:** Physics.md:11-12 (the bare-collider rule; no line states a friction default, so CQ-GP-01 step 3 adds a sentence rather than correcting one) and :57 (CQ-GP-06); EngineModules.md:294 (CQ-API-07); the local AGENTS.md test list (CQ-GAME-08); measurement rows that fixes must update: GameServicesDesign.md:624-630 (CQ-MEDIA-02), GpuDrivenDesign.md:218-230 (CQ-RND-04, CQ-GAME-03), ECSDesign.md:285-293 (CQ-GAME-09), UIDesign.md (CQ-RND-05), HotReloadDesign.md:326 and :349-350 (CQ-RND-13, CQ-CORE-01, CQ-BLD-02, CQ-BLD-03), DataModelDesign.md:662 (CQ-DATA-03); DataModelDesign FD-16 and ArchitectureAssessment-2026-11-26.md:127 (CQ-DATA-02); README.md:49-51 and TextureResources.md:73 (CQ-VK-12); InteractionGame.md:78's stale file name (CQ-GAME-03); HotReloadDesign.md:21 and TextureResources.md:100, 116 at the B5 rename (CQ-RND-06); AA2-RND-03 step 6's B5 draft revision (CQ-RND-02).
- **Fix:** each fix's snippet updates its doc line in the same commit; no doc sweep and no new docs. The Physics.md sentence lands now.

## 7. Proposed coding-quality rules for the team

Proposals only, for the owner to adopt into AGENTS.md. Each is tied to the findings it would have prevented.

- **R1. One fact, one place.** When a fact (a kind list, a rule, a layout, a limit, a name) appears a second time, make it a table or function first. A dispatch over a closed set throws on the unknown case instead of falling through. (CQ-X-01, CQ-RND-01, CQ-DATA-01, CQ-DATA-02, CQ-VK-01, CQ-MEDIA-05, CQ-GAME-05)
- **R2. Mirror structs copy whole.** When a struct mirrors another's state, store the source struct as one member and assign it whole; do not copy field lists. (CQ-GP-01)
- **R3. Comments change with behavior.** A snippet that changes behavior, cost or a capability updates the comment, error text and doc line that describe it in the same commit. A comment never claims a check, cost or mode the code does not have. (CQ-X-04, CQ-X-09, CQ-API-02)
- **R4. Persisted values are marked.** Every function whose output is written to a file or compared against stored data carries a "file-format; never change" comment, and lives apart from in-memory digests. (CQ-X-02)
- **R5. Check before dereferencing.** Game code never writes `TryGet(...)->`. Optional and data-dependent reads use a checked TryGet or Read and skip with one log line, so the system recovers as soon as the data is fixed. Get, which throws a DataError, is only for true invariants, because a throwing system stays suspended until the next code reload. (CQ-GAME-01)
- **R6. Share small helpers.** Before writing a result check, slot map, barrier, or per-game helper, look for the subsystem's shared one. (CQ-X-03)
- **R7. Name tuning.** Every tuning or capacity number gets a named constant with a one-line reason; designer-visible ones are listed in the subsystem doc. (CQ-X-07)
- **R8. Size functions to the next edit.** A function that the next milestone step edits and that is over about 80 lines or does more than one job is split in the same commit series as that edit, never in a separate polish pass, and never if a B-step deletes it. (CQ-X-05)
- **R9. Hot paths allocate only to grow.** Per-frame and per-step code reuses buffers (clear, keep capacity) and avoids string building and hash lookups per entity; additions are listed in the AA2-PERF-09 inventory. (CQ-X-06)
- **R10. State lock and callback contracts.** An interface whose callbacks run under a lock says so, and says which calls are forbidden from them. (CQ-CORE-03)
- **R11. Batch contract-hashed edits.** Public-header and contract-input changes go into the current phase's single batch list. (CQ-X-08)
- **R12. Scripts share one harness.** Validation scripts import shared helpers; no copied folder lists or key tables. (CQ-BLD-01)
- **R13. Name the deletions.** Every "delete with Bn" note is recorded in that step's checklist in the design doc once the step's design is settled, so the step removes everything at once and keeps the survivors. (CQ-RND-02, CQ-VK-12)
- **R14. noexcept means no throwing calls.** A noexcept function, or a callback the caller does not wrap in try, never calls a helper that throws; it uses the non-throwing form and its own reject path. (CQ-GP-13, CQ-CORE-03)
- **R15. Registration rejects ambiguity.** A table that can be read two ways (an alias chain, one old name claimed twice, an unclassified program) is rejected when it is registered, not resolved by iteration order. (CQ-DATA-02, CQ-RND-01)

## 8. Prioritized fix plan

Effort: S under a day, M a few days. "Batch" groups fixes that touch the same file into one snippet series. Severities are the verified ones. Pattern entries (CQ-X-01, CQ-X-03 to CQ-X-07, CQ-X-09) carry no rows of their own: their work is inside the listed findings.

| Priority | ID | Fix | Severity | Effort | Timing | Depends on |
|---|---|---|---|---|---|---|
| 1 | CQ-GAME-01 | Skip-and-log null checks (or FindCraft carrying the entity); port the six sites | high | S | now (Phase 1, game-only) | - |
| 2 | CQ-BLD-01 | Step 1: add Content to the live snapshot tuple; the owner runs it once on the laptop; then one shared folder-list constant | high | S | now | - |
| 3 | CQ-DATA-02 | One name-first matcher (FD-16); reject ambiguous alias tables at registration; extend the alias test | medium | S | Phase 1 | - |
| 4 | CQ-DATA-01 | AssetKinds table, ForEachAssetRef and ExpandSoundClips; convert the six tables | medium | M | Phase 1, early, before B2 | AA2-RT-05 step 3 (same GameSession function) |
| 5 | CQ-GP-01 | HasRigidBody in the rebuild test; one `Built` Desired member; one test case | medium | S | Phase 1 with AA2-GP-01 | - |
| 6 | CQ-RND-01, CQ-RND-03 | Step 1 each, one snippet: CheckProgram throws on an unclassified program, the reload loop logs and drops; correct the two comments | medium | S | now | - |
| 7 | CQ-CORE-15 | ParseUnsigned for --seed and --determinism-check | medium | S | now | - |
| 8 | CQ-GP-13 | Decompose returns false instead of calling the throwing Normalize | medium | S | now | - |
| 9 | CQ-CORE-02 | Pure restructure: build handle + RebuildPending + LastBuild, extracted members | medium | S | Phase 1, immediately before AA2-HR-03 | - |
| 10 | CQ-BLD-03, CQ-BLD-02 | Path check first, then static discovery; output tuple keys with sub-second mtimes, DLL-only verify (BuildGame.cmake.in batch) | medium | S | before M6B | - |
| 11 | CQ-GAME-02, CQ-GAME-11 | IsNewBest helper; FallOutZ through the shared EndRun result; swarm respawn (Run.cpp/Craft.cpp batch) | medium | S | Phase 1 with AA2-GP-05 step 3, after the Q2 answer | CQ-GAME-11 step 1 hand check |
| 12 | CQ-MEDIA-01 | Step 1: ContentRect with the RTL swap and the border inset | medium | S | Phase 1 | - |
| 13 | CQ-RND-13 | Shader compile phase timing; parallel dependency scan only if measured at 10% or more | medium | S | before M6B | - |
| 14 | CQ-GAME-05, CQ-BLD-08 (Screens part) | Action name constants; pass bindings through in BindingRows/RowSlots | medium | S | now | - |
| 15 | CQ-CORE-06 | Wake after unlocking plus removal of the busy flag in one snippet; re-armed settle deadline | medium | S | before M6B | - |
| 16 | CQ-RND-05 | RecordTextureUploads; one barrier call per direction for all images, N-region copies | medium | S | now | - |
| 17 | CQ-RND-02 | Steps 1-2: BuildFrameUniform; pre-acquire cull constants | low | S | now, with AA2-RT-01 step 1 | AA2-RT-01 step 1 |
| 18 | CQ-X-02 | ContentDigest (including the determinism hash), HashText reuse, exported UnwrapUserData | low | S | now, before AA2-HR-04 step 5 | - |
| 19 | CQ-CORE-11, CQ-CORE-16 | OnWorldReplaced; single TryUseAsset calls (GameSession.cpp batch) | low | S | now, before AA2-MEDIA-01 | - |
| 20 | CQ-GAME-06 | Samples helper header; split Samples after AA2-GP-12's action port | medium | M | Phase 1 with AA2-GP-12 | AA2-GP-12 step 1; CQ-GAME-03 step 1 for the Interaction.cpp part |
| 21 | CQ-GAME-03 | Rotators.cpp with the benchmark fixture moved, ordered before doors; bit-identical wrap; measure at 50k | medium | S | Phase 2 | - |
| 22 | CQ-GAME-09 | Timing.hpp, column label, split RunSize | low | S | Phase 2, before AA2-PERF-04 cases | - |
| 23 | CQ-VK-01 | GraphicsPipelineDesc; port DebugLine, Overlay, Color; lit layout co-owned by the bundles | medium | M | Phase 2, before B4 | - |
| 24 | CQ-RND-06 | Steps 1-3: (step 1 is CQ-RND-05's), barrier helpers, CreateFrameResources | medium | S per step | after CQ-RND-05 and CQ-VK-01 steps 1-4 | CQ-RND-05, CQ-VK-01 |
| 25 | CQ-VK-03 | SpirvModule, split checker, data-driven lit inputs, RAVEN_API, one CPU test | medium | M | Phase 2, before AA2-RND-01 widening | - |
| 26 | CQ-RND-03 | Step 2: Overlay kind, then DebugLine | medium | S | before B5 | CQ-VK-03 |
| 27 | CQ-RND-11, CQ-RND-04 | Baseline; layout accumulator + Allocate (VulkanFrameData batch); direct record writes with a count assertion | medium | S | before B4 GpuScene work | - |
| 28 | CQ-VK-04 | Overlay asserts (†); FrameUniformLayout before AA2-RND-01 step 3; tables with CQ-VK-01 | medium | S | Phase 1 † and now | - |
| 29 | CQ-GP-05, CQ-GP-02 | ContactTracker extraction (behavior-neutral); then deferred EndContacts | medium | M | before AA2-PERF-02 step 2, after AA2-RT-04 step 1 | CQ-GP-01 |
| 30 | CQ-GP-04 | EntitySlots + LifecycleFeed; adopt in physics and picking, EntitySlots in movers | medium | M | with AA2-PERF-02 step 2 | CQ-GP-05; replaces AA2-RT-04 step 3's hand fixes |
| 31 | CQ-GP-03 | Shape users count, RemoveAll split (with AA2-RT-04 steps 2-3); conditional Prepare at load | medium | M | steps 1-3 now; step 4 with AA2-PERF-02 step 4 | - |
| 32 | CQ-DATA-03 | CSR hierarchy, position vectors, label references | medium | M | now (steps 1-4) | - |
| 33 | CQ-BLD-05 | Version checks for Yoga and msdf; script-safe readers; vendored CMake helpers | medium | S | with AA2-BLD-02 (missing checks may land earlier) | - |
| 34 | CQ-CORE-05 | GameplayMode with preview; merge the two Reset+Snap pairs; RunFixedStep | medium | M | Phase 4 with AA2-CORE-02 steps 2-3 | AA2-CORE-02 step 2; AA2-PERF-01 before step 4 |
| 35 | CQ-DATA-05 | B4 interpolation override keyed by Entity (constraints for AA2-PERF-01 step 2) | medium | S | during B4 | CQ-GP-04 |
| 36 | CQ-X-08 | Sequencing: CQ items in AA2's † commit and Phase 3 window (no effort of its own) | low | - | Phase 1, Phase 3 | items listed in CQ-X-08 |
| 37 | CQ-CORE-01 | Single-hash rule inside AA2-HR-06 step 1 | low | S | with AA2-HR-06 step 1 | AA2-HR-03 step 2 |
| 38 | CQ-CORE-03 | Header lock contract with AA2-PERF-03 step 4; debug fatal guard | low | S | step 1 with AA2-PERF-03 step 4; step 2 before B6 | - |
| 39 | CQ-VK-06 | Upload batch; texture creation on it, with AA2-RND-04 step 3's timeline wait | low | M | with AA2-RND-04, by B3 | CQ-RND-04, CQ-RND-05 |
| 40 | CQ-API-01 | Public FieldType with a 64-bit ElementCount; TypeOf | low | S | Phase 3 | CQ-DATA-02 |
| 41 | CQ-API-03 | EachInIdOrder; port Drones; delete Query/GetEntities with their test ports | low | M | with AA2-ECS-08 step 6 (M6B) | CQ-X-08 Phase 3 |
| 42 | remaining low findings | as each finding's Timing says; notably CQ-MEDIA-02 step 2 (the Yoga leak on a throw) now | low | S | opportunistic or named step | - |

**Sequencing notes**

- **Phase 0 (now, docs only):** the Physics.md friction sentence (CQ-GP-01 step 3, CQ-X-09), CQ-GAME-08 step 2 (local AGENTS.md). CQ-CORE-03 step 1 is an engine header edit and goes with AA2-PERF-03 step 4. The B5 checklist (CQ-RND-02 step 3) waits for AA2-RND-03 step 6, after owner questions Q8 and Q9.
- **Phase 1 (review amendments):** priorities 1-12, 14, 16-20, plus the † commit list in CQ-X-08. Private items need no hash change. CQ-GP-01 lands in the same PhysicsWorld.cpp snippet series as AA2-GP-01. CQ-CORE-02 lands immediately before AA2-HR-03; CQ-CORE-01 is folded into AA2-HR-06 step 1, after AA2-HR-03 step 2. CQ-GAME-02 and CQ-GAME-11 step 2 land with AA2-GP-05 step 3 (one Run.cpp/Craft.cpp batch). CQ-DATA-01 step 2 lands after or with AA2-RT-05 step 3. CQ-RND-02 steps 1-2 land with AA2-RT-01 step 1.
- **AA2-RT-04 interplay:** AA2-RT-04 step 1 (try/catch around Step) and step 3 (hand-written SceneId checks) land now as written; CQ-GP-05 then splits the wrapped Step, and CQ-GP-04's LifecycleFeed replaces the step 3 hand fixes. CQ-GP-03 steps 1-3 share a snippet series with AA2-RT-04 steps 2-3.
- **Phase 2 (baseline before B4):** CQ-GAME-03 and CQ-GAME-09 before the AA2-PERF-04 cases, so the baseline measures the split rotor system with shared timing helpers. CQ-VK-01, CQ-VK-03, then CQ-RND-06 steps 1-3 (after CQ-RND-05), so B4 lands on the shared pipeline and parser.
- **Phase 3 (contract window at B2 start):** the ordered list in CQ-X-08.
- **Phase 4 (during M6B):** B3 needs CQ-VK-06 steps 1-2 (with AA2-RND-04). B4 takes CQ-RND-04/08/12, CQ-DATA-05, CQ-GP-04's RenderWorld adoption and CQ-VK-01 step 6. B5 takes the deletion checklist as revised in AA2-RND-03 step 6, the class rename (CQ-RND-06 step 4), Collision2D (CQ-API-07), the Public VulkanTexturedDraw.hpp and VulkanTransparentDraw.hpp deletions, and deletes CQ-CORE-12's Restore pass. B6 needs CQ-DATA-06 step 1, CQ-CORE-03 step 2, CQ-VK-11 and CQ-DATA-04 first. B7 takes CQ-DATA-06 step 4 and CQ-VK-06 step 3.
- **M4:** CQ-VK-09, CQ-BLD-10.
- **No-throwaway:** nothing above polishes VulkanTexturePipeline, VulkanTexturedMesh, VulkanDrawQueue's textured part, GatherSprites, the Restore pass, ReplaceTexturedPipelines, TransformBounds, today's interpolator Apply/Restore, DecodeFbxMesh, ParallelFor, the two B5-deleted Public draw headers or index-keyed Yoga node reuse; each is "delete with" its step or replaced by the planned design.
- **Tests:** one new test file is proposed (CQ-VK-03 step 5, because two rewrites of the checkers follow; it needs RAVEN_API on the two checkers). Two existing tests are extended (CQ-DATA-02 step 6, CQ-GP-01 step 4), each guarding behavior the next change rewrites. No other tracked test is added; CQ-BLD-03, CQ-GAME-11, CQ-GP-13 and CQ-CORE-15 are checked by hand or with ignored probes.

## 9. Decisions

### 9.1 Owner questions (4)

These are the only items that touch cost or licensing, what players or designers see, or scope. Verification added no owner question; the friction default (Q1) is the one designer-visible choice that the CQ-X-01 unification implies.

**Q1. Bare-collider friction (CQ-GP-01 step 3). A collider without a RigidBodyComponent uses Jolt's friction 0.2; the component default is 0.5.**

- Recommendation: (a) keep 0.2 and document it in Physics.md now. Revisit only if designers find it surprising.
- (a) Keep 0.2. Strengths: no change in feel; the documented determinism hash stays; zero risk in the review batch.
- (b) Use 0.5 for bare colliders. Strengths: one default for everything; matches what a designer reading the component would expect. Cost: Arena's crates on the bare floor go from combined friction 0.35 to 0.55 (derived, Jolt's sqrt combine), so pushing feels different, and the documented hash 09c983c4ab39958e likely changes (the CTest compares two runs in one process, so CI is unaffected).

**Q2. Arena fall-out (CQ-GAME-11). The craft can likely leave the arena (derived, to be confirmed by hand), most easily by stepping from the deck onto the 1.2 m east wall, and then falls forever; the run never ends.**

- Recommendation: (a) fall-out ends the run, in either case (the swarm mode respawns the craft instead). The wall height is an optional, separate choice.
- (a) End the run on fall-out. Strengths: works for any future level shape; one small game rule through the shared EndRun result (AA2-GP-05 step 3); keeps the current arena look and scene file (hash unchanged).
- (b) Also raise the walls in ArenaTool. Strengths: the player rarely leaves at all. Cost: raising the walls alone would not close the deck route (deck top 1.1 m, step-up 0.4 m, jump apex 1.83 m, all derived): they would need roughly 3 m or more near the deck, or an invisible blocker; it changes the arena's look and regenerates the scene (determinism hash changes); a later level with gaps needs (a) anyway.
- The engine's terminal-speed clamp (CQ-GP-12 step 2) is independent of this answer and recorded as delegated (choice 19).

**Q3. GPU memory sub-allocation before B3/B6 (CQ-VK-05 step 3). This adds either own code or a new vendored library (licensing).**

- Recommendation: (b) vendor VMA unmodified behind cmake/RavenVma.cmake and a Raven seam, when B3 or B6 is scheduled.
- (a) Write a small in-house block allocator. Strengths: no new license or dependency; only what Raven needs. Cost: allocator bugs are subtle, and defragmentation, budget queries and memory-type selection must be written later.
- (b) Vendor VMA (MIT). Strengths: widely used and maintained; budget tracking for AA2-RND-08; the never-edit rule is easy to follow because it is configured by macros. Cost: one more vendored library to update with a swap script.

**Q4. Should the one-file-per-object rule apply strictly to Arena's screens (CQ-GAME-06)?** Today Screens.cpp holds the pause, menu, bindings and HUD systems and ArenaUi.cpp draws all screens; this is the layout delivered as the owner's split (GameServicesDesign.md:957).

- Recommendation: (a) keep the delivered layout.
- (a) Keep it. Strengths: already reviewed shape; the drawing functions share one model and the F6 previews.
- (b) One file per screen (Hud.cpp, PauseMenu.cpp, Bindings.cpp). Strengths: applies the rule literally; each screen reviewable alone. Cost: splits shared drawing helpers across files.

### 9.2 Technical choices recommended as delegated

Each is decided by business alignment and expandability and should be recorded as delegated.

1. **Shader program groups throw on the unknown case on the reload worker; the main-thread reload loop logs and drops** (CQ-RND-01). A loud error beats a silent misroute, and a throw in Render would end the process.
2. **One GraphicsPipelineDesc realizes B4's pipeline key** instead of a pipeline class per material, **with the lit layout co-owned by each pipeline bundle** (shared_ptr) (CQ-VK-01). Matches FD-19 (shared vertex stage), keeps render state additive and stays safe under timeline retirement.
3. **Fragment interface widening lands after the shared SPIR-V parser** (CQ-VK-03). The widened checker judges designer shaders, so it must be data, not hex literals.
4. **Field matching keeps FD-16's name-first rule in one shared matcher; registration rejects alias chains, swaps and one old name claimed twice, so such renames are done in two steps** (CQ-DATA-02). A stored set can be in the old or new layout, so no ordering rule can resolve a chain safely.
5. **Hash split: public frozen StableHash, private swappable ContentDigest; the determinism hash uses ContentDigest** (CQ-X-02). Protects persisted data while freeing reload hashing to get faster.
6. **The DLL staging function hashes the copy once, inside AA2-HR-06 step 1; F5 keeps today's checks (no lock probe)** (CQ-CORE-01). Correct under /INCREMENTAL, where size and mtime are not proof; no new Platform seam for a rare manual action.
7. **GetPreviewNames gated in Application.cpp, no API change** (CQ-CORE-09). One private file, correct for both readers.
8. **Single<const T> as the non-breaking read-only form; no SingleMut** (CQ-API-05). Follows the TryGet<const T> convention and breaks no game.
9. **Data-dependent game reads skip and log; SceneAccess::Get<const T> (throwing a new DataError) is reserved for true invariants** (CQ-GAME-01). Skip-and-log recovers as soon as the data is fixed, while a throwing system stays suspended until the next code reload.
10. **Narrow renames only: GetLabel and Owns, plus a glossary** (CQ-API-04). Fixes the two misleading names for one game line.
11. **RenderWorld.hpp, VulkanContext.hpp and GameModule.hpp move to AA2-CORE-03's RavenEngine/Host/Raven root; the two B5-deleted draw headers stay in Public until B5** (CQ-X-08). No game includes them, so their comments stop costing game rebuilds, and no moved file is deleted soon after.
12. **Renderer-side glyph upload batching, one barrier call per direction across images, not a CPU page mirror** (CQ-RND-05). Smaller change, no 4 MB per page CPU copy, independent of TextSystem's placement order, shared with B6.
13. **Interpolation override keyed by ECS::Entity in a dense slot vector** (CQ-DATA-05). Slots swap-erase and persistent IDs need a hash.
14. **Log path trim at run time, not a compiler flag** (CQ-CORE-10). Avoids a contract-input edit and an undocumented flag.
15. **Static source discovery inside the reload recipe, after the recipe detects added and removed paths** (CQ-BLD-03). The duplicate check is measured cost, but only once the recipe covers the same-count swap.
16. **Every mouse press seeds the cursor** (CQ-BLD-08). Nothing depends on the right-only seed; middle and left drags need it.
17. **Drop the frame-constant varyings before Material.hlsli v2 is recorded, keeping the per-material Lit flag** (CQ-RND-12). Last point at which no external shader breaks.
18. **Audio time from the engine frame clock; pause as per-bus state** (CQ-CORE-11, CQ-MEDIA-04). One time source for AA2-MEDIA-07's pause.
19. **A named TerminalSpeed clamp for character movers** (CQ-GP-12 step 2). It changes fall feel only after about 5 s of free fall (derived), and it bounds the grid margin.
20. **One body-limit message per entity, with a hex-ID fallback** (CQ-GP-06). Logs only; players and designers do not see it.
21. **An asset ID referenced under two different tags is rejected in both dependency consumers** (CQ-DATA-01). Removes the last behavioral difference between GameSession's and PlayerContent's scans.
22. **The PNG CRC stays on every load, made faster with slicing-by-8** (CQ-DATA-06). stb_image skips CRCs, so it is the only chunk-integrity check for player content.

## 10. Disputed or unverified items, and scope

### 10.1 Decided disputes

The panel's decisions are kept as history; where verification changed the outcome, the last column says so.

| Topic | Positions | Decision and reason |
|---|---|---|
| CQ-RND-01 severity | RND high; VK, RND round 2 medium | medium: B4 adds no engine program; step 1 lands now. Verification: confirmed medium |
| CQ-VK-01 severity | VK medium; RND high | panel: high. Verification: medium (no difference changes behavior; cold path); kept as Phase 2 preparation for B4 |
| CQ-RND-02 (former RFP-2) | RND medium; GP, DATA showed the camera is validated pre-acquire | merged into the B5 checklist finding at medium. Verification: low; the checklist waits for AA2-RND-03 step 6 |
| Reload hashing | PLT-2 high, HOST-1 medium | panel: both medium, independent commits; SameMeta not used as proof (BLD). Verification: CQ-BLD-02 medium; CQ-CORE-01 low and folded into AA2-HR-06 step 1 |
| Hash family | CORE, GP low; DATA medium | panel: medium. Verification: low (TypeId is not persisted; a stability comment exists) |
| VkResult scope | sweep vs convert when touched | panel: five re-implementations now. Verification: low; only one reachable device-lost site, so the conversions ride other snippets |
| PenetratesXZ | delete in Phase 3 vs move at B5 | move and delete once at B5 |
| GetPreviewNames | API accessors; panel item API-12 key gate; MEDIA generation cache | CORE's refined gate (pressed or previewing): the clamp at Application.cpp:440 acts only while previewing |
| CQ-DATA-02 severity | API medium; DATA high | panel: high. Verification: medium; the proposed alias-first priority was replaced by FD-16's name-first rule plus registration-time rejection |
| CQ-GAME-01 engine accessor | CORE add Get; API not needed | panel: add Get in Phase 3. Verification: Get only for invariants; skip-and-log is the pattern for data-dependent reads (delegated choice 9) |
| CQ-MEDIA-04, CQ-GP-02, CQ-DATA-04, CQ-VK-05, CQ-VK-11 | owners medium; GAME low | low: no content near the scale; cheap steps kept |
| CQ-GP-01 severity | GP high; GAME medium | medium, consistent with the AA2-GP-01 precedent; still first batch. Verification: confirmed medium |
| CQ-API-02 constexpr check | DATA feasible behind a guard; API not feasible | message fix required; constexpr check only after an ignored probe |
| Single shape | AA2-ECS-08 SingleMut; API non-breaking | non-breaking Single<const T> |
| GTB-6 Arena screens | violation vs recorded split | recorded owner split; owner question Q4 only |
| Fix-plan order of CQ-RND-05 and CQ-RND-06 | panel placed CQ-RND-06 (which reuses CQ-RND-05 step 1) ahead of CQ-RND-05 | Verification: CQ-RND-05 moved ahead, with no dependency on CQ-RND-04 |

### 10.2 Unverified or derived (still to check)

- **CQ-GAME-11**: wall clearance and the deck step-up route are derived from geometry, jump speed and the 0.4 m step-up; not run. Check by hand before acting (its step 1).
- **CQ-BLD-01**: that Content is the only folder the live snapshot lacks is by reading; the owner's step 1 run confirms it.
- **CQ-GP-13**: plausible, not reproduced; check with an ignored probe matrix.
- **CQ-RND-13**: the 20-40 ms dependency-scan cost is an estimate until its step 1 measures it.
- **CQ-RND-04, CQ-VK-07**: the upload ring's memory type is not queried on the laptop (CQ-VK-07 step 1 logs it); CQ-RND-04's saving does not depend on it. It is unverified whether Plan's copy counts toward the 3.24-3.42 ms "CPU preparation" p95.
- **CQ-VK-05**: device limits use spec minimums; not queried.
- **CQ-GAME-03**: 0.28 ms per frame at 50k is derived from one M1-step-6 measurement (an upper bound); re-measure from frames.systems.csv.
- **CQ-BLD-02, CQ-CORE-01**: savings derived from the H5 Samples trace (pre-M6A, pre-PCH; the Samples DLL and PDB sizes were not recorded). The 25-40 ms pre-build share in CQ-BLD-02 has no measured split.
- **CQ-BLD-03**: the same-count swap regression is derived by reading; the manual check is part of its step 4.
- **CQ-RND-05, CQ-DATA-03, CQ-DATA-05, CQ-DATA-06, CQ-API-03, CQ-MEDIA-02, CQ-VK-06 scale figures**: estimates, as labeled.
- **CQ-CORE-14**: C4275 risk from a class-level export is not built.
- **CQ-BLD-04, CQ-BLD-05 step 4**: removing per-file defines and rerouting the two audio targets must pass the Debug and Release warnings-as-errors build.
- **CQ-GP-03 step 4**: broad-phase widening over a session is plausible but unmeasured.
- **CQ-GP-08**: contact-callback counts are not recorded.
- **CQ-DATA-01**: AssetCatalog.cpp:32-47 as a sixth table comes from the completeness critic and was not separately verified.

### 10.3 Out of scope and coverage

- **ThirdParty/\*\***: never edited and not reviewed for quality. Only Raven's seams and settings around it are in scope (CQ-BLD-05, CQ-BLD-06).
- **Architecture findings** owned by docs/ArchitectureAssessment-2026-11-26.md (AA2-*). This document adds code-level detail where it overlaps and repeats none of them (for example the O(total) render-world sync is AA2-PERF-02; the texture full-queue wait is AA2-RND-04; the job pool spin is AA2-PERF-05; the host-owned SceneCommands gap is AA2-ECS-02).
- **Blind spots the panel did not cover**, to assign in a later round: an exception-type inventory per subsystem (CORE), RavenCore's own build time per heavy TU (BLD), HLSL code beyond declarations and the cull/varying notes (RND), InputBindingsFile and the UserData format paths beyond the encode note (CORE, GP), a cross-area inventory of logging on per-pick and per-migration paths (CORE), and RenderWorld.cpp readability beyond its slot map (RND, with CQ-GP-04). The lock audit was partly covered (CQ-CORE-03 for the reload service; MEDIA found audio and text threads clean apart from CQ-MEDIA-12), and the scene hierarchy by AA2-ECS-02 and CQ-DATA-12.
- **Completeness critic's coverage gaps** (method: git ls-files without ThirdParty, line counts joined with how often each file's name appears in the draft; every file of 235 lines or more is cited at least once):
  - Never cited, sampled with no finding: Private/Platform/Windows/Win32Input.cpp (231 lines; clean scan-code tables shared both ways through GetNativeKeyName), Private/Input/KeyNames.cpp (85), Private/Audio/Backends/MiniaudioDevice.cpp (133; a good seam), Private/Physics/JoltSupport.cpp (70), Private/Core/FrameClock.hpp, Private/Core/ReloadTrace.hpp, Private/Platform/InputBuffer.cpp (65; Publish copies InputState once per frame and the Events vector keeps its capacity).
  - Never cited, now covered by new findings: Public/Raven/Renderer/CameraView.hpp, Public/Raven/Components/CameraComponent.hpp (CQ-API-14), PerspectiveCamera.hpp and OrthographicCamera.hpp (CQ-VK-12), cmake/CompileShader.cmake (CQ-RND-13), and the host argument parser (RavenEngine.cpp:36-43, 131-147; CQ-CORE-15).
  - GameSession.cpp:95-430 was uncited (constructor, Prepare, QuickSave/QuickLoad, MakeContext, the SetUi resolver, both PlaySound overloads, GatherSprites); CQ-CORE-16 now covers the asset-lookup copies there.
  - ShaderLibrary.cpp Import (:156-195) was cited only for locking and ordering, never for latency; now CQ-RND-13.
  - AssetCatalog.cpp (127 lines) holds a sixth hand-kept asset-kind list (:32-47), added to CQ-DATA-01 step 4. Small cost: Register builds `identity = record.Id.ToString()` (:31) for every asset at project load, even when nothing fails (not a finding).
  - Uncited platform duplicate: Win32DirectoryWatch.cpp:59-65 and PollDirectoryWatch.cpp:14-21 hold the same stop-aware sleep fallback (already in CQ-BLD-09's fix as one shared helper; the 63-directory fallback is AA2's).
  - Lightly reviewed: Platform/Linux/XcbWindow.cpp (537 lines, 1 citation), Platform/Mac/CocoaInput.mm (276) and CocoaWindow.mm (222); unverified on hardware and following Windows by project rule.
  - Tracked tests not reviewed one by one: Tests/HierarchyTests.cpp (215), FixedStepTests.cpp (185), UserDataTests.cpp (185), InputMappingTests.cpp (222); only their headers and include style are covered (CQ-GAME-08).
  - Build and config files never cited, nothing found: CMakePresets.json (171; clean inheritance), cmake/RavenBuildEnvironment.cmake (35), cmake/RavenFreeType.cmake, cmake/RavenHarfBuzz.cmake, scripts/Windows3DHighRefresh.py (72), .vscode/*.json.
  - Uncited but out of scope by rule: Private/Math/Collision2D.cpp (moves to Courtyard at B5, CQ-API-07), VulkanTexturePipeline.cpp and Texture.vert/frag.hlsl (delete with B5), FbxMesh.cpp FindMeshNode's one-mesh rule (rewritten by B3; the `result.Indices.assign` copy at :155 dies with it).
  - InputRouter::Route's per-frame path (InputRouter.cpp:285-358) was sampled: two O(261) code loops, one InputState copy and about 14 WasDeveloperPressed string lookups per frame (Application.cpp:415-473); all trivial (microseconds, estimate), no finding beyond CQ-GP-09.

### 10.4 Verification pass results

Verdicts from the adversarial verifiers (51 findings), the citation checkers' new defect and the critic's six proposals. "Panel" is the severity before verification.

| ID | Verdict | Severity (panel -> final) | Main correction |
|---|---|---|---|
| CQ-VK-01 | confirmed with corrections | high -> medium | The four copies differ in style, not behavior; the shared lit layout must be co-owned by retained bundles, not the factory |
| CQ-DATA-01 | confirmed with corrections | high -> medium | Failure is loud at the player's scene load; clip expansion is parallel logic; added ExpandSoundClips, the duplicate-ID policy and AA2-RT-05 sequencing |
| CQ-DATA-02 | confirmed with corrections | high -> medium | The proposed alias-first priority reversed FD-16 and corrupted data; replaced by name-first matching plus registration-time rejection |
| CQ-BLD-01 | confirmed with corrections | high -> high | Folder tuple at :511; Content is the only missing folder by reading; the owner runs step 1 |
| CQ-GAME-01 | confirmed with corrections | high -> high | Correctness (FD-06), one-time crash; Get suspends until a code reload, so skip-and-log is the pattern |
| CQ-X-01 | confirmed with corrections | high -> high | Sites split into diverged and not-yet-diverged; no separate fix-plan row |
| CQ-RND-01 | confirmed with corrections | medium -> medium | Throw only in CheckProgram (worker); the reload loop logs and drops; four lit fragments use the default today |
| CQ-RND-02 | overstated | medium -> low | Deleting the call gives a blank lit scene, caught on the first run; the B5 list waits for AA2-RND-03 step 6 |
| CQ-RND-03 | confirmed | medium -> medium | Validation layers log but do not reject; prioritize the Overlay kind |
| CQ-RND-04 | confirmed with corrections | medium -> medium | Saving re-estimated at 0.03-0.13 ms; baseline first; count assertion with CQ-RND-11 |
| CQ-RND-05 | confirmed with corrections | medium -> medium | Upload site :608, 16-byte alignment; one barrier call per direction across images; moved ahead of CQ-RND-06 |
| CQ-RND-06 | confirmed with corrections | medium -> medium | 6 spans plus a 5-span LitShaders struct; depth attachments join CreateFrameResources; effort S per step |
| CQ-VK-02 | confirmed with corrections | medium -> low | Only vkCreateSwapchainKHR can return device lost; nothing catches the type; message-only effect |
| CQ-VK-03 | confirmed with corrections | medium -> medium | The fragment loop whitelists while parsing; the test needs RAVEN_API; leave the textured widths |
| CQ-VK-04 | confirmed | medium -> medium | VkFormat is never cross-checked; FrameUniformLayout before AA2-RND-01 step 3; UNORM maps to float |
| CQ-VK-06 | overstated | medium -> low | Mesh path shares its pool and has no fence; texture half is AA2-RND-04; do not merge the index-driven bounds pass |
| CQ-CORE-01 | overstated | medium -> low | Same defect as AA2-HR-06 (low); fold into its step 1; no lock probe for F5 |
| CQ-CORE-02 | confirmed with corrections | medium -> medium | A four-value enum cannot represent live combinations; model handle + RebuildPending + LastBuild |
| CQ-CORE-03 | overstated | medium -> low | Rule documented in HotReloadDesign.md:30; AA2-PERF-03 step 4 owns the header comment; guard is a debug fatal check |
| CQ-CORE-04 | confirmed with corrections | medium -> low | Runs once at exit; lists are adjacent; step 2 is AA2-CORE-02 step 4 |
| CQ-CORE-05 | confirmed with corrections | medium -> medium | Only two Reset+Snap pairs; preview is a mode; nesting and capture counts corrected |
| CQ-CORE-06 | confirmed with corrections | medium -> medium | 108 matched files, about 5.4k stats/s only during imports; probe from outside; wake after unlocking |
| CQ-DATA-03 | confirmed with corrections | medium -> medium | About 2.2M hash operations (derived); label store keyed by string_view |
| CQ-DATA-05 | confirmed with corrections | medium -> medium | About 5.7-6.5 lookups per entity per frame; filed as constraints for AA2-PERF-01 step 2 |
| CQ-DATA-06 | confirmed with corrections | medium -> low | Content PNGs are tiny; keep the CRC (stb skips it) with slicing-by-8; ImageData change in the contract window |
| CQ-DATA-10 | overstated (duplicate) | medium -> dropped | Duplicate of AA2-ECS-02; one evidence addendum kept below |
| CQ-GP-01 | confirmed with corrections | medium -> medium | Line :945; member named `Built`; Desired must move before Entry can hold it; WriteBack also changes |
| CQ-GP-03 | overstated | medium -> medium | none_of short-circuits (about 2 compares today); 0.39 ms is ApplyTicks and 32 ms is the full build; conditional Prepare |
| CQ-GP-04 | confirmed with corrections | medium -> medium | RenderWorld is a fourth copy of reconcile and swap-remove; movers adopt EntitySlots only |
| CQ-GP-05 | confirmed with corrections | medium -> medium | Eight phases including the simulation call; EndedIds belongs to CQ-GP-02; order with AA2-RT-04 step 1 |
| CQ-GP-06 | confirmed with corrections | medium -> low | The copies agree; only body-limit reporting differs; keep one RotatedTranslatedShape |
| CQ-MEDIA-01 | confirmed with corrections | medium -> medium | Latent (no game uses Ui::Image); the image also overpaints the border; rename with AA2-MEDIA-06 |
| CQ-MEDIA-02 | overstated | medium -> low | 0.3 ms budget covers HUD plus pause menu; allocation share is small; RAII guard instead of node reuse |
| CQ-MEDIA-05 | confirmed with corrections | medium -> low | Limits have not drifted in a way that matters; worse case is an enum reorder |
| CQ-API-01 | overstated | medium -> low | No planned change needs four struct edits; ElementCount must be 64-bit; lighter TypeOf form |
| CQ-API-03 | overstated | medium -> low | About 9 us per step today; Run.cpp loops run once per run start; restates AA2-ECS-08 step 6 |
| CQ-BLD-02 | confirmed with corrections | medium -> medium | PDB hashed in Debug only; pre-build share unmeasured; sub-second mtimes; stamp format change rebuilds once |
| CQ-BLD-03 | confirmed with corrections | medium -> medium | The child glob check covers a same-count swap; the recipe's path check must land first |
| CQ-BLD-04 | confirmed with corrections | medium -> low | Six full copies plus one guard; failure is a compile error in one TU |
| CQ-BLD-05 | confirmed with corrections | medium -> medium | The Yoga and msdf pins are never checked; readers must be script-safe; ICU copies its .dat separately |
| CQ-GAME-02 | confirmed with corrections | medium -> medium | The profile saves on a tie; the decision moves out of HitSystem with AA2-GP-05 step 3 |
| CQ-GAME-03 | confirmed with corrections | medium -> medium | ContinuousRotationSpeed is the reload benchmark's edit fixture; rotators ordered before doors; cache step dropped |
| CQ-GAME-04 | confirmed with corrections | medium -> low | Clamp drift is a no-op at 60 Hz; regenerated scenes cannot be byte-compared |
| CQ-GAME-05 | confirmed with corrections | medium -> medium | GetBindings calls at Screens.cpp:67, :80 |
| CQ-GAME-06 | confirmed with corrections | medium -> medium | Shared Samples helper header first; Space owners split by mode; AA2-GP-12's action port lands first |
| CQ-GAME-11 | confirmed with corrections | medium -> medium | Correctness; the deck step-up route is likely easier; fall-out through the shared EndRun; swarm respawns |
| CQ-X-02 | overstated | medium -> low | TypeId is not persisted; a stability comment exists; determinism hash moves to ContentDigest |
| CQ-X-03 | confirmed with corrections | medium -> medium | Drifted and pure-duplicate families separated; no-throwaway caveat added |
| CQ-X-04 | confirmed with corrections | medium -> medium | Retitled; AssetStore.hpp:73 goes with the Phase 3 deletions |
| CQ-X-05 | confirmed with corrections | medium -> medium | Retitled; M6B targets separated; VulkanClearPass parts B5 keeps named explicitly |
| CQ-X-08 | confirmed with corrections | medium -> low | Destination is AA2-CORE-03's Host root; the two B5-deleted headers stay; sequencing note only |
| CQ-GP-13 (citation checker) | plausible, not reproduced | new -> medium | noexcept Decompose calls the throwing Normalize |
| CQ-CORE-15 (CRIT-1) | confirmed | new -> medium | --seed parses into a signed value |
| CQ-CORE-16 (CRIT-2) | confirmed with corrections | new (proposed medium) -> low | A wrong kind in a scene file is rejected at load; the sound-event path is silent, not spamming |
| CQ-RND-13 (CRIT-3) | confirmed with corrections | new -> medium | The first-edit outlier is not the scan; measure first |
| CQ-API-14 (CRIT-4) | confirmed | new -> low | Check that SamplesGame still gets CameraView.hpp |
| CQ-VK-12 (CRIT-6) | confirmed with corrections | new -> low | Keep the surviving ordering sentences; change only camera-overload doc sentences now |
| CRIT-5 | refuted (duplicate) | new -> not added | Duplicate of CQ-CORE-11 step 1 |

**Refuted or dropped items**

- **CQ-DATA-10** (generic SceneCommands Add/RemoveComponent can change ParentComponent behind Scene's back): dropped. AA2-ECS-02 (ArchitectureAssessment-2026-11-26.md:519-540) cites the same SceneCommands.hpp:35-48, SceneCommands.cpp:48-62 and 142-153, and SceneAccess.cpp:13-17, and its fix steps 1-3 and timing are this finding's. Its proposed Private/Scene/HostOwned.hpp would also have been a short-lived file. Evidence addendum for AA2-ECS-02: a generic RemoveComponent of ParentComponent leaves the node linked (Scene.cpp:405-436), so the child is drawn under its parent while Capture (SceneFile.cpp:836-872), QuickSave (GameSession.cpp:197) and the determinism hash (Application.cpp:201) record it as a root, and after reload it jumps. The generic SceneCommands Add/Remove has no caller beyond the typed templates (SceneCommands.hpp:39, 45), so the hazard is latent until editor or tool code uses it; RavenSceneTool edits SceneFile data directly (RavenSceneTool.cpp:54-76) and is not a caller.
- **CRIT-5** (the world-replaced reset written twice in GameSession): refuted as a duplicate of CQ-CORE-11 step 1, which already extracts OnWorldReplaced before AA2-MEDIA-01.

**Corrections checked and not applied**

- CQ-BLD-02's verifier asked to change the HotReloadDesign.md citation from :349 to :347. A grep of the file shows the "Build: lock + hash / configure / compile and link / verify" row at :349 and the stamp and copy row at :350, so the original citations were kept.

**Citation fixes applied** (from the two checkers): section 3 (offsetof scope; the persistent-ID sort at PhysicsWorld.cpp:534-537), the hot-path map rotor row (ECSDesign.md:256 and :293, an upper bound), CQ-RND-05 (:608), CQ-RND-06 (span count), CQ-RND-10 (Overlay.hpp:14-22), CQ-RND-12 (Color.vert.hlsl:35-37, 55-57; the Lit flag), CQ-CORE-05 (two Reset+Snap pairs), CQ-CORE-06 (108 files; the snapshot copy is per changed or audited read), CQ-CORE-13 (six statements), CQ-CORE-14 (CMakeLists.txt:48-57), CQ-GP-01 (:945), CQ-GP-02 (ArenaComponents.hpp:78-80), CQ-GP-07 (PhysicsWorld.cpp:1329 is per Overlap; added :403, :797, :534-537), CQ-GP-08 (Submit at :50-63; SubmitRange), CQ-DATA-07 (no heap allocation per byte), CQ-VK-09 (:58 and :62), CQ-MEDIA-04 (512 + 64), CQ-API-03 (Run.cpp:29-32), CQ-API-10 (Courtyard's exact-zero check), CQ-BLD-01 (:511), CQ-BLD-08 (eight cases; Screens.cpp:67, 80), CQ-BLD-10 (CocoaWindow.hpp:26), CQ-GAME-07 (Camera.cpp:24 has a why-comment), CQ-X-09 (Physics.md:11-12 only).
