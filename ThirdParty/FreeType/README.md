# FreeType

Unmodified files from **FreeType 2.15.0**: `freetype-2.15.0.tar.xz` (2,716,928 bytes) from `https://download-mirror.savannah.gnu.org/releases/freetype/`, released 2026-10-12. Downloaded on 2026-11-25 with the owner's approval. The archive's SHA-256 is `93cd47eb719c325107f2d3cf087d06299e944c3b48a40b55537302304dea1dd5`.

**Kept:** `include/`, `src/`, `builds/windows/`, `LICENSE.TXT`, `README`, `docs/FTL.TXT`, `docs/GPLv2.TXT`. Nothing else in the release is needed for the build.

**License:** Dual-licensed: the FreeType License ([docs/FTL.TXT](docs/FTL.TXT)) or GPLv2. Raven uses the FreeType License, whose credit clause asks shipped products to say they use FreeType. The license is staged as `Licenses/FreeType-LICENSE.txt`.

## Rule: never edit these files

Nothing in this folder except this README is changed by hand, so an update is a clean replacement. Every Raven setting lives in [`cmake/RavenFreeType.cmake`](../../cmake/RavenFreeType.cmake):

- **Library:** a static `RavenFreeType` from FreeType's own source list, with its unmodified module list (`ftmodule.h`) and options (`ftoption.h`).
- **No system libraries:** compressed fonts use FreeType's bundled zlib.
- **Warnings:** Raven's warning level does not apply to third-party code, and sources compile as UTF-8.

**Used for:** Font loading and glyph outlines for the text stack (decision U2), through `Private/Text/`. msdfgen reads outlines through it. Public engine headers never include it.

## Updating to a new release

1. Download and extract the new release into an empty folder.
2. Run `cmake -DSOURCE=<extracted freetype-<version>> -P scripts/UpdateFreeType.cmake`.
3. Set the version in `cmake/RavenFreeType.cmake` and record the release, archive hash and date above.
4. Build in Debug and Release, then check text drawing and the UI preview.
