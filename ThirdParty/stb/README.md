# stb_image

Unmodified `stb_image` v2.30, vendored from [nothings/stb](https://github.com/nothings/stb) at commit [`2c980bb59875b0d32144a71867fbdebb2f77cd20`](https://github.com/nothings/stb/tree/2c980bb59875b0d32144a71867fbdebb2f77cd20).

Downloaded on 2026-10-10. RavenEngine uses the MIT alternative in the accompanying [LICENSE](LICENSE). Preserve that notice when packaging this dependency.

| File | SHA-256 of the upstream bytes |
| --- | --- |
| `stb_image.h` | `594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3` |
| `LICENSE` | `bebfe904b14301657e4e5d655c811d51fd31b97c455b9cc2d8600d6bac6cff63` |

Upstream files:

- [stb_image.h](https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_image.h)
- [LICENSE](https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/LICENSE)

## Engine integration

This dependency is a private system include of RavenCore. `Private/Assets/Image.cpp` defines `STB_IMAGE_IMPLEMENTATION` exactly once, with `STBI_ONLY_PNG`, `STBI_ONLY_JPEG`, `STBI_NO_STDIO`, `STBI_NO_LINEAR`, and `STB_IMAGE_STATIC`. Raven reads bytes through its file utilities and decodes from memory, keeping native path handling outside the decoder. Public engine headers expose Raven types rather than stb types. See [the image-loading contract](../../docs/ImageLoading.md) for limits, ownership, and validation.

The same decoder source is intended for Windows, macOS, and Linux. Platform builds need no separate decoder DLL or manual download once these files are included in the checkout. Windows Debug/Release integration is verified; other platform builds remain their contributors' work. CMake stages `LICENSE` as `Licenses/stb-LICENSE.txt` beside the application; include that directory in distributed packages.

For an update, select a specific upstream commit, replace the two upstream files together, update the version/commit/hashes here, and validate the engine's image-loading behavior.
