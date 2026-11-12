# RavenEngine

RavenEngine is a work-in-progress C++23 game engine targeting 2D and 3D games, Vulkan rendering, ECS, native platform backends, and hot reload. The current Windows build contains `RavenCore.dll` and a `RavenEngine.exe` sample with native input, perspective/orthographic cameras, reusable meshes, depth-tested colored geometry, opaque/transparent textured quads, frustum culling, compatible draw batching, and ID-based texture reuse.

Track the remaining work and its completion criteria in the [RavenEngine roadmap](ROADMAP.md) and [TODO list](TODO.md).

As of 2026-10-19, Windows also has a joint ECS/game-code reload example: a host-owned GameInstance retains its Registry and copied component values while a separate game DLL is replaced. Source watching and background game builds preserve the active code on build or compatibility failure. General ECS queries, scene ownership/persistence, runtime resource release, imported meshes/lighting, physics, audio and playable games remain pending. The samples still construct objects and asset registrations in C++.

ECS and hot reload are joint goals. Transform/StaticMesh components drive Perspective colored geometry; Transform/Sprite components drive Orthographic textured sprites through GameInstance/Registry. Sprites share a quad and cached textures and support tint, opaque/alpha blending and transparent ordering. CameraComponent projection settings and the renderer handoff are implemented. The [next step is an ECS camera for OrthographicSample](TODO.md#next-implementation-target-cameracomponent), followed by Perspective and direct game-code migration to Transform. Scene ownership remains deferred. The design accommodates future editor editing and play sessions without adding a complete visual editor to v1. Fast iteration refers to the developer edit/build/observe cycle.

All components derive from the abstract `ECS::Component` parent, which supplies virtual destruction, type/size access and synchronous data copying. Registry add/read/write/remove operations accept `Component&` references and retain only copied values, never the object or its virtual-table pointer. Inside each derived type, `RAVEN_COMPONENT(FullyQualifiedType, Field1, Field2, ...);` generates its name and declares its retained fields; omit the fields for a tag component. Scalars, approved engine values/IDs and arrays of approved values are supported. Custom value structs require an explicit ComponentValue declaration; borrowed views and owning types are rejected. The parent contains no rendering or gameplay data. Reusable components have separate headers under `Public/Raven/Components/`, with MeshHandle declared separately from Vulkan draw submissions. Both samples clean up their own entities. See [EngineModules.md](docs/EngineModules.md) for the architecture assessment and remaining limits.

## Source layout

```text
RavenEngine/
  Public/Raven/       Engine headers shared with clients
    Assets/           Asset IDs/references, catalog/cache contracts, and owned CPU images
    Core/             Application, retained GameInstance, GameModule, and utilities
    Components/       Reusable Transform, StaticMesh, Sprite and Camera components
    ECS/              Abstract Component parent, field copying, Entity and Registry
    Math/             Shared transform math
    Platform/         Window and input contracts
    Renderer/         Camera, vertices, draw data, and renderer interface
  Private/            Engine implementation and internal headers
    Assets/           Asset catalog/cache implementations and private image decoder
    Core/             Application loop, utilities, logging implementation, and frame clock
    ECS/              Registry identity, entity slots, generations, and lifecycle
    Game/             Reloadable game logic and shared sample component definitions
    Platform/         Native backend implementations
    Renderer/         Vulkan resource owners and HLSL shaders
    Samples/          Sample entry point, geometry, animation, and camera controls
  CMakeLists.txt      Engine/sample targets, include visibility and build helper ordering
cmake/               Automatic source discovery, shared defaults, content and game builds
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

Use MSVC with C++23 support (Visual Studio 2022 17.13 or newer). Configure and build through VS Code CMake Tools or a Visual Studio developer shell targeting x64:

```powershell
cmake --preset x64-debug
cmake --build out/build/x64-debug
.\out\build\x64-debug\RavenEngine\RavenEngine.exe
.\out\build\x64-debug\RavenEngine\RavenEngine.exe --orthographic
.\out\build\x64-debug\RavenEngine\RavenEngine.exe --watch-game
```

In `--watch-game`, edit `RavenEngine/Private/Game/RavenGame.cpp` (for example, `MovementScale`). The game DLL rebuilds in the background and changes the moving sprite's behavior while retaining its position. `--game` loads the same example without source-build automation; F5 explicitly reloads the built DLL. Build it while running with `cmake -P out/build/x64-debug/RavenEngine/GameBuild/BuildGame.cmake`. Use this standalone build script while the engine runs; a full engine build can try to overwrite loaded engine files.

This is a trusted local development workflow for Windows/MSVC/single-config Ninja. Shared-header or build-contract changes are rejected until the host is rebuilt and restarted. Component layout versions are generated automatically; arbitrary C++ migration, module-owned persistent objects and untrusted plugins are outside this example. Game-only builds reuse the compiler/SDK environment captured during host configuration; runtime watching does not require a developer shell. See [EngineModules.md](docs/EngineModules.md) for ownership, compatibility, build logs and validation limits.

## macOS/Linux contributor work

See the [platform contributor handoff](docs/PlatformHandoff.md) for work order and verification criteria against the current Windows implementation. The [macOS build guide](docs/MacOSBuild.md) documents dependencies and commands. Cocoa framebuffer support, executable paths, and keyboard/mouse input are implemented; earlier Debug checks confirmed rendering and Space/scroll/R controls. The Linux X11/XCB backend and Debug/Release presets are also merged. The current asset system's `Platform::FillRandomBytes` is implemented only on Windows, so macOS/Linux contributors must add that hook and its CMake connection before verifying current Debug/Release builds and both samples. Platform work can proceed alongside Windows ECS development. Mark support complete only after verifying native execution of the current code on each OS.

Windows Debug/Release builds and focused ECS/reload validation are recorded in [EngineModules.md](docs/EngineModules.md). There are no registered CTest tests; integration remains deferred at the owner's request. Historical logs and result summaries under ignored `out/validation/` are local evidence, not a regression suite available from a clean checkout. Temporary source snapshots, probe projects and generated validation builds are removed after use. Earlier renderer evidence remains in the subsystem documents.
