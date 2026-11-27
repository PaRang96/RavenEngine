"""Exercise the stock Windows 3D executable in a disposable source snapshot."""

import argparse
import contextlib
import csv
import ctypes
from ctypes import wintypes
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
import uuid

from RavenSnapshot import copy_source_tree


USER = ctypes.WinDLL("user32", use_last_error=True)
KERNEL = ctypes.WinDLL("kernel32", use_last_error=True)
KERNEL.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
KERNEL.OpenProcess.restype = wintypes.HANDLE
KERNEL.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
KERNEL.CloseHandle.argtypes = [wintypes.HANDLE]
WINDOW_CALLBACK = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
USER.EnumWindows.argtypes = [WINDOW_CALLBACK, wintypes.LPARAM]
USER.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
USER.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
USER.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
USER.GetWindowRect.argtypes = USER.GetClientRect.argtypes
USER.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
USER.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int,
                             ctypes.c_int, ctypes.c_int, wintypes.UINT]
USER.SetProcessDpiAwarenessContext.argtypes = [ctypes.c_void_p]
USER.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
DIAGNOSTIC = rb"\[ERROR\]|VUID-|SYNC-HAZARD|Validation (?:Error|Warning)|Cannot play audio"


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def find_window(pid):
    result = None

    @WINDOW_CALLBACK
    def match(window, _):
        nonlocal result
        owner = wintypes.DWORD()
        USER.GetWindowThreadProcessId(window, ctypes.byref(owner))
        name = ctypes.create_unicode_buffer(80)
        if owner.value == pid and USER.GetClassNameW(window, name, len(name)) and name.value == "RavenEngineWindow":
            result = window
            return False
        return True

    USER.EnumWindows(match, 0)
    return result


def size(window, get_rect):
    rect = wintypes.RECT()
    check(get_rect(window, ctypes.byref(rect)), "Could not read window dimensions")
    return rect.right - rect.left, rect.bottom - rect.top


def save(path, data):
    temp = path.with_name(f".{path.name}.smoke-tmp")
    temp.write_bytes(data)
    try:
        deadline = time.monotonic() + 3
        while True:
            try:
                os.replace(temp, path)
                return time.perf_counter_ns()
            except PermissionError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(.02)
    finally:
        temp.unlink(missing_ok=True)


class Sample:
    def __init__(self, process, directory):
        self.process = process
        self.directory = directory
        self.log = directory / "run.log"
        self.frames = directory / "frames"
        self.window = self.wait(lambda: find_window(process.pid), "window")
        self.marker(b"First Vulkan frame presented")

    def wait(self, predicate, message, timeout=12):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline and self.process.poll() is None:
            result = predicate()
            if result:
                return result
            time.sleep(.015)
        raise RuntimeError(f"Timed out waiting for {message}. See {self.log}")

    def count(self, text):
        return self.log.read_bytes().count(text)

    def marker(self, text, count=1, timeout=12):
        self.wait(lambda: self.count(text) >= count, text, timeout)

    def post(self, message, flags=0, value=0):
        check(USER.PostMessageW(self.window, message, flags, value), f"Could not post {message:#x}")

    def key(self, vk, scan, down):
        bits = 1 | (scan << 16) | (0 if down else 0xC0000000)
        self.post(0x100 if down else 0x101, vk, bits)

    def tap(self, vk, scan):
        self.key(vk, scan, True)
        self.key(vk, scan, False)

    def level(self, name, limit=16):
        # F4 cycles the project's scene order, which can include benchmark scenes, so keep
        # advancing until the requested level loads.
        marker = f"Loaded level '{name}.ravenscene'".encode()
        expected = self.count(marker) + 1
        for _ in range(limit):
            loads = self.count(b"Loaded level '") + 1
            self.tap(0x73, 0x3E)
            self.marker(b"Loaded level '", loads)
            if self.count(marker) >= expected:
                return
        raise RuntimeError(f"F4 did not reach {name} within {limit} levels. See {self.log}")

    def resize(self, width, height):
        ow, oh = size(self.window, USER.GetWindowRect)
        cw, ch = size(self.window, USER.GetClientRect)
        if (cw, ch) == (width, height):
            return
        marker = f"Swapchain created: {width} x {height}".encode()
        expected = self.count(marker) + 1
        check(USER.SetWindowPos(self.window, None, 0, 0, ow - cw + width, oh - ch + height, 0x0016),
              "Could not resize sample")
        self.marker(marker, expected)

    def pickup(self, name, x, z, count):
        width, height = size(self.window, USER.GetClientRect)
        focal = height / (2 * math.tan(math.pi / 6))
        px = round(width / 2 + focal * x / 6)
        py = round(height / 2 - focal * (z - 1.6) / 6)
        coordinates = px | (py << 16)
        picked = f"Picked pickup-{name}".encode()
        expected = self.count(picked) + 1
        self.post(0x201, 1, coordinates)
        self.post(0x202, 0, coordinates)
        self.marker(picked, expected)
        self.marker(f"Collected {count}/3".encode())

    def screenshot(self, name, reference=None, changed=None):
        def captures():
            files = sorted(self.frames.glob("*.ppm"), key=lambda path: int(path.stem))
            for path in files[:-5]:
                path.unlink(missing_ok=True)
            return files[-5:]

        captures()
        queued = re.findall(rb"Queued screeshot for frame: (\d+)", self.log.read_bytes())
        previous = int(queued[-1]) if queued else -1

        def ready():
            files = captures()
            if len(files) < 2 or int(files[-2].stem) <= previous:
                return None
            data = files[-2].read_bytes()
            width, height = size(self.window, USER.GetClientRect)
            header = data[:100].split(maxsplit=4)
            if len(data) < width * height * 3 or header[:4] != [b"P6", str(width).encode(),
                                                               str(height).encode(), b"255"]:
                return None
            if changed is not None and (data != reference) != changed:
                return None
            return data

        result = self.wait(ready, f"{name} screenshot")
        (self.directory / f"{name}.ppm").write_bytes(result)
        return result


@contextlib.contextmanager
def run_sample(executable, directory, args=(), capture=False):
    directory.mkdir()
    env = os.environ.copy()
    for name in ("VK_INSTANCE_LAYERS", "VK_LAYER_VALIDATE_SYNC", "VK_SCREENSHOT_FRAMES", "VK_SCREENSHOT_DIR"):
        env.pop(name, None)
    env["RAVEN_FRAME_TRACE"] = str(directory / "frames.csv")
    if capture:
        env["VK_INSTANCE_LAYERS"] = "VK_LAYER_LUNARG_screenshot;VK_LAYER_KHRONOS_validation"
        env["VK_LAYER_VALIDATE_SYNC"] = "1"
        env["VK_SCREENSHOT_FRAMES"] = "0-16000-20"
        env["VK_SCREENSHOT_DIR"] = str(directory / "frames")
        (directory / "frames").mkdir()
    with (directory / "run.log").open("wb") as log:
        # The warning panel stays off: transient warnings must not enter compared screenshots, and the
        # checks read every problem from the log instead.
        process = subprocess.Popen([str(executable), "--no-warning-panel", *args], cwd=directory, env=env,
                                   stdout=log, stderr=subprocess.STDOUT)
        try:
            sample = Sample(process, directory)
            yield sample
            sample.post(0x10)
            check(process.wait(timeout=10) == 0, f"Sample did not exit cleanly. See {sample.log}")
            check(not re.search(DIAGNOSTIC, sample.log.read_bytes()), f"Native diagnostics in {sample.log}")
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()


def live(executable, source, directory, capture=True):
    shader = source / "RavenEngine/Private/Renderer/Shaders/Color.frag.hlsl"
    # The Samples game root (docs/DataModelDesign.md, FD-17).
    game = source / "Games/Samples/Source/Interaction.cpp"
    content = source / "Games/Samples/Content"
    scene = content / "Levels/Collection.ravenscene"
    metadata = content / "Project.ravenproject"
    mesh = content / "Meshes/Cube.fbx"
    png = content / "Textures/UVGridAlternate.png"
    # Material files and material shaders reload too (docs/RenderingContentDesign.md, B0 contracts 6 and 7).
    paint = content / "Materials/Paint.ravenmaterial"
    stripes = content / "Shaders/Stripes.hlsl"
    # Lit shaders reload when the engine material header changes (docs/GpuDrivenDesign.md, G4).
    header = shader.parent / "Raven/Material.hlsli"
    # Every stage and include reloads since M5 (docs/HotReloadDesign.md, H2).
    vertex = shader.parent / "Color.vert.hlsl"
    cull = shader.parent / "Cull.comp.hlsl"
    scene_header = shader.parent / "GpuScene.hlsli"
    originals = {path: path.read_bytes() for path in (shader, header, vertex, cull, scene_header, game, scene,
                                                       metadata, mesh, png, paint, stripes)}
    measurements = {}
    rejections = {}
    try:
        with run_sample(executable, directory, capture=capture) as app:
            screenshot = app.screenshot if capture else lambda *args: None
            app.marker(b"the module rebuilds on save")
            app.marker(b"Watching shaders in")
            app.marker(b"Watching project")
            app.resize(600, 400)
            for name in ("Gallery", "Assemblies", "Puzzle", "Collection"):
                app.level(name)
            app.pickup("left", -1.6, 1.6, 1)
            original_image = screenshot("original")

            shader_marker = b"Lit shader reload presented:"

            def change_shader(name, data, previous, preserve_timestamp=False):
                count = app.count(shader_marker) + 1
                stamp = shader.stat()
                start = save(shader, data)
                if preserve_timestamp:
                    check(len(data) == stamp.st_size, "The equal-size shader fixture changed")
                    os.utime(shader, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
                app.marker(shader_marker, count)
                lines = re.findall(rb"Lit shader reload presented:.*?clock (\d+) ns", app.log.read_bytes())
                measurements[name] = (start, int(lines[-1]), "presentation clock")
                return screenshot(name, previous, True)

            baseline = b"RavenApplyLighting(input, albedo), 1.0"
            warm = originals[shader].replace(baseline, b"RavenApplyLighting(input, albedo) * float3(1.0, 0.54, 0.36), 1.0")
            cool = originals[shader].replace(baseline, b"RavenApplyLighting(input, albedo) * float3(0.45, 0.74, 1.0), 1.0")
            check(warm != originals[shader] and cool != originals[shader], "Shader fixture moved")
            warm_image = change_shader("shader-warm", warm, original_image)
            generation = app.count(shader_marker)
            bad = b"Cannot reload shader"
            count = app.count(bad) + 1
            start = save(shader, warm + b"\nfloat4 BrokenShader( {\n")
            app.marker(bad, count)
            rejections["shader-syntax"] = (start, time.perf_counter_ns())
            screenshot("syntax-retained", warm_image, False)
            check(app.count(shader_marker) == generation, "Failed shader was activated")
            # An extra stage input (location 9) is outside the lit interface the vertex shader provides.
            incompatible = cool.replace(b"float4 PSMain(RavenFragmentInput input) : SV_Target0",
                b"float4 PSMain(RavenFragmentInput input, [[vk::location(9)]] float4 extra : TEXCOORD9) : SV_Target0").replace(
                b"float3(0.45, 0.74, 1.0), 1.0", b"float3(0.45, 0.74, 1.0) + extra.rgb, 1.0")
            check(incompatible.count(b"extra") == 2, "Incompatible shader fixture moved")
            start = save(shader, incompatible)
            app.marker(b"Incompatible shader: unexpected input at location 9")
            rejections["shader-interface"] = (start, time.perf_counter_ns())
            screenshot("interface-retained", warm_image, False)
            check(app.count(shader_marker) == generation, "Incompatible shader was activated")
            cool_image = change_shader("shader-recovered", cool, warm_image)
            app.resize(720, 480)
            screenshot("shader-resized")
            app.resize(600, 400)
            screenshot("shader-after-resize", cool_image, False)
            warm_image = change_shader("shader-repeat", warm, cool_image, preserve_timestamp=True)
            count = app.count(shader_marker) + 1
            save(shader, cool)
            start = save(shader, originals[shader])
            app.resize(680, 460)
            app.marker(shader_marker, count)
            app.resize(600, 400)
            screenshot("shader-latest-snapshot", original_image, False)
            check(app.count(shader_marker) == count, "An intermediate shader save was published")
            lines = re.findall(rb"Lit shader reload presented:.*?clock (\d+) ns", app.log.read_bytes())
            measurements["shader-superseded-save"] = (start, int(lines[-1]), "presentation clock")
            warm_image = change_shader("shader-restored-warm", warm, original_image)

            header_base = b"return albedo * (input.AmbientAndLit.rgb + input.LightColor * diffuse);"
            header_green = originals[header].replace(header_base,
                b"return albedo * (input.AmbientAndLit.rgb + input.LightColor * diffuse) * float3(0.6, 1.0, 0.6);")
            check(header_green != originals[header], "Material header fixture moved")
            count = app.count(shader_marker) + 1
            start = save(header, header_green)
            app.marker(shader_marker, count)
            lines = re.findall(rb"Lit shader reload presented:.*?clock (\d+) ns", app.log.read_bytes())
            measurements["shader-header"] = (start, int(lines[-1]), "presentation clock")
            screenshot("shader-header", warm_image, True)
            count = app.count(shader_marker) + 1
            save(header, originals[header])
            app.marker(shader_marker, count)
            screenshot("shader-header-restored", warm_image, False)

            # Vertex and compute reloads, their interface checks and the debug views (M5, H2/H3).
            def reload_stage(name, path, data, marker, previous, changed):
                count = app.count(marker) + 1
                start = save(path, data)
                app.marker(marker, count)
                lines = re.findall(re.escape(marker) + rb".*?clock (\d+) ns", app.log.read_bytes())
                measurements[name] = (start, int(lines[-1]), "presentation clock")
                return screenshot(name, previous, changed)

            def reject_stage(name, path, data, message, *more):
                start = save(path, data)
                for text in (message, *more):
                    app.marker(text)
                rejections[name] = (start, time.perf_counter_ns())
                screenshot(f"{name}-retained", warm_image, False)
                save(path, originals[path])
                # The restored source compiles to the published words, so nothing is published.
                time.sleep(1)

            vertex_marker = b"Lit shader reload presented: Color.vert.hlsl"
            swizzled = originals[vertex].replace(b"output.Color = input.Color *", b"output.Color = float3(1.0, 0.45, 0.45) * input.Color *")
            check(swizzled != originals[vertex], "Vertex shader fixture moved")
            reload_stage("vertex", vertex, swizzled, vertex_marker, warm_image, True)
            reload_stage("vertex-restored", vertex, originals[vertex], vertex_marker, warm_image, False)
            cull_marker = b"GPU scene shader reload presented: Cull.comp.hlsl"
            halved = originals[cull].replace(b"if (slot >= Cull.ObjectCount)", b"if (slot >= Cull.ObjectCount / 2u)")
            check(halved != originals[cull], "Culling shader fixture moved")
            reload_stage("compute", cull, halved, cull_marker, warm_image, True)
            reload_stage("compute-restored", cull, originals[cull], cull_marker, warm_image, False)
            reject_stage("vertex-interface", vertex, originals[vertex].replace(b"nointerpolation uint Texture",
                         b"nointerpolation float Texture"), b"output at location 6 has 1 float components")
            reject_stage("compute-numthreads", cull, originals[cull].replace(b"[numthreads(64, 1, 1)]",
                         b"[numthreads(32, 1, 1)]"), b"numthreads is (32, 1, 1); the engine dispatches (64, 1, 1)")
            widened = originals[scene_header].replace(b"uint Padding;", b"uint Padding;\n\tuint Extra;", 1)
            check(widened != originals[scene_header], "Scene header fixture moved")
            reject_stage("scene-stride", scene_header, widened,
                         b"Color.vert.hlsl: Incompatible shader: set 2 binding 0 element stride 144",
                         b"Cull.comp.hlsl: Incompatible shader: set 0 binding 0 element stride 144")
            previous = warm_image
            for view in ("unlit", "normals", "wireframe"):
                count = app.count(b"Render view: ") + 1
                app.tap(0x76, 0x41)
                app.marker(f"Render view: {view}".encode(), 1)
                check(app.count(b"Render view: ") == count, "F7 did not select one view")
                previous = screenshot(f"view-{view}", previous, True)
            app.tap(0x76, 0x41)
            app.marker(b"Render view: shaded")
            screenshot("view-shaded", warm_image, False)

            rejected = b"Cannot replace scene/project; keeping the active world"
            count = app.count(rejected) + 1
            start = save(scene, b"malformed smoke scene")
            app.marker(rejected, count)
            rejections["malformed-scene"] = (start, time.perf_counter_ns())
            screenshot("scene-retained", warm_image, False)

            original_rule = b"return collected == total;"
            edited_rule = b"""scene.View<const PickupComponent, ECS::StaticMeshComponent>().Each(
                    [](ECS::Entity, const PickupComponent& state, ECS::StaticMeshComponent& mesh)
                    {
                        if (state.Collected)
                            mesh.Tint = {.9f, .15f, .8f, 1};
                    });
                return collected + 1 >= total;"""
            changed_game = originals[game].replace(original_rule, edited_rule)
            check(changed_game != originals[game], "DLL fixture moved")
            activation = b"Game module activated, generation "
            count = app.count(activation) + 1
            start = save(game, changed_game)
            app.marker(activation, count, 60)
            edited_image = screenshot("dll-retains-pickup", warm_image, True)
            measurements["game-rule"] = (start, time.perf_counter_ns(),
                                         "observed changed screenshot" if capture else "observed activation")
            failure = b"Game build failed; keeping active code"
            count = app.count(failure) + 1
            start = save(game, b"#error intentional game compile failure\n" + changed_game)
            app.marker(failure, count, 60)
            rejections["failed-game-build"] = (start, time.perf_counter_ns())
            screenshot("failed-dll-retained", edited_image, False)
            app.pickup("center", 0, .85, 2)
            app.key(0x57, 0x11, True)
            try:
                app.marker(b"Goal completed.", timeout=6)
            finally:
                app.key(0x57, 0x11, False)
            check(app.count(b"Collected 3/3") == 0, "Revised two-pickup rule did not remain active")

            count = app.count(activation) + 1
            save(game, originals[game])
            app.marker(activation, count, 60)
            count = app.count(b"Loaded level 'Collection.ravenscene'") + 1
            start = save(scene, originals[scene])
            app.marker(b"Loaded level 'Collection.ravenscene'", count)
            screenshot("scene-recovered")
            measurements["scene-recovery"] = (start, time.perf_counter_ns(),
                                               "observed changed screenshot" if capture else "observed publication")
            app.pickup("left", -1.6, 1.6, 1)
            app.marker(b"Collected 1/3", 2)

            count = app.count(b"Published project metadata") + 1
            save(metadata, originals[metadata] + b"\n# live smoke metadata\n")
            app.marker(b"Published project metadata", count)
            app.level("Perspective")
            perspective = screenshot("perspective")
            count = app.count(b"Replaced PNG ") + 1
            start = save(png, png.with_name("UVGrid.png").read_bytes())
            app.marker(b"Replaced PNG ", count)
            png_image = screenshot("png-replaced", perspective, True)
            measurements["png"] = (start, time.perf_counter_ns(),
                                   "observed changed screenshot" if capture else "observed publication")
            count = app.count(b"FBX reload presented:") + 1
            start = save(mesh, mesh.with_name("Sphere.fbx").read_bytes())
            app.marker(b"FBX reload presented:", count)
            lines = re.findall(rb"FBX reload presented:.*?clock (\d+) ns", app.log.read_bytes())
            measurements["fbx"] = (start, int(lines[-1]), "presentation clock")
            screenshot("fbx-replaced", png_image, True)

            app.level("Puzzle")
            puzzle = screenshot("puzzle")
            count = app.count(b"Replaced material ") + 1
            start = save(paint, originals[paint].replace(b"\nlit\n", b"\nlit\nbase-color 1 0.54 0.36 1\n"))
            app.marker(b"Replaced material ", count)
            material_image = screenshot("material-replaced", puzzle, True)
            measurements["material"] = (start, time.perf_counter_ns(),
                                        "observed changed screenshot" if capture else "observed publication")
            striped = originals[stripes].replace(b"albedo = lerp(albedo, parameters.StripeColor.rgb, parameters.StripeColor.a);",
                                                 b"albedo = lerp(albedo, float3(0.1, 0.2, 0.9), 0.8);")
            check(striped != originals[stripes], "Material shader fixture moved")
            count = app.count(b"Replaced shader ") + 1
            start = save(stripes, striped)
            app.marker(b"Replaced shader ", count)
            shader_image = screenshot("material-shader-replaced", material_image, True)
            measurements["material-shader"] = (start, time.perf_counter_ns(),
                                               "observed changed screenshot" if capture else "observed publication")
            bad = b"Stripes.hlsl: Cannot compile material shader"
            count = app.count(bad) + 1
            start = save(stripes, striped + b"\nfloat4 BrokenShader( {\n")
            app.marker(bad, count, 30)
            rejections["material-shader-syntax"] = (start, time.perf_counter_ns())
            screenshot("material-shader-retained", shader_image, False)
            count = app.count(b"Replaced shader ") + 1
            save(stripes, originals[stripes])
            app.marker(b"Replaced shader ", count)
            count = app.count(b"Replaced material ") + 1
            save(paint, originals[paint])
            app.marker(b"Replaced material ", count)
            screenshot("material-restored", puzzle, False)
    finally:
        for path, data in originals.items():
            if path.read_bytes() != data:
                save(path, data)

    frames = read_trace(directory / "frames.csv")
    reported = {
        name: {("save_to_publication_ms" if clock in ("observed activation", "observed publication")
                else "save_to_visible_ms"): milliseconds(end - start), "clock": clock,
               "max_frame_ms": max(intervals(frames, start, end)),
               "max_frame_work_ms": max(milliseconds(work) for time_ns, work in frames if start <= time_ns <= end)}
        for name, (start, end, clock) in measurements.items()
    }
    for name, (start, end) in rejections.items():
        reported[name] = {"save_to_rejection_ms": milliseconds(end - start),
                          "max_frame_ms": max(intervals(frames, start, end))}
    return reported


def read_trace(path):
    with path.open(newline="") as file:
        return [(int(row["start_ns"]), int(row["work_ns"])) for row in csv.DictReader(file) if row["drawable"] == "1"]


def milliseconds(ns):
    return round(ns / 1_000_000, 3)


def intervals(frames, start, end):
    return [milliseconds(b - a) for (a, _), (b, _) in zip(frames, frames[1:]) if a <= end and b >= start]


def summary(values):
    values = sorted(values)
    return {f"p{percent}": values[math.ceil(len(values) * percent / 100) - 1] for percent in (50, 95, 99)}


def process_cpu_ns(pid):
    process = KERNEL.OpenProcess(0x1000, False, pid)
    check(process, "Could not open the benchmark process for timing")
    try:
        times = [wintypes.FILETIME() for _ in range(4)]
        check(KERNEL.GetProcessTimes(process, *(ctypes.byref(value) for value in times)),
              "Could not read benchmark CPU time")
        return 100 * sum((value.dwHighDateTime << 32) | value.dwLowDateTime for value in times[2:])
    finally:
        KERNEL.CloseHandle(process)


def idle(executable, directory, args):
    with run_sample(executable, directory, args) as app:
        app.resize(600, 400)
        for name in ("Gallery", "Assemblies", "Puzzle", "Collection"):
            app.level(name)
        time.sleep(2)
        start = time.perf_counter_ns()
        cpu_start = process_cpu_ns(app.process.pid)
        time.sleep(8)
        cpu_end = process_cpu_ns(app.process.pid)
        end = time.perf_counter_ns()
        if args:
            check(not re.search(rb"Watching (?:project|scene|asset|lit fragment)|Building game module in background",
                                app.log.read_bytes()), "Source watcher ran under --no-watch")
    frames = read_trace(directory / "frames.csv")
    work_times = [milliseconds(work) for stamp, work in frames if start <= stamp <= end]
    return {"process_cpu_ms_per_frame": milliseconds((cpu_end - cpu_start) / len(work_times)),
            "work_ms": summary(work_times),
            "frame_ms": summary(intervals(frames, start, end))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("preset", choices=("x64-debug", "x64-release"))
    preset = parser.parse_args().preset
    repo = Path(__file__).resolve().parent.parent
    report = repo / "out" / f"live3d-{preset}-{uuid.uuid4().hex[:7]}"
    source = report / "source"
    source.mkdir(parents=True)
    print(f"Default-launch evidence is under {report}", flush=True)
    copy_source_tree(repo, source)
    with (report / "build.log").open("wb") as output:
        try:
            subprocess.run(["cmake", "--preset", preset], cwd=source, stdout=output,
                           stderr=subprocess.STDOUT, check=True)
            subprocess.run(["cmake", "--build", f"out/build/{preset}", "--parallel", "4"], cwd=source,
                           stdout=output, stderr=subprocess.STDOUT, check=True)
        except subprocess.CalledProcessError as error:
            raise RuntimeError(f"Isolated build failed. See {report / 'build.log'}") from error

    runtime = source / "out" / "build" / preset / "RavenEngine"
    executable = runtime / "RavenEngine.exe"
    results = {"reloads": live(executable, source, report / "live")}
    print("Default launch and live reload checks passed.", flush=True)
    if preset == "x64-release":
        results["reloads_without_layers"] = live(executable, source, report / "live-no-layers", capture=False)
        results["idle_samples"] = []
        for index, args in enumerate((("--no-watch",), (), (), ("--no-watch",), ("--no-watch",), ())):
            label = "no-watch" if args else "default"
            sample = idle(executable, report / f"idle-{index}-{label}", args)
            results["idle_samples"].append({"mode": label, **sample})
        relocated = report / "relocated"
        relocated.mkdir()
        for name in ("RavenEngine.exe", "RavenCore.dll", "Samples.dll"):
            shutil.copy2(runtime / name, relocated / name)
        shutil.copytree(runtime / "Content", relocated / "Content")
        shutil.copytree(runtime / "Games", relocated / "Games")
        with run_sample(relocated / "RavenEngine.exe", report / "relocated-run", ("--no-watch",)) as app:
            app.marker(b"using staged content")
            for name in ("Gallery", "Assemblies", "Puzzle", "Collection"):
                app.level(name)
            app.pickup("left", -1.6, 1.6, 1)
            check(str(relocated).encode() in app.log.read_bytes(), "Relocated executable did not log its staged root")
    (report / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(json.dumps(results, indent=2), flush=True)


if __name__ == "__main__":
    main()
