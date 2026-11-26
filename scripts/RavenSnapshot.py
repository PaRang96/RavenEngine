"""The repository snapshot that validation scripts build in isolation.

One list for every script, so a folder the build needs (such as the engine fonts under Content)
is never missing from one copy only. Import it from a script in this folder.
"""

import shutil
from pathlib import Path

FOLDERS = ("RavenEngine", "Games", "Content", "ThirdParty", "cmake", "scripts", "docs", "BuildProfiles")
FILES = ("CMakeLists.txt", "CMakePresets.json")


def copy_source_tree(root: Path, destination: Path) -> None:
    """Copies the tracked folders and top-level files a configure and build need into destination."""
    for folder in FOLDERS:
        shutil.copytree(root / folder, destination / folder)
    for name in FILES:
        shutil.copy2(root / name, destination / name)
