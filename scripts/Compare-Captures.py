"""Capture first-frame screenshots of the 3D scenes and compare them with references.

    python scripts/Compare-Captures.py capture out/captures/reference
    python scripts/Compare-Captures.py capture out/captures/candidate
    python scripts/Compare-Captures.py compare out/captures/reference out/captures/candidate

Each scene is launched with --no-watch --scene <id>, like the 3D benchmark: the staged game
module and content, without watchers. The first drawable frame has a zero time step, so it
shows the authored state before any animation or camera movement. The Vulkan screenshot
layer saves that frame, and the process is then closed.

Compare passes when at most --max-fraction of pixels differ and no channel differs by more
than --max-delta (defaults 0.1% and 2/255, docs/RendererDesign.md). For every image that
differs, it writes <name>.diff.ppm beside the candidate: differing pixels are red, the rest
is a dimmed reference.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SCENES = ("Perspective", "Gallery", "Assemblies", "Puzzle", "Collection", "Benchmark-500")
HEADER = re.compile(rb"P6\s+(\d+)\s+(\d+)\s+(\d+)\s")


def scene_ids():
    ids = {}
    project = (ROOT / "Content/Project.ravenproject").read_text(encoding="utf-8")
    for line in project.splitlines():
        match = re.match(r'scene\s+([0-9a-fA-F-]{36})\s+\w+\s+"Levels/(.+)\.ravenscene"', line.strip())
        if match:
            ids[match.group(2)] = match.group(1)
    return ids


def read_ppm(path):
    data = path.read_bytes()
    match = HEADER.match(data)
    if not match or int(match.group(3)) != 255:
        raise ValueError(f"{path} is not an 8-bit binary PPM")
    width, height = int(match.group(1)), int(match.group(2))
    pixels = data[match.end():]
    if len(pixels) < width * height * 3:
        raise ValueError(f"{path} is truncated")
    return width, height, pixels[:width * height * 3]


def write_ppm(path, width, height, pixels):
    path.write_bytes(b"P6\n%d %d\n255\n" % (width, height) + bytes(pixels))


def wait_for_frame(path, process, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.is_file():
            try:
                read_ppm(path)
                return
            except ValueError:
                pass  # The layer is still writing the file.
        if process.poll() is not None:
            raise RuntimeError(f"The engine exited with {process.returncode} before frame 0 was saved")
        time.sleep(0.1)
    raise RuntimeError(f"Timed out waiting for {path}")


def capture(directory, preset, timeout):
    executable = ROOT / "out/build" / preset / "RavenEngine/RavenEngine.exe"
    if not executable.is_file():
        raise SystemExit(f"Build {preset} first: {executable} is missing")
    ids = scene_ids()
    # The engine runs from its own folder, so the screenshot layer needs an absolute path.
    directory = directory.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    for name in SCENES:
        if name not in ids:
            raise SystemExit(f"{name} is not registered in Content/Project.ravenproject")
        frames = directory / f"{name}.frames"
        shutil.rmtree(frames, ignore_errors=True)
        frames.mkdir()
        env = os.environ.copy()
        for variable in ("VK_INSTANCE_LAYERS", "VK_LAYER_VALIDATE_SYNC", "VK_SCREENSHOT_FRAMES", "VK_SCREENSHOT_DIR"):
            env.pop(variable, None)
        env["VK_INSTANCE_LAYERS"] = "VK_LAYER_LUNARG_screenshot"
        env["VK_SCREENSHOT_FRAMES"] = "0"
        env["VK_SCREENSHOT_DIR"] = str(frames)
        frame = frames / "0.ppm"
        with (directory / f"{name}.log").open("wb") as log:
            process = subprocess.Popen([str(executable), "--no-watch", "--scene", ids[name]],
                                       cwd=executable.parent, env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                wait_for_frame(frame, process, timeout)
            finally:
                process.kill()
                process.wait()
        width, height, _ = read_ppm(frame)
        shutil.copyfile(frame, directory / f"{name}.ppm")
        shutil.rmtree(frames, ignore_errors=True)
        print(f"{name}: captured {width}x{height}", flush=True)


def compare_images(reference, candidate):
    width, height, expected = read_ppm(reference)
    candidate_width, candidate_height, actual = read_ppm(candidate)
    if (width, height) != (candidate_width, candidate_height):
        return None
    if expected == actual:
        return width, height, 0, 0, None
    differing = 0
    worst = 0
    diff = bytearray(len(expected))
    for index in range(0, len(expected), 3):
        delta = max(abs(expected[index] - actual[index]), abs(expected[index + 1] - actual[index + 1]),
                    abs(expected[index + 2] - actual[index + 2]))
        if delta:
            differing += 1
            worst = max(worst, delta)
            diff[index] = 255
        else:
            diff[index:index + 3] = bytes(value // 4 for value in expected[index:index + 3])
    return width, height, differing, worst, diff


def compare(reference_directory, candidate_directory, max_fraction, max_delta):
    references = sorted(reference_directory.glob("*.ppm"))
    references = [path for path in references if not path.name.endswith(".diff.ppm")]
    if not references:
        raise SystemExit(f"No reference captures in {reference_directory}")
    failures = 0
    for reference in references:
        candidate = candidate_directory / reference.name
        if not candidate.is_file():
            print(f"{reference.stem}: FAIL (missing candidate)")
            failures += 1
            continue
        result = compare_images(reference, candidate)
        if result is None:
            print(f"{reference.stem}: FAIL (image sizes differ)")
            failures += 1
            continue
        width, height, differing, worst, diff = result
        fraction = differing / (width * height)
        passed = fraction <= max_fraction and worst <= max_delta
        failures += not passed
        print(f"{reference.stem}: {'PASS' if passed else 'FAIL'} "
              f"({differing} of {width * height} pixels differ = {fraction:.4%}, max delta {worst}/255)")
        if diff is not None:
            write_ppm(candidate_directory / f"{reference.stem}.diff.ppm", width, height, diff)
    print(f"{len(references) - failures} of {len(references)} captures within tolerance "
          f"(at most {max_fraction:.2%} of pixels, at most {max_delta}/255 per channel)")
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    modes = parser.add_subparsers(dest="mode", required=True)
    capture_mode = modes.add_parser("capture", help="capture frame 0 of each 3D scene")
    capture_mode.add_argument("directory", type=Path)
    capture_mode.add_argument("--preset", default="x64-release", choices=("x64-debug", "x64-release"))
    capture_mode.add_argument("--timeout", type=float, default=60)
    compare_mode = modes.add_parser("compare", help="compare candidate captures with references")
    compare_mode.add_argument("reference", type=Path)
    compare_mode.add_argument("candidate", type=Path)
    compare_mode.add_argument("--max-fraction", type=float, default=0.001)
    compare_mode.add_argument("--max-delta", type=int, default=2)
    args = parser.parse_args()
    if args.mode == "capture":
        capture(args.directory, args.preset, args.timeout)
        return 0
    return 1 if compare(args.reference, args.candidate, args.max_fraction, args.max_delta) else 0


if __name__ == "__main__":
    sys.exit(main())
