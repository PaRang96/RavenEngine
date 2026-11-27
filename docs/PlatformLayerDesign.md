# Platform layer design note

Status: **draft for the owner's decision (2026-11-27).** [Planning decision 21](../ROADMAP.md#planning-decisions-2026-11-24) (owner, 2026-11-27): Windows is the head platform, and platform code is only ever encapsulated, so macOS and Linux follow by implementing interfaces. The owner asked that the encapsulation take other engines as reference, Unreal's `FPlatform*` layer in particular. This note compares three engines with Raven's current layer and proposes how Raven's layer is shaped and checked from now on. [PlatformHandoff.md](PlatformHandoff.md) keeps the list of what each platform still lacks.

---

## How other engines do it

### Unreal Engine: the HAL (hardware abstraction layer)

- **One struct per area, with static functions:** `FGenericPlatformMisc`, `FGenericPlatformProcess`, `FGenericPlatformFile`, `FGenericPlatformTime`, `FGenericPlatformMemory`, `FGenericPlatformProperties` and others, under `Core/Public/GenericPlatform/` ([FGenericPlatformMisc](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/FGenericPlatformMisc), [FGenericPlatformProcess](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/FGenericPlatformProcess)).
- **Generic defaults, platform structs derive:** `struct FUnixPlatformMisc : public FGenericPlatformMisc` ([FUnixPlatformMisc](https://docs.unrealengine.com/4.27/API/Runtime/Core/Unix/FUnixPlatformMisc/index.html)). A platform redefines only what differs; everything else falls back to the generic version, so a new platform compiles before it is complete.
- **One alias, chosen at compile time:** each platform header ends with a typedef such as `typedef FUnixPlatformTypes FPlatformTypes` ([FPlatformTypes](https://docs.unrealengine.com/4.26/API/Runtime/Core/Unix/FPlatformTypes/index.html)). Engine code calls only `FPlatformMisc::…`, `FPlatformProcess::…`, never a concrete platform. The functions are not virtual: there is no runtime cost, and a call to the generic base by mistake is a known pitfall ([forum](https://answers.unrealengine.com/questions/270513/view.html)).
- **Capabilities as properties:** `FGenericPlatformProperties` answers questions such as "supports windowed mode" per platform ([FGenericPlatformProperties](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/FGenericPlatformProperties)).
- **Windows and applications are a separate module:** `ApplicationCore` holds the windowing side (`FGenericApplication`, `FGenericPlatformApplicationMisc`) as runtime objects ([ApplicationCore](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/ApplicationCore?lang=en-US)).
- Consoles live in separate platform-extension folders, so platforms under NDA stay out of the main tree.

### Godot: runtime singletons and self-contained platform folders

- A port lives in one folder, `platform/<name>/`, with a `detect.py` that the build reads; shared drivers (audio, rendering) stay in `drivers/` ([Custom platform ports](https://docs.godotengine.org/en/4.4/contributing/development/core_and_modules/custom_platform_ports.html)).
- The minimum port implements the `OS` singleton, which is enough for headless use. Windowing and input are one or more `DisplayServer` implementations, chosen at runtime (X11 or Wayland, for example).
- Unix-like ports inherit `OS_Unix` to get most of the work for free; others start from the Windows port.

### O3DE: the PAL (platform abstraction layer)

- Every module has `Platform/<Name>/` folders (AzCore has Android, Common, Emscripten, Linux, Mac, Windows and iOS), plus `Common` for shared code.
- CMake detects the platform (`PAL_PLATFORM_NAME`) and pulls in per-platform file lists (`platform_<name>_files.cmake`) through `o3de_pal_dir`, which also finds restricted platforms kept in a separate repository ([PAL.cmake](https://github.com/o3de/o3de/blob/development/cmake/PAL.cmake)).
- Each platform has a traits header of constants, such as `AZ_TRAIT_OS_PATH_SEPARATOR`, `AZ_TRAIT_OS_MAX_PATH_LEN` and `AZ_TRAIT_USE_PLATFORM_SIMD_SSE`, so shared code asks for a trait instead of testing an OS macro ([AzCore Platform](https://github.com/o3de/o3de/tree/development/Code/Framework/AzCore/Platform)).

### What they share

1. **Shared code never names a platform.** It calls one alias (Unreal), one singleton (Godot) or one trait (O3DE).
2. **A platform is a folder,** selected by the build, and a new one can start from defaults or fallbacks.
3. **Capabilities are explicit:** properties, `has_feature` style queries or traits, instead of `#ifdef` in shared code.
4. **Windowing is a runtime object,** because one OS can have several display systems.

---

## Raven today (checked at `cfef00f`)

- **Selection at link time.** Interfaces are free functions and small `Impl` classes in `Raven::Platform` (`Private/Platform/*.hpp`). Each OS implements them under `Platform/Windows`, `Mac` or `Linux`, and `cmake/RavenPlatform.cmake` picks the folder. That matches point 2 above.
- **Fallbacks are whole files:** `UnsupportedBuildProcess.cpp`, `UnsupportedGameModule.cpp` and `UnsupportedKeyNames.cpp`. Shared POSIX code (`PosixAtomicFile.cpp`, `PollDirectoryWatch.cpp`) sits at the top level beside them, and `RavenSources.cmake` keeps a hand-written regex to exclude them all, a second list beside `RavenPlatform.cmake`.
- **The window is a runtime interface** (`Raven::Window`, `Window::Create`), like Unreal's `ApplicationCore` and Godot's `DisplayServer`.
- **Capabilities are ad hoc:** one query exists, `SupportsBuildProcesses()`. Other differences are discovered by a fallback throwing.
- **Names are uneven:** `AtomicFile.hpp`, `BuildProcess.hpp`, `DirectoryWatch.hpp`, `GameModulePlatform.hpp`, `PlatformKeyNames.hpp`, `PlatformPaths.hpp` (whose functions sit in `Raven`, not `Raven::Platform`) and `PlatformRandom.hpp`. A contributor has no list of what a port must implement.
- **Leaks outside `Platform/`:** `_MSC_VER` in `Private/Host/RavenEngine.cpp`; `Api.hpp`'s export macros, which are inherent; and in CMake, the Windows and MSVC gates in `RavenGame.cmake` and `RavenGameModule.cmake` and the Windows libraries linked in `RavenPlatform.cmake` (CMake is the selector, so that last part is correct). Time, threads, atomics and memory use the C++ standard library, which is the platform layer for those, as in Godot's core.

---

## Options

| | **A. Unreal-style HAL** | **B. Godot-style runtime singletons** | **C. Raven's link-time layer, formalized with the shared points** *(recommended)* |
| --- | --- | --- | --- |
| **Shape** | One struct of static functions per area (`Platform::Process`, `Platform::File`, `Platform::Misc`, `Platform::Properties`), a generic base with defaults, a per-OS derived struct, and a compile-time alias | One abstract `Platform::Os` singleton plus the window interface, created at startup | Today's free functions and classes, grouped into named areas, with a capability header, folders per OS plus `Posix` and `Fallback`, one CMake selection, and a mechanical check |
| **Shared code calls** | `Platform::Process::StartBuild(…)` | `Platform::Os::Get().StartBuild(…)` | `Platform::StartBuildProcess(…)`, as today, in a named area |
| **New platform starts from** | Generic defaults, per function | A base class (`Os_Posix`), per class | Fallback files, per area, and a shared `Posix` folder |
| **Capabilities** | `Platform::Properties`, constexpr | Virtual queries | `Platform::Capabilities`, constexpr, one header per OS |
| **Runtime cost** | None (static, inlinable) | One virtual call per use (negligible for these calls) | None (direct calls) |
| **Change now** | Rewrite every interface header and caller (eight headers and about 20 call sites) | Same, plus a global object | Rename and move about 12 files, add one header and one CI check; callers keep their calls |
| **Strength** | Familiar to Unreal developers; per-function defaults | Runtime choice of backend; easy to fake in tests | Keeps what works, takes the shared points of all three, and is the cheapest to keep checked |
| **Weakness** | Name-hiding pitfalls; buys inlining Raven does not need, since time, atomics and threads come from the standard library | A process-wide singleton, and virtual dispatch for calls that never change at runtime | Per-area fallbacks instead of per-function defaults, so a port adds whole files |

### Recommended: C, in detail

1. **Named areas,** each one header in `Private/Platform/`, all in `Raven::Platform`. These are the Unreal HAL's areas, adapted to Raven's needs:

   | Area | Header | Covers |
   | --- | --- | --- |
   | Process | `PlatformProcess.hpp` | background build processes, game-module loading, image checks |
   | Files | `PlatformFiles.hpp` | atomic file replace, directory watching |
   | Paths | `PlatformPaths.hpp` | executable path, per-user data folder |
   | Random | `PlatformRandom.hpp` | OS random bytes |
   | Input | `PlatformInput.hpp` | native key names, and the window's input buffer |
   | Window | `Public/Raven/Platform/Window.hpp` | the window and surface (a runtime interface, as in Unreal and Godot) |
   | Capabilities | `PlatformCapabilities.hpp` | constexpr facts per platform, below |

2. **Capabilities, constexpr, one header per OS** (like Unreal's properties and O3DE's traits): `SupportsBuildProcesses`, `SupportsGameModuleReload`, `SupportsNativeDirectoryWatch`, `SupportsNativeKeyNames`. Shared code branches on these instead of discovering a throw. Development mode, for example, turns shader watching on only where the capability says so.
3. **Folders:** `Platform/Windows`, `Platform/Mac`, `Platform/Linux`, `Platform/Posix` (shared by Mac and Linux), `Platform/Fallback` (the `Unsupported*` files). `RavenPlatform.cmake` alone selects folders, and the second regex in `RavenSources.cmake` goes.
4. **A mechanical check,** like the vendored-file check: a CI step fails when an OS macro (`_WIN32`, `__APPLE__`, `__linux__`) or an OS header (`Windows.h`, `<unistd.h>`, Cocoa) appears outside `Platform/`, with `Api.hpp` as the only listed exception. Compiler checks such as `_MSC_VER` go to one `Raven/Core/Compiler.hpp`.
5. **A port contract table** in [PlatformHandoff.md](PlatformHandoff.md): area, functions, Windows file, fallback, and what M4 owes. That is the list a macOS or Linux contributor follows.
6. **Room for restricted platforms:** CMake can take a platform folder from outside the repository, as Unreal's platform extensions and O3DE's restricted platforms do. Nothing is built for consoles; the selection simply does not assume the folder is in the tree.

**Timing.** Steps 1–5 are private code with no game-contract change. They fit before M6B's B2, beside the remaining AA2-PLAT-02 items, since M6B adds callers (texture streaming workers, shader compiles). Step 6 costs nothing until a platform needs it.
