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

The basis satisfies `X cross Y = Z`. Positive rotations follow the right-hand rule; a positive quarter-turn around +Z turns +X toward +Y. Euler angle order and rotation storage are separate decisions.

Reference forward gives engine APIs and samples a common direction. It does not require every model or camera to face +Y in its local space.

## Cameras and 2D coordinates

The first camera implementation targets perspective projection and simple 3D geometry to support the Blender/FBX direction. Orthographic projection follows for the 2D sample. Both use the same world axes, object transforms, and camera view math; the projection defines how the view maps into clip space.

A reference camera at `(0, -distance, 0)` can look toward +Y with +Z as its up direction. Camera-local axes are a separate contract; imported Blender cameras must retain their intended orientation.

For a side-view 2D sample, logical `(x, y)` maps to world `(x, 0, y)` on the XZ plane. A top-down sample uses the XY ground plane. These mappings keep ordinary 2D controls independent of the world-axis labels.

## Assets and the Blender bridge

Blender is the primary authoring tool and FBX is the primary planned 3D asset format. Matching Blender's world basis makes positions and transforms easier to inspect and exchange through the planned live bridge.

The FBX import boundary must still read the file's axis and unit metadata and normalize the scene into this world convention. Export settings can change the file's basis. A future live bridge must declare its transmitted basis and units explicitly as well.

## Renderer boundary

World axes do not define Vulkan clip-space depth, framebuffer Y direction, or matrix memory layout. Handle projection and screen conventions at the renderer boundary instead of changing scene coordinates.

The perspective sample now supplies 3D vertex positions, object models, and a camera to the renderer. Its geometry and animation live in `Samples/PerspectiveSample.cpp`; the renderer records borrowed draw submissions and owns their uploaded meshes until shutdown.

## Implemented transform and projection contract

- C++ `Mat4` stores four contiguous columns and multiplies column vectors. `clip = projection * view * model * local`; the rightmost transform applies first. A model composed as `Translation * Rotation * Scale` scales, rotates, then translates.
- `LookAtRH` maps the camera's forward direction to view-space -Z. The default world up is +Z; world right maps to view right.
- The perspective camera uses vertical field of view in radians. Defaults are 60 degrees, near plane `0.1`, and far plane `100`, in engine distance units. The renderer derives aspect ratio from the actual swapchain extent on each draw.
- The projection maps near/far depth to `0`/`1`, matching [Vulkan depth conventions](https://docs.vulkan.org/guide/latest/depth.html). It negates projected Y for the existing positive-height viewport, keeping world/camera up toward the screen top.
- C++ uploads an 80-byte push constant: four clip-from-local columns at byte offsets `0`, `16`, `32`, and `48`, followed by tint at `64`. HLSL declares these columns explicitly and combines them with the vertex coordinates, avoiding implicit matrix packing defaults.
- Opaque color geometry uses depth clear `1` and comparison `LESS`. Every swapchain framebuffer owns its own depth image, memory, and view; they are recreated with the swapchain extent after GPU work completes.

The math functions reject invalid perspective parameters and degenerate camera directions. Engine distance units for imported assets, general rotation storage/Euler order, and imported camera conversion remain separate future decisions. Orthographic projection remains planned for the 2D sample.

## Sample controls and verification

Arrow keys orbit the camera, the wheel changes camera distance, Home or physical R resets the view, and Space pauses/resumes object animation. The sample draws an orange cube, a farther cyan cube, and an XY ground slab through one shared mesh with separate models and tints. The near cube is submitted first to exercise depth rejection of later geometry.

Verified on Windows on 2026-10-10: Debug/Release builds and HLSL/SPIR-V validation, 59 temporary CPU checks for transforms/projection/clipping/layout/invalid inputs, and computer-use checks of animation, orbit, zoom, Home reset, resize, minimize/restore, and normal shutdown. Debug runs with core and synchronization validation and reports no warnings/errors. Release also runs from a different working directory. The R shortcut was not verified by computer use because synthesized letter input did not reach the existing physical-key mapping.

Evidence is under ignored `out/validation/perspective-*.log` and `out/screenshots/ravenengine-perspective.jpg`. The temporary CPU probe was compiled from stdin and removed after validation; no test source or framework was added to the project.
