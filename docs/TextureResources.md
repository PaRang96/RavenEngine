# RGBA8 texture resources

The first texture milestone implements ownership and upload through `VulkanContext::CreateTexture`. [PNG/JPEG file decoding](ImageLoading.md) is now implemented separately through `Images::Load`; descriptors, UV geometry, and shader sampling remain subsequent asset/rendering work. The perspective sample still draws colored geometry.

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

Failed construction unwinds temporary upload resources before sampler, view, image, and image memory. Context shutdown waits for GPU work, destroys frame resources, then textures and meshes, and finally the device. Swapchain recreation does not recreate textures. The complete owner and native handles remain in `Private/Renderer/Vulkan/`; the public context adds one explicit export, for 14 engine exports in total.

## Validation on 2026-10-10

Debug/Release builds pass, and all 16 public headers compile independently. A temporary probe includes the production resource implementation with forwarding Vulkan hooks. It performs actual uploads/readbacks on the RTX 3070 Ti Laptop GPU and injects failures before selected native operations.

Each configuration passes 403 resource/parameter checks, nine byte-for-byte GPU readbacks, and 21 injected failure cases. Readbacks cover 1x1, 4x4, 7x3, and 257x129 textures in both color spaces, with varying color and alpha bytes, plus creation/readback after failures. Checks verify format/extent/handle values, release of upload temporaries before return, invalid descriptors, format support/limits, image and staging memory failures, view/sampler creation, command/fence setup, submission failure, and a failed wait after real submission. Handle/binding/mapping tracking verifies cleanup order and absence of leftover resources. The failed submitted-upload case performs the required queue-idle cleanup.

A public DLL client passes 260 checks, eight perspective frames, and 36 texture uploads in each configuration. It covers distinct context-owned allocations, invalid texture exceptions across the DLL, upload after rendering has been queued, repeated application runs, client failure cleanup, and texture creation during client destruction while the renderer remains alive. The existing client factory/frame/lifetime checks also pass. Debug core and synchronization validation report no warnings or errors; both configurations' logs are clean.

Evidence is under `out/validation/texture-*.log`. Probe sources are compiled from standard input; temporary executables and objects are removed after validation. No test framework or tracked test source is added. Texture sampling and sRGB shader decoding are not validated by byte readback; they belong to the following descriptor/shader milestone.
