# RavenEngine Architecture Assessment (2026-11-26)

Status: proposal for owner review. Reviewed revision: 7617e21 (master). Nothing was built or run.

## 1. Purpose and method

This document tells the implementation team what to fix in RavenEngine, in what order, and why. The ordering is judged against the v1 goal in ROADMAP.md:
1. One shared C++23/Vulkan runtime on Windows, Linux and macOS.
2. A data-oriented ECS.
3. Hot reload of game code, shaders and assets, keeping compatible state with last-good recovery. Items 2 and 3 are one joint goal: the host owns the component bytes and the game DLL can be replaced.
4. GPU-driven rendering of 50k+ dynamic objects at interactive rates on the minimum-spec laptop (i7-12700H, RTX 3070 Ti Laptop).

Two standing constraints apply as well:
- Future editor and play sessions must stay possible.
- The engine must keep the ability to build an action game now and an RTS later, without RTS-specific systems.

Method:
- Nine panelists each reviewed the code through one lens:
  - ECS and data model.
  - Hot reload and module contract.
  - Renderer and M6B readiness.
  - Gameplay services (physics, movement, input, fixed step, user data).
  - Media (audio, text, UI).
  - Concurrency and performance.
  - Build, vendoring and cross-platform.
  - Core runtime and error policy.
  - Docs and process.
- Round 1 produced one paper per lens. The lead (chief architect) then merged duplicates, named the contested points and spot-checked the code. Round 2 answered the cross-questions, challenged findings and withdrew or re-rated them.
- This document is the lead's decision. Where the panel disagreed, the resolution and its reason are stated under "Panel discussion" in each finding.
- The review was read-only. No file in the repository was changed, nothing was built and nothing was run. Every cited file:line was opened by at least one panelist or by the lead. Figures marked "derived" or "estimate" are arithmetic, not measurements.
- Verification state: everything in the repository is verified on Windows only. No code has been compiled on Linux or macOS since before M6C.
- **Verification round (applied to this version).** After the panel draft, 14 adversarial verifiers each re-traced one finding that the panel had rated high against HEAD 7617e21: AA2-RT-01, CORE-01, HR-01, HR-02, RND-01, RND-02, RND-03, RND-04, GP-01, GP-02, MEDIA-01, PERF-01, PLAT-01 and DOC-01. A separate citation checker opened 172 cited file:line references and reported 22 citation problems plus 9 problems in the section 4 follow-up table. Results:
  - No finding was refuted. Two verdicts were "overstated" (GP-01, GP-02); the other twelve were "confirmed with corrections".
  - Nine of the fourteen were lowered from high to medium: HR-01, HR-02, RND-01, RND-02, RND-03, RND-04, GP-01, GP-02 and DOC-01. Five stay high: RT-01, CORE-01, MEDIA-01, PERF-01 and PLAT-01.
  - Every correction, every sound objection to a fix step and every citation fix has been applied in place. Each verified finding carries a "Verification" line, and section 10 has the summary table.
  - Section 10 lists the claims that are still unverified.

Finding IDs use the form AA2-AREA-NN so that they never collide with the 2026-11-23 ARCH-* IDs. The areas are:

| Area | Covers |
|---|---|
| RT | Runtime error boundary (FD-06) |
| CORE | Core runtime, ownership, public API |
| ECS | ECS, data model, file formats |
| HR | Hot reload and module contract |
| RND | Renderer |
| GP | Gameplay services |
| MEDIA | Audio, text, UI |
| PERF | Performance and concurrency |
| BLD | Build and vendoring |
| PLAT | Cross-platform |
| DOC | Docs and process |

Severity scale:
- **Critical:** blocks a v1 goal, or crashes or loses data in normal use.
- **High:** will force rework, or materially risks a goal.
- **Medium:** real but contained.
- **Low:** polish.

Kinds: defect, architecture-gap, doc-drift, process.

## 2. Executive summary

Verdict: the engine is in good structural shape on Windows and there is no critical finding. The ECS and reload core is the strongest part of the codebase:
- Host-owned pools.
- Transactional migration.
- One field-matching rule shared by files, live migration and user data.
- A hardened scene codec.

The risks sit in three places:
- **Defects in M6C code** still pending review:
  - A Render stage outside the error boundary. A designer's dump/apply edit with a negative ambient or a zero light direction ends the process today (AA2-RT-01).
  - A packaged player that cannot leave its first scene (AA2-CORE-01).
  - An audio use-after-free for looping sounds. It is latent until the first looping ambience or music (AA2-MEDIA-01).
  - A kinematic-body drift that the first door or elevator will hit. It is latent, because Arena's platform never stops (AA2-GP-01).
- **The simulation is not yet a pure function of its tick inputs.** Render interpolation writes into the authoritative transform (AA2-PERF-01, which threatens the recorded 3.5 ms preparation budget). Frame picks and the live UI reach fixed steps, and random and ID streams are shared across phases (AA2-GP-02/03, latent today).
- **The draft M6B plan** under-specifies structural contracts that the current code will force: fragment-side material plumbing, group identity, sprites, and the upload contract. None of them forces rework yet, because no B-step code exists. It is also about to build on two unreviewed stages and a public tree that is not the game API.

Cross-platform readiness regressed in M6A without anyone noticing. Every game is now a module, the module path is Windows+MSVC+Ninja only, and the host executable no longer even compiles off Windows (AA2-PLAT-01).

| Goal | Rating | Basis |
|---|---|---|
| 1. One runtime on three OSes | At risk | No macOS/Linux host can run any game since M6A, and the host does not compile off Windows (RavenEngine.cpp:169; AA2-PLAT-01). The ARCH-PLAT-01/03 seams are untouched. Nothing has been compiled off Windows since M6C. All fixable with small seam and CMake snippets, so not blocked. |
| 2. Data-oriented ECS | On track | M1 and M6A delivered with measurements. The gaps are format pinning (AA2-ECS-01), type-erased bypasses (AA2-ECS-02) and the unbuilt D9 access declaration (AA2-ECS-06). |
| 3. Hot reload with last-good recovery | On track on Windows, with one live recovery hole; watch at M6B content scale | Code reload is sound (1.47-1.50 s). A bad lighting or sprite value ends the session instead of recovering (AA2-RT-01). Shader compile latency grows linearly with material shaders, though it never stalls frames (AA2-HR-01). The reload service holds every byte (AA2-HR-04). The previous ARCH-HR-01/02/03/05 items are all still open. |
| 4. 50k+ GPU-driven at interactive rates | On track for render-only content; at risk with gameplay and content variety | Interpolation re-dirties the ECS and adds a second world pass against a 3.24-3.42 ms p95 / 3.5 ms budget (AA2-PERF-01). Change detection is O(total) in four places (AA2-PERF-02). Goal 4 is measured only with two unique meshes (AA2-RND-02). The benchmark has no physics, movers or interpolation (AA2-PERF-04). |
| Editor and play-session readiness | At risk | Per-world state lives in Application::Run locals while shared services live in GameSession (AA2-CORE-02). Live capture is lossy (AA2-ECS-05). Identity (FD-09) and hierarchy (FD-07) are sound. |
| Multi-genre readiness: action now | At risk | No game-driven scene flow (AA2-CORE-01). Latent kinematic drift on stop (AA2-GP-01). Looping audio is unsafe (AA2-MEDIA-01). No 2D or analog input actions (AA2-GP-10). Collision layers do not filter collisions (AA2-GP-09). Audio does not pause (AA2-MEDIA-07). All are S/M fixes. |
| Multi-genre readiness: RTS later | On track as an option | The option is preserved, but it depends on AA2-GP-02/03 (tick-pure simulation), AA2-PERF-02 (change detection), AA2-ECS-06 (declared access) and AA2-GP-07 (mover threading) landing before content depends on today's behavior. |

### Top fixes in priority order

1. **Hold the M6A+M6C joint review before M6B step B2.** Bring the confirmed defects into it as one "review amendments" batch (AA2-DOC-01). The one canonical list is Phase 1 of section 7.1; its first items are RT-01, MEDIA-01, CORE-01 steps 1-6, GP-01, RT-02, RT-03, RT-05, GP-04, GP-12, MEDIA-02 and HR-03. Most are small snippets of one to three files. Parts that touch a contract input (a Public header, or the game CMake that GameContract.cmake hashes) are marked there, because they change the contract hash.
2. **Fix the Render boundary and the looping-audio lifetime first** (AA2-RT-01, AA2-MEDIA-01). They are the two remaining high defects in code. RT-01 has a reachable trigger today.
3. **Add a game-facing scene request service** (AA2-CORE-01). Without it the packaged player cannot reach its second level or restart.
4. **Add a "B0 contracts" section to RenderingContentDesign.md before any B2 code.** It covers:
   - the fragment-side plumbing for FD-19's parameter record;
   - (pipeline, mesh) group identity, retirement and the command index;
   - sprites as render-world objects with blended sprites still CPU-sorted;
   - the upload batch before mips, with a recorded descriptor contract;
   - a mutable material table;
   - material shaders as assets on a bounded compile lane;
   - bindless allocation classes and device-limit checks;
   - interpolation as a render-side override (AA2-RND-01..07, AA2-HR-01, AA2-PERF-01).

   Add one contract-break manifest alongside it (AA2-DOC-07). These are specification gaps in a draft, rated medium after verification, but the draft text is cheap to fix now.
5. **Measure and then remove the interpolation write-back** (AA2-PERF-01). Measure 50k plus N interpolated bodies against the 3.5 ms preparation budget before B4.
6. **Make the fixed step a function of tick inputs only**, with one phase-aware `MakeContext` (AA2-GP-02, GP-03, GP-05). This is latent today, so do it before more game code is written against today's context, not ahead of the defects above.
7. **Clean up the contract surface before M6B edits renderer and asset headers:**
   - Delete the dead public APIs (AA2-CORE-04).
   - Split Public into a module SDK and a host root (AA2-CORE-03).
   - Give game modules hidden visibility and an image-ownership guard (AA2-HR-02; cheap, so do it even though it is medium: the guard and the module-contract text now, the visibility properties in this window because RavenGame.cmake is a contract input).
8. **Restore a cross-platform signal:**
   - BUILD's ordered PLAT-02 snippet list, including UTF-8 build-config constants so the host compiles on POSIX.
   - The visibility and Jolt instruction-set settings.
   - A rewritten PlatformHandoff.
   - A non-blocking Linux compile-only job (AA2-PLAT-01..04, AA2-BLD-01). The CI job is subject to the owner's cost answer (Q2). The exit-split and the M4-contract change are owner questions (Q13).
9. **Move per-world state into the session and shared services into Application** (AA2-CORE-02 steps 1-2), before B6 grows AssetStore inside GameSession.
10. **Record the measurement baseline before B4:** the restore trace section, the animated-fraction A/B that explains the 0.27 ms, and a game-scale benchmark case (AA2-PERF-06, PERF-07, PERF-04).

## 3. Strengths to preserve

These held up under every lens. They should be kept as they are, and nothing below proposes undoing them.

- **Everything that crosses the module boundary is copied into the host.** This covers systems, previews, schemas, input contexts, ComponentInfo, field tables, SceneCommands values and UI text (GameModule.cpp:317-337, 457-462, 478-479; SceneCommands.cpp:39-57; UiContext.hpp:79). No host structure keeps a pointer into module memory past one call.
- **Activation is transactional and two-phase.** Contract check, registration and component checks run before `Registry::Migrate`, and Migrate builds every converted pool before committing with node moves (GameModule.cpp:434-472; Registry.cpp:554-643). 100k values migrate in 1.0-1.6 ms. Registration exceptions are translated while the candidate is loaded, and `catch(...)` is present (GameModule.cpp:442-455, 505-514).
- **One field-matching rule** serves scene files, live migration and user data: name, then alias, then identical kind, flags, extents, tag and size (SceneFile.cpp:797-817; Registry.cpp:494-498, 516-529, 545-549).
- **Field tables come from `RAVEN_COMPONENT` with compile-time checks**, and type tags are folded into LayoutHash (Component.hpp:228-234, 296-302, 358-364, 382-390).
- **The scene codec is self-describing, deterministic and bounded.** Every length is checked before allocation, and unknown sections round-trip (SceneFile.cpp:26-28, 192-200, 489-521, 558-562). The 100k load dropped from 1.67 s to 0.66 s.
- **The incremental hierarchy:** a host-owned ParentComponent, child lists and depth buckets. Reparenting 1k entities at 100k takes 0.37-0.55 ms instead of 17.7 ms (Scene.cpp:348-455).
- **`RenderWorld::Sync` is transactional and validate-and-hide.** It uses process-wide generations and clears on any throw (RenderWorld.cpp:20-41, 214-229, 269-282).
- **The GPU-driven core:** dirty-record scatter, one cull dispatch and one indirect draw, with timeline retirement everywhere. Shader hot reload is checked against the C++ layouts (VulkanGpuScene.cpp:201-220, 289-327, 465-479; FragmentShaderInterface.cpp:191-234).
- **Third-party seams hold in code.**
  - Jolt is reached only from Private/Physics, miniaudio only from Audio/Backends and AudioClip.cpp, ICU/HarfBuzz/msdf only from TextSystem.cpp, Yoga only from UiContext.cpp.
  - All are linked PRIVATE, and each M6C library fails configure on a version mismatch (RavenThirdParty.cmake:20-25, RavenJolt.cmake:14-19, RavenAudio.cmake:18-31).
  - Every decoder is fed from memory through Raven's own `Files::ReadBinary` (TextSystem.cpp:177-200; AudioClip.cpp:76, 135; FbxMesh.cpp:107; Image.cpp:16, 90).
- **The audio thread is real-time-safe:** SPSC queues, no locks or allocation in the callback, serial-stamped commands, and a null-device fallback (AudioMixer.hpp:13-50; AudioOutput.cpp:30-89).
- **Physics is deterministic in order:** ID-ordered body adds, sorted contacts and ID-ordered movers. `PhysicsWorld::Clear` rebuilds Jolt's world (PhysicsWorld.cpp:525-561, 991-1000, 1079-1174; movers sorted by ID at CharacterMovers.cpp:465-475). The Raven-owned CharacterGrid cut 500 movers from 22.1 ms to 3.1 ms without touching Jolt.
- **The input router:** consuming contexts, chord specificity, held-input ownership, and a developer context that does not exist in players (InputRouter.cpp:66-106, 220-284).
- **UI identity and state are host-owned and reload-safe.** IDs come from scope plus key, never from the shown text (UiContext.cpp:156-163, 540-555).
- **Measurement discipline:** same-day A/B runs against a clean checkout, three runs per set, and exit records that report misses honestly (DataModelDesign.md:660-661; GameServicesDesign.md:974-984).
- **Design note before code, with numbered owner decisions and explicit exit checklists.** This closed ARCH-DOC-01.

## 4. Follow-up of the 2026-11-23 assessment

Status values:
- **fixed:** every step landed.
- **partly:** some steps landed.
- **open:** no step landed.
- **superseded:** replaced by a different design.
- **regressed:** worse than when reported.

Rows the panel first got wrong are flagged with "(corrected)". The verification round re-checked nine rows (CORE-05, ECS-02, ECS-03, RND-01, PLAT-01, PLAT-05, ASSET-05, ASSET-06, BLD-05). It corrected their evidence and, for PLAT-05, ASSET-05, ASSET-06 and BLD-05, their status. The table below is the ledger proposed for TODO.md (AA2-DOC-04).

| ARCH ID | Status | Evidence | Home now |
|---|---|---|---|
| RT-01 | fixed for RenderWorld; partly elsewhere | RenderWorld.cpp:217-227, 269-282. PhysicsWorld::Step (PhysicsWorld.cpp:1002-1050) and PickWorld (PickWorld.cpp:448) have no reset. | AA2-RT-04 |
| RT-02 | fixed | `Scene::TryUseAsset` (Scene.hpp:71). Note that it does not catch decode failures. | AA2-RT-03 |
| RT-03 | fixed | GameSystems.hpp:87-109; Scene.cpp:484-491, 547-553, 562-571 | - |
| RT-04 | fixed | GameModule.cpp:442-455, 505-514 | - |
| RT-05 | partly | StageBoundary covers Prepare, Gameplay, PostUpdate and Extract only. Render, the overlay and BuildOverlay are outside it (Application.cpp:636-639). | AA2-RT-01 |
| CORE-01 | partly | Engine pipeline at Application.cpp:475-646. The smoke still hand-builds Extract (scripts/Windows3DSmoke.cpp:194-205), and per-world state is host locals. | AA2-CORE-02 |
| CORE-02 | fixed | Public/Raven/Components holds engine components only | - |
| CORE-03 | fixed | `Scene(Registry&)`; HierarchyTests is CPU | - |
| CORE-04 | fixed; window part partly | InputRouter.cpp:66-106. No cursor mode or text input (Window.hpp:50-68). | AA2-GP-10 |
| CORE-05 | open | AssetResources.hpp/.cpp have no callers. The dead VulkanContext entry points (VulkanContext.hpp:60-61, 64-65, 72-73, 96, 100, 104, 123-128) are still exported; the rest of 64-104 is live API. The dead PngFileWatch class (step 1) is still present (PngFileWatch.hpp:11-21, used only by AssetResources.cpp:52). | AA2-CORE-04 |
| CORE-06 | partly | Problem history and overlay added (Log.cpp:50-56). RAVEN_ASSERT has 0 uses, and std::cerr is the only sink. | AA2-RT-01 step 6, AA2-CORE-05 |
| CORE-07 | regressed | Registry friends grew from 3 to 6 classes (Registry.hpp:328-338). Window.hpp:10 still includes vulkan.h. | AA2-CORE-03 |
| CORE-08 | fixed | Application.cpp:655-663; TransformInterpolator | - |
| CORE-09 | fixed | Paths::GetUserDataDirectory; RavenEngine.cpp:175-183. The folder name drifts from FD-33 (see 9b). | - |
| ECS-01 | fixed | Scene.cpp:348-455; DataModelDesign exit item 3 | - |
| ECS-02 | partly | Steps 5 (RAVEN_VALUE) and 6 (mismatch log) are missing. The silent mismatch sites are FindPool (Registry.cpp:253-260) and AddComponent (:690-693); Registry.cpp:494-498 is the Opaque-accepting type comparison (IsSameFieldType). | AA2-ECS-04, AA2-ECS-08 |
| ECS-03 | partly | Identity is fixed: EntityRef (Entity.hpp:41-53), and StaticMeshComponent holds AssetRef<MeshAsset> with MeshHandle off the ComponentValue list (Component.hpp:40-46). The runtime-only field classification is missing (Sound.hpp:84-97). | AA2-ECS-04 |
| ECS-04 | fixed | Registry.cpp:624, 628-638 | - |
| ECS-05 | open (corrected) | Registry.cpp:289-293 | AA2-ECS-08 |
| ECS-06 | open | Registry.hpp:445-455. Mutable `Single<T>` and views stamp unchanged values, which is the pattern behind AA2-GP-01's latent trigger. | AA2-ECS-08, AA2-ECS-03 |
| ECS-07 | open; risk grew | Registry.cpp:838-841. Four consumers now advance the tick. | AA2-ECS-08 (epoch lands with AA2-PERF-02) |
| HR-01 | open (corrected) | DLL is still the watched output (GameModule.cpp:593-594). F5 is unguarded (Application.cpp:415-417; GameModule.cpp:693-696). Failure is still sticky (GameModule.cpp:609-612, 652). | AA2-HR-03 |
| HR-02 | open (corrected) | ReloadService.cpp unchanged since da8eaf8 (M5). The 40 ms poll at :384 is the silent fallback the finding described, not a fix. | AA2-HR-04 |
| HR-03 | open; grew (corrected) | The destructor cleanup predates the assessment (da8eaf8:212-213). Leaked copies: 13 to 33 (debug), 44 to 58 (release), about 98 to 243 raven-shader-*. | AA2-HR-05 |
| HR-04 | fixed with residual | GameContract.cmake:9-15; WatchedInputs.txt. Folder-level over-watch remains. TODO.md:170 is not ticked. | AA2-DOC-02 |
| HR-05 | open (corrected) | GameModule.cpp:415-430 on the frame thread | AA2-HR-06 |
| RND-01 | partly | Mesh-tick guard (RenderWorld.cpp:204-206). Linear FindGroup (:162-171) with no retirement (:146; RenderWorldState.hpp:84-86). Command index equals group index (Cull.comp.hlsl:47-48). | AA2-RND-02 |
| RND-02 | open | Step 2 (white on free) is not done (VulkanTextureDescriptors.cpp:196-216). The table is append-only. | AA2-RND-05 |
| RND-03 | partly | Steps 1-2 are fixed (RenderWorld.cpp:35-41). The per-view split is carried as draft FD-25. | - |
| RND-04 | open | VulkanTexture.cpp:81-94. The wrong comment at VulkanContext.hpp:86 is still there. | AA2-RND-04 |
| RND-05 | open | Only feature booleans are checked (VulkanPhysicalDevice.cpp:68-73) | AA2-RND-07 |
| RND-06 | contradictory record | DataModelDesign.md:667 vs RenderingContentDesign.md:183 | AA2-DOC-03 |
| PERF-01 | fixed; small residual | RendererConfig.hpp:8-23. The cap uses `sleep_until` (Application.cpp:672). | - |
| PERF-02 | open; broadened | RenderWorld.cpp:103-122. The same pattern now runs in PhysicsWorld.cpp:498-523 and PickWorld.cpp:225-238. | AA2-PERF-02 |
| PERF-03 | partly | The pool exists. 100% animated is unmeasured (SamplesTool.cpp:87). | AA2-PERF-07, AA2-PERF-04 |
| PERF-04 | partly | Same-day A/B exists. Cases 2-8 and a combined case are missing. | AA2-PERF-04 |
| PERF-05 | open (corrected) | std::map lookup per system call (GameModule.cpp:390-395). OwnsResource scan per textured draw (VulkanContext.cpp:686-689). | Scan: deleted with the CPU textured path at B5 (AA2-CORE-04 step 4); lookup: AA2-PERF-09 |
| PERF-06 | superseded | Jolt PickWorld. Unchanged sync is 0.39 ms at 50k, still O(entries). | AA2-PERF-02 |
| ASSET-01 | fixed | Generic .ravenscene, SceneFileTests | - |
| ASSET-02 | open, scheduled | M6B B7 (FD-23) | - |
| ASSET-03 | partly, scheduled | M6B B6 | - |
| ASSET-04 | fixed with residual | Dead `AudioPlayer::Play(AssetRef)` path | AA2-CORE-04 |
| ASSET-05 | partly; cook scheduled M7 | Step 1 landed: the dependency closure walks AssetRef fields by tag (PlayerContent.cpp:72-96). Only the cook (step 2) is left. | M7 |
| ASSET-06 | open (latent, before M4/Linux) | AssetPath.hpp:14-15 still uses weakly_canonical with no case-only comparison | M4 |
| PLAT-01 | open; impact grew | BuildProcess.hpp:10-11; ShaderLibrary.cpp:276-288; GameBuildConfig.hpp.in:5-10. RavenGame.cmake:4, 29, 33-39 (new in M6A) repeats the Windows shape. | AA2-PLAT-02 |
| PLAT-02 | open by planning decision 4 | Only windows.yml exists | Owner question Q2 |
| PLAT-03 | open; now a precondition | No VISIBILITY_PRESET or -fno-gnu-unique anywhere | AA2-HR-02, AA2-PLAT-04 |
| PLAT-04 | open | M4 | - |
| PLAT-05 | open; grew | The macOS/Linux input backends still lag the input contract: XcbWindow.cpp writes m_Input directly (331, 413, 518) and handles only FOCUS_OUT (526). aa0cc37 touched only LinuxPaths.cpp/MacPaths.cpp, and FD-29 added ordered events and modifiers. | M4 (listed in the AA2-PLAT-01 handoff rewrite) |
| PLAT-06 | open | MacOSBuild.md:7 | AA2-PLAT-05 |
| PLAT-07 | open; weight grew | PosixAtomicFile.cpp:5-8 has no fsync. Files.cpp:67-72 has no FlushFileBuffers. Player profiles now depend on it. | AA2-GP-05 step 5 |
| BLD-01 | fixed with residual | RavenBuildSettings.cmake:53-62. /utf-8 is not shared. | AA2-BLD-04 |
| BLD-02 | fixed for debug info | RavenGame.cmake:33-39. The CMake floor was not raised. | AA2-BLD-01 |
| BLD-03 | open | Raven3DSmoke is EXCLUDE_FROM_ALL | AA2-BLD-03 |
| BLD-04 | open; grew from 9 to 13 programs | RavenContent.cmake:50-66 vs ShaderLibrary.cpp:38-52 | AA2-HR-01 step 5 |
| BLD-05 | open | `add_dependencies(RavenCore RavenContent)` is still present (RavenContent.cmake:143) | - |
| BLD-06 | open | x86 presets; actions pinned by tag | AA2-BLD-03 step 4 |
| BLD-07 | open | Needs the Linux job | M4 |
| DOC-01 | fixed | Stage notes with exit checklists | - |
| DOC-02 | partly | README.md:13, 23, 32-35; PlatformHandoff.md:3, 7; ROADMAP.md:107 | AA2-DOC-02 |
| DOC-03 | open | No capture-hash manifest | AA2-DOC-03 step 3 |

### Claimed done in TODO/ROADMAP but not supported by the code, or the reverse

- **ROADMAP.md:190 says M6A "is the stable contract M4 ports against".** The code says otherwise: C1-C3 changed the public contract again (GameServicesDesign.md:288), and B2/B5 will change public components again. The contract M4 can port against is the end of M6B (AA2-DOC-02).
- **The GameServicesDesign.md:594 C7 record says `TextSystem::Prepare` shapes on the workers.** Nothing calls Prepare (AA2-MEDIA-08).
- **UIDesign, FD-31 and UIAudioLibraries describe fonts as hot-reloaded assets.** The code has a fixed list of three engine files (TextSystem.cpp:48; AA2-MEDIA-05).
- **The PlayerBuild.md:9 and :25 record of F3/F4 working in the player dates from f062b6f (2026-11-21).** It predates FD-29's developer context (aa0cc37, 2026-11-25). Today the keys do nothing in a player (AA2-CORE-01).
- **ECSDesign.md:209-211 (D9) says declared system access "is recorded".** SystemDesc has no such field (GameSystems.hpp:125-133; AA2-ECS-06).
- **The reverse case: contract narrowing is done in code** (GameContract.cmake:9-15) and ticked in ROADMAP.md:188, but it is still unticked in TODO.md:170.
- **The reverse case: ROADMAP.md:92 (deferred structural changes), :95 (field migration) and :107 (M2 indices)** are unchecked although they shipped.

## 5. Findings by area

Each finding stands alone. "Sources" lists the round-1 and round-2 panel IDs that contributed to it. Steps are sized for one reviewable snippet each unless they say otherwise.

### 5.1 Runtime error boundary (RT)

The FD-06 tiers are set out in DataModelDesign.md:161-167:
- **Data error:** use a placeholder and log once.
- **API misuse:** a std::logic_error-family throw to the caller. In a game system, the system boundary suspends that system.
- **Fatal:** bad_alloc, or a broken engine invariant.

The panel agreed to keep these tiers unchanged. Every RT fix below closes a place where code does not follow them.

#### AA2-RT-01 Render runs outside the FD-06 boundary, and render-path data errors are thrown as std::invalid_argument
- **Severity:** high
- **Kind:** defect
- **Sources:** CORE-1, CORE-5 (withdrawn and merged), ECS round 2 answer 5, RENDER round 2 answer 4, AUI-6 (part), lead spot-check
- **Evidence:**
  - Application.cpp:636-639: DrawDeveloperOverlay (:636), physics.DrawColliders (:638) and session.Render (:639) have no StageBoundary. Only Prepare, Gameplay, PostUpdate and Extract are wrapped (:475, :518, :608, :618), and Application::Run has no other try (only :86-98). A throw from Render reaches main.
  - GameSession.cpp:482-487: LightingComponent::GetLighting() is copied unchecked (LightingComponent.hpp:14). VulkanContext.cpp:704 calls BuildVulkanDrawQueue, and VulkanDrawQueue.cpp:78 calls `lighting.Validate()`. That throws std::invalid_argument on a negative or non-finite color (SceneLighting.hpp:17-26), and on a zero or near-zero direction through Normalize (Raven/Math/Transform.hpp:53-58). Nothing else validates LightingComponent.
  - **Current trigger:** RavenSceneTool apply accepts any finite number (RavenSceneTool.cpp:35). A designer who sets ambient to -0.1 or the light direction to 0 0 0 through dump/apply ends the process when the scene reloads. No shipped game writes LightingComponent at runtime, so a day/night cycle is a prospective trigger only.
  - GameSession.cpp:413-436: GatherSprites passes world.World and sprite.Tint unchecked. PrepareTransform (VulkanDrawQueue.cpp:15-25) runs for every draw before the frustum test. It throws std::invalid_argument for a non-finite tint, a non-finite MVP, or a finite model whose product with viewProjection overflows (:19-23). Scene.cpp:573-577 keeps non-finite worlds by design ("consumers skip non-finite worlds"), and the sprite consumer does not skip them.
  - Application.cpp:72-76, 94-97: a boundary rethrows every logic_error as fatal. Wrapping Render alone therefore would not help.
  - Application.cpp:640-646: the interpolation Restore and the second world pass run after Render. A caught Render failure placed naively would skip Restore and leave blended transforms authoritative.
  - Application.cpp:674-675 and RavenEngine.cpp:218-221: `SaveAll` runs only on a normal exit, and main catches only std::exception. UserDataService keeps changes in memory until Save (UserData.hpp:34-36), and only bindings are saved during the frame (Application.cpp:368-375).
- **Why it matters:** This breaks goal 3's recovery rule and FD-06 tier 1, which says a data error gets a placeholder. A reachable designer edit ends the development session today. Later, a NaN fade or a day/night light direction passing through zero would do the same. On any fatal exit, players lose user data the game has not yet saved.
- **Fix:**
  1. Add a non-throwing companion to `SceneLighting::Validate` in the public header Raven/Renderer/SceneLighting.hpp, for example `bool IsValid() const noexcept`. It includes a non-throwing direction-length check. Reimplement Validate on top of it (it throws when !IsValid), so there is one rule set; Validate stays as public API. GameSession::Render uses the check, falls back to the default lighting and logs once, as ReportCamera does (GameSession.cpp:406-411). This step edits a Public header, so it changes the contract hash.
  2. GatherSprites skips entities whose world or tint is non-finite, or whose MVP overflows, and logs once per entity. RenderWorld's finite helper is `IsDrawable` (RenderWorld.cpp:21-28), in an anonymous namespace. A first small snippet moves it into a shared private header, and the second reuses it. For the MVP overflow, either check the product at gather time, or make PrepareTransform's overflow case skip the draw with one log line instead of throwing. Delegated choice: skip in PrepareTransform, because only it holds viewProjection. This check survives the B5 sprite move.
  3. Add a private `Raven::DataError : std::runtime_error`, used for data or I/O conditions that legitimately escape to a stage. Do not sweep the existing logic_error-family sites: `grep -E "throw std::(logic_error|invalid_argument|out_of_range|length_error|domain_error)"` over Private and Public finds 172. Convert each one when it is next touched.
  4. Add a Render stage around the overlay, colliders and session.Render that **catches only DataError**. DataError is by construction thrown before swapchain acquisition (gather, TryUseAsset, overlay build). Every other exception from DrawFrame stays fatal, in particular VulkanDeviceLostError (a std::runtime_error, VulkanDevice.hpp:11; VulkanDevice.cpp:14-17) and post-acquire failures inside VulkanClearPass::Draw (VulkanClearPass.cpp:544-588). Retrying those would leave the acquired image and the ImageAvailable semaphore unconsumed, and would turn device loss into a silent frozen loop. Move Restore and the second world pass into a scope that runs whether or not Render succeeded. AA2-PERF-01 later deletes Restore.
  5. Add a scope guard in Application::Run that calls `userData->SaveAll()` on every exit, and add `catch(...)` in main so a non-std exception is logged.
     - Declare the guard after `userData` (Application.cpp:341), so it runs before userData is destroyed.
     - It also runs while unwinding a fatal broken-invariant or bad_alloc exit, where FD-06 says continuing "would hide corruption". Record that this is deliberate: Save keeps a .bak (UserData.cpp:340-345), so at worst one save is replaced by its backup.
  6. Add one paragraph to the FD-06 record:
     - Tiers are decided by who catches.
     - DataError covers escaped data errors, and capacity exhaustion is a DataError.
     - The Render stage recovers DataError only. Device loss and submission-time throws are engine invariants and stay fatal.
     - RAVEN_ASSERT is either used for hot-path invariants or deleted. Decide which in the same paragraph.
- **Effort:** S
- **Timing:** now (review amendment). Step 1's header change goes in the amendments' one hash-changing commit.
- **Panel discussion:** CORE-5 proposed letting StageBoundary treat invalid_argument and out_of_range as recoverable. ECS, RENDER and DOCS objected that this makes engine API misuse recoverable, which contradicts FD-06 tier 3. CORE withdrew it. Decision: keep the boundary's types unchanged, and fix this by gather-time validation plus a runtime_error-derived DataError. After steps 1-2, including the MVP-overflow skip, a throw in VulkanDrawQueue means an engine bug, so it stays fatal. ECS-F2's record-time invalid_argument is consistent with this, because the system boundary catches it.
- **Verification:** confirmed with corrections; severity high -> high, because a reachable designer edit (dump/apply lighting) ends the process during the hot-reload workflow, but it is a development-time trigger with no shipped-game data loss, so it is not critical. Step 4 was narrowed to DataError only, so that device loss is not swallowed, and step 2 gained the MVP-overflow case.

#### AA2-RT-02 Capacity exhaustion is typed as std::length_error, so a full bindless array ends the process
- **Severity:** medium
- **Kind:** defect
- **Sources:** CORE-10, RENDER round 2 answer 3, MEDIA challenge to CORE-1, lead grep
- **Evidence:**
  - VulkanTextureDescriptors.cpp:146-148 throws `std::length_error("The bindless texture array is full")`. Lead grep found the same type for pool and ring capacity: VulkanMesh.cpp:128 (mesh pool growth), VulkanFrameData.cpp:55 (upload ring) and VulkanDrawQueue.cpp:52.
  - Application.cpp:94-97: length_error is a logic_error, so it is fatal at every boundary.
  - RenderingContentDesign.md:84, 100 and TextSystem.cpp:612-615: M6B adds four textures per material, sprites and glyph pages to the same 4,096-entry array (VulkanTextureDescriptors.hpp:20).
- **Why it matters:** Goals 3 and 4 at content scale. A budget condition that ordinary M6B content can reach becomes a process exit. FD-06 says it should be a placeholder.
- **Fix:**
  1. Throw DataError (from AA2-RT-01 step 3) instead of length_error at VulkanTextureDescriptors.cpp:148.
  2. The asset path marks the asset failed (AA2-RT-03) and falls back to the white texture with one log line.
  3. Review VulkanMesh.cpp:128 and VulkanFrameData.cpp:55 in the same snippet. The ring overflow is an engine sizing invariant, so keep it fatal. Mesh-pool growth from content is a DataError.
  4. Bindless allocation classes and limits are covered in AA2-RND-07.
- **Effort:** S
- **Timing:** now, before B2/B5 add bindless users
- **Panel discussion:** MEDIA showed that wrapping Render in a boundary cannot protect text placement while this stays a logic_error. Everyone agreed.

#### AA2-RT-03 Scene::TryUseAsset does not catch decode or I/O failures
- **Severity:** medium
- **Kind:** defect
- **Sources:** CORE-4, MEDIA round 2, RENDER endorsement
- **Evidence:**
  - Scene.cpp:627-644: the pre-check covers only registration, kind and presence (AssetStore.cpp:199-209). After that, UseAsset is called with no catch.
  - Scene.hpp:68-70 documents this as the contract: "Decode and GPU failures still throw." Step 1 changes that documented public contract.
  - AssetStore.cpp:99-107: the first load decodes synchronously and can throw.
  - Application.cpp:618-634: a throw inside Extract reaches RenderWorld::Sync, which clears itself (RenderWorld.cpp:270-281). The next frame rebuilds 50k objects and fails again, while the boundary logs only once.
  - GameSession.cpp:426-428 and :297-306: sprites and UI images reach the same call from Render, so the process ends (AA2-RT-01).
  - GameSession.cpp:331-341: PlaySound with an unleased AssetRef throws into the calling system, which is then suspended for the rest of the module generation (GameModule.cpp:369-406).
- **Why it matters:** Goal 3 ("a bad reference draws nothing") and goal 4 (a full rebuild every frame).
- **Fix:**
  1. In TryUseAsset, catch std::exception around UseAsset. Record the ID with its error text in `m_ReportedAssets`, log once, and return nullptr. bad_alloc from a decode is caught here too, because it is a data-size problem; AA2-MEDIA-02 bounds it. Update the Scene.hpp:68-70 comment in the same snippet. Scene.hpp is a Public header, so this changes the contract hash.
  2. Check `m_ReportedAssets` before any retry. The entry is cleared only by the asset reload path for that ID.
  3. Note in the M6B B6 design that this failed state carries over to async loading.
- **Effort:** S
- **Timing:** now
- **Panel discussion:** No disagreement. RENDER confirmed that step 2 is not throwaway work, because B6 keeps the same failed state.

#### AA2-RT-04 PhysicsWorld and PickWorld are not exception-safe: no transactional reset, and the Jolt callbacks are not noexcept
- **Severity:** low
- **Kind:** defect
- **Sources:** GSV-10, CORE-9
- **Evidence:**
  - PhysicsWorld.cpp:1023-1049: the lifecycle batches are consumed before the sync runs, and there is no try/catch around it.
  - PhysicsWorld.cpp:1008 and PickWorld.cpp:448: the scene identity check compares pointers only. RenderWorld compares a SceneId (RenderWorld.cpp:289).
  - Jolt is built with exceptions off (Build/CMakeLists.txt:44, 175-177, 239-241). Raven callbacks that Jolt calls are not noexcept:
    - ContactRecorder (PhysicsWorld.cpp:214-247)
    - TraceToLog and AssertToLog (JoltSupport.cpp:21-41)
    - the CharacterGrid overrides (CharacterMovers.cpp:112-160)
  - JobSystemWithBarrier.cpp:165 runs jobs inline on the host thread. A throw there unwinds through Jolt frames that have no exception support. A std::system_error would then be treated as recoverable (Application.cpp:90-97) while Jolt's locks are abandoned.
- **Why it matters:** FD-06's "recoverable stage" promise holds only if every host-owned mirror returns to a consistent state. This is latent: the only triggers are allocation or mutex failure.
- **Fix:**
  1. Wrap the body of PhysicsWorld::Step in `try { ... } catch (...) { Clear(); throw; }`, as RenderWorld does.
  2. Do the same around the sync in PickWorld::Pick.
  3. Store the scene's SceneId next to the source pointer in both worlds, and compare it.
  4. Mark the ContactRecorder, LineRecorder, Trace/Assert hooks and CharacterGrid overrides noexcept, so a failure stops the process the same way on workers and on the host thread.
  5. Add a comment in JoltJobs.hpp: every Raven path reachable from a Jolt frame is noexcept.
- **Effort:** S
- **Timing:** now (opportunistic, batched)
- **Panel discussion:** None.

#### AA2-RT-05 SceneSession retries a failing scene or project candidate every 200 ms, forever, at full cost
- **Severity:** medium
- **Kind:** defect
- **Sources:** ECS-F3, RELOAD round 2 answer 1, MEDIA endorsement
- **Evidence:**
  - SceneSession.cpp:198-223: the backoff is a fixed `RetryAfter = now + 200 ms`. PendingScene is cleared only on success or when the file changes. `ReportedError` only suppresses the log line.
  - SceneSession.cpp:71, 200, 207: each retry reads the file twice (:200 and :71; the pending bytes are passed into Prepare at :207, so :64 does not read again) and reruns the full factory. There is no unchanged-hash short-circuit.
  - SceneSession.cpp:182-196: PendingProject also retries forever.
  - GameSession.cpp:83-111: the factory instantiates into the shared registry before LeaseAssets, so a missing asset fails after the expensive part. A 100k scene costs about 0.6 s per retry. The failed candidate's leases die, and the weak cache (AssetStore.cpp:143) forgets them, so every asset is re-decoded on each retry.
- **Why it matters:** Goal 3. The developer loop degrades into a 0.6 s hitch every 200 ms exactly while the developer is fixing the data.
- **Fix:**
  1. On any exception from Prepare or UpdateProject, drop PendingScene and PendingProject and log once. The watcher delivers the next save anyway.
  2. Keep a capped, growing backoff only for file-read failures, for example an editor holding the file open.
  3. In GameSession.cpp:107-110, call LeaseAssets on the decoded file before InstantiateScene, so a missing asset fails at decode cost.
- **Effort:** S
- **Timing:** now, before B6 reshapes Prepare
- **Panel discussion:** RELOAD confirmed the claim and found the project variant. No disagreement.

### 5.2 Core runtime, ownership and public API (CORE)

#### AA2-CORE-01 Games cannot change, restart or advance scenes, so the packaged player cannot leave its first scene
- **Severity:** high
- **Kind:** architecture-gap
- **Sources:** CORE-2; DOCS and CORE round 2 (dated verification); GAMEPLAY round 2 answer 5
- **Evidence:**
  - GameSystems.hpp:46-82: GameServices has no scene request.
  - Application.cpp:458-463: restart and next come only from developer actions (or the determinism check). They feed `session.Prepare` (Application.cpp:477) and reach `Advance(requests.RestartScene || requests.RestartPlay, requests.NextScene)` (GameSession.cpp:165). GameSession::Advance (GameSession.cpp:182-192) has no other caller, and `SceneSession::Update(restart, next)` (SceneSession.cpp:145-165) is the only scene switch. A grep finds no RequestScene, LoadScene or ChangeScene in RavenEngine/ or Games/.
  - RavenEngine.cpp:110, 174 sets `DeveloperInput = !playerBuild`. Application.cpp:348 builds `InputRouter router(config.DeveloperInput)`, and the InputRouter constructor returns early without the developer context (InputRouter.cpp:66-69). WasDeveloperPressed then returns false (InputRouter.cpp:361-370, early return at :363-365), so F3 and F4 do nothing in a player. `git log -S` dates both lines to aa0cc37 (2026-11-25).
  - The player shows the problem from three sides:
    - BuildProfiles/public-preview.json packages two scenes.
    - Games/Samples/Source/Interaction.cpp:68, 70, 149 still tell the player to press F3. The only other game keys in Samples are Space, F1/O and F6 (SamplesGame.cpp:212, 224), so nothing replaces it.
    - PlayerBuild.md:9 and :25 date from f062b6f (2026-11-21), before aa0cc37 (2026-11-25) added the developer context.
  - Games/Courtyard/Source/CourtyardGame.cpp:131 and :137 carry the same stale F3 hints. Courtyard is not in a player profile, so they matter only once it is packaged.
  - Games/Arena/Source/Run.cpp:27-40: Arena restarts in-scene, by destroying drones and cells and rewriting the craft by hand. That works and is not a defect.
  - No roadmap, TODO or design-note item schedules a game-facing scene API (grep).
- **Why it matters:** Every genre needs menu, level and retry flow. This is a player-visible regression in the only shipped player artifact today, and M7 cannot ship multi-level content.
- **Fix:**
  1. Add `GameServices::RequestScene(AssetRef<SceneAsset>)` and `RequestRestart()`. The last request wins, and it is applied at the next Prepare, between frames. State the fixed-step semantics: a request made in FixedUpdate takes effect at the next frame's Prepare, and any remaining fixed steps of the current frame are skipped once a request is pending, so no tick runs on a scene the game has already left.
  2. Define the semantics now: the request is applied when the target is prepared. Add a `GetSceneTransition()` state (idle, loading with progress, failed). Today it is trivially synchronous, so B6's async loading stays behind the same API.
  3. Add a `SceneSession::Update` overload that takes a target ID. Reuse the existing replaced path in Application.cpp:520-535, which already clears physics, picking, the interpolator and ticks.
  4. Port the Samples completion flow to the new requests. Leave Arena's in-scene retry as it is, as the documented in-scene pattern. Porting it would be churn with no player-visible gain, and it would change what the determinism check exercises.
  5. Correct PlayerBuild.md:9 and :25, the Interaction.cpp hints, and the Courtyard hints (CourtyardGame.cpp:131, 137).
  6. Out of v1 scope: additive scenes (blocked by AA2-ECS-05 step 4). Record the trigger: RTS map plus overlay, or streaming.
  7. **Separate row, later (M):** a typed carried record, `RequestScene(scene, carry)`.
     - It needs a non-template virtual seam, ComponentInfo plus a byte span, like SystemTable::AddComponent, because GameServices virtuals cannot be templates. A typed template wrapper sits on top in the header.
     - Create the carried entity right after StartSimulation (GameSession.cpp:319-324, called from Prepare at :166-167), from the seeded ID stream. The Scene constructor seeds m_IdState from OS random bytes (Scene.cpp:128-136), and StartSimulation reseeds it (GameSession.cpp:323). An entity created before the reseed would get a non-deterministic EntityRef, and the EntityRef is part of the hashed capture (Application.cpp:196-204).
     - Define what happens to a pending carry when the module reloads or is rejected between the request and Prepare. Proposed: drop it with one log line, because its layout belongs to the old generation.
- **Effort:** S (steps 1-6); M for step 7
- **Timing:** steps 1-6 before M6B B2. GameServices is public, so steps 1-2 go in Phase 1's one hash-changing (†) commit, costing one game rebuild; only if the owner's Q10 answer defers them do they join the Phase 3 window. The reason to land it before M6B is the player regression and keeping B6's async loading behind one request API. Any GameServices change forces one game rebuild whenever it lands, so there is no special "contract window" argument. Step 7 when a game first needs carried state. Owner question Q10 asks whether it lands in M6B or M7.
- **Panel discussion:** The lead first doubted the claim because PlayerBuild.md records F3/F4 working. CORE and DOCS dated that record to before FD-29, so the claim holds. GAMEPLAY added carried state as part of the minimum v1 API. After verification, it is split into its own later row (step 7), so that it does not hold up the S fix of a regression.
- **Verification:** confirmed with corrections; severity high -> high, because this is a player-visible regression in the only shipped player artifact and a game-flow API every genre needs that no roadmap item schedules (medium would be defensible against the four technical goals alone). The carried record moved to its own row and now creates its entity after the StartSimulation reseed, and Arena's retry is no longer ported.

#### AA2-CORE-02 Application::Run is a 440-line monolith with inverted ownership; the determinism guard cannot run in CI
- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** CORE-3, GSV-09 (merged), BUILD and CORE round 2
- **Evidence:**
  - Application.cpp:299-742 holds the whole loop, the DeterminismCheck class (:152-258), the UI preview, the overlay and the trace writers.
  - Application.cpp:327-335, 378: PhysicsWorld, PickWorld, FixedGameplayStep and TransformInterpolator are locals of Run. PhysicsWorld and PickWorld reach the session through raw setters (SetPhysics and SetPicking, GameSession.hpp:91, 96). FixedGameplayStep is passed per MakeContext call (Application.cpp:557, 579), and the interpolator is never given to the session. The other two setters at GameSession.hpp:89-96, SetUserData and SetUi, are services, not world state.
  - GameSession.cpp:95-117: each session builds its own SceneProject/AssetStore (which need `VulkanContext&`: AssetStore.hpp:77, SceneProject.hpp:51) and its own AudioPlayer. It also pins a renderer quad and turns on shader watching.
  - Scene.cpp:622-623: a store-less Scene throws on any lease, so headless loading is impossible today.
  - Tests/CMakeLists.txt:64-70 and windows.yml: DeterminismCheck is labeled gpu and never runs in CI.
  - scripts/Windows3DSmoke.cpp:194-205: the smoke re-implements Extract.
- **Why it matters:** This is the editor and play-session constraint. A second session would duplicate the asset store and its watchers while sharing one physics world and one clock. B6 will grow AssetStore inside the wrong owner. Determinism level 1 is unguarded in CI.
- **Fix:** Moves only, with no behavior change.
  1. Construct SceneProject/AssetStore and AudioPlayer in Application as engine services, and pass them by reference to GameSession. The quad and WatchShaders move with them.
  2. Move PhysicsWorld, PickWorld, FixedGameplayStep and TransformInterpolator into a session-owned PlayWorld struct. Delete SetPhysics and SetPicking only; SetUserData and SetUi stay, because they wire shared services.
  3. Extract the Prepare-to-Extract body (Application.cpp:475-635) into one Private/Core class, GameplayLoop or FramePipeline. Rebase the smoke on it, and give the Restore pass a trace section (AA2-PERF-06).
  4. Move DeterminismCheck, the UI preview plus overlay, and the trace writers into their own Private/Core files.
  5. Add a headless mode. GameSession accepts a nullable AssetStore; with null, the factory builds `Scene(Registry&)` and skips leases, and assets resolve to nothing (FD-06 data tier). A `--headless` host drives the loop for N ticks. DeterminismCheck then drops the gpu label and runs in the Windows job.
  6. Record the ownership rule in FD-13: shared services belong to Application, and world state belongs to the session.
- **Effort:** M
- **Timing:**
  - Steps 1-2 before M6B (before B6).
  - Steps 3-4 during M6B.
  - Step 5 at the end of M6B.
  - Until step 5 lands, "DeterminismCheck run locally" is an explicit M6B exit item.
- **Panel discussion:**
  - CORE re-rated this from high to medium (moves only, nothing rewritten). BUILD argued for high because of the CI gap. The lead decides medium: no goal is blocked if steps 1-2 land before B6, and the plan says they do.
  - For CI, the lead chose a headless host over lavapipe, which BUILD withdrew because it tests renderer features rather than simulation. A separate CPU test that re-drives physics was rejected because it duplicates the orchestration.
  - CORE showed that no renderer-free AssetStore is needed for headless. A nullable store is enough. B6 should still separate decode from upload, which is recorded as delegated.

#### AA2-CORE-03 The public tree is not the game API: host headers are hashed into the game contract and sit on the game include path
- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** RELOAD-4, CORE-6 (merged); RELOAD, BUILD and CORE round 2 rescans; lead check
- **Evidence:**
  - GameContract.cmake:6-10 hashes every .hpp under Public. RavenGame.cmake:16-17 puts all of Public on the game include path.
  - RELOAD and BUILD rescanned the 65 Public headers and agree:
    - 32 headers are reached by game modules (the module SDK).
    - 6 more are reached only from Games/*/Tools: Scene, SceneFile, AssetStore, AssetRecord, MeshHandle and TextureHandle.
    - 27 are reached by neither, for example Application, GameModule, Window, VulkanContext, RenderWorld, AssetCatalog, AssetResources, AudioPlayer, SceneProject and SceneSession.
  - Scene.hpp:3 includes AssetStore.hpp. Scene.hpp:146 holds `std::map<AssetId, AssetLease>` by value (lead-checked), so a forward declaration alone is not enough.
  - Registry.hpp:328-338 lists six engine friends. Each new sync edits a contract-hashed header.
  - Window.hpp:10 includes vulkan.h, which the game include path lacks.
  - GameModule.cpp:437-438: an engine-internal header edit rebuilds the game, and the host then rejects the result.
- **Why it matters:** Goal 3 iteration speed. Every M6B renderer or asset header edit forces a rejected game rebuild. Games can also couple to host internals, which hurts M7 SDK packaging.
- **Fix:**
  1. First delete the dead APIs (AA2-CORE-04), so fewer files move.
  2. Split AssetLease out of AssetStore.hpp into its own header. Scene.hpp then includes AssetLease.hpp and forward-declares AssetStore.
  3. Add a second physical root, RavenEngine/Host/Raven/..., as a PUBLIC include directory of RavenCore. The host, tools, tests and smoke see both roots; the game child sees only Public.
  4. Move the 27 host headers there, one folder per snippet (Renderer, Scene/Assets, Core). Then move the tool tier (Scene, SceneFile, AssetStore, the handles).
  5. Document three tiers in EngineModules.md:
     - module SDK: Public; hashed; in the PCH;
     - tool SDK: stable for game tools, not hashed;
     - host.
  6. Narrow GameContract.cmake to Public only. Keep the full tree in CMAKE_CONFIGURE_DEPENDS.
  7. Replace the six Registry friends with one `ECS::Detail::RegistryAccess` defined in Private/ECS. View stays a friend because it is an inline template. RegistryAccess also hosts the PERF-02 changed-since scan.
- **Effort:** M
- **Timing:** before M6B, in the contract-break window (section 7)
- **Panel discussion:**
  - The lead flagged RELOAD's first "33 host-only" count because game tools use Scene/SceneFile. CORE wanted three physical roots and AssetCatalog kept public. The rescans showed AssetCatalog is reached by neither modules nor tools.
  - Tools link RavenCore inside the host configure (RavenGameModule.cmake:63-75), so they inherit a second PUBLIC root. Decision: two physical roots and three documented tiers.
  - The lead's check of Scene.hpp:146 settles the forward-declaration question in CORE's favor: AssetLease is split out instead of forward-declared.

#### AA2-CORE-04 Dead public APIs are still exported, hashed into the contract and documented as current
- **Severity:** low
- **Kind:** architecture-gap
- **Sources:** CORE-8, RND-9, AUI-10, ARCH-CORE-05
- **Evidence:**
  - Public/Raven/Assets/AssetResources.hpp has no callers outside its own .cpp.
  - VulkanContext.hpp:60-61, 64-65, 72-73, 96, 100, 104, 123-128 (CreateMesh/CreateTexture, CreateMeshHandle, ReplaceMesh, ReplaceTexture, ReplaceTexturedFragment, the camera-only DrawFrame) are used only by AssetResources.cpp:68, 123, 151, 190. ReplaceTexture depends on the implicit queue drain (VulkanContext.cpp:636-648). The rest of VulkanContext.hpp:64-104 is live API: ResolveMesh (68), CreateManagedMesh/Texture and ResolveTexture (77-80), Release*/CollectResources/GetResourceStats (81-84), Swap*Contents (88-89) and CreateTexturedMesh (92, used by GameSession.cpp:114).
  - The PngFileWatch class (PngFileWatch.hpp:11-21) is used only by AssetResources.cpp:52. AssetStore uses only DecodePngSource (PngFileWatch.hpp:8).
  - Public/Raven/Audio/AudioPlayer.hpp is included only by Private/Core/GameSession.hpp:3. Its `Play(AssetRef)` decodes synchronously against the engine content path and has a cache that is never evicted (AudioPlayer.cpp:36, 52-75).
  - AssetSystem.md:34-61 and EngineModules.md:182, 515 present these APIs as live.
- **Why it matters:**
  - This is a second texture-replacement path that B6 would have to keep correct.
  - The surface sits inside the contract hash.
  - Under the no-throwaway rule, M6B must not offer two asset entry points.
- **Fix:**
  1. Delete AssetResources, and delete the five VulkanContext entry points plus the camera-only DrawFrame overloads. Keep `Impl::CreateTexture` for the white texture (VulkanContext.cpp:243). Delete the PngFileWatch class and keep DecodePngSource (PngFileWatch.hpp:8, 11-21), as ARCH-CORE-05 step 1 listed.
  2. Delete `AudioPlayer::Play(AssetRef)`, `Impl::Clips`, `ReportedFailures` and the catalog constructor argument. Move AudioPlayer.hpp to Private/Audio.
  3. Update AssetSystem.md and EngineModules.md in the same commit.
  4. At B5, also delete CreateTexturedMesh, VulkanTexturedDraw/VulkanTransparentDraw, the per-texture set-0 descriptors and samplers (VulkanTexture.cpp:219), and the VulkanDrawQueue sprite path.
- **Effort:** S
- **Timing:** steps 1-3 in the Phase 3 contract window (they edit Public headers); step 4 at B5
- **Panel discussion:** None.

#### AA2-CORE-05 Players get no diagnostics: cerr-only logging, no crash handler, developer key list and engine title shown
- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** CORE-7 (diagnostics part); the copy and in-place part moved to AA2-HR-05
- **Evidence:**
  - Log.cpp:47-49: std::cerr is the only sink.
  - RavenEngine.cpp:218-221: only std::exception is caught. A grep found no SetUnhandledExceptionFilter or set_terminate.
  - RavenEngine.cpp:212-213: players log the developer key list.
  - RavenEngine.cpp:197: the window title is "RavenEngine".
  - GameModule.cpp:679: the development BuildRoot path is baked into player builds.
- **Why it matters:** Players launched from Explorer leave no trace when they crash, which blocks M7 supportability. The window title is player-visible.
- **Fix:**
  1. Log the developer key list only when DeveloperInput is on.
  2. Add `catch(...)` in main (shared with AA2-RT-01 step 5).
  3. Add an optional file sink in the user-data folder. It goes through the AA2-PERF-08 logger thread.
  4. Take the window title from the project's game name.
  5. Add a terminate/crash handler writing a minidump. This is Windows-only, behind a Platform seam.
- **Effort:** S
- **Timing:** steps 1-2 now; steps 3-5 before M7
- **Panel discussion:** DOCS argued that the window title has an obvious default and should not use up an owner question. The lead agrees: it is listed for confirmation in 9b.

### 5.3 ECS, data model and file formats (ECS)

#### AA2-ECS-01 FieldKind ordinals are written into binary scenes and player saves with no pinned values, and Raven formats have no version policy
- **Severity:** medium
- **Kind:** defect
- **Sources:** ECS-F1, PROC-12, RENDER challenge
- **Evidence:**
  - Component.hpp:69-75: `enum class FieldKind : std::uint8_t` uses implicit values. Opaque comes last.
  - SceneFile.cpp:610 writes `static_cast<std::uint8_t>(field.Kind)`, and :181 casts it back.
  - CheckField (SceneFile.cpp:77-84) catches a shift only when sizes differ. For example, AssetRef/AssetId and Opaque/EntityRef can alias at equal sizes and then fall to defaults in MatchFields (:810-814).
  - UserData.cpp:76-81, 276, 321: player profiles use the same codec, in binary by default.
  - SceneFile.cpp:23, 528-529 rejects any version other than 1. SceneProject.cpp:71-72 accepts only v2 but is tolerant of unknown records. SoundEvent.cpp:46-47, 123 accepts only v1 and is strict. So each format has its own rule and none has an upgrade path.
- **Why it matters:** Goal 3 and FD-33. Inserting a kind in logical order would silently default every authored scene and every player profile. M6B adds a material file format.
- **Fix:**
  1. Give each FieldKind enumerator an explicit value equal to today's ordinal (Bool = 0 ... Opaque = 19). Add a comment saying that the values are a file format and new kinds are appended only.
  2. Next to the Kinds table in SceneFile.cpp, add a static_assert that each entry's ordinal matches its enumerator.
  3. Add one paragraph to FD-10:
     - Readers accept versions N and N-1.
     - A version bump ships with a RavenSceneTool upgrade path.
     - Each text format states whether it is tolerant or strict.
     - Binary enum values are pinned. The same applies to stored enums such as SpriteBlendMode.
  4. Apply the policy to the B2 material format from its first version.
- **Effort:** S
- **Timing:** now (prerequisite of AA2-ECS-04), and the policy before B2. Step 1 edits the Public Component.hpp, so it goes in the Phase 1 † commit (or the Phase 3 window if it misses that commit).
- **Panel discussion:** RENDER confirmed that no M6B step needs a new FieldKind. SpriteComponent extensions use Vec4, and there is no Vec2 or approved Quat. ECS agreed to re-rate from high to medium. The pin is still one line, and it lands now.

#### AA2-ECS-02 Type-erased SceneCommands bypass the host-owned rule and mandatory registration; pending commands survive a failed stage into activation
- **Severity:** medium
- **Kind:** defect
- **Sources:** ECS-F2, RELOAD-10 (merged), RELOAD round 2 answer 1
- **Evidence:**
  - SceneCommands.hpp:35-48: the templates static_assert `!HostOwned` (:35-46), but the public RAVEN_API `AddComponent(EntityRef, ComponentInfo, ptr)` and RemoveComponent overloads (:47-48) check nothing. SceneCommands.cpp:48-62 records them and :142-153 applies them directly.
  - Consequence: adding ParentComponent to a root desynchronizes Scene's child index until a save and reload.
  - SceneAccess.cpp:13-17: the generic host-owned check is a hard-coded list of two IDs. ComponentInfo::Flags carries only ComponentIsTransient (Component.hpp:57-58).
  - Registry.cpp:673-699: AddComponent creates a pool with no schema. CaptureScene iterates schemas (SceneFile.cpp:836), so it skips that pool, and CheckMigration never sees it.
  - Application.cpp:587-588: a failed Gameplay stage resets only the fixed clock. Commands recorded before the failure survive to the next frame's activation (Application.cpp:395-396, 415-417).
  - After a rename migration (Registry.cpp:616-626), a stale Add under the old ID silently creates an orphan pool (Registry.cpp:678-689). After a same-ID layout change it is logged as "could not add" (SceneCommands.cpp:142-153).
- **Why it matters:** Goal 2 (FD-07 promises a compile-time rejection) and goal 3 (values silently missing from saves and migration). Editor and undo code will be type-erased by nature.
- **Fix:**
  1. Add `ComponentIsHostOwned = 2` to ComponentInfo::Flags, set from `HostOwned<T>` in `ComponentTraits::Info`.
  2. Replace SceneAccess's IsHostOwned with a test of that flag.
  3. Make SceneCommands' generic Add/Remove reject host-owned infos at record time with std::invalid_argument. That is FD-06 tier 2: the system boundary catches it.
  4. Make SceneCommands::Apply skip a type with no registered schema, logging once with the hint "declare it with systems.Component<T>()". Make Registry::AddComponent refuse the same.
  5. When the Gameplay boundary returns false, drop pending commands with a count log. Record in FD-14 that the buffer is empty between frames.
  6. Log once per (Id, LayoutHash) on a FindPool/AddComponent mismatch (ARCH-ECS-02 step 6).
- **Effort:** S
- **Timing:** steps 3-6 now (review amendment; step 3 uses SceneAccess's existing IsHostOwned list until the flag exists). Steps 1-2 add a flag to the public ComponentInfo::Flags (Component.hpp), so they land in the Phase 3 contract window.
- **Panel discussion:** RELOAD corrected the "fails silently" premise: the same-ID case is logged, and only the rename case is silent. That case is latent and needs a host-code throw inside the stage.

#### AA2-ECS-03 The world pass stamps WorldTransformComponent on every recompute, even when the matrix is unchanged
- **Severity:** medium
- **Kind:** defect
- **Sources:** ECS-F11 (new in round 2), ECS answer 2
- **Evidence:**
  - Scene.cpp:559-560 recomputes whenever a local tick is newer or the parent is WorldChanged. Scene.cpp:587-590 always writes, stamps and marks WorldChanged, so every descendant recomputes too.
  - Registry.hpp:415, 428-433: mutable views stamp every element they visit.
  - RenderWorld.cpp:119-120 then re-uploads the record. PhysicsWorld.cpp:512 treats child colliders as changed.
- **Why it matters:** Goal 4. A common game pattern, iterating every transform through a mutable view, turns the steady-state cost into the 100%-animated cost with no visible motion.
- **Fix:**
  1. At Scene.cpp:585-590, when the world already exists and neither full nor forced is set, compare the new matrix bitwise (std::memcmp, so NaN equals NaN) with the stored one. If they are equal, keep the WorldPresent flag only: no stamp and no WorldChanged.
  2. Run a same-day A/B on the 50k animated benchmark. The added cost is one 64-byte compare per recomputed node.
  3. Record the rule in ECSDesign D7/D8: derived components are stamped only when their value changes.
- **Effort:** S
- **Timing:** now, or with the first B4 RenderWorld snippet
- **Panel discussion:** ECS rejected a general "stamp only on byte change" rule for mutable views (ARCH-ECS-06), because it would tax every correct write with a copy plus memcmp. Instead the propagation stops at the derived data. The lead agrees.

#### AA2-ECS-04 Opaque value fields are invisible to LayoutHash, migration, loading and reference discovery; runtime handles are persisted
- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** ECS-F7
- **Evidence:**
  - Component.hpp:28-33, 183-192, 228-234: an opted-in value struct becomes Opaque, and only its size and alignment are hashed.
  - Registry.cpp:494-498 and SceneFile.cpp:47, 810-812 accept any Opaque field whose size matches. So `{float Hp; float Armor;}` changing to `{int Hp; float Armor;}` is silently reinterpreted.
  - GameSession.cpp:65-82 and SceneFile.cpp:373-403 find AssetRef and EntityRef only as top-level fields.
  - Sound.hpp:84-97: SoundHandle, a run-local handle, is stored in files and quick-saves.
- **Why it matters:** Goal 3. The first game-defined value struct reopens the silent reinterpretation that M6-2 closed for scalars.
- **Fix:**
  1. Add `RAVEN_VALUE(Type, fields...)` reusing ComponentFields, and hash member kinds recursively into the parent's LayoutHash. Without the macro, opaque approval is limited to engine types.
  2. Add a `FieldIsRuntime` flag for handles (Entity, SoundHandle). These fields are zeroed on capture and never matched on load.
  3. Optional, when the first value struct needs it: flatten RAVEN_VALUE members into dotted field names.
- **Effort:** M
- **Timing:** before the first game value struct (during M6B); after AA2-ECS-01
- **Panel discussion:** None.

#### AA2-ECS-05 Capturing the live world is lossy, so it is not yet the editor save or play-session copy FD-10/11/13 assume
- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** ECS-F8, GSV-12 (save-game part)
- **Evidence:**
  - SceneFile.cpp:924-928 drops sections of unregistered types. SceneFile.cpp:804-808 drops fields that no longer exist.
  - SceneFile.cpp:836-872: capture writes only registered schemas and zeroes Entity fields.
  - DataModelDesign.md:65, 347-351, 444: the docs promise that the editor saves through the same code and that a play session copies the pools.
  - Scene.cpp:180-184: node storage is sized by the registry's highest slot index.
  - GameSession.cpp:194-207: the developer quick-save already captures the live world.
- **Why it matters:** Editor and play-session readiness. An editor save from a world loaded while a module was broken would delete authored data. Streaming several scenes per world would scale with the world, not with each scene.
- **Fix:**
  1. Now, docs only: record in FD-11 that the live path drops data and must not be used as an authoring save.
  2. Before any editor save: keep unloaded sections and unknown fields in a Scene-owned side record keyed by entity ID, and merge them back on capture.
  3. Before play sessions: add `CloneInto(Scene&, Registry&)`, a pool-by-pool copy that keeps IDs and dense order and remaps Entity fields.
  4. Before streaming or additive scenes: key Scene node storage by a scene-local dense index.
- **Effort:** M
- **Timing:** step 1 now; steps 2-3 before editor or save-game work (earlier if owner question Q4 puts player save games in v1); step 4 later
- **Panel discussion:** None.

#### AA2-ECS-06 ECSDesign D9's declared system access was never built, and view iteration is single-threaded by construction
- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** ECS-F5, CORE round 2 answer 3, PERF endorsement
- **Evidence:**
  - ECSDesign.md:209-211 promises declared access.
  - SystemDesc has Name, Phase, Order and Run only (GameSystems.hpp:125-133). 22 systems are already registered.
  - View::Run does `++*state.Active` on a plain uint32 (Registry.hpp:391-398; Registry.cpp:129).
  - SystemContext is entirely main-thread: one Commands buffer, one RNG, Ui and Services (GameSession.cpp:284-286).
- **Why it matters:** The threads-study rule, and keeping the RTS option open. Retrofitting access declarations gets more expensive with every system added.
- **Fix:**
  1. Add an optional access declaration to SystemDesc that defaults to "unknown means exclusive", so existing registrations stay valid. Put it in the B2 contract window.
  2. Record the scheduling rule in D9. A system that declares nothing, or that uses Services, Ui, Actions, Stream or Random, runs exclusively on the main thread. A parallel system receives a reduced context: declared views, a per-worker Commands buffer merged in fixed order, and a per-entity derived random value.
  3. Make the iteration counter safe for concurrent read-only views (atomic, or per thread with a debug assert).
  4. Add `View::EachChunk` / ParallelEach on top of the AA2-PERF-05 range API.
  5. Leave the scheduler itself for later.
- **Effort:** M
- **Timing:** step 1 in the B2 contract window; steps 2-4 during M6B; scheduler later
- **Panel discussion:** CORE settled that Services stay main-thread with no queue now. A per-worker sound-request buffer is built only when a measured parallel system needs one.

#### AA2-ECS-07 World-pass locality decays under runtime churn, and nothing measures it
- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** ECS-F6, PERF round 2 answer 1
- **Evidence:**
  - Scene.cpp:530-591: every frame walks all depth buckets.
  - Scene.cpp:348-356, 226-228 and Registry.cpp:227-245: swap-erase and append, with no recompaction.
  - DataModelDesign implementation notes: hierarchy-random order cost 4.5 ms and took the benchmark from 165 to 151 FPS. The loader fixes order only at load (SceneFile.cpp:887-907).
  - Arena --swarm spawns 120 drones (121 movers with the craft) once and never despawns them (GameServicesDesign.md:862; ArenaComponents.hpp:84; Run.cpp:48), so it cannot reveal drift.
- **Why it matters:** Goal 4 in long sessions with constant spawn and despawn (action hordes, RTS units).
- **Fix:**
  1. Measure first: an EcsBenchmark or SamplesTool soak at 100k, spawning and despawning 1k per frame for 5 minutes, recording world-pass and sync p50/p95 every 30 s against a fresh load. Keep it outside CTest.
  2. If it drifts, add a bounded per-frame re-sort, or compaction at scene restart.
  3. Make the pass change-driven through the shared primitive from AA2-PERF-02. Keep the full walk for m_WorldFull.
- **Effort:** M
- **Timing:** measurement before B4; step 3 with B4/PERF-02
- **Panel discussion:** ECS-F6 step 3 now adopts PERF's shared primitive instead of building a world-pass-specific scan.

#### AA2-ECS-08 Carried-over ECS sharp edges, and determinism by game-side convention
- **Severity:** low
- **Kind:** defect
- **Sources:** ECS-F9; ECS-F4 steps 2-3; ARCH-ECS-05/06/07
- **Evidence:**
  - Registry.cpp:289-293: a failed journal push sets MaxBacklog to 0 permanently.
  - Registry.hpp:445-455 and SceneAccess.hpp:82-93: `Single<T>` uses a mutable view, which stamps (Drones.cpp:41, Run.cpp:18-19).
  - Component.hpp:426-432: there is no check that a component name is qualified, so `RAVEN_COMPONENT(Tag)` declared in two namespaces gives both the same TypeId.
  - Registry.cpp:838-841: ticks wrap with no epoch handling, and four consumers now advance the tick.
  - Scene.cpp:166-173 (the duplicate-name throw at 170-171) and SceneCommands.cpp:100-110 (catch invalid_argument and report): Commands call the ID overload `Scene::CreateEntity(name, id)`, and a duplicate label skips the Create with a misleading log line.
  - Registry.hpp:353-354 and ArenaSystems.hpp:79-91: view order is unspecified, so games sort by ID with allocation every step.
- **Why it matters:** Wasted stamps, rare silent failures, confusing logs, and repeated per-game sort code.
- **Fix:**
  1. In the Record catch block, set Resync = true and Cursor = End per subscriber, and leave MaxBacklog unchanged.
  2. Make `Single<T>` take `View<const T>` and return `const T*`, and add `SingleMut<T>`.
  3. `static_assert` that the name contains "::".
  4. Add one tick-type alias and an epoch on wrap. Land this with AA2-PERF-02, not "before M7", because every new consumer relies on `>` comparisons.
  5. Report "label already used" at Create, or allow shared labels.
  6. Add `SceneAccess::EachInIdOrder` backed by a host-cached ID-sorted index that the journal invalidates. Document that quick-load does not preserve dense order.
- **Effort:** S
- **Timing:** opportunistic now; step 4 with PERF-02; step 6 during M6B
- **Panel discussion:** ECS-F4 step 1 (stream splitting) moved to AA2-GP-03.

### 5.4 Hot reload and module contract (HR)

#### AA2-HR-01 Shader reload does not scale to M6B per-material shaders
- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** RELOAD-1, RND-6 (lane part), XP-6 step 6, ARCH-BLD-04, RENDER round 2 answer 1
- **Evidence:**
  - ReloadService.cpp:28 sets WorkerCount = 2, and Work() (:388-402) pops the front of one shared std::deque, m_Jobs (:536). GameModule, ShaderLibrary and SettledFileWatch (assets) all use it.
  - ShaderLibrary.cpp:173-181: each compile starts cmake -P through Platform::StartBuildProcess, then loops on a 2 ms sleep until PollBuildProcess returns. It holds a reload worker for the whole cmake/DXC/spirv-val run.
  - GameModule.cpp:109-120 (WakeConsumer) and 586-591 (Subscribe): game-source wakes queue on the same deque. A WakeConsumer import is trivial (a change notice), so a wake only waits for the backlog ahead of it to drain: about 0.65 s today for 13 programs at about 100 ms each on 2 workers (derived). It is never blocked behind one long job.
  - ShaderLibrary.cpp:134-135 subscribes with no Baseline (the comment says so), and Watch() (:274-289) creates all 13 programs. So every **develop (watching) launch** compiles every program in the background. `--no-watch` and staged runs skip this (RavenEngine.cpp:173, 188; GameSession.cpp:116-117). This is a recorded, deliberate trade-off (HotReloadDesign.md:307 and :167).
  - The startup compiles never stall a frame. The prebuilt .spv loads in the constructor (ShaderLibrary.cpp:244-252), and results identical to it are filtered out (:301).
  - ShaderLibrary.cpp:291-295: TakeReady publishes nothing while a compile is in flight. That is the intended H2 atomic-publish rule (VulkanContext.cpp:451-452): a vertex/fragment pair must never mix versions. A failed compile does not hold the gate.
  - ShaderLibrary.cpp:38-52 (ProgramDesc at 31-36) and RavenContent.cmake:50-66: a fixed 13-entry table is kept twice. The comment at RavenContent.cmake:49 admits the duplication.
  - RenderingContentDesign.md:71 says that shader edits "recompile live through DXC as an external tool". That is accurate, so there is no doc drift here.
- **Why it matters:** Goal 3 iteration speed at M6B content scale. Background compile latency in develop launches grows linearly with the number of material shaders FD-19/B4 adds. A shared-include edit queues every dependent ahead of game-code and asset reloads. The fixed ShaderProgram enum and the duplicated table must change for content-enumerated material shaders anyway, so this is a scaling gap to design into B4, not rework of existing code.
- **Fix:**
  1. Make a lane job's wait non-blocking, so no worker sleeps. ReloadConsumer::Import is synchronous today: it returns the output string and runs Check and FollowIncludes after the process exits (ShaderLibrary.cpp:184-194). Split the change into small snippets:
     - (a) add a two-phase consumer contract (start, then complete), or a service-owned process handle plus a completion callback;
     - (b) have the service poll started processes at its existing 40 ms busy interval;
     - (c) move ShaderLibrary's Check and FollowIncludes into the completion phase.
  2. Add an execution lane to ReloadOptions (Decode or Process). The process lane is bounded at max(1, hw/8), which is 2 on the 20-thread laptop. It helps only once step 1 lands, because otherwise process jobs still occupy the two decode workers, so land it after or with step 1.
  3. Add a dependencies-only startup. At startup, a full compile runs only when the digest does not match.
     - The digest covers the compiler and validator paths, OPTIMIZATION, profile and entry, the importer version, and every include.
     - Reuse the separate -MF dependency pass that CompileShader.cmake already runs for the watcher (HotReloadDesign.md:307).
     - The build rule (the add_custom_command in RavenContent.cmake) emits the .d and the digest beside each .spv. Otherwise a mismatch between build and watch flags silently skips a needed recompile.
  4. With B4: generalize `ShaderLibrary::Program` to take a ProgramDesc value (source, profile, entry, check closure) instead of the ShaderProgram enum. VulkanContext dispatches by name on ShaderProgram values (VulkanContext.cpp:440-470: IsLitProgram, Scatter, Cull, DebugLine*, Overlay*), so either the generated table also generates the enum, or those call sites change in the same snippet.
  5. With B4: generate the engine program table once from a CMake-read data file, which closes BLD-04. Game material shaders never go in this table; they enumerate from content (AA2-RND-06). Steps 4-5 are partly the same work as B4's material-shader enumeration, so schedule them together to avoid double edits.
  6. With B4: replace the "all settled" gate with per-trigger publish groups. Keep the H2 guarantee: a vertex/fragment pair, and all dependents of one header edit, publish together.
  7. Record steps 1-6 as delegated in the B4 row and the threads table.
- **Effort:** M-L. Steps 1-6 together touch ReloadService's consumer API, the CMake shader rules, ShaderLibrary and VulkanContext.
- **Timing:** steps 1-3 before B4 (they can land now); steps 4-6 with B4
- **Panel discussion:**
  - RELOAD lowered its first lane size from hw/4 to hw/8 so that it fits PERF's pool background limit within one throttle table. The lead adopts the revised value; see AA2-PERF-03.
  - RENDER added the input-digest requirement, because a source edited while the host was closed must still recompile.
- **Verification:** confirmed with corrections; severity high -> medium, because the cost is background latency in develop launches with no frame stall and no current goal risk, and the enum generalization is inherent B4 work, not rework. The doc-drift claim was dropped (RenderingContentDesign.md:288 does not exist; :71 is accurate), and the non-blocking wait was split into small steps and ordered before the lane.

#### AA2-HR-02 On ELF with GCC, a new module's ComponentTraits<T>::Info can bind to an old generation's copy, so migration would compare old with old
- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** RELOAD-11 (new in round 2), XP-3 (game-module part), ARCH-PLAT-03
- **Evidence:**
  - Component.hpp:366-390: Info (382-390), FieldTable (366) and AliasTable (374) are static constexpr members of a class template. They are implicitly inline and have vague linkage.
  - GameSystems.hpp:150-156: `SystemTable::Component<T>` binds `ComponentTraits<T>::Info` by const& in its AddComponent call (:156), which is an odr-use that takes its address. The per-frame accessors pass the same Info to the host (Registry.hpp:221-283; SceneAccess.hpp:40-72), so a unified Info would hand the host a stale Size on every Read and Write, not only at registration.
  - GameModule.cpp, in Activate (from :408): the candidate is created at :418, loaded at :432 and registered at :446, and it becomes active only at `Active.swap(candidate)` (:480). The old image stays loaded until then. CheckComponents, Migrate and RegisterSchema (:466, :469, :472) trust the schemas the candidate reports.
  - Each generation loads from a fresh `.raven-game-N` copy (GameModule.cpp:421-427). With unique binding, every later generation would bind to the **first** generation's Info, not just the previous one's.
  - No CMake file outside ThirdParty sets CXX_VISIBILITY_PRESET, VISIBILITY_INLINES_HIDDEN or -fno-gnu-unique (grep). RavenGame.cmake sets no visibility, and Api.hpp:10, 20 uses visibility("default"). No doc records the module contract or this loader precondition, so ARCH-PLAT-03 (2026-11-23, medium) was not acted on.
  - A grep of Games/*/Source for `static` (excluding constexpr, static_assert and static_cast) and `thread_local` finds nothing, so today's games already comply with the contract below.
  - Nothing triggers this today: UnsupportedGameModule.cpp:7-8 throws, so no POSIX loader exists, and PE has no interposition.
- **Why it matters:** Goal 3 on Linux, once M4 adds the loader.
  - **The mechanism is GCC-specific.** GCC emits non-artificial COMDAT variables as STB_GNU_UNIQUE, and glibc binds those across RTLD_LOCAL images and makes the image undeletable, so dlclose is a no-op. Clang does not emit STB_GNU_UNIQUE, and the linux-base preset (CMakePresets.json:70-79) pins no compiler. The macOS (dyld weak coalescing) part is unverified.
  - **A size-changing component edit fails loudly, not silently.** The new module sizes its defaults with its own `sizeof(T)` (GameSystems.hpp:152), and MakeComponentSchema rejects `defaults.size() != info.Size` (Registry.cpp:419-421). The error message is misleading: "Component registration needs a name, a size, defaults and its field table".
  - **An equal-size edit corrupts silently.** This covers a reorder of same-size fields, or a type swap such as float to int32. CaptureDefaults compares field addresses index by index with the possibly-unified old FieldTable (Component.hpp:399-418), and CheckMigration's IsSameLayout (Registry.cpp:538) then sees an unchanged layout. The new code reads old-layout bytes.
- **Fix:**
  1. In the Phase 3 window (RavenGame.cmake is a contract input), and a no-op on MSVC: set CXX_VISIBILITY_PRESET hidden and VISIBILITY_INLINES_HIDDEN on the game target in RavenGame.cmake. The RAVEN_GAME_API exports keep default visibility. Off Windows, this takes effect only once the `NOT WIN32` FATAL_ERROR gate at RavenGame.cmake:4 is removed (AA2-PLAT-02 step 2), so sequence the two together.
  2. Now: add a loader-seam guard in SystemTable::AddComponent (GameModule.cpp:309-312), before MakeComponentSchema. Test the address of the passed ComponentInfo object (`&info`) against the candidate image, and reject with a clear message if it lies outside. Do not test schema Fields pointers: MakeComponentSchema copies names and offsets into owned strings and vectors (Registry.cpp:436-440), and Info.Fields is null or meaningless for empty tag components.
     - Declare the seam function, for example `ImageContains`, in Platform/GameModulePlatform.hpp.
     - Implement it in Platform/Windows/Win32GameModule.cpp with GetModuleHandleExW(FROM_ADDRESS | UNCHANGED_REFCOUNT).
     - Stub it in Platform/UnsupportedGameModule.cpp.
     - The POSIX version uses dladdr with the M4 loader.
  3. Now: record the module contract in EngineModules.md as a cross-platform rule, with the reasons:
     - two exports only;
     - no thread_local, because on macOS TLV prevents dylib unload;
     - no statics with non-trivial destructors, because they would run at unload against host-owned state;
     - no state expected to carry across generations.
  4. With the M4 loader: -fno-gnu-unique on GCC, and RTLD_NOW | RTLD_LOCAL.
  5. With the M4 loader: a CI check on the game .so using readelf (no UNIQUE or TLS symbols, two exported functions).
  6. With the M4 loader: verify on the first macOS build whether dyld coalesces weak definitions across dlopen'd images.
- **Effort:** S
- **Timing:** steps 2-3 now (cheap; step 3 is Phase 0 docs). Step 1 in the Phase 3 hash-only batch together with AA2-PLAT-02 step 2, because RavenGame.cmake is a contract input (GameContract.cmake:6-13). Steps 4-6 with the M4 loader.
- **Panel discussion:** BUILD and CORE endorsed this as a precondition, not hygiene. The panel rated it high because the code path is certain to run once M4 lands. Verification lowered it, because steps 3 and 5 already guard against M4 forgetting it.
- **Verification:** confirmed with corrections; severity high -> medium, because nothing triggers it today and the fix is two CMake properties plus a guard, which is the same latent class ARCH-PLAT-03 rated medium. The corruption is limited to equal-size edits, and size changes are rejected loudly. Do it now anyway because it is cheap. The guard moved to AddComponent and tests `&info`.

#### AA2-HR-03 Manual builds and F5 race the linker, and a failed auto-build blocks offers until the next source edit
- **Severity:** medium
- **Kind:** defect
- **Sources:** RELOAD-2, ARCH-HR-01
- **Evidence:**
  - GameModule.cpp:593-594: the DLL itself is the watched output. Builds are paused only around builds the host started itself (:641), but EngineModules.md:73 documents manual `cmake -P .../BuildGame.cmake` builds.
  - Application.cpp:415-417 and GameModule.cpp:693-696: F5 calls Activate with no IsBuilding check.
  - GameModule.cpp:609-612, 649-653: after a failure, nothing is offered until a source edit.
  - HotReloadDesign.md:306: this has already caused an LNK1104.
- **Why it matters:** Goal 3 reliability in the documented workflow. No wrong code loads, but spurious failures persist.
- **Fix:**
  1. While `IsBuilding()`, F5 logs "build in progress; it activates when done" and does nothing else.
  2. Watch `GameBuild/<Name>/BuildInputs.sha256` (named at BuildGame.cmake.in:7) instead of the DLL. Every successful build writes it (:188-189) after verification (:180-183).
  3. Clear `buildFailed` when that stamp changes, and on F5.
  4. Check with one ignored probe. No tracked test.
- **Effort:** S
- **Timing:** now (review amendment)
- **Panel discussion:** DOCS's "partly fixed" ledger entry was wrong and has been corrected.

#### AA2-HR-04 The reload service and the asset store hold and re-read every watched byte
- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** RELOAD-3, MEDIA-N2, MEDIA round 2 answer 2, ARCH-HR-02
- **Evidence:**
  - ReloadService.cpp:27, 315-338: every 2 s the service reads the full content of every input.
  - ReloadService.cpp:186, 346, 355, 369, 435: content is retained in Observed, copied into each Job, and re-read after each import.
  - ReloadService.cpp:31-40 and GameModule.cpp:37-57: hashing is byte-wise FNV.
  - Win32DirectoryWatch.cpp:31-32, 43-45: the service falls back to 40 ms polling with no log line.
  - AssetStore.cpp:44, 125: every leased asset keeps its encoded file in ManagedAsset::Bytes, players included.
  - AssetStore.cpp:131-133 and SettledFileWatch.hpp:33-41: a second, by-value copy is taken as the baseline.
  - AssetStore.cpp:73-76, 250: IsCurrent re-reads the whole file on the main thread on every published reload (called from GameSession.cpp:174).
  - ReloadService.cpp:29: there is a 64 MiB default cap.
- **Why it matters:**
  - Goal 3 at real content scale. 500 MB of watched content would cost about 1 GB of RAM plus about 250 MB/s of idle reads (derived).
  - Players keep encoded files resident in RAM.
  - A 64 MiB FBX reload stalls the frame thread.
- **Fix:**
  1. Observed keeps only {size, mtime, 64-bit digest} per input. Jobs carry paths, and the worker reads the file when it runs. The currency check compares digests.
  2. ManagedAsset keeps {size, digest}, computed by the decode worker, instead of Bytes. IsCurrent compares digests. A digest is accepted as the subscription baseline.
  3. The audit becomes a budgeted round-robin of about 4 MiB per 2 s pass.
  4. Log once, with the reason, when the directory watch is unavailable.
  5. Replace byte-wise FNV with an in-house word-wise 64-bit hash in ReloadService and GameModule, before anything keys on it.
  6. Make the per-file cap a per-consumer option: 64 MiB for textures and audio, 256 MiB opt-in for models.
  7. The recursive-root directory watch contract goes with M4.
  8. Acceptance measurement on the laptop, 1 GiB watched: idle CPU under 1% of one core, RSS overhead under 32 MiB, and no main-thread file read on commit.
- **Effort:** M
- **Timing:** steps 1-6 before B5/B6/B7; step 7 in M4
- **Panel discussion:** The laptop's RAM size is not recorded in the repo. The acceptance numbers assume 16 GB and must be confirmed.

#### AA2-HR-05 Execution copies and shader scratch directories leak; players and --no-watch load through the development copy path
- **Severity:** medium
- **Kind:** defect
- **Sources:** RELOAD-5, CORE-7 (steps 1-2), ARCH-HR-03
- **Evidence:**
  - GameModule.cpp:418-430: every load creates a `.raven-game-N` copy, and only the destructor cleans it up (:214-225). That cleanup already existed at M5.
  - ShaderLibrary.cpp:125, 147-148: each program creates one `%TEMP%/raven-shader-<uuid>` per run.
  - Counted today: 33 copies in x64-debug, 58 in x64-release, and 243 raven-shader-* directories.
  - RavenEngine.cpp:110-112, 168-171: players use the copy path. PlayerBuild.md:9 requires a writable install folder, and a read-only install throws at startup (GameModule.cpp:687-688).
- **Why it matters:** M7 read-only installs, unbounded disk use, and the first runnable POSIX target (AA2-PLAT-01 chain step c).
- **Fix:**
  1. At GameModule construction, try `remove_all` on sibling `.raven-game-*` directories. Directories still in use fail harmlessly because their DLL is locked.
  2. Put ShaderLibrary scratch under `%TEMP%/raven-shader/<pid>-<uuid>`. The first Watch() sweeps folders whose process is gone.
  3. Extract `LoadGeneration(imagePath)` from GameModule.cpp:432-483. Activate keeps the stamp, copy and revision guard and calls it. This is a move with no behavior change.
  4. When `!WatchGameModule` (players, --no-watch), load in place with no copy. Poll and Reload are no-ops and the worker never starts.
  5. Update PlayerBuild.md:9 and EngineModules.md for the two modes.
- **Effort:** S
- **Timing:** now (steps 1-5)
- **Panel discussion:** RELOAD moved the in-place mode from "before M7" to "now", because it gives the M4 dlopen loader a first target that needs no copy, stamp or retry semantics. The lead agrees.

#### AA2-HR-06 Activation hashes, copies and re-hashes the DLL on the frame thread (about 50 ms per reload)
- **Severity:** low
- **Kind:** defect
- **Sources:** RELOAD-7, ARCH-HR-05, ECS round 2 answer 4
- **Evidence:**
  - GameModule.cpp:415-430 runs in Poll on the frame thread (Application.cpp:395-396).
  - GameServicesDesign.md:970 records "about 50 ms".
  - Migration itself is small: 1.0-1.6 ms at 100k (DataModelDesign.md:663).
- **Why it matters:** Goal 3 feel. Each reload causes a visible hitch followed by fixed-step catch-up.
- **Fix:**
  1. After a successful build, the worker stages and verifies the copy and publishes {copy path, digest}.
  2. Activate only calls LoadGeneration. F5 uses the same staging function synchronously.
  3. Report Migrate as its own phase in the RAVEN_RELOAD_TRACE line, and record one "activation hitch" row.
- **Effort:** S
- **Timing:** during M6B, or together with AA2-HR-03
- **Panel discussion:** ECS showed that preparing migration off-thread is not worth it: it would need a frame barrier plus a copy to save 1-2 ms.

#### AA2-HR-07 The game DLL loader does not search the application directory
- **Severity:** low
- **Kind:** architecture-gap
- **Sources:** RELOAD-8
- **Evidence:** Win32GameModule.cpp:84-85 uses only DLL_LOAD_DIR and SYSTEM32. The load directory is `.raven-game-N` (GameModule.cpp:424-426).
- **Why it matters:** A game cannot link its own middleware DLL. Latent.
- **Fix:**
  1. Use LOAD_LIBRARY_SEARCH_DEFAULT_DIRS.
  2. Document in EngineModules.md where a game's own DLLs ship.
  3. Note the `$ORIGIN` equivalent in PlatformHandoff.
- **Effort:** S
- **Timing:** when the first game links a native dependency, at the latest M7
- **Panel discussion:** None.

#### AA2-HR-08 Background game builds run at normal priority with unbounded ninja parallelism
- **Severity:** low
- **Kind:** architecture-gap
- **Sources:** RELOAD-12
- **Evidence:**
  - BuildGame.cmake.in:167 passes no `-j`.
  - Win32GameModule.cpp:156-157 (StartBuildProcess) sets no priority class.
  - JobSystem.cpp:26-37 starts 19 pool workers on 20 hardware threads.
- **Why it matters:** Each edit-observe cycle hitches the running game.
- **Fix:**
  1. Measure frame p95 and reload latency during a game edit.
  2. Try BELOW_NORMAL priority and `-j max(2, hw/2)`. Adopt only if reload latency rises by no more than about 10%.
  3. Add the result to the throttle table (AA2-PERF-03).
- **Effort:** S
- **Timing:** during M6B, with the B4 process lane
- **Panel discussion:** None.

### 5.5 Renderer and M6B readiness (RND)

The current GPU-driven core is sound for today's content: about two unique meshes, one lit pipeline, inline materials, and sprites on a CPU side path. The weak point is the draft M6B note. The first four findings below are the "B0 contracts". Write them into RenderingContentDesign.md before any B2 code. Verification lowered all four from high to medium. They are specification gaps in an unapproved draft for scheduled work. No B-step code exists yet, so correcting the plan now costs a doc revision, not rework.

#### AA2-RND-01 The lit fragment ABI cannot yet reach FD-19's material parameters, and the draft does not say how it will
- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** RND-1
- **Evidence:**
  - Material.hlsli:13-24: RavenFragmentInput has locations 0-6 (Color, UV, Normal, AmbientAndLit, LightDirection, LightColor, Texture). It has no material index and no world position.
  - Color.vert.hlsl:45, 51, 58: the vertex stage reads Materials[object.Material], folds BaseColor into Color, and forwards only `material.Texture`.
  - VulkanGpuScene.cpp:114: the Materials binding is VK_SHADER_STAGE_VERTEX_BIT only.
  - FragmentShaderInterface.cpp:191-193: any UniformConstant other than set 0 bindings 0/1 is rejected. litWidths (:70) fixes the lit inputs at seven locations.
  - VulkanFrameData.hpp:18-26 (static_asserts at 28-33): FrameUniformData has no time value. Its set layout is vertex-only (VulkanFrameData.cpp:13-14).
  - RenderWorldState.hpp:40-50: GpuMaterialData is BaseColor, Texture, Lit and padding, static_asserted to 32 bytes.
  - VulkanContext.cpp:360: the lit vertex outputs are fixed at locations 0-6.
  - RenderingContentDesign.md:77-84, 211: B4 names "the parameter record", and FD-19 decides it: four float4 values and four texture indices. Neither says how the fragment stage reaches it: a material index or forwarded values, binding visibility, or checker changes.
  - Mitigating: the checker accepts shaders that declare a subset of inputs (FragmentShaderInterface.cpp:238-242), so appending locations later does not break shaders built on the header. GpuDrivenDesign.md:120 (G4) already chooses to grow the header one step at a time. Only Color, Unlit, Normals and Wireframe include Material.hlsli; no game material shaders exist yet.
- **Why it matters:** The M6B exit item "one custom material shader" cannot be met by today's code, and the draft does not specify the fragment-side plumbing B4 needs. If B4 starts without it, the first material shader is written against an ad-hoc interface. The rework risk is small and latent, because the header grows additively.
- **Fix:**
  1. Material.hlsli: add `nointerpolation uint Material` and `float3 WorldPosition`. Add accessors `RavenMaterialValue(i)` and `RavenMaterialTexture(i, uv)`, and a RavenFrame struct (time, delta, camera position). Do not reserve a shadow slot. With WorldPosition available, shadow coordinates come from the frame uniform in the fragment stage, and appending a location later does not break shaders.
  2. Widen GpuMaterialData to BaseColor, 4 float4 Values, 4 texture indices, 4 sampler indices and flags. The sampler slots stay at the single clamp sampler until B3's FD-21 sampler set lands, so this snippet implies no B3 work. Update the static_asserts, GetBufferLayouts and GpuScene.hlsli in the same snippet.
  3. Add Time to FrameUniformData. Make the frame-uniform set layout (VulkanFrameData.cpp:14) and the Materials binding (VulkanGpuScene.cpp:114) fragment-visible, and update LitVertexInterface and DebugLineVertexInterface.
  4. Let CheckFragmentShaderInterface accept the new read-only bindings and locations.
  5. Port Color, Unlit, Normals and Wireframe frag to the accessors.
  6. Record the ABI as versioned in GpuDrivenDesign G4. Validate with one capture comparison at M2 tolerance.
- **Effort:** M
- **Timing:** draft revision now. In code, only the GpuMaterialData widening (step 2) has to land before B2's material table. Steps 1 and 3-6 stay with B4 as its first snippet, because the ABI first matters there.
- **Panel discussion:** RENDER noted that GPU-side interpolation would change the lit vertex ABI. The CPU-override decision in AA2-PERF-01 keeps ABI v2 as specified here.
- **Verification:** confirmed with corrections; severity high -> medium, because this is a specification gap for scheduled work: FD-19 already decides the record, and the subset-tolerant checker makes later growth non-breaking. The "every shader breaks again" claim and the reserved shadow slot were dropped, and the frame-uniform citation was fixed (VulkanFrameData.hpp:18-26; .cpp:13-14).

#### AA2-RND-02 (pipeline, mesh) group identity, retirement and command index are under-specified
- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** RND-2, ARCH-RND-01, ECS round 2 answer 1 (Tint)
- **Evidence:**
  - RenderWorld.cpp:162-171: FindGroup is a linear scan over State::Groups and pushes a new group when it finds no match.
  - RenderWorld.cpp:146 and RenderWorldState.hpp:84-86: groups are append-only and never retired. Remove only decrements GroupCounts, and only Clear or a rebuild empties them (RenderWorld.cpp:69).
  - RenderWorld.cpp:205-206: the group is resolved only on add or mesh change.
  - VulkanGpuScene.cpp:298-316 (the per-group resolve call at :301) and VulkanGpuScene.hpp:29: Plan resolves every group through a std::function MeshResolver every frame. :425-426 copies the mesh table and the templates every frame.
  - Cull.comp.hlsl:28, 47-48: both Meshes and Commands are indexed by object.Group, so the command index equals the group index.
  - VulkanGpuScene.cpp:477-478: one vkCmdDrawIndexedIndirect with drawCount = group count. VulkanColorPipeline.cpp:73: cullMode NONE for everything.
  - StaticMeshComponent holds both Mesh and Tint (StaticMeshComponent.hpp:14-16). Game code writes Tint through mutable access (Games/Samples/Source/Interaction.cpp:37, 85, 142). That stamps the StaticMesh tick, so ApplyTicks (RenderWorld.cpp:118-120) passes meshChanged=true, and Write re-runs the mesh resolver lambda (Application.cpp:627-633, scene.TryUseAsset) and FindGroup's linear scan.
  - RenderingContentDesign.md:211 (B4) already plans "draw grouping by (pipeline, mesh), one indirect draw per pipeline". FD-19 (:70, :72) records the single-pipeline state and the owner's Q3 decision. What is missing is the detail: group identity and retirement, decoupling the command index from the group index, and pipeline-contiguous ranges.
  - GpuDrivenDesign.md:140 names an ~1,000-unique-mesh trigger for adding DrawIndexedIndirectCount compaction, not for redesigning grouping. B7's multi-object FBX can reach that with one model. GpuDrivenDesign.md:265: the stress scenes have only two unique meshes.
  - Cost today is small. Per-group per-frame work is one std::function call, a hash lookup and about 68 bytes of ring traffic: tens of microseconds and about 136 KB per frame at about 2,000 groups (derived). The linear FindGroup mostly costs at load or on spawn: tens of milliseconds for 50k adds against about 2k groups (derived). There is no current trigger: two meshes, and mesh hot reload keeps the same MeshHandle (AssetResources.cpp:150, ReplaceMesh), so reload never accumulates groups.
- **Why it matters:** Goal 4 is measured only with two unique meshes (GpuDrivenDesign.md:265); that is an evidence gap, not a demonstrated failure. If "command index == group index" survives into B4, B4 is rewritten when per-pipeline ranges and retirement arrive, which the no-throwaway rule forbids.
- **Fix:**
  1. Group key = (pipeline key, MeshHandle), where the pipeline key is (material shader asset, blend class, cull mode). Use a hash map. Recompute the group when the material changes as well as when the mesh changes. The default cull mode stays NONE (double-sided), as today (VulkanColorPipeline.cpp:73), so single-sided assets look the same unless a material opts into back-face culling.
  2. Keep a per-slot `MeshRefs` vector and resolve only when the AssetRef changes, so a Tint-only change takes the cheap path. Keep Tint on StaticMeshComponent: moving it would drop authored values, because FD-16 has no converter.
  3. Retire count-0 groups to a free list and give their slots empty commands, so IDs stay stable.
  4. Store CommandIndex in MeshData. Plan buckets commands contiguously per pipeline. Cull writes `Commands[mesh.CommandIndex]`.
  5. Issue one indirect draw per pipeline range. Debug views draw all ranges with one engine pipeline.
  6. Make the mesh table a persistent device buffer, re-uploaded only when a group is added or retired or a mesh revision changes.
     - Sub-step first: add a revision counter that ReplaceMesh bumps. Today ReplaceMesh keeps the MeshHandle (AssetResources.cpp:150), and mesh reload only works because Plan re-resolves ranges and bounds every frame. Without the counter, hot-reloaded FBX ranges go stale.
     - Keep the command templates on a per-frame GPU-side reset. Cull accumulates InstanceCount with atomics into Commands, so the buffer must be reset every frame. Each template's firstInstance depends on GroupCounts, which change on every spawn or despawn. Use a device-local template buffer copied into Commands, or a small clear pass, and rebuild the templates whenever counts change.
  7. Skinning (AA2-RND-09): record only the design constraint "do not widen the 128-byte record; a per-object vertex-base override would go in a parallel stream". Add no stream code unless owner question Q3 puts skinning in v1.
  8. Measurements: at B4, 50k objects with 256 meshes, 64 materials and 4 shaders. At B7/B8, one 2,000-part FBX scene, because multi-object FBX arrives only in B7.
- **Effort:** M
- **Timing:** design now; code as the first RenderWorld snippet of B4, sequenced before B2's material keying. This finding owns that reorder (AA2-RND-04 refers here).
- **Panel discussion:** ECS chose comparing AssetRefs over moving Tint, for migration reasons. The lead agrees.
- **Verification:** confirmed with corrections; severity high -> medium, because this is a latent design gap for the next step, with small per-frame cost today and no current trigger, and B4 already plans the grouping. Step 6 was corrected to keep a per-frame template reset and to add a mesh revision counter, step 7 became conditional on Q3, and the FBX measurement moved to B7/B8.

#### AA2-RND-03 The FD-20 sprite plan does not fit the code, and it changes visible behavior
- **Severity:** medium
- **Kind:** process (a flawed draft plan; the current code is self-consistent)
- **Sources:** RND-3; RENDER and MEDIA round 2 verification
- **Evidence:**
  - Overlay.hpp:24-43 (OverlayQuad, 112 bytes of pixel-space fields), Overlay.vert.hlsl:9-20, 36-41 (clip position from pixel / Screen.Size at :41) and VulkanContext.cpp:384-392: overlay quads are screen-space per-instance vertex attributes with no world transform and no descriptor buffers. The comment at Overlay.hpp:11-13 says M6B's world-space sprites will share this format.
  - Color.vert.hlsl:44: the culled path reads `Objects[Visible[i]]` from storage buffers, so the overlay format cannot be culled on the GPU.
  - RenderWorldState.hpp:21-37: the 128-byte object record has 4 spare bytes (one uint32 Padding) and no UV rect.
  - SpriteComponent.hpp:11-28: Texture, Tint, BlendMode, SortLayer and SortOrder only; there is no UV rect, size or pivot. The quad's size comes from the world transform's scale on the unit quad (GameSession.cpp:431).
  - Opaque sprites:
    - They drop SortLayer and SortOrder (GameSession.cpp:432-435).
    - They are depth-tested and depth-written by real Z (VulkanTexturePipeline.cpp:90-93).
    - In the Samples scene they sit among 3D meshes (SampleSceneBuilder.cpp:215-217).
  - Only blended sprites sort by layer, order and depth (VulkanDrawQueue.cpp:110-122, the sort at 113-122).
  - There is no alpha test anywhere: the only `discard` in engine shaders is in Overlay.frag.hlsl:31.
  - So RenderingContentDesign.md:101's layer-to-depth mapping for "cut-out (alpha-tested)" sprites would change what is on screen, including overlap with meshes. That contradicts :105 ("both behaviors players see today ... stay"). Line :102, about blended sprites, is consistent with today.
  - VulkanContext.cpp:52-60, 72-78, 685-690: a per-draw ownership scan over both the textured-mesh list and the texture list, O(draws x (texturedMeshes + textures)). Its cost is unmeasured. Courtyard has 3 texture files, so each draw makes about ten pointer compares, which is less than the per-draw TransformBounds, PrepareTransform and frustum work (VulkanDrawQueue.cpp:94-105).
- **Why it matters:** B5 is an M6B exit item, and sprites are the 2D-secondary goal and the base for VFX and billboards. Building B5 on the draft as written would change visible 2D and mixed 2D/3D output, and would try to cull a format that the GPU path cannot read.
- **Fix:**
  1. Opaque and cut-out sprites become render-world objects: a shared quad mesh group, a material (texture plus blend class), and the existing cull and indirect path. Reuse the overlay only for shared fragment helpers in an hlsli. **Blended sprites do not take the indirect path.** It draws in group order, has no blend pipeline, and Color.frag.hlsl:6 forces alpha 1.0. As RenderingContentDesign.md:102 already says, blended sprites stay sorted back-to-front on the CPU, now from render-world data, and are drawn through a blend pipeline.
  2. Conditional on a yes to owner question Q8: put per-sprite frame data in a parallel SpriteData stream indexed by slot (UV rect, pivot), so mesh records stay at 128 bytes. Measure its upload at 50k.
  3. Keep Opaque exactly as it is today: real Z, no discard. If cut-outs are wanted (Q9), append `Cutout` as a new SpriteBlendMode value so stored ordinals stay valid. Layers order only blended sprites.
  4. Conditional on a yes to Q8: extend SpriteComponent with existing kinds only (Vec4 UvRect, Vec4 SizePivot). The defaults reproduce today's output: UV rect 0,0,1,1, size 1 and a centered pivot, multiplied with the world transform.
  5. Measure B1 on today's path, ownership scan included, because a baseline should capture what ships. Do not add a context-id check to the CPU textured path: that path, and the scan with it, is deleted at B5 (AA2-CORE-04 step 4, as ARCH-PERF-05 planned). VulkanTexturedDraw holds references, not handles (VulkanTexturedDraw.hpp:13-14), so such a check would not be a drop-in change anyway.
  6. Correct RenderingContentDesign.md:101 and :105. In an owner-applied engine snippet, also correct the comment at Overlay.hpp:11-13, and the UIDesign.md "feed 2" sprite-sharing statement that RenderingContentDesign.md:95 cites.
- **Effort:** M
- **Timing:** draft revision now; code at B5
- **Panel discussion:** MEDIA independently confirmed the depth behavior and the missing alpha test. Whether sprite sheets and cut-out layering are in v1 are owner questions Q8 and Q9.
- **Verification:** confirmed with corrections; severity high -> medium, because the flaw sits in an unapproved draft for the secondary 2D path, there is no code defect, and correcting the plan before B5 costs only a doc revision. Blended sprites now explicitly keep CPU sorting, the pre-B1 scan removal was dropped as throwaway, and steps 2 and 4 depend on Q8.

#### AA2-RND-04 The M6B draft puts mips before the upload batch and records no upload or descriptor contract, and the batch would break the drain-dependent descriptor swap
- **Severity:** medium
- **Kind:** process
- **Sources:** RND-4, AUI-6 step 2, MEDIA round 2 answer 3, ARCH-RND-04
- **Evidence:**
  - VulkanTexture.cpp:81-95 (SubmitAndWait): each upload calls vkQueueSubmit with a fence (:88) and an infinite vkWaitForFences (:90-91) on the graphics queue (:223). The constructor (:124-264) calls it for every texture (:257), including managed ones (VulkanContext.cpp:610-621, 817-821).
  - VulkanContext.cpp:875-887 and AssetStore.cpp:78-84, 249-255: a reload's Commit calls SwapTextureContents, which swaps the images and rewrites the live bindless elements in place (:884-885). No frame runs between Load and Commit.
  - There is exactly one bindless set, shared by all frames in flight (VulkanTextureDescriptors.cpp:87-93; writes at :109, :229), flagged UPDATE_AFTER_BIND and UPDATE_UNUSED_WHILE_PENDING (:46-47 comment, :55-58 flags). The in-place rewrite is legal only because the upload fence wait (VulkanTexture.cpp:88-91) covers earlier submissions on the same queue, which VulkanContext.cpp:644 states. A batch without that wait would break texture reload silently, and with one shared set, any in-place rewrite always needs a full timeline wait.
  - RavenEngine/Public/Raven/Renderer/Vulkan/VulkanContext.hpp:86: the "drain" comment also covers SwapMeshContents, but mesh uploads are non-blocking timeline submissions (VulkanMesh.cpp:278-296). The comment is wrong for meshes.
  - RenderingContentDesign.md:137, 208-214: B3 (mips) comes before B6 (the batch). B2 and B4 both touch RenderWorld.cpp:162-231 (FindGroup 162-171, FindMaterial 173-193, Write 196-231).
  - TextSystem.cpp:612-615: each new glyph page builds a 1024x1024x4 zero vector (4 MB, PageSize at :39) and calls the synchronous CreateManagedTexture.
  - RenderWorld.cpp:179-181 stores the raw bindless index in the material record, so FD-22 placeholders filled by swap inherit the same hazard.
  - ARCH-RND-04 already rated this risk medium and named it ("Batching or async uploads would silently break the descriptor-swap invariant"). What is new is that the M6B draft still records no upload or descriptor contract.
- **Why it matters:**
  - Today's code is spec-correct, and nothing triggers the hazard until B6 removes the drain. After that, texture hot reload becomes a silent GPU hazard unless the contract is written down first.
  - The no-throwaway cost of the step order is small. B3's mip blit chain is barriers and blits recorded into a command buffer, so it can take any command buffer. Only the submission site changes in B6, and it changes whether or not B3 lands first.
- **Fix:**
  1. Make the per-frame upload batch the first texture snippet, inside VulkanContext's texture creation path rather than AssetStore. Use one command buffer that signals the timeline, a vkCmdClearColorImage for textures created cleared, and the bindless element handed out at once. The clear replaces the 4 MB CPU zero vector; VK_IMAGE_USAGE_TRANSFER_DST_BIT is already set (VulkanTexture.cpp:157-158). Assets, glyph pages, the white texture and UI images are all covered. Handing the element out at once is safe only because the batch is submitted on the same queue, before the frame that first samples it, and ends in a transfer-to-fragment barrier. That ordering is part of the contract in step 6.
  2. B3 records the mip blit chain into a caller-supplied command buffer (the batch), after a format-feature check.
  3. Split by use, delegated:
     - **Development texture reload:** once B6 removes the drain, SwapTextureContents waits explicitly on the timeline before rewriting the element (ARCH-RND-04 step 2). A rare one-frame stall is acceptable for development reload, and it needs no RenderWorld change.
     - **Placeholder fill and streaming:** these get a new bindless element, because FD-22 promises no stall. Material and sprite entries switch to it through one per-AssetId "content revision" event, and the old element is retired through the timeline. This depends on AA2-RND-05 steps 2-4: material entries keyed by asset, with dirty upload. Today the table is append-only with tail-only upload (VulkanGpuScene.cpp:291-292) and stores raw indices (RenderWorld.cpp:179-181). So this part is not sequenced before B2's material map.
  4. Fix the VulkanContext.hpp:86 comment for meshes.
  5. The B4-before-B2 RenderWorld reorder is owned by AA2-RND-02.
  6. Record the upload and descriptor contract in RendererDesign.md: same-queue ordering, the transfer-to-fragment barrier, the reload wait, and new-element retirement.
- **Effort:** S (draft); M (code)
- **Timing:** draft now; code in B3 and B6, with step 3's new-element path after B2's material map
- **Panel discussion:** MEDIA corrected the premise that FD-22 needs a new invalidation path. A stable managed handle filled by swap is picked up with no RenderWorld change, because the mesh table is re-resolved every frame (VulkanGpuScene.cpp:298-316). The only dependency is step 3.
- **Verification:** confirmed with corrections; severity high -> medium, because today's code is correct, the hazard has no trigger until B6, the same risk was already rated medium as ARCH-RND-04, and only the submission site, not B3's mip code, is rework. Step 3 was split into a development-reload wait and a placeholder/streaming new-element path that depends on RND-05, and the duplicate reorder step now points to RND-02.

#### AA2-RND-05 The material table is append-only with tail-only upload and raw bindless indices
- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** RND-5, ARCH-RND-02, ECS verification
- **Evidence:**
  - RenderWorldState.hpp:81 states the table is append-only.
  - RenderWorld.cpp:173-193 uses an exact float compare, so a NaN BaseColor never matches and appends on every write.
  - VulkanGpuScene.cpp:291-292, 368-370, 427-428 upload only the tail.
  - RenderWorld.cpp:179-181 stores a raw index with no lease. VulkanTextureDescriptors.cpp:196-216 reuses freed elements without resetting them to white.
- **Why it matters:** Material hot reload (goal 3, an M6B exit item) cannot work without a full 50k re-upload. Reused elements would sample the wrong texture.
- **Fix:**
  1. Now: ReleaseBinding writes white into the freed element.
  2. B2: a material map keyed by AssetId, plus one default entry per distinct inline value. Entries hold their texture leases and carry dirty flags and a free list.
  3. Upload dirty entries through Scatter, with the record stride as a parameter.
  4. Content-revision events (AA2-RND-04 step 3) mark only the affected entries dirty.
  5. RenderWorld::Write hides objects whose Tint or inline values are non-finite, logging once per entity. This closes the NaN growth.
  6. The .ravenmaterial parser rejects non-finite values and unknown parameters and keeps last-good.
- **Effort:** M
- **Timing:** step 1 now; the rest at B2
- **Panel discussion:** None.

#### AA2-RND-06 Material shaders as game content have no build rule, watch path, per-shader publish or fallback
- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** RND-6, RENDER round 2 answer 1
- **Evidence:**
  - ShaderLibrary.hpp:18-23, 50: programs are a fixed engine enum, and the watch is Windows-only.
  - RavenContent.cmake:23-54 compiles only Private/Renderer/Shaders.
  - VulkanContext.cpp:549-568 rebuilds every lit pipeline together.
  - No vkCreatePipelineCache exists anywhere.
- **Why it matters:** Goal 3 for shaders, and player builds. Players cannot ship custom materials, and edit-to-visible time would grow with the number of materials.
- **Fix:**
  1. ShaderAsset is an asset kind, compiled by a game-content rule next to `raven_add_game` into staged .spv. Players ship SPIR-V only.
  2. In development it is watched through ReloadService and AssetStore with last-good, using the generalized Program from AA2-HR-01 step 4 on the process lane.
  3. Build one pipeline per (shader, render state) off-thread, and publish and retire each one individually. Debug views keep the engine pipeline bundle.
  4. A missing or failed material shader falls back to the engine lit pipeline with one warning.
  5. Measure startup pipeline creation with 4 shaders. Add a VkPipelineCache in the user-data folder only if the numbers warrant it.
- **Effort:** M
- **Timing:** design now; code at B4
- **Panel discussion:** None.

#### AA2-RND-07 Device selection checks no limits behind the 4,096-entry bindless array; bindless has one unclassed free list
- **Severity:** medium
- **Kind:** defect
- **Sources:** RND-7, ARCH-RND-05, RENDER round 2 answer 3
- **Evidence:**
  - VulkanTextureDescriptors.hpp:20 and .cpp:48-58, 74-86 fix capacity at 4096, unconditionally.
  - VulkanPhysicalDevice.cpp:68-73 checks booleans only.
  - VulkanPhysicalDevice.cpp:238-239 enables the portability subset without querying it.
  - VulkanTextureDescriptors.cpp:145-147 throws when full (AA2-RT-02).
- **Why it matters:** Goal 1 (MoltenVK and low-limit devices) fails late instead of with a clear error. Content can also starve UI glyph pages.
- **Fix:**
  1. Chain VkPhysicalDeviceVulkan12Properties in selection, and reject devices below capacity with a named reason.
  2. Query and log the portability-subset features once.
  3. Add allocation classes: element 0 white, an Engine reserve (for example 64 elements for glyph pages) and Content. A full Content class returns the placeholder plus one log line.
  4. Report per-class counts in GetResourceStats and in the B8 measurements.
- **Effort:** S
- **Timing:** now, before B5 and B6. Step 4 extends VulkanResourceStats in the Public VulkanContext.hpp (:35), so it goes in the † commit.
- **Panel discussion:** None.

#### AA2-RND-08 Removing the drain adds silent GPU hazards with no synchronization validation and no VRAM budget accounting
- **Severity:** medium
- **Kind:** process
- **Sources:** RND-10 (new)
- **Evidence:**
  - VulkanInstance.cpp:54-74, 103-117: only the standard validation layer, with no VkValidationFeaturesEXT.
  - A grep finds no VK_EXT_memory_budget anywhere.
  - RenderingContentDesign.md:119, 195: B3 adds about 33% memory for mips, and FD-24 records counts only.
- **Why it matters:** Goals 3 and 4. A missed barrier after B6 goes unreported, and VRAM growth on the 8 GB laptop GPU is unmeasured.
- **Fix:**
  1. Add a CMake option RAVEN_VALIDATION_SYNC that chains synchronization validation.
  2. Make one sync-validated run of the 50k scene plus a texture reload a manual B6/B8 exit item.
  3. Query VK_EXT_memory_budget and report per-heap usage and budget in GetResourceStats and B8.
- **Effort:** S
- **Timing:** step 1 now; steps 2-3 before B6
- **Panel discussion:** None.

#### AA2-RND-09 Skeletal animation is post-v1, but the M6B group and record decisions decide whether it can be added cheaply
- **Severity:** medium
- **Kind:** process
- **Sources:** RND-11, PROC-11
- **Evidence:**
  - FutureDecisions.md:662, 672 list skinning under "After v1".
  - RenderWorldState.hpp:21-38 and 84-86: there is no per-object vertex offset, and there is one group per MeshHandle.
  - RenderingContentDesign.md:18: FD-19 is fragment-only.
- **Why it matters:** Animated characters are a day-one need of the action direction. If B4 fixes grouping without a per-object geometry range, skinning later rewrites it.
- **Fix:**
  1. Owner question Q3: skinning in v1 or not.
  2. Whatever the answer, record the delegated path in B0: compute skinning before culling, writing into per-instance ranges of the geometry pool. This keeps FD-19 and the lit vertex interface unchanged.
  3. AA2-RND-02 step 7 records the constraint (do not widen the 128-byte record; a vertex-base override would go in a parallel stream). Stream code is added only if Q3 puts skinning in v1.
  4. Add a skinning row to the "Not in M6B" table.
- **Effort:** S
- **Timing:** question now; constraint recorded in B0, code only on a yes to Q3
- **Panel discussion:** None.

#### AA2-RND-10 Renderer docs drift: walk cost and the upload comment
- **Severity:** low
- **Kind:** doc-drift
- **Sources:** RND-8 (one bullet dropped), PERF challenge
- **Evidence:**
  - GpuDrivenDesign.md:66 says "tens of microseconds at 50k". The measured figure is about 250 µs. The steady-state walk does a SlotOfIndex read plus two sparse Finds per mesh (RenderWorld.cpp:109-116), and two more Finds per written object (:200-201).
  - VulkanContext.hpp:86 is the wrong comment from AA2-RND-04.
- **Why it matters:** B4's gate starts from a wrong cost model.
- **Fix:**
  1. Correct GpuDrivenDesign.md:66 with the measured split.
  2. Name the dense-tick walk as the first B4 sync snippet.
  3. Do not attribute the 0.27 ms M6A regression to these lookups. See AA2-PERF-07.
- **Effort:** S
- **Timing:** now
- **Panel discussion:**
  - The round-1 bullet saying ROADMAP.md:194 claims six owner decisions was dropped. The lead checked, and line 194 says "five".
  - The "first suspect for the 0.27 ms" attribution was also dropped. `git diff 9b2cf05..5f4df75` shows the walk unchanged.

### 5.6 Gameplay services: physics, movement, input, fixed step, user data (GP)

#### AA2-GP-01 A kinematic body keeps its last MoveKinematic velocity when its transform is rewritten with an unchanged value
- **Severity:** medium
- **Kind:** defect
- **Sources:** GSV-01, PERF-3 (merged duplicate), lead spot-check, ECS/RENDER/PERF verification
- **Evidence:**
  - PhysicsWorld.cpp:515-521 (inside ApplyTicks, 498-523): a kinematic body's velocity is zeroed only on the branch where no tick changed.
  - PhysicsWorld.cpp:917-934: when the tick changed but the value did not, `moved == false`. SameVector is exact equality (JoltSupport.hpp:46-49). Then neither MoveKinematic nor a reset runs, so KinematicMoving and Jolt's velocity stay.
  - PhysicsWorld.cpp:563-599 (guard at :574): WriteBack writes back only dynamic bodies. Jolt keeps integrating the stale velocity while TransformComponent stays put, so the drift is invisible in components. That violates FD-32's rule that components are the source of truth.
  - Interpolation trigger: kinematic bodies are interpolated without the tag (TransformInterpolator.cpp:39-44). Apply (:101) and Restore (:113) both write through Registry::WriteComponent, which stamps the tick (Registry.cpp:710-716, 823-830), after the physics step (Application.cpp:612, 644). AdvanceChangeTick returns the old tick and then increments (Registry.cpp:838-841), so Restore's stamp is greater than Since. The first step after a body's last move therefore keeps the velocity.
  - Games/Arena/Source/Platforms.cpp:12-19 shows the unconditional mutable-view write pattern a door or elevator would use (mutable views stamp even unchanged values, Registry.hpp:415, 428-433). Arena itself does not trigger the bug: its only kinematic body is the platform (Games/Arena/Tools/ArenaTool.cpp:240-249, Period 9), whose cosine ease (Platforms.cpp:15-18) changes every fixed step and never stops. No game in Games/ has a kinematic body that stops or gets an unchanged rewrite.
  - GameServicesDesign.md:449: kinematic bodies moved by game code were never checked. Physics.md:21 ("Edits") describes only dynamic bodies.
- **Why it matters:** Moving platforms, doors and elevators are core action content, and the first door, elevator or clamped mover will hit this. There are two consequences, both with the drawn mesh staying put:
  - (a) With interpolation, every stop leaves a permanent collider offset of about v*dt.
  - (b) With an unchanged per-tick rewrite, the stale velocity is never cleared, so the collider drifts without bound.
- **Fix:**
  1. In PhysicsWorldSync::Update, after the moved block: if the motion is Kinematic and `!moved` and `entry.KinematicMoving`, zero both velocities and clear KinematicMoving.
  2. Keep the ApplyTicks zeroing. It still covers bodies that nobody writes after their last move. Do not adopt PERF-3 step 2.
  3. Add one case to the existing PhysicsTests (which already has a kinematic child case at :175-179): move a kinematic body for N steps, then rewrite the same transform for M steps, and assert there is no drift. Minimal-tests justification: AA2-PERF-02 step 2 rewrites this sync (a changed-since scan over the pools, plus a KinematicMoving slot list), and AA2-PERF-01 deletes the Restore stamp it currently depends on. If PERF-02 step 2 is not scheduled, the justification becomes the first kinematic door or elevator content in M6B or later, and the test lands with that content.
  4. Update Physics.md under "Edits": game code stops a kinematic body by not moving it. After the fix, an unchanged rewrite is safe but costs a sync each tick.
- **Effort:** S
- **Timing:** now (review amendment)
- **Panel discussion:** PERF rated it medium and GAMEPLAY rated it high. ECS, RENDER, DOCS and the lead chose high, because they believed the trigger pattern ships in the reference game today. Verification showed that Arena's platform never produces an unchanged rewrite or a stop.
- **Verification:** overstated; severity high -> medium, because the defect is latent with no current trigger in any shipped game, the fix is S, and it blocks no v1 goal and forces no rework. The Arena evidence was restated as the pattern, not a live trigger, and the two concrete consequences and the write-back guard (PhysicsWorld.cpp:574) were added.

#### AA2-GP-02 The simulation is not a pure function of the per-tick input stream
- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** GSV-03, AUI-9 (merged), ECS/PERF challenge on step 4, CORE round 2 answer 3
- **Evidence:**
  - Picks are frame-scoped, not tick-scoped. GameSession.cpp:170-172 swaps m_Picks once per frame. Every runStep calls `session.MakeContext(gameplay.GetInput(), actions, gameplay, step)` (Application.cpp:496-517, the call at :505; also :570-571), and MakeContext always passes m_Picks and the live UI (GameSession.cpp:256-286: NoUi at 271-283 with its comment at 270, the UI selection at 284, the return at 285-286). So every catch-up step in a frame sees the same picks, and on a frame with zero steps FixedUpdate never sees them. That contradicts GameSystems.hpp:98 ("delivered once to the next update") and depends on pacing.
  - The live UI reaches fixed steps. GameSystems.hpp:106-107 tells developers not to declare UI there, but nothing enforces it.
  - Fixed steps do not receive raw frame input. `gameplay.GetInput()` is FixedGameplayStep's own per-tick accumulation of keys and mouse buttons, filtered by the input contexts and cleared after each step (FixedGameplayStep.hpp:17-20, 41-48, 78, 96). That is the documented contract (GameSystems.hpp:90-92). It is per-tick, but it is not recorded in the input stream: FixedGameplayStep.hpp:161-164 stores m_Input and the per-tick action snapshot separately from the records.
  - Courtyard's FixedUpdate reads this per-tick key state (CourtyardGame.cpp:101, registered at :234). That is a command-channel style violation, not a tick-purity break.
  - Child colliders lag one frame. PhysicsWorld.cpp:674-678 reads the cached WorldTransformComponent. ApplyTicks re-reads a child only when its own collider, body, transform or world tick changes (PhysicsWorld.cpp:498-515, gate at 509-512). The world tick is stamped only in PostUpdate (Application.cpp:608-614, UpdateWorldTransforms at 613) and in the Restore pass (641-646). So within a frame, a child under a root that FixedUpdate moves keeps last frame's world matrix through every step.
  - Interpolation writes into TransformComponent (TransformInterpolator.cpp:84-116). That is covered by AA2-PERF-01.
  - Application.cpp:425-427: the determinism check pins exactly one tick per frame, so it cannot see any of this.
  - GameServicesDesign.md:184 states the level-1 guarantee ("same scene, seed and input stream give the same simulation"), which is not met when frame pacing differs. :156 calls the input stream "the only door into the simulation", and :195 says replay "can be added later on the same input stream".
  - No shipped game triggers it today. Samples reads Picks in Update (Interaction.cpp:73-77), and Arena reads Ui in Control and Update (Screens.cpp:121, 147, 178). A shipped child collider under a moving root could not be confirmed, because the binary Arena.ravenscene was not inspected.
- **Why it matters:** Determinism level 1 holds only at identical frame pacing. Level 2 lockstep (the RTS target) and replay would need rework, and every new system written against today's context deepens the problem. The gap is latent, and the rework it threatens is scheduled for the RTS milestone, not v1, so fixing it is cheapest before M6B adds game code.
- **Fix:**
  1. One phase-aware `MakeContext(phase)` that also sets a session-held phase:
     - FixedUpdate gets empty picks, NoUi (logging once in Debug on the first declaration) and the seeded RNG. It keeps the per-tick InputState: that state is already per tick, and the documented contract (GameSystems.hpp:90-92; FixedGameplayStep.hpp:17-20) stays unchanged. Purity is restored by recording it in step 3, not by emptying it.
     - Control and Update get the frame picks, live UI and the presentation streams from AA2-GP-03.
  2. Port Courtyard's PlayerSystem to Actions, or to Control plus a record, for the command-channel rule. The 2D run needs an owner request.
  3. Push the per-tick action snapshot and the per-tick InputState into the same per-tick record list in BeginStep. FixedUpdate's Actions becomes a read-only view of that tick's snapshot, with no context or rebinding control; Control keeps the full ActionView (Application.cpp:504).
  4. Child colliders: each step, PhysicsWorldSync visits every child-collider entry, composes its world live (`Scene::GetWorldTransform`, O(depth)) and compares it with `entry.World`. The cost is O(child colliders × depth) per step; child colliders are few, and the cost is recorded with the AA2-PERF-06 step_world_ns column. Keep the Decompose failure path (PhysicsWorld.cpp:679-683). Do not run a full world pass per step. Detecting ancestor TransformComponent ticks is the fallback if the measurement shows a large child-collider count.
  5. Interpolation output leaves TransformComponent (AA2-PERF-01).
  6. Later, with replay: DeterminismCheck pass 2 replays pass 1's per-tick inputs at a different scripted pacing.
  7. Document in GameSystems.hpp that picks become records carrying an EntityRef in Control, and correct :98. GameSystems.hpp is a Public header, so this text change alters the contract hash; land it with the other hash changes.
- **Effort:** M
- **Timing:** steps 1-4 and 7 before M6B adds game code, after the high review amendments; step 5 in B4; step 6 at the replay/RTS milestone
- **Panel discussion:**
  - Step 4 originally called `UpdateWorldTransforms()` per step and described it as incremental. ECS and PERF showed the pass is O(all nodes): 0.37-0.52 ms at about 100k nodes per call, and about 2 ms on a 4-step catch-up frame (derived). GAMEPLAY revised step 4 to the live compose above, and the lead adopts it.
  - CORE confirmed that one signature covers Picks, Input, Ui and Random. The session-held phase covers what SystemContext cannot reach: user data and the ID sequence.
- **Verification:** overstated; severity high -> medium, because nothing triggers it in shipped content, the rework it threatens (lockstep, replay) is not v1, and the fix is small and contained. "Raw input" was corrected to the documented per-tick InputState, which step 1 now keeps and step 3 records. The pick duplication across catch-up steps became the main evidence, and step 4 now names its visit strategy.

#### AA2-GP-03 Determinism state is shared across phases, and fixed-step time is not derived from the tick
- **Severity:** medium
- **Kind:** defect
- **Sources:** GSV-04, ECS-F4 step 1 (merged), GAMEPLAY round 2 answer 1
- **Evidence:**
  - Scene.cpp:145-157, 283-295 and SceneCommands.cpp:21-26: one seeded m_IdState serves every phase, and the ID is drawn at record time.
  - GameSession.cpp:285 with Application.cpp:557, 579: the same m_Random reaches Control and Update.
  - FixedGameplayStep.hpp:80, 92-114: fixed-step elapsed time comes from m_Elapsed, which Reset and RestartTicks never clear.
  - Precedent for splitting by phase: sound variation already uses a presentation RNG (GameSession.cpp:340-372), and contacts are phase-gated (PhysicsWorld.hpp:59, 78, 92).
- **Why it matters:** Silent level-1 breaks are waiting in common game code: cooldowns keyed on ElapsedSeconds, cosmetic spawns, random camera shake.
- **Fix:**
  1. `GetStepTime` returns `{(m_Tick + 1) * m_StepSeconds, m_StepSeconds}`, and m_Elapsed is removed. Extend the existing FixedStepTests case.
  2. Scene keeps a second, OS-seeded presentation ID sequence. SceneCommands draws from the sequence chosen by the session-held phase. The host selects the simulation sequence only around runStep.
  3. GameSession holds a second, OS-seeded SimulationRandom for Control and Update.
  4. Entities created in a frame phase get a transient engine tag. The DeterminismCheck hash excludes tagged entities, and so does PhysicsWorldSync in Debug (log once if a tagged entity carries physics components).
  5. Extend the DeterminismCheck scenario with one frame-phase spawn.
  6. Document in GameSystems.hpp which phase gets which stream.
- **Effort:** S
- **Timing:** now, together with AA2-GP-02 step 1, whose session-held phase step 2 needs. Steps 2 and 6 edit Public headers (Scene.hpp holds the ID state, GameSystems.hpp the stream text), so they go in the † commit.
- **Panel discussion:**
  - Two models were proposed: (a) separate presentation streams, or (b) refusing frame-phase Create and Random. The lead chooses (a), delegated. FD-14 supports Create in Update, and refusing it would ban damage numbers and debris.
  - A Debug per-phase write guard on game components is deferred to AA2-ECS-06's access declarations.

#### AA2-GP-04 Arena steers from one-shot records, so the craft's speed depends on frame rate
- **Severity:** medium
- **Kind:** defect
- **Sources:** GSV-02, lead spot-check
- **Evidence:**
  - FixedGameplayStep.hpp:86-100: records reach only the first step of a frame. FixedStepTests.cpp:45 guards this as intended behavior.
  - Craft.cpp:38-54: `move = {}` unless a record arrived.
  - CharacterMovers.cpp:503-513: with zero steering the craft stops for that tick.
  - InputStream.hpp:19-21 already says that a record emitted in a frame reaches that frame's first fixed step (:20-21). It does not say that continuous intent must be latched.
- **Why it matters:** Player-visible. Below 60 FPS, or on catch-up steps, the craft slows or stands still. New games will copy the pattern.
- **Fix:**
  1. Add a CraftInputComponent. CraftSystem latches the latest record into it and always steers from it, clearing Jump once it has been applied.
  2. Add one sentence to the InputStream.hpp:19-21 comment: continuous intent must be latched in a component or read from Actions. InputStream.hpp is a Public header (Public/Raven/Input), so this comment edit changes the contract hash; land it in the † commit with the other hash changes.
  3. Add an engine helper ("latest record of T") only when a second caller exists.
- **Effort:** S
- **Timing:** now (review amendment)
- **Panel discussion:** GAMEPLAY lowered its own rating from high to medium: it is a reference-game bug plus a documentation gap, and the fix forces no rework. The lead agrees.

#### AA2-GP-05 User data is read and saved synchronously inside fixed steps, and saves are not durable
- **Severity:** medium
- **Kind:** defect
- **Sources:** GSV-05, ARCH-PLAT-07, CORE endorsement
- **Evidence:**
  - Craft.cpp:85-95: HitSystem (FixedUpdate) calls Set and Save.
  - Run.cpp:41-46: a fixed step reads the profile into simulation state (Profiles, `Get<ArenaProfile>`, `run->Best = profile.BestScore`).
  - GameServicesDesign.md:240 says "never mid-frame", and :952 says the determinism check runs without user data.
  - UserData.cpp:331-343: Save does a .bak copy and an atomic write on the main thread. Win32AtomicFile.cpp:15-22 retries for up to about 200 ms.
  - Files.cpp:67-72: no FlushFileBuffers. PosixAtomicFile.cpp:5-8: no fsync.
  - GameSession.cpp:249-253 with ArenaSystems.cpp:9-19: games catch an exception as control flow.
- **Why it matters:** Disk I/O inside the simulation hitches the step. A per-player profile feeding simulation breaks level 2. Progress can be lost on power loss.
- **Fix:**
  1. `UserDataService::Save(file)` only marks the file. The host flushes marked files after the frame, next to saveBindings (Application.cpp:654), on a blocking-I/O thread.
  2. Add `GameServices::HasUserData()`, or a pointer-returning accessor.
  3. Arena: HitSystem only sets a RunOver result. An Update system writes the profile, and the best score enters the run at scene start or through a Control record.
  4. In Debug, log once when GetUserData is called during FixedUpdate, using the session phase from AA2-GP-02.
  5. FlushFileBuffers on Windows now. fsync of the file and its directory on POSIX with M4, inside `Platform::ReplaceSavedFile`.
- **Effort:** S
- **Timing:** now; step 4 with AA2-GP-02 step 1 (it needs the session phase); step 2 in the Phase 3 window (a GameServices addition). Step 1 changes what `Save` documents in the Public Raven/Core/UserData.hpp (:67-70), so that comment goes in the † commit. The POSIX part of step 5 in M4.
- **Panel discussion:** None.

#### AA2-GP-06 Mover queries also collide with every other mover's inner body
- **Severity:** medium
- **Kind:** defect
- **Sources:** GSV-11 (new)
- **Evidence:**
  - CharacterMovers.cpp:302-303: inner bodies use the moving layer.
  - CharacterMovers.cpp:517-520: ExtendedUpdate gets an empty BodyFilter.
  - PhysicsWorld.cpp:75-92: moving-versus-moving collides.
  - Jolt CharacterVirtual.cpp:399, 421, 591, 677: Jolt ignores only the character's own inner body.
  - CharacterVirtual.cpp:172-175: inner bodies are placed by SetPositionAndRotation, so they have zero velocity.
- **Why it matters:** Crowd cost and crowd feel. Every pair is solved twice, the second time as a static obstacle. It also blocks a deterministic parallel mover update (AA2-GP-07).
- **Fix:**
  1. Add a Raven-owned JPH::BodyFilter subclass that rejects mover inner-body IDs (already recorded in `links.BodyIds`, CharacterMovers.cpp:313-314), and pass it to ExtendedUpdate. No Jolt edit is needed.
  2. Run PhysicsBenchmark at 500 movers before and after, and check Arena's 120-drone pile (121 movers with the craft) with an ignored probe.
  3. Rebaseline the DeterminismCheck hash. The change is by design.
  4. Note in Physics.md that movers meet each other only through the grid.
- **Effort:** S
- **Timing:** now, before any AA2-GP-07 work
- **Panel discussion:** None.

#### AA2-GP-07 The character mover backend is serial and order-dependent, which ties large unit counts to one core
- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** PERF-2, GAMEPLAY challenge and round 2 answer 2, PERF revision
- **Evidence:**
  - CharacterMovers.cpp:463-521: ExtendedUpdate runs in ID order on the main thread.
  - CharacterMovers.cpp:112-160: the grid reads other characters' live state.
  - GameServicesDesign.md:529-533: 6.2-11.5 µs per mover; 500 movers take 3.1 ms per step.
  - FD-47 has no threads study.
  - The parallel path has preconditions:
    - The grid's mutable stamps race (CharacterMovers.cpp:191-205, 214-215).
    - Other characters' transforms are read live (:126, 157).
    - One shared TempAllocator serves every update (PhysicsWorld.cpp:406).
    - Inner-body timing matters (AA2-GP-06).
    - Dynamic-body impulses are applied inside the update (CharacterVirtual.cpp:769-811).
    - Jolt's simple checker is documented as not thread safe (CharacterVirtual.h:242-245).
- **Why it matters:** Hordes and RTS-like counts. About 1-2k movers would cost 6-12 ms per step on one core (derived), and the sequential semantics get baked into game feel.
- **Fix:**
  1. Now, docs only: write the threads study into FD-47 while it is still provisional:
     - snapshot semantics versus sequential;
     - the five preconditions above;
     - feel: today's order lets the lower ID win head-on; a snapshot is symmetric, with overlaps of about 0.2 m that Jolt's recovery removes.
  2. At the crowd milestone, or when a game needs about 1k movers:
     - make the grid stamps per job, read the transforms snapshotted at Build, and give each job its own TempAllocator;
     - apply the GP-06 filter;
     - put movers that touched a dynamic body last step on an ordered serial pass;
     - run the rest in chunked frame jobs on the AA2-PERF-05 range API.
  3. Rebaseline the determinism hash and add 1k and 2k cases to PhysicsBenchmark.
- **Effort:** S (step 1); M (step 2)
- **Timing:** step 1 now (with FD-47 confirmation in the joint review); step 2 at the crowd milestone
- **Panel discussion:**
  - PERF rated it high, then revised to medium after GAMEPLAY showed the original step 3 cannot be done without editing Jolt: Update moves the inner body itself (CharacterVirtual.cpp:1476). PERF's alternative, a Raven-managed proxy, is noted. The BodyFilter route is simpler, and the lead prefers it.
  - Snapshot-versus-sequential pushing is a delegated solver choice. It goes to the owner only if Arena's crowd feel visibly changes.

#### AA2-GP-08 Picking is a host special case: left press only, disabled by any left-button binding, and no hit point
- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** GSV-06
- **Evidence:**
  - GameSession.cpp:528-551: picks are made on left press only, and every hit is logged at Info.
  - Application.cpp:414, 639: picks read input after the router has consumed it.
  - ScenePicking.hpp:9-14: PickHit has only Entity and Distance.
  - GameSystems.hpp:87-109 and GameSession.cpp:489-538: the presented camera is not exposed to games. SystemContext has no camera view or framebuffer size, and the rendered camera stays local to Render.
- **Why it matters:** Click interaction in action games, and selection and orders in an RTS. Changing PickHit is a layout break, so it should be batched.
- **Fix:**
  1. Add Point, Normal and EntityRef to PickHit. This goes in the B2 contract window.
  2. Add a request API: Control code asks for a pick at a screen point and gets the result next frame. Keep the automatic left-click pick only as a default.
  3. Move the per-pick log line to the trace or to Debug.
  4. Expose the presented CameraView and framebuffer size in Control and Update.
- **Effort:** M
- **Timing:** B2 contract window
- **Panel discussion:** Picking against the latest tick, rather than the drawn position, is recorded as delegated (9b).

#### AA2-GP-09 Collision layers filter queries only; every layer collides with every other, and the character grid ignores layers
- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** GSV-07
- **Evidence:**
  - PhysicsWorld.cpp:75-92 checks only the moving bit.
  - CharacterMovers.cpp:117-137, 189-209 has no layer check.
  - PhysicsComponents.hpp:10-13 states that every layer collides with every other.
  - Physics.md:123, 126 lists no layer filtering and no joints.
- **Why it matters:** Projectiles that ignore their shooter, triggers that see only the player and debris that ignores characters are day-one action needs.
- **Fix:**
  1. Add a project-file 16x16 layer matrix, defaulting to all-collide, loaded into ObjectPairs and ObjectVsTree.
  2. Apply the same matrix in CharacterGrid.
  3. Let the project name its layers. The syntax is recorded for confirmation in 9b.
  4. Schedule constraints and joints as a separate later item.
- **Effort:** S
- **Timing:** during M6B, or before the next gameplay feature
- **Panel discussion:** None.

#### AA2-GP-10 The input contract lacks 2D axes, analog sources, gamepad, cursor capture and binding removal
- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** GSV-08, AUI-2 step 3 (shared addendum), ARCH-CORE-04 window part
- **Evidence:**
  - Input.hpp:99-103: InputDevice is Keyboard or Mouse only.
  - InputMap.hpp:35-42, 105-113: actions are Button or a 1D summed Axis; ActionSample has no Vec2.
  - InputMap.hpp:140-145: a binding cannot be removed.
  - Window.hpp:66: there is no cursor mode.
  - Games/Arena/Source/Camera.cpp:20 and ArenaGame.cpp:40: Arena turns its camera with a Q/E axis.
- **Why it matters:** Mouse-look, sticks and 2D move actions. Adding them later means another contract break plus an Arena rewrite.
- **Fix:**
  1. Write an FD-29 input addendum covering:
     - Axis2D;
     - analog sources (mouse delta and scroll now, gamepad later) with dead zone and scale;
     - `ActionSample.Value2`;
     - UI navigation as engine actions (AA2-MEDIA-04).
  2. Apply only the layout change in the B2 window, and implement the mouse-delta and scroll sources.
  3. Add `InputActions::Unbind` and a `GetConflicts` helper.
  4. Add a Window cursor mode (normal, hidden, captured-relative), with documented no-ops on unported backends.
  5. Gamepad support follows the SDL3 decision, which is a later owner question.
- **Effort:** M
- **Timing:** design now; layout in the B2 window; gamepad later
- **Panel discussion:** None.

#### AA2-GP-11 A snapshot is not a simulation checkpoint
- **Severity:** low
- **Kind:** architecture-gap
- **Sources:** GSV-12 (new)
- **Evidence:**
  - GameSession.cpp:194-207, 210-239: a snapshot holds scene bytes, seeds and the tick only. Jolt's contact cache, sleep state and CharacterVirtual state are lost, and m_Elapsed is not restored.
  - Jolt 5.7.0 already provides SaveState and RestoreState (PhysicsSystem.h:165-168; CharacterVirtual.h:496) and deterministic body IDs (CharacterVirtual.h:63-65).
- **Why it matters:** This sets the boundary for keeping lockstep resync, rollback and player save games possible. It is latent.
- **Fix:**
  1. Docs now: in FD-48 and UserData.md, state that a snapshot restores components and seeds, not a bit-exact simulation state. State that networking is out of v1 scope (owner question Q5 confirms it).
  2. At the replay milestone: build a checkpoint from PhysicsSystem and CharacterVirtual SaveState plus deterministic body IDs, then extend DeterminismCheck with "save at N, load, continue, compare".
- **Effort:** S
- **Timing:** docs now; code at the replay/RTS milestone
- **Panel discussion:** None.

#### AA2-GP-12 C7's developer F6 binding silently disabled the Samples' documented F6 mesh swap
- **Severity:** medium
- **Kind:** defect
- **Sources:** PROC-5, lead spot-check
- **Evidence:**
  - InputRouter.cpp:92-95 binds F6 in the developer context, and :237-245 consumes it.
  - SamplesGame.cpp:224 still reads raw F6.
  - README.md:63, AssetSystem.md:77 and InteractionGame.md:7 still document the swap.
  - M6C exit item 2 relied on the reload probe only (GameServicesDesign.md:1002).
- **Why it matters:** A designer-visible regression. The next developer key will collide the same way.
- **Fix:**
  1. Move the Samples' F6 swap (to a non-developer key such as G) and its O, F1 and Space toggles (SamplesGame.cpp:207-224) to mapped game actions.
  2. Update the three docs.
  3. At activation, log one warning per game action whose default binding is shadowed by a developer binding.
- **Effort:** S
- **Timing:** now (review amendment)
- **Panel discussion:** None.

### 5.7 Audio, text and UI (MEDIA)

#### AA2-MEDIA-01 A clip can be freed while the audio thread still mixes it
- **Severity:** high
- **Kind:** defect
- **Sources:** AUI-1, lead spot-check, MEDIA and BUILD round 2
- **Evidence:**
  - AudioVoices.cpp:252-253, 408-421: when a looping sound loses its voice, Update calls Release(sound, FadeFrames, true). Release sends Stop and sets Mode=Virtual and Voice=0 for loops, while the slot keeps fading for 480 frames (10 ms). The mixer reads the clip through a raw `Samples` pointer until the ramp ends (AudioMixer.cpp:93-99, 156-164; AudioMixer.hpp:78, 116; the pointer is set at AudioVoices.cpp:380).
  - AudioVoices.cpp:400-406, 456-459: a later Stop or StopAll of that Virtual sound takes the non-Real branch, becomes Stopping with Voice=0 and is erased at once. The Sound held the only per-sound `shared_ptr<const PcmClip>` (AudioVoices.hpp:65). Collect cannot hold it back, because it matches releases by sound.Voice, which is already 0 (AudioVoices.cpp:438-452, match at 445-446).
  - A fourth erase path: a concurrency-group steal in Play can pick a just-virtualized loop as its victim (AudioVoices.cpp:96-114), which is erased at :133.
  - The clip has no other owner on these paths:
    - Next and restart publish the new scene, destroying the old scene with its leases, before StopAll (SceneSession.cpp:76-78, 161 Next, 163 Restart; then GameSession.cpp:182-189).
    - Quick-load does the same (SceneSession.cpp:137-143, the old world dies at the end of RestoreSnapshot; then StopAll at GameSession.cpp:224-234).
    - Lazily used leases live only in the old Scene's m_Assets (Scene.cpp:627-640), and the store keeps only a weak_ptr (AssetStore.cpp:143).
    - A reload Commit swaps the clip out to a candidate that is destroyed right after (AssetStore.cpp:84, 249-255). This only removes the other owner; the crash still needs a later Stop, StopAll or steal of a loop virtualized within the fade window.
  - Only the lease path is exposed: GameSession::PlaySound to AudioPlayer::Play(const AssetLease&) (GameSession.cpp:331-372; AudioPlayer.cpp:77-83). AudioPlayer::Play(AssetRef) keeps every clip in Impl::Clips for the player's lifetime (AudioPlayer.cpp:36, 56-67), so it is immune.
  - Only loops are affected. A one-shot released with keep stays Stopping with its Voice until the release arrives (AudioVoices.cpp:416-423).
  - The window is wider than 10 ms plus one block. When AudioOutput leaves silent mode, it closes the null device first and then opens the real one (AudioOutput.cpp:46-56), which takes about 120 ms (GameServicesDesign.md:695). Nothing renders in that gap, so a virtualization Stop sent before a scene change is applied after the clip has been freed, and the command queue can also fill.
  - AudioVoices.cpp:413-414: Release ignores the result of Send.
    - If the Stop is dropped while virtualizing, the old voice keeps looping in its slot forever. After its Sound is erased, the dangling read is guaranteed, not timing-dependent.
    - With keep=false, a dropped Stop leaves a looping Real sound Stopping with a nonzero Voice forever, because its release never comes. That is a stuck audible loop, a leaked slot and a permanent m_Sounds entry, but no use-after-free.
  - GameServicesDesign.md:765 says the code never frees a clip while the audio thread reads it (also :681), which this contradicts.
  - No current trigger in tracked content: no Games/ source or content uses Loop, and Arena plays only one-shots (Craft.cpp:59, 84; Drones.cpp:17; Cells.cpp:58; Screens.cpp:39). But loop is a public feature (SoundParams.Loop; SoundEvent.cpp:108).
- **Why it matters:** Once a game uses its first looping ambience or music, a scene change, restart, quick-load or concurrency steal shortly after a loop is virtualized can make the audio thread read freed memory and crash the process. Every action game will use loops. Streaming (C8, next) builds on this lifetime protocol.
- **Fix:**
  1. Add `std::array<std::shared_ptr<const PcmClip>, MaxVoices> m_SlotClips` to AudioVoices. Set an entry in StartVoice after Send succeeds. Reset it in Collect, inside the `m_Slots[release.Slot] == release.Serial` branch (AudioVoices.cpp:443-444), not in the per-sound loop. That branch also covers the immediate release the mixer sends when it rejects a Start (AudioMixer.cpp:53-57). Destroying the shared_ptr in Collect happens on the main thread, and ~Impl joins the audio thread before the voices die (AudioPlayer.cpp:29-33).
  2. Make a dropped Stop retryable **per slot**, not per Sound. Keep an array of pending `{serial, fade}` stops indexed by slot. Resend them at the start of Update while CommandSpace() allows (AudioMixer.hpp:101), and clear an entry when the matching release arrives. The Sound needs no change. Keeping the voice ID and a StopPending flag on the Sound would not work: Collect would then match the release and end a loop that should stay Virtual (AudioVoices.cpp:445-450), and a later Stop or StopAll erases the Sound and its flag (:402-406, 456-459), so the mixer would loop forever.
  3. Update the C8 lifetime statement to name the slot as owner, and correct GameServicesDesign.md:765 and :681.
  4. Add one CPU guard test, following the existing RAVEN_API-on-private-class precedent (PhysicsWorld.hpp:48-74; PhysicsTests links RavenCore).
     - Add RAVEN_API to the out-of-line members the test calls: AudioMixer::Render, and AudioVoices' constructor, Play, Update, SetVolume, StopAll and GetCounts.
     - Build AudioVoices on a bare AudioMixer, so the test is CPU-only and CI-runnable. Drive `AudioMixer::Render` on the test thread (AudioMixer.hpp:95, 110).
     - Virtualize the loop deterministically with SetVolume(handle, 0), so its score falls to the AudibleFloor or below, instead of starting 65 sounds.
     - StopAll, drop the external reference, and keep a weak_ptr to the clip. Render past the 480-frame fade plus one 256-frame block, and assert the clip outlives the release.
     - Do not recompile private .cpp files into the test, because that is an ODR hazard.
- **Effort:** S
- **Timing:** now (review amendment); the test lands with the fix
- **Panel discussion:**
  - Whether a freed clip crashes or reads garbage depends on the allocator. The ~508 KB VirtualAlloc threshold is platform knowledge, not checked in code. At 384 KB/s (AudioClip.hpp:21), any loop longer than about 1.3 s is above it. That is why this is high, not critical.
  - MEDIA proposed a probe now and the test later. BUILD noted that streaming, which rewrites this path, is already the next C8 step. The lead chooses to land the tracked test with the fix, because that next rewrite is exactly what the minimal-tests rule asks a test to guard.
- **Verification:** confirmed with corrections; severity high -> high, because the use-after-free is real in a public feature that every action game will use, and the window is wider than first stated (about 120 ms around device reopen). It is not critical, because no tracked content uses Loop yet. Fix step 2 was rewritten to keep pending stops per slot, and step 4's test was made deterministic.

#### AA2-MEDIA-02 Audio decode limits are enforced after allocation, and there is no recorded trust policy for content decoders
- **Severity:** medium
- **Kind:** defect
- **Sources:** MEDIA-N1, MEDIA-N3 (merged)
- **Evidence:**
  - AudioClip.cpp:81-104: the Vorbis buffer grows until frames exceed 300 s × source rate. An 8-channel 192 kHz file can therefore request about 1.84 GB before rejection (derived).
  - AudioClip.hpp:20-22: the 64 MiB compressed cap does not bound decoded size. AudioClip.cpp:139-157, 165: a legal 300 s clip is about 115 MB resident.
  - Scene loads decode on the frame thread, and bad_alloc is fatal at every boundary (Application.cpp:90-98).
  - TextSystem.cpp:187-202: a bad font stops the host from starting.
  - AssetSystem.md states no policy on trusted content.
  - Image.cpp:14-95 and FbxMesh.cpp:20-22, 90-124 are well bounded and serve as the reference.
- **Why it matters:** Goal 3 recovery and frame pacing. Mod-friendly genres would need a policy.
- **Fix:**
  1. Bound decoded size before each resize in both audio paths, using a MaxDecodedAudioBytes budget (for example 64 MiB in mixer format).
  2. Optionally, reject early from the stream length. The library availability of that length is unverified.
  3. Lower the non-streamed clip limit to match the budget, and point long sounds to streaming.
  4. Record in AssetSystem.md, as delegated: "every decoder bounds its output before allocating", and "v1 game content is trusted author input, bounded but not sandboxed". Mods or user-generated content would need a separate owner scope decision.
  5. With AA2-MEDIA-05: a bad game font logs and falls back to the engine Noto chain.
- **Effort:** S
- **Timing:** now; step 5 with MEDIA-05
- **Panel discussion:** None.

#### AA2-MEDIA-03 The glyph atlas never evicts, and page creation is a synchronous 4 MB upload on the frame thread
- **Severity:** medium
- **Kind:** defect
- **Sources:** AUI-6, MEDIA and CORE challenges
- **Evidence:**
  - TextSystem.cpp:580-618: append-only placement, a 4 MB zero vector per page, then CreateManagedTexture, which uses SubmitAndWait (VulkanTexture.cpp:80-94).
  - VulkanTextureDescriptors.cpp:147: length_error when the array is full.
  - GameSession.cpp:440-453: text placement runs inside Render.
  - Estimate: the 11,172 Hangul syllables would need about 25 pages, roughly 100 MB.
- **Why it matters:** Frame stalls with CJK text, VRAM growth bounded only by the font, and a process exit on throw.
- **Fix:**
  1. Catch std::exception per glyph inside TakeUploads: mark the glyph Failed and log once. A boundary alone does not help, because length_error and bad_alloc are fatal there (AA2-RT-02).
  2. Route page creation and clearing through the B6 batch (AA2-RND-04 step 1).
  3. Pre-create page 0 when TextSystem starts.
  4. Add a page budget with least-recently-used reclaim, and record it in UIDesign section 7.
- **Effort:** M
- **Timing:** step 1 now; steps 2-4 with B6
- **Panel discussion:** None.

#### AA2-MEDIA-04 UI keyboard navigation is hard-coded outside the FD-29 router, and the input filter uses a stale element set
- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** AUI-2, GAMEPLAY round 2 answer 4
- **Evidence:**
  - UiContext.cpp:316-330: Tab, Up and Down move focus whenever any element exists.
  - UiContext.cpp:257-259: while anything has focus, Tab, Enter, Space and all four arrows are stripped from gameplay.
  - Application.cpp:405 vs 454 and UiContext.cpp:245, 267, 280: FilterInput scans frame N-2 while hover uses N-1.
  - InputRouter.cpp:66-75 and InputMap.hpp:70-75, 129-130: engine contexts, priorities and SetContextActive already exist.
- **Why it matters:** Player-visible. A Tab scoreboard binding plus one button steals jump and the arrows. UI keys cannot be rebound.
- **Fix:**
  1. One line now: FilterInput iterates m_NextOrder.
  2. Keyboard focus starts only in a scope that opts in. A pointer press outside the focused element, or any gameplay action, clears focus.
  3. Add an engine "ui" router context (ui.next, ui.previous, ui.accept, ui.back, ui.left, ui.right), active while focus existed last frame and saved with the other bindings. UiContext reads these actions. The keyboard half of FilterInput is then deleted.
  4. Gamepad buttons later bind to the same actions.
- **Effort:** M
- **Timing:** step 1 now; steps 2-3 before M6B ends
- **Panel discussion:** GAMEPLAY confirmed that no "consumed by UI" marker is needed, provided replay records the per-tick list rather than raw input.

#### AA2-MEDIA-05 Fonts are a fixed list of engine files, with no game font asset, family, weight, reload or shaping language
- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** AUI-3
- **Evidence:**
  - TextSystem.cpp:48, 187-224: a fixed list of three Noto files.
  - Ui.hpp:84-90: Style has no family or weight.
  - TextSystem.cpp:343: shaping uses the process default language, so Han characters get Korean forms.
  - UIDesign.md:80-81, 153, FD-31 and UIAudioLibraries.md:52, 83 promise font assets.
  - TextSystem.cpp:569: the glyph key already reserves a font index.
- **Why it matters:** Designer-visible: every game looks the same. It contradicts U4 and goal 3 for fonts.
- **Fix:**
  1. Add AssetKind::Font, leased and reloaded through SettledFileWatch, with the engine Noto fonts as the last fallback.
  2. Add a family ID and weight to TextStyle, and `AssetRef<FontAsset> Font` plus Weight to Ui::Style. This goes in the B2 contract window.
  3. Build a per-family fallback chain.
  4. Add a language to TextStyle from the active locale.
  5. Correct UIDesign section 4 and FD-31: runtime TTF with on-demand MTSDF.
- **Effort:** M
- **Timing:** decide during M6B, implement before more game UI. The shipped fonts are owner question Q6.
- **Panel discussion:** None.

#### AA2-MEDIA-06 The Ui API has no localization seam
- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** AUI-4
- **Evidence:**
  - Ui.hpp:113-116: widgets take a display string_view.
  - ArenaUi.cpp:79-168: English text built with std::format.
  - UIDesign.md:87-90 plans string tables. String tables have no milestone (GameServicesDesign C7; the RenderingContentDesign "Not in M6B" table).
  - UiContext.cpp:188-213: pseudo-localization rewrites every string.
- **Why it matters:** Under the no-throwaway rule, every screen written now gets rewritten later, and the later Ui.hpp change is another contract break.
- **Fix:**
  1. Settle the API shape now, delegated: `UiText { Key; Args }` next to the string_view overloads, plus `Services.Localize`.
  2. Until tables exist, return the key itself.
  3. Port the Arena screens.
  4. Later: per-locale string-table assets and ICU MessageFormat, with pseudo-localization applied only to table output.
- **Effort:** M
- **Timing:** API in the B2 window; tables during or after M6B. Launch languages are owner question Q6.
- **Panel discussion:** None.

#### AA2-MEDIA-07 Audio has no pause or time domain
- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** AUI-5
- **Evidence:**
  - GameSystems.hpp:76-80: SetSimulationPaused does not touch audio.
  - AudioPlayer.cpp:142-148: virtual voices advance on steady_clock.
  - GameSession.cpp:148-151: F8 has no audio effect.
  - Sound.hpp:13-21: buses have volume only.
- **Why it matters:** Player-visible: loops and one-shots keep playing behind a pause menu.
- **Fix:**
  1. Add `AudioVoices::SetPaused(busMask, fadeSeconds)` using a gain ramp and rate 0. The mixer already holds position at rate 0.
  2. Map game pause and F8 to every bus except Interface, and Music per the owner's answer to Q7.
  3. Add a simulation time-scale factor later.
- **Effort:** S
- **Timing:** before M6B ends
- **Panel discussion:** None.

#### AA2-MEDIA-08 TextSystem::Prepare (parallel shaping) is built but never called
- **Severity:** low
- **Kind:** doc-drift
- **Sources:** AUI-7, PERF-8 (merged)
- **Evidence:**
  - TextSystem.cpp:646-677 defines Prepare, and a grep finds no caller.
  - UiContext.cpp:132-146, 522: shaping happens one string at a time on the main thread.
  - GameServicesDesign.md:594, UIDesign section 8, JobSystem.hpp:19-20 and JobSystemDesign.md:15, 30-31 all describe worker shaping.
- **Why it matters:** The docs overstate the engine's parallelism, and ParallelFor has no production caller.
- **Fix:**
  1. Call Prepare once at the top of UiContext::EndFrame with the frame's text elements, and measure a preview language switch.
  2. Only if that shows no gain, delete Prepare and correct the docs.
- **Effort:** S
- **Timing:** during M6B
- **Panel discussion:** None.

#### AA2-MEDIA-09 Text and UI hardening: hash-only cache keys, silent duplicate IDs, ICU data lifetime
- **Severity:** low
- **Kind:** defect
- **Sources:** AUI-8
- **Evidence:**
  - TextSystem.cpp:376-381, 653: ShapedCache is keyed by hash only.
  - TextSystem.cpp:682-686: the layout key has a structured collision.
  - UiContext.cpp:156-175: duplicate IDs are silent.
  - TextSystem.cpp:177-182: ICU's common data is owned by one TextSystem instance.
- **Why it matters:** Rare wrong text, confusing bugs, and a latent use-after-free with a second TextSystem.
- **Fix:**
  1. Use struct keys with full equality, and store the source text.
  2. Add a Debug per-frame duplicate-ID log.
  3. Load ICU data into a process-lifetime static.
- **Effort:** S
- **Timing:** opportunistic
- **Panel discussion:** None.

### 5.8 Performance and concurrency (PERF)

#### AA2-PERF-01 Render interpolation writes blended transforms into the ECS and restores them
- **Severity:** high
- **Kind:** architecture-gap
- **Sources:** PERF-1, GSV-03 step 5, RENDER round 2 answer 2, ECS round 2 answer 2, CORE endorsement
- **Evidence:**
  - Application.cpp:611-613: Apply writes blended transforms into the ECS inside the PostUpdate boundary (608-614), and UpdateWorldTransforms follows.
  - Application.cpp:640-646: after session.Render (639), Restore and a second full world pass run.
  - TransformInterpolator.cpp:84-116: both Apply (:101) and Restore (:113) write through `access.Write`, which stamps ticks. Entries whose Previous equals Current are skipped (:91).
  - Scene.cpp:530-560: every world pass walks every depth-bucket node and does two sparse Finds before the tick test at :559. The second pass therefore adds a full O(total nodes) floor on every frame in which any entity is blended.
  - PhysicsWorld.cpp:509-514, 917-921, 1182: every moving interpolated body is re-evaluated on every step. The physics write-back is excluded through Since (:1182), but Restore stamps after Since, so ApplyTicks queues every blended body, including dynamic ones that would otherwise be skipped. Update(slot) then runs Evaluate plus the shape and placement comparisons (:852 onward). The guard at :917-921 (the comparison at 919-921) confirms the authors knew: its comment names render interpolation's restore and stops it becoming a teleport. The same restamping defeats the kinematic-zero branch (:515-521), which is AA2-GP-01's interpolation trigger.
  - PickWorld.cpp:233-236 and RenderWorld.cpp:119-120 test WorldTransform ticks, so the same writes re-dirty picking and the render world. The render-world write is not an extra cost: a moving blended object needs one 128-byte GpuObjectData write per frame anyway (RenderWorldState.hpp:37). The extra costs are the second O(total) world pass, the per-step physics Update of blended bodies (mainly dynamic ones), and pick re-updates on frames between fixed steps.
  - Latent hazard only: today Render has no StageBoundary and no catch wraps the frame loop, so a throw in Render reaches main (RavenEngine.cpp:218) and the process exits; no later frame sees the blended value. It would become real once AA2-RT-01 step 4 adds a Render stage, which RT-01 handles by moving Restore into an always-run scope.
  - Second-pass cost (derived, unmeasured): 0.37-0.52 ms floor at about 100k nodes (the 50k-mesh benchmark layout), plus about 50 ns per moving interpolated node.
  - DataModelDesign.md:661 records the 50k-animated CPU preparation p95 at 3.24-3.42 ms against a 3.5 ms budget. The derived floor alone would break that budget in any 50k scene with even one moving interpolated body.
  - Nothing has measured it: the exit benchmark has no interpolated entities, and the pass has no trace section (AA2-PERF-06).
- **Why it matters:** Goal 4 in any game with physics, against a recorded budget. This is also the shared root of AA2-GP-01's interpolation trigger.
- **Fix:**
  1. Measure first (AA2-PERF-04/06): 50k plus N = 0, 1, 1k and 5k interpolated bodies, against the 3.5 ms p95 preparation budget (DataModelDesign.md:661). This number decides between high and medium and between options (a) and (b) below.
  2. In the B4 render-world rewrite: after the single world pass, TransformInterpolator produces a render-only override. Compose each interpolated entity's blended local transform with its parent's world, and walk descendants through the child index. Key the override by entity, not by render slot, because slots swap-erase.
  3. `RenderWorld::Write` uses the override in place of WorldTransformComponent, and re-dirties the slot once with the true world when blending stops. Override entries whose entity is despawned, loses its mesh or is swap-erased are cleaned up in the same RenderWorld lifecycle path.
  4. The camera view and the audio listener read the same override. Scene::GetCameraView composes live from TransformComponent (Scene.cpp:600-601), and the listener uses the camera's WorldTransformComponent (GameSession.cpp:476-478). Without the override, a camera parented to an interpolated entity would step against the smoothly drawn parent. Delegated choice: the presented camera and the listener follow the drawn (blended) position; picking stays on the latest tick.
  5. At B5, once sprites are render-world objects (AA2-RND-03) and inherit the override, delete Restore and the second world pass. Until then they keep running, because GatherSprites reads WorldTransformComponent during Render (GameSession.cpp:419-431). Making GatherSprites read the override in B4 would be throwaway code, since B5 deletes GatherSprites. Update the now-stale comment at PhysicsWorld.cpp:917-918 in the same snippet, and keep the placement comparison, which still guards a game rewriting an unchanged value.
  6. Picking stays on the latest tick (recorded as delegated, 9b).
- **Effort:** M
- **Timing:** step 1 before M6B starts; steps 2-4 in B4; steps 5-6 at B5
- **Panel discussion:** Four mechanisms were proposed:
  - (a) a CPU override;
  - (b) previous and current world rows on the GPU;
  - (c) a non-stamping write;
  - (d) stamping only on byte change.

  Decision: (a), delegated, with (b) kept as a measured fallback in a parallel stream. Reasons:
  - (a) keeps rigid local blends: children follow an arc, not a chord.
  - (a) needs no per-step world rows.
  - (a) leaves the fragment and vertex ABI and culling untouched.
  - Bandwidth equals today's: one 128-byte record per interpolated object per frame.

  ECS rejected (c) because it hides a non-authoritative value in the source of truth, and (d) because it taxes every correct write. The trigger to switch to (b) is a measured ring-write or Sync cost above the B4 budget. The estimate is above about 10k interpolated objects at high refresh rates.
- **Verification:** confirmed with corrections; severity high -> high, because the derived second-pass floor (0.37-0.52 ms) would break the recorded 3.24-3.42 ms / 3.5 ms preparation budget in any 50k scene with one moving interpolated body. If the step 1 measurement shows a lower floor, downgrade to medium. The Render-throw bullet was reworded as latent, and the extra costs were restated. The plan now keeps Restore until B5, so that interpolated sprites keep interpolating, and the camera and listener read the override.

#### AA2-PERF-02 Change detection costs O(total entities) in four separate consumers
- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** PERF-5, ECS-F6 step 3, ARCH-PERF-02, PERF round 2 answer 2
- **Evidence:**
  - Scene.cpp:530-560: two sparse Finds per node, every frame.
  - RenderWorld.cpp:103-122: SlotOfIndex plus two Finds per mesh.
  - PhysicsWorld.cpp:498-523: four Finds per entry per fixed step, statics included.
  - PhysicsWorld.cpp:564-599: WriteBack walks every entry.
  - PickWorld.cpp:225-238: about 0.39 ms per pick on an unchanged 50k scene.
  - ArchitectureAssessment.md:940: 100k static costs 1.10 ms plus 0.50 ms.
- **Why it matters:** Scaling to 100k and to RTS-like maps of static props. Cost grows with what exists rather than with what changed, in four places that will drift apart.
- **Fix:**
  1. Add a pool-level "for each element changed since tick" scan over the dense Ticks array. It reports value changes only (lifecycle stays with the journal), lives on the private `RegistryAccess` seam (AA2-CORE-03 step 7), and lands together with the tick epoch (AA2-ECS-08 step 4). It is exact because Erase moves Ticks with the element (Registry.cpp:236-238).
  2. PhysicsWorldSync adopts it now, as a local change:
     - scan the four pools' Ticks, map hits through SlotOfIndex, and keep the Commit sort/unique;
     - keep a KinematicMoving slot list;
     - WriteBack iterates the dynamic slots only;
     - move m_Changed and m_Added into Impl.
  3. RenderWorld adopts it in B4, resolving slots only on hits.
  4. PickWorld adopts it next.
  5. The world pass adopts it to seed dirty roots (AA2-ECS-07 step 3).
  6. Only if FD-27's 100%-animated measurement misses its budget: chunked frame jobs on the AA2-PERF-05 range API.
- **Effort:** M
- **Timing:** step 2 now (opportunistic, with AA2-GP-01); steps 1 and 3 in B4; steps 4-5 right after
- **Panel discussion:** ECS accepted the primitive with conditions (i)-(iv), which steps 1 and 5 capture. Step 2 has no ECS dependency, because PhysicsWorldSync is already a friend holding the pool views.

#### AA2-PERF-03 Background jobs may occupy all workers but one, and there is no shared thread-placement policy
- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** PERF-4, RELOAD-1 (lane), PERF-10 (merged), RELOAD and PERF round 2 throttle tables
- **Evidence:**
  - JobSystem.cpp:36: `m_BackgroundLimit = workers - 1`, which is 18 of 19 workers.
  - JobSystem.hpp:24-27 and JobSystemDesign.md:31-35: jobs cannot be preempted, and blocking I/O stays off the pool.
  - RenderingContentDesign.md:137, 213 plans decodes and pipeline builds as background work.
  - GameServicesDesign.md:632-640: a 4,096-body physics step needs the whole pool.
  - ReloadService.cpp:359, 457, 462, 501 calls consumer callbacks while holding the subscription mutex, and ReloadService.hpp:68-77 does not document this.
- **Why it matters:** Async loading would turn load hitches into simulation hitches. An M6B consumer that calls its own subscription from a callback would deadlock.
- **Fix:**
  1. Make the background limit a constructor parameter, defaulting to max(1, workers/4), which is 4 on the laptop.
  2. Record one placement table in JobSystemDesign.md and the RenderingContentDesign threads section, delegated:

     | Work | Where | Limit |
     |---|---|---|
     | Change detection | ReloadService watcher | 1 thread |
     | Blocking file reads, settle checks, first-load reads | Reload decode workers | 2 |
     | External processes (DXC, cmake) | Process lane, polled without a sleeping worker | max(1, hw/8) = 2 |
     | CPU-only decode, mips, MTSDF, shaping batches | Pool background, each job at most about 2 ms, no blocking I/O | 4 |
     | Jolt, parallel sync, movers | Pool frame jobs | at least 15 workers free |
     | Unsplittable vkCreateGraphicsPipelines | Dedicated thread | capped at the lane limit |
     | Game build | GameModule's own worker | 1 process (see AA2-HR-08) |
     | User-data flush, durable writes | Blocking-I/O thread | - |
     | Audio | Its own threads | - |

     Worst case: about 8 of 20 hardware threads busy in the background.
  3. Before B6: measure the 4,096-body step while 200 decodes are queued.
  4. Add a comment to ReloadService.hpp: callbacks run under the subscription lock, must not call the subscription, and may take only leaf locks. When the M6B consumers land, optionally move Accept and Discard outside the lock.
- **Effort:** S
- **Timing:** before M6B (fold into the B4/B6 design text)
- **Panel discussion:** PERF proposed hw/4 for the process lane, and RELOAD proposed hw/8. The lead takes hw/8, because together with 4 pool background jobs, hw/4 would oversubscribe.

#### AA2-PERF-04 Scale benchmarks never exercise the combined engine
- **Severity:** medium
- **Kind:** process
- **Sources:** PERF-6, RND-2 step 6, PERF round 2 answer 1
- **Evidence:**
  - GameServicesDesign.md:974: the exit run uses the Samples scene only, under auto-fifo at 165 Hz.
  - PhysicsBenchmark.cpp:45 builds a private pool of min(DefaultWorkerCount, 8) workers (PhysicsWorld.cpp:969-971), not the host's 19.
  - RenderingContentDesign.md:221-223: M6B's exit reuses the same run.
  - No churn soak or RTS-shaped data point exists.
- **Why it matters:** Goal-4 exit decisions cannot see the costs M6C added.
- **Fix:**
  1. PhysicsBenchmark uses a `JobSystem(DefaultWorkerCount)` and prints the worker count.
  2. Add a SamplesTool "game-scale" generator: 50k renderables, 2k dynamic bodies, 500 interpolated movers and a HUD.
  3. Record it uncapped on mailbox and on FIFO, on AC power, with a 5-minute sustained run, using the existing CSV columns.
  4. Add two data points with no targets: a 100k churn soak (AA2-ECS-07), and 200k static plus 5k changing.
  5. Add RND-2's variety case: 256 meshes, 64 materials and 4 shaders.
  6. Keep all of it outside CTest.
- **Effort:** S
- **Timing:** before M6B starts (it is the B4/FD-27 baseline)
- **Panel discussion:** The panel agreed not to add a 200k target, only data points, in line with "keep RTS possible, do not focus on it".

#### AA2-PERF-05 The JobSystem has no range/grain ParallelFor, an unmeasured spin, and notifies after the waiter may have returned
- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** PERF-7, PERF-9 (merged)
- **Evidence:**
  - JobSystem.hpp:52-80 and JobSystem.cpp:65-81: one queue item per index under one mutex.
  - JobSystem.cpp:14, 137-139: each idle worker spins 4,000 PAUSE instructions.
  - GameServicesDesign.md:632-640: the shared pool adds 0.05-0.15 ms of overhead per step.
  - JobSystem.hpp:69-79 and TextSystem.cpp:563-564 vs 237-241: notify_all runs after the counter reaches 0, by which time the waiter may already have destroyed the counter. That is formally undefined behaviour.
- **Why it matters:** The FD-27 fallback, parallel movers and ParallelEach all need an efficient range API.
- **Fix:**
  1. Add `ParallelFor(count, grain, body(begin, end))` with an atomic chunk cursor. Keep the per-index overload as a thin wrapper.
  2. Add a Done flag. The last finisher stores it (release) after notify, and the waiter spins briefly until it is set. Apply the same to the glyph owner.
  3. Make the spin time-based (about 20 µs), and measure it on P-cores and E-cores.
  4. Replace the queue with a ring only if step 1's measurement still shows queue cost.
  5. Add a JobSystem CPU test only if step 4 is scheduled. Otherwise use an ignored probe.
- **Effort:** S
- **Timing:** before B4
- **Panel discussion:** PERF conceded the lead's point on the test: the queue replacement is the rewrite a test would guard, so the test is justified only if that replacement is scheduled.

#### AA2-PERF-06 The interpolation Restore world pass has no trace section, and the design record calls it incremental
- **Severity:** medium
- **Kind:** process
- **Sources:** PERF-11 (new)
- **Evidence:**
  - Application.cpp:640-646 has no FrameSection, while world_ns covers only Application.cpp:610-613.
  - GameServicesDesign.md:378 describes the extra pass as "incremental".
- **Why it matters:** A 0.4-0.5 ms cost on the benchmark layout goes unattributed in the trace that every exit decision uses.
- **Fix:**
  1. Wrap Application.cpp:641-646 in a FrameSection writing a `restore_ns` column. Add `step_world_ns` when AA2-GP-02 step 4 lands.
  2. Correct GameServicesDesign.md:378.
  3. Record the AA2-PERF-01 step 1 measurement with the new column.
  4. Delete the column together with the pass.
- **Effort:** S
- **Timing:** now, before the M6B baseline
- **Panel discussion:** None.

#### AA2-PERF-07 The unexplained M6A 0.27 ms sync and 0.3 ms world-pass regression is most likely benchmark change locality
- **Severity:** medium
- **Kind:** process
- **Sources:** PERF-14 (new), PERF challenge to RND-8
- **Evidence:**
  - `git diff 9b2cf05 5f4df75 -- RenderWorld.cpp` shows the walk unchanged.
  - SceneFile.cpp:491, 501-504 sort rows by random 64-bit ID, and :413-418 keep children in row order.
  - SamplesTool.cpp:87 animates `index % 5`: stride-regular before M6A, scattered after.
  - DataModelDesign.md:661 and RenderingContentDesign.md:223: B4 is gated on finding or explaining the regression.
- **Why it matters:** B4's gate could send the team hunting a non-bug.
- **Fix:**
  1. Add an animated-fraction and placement option to SamplesTool (game-tool code): 20% scattered, 20% contiguous, 100%.
  2. Run one same-day comparison. If contiguous returns to the pre-M6 numbers, close the regression as "explained: change locality" and keep scattered as the realistic baseline.
  3. Use the same run as the FD-27 baseline.
  4. Do not reorder scene files to fix it. The B4 dense scan is the engine-side answer.
- **Effort:** S
- **Timing:** before B4
- **Panel discussion:** This is a hypothesis that the A/B in step 2 confirms or refutes. Until then, treat it as unverified.

#### AA2-PERF-08 Log writes synchronously to the console under one global mutex that the main thread takes every frame
- **Severity:** low
- **Kind:** architecture-gap
- **Sources:** PERF-12 (new)
- **Evidence:**
  - Log.cpp:14-58: one mutex covers both the cerr write and the history.
  - Application.cpp:262, 636: GetRecentProblems runs every frame.
  - Worker-thread callers: ReloadService, TextSystem, AssetStore and AudioOutput.cpp:59-77.
- **Why it matters:** In development, a blocked console write (for example during a QuickEdit selection) stalls the frame.
- **Fix:**
  1. Log formats the line, appends it to the history and a bounded queue, and returns. One logger thread drains the queue to cerr, and later to the AA2-CORE-05 file sink.
  2. GetRecentProblems copies only when a revision counter has changed.
  3. Document in Log.hpp that Log may be called from any thread except the audio callback.
- **Effort:** S
- **Timing:** during M6B, before B6 adds background logging
- **Panel discussion:** None.

#### AA2-PERF-09 Per-step and per-phase allocation churn in sync and command paths
- **Severity:** low
- **Kind:** defect
- **Sources:** PERF-13, PERF round 2 answer 5, ARCH-PERF-05
- **Evidence:**
  - PhysicsWorld.cpp:962-963, 1030: the sync vectors are rebuilt every step.
  - SceneCommands.cpp:87-91: `std::exchange(..., {})` drops capacity, and a std::set is built per Apply.
  - ArenaSystems.hpp:82-91 with Drones.cpp:50: an unreserved sort every step.
  - Application.cpp:262, 636: problem strings are copied every frame.
  - GameModule.cpp:390-395: a map lookup per system call. This is CPU cost only, not churn.
- **Why it matters:** Small but recurring main-thread heap traffic that game code copies.
- **Fix:**
  1. Keep the sync vectors in Impl (with AA2-PERF-02 step 2).
  2. SceneCommands swaps into member "applying" buffers and clears them afterwards, keeping the "never applied twice" guarantee and the capacity.
  3. Reserve in InIdOrder until AA2-ECS-08 step 6 supersedes it.
  4. Replace the per-call map lookup with a stats index stored on LoadedSystem.
- **Effort:** S
- **Timing:** opportunistic now
- **Panel discussion:** No allocation counter was run. This is a static inventory.

### 5.9 Build and vendoring (BLD)

#### AA2-BLD-01 Local and CI builds run different CMake versions, and policy-dependent branches have already broken CI once
- **Severity:** medium
- **Kind:** process
- **Sources:** XP-4, ARCH-BLD-02 step 1
- **Evidence:**
  - CMakeLists.txt:1 declares 3.24, while :4-9 applies CMP0141 only on 3.25+.
  - Commit 0860170: HarfBuzz hit C1128 under /ZI, and only CI saw it.
  - RavenGame.cmake:33-39 and Games/Arena/CMakeLists.txt:1-5 keep a 3.24 path that CI never runs.
  - RavenThirdParty.cmake:7-10 and RavenJolt.cmake:57-58 exist only to undo /ZI.
  - windows.yml:43-47 neither prints nor pins the CMake version.
  - MacOSBuild.md:7 says "3.24 or newer".
- **Why it matters:** Debug flags differ silently between the owner's laptop and CI, and every newly vendored library makes another CI-only break more likely.
- **Fix:**
  1. Raise `cmake_minimum_required` everywhere to one version both the owner and CI use (3.28, or the VS-bundled 4.x line), and delete the CMP0141 guards and the 3.24 fallback.
  2. Add a CI step that prints `cmake --version`, or pin the version.
  3. Update MacOSBuild.md and the local AGENTS file.
- **Effort:** S
- **Timing:** now. It is the first step of the AA2-PLAT-02 list. It deletes the 3.24 branch in RavenGame.cmake, which GameContract.cmake:6-13 hashes, so it goes in the Phase 1 † commit.
- **Panel discussion:** BUILD reordered its list so this lands before the RavenGame.cmake MSVC guards. That way the 3.24 branch is deleted instead of guarded.

#### AA2-BLD-02 The never-edit rule is incomplete for ufbx and stb and is enforced mechanically for no library
- **Severity:** medium
- **Kind:** process
- **Sources:** XP-5, PROC-8 step 2
- **Evidence:**
  - RavenEngine/CMakeLists.txt:21, 24-26: ufbx is built inline and stb is a raw SYSTEM include. There is no Raven<Lib>.cmake, no version check and no update script for either, even though ufbx.h:270 and stb_image.h:1 carry version strings.
  - RavenMsdf.cmake:8-9, 18: the msdf pins are never compared against the copy.
  - Nothing re-hashes the 2,419 vendored files.
  - scripts/UpdateJolt.cmake:31-49 duplicates `raven_vendor_copy`.
  - RenderingContentDesign.md:144-148, 214: M6B B7 extends FbxMesh.cpp over ufbx.
- **Why it matters:** The owner's stated reason for the rule is maintainability and upgrades as a swap. The ufbx upgrade M6B is likely to want has no swap path.
- **Fix:**
  1. Add cmake/RavenUfbx.cmake: a version check from UFBX_HEADER_VERSION, then `raven_third_party`.
  2. Add cmake/RavenStb.cmake: an INTERFACE target with a header-line version check.
  3. Add UpdateUfbx/UpdateStb scripts on `raven_vendor_copy`, and move UpdateJolt onto it.
  4. Have `raven_vendor_copy` write ThirdParty/<lib>/MANIFEST.sha256.
  5. Add scripts/CheckVendored.cmake as one CI step. It is a check, not a test.
  6. Use the manifest as the pin for msdf.
- **Effort:** S
- **Timing:** before M6B, ahead of B7
- **Panel discussion:** None.

#### AA2-BLD-03 CI covers no GPU path and no reload recovery, and its cost has not been re-measured
- **Severity:** low
- **Kind:** process
- **Sources:** XP-8 (step 3 withdrawn), ARCH-BLD-03/06
- **Evidence:**
  - windows.yml:49-51 runs `-LE gpu`, so DeterminismCheck never runs in CI.
  - RavenEngine/CMakeLists.txt:48-56: Raven3DSmoke is EXCLUDE_FROM_ALL (:50).
  - TODO.md:27's job time predates the M6C libraries.
  - windows.yml:27, 30, 36 pin actions by tag.
  - CMakePresets.json:48-56, 97-112, 148-149 define x86 presets nothing uses.
- **Why it matters:** Goal 3 recovery and the determinism guard rely on manual runs only.
- **Fix:**
  1. Record the current CI job time. Add a compiler cache only if it exceeds about 10 minutes.
  2. Add the Raven3DSmoke build plus the build-only part of Test-Windows3D.ps1 as one CI step.
  3. DeterminismCheck reaches CI through the headless host (AA2-CORE-02 step 5), not lavapipe.
  4. Delete the x86 presets, and pin actions to commit SHAs.
- **Effort:** S
- **Timing:** steps 1 and 4 now; step 2 before M6B; step 3 at the end of M6B
- **Panel discussion:** BUILD withdrew lavapipe: it is an untrusted extra binary and turns the check into a renderer-feature test.

#### AA2-BLD-04 Engine targets compile without /utf-8 while games and third-party targets use it
- **Severity:** low
- **Kind:** defect
- **Sources:** XP-10
- **Evidence:**
  - RavenBuildSettings.cmake:53-58 has no /utf-8.
  - RavenGame.cmake:29 and RavenThirdParty.cmake:12-13 add it.
  - Transform.hpp:221 already contains a non-ASCII character, and the laptop uses the CP949 code page.
- **Why it matters:** Text correctness under the multilingual UI goal.
- **Fix:** Add /utf-8 to the MSVC branch of `raven_apply_warnings`, and remove the separate flag in RavenGame.cmake. This changes the contract hash, so expect one host restart.
- **Effort:** S
- **Timing:** the contract window (section 7)
- **Panel discussion:** None.

### 5.10 Cross-platform (PLAT)

#### AA2-PLAT-01 Since M6A every game is a module and the module path is Windows+MSVC+Ninja only, so no macOS/Linux host can run any game
- **Severity:** high
- **Kind:** architecture-gap
- **Sources:** XP-1, PROC-2 (merged), PROC-10 (merged), BUILD round 2 answer 3
- **Evidence:**
  - Before M6A the samples ran without a module: at ae4423a they were compiled into the host (RavenEngine/Private/Samples/RavenEngine.cpp built PerspectiveSample and OrthographicSample through ApplicationClientFactory). Now every game is a DLL.
  - cmake/RavenGameModule.cmake:4-7, 8, 27-30, 37: modules default to ON only for WIN32 AND MSVC AND Ninja (4-7). The option text says "Build the Windows development game modules" (8). Configure fails otherwise (27-30), and only the MSVC environment is captured (37). RavenBuildEnvironment.cmake:3-9 FATAL_ERRORs without the MSVC INCLUDE/LIB variables, and BuildGame.cmake.in:47-51 and :143 require and include that environment file.
  - cmake/RavenGame.cmake:4-5 fails configure off WIN32. :29 applies /utf-8, :33-39 apply MSVC_DEBUG_INFORMATION_FORMAT or the /Zi /FS fallback, and :53-60 use the PDB generator expressions.
  - RavenEngine/Private/Host/RavenEngine.cpp:159, 168-169 and GameModule.cpp:674-675: without GameBuild::Enabled the host has no game. Develop is set only when Enabled (:159), GameModulePath only when Enabled (:168-169), Application.cpp:315-316 creates a GameModule only when that path is set, and GameModule.cpp:674-675 throws if the build is not Enabled.
  - **The host does not even compile off Windows.** RavenEngine.cpp:169 computes `std::filesystem::path(game).native() + GameBuild::ModuleSuffix`. ModuleSuffix is wchar_t (GameBuildConfig.hpp.in:11), and native() is std::string on POSIX. Because :168 is a runtime `if`, not `if constexpr`, the line is compiled everywhere. This is in the RavenEngine target (RavenSources.cmake:22-24), not RavenCore.
  - The UnsupportedGameModule.cpp:7-13 stubs throw, and RavenPlatform.cmake:46-52 links them on every non-Windows build. Shader watching would also throw: RavenEngine/Private/Renderer/Shaders/ShaderLibrary.cpp:285-288 (the #else branch) throws off _WIN32, reached through GameSession.cpp:116-117 when WatchShaders is set, and RavenEngine.cpp:173 sets WatchShaders whenever develop is true.
  - Win32Random.cpp is the only FillRandomBytes implementation (AssetId.cpp:26 and Scene.cpp:135 call it), so nothing links off Windows either.
  - PlatformHandoff.md:3 is dated 2026-10-14, and :13-14 tells contributors to verify "both samples", which is now impossible. The file was touched on 2026-11-25 (aa0cc37), and :52 mentions M6A, so only the status line and the sample instructions are stale. :58-63 describe the M5 reload backends.
  - The last macOS contributor commit is 48841f3 (2026-10-10), and the last Linux one is 7bfc63c (2026-10-06). The only workflow is windows.yml.
  - ROADMAP.md:160, :174 vs :162: the M5 and M6 "all platforms" exits are closed on Windows only.
- **Why it matters:** Goal 1 and the M4/M6 exits. M6A raised the smallest runnable POSIX unit from "random bytes" to "random bytes, a compilable host, a neutral game CMake, environment capture and a loader" before anyone can see a game. The stale handoff hides this.
- **Fix:**
  1. Now: rewrite PlatformHandoff.md as an ordered chain, with a "Vendored libraries on your OS" table:
     - (a) FillRandomBytes (existing M4 item);
     - (b) a neutral RavenGame.cmake, build-environment capture, and UTF-8 char GameBuildConfig constants so the host compiles on POSIX (AA2-PLAT-02 steps 2, 4 and 8);
     - (c) a dlopen loader run with --no-watch, using the in-place mode from AA2-HR-05;
     - (d) a POSIX BuildProcess;
     - (e) auto-build and shader watch.
     The rewrite also lists the macOS/Linux input-backend gap that grew with FD-29 (ARCH-PLAT-05, section 4).
  2. Moved to AA2-PLAT-02 steps 6-7: `Platform::SupportsBuildProcesses()` and the shader-watch gate. It has no current trigger: off Windows, GameBuild::Enabled is false, so develop and WatchShaders are false, and chain step (c) runs with --no-watch. ShaderLibrary already throws a clear message.
  3. Make the CMake changes in AA2-PLAT-02 steps 2 and 4. Both are no-ops on Windows. Step 4 lands now. Step 2 edits RavenGame.cmake, a contract input (GameContract.cmake:6-13), so it lands in the Phase 3 hash-only batch.
  4. M4: relax RavenGameModule.cmake:5 and :28 to single-config Ninja, and add the backends. Do not add a second in-tree build recipe. This depends on AA2-PLAT-02 steps 2 and 4 (the RavenGame.cmake:4 WIN32 gate, the MSVC flags and the environment capture), and does nothing without them.
  5. Owner question Q13 (scope and roadmap change): split each stage exit into a "Windows exit" and a "platform exit", and append each stage's new platform surface to M4 once.
  6. Owner question Q13: change ROADMAP.md:190 to state that M4 ports against the end-of-M6B contract.
  7. A non-blocking Linux compile-only job, subject to owner question Q2:
     - ubuntu-24.04 and GCC 13+;
     - the Vulkan SDK, DXC and spirv-val (required at configure, RavenContent.cmake:6-17), xcb dev packages and Ninja;
     - `--target RavenCore RavenEngine RavenSceneTool -- -k 0`, without warnings-as-errors. The host target is needed to catch RavenEngine.cpp:169. Link errors from FillRandomBytes are expected until (a), but every translation unit still compiles.
     CPU tests follow once (a) lands.
- **Effort:** M
- **Timing:** step 1 now (Phase 0); step 3 before M6B (PLAT-02 step 4 now, step 2 in the Phase 3 window); steps 5-6 on the owner's answer to Q13; step 7 on the owner's answer to Q2; step 4 first in M4
- **Panel discussion:** BUILD found nothing in the vendored libraries that needs an upstream edit to compile on Linux. That remains a hypothesis until the first GCC run, especially for ICU, whose README says Clang is unverified.
- **Verification:** confirmed with corrections; severity high -> high, because M6A turned "no POSIX game" into a structural regression that the stale handoff hides. The verifier found that the host does not even compile off Windows (RavenEngine.cpp:169). It is not critical, because module loading is already an M4 item and no contributor currently hits it. The CI target now includes the host, SupportsBuildProcesses moved to PLAT-02, and the exit split and the M4 contract became owner question Q13.

#### AA2-PLAT-02 Windows-shaped build-process and shader seams remain, and M6B is about to add callers
- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** RELOAD-6, XP-6, XP-1 steps 2-3, ARCH-PLAT-01; BUILD's ordered list
- **Evidence:**
  - BuildProcess.hpp:10-11 takes `std::vector<std::wstring>` (callers: GameModule.cpp:637-638, ShaderLibrary.cpp:163-169).
  - ShaderLibrary.cpp:276-288 uses `#ifdef _WIN32`.
  - GameBuildConfig.hpp.in:5-10 and ShaderWatchConfig.hpp.in:5-10 hold wchar_t paths.
  - RavenPlatform.cmake:46-52 keeps the stubs in UnsupportedGameModule.cpp.
  - RavenSources.cmake:18 keeps a second, hand-maintained exclude regex.
  - RavenGame.cmake:4 (NOT WIN32), :29 (/utf-8) and :33-39 (MSVC debug format, /Zi /FS) are new in M6A and Windows-shaped. Lines 26 (per-source build ID) and 30 (raven_apply_warnings) are portable.
- **Why it matters:** If M6B lands first, its new code is written against these APIs and has to be converted again in M4.
- **Fix:** BUILD owns this list. Apply it in this order:
  1. Raise the CMake floor (AA2-BLD-01).
  2. In RavenGame.cmake, which stays a no-op on MSVC: replace the WIN32 gate with RAVEN_ROOT checks, put the PDB lines behind if(MSVC), add hidden visibility (AA2-HR-02 step 1) and move /utf-8 into the shared function (AA2-BLD-04).
  3. Third-party visibility and the ISA settings (AA2-PLAT-03/04), plus RavenUfbx/RavenStb (AA2-BLD-02).
  4. Rename `raven_capture_msvc_environment` to `raven_capture_build_environment`, with an else branch recording CC, CXX and PATH.
  5. BuildProcess takes UTF-8 std::string arguments. Update both callers.
  6. Split Win32BuildProcess.cpp out of Win32GameModule.cpp:105-189. Add UnsupportedBuildProcess.cpp with `SupportsBuildProcesses() = false` (absorbed from AA2-PLAT-01 step 2). Once every OS implements BuildProcess, the query is a constant, which is acceptable only as the Unsupported fallback.
  7. Replace the ShaderLibrary `#ifdef` with `SupportsBuildProcesses()`. Develop mode turns on shader watching only when it returns true, otherwise it logs.
  8. Make the config constants UTF-8 char, read through PathFromUtf8. This is also what lets the host compile on POSIX (AA2-PLAT-01: RavenEngine.cpp:169 mixes std::string with a wchar_t suffix).
  9. Optional and cosmetic: regroup into Platform/Posix and Platform/Fallback, and drop the exclude regex.
  10. M4: -fno-gnu-unique, --exclude-libs,ALL, -z defs (AA2-PLAT-06) and the POSIX backends.
- **Effort:** M
- **Timing:** before M6B. Steps 1 and 3-8 now, step 1 (AA2-BLD-01) in the Phase 1 † commit. Step 2 edits RavenGame.cmake, a contract input, so it lands in the Phase 3 hash-only batch with AA2-BLD-04 and AA2-HR-02 step 1. Step 9 is optional. Step 10 in M4.
- **Panel discussion:** None.

#### AA2-PLAT-03 Jolt adds AVX2/FMA flags as PUBLIC options on GCC/Clang x86-64, so RavenCore's CPU floor differs by OS
- **Severity:** medium
- **Kind:** defect
- **Sources:** XP-2, lead spot-check, GAMEPLAY round 2 answer 3
- **Evidence:**
  - Jolt Build/CMakeLists.txt:54-62: the USE_* options default to ON.
  - Jolt.cmake:1004-1029: on non-MSVC x86_64 they are applied as PUBLIC options.
  - Jolt.cmake:974-983: the MSVC branch keys on CMAKE_VS_PLATFORM_NAME, which is empty under Ninja, so Windows gets SSE2.
  - RavenJolt.cmake:21-63 sets none of these. RavenEngine/CMakeLists.txt:29 links Jolt PRIVATE, which still applies Jolt's PUBLIC options to every RavenCore translation unit.
  - Jolt README.md:36 says "players need no AVX2".
  - CROSS_PLATFORM_DETERMINISTIC defaults to OFF and also drops -mfma (Jolt.cmake:1028).
- **Why it matters:** Goal 1. Non-AVX2 Linux or Intel-Mac players would get SIGILL anywhere in the engine, and floating-point contraction would differ silently between OSes.
- **Fix:**
  1. Now, delegated parity: set every Jolt USE_* explicitly to match the SSE2 floor that Windows ships today. No effect on Windows.
  2. Set CROSS_PLATFORM_DETERMINISTIC OFF explicitly, with a comment that cross-OS determinism (level 3) is out of v1.
  3. Owner question Q1: whether to raise the floor. If raised, add the matching MSVC /arch flag, because Jolt's MSVC path does not fire under Ninja.
  4. Update Jolt README.md and Physics.md to state the floor for every OS.
  5. Record what level 2 needs (FD-48): the same build on different PCs. The MSVC CRT may pick FMA3 transcendental paths at runtime (known MSVC behavior, not checked in this repo), so level 2 needs `_set_FMA3_enable(0)` or bit-exact math.
  6. With the first Linux build: check that the binary has no AVX2 outside Jolt.
- **Effort:** S
- **Timing:** steps 1-2, 4-5 now; step 3 on the owner's answer
- **Panel discussion:** DOCS recommended SSE4.2 and BUILD recommended SSE2 parity. The lead lands parity now because it changes nothing on Windows, and leaves raising the floor to the owner as a player-visible question.

#### AA2-PLAT-04 Static third-party libraries are linked into a default-visibility libRavenCore.so
- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** XP-3 (game-module part moved to AA2-HR-02), CORE verification
- **Evidence:**
  - RavenThirdParty.cmake:5-17 sets no visibility preset. RavenAudio.cmake:38-39, RavenVorbis.cmake:22-23 and RavenEngine/CMakeLists.txt:24-25 bypass `raven_third_party`.
  - Api.hpp:10, 20 use default visibility.
  - RavenText.cmake:30 and RavenEngine/CMakeLists.txt:29-35 link FreeType, HarfBuzz, msdfgen, Yoga, ICU, Jolt, miniaudio and stb_vorbis into RavenCore.
  - FreeType and HarfBuzz have no symbol versioning.
- **Why it matters:** Goal 1. There is no trigger today, because XCB pulls in neither library. Planned SDL3 and Wayland, or an editor toolkit, would load the system FreeType and HarfBuzz, and the two copies can interpose each other. Default visibility also hides missing RAVEN_API annotations.
- **Fix:**
  1. `raven_third_party` sets C/CXX_VISIBILITY_PRESET hidden and VISIBILITY_INLINES_HIDDEN.
  2. Route miniaudio, stb_vorbis and Fbx through it, and apply the same to Jolt.
  3. Set CXX_VISIBILITY_PRESET hidden on RavenCore. Add `--exclude-libs,ALL` in M4.
  4. In M4, verify with `nm -D` and `readelf`.
- **Effort:** S
- **Timing:** steps 1-3 (properties) now; linker flags and checks in M4
- **Panel discussion:** CORE kept this at medium as latent but nearly free to fix. The lead agrees.

#### AA2-PLAT-05 No macOS toolchain floor is recorded, and the engine depends on library features of unverified availability
- **Severity:** low
- **Kind:** process
- **Sources:** XP-7 (re-rated by its author)
- **Evidence:**
  - Floating-point from_chars and to_chars are used at SceneFile.cpp:241, 357 and RavenSceneTool.cpp:34.
  - std::jthread and std::stop_token are used in JobSystem and ReloadService.
  - CMakePresets.json:58-62 sets only clang++, with no CMAKE_OSX_DEPLOYMENT_TARGET anywhere.
  - MacOSBuild.md:7 records no versions.
  - Whether AppleClang's libc++ provides these features is a hypothesis, not checked.
- **Why it matters:** Goal 1. RavenCore may fail to compile on macOS, and the problem would be found late.
- **Fix:**
  1. MacOSBuild.md: state that the Xcode version and deployment target are set by the first macOS build.
  2. Add one configure-time feature probe (`check_cxx_source_compiles`, selected by feature, not OS) that fails with a message naming the floor.
  3. Add ParseFloat/FormatFloat seams only if the chosen floor lacks the functions.
- **Effort:** S
- **Timing:** docs now; probe before the first macOS build
- **Panel discussion:** The lead asked BUILD to mark the libc++ claims as a hypothesis. BUILD did, and lowered the severity to low.

#### AA2-PLAT-06 libRavenCore.so links with unresolved symbols, so missing platform hooks surface only when executables link
- **Severity:** low
- **Kind:** process
- **Sources:** XP-11 (new)
- **Evidence:**
  - PlatformRandom.hpp:9 declares the hook, and only Win32Random.cpp defines it.
  - The Linux branch of RavenPlatform.cmake:33-37 sets no `-z defs`.
- **Why it matters:** Every new Windows-only hook breaks downstream executables one at a time.
- **Fix:**
  1. Add `-Wl,-z,defs` to RavenCore on Linux.
  2. Add a handoff rule: every new Platform hook lands with a Linux and a Mac implementation, or an Unsupported fallback, in the same commit.
- **Effort:** S
- **Timing:** with the first Linux compile job
- **Panel discussion:** None.

### 5.11 Docs and process (DOC)

#### AA2-DOC-01 Two stages (about 22k added lines) are pending one owner review, the override of the snippet rule is not recorded, and M6B is about to build on that contract
- **Severity:** medium
- **Kind:** process
- **Sources:** PROC-1 (restated in round 2)
- **Evidence:**
  - M6A, in 5f4df75 (2026-11-24), changed 146 files (+5,801/-4,323).
  - C1-C8, in aa0cc37 (2026-11-25 22:41:22), changed 149 files (+14,963/-571). C9 (a2391f4) was committed six seconds later: 23 files, +1,498. Insertions total 22,262 (about 27k with deletions). All of it is pending review (DataModelDesign.md:5, GameServicesDesign.md:7).
  - ROADMAP.md:40 (planning decision 5) gives each of M6A, M6B and M6C "its own exit and review". A joint M6A+M6C review departs from an adopted planning decision, and the departure is not recorded.
  - The single-review status itself is recorded (DataModelDesign.md:5, GameServicesDesign.md:7), and the M5 precedent of one-pass implementation plus one owner review is in HotReloadDesign.md:6. What is missing is a tracked record that direct implementation overrode TODO.md:5 (the snippet-only working agreement, still stated) for M6A and M6C. AGENTS.md states the same rule, but it is gitignored (.gitignore:5) and cannot be fixed by a commit.
  - TODO.md:134 sets the order as "the owner's review of C1-C6 and a commit, then C9". No separate C1-C6 commit exists in the log. Whether the review happened as a separate step cannot be verified from the repo. The UI/audio breakpoint is consistent with the dated U1-U5 and A1-A6 decisions on TODO.md:138.
  - Checkboxes are inconsistent:
    - ROADMAP.md:180-188 checks every M6A step, while :207-216 leaves every M6C step unchecked, although both stages are "implemented, pending review".
    - TODO.md:139-144 checks C1-C6, while :145-148 leaves C7-C10 unchecked, even though C10 says "Done".
    - TODO.md:137 ("Owner review of UIDesign.md sections 1, 2 and 4, before any UI code") is unchecked, although :138 records those sections as approved and UI code shipped.
  - RenderingContentDesign.md:209 (B2) changes the public MaterialComponent.
  - master equals origin/master (both 7617e21).
- **Why it matters:** If B2 starts before the review, a review that changes the M6A/M6C public API forces rework in both stages, under the no-throwaway rule. The trigger already needs the owner: TODO.md:151 and RenderingContentDesign.md:204 make M6B start after the owner's FD-18 decision, and the owner controls when the review happens. So this is a process risk, not a code defect.
- **Fix:**
  1. Add an "implemented, pending review" status, `- [~]` plus a date. Define it once in TODO.md's header, and apply it the same way in ROADMAP.md and TODO.md, including TODO.md:137.
  2. Record the mode in ROADMAP's planning-decisions table, next to decision 5 (ROADMAP.md:40): "M6A, M6C: direct implementation, one joint review; C1-C6 review folded in". Update TODO.md:5 and :134.
  3. Hold the joint review before B2. B1, the M6B draft revision and the owner decisions can proceed meanwhile.
  4. Present the "now" defects as one list of review amendments, with file:line and a one-paragraph fix each. **The canonical list is Phase 1 of section 7.1**; the Top fixes and this step refer to it and do not repeat it. The owner gives one go-ahead for the mode (direct close-out or snippets), which is owner question Q12.
     - Most amendments are private code. Several touch two or three files, so they are not one-file snippets: RT-01 touches GameSession.cpp, Application.cpp and RavenEngine.cpp and adds a DataError type.
     - Parts that edit a contract input change the contract hash, comments included, because GameContract.cmake:6-13 hashes every Public .hpp plus RavenBuildSettings.cmake, RavenGame.cmake and RavenGameSources.cmake. Phase 1 marks them, and they land together in one hash-changing commit. Parts that change a public layout or flag, such as ECS-02 steps 1-2, move to Phase 3.
  5. Give the owner a review map: one table of changed Public headers and new public types.
  6. Record each stage's review outcome in its note.
- **Effort:** S for the docs and status steps (1-3, 5-6). Step 4's amendments are sized separately: they are the Phase 1 rows of the plan table, M in total.
- **Timing:** now
- **Panel discussion:** DOCS withdrew "the breakpoint was violated". The C1-C6 review was folded into the joint-review mode de facto, and master equals origin/master, so the owner approved the push. The panel kept high "only on the condition that B2 does not start before the review". Verification noted that this wording is inverted: the risk exists only if B2 starts first, and that start already needs the owner.
- **Verification:** confirmed with corrections; severity high -> medium, because this is a process risk whose trigger, B2 starting, already needs the owner's FD-18 decision, and rework happens only if the review changes public APIs. One canonical amendment list replaces the three diverging ones, the "one-file snippet" and "no contract hash" claims were corrected, and ROADMAP.md:40 was added as evidence.

#### AA2-DOC-02 ROADMAP status, checkboxes, design notes and platform docs contradict the code and each other
- **Severity:** medium
- **Kind:** doc-drift
- **Sources:** PROC-3, XP-9, RELOAD-9, ECS-F10
- **Evidence:**
  - ROADMAP.md:43-65 is the 2026-11-23 snapshot. It says Commands are deferred and that HierarchyTests is gpu-labeled, and it omits physics, UI, text, input contexts and the job system.
  - ROADMAP.md:92, 95 and 107 are unchecked although shipped. TODO.md:170 is unchecked while ROADMAP.md:188 is checked.
  - TODO.md:50 vs :169, the "(current)" headings at :109 and :130, and :137 and :23 are stale.
  - FutureDecisions.md:3 says "Nothing here is decided".
  - ECSDesign.md:7, 133-139, 209-211 describes D5, D8 and D9 as planned, not as built.
  - SceneFile.hpp:24-28 overstates round-tripping.
  - HotReloadDesign.md:245 says there is no on-screen display, but the overlay exists.
  - PlatformHandoff.md:3, 7 and MacOSBuild.md:3 are dated 2026-10-14.
  - WindowsPlayerReadme.txt:17 says XAudio2, and :4-6 lists old controls.
  - README.md:9, 13, 23, 32-35 describe a virtual Component and deleted folders.
- **Why it matters:** The M7 exit is "from the documentation alone", and M4 onboarding depends on these docs.
- **Fix:**
  1. Make one .md-only commit with a 2026-11-26 status snapshot that matches the code.
  2. Tick ROADMAP.md:92, 95, 107 and TODO.md:170 with notes, and reconcile the TODO items listed above.
  3. Add a Status column to FutureDecisions' calendar.
  4. Fix ECSDesign D5 (superseded by FD-14), line 7 and D9. Qualify the SceneFile.hpp comment. Fix HotReloadDesign:245, README and the player readme.
  5. Add a standing exit-checklist item: "ROADMAP status, TODO, README and PlatformHandoff updated for every contract this stage changed".
- **Effort:** S
- **Timing:** now
- **Panel discussion:** None.

#### AA2-DOC-03 The 2D-run record contradicts itself, and M6B owner decision 5 rests on a false "Today"
- **Severity:** medium
- **Kind:** doc-drift
- **Sources:** PROC-4
- **Evidence:**
  - DataModelDesign.md:667 and TODO.md:169 record the M6-7 2D run as done.
  - RenderingContentDesign.md:183 says it was "not run since M1", and its decision 5 (:21) and B1 (:208) rest on that.
  - GameServicesDesign.md:1002 and TODO.md:50 disagree with each other.
- **Why it matters:** The owner is being asked a player-visible question based on incorrect evidence.
- **Fix:**
  1. Correct :183: Courtyard ran at M6-7, but has not run since C7 introduced the overlay, so B1 records a post-C7 baseline.
  2. Close TODO.md:50.
  3. B1 stores a tracked capture-hash manifest (ARCH-DOC-03).
- **Effort:** S
- **Timing:** before M6B
- **Panel discussion:** None.

#### AA2-DOC-04 The previous assessment had no follow-up ledger
- **Severity:** medium
- **Kind:** process
- **Sources:** PROC-6 (replaced by the corrected ledger in section 4)
- **Evidence:**
  - ROADMAP.md:36 leaves the "now" items "opportunistic", and TODO.md:113-122 tracks only seven of them.
  - The DOCS round-1 ledger was itself wrong on HR-01, 02, 03 and 05.
- **Why it matters:** Opportunistic items drift. This review rediscovered about 20 of them, several of which sit on M6B's path.
- **Fix:**
  1. Copy the section 4 table into TODO.md as "Assessment follow-up", with each row's home step. Rows marked fixed get one line and are not carried further.
  2. Add the same table for the AA2-* IDs using the section 7 plan.
  3. Each stage exit checklist reviews the table.
- **Effort:** S
- **Timing:** now
- **Panel discussion:** RELOAD corrected the HR rows using git dates: ReloadService.cpp was last changed 77 minutes before the previous assessment was committed.

#### AA2-DOC-05 Provisional decisions and residual work pile up while implementation builds on them, and stage records omit "planned but not built"
- **Severity:** medium
- **Kind:** process
- **Sources:** PROC-7, MEDIA question to DOCS
- **Evidence:**
  - GameServicesDesign.md:5, 144, 541, 1018: FD-47 is still provisional even after its measurement.
  - UIDesign.md:4 is a draft with sections 3, 5, 6 and 7 unwritten. ROADMAP.md:41 marks planning decision 6 as provisional.
  - TODO.md:90, 106, 120, 145-146, 168 hold residuals with no home.
  - GameServicesDesign.md:594, 612-620: the C7 record omits fonts-as-assets, worker shaping, shaping language and atlas eviction.
- **Why it matters:** M4 and M6B readers cannot tell which contracts are final.
- **Fix:**
  1. In the joint review, present each provisional item with its measurement, including FD-47 with the AA2-GP-07 study, and record it as "final" or "revisit at trigger X".
  2. Add a "Deferred" section to TODO.md: one line per residual, with its trigger and home milestone. The debugger-across-reload check becomes an M6B exit item.
  3. Give every stage record three parts: Built, Differences, and Planned but not built (with a home for each).
  4. Close C7-C10 in ROADMAP once the residuals have homes.
- **Effort:** S
- **Timing:** before M6B
- **Panel discussion:** None.

#### AA2-DOC-06 The working rules live only in a gitignored AGENTS.md
- **Severity:** medium
- **Kind:** process
- **Sources:** PROC-8 step 1
- **Evidence:**
  - .gitignore:5 ignores AGENTS.md.
  - A grep of the tracked docs finds no statement of the rules: one file per object, byte-identical vendoring, owner-question scope and delegated-decision criteria.
- **Why it matters:** The implementation team and M4 contributors cannot follow rules they cannot see. The M7 exit is "from the documentation alone".
- **Fix:**
  1. Add a tracked docs/WorkingRules.md with each rule, its date and the owner's wording, and link it from README.
  2. Keep AGENTS.md as a local pointer to it.
- **Effort:** S
- **Timing:** now
- **Panel discussion:** None.

#### AA2-DOC-07 The M6B draft has no B0 contracts, contract-change manifest, tracked-tests, threads or M4 sections
- **Severity:** medium
- **Kind:** architecture-gap
- **Sources:** PROC-9, DOCS round 2 manifest
- **Evidence:**
  - RenderingContentDesign.md:209, 212 change public components.
  - :61 covers only generated scenes, but Courtyard's levels have no generator (it has no Tools folder).
  - SceneFile.cpp:797-817: FD-16 has no value converter.
  - The draft has no tracked-tests section, and its only threads line is at :198.
  - :225 sets the sprite budget only after the fact.
  - UserData.md:78: quick-saves are memory-only, so they need no migration.
- **Why it matters:** Without these sections there is a risk of a second unplanned contract break and of saved data that no longer loads.
- **Fix:**
  1. Add a "B0 renderer contracts" section covering AA2-RND-01..07, PERF-01 and HR-01, scoped after owner questions Q3 and Q8.
  2. Add a "Contract changes" section: the manifest in section 7, with one window at the start of B2.
  3. Make B2 additive: keep MaterialComponent's inline fields as the procedural default entry, and add `AssetRef<MaterialAsset> Material`. Retire the inline fields only through FD-16's (unwritten) upgrade pass.
  4. Add a "Tracked tests" section with one SceneFileTests case: a pre-B2 MaterialComponent loads with its inline values intact.
  5. Add a "Threads and processes" section (the AA2-PERF-03 table) and an M4/MoltenVK row for each decision.
  6. Set the sprite budget a priori from M6C measurement 7.
  7. Replace "CI runs on the next push" with the run link in every exit record.
- **Effort:** S
- **Timing:** before any B2 code
- **Panel discussion:** None.

#### AA2-DOC-08 The v1 scope exclusions that matter to the action direction were never put to the owner
- **Severity:** medium
- **Kind:** process
- **Sources:** PROC-11, RND-11, GSV-12
- **Evidence:**
  - FutureDecisions.md:662, 672: skinning is "After v1".
  - UserData.md:78: no player save games.
  - GameServicesDesign.md:176, 195: networking is not built.
  - There is no "Not in v1" list.
- **Why it matters:** M6B's ABI and record choices depend on these answers, under the no-throwaway rule.
- **Fix:**
  1. Put owner questions Q3, Q4 and Q5 one at a time.
  2. Record the answers as a "Not in v1" list in ROADMAP.
  3. If skinning is in v1, the B0 hooks stay as proposed. If save games are in v1, AA2-ECS-05 steps 2-3 move to M6B or M7.
- **Effort:** S
- **Timing:** now, before B0 is written
- **Panel discussion:** None.

## 6. Cross-cutting themes

1. **The authoritative transform is written by presentation code.**
   - Interpolation's Apply/Restore and unchanged mutable-view writes re-stamp TransformComponent. Physics, picking and the render world all read those ticks.
   - One decision removes the root cause: interpolation becomes a render-side override (AA2-PERF-01, high, because it threatens the recorded 3.5 ms preparation budget). Two small fixes close the remaining triggers: kinematic zeroing (AA2-GP-01, latent until the first door or elevator) and world stamping only on a real change (AA2-ECS-03).

2. **Determinism level 1 holds only at identical pacing.** This is latent: no shipped game triggers it yet.
   - Frame picks (duplicated across catch-up steps, or missed on zero-step frames), the live UI and user data reach fixed steps (AA2-GP-02, GP-05). The per-tick InputState is per tick but is not recorded in the stream.
   - Streams are shared across phases, and step time is not derived from the tick (AA2-GP-03).
   - Child colliders lag one frame (AA2-GP-02).
   - The check pins one tick per frame and runs without user data, so it cannot see any of this.
   - The remedy is one phase-aware context, which also keeps the RTS lockstep and replay options open.

3. **FD-06 is right but incompletely applied.**
   - Render has no boundary (AA2-RT-01), and a designer's dump/apply lighting edit already reaches it.
   - Data and capacity errors carry logic_error types (AA2-RT-01, RT-02).
   - "Try" lookups throw (AA2-RT-03).
   - Two mirrors lack a reset (AA2-RT-04).
   - Failing candidates retry forever (AA2-RT-05).
   - The tiers themselves stay as they are. The fixes are validate-at-gather plus a runtime_error-derived DataError, and a Render stage that recovers DataError only, so device loss stays fatal.

4. **Unreviewed contracts plus a pending contract-heavy stage.**
   - Two stages are awaiting one review (AA2-DOC-01).
   - The public tree is not the game API (AA2-CORE-03), and dead APIs inflate it (AA2-CORE-04).
   - M6B will change public components (AA2-DOC-07).
   - Everything public should go through one window, after the review.

5. **Windows-only development since M6A has made goal 1 harder without anyone noticing** (AA2-PLAT-01..06, AA2-HR-02). The host executable no longer compiles on POSIX (RavenEngine.cpp:169). Every Windows-side fix in this area is a CMake property or a small seam, and none needs an M4 contributor.

6. **"Every design studies threads" is stated more often than it is delivered.**
   - D9 access was never built (AA2-ECS-06).
   - Movers are serial (AA2-GP-07).
   - There is no range ParallelFor (AA2-PERF-05).
   - Prepare is never called (AA2-MEDIA-08).
   - There is no shared thread-placement table (AA2-PERF-03).

7. **Scale costs follow total size, not change.**
   - Four O(N) change walks (AA2-PERF-02).
   - A world pass that does not use change detection (AA2-ECS-07).
   - Full-content reload snapshots (AA2-HR-04).
   - An append-only material table (AA2-RND-05).
   - Benchmarks that cannot see these costs (AA2-PERF-04).

8. **Docs describe plans as delivered.** Examples: D9, Prepare, fonts as assets, the player F3/F4 record, the 2D run, and "the M4 contract". Every stage record needs a "planned but not built" part (AA2-DOC-05).

9. **The draft M6B plan is under-specified rather than wrong** (AA2-RND-01..04, verified down to medium). FD-19's record, B4's (pipeline, mesh) grouping and B6's batch are already planned. What is missing is the plumbing and the contracts between them: the fragment-side path to the record, group identity and retirement, CPU sorting for blended sprites, and the upload and descriptor ordering. Writing these into the draft now costs text, not code.

## 7. Prioritized fix plan

### 7.1 Sequencing

The order relative to the owner reviews:

1. **Phase 0, now, before the joint review.** Docs-only items that need no engine approval, though committing them still needs the owner's go-ahead:
   - AA2-DOC-02, DOC-03, DOC-04, DOC-06.
   - The M6B draft revisions: AA2-DOC-07 and the B0 text of RND-01..07, including the corrected sprite plan (RND-03 step 6) and the upload and descriptor contract (RND-04 step 6).
   - PLAT-01 step 1 (handoff rewrite).
   - ECS-05 step 1.
   - GP-07 step 1 (the FD-47 threads study).
   - GP-11 step 1.
   - HR-02 step 3 (the module contract in EngineModules.md).
   - Pose the owner questions in 9a, one at a time.

2. **Phase 1, the M6A+M6C joint review (AA2-DOC-01).** This is **the canonical review-amendments list**; the Top fixes and AA2-DOC-01 step 4 refer to it. The amendments are presented as one list. After the owner's go-ahead (Q12), they land before any B2 code, in this order.
   - Items marked † edit a contract input, so they change the contract hash (comments included). The inputs are every Public .hpp plus RavenBuildSettings.cmake, RavenGame.cmake and RavenGameSources.cmake (GameContract.cmake:6-13). They land together in one hash-changing commit, which costs one game rebuild before the Phase 3 window. Everything else is private code with no contract-hash change.
   - **High, first:**
     - RT-01 (step 1 is †: SceneLighting.hpp);
     - MEDIA-01 with its test;
     - CORE-01 steps 1-6 (†: GameServices). It is a player regression, so it does not wait for Phase 3.
   - **Medium defects:**
     - GP-01 with its PhysicsTests case;
     - RT-02, RT-03 (step 1's comment is †: Scene.hpp), RT-05;
     - GP-04 (step 2 is †: InputStream.hpp), GP-12;
     - MEDIA-02, MEDIA-03 step 1, MEDIA-04 step 1;
     - HR-03, HR-05;
     - ECS-02 steps 3-6 (steps 1-2 change the public ComponentInfo::Flags and move to Phase 3);
     - GP-05 steps 1, 3 and 5 (step 1's Save comment is †: Raven/Core/UserData.hpp; step 2's HasUserData is a GameServices addition and moves to Phase 3; step 4 lands with GP-02 below), GP-06.
   - **Build, platform and module settings:**
     - BLD-01 (†: RavenGame.cmake), BLD-02;
     - PLAT-02 steps 4-8 (step 1 is BLD-01 and step 3 is PLAT-03/04 plus BLD-02, listed here; step 2 edits RavenGame.cmake and moves to Phase 3);
     - PLAT-03 steps 1-2, PLAT-04 steps 1-3 (properties only);
     - HR-02 step 2 (the AddComponent guard with its platform seam).
   - **Medium, structural, after the above:**
     - ECS-01 (the pin is †: Component.hpp), ECS-03;
     - PERF-02 step 2 and PERF-06;
     - RND-05 step 1, RND-07 (step 4 is †: VulkanResourceStats in VulkanContext.hpp);
     - CORE-02 steps 1-2;
     - GP-02 steps 1-4 and 7 (step 7 is †: GameSystems.hpp), together with GP-03 (steps 2 and 6 are †: Scene.hpp, GameSystems.hpp) and GP-05 step 4, which need GP-02 step 1's session-held phase. These are latent, and they come before M6B adds game code.
   - **Low:** RT-04.

3. **Phase 2, baseline before B4.** AA2-PERF-04, PERF-07, PERF-01 step 1, and the ECS-07 churn measurement. B1 records the post-C7 2D capture manifest (AA2-DOC-03).

4. **Phase 3, the contract-break window at the start of B2.** One host restart carries all of the following:
   - **Layout, persisted:** MaterialComponent gains an AssetRef, additively (B2). SpriteComponent gains Vec4 UvRect and SizePivot (RND-03 steps 2 and 4, only on a yes to Q8). The ECS-04 runtime flag.
   - **Layout, not persisted:**
     - PickHit (GP-08), ActionSample and Axis2D (GP-10);
     - Ui Style Font and Weight (MEDIA-05), UiText (MEDIA-06);
     - SystemDesc access (ECS-06), ComponentInfo host-owned flag (ECS-02 steps 1-2);
     - GameServices additions: HasUserData (GP-05 step 2) and audio pause (MEDIA-07). RequestScene and RequestRestart (CORE-01) land earlier, in Phase 1's † commit, unless the owner's Q10 answer moves them here.
   - **Hash-only:**
     - the dead API deletion (CORE-04) and the header tiers (CORE-03);
     - /utf-8 (BLD-04) and game-module visibility (HR-02 step 1), both carried by PLAT-02 step 2 in RavenGame.cmake and RavenBuildSettings.cmake. Off Windows the visibility takes effect only once that step removes the RavenGame.cmake:4 gate;
     - the FieldKind pin, if it did not land earlier with the amendments (ECS-01).
   - **Not contract, lands as private code:** the fragment ABI v2 (RND-01). Only the GpuMaterialData widening must land before B2's material table; the rest is B4's first snippet. It becomes contract when B4 lets games author shaders.

   FD-16 coverage: added fields take defaults that reproduce today's output. Removed or retyped fields lose values, which is why B2 must be additive. Quick-saves are memory-only and need no migration. Courtyard levels are authored data that migrate through FD-16.

5. **Phase 4, during M6B, in the reordered step order:**
   - B4's RenderWorld restructuring (RND-02, PERF-02 step 3, PERF-01 steps 2-4) comes first, then B2 material keying (RND-05). RND-02 owns this reorder.
   - The upload batch (RND-04 step 1) is the first texture snippet, and B3 mips record into it. RND-04 step 3's new-element path for placeholders and streaming follows B2's material map (RND-05 steps 2-4).
   - HR-01 steps 1-3 land before B4. B4 shaders then come with HR-01 steps 4-6 and RND-06, scheduled together to avoid double edits.
   - At B5: sprites move to the render world (RND-03), and PERF-01 steps 5-6 delete Restore and the second world pass.
   - Also in this phase: CORE-02 steps 3-5, ECS-04/06/07/08, MEDIA-03..08, PERF-03/05/08, HR-04/06/08, RND-08/09, and DOC-05.

6. **M4.** PLAT-01 step 4, PLAT-02 step 10, HR-02 steps 4-6, PLAT-04 linker flags, PLAT-05, PLAT-06, GP-05 step 5 (POSIX). PLAT-01 steps 5-6 only as the owner answers Q13.

7. **M7.** CORE-05 steps 3-5, HR-07, and the remaining ECS-08 items.

8. **Later.** CORE-01 step 7 (the carried record, when a game first needs it), GP-07 step 2 (crowd milestone), GP-02 step 6 and GP-11 step 2 (replay/RTS), ECS-05 steps 2-4 (editor and streaming).

### 7.2 Tracked tests under the minimal-tests rule

A test qualifies only if it guards behavior that an upcoming change rewrites.

| Test | Guards | Rewritten by | Verdict |
|---|---|---|---|
| MEDIA-01 slot clip ownership (CPU; RAVEN_API on AudioMixer::Render and the AudioVoices members it calls; loop virtualized with SetVolume(0); weak_ptr checked after the fade plus one block) | Clip lifetime across virtualize, StopAll and release | C8 streaming (next) | Add with the fix |
| GP-01 kinematic rewrite case, extending PhysicsTests | No drift on an unchanged rewrite | PERF-02 step 2 (PhysicsWorldSync rewritten as a changed-since scan) and PERF-01 (Restore stamp removed). If PERF-02 step 2 is not scheduled: the first kinematic door/elevator content | Add with the fix |
| GP-03 step time from tick, extending FixedStepTests | Time after restart and quick-load | Replay / level 2 | Extend the existing case |
| SceneFileTests: pre-B2 MaterialComponent | Inline values survive the B2 change | B2 | Add at B2 |
| DeterminismCheck relabeled CPU through headless | Level 1 | CORE-02 extraction | Relabel at the end of M6B; no new test |
| PERF-05 JobSystem | Range ParallelFor | Queue replacement (step 4) | Only if step 4 is scheduled; otherwise an ignored probe |
| ECS-01 FieldKind | Ordinals | - | static_assert; no test |
| HR-03, HR-05, RT-*, GP-06, MEDIA-02 | - | - | Ignored probes only |

### 7.3 Plan table

| Priority | ID | Fix (short) | Severity | Effort | Timing | Depends on |
|---|---|---|---|---|---|---|
| 1 | AA2-DOC-01 | Record the mode; hold the joint review before B2; one canonical amendments list (gate for the Phase 1 amendments of section 7.1, which include the rows timed "review amendment" and the "now" parts listed there) | medium | S (docs); amendments sized per row | now | - |
| 2 | AA2-RT-01 | Lighting/sprite gather validation (incl. MVP overflow), DataError, DataError-only Render stage, SaveAll on exit | high | S | review amendment (step 1 †) | DOC-01 |
| 3 | AA2-MEDIA-01 | Slot-owned clips; per-slot retry of dropped Stops; CPU test | high | S | review amendment | DOC-01 |
| 4 | AA2-CORE-01 | RequestScene/RequestRestart, transition state, Samples flow, doc hints (steps 1-6) | high | S | review amendment (†), before B2 (Q10) | DOC-01 |
| 5 | AA2-PERF-01 | Measure vs 3.5 ms budget; render-side override (B4); delete Restore at B5 | high | M | step 1 before M6B; B4; B5 | PERF-04, PERF-06, RND-02 |
| 6 | AA2-PLAT-01 | Handoff chain; host compiles on POSIX (via PLAT-02); Linux compile job incl. host target | high | M | now (Q2, Q13) | PLAT-02 |
| 7 | AA2-GP-01 | Zero kinematic velocity on an unchanged rewrite; PhysicsTests case | medium | S | review amendment | DOC-01 |
| 8 | AA2-RT-02 | Capacity exhaustion becomes DataError | medium | S | review amendment | RT-01 step 3 |
| 9 | AA2-RT-03 | TryUseAsset remembers failures | medium | S | review amendment (comment †) | - |
| 10 | AA2-RT-05 | Drop failing scene/project candidates; lease before instantiate | medium | S | review amendment | - |
| 11 | AA2-GP-04 | Latch Arena craft input; document latching | medium | S | review amendment (step 2 †) | - |
| 12 | AA2-GP-12 | Samples F6 to mapped actions; shadow warning | medium | S | review amendment | - |
| 13 | AA2-HR-03 | F5 guard; watch the stamp file; clear failure | medium | S | review amendment | - |
| 14 | AA2-HR-05 | Sweeps; LoadGeneration; in-place mode | medium | S | review amendment | - |
| 15 | AA2-MEDIA-02 | Bound decoded audio; trust policy | medium | S | review amendment | - |
| 16 | AA2-ECS-02 | Record-time host-owned rejection; schema-required apply; drop commands on failed stage (flag in window) | medium | S | review amendment (steps 3-6); window (steps 1-2) | - |
| 17 | AA2-GP-03 | Presentation ID and RNG streams; step time from tick | medium | S | review amendment, with GP-02 step 1 (steps 2, 6 †) | GP-02 step 1 |
| 18 | AA2-GP-05 | Deferred user-data saves; FlushFileBuffers | medium | S | review amendment (step 1 comment †; step 4 with GP-02; step 2 in window) | GP-02 |
| 19 | AA2-GP-02 | Phase-aware MakeContext; record per-tick InputState; child-collider compose | medium | M | review amendment, before M6B adds game code (steps 1-4, 7; step 7 †) | - |
| 20 | AA2-ECS-01 | Pin FieldKind; format version policy | medium | S | review amendment (pin †); policy before B2 | - |
| 21 | AA2-DOC-07 | B0 contracts, manifest, tests, threads sections in the M6B note | medium | S | now | DOC-08 answers |
| 22 | AA2-RND-01 | Fragment-side plumbing for FD-19's record (ABI v2) | medium | M | widening before B2; rest first B4 snippet | DOC-07 |
| 23 | AA2-RND-02 | Group identity/retirement, command index, persistent mesh table with revision counter | medium | M | first B4 snippet | DOC-07, PERF-04 |
| 24 | AA2-RND-04 | Upload batch first; reload wait; new-element retirement for placeholders/streaming; contract doc | medium | M | B3/B6; new-element path after B2 | DOC-07, RND-05 |
| 25 | AA2-RND-03 | Sprites as render-world objects; blended stay CPU-sorted; keep Opaque behavior | medium | M | B5 | RND-02, Q8, Q9 |
| 26 | AA2-HR-01 | Non-blocking process wait, process lane, digest startup (before B4); ProgramDesc and one table (with B4) | medium | M-L | steps 1-3 before B4; 4-6 with B4 | PERF-03 |
| 27 | AA2-CORE-04 | Delete dead public APIs and PngFileWatch | low | S | contract window | - |
| 28 | AA2-CORE-03 | AssetLease split; host root; contract narrowed; RegistryAccess | medium | M | contract window | CORE-04 |
| 29 | AA2-HR-02 | Hidden visibility; AddComponent image guard; module contract doc | medium | S | steps 2-3 now; step 1 in window | PLAT-02 step 2 (off Windows) |
| 30 | AA2-BLD-01 | Raise the CMake floor | medium | S | review amendment (†: RavenGame.cmake) | - |
| 31 | AA2-PLAT-02 | Ordered seam list (incl. SupportsBuildProcesses, UTF-8 config constants) | medium | M | before M6B (steps 1, 3-8 now; step 2 in window) | BLD-01 |
| 32 | AA2-PLAT-03 | Jolt ISA parity; determinism flag explicit | medium | S | now (Q1) | - |
| 33 | AA2-PLAT-04 | Third-party and RavenCore visibility | medium | S | now / M4 | - |
| 34 | AA2-CORE-02 | Ownership moves; loop extraction; headless DeterminismCheck | medium | M | steps 1-2 before B6 | - |
| 35 | AA2-PERF-04 | Game-scale and variety benchmark cases | medium | S | before B4 | - |
| 36 | AA2-PERF-06 | restore_ns trace section | medium | S | now | - |
| 37 | AA2-PERF-07 | Animated-fraction A/B explains the 0.27 ms | medium | S | before B4 | PERF-06 |
| 38 | AA2-PERF-02 | Changed-since dense scan; physics first | medium | M | now / B4 | ECS-08 step 4 |
| 39 | AA2-ECS-03 | Stamp world only on change | medium | S | now / B4 | - |
| 40 | AA2-PERF-03 | Background limit; thread placement table; callback contract | medium | S | before M6B | - |
| 41 | AA2-PERF-05 | Range ParallelFor; Done flag | medium | S | before B4 | - |
| 42 | AA2-RND-05 | Mutable material table; white on free | medium | M | step 1 now; B2 | RND-02 |
| 43 | AA2-RND-06 | Material shaders as assets | medium | M | B4 | HR-01 |
| 44 | AA2-RND-07 | Device limits; bindless classes | medium | S | review amendment (step 4 †) | RT-02 |
| 45 | AA2-RND-08 | Sync validation; memory budget | medium | S | before B6 | - |
| 46 | AA2-RND-09 | Skinning constraint in B0 (code only on a yes to Q3) | medium | S | B0 | Q3 |
| 47 | AA2-HR-04 | Digest model; no resident bytes; budgeted audit | medium | M | before B5-B7 | - |
| 48 | AA2-GP-06 | Inner-body BodyFilter | medium | S | now | - |
| 49 | AA2-GP-07 | FD-47 threads study now; parallel movers later | medium | S/M | now / crowd milestone | GP-06, PERF-05 |
| 50 | AA2-GP-08 | Pick request API; PickHit point/normal | medium | M | contract window | - |
| 51 | AA2-GP-09 | Layer collision matrix | medium | S | during M6B | - |
| 52 | AA2-GP-10 | Input addendum; Axis2D; cursor mode | medium | M | design now; window | - |
| 53 | AA2-MEDIA-03 | Per-glyph catch; batch pages; LRU | medium | M | now / B6 | RND-04 |
| 54 | AA2-MEDIA-04 | Stale filter set now; UI as a router context | medium | M | step 1 review amendment; steps 2-3 before M6B ends | GP-10 |
| 55 | AA2-MEDIA-05 | Font assets, family, weight, language | medium | M | during M6B | Q6 |
| 56 | AA2-MEDIA-06 | UiText localization seam | medium | M | window | Q6 |
| 57 | AA2-MEDIA-07 | Bus pause | medium | S | before M6B ends | Q7 |
| 58 | AA2-ECS-04 | RAVEN_VALUE; runtime field flag | medium | M | during M6B | ECS-01 |
| 59 | AA2-ECS-05 | Document lossy capture; lossless capture later | medium | M | doc now | Q4 |
| 60 | AA2-ECS-06 | SystemDesc access; concurrent views | medium | M | window / M6B | PERF-05 |
| 61 | AA2-ECS-07 | Churn soak; recompaction if needed | medium | M | before B4 | PERF-02 |
| 62 | AA2-BLD-02 | ufbx/stb cmake, update scripts, manifests | medium | S | before B7 | - |
| 63 | AA2-DOC-02 | Status snapshot and doc fixes | medium | S | now | - |
| 64 | AA2-DOC-03 | Correct the 2D record; capture manifest | medium | S | before M6B | - |
| 65 | AA2-DOC-04 | Ledger into TODO | medium | S | now | - |
| 66 | AA2-DOC-05 | Deferred ledger; "planned but not built" | medium | S | before M6B | DOC-01 |
| 67 | AA2-DOC-06 | Tracked WorkingRules.md | medium | S | now | - |
| 68 | AA2-DOC-08 | v1 scope exclusions | medium | S | now | Q3-Q5 |
| 69 | AA2-CORE-05 | Player diagnostics | medium | S | now / M7 | PERF-08 |
| 70 | AA2-RT-04 | Physics/pick resets; noexcept Jolt callbacks | low | S | review amendment | - |
| 71 | AA2-HR-06 | Stage the copy off the frame thread | low | S | during M6B | HR-05 |
| 72 | AA2-HR-07 | DLL search path | low | S | M7 | - |
| 73 | AA2-HR-08 | Build priority and -j | low | S | during M6B | PERF-03 |
| 74 | AA2-ECS-08 | ECS sharp edges; EachInIdOrder | low | S | opportunistic | - |
| 75 | AA2-MEDIA-08 | Call Prepare | low | S | during M6B | - |
| 76 | AA2-MEDIA-09 | Text/UI key hardening | low | S | opportunistic | - |
| 77 | AA2-PERF-08 | Logger thread | low | S | during M6B | - |
| 78 | AA2-PERF-09 | Allocation churn | low | S | opportunistic | PERF-02 |
| 79 | AA2-RND-10 | Renderer doc drift | low | S | now | - |
| 80 | AA2-BLD-03 | CI time, smoke step, presets, SHAs | low | S | now / before M6B | CORE-02 |
| 81 | AA2-BLD-04 | Shared /utf-8 | low | S | window | - |
| 82 | AA2-PLAT-05 | macOS floor and feature probe | low | S | before first macOS build | - |
| 83 | AA2-PLAT-06 | -z defs; hook rule | low | S | with Linux job | PLAT-01 |
| 84 | AA2-GP-11 | Snapshot is not a checkpoint (docs) | low | S | docs now | - |
| 85 | AA2-CORE-01 step 7 | Typed carried record (non-template seam; created after StartSimulation; dropped on reload) | medium | M | when a game first needs carried state | CORE-01 steps 1-6 |

"†" marks a part that edits a contract input (a Public header, or RavenBuildSettings.cmake, RavenGame.cmake or RavenGameSources.cmake, per GameContract.cmake:6-13) and therefore changes the contract hash; see section 7.1 Phase 1. Priorities 2-6 are the five findings that stay high after verification.

## 8. Suggested roadmap and TODO adjustments (proposals only)

1. **ROADMAP planning-decisions table.** Add "M6A, M6C: direct implementation, one joint review (C1-C6 review folded in)" and an "implemented, pending review" status marker. Update TODO.md:5 and :134.
2. **ROADMAP.md:190, subject to owner question Q13.** Change "M6A is the contract M4 ports against" to "end of M6B".
3. **Replace "Where the engine stands (2026-11-23)"** with a 2026-11-26 snapshot. Tick ROADMAP.md:92, 95, 107 and TODO.md:170.
4. **Add an "M6C review amendments" block to TODO.md** that copies the canonical Phase 1 list of section 7.1, including the † markers. It closes before B2.
5. **Insert "B0 contracts" before B2 in the M6B step list,** and reorder the steps:
   - the upload batch as the first texture snippet, with B3 mips recorded into it;
   - B4's RenderWorld restructuring before B2's material keying;
   - one contract window at the start of B2.
6. **Add three M6B exit items:**
   - DeterminismCheck runs, locally until the headless host lands, then in CI;
   - the debugger-across-reload check;
   - one synchronization-validated run.
7. **Add "Not in v1" to ROADMAP** after owner questions Q3-Q5.
8. **Add an "Assessment follow-up" table** (section 4) and a "Deferred" table (residual, trigger, home milestone) to TODO.md.
9. **Subject to owner question Q13: split each stage exit into a Windows exit and a platform exit,** and append each stage's new platform surface to M4.
10. **Add a pre-M4 item: the PLAT-02 ordered seam list,** done on the Windows side so M4 starts from branch points. Add a non-blocking Linux compile job if the owner answers Q2 with yes.
11. **Mark FD-47 and planning decision 6** as "final" or "revisit at trigger X" during the joint review.
12. **Add a standing item to every stage exit checklist:** "docs updated for every contract this stage changed; planned-but-not-built list present".

## 9. Decisions

### 9a. Owner questions (13)

Only cost or licensing, player- or designer-visible behavior, or scope questions are listed here. Ask them one at a time. Each one gives the recommendation first, then the strengths of every option. The M6B decisions already listed in RenderingContentDesign.md (FD-18, FD-19, FD-23, FD-26 and the 2D run) are not repeated.

**Q12 (ask first): How should the review amendments be implemented?** (scope and process)
- Recommendation: as M6C close-out, implemented directly after the joint review and before B2, then reviewed as one list (the canonical Phase 1 list in section 7.1). The items are bugs in M6A and M6C code that is still pending review. The parts marked † change the contract hash and land in one commit, costing one game rebuild.
- Option strengths:
  - Direct close-out: fastest, lands before B2, and needs one review instead of about 20.
  - Snippets per AGENTS.md: the owner applies each change, keeping the fullest control.

**Q1: Minimum CPU instruction set for players on every OS** (player-visible; AA2-PLAT-03)
- Recommendation: keep the SSE2 floor that Windows ships today on every OS for v1. Revisit only if a physics measurement shows a gain. Cross-OS determinism stays out of v1.
- Option strengths:
  - SSE2: widest reach and no change on Windows.
  - SSE4.2: covers practically every x86-64 PC still in use, with a modest Jolt gain. The gain is unmeasured.
  - AVX2: the minimum-spec laptop has it and Jolt is fastest with it, but it excludes some older and low-end CPUs.

**Q2: A non-blocking Linux compile-only CI job** (cost: CI minutes; ARCH-PLAT-02, AA2-PLAT-01)
- Recommendation: yes, non-blocking. It compiles the RavenCore, RavenEngine and RavenSceneTool targets with `-k 0` and needs no code change. The host target matters, because the host does not compile off Windows today (AA2-PLAT-01). Link errors are expected until FillRandomBytes is ported.
- Option strengths:
  - Yes: goal 1 stops accumulating breakage nobody sees, and it costs only an extra job per push.
  - Keep planning decision 4 (Linux CI at M4): zero CI cost now.

**Q3: Is skeletal animation in v1?** (scope; AA2-RND-09)
- Recommendation: post-v1. Record the compute-skinning path and the "do not widen the 128-byte record" constraint in the M6B B0 contracts now, as text only. Under the no-throwaway rule, no vertex-base stream code is added unless skinning is in v1.
- Option strengths:
  - In v1: animated action characters on day one, at the cost of an importer, a skinning pass and a larger M6B.
  - Post-v1 with the recorded constraint: M6B keeps its size, and adding skinning later needs no rework of the record.

**Q4: Are player save games (save slots or checkpoints beyond profiles) in v1?** (scope; AA2-ECS-05, GP-11)
- Recommendation: not in v1. Keep the lossless-capture path (AA2-ECS-05 steps 2-3) as the planned route.
- Option strengths:
  - In v1: required by many action games. It pulls lossless capture and runtime-only fields into M6B or M7.
  - Not in v1: smaller scope, while the route stays open.

**Q5: Confirm that networking (prediction, rollback, lockstep) is excluded from v1** (scope; AA2-GP-11)
- Recommendation: confirm the exclusion, and keep the boundary (the per-tick record list and the seeds) as designed.
- Option strengths:
  - Exclude: the scope stays as planned.
  - Include: unlocks multiplayer, but needs checkpoints and level-2 work far beyond v1.

**Q6: Launch languages and the fonts to ship** (cost, licensing, size; AA2-MEDIA-05, MEDIA-06)
- Recommendation: keep the current Noto Sans, Arabic and KR set for v1 (OFL, no license cost), and fix the ICU data filter to match. Add Japanese and Simplified/Traditional Chinese fonts only when a target market needs them.
- Option strengths:
  - Current set: no new cost, and RTL and CJK are already proven.
  - Add JP/SC/TC: correct Han glyph forms for those markets, at a cost in package size.

**Q7: Does music keep playing behind a pause?** (player-visible; AA2-MEDIA-07)
- Recommendation: music keeps playing. Gameplay buses pause, and Interface keeps playing.
- Option strengths:
  - Music continues: the common pause-menu feel.
  - Music pauses: everything freezes together, which suits tense or narrative games.

**Q8: Are sprite sheets (UV frames) in v1?** (designer-visible scope; AA2-RND-03)
- Recommendation: yes. Add UvRect, size and pivot in the B2 window with defaults that reproduce today's output. This avoids a later record and component migration.
- Option strengths:
  - Yes: flipbook VFX and 2D animation become possible, and the cost is low now.
  - No: a smaller B5.

**Q9: Should cut-out (alpha-tested) sprites follow sort layers or real depth?** (designer-visible; AA2-RND-03)
- Recommendation: keep Opaque sprites exactly as today (real Z, no alpha test). Add Cutout as a new mode that is also depth-ordered. Sort layers order only blended sprites.
- Option strengths:
  - Depth-ordered: nothing on screen changes, and it mixes correctly with 3D meshes.
  - Layer-ordered: 2D-style authoring control, at the cost of changing current overlap.

**Q10: When does game-driven scene flow land?** (scope; AA2-CORE-01)
- Recommendation: now, as a review amendment before M6B B2. It is small, and the packaged player cannot reach its second scene or restart without it. Landing it before B6 also keeps async loading behind the same request API. The typed carried record is a separate later item (AA2-CORE-01 step 7).
- Option strengths:
  - Before or in M6B: players can reach their second level now.
  - M7: no change to M6B.

**Q11: What is the performance target off the laptop?** (FD-39, scope; ask before M4 work starts in earnest)
- Recommendation: "runs at interactive rates; record numbers" on macOS and Linux for v1, with no budget target.
- Option strengths:
  - No target: M4 stays a parity milestone.
  - A target: a stronger product promise, but it needs reference hardware for every OS.

**Q13: Should each stage exit be split into a Windows exit and a platform exit, and should M4 port against the end-of-M6B contract?** (scope and roadmap; AA2-PLAT-01 steps 5-6)
- Recommendation: yes to both.
  - Close every stage on Windows with an explicit "platform exit" that is appended to M4 once.
  - Change ROADMAP.md:190 from "M6A is the stable contract M4 ports against" to "the end of M6B", because C1-C3 and B2/B5 change the public contract again.
- Option strengths:
  - Split exits plus the end-of-M6B contract: the roadmap matches what is verified, and M4 contributors port once against a contract that will not move under them.
  - Keep the current wording: no roadmap change now. The "all platforms" exits (ROADMAP.md:160, :174) then stay formally open while the stages are treated as closed.

These items come later, not now: gamepad support and the SDL3 dependency (when a caller exists, AA2-GP-10), and mod or user-generated content trust (when a genre asks for it, AA2-MEDIA-02).

### 9b. Technical choices recommended as delegated

Each item below is decided by business alignment and expandability, and recorded as "delegated (owner rule 2026-11-25)" when it lands. It is also listed in the joint review's "choices worth your confirmation" so the owner can overrule it without being asked.

| Decision | Choice | Why it wins | Recorded in | Writer |
|---|---|---|---|---|
| Interpolation output | CPU render-only override keyed by entity, built in B4 and also read by the camera view and audio listener; Restore and the second pass deleted at B5 once sprites use the override; GPU rows only as a measured fallback | Rigid blends, no ABI change, same bandwidth as today, deletes the second pass and the tick coupling without throwaway sprite code | RenderingContentDesign B0, FD-34 | RENDER, PERF |
| Picking time | Latest tick | Matches the simulation; at most one tick of difference | FD-34 | GAMEPLAY |
| Error taxonomy | Keep FD-06 tiers; validate and skip at gather (including MVP overflow); DataError : runtime_error for escaped data and capacity; boundary types unchanged; the Render stage catches DataError only, so device loss and post-acquire failures stay fatal | Keeps misuse fatal at stage level, recovers data errors, never retries after swapchain acquisition | FD-06 | CORE, ECS |
| Determinism streams | Separate OS-seeded presentation ID and RNG streams for Control and Update; transient tag excluded from the hash | Allows cosmetic spawns; follows the sound-RNG and contact-phase precedents | FD-48 | GAMEPLAY |
| Fixed-step context | Phase-aware MakeContext plus a session-held phase; the per-tick InputState is kept and recorded in the per-tick record list rather than emptied | One snippet covers picks, UI, RNG, IDs and user data, with no change to the documented InputState contract | FD-47 / GameSystems.hpp | GAMEPLAY, CORE |
| Child colliders per step | Visit every child-collider entry each step and compose its world live; ancestor-tick detection only if measured child counts demand it | O(child colliders × depth) instead of O(scene) | Physics.md | GAMEPLAY |
| Header tiers | Two physical roots (Public = module SDK; Host), three documented tiers; AssetLease split out | Stops rejected rebuilds; tools inherit RavenCore's PUBLIC root | FD-17, EngineModules.md | BUILD, RELOAD, CORE |
| Registry access | One private RegistryAccess friend | Stops new syncs editing a hashed header, at zero cost | ECSDesign | ECS |
| Thread placement | The AA2-PERF-03 table (background max(1, workers/4); process lane max(1, hw/8)) | Bounds background work to about 8 of 20 threads | JobSystemDesign, RenderingContentDesign threads | PERF, RELOAD |
| Shader compile path | Non-blocking two-phase process jobs first, then a process lane at max(1, hw/8); deps-only startup with input digests (compiler flags, importer version, every include); ProgramDesc seam and one engine table with B4; per-trigger publish groups that keep the H2 pair guarantee; game shaders as assets | Scales to N materials with no sleeping workers and no mixed-version vertex/fragment pairs; no-throwaway | RenderingContentDesign B4 | RELOAD, RENDER |
| Upload contract | Per-frame upload batch in VulkanContext texture creation, same queue, ending in a transfer-to-fragment barrier; development reload keeps an explicit timeline wait; placeholders and streaming get a new element, retired through the timeline, through one content-revision event (after B2's material map) | One mechanism for uploads; the stall is accepted only for development reload, and FD-22's no-stall promise holds | RendererDesign | RENDER |
| Bindless classes | White, an Engine reserve, Content; a full Content class returns the placeholder | UI and content cannot starve each other | RendererDesign | RENDER |
| B2 material change | Additive AssetRef; keep inline fields as defaults | FD-16 has no converter; Courtyard levels are authored | RenderingContentDesign contract changes | DOCS |
| Contract window | One restart at the start of B2 for all public layout changes, plus one earlier † commit for the hash-changing parts of the review amendments | Two game restarts instead of many | RenderingContentDesign | DOCS |
| Tint placement | Stays on StaticMeshComponent; RenderWorld compares the AssetRef | Moving it would drop authored values | RenderingContentDesign B4 | ECS |
| Default cull mode | NONE (double-sided) in every pipeline key unless a material opts into back-face culling | Single-sided assets look the same as today | RenderingContentDesign B4 | RENDER |
| Blended sprites | Stay CPU-sorted back-to-front from render-world data, drawn through a blend pipeline; only opaque and cut-out sprites take the indirect path | The indirect path has no blend pipeline and draws in group order | RenderingContentDesign B5 | RENDER, MEDIA |
| Audio slot ownership | Each mixer slot owns its clip until the matching release; dropped Stops are retried per slot | Lifetime follows the audio thread, not the Sound record; streaming builds on it | GameServicesDesign C8 | MEDIA |
| Presented camera | The camera view and listener follow the drawn (blended) position; picking stays on the latest tick | A camera parented to an interpolated entity does not step against it | FD-34 | RENDER, GAMEPLAY |
| Mover semantics | Snapshot collision at the crowd milestone, BodyFilter now; escalate only if feel visibly changes | Symmetric, parallel-ready, no Jolt edit | FD-47 | GAMEPLAY, PERF |
| Jolt settings | ISA parity at SSE2 (pending Q1); CROSS_PLATFORM_DETERMINISTIC OFF explicit | Visible, settings-only, no Windows change | cmake/RavenJolt.cmake, Physics.md | BUILD |
| Module contract | Hidden visibility, two exports, no thread_local, no statics with non-trivial destructors; image-ownership guard on `&info` in SystemTable::AddComponent through a platform seam | Precondition for correct migration with GCC on ELF; unload-safe on macOS | EngineModules.md | RELOAD |
| Format policy | Accept N and N-1; upgrade path per bump; strict or tolerant stated per format; pinned enums | One rule for every Raven format | FD-10 | ECS |
| Content trust | Trusted, bounded, not sandboxed in v1 | Matches current use; mods need their own decision | AssetSystem.md | MEDIA |
| Localization API | UiText {Key, Args} next to plain-value overloads; key returned until tables exist | Call sites final now | UIDesign | MEDIA |
| UI navigation | Engine "ui" router context, opt-in focus | Rebindable; gamepad-ready; no raw keys | UIDesign section 5, FD-29 | MEDIA, GAMEPLAY |
| Scheduling rule | Unknown access means exclusive on the main thread; Services stay main-thread | Existing systems stay valid; no premature queues | ECSDesign D9 | ECS, CORE |
| Window title | The game name from the project file | Obvious default | PlayerBuild.md | CORE |
| User-data folder name | Keep "RavenEngine/<Game>" until a studio name exists | No migration churn | UserData.md | GAMEPLAY |
| Layer-name syntax | Named layers in the project file | Designers see names, not numbers | Physics.md | GAMEPLAY |
| DeterminismCheck in CI | Headless host after the ownership moves, not lavapipe and not a duplicate CPU test | Tests the simulation itself | FD-48 | CORE, BUILD |

## 10. Disputed or unverified items, and what was out of scope

### Unverified (still open after the verification round)

- **AA2-MEDIA-01** (crash versus garbage): the ~508 KB VirtualAlloc threshold comes from platform knowledge. The mechanism itself is now verifier-confirmed in code. Any loop longer than about 1.3 s at 384 KB/s exceeds the threshold.
- **AA2-HR-02**: GNU_UNIQUE binding of template statics is known GCC behavior, but it has not been reproduced in this repo. Clang does not emit STB_GNU_UNIQUE. dyld coalescing on macOS is a hypothesis.
- **AA2-PLAT-05**: AppleClang libc++ support for floating-point from_chars/to_chars and jthread is a hypothesis.
- **AA2-PLAT-01**: "no upstream edit needed for a Linux compile" is a hypothesis until the first GCC run, especially for ICU. The host compile error at RavenEngine.cpp:169 is confirmed by reading, not by compiling.
- **AA2-PLAT-03 step 5**: the MSVC CRT FMA3 runtime dispatch was not checked in this repo.
- **AA2-PLAT-04**: no current trigger. Interposition depends on future SDL3, Wayland or toolkit loading.
- **AA2-PERF-01, PERF-02, GP-02, GP-07, RND-02**: all costs are derived from recorded floors, not measured. AA2-PERF-04 and PERF-06 produce the measurements. PERF-01's high rating depends on its step 1 measurement.
- **AA2-PERF-07**: the change-locality explanation for the 0.27 ms is a hypothesis to be confirmed by the A/B.
- **AA2-PERF-09**: a static inventory. No allocation counter was run.
- **AA2-HR-04**: the acceptance numbers assume 16 GB of laptop RAM, which is not recorded in the repo.
- **AA2-MEDIA-02**: whether stb_vorbis and miniaudio can report stream length was not verified.
- **AA2-GP-07**: the feel of snapshot pushing (about 0.2 m overlaps) is an estimate that needs an Arena probe.
- **AA2-GP-02**: whether a shipped scene has a collider parented under a root moved in FixedUpdate. The binary Arena.ravenscene was not inspected without running tools.
- **AA2-RND-03**: the cost of the per-draw ownership scan is unmeasured. B1 measures it as part of today's path.
- ARCH-ASSET-06 and ARCH-BLD-05 were re-verified in the citation pass (both open; see section 4).

### Resolved disputes (summary; details are in each finding)

- Interpolation mechanism: (a), the CPU override. After verification, Restore is deleted at B5, not B4, and the camera and listener read the override.
- CORE-5's boundary change: withdrawn. FD-06 is kept. After verification, the new Render stage catches DataError only.
- Header split: two roots and three tiers, with AssetLease split out. Lead-checked at Scene.hpp:146.
- Severities in the panel round:
  - GP-01 was kept high (PERF-3 merged into it). Verification lowered it to medium, because Arena's platform never triggers it.
  - ECS-01 lowered to medium.
  - PERF-2 lowered to medium.
  - GSV-02 lowered to medium.
  - CORE-3 lowered to medium (GSV-09 merged).
  - XP-7 lowered to low.
- Stream model: (a), presentation streams.
- Per-step world pass: replaced by a live compose of child colliders, visiting every child-collider entry each step.
- Throttle values: the process lane is hw/8.
- DeterminismCheck in CI: the headless host.
- AUI-1 test: lands with the fix.
- Contract batching: one window at B2, plus one earlier hash-changing commit for the † parts of the review amendments.

### Verification pass results

There were 14 adversarial verifiers, one per finding the panel rated high, and a citation checker that opened 172 citations (22 citation problems, 9 follow-up-table problems, all applied). No finding was refuted, so none was removed from the tables.

| ID | Verdict | Severity (panel -> final) | Main correction |
|---|---|---|---|
| AA2-RT-01 | confirmed with corrections | high -> high | Added the live dump/apply lighting trigger (RavenSceneTool.cpp:35) and the MVP-overflow throw. The Render stage now catches DataError only, so VulkanDeviceLostError and post-acquire failures stay fatal. The SaveAll guard is declared after userData. The 172-site grep is stated. |
| AA2-CORE-01 | confirmed with corrections | high -> high | StartSimulation is at :319-324, and the root cause is the InputRouter constructor (:66-69). The carried record became its own later row, created after the StartSimulation reseed. Only the Samples flow is ported; Arena's retry stays. Courtyard hints added. |
| AA2-HR-01 | confirmed with corrections | high -> medium | Startup compile is develop-only, deliberate, and never stalls frames. The doc-drift claim was dropped (RenderingContentDesign.md:288 does not exist). The non-blocking wait was split into small steps ahead of the lane. ProgramDesc and the table moved to B4. H2 grouping is kept. |
| AA2-HR-02 | confirmed with corrections | high -> medium | The mechanism is GCC-specific. Size changes are rejected loudly; only equal-size edits corrupt. The guard moved to SystemTable::AddComponent, testing `&info`, behind a platform seam. Off Windows it depends on removing the RavenGame.cmake:4 gate. |
| AA2-RND-01 | confirmed with corrections | high -> medium | FD-19 already decides the record; what is missing is fragment-side plumbing. The checker is subset-tolerant. The reserved shadow slot was dropped. VulkanFrameData citations were fixed, and .cpp:14 was added to step 3. |
| AA2-RND-02 | confirmed with corrections | high -> medium | "Undesigned" became "under-specified" (B4 already plans the grouping). Per-frame cost is small. Templates keep a per-frame GPU reset, and a mesh revision counter is needed. Skinning is constraint-only unless Q3. The FBX measurement moved to B7/B8. |
| AA2-RND-03 | confirmed with corrections | high -> medium | Kind is now process (a flawed draft). Blended sprites stay CPU-sorted. The pre-B1 scan removal was dropped as throwaway. UV and size depend on Q8. The conflicting line is :105, not :102. The Overlay.hpp:11-13 and UIDesign corrections were added. |
| AA2-RND-04 | confirmed with corrections | high -> medium | Today's code is correct, and the risk was already ARCH-RND-04 at medium. Only the submission site is rework. Development reload keeps a timeline wait; the new-element path is for placeholders and streaming, after RND-05's map. The reorder is owned by RND-02. |
| AA2-GP-01 | overstated | high -> medium | Arena's platform changes every tick and never stops, so there is no live trigger. Added the two concrete consequences and the write-back guard (PhysicsWorld.cpp:574). The test justification was tied to PERF-02 step 2. |
| AA2-GP-02 | overstated | high -> medium | Fixed steps get the documented per-tick InputState, not raw input. Step 1 keeps it, and step 3 records it. Pick duplication across catch-up steps is the main evidence. No shipped trigger. Step 4 names its visit strategy. |
| AA2-MEDIA-01 | confirmed with corrections | high -> high | Added the steal and quick-load paths, the about 120 ms device-reopen window, and the loop-only scope. The dropped-Stop retry was moved per slot, because a per-Sound flag breaks virtualization. The test is deterministic. |
| AA2-PERF-01 | confirmed with corrections | high -> high | Justified by the 3.24-3.42 ms / 3.5 ms budget (DataModelDesign.md:661). The Render-throw bullet is latent. Restore stays until B5 for sprites. The camera and listener use the override, and override cleanup was added. |
| AA2-PLAT-01 | confirmed with corrections | high -> high | Added the host compile error (RavenEngine.cpp:169) and the MSVC environment hard fail. The CI target includes RavenEngine and RavenSceneTool. SupportsBuildProcesses moved to PLAT-02. The exit split and the M4 contract became owner question Q13. |
| AA2-DOC-01 | confirmed with corrections | high -> medium | "22k added lines". ROADMAP.md:40 was added. There is one canonical amendments list. The "one-file" and "no contract hash" claims were corrected, and ECS-02 steps 1-2 moved to Phase 3. TODO.md:137 was added. |

Refuted findings: none.

### Dropped from round 1

- RND-8's "ROADMAP says six owner decisions": ROADMAP.md:194 says five.
- RND-8's attribution of the 0.27 ms to the Finds.
- PERF-2's original step 3 (inner-body commit), which needs a Jolt edit.
- XP-8 step 3 (lavapipe).
- The "fails silently" premise of the stale-commands question, which is true only for renames.
- The DOCS round-1 "partly fixed" rows for HR-01, 02 and 03.

### Out of scope or not covered

- GPU device-lost and out-of-memory handling. AA2-RND-08 covers only the budget query.
- Window and display behavior: DPI changes, hot-plug, fullscreen, HDR.
- Licensing wording beyond staging Licenses/: FTL attribution, OFL subsetting rules, the ICU data filter.
- Editor undo, inspector and gizmos beyond identity and capture.
- The M7 packaging and cooking pipeline.
- macOS and Linux runtime behavior: nothing was compiled or run off Windows.
- Performance on any machine other than the minimum-spec laptop.
- The panel did not review the content of the existing M6B owner decisions (FD-18, FD-19, FD-23, FD-26), only their fit with the code.

