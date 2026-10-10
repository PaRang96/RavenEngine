# PNG/JPEG image loading

`Raven/Assets/Image.hpp` provides `ImageData`, `ImageLoadOptions`, and two exported overloads: `Images::Load(path)` and `Images::Load(path, options)`. The original one-argument overload forwards to the options overload with `ImageLoadOptions{}`. The result owns CPU pixels and can be used before an application or Vulkan device exists. Use the existing content-path utilities to locate game content:

```cpp
#include "Raven/Assets/Image.hpp"
#include "Raven/Core/Paths.hpp"
#include "Raven/Renderer/Vulkan/VulkanContext.hpp"

const auto image = Raven::Images::Load(
	Raven::Paths::GetContentPath("Textures/Example.png"));
const Raven::TextureDesc description
{
	image.Width, image.Height, image.Pixels, Raven::TextureColorSpace::SRGB
};
const auto& texture = renderer.CreateTexture(description);
```

`CreateTexture` completes its copy/upload before returning, so the CPU image can then be released. `ImageData` is a value type: copying it copies its pixel storage, and moving it transfers that storage. The existing engine/client compiler, STL, runtime, and build-configuration requirements apply to this API across the DLL boundary; see [EngineModules.md](EngineModules.md).

## Pixel and format contract

- `Width` and `Height` are nonzero pixel dimensions.
- `Pixels` contains exactly `Width * Height * 4` bytes in R, G, B, A order.
- Rows run from top to bottom, with the top-left pixel first and no row padding.
- Alpha is straight (unassociated); images without alpha receive `255`.
- Supported PNG input includes RGB/RGBA, grayscale/grayscale-alpha, palettes/transparency, low-bit-depth samples, and Adam7 interlacing. Sixteen-bit PNG samples are rejected rather than silently reduced to eight bits.
- Ordinary eight-bit baseline and progressive JPEG images are supported. RGB and grayscale inputs are verified.
- The format is detected from file contents; `.png`, `.jpg`, and `.jpeg` are conventional names rather than an extension whitelist. Other decoders are disabled.
- CgBI PNG input is normalized from its BGR/BGRA storage and premultiplied alpha into this same pixel contract. Decoder options and failure state use thread-local storage.

Loading does not apply ICC/gamma color management or EXIF orientation, and animation data is not exposed. Color-space selection remains explicit at texture creation: use `SRGB` for prepared color images and `Linear` for normal, roughness, and other data maps. The existing GPU upload remains a single-mip RGBA8 path; descriptors and visible texture sampling are subsequent rendering work.

## Development diagnostics

`ImageLoadOptions::WarnIfNonPowerOfTwo` defaults to `true`. After a successful decode, a development build logs one warning when either dimension is not a power of two. The warning includes the absolute UTF-8 source path and dimensions. Loading preserves the original pixels and dimensions; non-power-of-two images remain valid for the current RGBA8 texture path. A rectangular `1024 x 512` image meets the convention, and `1 x 1` also qualifies.

CMake defines `RAVEN_DEVELOPMENT` privately for RavenCore in Debug and RelWithDebInfo. Release and MinSizeRel omit this diagnostic even when the option is `true`. The engine build controls logging; the caller's build macros do not change that behavior. A failed load produces its normal exception without this successful-load warning.

For an intentionally non-power-of-two asset, suppress the diagnostic through the options:

```cpp
const auto image = Raven::Images::Load(
	Raven::Paths::GetContentPath("Textures/Example.png"),
	{ .WarnIfNonPowerOfTwo = false });
```

The original one-argument DLL entry point is retained alongside the options overload. Existing callers of either signature retain their entry point; callers using the new overload require the corresponding engine DLL. Both image overloads are explicit DLL exports; see [EngineModules.md](EngineModules.md) for the current export count. The matching compiler, STL, runtime, and build-configuration requirements still apply.

## Paths, limits, and failures

Relative paths resolve from the current working directory, like `Files::ReadBinary`. `Paths::GetContentPath` produces paths under the active content root, including application content-root overrides. Native filesystem paths support Unicode filenames; the decoder receives file bytes rather than filenames.

The initial loader accepts dimensions from `1` through `16384`, encoded input up to `64 MiB`, and decoded RGBA8 output up to `256 MiB`. It checks file size before reading and validates the actual byte count afterward. Metadata and decoded-size checks occur before pixel decoding; the decoded dimensions must also match the inspected header. Loading is synchronous.

An empty path throws `std::invalid_argument`. File, limit, unsupported precision, and decoder failures throw `std::runtime_error`; their diagnostics include the absolute attempted path in UTF-8. Decoder pixels have RAII ownership, including cleanup when allocation of the returned pixel vector fails.

## Dependency and build

The pinned, unchanged [stb_image copy](../ThirdParty/stb/README.md) is a private system include of RavenCore. Its implementation is compiled once in `Private/Assets/Image.cpp`, with PNG/JPEG enabled, filename I/O and floating-point conversion disabled, and stb functions given internal linkage. Public headers expose only Raven and standard C++ types. Builds use the vendored source without a decoder download or additional decoder DLL.

The upstream license is preserved in `ThirdParty/stb/LICENSE` and staged as `Licenses/stb-LICENSE.txt` beside the application in both configurations. Include that directory when packaging the application and DLL. The shared decoder source is intended for Windows, macOS, and Linux; this milestone is verified on Windows only.

## Validation

At the initial image-loading milestone, Windows Debug and Release builds pass. All 17 public headers compile independently, the new implementation passes MSVC `/W4 /WX` with the vendored header treated as an external include, and the DLL has 15 explicit exports with `Images::Load` as the only new export. The third-party include path is absent from both sample compilation commands, and staged licenses match the upstream file hash.

Each configuration passes 109 CPU/public-DLL checks over 19 valid fixture cases and 15 failure cases. PNG comparisons are byte-exact, including transparent RGB values, row orientation, grayscale, palette depths/transparency, interlacing, unchanged gamma-tagged pixels, a `16384 x 1` image, and CgBI normalization. Baseline/progressive/grayscale JPEG comparisons use an independent decoder with a small RGB rounding tolerance and exact opaque alpha. Failure cases cover empty/missing/directory paths, empty/junk/truncated/corrupt inputs, disabled formats, sixteen-bit PNG, and dimension/encoded/decoded-size limits. Unicode paths, relative paths from a different working directory, value ownership, and decoding after failures are checked.

Eight threads complete 128 stress iterations per configuration, containing 384 successful decodes and 128 rejected loads. A public application client uploads 39 decoded images in Linear/SRGB modes, including an upload after a frame is queued, and presents eight frames of the existing perspective sample. Debug core and synchronization validation report no warnings or errors. This verifies decoding/upload integration; the sample still displays colored geometry.

Evidence: `out/validation/image-loader-<config>-results.log`, `image-loader-<config>.log`, `image-headers.log`, and `image-exports.log`. Probe sources are compiled from standard input; temporary fixtures, executables, objects, and probe debug symbols are removed. No test framework or tracked test source is added.

After adding development diagnostics, Windows Debug/Release builds and MSVC `/W4 /WX` checks pass. Focused public-DLL probes pass 38 checks each across ten successful PNG/JPEG cases and six rejected-load cases. Debug produces exactly six expected warnings; explicit suppression, power-of-two rectangles, `1 x 1`, and failed loads stay quiet. Release produces no warning even with explicit `true`. Warning paths/dimensions and unchanged pixel output are verified. The Debug client is compiled with `NDEBUG`, and the Release client with `RAVEN_DEVELOPMENT`, confirming that the engine configuration controls the diagnostic. The RelWithDebInfo/MinSizeRel policy is defined in CMake; those full configurations are not exercised in this follow-up.

Evidence for the follow-up is in `out/validation/image-warning-<config>-results.log` and `image-warning-<config>.log`. Temporary fixtures and probe binaries/objects are removed after validation.

After restoring the original one-argument export, Debug and Release builds pass. Each configuration passes 38 checks through both overloads, including typed function pointers, and six additional checks using the original declarations without the current image header. The DLL has 16 exports and preserves every symbol from both prior image API versions. Debug produces six expected warnings in the options probe and one in the original-declaration probe; Release produces none. Warning counts, Unicode source paths, dimensions, and decoded pixels are verified. Evidence is in `out/validation/image-signatures-<config>-results.log`, `image-signatures-<config>.log`, `image-legacy-signature-<config>-results.log`, `image-legacy-signature-<config>.log`, and `image-signature-exports-<config>.log`. Temporary fixtures and probe binaries/objects are removed after validation.
