# Yoga

Unmodified files from **Yoga 3.2.1**: the `v3.2.1` tag archive of `https://github.com/facebook/yoga` (2,172,950 bytes), tag commit `042f5013152eb81c1552dec945b88f7b95ca350f`, released 2024-12-13. Downloaded on 2026-11-25 with the owner's approval. The archive's SHA-256 is `86b399ac31fd820d8ffa823c3fae31bb690b6fc45301b2a8a966c09b5a088b55`.

**Kept:** `yoga/`, `LICENSE`, `README.md`. Nothing else in the release is needed for the build.

**License:** MIT ([LICENSE](LICENSE)), staged as `Licenses/Yoga-LICENSE.txt`.

## Rule: never edit these files

Nothing in this folder except this README is changed by hand, so an update is a clean replacement. Every Raven setting lives in [`cmake/RavenYoga.cmake`](../../cmake/RavenYoga.cmake):

- **Library:** a static `RavenYoga` from every source under `yoga/`, in C++20.
- **Warnings:** Raven's warning level does not apply to third-party code, and sources compile as UTF-8.

**Used for:** Flex layout for the UI stack (decision U3), through `Private/UI/`. Raven's own grid, list, scroll and scale containers sit in its tree as measured leaves. Public engine headers never include it.

## Updating to a new release

1. Download and extract the new release into an empty folder.
2. Run `cmake -DSOURCE=<extracted yoga-<version>> -P scripts/UpdateYoga.cmake`.
3. Set the version in `cmake/RavenYoga.cmake` and record the release, archive hash and date above.
4. Build in Debug and Release, then check text drawing and the UI preview.
