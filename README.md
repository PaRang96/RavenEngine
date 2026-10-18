# RavenEngine

RavenEngine is a work-in-progress C++20 game engine targeting 2D and 3D games, Vulkan rendering, ECS, native platform backends, and hot reload. The current Windows build contains `RavenCore.dll` and a `RavenEngine.exe` sample with native input, perspective/orthographic cameras, reusable meshes, depth-tested colored geometry, opaque/transparent textured quads, frustum culling, compatible draw batching, and ID-based texture reuse.

Track the remaining work and its completion criteria in the [RavenEngine roadmap](ROADMAP.md) and [TODO list](TODO.md).

As of 2026-10-14, the Windows rendering foundation is ready for a minimal scene/game framework. The next task is entity identity and lifecycle, followed by an ECS-backed orthographic sample. Scene persistence, runtime resource release, imported meshes/lighting, physics, audio, playable games and runtime reload remain pending. The samples currently construct objects and asset registrations in C++.

## Source layout

```text
RavenEngine/
  Public/Raven/       Engine headers shared with clients
    Assets/           Asset IDs/references, catalog/cache contracts, and owned CPU images
    Core/             Application/client interface, paths, files, logging, and frame time
    Math/             Shared transform math
    Platform/         Window and input contracts
    Renderer/         Camera, vertices, draw data, and renderer interface
  Private/            Engine implementation and internal headers
    Assets/           Asset catalog/cache implementations and private image decoder
    Core/             Application loop, utilities, logging implementation, and frame clock
    Platform/         Native backend implementations
    Renderer/         Vulkan resource owners and HLSL shaders
    Samples/          Sample entry point, geometry, animation, and camera controls
  CMakeLists.txt      Explicit engine/sample sources and include visibility
ThirdParty/stb/       Pinned image decoder, license, and upstream provenance
Content/Textures/    Original PNG patterns used by the sample
```

Clients include headers such as `Raven/Core/Application.hpp` through the `RavenCore` target. Only `Public` is shared as an engine include directory. `VulkanContext` stores its implementation privately, and meshes are borrowed through an opaque reference; public renderer headers do not include internal Vulkan owners. See [EngineModules.md](docs/EngineModules.md) for exports and ownership.

Windows input uses shared state rules and a pending/published buffer owned by each window. The Win32 event pump owns the thread message queue, while key translation and native mouse capture stay in the Windows backend. See [InputProcessing.md](docs/InputProcessing.md) for ownership and snapshot timing.

Developer utilities include `Paths::GetContentPath()`, `GetShaderPath()`, executable paths, and binary/text file readers. Validated shaders are staged in `Content/Shaders` beside the executable; copying the executable, DLL, and `Content` together preserves shader lookup from any working directory. See [RuntimeUtilities.md](docs/RuntimeUtilities.md) for usage and application content-root configuration.

`VulkanContext::CreateTexture()` owns and synchronously uploads RGBA8 textures in Linear or SRGB mode. `CreateTexturedMesh()` and the additional `DrawFrame()` overload render UV geometry with a texture, model transform, and RGBA tint. Texture descriptors and native image/view/sampler ownership stay private. The existing color-mesh API remains available. See [TextureResources.md](docs/TextureResources.md) for the contract and GPU validation.

The four-argument `DrawFrame()` adds straight-alpha draws with explicit layer/order and automatic camera-depth sorting. A private preparation stage performs per-instance frustum selection before Vulkan command recording. The sample shows overlapping transparent quads; `F1` or physical `O` toggles explicit ordering, arrows orbit, wheel zooms, Home/R resets, and Space pauses animation.

Run with `--orthographic` for the 2D sample. `OrthographicCamera::VerticalSize` sets the full visible height in world units; width follows the drawable aspect ratio. The shared frame path handles both camera types with the same depth, transparency, and frustum rules. Arrows pan, wheel/PageUp/PageDown zoom, Home resets the camera, and F1 toggles transparent order. The two upper sprites have equal world/screen sizes despite different depths. See [the coordinate contract](docs/CoordinateSystem.md).

`Images::Load()` decodes PNG/JPEG files into owned RGBA8 CPU pixels for texture creation. The pinned decoder is compiled privately into RavenCore; builds need no manual image-library download or additional decoder DLL. Its license is staged in `Licenses` beside the executable. The sample loads two staged PNGs and displays them alongside the existing cubes and ground slab. See [ImageLoading.md](docs/ImageLoading.md) for usage, format/size limits, and validation.

`AssetId`, `AssetRef<T>`, `AssetCatalog` and `AssetResources` provide stable UUIDv4 identity, typed references, in-memory loading metadata and synchronous texture reuse within a renderer context. The renderer owns GPU resources until shutdown; destroying an asset service does not unload textures. Persistent catalog/scene files, resource release handles and reload are future work. See [AssetSystem.md](docs/AssetSystem.md).

## Build on Windows

From the **x64 Native Tools Command Prompt for VS 2026** (or another developer shell targeting x64):

```powershell
cmake --preset x64-debug
cmake --build out/build/x64-debug
.\out\build\x64-debug\RavenEngine\RavenEngine.exe
.\out\build\x64-debug\RavenEngine\RavenEngine.exe --orthographic
```

## macOS/Linux contributor work

See the [platform contributor handoff](docs/PlatformHandoff.md) for work order and verification criteria against the current Windows implementation. The [macOS build guide](docs/MacOSBuild.md) documents dependencies and commands. Cocoa framebuffer support, executable paths, and keyboard/mouse input are implemented; earlier Debug checks confirmed rendering and Space/scroll/R controls. The Linux X11/XCB backend and Debug/Release presets are also merged. The current asset system's `Platform::FillRandomBytes` is implemented only on Windows, so macOS/Linux contributors must add that hook and its CMake connection before verifying current Debug/Release builds and both samples. Platform work can proceed alongside Windows ECS development. Mark support complete only after verifying native execution of the current code on each OS.

The Windows Debug/Release builds were checked successfully on 2026-10-14; CTest lists zero registered tests. Earlier native/GPU evidence is recorded in the subsystem documents and ignored `out/` artifacts; those checks were not rerun during that assessment. CTest integration remains deferred at the owner's request and will be revisited when defining entity/component lifetime checks. The `out/` probes are not a regression suite available from a clean checkout.
