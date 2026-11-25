# Scene and asset development workflow

Each game root (`Games/<Name>`, see [EngineModules.md](EngineModules.md#game-roots-m6a)) has its own content folder with a project catalog and scene files. Add supported assets and scenes through files, then save to observe them in a development session. Game rules live in the game's module. Since M6A (2026-11-24, [DataModelDesign.md](DataModelDesign.md)), scenes use one generic, self-describing format for every component. Prefabs (FD-15) are later work.

## Project data

`Games/<Name>/Content/Project.ravenproject` is UTF-8 text with a `raven-project 2` header. Blank lines and `#` comments outside quoted paths are allowed. Paths are Content-relative; use forward slashes and quotes around paths containing spaces. Existing asset UUIDs remain stable when their paths change.

```text
raven-project 2
texture 7a590cee-ec82-4a27-927f-860e64619c19 "Textures/UVGrid.png" srgb
mesh 6938c6b9-72ca-4bc1-b0bf-9dc31e158087 "Meshes/Cube.fbx"
audio ef451c5a-02db-4d81-89a1-ec2ea98ed719 "Audio/Win.wav"
scene 78e12fa6-6bc9-4ad6-953c-ec8f45ad4180 "Levels/Perspective.ravenscene"
```

**Records.**
- Records use UUIDv4 identities in one namespace.
- `texture` requires a PNG and `srgb` or `linear`. `mesh` requires a single-object static FBX. `audio` requires 16-bit PCM WAV.
- Scenes are `.ravenscene` files in the generic format. The scene-record order is the F4 order.
- `fixed-step <Hz>` (optional, 10–480, default 60) sets the simulation rate ([FD-34](GameServicesDesign.md#fd-34--smaller-services)). The rate is fixed for a session: an edit during a run logs a warning and applies at the next start. Readers that predate it skip it.
- `physics-limits <bodies> <body pairs> <contact constraints>` (optional, each 16–4194304, default 65536, 65536 and 10240) sets the physics world's fixed capacities ([Physics.md](Physics.md#project-limits)). Like the step rate, it is fixed for a session.
- A record kind this reader does not know is skipped with a warning, so later versions can add records (FD-10).

**Rejected.** The catalog rejects:
- duplicate or nil identities and unsupported settings;
- incorrect source extensions;
- rooted paths, parent escapes and paths that escape through physical links;
- missing source files.

Inactive scene files are not instantiated during catalog loading.

## Scene files

A `.ravenscene` file ([SceneFile](../RavenEngine/Public/Raven/Scene/SceneFile.hpp)) stores:
- the entities, with their random 64-bit IDs and optional labels;
- one section per component type, with the type name, layout hash, field list (name, kind, extents, asset-type tag, size) and the values packed field by field.

**Loading.**
- A file written with another component layout loads by field name and type. New fields take their defaults; renamed fields and types are found through `RAVEN_COMPONENT_ALIASES`.
- Sections and fields the running code does not know are logged and skipped. Tools keep them when they rewrite a file.
- Encoding is deterministic, so saving an unchanged scene gives identical bytes.
- Parents are stored as `ParentComponent` and applied through `Scene::SetParent`. References between entities are `EntityRef` values. `WorldTransformComponent` is derived and never stored.
- Loading leases every `AssetRef` in the file, so a scene that names a missing file is rejected before it replaces the active one.

**Tools.** `RavenSceneTool <Content-root> <command> ...` works on any game's scenes:

| Command | Effect |
| --- | --- |
| `validate <scene>` | Decodes and counts entities, component types and unknown sections |
| `dump <scene> [<text>]` | Lossless text form (`raven-scene-text 1`), to a file or the console |
| `apply <text> <scene>` | Writes the text form back as a binary scene |
| `position\|rotation\|scale <scene> <label> <X> <Y> <Z>` | Edits the labeled entity's `TransformComponent` |
| `stage-player <destination> --scene <UUID>... [--asset <UUID>...]` | Copies the selected scenes and every asset they reference (from their `AssetRef` fields) for a player |

The Samples game's tool-made scenes are regenerated with `SamplesTool create-puzzle|create-collection|create-physics <output>` and `SamplesTool create-benchmark <output> <mesh-count>`.

### Runtime hierarchy contract

[Scene](../RavenEngine/Public/Raven/Scene/Scene.hpp) keeps the hierarchy as a host-written `ParentComponent` plus a runtime index: a child list and a depth per entity, with entities bucketed by depth. Spawn, destroy and reparent cost the entities they touch.

- `SetParent(child, parent)` validates live ownership and ancestry before changing anything. A null parent detaches.
- Reparent and detach preserve local TRS, so the world pose can change.
- `DestroyEntity` cascades through the owned subtree.
- If a parent is destroyed directly in the Registry, its children become detached roots and are logged once. If the registry reuses that slot, they become roots.

World transforms are computed as `parentWorld * T * Rz * Ry * Rx * S`. Camera world position uses the complete affine ancestor chain. Camera axes use only rotations, so the view stays rigid. The engine draws the camera tagged `ActiveCameraComponent`.

## Source roots and staging

**Development.** The default development launch reads the game's source content folder directly, and watches the project, the active scene and every acquired PNG and FBX. The host logs `Scene content root`, `Watching project`, `Watching scene`, and each acquired asset's absolute path.

**Staged runs.** With `--no-watch`, the host reads the staged copy beside the executable (`Games/<Name>/Content`). `cmake/RavenContent.cmake` discovers every game's content automatically and stages it with the engine shaders and licenses. A newly created source asset can be used immediately in development; the next host build stages it. Do not full-build a host while it is running.

## Scene and resource ownership

[SceneProject](../RavenEngine/Public/Raven/Scene/SceneProject.hpp) owns the committed catalog, scene list and an [AssetStore](../RavenEngine/Public/Raven/Assets/AssetStore.hpp). Its constructor accepts the game's Content root; the engine game session selects the source or staged root.

[SceneSession](../RavenEngine/Public/Raven/Scene/SceneSession.hpp) owns an active scene and the engine's factory, which decodes, instantiates and leases a scene file. [Scene](../RavenEngine/Public/Raven/Scene/Scene.hpp) owns its entity handles, persistent IDs, labels and asset leases. Entity destruction affects only owned handles, including when Registry slots have been reused.

AssetStore keeps weak cache entries. Scenes and other explicit application owners hold `AssetLease` values. Acquiring the same UUID in the same store shares its resource. A candidate scene acquires its dependencies while the outgoing scene still holds its leases, preserving shared runtime identities during switching. Releasing the last lease stops the source worker and releases the managed resource. Expired cache entries are pruned. The transparency demo quads are sprite entities, so their textures are scene leases like any other.

Project/session operations, resource access and final lease release run on the main/rendering thread. Worker decoders retain only owned CPU snapshots, not leases or Registry references.

GameInstance and Registry outlive the active scene. The renderer outlives the project and every asset lease. Scene factories belong to the engine; game-DLL callbacks and objects never enter retained scene/ECS state. Game code borrows `SceneAccess`, picks and commands through its `SystemContext` during its synchronous update. AssetLease and other ownership objects are not ComponentValue fields. Audio playback retains shared PCM data until its backend voice is stopped or destroyed.

```mermaid
flowchart LR
    Files[Project and level files] --> Project[SceneProject]
    Project --> Session[SceneSession]
    Session --> Scene[Active Scene]
    Scene --> Registry[Host Registry entities]
    Scene --> Leases[Asset leases]
    Leases --> Store[AssetStore weak cache]
    Store --> Renderer[Managed renderer handles]
    Renderer --> Frames[GPU frame retention]
    Game[Replaceable game DLL] --> Registry
```

## Runtime handles and GPU retirement

A persistent AssetId identifies authored content. MeshHandle and TextureHandle identify a runtime slot, renderer context and generation. Reuse advances the generation; null, foreign, released and stale handles fail resolution. Identity-preserving source replacements keep the active handle and texture descriptor while swapping the uploaded contents.

`CreateManagedMesh` and `CreateManagedTexture` are released through their matching APIs. Release invalidates the handle immediately. Submitted frame slots retain the exact mesh buffers and texture objects until their fences complete. `CollectResources` polls completed slots; normal frame and asset-store processing calls it automatically. Non-rendering clients can call it explicitly after retiring scenes. Individual descriptor sets are freed after GPU use and their pool capacity is reused. Empty descriptor pages and slot metadata retain peak capacity, while live resources, bindings, cache entries and workers return to the current owners' needs.

Legacy services remain standalone and do not participate in SceneProject catalog transactions. Legacy `CreateMesh`, `CreateMeshHandle`, `CreateTexture`, and `AssetResources` preserve their context-lifetime borrowed-reference contract. Their resources are pinned and the managed release APIs refuse to release them. A reference obtained from a managed lease is borrowed only while a lease keeps that resource live. Runtime handles can become stale after the last owner leaves; persistent asset references remain the authored identity.

## Publication and recovery

Project and scene source bytes settle on workers. PNG/FBX workers also decode immutable snapshots. Scene codecs and GPU preparation run on the host before publication. A candidate owns all newly acquired dependencies and entities until it is accepted. Source bytes are checked again after preparation; superseded candidates are discarded. Prepared metadata updates commit resource contents into existing runtime identities without replacing the scene.

| Edit | Successful result | Failed result |
| --- | --- | --- |
| Active level save or F3 | New authored attempt, owned entities/bindings replaced, outcome reset and old cue stopped | Current attempt stays active |
| F4 | Next valid scene in the project's catalog order | Current scene stays active |
| Asset path/color-space metadata | Prepared contents and workers replace existing contents; UUID, runtime handle, entity pose and selection stay | Previous catalog/resources stay active |
| Catalog scene-path change for the active ID | Prepare the new scene and dependencies, then publish together | Previous catalog and scene stay active |
| PNG/FBX save | Existing runtime resource receives new contents; gameplay state stays | Last good resource stays active |
| Compatible game DLL | Update code changes; scene, entities, selection, values and attempt outcome stay | Active DLL stays on build/contract failure |

Removing the active scene is rejected until the user switches away. Removing or changing the kind of a leased asset is rejected while an owner still needs it. Correcting a failed dependency retries without requiring another metadata/level save. Dependency-blocked scene/project candidates retry at 200 ms; project parse/source failures retry after settling with duplicate diagnostics suppressed. Newly saved snapshots supersede pending work. WAV clips load through scene ownership; live WAV source watching is not part of this version.

## Complete launch commands

Build through CMake Tools or a Visual Studio developer environment before launching. The default launch develops the Samples game: it reads source content, watches it, and rebuilds the module on save.

```powershell
# Samples (3D), Debug or Release
& 'D:\RavenEngine\outuildd-debug\RavenEngine\RavenEngine.exe'
& 'D:\RavenEngine\outuildd-release\RavenEngine\RavenEngine.exe' --orbit

# Courtyard (2D)
& 'D:\RavenEngine\outuildd-debug\RavenEngine\RavenEngine.exe' --game Courtyard
```

Game-only builds while running:

```powershell
cmake -P 'D:\RavenEngine\outuildd-debug\RavenEngine\GameBuild\Samples\BuildGame.cmake'
cmake -P 'D:\RavenEngine\outuildd-debug\RavenEngine\GameBuild\Courtyard\BuildGame.cmake'
```

## Repeatable authoring check

1. Start either development command and note its absolute watch paths.
2. Copy a supported mesh and PNG into new source paths under the game's Content. Choose new UUIDv4 IDs and add their metadata records.
3. Copy a scene, `dump` it, reference the new IDs in its `StaticMeshComponent`, `MaterialComponent` or `SpriteComponent` rows, `apply` it under a new name, and append a scene record with a new UUID. Save the catalog. The active scene keeps its state.
4. Press F4 to reach the new scene. Change and save its values; the authored attempt restarts automatically. F3 is optional.
5. Save malformed scene/metadata/asset data. The last good world remains visible. Correct the data and verify automatic recovery. Test creating a previously missing referenced file without resaving the catalog.
6. Switch repeatedly between scenes with shared and exclusive assets. Check renderer/store diagnostics and watch logs: shared resources persist, exclusive handles expire, and counts stay bounded after frame completion.
7. Edit a game function body or a game component, verify replacement and preserved scene state (components migrate by field name), then check failed-build recovery. Engine public header changes still require a host rebuild/restart.

Restore validation data before staging or packaging. Native macOS/Linux execution and tracked regression integration remain open; CTest is deferred. Local executable probes and capture-based evidence are under ignored `out/validation/scene-workflow/`.


## Windows verification — 2026-11-19

Both Windows presets build and run the shared services. The focused probes are local ignored artifacts, not registered CTest tests or a regression suite available from a clean checkout.

| Check | Debug and Release evidence |
| --- | --- |
| Data-only authoring | Stock sessions created new FBX/PNG/level files during play, published metadata, and displayed the added F4 scene without sample source edits or a host rebuild/restart |
| Scoped lifetime | 100 managed-resource cycles; shared lease identity, stale handles after slot reuse, prepared-upload supersession, texture handle/descriptor retention, released mesh buffers and bounded slots/pools |
| Scene ownership | Actual sample codecs exercised 60 switches per family across shared/exclusive resources, candidate rollback, owned-only destruction, stale-camera switching and restart |
| Validation/recovery | Format/version/UUID/type/path/Unicode checks; duplicate/missing metadata, malformed PNG/FBX/scene, missing active level, corrected dependencies without resaving metadata, and superseded scene snapshots |
| DLL state | Both families retained entity/player/camera selection, pose/lens, resource identity and completion through compatible replacement, incompatible rejection and valid recovery; stock failed builds retained pixels and recovered |
| Rendering/controls | Existing Perspective/Gallery pixels match pre-milestone captures; F6/T, orbit, F2/Home, resize and minimize/restore pass; 2D PNG and both shader passes recover exact pixels after rejected edits |
| Audio | Endpoint loopback captured the two expected 3D win cues; restart, Courtyard completion and scene-owned audio playback pass |
| GPU/header integrity | Core/synchronization validation is clean; traced resources balance at shutdown; 24 shared-resource switches allocate no additional buffers/images/descriptors; eight new/changed public headers compile independently |

The stock 3D timing samples measured level lighting activation at 0.174 s Debug / 0.212 s Release, material activation at 0.236 s / 0.235 s, and game source edit/build/activation at 2.552 s / 2.738 s. These are single local observations with screenshot capture enabled, not performance guarantees. Initial resource acquisition and catalog-dependent decode/upload remain synchronous on the host; subsequent watched PNG/FBX decoding runs on workers.

Current local validation commands, after building both hosts and with no running sample during preparation:

```powershell
Set-Location -LiteralPath 'D:\RavenEngine'
python 'D:\RavenEngine\out\validation\scene-workflow\prepare-probes.py'
python 'D:\RavenEngine\out\validation\scene-workflow\prepare-reload-probes.py'
cmd.exe /d /c 'D:\RavenEngine\out\validation\scene-workflow\x64-debug-probes.cmd'
cmd.exe /d /c 'D:\RavenEngine\out\validation\scene-workflow\x64-release-probes.cmd'
cmd.exe /d /c 'D:\RavenEngine\out\validation\scene-workflow\x64-debug-reload-probes.cmd'
cmd.exe /d /c 'D:\RavenEngine\out\validation\scene-workflow\x64-release-reload-probes.cmd'
cmd.exe /d /c 'D:\RavenEngine\out\validation\scene-workflow\x64-debug-orthographic-reload.cmd'
cmd.exe /d /c 'D:\RavenEngine\out\validation\scene-workflow\x64-release-orthographic-reload.cmd'
python 'D:\RavenEngine\out\validation\scene-workflow\run-probes.py' x64-debug
python 'D:\RavenEngine\out\validation\scene-workflow\run-probes.py' x64-release
python 'D:\RavenEngine\out\validation\scene-workflow\reload-state.py' x64-debug
python 'D:\RavenEngine\out\validation\scene-workflow\reload-state.py' x64-release
python 'D:\RavenEngine\out\validation\scene-workflow\live.py' x64-debug
python 'D:\RavenEngine\out\validation\scene-workflow\live.py' x64-release
python 'D:\RavenEngine\out\validation\scene-workflow\live-2d.py' x64-debug
python 'D:\RavenEngine\out\validation\scene-workflow\live-2d.py' x64-release
python 'D:\RavenEngine\out\validation\scene-workflow\play.py' x64-debug
python 'D:\RavenEngine\out\validation\scene-workflow\play.py' x64-release
```

The live scripts temporarily change repository input files and restore their saved bytes in cleanup. Run these validations serially so they do not share mutable source inputs. Pixel/trace/audio helpers use the installed Vulkan SDK layers and local Python/Windows endpoint tools. The manual authoring check above remains usable without those ignored probes.

Native macOS/Linux execution, a tracked automated regression suite, general component/asset serialization, prefabs, import dependency/build-output metadata and live WAV watching remain open. Legacy pinned resources intentionally retain renderer lifetime. Descriptor pages and runtime slot arrays retain peak capacity; the tests verify bounded reuse and released live objects/bindings, not return of every allocation page to the OS after each scene switch.

## Historical hierarchical 3D validation — 2026-11-19

This record describes the text writer before binary migration. Its ignored probes target that earlier API and are historical evidence. Current runnable checks and commands are recorded in [InteractionGame.md](InteractionGame.md).

Windows Debug and Release builds and 3D-only native probes pass: nested local/world propagation, nonuniform scale/reflection/shear, inverse-transpose normals, transformed AABBs, rigid parented-camera rays, cycle/self/stale/cross-scene rejection, local-preserving detach/reparent, cascade destruction and binding cleanup. Version 1/2 flat files remain readable; version 3 writer round trips retain authored IDs, parents, local transforms, materials/assets, lighting, camera settings and gameplay/audio bindings. Invalid saves, locked-destination replacement failure, Unicode paths and temporary-file cleanup pass. Live saves publish once, restart authored state, reject invalid/superseded candidates and recover without F3.

Both configurations verify real DLL-controlled child rotation and descendant movement, host-controlled second-arm propagation, compatible/incompatible DLL replacement, retained camera pose/selection, hierarchy, completion tint and mesh handles, malformed FBX retention/recovery, 30 scene switches with bounded slots/descriptors and zero remaining scene-owned assets/workers/entities after retirement. The renderer's one pinned white-texture binding remains its baseline. Stock 3D sessions show root/lighting edits, FBX changes and last-good pixels, DLL-only speed edits, intentional failed game builds and valid recovery, T/F6, ground picking, standalone orbit/Home, resize/minimize/restore and normal shutdown. Vulkan core/synchronization validation reports no warnings/errors. Screenshots and native sources/logs are local under ignored `out/validation/scene-hierarchy/`; they are not a tracked regression suite.

Temporary production level/FBX/game-source edits were restored. Existing workspace edits were preserved. The Perspective codec now retains material names for saving, separates file parsing/writing from runtime behavior, and removes its unused adapter constructor. All existing command-line options still have callers and remain supported. No 2D runtime tests were run; CTest stays deferred. Native macOS/Linux execution, general component/asset serialization, multi-camera authoring, prefabs, editor UI, multi-object FBX/import dependencies, broader physics and additional rendering features remain outside this milestone. Atomic saves promise visibility on the supported local filesystem, not power-loss durability or concurrent-writer conflict resolution.

The complete native probe commands below use ignored local validation sources. Run from a Visual Studio development environment after the full host builds, with no host running during those builds. `prepare.py` refreshes isolated Content/runtime copies, and each generated command file initializes its own toolchain. The live scripts then use the real source Content root and restore their edits.

```powershell
Set-Location -LiteralPath 'D:\RavenEngine'
python 'D:\RavenEngine\out\validation\scene-hierarchy\prepare.py'
cmd.exe /d /c 'D:\RavenEngine\out\validation\scene-hierarchy\x64-debug-probes.cmd'
cmd.exe /d /c 'D:\RavenEngine\out\validation\scene-hierarchy\x64-release-probes.cmd'
python 'D:\RavenEngine\out\validation\scene-hierarchy\run-probes.py'
python 'D:\RavenEngine\out\validation\scene-hierarchy\live.py' x64-debug
python 'D:\RavenEngine\out\validation\scene-hierarchy\live.py' x64-release
```

Save-to-activation observations for this run are recorded in each configuration's `live-timings.json`; these are local single-save timings, not latency guarantees. Game edits include polling, background compilation/linking, validation and activation. Level edits require no C++ compilation. Screenshot assertions compare settled rendered frames, including unchanged images through invalid edits and failed builds. See [complete launch commands](#complete-launch-commands) for ordinary use.
