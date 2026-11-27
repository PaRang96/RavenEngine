# UI design note: the engine-owned 2D and UI stack

Status:
- **The stack is approved in full (owner, 2026-11-27, joint review).** [FD-31](GameServicesDesign.md#fd-31--text-and-ui) first approved the engine-owned stack for the M6C early deliverable only (2026-11-24): "this approval is not full approval, but for early deliverable". Sections 1, 2 and 4 were approved on 2026-11-25; sections 3, 5, 6 and 7 are still written and reviewed before the work each covers.
- **Owner decisions (2026-11-25):**
  - U1: Raven builds its own stack ("Let's build own stack").
  - U4: it must support many languages, "not only kor or eng".
  - U3: layout through Yoga plus our own containers (section 3).
  - U5: Unicode processing through ICU4C, unmodified: `common` in C7 (bidi, script runs, breaks with dictionaries, normalization), `i18n` with string tables. Only `Private/Text/` calls it.
  - Section 4's text plan is therefore revised by decisions U2 and U5 ([UIAudioLibraries.md](UIAudioLibraries.md#decisions-for-the-owner)). Shaping, on-demand glyphs, right-to-left text and font fallback move from "not in M6C" into the base design.
- **Early deliverable built (2026-11-25):** the shared 2D path, the text stack, the UI core with game widgets, the preview mode (layer 1) and on-screen diagnostics. The record, measurements and differences from this note are in [GameServicesDesign.md](GameServicesDesign.md#c7--the-2d-and-ui-early-deliverable-2026-11-25).
- **Written now:** sections 1 (API model), 2 (ownership and lifetime) and 4 (rendering). The early deliverable rests on these three, so they are reviewed before any UI code.
- **Written before any UI work beyond the early deliverable:** sections 3 (layout), 5 (input and text editing), 6 (editor path) and 7 (budgets). Their open questions are listed so the early deliverable does not close them.

## Goals and constraints

- **One stack for the game and the future editor,** in the spirit of Unreal's Slate (the Unreal Editor is built on Slate, and the in-game UMG system sits on top of it). The owner's earlier attempt with Dear ImGui needed too many modifications.
- **Hot reload:** nothing retained may point into the game DLL, and no module callback is stored (the joint ECS and reload rule).
- **Must not block an RTS:** heavy HUDs, command panels, and thousands of world-space bars and labels.
- **No throwaway work:** the early deliverable is a thin cut of the final design, not a prototype. In particular, M6B's sprites (FD-20) and this stack share one 2D path instead of building two.
- **Minimum spec:** the laptop in Release; budgets in section 7.

---

## 1. API model

### Options

| Model | How it works | Strengths | Costs |
| --- | --- | --- | --- |
| **(a) Retained, Slate-style** | A tree of widget objects lives across frames; events reach game code through bound callbacks (`OnClicked`); invalidation redraws what changed | Efficient for large, mostly static UIs; familiar to designers; animations and transitions live on the widget | Every bound callback points into the game DLL, so each reload must tear down and rebuild the binding web. Widget state and game state are two copies that must be kept in step |
| **(b) Pure immediate mode** (Dear ImGui, microui) | Game code declares the whole UI every frame (`if (ui.Button("Resume")) ...`); the library keeps a little state keyed by ID | Nothing points into the DLL between frames, so reload just works; the UI always shows the current game state; little boilerplate | Rebuilds every frame; layout usually uses last frame's sizes or is very simple; authored layouts and designers are hard to support |
| **(c) Immediate API over an engine-owned cache (recommended)** | Code declares elements every frame into a transient list; the engine lays them out, keeps per-ID state (focus, scroll, animation, cached layout and text) and draws them | (b)'s reload behavior and simplicity, plus a real layout pass and caches that keep per-frame cost low. Later, authored UI documents can produce the same element list from data | The engine owns a cache and its garbage collection; hit tests use last frame's rectangles (one frame of lag, invisible in practice) |

### Precedents for (c)

Added for the owner's question on 2026-11-25:
- **Unity Editor IMGUI** (`OnGUI`), which built most of the Unity Editor for over a decade:
  - an immediate API with control IDs (`GUIUtility.GetControlID`);
  - per-ID state objects kept by the engine;
  - a real layout pass (`GUILayout`'s Layout event before Repaint).
- **RAD Debugger** (Epic Games Tools, open source):
  - an immediate API that builds a widget tree each frame;
  - widgets keyed by hash and cached across frames;
  - an automatic layout pass, and input tested against the previous frame's rectangles.
  - This is the closest match to (c).
- **Zed's GPUI**, the editor's own Rust framework:
  - described by its authors as "hybrid immediate and retained mode";
  - element trees rebuilt every frame, with state kept in retained entities;
  - flexbox layout by Taffy, the Rust counterpart of Yoga.
- **Dear ImGui and egui.** Both are often called purely immediate, but they keep ID-keyed state across frames: window positions, scroll, tree and table state. They lack only a real layout pass.
- **Outside games:** React, Flutter, SwiftUI and Jetpack Compose. Each re-runs a declarative description on every update and keeps state keyed by identity.

### Decision: (c) (owner, 2026-11-25)

The owner approved (c), with a visual designer added later: "We will support visual designer later, like unreal umg design panel. With that in count, I approve c." See [Visual editing and preview](#visual-editing-and-preview-owner-request-2026-11-25-decided).

**Declare, lay out, draw.** Each frame:

1. **Declare.** UI code (the per-frame control phase from [FD-47](GameServicesDesign.md#fd-47--movement-and-control)) calls the UI API. Each call appends a small element record to a transient frame list and returns the interaction result computed from the previous frame (hovered, pressed, clicked, value changed).
2. **Lay out.** After the control phase, the engine runs layout over the element list (anchors, stacks, grids; section 3) and stores each element's rectangle in the ID-keyed cache.
3. **Draw.** The engine turns laid-out elements into 2D quads for the shared path (section 4).

**Events are return values, never callbacks.** A button returns `true` on the frame it is clicked. To affect the simulation, UI code writes to the input stream (FD-47); to change session state such as pause, it calls the session service. Nothing the UI keeps refers to game code.

**Authored UI later.** Designers will author UI as content documents in a visual designer, like Unreal UMG's design panel (owner, 2026-11-25). Under (c), a document is just another producer of the same element list, so adding it changes neither the cache nor the renderer.

---

## 2. Ownership and lifetime

**Approved by the owner on 2026-11-25, with the three refinements below** (stable-key labels, retained scopes, styles as data assets). The owner added: "if we can make things into multi thread or multi process for better performance, study it". See [section 8](#8-threads-and-processes-owner-request-2026-11-25).

| Thing | Owner | Lifetime | Survives game DLL reload |
| --- | --- | --- | --- |
| `UiContext` (one per window, later one per editor panel) | Host | Session | Yes |
| ID-keyed state: hover/active/focus, scroll offsets, animation timers, toggle and text-field state | Host, inside `UiContext` | Until the ID has not been declared for a few frames, then collected | Yes |
| Layout cache and text-layout cache | Host | Same as the ID entry | Yes |
| Element list for this frame | Host frame arena | One frame | Not needed |
| Font atlases and UI textures | Asset store, as leases | Asset lifetime; hot-reloaded | Yes |
| Styles (colors, spacing, fonts) | Data records declared like components (field tables) | Content or code; hot-reloadable | Yes |
| What the UI shows (score, menu options) | The game's own state: ECS session data or the profile store (FD-33/FD-34) | Game | Yes, through those stores |

**Widget IDs.** An ID is a hash of the current scope stack plus a label or an explicit key. Code that declares the same UI after a reload produces the same IDs, so focus, scroll and animation survive edits. Dynamic lists must pass an explicit key, normally the item's `EntityRef`, so state follows the item and not its position.

**Refinement for many languages (U4, 2026-11-25; approved).**
- **The label is a stable key, never the displayed text.** In `ui.Button("Resume")`, `"Resume"` names the element. Its shown text comes from the string table for the current language.
- **Why:** if IDs were hashed from displayed text, switching to Korean would change every ID. Focus, scroll and animation state would be lost, and UI documents (layer 3) could not find their elements by name.
- **Text that is not a label** (a player name, a score) is passed as a value and is never hashed into an ID.
- **String tables** (keyed text per language, plural rules and argument formatting) are researched with U5.

**Refinement for retention (approved).**
- **Default:** state for an ID not declared for 2 frames is collected.
- **Retained state:** a scope can ask to keep its state while hidden, such as a closed tab's scroll position. That state is collected when the context or scene ends.

**Styles for the designer.** Style records are data assets (field tables), so the later designer edits them like any other content. Code can still override a value for one element.

**Input.** The UI has its own mapping context in the [FD-29](GameServicesDesign.md#fd-29--input-mapping-contexts-and-bindings) stack, above gameplay. It consumes pointer input when the cursor was over an interactive element in the previous frame's layout, and keyboard or gamepad input while an element has focus. Consumed input never reaches gameplay contexts, so clicking a menu button does not also fire a weapon.

**Several contexts.** The game HUD and, later, each editor panel own separate `UiContext`s, so their ID spaces and focus never collide. They share fonts, styles and the renderer.

---

## 3. Layout (to be written before work beyond the early deliverable)

**Library decided (U3, owner, 2026-11-25): option D** ([U3 options in detail](UIAudioLibraries.md#u3-options-in-detail-2026-11-25)). Yoga 3.2.1 does flex layout: rows, columns, wrapping, min and max sizes, and absolute positioning with percentage insets. Its right-to-left direction flips start and end. Our own grid, virtualized list, scroll box and scale box sit in Yoga's tree as leaves with a measure function, and follow the same direction and sizing rules. Anchors, stacks and DPI scaling in the early deliverable are expressed through Yoga from the start, so nothing is replaced later. Each UI context's tree lays out on one worker (section 8).

Open questions, settled when this section is written:

- How far layout must go for the editor: docking and splitters.
- The exact rules our grid and list follow inside Yoga's sizing.

---

## 4. Rendering: the shared GPU 2D path

**Approved by the owner on 2026-11-25:** one instanced-quad format for UI, text and sprites, with rounded corners, borders and shadows drawn by the shader. Alternatives were a separate triangle renderer like Dear ImGui's, and one mesh per element. Arbitrary shapes, such as editor graph curves, get a small triangle path when the editor needs them. The text plan below is revised by decisions U2 and U5.

### Today (checked)

Sprites are gathered on the CPU every frame (`GameSession::GatherSprites`) into `VulkanTexturedDraw` records: one textured mesh, one texture with its own descriptor and one model matrix each. Lit meshes already use a bindless texture array behind `Raven/Material.hlsli` (M3, G4). There is no text drawing.

### One instance format, two feeds

Everything 2D is drawn as **instanced quads with one shared instance format**:

| Field | Purpose |
| --- | --- |
| Transform (2×3 for screen space; the world transform for world space) | Position, size, rotation |
| UV rectangle | Region of the texture or atlas |
| Color and tint | Per-quad color |
| Texture index | Index into the same bindless array that materials use |
| Flags | Plain image, MSDF glyph, solid color |
| Clip rectangle index | Clipping inside scroll panels |
| Layer and order | Draw order |

It is drawn by one family of shaders sharing an engine header, reloaded through the shader library and checked against the C++ layout like every other engine program.

**Feed 1, screen-space UI (M6C).** Built on the CPU every frame from the laid-out element list, written into the persistently mapped per-frame ring (M2), and drawn in an overlay pass after the 3D scene with alpha blending, in layer and submission order. There is no culling; clipping happens in the fragment shader through the clip index, so a typical HUD or menu is **one instanced draw**.

**Feed 2, world-space quads (M6B, FD-20).** Sprites, and later world-space bars and labels, go through the render world with GPU culling, using the same instance format and texture indices. This is the path that scales to thousands of RTS health bars.

M6C builds the instance format, the shader header and feed 1. M6B builds feed 2 on the same format, replacing the per-texture sprite descriptors. If M6B starts first, it builds the format and header from this section and M6C adds feed 1. Either way, nothing is written twice.

### Text

**Revised by U2 (owner, 2026-11-25):**
- **Target D:** HarfBuzz, FreeType, msdfgen and msdf-atlas-gen run at runtime, with atlases filled on demand.
- **Two glyph paths:** MTSDF by default, plus hinted bitmaps for small screen text and color emoji.
- **Same layout:** both paths lay out with the same unhinted advances.
- **This replaces** the offline-only atlas tool described below ([UIAudioLibraries.md](UIAudioLibraries.md#u2-options-in-detail-owner-question-2026-11-25)).

- **Fonts are content.** A content tool runs msdf-atlas-gen (MIT) on a font file and writes an atlas image plus glyph metrics (rectangles, advances, kerning). The reload service records the font as an input, so editing or replacing a font reloads it like a PNG.
- **MSDF** (multi-channel signed distance fields) keeps glyphs sharp at any size and zoom, which matters for DPI scaling, the editor and labels in the world. The shader takes the median of the three channels and anti-aliases over a screen-pixel range.
- **Layout at runtime:** UTF-8 decoding, advances, kerning and line breaks produce glyph quads that go through the same instance format. Laid-out runs are cached by ID and text.
- **Large character sets** (Korean, Japanese, Chinese) need glyphs added on demand. Atlases are split into pages so that a page can be added later without changing the format. Not in M6C.
- **Not in M6C:** text shaping for scripts such as Arabic or Thai (HarfBuzz), rich text, IME input.

#### Sizes match the design tools (owner question, 2026-11-25; proposal)

**The Unreal problem.** UMG reads the font size you type as points, then renders at 96 DPI, so a size of 12 becomes 16 pixels. Design tools such as Figma, Photoshop at 72 ppi and CSS read the same number as pixels. Designs therefore came out a third larger, and teams divided by 4/3 by hand. Epic now offers a 72 DPI font resolution in Project Settings, with 96 DPI kept as the legacy default, and warns of rounding errors with custom values ([Epic: Font DPI scaling](https://dev.epicgames.com/documentation/unreal-engine/font-dpi-scaling-in-unreal-engine)).

Other common sources of mismatch:
- **Line height:** fonts carry several sets of vertical metrics (hhea, typo, win). Tools that pick different ones place lines differently.
- **UI scale:** UMG scales by the window's shortest side against a reference resolution. A design viewed at another resolution changes size.
- **Hinting:** FreeType hinting snaps outlines to the pixel grid, which changes apparent size and weight.
- **Letter spacing units:** Figma uses percent or pixels; Unreal uses 1/1000 em.
- **Font fallback:** a missing glyph drawn by another font has different metrics.

**Raven's rules** (proposal with U2):
- **Size:** a font size is the em size in logical pixels, the CSS and Figma unit. There is no point unit and no DPI conversion in the number a designer types.
- **Scale:** one scale is applied afterwards, the window's DPI scale times the user's UI scale, and it is shown in the preview.
- **Line height:** default line spacing comes from one documented set of font metrics (the typo metrics, as CSS uses when the font sets `USE_TYPO_METRICS`, otherwise hhea). An explicit line height is a multiple of the font size, as in CSS and Figma.
- **Letter spacing:** in em-relative units, with percent accepted, as in Figma.
- **No hinting on the MSDF path,** so outlines are the designer's outlines. Only the optional small-size bitmap path (U2) would hint.
- **Design overlay in the preview (layer 1):** a designer's mockup image can be shown over a previewed screen at the reference resolution, with adjustable opacity, so differences are visible at once.

### Other users of the path

On-screen reload diagnostics and physics or frame statistics use the same text drawing. Debug line drawing (the collider view) stays on the existing debug-draw path.

---

## 5. Input and text editing (to be written before work beyond the early deliverable)

The early deliverable has pointer hover and click, keyboard focus and keyboard activation (built 2026-11-25):
- Tab and Shift+Tab, Up and Down (with repeat) move focus;
- Enter and Space activate;
- the modifiers of each key event decide;
- while the game listens for a key to bind, the UI leaves the keyboard alone.

Open questions:

- Gamepad navigation: spatial or explicit neighbors.
- Text fields: caret, selection, clipboard, IME composition (needs the typed-text input deferred in FD-29).
- Drag and drop between editor panels.

---

## 6. Editor path (to be written before work beyond the early deliverable)

Planned order: the component inspector first, built from the field tables, starting with drag-to-change number fields so it needs no text editing; then text fields, trees and tables; then docking. Open questions: whether designers author UI as documents, and where editor panels live relative to game play sessions.

### Visual editing and preview (owner request, 2026-11-25; decided)

**Decided with (c):** all three layers below. The visual designer (layer 3) comes later, as a design panel like Unreal UMG's. Layers 1 and 2 come first.

The owner asked how UI is edited visually, and wants a UI preview because it has high value. Under (c), every way of editing produces the same per-frame element list, so preview and visual editing are layers on one stack, not a second UI system.

**Precedents.** Code-first declarative UIs with visual previews:
- **SwiftUI** (`#Preview`): Xcode's live canvas, with device, locale and dark-mode variants.
- **Jetpack Compose** (`@Preview`): previews with locale, font-scale and screen-size parameters.
- **Flutter:** hot reload plus a widget inspector.
- **Storybook** (web): each screen shown alone with sample data.

Retained engines edit visually through documents: Unreal UMG's designer, Unity's UI Builder (UXML) and Godot's scene editor.

| Layer | What it gives | When |
| --- | --- | --- |
| **1. Live preview** | Editing UI code reloads the game module, and editing a style or font reloads it, with the screen updating in place (existing hot reload). A game registers preview entries: a screen plus sample data, registered like systems. A preview mode shows one entry alone, without playing. In it, the developer switches resolution, DPI scale, language, right-to-left and pseudo-localization (longer, accented strings that expose overflow) | C7, the early deliverable |
| **2. In-game UI inspector** | A developer key overlays element outlines. Clicking an element shows its ID, rectangle and style. Style values (colors, spacing, font sizes) are changed live and written back to the style records | Before the editor |
| **3. UI documents and a visual designer** | Designers lay out screens as content files. Code looks up document elements by name every frame (`if (menu.Button("Resume"))`) and supplies data and behavior. A drag-and-drop designer edits only the document, and documents hot-reload like scenes | With the editor (M7) |

**Why this order:**
- Layer 1 costs little: preview entries and switches over the existing reload path.
- Multi-language support (U4) makes previewing a screen in Arabic, German or Korean the most valuable check.
- Layer 3 needs the editor's panels, selection and undo. Its document format is designed with section 6, so the early deliverable must not close it.

---

## 7. Budgets (to be written before work beyond the early deliverable)

Proposed for the early deliverable, to be confirmed by measurement 7 in [GameServicesDesign.md](GameServicesDesign.md#measurements-that-gate-provisional-parts): the arena HUD plus the pause menu under 0.3 ms of CPU per frame in Release on the laptop, drawn in one or two instanced draws.

**Measured on 2026-11-25:** the HUD takes 0.117 ms at p95 and the pause menu 0.167 ms. The key-binding screen, with 196 quads, takes 0.288 ms. Each frame's overlay is one instanced draw.

---

## 8. Threads and processes (owner request, 2026-11-25)

The owner asked to study multithreading and multiple processes wherever they improve performance.

**Today (checked):**
- **Main thread:** game systems run here (ECS D9: single-threaded, with system access declared for later parallel scheduling), and so does render recording.
- **Separate threads:**
  - Jolt has its own pool: the CPU count minus one, capped at 8.
  - The reload service has a watcher and two workers.
  - The game build has a worker.
  - Audio has a control thread and a device thread.
- **No shared job system.** These groups do not share workers, so on 8 cores they can oversubscribe.

**The UI frame, stage by stage:**

| Stage | Cost | Thread | Why |
| --- | --- | --- | --- |
| Declare (game UI code) | Small: appending element records | Main thread, in the Control phase | The API is not thread-safe within one context, and game code runs there |
| Shaping text that missed the cache (HarfBuzz) | High for complex scripts, but only on a miss | **Workers, in parallel.** Misses are collected after Declare and shaped as one batch | A shared HarfBuzz font is safe to read from many threads, and each worker has its own buffer |
| Layout (Yoga) | Moderate; incremental through dirty flags | **A worker, overlapping the fixed steps and physics.** Joined before the draw build | Layout reads only the element list and the caches, never game state. Hit tests use the previous frame's rectangles, so nothing this frame waits for it |
| Glyph generation (msdfgen) | The highest: a few hundred microseconds or more per glyph | **Always on workers, asynchronous.** New atlas regions upload through the per-frame ring | Must never block a frame. Common sets are baked in the background when a font loads; offline-baked pages from the content tool cover the most common glyphs |
| Draw-list build | Small, linear in elements | Per context, on workers. Each context writes its own region of the ring | The HUD and editor panels build in parallel |
| GPU | One instanced draw per context | — | Nothing to parallelize on the CPU |

**Text whose glyphs are not ready yet** appears when its glyphs arrive, normally one or two frames later; it is never drawn half-formed. Budgets in section 7 cap that delay.

**Processes:**
- **Game UI:** no benefit. Composing UI across processes adds latency and copies.
- **Content work:** atlas baking, font import and shader compiles can run as separate processes, as the game build already does. That isolates crashes in third-party parsers and uses every core.
- **The editor and play sessions** (section 6) are the real multi-process question. Running the game in its own process, as Godot does, isolates crashes and gives a clean reload; running it in the editor's process, as Unreal's Play In Editor does, is faster to start and shares memory. That decision is made with section 6.

**Engine-wide follow-up (not decided here):**
- A shared job system would serve every system above:
  - Jolt, through its `JobSystem` interface implemented in Raven's code, which leaves Jolt unmodified;
  - shaping and glyphs;
  - reload imports and asset decoding;
  - parallel game systems later (ECS D9).
- It is a separate decision for the owner, to be studied before more subsystems add threads of their own.
