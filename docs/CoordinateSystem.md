# RavenEngine coordinate system

Agreed on 2026-10-09. This is the world-space contract for future engine math, scenes, cameras, and imported assets.

## World axes

RavenEngine uses Blender's right-handed, Z-up world convention.

| Direction | World axis |
| --- | --- |
| Right | +X |
| Reference forward | +Y |
| Up | +Z |
| Ground plane | XY |

The basis satisfies `X cross Y = Z`. Positive rotations follow the right-hand rule; a positive quarter-turn around +Z turns +X toward +Y. ECS Transform rotation storage and order are specified below; imported rotation conversion remains a separate boundary.

Reference forward gives engine APIs and samples a common direction. It does not require every model or camera to face +Y in its local space.

## Cameras and 2D coordinates

The first camera implementation targets perspective projection and simple 3D geometry to support the Blender/FBX direction. Orthographic projection follows for the 2D sample. Both use the same world axes, object transforms, and camera view math; the projection defines how the view maps into clip space.

A reference camera at `(0, -distance, 0)` can look toward +Y with +Z as its up direction. ECS pose cameras use local +Y forward and +Z up; imported Blender cameras must be converted to retain their intended orientation.

For a side-view 2D sample, logical `(x, y)` maps to world `(x, 0, y)` on the XZ plane. A top-down sample uses the XY ground plane. These mappings keep ordinary 2D controls independent of the world-axis labels.

## Assets and the Blender bridge

Blender is the primary authoring tool and FBX is the primary planned 3D asset format. Matching Blender's world basis makes positions and transforms easier to inspect and exchange through the planned live bridge.

The static FBX importer reads the file's axis and unit metadata and converts mesh positions to this world convention in meters. Export settings can change the file's basis. A future live bridge must declare its transmitted basis and units explicitly as well.

## Renderer boundary

World axes do not define Vulkan clip-space depth, framebuffer Y direction, or matrix memory layout. Handle projection and screen conventions at the renderer boundary instead of changing scene coordinates.

The perspective sample now supplies 3D vertex positions, object models, and a camera to the renderer. Its mesh assignments and animation live in `Samples/PerspectiveSample.cpp`; the renderer records borrowed draw submissions and owns their uploaded meshes until shutdown.

## Implemented transform and projection contract

- C++ `Mat4` stores four contiguous columns and multiplies column vectors. `clip = projection * view * model * local`; the rightmost transform applies first. A model composed as `Translation * Rotation * Scale` scales, rotates, then translates.
- `LookAtRH` maps the camera's forward direction to view-space -Z. The default world up is +Z; world right maps to view right.
- The perspective camera uses vertical field of view in radians. Defaults are 60 degrees, near plane `0.1`, and far plane `100`, in engine distance units. The renderer supplies the actual swapchain extent to the camera builder for each draw.
- The projection maps near/far depth to `0`/`1`, matching [Vulkan depth conventions](https://docs.vulkan.org/guide/latest/depth.html). It negates projected Y for the existing positive-height viewport, keeping world/camera up toward the screen top.
- Mesh draws use a 176-byte per-frame vertex-instance record with explicit clip and normal columns, tint and lighting. Sprite draws use their 80-byte clip/tint instance record. HLSL consumes explicit columns in both paths. See [mesh materials](MeshMaterials.md) for layout and normal-transform rules.
- Opaque color geometry uses depth clear `1` and comparison `LESS`. Every swapchain framebuffer owns its own depth image, memory, and view; they are recreated with the swapchain extent after GPU work completes.

The math functions reject invalid projection parameters and degenerate camera directions. ECS Transform stores XYZ radians and applies local X, then Y, then Z rotation (Rz * Ry * Rx with column vectors). Engine distance units for imported assets and imported camera conversion remain separate future decisions.

## ECS camera pose foundation

`ViewFromPoseRH(position, rotation)` converts a Transform pose to view space. At zero rotation, local +Y maps to view -Z and local +Z maps to view +Y. It reads position and rotation only, so zero, negative or nonuniform Transform scale does not change the camera view. Orientation is evaluated at the origin and followed by negative-position translation.

CameraComponent retains projection mode, vertical FOV in radians, full orthographic height and shared clipping distances. `GetCameraView(transform, camera, aspect)` combines the Transform pose with the active projection. `CameraView` contains `View` and `Projection`. The renderer computes `Projection * View` for culling and drawing and uses `View` for transparent depth sorting.

VulkanContext supplies the prepared swapchain width and height to the camera builder. Both samples read the selected camera's Transform and projection settings from Registry. Perspective ground picking uses top-left framebuffer pixels and the same view, projection and extent that were used to present the frame. See the [renderer handoff and validation](EngineModules.md#camera-pose-projection-and-renderer-handoff).

## Orthographic camera and 2D sample

`CameraComponent::OrthographicHeight` sets the full visible height in world units, with a default of `6`. The visible width is `OrthographicHeight * aspect`. Resizing preserves vertical world extent and adjusts horizontal extent without stretching sprites. Zoom changes the height. Near and far are forward distances (`0 <= near < far`), with defaults of `0.1` and `100`, mapped to Vulkan depth `0` and `1`. Projection Y follows the same positive-height viewport convention as perspective.

The primary camera starts at `(0, -10, 0)` and the second starts at `(4, -10, 0)`. Both face +Y with +Z up. The side-view sample maps logical 2D coordinates `(x, y)` into world `(x, 0, y)` on the XZ plane. World Y separates sprite depths. Panning updates the selected camera’s Transform position and preserves its rotation.

Run `RavenEngine.exe --orthographic`; the default executable or `--perspective` retains the 3D sample. Arrows pan, wheel/PageUp/PageDown zoom, and Home resets the selected camera. F1 switches the blue-tinted transparent sprite between automatic depth order and explicit order. F2 switches cameras and preserves each camera’s pose and zoom. Two upper sprites at different depths retain equal projected size; a third opaque sprite starts outside the view and enters when the view is widened or panned. Overlapping lower sprites exercise straight-alpha composition. Sprite batching is still pending.

All three `DrawFrame` forms are available for either camera type. Their public declarations/definitions match; existing perspective symbols are preserved. Camera choice uses static dispatch into the same private frame implementation, which obtains matrices using the actual swapchain aspect ratio. The existing draw queue and pipelines receive matrices and do not depend on the projection type. No virtual camera base or OS headers are introduced.

Verified on Windows on 2026-10-11: Debug/Release builds, 38 focused math/frustum/DLL checks and 13 public frame calls per configuration, all three overloads for both cameras, invalid-camera rejection and recovery, Vulkan depth/Y conventions, zoom, portrait aspect, depth-invariant size, all six frustum planes and touching/spanning bounds. Both DLLs retain all 21 previous exports and add three orthographic overloads (24 total). Debug core/synchronization validation is clean. Native Debug computer use verifies equal-sized sprites, pan/zoom, reset, transparent order, offscreen-sprite entry after widening the view, maximize, minimize/restore, and normal shutdown. Portrait aspect is checked mathematically; the native checks use the normal and maximized window sizes. No permanent test source or CTest target is added.

Evidence: `out/validation/orthographic-*.log`, `out/validation/orthographic-exports-*.txt`, and `out/screenshots/ravenengine-orthographic-debug.jpg`.

## Sample controls and verification

Arrow keys orbit the camera, the wheel changes camera distance, Home or physical R resets the view, and Space pauses/resumes object animation. The sample draws an orange cube, a farther cyan cube, and an XY ground slab through one shared mesh with separate models and tints. The near cube is submitted first to exercise depth rejection of later geometry.

The texture extension adds two opaque PNG quads and two overlapping straight-alpha quads. `F1` or physical `O` toggles the blue-tinted transparent quad's explicit order. Transparent sorting uses the transformed mesh center's view-space depth, independent of the world's +Z-up convention. Visibility selection uses the current camera's Vulkan clip volume; see [TextureResources.md](TextureResources.md).

Verified on Windows on 2026-10-10: Debug/Release builds and HLSL/SPIR-V validation, 59 temporary CPU checks for transforms/projection/clipping/layout/invalid inputs, and computer-use checks of animation, orbit, zoom, Home reset, resize, minimize/restore, and normal shutdown. Debug runs with core and synchronization validation and reports no warnings/errors. Release also runs from a different working directory. The R shortcut was not verified by computer use because synthesized letter input did not reach the existing physical-key mapping.

Evidence is under ignored `out/validation/perspective-*.log` and `out/screenshots/ravenengine-perspective.jpg`. The temporary CPU probe was compiled from stdin and removed after validation; no test source or framework was added to the project.

## Hierarchical 3D transforms

Perspective scene draws now use Scene world matrices: `parentWorld * localTRS`, recursively, with no world-TRS decomposition. Bounds/culling and normal transforms consume the same matrix, including shear from nested nonuniform scale and reflected roots. The parented camera's position uses that affine chain; its rigid view axes compose rotations only and ignore scale/reflection. Ground picking uses the presented rigid view and viewport. Orbit-target and controller edits remain in the camera parent's local coordinates. See [hierarchy and camera policy](SceneWorkflow.md#runtime-hierarchy-contract) for destruction/reparent behavior and validation limits.

The 3D interaction game picks exact two-sided scene-mesh triangles after world AABB rejection, using complete affine models and the presented camera's near/far range. CPU picking geometry is replaced and retired with the corresponding GPU mesh data. See [interaction picking and scene access](InteractionGame.md#scene-access-and-gameplay-ownership).
