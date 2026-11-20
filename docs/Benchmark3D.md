# Windows 3D performance benchmark

The development project includes deterministic Perspective scenes with 100, 500, 2,000, and 4,000 meshes. They use the existing cube and sphere with shared lit materials. Rows own pivot entities, and each pivot owns a mesh. One in five pivots runs an ECS rotator. The camera and geometry are the same for static and animated measurements. The driver pauses the rotators with Space for the static run. The measured frustum mesh count excludes the four constant textured demonstration draws, which are included in the submitted draw count.

The benchmark scene IDs are registered in the development project. The public preview profile selects Puzzle and Collection. To open the 2,000-mesh scene directly from a Windows development build, use:

```powershell
.\out\build\x64-release\RavenEngine\RavenEngine.exe --scene 84d2e14b-30ba-4158-b3e4-dcfad32b35e0
```

## Repeat the measurements

From the Visual Studio x64 developer shell, build the Release host and run the benchmark:

```powershell
cmake --build out/build/x64-release --parallel 4
python scripts/Benchmark-Windows3D.py measure current
```

For regeneration, run `python scripts/Benchmark-Windows3D.py generate` using the built scene tool. Rebuild to stage the regenerated source bytes. The direct launch uses the default watcher set. Adding `--no-watch` uses the staged copy of the same scene. The measurement command checks those copies before running.

The Release driver uses a 1280 × 720 drawable extent, a stationary camera, 1.5 seconds of warmup, and four seconds of capture per run. By default it covers all 12 workload, movement, and watch combinations three times and reverses the order on the middle repetition. The measured runs use the normal renderer without the validation and screenshot layers. Allow the benchmark to run without other simultaneous builds or captures.

Each invocation writes an ignored `out/benchmark-measure-*` directory. Each run has a native log, `frames.csv`, and a percentile summary. The parent directory has `results.json`. Percentiles use nearest rank. Frame interval measures consecutive frame starts, including the default frame pacing. Stage timings measure elapsed host time in the measured scopes. The mesh-world column measures the full ancestor composition for submitted scene meshes. The remaining scene gather excludes that time. Renderer preparation, recording and submission, the frame-slot fence, image acquisition, and the present call are reported separately. GPU timestamps bracket the recorded render pass and are read without a query wait when a completed frame slot is reused. The raw trace names the originating frame of each GPU result.

```powershell
python scripts/Benchmark-Windows3D.py reload
cmake --build out/build/x64-debug --parallel 4
python scripts/Benchmark-Windows3D.py validate
```

The reload check uses an isolated source snapshot, modifies the continuous rotation speed in the existing game DLL, and compares the first new phase against the last old phase plus one update at the new speed. It also checks the original and replacement speeds and rejects unexpected scene recreation. Save-to-present measures the first successful presentation handoff with the updated motion. The validation command uses the Vulkan validation and screenshot layers for a separate 2,000-mesh Debug run.

## Measured results

The baseline and optimized builds each ran 36 Release captures on an Intel Core i7-12700H with an NVIDIA GeForce RTX 3070 Ti Laptop GPU. Both used Vulkan FIFO presentation and the default application pacing. The 100-, 500-, and 2,000-mesh workloads produced 104, 504, and 2,004 draw calls, respectively. Each cell shows the baseline → optimized median of three per-run percentiles in milliseconds.

| Meshes | Motion | Watch | World p95 | Frame p95 | Frame p99 |
| ---: | --- | --- | ---: | ---: | ---: |
| 100 | Static | On | 0.128 → 0.055 | 27.104 → 27.075 | 27.710 → 27.383 |
| 100 | Static | Off | 0.113 → 0.064 | 27.033 → 27.017 | 27.544 → 27.336 |
| 100 | Animated | On | 0.120 → 0.057 | 27.146 → 27.151 | 28.198 → 27.744 |
| 100 | Animated | Off | 0.122 → 0.065 | 27.292 → 27.174 | 27.995 → 27.613 |
| 500 | Static | On | 1.373 → 0.258 | 27.621 → 27.510 | 28.207 → 28.153 |
| 500 | Static | Off | 1.390 → 0.276 | 27.474 → 27.571 | 27.994 → 28.224 |
| 500 | Animated | On | 1.450 → 0.269 | 27.479 → 27.468 | 27.916 → 28.055 |
| 500 | Animated | Off | 1.513 → 0.271 | 27.464 → 27.612 | 27.832 → 28.021 |
| 2,000 | Static | On | 17.676 → 1.141 | 20.354 → 27.454 | 22.332 → 27.980 |
| 2,000 | Static | Off | 16.953 → 1.097 | 19.454 → 27.420 | 20.218 → 27.837 |
| 2,000 | Animated | On | 17.702 → 1.103 | 20.630 → 27.436 | 21.291 → 27.984 |
| 2,000 | Animated | Off | 16.919 → 1.106 | 19.680 → 27.479 | 20.490 → 28.312 |

World-transform composition dominated the 2,000-mesh CPU path. The improvement replaced linear scene-membership and parent searches with a scene-owned hash index keyed by entity identity. It retains entity enumeration order and composes the current local transforms along each ancestor chain. For the 2,000-mesh animated watch run, world p95 ranged from 17.095–17.820 ms across baseline runs and 1.079–1.131 ms across optimized runs.

| Stage, 2,000 animated with watch | Baseline p95 (ms) | Optimized p95 (ms) |
| --- | ---: | ---: |
| Gameplay ECS | 1.442 | 0.775 |
| Mesh world transforms | 17.702 | 1.103 |
| Remaining scene gather | 0.695 | 0.602 |
| Renderer preparation | 0.397 | 0.355 |
| Recording and submission | 0.493 | 0.535 |
| GPU pass | 0.892 | 0.932 |
| Frame-slot fence wait | 0.004 | 24.574 |
| Image acquisition | 0.007 | 0.008 |
| Queue present call | 0.156 | 0.035 |

For that workload, median frame p50 fell from 18.616 to 10.268 ms and median presented throughput rose from 52.9 to 59.9 frames per second. Frame p95 and p99 increased across all four 2,000-mesh cases. The existing frame-slot fence wait dominated the optimized p95 on this setup, while the GPU pass remained below 1 ms at p95. Stage percentiles describe separate distributions and cannot be added to obtain a frame percentile.

The 500-mesh live edit activated generation 2 exactly once. The rotor continued from its retained phase at 1.30 rad/s, up from 0.65 rad/s. The first successful presentation handoff with the new motion occurred 3.605 s after the save. The longest frame interval during reload was 37.154 ms.

The Windows Release 3D smoke check and the separate 2,000-mesh Debug Vulkan core and synchronization validation passed. Regenerating the three scenes reproduced the source files byte for byte. The rebuilt `windows-x64` public preview folder contained only Puzzle and Collection in both its scene catalog and level directory. Its launch file and project catalog excluded all three benchmark scene IDs.

Local evidence is under `out/benchmark-measure-3zwko8li` (baseline), `out/benchmark-measure-vj5_4_7o` (optimized), `out/benchmark-reload-ptb79gu8` (live edit), and `out/benchmark-validate-7fg8k_dy` (Vulkan validation).

## 4,000 meshes at native resolution

From the Visual Studio x64 developer shell, use the `scale` command to build a disposable Release host in `out/`, open it on the fastest connected display at its current resolution, and run the 4,000-mesh animated level with the default watchers. The disposable host offers explicit tear-free presentation and graphics adapter selection, with its drawable-frame sleep disabled. `auto-fifo` selects the first compatible adapter and standard VSync. `auto-mailbox` selects the first compatible adapter and requests mailbox presentation. The command checks the actual presentation mode, resolution, animation speed, and 4,004 draw calls.

```powershell
python scripts/Benchmark-Windows3D.py scale current --case intel-fifo --case nvidia-mailbox
python scripts/Benchmark-Windows3D.py reload --meshes 4000
python scripts/Benchmark-Windows3D.py validate --meshes 4000
```

The scale example uses the Intel-connected 165 Hz display at 2560 × 1440, testing Intel with VSync and NVIDIA with mailbox in alternating order for three runs each. Use `--case auto-fifo` to measure a machine with a single adapter. The `validate` command uses the built `x64-debug` host. Each capture saves frame traces and result summaries. CPU preparation is frame work minus elapsed time measured in the frame-slot fence, image acquisition, and queue-present call. This measures elapsed time on the frame thread and can still include scheduling or driver work inside other calls. GPU timestamps cover the recorded render pass. Frame rates and intervals describe Raven's frame production and presentation handoff. Physical panel refresh and actual scanout would require a separate display timing capture.

### Before and after

The 4,000-mesh level has 50 rows of 80 meshes with 800 animated pivots. The baseline and optimized captures used byte-identical scene and asset data and the same benchmark command. The optimized draw gather evaluates a parent once when a child first needs it and shares that world matrix with subsequent children. It applies unrotated local scale and translation directly to the parent's affine matrix, and uses the local transform already read for each mesh. The pass starts after gameplay and discards cached parent values before the next gather. Direct gameplay and camera queries continue to compute from live components. Extremely large or small scales can overflow an intermediate or collapse an axis, so the draw path can retry in the direct query's original multiplication order.

FPS values are medians of three runs with the full range in parentheses. The time columns use the median of the three per-run 95th-percentile times. A 165 FPS cadence corresponds to 6.06 ms between frames.

| Tear-free mode | FPS before → after | CPU preparation before → after | Frame interval before → after | Frames with CPU prep under 5 ms |
| --- | ---: | ---: | ---: | ---: |
| Intel VSync | 142.7 (129.7–151.0) → 160.6 (123.1–162.8) | 9.11 → 6.73 ms | 9.28 → 6.94 ms | 0% → 13.2% pooled |
| RTX mailbox | 100.7 (98.1–101.5) → 122.3 (121.4–123.3) | 6.44 → 4.81 ms | 12.97 → 11.17 ms | 0% → 97.4% pooled |

The RTX result represents 1,433 of 1,472 frames under 5 ms, with individual runs ranging from 94.3% to 99.0%. Intel reached 162.8 FPS in the fastest optimized run. Two of its optimized runs had fewer than 3% of frame intervals over 8 ms. One ran at 123.1 FPS and spent more time waiting for an image and recording and submitting frames. The 165 FPS cadence remains variable on this setup.

| Stage | Intel VSync before → after (ms) | RTX mailbox before → after (ms) |
| --- | ---: | ---: |
| Gameplay ECS | 1.875 → 1.836 | 1.550 → 1.580 |
| Mesh world transforms | 2.611 → 0.960 | 2.236 → 0.728 |
| Remaining scene gather | 1.759 → 1.560 | 1.398 → 1.153 |
| Renderer preparation | 0.903 → 0.933 | 0.731 → 0.737 |
| Recording and submission | 3.304 → 2.857 | 0.921 → 0.886 |
| GPU pass | 3.527 → 3.049 | 0.723 → 0.714 |
| Frame-slot fence wait | 0.004 → 0.004 | 0.006 → 0.006 |
| Image acquisition | 0.119 → 1.713 | 0.003 → 0.004 |
| Queue present call | 0.023 → 0.020 | 6.826 → 6.689 |

Recording and submission was the largest remaining measured CPU preparation stage on Intel. The queue present call was the largest wait on the RTX. It took 3.25 ms on a typical optimized frame and 6.69 ms at the 95th percentile, while the GPU pass stayed below 1 ms at that percentile. Stage percentiles represent separate distributions.

The matching baseline capture is under `out/benchmark-scale-reference-c6b62bcc/out/benchmark-scale-reljig77`. The optimized capture is under `out/benchmark-scale-6rlwly0r`.

### Verification

Windows Debug and Release builds passed. The Release 3D smoke check compared direct and batched world transforms with reflected nonuniform scale, shared parents, runtime updates, detach, stale handles, and extreme scales. The Debug 4,000-object render passed Vulkan validation, as did both native-resolution tear-free modes in separate correctness runs under `out/benchmark-scale-6rlwly0r/validate-*`. In the watched 4,000-object Release run, the compatible DLL edit changed the rotor from 0.65 to 1.30 rad/s without resetting its phase or recreating the scene. First presentation with the new speed took 3.371 s after saving. The longest frame during reload was 64.842 ms. The corresponding local captures are under `out/benchmark-validate-9l8d9tol` and `out/benchmark-reload-7uqf1kg9`.

Regenerating `Benchmark-4000.ravenscene` reproduced its bytes exactly. The rebuilt `windows-x64` public preview folder contains Puzzle and Collection. Its files and manifests exclude all four benchmark levels. Rebuild that folder with `python scripts/BuildPlayer.py --preset x64-release --profile public-preview`.
