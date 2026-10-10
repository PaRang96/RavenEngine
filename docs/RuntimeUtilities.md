# Runtime paths and file utilities

Include `Raven/Core/Paths.hpp` and `Raven/Core/Files.hpp` through the `RavenCore` target. Both engine code and game clients use the same exported functions. Public headers use C++ filesystem paths and expose no native operating-system types.

## Runtime layout

```text
RavenEngine.exe
RavenCore.dll
Content/
  Shaders/
    Color.vert.spv
    Color.frag.spv
```

`Paths::GetExecutablePath()` returns the host executable's absolute path, even when called inside the engine DLL. `GetExecutableDirectory()` returns its containing directory. Executable discovery uses the private Windows backend and preserves native Unicode paths.

`GetContentPath()` defaults to `Content` beside that executable. `GetShaderPath()` defaults to its `Shaders` subdirectory. Neither depends on the process working directory. Getters resolve names without requiring the resulting files or directories to exist.

```cpp
const auto texture = Raven::Paths::GetContentPath("Textures/Example.png");
const auto bytes = Raven::Files::ReadBinary(texture);
const auto shader = Raven::Paths::GetShaderPath("Color.vert.spv");
```

Content and shader arguments must be relative paths. Rooted paths, drive-relative paths such as `C:Example.png`, and normalized `..` escapes are rejected with `std::invalid_argument`. Internal parents such as `Textures/../Example.png` are accepted. Normalization is lexical; these helpers do not resolve symbolic links or junctions.

## Application configuration

The optional third argument to `Application::Run` specifies a content root:

```cpp
Raven::ApplicationConfig config;
config.ContentRoot = "GameContent";
app.Run(windowDescription, clientFactory, config);
```

An empty root uses the default. A relative override is based on the executable directory; an absolute override is used directly. A rooted but nonabsolute override is rejected. Roots are not automatically created.

Configuration is established before the window, renderer, and client are created. It stays fixed until all three are destroyed, including exception cleanup. Getters return values and share the current root safely between threads. Only one `Application::Run` may be active in the process; overlapping or nested runs throw `std::logic_error` before creating a second window. Sequential runs are supported. Outside a run, utilities use the default root again.

The renderer captures the shader directory at construction and reuses it during swapchain recreation. `Run(description, factory)` retains its original exported signature and delegates to `Run(description, factory, ApplicationConfig{})`. The explicit three-argument overload accepts custom configuration. Both overloads share the same application loop and cleanup behavior.

## Reading files

`Files::ReadBinary(path)` synchronously returns the entire file as `std::vector<std::byte>`. `ReadText(path)` returns the same bytes as `std::string`, preserving line endings, embedded nulls, and any encoding marker. It performs no character decoding. Empty files return empty containers.

Readers accept native filesystem paths. Relative file paths use the process working directory, so use the path helpers for content lookup. Read errors include the absolute attempted path encoded as UTF-8. An empty input path is rejected; unreadable files, invalid sizes, and incomplete reads throw exceptions.

Shader loading uses `ReadBinary`, checks the SPIR-V minimum header size and word size, copies bytes into aligned `uint32_t` storage, and checks the magic before creating the Vulkan module.

## Build and relocation

DXC and `spirv-val` still generate and validate intermediate shaders under the build directory. The `RavenContent` dependency copies validated outputs to `Content/Shaders` beside the sample on every build, using `copy_if_different`. This restores deleted runtime shaders even when no C++ target needs relinking. Runtime code contains no build-tree shader path.

Copy the executable, matching engine DLL, and `Content` directory together to relocate the sample. The Windows runtime and Vulkan driver requirements still apply. Custom clients using another executable directory must stage their content there or provide an explicit content root.

## Validation on 2026-10-10

Debug and Release builds pass. All 15 public headers compile independently. A temporary client probe passes 61 utility checks and renders 12 frames per configuration, covering executable-relative lookup, normalization and escape rejection, Unicode names, byte-preserving and empty-file reads, missing-file diagnostics, relative/absolute overrides, nested-run rejection, and root restoration after client, factory, and shader failures. The same checks pass again in relocated packages whose directory names contain Korean characters and spaces, launched from another working directory. The existing 17 DLL client checks and eight rendered frames also pass per configuration. Debug core/synchronization validation reports no warnings or errors during these probes.

The relocated Debug sample is visually verified at its original size and after maximizing. Deleting a staged runtime shader and rebuilding only `RavenEngine` restores the identical validated output without recompiling or relinking. In Debug and Release, deleting a relocated package's vertex shader makes the actual sample exit with status 1 and report the exact Unicode path, even while the original build's shader remains available. The test restores the deleted copies afterward.

Follow-up checks verify real binary/text reads and missing-file diagnostics at a 321-character extended Windows file path in both configurations. Eight isolated checks exercise the actual Win32 backend through an API shim, including exact buffer boundaries, repeated growth, the maximum accepted length, limit rejection, and preservation of the native error code. Windows launchers reject the attempted executable path beyond 260 characters in this environment; running the engine from such a directory is not verified. No Windows settings or application manifests were changed.

Evidence is under `out/validation/runtime-*.log`. Probe sources are compiled from standard input; temporary executables, objects, and file fixtures are removed afterward. No test framework or tracked test source is added. Native minimize/restore and Release visual checks for this milestone were not completed after the owner stopped computer use; the earlier renderer milestone's checks remain recorded separately.
