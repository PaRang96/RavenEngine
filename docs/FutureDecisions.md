# RavenEngine future milestones and decisions (2026-11-23)

Status: **proposal for owner review. Nothing here is decided.** It was written from `ROADMAP.md`, `TODO.md`, the design notes and a read-only study of the code at `8f16a41`. No engine file, `ROADMAP.md` or `TODO.md` was changed.

## 1. What this document is

`ROADMAP.md` says what v1 must do. [ArchitectureAssessment.md](ArchitectureAssessment.md) says what is wrong today and lists 16 owner questions (Q1–Q16) with short recommendations. This document looks forward:

- **Section 2** proposes how the remaining milestones could be shaped.
- **Section 3** is a calendar: which decision is needed before which step.
- **Sections 4–10** predict each technical decision the remaining milestones will force. Each entry gives the recommendation first, then every option with its strength and its cost, and what the choice does to the four v1 goals.
- **Section 11** lists post-v1 directions, and what today's code must keep open for them.

Decisions use the IDs `FD-01` to `FD-45`. Where the assessment already asks the question, the entry names it (for example "Q1 / D-A"); the assessment stays the source for file:line evidence, and this document adds the option trade-offs and the decisions the assessment does not cover.

How to read the labels:

| Label | Meaning |
| --- | --- |
| **Checked** | I read the code or document that the statement rests on. |
| **Predicted** | Nothing forces the decision yet. I expect a named trigger to force it. |
| **Unverified** | A claim about a tool or platform that was not run here. Check it before relying on it. |

## 2. Proposed milestone map

Remaining v1 work today: one M0 item (warning level), M4 (platforms), M6 (gameplay services) and M7 (quality and release). M6 is by far the largest: the assessment orders it into ten dependent steps plus three parallel tracks.

| Stage | Content | Proposed exit | Needs decided first |
| --- | --- | --- | --- |
| **P0 — Hardening** (now) | The assessment's "Phase 0": the host stops ending the session on game-written data, the M3 numbers are re-run on the unpatched host, the warning level lands, small renderer and reload hygiene. | A zero scale, a bad texture ID or a destroyed parent written by game code no longer exits. M3 exit numbers reproduced from HEAD. `/W4` on all targets. | FD-01 to FD-05 |
| **M6 design note** | One note with the data-model decisions, as for M1–M5. | Owner decisions recorded. | FD-06 to FD-17 |
| **M6A — Host and data model** | Assessment steps M6-1 to M6-8: engine frame pipeline, field table, hierarchy, persistent identity, generic scene format, the one module-contract break, Commands, field migration. | A game defines a component the host never compiles, authors it in a scene file, and edits a field while running with no restart. Save/load round trip runs in CI. Spawn and despawn of 1k entities per frame at 100k stays in budget. | The M6 note |
| **M6B — Rendering and content** | Assessment steps M6-9 and M6-10: draw grouping by pipeline, material identity, sprites on the GPU path, per-material shaders, upload batching, multi-object FBX, asset resolution. | Sprites and one custom material shader run on the GPU-driven path. A multi-object FBX imports with stable IDs. The owner-requested 2D run passes. | FD-18 to FD-26 |
| **M6C — Game services and the v1 sample game** | Key bindings, audio voices, user-data path, text, camera helpers, and the named small 3D game. Runs in parallel with M6A/M6B once the note is approved. | Today's M6 exit: a small 3D game, the stress sample and the 2D sample run using only engine APIs. | FD-29 to FD-34 |
| **M4 — Platform parity** (parallel) | Unchanged task list. | Unchanged, but **evaluated after M6A**: M6A breaks the module contract and replaces the scene format, so a parity pass before it would be repeated. | FD-36 to FD-41 |
| **M7 — Quality and release** | Unchanged, plus cooking and in-place player loading. | Unchanged. | FD-35, FD-42 to FD-45 |

Why split M6: the project's review flow is one joint review per milestone. One review covering ten dependent steps and three parallel tracks would be the largest so far, and a problem found late in the field table (step 2) would reach back through six later steps. Three stages give three measurable exits. The step order inside them is the assessment's. This is decision FD-01.

## 3. Decision calendar

| When | Decisions |
| --- | --- |
| **Now**, before any M6 code | FD-01 M6 shape · FD-02 hardening timing · FD-03 frame pacing · FD-04 Linux compile job · FD-05 warning policy · FD-30 audio architecture (only because M4 contributors are about to write audio backends) |
| **In the M6 design note** | FD-06 error rule · FD-07 hierarchy · FD-08 field table · FD-09 entity identity · FD-10 scene format · FD-11 unknown data · FD-12 system context · FD-13 worlds · FD-14 Commands · FD-15 prefabs · FD-16 field renames · FD-17 game project layout and contract |
| **Before M6B starts** | FD-18 material model · FD-19 shader interface · FD-20 transparency and sprites · FD-21 textures · FD-22 asset loading · FD-23 multi-object FBX |
| **Before M6C starts** | FD-29 input bindings · FD-31 text · FD-32 collision scope · FD-33 save data · FD-34 smaller service decisions |
| **When a measurement or trigger arrives** | FD-24 GPU memory · FD-25 offscreen targets · FD-26 lighting scope · FD-27 full-dynamic scaling · FD-28 threading · FD-35 derived-data cache |
| **With the M4 contributors** | FD-36 module unload · FD-37 MoltenVK · FD-38 Linux display · FD-39 performance off Windows · FD-40 GPU tests in CI · FD-41 toolchain floors |
| **Before M7** | FD-42 package format · FD-43 game module in players · FD-44 sanitizers and crash data · FD-45 compatibility promise |

The data-model decisions FD-07 to FD-17 depend on each other. Under the no-throwaway rule their order matters more than any one answer: the field table (FD-08) feeds serialization, migration, prefabs and saves; identity (FD-09) feeds the scene format, prefabs, play-session cloning and multi-object FBX.

---

## 4. Decide now (before M6 code)

### FD-01 — Shape of M6

**Recommend (c):** three stages, M6A/M6B/M6C, using the assessment's step order inside them.

| Option | Strength | Cost |
| --- | --- | --- |
| (a) Keep today's flat M6 list | No replanning. | The dependency order stays hidden, and the exit ("a small 3D game") cannot be checked step by step. |
| (b) One M6 with ordered steps M6-1 to M6-10 | The order is explicit; one design note. | One joint review of the whole data model, renderer and services. Platform contributors get no stable point to port against until the very end. |
| (c) Three stages with their own exits | Three smaller reviews. M6A's exit is the stable contract M4 ports against. M6C can run in parallel. | Two more reviews; M6B cannot start its material work before M6A's scene format exists. |

Effect on goals: none directly; it lowers the risk of rework in goals 2 and 3.

### FD-02 — When the hardening fixes land (assessment Phase 0)

**Recommend (a):** now, as a short gate of its own.

| Option | Strength | Cost |
| --- | --- | --- |
| (a) Now, before M6 | Every fix is small and none is replaced later. "Last-good recovery" becomes true for game-written data before M6 makes such writes routine. | Delays M6 by a set of small snippets. |
| (b) Fold each fix into the M6 step that touches the same file | No separate gate. | The fixes get mixed into larger moves and are harder to review; the session-ending paths stay open while M6A is built on them. |
| (c) Fix only what is triggered | Least work now. | Nothing triggers them today (**checked** in the assessment), so nothing would be fixed until a game hits one and loses its session. |

Effect on goals: goal 3 (recovery).

### FD-03 — Default frame pacing and present mode (Q11)

**Checked:** the swapchain always uses FIFO (`VulkanSwapchain.cpp:158`) and the loop sleeps to 120 FPS (`Application.cpp:156`). The recorded M3 FPS numbers came from a patched host.

**Recommend (a):** FIFO by default with no extra limiter, an optional `MaxFrameRate`, and the 30 FPS idle throttle kept.

| Option | Strength | Cost |
| --- | --- | --- |
| (a) FIFO, no limiter by default, optional cap | The display paces the frame, so the stock host reaches the panel's refresh rate and the M3 numbers can be reproduced without patches. Lowest power use of the uncapped options. | A changed default for every user. Mailbox or Immediate run uncapped unless a cap is set; that must be documented. |
| (b) Keep the 120 FPS cap as the default, add options | No behavior change. | The stock re-run measures a capped host, so goal-4 evidence still needs a flag. A sleep-based cap has coarse timing on Windows (**unverified**: granularity not measured). |
| (c) Uncapped Mailbox as the default | Lowest input latency. | Highest GPU load and heat on the laptop that is the minimum spec; frame times depend on thermal state, which makes benchmarks noisier. |

Effect on goals: goal 4 evidence.

### FD-04 — Linux compile job in CI (Q10)

**Recommend:** add an informational compile-only Linux job now; never a required check for Windows milestones. Whether the Windows side also lands Linux `FillRandomBytes` with it (so tests link) is the owner's call.

| Option | Strength | Cost |
| --- | --- | --- |
| (a) Job now, with Linux `FillRandomBytes` | Full build plus CPU tests from the first week; shared-code breaks are seen when they are made. | Platform code written from Windows, checked only by the job. |
| (b) Job now, core library only | No platform code written from Windows. | Samples, tools and tests are skipped until the M4 contributor lands the hook. |
| (c) Wait for M4 | No CI cost now. | Linux and macOS code has not compiled since October; drift keeps growing, and M6A will touch nearly every shared file. |

Effect on goals: goal 1.

### FD-05 — Warning policy (the last M0 item)

**Checked:** the game DLL already builds with `/W4 /WX` (`Private/Game/CMakeLists.txt`); the engine targets have no set level.

**Recommend (a).**

| Option | Strength | Cost |
| --- | --- | --- |
| (a) `/W4` and `-Wall -Wextra` everywhere; warnings-as-errors only in CI | Local builds never break on a new compiler's new warning; CI still keeps the tree clean. | A warning can sit locally until CI runs. |
| (b) Warnings-as-errors everywhere | One rule, same as the game DLL. | A toolchain update on any contributor's machine can stop their build; painful on three compilers. |
| (c) `/W4` with no error gate | No friction. | Warnings pile up again; the item has already stayed open through M1–M5. |

It must land before M6A's field-table step, which edits `Component.hpp`, the header every component includes.

---

## 5. M6A — host and data model (decide in the M6 design note)

### FD-06 — What a host stage does with bad data

**Recommend (b)** as the written engine rule, applied to every later stage (asset resolution, Commands, migration).

| Option | Strength | Cost |
| --- | --- | --- |
| (a) Throw, and catch at one boundary | Smallest code at each site. | A throw half-way through a stage leaves derived state half-written, so the boundary must rebuild everything; it also hides which object was wrong. |
| (b) Tiered: data errors give a placeholder or a hidden object plus one log line; programmer errors throw; out-of-memory and broken invariants end the process | The session survives a gameplay mistake, and the mistake stays visible. Engine bugs are not masked. | Each stage needs a defined placeholder (white texture, hidden object, detached root). |
| (c) No exceptions in engine code (result codes everywhere) | Predictable control flow; no unwinding across the module boundary. | A rewrite of authoring and IO code that works today, with no v1 goal behind it. |

Sub-decision: the non-throwing normal-matrix helper can be public (reusable, but a client-ABI addition) or private to the render world. **Recommend private**; make it public when a second caller exists.

### FD-07 — How the hierarchy is stored (Q1 / D-A)

**Checked:** the hierarchy is a private `unordered_map` in `Scene`; any create, destroy or reparent rebuilds the whole world order. No runtime path triggers this yet.

**Recommend (c) with host-only writes:** `ParentComponent` is the saved truth; child lists and depth order are a `Scene` runtime index; only `Scene::SetParent` writes the component.

| Option | Strength | Cost |
| --- | --- | --- |
| (a) Keep `Scene` side tables, add a child index | Smallest change. | The hierarchy is invisible to generic serialization, cloning and the field table, so each needs a special path. |
| (b) Parent, FirstChild and NextSibling all as components | Everything is ECS data. | Sibling links are derived data: every reparent writes 3–5 entities, the links must be repaired on each erase and clone, and redundant data is saved. |
| (c) `ParentComponent` plus a runtime index | The saved data is minimal; serialization and cloning carry the hierarchy for free; the index code is the same wherever Parent lives. | A rule is needed for who may write Parent (below). |

Write policy for (c):

| Option | Strength | Cost |
| --- | --- | --- |
| (i) Host-only writes through `SetParent` | No per-frame reconciliation; cycles are checked at one site. | A future editor must call `SetParent` instead of a generic field write. |
| (ii) Any writer; `Scene` reconciles from change ticks each frame | Generic editor writes just work. | A dense tick scan every frame, plus cycle checks at apply time. |

Measure first: add the spawn/despawn/reparent benchmark case before choosing the implementation priority.

### FD-08 — Where the field table comes from (Q3 / D-C)

The field table is a runtime list of each component's field names, offsets and types. Serialization, migration, prefabs, saves and a future editor all read it.

**Recommend (a):** generated by `RAVEN_COMPONENT`, registered through the existing module table, copied by value. Design the `FieldInfo` record so a later generator could fill the same table.

| Option | Strength | Cost |
| --- | --- | --- |
| (a) The existing macro emits a constexpr table | No new tool and no change to component declarations. It cannot drift from the struct, because the macro already checks every field at compile time. | Macros see only names and types. Per-field metadata (ranges, display names, "do not save") needs an extra macro form later. |
| (b) Hand-written registration functions | Any metadata can be attached. | A second description of each struct that can drift from it; more work per component. |
| (c) An external code generator that parses headers | Rich metadata and attributes; this is how large engines do it. | A parser to maintain on three platforms, and a generation step inside the 1.5 s reload loop. |
| (d) Wait for C++26 static reflection | The cleanest long-term source. | Not usable on the project's compilers today (**unverified**: no schedule checked), so M6 cannot depend on it. |

Effect on goals: goals 2 and 3. Default values must be captured at runtime from a default-constructed object, not by a compile-time byte cast (the assessment explains why).

### FD-09 — Persistent entity identity (Q4 / D-D)

**Checked:** entities are found by authored names through a `std::map<std::string, Entity>`; components hold 129-byte name arrays as references.

**Recommend (a), generated randomly instead of counted:** a scene-scoped 64-bit ID in a `Scene`-owned table, with names kept as optional labels.

| Option | Strength | Cost |
| --- | --- | --- |
| (a) 64-bit ID in a `Scene` table | 8 bytes, no extra pool, fast lookup. Enough for references inside one scene. | No cross-scene references by itself. |
| (b) UUID component on every entity | Globally unique, so cross-scene references and prefab instances need nothing more. | 16 bytes and a pool entry per entity (100k entities in the stress scenes); game code can see and overwrite it. |
| (c) Keep authored names as the identity | Readable in files; no new concept. | A rename breaks every reference; string compares at runtime; generated entities need invented names. |

Why random IDs: two scenes or two contributors never produce the same ID, so a later cross-scene reference is just the pair (scene asset ID, entity ID) with no format change. That absorbs most of (b)'s strength at (a)'s cost.

### FD-10 — Scene file format and conversion (Q8 / D-H, extended)

**Checked:** 3D scenes are positional binary (`.ravenscene`); 2D levels are text (`.ravenlevel`); v1 has no editor, so files and `RavenSceneTool` are the authoring interface.

**Recommend (a) plus a text dump/apply command:** one chunked, self-describing binary format driven by the field table; `RavenSceneTool` gains a lossless text dump and apply written once against the same table. Convert existing files once, then delete the old readers.

| Option | Strength | Cost |
| --- | --- | --- |
| (a) Chunked binary only | Fast and compact (the 100k-mesh benchmark scenes); keeps today's 3D workflow and atomic saves. | Opaque to `git diff` and merges; every edit needs the tool or code. |
| (b) Text only | Diffable, mergeable, editable by hand, which matters while there is no editor. | Slow and large at 100k entities; float round-tripping needs care. |
| (c) Text as the source, binary as a cooked form | Both strengths. | Two formats to keep in step, and a cook step in the reload path. |
| Recommended: (a) + dump/apply | Reviews and hand edits go through text when wanted, without a second runtime format. | Text is not what is committed, so merge conflicts in scenes still need the tool. |

Revisit making text the committed form when a second person edits scenes, or when the editor arrives. For old files: a one-time converter then deletion (recommended) keeps one reader; a legacy runtime decoder avoids conversion but keeps sample-specific code in the engine forever.

### FD-11 — Data whose component type is not registered

This happens when a game DLL fails to load or a component was removed.

**Recommend (a).**

| Option | Strength | Cost |
| --- | --- | --- |
| (a) Keep the section as opaque bytes and write it back unchanged; log it | A broken DLL never destroys authored data; matches last-good recovery. | Stale data can linger until someone removes it on purpose. |
| (b) Reject the file | No stale data. | A scene cannot load while the DLL is broken, which is exactly when hot reload must keep working. |
| (c) Drop it silently on save | Files stay clean. | Silent data loss. |

### FD-12 — What a game system receives (Q2 / D-B)

**Checked:** `SystemContext` carries the raw `GameInstance`, `GameBindings{Camera, Player, Mesh, World, Interactions}` and `GameUpdateResult{PlayerDisplacement}`. These sample concepts are in every game DLL's ABI. The host stops gameplay today by returning empty bindings.

**Recommend (a), with play state owned by the engine session and checked by the schedule:** the context becomes scene access, input, time and a generic `Picks` span; Player and similar become game-defined tag components; the game phases run only while the session is playing.

| Option | Strength | Cost |
| --- | --- | --- |
| (a) Tags and singleton components, engine concepts only | The host reads only engine-defined components. New gameplay needs no public-header edit and no restart. | One planned contract break; Courtyard's movement moves into a game system and needs a 2D run. |
| (b) Generic named entity slots and an opaque result | A smaller break now. | A second identity system that serialization, cloning and the editor must all carry; authored tags would replace it later. |

How gameplay is switched off (replaces the empty-bindings trick):

| Option | Strength | Cost |
| --- | --- | --- |
| Play state on the session; the host skips game phases unless playing | Matches today's behavior, and is what an editor's edit mode needs. | All-or-nothing: no "this system also runs while paused". |
| A mode component that each system checks | No schedule change. | Every system must remember to check it. |
| Per-system run conditions in `SystemDesc` | Fine-grained. | More schedule API than any current game needs; add it when the first system needs it. |

### FD-13 — Worlds and registries (Q7 / D-G)

**Recommend (a).**

| Option | Strength | Cost |
| --- | --- | --- |
| (a) One Registry per world; scene IDs separate scenes inside a world | Isolation by construction. A play session is a per-pool copy. Ticks and journals are per world. | Entities cannot be viewed across worlds in one query (rarely wanted). |
| (b) One shared Registry with scene IDs | One place for everything. | Every view, journal and tick consumer must filter by world; a play-session bug can touch editor data. |

### FD-14 — Deferred structural changes (Commands, ECS decision D5)

**Predicted trigger:** the first runtime spawn or despawn in the sample game.

**Recommend (a):** flush at the end of each phase, with entity handles reserved at record time, and destroy routed through `Scene`.

| Option | Strength | Cost |
| --- | --- | --- |
| (a) Flush at the end of each phase | Few, fixed sync points; deterministic; leaves room for parallel systems inside a phase. | A system cannot see what an earlier system in the same phase spawned (the handle is valid, the components arrive at the flush). |
| (b) Flush after every system | Later systems see earlier results. | Many flushes, each touching the hierarchy index; rules out parallel systems inside a phase. |
| (c) Allow direct structural calls whenever no view is open | Simplest for game code. | Behavior depends on where the call is made; easy to break by wrapping code in a view; rules out parallel systems. |

### FD-15 — Prefabs (spawn from data)

**Checked:** `SceneWorkflow.md` lists prefabs as later work. **Predicted trigger:** Commands answer "how to spawn"; the next question is "spawn what".

**Recommend (b)**, built when the first spawn-from-data need appears.

| Option | Strength | Cost |
| --- | --- | --- |
| (a) Spawn from code only | Nothing to build. | Every spawned object is described in C++, so its values are outside asset hot reload. |
| (b) Instantiate a scene file as a template: copy plus ID remap, no overrides | Reuses the generic format and the same copy-and-remap code that play sessions and multi-object FBX need. | Editing one instance does not flow back; no nesting rules. |
| (c) Nested prefabs with per-instance overrides | What a full editor workflow wants. | Override storage, merge rules and a large test surface, with no editor in v1 to use them. |

### FD-16 — Renamed fields and components

**Checked:** the field-migration design copies by matching name and type, and rejects a type change. A rename is not covered. Once scenes are keyed by field name, a rename also orphans the value in every scene file.

**Recommend (b).**

| Option | Strength | Cost |
| --- | --- | --- |
| (a) A rename is a removal plus an addition | Nothing to build. | The value is lost in the live world and in every file. |
| (b) An alias list declared next to the component, read by both the serializer and migration | One small mechanism covers live reload and files. | Aliases accumulate until someone removes them. |
| (c) A one-off conversion tool per rename | No runtime mechanism. | Does not help the live session; a tool run per rename. |

### FD-17 — Game project layout, and what still forces a restart (Q6 / D-F)

**Checked:** the game module is a Windows-only child CMake project under `Private/Game`; the samples are compiled into the host executable; the contract hashes every public header, every game header and the CMake files, so any of those edits forces a host restart. The M6 exit needs three clients (a 3D game, the stress sample, the 2D sample) "using only engine APIs".

**Recommend (b) for layout and (ii) for the contract.**

Layout:

| Option | Strength | Cost |
| --- | --- | --- |
| (a) One in-tree game under `Private/Game` | No restructuring. | Three clients share one module and one content root; "only engine APIs" is hard to prove while they live inside the engine's private tree. |
| (b) Game roots in the repository, outside the engine tree (each with its module, content and project file), built against the engine by source path | Proves the public API is enough; gives M7's "clone, build, modify and package a game" a real shape. | The game build, the contract and the watch lists are restructured; the host needs a game selection. |
| (c) The engine as an installed SDK package | The cleanest separation. | Install and versioning work with no v1 user for it. |

Contract after field migration exists:

| Option | Strength | Cost |
| --- | --- | --- |
| (i) Keep hashing the whole tree | Simple and safe. | Every header or CMake edit restarts; field migration would never be reached. |
| (ii) Hash only the engine's public surface; game components are checked by the field table | Game component edits reload; an engine ABI change still restarts. | The public surface must be listed precisely. |
| (iii) Check component layouts only | Fewest restarts. | A change to a non-component engine type (for example `InputState`) goes unnoticed and corrupts memory. |

---

## 6. M6B — rendering and content

### FD-18 — Material model (Q9 / D-I, extended)

**Checked:** a material is three inline values in `MaterialComponent` (base color, base-color texture, lit flag); the render world keeps an append-only table compared by exact value and holds no texture lease.

**Recommend (b):** a material asset referenced by ID, with `Tint` as the only per-object override in v1.

| Option | Strength | Cost |
| --- | --- | --- |
| (a) Inline values per entity (today) | No new asset type. | Every distinct value is a new table entry; a shader reference would be repeated on every entity; no shared edit. |
| (b) A material asset (parameters, textures, shader) referenced by `AssetRef` | One edit reloads everywhere. The asset ID is the natural table key and owns the texture leases. | A new file type and importer, authored as text while there is no editor. |
| (c) Asset plus an inline override component | Most flexible. | The key becomes (asset, overrides); animated overrides grow the table again. |

### FD-19 — What a custom material shader may change

**Checked:** lit fragment shaders are written against `Raven/Material.hlsli` (fixed inputs, one bindless texture array, one sampler). The owner already decided per-material shaders go in M6 on the same command layout.

**Recommend:** fragment-only shaders, one fixed material record with a few generic slots (for example four `float4` values and four texture indices), compiled at build time so players ship no shader compiler.

| Option | Strength | Cost |
| --- | --- | --- |
| Fragment only (recommended) | Vertex layout, culling and the interface checks stay single; one pipeline per shader. | No vertex animation (wind, wobble) in materials. |
| Vertex and fragment | Vertex effects. | The GPU-culled path assumes one vertex transform; each vertex shader must be checked against the object buffer layout. |
| Fixed parameter record with generic slots (recommended) | One material buffer with one stride for every shader. | Slots are unnamed in the file unless the material asset maps names to slots. |
| Per-shader reflected parameter block | Named, typed parameters of any size. | Variable stride breaks the single material table; reflection and validation per shader. |
| Compile shaders at runtime in players | Mods could add shaders. | Ships DXC; a licensing and size question with no v1 need. |

### FD-20 — Sprites and transparent objects on the GPU path

**Checked:** transparent draws are sorted on the CPU by layer, order, camera depth and submission order. GPU-culled indirect draws have no order inside a group.

**Recommend:** sprites enter the render world and the bindless texture array. Cut-out sprites (alpha test) are GPU-culled like meshes. Blended objects keep a CPU-sorted path fed from render-world data. Add a GPU sort or order-independent blending only if a measured blended count requires it.

| Option | Strength | Cost |
| --- | --- | --- |
| (a) Alpha-tested sprites in the same object buffer | Fully GPU-driven; no sort; layers map to depth. | Hard edges only (no soft alpha). |
| (b) CPU sort of the visible blended objects | Exact order, as today. | CPU cost grows with the blended count; these objects are culled on the CPU. |
| (c) GPU sort of visible keys in a compute pass | Exact order at scale. | A new compute pass and an ordered instance list; real complexity. |
| (d) Order-independent blending | No sort at all. | Approximate colors where layers overlap; extra render targets (see FD-25). |

Needs the owner-requested 2D run first: Courtyard has not been run through four renderer rewrites.

### FD-21 — Textures: mip maps, compression and samplers

**Checked:** every texture is RGBA8 with one mip level; the bindless path has one clamp sampler without mip filtering.

**Recommend (b) with the material work, then (c) at M7 for players only.**

| Option | Strength | Cost |
| --- | --- | --- |
| (a) Stay as today | Nothing to build. | Distant textures shimmer, which shows most in exactly the 50k-object scenes; 4 bytes per texel of video memory. |
| (b) Generate mips on the GPU at load; a small fixed sampler set (clamp/repeat × linear/mip) | Small code; no cache; fixes the shimmer; reload stays instant. | One third more video memory; a little load time per texture. |
| (c) Cook to a block-compressed format with mips | 4–6× less video memory; fast loads. | An encoder dependency; encoding is slow, so development sessions should stay on (b); this is the first real need for the disk cache (FD-35). Desktop GPUs support BC formats; Apple Silicon through MoltenVK is **unverified**. |
| (d) A universal container transcoded per platform | One file for every GPU family. | Another library and a transcode step, for platforms v1 does not target. |

### FD-22 — How assets are resolved and released

**Checked:** an asset loads synchronously on first use, sometimes inside the render-world sync; a scene's leases only grow.

**Recommend:** preload at scene load, asynchronous for references written at runtime, leases held for the scene's lifetime in v1.

| Option | Strength | Cost |
| --- | --- | --- |
| (a) Synchronous on first use (today) | Simple; nothing is ever "not ready". | A decode and upload in the middle of a frame the first time a game writes a new asset ID. |
| (b) Asynchronous with placeholders | No hitch. | Every consumer must handle "not ready": a white texture is easy, a mesh without bounds affects culling and picking. |
| (c) Preload everything a scene references, found through the field table | Deterministic; no popping; no placeholder logic for authored data. | Does not cover IDs written at runtime; longer scene switches. |
| Recommended: (c) + (b) | Authored data never pops; runtime writes never hitch. | Both mechanisms exist. |

Releasing leases while a scene lives saves memory but is only safe after material identity owns its texture leases (FD-18). Keep scene-lifetime leases until a measurement asks for more.

### FD-23 — Multi-object FBX (Q5 / D-E)

**Checked:** the importer rejects files with more than one mesh node; one asset ID maps to one file.

**Recommend (a):** generated IDs stored in the project file; each node becomes a mesh sub-asset, and the node hierarchy is instantiated as entities through the prefab path (FD-15).

| Option | Strength | Cost |
| --- | --- | --- |
| (a) Generated IDs written as records in the project file | A rename in the modelling tool can be repaired by editing one record; references survive. | The project file grows a record per sub-mesh; an import command must write them. |
| (b) IDs derived from the node path | Nothing stored. | Renaming a node silently orphans every reference, with nowhere to record a fix. |
| Merge all nodes into one mesh | Simplest import. | Loses per-part transforms, picking and materials. |

### FD-24 — GPU memory allocation

**Checked:** every texture and buffer makes its own `vkAllocateMemory` call; geometry is pooled. **Predicted trigger:** resource counts approaching the device's allocation limit (commonly 4,096; this laptop's limit is not recorded).

**Recommend (a) until measured, then (b).**

| Option | Strength | Cost |
| --- | --- | --- |
| (a) One allocation per resource (today) | Simple; fine for tens to hundreds of textures. | A hard device limit, and slow at high counts. |
| (b) Engine pools per usage, like the geometry pools | No dependency; fits the existing retirement mechanism. | More engine code to own. |
| (c) Vendor the Vulkan Memory Allocator library | Well tested; handles fragmentation. | A new dependency and a multi-file rewrite of resource creation. |

### FD-25 — The first offscreen render target

**Checked:** the renderer draws straight to the swapchain, with one sample per pixel and no post-processing. A render graph is out of scope for v1. **Predicted trigger:** the first of shadows, anti-aliasing, tone mapping, render scale, an editor viewport or GPU picking.

**Recommend (c) for v1; (a) when the first pass arrives; (b) at about four passes or with editor viewports.**

| Option | Strength | Cost |
| --- | --- | --- |
| (a) An explicit, hand-ordered pass list | Small and readable for two or three passes. | Barriers and target lifetimes are written by hand; grows badly. |
| (b) A small render graph (declared passes and resources) | Correct transitions by construction; easy to add passes and views. | A real subsystem; explicitly outside v1. |
| (c) Stay swapchain-only | No work. | None of the listed features. |

Keep open now: when `VulkanGpuScene` is next touched, separate per-world state from per-view state, so a second view does not need a rewrite.

### FD-26 — Lighting and shading scope for v1

**Checked:** one directional light plus ambient, simple diffuse shading, no shadows. The v1 goal names scale, not visual features.

**Recommend (a) for v1, and (b) as the first post-v1 renderer item.** This is a scope call for the owner.

| Option | Strength | Cost |
| --- | --- | --- |
| (a) Keep today's lighting | No renderer work beyond the roadmap. | The sample game looks flat; objects do not appear grounded. |
| (b) Directional shadow map | The largest visual gain for one feature. | Needs an offscreen pass (FD-25) and culling for a second view, which the roadmap excludes unless needed. |
| (c) Many point and spot lights through a GPU light list | Richer scenes. | A light-culling structure and shader changes in every lit material. |
| (d) Physically based materials | Standard asset look. | More textures per material, changing FD-18, FD-19 and FD-21 together. |

### FD-27 — Scaling when every object moves

**Checked:** the M3 exit measured 50k objects with 20% animated (CPU preparation 3.53 ms against a 4 ms budget). 100% animated has never been measured; the assessment projects 9–14 ms.

**Recommend:** measure first. If it misses, do (d), then (b). Consider (c) only if far more than 100k moving objects becomes a goal.

| Option | Strength | Cost |
| --- | --- | --- |
| (a) Define "dynamic" as the measured 20% case | No work. | The goal statement ("tens of thousands of dynamic objects") reads stronger than the evidence. |
| (b) Run the transform pass and render-world sync on worker threads | Uses the laptop's 14 cores; game code is unaffected. | Needs FD-28. |
| (c) Compose transforms on the GPU (upload local values and a parent index) | Removes the CPU compose and shrinks uploads. | Gameplay and picking that need world transforms must compute them separately; hierarchy order on the GPU is complex. |
| (d) Tighten the single-threaded path (dense tick walk, one dirty copy) | No new architecture; already identified in the assessment. | A bounded gain. |

### FD-28 — Threading model

**Checked:** the frame loop is single-threaded. Other threads exist only in the reload service (one watcher, two workers) and the game build worker. The ECS design says declared read/write access "waits for parallel scheduling".

**Recommend (a) for v1 unless FD-27 misses, then (b).**

| Option | Strength | Cost |
| --- | --- | --- |
| (a) Single-threaded frame plus service workers (today) | Simple; reload and borrowing rules stay easy to reason about. | One core's worth of frame work. |
| (b) A small engine job system used by host stages only | Speeds transforms, sync and asset work; no game code runs on workers, so the module boundary and reload are untouched. | A worker pool to own; host stages must be made data-parallel. |
| (c) Parallel game systems scheduled from declared access | The most scaling for gameplay. | Game code must be thread-safe; reload must stop workers first; the view guard and per-system exception handling become thread-aware. |
| (d) A third-party task library | Less code to write. | A dependency in the core loop; still needs all of (b)'s or (c)'s rules. |

---

## 7. M6C — game-facing services

### FD-29 — Input bindings (owner priority)

**Checked:** developer hotkeys (F1–F7, F5 reload) are read directly from `InputState` and cannot be consumed, so a game binding on F5 would also reload. Keys and mouse buttons are in two separate lists. There is no gamepad and no text input in the contract.

**Recommend:** prioritized binding contexts (engine-developer, then editor, then game) that can consume input; one ordered event list for keys and mouse buttons, changed in the same contract break as FD-12; layout-aware key names through a platform hook with a shared fallback; gamepad and text input when a caller needs them.

| Option | Strength | Cost |
| --- | --- | --- |
| One global action map | Smallest design. | Developer keys and game keys collide; no way for a menu to take input away from gameplay. |
| Prioritized contexts that consume (recommended) | Solves the collision; the developer context is simply absent in player builds; the same structure serves an editor. | A little more API: a context stack and a consumed flag. |
| Keep keys and mouse buttons in separate lists | No layout change. | "Press any input to bind" cannot tell which came first; a gamepad would add a third list. |
| One ordered event list (recommended) | Key listening is exact across devices; gamepad buttons join later without another change. | Changes the `InputState` layout, so one host rebuild; do it inside the planned contract break. |

Bindings store HID key values (already stable) in a file under the user-data path (FD-33).

### FD-30 — Audio architecture (decide now for M4)

**Checked:** `AudioPlayer::Play` replaces the current sound (one voice); Windows uses XAudio2; macOS and Linux have no backend; only 16-bit PCM WAV loads. M4 contributors are about to write backends against this single-voice contract.

**Recommend (b):** an engine-owned mixer in shared code (voices with IDs, stop-oldest policy), with each platform backend only opening an output device and pulling mixed samples.

| Option | Strength | Cost |
| --- | --- | --- |
| (a) Pooled voices implemented natively in each backend | Uses each OS's mixer; lowest latency. | Three implementations of the same voice logic; behavior differs per platform; each M4 backend is larger. |
| (b) Engine mixer plus thin device backends | Identical behavior everywhere; each backend is small; volume, pitch and later effects are written once. | The engine owns mixing code and its real-time thread. |
| (c) Vendor a single-file audio library | Least code; all three platforms at once. | A larger dependency than `stb`/`ufbx`, and less control over the device thread. |

Later, by trigger (music or long ambience): compressed audio and streaming. Game code should request sounds through a host-drained request component, so no module pointer enters the audio thread.

### FD-31 — Text and UI

**Checked:** the engine cannot draw text. On-screen reload diagnostics already wait for it, and a "press a key to bind" screen needs it.

**Recommend (b)** after sprites move to the GPU path; decide (c) with the editor.

| Option | Strength | Cost |
| --- | --- | --- |
| (a) No text in v1 | Nothing to build. | The sample game cannot show a score, a prompt or a binding screen; diagnostics stay in the log. |
| (b) An engine font atlas drawn through the sprite path | Game-facing text and a debug overlay from one mechanism; a glyph atlas is one bindless texture. | Font baking and basic layout to write; no widgets. |
| (c) An immediate-mode developer UI library, for tools only | Fast to get panels and inspectors. | Not suitable as game UI; a second renderer integration that mainly serves the post-v1 editor. |

### FD-32 — Collision and physics scope

**Checked:** deferred with no milestone. Courtyard has swept checks in the XZ plane; 3D has picking only.

**Recommend (a) until the named sample game needs a mechanic, then (b); (c) is post-v1.**

| Option | Strength | Cost |
| --- | --- | --- |
| (a) Stay deferred; game-specific code | No engine work. | Each game rewrites the same queries. |
| (b) Engine queries only: ray, sweep and overlap against simple shapes held in components | State stays plain component bytes, so reload, saving and migration need nothing new. | No simulation (stacking, joints). |
| (c) Integrate a physics library as a host-owned service | Full simulation. | Body state lives outside components, so saving and migration need a second path; the library brings its own threads (FD-28). |

### FD-33 — Save data and the user-data path

**Checked:** there is no writable user location; the player build needs a writable install folder.

**Recommend:** a platform-backed user-data path; settings and bindings as small game-defined files; world saves through the generic scene serializer applied to the live world.

| Option | Strength | Cost |
| --- | --- | --- |
| Game-defined save files only | Each game saves exactly what it wants. | Every game writes its own serializer. |
| World snapshot with the scene codec (recommended for world state) | Nearly free once the generic format and persistent IDs exist; old saves load through the field-migration rules. | Derived and transient data must be marked "do not save", which the field table needs anyway. |

### FD-34 — Smaller service decisions

| Topic | Recommendation | Trade-off in one line |
| --- | --- | --- |
| Fixed step | Keep the per-client 60 Hz step; count and log dropped time; expose the step remainder. Add interpolation when a game needs it. | Interpolation smooths motion on high-refresh displays but every rendered component then needs a previous value. |
| Picking (Q13) | Measure first. If slow, test against cached world transforms and re-compose only the final candidate. | Cached transforms can be one frame old; live composes cost per entity. |
| Singleton game state | A component on one entity, read with `Single<T>`. No separate resource store. | A resource store is a second kind of retained state that reload and saving would have to cover. |
| Separate graphics/present queue option (open in `TODO.md`) | Add the development flag. | A small amount of dev-only code, against a path that no tested device has ever run. |

---

## 8. Hot reload and iteration

### FD-35 — The derived-data cache, and cooking for players

**Checked:** reload records are in memory, keyed by input hashes plus an importer version, so they can be persisted later (owner decision Q4). Player packages ship raw FBX and PNG, so the importers run in shipped games. **Predicted trigger:** texture compression (FD-21) or M7 packaging, whichever is first.

**Recommend (c).**

| Option | Strength | Cost |
| --- | --- | --- |
| (a) No cache; players import sources | Nothing to build. | Load time grows with content; importers and their attack surface ship in players. |
| (b) A development-only disk cache | Faster cold starts. | Players still import; packaging remains a separate code path. |
| (c) One content-addressed store: development sessions read and write it, and packaging copies the entries a profile needs | One pipeline for both; players need no importers; the keys already exist. | Cache location, size limits and cleanup must be decided; a stale-cache bug becomes possible, so the importer version discipline matters. |

### FD-36 — Module unload on Linux and macOS

**Unverified** until the first Linux build: on Linux a `dlclose` may leave the library mapped (unique symbols, thread-local destructors), and a second load could then bind to the first generation's data.

**Recommend (b).**

| Option | Strength | Cost |
| --- | --- | --- |
| (a) Require a real unload | Clean memory. | Depends on toolchain behavior the engine does not control. |
| (b) Load every generation from a uniquely named copy; treat unload as best effort; build the game with hidden visibility and without unique symbols; forbid thread-locals with destructors in game code | The same model as Windows' execution copy; correctness does not depend on unload. | Old generations may stay mapped in development sessions. |

Field migration must not be enabled on Linux before these flags are in place.

---

## 9. Platform decisions (not the M4 task list)

### FD-37 — MoltenVK scope (Q12)

**Recommend (a).**

| Option | Strength | Cost |
| --- | --- | --- |
| (a) Require the MoltenVK tier that supports the bindless array; reject others with a clear message | One renderer path on every platform. | Intel Macs and low-limit configurations cannot run the engine. |
| (b) Build a per-texture fallback path | Wider Mac support. | A second batching path that M6B's grouping must carry and test forever. |

Related, same answer either way: record the macOS and Xcode floor, and whether Apple Silicon is the only supported Mac.

### FD-38 — Linux display server

**Checked:** the Linux backend is XCB.

**Recommend (a) for v1.**

| Option | Strength | Cost |
| --- | --- | --- |
| (a) XCB only; Wayland desktops run it through the X compatibility layer | One Linux backend. | Scaling and some input behavior are less exact under the compatibility layer (**unverified** here). |
| (b) Add a native Wayland backend | Correct scaling and input on modern desktops. | A second Linux backend to write and verify. |

### FD-39 — What the performance goal means off the Windows laptop

**Checked:** the minimum spec is the Windows test laptop, and M4's exit only asks that samples and reload paths run.

**Recommend (b).**

| Option | Strength | Cost |
| --- | --- | --- |
| (a) The target applies to Windows only | No extra measurement. | "Cross-platform" and "high performance" are then never shown together. |
| (b) Linux is measured on the same laptop and must meet the target; macOS records a number on the contributor's machine without a pass/fail line | One hardware baseline for two systems; honest about the Mac. | A dual-boot setup, and a second set of recorded results. |
| (c) A minimum spec per platform | Complete. | Needs reference hardware nobody has named. |

### FD-40 — GPU tests in CI

**Checked:** GPU-labelled tests and the smoke run only locally.

**Recommend (b), after the Linux compile job exists.**

| Option | Strength | Cost |
| --- | --- | --- |
| (a) Local only (today) | No CI work. | A renderer crash or validation error is found only when someone runs the scripts. |
| (b) A Linux CI job on a software Vulkan driver | Free; catches crashes and validation errors on every push. | No performance signal; images may differ from the NVIDIA references, so use it for validation only. **Unverified** that the driver has every required feature; check on the runner first. |
| (c) A self-hosted runner with a GPU | Real hardware and real timings. | A machine to maintain, and care needed before running outside pull requests on it. |

### FD-41 — Toolchain floors

**Recommend:** record exact tested versions (MSVC, Clang, GCC, standard library, Xcode, Vulkan SDK, DXC) in one place and pin them in CI. The alternative, "latest", costs nothing until a new compiler breaks the build on a contributor's machine, which is the hardest moment to find out.

---

## 10. M7 — quality and release

### FD-42 — Package format

**Recommend (a) for v1.**

| Option | Strength | Cost |
| --- | --- | --- |
| (a) A folder of loose, cooked files | Simple; easy to inspect and patch; fits the existing staged, atomic packaging. | Many small files; no compression. |
| (b) One archive file | Fewer files; optional compression. | An archive format and reader to own, with no measured load-time need. |

### FD-43 — How players load the game module

**Checked:** players load the DLL through the development reload path, which makes an execution copy and needs a writable install folder.

**Recommend (b).**

| Option | Strength | Cost |
| --- | --- | --- |
| (a) Keep the development path | No work. | Needs a writable folder; leaves copies behind; a file watcher in a shipped game. |
| (b) Load the DLL in place, with no watcher and no copy | The same module contract as development, minus the reload machinery. | A small second loading mode to verify. |
| (c) Link the game statically into the player | One binary; whole-program optimization. | A build that differs from what was developed and tested all along. |

### FD-44 — Sanitizers, analysis and crash data

**Recommend:** address and undefined-behavior sanitizers on the CPU tests in the Linux CI job; an MSVC address-sanitizer preset for local use; static analysis as information, not a gate; on a crash, a log file under the user-data path plus a Windows minidump.

| Option | Strength | Cost |
| --- | --- | --- |
| Sanitizers in CI on CPU tests | Finds memory errors in exactly the code M6A rewrites (pools, migration, serialization). | Longer CI; needs the Linux job. |
| Sanitizers as a required local step | Earlier detection. | Slow builds in the reload loop. |
| Log file plus minidump (recommended) | Enough to diagnose a tester's crash. | Symbols must be kept per build. |
| A crash-reporting service | Automatic collection. | A third-party service and privacy questions before there are users. |

### FD-45 — Compatibility promise after v1

**Recommend (c) as the default, with (a) for container-level breaks.** Decide at release.

| Option | Strength | Cost |
| --- | --- | --- |
| (a) Converter, then delete the old reader | One reader in the engine at any time. | Old files need the tool from the matching version. |
| (b) Keep versioned readers for several versions | Old files just load. | Old code paths stay in the engine and must be tested. |
| (c) Rely on the self-describing format: unknown fields skipped, new fields defaulted, renames through aliases | Most changes are not breaks at all. | Does not cover changes to the container itself. |

---

## 11. After v1: directions, and what to keep open now

Nothing here is scheduled. Each row names the trigger and the present-day choice that keeps it cheap.

| Direction | Main choice ahead | Keep open now |
| --- | --- | --- |
| Editor and play sessions | In-process tool UI, or a separate editor process talking to the engine. In-process is faster to build and shares the renderer; a separate process survives game crashes. | One Registry per world (FD-13), engine-owned session and play state (FD-12), persistent IDs (FD-09), the field table (FD-08), per-view renderer state (FD-25). |
| Parallel game systems | Declared read/write access per system, with a scheduler. | Phase-end Commands (FD-14); no game code on worker threads until then (FD-28). |
| Render graph, shadows, post-processing | Explicit pass list or graph (FD-25, FD-26). | Per-world and per-view state kept apart. |
| Level of detail, meshlets, occlusion | Only when measurements ask; Hi-Z was measured as not needed at 100k. | The culling pass already leaves room for one more test. |
| Physics | Library integration as a host-owned service (FD-32). | Collision shapes as plain components. |
| Skeletal animation | Skinning in the vertex stage or in a compute pass before culling. | The object record and instance layout have no skinning data today; adding it touches the importer, the mesh pools and the lit vertex shader together. |
| Large worlds and streaming | Scene-level streaming on top of async resolution. | Asynchronous resolution with placeholders (FD-22), random entity IDs (FD-09). |
| Archetype storage | Only if a benchmark gate fails, as agreed in M1. | Storage stays behind the Registry and view API. |

## 12. What this study did not do

- Nothing was built, run or measured. Performance statements are the recorded ones from the design notes or the assessment's projections, and are marked as such.
- The assessment's file:line evidence was reused, not re-verified line by line. Statements marked **Checked** were re-read in the code where they describe current code (pacing, warning flags, `SystemContext`, materials, shaders, lighting, textures, audio, input, threads, hierarchy storage, name references, the FBX importer, GPU allocations, the Linux backend). Three rest on the assessment or a design note instead: the session-ending paths (FD-02), the lease and resolution behavior (FD-22), and the player loading path (FD-43).
- Statements marked **Unverified** concern tools and platforms that were not run from this machine.
- No effort or time estimates are given beyond the assessment's S/M/L sizes.
