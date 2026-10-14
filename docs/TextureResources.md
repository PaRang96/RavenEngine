# RGBA8 texture resources

Textures now have a complete initial path from [PNG/JPEG file decoding](ImageLoading.md) through upload, immutable descriptors, UV geometry, and HLSL sampling. The perspective sample displays two opaque textured quads alongside its colored cubes and ground slab.

## Public interface

Include `Raven/Renderer/TextureDesc.hpp` and the public renderer context. `TextureDesc` contains width, height, a borrowed span of tightly packed RGBA8 bytes, and `TextureColorSpace` (`Linear` or `SRGB`, default `SRGB`). Rows contain exactly `Width * 4` bytes, and the span must contain exactly `Width * Height * 4` bytes.

```cpp
std::array<std::uint8_t, 16> rgba
{
    255, 0, 0, 255,    0, 255, 0, 255,
    0, 0, 255, 255,    255, 255, 255, 255
};

Raven::TextureDesc description
{
    2, 2, std::as_bytes(std::span{rgba}), Raven::TextureColorSpace::SRGB
};
const Raven::VulkanTexture& texture = renderer.CreateTexture(description);
```

Creation copies and uploads the pixels synchronously. The caller can release or change its source storage when the call returns. The returned texture is an opaque borrowed reference owned by that context, like meshes; it remains alive until context shutdown and cannot be released independently through the public API. New texture allocations do not relocate existing owners.

Call renderer methods from the application/rendering thread. Uploads share the graphics queue with drawing and temporarily wait on their own fence. This is an initial synchronous path, not asynchronous streaming or batched upload.

## Resource and synchronization contract

The private owner holds a device-local, optimal-tiled 2D image, its memory, an image view, and a sampler. `Linear` selects `VK_FORMAT_R8G8B8A8_UNORM`; `SRGB` selects `VK_FORMAT_R8G8B8A8_SRGB`. Upload and image-copy readback preserve the supplied byte values.

Initial textures have one mip level, one array layer, linear minification/magnification filtering, clamped U/V/W addressing, and no anisotropy. The image supports sampled use, transfer destination upload, and transfer source readback. Device limits and sampled-image/linear-filter support are checked, along with the selected image format's extent/usage capabilities. Invalid dimensions, byte counts, and color-space values are rejected before allocation.

A host-visible coherent staging buffer holds the bytes. A transient command pool records `UNDEFINED -> TRANSFER_DST_OPTIMAL`, the buffer-to-image copy, and `TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL`. The final barrier makes transfer writes available to graphics shader reads on the graphics queue. Upload submission uses a fence; successful creation waits for it before releasing staging memory and commands. This uses the Vulkan 1.0 synchronization APIs; see [Khronos synchronization examples](https://docs.vulkan.org/guide/latest/synchronization_examples.html) for the dependency patterns.

If a wait fails after a successful submission, cleanup drains the graphics queue before releasing upload resources. A queue-wait failure is logged and propagated through the original construction failure; device loss remains a fatal renderer failure, not a recovery feature.

Failed construction unwinds temporary upload resources before sampler, view, image, and image memory. Context shutdown waits for GPU work, destroys frame resources/pipelines, then texture descriptor pools/layout, textures and meshes, and finally the device. Swapchain recreation preserves textures and their descriptors. The complete owners and native handles remain in `Private/Renderer/Vulkan/`. The original upload milestone added one explicit export, bringing the engine to 14 exports at that milestone.

## Textured draw submissions

`TexturedVertex` contains `float Position[3]` and `float UV[2]` (20 bytes). `VulkanTexturedMesh` is a separate opaque type from the existing color mesh, so their vertex layouts cannot be mixed accidentally. Include `Raven/Renderer/Vulkan/VulkanContext.hpp` for mesh creation and draw submissions:

```cpp
const std::array<Raven::TexturedVertex, 4> vertices
{
	Raven::TexturedVertex{{-1, 0, 1}, {0, 0}},
	Raven::TexturedVertex{{1, 0, 1}, {1, 0}},
	Raven::TexturedVertex{{1, 0, -1}, {1, 1}},
	Raven::TexturedVertex{{-1, 0, -1}, {0, 1}}
};
const std::array<std::uint16_t, 6> indices{0, 1, 2, 2, 3, 0};
const auto& mesh = renderer.CreateTexturedMesh(vertices, indices);
std::array<Raven::VulkanTexturedDraw, 1> draws
{
	Raven::VulkanTexturedDraw{mesh, texture}
};
draws[0].Tint = {0.8f, 1.0f, 0.8f, 1.0f};
renderer.DrawFrame(camera, std::span<const Raven::VulkanDraw>{}, draws);
```

Creation copies the CPU geometry. Meshes and textures must belong to the submitting context; foreign resources are rejected before frame submission. Returned references remain valid until context shutdown. The existing `ColorVertex`, `VulkanDraw`, `CreateMesh`, and two-argument `DrawFrame` are unchanged. The new mesh factory and three-argument overload add two DLL exports while retaining all 18 previous exports, for 20 total.

Set 0 uses binding 0 for a sampled image and binding 1 for a sampler. The context owns descriptor pools in pages of 64 texture bindings. Each binding is written once during successful texture creation, stays valid across frames and swapchain recreation, and is released only after GPU completion at shutdown. Drawing performs no descriptor allocation or mutation.

The texture pipeline uses `Texture.vert.hlsl` and `Texture.frag.hlsl`, compiled and validated through the existing DXC/SPIR-V build. Each textured instance supplies an 80-byte transform/tint record through vertex binding 1; the color path retains its push constants. UV `(0, 0)` addresses the image's top-left; the supplied quad places V=0 on its +Z edge. SRGB textures decode RGB during sampling, then sampled RGBA is multiplied by `Tint`.

Opaque geometry uses depth test/write with blending disabled. Color draws precede opaque textured draws, followed by a separate transparent queue. Compatible textured draws use the instancing path below. Mipmaps and broader material updates remain later work.

The same draw overloads, descriptors and pipelines support `OrthographicCamera`. Run the sample with `--orthographic` to verify depth-invariant sprite sizes, pan/zoom and overlapping transparency. See [CoordinateSystem.md](CoordinateSystem.md) for projection and resize behavior.

## Transparency and visibility

The four-argument overload adds straight-alpha textured submissions while preserving both existing overloads and their DLL symbols:

```cpp
std::array<Raven::VulkanTransparentDraw, 2> transparent
{
	Raven::VulkanTransparentDraw{{mesh, texture}},
	Raven::VulkanTransparentDraw{{mesh, anotherTexture}}
};
transparent[0].Draw.Tint.W = 0.45f;
transparent[1].Draw.Tint.W = 0.65f;
renderer.DrawFrame(camera, colorDraws, opaqueTexturedDraws, transparent);
```

Input RGB must be unassociated (straight alpha), as with the decoder's ordinary RGBA8 output. HLSL multiplies sampled RGBA by tint. The transparent pipeline keeps depth testing (`LESS`) enabled and disables depth writes. RGB blending uses `SRC_ALPHA` / `ONE_MINUS_SRC_ALPHA`; alpha uses `ONE` / `ONE_MINUS_SRC_ALPHA`, both with `ADD`. SRGB texture RGB is decoded before blending. Opacity does not select the pipeline automatically: callers explicitly choose the opaque or transparent list.

Transparent draws sort by ascending signed `SortLayer`, ascending signed `SortOrder`, then descending camera-space depth of the transformed mesh bounds center. Equal keys retain submission order. Defaults of zero give automatic far-to-near sorting. Higher layer/order values composite later, but never bypass opaque depth occlusion. Sorting uses view depth, independent of world Z, distance from the camera, or the mesh pivot. This is object sorting: intersecting surfaces and triangles within one mesh can still require a more advanced transparency method.

Ownership and responsibilities are explicit:

- Each uploaded mesh caches a local AABB center/half extent from its indexed positions. CPU vertices need not remain alive.
- `TransformBounds` derives a world AABB per submitted instance, supporting affine rotation, translation, shear, zero scale, negative scale, and nonuniform scale.
- `ViewFrustum` extracts six planes from the actual camera view/projection using Vulkan's `0 <= z <= w` clip depth. Bounds entirely outside any plane are excluded; touching/intersecting bounds remain, with a conservative rounding tolerance.
- `BuildVulkanDrawQueue` validates transforms/tints, selects visible instances, prepares MVPs, sorts the transparent queue, and builds adjacent compatible batches before image acquisition. The context reuses its CPU queue storage and first checks all submitted resources, including resources on offscreen instances.
- `VulkanClearPass` owns each frame slot's instance buffer, uploads the prepared data after that slot's fence completes, then records/submits/presents one frame. Meshes and textures remain context-owned. No GPU resources are released or game objects disabled by culling.

Model matrices must be finite affine transforms; invalid values and numeric overflow are reported before frame acquisition. Extremely distant far planes that become unbounded in float projection math are kept conservatively. The same visibility preparation applies to both older opaque overloads.

This initial implementation handles static uploaded meshes. Animation/deformation will require updated or expanded bounds. A large mesh intersecting the frustum remains a full draw. Bounds do not detect objects hidden behind walls; occlusion culling, submesh/cluster selection, and optional GPU culling are separate extensions. Future shadows/reflections need visibility selection for their own views. Backface culling remains disabled so the current quads are visible from either side.

The perspective sample includes two opaque PNG quads and two overlapping transparent instances. Arrow keys orbit, wheel zooms, Home/R resets the camera, and Space pauses animation. `F1` or physical `O` toggles the blue-tinted transparent quad between automatic depth sorting and explicit `SortOrder = 1`.

## Compatible textured draw batching

The public perspective/orthographic `DrawFrame` overloads and submission fields are unchanged. Batching supports any uploaded `VulkanTexturedMesh`; shared quad geometry is a caller choice, not an engine requirement.

After per-instance culling and transparent sorting, the queue merges only adjacent submissions with identical mesh and texture objects, independently within the opaque and transparent passes. `A,A,B,A` produces `[AA][B][A]`; neither pass is reordered for grouping. Different transforms, tint, layer, and order values may share a batch because instances retain the already prepared primitive order. A texture owns its immutable image/sampler binding; a future separate material/sampler must extend the compatibility key.

`TexturedInstanceData` contains four column-major matrix columns at offsets 0/16/32/48 and tint at offset 64, with an 80-byte stride. Static assertions verify its CPU layout. HLSL reads locations 2–6 from binding 1 with `VK_VERTEX_INPUT_RATE_INSTANCE`, while position/UV remain per-vertex in binding 0. Each batch uses `vkCmdDrawIndexed` with its instance count and a global `FirstInstance`; binding offsets remain zero. Transparent instance ranges follow opaque ranges in the same uploaded array.

The context retains CPU vector capacity between calls. Each of the two `VulkanClearPass` frame slots owns a separate host-visible/coherent instance buffer. Its fence must finish before upload or replacement; capacity starts at 64 records and grows geometrically without shrinking. Replacement ownership changes only after allocation and copying succeed. Upload precedes image acquisition so a new allocation/map failure does not strand an acquired image or semaphore. Empty submissions require no upload or buffer binding. Swapchain recreation/shutdown retains the existing device-idle completion rule.

This step reduces calls for adjacent compatible textured submissions. It does not add atlas packing, GPU culling, skinned meshes, material reordering, or a new public sprite asset API.

## Batching validation on 2026-10-11

Windows Debug/Release builds pass, including DXC and Vulkan 1.0 SPIR-V validation. Five changed internal headers compile independently with `/W4 /WX` (the existing alignment-padding warning is excluded). Both DLLs preserve exactly the same 24 public exports as the pre-batching build.

A temporary public-DLL client uses production queue construction for CPU checks and public `DrawFrame` for actual GPU work on the RTX 3070 Ti Laptop GPU. It repeats empty/single/257-instance/mixed-mesh/mixed-texture/transparent-only frames, checks global instance ranges against prepared transforms/tints, and exercises growth and reuse of both frame slots. API dump confirms 257 opaque instances plus four transparent instances use two actual indexed draws, with transparent `firstInstance = 257`. The mixed sequence has seven visible opaque submissions in five batches and four transparent submissions in one batch; an offscreen submission is culled. Core and submit-time synchronization validation report no warnings/errors.

LunarG's installed screenshot layer reads five complete 960×640 RGB frames without compression, including repeated geometry, mixed quad/triangle geometry, incompatible textures, global offsets, and transparent layer/order/depth ties. Every Debug and Release RGB capture matches the preserved pre-batching individual-draw renderer byte for byte. Computer-use JPEG captures are used for visual inspection only, not exact pixel equality.

Native Debug computer use also verifies the existing perspective/orthographic samples, F1 order changes, camera movement/reset, perspective wheel zoom, maximize, minimize/restore, size restoration, and normal shutdown with clean diagnostics. Draw information stays in validation logs; window titles are unchanged. No CTest targets or permanent test sources are added. Evidence is under `out/validation/batching/`, including `api-debug.txt`, `gpu-*.log`, `capture-*.log`, `capture-*/`, and `native-*.log`.

## Transparency validation on 2026-10-11

Debug and Release each pass 42 checks, including 13 actual GPU pixel readbacks and six frames through the public rendering entry points. The temporary executable links the configuration's production engine objects. Checks cover straight-alpha composition, source alpha 0/1, RGB/alpha tint, SRGB decoding before blending, opaque color/texture occlusion, disabled transparent depth writes, explicit layer/order, stable equal-depth order, camera reversal, view depth versus radial distance, mesh center versus pivot, all six frustum planes, touching/spanning bounds, negative/rotated scale, a rounded far plane, cross-context rejection, and a valid frame after rejected input.

Debug core and synchronization validation report no warnings/errors using `VK_LAYER_VALIDATE_SYNC=1`. Logs are under `out/validation/transparency-debug.log` and `transparency-release.log`. Temporary probe sources/binaries/objects are removed after validation; no permanent test sources or CTest targets are added.

Nine changed/new public/private headers compile independently with `/W4 /WX` (only the intentional alignment-padding warning is excluded). Both DLLs retain all 20 previous exports and add exactly the four-argument overload, for 21 total. Separate public-only DLL clients exercise both older overloads, transparent-only/mixed frames, exception propagation, and a subsequent valid frame: five frames per configuration. Evidence is under `out/validation/transparency-dll-*.log` and `transparency-exports-*.txt`.

Native Debug computer use confirms overlapping alpha quads, pause, camera orbit, wheel zoom, explicit/automatic order switching with F1, maximize, minimize/restore, and normal shutdown. The final logged run also checks F1 and swapchain recreation after restore, with no validation diagnostics. Physical O is implemented but not verified by automation because injected letter input does not carry its physical scan code. Evidence: `out/validation/transparency-native-debug.log` and `out/screenshots/ravenengine-transparency-debug.jpg`.

## Validation on 2026-10-10

Debug/Release builds pass, and all 16 public headers compile independently. A temporary probe includes the production resource implementation with forwarding Vulkan hooks. It performs actual uploads/readbacks on the RTX 3070 Ti Laptop GPU and injects failures before selected native operations.

Each configuration passes 403 resource/parameter checks, nine byte-for-byte GPU readbacks, and 21 injected failure cases. Readbacks cover 1x1, 4x4, 7x3, and 257x129 textures in both color spaces, with varying color and alpha bytes, plus creation/readback after failures. Checks verify format/extent/handle values, release of upload temporaries before return, invalid descriptors, format support/limits, image and staging memory failures, view/sampler creation, command/fence setup, submission failure, and a failed wait after real submission. Handle/binding/mapping tracking verifies cleanup order and absence of leftover resources. The failed submitted-upload case performs the required queue-idle cleanup.

A public DLL client passes 260 checks, eight perspective frames, and 36 texture uploads in each configuration. It covers distinct context-owned allocations, invalid texture exceptions across the DLL, upload after rendering has been queued, repeated application runs, client failure cleanup, and texture creation during client destruction while the renderer remains alive. The existing client factory/frame/lifetime checks also pass. Debug core and synchronization validation report no warnings or errors; both configurations' logs are clean.

Evidence is under `out/validation/texture-*.log`. Probe sources are compiled from standard input; temporary executables and objects are removed after validation. No test framework or tracked test source is added. Those upload byte readbacks do not verify shader sampling; the rendering checks below cover it separately.

## Rendering validation on 2026-10-11

Windows Debug/Release builds and both new shader stages pass DXC compilation and Vulkan 1.0 SPIR-V validation. Seven changed/new renderer headers compile independently with warnings treated as errors. Both DLLs retain their 18 prior exports and add exactly the two intended functions.

Per configuration, a temporary public-DLL probe passes 13 checks, renders 24 frames, and allocates 72 textures across descriptor pages. It verifies PNG loading, invalid geometry, cross-context rejection, old/new draw overloads, color-only/texture-only/mixed frames, repeated application runs, and exception cleanup.

A separate probe uses the production Vulkan implementations to render into an offscreen target and read pixels back. Each configuration passes 13 pixel checks across nine readbacks and 73 immutable descriptor bindings: UV corner orientation, RGBA tint, UNORM versus SRGB decoding, transforms, color/texture depth occlusion, and both old and newly allocated bindings after page growth. Debug core/synchronization validation reports no warnings or errors; Release diagnostics are also clean.

Native Debug computer use verifies both PNG quads, pause, orbit, wheel zoom, Home reset, maximize/restore, minimize/restore, and normal shutdown. The screenshot is `out/screenshots/ravenengine-textured-debug.jpg`; logs are `out/validation/textured-*.log`. Probe sources were compiled from standard input and their executables/objects removed afterward. No CTest targets or permanent test files were added; repeatable test integration is deferred at the owner's request.

## Asset identity and texture reuse on 2026-10-11

Both samples now register fixed UUID texture assets through `SampleAssets` and load them through a common `AssetCatalog`/`AssetResources` boundary. Repeated requests for one registered ID reuse decoding, upload and the immutable descriptor. The renderer retains GPU ownership; cache/service destruction does not invalidate an existing renderer-owned texture reference. Failed loads erase their pending cache entry and can be retried. Different IDs, color-space assets and renderer contexts remain distinct. Existing draw APIs and batching behavior are preserved. See [AssetSystem.md](AssetSystem.md) for contracts and Debug/Release validation, including API-traced resource reuse and the 257-opaque/4-transparent two-draw case.
