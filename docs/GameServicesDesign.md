# M6C design note: game services and the sample game

Status:
- **Decided (2026-11-24),** with two parts approved only provisionally. The owner settled each decision one at a time; the record is in [Owner decisions](#owner-decisions-2026-11-24).
  - **FD-47 (movement and control) is provisional.** It is revisited after the measurements listed in [Measurements that gate provisional parts](#measurements-that-gate-provisional-parts).
  - **FD-31 (text and UI) is approved for the early deliverable only.** Full approval waits for [UIDesign.md](UIDesign.md).
- **Implemented on Windows (2026-11-25), pending the owner's review.** The step order is in [Step order](#step-order); finished steps are recorded in [Implementation record](#implementation-record); the exit is in [M6C exit checklist](#m6c-exit-checklist), with its [results](#m6c-exit-checklist-results-2026-11-25).

This note records the M6C decisions FD-29 and FD-31 to FD-34 from [FutureDecisions.md](FutureDecisions.md#7-m6c--game-facing-services), plus three decisions the discussion added: the sample game (FD-46), movement and control (FD-47) and the determinism level (FD-48). Every "Today" statement was checked against the code at `5f4df75` on 2026-11-24 and 2026-11-25.

## Reviewer guide

### What each decision is about, in one line

| ID | Question | Decision | Status |
| --- | --- | --- | --- |
| [FD-29](#fd-29--input-mapping-contexts-and-bindings) | How actions map to input, and how engine keys and game keys stop colliding | Prioritized mapping contexts that consume input (engine developer, then editor, then game), one ordered event list for all devices, and a rebinding feature set | Decided |
| [FD-46](#fd-46--the-sample-game) | What the M6 "small 3D game" is | `Games/Arena`: a craft collecting energy cells while drones roam | Decided |
| [FD-32](#fd-32--collision-and-physics) | How much collision and physics v1 has | Jolt Physics inside the engine; game components stay the source of truth | Decided |
| [FD-47](#fd-47--movement-and-control) | How characters move, and how input reaches the simulation | An engine-owned input stream (adopted with amendments from another team's proposal), one `MoverComponent`, and a character backend on Jolt | **Provisional** |
| [FD-48](#fd-48--determinism-level) | How deterministic the simulation must be | Level 1 now (same build, same machine), with one tracked check and no replay feature; level 2 at the RTS milestone; level 3 later | Decided |
| [FD-31](#fd-31--text-and-ui) | Whether and how the engine draws text and UI | An engine-owned unified 2D and UI stack for game and editor; no Dear ImGui | **Early deliverable only** |
| [FD-33](#fd-33--save-data-and-user-files) | Where and how per-user data is written | OS per-user folders, field-table records, text settings, binary saves with a checksum, crash-safe writes, a host-owned profile store and a developer quick-save | Decided |
| [FD-34](#fd-34--smaller-services) | Fixed step, picking, global state, present queue | Render interpolation and a step-rate setting; Jolt picking everywhere; profile data in the host store and session data in the ECS; no present-queue switch | Decided |

### Direction for every decision

The owner's direction (2026-11-24): the engine must keep the **ability** to build an RTS, a real-time action game and other genres. An RTS sets the hardest requirements (many units, bulk movement, lockstep determinism, heavy UI), so no M6C choice may close those doors. RTS-specific systems are **not** built in M6C. The near-term game is the arena sample, which is close to the owner's own target.

Every choice must also keep the edit → build → observe loop fast. The physics iteration set in FD-32 and the developer quick-save in FD-33 exist for that reason.

---

## Goals and fixed constraints

- **M6 exit (roadmap):** a small 3D game, the stress sample and the 2D sample run using only engine APIs.
- **Joint ECS and hot-reload goal:** retained state stays plain values owned by the host. Nothing retained points into a game module, and no module callback is stored.
- **No throwaway work:** no step writes code that a later step or milestone replaces.
- **Minimal tests:** a tracked test only guards behavior that an upcoming change rewrites.
- **Minimum spec:** every budget is measured on the RTX 3070 Ti laptop, in Release.
- **Public API:** independent of OS headers. Third-party headers (Jolt, the audio device layer, the font tool) never appear in public headers. macOS and Linux follow through the platform interfaces.
- **Dependencies:** each new library is pinned to a tagged release, vendored with its license, built as a separate target with its own warning settings, and approved by the owner before it is added.

---

## FD-29 — Input mapping contexts and bindings

### Today (checked)

- Game code and the engine's developer keys (F3, F4, F5, F7, F8) both read physical keys from `InputState` directly. Nothing can consume a key, so a game binding on F5 also reloads the module, and a menu cannot take input away from gameplay.
- Keys and mouse buttons arrive in two separate lists, so "press any input to bind" cannot tell which came first within one frame.
- The fixed step forwards only keyboard state; pointer input and picks stay per-frame (`FixedGameplayStep.hpp:16`).
- Key values are stable USB HID codes, so saved bindings survive layouts and platforms.

### Decision

| Part | Decision |
| --- | --- |
| A. Layering | **Prioritized mapping contexts that consume input,** in order: engine developer, editor (later), game contexts (for example a pause menu above gameplay). The developer context does not exist in player builds. |
| B. Event order | **One ordered event list for all devices.** It changes `InputState`, a public header, so the host and every game module rebuild once. |
| C. Feature set now | An action map storing key or mouse-button values plus modifiers; capture of the next pressed input for key listening; layout-aware key names through a platform hook with a shared fallback. Bindings are saved through FD-33. |
| C. Later | Gamepad support and typed text (IME), when a caller needs them. |

The engine's developer keys move into the developer context: F3/F4 scene switch, F5 game reload, F7 render view, F8 play/pause, plus the FD-32 additions F9 (single step), F10 (collider view) and Shift+F8 (restart play). They become rebindable through the same map. F12 stays free because Visual Studio uses it.

---

## FD-46 — The sample game

`Games/Arena`: a small 3D arena game. The player steers a craft that collects energy cells while drones roam. It uses runtime spawning (cells and drones), a score during the run (session state), a best score saved to the profile (FD-33), a pause menu (a game input context that consumes gameplay input), a key-binding screen (key listening), and overlapping sounds (the audio mixer). The owner described it as "getting closer to what i really want in the future".

---

## FD-32 — Collision and physics

### Today (checked)

Deferred with no milestone. Courtyard has swept checks in the XZ plane; 3D has mesh picking only.

### Options considered

| Option | Strength | Cost |
| --- | --- | --- |
| Game-specific code | No engine work | Every game repeats it; no broad phase, sweeps or physics |
| Engine queries over component shapes | All state in components; no dependency | Queries only; real physics later would replace it |
| **Jolt Physics (chosen)** | MIT; C++17; CPU on Windows, Linux and macOS x64/ARM64; created at Guerrilla for Horizon Forbidden West and listed by the project as used in Death Stranding 2; bodies, sensors, queries, a character controller, constraints, optional cross-platform determinism | A large dependency; separate body state needs a sync path; callbacks on worker threads; several build defaults must be overridden |
| NVIDIA PhysX 5 | BSD-3; very mature | GPU path is CUDA only; official macOS support in 5.x unconfirmed; heavier build |

Also rejected: Bullet (ageing), Havok (commercial), Box2D (2D only).

### Decision: Jolt inside the engine, components as the source of truth

**Game-facing data.** `ColliderComponent` (shape, size, offset, sensor flag, layer) and `RigidBodyComponent` (motion type, mass, friction, restitution, linear and angular velocity). Games get ray, overlap and sweep queries and an ordered contact-event list through `SystemContext`.

**Engine side.** A host-owned `PhysicsWorld` in `Private/Physics/` is the only code that includes Jolt. It follows the `RenderWorld` pattern: built from components, kept in sync through change ticks and the lifecycle journal.

**Order inside each fixed step:**

1. Game fixed-step systems run and read the previous step's contact events.
2. Deferred spawns and despawns are applied.
3. Changes go into Jolt: new and removed bodies in one batch from the lifecycle journal (Jolt aborts when broad-phase nodes run out during one-by-one adds); transforms and velocities written by the game since the physics tick become teleports or velocity changes.
4. Jolt steps.
5. Positions and velocities are written back to components. The physics consumer then calls `AdvanceChangeTick()` and keeps that tick, so its own write-back is never read as a game edit, while `RenderWorld` still sees the new positions through its own tick.

**Rules:**

- A dynamic body must be a scene root; otherwise it is logged once and left out of the simulation (FD-06, recoverable per entity). Static and kinematic colliders may be children and follow their world transform; moving kinematic bodies go through `MoveKinematic`.
- The world is Z-up: gravity (0, 0, −9.81) and the character up axis +Z. Height fields, when added, are wrapped in a rotation.
- Shape sizes are multiplied by world scale and checked with Jolt's scale validation; an invalid scale is logged and the entity skipped. A changed size or scale rebuilds only that entity's shape.
- Transform rotation stays XYZ Euler. Write-back picks the Euler angles closest to the previous value, so stored angles do not jump.
- Velocities live in components, so saves, scene files and field migration carry them. Game DLL replacement never touches the physics world; a scene reload rebuilds it in one batch.

**Jolt pitfalls and how the engine contains them:**

| Pitfall | Containment |
| --- | --- |
| No exceptions: some failures call `std::abort`; Debug asserts | Every value is validated before it reaches Jolt (finite numbers, positive sizes, valid layers and scale). Jolt's assert and trace hooks go to the engine log. Its startup check catches build-setting mismatches. |
| Body, body-pair and contact limits are fixed at startup; overflow fails silently | Limits come from project settings; the body limit is checked before each add; Jolt's step error flags are logged as warnings; the temporary allocator falls back to the heap. |
| Kinematic bodies get no contacts with static or kinematic bodies by default | The engine sets the flag where it is needed. Player-like movers use the character backend (FD-47), not plain kinematic bodies. |
| Static sensors lose sleeping bodies | Sensors are created as kinematic bodies internally; the game only sets the sensor flag. |
| Contact callbacks run on worker threads and may only read bodies | Contacts are copied into a buffer, sorted by persistent entity pair after the step and published as one ordered list. The engine emits End events itself for removed entities. |
| Fast bodies can pass through thin walls | Continuous collision is opt-in per body, for example for projectiles. |

**Build settings** (engine-private target): static MSVC runtime OFF, all-warnings-as-errors OFF for Jolt's own code, `OVERRIDE_CXX_FLAGS` OFF, RTTI ON (a mismatch fails to link), profiler OFF, debug renderer ON in Debug and Release. Link-time optimization is decided after measuring. Jolt compiles itself with `/fp:fast` on MSVC; that is harmless at determinism level 1 (FD-48) and revisited at level 3. The source is pinned to Jolt 5.7.0 in `ThirdParty/JoltPhysics/`, with its license, so builds work offline; player builds ship the license text. That copy is never edited (owner, 2026-11-25): every setting lives in `cmake/RavenJolt.cmake`, and `scripts/UpdateJolt.cmake` replaces the copy for a new release. See the [vendored copy's README](../ThirdParty/JoltPhysics/README.md) for the update steps.

### Physics iteration set (approved)

| Loop | Feature |
| --- | --- |
| Build | Jolt is a separate library rebuilt only when its source changes; it never enters the game build or the game contract. **Budget:** a game-code edit stays within the M5 targets (Release ≤ 2.0 s save-to-active; M6A measured 1.68–1.72 s). |
| Build | Jolt is compiled with optimization even in `x64-debug`, asserts kept, with the same runtime library and iterator settings as the engine. Confirmed on the laptop in C4: 5.5–6× faster steps than unoptimized Jolt (measurement 2). |
| Tune | Physics values are component fields; scene edits reload through the last-good path, and only the changed shape is rebuilt. |
| Tune | The physics world survives game DLL replacement, so code changes apply mid-motion. |
| Tune | An invalid collider is logged once with the entity name and field, and that entity is skipped. |
| Debug | Collider view (F10): shapes, sensors, contacts and sleeping bodies through the debug-draw path. |
| Debug | Single fixed step (F9 while paused with F8): game systems, then physics. |
| Debug | Restart play (Shift+F8): reset the scene to its file state without restarting the process. |
| Debug | A physics stage time and body and contact counts in the frame metrics and trace. |

---

## FD-47 — Movement and control

**Status: provisional.** The control layer was proposed by another team; the owner took it as a strong suggestion and adopted it with the amendments below. The whole decision is revisited after the measurements.

### Why movement and control are hard to get right once

- An action game moves tens of characters precisely; an RTS moves hundreds to thousands of units in bulk. Jolt's `CharacterVirtual` suits the first (several shape casts per character, one character at a time) and not the second.
- In an action game, input steers one entity every frame. In an RTS, the player selects units and gives orders that each unit carries out over time. Both end by writing "move this way" on a mover.

### Decision

**Three layers, split by lifetime:**

1. **Control** (per frame, local, game code): camera rig, cursor, aim, selection, hover and UI. It never writes simulation state.
2. **Input stream** (engine-owned, one lane per player, stamped with the fixed tick): the only door into the simulation.
3. **Simulation** (fixed step, game code plus the engine): reads the lanes, keeps gameplay input buffers (combo windows, jump grace) as components, runs AI, and writes `MoverComponent`.

**Movement contract.** `MoverComponent`: the game writes the desired velocity, maximum speed and capsule size; the engine writes back position, velocity, ground state and ground normal. Two backends behind it:

- **Character backend (M6C):** Jolt `CharacterVirtual`, with an inner body so sensors and queries find characters. Wall sliding, slopes, steps, riding moving ground, pushing dynamic bodies and character-to-character collision come from Jolt. Characters must be scene roots; a game transform write is a teleport.
- **Crowd backend (later, with navigation):** data-oriented, parallel and deterministic; paths, separation, avoidance, a spatial grid for range queries and terrain height. A game switches a unit between backends with one setting.

**Amendments to the input-stream proposal:**

| # | Amendment | Reason |
| --- | --- | --- |
| 1 | A per-frame **control phase runs before the fixed steps** | Controllers in `Update` would run after the steps, adding up to a full frame of delay (33 ms at 30 FPS) |
| 2 | **Only player input** enters the stream and would be recorded or sent; AI acts inside the fixed step | Recorded AI commands would be applied twice on replay and sent needlessly under lockstep |
| 3 | A **default command**: a per-tick snapshot of the input actions (FD-29); custom records (for example RTS orders) are declared like components | Action games need no custom command types; one line of game code instead of a type plus a system |
| 4 | Records name entities only by `EntityRef` | `Entity` handles mean nothing on another machine or in a later run |
| 5 | On game reload, waiting records whose layout changed are dropped (at most one tick of input) | The stream is host-owned, but its record types are game-defined |
| 6 | A name that does not clash with `SystemContext.Commands` (deferred spawn and despawn, FD-14); working name *input stream* | Two different "command buffers" would confuse readers |
| 7 | Determinism is stated as a separate requirement (FD-48) | Commands alone do not make replay or lockstep work |

**Not built in M6C:** engine controller components, possession and player slots (added when local multiplayer, networking or an editor "possess" needs them); the RTS command layer (box selection, per-unit orders); the crowd backend; input delay for lockstep; recording.

---

## FD-48 — Determinism level

| Level | Guarantee | Status |
| --- | --- | --- |
| 1 | Same build, same machine: the same scene, seed and input stream give the same simulation | **M6C** |
| 2 | Same build, different PCs (Windows x64): lockstep between players | Target of the RTS milestone |
| 3 | Different platforms and compilers | Decided later |

**What M6C builds for level 1:**

1. A seeded simulation RNG in `SystemContext`. Fixed-step code must not read the wall clock or `std::random_device`.
2. Entities spawned by the simulation take persistent IDs from the simulation seed; scene-loaded entities keep their file IDs. (Contacts and other sorted lists key on these IDs.)
3. Simulation code uses a small engine math module for trigonometry and square roots. It wraps the standard functions today, so bit-exact versions can replace them later without game changes.
4. One tracked test loads the arena scene twice, feeds both runs the same scripted input lane for about 600 ticks, and compares a hash of the simulation components; on a mismatch it reports the first diverging tick. It guards behavior that the crowd milestone rewrites. If it cannot run in CI because it needs the GPU, it runs in the Windows smoke script.

**No replay feature.** Recording to files and playback are not built; they can be added later on the same input stream.

Already in place: fixed steps, sorted contacts, a fixed body-add order, and only player input entering the simulation. Jolt is deterministic on the same binary when it receives the same calls in the same order.

---

## FD-31 — Text and UI

**Status: approved for the early deliverable only.** The full stack is planned in [UIDesign.md](UIDesign.md), which the owner reviews before any UI work beyond the early deliverable.

### Today (checked)

The engine cannot draw text. Reload diagnostics stay in the log. Sprites are CPU-gathered textured draws with per-texture descriptors (`GameSession::GatherSprites`, `VulkanTexturedDraw`); they move to the GPU path in M6B (FD-20).

### Decision

An **engine-owned unified 2D and UI stack** serving both the game and the future editor, in the spirit of Unreal's Slate. Dear ImGui is not used: the owner's earlier experience was that it needed too many modifications. Unlike Slate's retained widgets with bound callbacks, the API is **immediate-mode over engine-owned state keyed by widget ID**, with clicks returned as values instead of callbacks, so nothing points into the game DLL between frames.

| Layer | Contents | When |
| --- | --- | --- |
| 0. Shared GPU 2D path | Batched quads, images, MSDF text, clipping, draw layers | M6C, designed with M6B sprites (one quad format, two feeds) |
| 1. UI core | ID-keyed state, layout (anchors, stacks, grids), input consumed through the UI mapping context, focus and navigation, DPI scaling | M6C, thin cut |
| 2. Game widgets | Text, image, button, panel, toggle | M6C, thin cut |
| 3. Tool widgets | Number drag fields, text fields, trees, tables, docking | Later, starting with the component inspector |

**Early deliverable (M6C):** layer 0 and the thin cut of layers 1 and 2: anchored layout, text, image, button and panel, input consumption through the UI context, and keyboard focus. It covers the arena's score, pause menu and key-binding screen, plus on-screen reload diagnostics. Fonts are MSDF atlases baked by a content tool (msdf-atlas-gen, MIT), hot-reloaded like PNG files. Even the early deliverable follows UIDesign.md sections 1, 2 and 4, which are written first.

---

## FD-33 — Save data and user files

### Today (checked)

No code looks up a per-user folder. The player build assumes its install folder is writable, which fails under Program Files. `CaptureScene` and `InstantiateScene` already copy a live world to and from the scene format, and transient and host-owned components are already marked.

### Decision

| Part | Decision |
| --- | --- |
| A. Location | The OS per-user folder through a platform interface: Windows `%LOCALAPPDATA%\<Studio>\<Game>`, macOS `~/Library/Application Support/<Studio>/<Game>`, Linux `$XDG_DATA_HOME/<Studio>/<Game>`. Development runs use `out/User/<Game>/`, so tests never touch a player profile. |
| B. Data model | Saved records are plain structs declared like components and described by the field tables. Renames migrate through the aliases; new fields get defaults. |
| B. Encoding | A pipeline of independent stages: record → encoding → optional compression → integrity → optional encryption → crash-safe write. Settings and bindings use the text form, so people can read and fix them. Progress and world saves use the binary scene codec plus a checksum, readable through `RavenSceneTool dump`. Compression, signing (HMAC) and encryption are later optional stages; client-side signing and encryption only deter casual edits, because the key ships with the game. |
| C. Safety | Write to a temporary file, flush, then atomically replace; keep one `.bak`. A corrupt or unreadable file is logged, renamed aside, and replaced by defaults (FD-06, recoverable). Small files are written synchronously. |
| D. World saves | Designed now, built when a game needs mid-session saves. A **developer quick-save** is built in M6C: one key snapshots the live world (plus the simulation RNG state), another restores it, reusing `CaptureScene`/`InstantiateScene` and the physics rebuild. |

**Profile store.** Data that outlives a play session (stage reached, character level, unlocks, best score, settings, bindings) lives in a host-owned user-data store, outside the ECS and outside the game DLL: `Services.UserData.Get<T>()` and `Set(...)`. Values survive DLL reload and migrate through the field tables. Changes are marked dirty and written at explicit save points and at shutdown, never mid-frame. When a stage starts, the game copies what it needs into the ECS; when it ends, it writes results back.

---

## FD-34 — Smaller services

| Topic | Today (checked) | Decision |
| --- | --- | --- |
| Fixed step | 60 Hz; at most 4 steps per frame; extra time dropped silently | **Render interpolation in M6C:** each moving entity keeps its previous transform and rendering blends by the step remainder. It adds up to one step (16.7 ms) of display latency and leaves the simulation untouched. The step rate becomes a **project setting** (default 60 Hz, fixed for a session). Dropped time is logged. |
| Picking | `Pick` loops over every entity, composes each world transform live and tests the mesh | **Jolt picking everywhere,** game and editor alike. Every renderable gets a query-only body in a pick layer that is never simulated. Shapes are exact mesh shapes shared per mesh asset; lights and cameras get small editor shapes. Pick bodies sync only when a query runs. Replaces `ScenePicking.cpp`. |
| Global game state | Nothing dedicated | **Profile data** in the host-owned user-data store (FD-33). **Session data** (score in this run, wave, timers) in the ECS: a component on one entity read with `Single<T>`. |
| Separate present queue | The engine already handles split graphics and present families (`VulkanDevice.cpp`, concurrent swapchain sharing at `VulkanSwapchain.cpp:159`); no tested device uses that path | **Skipped.** No developer switch; the TODO item stays open until hardware needs it. |

---

## Measurements that gate provisional parts

Each is measured on the laptop, in Release unless stated, and recorded in this note.

| # | Measurement | Gates |
| --- | --- | --- |
| 1 | Jolt's effect on a clean engine build, an incremental engine build, CI time and binary size | FD-32 build settings (link-time optimization) |
| 2 | Debug-build physics speed with Jolt optimized inside `x64-debug` | FD-32 iteration set |
| 3 | Character backend cost per character with the arena drones, and at 100 and 500 characters | FD-47 |
| 4 | Pick query time at 50k renderables, and pick-layer shape build time per mesh | FD-34 picking |
| 5 | Game-code edit save-to-active time with Jolt and the UI stack present | M5 targets. *Measured 2026-11-25: Arena 1.47–1.50 s Debug and 1.40 s Release (medians), after the game PCH change and the Arena split ([C10](#c10--determinism-check-and-the-exit-check-2026-11-25)).* |
| 6 | 50k animated steady-state CPU p95 compared with the M6A record (3.24–3.42 ms) | No regression from entities without physics or UI. *Measured 2026-11-25: 3.26–3.52 ms, inside a same-day M6A A/B (3.22–3.78 ms) ([C10](#c10--determinism-check-and-the-exit-check-2026-11-25)).* |
| 7 | HUD and pause-menu CPU time per frame | FD-31, UIDesign.md budgets. *Measured 2026-11-25: 0.117 and 0.167 ms p95 ([C7](#c7--the-2d-and-ui-early-deliverable-2026-11-25)).* |

---

## Step order

Each step builds on the previous ones and leaves nothing to replace. Steps that add a dependency start with the owner's approval of the exact source and release.

| Step | Content | Uses |
| --- | --- | --- |
| C1 | **Input contract:** one ordered event list (the `InputState` break), mapping contexts with consumption, the developer context with the existing F-keys, the action map, key listening and layout-aware names (Windows plus the shared fallback) | FD-29 |
| C2 | **User data:** the platform user-data path (Windows; macOS/Linux through the interface), `out/User` in development, field-table records in text and binary forms, checksum, crash-safe writes with `.bak`, corrupt-file recovery, the host-owned profile store; bindings saved | FD-33 |
| C3 | **Fixed-step services:** step-rate setting, dropped-time log, the control phase before the fixed steps, the input stream with the default action snapshot and custom records, the simulation RNG, deterministic spawn IDs, the math module, render interpolation | FD-34, FD-47, FD-48 |
| C4 | **Physics:** vendor Jolt (measurement 1), `PhysicsWorld`, `ColliderComponent` and `RigidBodyComponent`, sync and write-back, ordered contact events, queries in `SystemContext`, validation and limits, the iteration set (F9, F10, Shift+F8, metrics) | FD-32 |
| C5 | **Picking on Jolt:** the pick layer, shared mesh shapes, on-demand sync; `ScenePicking.cpp` replaced | FD-34 |
| C6 | **Movement:** `MoverComponent` and the character backend (measurement 3) | FD-47 |
| C7 | **2D and UI early deliverable:** UIDesign.md sections 1, 2 and 4 reviewed; the shared GPU 2D path; the MSDF font tool and font assets; the thin UI core and game widgets; on-screen reload diagnostics | FD-31 |
| C8 | **Audio voices:** the engine mixer over one vendored device layer (planning decision 6, provisional; license, size and Linux backends checked first) | Planning decision 6 |
| C9 | **Arena game** on the above, plus the developer quick-save | FD-46, FD-33 |
| C10 | **Determinism check** (the tracked test) and the **exit check** with every measurement | FD-48 |

C1 to C3 change the public contract and come first, so the game contract breaks once early instead of several times. C7 can run in parallel with C4 to C6 once its UIDesign.md sections are approved.

---

## M6C exit checklist

1. The arena game runs using only engine APIs: the craft through the input stream and the character backend, drones through AI writing their movers, energy cells as sensors, runtime spawning, the score as session state, the best score in the profile store, a pause menu that consumes gameplay input, a key-binding screen with key listening and saved bindings, and overlapping sounds.
2. The 3D samples keep working; Courtyard keeps building (its 2D run needs an owner request).
3. Developer keys live in the developer context; a game binding on F5 no longer reloads the module.
4. The determinism check passes; the developer quick-save restores a snapshot and play continues.
5. Bindings and the best score survive a restart; a corrupted settings file falls back to defaults with one log line.
6. Every measurement in the table above is recorded; a game-code edit stays within the M5 targets; the 50k animated steady state shows no regression beyond measurement noise.
7. Jolt, the font tool and the audio device layer are vendored with their licenses, outside the public headers, and CI passes with warnings as errors.

---

## Implementation record

### C1 — Input contract (2026-11-25)

**Built:** the ordered event list, mapping contexts with consumption, the `Engine.Developer` context, the action map with axes, key listening, rebinding that survives module reloads, and layout-aware key names (Windows; shared English fallback elsewhere). Details: [InputProcessing.md](InputProcessing.md#input-mapping-contexts-bindings-and-key-listening-m6c-step-c1-2026-11-25).

**Differences from the plan:**
- `SystemContext::Input` stays, but now shows raw input *minus* everything a context consumed. Existing games that read physical keys keep working, and still cannot see the developer keys. New games should use `SystemContext::Actions`.
- Fixed steps now also accumulate mouse-button transitions; before C1 they saw keyboard input only.
- `GameSession` receives developer requests as plain flags, not the router, so the smoke test can keep compiling the session on its own.
- Saving bindings waits for C2 (user data), as planned.

**Checked:**
- Builds: Debug, and the CI-equivalent Release build with warnings as errors, both pass.
- Tests: all four CPU tests pass, including the new `InputMappingTests` guard.
- Smoke: the Debug Windows smoke passes.
- Live: the Debug live default-launch check passes. Real F7 presses cycled all four render views through the developer context, and the reload, rejection and recovery cases passed.

**Not checked:** a non-US keyboard layout for key names, and macOS and Linux, which have no native key names yet.

### C2 — User data and the profile store (2026-11-25)

**Built:**
- **Store.** `UserDataStore` (`Raven/Core/UserData.hpp`) is reached through `GameServices::GetUserData()` and implemented by the host's `UserDataService`. It keeps records by file and key, in two encodings: binary with a checksum, and text.
- **Migration.** Fields migrate on read through the scene codec's field matching, which C2 factored out of `InstantiateScene` into `MatchesSchema`, `CaptureRecord` and `ApplyRecord`.
- **Safety.** Saves keep a `.bak`, damaged files are set aside, and loading recovers from the backup.
- **Folders.** Per-platform user-data folders (`Paths::GetUserDataDirectory()`); development uses `out/User/<Game>`.
- **Bindings.** Saved bindings live in `Bindings.ravendata.txt`.
- **Tooling.** `RavenSceneTool dump` reads binary user-data files.

Details: [UserData.md](UserData.md).

**Differences from the plan:**
- Records are kept as stored rows and matched on every read, instead of being converted once when a file loads. A module reload that changes a record type therefore needs no extra step.
- Keys were added, so one file can hold a record per level or slot. Bindings use them: one labeled record per slot, which keeps the text file readable.
- The studio folder name is `RavenEngine` until the project file names one.

**Checked:**
- Builds: Debug, and the CI-equivalent Release build with warnings as errors, both pass.
- Tests: all five CPU tests pass, including the new `UserDataTests` guard.
- Smoke and live: the Debug Windows smoke and the live default-launch check pass, and the live host logged its user-data folder under the snapshot's `out/User/Samples`.
- Tooling: `RavenSceneTool dump` printed a wrapped binary file and rejected one with a flipped byte.

**Not checked:**
- A player build writing to `%LOCALAPPDATA%`.
- The macOS and Linux folder lookups, which have not been compiled.
- Bindings saved from a real rebinding screen, which comes with the arena game (C9). The guard test covers the save and load path.

### C3 — Fixed-step services, input stream and determinism level 1 (2026-11-25)

**Built:**
- **Step rate.** A project setting, `fixed-step <Hz>` (10–480, default 60), fixed for the session ([SceneWorkflow.md](SceneWorkflow.md#project-data)).
- **Catch-up limit.** A frame catches up at most 4/60 s of game time (or one step, if a step is longer). Dropped time is counted and logged at most once a second.
- **Control phase.** `SystemPhase::Control` runs once per frame, before the fixed steps.
- **Input stream** (`Raven/Input/InputStream.hpp`, `SystemContext::Stream`):
  - Controllers `Emit` component-typed records, and fixed steps read their tick's records with `ForEach`.
  - A record emitted before the steps reaches that frame's first step; one emitted later waits for the next tick.
  - Records that hold an `Entity` field are rejected.
  - Waiting records are dropped when the module is replaced.
  - The per-tick action snapshot is the stream's default record (`SystemContext::Actions` in a fixed step).
- **Determinism level 1.**
  - `SystemContext::Random` (`SimulationRandom`, SplitMix64).
  - `Scene::SeedIds`, so runtime spawns take IDs from the simulation seed. The seed comes from `--seed <n>`, or from the OS once per session, and is logged.
  - `Raven/Math/SimulationMath.hpp` for simulation math.
- **Render interpolation.**
  - `InterpolateComponent` tags entities; `TransformInterpolator` keeps each tagged entity's transform before and after the latest step.
  - While a frame is drawn, the blended transform goes through the one world pass, the render world and the sprites. The latest tick is restored afterwards.
  - Rotation blends through quaternions (`QuatFromEuler`, `Nlerp`, `EulerFromQuat`), and `EulerFromQuat` keeps stored angles continuous (closest to a reference). Physics write-back will reuse these.

**Differences from the plan:**
- **Interpolation is opt-in** through the tag, not automatic for every entity moved in a fixed step. Scenes that animate many entities in `Update` (the 50k benchmark) pay nothing. Physics bodies and characters will get the tag automatically in C4 and C6.
- **Restoring rewrites** the latest tick, which stamps change ticks again. Physics (C4) must therefore compare a transform with its own last write-back before treating it as a game teleport.
- **Children follow.** Because blending happens before the world pass, children of an interpolated entity follow its blended transform.
- **Picking matches what is drawn.** Picking runs during drawing, so it uses the blended transforms.
- **Restore costs** one extra incremental world pass, and only on frames that blended something.

**Checked:**
- Builds: Debug, and the CI-equivalent Release build with warnings as errors, both pass.
- Tests: all six CPU tests pass, including the new `FixedStepTests` guard. It covers tick delivery of records, a step-time emit going to the next tick, dropped records, action accumulation, catch-up and dropped time, rate bounds, Euler round trip and continuity, interpolation (blend, restore, teleport), the seeded RNG and seeded IDs.
- Smoke and live: the Debug Windows smoke and the live default-launch check pass.

**Not checked:** interpolation on screen, because no sample tags an entity yet. The arena game (C9) exercises it.

### C4 — Jolt physics and the iteration set (2026-11-25)

**Built** (usage in [Physics.md](Physics.md)):
- **Dependency.** Jolt 5.7.0 is vendored unmodified in `ThirdParty/JoltPhysics/`, approved by the owner on 2026-11-25. Every setting lives in `cmake/RavenJolt.cmake`, and `scripts/UpdateJolt.cmake` replaces the copy for a new release ([README](../ThirdParty/JoltPhysics/README.md)).
  - Jolt is a static library linked privately into RavenCore, and its headers are system includes.
  - Configure fails if the copy and the version pin disagree.
  - The license is staged as `Licenses/JoltPhysics-LICENSE.txt`.
- **Components.** `ColliderComponent` and `RigidBodyComponent` are engine components. `SystemContext::Physics` offers ray, shape-cast and overlap queries and the ordered contact events.
- **`PhysicsWorld`** (`Private/Physics/`) is host-owned in `Application`.
  - Per fixed step: the game's systems, their commands, then sync, simulate and write back.
  - Sync uses the lifecycle journal and change ticks; write-back closes its own change tick.
  - New bodies are created in persistent-ID order and added in batches.
  - Invalid colliders are logged once and left out.
  - Body, pair and contact limits come from a new `physics-limits` project record.
- **Iteration set.**
  - F10 collider view: a new debug-line path in the renderer. `DebugLine` shaders are compiled and reloaded with the other engine programs, and the line list is drawn last, depth-tested.
  - F9 runs a single fixed step while paused; Shift+F8 restarts play.
  - The frame trace gains `physics_ns`, `physics_bodies`, `physics_active` and `physics_contacts`.
- **Interpolation.** Dynamic and kinematic bodies are interpolated without the tag.
- **Sample.** The Samples game's Physics scene (`SamplesTool create-physics`).

**Differences from the plan:**
- **Sleeping keeps contacts.** Jolt reports a contact as removed when a body falls asleep. The engine keeps such pairs touching until both bodies are awake and their bounds (plus 5 cm) have separated, so games see no false End/Begin pairs.
- **Our own scale validation.** Scale is checked by the engine instead of Jolt's scale validation, and sizes are scaled into the shape directly (no `ScaledShape`). Spheres need a uniform scale, and capsules equal X and Y scale.
- **Kinematic children** follow `WorldTransformComponent`, which within the fixed steps is one frame behind. Roots use their transform directly.
- **Moving a static collider** does not wake bodies resting on it.
- **Contact events carry Sensor, Point and Normal on Begin only.**
- **Collision layers don't filter each other yet:** layers filter queries only, and every layer collides with every other.
- **Jolt's ObjectStream (its own serialization) is off.** It saves 90 KB in Release, and Raven stores physics in components.
- **Instruction set.** Jolt's MSVC instruction-set selection runs only under Visual Studio generators, so Ninja builds use the x64 SSE2 baseline for Jolt and RavenCore alike. The settings match, and players need no AVX2. Raising the baseline is a measured decision for later.
- **`RAVEN_JOLT_OPTIMIZE_DEBUG`** (ON) lets a developer step through Jolt's own code.

**Measurements** (the laptop, i7-12700H, 20 hardware threads):

| # | What | Result |
| --- | --- | --- |
| 1 | Clean build of the Jolt library | 20 s in Release and 21 s in Debug with optimization (5.5 s unoptimized), on 20 threads. Engine edits never rebuild it. |
| 1 | Incremental engine build | Touching `PhysicsWorld.cpp` (the only file that includes Jolt) rebuilds RavenCore in 3.6 s, against 2.3 s for `Scene.cpp`. Game-code builds are unaffected: Jolt is not part of the game contract or build. |
| 1 | CI-equivalent build from scratch | 41 s for configure, build, smoke target and CPU tests. Jolt's 20 s compile overlaps the engine's on 20 threads. No pre-Jolt baseline was recorded on this machine; GitHub CI times come from the next push. |
| 1 | Binary size | `RavenCore.dll` (Release) grows from 1,693,184 to 2,973,696 bytes. `Jolt.lib` is 21 MB and does not ship. |
| 1 | Link-time optimization | Not turned on: a release physics step is already cheap (below), and link-time optimization would slow every RavenCore link. Revisit if physics shows up in the CPU profile. |
| 2 | Physics step, Release (`PhysicsBenchmark`) | Mean and p95 per 60 Hz step, piles of boxes and spheres settling: 125 bodies 0.03 / 0.11 ms, 1,000 bodies 1.0 / 1.6 ms, 4,096 bodies 6.1 / 9.9 ms. |
| 2 | Physics step, `x64-debug` with optimized Jolt | 125 bodies 0.13 / 0.46 ms, 1,000 bodies 3.4 / 6.1 ms, 4,096 bodies 14.1 / 19.5 ms. |
| 2 | Physics step, `x64-debug` with unoptimized Jolt | 125 bodies 0.27 / 1.16 ms, 1,000 bodies 18.7 / 32.6 ms, 4,096 bodies 86.4 / 125.0 ms. Optimizing Jolt in Debug is kept. |
| — | The Physics sample, live in `x64-debug` | 27 bodies: physics per frame p50 0.20 ms, p95 0.57 ms. |

**Checked:**
- **Builds:** Debug, and the CI-equivalent Release build with warnings as errors, both pass.
- **Tests:** all seven CPU tests pass, including the new `PhysicsTests` guard. It covers:
  - falling and resting under Z-up gravity;
  - a restore-style rewrite that is not a teleport, a transform teleport and a velocity edit;
  - sleeping that keeps the contact;
  - a despawn ending its contact;
  - the ray, overlap and shape-cast queries, with masks and sensors;
  - a static sensor seeing a static body;
  - rejection of an invalid size and of a dynamic child, and both rejoining after a fix;
  - a rebuild after `Clear`.
- **Smoke and live:** the Debug Windows smoke and the live default-launch check pass.
- **Physics sample, live with validation:** run with the Vulkan validation and synchronization layers, with no diagnostics. Captures show the falling spheres, the settled pyramid and the F10 collider view (sleeping crates red, moving spheres yellow, edges drawn cleanly). The log confirms F9 single steps while paused and Shift+F8 reloading the scene and resuming play.

**Not checked:**
- the smoothness of interpolated bodies, measured frame by frame;
- kinematic bodies moved by game code in a running scene (covered only by the test's kinematic child);
- macOS and Linux;
- the 50k steady-state regression check, which is part of the exit check (measurement 6).

### C5 — Picking on Jolt (2026-11-25)

**Built** (usage in [Physics.md](Physics.md#picking)):
- **`PickWorld`** (`Private/Physics/PickWorld.*`) is host-owned next to the physics world.
  - Every entity with a static mesh and a world transform has a static, query-only body.
  - Each mesh asset has one exact triangle shape, wrapped per entity for a non-unit or mirrored scale.
  - Bodies are synced only when a pick runs, through the lifecycle journal and change ticks, from the world transforms of the frame being drawn.
  - A reloaded mesh rebuilds its shared shape.
- **Replaced:** `GameSession::Pick` replaces the free `Pick` function. `ScenePicking.cpp` and the renderer's `IntersectMesh` are removed, and `ScenePicking.hpp` keeps only `PickHit`.
- **Shared Jolt support:** `Private/Physics/JoltSupport.*` holds the Jolt runtime, conversions and transform decomposition for both worlds.

**Differences from the plan:**
- **A separate Jolt system instead of a pick layer.** Pick bodies live in a Jolt system of their own, which is never stepped. Renderables therefore never use the simulation's body limit, never appear in physics queries, and can grow past 65,536 (the system is rebuilt larger when needed). The simulation stays exactly as C4 left it.
- **No editor shapes yet:** lights and cameras get pick shapes with the first editor that picks them. Game picking never wants them: the camera's own shape would block every click.
- **Shear.** A rotated child of a non-uniformly scaled parent is picked with its axes orthogonalized; the old CPU test used the full matrix.

**Measurements** (measurement 4, `PhysicsBenchmark`, the laptop):
- **Scene:** 50,000 renderables in the 50k benchmark layout, alternating cube (12 triangles) and sphere (224 triangles), with one in five moving between picks.

| Case | Release sync / query | `x64-debug` sync / query |
| --- | --- | --- |
| First pick (builds every body) | 32.1 / 0.03 ms | 225 / 0.05 ms |
| Unchanged scene | 0.39 / 0.004 ms | 1.8 / 0.02 ms |
| After 10,000 entities moved | 2.4–2.6 / 0.004 ms | 19–20 / 0.02 ms |
| After a mesh reload (4,096-triangle sphere, 25,000 users) | 7.6 / 0.005 ms | 81.5 / 0.02 ms |
| Shape build per mesh | cube and sphere together 0.14 ms; 4,096 triangles 1.96 ms | 0.31 ms; 4.0 ms |

The previous loop tested every entity's mesh on each click.

An earlier version rebuilt the broad-phase tree after many moves and allocated a scale wrapper per moved entity, which cost 8.5–9.3 ms after 10,000 moves. Both now happen only when needed. Moved static bodies only widen their tree nodes, and queries stay correct and fast.

**Checked:**
- **Builds:** Debug, and the CI-equivalent Release build with warnings as errors, both pass.
- **Tests:** all seven CPU tests pass. `PhysicsTests` gained a picking check covering:
  - the nearest hit;
  - a mirrored, scaled mesh;
  - both faces;
  - the distance limit;
  - a moved entity;
  - a mesh reload;
  - a despawn;
  - growth past the initial capacity.
- **Smoke:** the Windows smoke picks pickups through `GameSession::Pick`.
- **Live:** the live check clicks the Collection pickups with the mouse, and the log shows each pick's sync and query time (for example 0.66 ms and 0.03 ms in Debug). The Physics sample live probe still passes.

**Not checked:**
- clicking in the 50k scene live; the benchmark measures the same code;
- macOS and Linux.

### C6 — Movement and the character backend (2026-11-25)

**Built** (usage in [Physics.md](Physics.md#movers)):
- **`MoverComponent`:**
  - The game writes desired velocity, maximum speed, gravity scale, capsule, maximum slope and layer.
  - The engine writes position, velocity, ground state and ground normal.
  - It is an engine component, and movers are interpolated automatically.
- **Character backend** (`Private/Physics/CharacterMovers.*`, owned by the physics world):
  - One Jolt `CharacterVirtual` per mover, Z-up, with an inner kinematic body on the mover's layer so queries, sensors and contact events see it.
  - Per fixed step: the game's systems and commands, the body sync, the mover sync, the characters' update in persistent-ID order, the Jolt step, then write-back.
  - Edits follow the physics rules: a transform write teleports, a `Velocity` write sets momentum, and a changed capsule reshapes in place.
  - Invalid or conflicting movers are logged once and left out.
- **Character-vs-character collision** through Raven's grid of nearby characters, in place of Jolt's all-pairs checker (below).
- **Shared code:** the object-layer helpers moved to `JoltSupport.hpp`. A collider on a mover entity is rejected on both sides.
- **Sample:** three walkers in the Physics scene, steered by a fixed-step `Patrol` system in the Samples game (`PatrolComponent`).

**Differences from the plan:**
- **Character-vs-character collision is Raven's own** (`CharacterGrid`), implementing Jolt's `CharacterVsCharacterCollision` interface without touching Jolt.
  - Jolt's simple checker tests every pair, and measured 22 ms a step at 500 movers.
  - The grid does the same per-pair work for characters in nearby XY cells. It is built once per step with a 1 m margin and visited in a fixed order (FD-48).
- **Jumping** is a positive `DesiredVelocity.Z` while on walkable ground, rather than a separate field. Without gravity, Z steers.
- **Contacts:** a mover beside a wall reports `NotSupported`, which is Jolt's state for touching something that does not hold it up. Mover-versus-static contacts produce no contact events, because the inner body is kinematic; ground contact is in `Ground`.

**Measurements** (measurement 3, `PhysicsBenchmark`, the laptop):
- **Scene:** movers walking crossing circles among 16 pillars and 32 crates.
- **Arena drones:** this part waits for the arena game (C9).

| Movers | Release mean / p95 per step | Per mover | `x64-debug` mean / p95 |
| --- | --- | --- | --- |
| 10 | 0.12 / 0.16 ms | 11.5 µs | 0.52 / 0.67 ms |
| 100 | 0.70 / 0.94 ms | 7.0 µs | 7.7 / 10.3 ms |
| 500 | 3.1 / 3.5 ms | 6.2 µs | 45.3 / 50.0 ms |

- **Grid versus Jolt's checker:** with Jolt's all-pairs checker, Release cost 1.24 ms at 100 movers and 22.1 ms at 500.
- **Without character-vs-character collision:** a probe that turned it off (then reverted) measured 0.35 ms at 100 and 1.5 ms at 500.

For FD-47 this means:
- The character backend covers action games with room to spare, and handles 500 movers in a fifth of a 60 Hz frame.
- An RTS with thousands of units still needs the crowd backend.
- FD-47 stays provisional until the arena drones are measured in C9.

**Checked:**
- **Builds:** Debug, and the CI-equivalent Release build with warnings as errors, both pass.
- **Tests:** all seven CPU tests pass. `PhysicsTests` gained a mover check covering:
  - falling and standing at radius plus half height, with the ground normal;
  - a ray query hitting the mover;
  - a sensor reporting it;
  - walking at the speed limit;
  - stopping at a wall;
  - jumping;
  - a transform teleport;
  - rejection with a collider and rejoining;
  - two movers walking head-on stopping apart.
- **Smoke and live:** the Debug Windows smoke and the live default-launch check pass.
- **Physics sample, live with validation:** run with the Vulkan validation and synchronization layers, with no diagnostics. Captures show the walkers moving between frames and drawn green in the collider view; the scene has 30 bodies.

**Not checked:**
- riding a moving kinematic platform, and pushing crates, observed on screen;
- the arena drones (C9);
- macOS and Linux.

### C7 — the 2D and UI early deliverable (2026-11-25)

**Scope.** The owner ordered "implement till step d" on 2026-11-25:
- a, the job-system study;
- b, C7;
- c, sound-event assets;
- d, C9's second part.

The library decisions are U1–U5 ([UIAudioLibraries.md](UIAudioLibraries.md#decisions-for-the-owner)). The design is [UIDesign.md](UIDesign.md) sections 1, 2, 4, 6 and 8. The downloads were approved on 2026-11-25 ("Download all").

**Built:**
- **The shared workers:** `JobSystem`, with Jolt on it through `JoltJobs` ([JobSystemDesign.md](JobSystemDesign.md)).
- **Vendored unmodified:** FreeType 2.15.0, HarfBuzz 15.2.0, msdfgen 1.13.1, msdf-atlas-gen 1.4, Yoga 3.2.1 and ICU4C 79.1 (`common`).
  - Each has its own `cmake/Raven<Lib>.cmake` and update script, and a README with archive hashes. Shared helpers live in `cmake/RavenThirdParty.cmake` and `scripts/VendorCopy.cmake`.
  - Each update script, run on the downloaded release, reproduces the vendored copy byte for byte.
  - ICU's data is ICU's prebuilt package reduced by ICU's own `icupkg` (built by `cmake/IcuTools`) to the items in `cmake/RavenIcuData.txt`: 38 items, 3.45 MB instead of 33.6 MB.
  - Third-party code builds without Raven's warning level, and as UTF-8: one ICU file has Japanese comments, which MSVC otherwise misreads in the Korean code page.
  - The libraries build in their own CMake directory, so `RAVEN_TEXT_OPTIMIZE_DEBUG` (on by default) compiles them optimized in Debug, as `RAVEN_JOLT_OPTIMIZE_DEBUG` does for Jolt.
- **Fonts:** Noto Sans, Noto Sans Arabic and Noto Sans KR (SIL OFL) in `Content/Fonts`, staged beside the application with their licenses. Variable fonts are drawn at weight 400: Noto Sans KR's default instance is weight 100.
- **Shared 2D path, feed 1** (UIDesign.md section 4):
  - `OverlayQuad` (112 bytes) carries the rectangle, UV, color, border color, clip, corner radius, border width, softness, distance range, texture element and flags.
  - `Overlay.vert/.frag.hlsl` draw it, with the interface checked against the C++ layout on reload.
  - `VulkanOverlayPipeline` draws every quad in one instanced draw after the debug lines, with straight alpha and no depth test.
  - The shader draws rounded rectangles with borders and soft edges, textured images, and MTSDF glyphs, all from the same bindless array as materials.
  - Texture regions (new glyphs) are copied from the frame's upload ring before drawing, with barriers. The GPU never stalls for a glyph.
- **Text (`Private/Text/TextSystem`):** decisions U2, U4 and U5.
  - ICU starts from the filtered data with `udata_setCommonData`.
  - Each paragraph is converted to UTF-16, then gets line-break opportunities (ICU `ubrk`, with dictionaries) and bidi levels (`ubidi`).
  - It is split into runs of one level, one script (`uscript`) and one font. Font fallback goes through Noto Sans, Arabic and KR in order, and combining marks stay with their base's font.
  - Runs are shaped by HarfBuzz in em units, cached per paragraph and direction.
  - Lines break at the last opportunity that fits. Runs are reordered visually per line (`ubidi_reorderVisual`), and lines are aligned start, center or end with the direction.
  - `Prepare` shapes a batch of cache misses in parallel on the workers.
  - Glyphs are MTSDF, 40 px per em with a 4 px range. Each is generated on a background job (msdf-atlas-gen geometry; ink-trap edge coloring; overlap support and a scanline pass for variable-font contours) and packed into 1024² linear pages, which are ordinary managed textures.
  - Text never draws half-formed: a block appears when all its glyphs exist. Printable ASCII is generated from the start.
- **UI (`Private/UI/UiContext`, public `Raven/UI/Ui.hpp`):** decision (c).
  - Systems declare `Begin`/`BeginKey`/`End`, `Text`, `Button`, `Toggle`, `Image` and `Focus` through `SystemContext::Ui`, in Control or Update.
  - IDs hash the scope stack with the stable key, never the displayed text.
  - After Update, a Yoga tree is built from the frame's element list and laid out in logical pixels at the window's DPI scale (Yoga's point scale), right-to-left when asked. Text elements are measured through `TextSystem`.
  - State not declared for two frames is collected.
  - Interactions come from the previous frame's rectangles: hover (the topmost), press, click on release, focus.
  - Keyboard focus moves with Tab/Shift+Tab, Up and Down (with key repeat), and Enter or Space activate.
  - Before routing, the UI withholds pointer buttons over an interactive element, and navigation keys while focused, so gameplay never sees them. While the game listens for a key to bind, the UI neither withholds nor sees the keyboard.
  - Buttons shade on hover and press, and draw a focus ring.
  - `Window::GetContentScale` (Win32: `GetDpiForWindow`) gives the DPI scale.
- **Preview mode, layer 1** (UIDesign.md section 6):
  - games register entries with `SystemTable::Preview` (a name and a function drawing one screen from sample data), kept only for the loaded generation;
  - F6 shows each entry alone without playing, then returns to play;
  - Shift+F6 cycles the UI scale (1, 1.5, 2, 0.75), Ctrl+F6 switches right-to-left, and Alt+F6 pseudo-localizes (accented letters, about 30% longer, in brackets).
- **On-screen diagnostics:** a second UI context draws, bottom-left, the warnings and errors of the last ten seconds (a short log history) and the preview's state.
- **Frame trace:** `ui_ns` (every context's layout and draw-list build) and `ui_quads`.

**Differences from the plan:**
- **Layout runs on the main thread,** one Yoga tree per frame, not a retained tree on a worker (UIDesign.md section 8). Measurement 7 puts the whole UI at 0.05–0.18 ms; the worker and retained nodes arrive with editor-sized trees.
- **Resolution switching in the preview** is the scale switch. A fixed reference resolution is not built.
- **Language switching** waits for string tables (ICU `i18n`, decision U5); pseudo-localization covers overflow now.
- **The bitmap path** for small text and color emoji (U2) is not built. MTSDF at 14 px reads well on the laptop.
- **UI input** is a filter before the router, equivalent to a top context that consumes, rather than a context in the router's table.
- **No scroll box, grid or list containers yet** (U3). The arena's screens did not need them; they arrive with the inspector.

**Measurements** (Release, the laptop; measurement 7 in the table above):

| Screen | UI mean | UI p95 | Quads |
| --- | --- | --- | --- |
| Arena HUD | 0.053 ms | 0.117 ms | 27–53 |
| Pause menu | 0.079 ms | 0.167 ms | 63–196 |
| Key-binding screen | 0.175 ms | 0.288 ms | 196 |

The budget (UIDesign.md section 7, HUD plus pause menu under 0.3 ms) holds. Glyph generation for the text-samples entry (about 150 glyphs in Latin, Greek, Cyrillic, Korean and Arabic) finishes within 1.5 s in Release, and in about 10 s in Debug. Debug is slower even with optimized libraries, because of the debug C++ runtime.

**The job system and physics** (`PhysicsBenchmark`, Release, the same thread count):

| Case | Jolt's own pool | Shared pool |
| --- | --- | --- |
| 1,000 bodies, mean / p95 | 1.106 / 1.832 ms | 1.177–1.249 / 2.000–2.091 ms |
| 4,096 bodies, mean / p95 | 4.994 / 7.509 ms | 5.046–5.151 / 7.623–8.314 ms |
| 10 movers, mean | 0.122 ms | 0.177–0.215 ms |

That is 0.05–0.15 ms per step, the cost of one locked queue shared with the UI and glyph work. A lock-free queue inside `JobSystem` is the follow-up if a physics budget gets tight. The "active after" count varied between runs (998–1000). The likely cause is random entity IDs changing body order; the C10 determinism check covers it.

**Checked:**
- Debug and the CI-equivalent Release build with warnings as errors; the smoke test; all 7 tests.
- **Captures** with the screenshot, validation and synchronization-validation layers, and a clean exit with no diagnostics:
  - the HUD;
  - the four preview entries;
  - right-to-left mirroring;
  - pseudo-localization;
  - scale 1.5;
  - the diagnostics panel;
  - the text samples: Hangul, Arabic joining and right-to-left order, a mixed line, and wrapping across Korean, English and Arabic.
- **A bug found by the captures and fixed:** a vertex-interface declaration (an integer input), caught by the reload check and shown on screen by the new diagnostics.

**Not checked:** IME, gamepad navigation, a Thai or Devanagari font (none is vendored), Clang (the ICU condition holds for MSVC only), macOS and Linux.

### C8, first part — the audio device layer (2026-11-25)

**Scope.**
- **Decision A1 (owner, 2026-11-25):** "Take miniaudio for the backend now, and make sure to encapsulate things."
- **This part:** the device layer only. Decisions A2–A6 ([UIAudioLibraries.md](UIAudioLibraries.md#decisions-for-the-owner)) are settled one by one before the rest of C8.
- **Next:** the UI discussion (C7).

**Built:**
- **miniaudio 0.11.26**, vendored unmodified in `ThirdParty/miniaudio` ([README](../ThirdParty/miniaudio/README.md), with hashes).
  - `cmake/RavenAudio.cmake` holds every setting:
    - the version pin;
    - its engine, node graph, resource manager, decoders, encoders and generators compiled out;
    - WASAPI, Core Audio, PulseAudio and ALSA only;
    - linked frameworks on macOS.
  - `scripts/UpdateMiniaudio.cmake` swaps in a new release.
- **`Private/Audio/AudioDevice.hpp`:** the engine's only view of an output:
  - the fixed mixer format, 48 kHz stereo float;
  - a render callback that may ask for any frame count;
  - `Open`, `Close`, and lock-free events (`Lost`, `Moved`).
  - `RAVEN_AUDIO_BACKEND` (only `miniaudio` for now) compiles the one file under `Private/Audio/Backends/`. No other engine file calls the device API.
- **`Backends/MiniaudioDevice.cpp`:** the default device in shared mode, with the "Games" thread class on WASAPI, automatic default-device moves, and no fixed-size callback buffer. Notifications only set flags; they never start or stop a device.
- **`NullAudioDevice.cpp`:** renders 10 ms at a time in real time and discards the output. It is the silent fallback and the headless path.
- **`AudioMixer`:** the mixer core, with no device or thread of its own.
  - Fixed 256-frame blocks.
  - A lock-free command queue from the main thread.
  - A release queue back, so the main thread frees a clip only after the audio thread has let go of it.
- **`AudioOutput`:** the device lifecycle on a control thread.
  - It opens the default device and reopens it 250 ms after a loss.
  - With no device, it renders silence and retries every 2 s.
  - It logs the device it opened.
- **Clips:** `DecodeWav` converts at load to the mixer format through miniaudio's converter, using a linear resampler with an 8th-order low-pass filter.
- **Removed:** the XAudio2 backend (`Win32Audio.cpp`, `AudioBackend.hpp`), `UnsupportedAudio.cpp` and the `xaudio2` link.

**Behavior kept:**
- `GameServices::PlaySound` and `AudioPlayer` are unchanged for games: one voice, and a new sound replaces the current one.
- Only 16-bit PCM WAV loads.

**Differences from the plan:**
- **The interface is narrower than the note's sketch.** It has no capabilities and no device listing or selection. Both arrive when a settings screen or a second backend needs them.
- **The output opens when the session starts,** in the background. Opening it on the first sound, synchronously, blocked that frame for about 120 ms.
- **Memory:** clips are kept in the mixer format, 384 KB per second of sound. For the 24 kHz mono cues that is 8 times their 16-bit size. Streaming and compressed formats come with decision A6.
- **Reload:** a clip replaced by an asset reload is freed at the next `Play` or `Stop` after the audio thread releases it.
- **macOS and Linux** now build the same backend instead of having no audio. This is not verified there.

**Measurements** (Debug, the laptop, WASAPI shared mode on a USB audio interface, 48 kHz, 480-frame period):

| Case | Result |
| --- | --- |
| A command reaching the WASAPI loopback recording (Play or Stop) | 50–57 ms. Includes the loopback capture's own latency, which cannot be separated out |
| Device open on the control thread | About 120 ms. A sound sent before it started 144 ms after being sent |
| Live Courtyard: the loss cue after its log line | 45 ms. Gameplay at most 0.45 ms per frame, and no hitch from the first sound |

**Checked:**
- **Builds and tests:**
  - Debug and the CI-equivalent Release build with warnings as errors;
  - all seven CPU tests;
  - the Windows smoke test;
  - the live default launch with reloads.
  - Quitting right after the first frame: 8 of 8 clean exits (Samples and Arena).
- **A temporary harness** (ignored, in the scratchpad) compiled the engine's audio files and checked:
  - callback sizes of 1, 255, 257, 1000, 7, 480 and 4096 frames reproduce a clip sample for sample, then silence;
  - a played clip stays held until the audio thread releases it, and is then freed;
  - the null device renders in real time and releases a clip when it ends;
  - on the real device, through loopback, the measured lengths match the clips (600 ms and 480 ms):
    - a sound sent before the device opened;
    - repeated sounds;
    - a replacement;
    - `Stop`.
- **Live Courtyard,** recorded through loopback: the loss cue plays for 610 ms after the courier hit.

**Not checked:**
- **Device changes** (condition 5): unplugging, a default-device change, sleep and wake, a sample-rate change, a Bluetooth headset. These need the owner, because they change system settings.
- macOS and Linux.
- A WAV edited while it plays. The release path is checked in the harness, not by a live file edit.

No tracked test was added. Tests link RavenCore, which does not export the private mixer. A mixer test that guards these behaviors fits with the voice work of decision A3, which rewrites them.

### C8, second part — the engine mixer (2026-11-25)

**Decisions.**
- **A2 (owner, 2026-11-25):** "Implements our own mixer". No middleware; FMOD or Wwise could later be another backend if a game needs one.
- **A3–A6 were delegated** on 2026-11-25. The owner asked to be brought only questions that cost money, change what players or designers see, or change scope. They were judged by expandability and business alignment:
  - **A3, voices:**
    - 64 real voices out of at most 512 playing sounds;
    - concurrency groups with four rules;
    - ranking by priority times audibility;
    - virtual voices, and 10 ms fades on every steal.
    - The model is Unreal's concurrency and Unity's audibility ranking, simplified.
  - **A4, sound events:** systems will play event assets: variations, ranges, a bus and concurrency as data. They are the next part of C8. `SoundParams` is the runtime shape an event fills, so nothing built now is replaced.
  - **A5, buses:** six engine buses with volumes, and Dialogue ducking Music and Ambience. Each bus mixes into its own buffer, so effect slots attach later without changing voices. Buses a game defines itself come when a game needs them, and the engine's six keep their values.
  - **A6, formats:** WAV (any PCM or float depth), FLAC and MP3 through miniaudio's own decoders now. Ogg Vorbis through stb_vorbis 1.25, approved and downloaded by the owner's request on 2026-11-25. Ogg is the small format that loops seamlessly: MP3 adds encoder padding (22 ms on the test clip), so it suits one-shots only. Clips are still decoded whole at load, so until streaming the gain is disk and package size. Opus comes later. Clips longer than 5 minutes wait for streaming.

**Built:**
- **`Raven/Audio/Sound.hpp` (public):**
  - `AudioBus`: Master, Music, Effects, Dialogue, Ambience, Interface;
  - `SoundParams`: bus, volume, pitch, pan, loop, spatial position with minimum and maximum distance, priority, fade-in and concurrency;
  - `SoundConcurrency` with `SoundGroup("Name")`, and the rules StopOldest, StopQuietest, StopLowestPriority and RefuseNew;
  - `SoundHandle`: a 64-bit serial that never repeats within a run and starts at a random value. It is an approved component value, so a game may keep one in a component.
- **`GameServices`:**
  - `PlaySound(sound, params)`, which returns a handle; the one-argument form still compiles;
  - `StopSound`, `IsSoundPlaying`, `SetSoundVolume`, `SetSoundPitch`, `SetSoundPosition`;
  - `SetBusVolume` and `GetBusVolume`.
  - `StopSounds`, scene restarts and quick-loads fade every sound out over 50 ms.
- **`AudioMixer` (audio thread)** renders 64 voice slots in 256-frame blocks:
  - a 32.32 fixed-point position with linear interpolation for pitch;
  - a stereo gain ramp for fades and parameter changes;
  - loops;
  - per-bus buffers whose gains ramp across a block;
  - a safety clamp on the output.
  - Commands carry a slot and a voice serial; stale commands are ignored. Each slot publishes its position once per block. A slot returns to the main thread only through the release queue, so a clip is never freed while the audio thread reads it.
- **`AudioVoices` (main thread)** runs once per frame from `GameSession::Render`, with the drawing camera as the listener. It owns every sound:
  - it applies the concurrency rules when a sound starts;
  - it advances the time of virtual sounds;
  - it computes distance attenuation and constant-power panning: inverse distance from `MinDistance`, fading to silence over the last fifth before `MaxDistance`. The near side stays at exactly 1, so centered playback is sample-exact;
  - it smooths the duck: 9 dB, 80 ms attack, 400 ms release;
  - it ranks sounds with a 10% bonus for those already real, so ranks do not flap.
  - A real one-shot that loses its voice stops. A loop goes virtual and resumes where it would be.
- **Clips:** `DecodeAudio` replaces `DecodeWav`. miniaudio's WAV, FLAC and MP3 decoders convert at load, with the same 8th-order resampling filter. Project files accept `.wav`, `.flac`, `.mp3` and `.ogg` audio.
- **stb_vorbis 1.25**, vendored unmodified in `ThirdParty/stb_vorbis` ([README](../ThirdParty/stb_vorbis/README.md), with hashes):
  - `cmake/RavenVorbis.cmake` holds its settings: a static `RavenStbVorbis` with `STB_VORBIS_NO_STDIO` and no project warnings;
  - `scripts/UpdateStbVorbis.cmake` swaps it;
  - its license is staged as `Licenses/stb_vorbis-LICENSE.txt`.
  - Only `AudioClip.cpp` calls it. Its output goes through miniaudio's converter with the Vorbis channel order: Vorbis puts front center second, so a 5.1 file read in WAV order would put its center on the right.

**Threads.**

| Work | Cost | Thread |
| --- | --- | --- |
| Mixing | 64 voices × 256 frames per 5.3 ms block | The device's audio thread: never allocates, locks or touches assets |
| Voice management | 1.9 µs per frame with 60 sounds | The main thread, once per frame |
| Decoding | At asset load | The loading thread, today the main thread. Long music needs streaming on a worker; that belongs with the engine-wide job-system study |

**Checked:**
- Debug build, and the CI-equivalent Release build with warnings as errors: smoke test and 7/7 CPU tests pass.
- **An offline harness** (ignored, in the scratchpad) that renders on its own thread checked:
  - a centered sound at full volume reproduces the clip sample for sample with callback sizes 1–4096, then silence, and its handle then ends;
  - two sounds sum;
  - a zero-length stop is silent within one block plus the 64-frame declick, with no step larger than the tone's own slope;
  - pitch 2 plays a 4,800-frame clip in 2,400 frames, and a loop plays past its end;
  - StopOldest, RefuseNew, and StopQuietest admitting louder sounds and refusing quieter ones;
  - 80 loops give 64 real and 16 virtual. After 20 stop, the virtual ones take the freed voices;
  - spatial gains (1/10 at 10 m, silence past `MaxDistance`), panning after a move, a bus at 0.5, and the 9 dB duck and its release;
  - FLAC decodes losslessly against the WAV, MP3 decodes with codec padding, and 24-bit 44.1 kHz stereo WAV converts to 48 kHz;
  - Ogg Vorbis decodes to the WAV's exact length, within 0.008 of it;
  - a 6-channel 44.1 kHz Ogg with only a center channel converts to 2 s of centered stereo at 48 kHz. This check found the channel-order bug before it shipped;
  - a corrupt Ogg file and an unknown extension are refused with an error.
- **On the real device** (WASAPI, recorded through loopback):
  - an MP3 plays;
  - a spatial tone 1 m ahead plays centered at 0.200;
  - at 4 m right: right only, 0.050; at 2 m left: left only, 0.100;
  - the Effects bus at 0.25 gives 0.025;
  - it is silent after a 200 ms stop fade.
- Courtyard runs with the new module contract: the game rebuilds, the systems run, and gameplay takes at most 0.48 ms per frame.

**Not checked:**
- **Sounds triggered by live play.** The engine takes keyboard input only while its window is in the foreground, and the window did not get the foreground during this remote session, so the posted keys never moved the courier.
- Device changes (condition 5), macOS and Linux, as in the first part.
- A FLAC or MP3 asset listed in a project file. The project check and the asset decoder share `IsAudioExtension` and `DecodeAudio`, which the harness covers.

No tracked test was added: the mixer is private and tests link RavenCore's exports only (the minimal-tests rule).

### C8, third part — sound-event assets (2026-11-25)

Decision A4 (delegated): games play sound events, not raw buffers.

**Built:**
- **`.ravensound` files** (`Private/Audio/SoundEvent`), text, one setting per line:
  - `clip` (one per variation);
  - `volume` and `pitch` ranges;
  - `bus`, `priority`;
  - `limit <group> <count> <rule>`;
  - `spatial <min> <max>`, `loop`, `fade-in`.
  - Unknown settings are errors.
- **`AssetKind::Sound`** and `AssetRef<SoundAsset>`:
  - project files list them as `sound <uuid> "<path>"`;
  - scenes lease an event's clips with it, so playing never loads a file;
  - player content stages the clips with the event.
- **Hot reload:** sound events and audio clips reload when edited, through the same settled watch as textures. A clip that is playing keeps the old samples until its voice ends.
- **`GameServices::PlaySound(AssetRef<SoundAsset>, SoundOverrides)`:**
  - it picks a variation and random volume and pitch from a presentation-only generator, never the simulation's;
  - the overrides are the position for spatial events, and multipliers for volume and pitch.

**Checked:** in the arena, with the build and tests above.
- The six events load and are watched.
- Their clips are leased with the scene.
- The pause, click and game-over cues and the drone blips reach the system output, recorded through WASAPI loopback around each menu action.

**Not checked:**
- **Which recorded sound belongs to which event:** the drones' blips overlap the menu sounds.
- **A `.ravensound` edited while playing.**

### C9 — Arena game and the developer quick-save, first part (2026-11-25)

**Scope.** The owner ordered on 2026-11-25: C9 without the HUD and sounds runs before C7 and C8. The HUD, the pause-menu screen, the key-binding screen and the sounds follow C7 (UI) and C8 (audio). Both start with a discussion and research with the owner.

**Built:**
- **`Games/Arena`** (FD-46), using only engine APIs:
  - The craft is a mover. A Control system turns the `Arena.Play` actions into a `CraftCommand` input-stream record, relative to the camera; a fixed-step system steers the mover from the tick's records.
  - Drones are spawned at runtime as movers. Their AI runs in fixed steps: wander between random waypoints, chase the craft within 7 m.
  - Energy cells are spawned as sensors. A collected cell is despawned and replaced.
  - A kinematic platform slides along the south wall, and five crates can be pushed.
  - The run is session state on `$arena` (`ArenaRunComponent`): score, drones and timers. Drones are added over time; a drone touching the craft (an `Overlap` query on the drone layer) ends the run, and the next starts after 2.5 s.
  - The best score and the run count live in the profile store (`ArenaProfile` in `Profile`), saved when a run ends.
  - Esc or P pauses through the blocking `Arena.Pause` context and the new game pause below.
  - The camera rig in Update follows the craft and turns with Q and E.
  - Fixed-step code draws its random numbers from `SystemContext::Random` and visits entities in persistent-ID order (FD-48).
  - `--swarm` starts 120 drones without a game over, for measurement 3.
- **`ArenaTool create-arena`** generates `Levels/Arena.ravenscene`.
- **Developer quick-save** (FD-33 D):
  - Ctrl+F5 snapshots the live world, together with the simulation random numbers, the entity-ID sequence and the fixed tick. Ctrl+F9 restores it.
  - It reuses `CaptureScene` and the scene factory, through the new `SceneSession::RestoreSnapshot` and `Scene::GetIdState`. Physics, picking and interpolation rebuild as they do after a scene restart.
  - Details: [UserData.md](UserData.md#developer-quick-save).
- **Game pause:** `GameServices::SetSimulationPaused` and `IsSimulationPaused`. While paused, fixed steps and physics stop; Control and Update keep running.
- **Startup log:** the engine-keys line now lists every developer key.

**Differences from the plan:**
- **The game pause is an engine service.** FD-46 described the pause menu as an input context that consumes gameplay input. That alone leaves physics running: crates keep sliding and movers keep their momentum. So the game also asks the engine to stop the fixed steps.
  - Stream records emitted while paused are dropped.
  - The developer pause (F8) stays separate: it stops every game system.
- **Quick-save keys and storage:**
  - Ctrl+F5 and Ctrl+F9 follow the usual F5/F9 quick-save convention without taking F5 (reload) and F9 (step) from the developer context.
  - The snapshot stays in memory for the session, and loads only into the scene it came from.
  - Physics restarts from the components: positions and velocities carry over, but Jolt's contact and sleep state does not, so a sensor touching at the save reports Begin again after the load.
- **The score is logged** until the HUD arrives with C7.
- **The floor** is two-tone tiles over one collider, so motion reads on screen; the stretched UV-grid texture looked like shadows.

**Measurements** (measurement 3, arena part, Release, the laptop, 60 Hz FIFO, one fixed step per frame):

| Case | Movers | Physics per step, p50 / p95 |
| --- | --- | --- |
| Normal play, idle craft: drones catch it and runs restart | 4 | 0.084 / 0.166 ms |
| `--swarm`, first 3 s, drones spread out | 121 | 1.15 ms p50 (about 9.5 µs per mover) |
| `--swarm`, 3–28 s | 121 | 2.50 / 2.80 ms |
| `--swarm`, 24–27 s, drones piled onto the craft | 121 | 2.74 ms p50 (about 23 µs per mover) |

- **Debug:** normal play, with validation layers, costs 0.28 / 0.41 ms.
- **For FD-47:**
  - Arena play costs under 0.2 ms.
  - Spread movers cost about what the C6 benchmark measured, plus the AI.
  - A pile of characters on one target costs 2–3 times more per mover, because each character collides with many neighbors.
  - Action games with tens of characters fit easily; hundreds crowding one spot belong to the crowd backend.
  - Measurement 3 is complete, and FD-47 is ready for the owner's review.

**Checked:**
- **Builds:** Debug and Release, and the CI-equivalent Release build with warnings as errors, pass.
- **Tests, smoke and live:** all seven CPU tests, the Debug Windows smoke and the live default-launch check pass. No tracked test was added; the C10 determinism test will load the arena scene.
- **Arena, live with the Vulkan validation and synchronization layers,** with no diagnostics:
  - steering and jumping;
  - Esc pausing, with held keys consumed and the picture still;
  - resuming;
  - a quick-save (tick 245, 67 entities, 14.3 KB), then a quick-load, with play continuing;
  - the collider view.
- **On screen, in temporary variants of the scene:**
  - the craft riding the kinematic platform;
  - the crate pyramid scattering as the craft drives through it;
  - cells collected (12 in one run);
  - a drone ending the run with a new best saved;
  - the best score (4) shown after the host restarted.

**Not checked:**
- the HUD, the pause-menu screen, the key-binding screen with saved bindings, and sounds: built in the second part below;
- interpolation judged by eye on a high-refresh display;
- macOS and Linux.

**Found in passing and fixed:** quitting within about 0.5 s of the first frame ended in an access violation. The committed M6A build did the same, so it predated M6C. The cause was a shader-library teardown race during the startup shader compiles ([HotReloadDesign.md](HotReloadDesign.md#implementation-notes-2026-11-23)), fixed on 2026-11-25.

### C9, second part — HUD, menus, key bindings and sounds (2026-11-25)

**Built in `Games/Arena`:**
- **`ArenaUi.hpp/.cpp`:** each screen is drawn from a plain model, so the game systems and the preview entries share it.
  - **HUD:** score, best, time and drones, plus the start and game-over banners, hidden while paused.
  - **Pause menu:** Resume and Key bindings, with focus on Resume when it opens.
  - **Key-binding screen:** a row per action direction (Forward, Back, Left, Right, Jump, Turn left, Turn right, Pause) with its keys, through the existing rebinding API. Clicking a key listens for a new one; Esc keeps the old one. "+" adds a key, and Reset to defaults and Back follow. Esc on this screen goes back to the menu.
  - The menu's state is view state on the camera rig (`ArenaMenuComponent`), never simulation state.
- **Preview entries:** Arena HUD, Arena game over, Pause menu, Key bindings and Text samples.
- **Sounds:**
  - `ArenaTool create-sounds` synthesizes seven 48 kHz clips and writes six sound events: Pickup (two variations, spatial, limited), Jump, Caught, Drone, Click and Pause (the Interface bus);
  - `ArenaRulesComponent` references them, and the scene was regenerated with `ArenaTool create-arena`;
  - cells, jumps, drone spawns, being caught, pausing and menu clicks play them.

**Checked:** live, by keyboard only (posted keys; the window had no foreground in this remote session), with captures:
- **The menu flow:** pause, then Key bindings by Tab and Enter, back by Esc, and resume by Enter.
- **Rebinding:** Forward's W became Space, and the saved bindings file showed Space (code 44). Binding Space works because the UI stays out of the keyboard while listening, a bug found by this check and fixed.
- **Reset to defaults** by Shift+Tab and Enter restored W. Shift+Tab first moved the wrong way, because navigation read the end-of-frame Shift state; it now reads each key event's modifiers.
- **Fixed with the captures:**
  - the menu's state component was added twice in one frame;
  - the game-over banner covered the pause menu.

**Not checked:**
- the mouse on the menus (posted mouse input needs the foreground);
- a gamepad;
- playing by hand and judging the sound mix by ear.

### C10 — Determinism check and the exit check (2026-11-25)

**Built:**
- **The determinism check (FD-48), a tracked GPU test.** `RavenEngine --game Arena --determinism-check [ticks]` (600 by default) runs the staged game with a fixed seed, no watching and no user data. A scripted key lane (steer, turn, jump) drives the real frame loop one fixed tick per frame. After every tick the scene is hashed (`CaptureScene().Encode()`, FNV-1a). After the last tick the scene restarts, plays the same lane again, and every tick's hash is compared.
  - On the first difference the host writes both scenes (`DeterminismFirstRun.ravenscene` and `DeterminismSecondRun.ravenscene`) beside the executable, for `RavenSceneTool dump`, and exits with an error naming the tick.
  - CTest runs it as `DeterminismCheck` (label `gpu`, about 23 s in Debug).
- **Physics restarts like a first start.** The check found that a restarted Arena diverged from tick 2, while three fresh processes matched. Clearing the physics world removed the bodies but kept Jolt's body-ID sequence, broad-phase trees and contact caches. `PhysicsWorld::Clear` now destroys Jolt's world and builds a new one; the job-system workers outlive it. Approved by the owner on 2026-11-25.
- **Reload time (measurement 5):** the game PCH now also holds the game-facing engine headers: `GameSystems.hpp`, `Log.hpp`, `SceneCommands.hpp` and every header in `Raven/Components`. The H5 finding that they gave no gain predates M6C's input, UI, sound and physics headers.
- **Arena is split by object** (owner, 2026-11-25): `ArenaSystems.hpp/.cpp` for the shared helpers, then `Craft.cpp`, `Drones.cpp`, `Cells.cpp`, `Platforms.cpp`, `Run.cpp`, `Camera.cpp` and `Screens.cpp` (pause, menus, HUD). `ArenaGame.cpp` keeps only the contract, the components, the input mapping and the system order; the F6 preview entries moved to `ArenaUi.cpp`. The determinism hash after 600 ticks is the same before and after the split (`09c983c4ab39958e`), so behavior is unchanged.
- **Benchmark script:** `Benchmark-Windows3D.py` snapshots now copy the top-level `Content` folder (the engine fonts since C7). Without it the snapshot build failed.

**Measurement 5: a one-line game-code edit, save to active** (an external probe edits a log string five times per run, the laptop, Jolt and the UI stack present):

| Case | Debug (target ≤ 1.5 s) | Release (target ≤ 2.0 s) |
| --- | --- | --- |
| Arena before C10 (one 718-line file) | 2.71–2.89 s, median 2.81 s | 3.34–3.80 s, median 3.57 s |
| Arena with the new PCH | median 2.00 s | median 2.47 s |
| **Arena with the PCH, split by object** (edit in `Screens.cpp`, two launches) | **1.46–1.57 s, medians 1.47 and 1.50 s** | **1.37–1.46 s, median 1.40 s** |
| Samples before C10 (`Interaction.cpp`) | 1.94–2.05 s, median 1.95 s | 2.17–2.26 s, median 2.21 s |
| **Samples with the new PCH** | **1.41–1.46 s, median 1.44 s** | **1.60–1.76 s, median 1.71 s** |

- Almost all the time is the game build. Before C10, compiling `ArenaGame.cpp` alone took 2.3–2.4 s, with or without the engine running. Activation, load and copy take about 50 ms.
- The first edit after a host rebuild takes 4–7 s, while the game's PCH is built. Later edits and later launches run at the speeds above.
- Debug meets ≤ 1.5 s at the median, like M5 did (1.3–1.6 s); single edits reach 1.57 s.

**Measurement 6: 50k animated steady state** (`Benchmark-Windows3D.py scale --meshes 50000 --case auto-fifo`, Release, 165 Hz FIFO). A same-day A/B against a clean checkout of the M6A commit (`5f4df75`), three runs per set:

| Set | FPS | CPU preparation p95 | World pass p50 | Render-world sync p50 |
| --- | --- | --- | --- | --- |
| M6A, first set | 165.0–165.4 | 3.78, 3.61, 3.31 ms | 1.37–1.44 ms | 0.94–1.02 ms |
| **M6C** | **165.0–165.1** | **3.52, 3.32, 3.26 ms** | **1.35–1.45 ms** | **0.90–0.98 ms** |
| M6A, second set | 165.0 | 3.40, 3.22, 3.31 ms | 1.35–1.43 ms | 0.90–0.98 ms |

- **No regression:** M6C falls inside M6A's own spread on the same day. Every M6C frame took under 5 ms of CPU.
- The first run of a set is often the slowest, in both builds. It explains the 3.5–3.8 ms p95 values; two earlier M6C sets measured 3.30–3.64 ms the same way.

**Checked:**
- **Tests:** all eight pass in Debug, including `DeterminismCheck`. The determinism check also passed by itself before and after the Arena split.
- **Exit item 5, live** (Debug, posted keys, captures; the owner's `out/User/Arena` backed up and restored):
  - Forward was rebound from W to Space, and the saved file held Space (code 44).
  - After a restart the key-binding screen showed Space, and the best score (9) was unchanged.
  - With the bindings file and its backup both damaged, both were moved aside as `.damaged-<UTC time>` and Forward showed W again. The log has one warning per damaged file, so two here.
- **Exit item 7, statically:** no public header includes a vendored library, and every library under `ThirdParty` carries its license file.
- **CI-equivalent builds:** a fresh checkout plus the uncommitted changes, configured with CMake 4.3.1 (Visual Studio's) and warnings as errors. The Debug and Release builds pass, and so do the 7 CPU tests in each.
  - The first attempt ran from a 120-character scratch folder and failed in the game modules' compiler checks (`C1083` with an empty file name, and a corrupted manifest `RC2136`). Their paths reached about 230 characters, near Windows' 260-character limit. The same tree passed from `D:\RavenEngine\out\ci4`. Deep checkout folders can hit this; CI's checkout path is short.

**Not checked:** an audio device change by hand, and a debugger breakpoint across an automatic rebuild. Both stay manual owner items.

### M6C exit checklist results (2026-11-25)

| # | Item | Result |
| --- | --- | --- |
| 1 | The arena game runs on engine APIs only | **Pass.** Every part is in the C9 records: the craft through the input stream and the character backend, drone AI writing movers, sensor cells, runtime spawning, the run as session state, the best score in the profile store, the pause menu consuming gameplay input, the key-binding screen with listening and saved bindings, and overlapping sounds. The owner played it on 2026-11-25. |
| 2 | 3D samples keep working; Courtyard keeps building | **Pass.** The Samples reload probe ran the default launch; Courtyard builds in both configurations. Its 2D run was not requested. |
| 3 | Developer keys in the developer context; a game binding on F5 no longer reloads | **Pass.** `InputMappingTests` binds F5 in a game context: the developer context reloads and the game never sees F5. |
| 4 | The determinism check passes; the quick-save restores and play continues | **Pass.** `DeterminismCheck` (600 ticks); the quick-save in the C9 first part (tick 245, 67 entities). |
| 5 | Bindings and the best score survive a restart; a damaged settings file falls back | **Pass,** live above. A damaged file and its backup give one line each. |
| 6 | Every measurement recorded; game-code edits within the M5 targets; no 50k regression | **Pass.** Measurements 1–7 are recorded. Measurement 5 meets both targets at the median after C10's two changes; measurement 6 shows no regression against M6A. |
| 7 | Vendored with licenses, outside the public headers; CI passes with warnings as errors | **Pass locally.** CI-equivalent Debug and Release builds with CMake 4.3.1 and warnings as errors, plus the CPU tests, on a fresh checkout. GitHub CI runs on the next push. |

## Owner decisions (2026-11-24)

The owner settled each decision one at a time.

| ID | Decision | Notes |
| --- | --- | --- |
| FD-29 | A(b), B(b), C as listed | "context should work / input mapping context would be great" |
| FD-46 | The arena game | "that is getting closer to what i really want in the future" |
| FD-32 | Jolt inside the engine | "jolt physics, i personally like that solution", after a review of its costs and pitfalls; the physics iteration set approved separately, with the requirement to "find effective way for game dev iteration" |
| FD-47 | Input stream with amendments, minimal movement | Provisional. The owner first set RTS as the primary genre, then clarified: "we must secure ability to build rts", not focus on it. The control layer came from another team's proposal, taken "as strong suggestion" |
| FD-48 | Level 1 with one check, no replay; level 2 at the RTS milestone; level 3 later | "approved for now / after measurement, we can decide and discuss things again" |
| FD-31 | Engine-owned unified stack, no ImGui | "this approval is not full approval, but for early deliverable"; the owner asked to plan the engine-owned method correctly |
| FD-33 | A(b), B (field-table records, text plus binary with checksum), C, D with the developer quick-save | The owner asked for formats beyond JSON, including binary serialization and encryption, which became the stage pipeline |
| FD-34 | Interpolation; Jolt picking in game and editor; profile data outside the ECS and DLL; present-queue switch skipped | The owner proposed Jolt for editor picking and the separate profile store |
