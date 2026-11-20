# Build a RavenEngine player folder

From a Visual Studio x64 developer shell with Python 3, CMake, Ninja, and the Vulkan SDK available, run:

```powershell
python scripts/BuildPlayer.py --preset x64-release --profile public-preview
```

The command builds and publishes `out/player/windows-x64/public-preview/`. Copy the entire folder and open `RavenEngine.exe`. The executable opens Puzzle with staged content and development watching disabled. F4 switches to Collection. F3 restarts the active level. The folder's `README.txt` lists native runtime prerequisites. Keep the copy in a writable directory so the game DLL can create its execution copy.

## Select levels and assets

Build profiles live under `BuildProfiles/`. The `scenes` array contains an ordered list of UUIDs from the development `Content/Project.ravenproject`. Its first scene is the startup level. The packager reads each selected binary Perspective scene and includes its meshes, material textures, and completion audio. The sample's startup textures are included automatically. Put UUIDs for other textures, meshes, or audio that the game loads outside the scene document in the optional `assets` array.

The package contains a generated `Content/Project.ravenproject` with the selected scenes and their required assets. Levels outside the profile remain available in development. Each package is assembled in an empty staging folder before it replaces the previous generated output. This removes stale files when a level is removed from the profile. A failed build or staging check leaves the previous player folder available.

## Select a platform

`--preset` selects the native CMake toolchain, architecture, and build configuration. `--profile` selects game content independently. The player command consumes the native files declared by the configured CMake backend. New platform support can supply its executable, libraries, and prerequisites through that backend and use the same content profile.

Windows x64 Release is the verified player backend. The first profile packages the 3D sample. The macOS and Linux presets remain development presets until their game-module and player runtime support is ready. The separate development executable still opens the full project with hot reload on by default.

## Verification

The public preview was copied to a path with spaces and launched from a separate working directory with the developer tool paths removed. Puzzle and Collection each completed and restarted, and F4 cycled through the two selected levels. An isolated build also added a private level with its own mesh, opened it in development, and explicitly included it once. Rebuilding the public profile removed the private level and its mesh from both the folder and the manifest. An unknown scene UUID failed staging and preserved the previous player's files. These checks ran on the development Windows machine. A separate clean-machine run is still a release check.
