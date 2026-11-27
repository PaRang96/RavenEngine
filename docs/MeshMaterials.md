# Mesh materials and scene lighting

> **Since M6A (2026-11-24):** this game lives in `Games/Samples` ([EngineModules.md](EngineModules.md#game-roots-m6a)), its components are game components (`Samples::...`), and scenes use the generic format ([SceneWorkflow.md](SceneWorkflow.md#scene-files)). Sections that name `SceneDocument`, `PerspectiveLevel`, `GameBindings` or `Private/Game` describe the layout before M6A.

Perspective and Gallery use the same imported-mesh, ECS, renderer and development reload path. F4 follows the project scene order, including Assemblies and Puzzle. F3 restarts the selected level in free-fly mode.

## Windows launch and live files

Build the host after adopting shared component, vertex and handle definitions. The [scene workflow](SceneWorkflow.md) provides complete Debug/Release commands and current source-root behavior.

~~~powershell
Set-Location -LiteralPath 'D:\RavenEngine'
& 'D:\RavenEngine\out\build\x64-debug\RavenEngine\RavenEngine.exe' 
~~~

The development run logs its configured absolute source paths and reads the active level under the game's `Games/Samples/Content/Levels`, independent of the working directory. A valid save restarts the attempt automatically. F3 still restarts manually; F4 follows the current family's scene records in `Content/Project.ravenproject`. Ordinary runs use executable-relative staged Content.

Material PNG and FBX workers follow acquired asset UUIDs and source paths from the project catalog. New supported assets and scenes can be added through files and metadata while the development host runs. Source replacements preserve existing runtime identities and game state; level values change by restarting the authored attempt. Compatible `Private/Game/RavenGame.cpp` body edits use the existing game watcher.

## Level records

Current 3D levels use the binary SceneDocument codec and [save/export workflow](InteractionGame.md). The text records below document legacy import through RavenSceneTool; the runtime does not parse them. A document without material assignments retains unlit white-texture defaults.

| Record | Fields |
| --- | --- |
| material | Unique name, `lit` or `unlit`, base RGBA in [0, 1], texture UUID or `-` |
| use-material | Mesh name, material name |
| lighting | Ambient RGB, world-space direction in which light travels, directional RGB |

Each mesh has at most one material assignment. Material and mesh records may appear in either order. Missing material names, wrong asset types, unknown asset IDs, missing texture files, invalid colors, zero light directions and unsupported transforms reject the candidate. Light colors are finite nonnegative linear RGB values. Defaults are ambient white, downward light direction and zero directional color.

~~~text
material painted lit 1 0.7 0.4 1 -
material grid lit 1 1 1 1 7a590cee-ec82-4a27-927f-860e64619c19
use-material cube painted
use-material floor grid
lighting 0.18 0.20 0.25  -0.3 0.4 -1  0.95 0.88 0.78
~~~

`Perspective.ravenscene` has three meshes. `Gallery.ravenscene` uses four meshes, different materials and lighting, and a reflected, nonuniform object scale. Both use the existing camera controller, beacon outcome, win cue and restart logic. G swaps geometry between the controlled mesh and beacon while their entity material values stay with their entities.

## Shared engine contracts

`MaterialComponent` retains base RGBA, a typed texture reference and a lit flag through `RAVEN_COMPONENT`. `LightingComponent` retains ambient RGB, light direction and directional RGB on a level-owned entity. Registry stores their copied fields under the existing retained-value policy. File-local material names resolve to those copied component settings. Texture UUIDs resolve through the project catalog and scene-owned AssetStore leases.

`SceneLighting` is the renderer's value snapshot. The CameraViewBuilder DrawFrame overload accepts it alongside a `RenderWorld`. Since M3, lit meshes come only from the render world, which mirrors `StaticMeshComponent`, `WorldTransformComponent` and `MaterialComponent`, and since M6B's B5 sprites do too (`SpriteComponent`). Each distinct material becomes a GPU material table entry naming a bindless texture element. A material without a texture uses element 0, the context's white texture. See [GpuDrivenDesign.md](GpuDrivenDesign.md). Texture and mesh ownership checks run before frame acquisition.

The mesh pass is opaque and writes depth. Its albedo is vertex RGB multiplied by material base RGB, mesh tint RGB and sampled texture RGB. Lit shading multiplies albedo by ambient RGB plus directional RGB times the clamped dot product of the normalized world normal and the opposite light direction. SRGB textures decode during sampling. The SRGB swapchain performs output encoding. The mesh pass outputs alpha 1.

ColorVertex retains its position and color fields and adds normal XYZ and UV, giving a 44-byte stride. Existing source aggregate initializers receive a +Z normal and zero UV defaults. The importer applies axis and unit conversion, transforms normals with the inverse transpose of the FBX geometry transform, normalizes them, and preserves winding under reflection. ufbx generates missing normals. Deduplication considers the full vertex tuple, preserving normal, UV and color seams. FBX V coordinates convert to the engine's image-top convention. Missing source UVs use (0, 1).

The renderer uses the model's inverse-transpose linear transform for normals, including nonuniform and reflected scale. A lit object whose transform is singular (for example a zero scale) or whose normal transform leaves the float range is hidden instead, because it rasterizes nothing; authored scenes still reject such transforms at load. Each fence-protected frame slot owns a reusable mesh-instance vertex buffer with a 176-byte record. The same prepared camera matrix drives culling and drawing. The sprite pipeline continues to use its own 80-byte instance layout.

## Resource lifetime and rejection

The asset cache keeps renderer-scoped mesh handles and texture objects stable. Each used PNG can have one settled-file worker. File workers parse immutable snapshots and publish only current candidates. Invalid images, meshes and levels preserve the last working result and report the path and cause.

Level creation resolves and validates references before replacing active entities. Old level cleanup destroys only owned entities. The audio service and asset cache serve both levels throughout switching and compatible DLL replacement. Mesh replacements retain their superseded buffers in the submitted frame slots until their fences complete. Texture replacement reuses the descriptor after its upload fence covers earlier queue use, then releases the old image resources. Scene leases release unused resources and stop workers; shared resources remain until their last owner leaves. Managed mesh/texture slots carry generations, and individual descriptor sets are safely retired and reused. Legacy renderer-created borrowed resources retain context lifetime. See [the current lifetime contract](SceneWorkflow.md#runtime-handles-and-gpu-retirement).

## Windows verification

Debug and Release builds and SPIR-V validation passed. Focused importer checks cover the shipped cube and sphere and an ASCII FBX fixture with generated normals, a UV seam, rotation, nonuniform scale and reflection. The imported normals agree with triangle winding. CPU checks verify normal orthogonality and singular-transform rejection.

A GPU reference check renders a known normal under nonuniform and mirrored transforms. Both presets produce center pixels 135, 63 and 135 for the three lighting cases, matching the expected linear lighting followed by SRGB encoding.

Native state checks cover copied material values across compatible DLL replacement, changed gameplay behavior, defaults for the previous level format, invalid materials and light settings, unknown texture IDs and missing texture files. After warming both levels, 24 switches create no buffers, images or descriptor sets. Traces cover repeated image and mesh replacement and return to zero live buffers, images, image views, samplers, graphics pipelines and descriptor pools at shutdown. Weak references verify that replaced mesh buffers survive their submitted frames and are released after the two frame slots complete.

Stock runs in both presets show live lighting, base-color, PNG, FBX and gameplay changes in one session. Bad level, PNG, FBX and C++ saves preserve working pixels, and corrected saves recover. Both beacon levels complete and play their cues. F3, F4, F6, standalone orbit, Courtyard, portrait resizing and minimize/restore pass native checks with Vulkan core and synchronization validation. Windows endpoint capture confirms one ascending cue from each level's completion.

Local probe sources, logs, traces, screenshots and capture-dependent timings are under ignored `out/validation/materials-lighting/`. Temporary gameplay, staged level, source PNG and source FBX edits were restored. Native macOS/Linux verification and tracked regression coverage remain open. Persistent catalog authoring and scoped resource release are now implemented and validated in the [scene workflow](SceneWorkflow.md); broader import dependencies and general scene serialization remain open.
