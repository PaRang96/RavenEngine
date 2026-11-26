# Reloadable 3D interaction game

> **Since M6A (2026-11-24):** this game lives in `Games/Samples` ([EngineModules.md](EngineModules.md#game-roots-m6a)), its components are game components (`Samples::...`), and scenes use the generic format ([SceneWorkflow.md](SceneWorkflow.md#scene-files)). Sections that name `SceneDocument`, `PerspectiveLevel`, `GameBindings` or `Private/Game` describe the layout before M6A.

The Windows 3D path uses machine-readable binary scenes and reusable DLL gameplay. Launch free-fly development mode, press **F4 three times** to select Puzzle, left-click both orange switches, wait for the purple gate to rise, then fly through it to the cyan goal. Active switches turn green. The goal completes once and plays the existing win cue. F3 restarts. F4 order is Perspective, Gallery, Assemblies, Puzzle.

WASD moves, Q/E changes height, right-drag looks, wheel changes FOV, and left-click picks. Space pauses/resumes component animation while leaving camera control available. G swaps the explicitly bound mesh sources (a mapped action; F6 was taken by the developer preview key in C7). The original Perspective T rotation and standalone orbit/Home controls remain available. The puzzle gates completion through switch/rotator/door state; it uses the existing free-fly controller and does not add general collision or physics.

## Complete launch commands

Build the full host in a Visual Studio developer environment before launching. Shared header/build changes require rebuilding and restarting the host through the generated compatibility contract.

```powershell
# Debug
Set-Location -LiteralPath 'D:\RavenEngine'
& 'D:\RavenEngine\out\build\x64-debug\RavenEngine\RavenEngine.exe' 

# Release
Set-Location -LiteralPath 'D:\RavenEngine'
& 'D:\RavenEngine\out\build\x64-release\RavenEngine\RavenEngine.exe' 

# Game-only builds while a host is running
cmake -P 'D:\RavenEngine\out\build\x64-debug\RavenEngine\GameBuild\BuildGame.cmake'
cmake -P 'D:\RavenEngine\out\build\x64-release\RavenEngine\GameBuild\BuildGame.cmake'
```

## Live binary authoring

3D project records point to `.ravenscene` files. The three earlier scenes were migrated with their scene UUIDs, entity IDs, hierarchy, transforms, materials/assets, lighting, cameras and bindings preserved. The runtime reads the binary codec exclusively. Orthographic retains its existing text codec and was not runtime-tested in this milestone. `RavenSceneTool import` remains an explicit legacy 3D text conversion path in the authoring target, outside the runtime codec.

[RavenSceneTool](../RavenEngine/Private/Tools/RavenSceneTool.cpp) accepts `Content-root command scene arguments`. It loads/validates a document, applies the requested authored edit, validates typed resource references, and saves atomically. The active scene watcher applies a valid save automatically without F3. Every valid changed level save starts a new authored attempt. An unchanged byte snapshot does not restart; publication never writes the file back, preventing reload loops.

```powershell
# Move an assembly root; descendants follow after automatic reload.
& 'D:\RavenEngine\out\build\x64-debug\RavenEngine\RavenSceneTool.exe' 'D:\RavenEngine\Content' position 'D:\RavenEngine\Content\Levels\Puzzle.ravenscene' assembly-a -2.6 9 0

# Change a switch-driven arm's full XYZ angular offset and transition duration.
& 'D:\RavenEngine\out\build\x64-debug\RavenEngine\RavenSceneTool.exe' 'D:\RavenEngine\Content' rotator 'D:\RavenEngine\Content\Levels\Puzzle.ravenscene' arm-a 0 0 0.7853982 0.6

# Change scene-global ambient lighting.
& 'D:\RavenEngine\out\build\x64-debug\RavenEngine\RavenSceneTool.exe' 'D:\RavenEngine\Content' lighting 'D:\RavenEngine\Content\Levels\Puzzle.ravenscene' ambient 0.6 0.2 0.1
```

Other commands are `rotation`/`scale` with node ID and XYZ, `validate`, `import legacy-file binary-output`, `create-puzzle binary-output`, and `create-collection binary-output`. The create commands generate the corresponding sample and replace their output destination. Release provides the same tool under `x64-release`.

[SceneDocument](../RavenEngine/Public/Raven/Scene/SceneDocument.hpp) is the reusable host-side authoring API. `Decode` reads bytes, `Validate` checks schema/relationships, `Encode` produces canonical binary bytes, and `Save(path, catalog, contentRoot)` additionally validates source existence/types before atomic replacement. Other exporters can build a SceneDocument directly. `PerspectiveLevel::GetAuthoringDefinition()` exposes the const source snapshot; copy it before changing authored values.

## Binary format and persistence

The wire format is independent of C++ object layout. All multibyte integers are little-endian; floats are explicit IEEE-754 binary32; UUIDs occupy their canonical 16 bytes. Strings are length-prefixed byte tokens (up to 128 bytes, with no ASCII whitespace/control characters, `#` or quotes). Optional references use an empty token or nil UUID; a nil sound UUID requests a silent scene. Legacy text import supplies the earlier default cue when its sound record is absent. Native bool, enum, padding, pointers, vtables and platform handles are never dumped into files.

| Header offset | Encoding | Meaning |
| --- | --- | --- |
| 0 | 8 bytes | `RVN3DSCN` |
| 8 | u32 | Binary schema version, 1 for the base scene and 2 for scenes with pickups |
| 12 | u32 | Exact payload byte length |
| 16 | u32 | FNV-1a 32-bit payload checksum |
| 20 | bytes | Ordered payload |

Payload order is camera/light IDs, legacy controlled/beacon bindings and radius, audio UUID, camera local TRS and projection settings, parent-local orbit target, global lighting, then counted meshes, groups, parent edges, named materials, switches, rotators, doors and goals. Meshes store asset UUIDs, local TRS, tint and material names. Rotators store their switch ID, full angular offset/rate and duration. Doors store up to four required rotator IDs, local opening offset and duration. Goals store their door ID and world-space radius. Switch presence is authored; activation is runtime state.

Version 2 appends a u32 pickup count followed by that many authored mesh ID tokens. Collection requires unique pickup mesh IDs distinct from its single exit goal, with no door dependency on the exit. Existing version 1 scenes retain their binary encoding. Collection status and the changed pickup tint are runtime values.

The reader caps the whole file at 4 MiB, each ordinary collection at 4,096 entries, parent edges at 8,193 and hierarchy depth at 256. It checks exact length/checksum/version, truncation, trailing fields, enum/boolean encodings, counts, duplicate/unknown IDs, parenting cycles, finite transforms/camera/lighting and gameplay dependencies. Lit world transforms validate the inverse-transpose. The checksum detects accidental corruption; it is not an authenticity mechanism. Schema versioning concerns disk data; the generated host/game contract separately governs C++ compatibility.

Saves persist authored component configuration, IDs, hierarchy and asset references. Runtime switch activation, rotator offsets/progress, door progress, attempt completion/pause/counters, moved camera pose/FOV, live animation, F6 swaps, derived matrices and native GPU/DLL state are excluded. Compatible DLL and asset replacement retains those runtime values; a level save or F3 recreates them from authored data.

Atomic writing uses the existing exclusive same-directory temporary file and native replacement. A rejected document or failed replacement leaves the destination intact and removes the temporary file. This is an atomic visibility contract on the supported local filesystem, not power-loss durability or concurrent-editor conflict resolution. Invalid, truncated, unsupported or superseded saves leave the active scene intact and corrected valid data recovers.

## Scene access and gameplay ownership

[SceneAccess](../RavenEngine/Public/Raven/Scene/SceneAccess.hpp) is borrowed through `GameBindings.World` for one synchronous DLL update. `Find` resolves scene-scoped authored IDs. `Query<T...>` returns an owned snapshot of live entities with all requested component names/sizes. Read/write reject stale or foreign entities. The view exposes copied values and world transforms; it does not expose entity/resource ownership operations. Do not retain it, interaction spans, component objects or module callbacks across the call or scene replacement. Existing legacy Registry access remains for earlier sample behavior; this matching-toolchain C++ development interface is not a sandbox.

The host picks the closest two-sided mesh triangle using the presented camera/extent and the same full world model as rendering. World AABBs are only a broad phase; triangle tests reject AABB-only false hits. Presented-frame rays begin on the near plane and are limited to the camera far plane. Noninteractive meshes occlude switches. CPU triangle positions/indices share the GPU mesh data's replacement and retirement lifetime, so an FBX replacement changes picking and rendering together. Picked entity values are queued and delivered once through the next update's borrowed interaction span; replacement, restart or a zero-delta resume drops stale queued interactions.

[InteractionComponents](../RavenEngine/Public/Raven/Components/InteractionComponents.hpp) retain only reviewed scalars, vectors and fixed arrays. [InteractionGame.cpp](../RavenEngine/Private/Game/InteractionGame.cpp) owns switch activation, conditional/continuous rotation, door transitions and completion. Multiple independent switches and rotators use the same systems and authored references. Door readiness requires its configured rotators; reversing a switch reverses its arm and closes the gate. The host only owns scenes/resources, gathers draws and observes the retained completion value to issue one audio notification. Original free-fly beacon rules/animation also run in the DLL; standalone orbit retains its legacy visual animation and never overwrites the puzzle's component-driven transforms.

For a DLL-only behavior edit, change `InteractionSpeed` in InteractionGame.cpp. It changes transition speed without changing authored data or resetting progress. Keep public declarations and build rules unchanged while the host is running; those changes require a host rebuild/restart.

## Collection

Press F4 four times from Perspective in free-fly mode. Left-click the three gold pickups, then fly to the cyan exit. Each collected pickup dims and the log reports progress. The exit stays locked until every pickup is collected. The same host-owned scene, picking, goal notification and restart paths serve both objectives. The DLL owns the collection rule and writes retained components through `SceneAccess`, which also handles 3D free-fly and mesh control. Compatible DLL replacement keeps the partial collection. A valid scene save or F3 starts a new attempt.

## Repeatable Windows 3D smoke

From a Visual Studio x64 developer shell with PowerShell 7 and the Vulkan SDK validation layer installed, configure both Windows presets and run:

```powershell
pwsh -File scripts/Test-Windows3D.ps1 -Preset x64-debug
pwsh -File scripts/Test-Windows3D.ps1 -Preset x64-release
```

The script builds the opt-in `Raven3DSmoke` target. It renders both objectives through the native renderer and checks the existing scene round trips, scoped queries, exact pickup hits, completion, restart, and recovery after a malformed scene save. It creates a compatible DLL with a two-pickup exit rule, then verifies that an intentional compiler error preserves the last built DLL. The native run switches to that DLL mid-attempt, checks retained progress and the revised rule, rejects a corrupt DLL, and restores the original. Scratch content, the edited source copy, and logs are under `out/validation/windows-3d/`. The script uses neither ignored source fixtures nor CTest.

## Validation and limits

Current native probes pass in Windows Debug and Release with core/synchronization Vulkan validation: exact legacy migration, full binary round trips, 512 checksum-repaired payload mutations, truncation/version/count/schema rejection, scene-scoped queries, stale/foreign access, affine/reflected/sheared exact picking, occlusion, gate prerequisites, reversible reusable behavior, completion/restart, DLL retention/rejection/recovery, atomic locked-target and Unicode saves, single-publication reset, superseded edits, FBX recovery and 32 scene switches with bounded resources and complete scene-owned retirement. Local sources/logs are under ignored `out/validation/interaction-game/`; they are evidence, not a tracked regression suite.

CTest remains deferred. Native macOS/Linux execution, a full editor, general reflected component serialization, arbitrary C++ state migration, additional import formats and full physics are outside this milestone. Keep implementation and runtime checks 3D-only unless the owner requests otherwise.


Both stock Debug/Release live sessions passed binary root, lighting and rotator edits through the tool without F3; invalid tool edits, truncated/checksum/unsupported files, and valid recovery; retained pixels/progress through compatible DLL replacement and an intentional failed game build; FBX/PNG replacement and last-good recovery; completion once, restart, T/F6, standalone orbit/Home, resize/minimize/restore and normal shutdown. Screenshots show the closed gate hiding the goal and the opened route after both switches activate. Both configurations report no Vulkan core/synchronization warnings/errors. Final native runs also check far-plane picking, independent gameplay in two scenes, expired CPU/GPU mesh data after replacement/retirement, and the final rebuilt host/DLL contract.

| Local observation | Debug | Release |
| --- | --- | --- |
| Binary root save to activation, including tool invocation | 0.273 s | 0.264 s |
| Binary lighting save to activation | 0.254 s | 0.357 s |
| DLL-only speed save to activation | 2.794 s | 2.944 s |
| Original gate sequence | 2.501 s | 2.459 s |
| Faster DLL gate sequence, retaining prior state | 0.779 s | 0.823 s |

These are single local observations, not latency guarantees. DLL timing includes polling, compiler/linker work and activation. Source level/mesh/texture/gameplay validation edits were restored. CTest was not introduced and no 2D runtime tests were run.

The local validation commands use ignored probe sources and fixtures; they are not available as a tracked suite from a clean checkout. Run full builds before these commands, with the native host stopped during builds. Live scripts run sequentially because they exercise and restore the same development inputs.

```powershell
Set-Location -LiteralPath 'D:\RavenEngine'
python 'D:\RavenEngine\out\validation\interaction-game\prepare.py'
cmd.exe /d /c 'D:\RavenEngine\out\validation\interaction-game\x64-debug-probes.cmd'
cmd.exe /d /c 'D:\RavenEngine\out\validation\interaction-game\x64-release-probes.cmd'
python 'D:\RavenEngine\out\validation\interaction-game\run-probes.py'
python 'D:\RavenEngine\out\validation\interaction-game\live.py' x64-debug
python 'D:\RavenEngine\out\validation\interaction-game\live.py' x64-release
python 'D:\RavenEngine\out\validation\interaction-game\quick.py' x64-debug
python 'D:\RavenEngine\out\validation\interaction-game\quick.py' x64-release
cmd.exe /d /c 'D:\RavenEngine\out\validation\interaction-game\headers.cmd'
```
