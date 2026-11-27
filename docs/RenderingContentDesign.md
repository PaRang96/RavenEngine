# M6B design note: rendering and content

Status:
- **Decided (2026-11-26).** The five decisions that needed the owner (they change what designers or players see, or the scope) are settled; they are listed in [Decisions for the owner](#decisions-for-the-owner). The technical decisions are delegated and recorded in [Delegated decisions](#delegated-decisions), judged by business alignment and expandability.
- M6C (the previous stage) was accepted with M6A and the review amendments in the owner's joint review on 2026-11-27 ([GameServicesDesign.md](GameServicesDesign.md#m6c-exit-checklist-results-2026-11-25)).

This note settles FD-18 to FD-28 from [FutureDecisions.md, section 6](FutureDecisions.md#6-m6b--rendering-and-content) for M6B, the assessment's steps M6-9 and M6-10. Every "Today" statement was re-checked against the code at `0f593d6` on 2026-11-26.

---

## Reviewer guide

### Decisions for the owner

| # | ID | Question | Recommendation |
| --- | --- | --- | --- |
| 1 | [FD-18](#fd-18--material-model) | How designers author materials | **Decided 2026-11-26 (owner): (b).** A material **file** shared by many objects, referenced by ID; per-object tint stays; the file extension is `.ravenmaterial` (owner, 2026-11-27) |
| 2 | [FD-19](#fd-19--custom-material-shaders) | What a custom material shader may change | **Decided 2026-11-26 (owner), revised after research:** pixel shaders plus an engine-owned vertex offset hook with a declared maximum displacement; four textures and up to 16 named `float4` values declared per shader; compiled at build time for players and on a background compile queue in development |
| 3 | [FD-23](#fd-23--multi-object-fbx) | How a multi-part FBX keeps stable IDs when the modelling file changes | **Decided 2026-11-26 (owner), revised:** IDs generated once; imported files get a `.ravenasset` sidecar (ID, kind, import settings, part table), Raven's own formats carry their ID in their header, and the project file keeps only project settings; parts match by name path and renames are reported |
| 4 | [FD-26](#fd-26--lighting-in-v1) | How much lighting v1 has | **Decided 2026-11-26 (owner):** v1 keeps one sun plus ambient light; the renderer's target after v1 is physically based materials plus global illumination, and M6B must not block it |
| 5 | [The 2D run](#the-2d-run) | Whether to run Courtyard (2D) now | **Decided 2026-11-26 (owner): no 2D work at all for now, and Courtyard is removed.** Sprites stay as 3D world quads (the Samples Gallery), and B5 is checked against a Gallery baseline |

### Delegated decisions

| ID | Decision |
| --- | --- |
| [FD-20](#fd-20--sprites-and-transparent-objects) | Sprites join the render world. Cut-out sprites are GPU-culled like meshes; blended sprites keep exact back-to-front order through a CPU-sorted path fed from the render world. |
| [FD-21](#fd-21--textures) | Mip maps generated on the GPU at load, and a small fixed sampler set. Compressed texture formats wait for M7 cooking (a dependency, so an owner decision then). |
| [FD-22](#fd-22--asset-resolution) | Keep preloading at scene load; asset IDs written at runtime load on workers with a placeholder instead of stalling a frame. |
| [FD-24, FD-25, FD-27, FD-28](#other-decisions) | VMA for GPU memory (owner, 2026-11-26), no offscreen targets; measure 100% animated objects; threading already settled by M6C's job system. |

---

## Goals and fixed constraints

- **M6B exit (roadmap):** sprites and one custom material shader run on the GPU-driven path; a multi-object FBX imports with stable IDs. The 2D run was dropped with Courtyard (owner, 2026-11-26).
- **No regression:** the 50k animated steady state stays within measurement noise of M6C (same-day A/B), and the 3D captures stay within the M2 image tolerance.
- **Hot reload:** every new content type (material, shader, multi-object model) reloads with last-good recovery, like textures and meshes today.
- **No throwaway work**, **minimal tests**, **minimum spec** (the RTX 3070 Ti laptop, Release), and **third-party code never edited**, as in every stage.
- **Owner follow-up from M6A:** M6-9 rewrites the render-world sync and must find the 0.27 ms it gained at 50k animated objects ([DataModelDesign.md](DataModelDesign.md#m6a-exit-record-2026-11-24), item 4).

---

## FD-18 — Material model

### Today (checked)

- `MaterialComponent` holds three values inline on each entity: a base color, a base-color texture and a lit flag (`MaterialComponent.hpp:11-15`). `StaticMeshComponent` already has a per-object `Tint`.
- The render world turns each distinct value into a 32-byte table entry, found by a linear search and compared exactly. Entries are never removed while a scene lives (`RenderWorld.cpp:173-193`).
- The render world holds no texture leases; the scene does.
- `AssetKind::Material` and `MaterialAsset` already exist as names, but nothing loads them. `.ravensound` (M6C) is a precedent for a text-authored asset.

### Options

| Option | What designers see | Strength | Cost |
| --- | --- | --- | --- |
| (a) Values on each object (today) | Every object carries its own color and texture | No new file type | Changing "all red crates" means editing every crate; a custom shader would be repeated on every object; the table grows with every distinct value |
| **(b) A material file referenced by ID** | A `.ravenmaterial` text file (shader, colors, textures); objects point to it; per-object tint still works | One edit changes every object using it, live; the material ID is the table key and owns the texture leases | A new file type, edited as text until an editor exists |
| (c) Material file plus per-object overrides of any parameter | As (b), plus any value overridable per object | Most flexible | Every override combination is a new table entry again; animated overrides grow it every frame |

**Decision (owner, 2026-11-26): (b).** The change is additive: `MaterialComponent` gains the material `AssetRef`, and its inline values stay as defaults ([planning decision 7](../ROADMAP.md#planning-decisions-2026-11-24)).

**Recommendation: (b).** It keeps (a)'s per-object variation through the existing `Tint`, which covers the common "same material, different color" case without (c)'s table growth. Overrides beyond tint can be added later as their own decision without changing the file format. Existing scenes are rewritten by the sample generators (no hand migration).

---

## FD-19 — Custom material shaders

### Today (checked)

- Lit fragment shaders are written against `Raven/Material.hlsli`: fixed inputs (color, UV, normal, one sun light), one bindless texture array and one sampler.
- All GPU-culled meshes draw with one pipeline in one indirect call, one command per mesh. There is no per-material pipeline yet.
- Shaders compile at build time with DXC; players ship only compiled `.spv` files. In development, shader edits recompile live through DXC as an external tool.
- The owner already decided (2026-11-23, Q3) that per-material shaders arrive in M6 on the same command layout.

### Options

| Option | What designers see | Strength | Cost |
| --- | --- | --- | --- |
| **Pixel (fragment) shaders only** | Custom surfaces: patterns, scrolling, glow, dissolve, toon shading | Vertex layout, culling and the interface checks stay single; one pipeline per shader | No vertex movement in materials (wind on grass, wobble) |
| Vertex and pixel shaders | Also wind and wobble | Vertex effects | The GPU-culled path assumes one vertex transform; every vertex shader must be checked against the object layout, and culling bounds no longer hold when vertices move |
| **Fixed parameter record: 4 values + 4 textures, named in the material file** | Parameters by name (`EdgeColor`, `NoiseTexture`) | One material table with one stride for every shader | At most four values and four textures per material |
| Any number of typed parameters per shader | Unlimited parameters | Flexible | Variable sizes break the single table; reflection and validation per shader |
| Compile shaders at runtime in players | Mods could add shaders | — | Ships DXC (licensing and size), with no v1 need |

**Decision (owner, 2026-11-26), revised after a survey of Unreal 5 (Nanite World Position Offset), Unity 6 (SRP Batcher, GPU Resident Drawer), Godot 4, Bevy and id Tech:**

- **Vertex control:** the engine keeps its own vertex shader. A material may add an `Offset` function (the Nanite World Position Offset model) and must then declare its maximum displacement. Culling grows those objects' bounds by that amount, and only materials with an offset take their own, slower vertex path; plain materials keep today's path. Full custom vertex shaders stay excluded, as in every surveyed GPU-driven engine.
- **Parameters:** four textures, plus 0 to 16 `float4` values declared per shader as an HLSL struct and named in the material file. Material data lives in one GPU buffer, each material at its own offset, so a material pays only for the values it uses and the single table survives variable sizes. The names are checked against the compiled shader at load. Raising the cap changes one constant, not the format.
- **Compiling:** players ship only build-time shaders. In development a background compile queue (the process lane of AA2-HR-01) compiles them, and the last good version, or a placeholder for a new material, draws until each compile finishes.
- **Few shaders, rich data:** the survey shows that shader count, not parameter count, drives cost at scale (Nanite bins, Doom Eternal's few uber shaders), so variety comes from parameters and textures.
- The B0 contracts specify the fragment-side and vertex-hook plumbing before B2 (AA2-RND-01).

**Texture names and wrap (owner, 2026-11-27, after a survey of Unity, Unreal, Godot 4, Bevy and O3DE, all of which take parameter names from the shader or its material type):** the shader declares its texture names and whether each repeats or clamps, and the material file fills them by name, checked against the compiled shader like values. *Delegated shape:* one parameter struct per shader holds the named `float4` values and the named textures, each texture typed `RavenClampTexture` or `RavenRepeatTexture`; SPIR-V keeps the member and type names, so the engine reads both from the compiled shader, and the shader samples with `RavenSample(parameters.Noise, uv)`, the wrap fixed by the type. Fixed slot names and numbered slots were the alternatives; no surveyed engine uses them.

**Original draft recommendation, kept for the record:** pixel shaders only, a fixed record of four `float4` values and four texture indices, names mapped to slots in the material file, compiled at build time and reloaded live in development. Naming in the file gives the readability of typed parameters without breaking the single table. Vertex effects stay possible later as a separate decision; they would not change the parameter record or the files.

---

## FD-20 — Sprites and transparent objects

*Delegated.*

### Today (checked)

- Sprites are gathered on the CPU every frame, culled on the CPU, sorted (layer, order, depth, submission) and drawn in instanced runs, each run binding its own per-texture descriptor set. They never enter the render world.
- The M6C UI overlay already draws instanced quads from the bindless texture array with the shared sampler, and was built for sprites to share ("feed 2" in [UIDesign.md](UIDesign.md)).
- Courtyard draws only sprites. It is removed (owner, 2026-11-26); the Samples Gallery's opaque and blended quads remain the sprite content.

### Decision

- Sprites join the render world and the bindless array, using the overlay's instance format. The per-texture descriptor sets and per-texture samplers go away.
- **Opaque and cut-out sprites** are GPU-culled like meshes and ordered by real depth; Opaque draws exactly as today, and `Cutout` is a new mode that discards transparent texels (Q9, delegated; corrected 2026-11-27 by [B0 contract 4](#4-sprites-aa2-rnd-03-q8-and-q9), since mapping sort layers to depth would have changed what is on screen).
- **Blended sprites and blended meshes** keep exact back-to-front order: the visible ones are sorted on the CPU from render-world data, as today, so nothing changes on screen. Sort layer and order apply only to them.
- A GPU sort or order-independent blending only if a measured blended count needs it.

**Why:** both behaviors players see today (hard and soft edges, exact order) stay, the common case becomes GPU-driven, and the overlay work is reused, not replaced.

---

## FD-21 — Textures

*Delegated.*

### Today (checked)

Every texture is RGBA8 with one mip level. The bindless set has one clamp sampler without mip filtering, and each texture also creates its own identical sampler.

### Decision

- **Now:** generate mip maps on the GPU at load, and a fixed sampler set (clamp or repeat × linear with mips), chosen per material texture. This removes the shimmer of distant textures, which shows most in the 50k-object scenes, with no new dependency and instant reload. Cost: about a third more texture memory.
- **At M7:** cook block-compressed textures for players. That needs an encoder library, so it comes to the owner as a dependency decision then.

---

## FD-22 — Asset resolution

*Delegated.*

### Today (checked)

- Scene load already preloads every asset referenced by any component field (`LeaseAssets`, through the field table), so authored content never loads mid-frame.
- An asset ID written by game code at runtime that no component referenced at load still loads synchronously on first use: inside the render-world sync, in sprite gathering, in the UI and in audio.
- No first load uses a worker. Leases live as long as the scene.

### Decision

- Keep the preload (it already covers assets that game rules components reference).
- **Runtime-only IDs load on the job system** with placeholders: a white texture, an untextured material, a silent sound. A mesh that is not ready is not drawn or picked until it is (no wrong bounds). The GPU upload moves into one per-frame upload batch (M6-10).
- Leases stay scene-lifetime; release while a scene lives waits for a measured memory need.

**Why:** players never see a frame stall; a brief placeholder appears only for content no scene referenced, which games can avoid by referencing it from a component.

---

## FD-23 — Multi-object FBX

### Today (checked)

The FBX importer rejects files with more than one mesh node. One project record maps one asset ID to one file. Unknown project records are skipped with a warning, which leaves room for sub-asset records.

### Options

| Option | What designers see | Strength | Cost |
| --- | --- | --- | --- |
| **(a) IDs generated once and stored as records in the project file** | Re-importing after edits keeps every reference; a renamed part is reported and can be repaired in one line | References survive renames and re-exports | The project file grows one record per part; an import command writes them |
| (b) IDs computed from each part's name/path in the file | Nothing stored | No records | Renaming a part in the modelling tool silently breaks every reference to it, with nowhere to fix it |
| (c) Merge all parts into one mesh | One mesh per file | Simplest | Loses per-part transforms, picking and materials |

**Decision (owner, 2026-11-26): (a), with the storage revised so that asset properties and project settings are separate** (the Godot model, compared with Unity's `.meta` files):

- **Imported files** (PNG, FBX, WAV and the other decoded formats) get a sidecar beside them, for example `Cube.fbx.ravenasset`. It holds the asset ID, its kind, its import settings (color space now; mips and compression later) and, for models, the part table: each part's name path and ID.
- **Raven's own formats** (`.ravenscene`, `.ravensound`, `.ravenmaterial`) store their ID in their own header, so they need no second file.
- **`Project.ravenproject` keeps only project settings:** the scene order and startup scene, the window title and the physics layer names.
- **Discovery:** development scans `Content/` at startup, and the reload service's existing watch reimports new or edited sidecars live. Packaging writes one generated index, so players never scan.
- **Reported, never silent:** a source file without a sidecar gets a new ID with one log line; a sidecar without its source is reported as an orphan; two files with one ID (a copy) are reported and the copy gets a new ID.
- **Timing:** the split lands at the start of B2, before `.ravenmaterial` exists, as `raven-project 3` with a converter from today's records; the sample generators regenerate their content.

**Original draft recommendation, kept for the record: (a), with (b)'s convenience.** A `RavenSceneTool import-model` command (and the live reload) matches parts by name path first, so ordinary re-exports need nothing. New parts get new IDs, and a part whose name vanished is reported with its old ID so a rename can be repaired. Each part becomes a mesh sub-asset; the file's node hierarchy is instantiated as entities.

---

## FD-26 — Lighting in v1

### Today (checked)

One directional light plus ambient, simple diffuse shading, no shadows. The v1 goal names scale, not visual features. The Arena sample looks flat: objects do not appear grounded.

### Options

| Option | What players see | Strength | Cost |
| --- | --- | --- | --- |
| **(a) Keep today's lighting for v1** | Flat look | No renderer work beyond the roadmap | The samples look plain |
| (b) Directional shadows | Objects grounded; the largest visual gain for one feature | Biggest improvement per effort | Needs an offscreen pass (FD-25) and culling for a second view; adds to M6B |
| (c) Many point and spot lights | Richer scenes | — | A light-culling structure; changes every lit material |
| (d) Physically based materials | Standard asset-store look | — | More textures per material; changes FD-18, FD-19 and FD-21 together |

**Decision (owner, 2026-11-26): (a) for v1; the target direction after v1 is (d), physically based materials, plus global illumination.** M6B keeps that path open without building any of it (no throwaway):

- FD-19's four textures fit the usual PBR set: base color, normal, packed occlusion/roughness/metallic, emissive. Its up to 16 values cover the PBR factors.
- Normal maps need vertex tangents, which `ColorVertex` lacks today. The B7 FBX importer keeps the tangent data reachable, and tangents join the vertex layout when PBR lands, not before.
- Directional shadows remain the first step, because GI builds on them. GI and PBR also need an HDR offscreen target with tone mapping, which FD-25's per-view split keeps possible.
- `Material.hlsli` keeps lighting behind its one lighting function, so PBR and GI replace that function rather than every material shader.

**Original draft recommendation: (a) for v1, (b) as the first renderer item after v1.** M6B's material and shader work keeps (b) open: shadows would add one input to `Material.hlsli` and one pass, without changing material files.

---

## The 2D run

The project rule is that 2D runtime testing needs an explicit owner request. Courtyard builds, but its movement has not been run through the renderer rewrites since M1, and M6B moves its sprites to a new path.

**Decision (owner, 2026-11-26): no 2D work at all for now.** "We can make things in 2D, but no value for now." The Courtyard sample is removed with the review amendments, together with the 2D-only `Collision2D` helpers that only Courtyard calls. The engine keeps sprites as 3D world quads, the overlay and the UI. B1 becomes a sprite baseline: the Samples Gallery's opaque and blended quads captured on today's path, so B5 is compared against it.

**Original draft recommendation:** request it now as step B1. It records Courtyard's look and play on today's path, so the sprite move (B5) is compared against a known-good baseline, and the exit's "owner-requested 2D run" is checked against the same captures.

---

## Other decisions

*Delegated; each keeps its trigger from FutureDecisions.md.*

| ID | Decision |
| --- | --- |
| FD-24 GPU memory | *Owner decision, 2026-11-26 ([planning decision 16](../ROADMAP.md#planning-decisions-2026-11-24)), replacing the delegated draft:* textures, staging buffers and attachments are sub-allocated through AMD's Vulkan Memory Allocator (VMA, MIT), vendored unmodified behind a Raven seam when B3 or B6 starts. It removes the per-resource allocation cap and load hitches and provides the memory budget. M6B still records the texture count and the device limits. |
| FD-25 Offscreen targets | Stay swapchain-only unless FD-26 changes. When `VulkanGpuScene` is touched, per-world state is separated from per-view state so a second view needs no rewrite. |
| FD-27 Every object moving | Measured in M6B (100% of 50k animated) after the render-world sync rewrite; if it misses, tighten the single-threaded path, then use the job system. |
| FD-28 Threading | Settled by M6C's shared job system: host stages may use it; game systems stay single-threaded. |

---

## B0 contracts (2026-11-27)

The renderer and content contracts M6B builds against, written before any B2 code (AA2-DOC-07). Each names the assessment finding it settles; the assessment holds the evidence. All are delegated technical decisions within the owner's FD-18, FD-19, FD-23 and Q8/Q9 answers.

### 1. Fragment ABI v2 and the material record (AA2-RND-01, FD-19)

- **The lit fragment input grows additively** (`Raven/Material.hlsli`): `nointerpolation uint Material` and `float3 WorldPosition` join locations 0-6. Shaders that declare a subset keep compiling, because the interface checker accepts subsets. Accessors hide the layout: `RavenMaterialValue(i)`, `RavenMaterialTexture(i, uv)`, and a `RavenFrame` struct (time, delta, camera position). No shadow slot is reserved: shadow coordinates come later from `WorldPosition` and the frame uniform.
- **The material record (`GpuMaterialData`)** becomes BaseColor, four texture indices, four sampler indices, flags, and an offset to the material's values. Values live after the fixed record in the same buffer, 0 to 16 `float4` per material as its shader declares (FD-19's variable-size table), so a material pays only for the values it uses. The sampler slots name the single clamp sampler until B3's sampler set. The widening lands before B2's material table; the static asserts, `GetBufferLayouts` and `GpuScene.hlsli` change in the same snippet.
- **The frame uniform gains `Time`**, and its set and the materials binding become fragment-visible. The fragment checker accepts the new read-only bindings and locations. Color, Unlit, Normals and Wireframe move to the accessors at the start of B4.
- **Versioning:** the ABI is recorded as v2 in [GpuDrivenDesign.md](GpuDrivenDesign.md) G4. It is engine-private until B4 lets games author shaders; from then on it is part of the game contract.
- **Checked by** one capture comparison at the M2 tolerance after the port.

### 2. The vertex offset hook (FD-19)

- The engine keeps its vertex shader. A material shader may define `float3 RavenOffset(RavenOffsetInput input)`, called after the object transform, in world space, with the same inputs as the fragment side plus the vertex position and normal.
- **A shader with an offset must declare `RAVEN_MAX_OFFSET` (metres).** The material loader rejects a shader that defines the function without the bound. Culling grows each such object's bounding sphere by the bound, so an offset can never pop an object out of view.
- **Two vertex programs:** materials without an offset keep today's vertex path and pipeline; materials with one get a vertex program compiled with the hook. Grouping (contract 3) keys on the pipeline, so plain materials pay nothing.
- Normals are not re-derived from the offset in v1; a shader that needs them returns its own from an optional `RavenOffsetNormal`.

### 3. Draw grouping, retirement and commands (AA2-RND-02)

- **Group key:** (pipeline key, mesh), where the pipeline key is (material shader, blend class, cull mode, offset or not). The default cull mode stays none (double-sided), as today. Groups live in a hash map, and the group is recomputed when the material or the mesh changes.
- **Tint-only changes take the cheap path:** a per-slot record of the mesh reference means a `Tint` write never re-resolves the mesh. `Tint` stays on `StaticMeshComponent`, because moving it would lose authored values.
- **Retirement:** a group whose count reaches zero goes to a free list, and its slot draws an empty command, so group IDs stay stable.
- **Commands:** each mesh record stores its `CommandIndex`. The plan lays commands out contiguously per pipeline, Cull writes `Commands[mesh.CommandIndex]`, and the frame issues one indirect draw per pipeline range. Debug views draw all ranges with one engine pipeline.
- **The mesh table is a persistent device buffer,** uploaded only when a group is added or retired or a mesh's revision changes. Mesh hot reload bumps a revision counter (today it relies on the per-frame re-resolve). The command templates are still reset on the GPU every frame, because Cull accumulates instance counts.
- **Skinning stays out of v1:** the 128-byte object record does not widen. A later per-object vertex-base override would go in a parallel stream.
- **Order in M6B:** this restructuring is the first render-world work, before B2's material keying, because B2 keys entries on the same group code.

### 4. Sprites (AA2-RND-03, Q8 and Q9)

This corrects the FD-20 draft, which would have changed what players see:
- **Opaque sprites stay exactly as today:** real depth, depth-written, no alpha test, sort fields ignored.
- **`Cutout`** is appended to `SpriteBlendMode` (stored ordinals stay valid). It is depth-ordered like Opaque and discards below alpha 0.5.
- **Opaque and Cutout sprites become render-world objects:** one shared quad mesh group, a material per texture and blend class, and the normal cull and indirect path.
- **Blended sprites keep their exact back-to-front CPU sort,** now fed from render-world data and drawn with a blend pipeline. Sort layer and order apply only to them.
- **Sprite sheets (Q8):** `SpriteComponent` gains `Vec4 UvRect` (default 0, 0, 1, 1) and `Vec4 SizePivot` (default size 1, 1, pivot 0.5, 0.5), multiplied with the world transform, so existing sprites look the same. Per-sprite UV and pivot travel in a parallel `SpriteData` stream indexed by slot, so mesh records stay 128 bytes. Its upload is measured at 50k.
- The overlay's screen-space quad format is not culled on the GPU. Sprites share only fragment helpers with it, through an `.hlsli`. The comments in `Overlay.hpp` and `UIDesign.md` that promise a shared instance format are corrected when B5 lands.

### 5. Uploads and descriptors (AA2-RND-04)

Recorded in full in [RendererDesign.md](RendererDesign.md) when the batch lands:
- **One per-frame upload batch** is the first texture change in M6B, before mips: one command buffer per frame that signals the timeline semaphore and ends in a transfer-to-fragment barrier. Textures created cleared use `vkCmdClearColorImage` instead of a CPU zero buffer (the 4 MB per glyph page goes). The bindless element is handed out at once, which is safe because the batch is submitted on the same queue before the frame that first samples it.
- **B3's mip chain** records into the batch's command buffer, after a format-feature check.
- **Development texture reload** keeps rewriting the element in place. Once the per-upload fence wait is gone, it first waits for the timeline: a rare one-frame stall in development only.
- **Placeholders and streaming (B6)** never rewrite in place. They take a new element, material and sprite entries switch through one per-asset content-revision event, and the old element is retired through the timeline. This needs contract 6's keyed material map, so it follows B2.

### 6. The material table (AA2-RND-05, FD-18)

- **Keyed by material asset ID,** plus one default entry per distinct inline value of `MaterialComponent` (the additive B2 change). Entries own their texture leases and carry dirty flags and a free list, so a material edit uploads only its entry, through Scatter with the record stride as a parameter.
- **A freed bindless element is reset to white** before it is reused, so a stale index never samples another texture.
- **Non-finite values never enter the table:** the render world hides an object whose Tint or inline values are NaN or infinite, logging once per entity; the `.ravenmaterial` parser rejects non-finite values and unknown parameters and keeps the last good version.

### 7. Material shaders as content (AA2-RND-06, AA2-HR-01)

- **`AssetKind::Shader`** is an asset kind (one row in `AssetKinds.hpp`). A game's shaders are compiled by a build rule beside `raven_add_game` into staged `.spv`; players ship SPIR-V only, never DXC.
- **In development** each shader is watched through the reload service and compiled on its process lane, with last good recovery.
- **One pipeline per (shader, render state)** is built off the main thread and published and retired on its own; one shader's edit never rebuilds the others. Debug views keep the engine's pipeline bundle.
- **A missing or failed material shader** draws with the engine's lit pipeline, with one warning.
- **Pipeline cache:** startup pipeline creation is measured with four shaders; a `VkPipelineCache` in the user-data folder comes only if the numbers ask for it.

### 8. Device limits and bindless classes (AA2-RND-07)

- Device selection reads the Vulkan 1.2 descriptor-indexing limits and rejects a device below the 4,096-texture bindless capacity with a named reason. MoltenVK's portability-subset features are queried and logged once.
- **Allocation classes** in the bindless array: element 0 white; an engine reserve (64 elements, for glyph pages and engine textures); content for the rest. A full content class returns the placeholder with one log line instead of throwing, so content can never starve the UI.
- Per-class counts are reported in the resource stats and the B8 measurements.

### 9. Asset sidecars and `raven-project 3` (FD-23)

- **Sidecar** `<file>.ravenasset` beside every imported file, a small text file:
  ```
  raven-asset 1
  id 4f6c2c8e-6c7e-4a51-9a0d-0f8a0b6b2a10
  kind texture
  color-space srgb
  ```
  Models add one `part <name path> <id>` line per part (B7). Unknown keys are skipped with a warning, like project records.
- **Raven's own formats** (`.ravenscene`, `.ravensound`, `.ravenmaterial`) carry their ID in their header, so they have no sidecar.
- **`Project.ravenproject` (`raven-project 3`)** keeps only project settings: scene order and startup scene, window title, fixed-step rate, physics limits and layer names. Asset records leave it.
- **Discovery and reports:** development scans `Content/` at startup, and the reload service's watch picks up new or edited sidecars live. A source without a sidecar gets a new ID, with one log line, and the sidecar is written. A sidecar without its source is reported as an orphan. Two files with one ID (a copied file) are reported, and the copy gets a new ID. Packaging writes one generated index, so players never scan.
- **Conversion:** `RavenSceneTool upgrade-project` turns a `raven-project 2` file into version 3 plus sidecars, keeping every ID, so scene references survive. The sample generators then write version 3 directly.

### Contract changes (one window, at the start of B2)

Everything that changes the game contract lands in one commit series at the start of B2, so games rebuild once (rule 10 of [CodingRules.md](CodingRules.md)). The rest of M6B is private code.

| Change | Kind | Source |
| --- | --- | --- |
| `MaterialComponent` gains `AssetRef<MaterialAsset> Material`; inline fields stay as the default entry | Layout, persisted, additive | FD-18 |
| `SpriteComponent` gains `UvRect` and `SizePivot`; `SpriteBlendMode` gains `Cutout` | Layout, persisted, additive | Q8, Q9 |
| UI `Style` gains a font (`AssetRef<FontAsset>`) and weight; `UiText { Key, Args }` beside the string overloads | Layout, not persisted | AA2-MEDIA-05, MEDIA-06 (FD-31) |
| `ComponentInfo` gains the host-owned flag, replacing the hard-coded list | Layout, not persisted | AA2-ECS-02 steps 1-2 |
| `FieldKind` ordinals pinned with explicit values | Hash only | AA2-ECS-01 |
| Dead public APIs deleted | Hash only | AA2-CORE-04 |
| `/utf-8` for engine targets; hidden visibility for game modules | Hash only (build inputs) | AA2-BLD-04, HR-02 step 1 |

Left for the contract window of the work that builds them, since M6B does not need them: `GameServices::HasUserData` (AA2-GP-05 step 2), per-bus audio pause (MEDIA-07), the picking hit point and source (GP-08), 2D and analog input (GP-10), and moving host headers off the game include path (CORE-03). Each costs one more game rebuild then, which is cheaper than reserving fields for features not yet designed.

FD-16 covers saved data: added fields take defaults that reproduce today's output, nothing is removed or retyped, and quick-saves are memory-only.

### Tracked tests (minimal-tests rule)

One new case only: `SceneFileTests` loads a scene saved before B2 and checks that `MaterialComponent`'s inline values survive the additive change. Everything else in M6B is checked through captures and measurements, as M2 and M3 were.

### Threads and processes

| Work | Where |
| --- | --- |
| Render-world sync, material table upkeep, upload batch recording | Main thread, inside Extract and Render (measured; the job system only if FD-27's 100% animated case misses) |
| Texture decode and mip CPU work for runtime-only IDs (B6) | Job system, background priority |
| Shader compiles | The reload service's process lane: one DXC process per compile |
| Pipeline creation for a changed shader | A worker, published on the main thread between frames |
| Sidecar scan at startup | Main thread once; the reload service afterwards |

### macOS and Linux (M4)

Each contract stays within Vulkan 1.2 with descriptor indexing, which MoltenVK provides. Contract 8 makes a device below the limits fail with a reason instead of later. GPU mip generation needs `VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT` on RGBA8, which the format check covers. Nothing in M6B adds platform code outside the platform layer.

### Sprite budget, set before B5

From M6C measurement 7 (the UI overlay, 0.12-0.17 ms p95 per frame) and the 50k mesh path: 10k sprites, cut-out and blended mixed, must cost at most 1.0 ms of CPU and 1.0 ms of GPU per frame on the laptop in Release. B1 records today's path against the same scene, and B5 must beat it.

## Step order

Each step leaves nothing to replace. Steps that need an owner decision start after it.

*Revised 2026-11-27 with the B0 contracts:* the render-world grouping moves ahead of material keying, and the upload batch ahead of mips, because each later step builds on the earlier code (AA2-RND-02, RND-04).

| Step | Content | Uses |
| --- | --- | --- |
| B1 | **Sprite baseline:** the Samples Gallery's opaque and blended quads captured on today's path (replaces the 2D run, owner 2026-11-26), plus the 10k-sprite baseline for the budget | The 2D run decision |
| B2 | **Contract window and materials:** the contract changes above; `raven-project 3` with sidecars (contract 9); device limits and bindless classes (contract 8); the render-world grouping restructure (contract 3, including the 0.27 ms sync cost); then `.ravenmaterial`, `MaterialComponent` referencing it, and the keyed material table owning its texture leases (contract 6), with hot reload; samples regenerated | FD-18, FD-23 |
| B3 | **Textures:** the per-frame upload batch first (contract 5), then GPU mip generation into it, the sampler set, and per-texture samplers removed. VMA is vendored here, after the owner approves the download | FD-21, FD-24 |
| B4 | **Material shaders:** fragment ABI v2 and the vertex offset hook (contracts 1-2), shader assets with their build rule and per-shader pipelines (contract 7), one indirect draw per pipeline range, live reload | FD-19 |
| B5 | **Sprites on the GPU path:** sprites in the render world on the overlay instance format, cut-out GPU-culled, blended CPU-sorted from the render world | FD-20 |
| B6 | **Asset resolution:** runtime-only IDs on the job system with placeholders; the per-frame upload batch | FD-22 |
| B7 | **Multi-object FBX:** sub-asset records, `import-model`, rename reporting, hierarchy instantiation | FD-23 |
| B8 | **Exit check** with every measurement | All |

## Implementation record

### B1 — Sprite baseline (2026-11-27)

- **Images:** `scripts/Compare-Captures.py capture out/captures/m6b-b1-reference` (Release) recorded the first frames of Perspective, Gallery, Assemblies, Puzzle, Collection and Benchmark-500 on today's path. Every later step compares against these at the M2 tolerance; the Gallery's two opaque and two blended quads are the sprite reference. The folder is ignored evidence and is recaptured only from this commit's code.
- **Sprite scene:** `SamplesTool create-sprite-benchmark <output> 10000|0` builds a 100 x 100 wall of quads facing the camera, opaque and blended in a checkerboard with both sample textures, and a control scene with the same camera and light but no wall. `scripts/Benchmark-Windows3D.py sprites <label>` generates both into a snapshot, measures each three times alternately for 4 s at 1280 x 720, and reports the loaded scene minus the control. The frame trace gained a `sprites` column (sprites submitted per frame).
- **Today's cost of 10k sprites** (laptop, Release, 60 Hz FIFO, medians of three runs):

  | | p50 | p95 |
  | --- | --- | --- |
  | CPU preparation | 2.62 ms | 2.87 ms |
  | of which renderer prepare (cull, sort, batching) | 1.73 ms | 1.92 ms |
  | of which record and submit | 0.47 ms | 0.52 ms |
  | of which sprite gathering | 0.04 ms | 0.06 ms |
  | GPU | 0.45 ms | 0.89 ms |

  The wall draws in 2,648 instanced runs, because alternating textures and blend modes break runs. B5's budget (1.0 ms CPU and 1.0 ms GPU for the same scene) needs the CPU cost to fall by more than half; the GPU cost is already within it.

### B2, part 1 — the contract window (2026-11-27)

The game-contract changes of the window above, landed together:
- **Format pins:** `FieldKind` and every stored component enum carry explicit values, and `SceneFile.cpp` asserts its kind table follows them; FD-10 gained its version policy.
- **The host-owned flag:** `ComponentIsHostOwned` comes from `HostOwned<T>` in each `ComponentInfo`, replacing the hard-coded list type-erased callers used.
- **Build inputs:** every project target compiles with `/utf-8`; game modules build with hidden visibility and no longer refuse a non-Windows configure.
- **Additive fields:** `SpriteComponent::UvRect` and `SizePivot` (today's CPU path honours the size and pivot; `UvRect` and `Cutout`'s discard come with B5's GPU path, and `Cutout` draws like Opaque until then); `SpriteBlendMode::Cutout`; `MaterialComponent::Material`; `Ui::Style::Font` and `Weight`; `UiText` with `Ui::Localize`, which shows the key with its arguments until string tables exist; `AssetKind::Font` and `FontAsset`.
- **Removed:** `AssetResources`, the `PngFileWatch` class, the six `VulkanContext` entry points only it used and the camera-only `DrawFrame` overloads; `AudioPlayer` lost its uncached path and its catalog, and moved to `Private/Audio`.
- **Checked:** Debug and Release build; the 9 tests pass; all six captures are bit-identical to B1's; the Arena determinism check passes, its 600-tick hash now `36f045813e737e08` because the new fields are part of every captured scene.

### B2, part 2 — asset identity with the files (2026-11-27)

Contract 9, built:
- **Formats:** `.ravenasset` sidecars (`Private/Assets/ContentIndex.*`); scene files gained an asset section written first, which older readers keep as an unknown section, and `ReadSceneFileId` reads it from the first 64 bytes; `raven-sound 2` adds `id`. `raven-project 3` holds settings and the scene order; `content index` with `asset` records is a packaged player's generated index. Version 2 projects and version 1 sound events are still read.
- **Discovery:** a version 3 project scans its content root; development writes the sidecars of new imported files. The project watch now reads the project text plus a fingerprint of every asset file, so a new or edited sidecar republishes the project on the reload service's audit, with no watch per file.
- **Tools:** `RavenSceneTool upgrade-project` and `list`; `stage-player` writes the generated index, and now also the project's `fixed-step` and `physics-limits`, which the version 2 manifest had dropped. `SamplesTool` and `ArenaTool` write the IDs into what they generate (scene headers, sound events, clip sidecars); the benchmark and capture scripts read scene IDs through `list`.
- **Content:** both game roots were upgraded; their `list` output is identical before and after, and regenerating Arena's sounds reproduces the upgraded files byte for byte.
- **Checked live:** a PNG dropped into Samples' content during a session got its sidecar and the project republished in about two seconds. A copied PNG with its copied sidecar, sorting before the original, got the new ID while the original kept its own; a first version of the rule gave the new ID by path order and so broke the original's references. That bug was found by this check and fixed before commit, and the engine kept the last good world meanwhile. Copies with equal timestamps rewrite nothing and are reported.
- **Also checked:** the 9 tests; the Arena determinism hash is unchanged (`36f045813e737e08`); Arena's staged player project decodes; all six captures are bit-identical to B1's.

### B2, part 3 — device limits and bindless classes (2026-11-27)

Contract 8, built:
- **Selection:** device selection reads `VkPhysicalDeviceVulkan12Properties` and rejects a device whose update-after-bind sampled-image limits (per set or per stage) are below `BindlessTextureCapacity` (4,096), naming the limit in the "No Vulkan GPU meets" message beside the missing features.
- **Classes** (`VulkanTextureDescriptors`): element 0 is the context's white texture; elements 1–64 are the engine reserve; content takes 65–4095. `CreateManagedTexture` takes a `TextureUse` (Content by default); the text system's glyph pages use Engine, and textures the context creates itself are engine textures. A full class gives the texture element 0 and logs one warning per fill instead of throwing; replacing such a texture never overwrites white.
- **Release:** a freed element is pointed at white before it returns to its class's free list (AA2-RND-05 step 1), so a stale index shows white, never another texture.
- **Stats:** `VulkanResourceStats::BindlessEngine` and `BindlessContent`.
- **Checked:** a temporary probe (deleted) filled both classes, 4,100 content and 70 engine textures: 4,031 and 64 elements were used, each class logged once, and releasing everything returned both counts to zero, with freed elements reused. The 9 tests pass, and the six captures are bit-identical to B1's.
- **Found by the probe:** every texture still creates its own `VkSampler`, and the test laptop's driver allows 4,000 samplers, so content cannot actually reach the class limit before validation errors. B3 already removes per-texture samplers for the fixed sampler set, which removes this limit too; until then the practical texture limit is about 4,000.
- **Left to the macOS port:** logging MoltenVK's portability-subset features. Their structure exists only in Vulkan's beta header, which the build does not enable, and no Windows device reports the extension, so it cannot be built or exercised here. It is listed in [PlatformHandoff.md](PlatformHandoff.md).

### B2, part 4 — draw groups, the command index and the 0.27 ms (2026-11-27)

**Measurements 3 and 2, taken first.** `SamplesTool create-benchmark` takes a placement (`scattered`, the default and the realistic baseline; `contiguous`, the same animated fifth first in the scene file through sequential entity IDs; `all`), and `Benchmark-Windows3D.py scale --animation` passes it on. One same-day run on the test laptop, 50k objects, auto FIFO at 165 Hz, three runs each, before the restructure:

| Placement | Render-world sync p50 / p95 | World pass p50 / p95 | CPU preparation p50 / p95 | FPS |
| --- | --- | --- | --- | --- |
| Scattered fifth | 0.89–0.94 / 1.07–1.21 ms | 1.38–1.45 / 1.56–1.72 ms | 2.79–2.95 / 3.16–3.56 ms | 165.0 |
| Contiguous fifth | 0.60–0.61 / 0.71 ms | 1.13–1.17 / 1.38–1.39 ms | 2.12–2.17 / 2.48–2.51 ms | 165.0 |
| Every object | 2.32–2.35 / 2.59–2.65 ms | 4.19–4.25 / 4.56–4.76 ms | 7.95–8.02 / 8.57–9.04 ms | 121.7–123.3 |

- **The 0.27 ms is explained: change locality** (AA2-PERF-07). With the animated fifth contiguous in the file, the sync returns to the pre-M6 0.63–0.69 ms and the world pass to the pre-M6 1.07–1.19 ms; scattered reproduces M6A's 0.90–0.93 ms. M6A's random 64-bit IDs order scene rows randomly, so the changed objects are spread through the pools and every write misses cache. The code is not slower. Scene files are not reordered to hide it; scattered stays the baseline.
- **FD-27 misses:** with every object moving, CPU preparation is 8.6–9.0 ms p95 against the 3.5 ms budget, mostly the world pass (4.2 ms) and the sync (2.3 ms). FD-27's rule applies: tighten the single-threaded path first, then use the job system. The scattered p95 of 3.16–3.56 ms is also at the edge of the 3.5 ms budget. Both are taken up after B2's material table and measured again at B8.

**Contract 3, built:**
- **Groups** (`RenderWorldState.hpp`): `DrawGroup` keyed by `DrawGroupKey` (`DrawPipelineKey` of shader, blend, cull and offset, plus the mesh handle) in a hash map; `MeshHandle` gained a `std::hash`. Every lit object uses the default pipeline key until B4. A group whose count reaches zero is retired: it leaves the map and goes to a free list, and the next new key reuses its ID.
- **Tint-only writes:** each slot keeps the mesh reference it resolved, so a `StaticMeshComponent` write that keeps `Mesh` reuses the group's handle and never calls the resolver.
- **Revisions:** the world bumps `LayoutRevision` when a group is added, retired or reused and `CountRevision` when any count changes. The context bumps a mesh revision when managed mesh contents are swapped or released.
- **GPU** (`VulkanGpuScene`): the mesh table is a resident device buffer, resolved and uploaded only when the layout or mesh revision changes; before, every group was resolved every frame. Live groups get commands sorted by pipeline key, each table entry stores its `CommandIndex` (the former padding word), and Cull writes `Commands[mesh.CommandIndex]`. Templates live in their own buffer, uploaded when counts change and copied over the commands on the GPU every frame, because Cull accumulates instance counts. The frame issues one indirect draw per pipeline range; debug views draw every range with their one pipeline.
- **Checked:** a temporary CPU probe (deleted) showed a tint-only write resolving nothing and changing no revision, a new mesh reference resolving once, and removing every object retiring the group, which the next object reused. The 9 tests pass and the six captures are bit-identical to B1's. In Debug, with Samples running, replacing `Cube.fbx` with the sphere redrew every cube user (the boxes and the floor slab) through the resident mesh table, and restoring it brought the cubes back; the shader reload steps of the live check pass. The tracked live check (`scripts/Test-Windows3DLive.py`) does not run to its end: its screenshot comparisons predate M6C's on-screen problem overlay, and one awaited message changed in the review amendments. It found one real regression from part 2: a broken scene file made the project retry its import several times a second, each retry rescanning the content and repeating its warning. Both were reported for the owner's approval. *Fixed (owner, 2026-11-27, option A):* the project watch no longer retries a failed import, since its inputs already include the content fingerprint. With the active scene broken, the warning now appears once instead of several times a second (67 lines in one earlier run); restoring the file reloaded the scene in 0.17 s and republished the project. *Live check repaired (owner, 2026-11-27, option B):* the host takes `--no-warning-panel` (`ApplicationConfig::ShowWarningPanel`), which keeps problems in the log but off the screen; the live check, the benchmarks and the capture comparison pass it, and the live check waits for the current rejection wording. The unmodified live check then passed end to end in Debug: shader, vertex, compute, game-code, scene, PNG and FBX reloads (FBX 148 ms save to presented) and every rejection.

### B2, part 5 — material files and the keyed material table (2026-11-27)

Contract 6 and contract 1's record step, built:
- **`.ravenmaterial`** (`Private/Renderer/MaterialFile.*`, the extension decided by the owner on 2026-11-27): `raven-material 1`, the file's own `id`, `base-color` (four numbers, 0 to 64), `texture base-color <id>` and `lit`. Strict like `.ravensound`: an unknown, repeated or non-finite setting rejects the file with its line, and a reload keeps the last good version. `AssetKind::Material` is file-backed, carries its ID in its header and is a scene dependency, so content scanning, catalogs, scene loading (which also leases the material's textures), `stage-player` (`ExpandMaterialTextures`) and content watching all cover it. A material whose header names another ID than its record is refused.
- **The record** (`GpuMaterialData`, 64 bytes): base color, four texture elements, four sampler slots (the single clamp sampler until B3), flags (bit 0 lit), and a value offset and count for B4's values. `GpuScene.hlsli`, `GetBufferLayouts` and `Color.vert.hlsl` follow it. The rest of contract 1 (fragment inputs, `RavenFrame`, accessors) comes with B4's shaders.
- **The table** (`RenderWorldState.hpp`, `RenderWorld.cpp`): entries keyed by material file ID, or by an entity's inline values (color bits, texture, lit) when it has no usable file. Each entry holds the leases it was built from (the file and up to four textures), counts its objects, and returns its slot to a free list with its last user. Only a new key resolves leases, through one `RenderWorld::AssetResolver` that replaced the texture resolver. A reloaded file bumps its asset's revision (`AssetLease::GetRevision`); `Sync` rebuilds that entry's record, and a change of lighting rewrites the objects that use it, because their normal matrices depend on it.
- **Upload:** the GPU scene uploads the whole table when its buffer is new or must grow, and otherwise only the changed slots, as one copy region each. *Delegated change from the contract's "through Scatter":* a material edit changes a handful of 64-byte records, for which copy regions are simpler and as cheap; Scatter stays for object records.
- **Non-finite values:** an object whose `Tint` or inline color is not finite is hidden and logged once per entity; its values never enter the table.
- **Samples:** `Content/Materials/Paint`, `Grid` and `Signal` replace the generator's inline setups, with matching inline values as the fallback; the Puzzle, Collection, Physics and benchmark scenes were regenerated. Arena keeps inline values, so its determinism hash is unchanged.
- **Checked:** the 9 tests, including the new `SceneFileTests` case (a scene saved before the `Material` field loads its inline values unchanged, with no file); all six captures are bit-identical to B1's, now drawing Puzzle, Collection and Benchmark-500 through material files; staging Collection and Benchmark-500 copies the three materials and their textures. In a Debug session on Collection: editing Paint's color reloaded and changed the image; switching Signal to lit reloaded and rewrote its four objects (the samples' default light, ambient 1 and no sun color, draws lit and unlit alike, so this was confirmed from a temporary log); an unknown setting was rejected naming the file and line, keeping the last good version; restoring both files reloaded them.

### B3 — memory, the upload batch, the sampler and mip maps (2026-11-27)

- **VMA (FD-24):** v3.5.0, downloaded with the owner's approval and checked against the tag's blob IDs, vendored unmodified in `ThirdParty/VulkanMemoryAllocator` with `cmake/RavenVma.cmake` and `scripts/UpdateVma.cmake`. `VulkanMemory` (owned by `VulkanDevice`) is the only user; buffers, textures and the depth attachment allocate through it with their previous memory properties, and `VulkanResourceStats` gained the totals. Captures were bit-identical.
- **Upload batch (contract 5):** `VulkanUploadBatch`, recorded in [RendererDesign.md, R3](RendererDesign.md#r3--timeline-semaphore-frame-pacing-and-retirement). Texture creation no longer submits and waits; glyph pages are created cleared on the GPU instead of from a 4 MB zero buffer. Captures were bit-identical.
- **Sampler (FD-21):** one shared sampler, linear with mip blending, replaces the sampler every texture created; the textured pipeline's per-texture sets name it too. This also removes the 4,000-sampler driver limit found in B2 part 3: a temporary probe filled both bindless classes (4,031 content and 64 engine textures) with no validation error. Captures were bit-identical. *Delegated split:* the repeat sampler and the per-material choice through `GpuMaterialData::Samplers` come with B4, because choosing in the shader needs B4's fragment ABI v2; changing the sampler binding now would change the ABI ahead of that version.
- **Mip maps (FD-21):** content textures created from pixels get their full chain, blitted level by level with linear filtering in the batch after a format-feature check; engine textures (glyph pages, updated in place) keep one level. The four textured captures changed by design: 0.29–0.48% of pixels beyond 2/255, at distant textured meshes and the small demo sprites, which now show the texture's averaged color instead of aliasing specks. The capture baseline is now `m6b-b3-reference`; B1's stays as history.
- **Checked:** the 9 tests; the Debug live check with Vulkan validation and synchronization validation passed end to end, once with the batch and once with mip maps (shader, game-code, scene, PNG and FBX reloads and their rejections); a CI-equivalent check follows the commit. Measurement 6 (mip time per texture and texture memory) is taken at B8.

### B4, parts 1 and 2 — fragment interface v2 and material shaders (2026-11-27)

- **Fragment interface v2 (contract 1):** recorded in [GpuDrivenDesign.md, G4](GpuDrivenDesign.md#g4--bindless-lit-materials-behind-an-engine-header-q3): the material index and world position inputs, the frame uniform with `Time`, the material records and parameter blocks readable by fragment shaders, the sampler set (clamp, repeat) and the accessors; `ShaderInterface` checks every stage and `FragmentShaderInterface` is gone. The engine's lit shaders moved to it with bit-identical captures.
- **Parameters (FD-19, owner's texture-naming decision):** a material shader declares one struct of `float4` values and `RavenClampTexture` / `RavenRepeatTexture` textures, names it with `RAVEN_MATERIAL_PARAMETERS(Type)` and reads it with `RavenGetParameters(input)`; `RavenSample(texture, uv)` samples with the wrap the type names. `ReadMaterialParameters` reads the names and types from the compiled SPIR-V, which keeps them at every optimization level.
- **Shader assets (contract 7):** `AssetKind::Shader` (`.hlsl` with a sidecar, or precompiled `.spv`). Development compiles a shader when a scene loads it and again on every save, through `CompileHlsl`, the compile the engine's own programs now share; a candidate is checked against the lit fragment interface and reflected before it replaces the last good one. *Delegated change:* players get SPIR-V from `stage-player`, which compiles each shader into `<source>.spv` and records that file, instead of from a build rule; a build rule would put `.spv` files into development content, where the content scan expects sidecars. Players still ship SPIR-V only.
- **Material files:** `shader <id>`, `texture <Name> <id>` and `value <Name> x y z w`; the names are checked against the shader when the material is built, and a mismatch draws it with the engine's lit shader and one warning naming the setting. A material's dependencies (`ExpandMaterialDependencies`) are its shader and textures.
- **Table and pipelines:** each shader material packs its parameter block in the shader's order into `RenderWorld::State::Values`, aligned to its own size (`ValueOffset` and `ValueCount` in float4 units), uploaded whole when anything changes and compacted when moved blocks outweigh live ones. The material's pipeline key, from `VulkanContext::UseMaterialShader`, joins the draw-group key, so objects group by (shader, mesh) and each pipeline range draws with its pipeline; debug views keep their engine pipelines. A material or shader reload rebuilds the entry and regroups its objects when the key changes. Pipelines are rebuilt when the shader, the lit vertex shader or the attachment format changes, and retired through the timeline.
- **Pipeline creation:** measured on the main thread: 36–38 ms for the first build of a shader, 12 ms after a shader edit, under 1 ms when the driver's cache hits (the packaged player measured 0.4 ms). The load-time cost is accepted; off-thread creation and a pipeline cache wait for B8's numbers with more shaders (contract 7).
- **Sample:** `Content/Shaders/Stripes.hlsl` with `Content/Materials/Stripes.ravenmaterial` draws the Puzzle floor: world-space stripes over the repeating UV grid. The Puzzle capture changed by design; the capture baseline is now `m6b-b4-reference`, the other five unchanged.
- **Checked:** the 9 tests; in Debug with validation, a material value edit and a shader edit each reloaded live and changed the image, a broken shader was rejected keeping the last good, an unknown value name was reported and fell back, and restoring both files restored the image exactly, with no validation message; a `public-preview` player with no HLSL loaded Puzzle and built the shader's pipeline from its SPIR-V.
- **Known limit:** a material shader's watch follows its own file only; editing an engine include such as `Raven/Material.hlsli` recompiles engine programs but not material shaders until they are saved or the scene reloads.

### B4, parts 3 and 4 — sampler choice and the vertex offset hook (2026-11-27)

- **Per-material sampler choice (from B3):** delivered by the texture-naming decision: each texture parameter's type (`RavenClampTexture`, `RavenRepeatTexture`) picks its sampler, and the material record's sampler slots carry it for `RavenMaterialTexture`.
- **The hook (contract 2):** a material shader defines `RAVEN_OFFSET_BOUND` (metres) and `float3 RavenOffset(RavenOffsetInput input)` (`RavenOffsetInput`: local and world position and normal, UV, color, material slot); `RAVEN_OFFSET_NORMAL` with `RavenOffsetNormal` returns new normals. The engine's lit vertex program (`Color.vert.hlsl`) runs it after the object transform and clamps it to the bound. Such a shader also gets a vertex program, compiled from its source followed by `Color.vert.hlsl` (`CompileOffsetVertex`), checked against the lit vertex interface; its bound is read back from specialization constant 1 and must be above 0 and at most 100 m. Players get it from `stage-player` as `<source>.vert.spv`. Materials without the hook keep the engine's vertex program, and their captures are bit-identical.
- **Culling:** draw groups carry the bound, the mesh table passes it in `Center.w`, and `Cull.comp.hlsl` grows each object's world-space extent by it, so an offset never moves a vertex outside the bounds culling tests.
- **Bindless stage visibility:** textures and samplers are visible to vertex programs too, because `Material.hlsli` declares them in every lit program and Debug compiles keep unused declarations; this was found in Debug, where the stricter vertex check had rejected the engine's own lit vertex program and the offset program. A hook may sample with `SampleLevel`.
- **Sample:** `Content/Shaders/Wave.hlsl` with `Content/Materials/Wave.ravenmaterial` on a decorative sphere in the Physics scene: vertical waves (an offset along split normals would crack a mesh at its seams, which the first version of the sample showed).
- **Checked:** the 9 tests; all six captures unchanged; in Debug with validation the sphere's offset animated, a bound of 500 m was rejected keeping the last good version, a new bound reloaded, and no validation message appeared.

## Measurements

| # | Measurement | Gate |
| --- | --- | --- |
| 1 | 50k animated steady state, same-day A/B against M6C | No regression beyond noise |
| 2 | 50k with 100% animated (FD-27) | Recorded; decides FD-27's next step |
| 3 | Render-world sync at 50k animated, against M6A's 0.90–0.93 ms p50 | The 0.27 ms found or explained |
| 4 | Material and shader edit, save to visible | Within the M5 asset targets |
| 5 | 10k sprites, cut-out and blended, CPU and GPU time | Budgets set from B1's baseline |
| 6 | Mip generation time per texture at load; texture memory | Recorded |

## M6B exit checklist

1. Sprites and one custom material shader run on the GPU-driven path.
2. A multi-object FBX imports with stable IDs, and a re-export with a renamed part is reported, not silently broken.
3. The Gallery's sprites on the GPU path match B1's captures within the M2 image tolerance.
4. Materials, shaders and multi-object models hot-reload with last-good recovery.
5. Every measurement above is recorded; the 50k steady state shows no regression; the 3D captures stay within the M2 tolerance.
6. CI passes with warnings as errors in Debug and Release.
