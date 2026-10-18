# Common asset identity and runtime reuse

Status as of 2026-10-14. Common UUIDv4 identities, typed persistent references, an in-memory asset catalog, and ID-based texture reuse are implemented and verified in Windows Debug/Release. The detailed validation below records checks from 2026-10-11; the 2026-10-14 assessment checked the current Windows builds and reviewed that evidence. FBX loading, a persistent catalog format, dependency/build outputs, runtime handles, and hot reload remain unimplemented.

## Identity and types

`AssetId` in [AssetId.hpp](../RavenEngine/Public/Raven/Assets/AssetId.hpp) is a 16-byte UUIDv4 in standard byte order. The default value is nil and `IsValid()` returns false. `Generate()` uses operating-system random bytes to create a UUIDv4 and propagates failures from the random provider. It does not fall back to time or counters.

`Parse(std::string_view)` accepts exactly 36 characters in UUIDv4 format, with either case for hexadecimal digits. It rejects invalid lengths, separators, characters, versions, variants, and nil with `std::invalid_argument`. Restricting IDs to UUIDv4 is the current asset identity policy. `ToString() const` returns the canonical lowercase string; it can also format nil for diagnostics. Generate an ID once when registering an asset and preserve it afterward. Asset identity is separate from a source-content hash.

`AssetRef<T>` in [AssetRef.hpp](../RavenEngine/Public/Raven/Assets/AssetRef.hpp) is a typed value containing a UUID. It neither loads nor owns resources. `TextureAsset`, `ModelAsset`, `MeshAsset`, `MaterialAsset`, `SkeletonAsset`, `AnimationAsset`, `ShaderAsset`, and `SceneAsset` are currently forward declarations that distinguish reference types; they do not imply implemented data types or loaders. For example, passing `AssetRef<MeshAsset>` to texture loading does not compile. The catalog also checks the actual type of IDs read from stored data.

## Catalog

[AssetCatalog.hpp](../RavenEngine/Public/Raven/Assets/AssetCatalog.hpp) stores IDs, `AssetKind`, and Content-relative paths. Its public interface is separated from its internal containers and implementation. Registration, lookup, and resource loading currently run on the application/rendering thread. Concurrent registration and asynchronous loading are unsupported. Concurrent UUID generation was validated separately.

- `Register(const AssetRecord&)` registers general asset metadata. Textures require color-space settings and must use the dedicated overload.
- `Register(const TextureAssetDesc&)` registers a texture path and its Linear/SRGB setting.
- `Get(const AssetId&) const` retrieves metadata for any supported asset kind.
- `GetTexture(AssetRef<TextureAsset>) const` checks the registered type and returns the texture description.

Registered entries cannot be changed or removed. Duplicate IDs are rejected even when their metadata matches. A failed registration leaves existing entries unchanged. Lookup references remain valid for the catalog's lifetime, including after further registrations. Missing IDs produce `std::out_of_range`; nil IDs and type/registration errors produce `std::invalid_argument`.

Paths are checked with the existing `Paths::GetContentPath` policy for rooted paths and parent-directory escape, then stored in normalized relative form. Empty paths, embedded null characters, and paths without filenames are rejected. Physical symlinks are not resolved, and Windows case aliases are not merged. Registration neither reads the file nor checks whether it exists, keeping metadata registration separate from loading.

Registering Model/Mesh/Material and other general kinds provides metadata only. It does not implement FBX sub-asset relationships, reimport mapping, or decoding for those kinds.

## Runtime resource service

`AssetResources` in [AssetResources.hpp](../RavenEngine/Public/Raven/Assets/AssetResources.hpp) is a synchronous service that borrows the catalog and renderer. It captures the Content root at construction and uses the catalog's description in `LoadTexture(AssetRef<TextureAsset>)`. A common game owner should share one catalog and resource service across scenes and objects.

```cpp
#include "Raven/Assets/AssetCatalog.hpp"
#include "Raven/Assets/AssetResources.hpp"

Raven::AssetCatalog catalog;
const auto id = Raven::AssetId::Parse("7a590cee-ec82-4a27-927f-860e64619c19");
catalog.Register(Raven::TextureAssetDesc
{
	id, "Textures/UVGrid.png", Raven::TextureColorSpace::SRGB
});

Raven::AssetResources resources(catalog, renderer);
const auto& first = resources.LoadTexture({ id });
const auto& second = resources.LoadTexture({ id });
// &first == &second: decoding, upload and descriptor allocation are reused.
```

Within one service, the same ID returns the same GPU texture. Cache hits do not read, decode, or upload the file; an already loaded resource remains available even if its source file is deleted. Different IDs referring to the same file are separate logical assets. Caches are not shared across services or renderer contexts. Different contexts require separate GPU resources.

Cache nodes are allocated before GPU creation. If file reading, decoding, or upload fails, the node is removed and the exception propagates, allowing a retry. The existing `VulkanContext` continues to own GPU textures and immutable descriptors, and returned references remain valid until renderer shutdown. Destroying the resource service does not release its GPU textures. The catalog and renderer must outlive the service.

Loading uses the default `Images::Load` options. The first successful decode retains the development-build warning for non-power-of-two (NPOT) images; cache hits do not repeat it. Release builds do not emit that warning. The catalog defines the path and color-space settings; the loading API does not accept an alternative description that could override them.

## Samples and batching

Both sample clients own a common `SampleAssets` member. Fixed UUIDs and paths are collected in `SampleAssets.cpp`, replacing the earlier local file-loading helpers. Samples do not generate fresh IDs at startup. The service is initialized before the borrowed GPU-reference members, and the client is destroyed before the renderer under the existing lifecycle contract.

The renderer's creation/draw APIs and batching algorithm are unchanged. The asset cache ensures requests for the same ID use the same texture object. Current batching combines adjacent draws using the same mesh and texture within one pass while preserving transparent ordering. A shared FBX file ID alone does not cause its contents to render in one draw.

## ECS and subsequent asset work

The next task in [TODO.md](../TODO.md) is entity identity, creation/destruction, and a minimal ECS-backed orthographic scene. It can reuse the existing catalog/cache and a scene-shared mesh without first implementing a catalog file format or FBX loader. Runtime entity index/generation identity and persistent asset UUIDs serve different lifetimes and purposes.

After the minimal scene, define asset metadata and scene file formats/versions, UUID preservation, and source/dependency/build-output records. Before repeated scene switching or resource replacement, implement and validate runtime handles, sharing/release, stale-handle rejection, and destruction after GPU completion. Creating a new service for each scene does not share its cache, even for the same IDs, and its GPU resources remain until renderer shutdown. Do not treat service destruction as scene-resource unloading. These are planned capabilities, not features of the current API.

## Platform integration

UUID layout, parsing, and formatting use shared C++20 code. The only new platform hook is private [PlatformRandom.hpp](../RavenEngine/Private/Platform/PlatformRandom.hpp).

```cpp
namespace Raven::Platform
{
	void FillRandomBytes(std::span<std::uint8_t> bytes);
}
```

Fill the complete buffer with secure operating-system random bytes or throw. Windows uses `BCryptGenRandom` and the system `bcrypt` library. macOS/Linux implementation and native verification belong to their platform contributors and have not been added here. Without this hook, those platforms cannot complete the link for `AssetId::Generate`. See [PlatformHandoff.md](PlatformHandoff.md).

## Validation recorded on 2026-10-11

Windows Debug/Release builds, public DLL clients, and independent `/W4 /WX` compilation of seven public asset headers passed. The four new asset/random implementation sources also passed `/W4 /WX`, excluding the existing intentional alignment-padding warning in math types. Each configuration passed 3,417 checks and submitted 25 real frames. Function/member-pointer checks verified declarations, arguments, returns, and const contracts. All 24 existing DLL exports were preserved and 12 asset exports were added.

Four threads generated 1,024 UUIDs to check version/variant bits, parsing/formatting round trips, and duplicate absence within the sample. This is not a proof of absolute uniqueness. A separate temporary probe injected random-provider failure and verified exception propagation. Catalog checks covered preservation after duplicates/conflicts, type errors, invalid paths/color spaces, Unicode, retry after failed registration, and stable lookup references.

Public GPU clients verified repeated requests, cache use after source deletion, retry after repairing missing/corrupt files, separation by ID/color space, valid renderer references after service destruction, resource separation across renderer contexts, and rejection of draws using the wrong context. Debug API traces recorded only the intended eight texture resources and eight bindings, with no growth from hundreds of cache hits.

Batching traces showed 257 opaque and four transparent instances submitted as two indexed draws in each of 24 frames, with one offscreen instance excluded. A separate frame checking reference validity after service destruction used one draw. Core and submit-time synchronization validation reported no warnings/errors. The intentional NPOT warning appeared once in Debug and zero times in Release.

Computer use verified texture display and F1 order switching in both actual Debug samples, perspective maximization, and orthographic wheel zoom. Both samples exited normally with clean validation logs. Evidence is under `out/validation/assets/`; test code exists only in ignored temporary paths. No CTest targets or tracked test files were added.
