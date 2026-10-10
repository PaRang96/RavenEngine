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

```sh
./out/build/macos-debug/RavenEngine/RavenEngine
```

Debug는 `VK_LAYER_KHRONOS_validation`을 사용합니다. Homebrew의 layer manifest는
라이브러리 이름만 지정하므로, 목록 열거는 성공해도 dylib 검색이 실패하면
`vkCreateInstance failed: -6` (`VK_ERROR_LAYER_NOT_PRESENT`)가 발생할 수 있습니다.
CMake는 설치된 validation dylib를 찾아 Debug 빌드의 `BUILD_RPATH`에 디렉터리를
추가합니다. 이는 빌드 디렉터리에서의 개발 실행 설정이며, 배포 패키지의 설치
경로 설정은 별도로 필요합니다.

2026-10-10 Apple M4 Pro에서 환경 변수 없이 Debug 샘플을 실행해 GPU 선택,
2560 × 1440 swapchain 생성, 첫 perspective 프레임 표시 로그를 확인했습니다.
해당 시작 로그에는 Vulkan warning/error가 없습니다. macOS의 인스턴스 확장에는
Vulkan 1.0에서 portability subset의 의존성인
`VK_KHR_get_physical_device_properties2`도 포함합니다.

키보드·마우스 입력은 `CocoaInput.mm`에서 공통 `InputState`로 변환합니다.
키는 입력 언어나 IME의 문자열 대신 물리 key code를 사용하고, 반복 key-down은
새로운 `Pressed`를 만들지 않습니다. 좌우 Shift/Control/Option/Command는 각각
공통 modifier 키에 연결하며, focus 상실·최소화·닫기 때 눌린 키와 버튼을 해제합니다.

마우스 좌표는 Windows와 같은 content 영역의 왼쪽 위 원점과 실제 픽셀 단위입니다.
가로 wheel은 오른쪽이 양수, 세로 wheel은 위쪽이 양수이며, macOS의 자연스러운
스크롤 반전은 이 방향으로 정규화합니다. 정밀 trackpad delta는 10 point를 한
wheel step으로 변환하고 소수값을 보존합니다. 이는 엔진의 step 단위로 변환하기
위한 배율이며, AppKit이 제공하는 delta 단위는
[Apple 문서](https://developer.apple.com/documentation/appkit/nsevent/scrollingdeltay)를
참고하세요.

47개 임시 입력 변환 검사에서 Space 반복/짧은 tap, 방향키/Home/R, 좌우 modifier,
wheel 누적·반전, Retina cursor, content 밖 drag/release와 focus 해제를 확인했습니다.
검사 로그는 `out/validation/cocoa-input-probe.log`입니다. 실제 Debug 샘플 창에서도
Space로 정지한 두 화면이 동일하고, 스크롤 zoom과 R reset이 반영되는 것을 확인했습니다.
네이티브 검증에는 `out/validation/RavenInputCheck.app` 임시 bundle을 사용했습니다.
여러 입력 장치·키보드 배열과 리사이즈·최소화/복원 등 남은 검증은
[플랫폼 작업 목록](PlatformHandoff.md)을 따릅니다.
