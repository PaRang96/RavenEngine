# stb_vorbis

Unmodified files from [stb](https://github.com/nothings/stb): `stb_vorbis.c` **v1.25**, released 2026-11-19. Downloaded on 2026-11-25 from `https://raw.githubusercontent.com/nothings/stb/6b5799bae8a1fe3792691ef4b72ae26434c3e799/<file>`. That is the repository head on that day; the last commit to change `stb_vorbis.c` is [`033efa1bcb750136ac98e997d53833b758e0089b`](https://github.com/nothings/stb/commit/033efa1bcb750136ac98e997d53833b758e0089b).

| File | Bytes | SHA-256 |
| --- | --- | --- |
| `stb_vorbis.c` | 199,371 | `97a8febc738e6a29fafc8d8bc557c7f17ad3db7ba6544485f59395cc6008b764` |
| `LICENSE` | 2,588 | `eb36e45c704dd0b59730115523e089b051852625b4db0a6fe76c991900af20e7` |

The [LICENSE](LICENSE) offers MIT or public domain (Unlicense). CMake stages it as `Licenses/stb_vorbis-LICENSE.txt` beside the application.

## Rule: never edit these files

Nothing in this folder except this README is changed by hand, so an update is a clean replacement. Its `MANIFEST.txt`, written only by the update script, records every file, and CI checks the copy against it (`scripts/CheckVendored.cmake`). Every Raven setting lives in [`cmake/RavenVorbis.cmake`](../../cmake/RavenVorbis.cmake):

- **Library:** a static `RavenStbVorbis`, built from `stb_vorbis.c` as C and linked privately into RavenCore. Game DLLs and public headers never see it.
- **Compiled out:** the `FILE*` functions (`STB_VORBIS_NO_STDIO`); Raven reads files itself.
- **Warnings:** Raven's warning level does not apply to third-party code.

Raven uses stb_vorbis in one place: Ogg Vorbis decoding in [`Private/Audio/AudioClip.cpp`](../../RavenEngine/Private/Audio/AudioClip.cpp), behind `DecodeAudio`, which declares the API with `STB_VORBIS_HEADER_ONLY`. Music streaming will be the second, behind the same clip seam.

Untrusted files: stb_vorbis has had memory-safety bugs. Fuzz it before games load audio from mods or downloads ([UIAudioLibraries.md](../../docs/UIAudioLibraries.md#risks)).

## Updating to a new release

1. Download `stb_vorbis.c` and `LICENSE` from a commit of `nothings/stb` into an empty folder.
2. Run `cmake -DSOURCE=<that folder> -P scripts/UpdateStbVorbis.cmake`.
3. Set `RAVEN_STB_VORBIS_VERSION` in `cmake/RavenVorbis.cmake` and record the commit and hashes above.
4. Build, then decode an Ogg clip and a looping one.
