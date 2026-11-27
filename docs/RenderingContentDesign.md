# M6B design note: rendering and content

Status:
- **Decided (2026-11-26).** The five decisions that needed the owner (they change what designers or players see, or the scope) are settled; they are listed in [Decisions for the owner](#decisions-for-the-owner). The technical decisions are delegated and recorded in [Delegated decisions](#delegated-decisions), judged by business alignment and expandability.
- M6C (the previous stage) is implemented and waits for the owner's single review, together with M6A and the review amendments ([GameServicesDesign.md](GameServicesDesign.md#m6c-exit-checklist-results-2026-11-25)).

This note settles FD-18 to FD-28 from [FutureDecisions.md, section 6](FutureDecisions.md#6-m6b--rendering-and-content) for M6B, the assessment's steps M6-9 and M6-10. Every "Today" statement was re-checked against the code at `0f593d6` on 2026-11-26.

---

## Reviewer guide

### Decisions for the owner

| # | ID | Question | Recommendation |
| --- | --- | --- | --- |
| 1 | [FD-18](#fd-18--material-model) | How designers author materials | **Decided 2026-11-26 (owner): (b).** A material **file** shared by many objects, referenced by ID; per-object tint stays |
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
- **Cut-out sprites** (opaque or alpha-tested) are GPU-culled like meshes, with sort layers mapped to depth.
- **Blended sprites and blended meshes** keep exact back-to-front order: the visible ones are sorted on the CPU from render-world data, as today, so nothing changes on screen.
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

## Step order

Each step leaves nothing to replace. Steps that need an owner decision start after it.

| Step | Content | Uses |
| --- | --- | --- |
| B1 | **Sprite baseline:** the Samples Gallery's opaque and blended quads captured on today's path (replaces the 2D run, owner 2026-11-26) | The 2D run decision |
| B2 | **Material files:** `.ravenmaterial`, the `material` project record, `MaterialComponent` referencing it, the material table keyed by asset ID and owning texture leases, hot reload; samples regenerated | FD-18 |
| B3 | **Textures:** GPU mip generation, the sampler set, per-texture samplers removed | FD-21 |
| B4 | **Material shaders:** shader assets, the parameter record, draw grouping by (pipeline, mesh), one indirect draw per pipeline, live reload; the render-world sync rewrite and its 0.27 ms | FD-19 |
| B5 | **Sprites on the GPU path:** sprites in the render world on the overlay instance format, cut-out GPU-culled, blended CPU-sorted from the render world | FD-20 |
| B6 | **Asset resolution:** runtime-only IDs on the job system with placeholders; the per-frame upload batch | FD-22 |
| B7 | **Multi-object FBX:** sub-asset records, `import-model`, rename reporting, hierarchy instantiation | FD-23 |
| B8 | **Exit check** with every measurement | All |

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
