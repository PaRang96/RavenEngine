# Default Windows 3D development workflow

The Windows `x64-debug` and `x64-release` builds run the Samples game root ([EngineModules.md](EngineModules.md#game-roots-m6a)) in free flight with no arguments; `--game Courtyard` runs the 2D game. The executable uses the source checkout selected at configuration for the game's code and content, and for every engine shader. Press F4 four times to open Collection. Click the gold pickups, then fly to the cyan exit. F3 restarts the current level, F8 pauses or resumes play, and F7 cycles the lit-mesh render views: shaded, unlit, normals and wireframe ([HotReloadDesign.md](HotReloadDesign.md#h3--debug-views-and-general-use-shaders-q3)).

```powershell
cmake --preset x64-debug
cmake --build out/build/x64-debug
.\out\build\x64-debug\RavenEngine\RavenEngine.exe
.\out\build\x64-debug\RavenEngine\RavenEngine.exe --no-watch
```

`--no-watch` runs the same gameplay from the staged `Games/<Name>/Content` beside the executable. It disables source and DLL watching and automatic game builds. Manual F5 still requests a DLL reload. A relocated run needs the executable, `RavenCore.dll`, the game's `<Name>.dll`, and the staged `Content` and `Games` trees together, with a writable directory for the DLL execution copy. Other `--name` options are the game's: `--orbit` opens the Samples orbit demo. Presentation options combine with any mode: `--present fifo|mailbox|immediate` (default FIFO; an unsupported mode falls back to FIFO with a warning), `--max-fps <rate>` (default 0, no cap: FIFO already paces to the display, so Mailbox and Immediate run uncapped unless a cap is set), `--gpu discrete|integrated` (which device type is ranked first) and `--borderless <x> <y> <width> <height>` (a topmost window without a frame, Windows only). A minimized window is throttled to 30 frames per second. The swapchain log line names the present mode in use. The benchmark harness uses these options instead of patching a source snapshot. Set `RAVEN_RELOAD_TRACE=1` to log each reload's phase timing, including the game build's phases in `AutoBuild.log`.

## Publication and recovery

- **One reload service watches everything** (M5, [HotReloadDesign.md](HotReloadDesign.md#h1--one-reload-service-with-dependency-records-q1-q4)): shaders, PNG and FBX assets, scenes, the project, game sources and the game DLL.
  - One thread waits for Windows directory notifications. A pass reads an input's contents only when its size or write time changed, and treats a change as settled once the contents stay unchanged for 100 ms. Every 2 s an audit reads all contents, which catches missed notifications and edits that keep their timestamp.
  - Two workers decode or compile. A result superseded by a newer save is dropped.
  - Each consumer keeps an in-memory dependency record: its inputs with content hashes, an importer version, and its last import time and error.
  - The service exists only while something is watched, so `--no-watch` and player builds start no thread.
- **The game worker** only starts and polls the standalone build. It starts as soon as the sources settle, and it pauses the DLL watch while the linker runs. The application checks for a pending result between updates. An unchanged build keeps the existing DLL, and a failed or incompatible build keeps the last usable code. Shared host/game contract edits require rebuilding and restarting the host.
- **Every engine shader reloads:** the lit and textured vertex and fragment shaders, the three debug-view fragment shaders, and the `Scatter` and `Cull` compute passes.
  - Each program compiles with the same DXC and SPIR-V validation as the build. DXC's dependency list makes every include an input, so editing `Raven/Material.hlsli` or `GpuScene.hlsli` recompiles exactly the programs that include it.
  - Fragment shaders keep the M3 check. Since M3, the lit interface is defined by `Raven/Material.hlsli` (docs/GpuDrivenDesign.md, G4): the bindless texture array and sampler at set 0 bindings 0 and 1, seven inputs including three flat lighting fields and a flat `uint` texture index, and one color output. Since M5 a shader may read any subset of the inputs and skip the texture and sampler. Optimized builds remove what a shader does not read, which the debug views rely on. Everything it does declare must match.
  - Vertex and compute shaders are checked against what the C++ code binds and writes: stage inputs and outputs, buffer strides and member offsets, push constants, and the compute workgroup size ([H2](HotReloadDesign.md#h2--shader-programs-for-every-stage-q2)). A mismatch is rejected with a message naming it, and the last good shader stays active.
  - Results are published only once every watched program has settled, so a header edit's dependents change in the same frame.
  - Lit pipelines (every view) build on a worker against a retained render pass. The frame thread checks the render pass before publishing them, and each in-flight frame keeps its pipelines until its timeline value completes. Textured and compute pipelines are replaced between frames. Swapchain recreation uses the accepted shader words.
- Compatible DLL edits and shader or asset replacements retain the active ECS attempt. A valid authored level save starts a new attempt, and a rejected save leaves the active world in place. An accepted project edit can update metadata while the current level stays active.

## Validation

From a Visual Studio x64 developer shell with Python 3 and the Vulkan SDK validation and screenshot layers installed, run (Windows PowerShell 5.1 or PowerShell 7):

```powershell
powershell -File scripts/Test-Windows3D.ps1 -Preset x64-debug -DefaultLaunch
powershell -File scripts/Test-Windows3D.ps1 -Preset x64-release -DefaultLaunch
```

The checks build a copy of the sources and content under `out/live3d-*`, drive the stock executable, and edit only that copy. They compare rendered frames through:
- fragment, vertex and compute shader edits, the shared headers, and five shader rejections
- the four render views
- a compatible game rule edit and a failed build
- malformed and recovered scenes, resize, and PNG and FBX replacement The Release run also verifies a relocated no-watch launch and compares alternating stationary Collection runs at the same window extent. Results, raw timing traces, screenshots, and logs are saved in the run directory.

The opt-in `RAVEN_FRAME_TRACE` environment variable names a CSV to write at shutdown. Two more files go beside it: `<name>.systems.csv` with per-system timing and suspension, and `<name>.reloads.csv` with every watched input's dependency record (inputs, importer version, imports, failures, superseded imports, trigger, output, import and publish times, and last error). Every reload also logs one summary line with its trigger and its import and publish times. The recorder keeps up to 32,768 frame samples in memory. CPU-side frame work is elapsed frame-loop time before the application's pacing sleep and can include Vulkan driver waits. Frame interval is the time between consecutive frame starts. Release idle samples run without screenshot or validation layers and separately report process CPU time per frame, including watcher threads. The Release reload run repeats the scenario without either layer to distinguish render and capture costs. Shader and FBX save-to-visible measurements use the presentation clock. DLL, PNG, and scene visibility measurements include the screenshot sampling delay. The layer-free pass records publication for those three operations.

## Measured Windows results

Results after M5 (2026-11-23, final code), from `Test-Windows3DLive.py x64-release`, its layer-free pass and separate probes. All runs used the RTX 3070 Ti Laptop GPU. Both presets completed every case with no Vulkan validation messages. The full record, including the earlier pre-M5 numbers it replaces, is the [M5 exit record](HotReloadDesign.md#m5-exit-record-2026-11-23).

| Release operation | Save to visible | Longest frame, layer-free |
| --- | ---: | ---: |
| Lit fragment shader | 248–432 ms (median 268 ms) | 38 ms |
| Vertex / compute shader | 270–281 ms / 218–251 ms | 28 ms / 27 ms |
| `Material.hlsli` edit (every dependent) | 337 ms | 27 ms |
| Compatible DLL rule edit, save to active | 1.63–1.87 s (median 1.69 s); Debug 1.3–1.6 s | 37 ms |
| Valid scene recovery | 761 ms (with screenshot sampling) | 44 ms |
| PNG / FBX replacement | 640 ms / 141–173 ms | 38 ms / 27 ms |
| Edit that keeps its size and timestamp | about 2.3 s, by the 2 s audit | 28 ms |

Shader and interface rejections took 211–313 ms, and a failed game build 605 ms.

Idle process CPU per frame in Release was 0.46–0.68 ms with watching and 0.42–0.91 ms with `--no-watch`. Those ranges overlap each other and the pre-M5 build measured the same day (0.36–0.49 and 0.42–0.75 ms). The 0.29 ms recorded before M3 came from a different machine state.

The reload service ran 17 watched consumers on 3 threads in a Collection session. Before M5, that session needed one thread per watched file, 8 in total, and the count grew with every asset. These are observations from one machine.
