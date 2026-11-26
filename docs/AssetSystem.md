# Common asset identity and runtime reuse

Current status as of 2026-11-19: SceneProject loads a versioned file-backed catalog and scene list, and AssetStore shares scoped resources with generation-checked handles and GPU-safe release. Both samples use this path. [SceneWorkflow.md](SceneWorkflow.md) defines the metadata format, source roots, automatic scene/catalog editing, ownership and current validation. AssetResources remains the legacy pinned service described below. Earlier validation records remain historical evidence; general import dependencies, multi-object FBX identities and broader scene serialization remain open.

## Identity and types

`AssetId` in [AssetId.hpp](../RavenEngine/Public/Raven/Assets/AssetId.hpp) is a 16-byte UUIDv4 in standard byte order. The default value is nil and `IsValid()` returns false. `Generate()` uses operating-system random bytes to create a UUIDv4 and propagates failures from the random provider. It does not fall back to time or counters.

`Parse(std::string_view)` accepts exactly 36 characters in UUIDv4 format, with either case for hexadecimal digits. It rejects invalid lengths, separators, characters, versions, variants, and nil with `std::invalid_argument`. Restricting IDs to UUIDv4 is the current asset identity policy. `ToString() const` returns the canonical lowercase string; it can also format nil for diagnostics. Generate an ID once when registering an asset and preserve it afterward. Asset identity is separate from a source-content hash.

`AssetRef<T>` in [AssetRef.hpp](../RavenEngine/Public/Raven/Assets/AssetRef.hpp) is a typed value containing a UUID. It neither loads nor owns resources. `TextureAsset`, `ModelAsset`, `MeshAsset`, `MaterialAsset`, `SkeletonAsset`, `AnimationAsset`, `ShaderAsset`, and `SceneAsset` are currently forward declarations that distinguish reference types; they do not imply implemented data types or loaders. For example, passing `AssetRef<MeshAsset>` to texture loading does not compile. The catalog also checks the actual type of IDs read from stored data.

## Catalog

[AssetCatalog.hpp](../RavenEngine/Public/Raven/Assets/AssetCatalog.hpp) stores IDs, `AssetKind`, and Content-relative paths. Its public interface is separated from its internal containers and implementation. Registration, lookup, first load, and GPU publication run on the application/rendering thread. The PNG watch worker reads and decodes independently of the catalog. Concurrent UUID generation was validated separately.

- `Register(const AssetRecord&)` registers general asset metadata. Textures require color-space settings and must use the dedicated overload.
- `Register(const TextureAssetDesc&)` registers a texture path and its Linear/SRGB setting.
- `Get(const AssetId&) const` retrieves metadata for any supported asset kind.
- `GetTexture(AssetRef<TextureAsset>) const` checks the registered type and returns the texture description.

Individual Register entries are immutable. SceneProject replaces a complete validated catalog through Swap. Duplicate IDs are rejected even when their metadata matches. A failed registration leaves existing entries unchanged. Lookup references survive further registrations, but expire at Swap, move assignment or destruction. SceneProject swaps the complete validated catalog at a host publication boundary; callers copy metadata they need to retain. Missing IDs produce `std::out_of_range`; nil IDs and type/registration errors produce `std::invalid_argument`.

Paths are checked with the existing `Paths::GetContentPath` policy for rooted paths and parent-directory escape, then stored in normalized relative form. Empty paths, embedded null characters, and paths without filenames are rejected. Physical symlinks are not resolved, and Windows case aliases are not merged. Registration neither reads the file nor checks whether it exists, keeping metadata registration separate from loading.

Mesh entries can be resolved through AssetResources. Model and material entries currently provide metadata. FBX sub-asset relationships and reimport mapping remain open.

## Content trust (2026-11-26)

Delegated (business alignment and expandability; AA2-MEDIA-02). v1 game content is trusted author input: it is bounded but not sandboxed. Every decoder bounds its output before it allocates: images and FBX meshes by their header limits, audio by `MaxClipSeconds` and `MaxDecodedAudioBytes` (128 MiB of decoded samples) in `Private/Audio/AudioClip.hpp`. Mods or user-generated content would need their own owner decision on scope.

## Runtime resource service

The migrated sample path uses [AssetStore](../RavenEngine/Public/Raven/Assets/AssetStore.hpp). Its weak cache shares resources by UUID while host-owned AssetLease values keep them live. Scene owns its acquired leases; the last owner stops the watcher and releases the managed mesh/texture or PCM clip. SceneProject validates source paths against its explicit root, including physical escapes and source existence. References written by game code at runtime go through `Scene::TryUseAsset` (2026-11-24): a nil or unregistered ID, a wrong kind or a missing file returns null and logs once per asset, so a material samples the white texture and a sprite is skipped. Decode and GPU failures still throw. The placeholder stays until the material or sprite changes again. See [scene/resource ownership](SceneWorkflow.md#scene-and-resource-ownership).

### Legacy pinned service

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

Cache nodes are allocated before GPU creation. If file reading, decoding, or upload fails, the node is removed and the exception propagates, allowing a retry. `VulkanContext` owns the texture objects and descriptor sets. Returned references remain valid until renderer shutdown. Destroying the resource service does not release its GPU textures. The catalog and renderer must outlive the service.

Loading uses the default `Images::Load` options. The first successful decode retains the development-build warning for non-power-of-two (NPOT) images; cache hits do not repeat it. Release builds do not emit that warning. The catalog defines the path and color-space settings; the loading API does not accept an alternative description that could override them.

## Static FBX meshes

The Windows Perspective sample loads [Cube.fbx](../Content/Meshes/Cube.fbx) and [Sphere.fbx](../Content/Meshes/Sphere.fbx) through AssetResources::LoadMesh. Run the sample with --perspective. CMake stages the FBX files under Content/Meshes beside the executable. Each file has a stable mesh asset UUID. The orange cube and the floor share one cached MeshHandle, and the cyan sphere uses another. Entity transforms and tints belong to the existing ECS components.

Both fixtures were exported with Blender 5.2.3 LTS as selected mesh objects, with -Z forward, +Y up, metric scene units, and linear preview vertex colors. The cube is one meter per side and the UV sphere has a one-meter diameter with 16 segments and 8 rings. The import path uses the vendored [ufbx release](../ThirdParty/ufbx/README.md) to convert axes and units to Raven's right-handed Z-up meter convention. It applies the FBX mesh node transform, triangulates polygons, and preserves outward winding under reflection. Linear RGB vertex colors, normalized normals and UVs feed the mesh pipeline. Missing colors use white, missing normals are generated, and normal/UV seams survive deduplication. Level records assign ECS material values. See [mesh materials and scene lighting](MeshMaterials.md).

The importer accepts one static mesh object per FBX and checks the existing 16-bit index limit. Material assignment comes from the level file. Skinning and multi-object FBX import remain future work. Initial loads are synchronous and cached for the renderer's lifetime. Startup and reload both pass bounded file bytes to the same private FBX decoder, which returns Raven vertex and index arrays. Only the decoder implementation uses ufbx types.

Windows x64-debug and x64-release built and captured both meshes in the Perspective sample with Vulkan synchronization validation enabled. A geometry probe checked 12 cube triangles and 224 sphere triangles, outward winding, and one-meter bounds. A cache probe verified same-ID handle reuse and retry after a malformed FBX was restored. Local logs and captures are under ignored out/validation/fbx/. Native macOS and Linux were not run.

## Live Perspective mesh reload

Build a Windows preset once and launch from the repository root.

~~~powershell
.\out\build\x64-debug\RavenEngine\RavenEngine.exe --perspective --watch-mesh
~~~

Press G to swap the two object entities' mesh handles. Press it again to restore them. The floor keeps its original handle. G also works in Perspective without file watching. (It was F6 until C7's developer preview key took F6 over; the swap is a mapped game action since 2026-11-26.)

Export a single mesh object from Blender to either source FBX under `Content/Meshes`. With mesh development watching enabled, initial geometry and subsequent saves use the configured source Content root. Ordinary runs use staged FBX beside the executable. The startup log shows the absolute source paths being watched. Exporting box geometry over `Sphere.fbx` updates entities using that asset. Exporting sphere geometry over `Cube.fbx` updates both the box and the floor when their original assignments are active.

The worker waits for settled file bytes, decodes them, and drops a result superseded during decoding. Between frames, the rendering thread publishes the new vertex buffer, index buffer, index count, and local bounds behind the same handle and mesh object. Each frame keeps the mesh data it recorded until its fence signals. A bad or missing file or a failed buffer allocation leaves the last good mesh active.

Windows Debug and Release frame captures verified F6, separate source replacements, shared consumers, burst saves, and recovery from invalid and missing files with Vulkan synchronization validation enabled. A native renderer probe checked that 51 replacements preserved the original handle and object and that old buffers were released after the two submitted frame slots completed. The probe also forced index buffer memory allocation to fail after vertex buffer creation and verified that the previous geometry survived and a subsequent replacement succeeded.

Across 12 saves per preset without the screenshot layer, save to present took 138–187 ms in Debug (168 ms median) and 135–187 ms in Release (156 ms median). The longest replacement frame took 26.7 ms in Debug and 26.6 ms in Release, measured from the start of the sample frame through the present call. Local evidence is under ignored `out/validation/live-mesh/`.

## Samples and batching

The engine game session owns the SceneProject of the running game root (before M6A, each sample client owned a SampleAssets adapter). UUID/type/path/settings and ordered scene records come from `Games/<Name>/Content/Project.ravenproject`; the adapter retains only explicit leases for the 3D diagnostic quads. Scenes acquire their own dependencies through AssetStore. UUIDs are never regenerated at startup. Host clients, projects and their leases are destroyed before the renderer.

The samples use the same draw and batching paths for initial and reloaded assets. The asset cache ensures requests for the same ID use the same texture object. Current batching combines adjacent draws using the same mesh and texture within one pass while preserving transparent ordering. A shared FBX file ID alone does not cause its contents to render in one draw.

## ECS and subsequent asset work

The Windows samples retain GameInstance and Registry during game DLL replacement. The DLL updates the Orthographic sprite's Transform and the opt-in Perspective camera's pose and FOV. Transform/StaticMesh drives Perspective colored geometry, and Transform/Sprite drives Orthographic through the catalog/cache and one shared quad. Both samples select ECS cameras. The [gameplay reload checks](EngineModules.md#windows-gameplay-hot-reload-validation) verified that submitted geometry and cached texture objects retain their identity across replacements. Scene owns each level's entities, selected camera, bindings and resource leases. The host retains the project and shared quad across changes; managed resources retire when the last owner leaves.

The reload sample keeps renderer, OrthographicSample and SampleAssets initialization in the retained host. Game-code replacement does not reconstruct the client/cache or allocate replacement mesh/texture resources. The host also retains the wall watcher and its current texture across a game-code reload. See [EngineModules.md](EngineModules.md).

The [Orthographic level files](EngineModules.md#file-authored-orthographic-levels) reference registered texture IDs. Level validation checks the catalog and texture files before switching. An already loaded resource keeps its last good contents when a source disappears; new scene acquisition still validates required source existence. Both supported codecs now switch resource sets through persistent metadata, generation-checked handles and GPU-safe retirement; broader scene schemas remain future work.

## Live Courtyard wall reload

Build the Windows preset once. From the repository root run:

~~~powershell
.\out\build\x64-debug\RavenEngine\RavenEngine.exe --game --watch-wall
~~~

Use the corresponding path under x64-release for Release. Press F4 to enter Courtyard. Edit and save [Content/Textures/CourtyardWall.png](../Content/Textures/CourtyardWall.png) in the configured source Content root. The watcher logs the absolute path independently of the launch directory. Source Content supplies both the initial wall and subsequent saves in this development mode; missing required sources reject loading instead of falling back to a staged baseline. Use --watch-game to enable automatic game builds and live scene/project editing as well. See [complete commands and roots](SceneWorkflow.md#complete-launch-commands).

The wall keeps its sRGB catalog entry and UUID 638a4a91-4e75-4c27-ae9d-b7f8026cf043. The worker polls file contents at 40 ms intervals and waits for 100 ms of stability before validating PNG chunks and decoding. It hands off the latest decoded image. A bad or missing save logs a diagnostic and leaves the current wall visible. Saving a valid file again recovers automatically. GPU upload runs on the rendering thread before draw collection. The candidate upload fence covers earlier draws on the same graphics queue. Raven then rewrites the existing descriptor, swaps the native image resources behind the same texture reference, and destroys the superseded image, view, sampler, and memory. A failed upload leaves the current binding and image intact. F3, F4, and compatible game DLL replacement reuse the cached texture and watcher.

### Windows verification

Stock Debug and Release captures verified changed wall pixels during play and after a win. The stock tests exercised truncated and missing saves, recovery by rename, F3, F4, and saving while minimized. The focused state probe checked player and courier poses, courier velocity, selected camera, retained outcome, texture reference and descriptor identity, and rollback after a rejected upload. A Debug run from the repository root used --watch-game --watch-wall and applied a game speed edit and a wall edit during the same attempt. Both source files were restored. The PNG worker probe also covered checksum corruption, oversized input, and rapid saves.

Vulkan API traces covered 75 timed replacements per preset with core and synchronization validation enabled. The wall used one of three descriptor sets throughout. Each run peaked at four texture samplers including the in-flight candidate. The trace returned to zero live images, views, samplers, and descriptor pools at shutdown. Resource counts stayed bounded across the run.

| Windows build | Live save to first present, median / worst | Save, build, relaunch to observed wall, median / worst | Longest reload frame work | Upload and publication, median / worst |
| --- | ---: | ---: | ---: | ---: |
| x64-debug | 186.3 / 217.8 ms | 3.48 / 3.84 s | 38.1 ms | 29.9 / 37.7 ms |
| x64-release | 187.8 / 208.4 ms | 3.17 / 3.44 s | 36.6 ms | 29.8 / 36.4 ms |

Live timing used 14 edits per preset on an RTX 3070 Ti Laptop GPU. It measures from a completed file replacement to the successful Vulkan present call and excludes the screenshot and API dump layers. Frame work is measured from the start of the sample draw callback through present. Upload time includes the synchronous fence wait. The five relaunch trials per preset measure until the expected pixel is observed in a screenshot and include capture overhead. Separate live capture runs verified the pixels and showed substantial overhead from the LunarG screenshot layer. Logs, scripts, and timings are under ignored out/validation/live-texture/.

The Courtyard validation above covers its wall PNG. The material service now supports one worker per used texture ID, as recorded in [mesh materials](MeshMaterials.md). The current synchronous upload can exceed one frame budget. Generalized asset watching and native macOS and Linux verification remain open. See [Courtyard shader reload](EngineModules.md#live-courtyard-shader-reload) for the separate Windows shader workflow.

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
