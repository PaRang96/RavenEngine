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
| `Private/Core/Application.cpp` | Passes published input to the engine game session and the game systems after the window's `PollEvents()` (before M6A, to `ApplicationClient::OnFrame()`) |

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
- Native auto-repeat goes through `RepeatKey()`, which records a `KeyTransition::Repeated` event and leaves `Pressed` untouched. See [Focus, key repeat and stable key IDs](#focus-key-repeat-and-stable-key-ids-2026-11-23).
- `SetCursorPosition()` calculates position and delta. The initial position and first position after focus recovery establish a reference without generating delta.
- `HandleFocusLost()` releases keys/buttons and resets cursor tracking. As with the previous key-event behavior, focus-loss releases record `Modifier::None`.
- Capture loss calls only `ReleaseAllMouseButtons()`. It does not treat keyboard state or the cursor reference as though focus were lost.
- `UpdateMouseCapture()` and `ReleaseMouseCapture()` manage Windows native capture only. Shared state updates happen before capture calls. Explicitly releasing capture also generates `WM_CAPTURECHANGED`, so shared release handling tolerates repeated calls. See the [Win32 documentation](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-capturechanged).

## Focus, key repeat and stable key IDs (2026-11-23)

The M4 Windows input step adds three things to the shared contract in `Input.hpp`. They change the `InputState` layout, so game DLLs need one host rebuild.

- **Focus:** `InputState::Focused` says whether the window receives keyboard input.
  - It persists across snapshots and is copied into the fixed gameplay step.
  - Windows refreshes it in each `PollEvents()` from the foreground window. Thread-local focus is not used: a window created behind another application is focused within its thread but receives no keys.
  - Losing focus still releases held keys and buttons through `HandleFocusLost()`.
  - Backends that do not report focus leave it `true`, so an unported backend never looks unfocused.
- **Key repeat:** native auto-repeat becomes a `KeyTransition::Repeated` entry in `KeyEvents`, with the current modifiers.
  - `Pressed` still fires once per hold, so gameplay checks are unchanged.
  - `WasKeyRepeated(key)` is the helper for menus and text navigation.
  - Windows identifies repeats by `KF_REPEAT` (bit 30) on `WM_KEYDOWN`/`WM_SYSKEYDOWN`.
  - A repeat for a key that is not down starts a new press. This happens when a key is still held as focus returns.
  - Backends call `RepeatKey()` only for native repeats. A duplicate `SetKeyDown(key, true)` stays silent, because macOS modifier-flag updates resend held modifiers.
- **Stable key IDs:** `Key` values are USB HID keyboard usages for physical positions, independent of layout and platform. Saved key bindings may store them.

Text input (typed characters) is not part of this step. It is added when a text field or console needs it, as a separate per-snapshot list kept apart from physical keys.

### Planned: key bindings and key listening

The owner requires key bindings and key listening later (2026-11-23). Both build on this contract without changing it:

- **Bindings:** an action map stores `Key` or `MouseButton` values, plus modifiers. It reads `IsKeyDown`, `WasKeyPressed` and `KeyEvents` from the snapshot each frame or fixed step.
- **Key listening** (for example, press a key to bind it): the first `Pressed` entry in `KeyEvents` (skipping `Repeated`), or the first `MouseButtonPresses` entry, in arrival order.
- **Still needed:**
  - Layout-aware display names for a `Key` (for example, the Q position shows "A" on AZERTY), through a platform hook such as `GetKeyNameTextW`.
  - A decision on whether mouse buttons join one ordered event list with keys.

  Both are decided when the binding layer is designed.

### Validation (2026-11-23)

- **Builds and tests:** Debug and Release built, and `ctest` passed. The Debug 3D smoke reported no Vulkan validation messages.
- **Posted-message probe:** a temporary probe (ignored `out/probe-input/`) posted native key messages to a real `Win32Window`. Every case below behaved as specified:
  - press, two repeats and release
  - a Shift-modified repeat (`Modifier::Shift` recorded)
  - press, repeat and release within one snapshot (all three kept)
  - release on focus loss, then a held-key repeat that starts a new press
  - cursor coordinates in client pixels on both monitors
  - `Focused` false for a window created behind the active application
- **Not verified:**
  - **Foreground focus with real input:** `Focused` becoming true when activated, and auto-repeat from a physical keyboard. This process's foreground lock blocked real input, so it needs a manual run.
  - **Mixed-DPI movement:** both attached monitors run at 96 DPI, so moving between them sends no `WM_DPICHANGED`. Checking it needs one monitor set to a different scale.
  - **Non-US keyboard layouts.**

## Windows key translation

Physical scan codes and the extended flag distinguish character keys, left/right modifiers, the navigation cluster, and the numeric keypad. Numeric keypad keys remain the same `Key::Numpad*` values despite NumLock-dependent virtual-key changes. Alternative Pause/PrintScreen scan sequences and F13-F24 use the special virtual-key identifiers provided by Windows. A limited non-character virtual-key fallback applies only to messages without scan codes; unmapped physical keys remain Unknown. Scan-code meanings and tables follow the [Windows Keyboard Input documentation](https://learn.microsoft.com/en-us/windows/win32/inputdev/about-keyboard-input).

Linux/macOS contributors can reuse the same shared state methods and `InputBuffer`. Native event connections in those two backends were not changed as part of this input-ownership milestone.

## Validation results

Windows x64 Debug/Release configure/build passed. Temporary validation passed 57 input checks per configuration. Size, alignment, and every data-field offset were compared with the previous `InputState`; all 18 DLL export names and public function signatures at that milestone were preserved. Six changed input headers compiled independently with `/W4 /WX`.

Messages injected into two real Win32 windows verified pending/published isolation, press/release and repeat, NumLock-dependent keypad virtual keys, extended navigation/Enter, cursor and wheel behavior, capture retention/release with multiple buttons, capture/focus loss, per-window close, and thread-wide quit. Message injection does not establish coverage of every physical keyboard layout or device.

A separate public DLL client passed 10 checks and rendered eight perspective frames per configuration. Debug Vulkan validation reported no warnings/errors, and Release reported no error diagnostics. Checks covered initial frame delta, client/renderer lifetime, and both Run overloads.

Logs are in ignored `out/validation/input-ownership-<config>-results.log`, `input-render-<config>-results.log`, `input-render-<config>.log`, and `input-exports-<config>.log`. Temporary probe sources were not added as tracked files; executables and objects were removed after validation.
