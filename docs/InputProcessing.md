# Input ownership and execution order

Windows implementation baseline: 2026-10-11. Public `Window` and `ApplicationClient` signatures and the `InputState` data layout are preserved. Editor/game input contexts, action bindings, and text input were outside this change.

## Responsibilities and ownership

| Code | Ownership or responsibility |
| --- | --- |
| `Public/Raven/Platform/Input.hpp` | OS-independent keyboard/mouse transitions, modifiers, cursor delta, scroll accumulation, and recovery on focus loss |
| `Private/Platform/InputBuffer.hpp/.cpp` | Pending input and a published snapshot owned by one native window; separates collection from publication |
| `Private/Platform/Windows/Win32Input.hpp/.cpp` | Translation of Win32 key messages into shared physical `Key` values |
| `Private/Platform/Windows/Win32EventPump.hpp/.cpp` | The calling UI thread's message queue and `WM_QUIT` requests |
| `Private/Platform/Windows/Win32Window.hpp/.cpp` | `HWND`, window lifetime, native event interpretation, and mouse capture; records events in its input buffer and publishes snapshots |
| `Private/Core/Application.cpp` | Passes published input to `ApplicationClient::OnFrame()` after the window's `PollEvents()` |

Use and destroy each window and input buffer on the event thread that created the window. The message pump does not own their lifetimes. `WM_QUIT` requests thread termination, so every Windows window on that thread observes it through `ShouldClose()`.

## Publishing input snapshots

1. `WindowProc` records input in the receiving window's `InputBuffer`. Shared `InputState` methods update pending state.
2. The window's `PollEvents()` runs the thread message pump. Events for other windows can also be recorded in their respective pending buffers.
3. Only the calling window publishes its pending state through `InputBuffer::Publish()`, then resets pending transient values. Held state and the cursor reference position are retained.
4. `GetInputState()` returns a const reference to the last published snapshot. Other windows' polling and native callbacks cannot change it before this window's next `PollEvents()`.

For example, if polling window A processes a key event for window B, B retains it in its pending buffer. B's next poll publishes Pressed/Released and KeyEvents. A subsequent B poll clears transient values if no new events arrived. Reading `GetInputState()` repeatedly does not consume events.

The snapshot boundary is window polling. Delivery across fixed simulation ticks and rendering ticks, or consumption by separate input contexts, belongs to another layer.

## State updates and native behavior

- Keyboard keys and mouse buttons use the same shared transition rules. Duplicate down events do not create a new Pressed transition; a press and release in one snapshot are both preserved.
- `SetCursorPosition()` calculates position and delta. The initial position and first position after focus recovery establish a reference without generating delta.
- `HandleFocusLost()` releases keys/buttons and resets cursor tracking. As with the previous key-event behavior, focus-loss releases record `Modifier::None`.
- Capture loss calls only `ReleaseAllMouseButtons()`. It does not treat keyboard state or the cursor reference as though focus were lost.
- `UpdateMouseCapture()` and `ReleaseMouseCapture()` manage Windows native capture only. Shared state updates happen before capture calls. Explicitly releasing capture also generates `WM_CAPTURECHANGED`, so shared release handling tolerates repeated calls. See the [Win32 documentation](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-capturechanged).

## Windows key translation

Physical scan codes and the extended flag distinguish character keys, left/right modifiers, the navigation cluster, and the numeric keypad. Numeric keypad keys remain the same `Key::Numpad*` values despite NumLock-dependent virtual-key changes. Alternative Pause/PrintScreen scan sequences and F13-F24 use the special virtual-key identifiers provided by Windows. A limited non-character virtual-key fallback applies only to messages without scan codes; unmapped physical keys remain Unknown. Scan-code meanings and tables follow the [Windows Keyboard Input documentation](https://learn.microsoft.com/en-us/windows/win32/inputdev/about-keyboard-input).

Linux/macOS contributors can reuse the same shared state methods and `InputBuffer`. Native event connections in those two backends were not changed as part of this input-ownership milestone.

## Validation results

Windows x64 Debug/Release configure/build passed. Temporary validation passed 57 input checks per configuration. Size, alignment, and every data-field offset were compared with the previous `InputState`; all 18 DLL export names and public function signatures at that milestone were preserved. Six changed input headers compiled independently with `/W4 /WX`.

Messages injected into two real Win32 windows verified pending/published isolation, press/release and repeat, NumLock-dependent keypad virtual keys, extended navigation/Enter, cursor and wheel behavior, capture retention/release with multiple buttons, capture/focus loss, per-window close, and thread-wide quit. Message injection does not establish coverage of every physical keyboard layout or device.

A separate public DLL client passed 10 checks and rendered eight perspective frames per configuration. Debug Vulkan validation reported no warnings/errors, and Release reported no error diagnostics. Checks covered initial frame delta, client/renderer lifetime, and both Run overloads.

Logs are in ignored `out/validation/input-ownership-<config>-results.log`, `input-render-<config>-results.log`, `input-render-<config>.log`, and `input-exports-<config>.log`. Temporary probe sources were not added as tracked files; executables and objects were removed after validation.
