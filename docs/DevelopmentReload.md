# Default Windows 3D development workflow

The Windows `x64-debug` and `x64-release` builds launch the ECS-driven Perspective sample in free-fly mode with no arguments. The executable uses the source checkout selected at configuration for game code, project metadata, the active level, PNGs, FBX meshes, and `Color.frag.hlsl`. Press F4 four times to open Collection. Click the gold pickups, then fly to the cyan exit. F3 restarts the current level.

```powershell
cmake --preset x64-debug
cmake --build out/build/x64-debug
.\out\build\x64-debug\RavenEngine\RavenEngine.exe
.\out\build\x64-debug\RavenEngine\RavenEngine.exe --no-watch
```

`--no-watch` runs the same gameplay from staged `Content` beside the executable. It disables source and DLL watching and automatic game builds. Manual F5 still requests a DLL reload. A relocated run needs the executable, `RavenCore.dll`, `RavenGame.dll`, and the staged `Content` tree together, with a writable directory for the DLL execution copy. `--perspective` opens standalone orbit. `--free-fly --watch-shader` opts in to the lit watcher on an explicit free-fly launch. `--orthographic --game --watch-shader` selects the existing textured fragment watcher.

## Publication and recovery

- Windows directory notifications wake the file workers. They read settled contents and periodically audit unchanged files as a fallback for missed notifications. The game worker checks source contents and DLL stamps and manages the standalone build. The application checks for a pending result between updates. An unchanged build keeps the existing DLL, and a failed or incompatible build keeps the last usable code. Shared host/game contract edits require rebuilding and restarting the host.
- The fragment watcher compiles a settled source snapshot with the same DXC and SPIR-V validation as the build. Since M3, the lit interface is defined by `Raven/Material.hlsli` (docs/GpuDrivenDesign.md, G4). It fixes the bindless texture array and sampler at set 0 bindings 0 and 1. It also fixes seven inputs, including three flat lighting fields and a flat `uint` texture index, plus one color output. Editing the header reloads every watched lit shader. Compatible edits can change shading calculations. A worker builds the lit pipeline against a retained render pass. The frame thread rechecks the source and render pass before publishing the finished pipeline, and each in-flight frame retains its pipeline until its fence completes. Swapchain recreation uses the accepted shader bytes. If resize overtakes a pending build, the candidate is prepared for the new pass.
- Compatible DLL edits and shader or asset replacements retain the active ECS attempt. A valid authored level save starts a new attempt, and a rejected save leaves the active world in place. An accepted project edit can update metadata while the current level stays active.

## Validation

From a Visual Studio x64 developer shell with PowerShell 7, Python 3, and the Vulkan SDK validation and screenshot layers installed, run:

```powershell
pwsh -File scripts/Test-Windows3D.ps1 -Preset x64-debug -DefaultLaunch
pwsh -File scripts/Test-Windows3D.ps1 -Preset x64-release -DefaultLaunch
```

The checks build a copy of the sources and content under `out/live3d-*`, drive the stock executable, and edit only that copy. They compare rendered frames through shader success and failure, a compatible game rule edit and failed build, malformed and recovered scenes, resize, and PNG and FBX replacement. The Release run also verifies a relocated no-watch launch and compares alternating stationary Collection runs at the same window extent. Results, raw timing traces, screenshots, and logs are saved in the run directory.

The opt-in `RAVEN_FRAME_TRACE` environment variable names a CSV to write at shutdown. The recorder keeps up to 32,768 frame samples in memory. CPU-side frame work is elapsed frame-loop time before the application's pacing sleep and can include Vulkan driver waits. Frame interval is the time between consecutive frame starts. Release idle samples run without screenshot or validation layers and separately report process CPU time per frame, including watcher threads. The Release reload run repeats the scenario without either layer to distinguish render and capture costs. Shader and FBX save-to-visible measurements use the presentation clock. DLL, PNG, and scene visibility measurements include the screenshot sampling delay. The layer-free pass records publication for those three operations.

## Measured Windows results

Both presets completed Collection through the default launch after a visible shader edit and a DLL rule edit. Failed shader compilation, incompatible descriptor bindings, a failed DLL build, and a malformed scene left the prior image or attempt active. A valid shader recovered, survived resize, and accepted an equal-size edit with its timestamp preserved. The latest of two rapid saves won. Project metadata, PNG, and FBX also reloaded. The Vulkan core and synchronization checks reported no warnings or errors. Separate Release checks finished a partially completed Collection attempt after replacing PNG and FBX assets, and verified shared-header contract rejection and recovery. Release also loaded Collection from the relocated staged tree under `--no-watch`.

Idle values below are medians across three eight-second samples per mode in Release, after settling on Collection at 600 × 400 on an RTX 3070 Ti Laptop GPU. All values are milliseconds.

| Launch | Process CPU per frame | CPU-side work p95 / p99 | Frame interval p95 / p99 |
| --- | ---: | ---: | ---: |
| Default watching | 0.293 | 24.274 / 27.418 | 24.276 / 27.419 |
| `--no-watch` | 0.295 | 24.305 / 26.652 | 24.306 / 26.654 |

The first Release comparison measured 0.588 ms of idle process CPU per frame with watching and 0.228 ms with watching disabled. Directory notifications replaced repeated reads of unchanged files and brought the two modes into the same range. The final per-run p99 ranges overlapped at 20.28–27.87 ms for watching and 26.14–27.97 ms for `--no-watch`. The elapsed-work percentiles include presentation waits, which also appear in the no-watch baseline.

| Release operation | Save to visible, with visual verification | Longest frame, layer-free |
| --- | ---: | ---: |
| Lit shader, ordinary replacement | 234–289 ms | 24.28 ms |
| Latest shader save during resize | 294 ms | 78.09 ms |
| Compatible DLL rule edit | 4,682 ms | 49.67 ms |
| Valid scene recovery | 580 ms | 24.77 ms |
| PNG replacement | 667 ms | 34.45 ms |
| FBX replacement | 133 ms | 40.35 ms |

The layer-free rejection cases peaked at 24.14 ms for shader syntax, 24.24 ms for the incompatible shader, 34.61 ms for the failed game build, and 17.05 ms for the malformed scene. The resize case includes the existing swapchain recreation wait. The game edit includes the separate compile and link. These are observations from one machine. The raw Release traces and visual checks are under `out/live3d-x64-release-76deea9/`.
