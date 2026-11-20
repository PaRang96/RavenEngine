"""Build a relocatable RavenEngine player folder for a preset and content profile."""

import argparse
from contextlib import contextmanager
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import uuid


MARKER = "RavenPlayer.ravenlaunch"
MARKER_BYTES = b"raven-player 1\n"


def name(value):
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]*", value):
        raise argparse.ArgumentTypeError("Use letters, digits, hyphens, or underscores.")
    return value


def profile(path):
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict) or data.keys() - {"scenes", "assets"}:
        raise ValueError("Player profile fields are scenes and optional assets.")
    scenes, assets = data.get("scenes"), data.get("assets", [])
    if not isinstance(scenes, list) or not scenes or not isinstance(assets, list):
        raise ValueError("Player profiles need a nonempty scene list and may include an asset list.")
    ids = []
    for kind, values in (("--scene", scenes), ("--asset", assets)):
        canonical = [str(uuid.UUID(value)) for value in values if isinstance(value, str)]
        if len(canonical) != len(values) or len(set(canonical)) != len(canonical):
            raise ValueError(f"{kind} requires unique UUID strings.")
        for value in canonical:
            ids.extend((kind, value))
    return ids


def direct_child(parent, child):
    if child.parent != parent or child.resolve() != child:
        raise ValueError(f"Expected a direct, non-linked path under {parent}: {child}")
    return child


@contextmanager
def exclusive(path):
    with path.open("a+b") as lock:
        lock.seek(0)
        try:
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            raise RuntimeError(f"Another player build holds {path.name}.") from error
        yield


def read_inputs(path):
    platform = tool = None
    copies = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        parts = line.split("\t")
        if len(parts) == 2 and parts[0] == "unavailable":
            raise ValueError(parts[1])
        if len(parts) == 2 and parts[0] == "platform" and platform is None:
            platform = name(parts[1])
        elif len(parts) == 2 and parts[0] == "tool" and tool is None:
            tool = Path(parts[1])
        elif len(parts) == 3 and parts[0] == "copy":
            source, destination = map(Path, parts[1:])
            if destination.anchor or ".." in destination.parts or not destination.parts or destination in copies:
                raise ValueError(f"Invalid player input destination: {destination}")
            copies[destination] = source
        else:
            raise ValueError(f"Malformed player inputs: {line}")
    if not platform or not tool or not tool.is_absolute() or not copies:
        raise ValueError(f"Incomplete player inputs: {path}")
    return platform, tool, copies


def publish(staging, destination):
    parent = destination.parent
    direct_child(parent, staging)
    direct_child(parent, destination)
    previous = None
    if destination.exists():
        marker = destination / MARKER
        if not destination.is_dir() or not marker.is_file() or marker.read_bytes() != MARKER_BYTES:
            raise ValueError(f"Will only replace a generated player folder: {destination}")
        previous = direct_child(parent, parent / f".previous-{uuid.uuid4().hex}")
        os.replace(destination, previous)
    try:
        os.replace(staging, destination)
    except OSError:
        if previous is not None:
            os.replace(previous, destination)
        raise
    if previous is not None:
        try:
            shutil.rmtree(direct_child(parent, previous))
        except OSError as error:
            print(f"Player published. The previous generated folder remains at {previous}: {error}")


def build_player(repo, preset, profile_name):
    selection = profile(repo / "BuildProfiles" / f"{profile_name}.json")
    out = direct_child(repo, repo / "out")
    out.mkdir(exist_ok=True)
    with exclusive(out / f".player-{preset}.lock"):
        subprocess.run(["cmake", "--preset", preset], cwd=repo, check=True)
        build = out / "build" / preset
        platform, tool, copies = read_inputs(build / "RavenEngine" / "PlayerInputs.txt")
        subprocess.run(["cmake", "--build", str(build), "--target", "RavenPlayerInputs"], cwd=repo, check=True)
        # The build may have regenerated the target graph.
        platform, tool, copies = read_inputs(build / "RavenEngine" / "PlayerInputs.txt")
        players = direct_child(out, out / "player")
        players.mkdir(exist_ok=True)
        parent = direct_child(players, players / platform)
        parent.mkdir(exist_ok=True)
        destination = direct_child(parent, parent / profile_name)
        staging = direct_child(parent, Path(tempfile.mkdtemp(prefix=".stage-", dir=parent)))
        try:
            subprocess.run([str(tool), str(repo / "Content"), "stage-player", str(staging / "Content"), *selection],
                           cwd=repo, check=True)
            for relative, source in copies.items():
                target = staging / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
            (staging / MARKER).write_bytes(MARKER_BYTES)
            with exclusive(parent / f".{profile_name}.lock"):
                publish(staging, destination)
        finally:
            if staging.exists():
                shutil.rmtree(direct_child(parent, staging))
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", required=True, type=name, help="native CMake configure preset")
    parser.add_argument("--profile", required=True, type=name, help="profile from BuildProfiles/")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parent.parent
    try:
        result = build_player(repo, args.preset, args.profile)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Player build failed: {error}\n")
    print(f"Player folder: {result}")


if __name__ == "__main__":
    main()
