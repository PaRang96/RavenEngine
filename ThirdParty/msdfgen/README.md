# msdfgen

Unmodified files from **msdfgen 1.13.1**: the `v1.13.1` tag archive of `https://github.com/Chlumsky/msdfgen` (96,024 bytes), tag commit `ec84cd53590bb114c5a7fa4fe7be5adafec6276a`, released 2026-11-11. Downloaded on 2026-11-25 with the owner's approval. The archive's SHA-256 is `ad381b09ef61af235690ea96eefafd01fd35f5163bcb6ad73db7904997675b71`.

**Kept:** `core/`, `ext/`, `msdfgen.h`, `msdfgen-ext.h`, `LICENSE.txt`, `README.md`. Nothing else in the release is needed for the build.

**License:** MIT ([LICENSE.txt](LICENSE.txt)), staged as `Licenses/msdfgen-LICENSE.txt`.

## Rule: never edit these files

Nothing in this folder except this README is changed by hand, so an update is a clean replacement. Its `MANIFEST.txt`, written only by the update script, records every file, and CI checks the copy against it (`scripts/CheckVendored.cmake`). Every Raven setting lives in [`cmake/RavenMsdf.cmake`](../../cmake/RavenMsdf.cmake):

- **Library:** a static `RavenMsdfgen` from `core/` and `ext/import-font.cpp` (glyph outlines through FreeType).
- **Left out:** the PNG, SVG and Skia extras. The macros its generated config header would hold are given as compile definitions instead.
- **Warnings:** Raven's warning level does not apply to third-party code, and sources compile as UTF-8.

**Used for:** Multi-channel signed distance fields for glyphs (decision U2), through `Private/Text/`. Public engine headers never include it.

## Updating to a new release

1. Download and extract the new release into an empty folder.
2. Run `cmake -DMSDFGEN=<extracted msdfgen> -DMSDFGEN_VERSION=<version> -DATLAS=<extracted msdf-atlas-gen> -DATLAS_VERSION=<version> -P scripts/UpdateMsdf.cmake`.
3. Set the version in `cmake/RavenMsdf.cmake` and record the release, archive hash and date above.
4. Build in Debug and Release, then check text drawing and the UI preview.
