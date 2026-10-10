# RavenEngine

RavenEngine is a work-in-progress C++20 game engine targeting 2D and 3D games, Vulkan rendering, ECS, native platform backends, and hot reload. The current Windows build contains `RavenCore.dll` and a `RavenEngine.exe` sample with native input, a perspective camera, reusable meshes, and depth-tested 3D rendering.

Track the remaining work and its completion criteria in the [RavenEngine roadmap](ROADMAP.md) and [TODO list](TODO.md).

## Source layout

```text
RavenEngine/
  Public/Raven/       Engine headers shared with clients
    Assets/           Owned CPU images and PNG/JPEG loading
    Core/             Application/client interface, paths, files, logging, and frame time
    Math/             Shared transform math
    Platform/         Window and input contracts
    Renderer/         Camera, vertices, draw data, and renderer interface
  Private/            Engine implementation and internal headers
    Assets/           Private image decoder integration
    Core/             Application loop, utilities, logging implementation, and frame clock
    Platform/         Native backend implementations
    Renderer/         Vulkan resource owners and HLSL shaders
    Samples/          Sample entry point, geometry, animation, and camera controls
  CMakeLists.txt      Explicit engine/sample sources and include visibility
ThirdParty/stb/       Pinned image decoder, license, and upstream provenance
```

Clients include headers such as `Raven/Core/Application.hpp` through the `RavenCore` target. Only `Public` is shared as an engine include directory. `VulkanContext` stores its implementation privately, and meshes are borrowed through an opaque reference; public renderer headers do not include internal Vulkan owners. See [EngineModules.md](docs/EngineModules.md) for exports and ownership.

Developer utilities include `Paths::GetContentPath()`, `GetShaderPath()`, executable paths, and binary/text file readers. Validated shaders are staged in `Content/Shaders` beside the executable; copying the executable, DLL, and `Content` together preserves shader lookup from any working directory. See [RuntimeUtilities.md](docs/RuntimeUtilities.md) for usage and application content-root configuration.

`VulkanContext::CreateTexture()` now owns and synchronously uploads RGBA8 textures in Linear or SRGB mode. The public interface exposes a pixel description and an opaque borrowed texture reference; native image/view/sampler ownership stays private. See [TextureResources.md](docs/TextureResources.md) for the contract and GPU validation.

`Images::Load()` decodes PNG/JPEG files into owned RGBA8 CPU pixels for texture creation. The pinned decoder is compiled privately into RavenCore; builds need no manual image-library download or additional decoder DLL. Its license is staged in `Licenses` beside the executable. See [ImageLoading.md](docs/ImageLoading.md) for usage, format/size limits, and validation. Descriptors and visible texture sampling remain pending.

## Build on Windows

From the **x64 Native Tools Command Prompt for VS 2026** (or another developer shell targeting x64):

```powershell
cmake --preset x64-debug
cmake --build out/build/x64-debug
.\out\build\x64-debug\RavenEngine\RavenEngine.exe
```

## macOS/Linux 담당자 작업

현재 Windows 구현에 맞춘 담당자별 작업 순서와 검증 기준은 [플랫폼 협업 작업 목록](docs/PlatformHandoff.md)을 참고하세요. macOS preset은 있지만 Cocoa 인터페이스와 실행 파일 경로 구현이 아직 필요합니다. Linux X11/XCB backend와 Debug/Release preset은 현재 엔진 구조에 맞춰 병합했으며, Linux에서의 빌드와 실행 검증은 아직 필요합니다. 각 OS의 네이티브 실행을 검증한 뒤 지원 완료로 표시합니다.

The `out/` directory contains generated build files and is ignored by Git. The project currently has no automated tests.
