# RavenEngine

RavenEngine is a work-in-progress C++20 game engine targeting 2D and 3D games, Vulkan rendering, ECS, native platform backends, and hot reload. The current Windows build contains `RavenCore.dll` and a `RavenEngine.exe` sample with native input, a perspective camera, reusable meshes, depth-tested colored geometry, opaque/transparent textured quads, and object frustum culling.

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
Content/Textures/    Original PNG patterns used by the sample
```

Clients include headers such as `Raven/Core/Application.hpp` through the `RavenCore` target. Only `Public` is shared as an engine include directory. `VulkanContext` stores its implementation privately, and meshes are borrowed through an opaque reference; public renderer headers do not include internal Vulkan owners. See [EngineModules.md](docs/EngineModules.md) for exports and ownership.

Windows input uses shared state rules and a pending/published buffer owned by each window. The Win32 event pump owns the thread message queue, while key translation and native mouse capture stay in the Windows backend. See [InputProcessing.md](docs/InputProcessing.md) for ownership and snapshot timing.

Developer utilities include `Paths::GetContentPath()`, `GetShaderPath()`, executable paths, and binary/text file readers. Validated shaders are staged in `Content/Shaders` beside the executable; copying the executable, DLL, and `Content` together preserves shader lookup from any working directory. See [RuntimeUtilities.md](docs/RuntimeUtilities.md) for usage and application content-root configuration.

`VulkanContext::CreateTexture()` owns and synchronously uploads RGBA8 textures in Linear or SRGB mode. `CreateTexturedMesh()` and the additional `DrawFrame()` overload render UV geometry with a texture, model transform, and RGBA tint. Texture descriptors and native image/view/sampler ownership stay private. The existing color-mesh API remains available. See [TextureResources.md](docs/TextureResources.md) for the contract and GPU validation.

The four-argument `DrawFrame()` adds straight-alpha draws with explicit layer/order and automatic camera-depth sorting. A private preparation stage performs per-instance frustum selection before Vulkan command recording. The sample shows overlapping transparent quads; `F1` or physical `O` toggles explicit ordering, arrows orbit, wheel zooms, Home/R resets, and Space pauses animation.

Run with `--orthographic` for the 2D sample. `OrthographicCamera::VerticalSize` sets the full visible height in world units; width follows the drawable aspect ratio. The shared frame path handles both camera types with the same depth, transparency, and frustum rules. Arrows pan, wheel/PageUp/PageDown zoom, Home resets the camera, and F1 toggles transparent order. The two upper sprites have equal world/screen sizes despite different depths. See [the coordinate contract](docs/CoordinateSystem.md).

`Images::Load()` decodes PNG/JPEG files into owned RGBA8 CPU pixels for texture creation. The pinned decoder is compiled privately into RavenCore; builds need no manual image-library download or additional decoder DLL. Its license is staged in `Licenses` beside the executable. The sample loads two staged PNGs and displays them alongside the existing cubes and ground slab. See [ImageLoading.md](docs/ImageLoading.md) for usage, format/size limits, and validation.

## Build on Windows

From the **x64 Native Tools Command Prompt for VS 2026** (or another developer shell targeting x64):

```powershell
cmake --preset x64-debug
cmake --build out/build/x64-debug
.\out\build\x64-debug\RavenEngine\RavenEngine.exe
.\out\build\x64-debug\RavenEngine\RavenEngine.exe --orthographic
```

## macOS/Linux 담당자 작업

현재 Windows 구현에 맞춘 담당자별 작업 순서와 검증 기준은 [플랫폼 협업 작업 목록](docs/PlatformHandoff.md)을 참고하세요. macOS 빌드 의존성과 명령은 [macOS 빌드 안내](docs/MacOSBuild.md)에 정리되어 있습니다. Cocoa framebuffer 인터페이스, 실행 파일 경로와 키보드·마우스 입력 연결은 구현되어 있으며, Debug 샘플의 화면 출력과 Space·스크롤·R 조작을 확인했습니다. 나머지 플랫폼 검증은 작업 목록을 따릅니다. Linux X11/XCB backend와 Debug/Release preset은 현재 엔진 구조에 맞춰 병합했으며, Linux에서의 빌드와 실행 검증은 아직 필요합니다. 각 OS의 네이티브 실행을 검증한 뒤 지원 완료로 표시합니다.

The `out/` directory contains generated build files and is ignored by Git. The project currently has no automated tests.
