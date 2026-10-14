# 입력 소유와 실행 순서

Windows 구현 기준: 2026-10-11. 공개 `Window`와 `ApplicationClient`의 signature 및 `InputState`의 데이터 배치는 유지합니다. 에디터/게임 입력 context, action binding, text input은 이번 변경에 포함하지 않습니다.

## 책임과 소유

| 코드 | 소유하거나 처리하는 것 |
| --- | --- |
| `Public/Raven/Platform/Input.hpp` | OS 독립 키·마우스 상태 전환, modifier, cursor delta, scroll 누적, 포커스 상실 시 상태 복구 |
| `Private/Platform/InputBuffer.hpp/.cpp` | 네이티브 창 하나가 소유하는 pending 입력과 published snapshot. 입력 수집과 공개 시점을 분리 |
| `Private/Platform/Windows/Win32Input.hpp/.cpp` | Win32 key message를 공통 물리 `Key`로 변환 |
| `Private/Platform/Windows/Win32EventPump.hpp/.cpp` | 호출한 UI 스레드의 메시지 큐와 `WM_QUIT` 종료 요청 |
| `Private/Platform/Windows/Win32Window.hpp/.cpp` | `HWND`, 창 수명, 네이티브 이벤트 해석과 마우스 capture. 자신의 입력 버퍼에 이벤트를 기록하고 snapshot을 발행 |
| `Private/Core/Application.cpp` | 창의 `PollEvents()` 이후 공개된 입력을 `ApplicationClient::OnFrame()`에 전달 |

창과 입력 버퍼는 해당 창을 생성한 이벤트 스레드에서 사용하고 파괴합니다. 메시지 펌프가 창이나 입력 버퍼의 수명을 소유하지 않습니다. `WM_QUIT`은 스레드 종료 요청이므로 같은 스레드의 모든 Windows 창이 `ShouldClose()`에서 이를 확인합니다.

## 입력 snapshot 발행

1. `WindowProc`는 해당 창의 `InputBuffer`에 입력을 기록합니다. 공통 `InputState` 메서드가 pending 상태를 갱신합니다.
2. 창의 `PollEvents()`는 스레드 메시지 펌프를 실행합니다. 다른 창의 이벤트도 각각의 pending 버퍼에 기록될 수 있습니다.
3. 호출한 창만 `InputBuffer::Publish()`로 자신의 pending 상태를 공개하고, pending의 transient 값만 초기화합니다. held 상태와 cursor 기준 위치는 유지합니다.
4. `GetInputState()`는 마지막으로 발행한 snapshot의 const reference를 반환합니다. 다음 자기 창의 `PollEvents()`까지 다른 창의 polling이나 네이티브 callback이 이 snapshot을 변경하지 않습니다.

예를 들어 A의 polling이 B의 키 이벤트를 처리해도, B의 이벤트는 pending 버퍼에 남습니다. B의 다음 polling에서 Pressed/Released와 KeyEvents가 발행됩니다. 그다음 B polling에서는 새 이벤트가 없다면 transient 값이 초기화됩니다. `GetInputState()`를 여러 번 읽어도 이벤트를 소비하지 않습니다.

이 snapshot의 경계는 창의 polling입니다. 고정 simulation tick과 rendering tick 사이의 이벤트 전달이나 입력 context별 소비는 별도 계층에서 정할 사항입니다.

## 상태 갱신과 네이티브 동작

- 키와 마우스 버튼은 같은 공통 전환 규칙을 사용합니다. 중복 down은 새 Pressed를 만들지 않고, 같은 snapshot의 press/release는 둘 다 보존합니다.
- `SetCursorPosition()`이 위치와 delta를 계산합니다. 첫 위치와 focus 복귀 후 첫 위치는 기준점으로만 사용합니다.
- `HandleFocusLost()`는 키·마우스 버튼을 해제하고 cursor 추적 기준을 초기화합니다. 기존 키 이벤트처럼 포커스 상실에 의한 release에는 `Modifier::None`을 기록합니다.
- capture 상실은 `ReleaseAllMouseButtons()`만 호출합니다. 키 입력이나 cursor 기준까지 focus 상실로 취급하지 않습니다.
- `UpdateMouseCapture()`와 `ReleaseMouseCapture()`는 Windows 네이티브 capture만 관리합니다. 공통 상태 갱신은 capture 호출 전에 수행합니다. 직접 capture를 해제해도 `WM_CAPTURECHANGED`가 발생하므로 공통 해제는 반복 호출에 안전합니다. [Win32 문서](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-capturechanged)

## Windows 키 변환

물리 scan code와 extended flag로 문자 키, 좌우 modifier, navigation cluster, 숫자패드를 구분합니다. 숫자패드는 NumLock에 따른 virtual-key 변화에도 같은 `Key::Numpad*`로 유지합니다. Pause/PrintScreen의 대체 scan sequence와 F13–F24는 Windows가 전달한 특수 virtual-key 식별을 사용합니다. scan code가 없는 메시지에서만 제한된 비문자 virtual-key fallback을 사용하며, 매핑하지 않은 물리 키는 Unknown으로 둡니다. scan code의 의미와 표는 [Windows Keyboard Input 문서](https://learn.microsoft.com/en-us/windows/win32/inputdev/about-keyboard-input)를 따릅니다.

Linux/Mac 담당자는 같은 공유 상태 메서드와 `InputBuffer`를 재사용할 수 있습니다. 두 native backend의 이벤트 연결은 이 변경에서 수정하지 않았습니다.

## 검증 결과

Windows x64 Debug/Release configure/build를 완료했습니다. 구성별 임시 검증에서 57개 입력 검증을 통과했습니다. 이전 `InputState`와 크기·정렬·각 데이터 필드 위치를 비교했고, 기존 18개 DLL export 이름과 공개 함수 signature를 유지했습니다. 변경된 입력 관련 헤더 6개는 각각 `/W4 /WX`로 독립 컴파일했습니다.

실제 Win32 창 두 개에 메시지를 주입해 pending/published 격리, press/release와 repeat, NumLock에 따른 숫자패드 virtual-key 변화, extended navigation/Enter, cursor와 wheel, 여러 버튼의 capture 유지·해제, capture/focus 상실, 창 단위 close와 스레드 단위 quit를 확인했습니다. 메시지 주입 검증이므로 실제 여러 키보드 layout과 하드웨어 전체를 검증한 결과는 아닙니다.

별도 공개 DLL 클라이언트는 구성별 10개 검증과 perspective 샘플 8프레임을 통과했습니다. Debug Vulkan validation에 경고·오류가 없었고 Release 실행에도 오류 진단이 없었습니다. 정상적인 최초 frame delta와 client/renderer 수명, Run 두 overload의 호출을 확인했습니다.

검증 로그는 Git에서 제외되는 `out/validation/input-ownership-<config>-results.log`, `input-render-<config>-results.log`, `input-render-<config>.log`, `input-exports-<config>.log`에 있습니다. 임시 probe source를 추적 파일로 추가하지 않았고 실행 파일과 object는 검증 후 제거했습니다.
