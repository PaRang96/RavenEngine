# ufbx

Unmodified source and upstream license from [ufbx v0.23.1](https://github.com/ufbx/ufbx/releases/tag/v0.23.1), commit 26a482ae66871d7de36eb722aa060bce95bce274. RavenEngine uses the MIT license option. The engine compiles the parser privately and stages the upstream license beside the executable.

## Rule: never edit these files

Nothing in this folder except this README is changed by hand, so an update is a clean replacement. Its `MANIFEST.txt`, written only by the update script, records every file, and CI checks the copy against it (`scripts/CheckVendored.cmake`). Every Raven setting lives in [`cmake/RavenUfbx.cmake`](../../cmake/RavenUfbx.cmake): a static `RavenFbx` target, built without Raven's warning level and with hidden symbols, and the version pin checked against `UFBX_HEADER_VERSION`. Only `Private/Assets/FbxMesh.cpp` includes it.

## Updating to a new release

1. Download the release into a new, empty folder outside the repository, with the owner's approval.
2. Replace the copy: `cmake -DSOURCE=<folder holding ufbx.c, ufbx.h and LICENSE> -P scripts/UpdateUfbx.cmake`.
3. Set `RAVEN_UFBX_VERSION` in `cmake/RavenUfbx.cmake`; configure fails while the copy and the pin disagree.
4. Update the version and commit in this README, rebuild, check the FBX samples, and commit the update on its own.
