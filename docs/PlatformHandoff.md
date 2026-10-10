# macOS / Linux 담당자 작업 목록

기준일: 2026-10-10. 목표는 **현재 Windows에서 검증한 엔진과 perspective 샘플을 동일한 공유 인터페이스로 실행하는 것**입니다. 아래 항목은 각 플랫폼 담당자가 구현하고 해당 OS에서 검증할 작업입니다. 체크박스는 검증 결과를 남긴 뒤 완료 처리합니다.

## 현재 기준과 담당 범위

Windows에서는 C++20 Debug/Release 빌드, 네이티브 창과 입력, Vulkan 초기화와 GPU 선택, swapchain 복구, perspective 카메라, depth testing, mesh와 texture 업로드가 동작합니다. PNG/JPEG 로딩과 실행 파일 기준 Content 경로도 구현되어 있습니다. 최근 인터페이스 검증에서는 구성별 27개 DLL 클라이언트 검증과 8프레임 렌더링을 통과했습니다.

macOS는 기존 Cocoa 구현의 인터페이스가 오래되어 현재 공유 코드와 맞지 않습니다. Linux는 창 backend와 CMake preset이 없습니다. 두 플랫폼의 네이티브 실행은 아직 검증하지 않았습니다.

| 담당 | 우선 수정할 위치 | 목표 |
| --- | --- | --- |
| macOS 담당자 | `RavenEngine/Private/Platform/Mac/` | 기존 Cocoa backend를 현재 창·입력·framebuffer·경로 계약에 맞추기 |
| Linux 담당자 | `RavenEngine/Private/Platform/Linux/` — 신규 | 선택한 네이티브 창 backend와 실행 파일 경로 구현 |
| 두 담당자 조율 | `RavenEngine/CMakeLists.txt`, `CMakePresets.json`, 빌드 문서 | 각 OS의 소스 선택, 의존성, 공유 라이브러리 실행 구성 |

공유 renderer, math, 샘플, 이미지 decoder는 기존 구현을 재사용합니다. 공유 API 수정이 필요하면 이유와 영향을 먼저 정리해 Windows 담당자와 조율합니다. CMake 공통 파일을 수정할 때 다른 담당자의 변경을 덮어쓰지 않습니다.

## 먼저 확인할 공유 인터페이스

선언의 기준은 [Window.hpp](../RavenEngine/Public/Raven/Platform/Window.hpp)입니다. 각 backend의 헤더와 구현은 반환형, 인자형, 참조, `const`를 그대로 맞춰야 합니다.

```cpp
void PollEvents() override;
bool ShouldClose() const override;
std::uint32_t GetWidth() const override;
std::uint32_t GetHeight() const override;
FramebufferState GetFramebufferState() const override;
std::vector<const char*> GetRequiredVulkanInstanceExtensions() const override;
const InputState& GetInputState() const override;
VkSurfaceKHR CreateVulkanSurface(VkInstance instance) const override;
```

`Window::Create(const WindowDesc&)`는 해당 OS에서 선택한 backend를 생성합니다. 공개 선언의 `RAVEN_API`를 유지하고, backend 구현은 `Window`의 virtual destructor를 통해 정상적으로 해제되어야 합니다. 엔진이 `WindowDesc::Title`의 UTF-8 문자열을 네이티브 제목으로 전달할 수 있어야 합니다.

[PlatformPaths.hpp](../RavenEngine/Private/Platform/PlatformPaths.hpp)의 필수 경로 hook은 다음과 같습니다.

```cpp
namespace Raven
{
	std::filesystem::path GetPlatformExecutablePath();
}
```

이 함수는 **실행 중인 호스트 실행 파일의 절대 경로**를 반환합니다. 엔진 공유 라이브러리의 위치나 현재 작업 디렉터리를 반환하면 안 됩니다. 나머지 Content/Shader 경로 계산은 공유 [Paths.cpp](../RavenEngine/Private/Core/Paths.cpp)를 사용합니다.

다음 공개 진입점도 유지합니다. 기본 인자를 추가하는 방식으로 기존 overload를 없애면 이미 빌드된 클라이언트의 링크가 깨질 수 있습니다.

- `Application::Run(description, factory)`와 `Application::Run(description, factory, config)`.
- `Images::Load(path)`와 `Images::Load(path, ImageLoadOptions)`; options는 값으로 전달합니다.
- `Window::Create(const WindowDesc&)`의 공개 export.
- `ApplicationClient::OnFrame(VulkanContext&, const InputState&, const FrameTime&)`와 `ApplicationClientFactory`의 create/destroy callback. Destroy callback의 `noexcept`를 유지하고, 객체를 생성한 모듈에서 해제합니다.

계약과 최근 수정 이유는 [EngineModules.md](EngineModules.md), [RuntimeUtilities.md](RuntimeUtilities.md), [ImageLoading.md](ImageLoading.md)를 참고합니다. 각 OS에서도 공개 헤더만 사용하는 별도 클라이언트가 엔진 공유 라이브러리에 링크되어야 합니다. Windows의 export 이름 수를 다른 OS에 그대로 적용하지 말고, 공개 진입점의 링크와 호출 가능 여부를 검증합니다.

## macOS 담당자 — 작업 순서

1. **창 인터페이스부터 수정**

   - [ ] `Arm64Mac.hpp`와 `Arm64Mac.mm`의 오래된 `IsKeyDown(Key) const override` 선언/구현을 현재 입력 인터페이스로 교체합니다. `Window`에는 이 virtual 함수가 없습니다.
   - [ ] `const InputState& GetInputState() const`와 `FramebufferState GetFramebufferState() const`를 선언하고 구현합니다. 헤더에 선언만 추가한 상태를 완료로 처리하지 않습니다.
   - [ ] 모든 공유 창 메서드에 올바른 `override`가 적용되고, `Arm64Mac`이 abstract class로 남지 않는지 확인합니다. 네이티브 타입은 private 구현에 둡니다.

2. **Cocoa 입력과 drawable 상태 연결**

   - [ ] Escape만 저장하는 현재 구현을 공유 `InputState`로 연결하고, 아래 공통 입력 검증에 필요한 키·마우스·휠을 구현합니다.
   - [ ] Retina 배율, 창 크기 변경, 최소화/복원에 따라 drawable pixel 크기와 지속적인 framebuffer revision을 갱신합니다.
   - [ ] 이벤트 처리는 창 스레드에서 수행하고, 창 닫기 요청 후 Vulkan surface가 해제되기 전에 네이티브 창을 먼저 파괴하지 않도록 수명을 확인합니다.

3. **실행 파일 경로 hook 구현**

   - [ ] Mac 전용 경로 구현 파일에서 `Raven::GetPlatformExecutablePath()`를 정의하고 CMake의 APPLE 소스 목록에 포함합니다. 현재 Mac 소스 목록에는 이 구현이 없습니다.
   - [ ] 한글·공백 경로, 다른 작업 디렉터리에서 실행, ContentRoot override, 배포 위치 변경을 공통 경로 검증으로 확인합니다.

4. **Vulkan / MoltenVK 실행 확인**

   - [ ] Metal surface 생성, GPU 열거, graphics/present queue 선택, swapchain 생성과 presentation을 실제 Mac에서 확인합니다.
   - [ ] 기존 공유 코드의 portability enumeration flag와 portability subset device extension 처리를 확인합니다. 필요한 추가 요구 사항이 발견되면 공유 renderer를 복제하지 않고 Windows 담당자와 조율합니다.
   - [ ] 현재 HLSL/SPIR-V, depth format, RGBA8 Linear/SRGB texture 업로드가 대상 GPU에서 동작하는지 확인하고, 지원되지 않는 기능의 오류를 기록합니다.

5. **빌드와 라이브러리 배치 확인**

   - [ ] 기존 `macos-debug` / `macos-release` preset을 사용해 실제 빌드를 완료합니다. Apple Silicon 대상 아키텍처, C++20/Objective-C++20, ARC와 Cocoa/QuartzCore 연결을 확인합니다.
   - [ ] DXC, `spirv-val`, Vulkan loader/MoltenVK의 탐색과 실행 설정을 재현 가능하게 문서화합니다. 개발자 개인의 절대 경로를 소스에 넣지 않습니다.
   - [ ] 샘플이 `RavenCore` 공유 라이브러리와 `Content/Shaders`, `Licenses/stb-LICENSE.txt`를 배포 위치에서 찾도록 확인합니다. 공유 라이브러리의 런타임 검색 경로도 검증합니다.

완료 후 사용할 빌드/실행 경로는 아래와 같습니다. 현재 backend 수정 전에는 성공이 보장되지 않습니다.

```sh
cmake --preset macos-debug
cmake --build out/build/macos-debug
./out/build/macos-debug/RavenEngine/RavenEngine

cmake --preset macos-release
cmake --build out/build/macos-release
./out/build/macos-release/RavenEngine/RavenEngine
```

## Linux 담당자 — 작업 순서

1. **지원할 창 시스템 결정**

   - [ ] 첫 대상이 X11, Wayland, 또는 둘 모두인지 정하고, 배포판·desktop session·GPU를 기록합니다. 현재 코드와 roadmap에는 선택이 확정되어 있지 않습니다.
   - [ ] 첫 backend의 네이티브 창/입력/Vulkan surface 의존성을 정리합니다. 다른 창 시스템 지원은 별도 작업으로 기록하고 지원 여부를 명확히 표시합니다.

2. **창 backend 구현**

   - [ ] `Private/Platform/Linux/` 아래에 backend 헤더/구현을 추가하고, 위 공유 창 메서드를 모두 구현합니다.
   - [ ] Linux 전용 `Window::Create(const WindowDesc&)` 정의를 연결합니다. CMake가 선택한 backend의 factory 정의를 정확히 하나만 링크하도록 합니다.
   - [ ] 창 생성/닫기/이벤트 처리, UTF-8 제목, 입력, drawable pixel 크기, 최소화/복원과 framebuffer revision을 연결합니다.

3. **실행 파일 경로 hook 구현**

   - [ ] Linux 전용 구현에서 `Raven::GetPlatformExecutablePath()`를 정의합니다. 공유 경로 계산과 파일 reader는 재사용합니다.
   - [ ] 실행 파일 위치, 다른 작업 디렉터리, 한글·공백 경로, Linux 파일명의 대소문자 구분과 ContentRoot override를 확인합니다.

4. **CMake와 preset 추가**

   - [ ] 현재 미지원 플랫폼 오류로 끝나는 CMake 분기에 Linux 소스·의존성 선택을 추가합니다. 다른 미지원 OS까지 Linux backend로 선택하지 않도록 합니다.
   - [ ] Linux Debug/Release preset을 추가하고 실제 preset 이름과 실행 경로를 문서화합니다. Windows의 MSVC 설정이나 macOS의 Cocoa 설정을 공유 코드로 옮기지 않습니다.
   - [ ] C++20, Vulkan, DXC, `spirv-val`, 선택한 창 시스템의 의존성 탐색을 확인합니다. 샘플과 `RavenCore` 공유 라이브러리의 링크·런타임 검색 경로 및 Content/라이선스 복사를 검증합니다.

5. **Vulkan 실행 확인**

   - [ ] 선택한 창 시스템에 맞는 instance extension 목록과 `CreateVulkanSurface()`를 구현하고, GPU 선택부터 presentation까지 실행합니다.
   - [ ] 창 크기 변경, drawable 사용 불가 상태, out-of-date/suboptimal 복구가 공유 renderer를 통해 동작하는지 검증합니다.
   - [ ] graphics/present queue family가 다른 환경을 사용할 수 있으면 추가 검증합니다. 사용할 수 없으면 미검증 항목으로 남기고 실제 검증한 GPU/queue 정보를 기록합니다.

Linux preset은 아직 없으므로 완료 시 실제 사용한 configure/build/run 명령을 이 문서에 추가합니다.

## 두 플랫폼의 공통 검증 기준

- [ ] **입력:** `PollEvents()` 시작 시 transient 상태를 초기화하고, 실제 이벤트마다 상태를 반영합니다. 같은 poll에서 눌렀다 뗀 입력의 Pressed/Released와 KeyEvents를 모두 보존하고, 반복 key-down을 새 Pressed로 만들지 않습니다.
- [ ] **키와 modifier:** 네이티브 키 번호를 공유 `Key` 값으로 변환합니다. 문자 입력과 physical key를 혼동하지 않습니다. Shift/Control/Alt/Super와 좌우 modifier, 방향키, Home, R, Space를 확인합니다. Text input과 별도 repeat API는 Windows에서도 미완료이므로 새로운 공유 계약이 필요하면 별도 조율합니다.
- [ ] **마우스:** 버튼 전환, cursor 위치/delta, 가로·세로 wheel 누적, focus 상실 시 눌린 상태 해제를 확인합니다. 첫 cursor 위치나 focus 복귀에서 큰 delta가 발생하지 않도록 합니다. 좌표 단위·원점과 휠 방향은 Windows 동작과 비교해 기록합니다.
- [ ] **framebuffer:** 크기는 drawable pixel 기준입니다. 최소화/사용 불가 상태는 `0 x 0`으로 보고하고, 크기 변경 시 Revision을 증가시킵니다. getter 호출이 Revision을 소비하거나 초기화하면 안 됩니다. resize, 배율 변경, 최소화/복원, 최소화 중 닫기를 확인합니다.
- [ ] **시간과 renderer:** 첫 drawable frame과 복원 후 첫 frame의 DeltaSeconds가 0인지 확인합니다. 두 cube와 ground slab, perspective 비율, depth occlusion, orbit/zoom/reset/pause가 Windows 샘플과 같은 방식으로 동작해야 합니다. 공유 Blender 기준 축과 matrix 계약은 [CoordinateSystem.md](CoordinateSystem.md)를 유지합니다.
- [ ] **파일과 이미지:** 실행 파일 기준 Content/Shader 경로, 상대/절대 ContentRoot override, 잘못된 경로와 누락 파일의 진단을 확인합니다. PNG/JPEG를 RGBA8로 로드해 texture 생성에 전달합니다. non-power-of-two 이미지는 허용하면서 기본 옵션에서 개발 빌드에만 경고하고, 명시적 억제와 Release 무경고를 확인합니다.
- [ ] **공개 API와 수명:** 공개 헤더의 독립 컴파일, 별도 클라이언트의 링크, Run/Load의 두 overload 호출, Window factory 호출을 확인합니다. client는 자기 모듈에서 해제되고 renderer보다 먼저 파괴되어야 하며, GPU 자원/surface는 네이티브 창보다 먼저 해제되어야 합니다. 정상 종료와 초기화 실패 양쪽을 확인합니다.
- [ ] **배포와 검증 결과:** Debug/Release와 다른 작업 디렉터리·배포 위치에서 실행합니다. Debug Vulkan core/synchronization validation의 경고·오류를 확인하고 해결합니다. 현재 검증 환경과 미검증 항목을 구분해 남깁니다.

현재 샘플에는 texture descriptor와 화면상의 texture sampling이 없습니다. 이미지 decode/업로드 검증은 별도 간단한 클라이언트로 진행합니다. OS 이식 중 ECS, 물리, 오디오, shader/asset reload, game-module reload를 동시에 새로 구현하지 않습니다. 이들은 기존 [TODO.md](../TODO.md)의 후속 엔진 작업입니다.

## 완료 보고에 포함할 내용

- [ ] OS/아키텍처, compiler/CMake/Ninja/Vulkan 도구 버전, GPU와 graphics/present queue family를 기록합니다.
- [ ] Debug/Release의 실제 configure/build/run 명령, 의존성 설정과 공유 라이브러리 배치 방법을 기록합니다.
- [ ] 위 공통 검증의 통과/실패/미검증 결과, validation log, 샘플 screenshot을 남깁니다. 재현용 생성 파일과 임시 probe는 `out/`에 두고, 검증만을 위한 임시 구현을 엔진에 남기지 않습니다.
- [ ] 공유 파일 변경이 있으면 Windows preset의 회귀 검증 결과를 남기거나 Windows 담당자에게 필요한 검증을 명시합니다. 완료되지 않은 backend를 지원 완료로 표시하지 않습니다.

현재 Windows 증거는 `out/validation/signature-*.log`와 기존 기능별 validation log에 있습니다. `out/`은 Git에서 제외되므로 다른 담당자에게 전달할 때는 검증 요약과 필요한 log/screenshot을 변경 보고에 함께 첨부합니다.
