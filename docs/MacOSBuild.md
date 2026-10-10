# macOS 빌드

Xcode Command Line Tools, CMake 3.24 이상, Ninja, Vulkan 개발 라이브러리,
SPIR-V 지원 DXC(`dxc`), SPIRV-Tools(`spirv-val`)가 필요합니다.

[공식 macOS Vulkan SDK](https://vulkan.lunarg.com/doc/view/latest/mac/getting_started.html)는
DXC를 포함합니다. Homebrew의 `shaderc`/`glslang`만으로는 이 프로젝트가 사용하는
DXC 명령을 제공하지 않습니다.

SDK를 설치한 뒤 실제 설치 위치로 환경 변수를 설정합니다. `VULKAN_SDK`는
SDK의 `macOS` 디렉터리를 가리키며, 도구 디렉터리 이름은 소문자 `bin`입니다.

```sh
export VULKAN_SDK="/path/to/VulkanSDK/<version>/macOS"
export PATH="$VULKAN_SDK/bin:$PATH"
cmake --preset macos-debug
cmake --build out/build/macos-debug
```

이미 Homebrew Vulkan loader/headers를 사용하는 빌드라면 DXC 경로만 지정할 수도 있습니다.

```sh
cmake --preset macos-debug -DRAVEN_DXC_EXECUTABLE="$VULKAN_SDK/bin/dxc"
cmake --build out/build/macos-debug
```

`RAVEN_DXC_EXECUTABLE-NOTFOUND`는 C++ 컴파일 전 CMake 의존성 탐색 실패입니다.
`dxc`를 설치하거나 위 옵션으로 실제 실행 파일을 지정한 뒤 다시 구성합니다.
`glslc`를 해당 옵션에 지정하면 명령행 옵션이 달라 셰이더 빌드가 실패합니다.

현재 작업 공간에는 SDK 1.4.363.0을 시스템 설치 없이 `out/tools/vulkan-sdk/1.4.363.0`에
준비했습니다. 이 위치의 DXC를 사용하려면 저장소 루트에서 다음 명령을 실행합니다.

```sh
cmake --preset macos-debug \
    -DRAVEN_DXC_EXECUTABLE="$PWD/out/tools/vulkan-sdk/1.4.363.0/macOS/bin/dxc"
cmake --build out/build/macos-debug
```

`out/`은 Git에 포함되지 않습니다. 새 체크아웃이나 `out/` 삭제 후에는 도구 설치와
구성이 다시 필요합니다. Release는 `macos-release` preset과 빌드 디렉터리를 사용합니다.

실행 파일은 `out/build/macos-debug/RavenEngine/RavenEngine`에 생성됩니다.
빌드 완료와 네이티브 입력·Vulkan 실행 검증은 별개이며, 남은 검증은
[플랫폼 작업 목록](PlatformHandoff.md)을 따릅니다.
