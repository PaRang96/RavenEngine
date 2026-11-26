# miniaudio

Unmodified files from [miniaudio 0.11.26](https://github.com/mackron/miniaudio/releases/tag/0.11.26), tag commit [`877ea4a7c2601c7b0f2a44e83dfac42e6470fea6`](https://github.com/mackron/miniaudio/tree/877ea4a7c2601c7b0f2a44e83dfac42e6470fea6), released 2026-10-31. Downloaded on 2026-11-25 from `https://raw.githubusercontent.com/mackron/miniaudio/0.11.26/<file>`:

| File | Bytes | SHA-256 |
| --- | --- | --- |
| `miniaudio.h` | 4,161,918 | `6b376c7fbb1352ba1e0e432ab66069b335ea9405e45cb6c30b97ef0f3c9c3e0e` |
| `miniaudio.c` | 56 | `ab1984bb9804ffd7b0303813595d0b345a8a86c34da1daffc353a14b34102a65` |
| `LICENSE` | 2,597 | `457f1b500e0adf6bc059edddfa78a2f62012e7c3bb43476c20e0bd23b25ba0eb` |

The [LICENSE](LICENSE) offers public domain (Unlicense) or MIT-0. CMake stages it as `Licenses/miniaudio-LICENSE.txt` beside the application.

## Rule: never edit these files

Nothing in this folder except this README is changed by hand, so an update is a clean replacement. Its `MANIFEST.txt`, written only by the update script, records every file, and CI checks the copy against it (`scripts/CheckVendored.cmake`). Every Raven setting lives in [`cmake/RavenAudio.cmake`](../../cmake/RavenAudio.cmake):

- **Library:** a static `RavenMiniaudio`, built from `miniaudio.c` and linked privately into RavenCore. Game DLLs and public headers never see it.
- **Compiled out:** the high-level engine, node graph, resource manager, encoders and generators. Raven owns the mixer ([UIAudioLibraries.md](../../docs/UIAudioLibraries.md#hiding-the-device-layer-owner-question-2026-11-25)). The WAV, FLAC and MP3 decoders stay in for clip loading (decision A6).
- **Backends:** WASAPI (Windows), Core Audio (macOS), PulseAudio and ALSA (Linux) only.
- **macOS:** runtime linking is off and the frameworks are linked, so notarized builds pass.

Raven uses miniaudio in two places, each behind an engine interface:
- the device backend, [`Private/Audio/Backends/MiniaudioDevice.cpp`](../../RavenEngine/Private/Audio/Backends/MiniaudioDevice.cpp), behind `AudioDevice`;
- clip decoding in [`Private/Audio/AudioClip.cpp`](../../RavenEngine/Private/Audio/AudioClip.cpp): its WAV, FLAC and MP3 decoders, and the converter that turns every decoded file, Ogg Vorbis included, into the mixer format.

## Updating to a new release

1. Download `miniaudio.h`, `miniaudio.c` and `LICENSE` of the release tag into an empty folder.
2. Run `cmake -DSOURCE=<that folder> -P scripts/UpdateMiniaudio.cmake`.
3. Set `RAVEN_MINIAUDIO_VERSION` in `cmake/RavenAudio.cmake` and record the tag, commit and hashes above.
4. Build, then check playback, an unplugged device and a default-device change.

Version 0.12 moves the implementation into `miniaudio.c` and changes some APIs. Only the two files above need to follow.
