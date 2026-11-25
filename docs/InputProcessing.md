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

For example, if polling window A processes a key event for window B, B retains it in its pending buffer. B's next poll publishes Pressed/Released and Events. A subsequent B poll clears transient values if no new events arrived. Reading `GetInputState()` repeatedly does not consume events.

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
- **Key repeat:** native auto-repeat becomes an `InputTransition::Repeated` entry in `Events` (named `KeyTransition` and `KeyEvents` until M6C step C1), with the current modifiers.
  - `Pressed` still fires once per hold, so gameplay checks are unchanged.
  - `WasKeyRepeated(key)` is the helper for menus and text navigation.
  - Windows identifies repeats by `KF_REPEAT` (bit 30) on `WM_KEYDOWN`/`WM_SYSKEYDOWN`.
  - A repeat for a key that is not down starts a new press. This happens when a key is still held as focus returns.
  - Backends call `RepeatKey()` only for native repeats. A duplicate `SetKeyDown(key, true)` stays silent, because macOS modifier-flag updates resend held modifiers.
- **Stable key IDs:** `Key` values are USB HID keyboard usages for physical positions, independent of layout and platform. Saved key bindings may store them.

Text input (typed characters) is not part of this step. It is added when a text field or console needs it, as a separate per-snapshot list kept apart from physical keys.

### Input mapping contexts, bindings and key listening (M6C step C1, 2026-11-25)

Decided in [GameServicesDesign.md](GameServicesDesign.md#fd-29--input-mapping-contexts-and-bindings) (FD-29). It changes the `InputState` layout and `SystemContext`, so the host and every game module rebuild once.

**One ordered event list.** `InputState::Events` replaces `KeyEvents` and `MouseButtonPresses`. Each `InputEvent` carries an `InputCode` (device plus a stable `Key` or `MouseButton` value), the transition, the modifiers held after it, and for mouse buttons the pointer position and source framebuffer size. Mouse releases are recorded too, with the last cursor position. Key listening can therefore tell which input came first within one snapshot, and gamepad buttons can join later as another `InputDevice` without another layout change.

**Mapping contexts** (`Raven/Input/InputMap.hpp`, host side in `Private/Input/InputRouter`):

1. **Declaring.** A game declares contexts and actions in `RavenRegisterSystems`:
   - `SystemTable::InputContext({ name, priority, blocking, active })`;
   - `SystemTable::Action({ context, name, Button|Axis, bindings })`.

   The host copies them. A duplicate, an invalid binding, an undeclared context or a reserved `Engine.` name rejects the module.
2. **Routing order.** Each frame the router walks every published snapshot through, in order:
   - the engine's **developer context** (development hosts only);
   - **key listening**;
   - the game's active contexts, from the highest priority down.
3. **Consumption.**
   - The first context with a matching binding takes the input.
   - Within that context, the binding requiring the most held modifiers wins, so `Shift+F8` and `F8` can both be bound.
   - A **blocking** context, such as a pause menu, also takes every input it does not bind.
4. **Owned for the whole hold.** An input belongs to its context from press to release. Held state, axes (the sum of held bindings' `Scale`, clamped to [-1, 1]) and `Released` all come from that owner.
5. **What the game sees.**
   - `SystemContext::Actions` (`InputActions`) holds the combined `ActionSample`s of the active game contexts for the frame or fixed step.
   - `SystemContext::Input` is the raw snapshot with every consumed input removed, so a game that still reads physical keys can no longer react to F5.
   - Fixed steps accumulate pressed and released transitions for keys, mouse buttons and actions until a step consumes them. Pointer motion and scroll stay per frame.
6. **Host state.** Context activation (`SetContextActive`, effective from the next frame), rebinding (`Rebind`, `ResetBindings`, `GetBindings`) and key listening (`BeginListening`, `TakeListened`) all live in the host. They survive module reloads: contexts and actions keep their state and rebindings by name. Saving bindings to the user-data folder comes with step C2.
7. **Key listening** captures the next key or mouse-button press with its modifiers. A modifier key counts only when it is released without another press. The developer context stays above listening.
8. **Developer context.** F3 (restart scene), F4 (next scene), F5 (reload game), F7 (render view) and F8 (play/pause) are actions of the `Engine.Developer` context instead of direct key reads. C4 added the physics iteration keys ([Physics.md](Physics.md#developer-keys)): Shift+F8 (restart play), F9 (one fixed step while paused) and F10 (collider view). C9 added the developer quick-save: Ctrl+F5 saves and Ctrl+F9 loads ([UserData.md](UserData.md#developer-quick-save)). The context is absent in player builds (`ApplicationConfig::DeveloperInput`).

**Display names.** `GetDisplayName(chord)` names modifiers first ("Shift+F8"). Keys come from the platform hook `Platform::GetNativeKeyName`, with a shared English fallback. On Windows, the hook runs every scan code through the existing translation table and asks `GetKeyNameTextW` for the active layout's label, so the Q position reads "A" on AZERTY. macOS and Linux return the fallback until their M4 backends add native names.

**Guard:** `Tests/InputMappingTests.cpp` (CPU) covers:
- registration checks;
- developer-first consumption;
- the raw view filter;
- axes;
- blocking contexts;
- modifier specificity;
- key listening, including modifiers released alone;
- rebindings across a module reload;
- the mixed event order.

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
