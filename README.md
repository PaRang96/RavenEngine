# RavenEngine

RavenEngine is a work-in-progress C++23 game engine targeting 2D and 3D games, Vulkan rendering, ECS, native platform backends, and hot reload. The current Windows build contains `RavenCore.dll` and a `RavenEngine.exe` sample with native input, perspective/orthographic cameras, reusable meshes, depth-tested colored geometry, opaque/transparent textured quads, frustum culling, compatible draw batching, and ID-based texture reuse.

Launch either Windows development build with no arguments to play Perspective in free-fly mode with live reload for gameplay code, scenes, project metadata, PNGs, FBX meshes, and the lit fragment shader. Press F4 three times for Puzzle or four times for Collection. F3 restarts the active level. Use `--no-watch` to play the same ECS game from staged content with watching and automatic builds disabled. See the [default Windows workflow](docs/DevelopmentReload.md) and [3D gameplay and authoring guide](docs/InteractionGame.md).

Track the remaining work and its completion criteria in the [RavenEngine roadmap](ROADMAP.md) and [TODO list](TODO.md).

On Windows, a host-owned GameInstance retains its Registry and copied component values while a separate game DLL is replaced. Source watching and background game builds preserve the active code on build or compatibility failure. The Arena sample game (`--game Arena`) shows the game services: input mapping, Jolt physics, character movers, UI, audio and the profile store. 3D is the only target: the 2D Courtyard sample was removed on 2026-11-26 ([planning decision 18](ROADMAP.md#planning-decisions-2026-11-24)).

ECS and hot reload are joint goals. Perspective and Orthographic use ECS cameras and shared renderer resources. The Windows game DLL updates the Orthographic sprite's Transform directly and supports an opt-in Perspective free-fly camera. Both samples use host-owned Scene/SceneSession services with a file-backed project catalog, data-driven F4 order, shared asset leases and safe resource retirement. The 3D codec also supports hierarchy, full world transforms and atomic authored save/load; F4 twice opens the shared-asset Assemblies demo. See [the live scene workflow](docs/SceneWorkflow.md) and [TODO.md](TODO.md).

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
    Scene/            Host-owned scenes, project metadata and live sessions
  Private/            Engine implementation and internal headers
    Assets/           Asset catalog/cache implementations and private image decoder
    Core/             Application loop, utilities, logging implementation, and frame clock
    ECS/              Registry identity, entity slots, generations, and lifecycle
    Game/             Reloadable game logic and shared sample component definitions
    Platform/         Native backend implementations
    Renderer/         Vulkan resource owners and HLSL shaders
    Samples/          Sample entry point, scene codecs, rules and camera controls
    Scene/            Scene/project/session ownership and publication
  CMakeLists.txt      Engine/sample targets, include visibility and build helper ordering
cmake/               Automatic source discovery, shared defaults, content and game builds
ThirdParty/stb/       Pinned image decoder, license, and upstream provenance
Content/Textures/    Original PNG patterns used by the sample
```

Clients include headers such as `Raven/Core/Application.hpp` through the `RavenCore` target. Only `Public` is shared as an engine include directory. `VulkanContext` stores its implementation privately, and meshes are borrowed through an opaque reference; public renderer headers do not include internal Vulkan owners. See [EngineModules.md](docs/EngineModules.md) for exports and ownership.

Windows input uses shared state rules and a pending/published buffer owned by each window. The Win32 event pump owns the thread message queue, while key translation and native mouse capture stay in the Windows backend. See [InputProcessing.md](docs/InputProcessing.md) for ownership and snapshot timing.

Developer utilities include `Paths::GetContentPath()`, `GetShaderPath()`, executable paths, and binary/text file readers. Validated shaders are staged in `Content/Shaders` beside the executable; copying the executable, DLL, and `Content` together preserves shader lookup from any working directory. See [RuntimeUtilities.md](docs/RuntimeUtilities.md) for usage and application content-root configuration.

`VulkanContext::CreateManagedTexture()` uploads RGBA8 textures in Linear or SRGB mode behind a releasable handle; `AssetStore` leases are the way game content reaches it. Texture descriptors and native image/view/sampler ownership stay private. See [TextureResources.md](docs/TextureResources.md) for the contract and GPU validation.

The four-argument `DrawFrame()` adds straight-alpha draws with explicit layer/order and automatic camera-depth sorting. A private preparation stage performs per-instance frustum selection before Vulkan command recording. The sample shows overlapping transparent quads; `F1` or physical `O` toggles explicit ordering, arrows orbit, wheel zooms, Home/R resets, and Space pauses animation.

Run with `--game Arena` for the M6C sample game: steer the craft with WASD or the arrows, jump with Space, turn the camera with Q/E and pause with Esc. Collect the cyan energy cells and avoid the red drones; the best score is kept in `out/User/Arena`. Ctrl+F5 and Ctrl+F9 quick-save and quick-load the world.

`Images::Load()` decodes PNG/JPEG files into owned RGBA8 CPU pixels for texture creation. The pinned decoder is compiled privately into RavenCore; builds need no manual image-library download or additional decoder DLL. Its license is staged in `Licenses` beside the executable. The sample loads two catalog PNGs and displays them alongside the imported Blender cube, sphere, and ground slab. See [ImageLoading.md](docs/ImageLoading.md) for usage, format/size limits, and validation.

Each game root's content gives every asset a stable UUID: imported files in a `.ravenasset` sidecar, scenes and sound events in their header; `Games/<Name>/Content/Project.ravenproject` holds the project settings and scene order. Scenes use one generic, self-describing format that `RavenSceneTool dump` and `apply` turn into text and back. AssetStore shares resources through host-owned leases; managed runtime handles reject stale generations and retire unused GPU resources after frame completion. The default development launch reloads source scenes and project metadata automatically. Legacy AssetResources/CreateTexture references keep their context-lifetime contract. See [SceneWorkflow.md](docs/SceneWorkflow.md) for adding content without C++ edits, complete launch commands, ownership and recovery, and [AssetSystem.md](docs/AssetSystem.md) for asset details.

On Windows, the default development launch watches every engine shader stage and include. See the [Windows 3D workflow](docs/DevelopmentReload.md).

In Perspective, press G to swap the cube and sphere mesh sources (F6 is the developer preview key). The development launch watches the game's source content root, independent of the launch directory. See [live Perspective mesh reload](docs/AssetSystem.md#live-perspective-mesh-reload).

## Build on Windows

Use MSVC with C++23 support (Visual Studio 2022 17.13 or newer). Configure and build through VS Code CMake Tools or a Visual Studio developer shell targeting x64:

```powershell
cmake --preset x64-debug
cmake --build out/build/x64-debug
.\out\build\x64-debug\RavenEngine\RavenEngine.exe
.\out\build\x64-debug\RavenEngine\RavenEngine.exe --orbit
.\out\build\x64-debug\RavenEngine\RavenEngine.exe --game Arena
.\out\build\x64-debug\RavenEngine\RavenEngine.exe --no-watch
```

Run the tracked tests with `ctest --preset x64-debug`, or `ctest --preset x64-debug -LE gpu` to skip tests that need a Vulkan device. Tests enroll automatically from `Tests/**/<Name>Tests.cpp`; see [Tests/CMakeLists.txt](Tests/CMakeLists.txt). Run `out\build\x64-release\RavenEngine\EcsBenchmark.exe` for the ECS baseline recorded in [ECSDesign.md](docs/ECSDesign.md#performance-budgets). CI builds both x64 presets and runs the CPU tests on every push and pull request ([workflow](.github/workflows/windows.yml)).

Run the [Windows 3D smoke check](docs/InteractionGame.md#repeatable-windows-3d-smoke) with `pwsh -File scripts/Test-Windows3D.ps1 -Preset x64-debug` or `-Preset x64-release` after configuring that preset.
Add `-DefaultLaunch` to exercise the stock executable with live source edits in an isolated source copy. The Release run also measures frame times. See the [validation commands](docs/DevelopmentReload.md#validation).

Build the [Windows x64 player folder](docs/PlayerBuild.md) with `python scripts/BuildPlayer.py --preset x64-release --profile public-preview`. Edit a build profile to control which levels go into the folder.

Profile the development-only [Windows 3D benchmark](docs/Benchmark3D.md) with `python scripts/Benchmark-Windows3D.py measure current`.

Game code lives in its game root, outside the engine (`Games/Samples/Source`, `Games/Arena/Source`). In the default development launch, edit a game source or component header: the module rebuilds in the background and reloads, and component values migrate by field name. F5 explicitly reloads the built module. Build it while running with `cmake -P out/build/x64-debug/RavenEngine/GameBuild/<Name>/BuildGame.cmake`; a full engine build can try to overwrite loaded engine files. In the Samples game, use WASD and Q/E to move, right-drag to look, the wheel for FOV, and left-click to pick. Edit `FreeFlySpeed` to test camera movement reload.

This is a trusted local development workflow for Windows/MSVC/single-config Ninja. Engine public-header or build-contract changes are rejected until the host is rebuilt and restarted. Module-owned persistent objects and untrusted plugins are outside this example. Game-only builds reuse the compiler/SDK environment captured during host configuration; runtime watching does not require a developer shell. See [EngineModules.md](docs/EngineModules.md) for ownership, compatibility, build logs and validation limits.

## Coding rules

Every change follows [docs/CodingRules.md](docs/CodingRules.md), eleven rules adopted by the owner on 2026-11-27.

## macOS/Linux contributor work

See the [platform contributor handoff](docs/PlatformHandoff.md) for work order and verification criteria against the current Windows implementation. The [macOS build guide](docs/MacOSBuild.md) documents dependencies and commands. Cocoa framebuffer support, executable paths, and keyboard/mouse input are implemented; earlier Debug checks confirmed rendering and Space/scroll/R controls. The Linux X11/XCB backend and Debug/Release presets are also merged. The current asset system's `Platform::FillRandomBytes` is implemented only on Windows, so macOS/Linux contributors must add that hook and its CMake connection before verifying current Debug/Release builds and both samples. Platform work can proceed alongside Windows ECS development. Mark support complete only after verifying native execution of the current code on each OS.

Windows Debug/Release builds and focused ECS/reload validation are recorded in [EngineModules.md](docs/EngineModules.md) and the milestone design notes. Historical logs and result summaries under ignored `out/validation/` are local evidence, not a regression suite available from a clean checkout. Temporary source snapshots, probe projects and generated validation builds are removed after use. Earlier renderer evidence remains in the subsystem documents.
