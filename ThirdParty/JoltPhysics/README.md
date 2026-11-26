# Jolt Physics

Unmodified library source from [Jolt Physics v5.7.0](https://github.com/jrouwe/JoltPhysics/releases/tag/v5.7.0), tag commit [`cf16769bb0ede9c45d57d06068ff0689e9702f5c`](https://github.com/jrouwe/JoltPhysics/tree/cf16769bb0ede9c45d57d06068ff0689e9702f5c). Downloaded on 2026-11-25 as `https://github.com/jrouwe/JoltPhysics/archive/refs/tags/v5.7.0.zip` (SHA-256 `e98f1c5b3e7227a96e7aef9c7ffc25fb35bb1cfe16c69207442bd4c1a447d924`; the archive comment names the tag commit). The [LICENSE](LICENSE) is MIT. CMake stages it as `Licenses/JoltPhysics-LICENSE.txt` beside the application; include that directory in distributed packages.

## What is here

Only the parts Raven builds from, byte-identical to the release:

| Path | Purpose |
| --- | --- |
| `Jolt/` | The library source, including `Jolt/Jolt.cmake` (its source list and compile definitions). |
| `Build/CMakeLists.txt` | Jolt's build file, which turns its options into compiler flags and includes `Jolt.cmake`. |
| `LICENSE` | The upstream license. |

The samples, tests, viewer, assets and documentation are left out. This README and `MANIFEST.txt` are the only Raven files in the folder.

## Rule: never edit these files

Nothing under this folder except this README is changed by hand, so an update is a clean replacement and its diff shows only upstream changes. Its `MANIFEST.txt`, written only by the update script, records every file, and CI checks the copy against it (`scripts/CheckVendored.cmake`). Every Raven setting lives outside it, in [`cmake/RavenJolt.cmake`](../../cmake/RavenJolt.cmake):

- **Library type:** static, linked privately into RavenCore. Game DLLs never include or link Jolt; its headers are system includes of RavenCore.
- **Build settings:**
  - Raven's dynamic MSVC runtime.
  - RTTI on.
  - Jolt's own warnings-as-errors off.
  - Its optimization-flag override off.
  - Profiler off.
  - Debug renderer on in Debug and Release.
  - Link-time optimization off until measured.
- **Unused parts:** GPU compute (DX12, Vulkan, Metal and CPU) is off; it serves only Jolt's hair simulation.
- **Debug builds:** Jolt stays optimized (`/O2`) with the same debug runtime as RavenCore (FD-32 measurement 2).

If a Jolt behavior has to change, change it through these options or through Raven's code in `Private/Physics/`, never in this folder. If something can only be fixed in Jolt's source, report it upstream and take the fixed release.

Two facts follow from Jolt's build file under Ninja:
- **Instruction set:** Jolt's MSVC instruction-set selection runs only under Visual Studio generators. Ninja builds therefore use the x64 SSE2 baseline for both Jolt and RavenCore, so the settings match and players need no AVX2. Raising it is a measured decision.
- **Floating point:** Jolt compiles itself with `/fp:fast`, which is acceptable at determinism level 1 (FD-48).

## Updating to a new release

1. Download the release archive from the Jolt releases page into a new, empty folder outside the repository and extract it. Note the archive's SHA-256 and the tag commit; `unzip -z` prints the commit for GitHub archives.
2. Replace the copy: `cmake -DSOURCE=<extracted JoltPhysics-x.y.z folder> -P scripts/UpdateJolt.cmake`. It removes everything here except this README, copies the three parts above and writes `MANIFEST.txt`.
3. Set `RAVEN_JOLT_VERSION` in `cmake/RavenJolt.cmake` to the new version. Configure fails while the copy and the pin disagree.
4. Read the release notes for option or API changes. Adjust `cmake/RavenJolt.cmake` for renamed options, and `Private/Physics/` for API changes; nothing outside those two places includes Jolt.
5. Update the version, commit, date and hash in this README.
6. Rebuild, run the tests and the physics checks in `docs/GameServicesDesign.md`, and commit the update on its own so the diff is reviewable.
