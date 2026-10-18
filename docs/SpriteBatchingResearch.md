# Sprite representation and batching research

Research date: 2026-10-11. This design review compared the RavenEngine code at the time with official documentation and public engine sources without changing implementation.

This note records the baseline before batching was implemented. Adjacent compatible textured instancing has since been implemented; see [TextureResources.md](TextureResources.md) and [TODO.md](../TODO.md) for the current contract and completion status. The implementation candidates below remain part of the historical review.

## Scope and conclusions

The commercial-engine comparison covered Unity, Unreal Engine's Paper2D, and GameMaker. Godot and Defold provided comparisons with inspectable implementations. For BlackSpace, the review separated facts supported by Pearl Abyss and PlayStation public material from unknown details. It does not claim access to every proprietary engine's internals.

Unity references use the 6.0 documentation; Godot references use the `4.6-stable` RD renderer source. Unreal references use the Paper2D official documentation/API available during the review, and GameMaker references use its monthly manual. Defold implementation descriptions apply only to the `dev` source read on the research date and do not establish identical behavior across all released versions.

**Sprite assets, individual sprite submissions, and GPU batches have different responsibilities.** Representing a sprite as a rectangle does not establish that all sprites share one GPU mesh or render in one draw call.

An earlier explanation incorrectly generalized the sample's shared quad into an engine-wide requirement. Unity, Unreal, and Defold also support sprite geometry beyond rectangles. Quad instancing is a useful optimization path, but need not define every sprite.

## Findings by engine

| Engine | Sprite representation | Batching findings | Design considerations |
| --- | --- | --- | --- |
| Unity | Supports `Full Rect` rectangles, alpha-contour `Tight` meshes, and custom outlines. | Sprite Atlas packs images into a shared texture. SRP Batcher separately reduces CPU state-setup costs between draws. | Atlas use or the SRP Batcher name does not establish GPU instancing or a single draw for every sprite. |
| Unreal Paper2D | Sprite assets contain render geometry, including rectangular, outline-based, custom, and Diced forms. | `UPaperGroupedSpriteComponent` handles instances of multiple sprite assets. `UPaperSpriteAtlas` enables draw combination through shared textures. | A grouped actor renders/culls its entire group as one unit. The public API's term "instance" does not by itself prove hardware GPU instancing. |
| GameMaker | Developers request sprite drawing; the engine packs images into Texture Pages. | The manual describes vertex batches. Blend/shader/shader-constant changes and some shape drawing break batches. | Developers need not create a shared quad mesh themselves. Texture-page organization and state changes affect batching efficiency. |
| Godot | The 2D renderer distinguishes rectangle, nine-patch, polygon, mesh, and other commands. | The reviewed RD rectangle path writes individual color/transform/region data into an instance buffer and draws using the batch instance count. Polygons have a separate draw path. | Rectangle-specific instancing can coexist with other geometry paths. Texture, material, and clip differences split batches. |
| Defold | Supports the default rectangle, contour trimming with 4-8 vertices, and slice-9. | Reviewed code writes batch vertices/indices into common buffers on the CPU. Documentation defines when adjacent sorted items combine into one draw. | Vertex batching need not assume one identical sprite mesh. Differences in values supplied as shader constants, such as default tint, can also split batches. |
| BlackSpace | No public evidence establishing its sprite asset/API structure was found in this review. | Official PlayStation material confirms PS5 use of NGG Culling and Geometry Shader Oversubscription. | These facts do not establish shared sprite meshes, vertex batching, instancing, atlases, or transparent sorting. |

### Unity

Sprite shape is part of import settings. `Full Rect` creates a rectangle; `Tight` creates a mesh fitted to pixel alpha. Asset meaning and GPU mesh sharing therefore need separate consideration. See [Sprite Import Settings](https://docs.unity3d.com/6000.0/Documentation/Manual/texture-type-sprite.html).

Sprite Atlas lets multiple images share a texture, increasing opportunities to combine draws. The official documentation states that SRP Batcher can improve performance even without reducing draw count. See [Sprite Atlas introduction](https://docs.unity3d.com/6000.0/Documentation/Manual/sprite/atlas/atlas-introduction.html).

SRP Batcher reduces state-setup costs between compatible shader draws; it should not be described as vertex merging or GPU instancing. The 6.0.0b11 release notes also record a dynamic-batching fix for URP Sprite Renderer materials that are not SRP compatible. This illustrates conditional rendering paths, rather than establishing every Sprite Renderer's internal algorithm. See [SRP Batcher](https://docs.unity3d.com/6000.0/Documentation/Manual/SRPBatcher.html) and the [6.0.0b11 release notes](https://unity.com/releases/editor/beta/6000.0.0b11).

### Unreal Paper2D

The editor handles `Source Bounding Box`, `Tight Bounding Box`, `Shrink Wrapped`, `Fully Custom`, and `Diced` render geometry. The asset API's `BakedRenderData` stores triangle vertices with positions and UVs. This does not restrict every sprite to one identical quad mesh. See [Sprite Editor](https://dev.epicgames.com/documentation/en-us/unreal-engine/paper-2d-sprite-editor-in-unreal-engine) and [UPaperSprite](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Paper2D/UPaperSprite).

The grouped component accepts instances with sprite assets, transforms, colors, and materials. The grouped actor documentation describes the whole group as one rendering/culling unit. Reduced per-sprite culling precision for large groups is a design cost inferred from that contract. See [Grouped Component](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Paper2D/UPaperGroupedSpriteComponent) and [Grouped Actor](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Paper2D/APaperGroupedSpriteActor).

The Atlas API enables draw combination through shared textures; the API labeled the class Experimental at the time of review. Public APIs alone did not establish every internal GPU submission mechanism for grouped components or the automatic batching scope of ordinary Sprite Components. See [UPaperSpriteAtlas](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Paper2D/UPaperSpriteAtlas).

### GameMaker

The engine packs sprite, tile, and font images into Texture Pages, trimming empty transparent borders by default. This reduces stored image regions; it does not establish the same geometry as Unity's Tight polygon meshes. See [Texture Pages](https://manual.gamemaker.io/monthly/en/Settings/Texture_Information/Texture_Pages.htm).

The manual describes texture swaps and vertex batches separately. Vertex data from multiple drawing requests is sent in batches, which break on blend, shader, shader-constant, and other changes. The review did not infer every backend's low-level calls from that public explanation. See [Texture Swaps and Vertex Batches](https://manual.gamemaker.io/monthly/en/Additional_Information/Best_Practices_When_Programming.htm).

### Godot

These findings concern `RendererCanvasRenderRD` in `4.6-stable`. Rectangles and nine-patches use instance buffers and common rectangle indices. Each rectangle records transform, color, source UV region, and destination region; the batch's `instance_count` is passed to the draw. Polygons instead submit their own vertex/index buffers. Material, texture/filter/repeat settings, clip state, and command type can start a new batch. This is an implementation example of rectangle-specific optimization coexisting with general geometry. It does not establish identical behavior in other versions or the Compatibility backend. See the [RD renderer source](https://github.com/godotengine/godot/blob/4.6-stable/servers/rendering/renderer_rd/renderer_canvas_render_rd.cpp).

### Defold

An atlas's Sprite Trim Mode can retain the default rectangle or select a 4-8 vertex shape that reduces transparent regions. Trimming cannot be combined with slice-9. See [Atlas image properties](https://defold.com/manuals/atlas/).

Documentation describes combining adjacent sorted items into one draw when they share collection proxy, component type, texture, material, and shader constants. If tint is implemented as a constant, different values split batches. See [Draw Calls and Batching](https://defold.com/manuals/render/).

In the `dev` source read on the research date, `RenderBatch` uses `CreateVertexData` to write common vertex/index data and upload it to dynamic buffers. Rectangles and trimmed shapes can have different vertex counts. Each sprite registers a render-list visibility callback that checks its bounding sphere against the frustum. This demonstrates that batching does not require culling only an entire group. See the [sprite component source](https://github.com/defold/defold/blob/dev/engine/gamesys/src/gamesys/components/comp_sprite.cpp).

## BlackSpace: established facts and unknowns

Pearl Abyss's 2025-03-28 GDC introduction describes water, fluid-based volumetric fog, atmospheric scattering, GPU cloth/hair simulation, and ray tracing. It is not technical documentation of sprite geometry, batch keys, or instance-data layout. See [Pearl Abyss Dev Archives](https://crimsondesert.pearlabyss.com/en-us/News/Notice/Detail?_boardNo=40).

PlayStation material dated 2026-03-04 reports NGG Culling and Geometry Shader Oversubscription in Pearl Abyss's PS5 optimizations. The platform and named techniques can be established, but the material does not describe sprite batching. See the [official PS5 feature](https://blog.playstation.com/2026/03/04/crimson-desert-hands-on-report-four-hours-in-the-rpgs-massive-open-world/).

| Question | Research result |
| --- | --- |
| Does one engine-wide quad represent every sprite? | No public evidence was found to establish this. |
| Are sprites batched through vertex merging or GPU instancing? | No public evidence was found to establish this. |
| Which texture/material conditions split batches? | Unknown. |
| Which sorting or OIT algorithm handles transparent sprites? | Unknown. |
| Are PS5 geometry-culling techniques public? | NGG Culling use is confirmed; the per-sprite culling structure is unknown. |

Claims from third-party analyses or anonymous posts about Nanite-like clusters, particular instancing structures, or vertex removal were not accepted as evidence. Earlier Black Desert engine material was not treated as proof of the same BlackSpace implementation. Unknown does not mean absent.

## Implementation tradeoffs

The following are design judgments for Raven based on the examples reviewed, rather than measured performance results for a particular engine.

| Approach | Benefits | Costs or limits |
| --- | --- | --- |
| GPU instancing of identical meshes | Collects individual transforms/colors without rewriting repeated mesh vertices. Works with any shared mesh resource, not only quads. | A simple instanced draw requires identical geometry. Different meshes cannot be merged merely because they share a material. Instance uploads and per-frame lifetime management are required. |
| Dynamic vertex/index batching | Different sprite shapes can occupy common buffer ranges when vertex formats and render state match. | Requires CPU vertex construction/transformation, uploads, index-range handling, and buffer-capacity management. It does not fit directly into Raven's existing GPU-only mesh ownership model. |
| Reduced state-setup cost | Can help draws whose geometry is difficult to combine. | Does not itself reduce draw count. Measure its effects separately from instancing and vertex batching. |
| Texture Atlas | Lets different source images use one GPU texture and improves batch compatibility. | Requires padding, filtering, mip-boundary handling, packing, page lifetime, and reload management. It does not remove ordering constraints or other render-state differences. |

Rectangles have few vertices and simple instancing. Alpha-contour geometry may reduce fragment processing in transparent regions but increases vertex counts and asset-processing costs. Measure CPU draw-preparation costs and GPU pixel work to determine which is faster. Fewer batched draws do not automatically reduce the number of overlapping pixels processed.

## Raven baseline at review and recommended direction

`VulkanTexturedDraw` references an arbitrary `VulkanTexturedMesh` and `VulkanTexture`. `CreateTexturedMesh` accepts caller-supplied triangle data. Sharing `m_Quad` is a choice made by `OrthographicSample`.

At the time of this review, the queue performed frustum checks using each submission's mesh bounds. Transparent entries were sorted by `SortLayer`, `SortOrder`, camera depth, and submission order, and the render pass drew each item with `instanceCount = 1`. That implementation had no contract requiring every mesh to be a rectangle.

The recommended responsibilities are:

| Responsibility | Information owned |
| --- | --- |
| Sprite asset | Image/texture region, size, pivot, and optional render geometry; design alongside asset identity. |
| Individual render submission | Asset reference, transform, tint, layer, and sort values; game code stays independent of GPU buffer arrangement. |
| Renderer | Visibility selection, final ordering, compatible batches, per-frame GPU data, and actual draws. |

The smallest implementation candidate at that baseline was **instancing consecutive queue entries that share mesh, texture, and pipeline**. This preserves arbitrary mesh support without requiring all sprites to share a mesh. Moving transforms and tint into per-instance data lets different colors share a batch. Different mesh resources remain separate, so this has a narrower scope than a complete Sprite Batcher that automatically combines different sprite shapes.

A future dedicated sprite asset/API could use internal shared quad geometry and UV regions. Outlined or deformed shapes could use the existing mesh path, with vertex batching added if measurements justify it. The dedicated API should still avoid assuming every sprite shape is a rectangle.

Preserve this processing order: **per-item visibility checks -> final render ordering -> consecutive compatible batches -> GPU submission**. For a transparent texture sequence `A, A, B, A`, the default batches are `[A,A] [B] [A]`. Moving the last A forward to reduce draws can change compositing. An atlas that supplies the same texture can allow larger batches while preserving that order, provided other state is compatible.

Large permanent groups culled only by group bounds were not recommended as Raven's initial default. Keeping individual visibility checks and batching the surviving items within the frame preserves both batching and per-item culling. If CPU culling becomes a measured bottleneck, review spatial partitioning or GPU visibility separately.

Verify that batching preserves the image, reduces actual draws for compatible repeated submissions, and correctly handles different textures/meshes, transparent order, and offscreen items. The small two-texture samples alone cannot establish a general batching performance improvement.

The original review documented research and implementation candidates only; it changed no engine code or roadmap/TODO completion states. The current implemented baseline is documented separately in the links at the start of this note.
