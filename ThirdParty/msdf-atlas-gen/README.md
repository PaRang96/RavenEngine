# msdf-atlas-gen

Unmodified files from **msdf-atlas-gen 1.4**: the `v1.4` tag archive of `https://github.com/Chlumsky/msdf-atlas-gen` (66,704 bytes), tag commit `2ede254314a2512252a225fa6c975948d6af559a`, released 2026-03-05. Downloaded on 2026-11-25 with the owner's approval. The archive's SHA-256 is `57db9548b30905b18640ceab5011144e9d362f33a53748bef6be53dd743c9992`.

**Kept:** `msdf-atlas-gen/`, `LICENSE.txt`, `README.md`. Nothing else in the release is needed for the build.

**License:** MIT ([LICENSE.txt](LICENSE.txt)), staged as `Licenses/msdf-atlas-gen-LICENSE.txt`.

## Rule: never edit these files

Nothing in this folder except this README is changed by hand, so an update is a clean replacement. Every Raven setting lives in [`cmake/RavenMsdf.cmake`](../../cmake/RavenMsdf.cmake):

- **Library:** a static `RavenMsdfAtlasGen` from its glyph geometry, glyph generators, padding, workload and rectangle packer.
- **Left out:** its command-line tool, and image and Artery font export (`MSDF_ATLAS_NO_ARTERY_FONT`).
- **Warnings:** Raven's warning level does not apply to third-party code, and sources compile as UTF-8.

**Used for:** Glyph geometry and atlas packing for runtime glyph pages (decision U2), through `Private/Text/`. Public engine headers never include it.

## Updating to a new release

1. Download and extract the new release into an empty folder.
2. Run `cmake -DMSDFGEN=<extracted msdfgen> -DATLAS=<extracted msdf-atlas-gen> -P scripts/UpdateMsdf.cmake`.
3. Set the version in `cmake/RavenMsdf.cmake` and record the release, archive hash and date above.
4. Build in Debug and Release, then check text drawing and the UI preview.
