"""Generate and measure the Windows 3D benchmark through the stock executable."""

import argparse
import csv
from itertools import product
import json
import math
from pathlib import Path
import runpy
import shutil
import statistics
import subprocess
import tempfile
import time


ROOT = Path(__file__).resolve().parent.parent
SMOKE = runpy.run_path(str(ROOT / "scripts/Test-Windows3DLive.py"))
SCENES = {
    100: "5f708ee7-8927-4b46-9434-20a5392eb2c4",
    500: "675da478-1d28-4440-8d75-4bccd9942c31",
    2000: "84d2e14b-30ba-4158-b3e4-dcfad32b35e0",
    4000: "b61438cc-321d-41da-b9ec-1d17b34d7357",
}
# Stress scenes (docs/GpuDrivenDesign.md, G6) are generated into each benchmark snapshot, never into Content.
STRESS = {
    10000: "3c5e1f0a-7d2b-4e61-9a84-1b0c6f2d9e31",
    50000: "8a41d7c2-5b9e-4f03-b6d1-2e7f90c4a358",
    100000: "d29f6b14-0c83-4a7e-8f52-6b1e3a9d07c4",
}
SCENE_IDS = {**SCENES, **STRESS}
EXTENT = 1280, 720
RATE = .65
RELEASE = "x64-release"
COMPARISON_COUNTS = (100, 500, 2000)


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def runtime(root=ROOT, preset=RELEASE):
    return root / "out/build" / preset / "RavenEngine"


# The Samples game root owns the benchmark scenes and their generator (docs/DataModelDesign.md, FD-17).
CONTENT = "Games/Samples/Content"


def generate():
    for count in SCENES:
        subprocess.run([str(runtime() / "SamplesTool.exe"), "create-benchmark",
                        str(ROOT / CONTENT / f"Levels/Benchmark-{count}.ravenscene"), str(count)], check=True)


def read_frames(path):
    with path.open(newline="") as file:
        return [{key: float(value) if key in ("delta_seconds", "benchmark_phase", "gpu_ns", "gpu_cull_ns") else int(value)
                 for key, value in row.items()} for row in csv.DictReader(file) if row["drawable"] == "1"]


def distribution(values):
    values = sorted(values)
    check(values, "No timing samples")
    return {f"p{pct}": round(values[math.ceil(len(values) * pct / 100) - 1] / 1e6, 4)
            for pct in (50, 95, 99)}


def rate(previous, current):
    delta = min(current["delta_seconds"], .1)
    check(delta > 0, "The benchmark clock was reset")
    return math.remainder(current["benchmark_phase"] - previous["benchmark_phase"], 2 * math.pi) / delta


def configure_app(app, count, watching, animated):
    app.resize(*EXTENT)
    app.marker(f"Loaded level 'Benchmark-{count}.ravenscene'".encode())
    app.marker(b"Watching shaders in" if watching else b"using staged content")
    if not animated:
        app.tap(0x20, 0x39)


VIEWS = ("shaded", "unlit", "normals", "wireframe")


def select_view(app, view):
    # F7 cycles the render views from shaded (docs/HotReloadDesign.md, H3).
    for _ in range(VIEWS.index(view)):
        count = app.count(b"Render view: ") + 1
        app.tap(0x76, 0x41)
        app.marker(b"Render view: ", count)
    check(view == "shaded" or f"Render view: {view}".encode() in app.log.read_bytes(), f"Could not select the {view} view")


def profile(executable, directory, count, animated, watching, seconds, capture=False, place=None, view="shaded",
            options=()):
    args = ["--scene", SCENE_IDS[count], *options]
    if not watching:
        args.append("--no-watch")
    with SMOKE["run_sample"](executable, directory, args, capture=capture) as app:
        if place:
            place(app)
            app.marker(f"Loaded level 'Benchmark-{count}.ravenscene'".encode())
            if not animated:
                app.tap(0x20, 0x39)
            app.marker(b"Watching shaders in")
        else:
            configure_app(app, count, watching, animated)
        select_view(app, view)
        time.sleep(1.5)
        if capture:
            app.screenshot("benchmark")
        start = time.perf_counter_ns()
        time.sleep(seconds)
        end = time.perf_counter_ns()
        if place:
            place(app)
        time.sleep(.15)  # Give the last measured slot time to deliver its timestamp pair.
    frames = read_frames(directory / "frames.csv")
    window = [row for row in frames if start <= row["start_ns"] < end and row["presented_ns"]]
    check(len(window) >= (4 if capture else 20), "Too few presented frames for a timing distribution")
    check({row["visible_meshes"] for row in window} == {count}, f"Unexpected visible mesh count for {count}")
    calls = {row["draw_calls"] for row in window}
    check(len(calls) == 1 and min(calls) > 0, "Draw count drifted during the workload")
    rates = [rate(a, b) for a, b in zip(window, window[1:])]
    check(all(math.isclose(value, RATE if animated else 0, abs_tol=.004) for value in rates),
          "The selected animation mode did not produce the expected motion")
    timestamps = {row["gpu_frame_ns"]: row["gpu_ns"] for row in frames if row["gpu_frame_ns"]}
    gpu = [timestamps[row["start_ns"]] for row in window if row["start_ns"] in timestamps]
    culls = {row["gpu_frame_ns"]: row.get("gpu_cull_ns", 0) for row in frames if row["gpu_frame_ns"]}
    cull = [culls[row["start_ns"]] for row in window if row["start_ns"] in culls]
    check(len(gpu) >= len(window) - 2 and min(gpu) > 0, "Completed GPU timestamps are missing")
    intervals = [b["start_ns"] - a["start_ns"] for a, b in zip(window, window[1:])]
    stages = {
        "ecs": lambda row: row["gameplay_ns"],
        "world_transforms": lambda row: row["world_ns"],
        "other_scene_gather": lambda row: row["scene_gather_ns"] - row["world_ns"],
        "renderer_prepare": lambda row: row["render_prepare_ns"],
        "record_submit": lambda row: row["record_submit_ns"],
        "frame_fence": lambda row: row["fence_wait_ns"],
        "image_acquire": lambda row: row["acquire_wait_ns"],
        "queue_present": lambda row: row["present_wait_ns"],
        "frame_work": lambda row: row["work_ns"],
    }
    result = {name: distribution(stage(row) for row in window) for name, stage in stages.items()}
    waits = [row["fence_wait_ns"] + row["acquire_wait_ns"] + row["present_wait_ns"] for row in window]
    preparation = [row["work_ns"] - wait for row, wait in zip(window, waits)]
    result.update({"meshes": count, "view": view, "draw_calls": next(iter(calls)),
                   "animation": "animated" if animated else "static",
                   "watching": watching, "frames": len(window), "frame": distribution(intervals),
                   "fps": round(len(intervals) * 1e9 / sum(intervals), 2), "display_waits": distribution(waits),
                   "gpu_cull": distribution(cull),
                   "cpu_preparation": distribution(preparation),
                   "cpu_under_5_ms_percent": round(100 * sum(x < 5_000_000 for x in preparation) / len(window), 2),
                   "intervals_at_most_6_06_ms_percent": round(100 * sum(x <= 6_060_606 for x in intervals)
                                                           / len(intervals), 2),
                   "gpu": distribution(gpu), "start_ns": start, "end_ns": end})
    (directory / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    return result


def measure(report, seconds, repeats):
    content = ROOT / CONTENT
    staged = runtime() / CONTENT
    inputs = ["Project.ravenproject", "Meshes/Cube.fbx", "Meshes/Sphere.fbx",
              "Textures/UVGrid.png", "Textures/UVGridAlternate.png"]
    inputs.extend(f"Levels/Benchmark-{count}.ravenscene" for count in COMPARISON_COUNTS)
    for relative in inputs:
        check((content / relative).read_bytes() == (staged / relative).read_bytes(),
              f"Source and staged benchmark content differ for {relative}. Build x64-release to restage it.")
    cases = list(product(COMPARISON_COUNTS, (False, True), (True, False)))
    results = []
    for repeat in range(repeats):
        for count, animated, watching in (cases if repeat % 2 == 0 else cases[::-1]):
            label = f"{repeat + 1}-{count}-{'animated' if animated else 'static'}-{'watch' if watching else 'no-watch'}"
            result = profile(runtime() / "RavenEngine.exe", report / label, count, animated, watching, seconds)
            results.append({"run": label, **result})
            print(f"{label}: {result['draw_calls']} draws, frame p95 {result['frame']['p95']:.3f} ms, "
                  f"world p95 {result['world_transforms']['p95']:.3f} ms", flush=True)
    return results


def build_snapshot(report, edit=None, preset=RELEASE, stress=None):
    source = report / "source"
    source.mkdir()
    for folder in ("RavenEngine", "Games", "Content", "ThirdParty", "cmake", "scripts", "docs", "BuildProfiles"):
        shutil.copytree(ROOT / folder, source / folder)
    for name in ("CMakeLists.txt", "CMakePresets.json"):
        shutil.copy2(ROOT / name, source / name)
    if edit:
        edit(source)
    with (report / "build.log").open("wb") as output:
        subprocess.run(["cmake", "--preset", preset], cwd=source, stdout=output, stderr=subprocess.STDOUT, check=True)
        build = ["cmake", "--build", f"out/build/{preset}", "--parallel", "4"]
        subprocess.run(build, cwd=source, stdout=output, stderr=subprocess.STDOUT, check=True)
        if stress in STRESS:
            # The snapshot's own scene tool writes the scene; the rebuild stages it with the project.
            level = f"Levels/Benchmark-{stress}.ravenscene"
            subprocess.run([str(runtime(source, preset) / "SamplesTool.exe"), "create-benchmark",
                            str(source / CONTENT / level), str(stress)],
                           stdout=output, stderr=subprocess.STDOUT, check=True)
            with (source / CONTENT / "Project.ravenproject").open("a", encoding="utf-8", newline="\n") as project:
                project.write(f'scene {STRESS[stress]} "{level}"\n')
            subprocess.run(build, cwd=source, stdout=output, stderr=subprocess.STDOUT, check=True)
    return source


def hot_reload(report, count):
    source = build_snapshot(report, stress=count)
    path = source / "Games/Samples/Source/Interaction.cpp"
    original = path.read_bytes()
    rule = b"ContinuousRotationSpeed = 1.0f;"
    check(original.count(rule) == 1, "The benchmark's compatible speed edit moved")
    directory = report / "reload"
    activation = b"Game module activated, generation "
    try:
        with SMOKE["run_sample"](runtime(source) / "RavenEngine.exe", directory,
                                 ("--scene", SCENE_IDS[count])) as app:
            configure_app(app, count, True, True)
            time.sleep(2)
            expected = app.count(activation) + 1
            saved = SMOKE["save"](path, original.replace(rule, b"ContinuousRotationSpeed = 2.0f;"))
            app.marker(activation, expected, timeout=60)
            time.sleep(1.5)
    finally:
        if path.read_bytes() != original:
            SMOKE["save"](path, original)

    frames = read_frames(directory / "frames.csv")
    pairs = list(zip(frames, frames[1:]))
    transitions = [(a, b) for a, b in pairs if b["start_ns"] >= saved and a["game_generation"] != b["game_generation"]]
    check(len(transitions) == 1, "The live rule edit should activate exactly one new generation")
    a, b = transitions[0]
    check(b["presented_ns"] and b["visible_meshes"] == count, "The first updated pose was not presented")
    check(math.isclose(rate(a, b), 2 * RATE, abs_tol=.004), "The first frame after reload did not retain the pose")
    before = [rate(old, new) for old, new in pairs if saved - 500_000_000 <= new["start_ns"] <= saved]
    after = [rate(old, new) for old, new in pairs if old["start_ns"] >= b["start_ns"]]
    check(len(before) >= 10 and len(after) >= 10, "Missing rotor samples before or after reload")
    check(all(math.isclose(value, RATE, abs_tol=.004) for value in before), "Unexpected original speed")
    check(all(math.isclose(value, 2 * RATE, abs_tol=.004) for value in after), "Unexpected replacement speed")
    check(not any(row["scene_replaced"] for row in frames), "The reload replaced the authored scene")
    longest = max(new["start_ns"] - old["start_ns"] for old, new in pairs
                  if old["start_ns"] <= b["presented_ns"] and new["start_ns"] >= saved)
    return {"meshes": count, "before_generation": a["game_generation"], "after_generation": b["game_generation"],
            "before_rate_rad_s": statistics.median(before), "after_rate_rad_s": statistics.median(after),
            "previous_phase_rad": a["benchmark_phase"], "first_updated_phase_rad": b["benchmark_phase"],
            "first_transition_rate_rad_s": rate(a, b), "save_to_present_ms": (b["presented_ns"] - saved) / 1e6,
            "longest_frame_ms": longest / 1e6}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_subparsers(dest="mode", required=True)
    modes.add_parser("generate")
    runs = modes.add_parser("measure")
    runs.add_argument("label")
    runs.add_argument("--seconds", type=float, default=4)
    runs.add_argument("--repeats", type=int, default=3)
    reload = modes.add_parser("reload")
    reload.add_argument("--meshes", type=int, choices=SCENE_IDS, default=500)
    validate = modes.add_parser("validate")
    validate.add_argument("--meshes", type=int, choices=SCENE_IDS, default=2000)
    scale = modes.add_parser("scale")
    scale.add_argument("label")
    scale.add_argument("--case", action="append", choices=("auto-fifo", "auto-mailbox", "intel-fifo", "nvidia-mailbox"))
    scale.add_argument("--seconds", type=float, default=4)
    scale.add_argument("--repeats", type=int, default=3)
    scale.add_argument("--meshes", type=int, choices=(4000, *STRESS), default=4000)
    scale.add_argument("--static", action="store_true", help="Pause the rotators (M3 100k static target)")
    scale.add_argument("--view", choices=VIEWS, default="shaded", help="Render view selected with F7 before measuring")
    scale.add_argument("--max-fps", type=int, default=0, help="Frame-rate cap passed to the host (0: none)")
    args = parser.parse_args()
    if args.mode == "generate":
        generate()
        return
    report = Path(tempfile.mkdtemp(prefix=f"benchmark-{args.mode}-", dir=ROOT / "out"))
    print(f"Benchmark evidence: {report}", flush=True)
    if args.mode == "measure":
        check(args.seconds >= 1 and args.repeats >= 1, "Measure at least one run of one second or longer")
        result = {"label": args.label, "resolution": EXTENT, "seconds": args.seconds, "repeats": args.repeats,
                  "samples": measure(report, args.seconds, args.repeats)}
    elif args.mode == "reload":
        result = hot_reload(report, args.meshes)
        print(json.dumps(result, indent=2), flush=True)
    elif args.mode == "scale":
        from Windows3DHighRefresh import cases, fastest_display

        check(args.seconds >= 1 and args.repeats >= 1, "Measure at least one run of one second or longer")
        display = fastest_display(SMOKE["USER"])
        source = build_snapshot(report, stress=args.meshes)
        selected = args.case or ["auto-fifo"]
        samples = []
        for trial in range(1, args.repeats + 1):
            for name, options, place in cases(display, selected, trial, SMOKE["USER"]):
                if args.max_fps:
                    options += ["--max-fps", str(args.max_fps)]
                row = profile(runtime(source) / "RavenEngine.exe", report / name, args.meshes, not args.static, True,
                              args.seconds, place=place, view=args.view, options=options)
                log = (report / name / "run.log").read_text(encoding="utf-8", errors="replace")
                _, adapter, mode = name.split("-", 2)
                selected_adapter = next(line.split("Selected GPU: ", 1)[1] for line in log.splitlines()
                                        if "Selected GPU: " in line)
                check(adapter == "auto" or selected_adapter.lower().startswith(adapter), "Wrong GPU selected")
                check(f"present={mode}" in log, "Wrong presentation mode selected")
                check(f"Swapchain created: {display.width} x {display.height}," in log,
                      "The renderer did not use the selected display resolution")
                # One indirect command per lit mesh (cube, sphere) and four textured batches (G5); the
                # wireframe view draws the lit commands a second time as lines (H3).
                check(row["draw_calls"] == (8 if args.view == "wireframe" else 6), "Unexpected benchmark draw count")
                samples.append({"run": name, "adapter": selected_adapter, "present": mode, **row})
                print(f"{name}: {row['fps']:.1f} FPS, {row['cpu_under_5_ms_percent']:.1f}% CPU under 5 ms", flush=True)
        result = {"label": args.label, "view": args.view, "max_fps": args.max_fps, "display": display.name, "hz": display.hz,
                  "resolution": [display.width, display.height], "samples": samples}
    else:
        debug = runtime(preset="x64-debug")
        if args.meshes in STRESS:
            debug = runtime(build_snapshot(report, preset="x64-debug", stress=args.meshes), "x64-debug")
        result = profile(debug / "RavenEngine.exe", report / "vulkan",
                         args.meshes, True, False, 4 if args.meshes == 4000 else 2, capture=True)
        print(f"The {args.meshes:,}-mesh Debug render passed Vulkan validation.", flush=True)
    (report / "results.json").write_text(json.dumps(result, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
