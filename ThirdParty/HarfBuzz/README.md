# HarfBuzz

Unmodified files from **HarfBuzz 15.2.0**: `harfbuzz-15.2.0.tar.xz` (20,761,992 bytes) from `https://github.com/harfbuzz/harfbuzz/releases/tag/15.2.0`, released 2026-11-19. Downloaded on 2026-11-25 with the owner's approval. The archive's SHA-256 is `e2ec49090744383cdbe7df808c1cedaafba06a97be8dc4211fe546ba585f12cb`.

**Kept:** `src/`, `COPYING`, `README.md`, `AUTHORS`. Nothing else in the release is needed for the build.

**License:** "Old MIT" license ([COPYING](COPYING)), staged as `Licenses/HarfBuzz-COPYING.txt`.

## Rule: never edit these files

Nothing in this folder except this README is changed by hand, so an update is a clean replacement. Every Raven setting lives in [`cmake/RavenHarfBuzz.cmake`](../../cmake/RavenHarfBuzz.cmake):

- **Library:** a static `RavenHarfBuzz` from HarfBuzz's single-file build, `src/harfbuzz.cc`.
- **Fonts** are read through HarfBuzz's own OpenType functions, so no FreeType integration is compiled. It stays thread-safe, for shaping on workers.
- **Warnings:** Raven's warning level does not apply to third-party code, and sources compile as UTF-8.

**Used for:** Text shaping for every script (decisions U2 and U4), through `Private/Text/`. Public engine headers never include it.

## Updating to a new release

1. Download and extract the new release into an empty folder.
2. Run `cmake -DSOURCE=<extracted harfbuzz-<version>> -P scripts/UpdateHarfBuzz.cmake`.
3. Set the version in `cmake/RavenHarfBuzz.cmake` and record the release, archive hash and date above.
4. Build in Debug and Release, then check text drawing and the UI preview.
