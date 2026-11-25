# UI and audio libraries: research for C7 and C8

Status: **research for owner discussion (2026-11-25).** Decided so far: A1 (audio device layer), U1 (our own UI stack), U2 (text target D), U3 (Yoga plus our own containers), U4 (many languages) and U5 (ICU4C); see the decisions table. The owner asked whether UI and sound have libraries that would carry real weight the way Jolt did for physics, even at some integration cost. Versions, licenses and prices were checked on the projects' own pages on 2026-11-25. Recheck prices and license terms before adopting anything, because vendors change them.

Every library below would follow the [vendoring rule](../ThirdParty/JoltPhysics/README.md):
- vendored unmodified at a tagged release;
- built as a static library, with settings in `cmake/Raven<Lib>.cmake`;
- an update script that swaps the copy;
- never visible in public engine headers.

The hot-reload rule also applies: nothing the engine keeps may point into the game DLL, so no stored callbacks into game code.

## Short answer

| Area | Worth adopting like Jolt | Build ourselves | Not recommended |
| --- | --- | --- | --- |
| UI | **HarfBuzz** (text shaping), **FreeType** (font loading), **msdfgen + msdf-atlas-gen** as libraries (sharp text, runtime glyph atlases), **Yoga** (flexbox layout) | The immediate-mode API and its ID cache, widgets, focus and gamepad navigation, the quad and text renderer, IME, world-space labels | Full UI frameworks (RmlUi, NoesisGUI, Ultralight, Gameface, Dear ImGui) |
| Audio | **miniaudio** (devices on all three platforms, decoders, resampling, panning), with **stb_vorbis** for Ogg | The mixer: voices with IDs, priorities and stealing, buses, the listener, asset reload | OpenAL Soft (LGPL), SoLoud (stale), FMOD and Wwise (closed, fees per title) |
| Later | **Steam Audio** (HRTF, occlusion; Apache-2.0), SheenBidi (right-to-left text), ThorVG (vector icons), Rive (designer animation) | — | — |

In both areas, the heavy and risky parts already have excellent permissive libraries. Those parts are font parsing, shaping, device backends, decoding and resampling. The parts that touch our contracts (hot reload, ECS, the input stream, asset leases) are small enough to own. Full frameworks and middleware would take over those contracts, which is the opposite of how Jolt fits.

---

## UI

### Full frameworks

| Library | Latest | License and cost | Strengths | Conflicts with Raven |
| --- | --- | --- | --- | --- |
| **RmlUi** (HTML/CSS-like) | 6.3, 2026-08-22 | MIT | Active; renderer interface with sample Vulkan backends; shipped in The Thing: Remastered and Unvanquished | A retained DOM. Data bindings keep raw pointers and `std::function` callbacks into game memory and code, breaking the reload rule. Not built for thousands of world labels |
| **NoesisGUI** (XAML) | 4.0.1, 2026-10-21 | Proprietary. Indie €195 per project (revenue ≤ €100K); Pro €9,000 plus €3,200 first-year support | Mature, with designer tooling; shipped in Baldur's Gate 3 and Hytale | ViewModels are reflected C++ classes, so they would live in the game DLL. Recurring cost |
| **Ultralight** (WebKit) | 2.0 beta, 2026-10-06 | Free under $200K with a 60 FPS cap; $395–4,500 per title | HTML and JavaScript authoring | Heavy web stack, closed binaries at lower tiers, callback bindings |
| **Coherent Gameface** | Not public | Quoted per title and platform | AAA-proven | Price on request, lock-in |
| **Dear ImGui** | 1.93.0, 2026-10-14 | MIT | The tools standard | The owner rejected it, and an ImGui tools layer would later be replaced by the editor stack |
| **Nuklear** / **microui** | 4.13.4 / no tags | MIT / public domain | Tiny | Nothing beyond our planned stack; microui has no releases |

**Recommendation: keep FD-31's own stack.** No framework fits the immediate-mode-over-ID-cache model, the reload rule and an RTS's world-space volume together. RmlUi stays the fallback if HTML/CSS authoring ever becomes a goal.

### Text (the heavy lifting)

| Library | Latest | License | What it saves us |
| --- | --- | --- | --- |
| **HarfBuzz** | 15.2.0, 2026-11-19 | Old MIT (keep the notice) | Shaping: kerning, ligatures, combining marks, Indic and Arabic. It is years of specialist work and the industry standard. It builds as one amalgamated `harfbuzz.cc`. It does not do bidi, line breaking or font fallback |
| **FreeType** | 2.15.0, 2026-10-12 | FTL (BSD-style, credit in the documentation) | Parsing TTF/OTF files safely, plus hinted rasterization for small sizes. msdfgen needs it anyway |
| **msdfgen** | 1.13.1, 2026-11-11 | MIT | Multi-channel distance fields: sharp text at any size and zoom |
| **msdf-atlas-gen** | 1.4, 2026-03-05 | MIT | Atlas packing. As a library, `DynamicAtlas` adds glyphs at runtime, which large character sets such as Korean need. Its CMake defaults to vcpkg, so options must be set from our `cmake/` |

**Change from the current plan.** UIDesign.md uses msdf-atlas-gen only as an offline tool that bakes fixed atlases. Using these as libraries inside a font importer gives three things:
- one path for both baked Latin pages and on-demand glyphs;
- shaping from day one, so text layout is written once over shaped runs instead of being rewritten when HarfBuzz arrives;
- font reload through the reload service, like PNG files.

Korean alone has 11,172 precomposed syllables, so a fixed atlas does not scale.

**Text rendering approaches:**

| Approach | Strengths | Weaknesses |
| --- | --- | --- |
| MSDF / MTSDF (planned) | Sharp at any scale; one instanced path for HUD and world labels; MTSDF adds outlines and shadows | Soft below about 12–14 px, because it has no hinting |
| FreeType bitmaps | Crispest small text | One atlas per size; poor under scaling or perspective |
| Slug-style outlines | Exact at any size and in perspective | High per-pixel cost and a large implementation; the patent was reportedly dedicated to the public domain in March 2026, with MIT reference shaders |

**Recommendation:** MTSDF for everything now, measured at small sizes. FreeType's hinted bitmaps remain available as a second path if small text proves too soft. Decide that before the HUD's smallest size is fixed.

### U2 options in detail (owner question, 2026-11-25)

**The text pipeline.** Every option runs the same stages; they differ in which ones exist and who does them.

| Stage | What it does | Library |
| --- | --- | --- |
| 1. Load | Reads a TTF or OTF file: outlines, metrics, kerning and layout tables | FreeType (or stb_truetype) |
| 2. Itemize, bidi, line break | Splits text by script, direction and font, and finds legal break points | U5 (ICU or smaller libraries) |
| 3. Shape | Turns characters into positioned glyphs: Arabic joining forms, Indic reordering, ligatures, combining marks, kerning | HarfBuzz |
| 4. Rasterize | Turns a glyph outline into pixels: a bitmap at one size, or a distance field for all sizes | FreeType (bitmap) or msdfgen (distance field) |
| 5. Pack | Places glyph images into atlas pages, adding new ones at runtime | msdf-atlas-gen (or our own packer) |
| 6. Draw | One instanced quad per glyph (decision 4) | Raven |

**The options:**

| Option | How it works | Strengths | Weaknesses |
| --- | --- | --- | --- |
| **A. HarfBuzz + FreeType + msdfgen + msdf-atlas-gen at runtime, with atlases filled on demand** (recommended) | Fonts load as assets. Text is shaped by HarfBuzz; each new glyph's MTSDF is generated on a worker and added to an atlas page | Every script. One atlas serves every size, DPI scale and zoom, including world labels in perspective. MTSDF's fourth channel gives outlines, glow and shadows in the shader. Unhinted outlines match design tools. Fonts hot-reload | Four libraries to vendor and build. Generating a distance field costs more per glyph than a bitmap. Large atlases for CJK. Text below about 12–14 px can look soft |
| **B. Offline atlases only** (the current note) | A content tool bakes fixed atlases from a chosen character set; the runtime does no shaping | No runtime libraries; simplest | Fails U4: no shaping means wrong Arabic and Indic text, and fixed sets cannot hold CJK or player-typed text |
| **C. HarfBuzz + FreeType bitmaps** (Unreal Slate's and Godot's default model) | Glyphs are rasterized, usually hinted, at each size used, into atlases per size | The crispest small text. Cheap per glyph. Small atlases at small sizes. Color emoji work through FreeType | One cache per size and scale: every UI-scale or DPI change adds sizes. Blurry when scaled or in perspective, so it fails world labels. Hinting causes the design mismatch described in UIDesign.md |
| **D. A plus C's bitmaps as a second path** (Godot offers both per font) | MTSDF by default; hinted bitmaps for small UI sizes and color emoji | The best of A and C | Two glyph paths to maintain; decided by measurement |
| **E. GPU vector outlines** (Slug-style) | The shader evaluates glyph curves per pixel; no atlas images | Exact at any size and in perspective; no atlas memory | Several times the per-pixel cost and a large, specialist implementation; Slug's patent was reportedly dedicated to the public domain in March 2026 |
| **F. OS text engines** (DirectWrite, Core Text, Pango) | The OS shapes, falls back and rasterizes | OS-quality shaping, fallback and hinting | Different results on each OS, nothing on consoles, bitmaps only. Fails one engine behaving the same everywhere |

**Sub-choice for loading: FreeType or stb_truetype.** stb_truetype is one small file, but it does not cover variable fonts, color fonts, Type 1 or CFF2, and its own documentation warns against untrusted fonts. FreeType is hardened, does all of these, and msdfgen's font import is built on it.

**Compared** (A is the baseline; numbers are rough estimates to be measured in C7):

| Criterion | A. MTSDF runtime | B. Offline | C. Bitmaps | D. A + bitmaps | E. Vector |
| --- | --- | --- | --- | --- | --- |
| Every script (U4) | Yes | No | Yes | Yes | Yes, with HarfBuzz |
| CJK and player-typed text | Yes, on demand | No | Yes, on demand | Yes | Yes |
| Small text (under 14 px) | Softer; measure | Softer | **Best** | **Best** | Good |
| Scaling, DPI, world labels | **Best** | Good | Poor | **Best** | **Best** |
| Matches design tools | **Yes** (unhinted) | Yes | No (hinted) | Mostly | Yes |
| Cost per new glyph | About 0.1–1 ms on a worker | None at runtime | About 10–50 µs | Either | Curve preprocessing |
| Atlas memory, 2,350 Hangul glyphs | About 16 MB, one page for all sizes | — | About 1 MB per size | Both | No atlas |
| GPU cost per pixel | 1 sample + median | Same | 1 sample | Either | Several times higher |
| Libraries | 4 | 1 (tool) | 2 | 4 | 2 + our renderer |
| Color emoji | No (needs bitmaps) | No | Yes | Yes | Partly (COLR layers) |
| Used by | Godot (MSDF option), Unity TextMeshPro (single-channel SDF) | Many small engines | Unreal Slate, Godot default | Godot | Slug licensees |

**Recommendation: A now, built so D's bitmap path can be added.** Measure 10–14 px text on the laptop in C7. If it is too soft, add the bitmap path for small sizes, which color emoji would need anyway.

**D's weaknesses** (owner question, 2026-11-25). D uses the same libraries as A; its costs are in rules, memory and testing.

| Weakness | What goes wrong | Containment |
| --- | --- | --- |
| **Reflow at the switch size** | Hinted bitmaps can have different advance widths, so text crossing the threshold, for example under a UI-scale slider, wraps onto different lines | Both paths lay out with the same unhinted advances; only the pixels differ |
| **A visible pop** | Hinted and unhinted glyphs differ in weight and shape, so text animating through the threshold changes look | The path is chosen per text run from its final size and stays fixed while it animates. Light, vertical-only hinting keeps the shapes close |
| **The design mismatch returns** | Full hinting moves outlines away from the designer's, the Unreal problem again | Light hinting only, and the design overlay compares both paths |
| **A cache per size** | Every font size times UI scale times DPI makes a new set of bitmaps, which costs memory and generation time | Sizes are rounded to quarter pixels on the bitmap path; its pages are evicted least-recently-used under a budget |
| **Two atlas kinds** | MTSDF pages (RGBA) and bitmap pages (single channel, or color for emoji) need separate allocation and budgets | One packer and one page manager, with a format per page |
| **Twice the testing** | Every script and size is checked on both paths, and a bug can live in only one | The preview (layer 1) shows a run on both paths side by side |
| **Choosing the path** | World labels in perspective blur as bitmaps even when small | The threshold applies to screen-space UI only; world-space text is always MTSDF |

**Added cost:** about a fifth more text-renderer code (bitmap rasterization, a second page format, a shader branch), plus the rules above. Color emoji also need an emoji font, such as Noto Color Emoji, which is about 10 MB.

### Layout

| Option | Latest | License | Strengths | Weaknesses |
| --- | --- | --- | --- | --- |
| **Yoga** (Meta) | 3.2.1, 2024-12-13; main active | MIT | Spec-conformant flexbox (React Native's); incremental relayout through dirty flags, which suits large editor trees; well backed | Retained node tree: we keep one node per element ID in our cache. No tag for a year, so we pin 3.2.1 |
| **Clay** | 0.14, 2025-06-06 | zlib | Almost exactly our model: per-frame declaration, hashed IDs, render-command output; tiny, no allocation | 0.x API with breaking changes; mostly one maintainer; it keeps slices of the caller's strings, so game strings must be copied first |
| **Our own** | — | — | A few hundred lines covers anchors and stacks for HUD and menus | The editor later needs flexbox-level layout, which would replace it (no-throwaway rule) |

**Recommendation: Yoga**, behind our immediate API. Its measure callback is an engine function (text measurement), so nothing points into the game DLL.

### U3 options in detail (2026-11-25)

**Requirements since U1–U4:**
- right-to-left layout for Arabic and Hebrew (U4);
- the containers a UMG-style designer will offer (decision 2);
- layout on a worker thread (UIDesign.md section 8);
- editor-scale trees later.

| Option | Strengths | Weaknesses |
| --- | --- | --- |
| **A. Yoga 3.2.1** (Meta, MIT) | Spec-conformant flexbox: grow, shrink, wrap, min and max sizes, absolute positioning. **Right-to-left built in:** start and end flip with the layout direction. Incremental relayout through dirty flags. Text is measured through an engine callback. **Used by** React Native and Unity UI Toolkit, whose layout engine is Yoga | No CSS grid. A retained node tree, so the cache keeps one Yoga node per element ID. No release tag for a year (we pin 3.2.1). One tree is single-threaded, which is fine because each context lays out on one worker |
| **B. Clay 0.14** (zlib) | Exactly our per-frame model, very fast, no allocation | A 0.x API with breaking changes and mostly one maintainer. **No right-to-left support found**, and adding it would mean editing third-party code, which is banned. It hashes its own IDs beside ours, and keeps slices of the caller's strings |
| **C. Our own layout** | Full control, including grids | Flexbox's edge cases (grow and shrink order, wrap, min and max, baselines) take years to get right; we would rebuild Yoga, worse |
| **D. Yoga plus our own containers** (**decided 2026-11-25, owner**) | Yoga does flex layout. Our own containers sit in the tree as Yoga leaves with a measure function: a grid with row and column spans, a virtualized list that lays out only visible rows, and a scroll box. Right-to-left comes from Yoga, and our containers honor the same direction | Our grid and list must follow the same direction and sizing rules as Yoga |
| **E. Taffy** (Rust; flexbox plus CSS grid; used by Bevy and Zed) | The only option with grid built in | Rust: a second toolchain and a foreign-function boundary in a C++ engine |

**How UMG's panels map onto D**, so the later designer can offer familiar containers:

| UMG panel | In D |
| --- | --- |
| Horizontal Box, Vertical Box | A Yoga row or column |
| Wrap Box | A Yoga row with wrap |
| Overlay | Absolutely positioned children that fill the parent |
| Canvas Panel (anchors and offsets) | Absolute position with percentage insets |
| Size Box | Fixed, minimum and maximum sizes |
| Scale Box | Our own container that scales its content |
| Grid Panel, Uniform Grid Panel | Our own grid container |
| Scroll Box, List View | Our own scroll box and virtualized list |

### U5 options in detail (2026-11-25)

Shaping (HarfBuzz, U2) turns one run of one script, one direction and one font into glyphs. Everything around it is Unicode text processing:
- **Bidirectional order (UAX #9):** splitting a mixed Arabic and English line into runs and reordering them for display.
- **Script runs (UAX #24):** HarfBuzz needs one script per run.
- **Line breaking (UAX #14):** where a line may wrap. Thai, Lao, Khmer and Burmese write words without spaces; UAX #14 leaves those to a dictionary or a model.
- **Grapheme clusters (UAX #29):** what a caret step, a backspace or a selection covers (Korean jamo, emoji with modifiers, Indic conjuncts).
- **Normalization:** matching typed text against stored text.
- **For string tables:** plural categories per language (CLDR rules: English has two forms, Arabic six, Korean one), number formatting per locale, and message formatting.

Font fallback (which font covers each run) is ours in every option: no library below does it for a game engine.

| Option | Strengths | Weaknesses |
| --- | --- | --- |
| **A. ICU4C 79.1** (Unicode Consortium, Unicode License; released 2026-10-22) | Everything above from one source, kept current with each Unicode version. Dictionary breaking for Thai, Lao, Khmer and Burmese, and Chinese and Japanese word breaking. `i18n` adds CLDR plural rules, number and date formatting, collation and MessageFormat. **Used by** Unreal (all text and localization), Godot (TextServerAdvanced: bidi, breaks and scripts), Chrome, Firefox and Android | Large. No CMake build upstream (autotools and Visual Studio projects), so `cmake/RavenICU.cmake` must list its sources. Locale data is a separate file built by ICU's data tool with a filter; full data is about 30 MB. Unreal ships presets instead: English 1.77 MB, EFIGS 2.38 MB, CJK 5.16 MB, all languages 15.3 MB (UE 4.26 docs). Godot carries two small compiler patches on ICU, which our rule forbids, so an unmodified build must be proven first |
| **B. Small libraries:** SheenBidi (Apache-2.0; bidi and script runs) plus libunibreak 8.0.1 (zlib; line, word and grapheme breaks, Unicode 17) | Small, plain C, CMake builds, no data file. Fast | **No dictionary breaking:** Thai, Lao, Khmer and Burmese lines break only at spaces, so a Thai sentence cannot wrap. No plural rules, number formatting or normalization. Two sources to track, and the dictionary scripts would later need ICU anyway, which would replace these (no-throwaway rule) |
| **C. ICU4X 2.x** (Unicode Consortium; Rust with generated C++ headers) | Modern, small data, line breaking for the dictionary scripts by dictionary or a small model; plurals and formatting | Rust: a second toolchain and a language boundary, the reason Taffy lost in U3 |
| **D. Operating-system text services** (DirectWrite, Core Text, ICU on Linux) | Nothing to vendor | Different results on each platform, against the cross-platform goal and determinism of layout; consoles later have none of these |
| **E. Our own** | Full control | Line breaking, bidi and segmentation are large, versioned Unicode specifications; we would rebuild ICU, worse |

**Decided (owner, 2026-11-25): A, ICU4C, vendored whole and unmodified, built in two parts.**
- **C7 builds only `common`:** bidi, script runs, line, word and grapheme breaks with the dictionaries, and normalization. This is Godot's choice.
- **String tables (C9 part 2 or later) add `i18n`:** plural rules and number formatting. The message syntax over them is decided with the string-table design. ICU's MessageFormat 2 implementation was a technical preview in ICU 76, so its status must be rechecked then.
- **Data:** a filter file in `cmake/` (a setting, not an edit) chooses the languages, and `scripts/UpdateICU.cmake` rebuilds the data file with ICU's own tool when ICU is swapped. Each game picks a language set the way Unreal's packaging presets do. Development builds keep every language.
- **Condition before adoption:** ICU 79.1 builds unmodified with MSVC and Clang in C++23 under our warnings-as-errors flags, compiled as ICU's own C++ settings (warnings off for vendored code, as for Jolt and miniaudio). If it needs a source change, we stop and decide again.
- **Seam:** only `Private/Text/` calls ICU. Public headers never show ICU types.
- **Threads:** ICU break iterators and bidi objects are per-thread objects over shared read-only data, so each shaping worker owns its own (UIDesign.md section 8).

### Built ourselves either way

- the declaration API and the ID-keyed cache (UIDesign.md sections 1 and 2);
- widgets, focus, gamepad navigation, key-binding capture;
- the instanced quad renderer and MTSDF atlas pages in the bindless array;
- IME (Win32 IMM32/TSF, later macOS and Linux), which no library solves portably;
- world-space bars and labels, batched outside layout.

---

## Audio

### Device layers and engines

| Library | Latest | License and cost | Backends | Verdict |
| --- | --- | --- | --- | --- |
| **miniaudio** | 0.11.26, 2026-10-31 | Public domain or MIT-0 | WASAPI, Core Audio, ALSA, PulseAudio, JACK and more. PipeWire through its Pulse layer today; native PipeWire planned for 0.12 | **Adopt.** One C file plus header, static library, no Linux link dependencies; used by raylib and ezEngine. Built-in WAV, FLAC and MP3 decoders, resampler, panner, spatializer, filters |
| SDL3 audio + SDL3_mixer | 3.4.20 / 3.2.4 | zlib | Everything, including native PipeWire | Solid, but a second platform layer beside ours; the mixer API was just rewritten |
| OpenAL Soft | 1.25.2 | LGPL-2.0+ | Everything | LGPL forbids our static vendoring; its global API would replace our mixer |
| SoLoud | Last tag 2020 | zlib | Several | Good features, stale |
| FMOD | 2.03.13 | Closed. Free under $200K revenue, then $2,000–18,000 per title | All | Best designer workflow, but binary-only, fees per title, and it replaces our mixer |
| Wwise | 2025.1 | Closed. Free for budgets ≤ $250K, then per title and platform | All | AAA standard; same conflicts as FMOD, heavier licensing |
| PortAudio, libsoundio, LabSound, FAudio, cubeb | — | Permissive | Various | Redundant with miniaudio, dormant, or fetching dependencies at configure time |

### Our mixer, or miniaudio's engine?

miniaudio has an optional high-level engine (`ma_engine`): groups, 3D attenuation and panning, streaming and an async resource manager.

| Option | Strengths | Weaknesses |
| --- | --- | --- |
| **A. Use `ma_engine` as the mixer** | Fastest route to groups, 3D and streaming; tested mixing | No voice IDs, priorities, voice limits or stealing, which an RTS needs, so we would build them on top anyway. Its resource manager would be a second asset cache beside AssetStore, complicating reload. Its end callbacks invite game callbacks |
| **B. Our mixer over miniaudio's parts (recommended)** | We own voices with generation-checked IDs, priority and stop-oldest stealing, virtual voices, buses, the listener, a command queue from fixed steps, and buffers as AssetStore leases with safe retirement on reload. miniaudio keeps the hard parts: devices and device switching, decoding, resampling and panning. About 1–2k lines of our code | We write and test the mixing loop ourselves |

B also follows the no-throwaway rule: later voice priorities and Steam Audio effects slot into our voice loop instead of fighting `ma_engine`.

### Formats

- **Now:** WAV, FLAC and MP3 come inside miniaudio. Ogg Vorbis comes through **stb_vorbis** (1.25, 2026-11-19, public domain or MIT), a single file.
- **Later, if a game needs it:** Opus through libopus, libogg and opusfile (BSD, three libraries).

### How other engines do audio

Checked on 2026-11-25 from official documentation and source repositories. Points marked *reported* come from a single forum post or article.

| Engine | Mixer | Device output | Voice management | Middleware | Alignment with Raven's plan |
| --- | --- | --- | --- | --- | --- |
| **Unreal 5** | Its own Audio Mixer, one renderer for every platform. An audio thread decides what plays and a render thread mixes | Windows: XAudio2, moving to WASAPI in 5.8 (*reported*). Linux: SDL (SDL3 in 5.7). Mac: AudioUnit | The most complete: per-group concurrency limits (stop oldest, farthest, quietest or lowest priority), virtualization modes per sound | FMOD/Wwise plugins common in AAA; the built-in path is rich (MetaSounds, Quartz, submixes) | **High in shape.** It moved from per-platform voices to an owned mixer, as we plan. Its voice manager is the reference for ours |
| **Unity 6** | Built on FMOD Core. 6.3 added the Scriptable Audio Pipeline: processors split into a control part and a real-time part, with lock-free value messages | Through FMOD | Real/virtual voice limits; the loudest voices play each frame | FMOD/Wwise often replace the built-in path | **Medium.** The new pipeline matches our reload rule closely (handles, values instead of callbacks), but the base is closed middleware |
| **Godot 4** | Its own AudioServer: buses with effect chains | WASAPI, XAudio2, CoreAudio, ALSA, PulseAudio; no native PipeWire | Per-player polyphony only; no global stealing or virtual voices | Community FMOD extension | **Medium-high.** The same shape as ours (owned mixer, thin drivers), with weaker voice management than we need |
| **O3DE** | Translation layer over Wwise, plus a separate MiniAudio gem | Wwise's, or miniaudio's | Wwise's; the gem is basic | Middleware first | **Medium.** The gem shows ECS playback and listener components on miniaudio, but it uses miniaudio's own engine |
| **CryEngine 5** | Translation layer with swappable Wwise, FMOD, ADX2 or SDL Mixer | Middleware's | Middleware's | Middleware first; game triggers survive a middleware swap | **Low** for the mixer. Its idea of games naming *triggers*, not mixer internals, is worth keeping |
| **Bevy** | Built-in on rodio/cpal; the community replaces it with Kira or Firewheel/bevy_seedling | cpal: WASAPI, CoreAudio, ALSA | Minimal built in | — | **Medium.** bevy_seedling's ECS sampler pools are close to ours. Its review called rodio unsuitable for games (allocation and blocking on the audio thread) |
| **Fyrox, Defold** | Their own mixers and buses (Fyrox has HRTF) | Thin per-OS drivers | Basic | — | **Medium-high** in shape, smaller in scope |
| **raylib** | Its own simple mixing on miniaudio's device layer | miniaudio | None | — | **High** for the miniaudio split itself (device layer only, our own mixing) |
| **Flax, Stride, Wicked** | Thin wrappers over backend voices (XAudio2, OpenAL, AVAudioEngine) | Per-OS | Backend-level or none | — | **Low.** The per-platform-voice design Unreal abandoned; the smallest feature sets |
| **ezEngine** | No built-in system; an FMOD plugin | FMOD's | FMOD's | FMOD only | **Low** |
| **Source 2** | Its own mixer driven by data-defined sound events; Steam Audio for spatial audio | Its own | Defined per event | In-house | **High in direction:** owned mixer, Steam Audio, and sound events as data |
| **id Tech 7** (Doom Eternal) | Wwise | Wwise's | Wwise's | Middleware | **Low** |

**Where Raven's plan sits.** It is the mainstream owned-mixer design of Unreal, Godot and Source 2, with miniaudio as a thin device layer, as raylib uses it. No engine we checked found an owned mixer to be a mistake. Engines with thin built-in paths (Unity's built-in path, Bevy, CryEngine's SDL Mixer path) get replaced by middleware or community libraries, so the built-in path has to be good enough not to be replaced.

**What the others treat as essential and our plan lacks or defers:**

| Feature | Who has it | For Raven |
| --- | --- | --- |
| **Concurrency groups:** a cap per sound or group, with a rule (stop oldest, farthest, quietest, lowest priority, or refuse the new one) | Unreal | One global stop-oldest rule fails an RTS (200 rifles) and even an action game (footsteps crowding out a boss). Needed in C8 |
| **Audibility ranking** for which voices are real and which are virtual | Unity | Priority alone ignores distance: rank by priority times attenuated volume |
| **Fades on steal and virtualize** | Unreal | Prevents clicks; needed from day one |
| **Sound events as data:** random variations, pitch and volume ranges, concurrency settings | Source 2, Unreal cues, Unity random containers, CryEngine triggers | Systems play an *event asset*, not a raw buffer. Designers tune it, it hot-reloads, and a later middleware could implement the same events |
| **Bus effects and ducking:** reverb sends, EQ, dialogue lowering music | Unreal, Godot, Wicked | Our buses need effect slots and at least one bus-to-bus duck |
| **Sample-accurate start times** | Unreal Quartz, Kira | Defer, but give the command an optional start time in samples now |
| **Occlusion** | Unreal traces, Steam Audio | Give each voice a low-pass parameter now, so Jolt ray casts can drive it before Steam Audio arrives |
| **Virtualization per sound** (drop one-shots; seek-resume loops) | Unreal | Looping ambience must resume at the right point |
| **Device changes and streaming music** | Unreal's platform layer | Handle default-device changes and stream music from day one |

**Pitfalls they hit:**
- **Device-layer changes are risky.** Unreal's XAudio2-to-WASAPI switch brought reports of pops, silence and worse output in packaged builds (*reported*). Keep the device layer thin, and test device start, sample-rate changes and default-device changes on every OS.
- **Linux is fragmented** (ALSA, PulseAudio, PipeWire). Unreal uses SDL; Godot reports silence and crackling on PipeWire through its Pulse layer. miniaudio puts us where Godot is until its native PipeWire backend; plan for latency tuning in M4.
- **Real-time safety.** The mixer thread must never allocate, lock or touch the asset store (Bevy's lesson with rodio).
- **Virtualization bugs** around loops in Unreal are still reported in 5.7. Test loops that go virtual and come back.

### Is miniaudio enough? (owner question, 2026-11-25)

**How close Raven is to raylib.** Only the bottom layer matches: both use miniaudio's device and mix their own sound. Above that, raylib's `raudio` is the design to avoid.

| | raylib `raudio` | Raven's plan |
| --- | --- | --- |
| Mix callback | Takes a global mutex on every callback | Lock-free; it reads only a command queue |
| Music streaming | `UpdateMusicStream` is called from the game frame, so a long frame starves the music | Workers decode into ring buffers. A hitch, a scene load or a module rebuild never touches the stream |
| Voices | Sounds and aliases with no IDs, limits or stealing | Generation-checked IDs, concurrency groups, virtual voices |
| Buses, effects | Master volume and per-stream processors | Buses, a duck, effect slots |
| Reload | None | Buffers are AssetStore leases, retired only after the audio thread lets go |

So the comparison with raylib says nothing about whether miniaudio is enough for us. raylib's audio is limited by its own mixer, not by miniaudio.

**What miniaudio has to do for us:**
- open the device and deliver a float buffer callback;
- report device changes;
- decode WAV, FLAC and MP3, and resample.

Everything that grows later is in our mixer:
- concurrency groups, buses and effects;
- Steam Audio;
- occlusion;
- sample-accurate timing;
- streaming.

**miniaudio's ceiling is therefore not the engine's ceiling.** That is the structural reason it is enough.

**Assessed on our criteria:**

| Criterion | Assessment |
| --- | --- |
| **Fits our rules** | Yes. It is configured entirely by `MA_*` defines in our own source file (`MA_NO_ENGINE`, `MA_NO_NODE_GRAPH`, `MA_NO_RESOURCE_MANAGER`), so the vendored file stays unmodified and updates are a swap, like Jolt. Linux audio libraries are loaded at runtime, so there are no link dependencies |
| **Extensible without patching** | Yes. 0.11 already has a custom-backend hook (`ma_backend_custom`), so a console or other output can be added without editing the file. 0.12 plans pluggable backends outside the header, with native PipeWire as the first of them ([discussion 711](https://github.com/mackron/miniaudio/discussions/711)) |
| **Windows and macOS** | Mature: WASAPI and Core Audio, with automatic routing when the default device changes |
| **Device-change edge cases** | **The real weakness.** Open reports cover WASAPI rerouting after sleep ([#717](https://github.com/mackron/miniaudio/issues/717)), a crash after repeated plug and unplug ([#758](https://github.com/mackron/miniaudio/issues/758)), an assertion when a device is started from its own notification ([#777](https://github.com/mackron/miniaudio/issues/777)), and raylib failing to switch devices ([raylib #3743](https://github.com/raysan5/raylib/issues/3743)) |
| **Linux** | PulseAudio, which PipeWire systems run through its Pulse layer, plus ALSA and JACK. Native PipeWire arrives in 0.12 as an extra that needs the PipeWire development packages. Medium confidence until it is tested in M4 |
| **DSP quality** | The built-in resampler is linear with a low-pass filter. That is acceptable for games, but sound events with wide pitch ranges may alias. The resampler is a leaf call, so our own windowed-sinc resampler can replace it if listening shows a problem |
| **Maintenance** | One main maintainer since 2016. Releases are steady, and 0.12 changes the file split (`miniaudio.c` plus a header) and some APIs. It is public domain, so a fork is always possible |
| **Missing, and not its job** | Operating-system object audio (Windows Sonic, Dolby Atmos) and console outputs under NDA. Unreal adds both through plugins, which would be a separate native path for us too |

**Alternatives if it fails us:**

| Alternative | Strengths | Weaknesses |
| --- | --- | --- |
| **SDL3 audio only** | Large team backed by Valve. Native PipeWire. Logical devices that move with the default device. Console ports exist under NDA. Unreal uses it on Linux | We would build all of SDL as a second platform layer beside ours. Its only decoder is WAV, so the decoders would still come from mackron's dr_libs |
| **Our own backends** (WASAPI, Core Audio, PipeWire) | Full control and no dependency | About 1.5–3k lines per OS, plus every edge case above, written ourselves |

**Verdict:** miniaudio is good enough as the device layer and decoders, under conditions that keep the cost of replacing it to one file:
1. **Parts used:** only the context, the device, the decoders and the resampler and filters. The engine, node graph and resource manager are compiled out.
2. **One file per use:** every miniaudio device call is in one backend file behind an engine `AudioDevice` interface, and the format conversion of clips is in the clip importer. No public header mentions miniaudio. Moving to SDL3 or a native backend then rewrites the backend file only; moving to 0.12 touches those two files.
3. **We own the device lifecycle:**
   - A device notification only posts a message. A control thread closes and reopens the device, never from the callback (#777).
   - Mixer state survives device loss.
   - With no device, a silent output keeps the game running, and the control thread retries.
4. **A real-time-safe mixer:** no lock or allocation in the callback, and streaming fed from workers (the raylib lesson above).
5. **Device tests on every OS:** unplug, default change, sleep and wake, sample-rate change, and a Bluetooth headset (Windows now, Linux and macOS in M4).
6. **A fallback:** if maintenance stalls or the device issues persist, SDL3's audio subsystem is the replacement behind the same interface.

### miniaudio or SDL3 audio? (owner question, 2026-11-25)

**How SDL3 audio works.**
- **Logical devices:** an application opens logical devices that SDL binds to physical ones. A logical device opened on the system default moves to the new default automatically ([migration guide](https://wiki.libsdl.org/SDL3/README-migration)).
- **Data path:** the application feeds an `SDL_AudioStream`. SDL's own device thread pulls from the stream, converts the format, mixes every stream bound to the device, and writes to the OS. With the get-callback form, SDL calls us on that thread with the stream lock held, and we put data into the stream ([`SDL_AudioStreamCallback`](https://wiki.libsdl.org/SDL3/SDL_AudioStreamCallback)). Putting data copies it into blocks the stream allocates ([`SDL_PutAudioStreamData`](https://wiki.libsdl.org/SDL3/SDL_PutAudioStreamData)).
- **Buffer size:** SDL picks it. A hint can request a size, but it is not guaranteed ([SDL #12318](https://github.com/libsdl-org/SDL/issues/12318)).
- **Linux:** native PipeWire, preferred over PulseAudio since 3.1.2 ([Phoronix](https://Phoronix.com/news/SDL-3.1.2-Released)).
- **Resampler:** band-limited, with SIMD ([source](https://skia.googlesource.com/external/github.com/libsdl-org/SDL/+/f5ed7160bfbde413fc36e9aeeae8f6065a1f2f8c/src/audio/SDL_audioresample.c)).
- **Decoders:** only WAV. SDL3_mixer bundles dr_wav, dr_flac, dr_mp3 and stb_vorbis, the same decoders miniaudio has, but it is a mixer we would not use.
- **Precedent for one subsystem only:** Godot 4.5 takes only controller input from SDL3, not windowing or sound ([Godot docs](https://docs.godotengine.org/en/4.5/tutorials/inputs/controllers_gamepads_joysticks.html)). An SDL subsystem beside our own platform layer is an established pattern.

**The options:**

| Option | Strengths | Weaknesses |
| --- | --- | --- |
| **A. miniaudio for the device and decoders** | Two vendored files; nothing to install on any OS; one translation unit to build. A direct callback into our buffer: no extra copy and no lock that is not ours. Full control: period size, a low-latency profile, WASAPI exclusive mode. Decoders, resampler and filters included | Device-change edge cases (open issues #717, #758, #777), which we handle ourselves. Linux PipeWire only through its Pulse layer until 0.12. One main maintainer. No console outputs (a custom backend would be ours to write) |
| **B. SDL3 for the device, with dr_libs and stb_vorbis for decoding** | A large team backed by Valve, the widest shipped use (Steam, Proton, Unreal on Linux). Robust automatic device moves. Native PipeWire now. Console ports exist under NDA. The same library could later serve gamepads (as in Godot 4.5) | A large vendored tree built through its CMake, with every unused subsystem switched off by cache variables. Linux builds need the PipeWire, PulseAudio and ALSA development headers (the libraries load at runtime). Global `SDL_Init` state beside our platform layer. Device events arrive through SDL's event queue. SDL owns a lock and block allocations on the audio path. Less control over latency. Its resampler would duplicate ours |
| **C. SDL3 for the device, with miniaudio compiled for decoding only** (`MA_NO_DEVICE_IO`) | B's device strengths, plus miniaudio's decoders, resampler and filters from a single file | B's integration cost, plus a second vendored audio library |

**Cost:**

| | A | B and C |
| --- | --- | --- |
| Vendoring | One header plus one source file | SDL's source tree (hundreds of files), plus decoders |
| Build | One file, a few seconds | A static SDL3 with audio and events only. Size and build time are unmeasured; we measure them after the download if chosen |
| Linux and macOS setup | Nothing | Development headers on Linux |
| Our device code | About 300–500 lines: device, reopen thread, notifications | About 200–300 lines; moving to the new default is built in |
| Testing | Our reopen path on every OS (condition 5) | The same tests, but most cases are handled by SDL |

**Performance.** The device layer is not where the audio time goes in either option: mixing, decoding and later Steam Audio are ours in every option. A 10 ms stereo float buffer is 3.75 KB, so SDL's extra copy and uncontended lock cost microseconds. This is not measured; condition 5's device tests would include a callback-timing check.

Where they really differ:
- **Latency control:** A sets the buffer size and can use exclusive mode. B and C take the size SDL chooses, and the hint can be ignored.
- **Real-time behavior:** A keeps everything on the audio thread under our rules. B and C add SDL's lock and block allocations, normally uncontended and from pools, but not under our control.

**Strengths compared:**

| Concern | Better | Why |
| --- | --- | --- |
| Device changes and Bluetooth | **SDL3** | A larger test base, and automatic moves that are part of its design |
| Linux | **SDL3** | Native PipeWire today |
| Consoles (beyond v1) | **SDL3** | Existing ports under NDA, versus our own backend |
| Bus factor | **SDL3** | A team, versus one maintainer |
| Build, footprint, setup | **miniaudio** | Two files, no headers, a few seconds |
| Control and real-time guarantees | **miniaudio** | Our buffer size, our thread rules, exclusive mode |
| Decoders and DSP | **miniaudio** | Built in. SDL needs the same decoders from elsewhere |
| Fit with our own platform layer | **miniaudio** | No global initialization, no second event queue |
| Future gamepads | **SDL3** | Its gamepad layer (controller database, PlayStation and Switch drivers, rumble, gyro) is the likely choice, and audio would then cost little extra |

**Recommendation: A, miniaudio, under the six conditions above.** Re-check the choice when gamepads are planned.
- **Why not SDL3 now:**
  - Its main advantages are robustness on devices we have not tested yet (Linux PipeWire, Bluetooth) and consoles, which are outside v1.
  - Its costs fall on every build now: the vendored tree, Linux development headers, global initialization, and less control on the audio thread.
- **Why the choice is cheap to revisit:**
  - Condition 2 puts the device in one file, so moving to SDL3 replaces only that file. The decoders, resampler and filters stay, exactly as in option C.
  - The risk is 300–500 lines of device code. That code is replaced only if two things both happen: gamepads bring SDL3 in, and our device tests show problems.
- **When to switch to C instead:**
  - when gamepad support is planned through SDL3 (decided with that feature);
  - when the condition 5 device tests fail in ways we cannot fix in our reopen path;
  - when Linux PipeWire testing in M4 shows crackling or silence through the Pulse layer.

### Hiding the device layer (owner question, 2026-11-25)

Yes. The device layer can sit behind one private engine interface, so miniaudio and SDL3 become two interchangeable backends. CMake picks the backend, as it picks OS backends (`RAVEN_AUDIO_BACKEND=miniaudio|sdl3`). Games and public headers never see either library.

**Sketch** (a design shape, not code to apply):

```cpp
// Private/Audio/AudioDevice.hpp: engine-internal, no library headers.
struct AudioFormat { std::uint32_t SampleRate; std::uint32_t Channels; }; // always float32, interleaved

// Called on the backend's audio thread. Must not lock, allocate or throw.
using AudioRenderFn = void (*)(void* user, float* out, std::uint32_t frames) noexcept;

enum class AudioDeviceEvent : std::uint8_t { DefaultChanged, Lost, ListChanged };

struct AudioDeviceCaps
{
	bool FollowsDefault;  // the backend moves to a new default device itself (SDL3; miniaudio on WASAPI and Core Audio)
	bool Exclusive;       // exclusive mode is available (miniaudio on WASAPI)
};

class AudioDevice
{
public:
	virtual ~AudioDevice() = default;
	// format: the mixer's fixed format (48 kHz stereo). The backend converts to the device's.
	// preferredFrames is a hint; the callback may ask for any number of frames.
	virtual bool Open(std::optional<AudioDeviceId> device, AudioFormat format, std::uint32_t preferredFrames,
		AudioRenderFn render, void* user) = 0;
	virtual void Close() = 0; // no callback runs after Close returns
	virtual AudioDeviceCaps GetCaps() const = 0;
	virtual void DrainEvents(std::vector<AudioDeviceEvent>& out) = 0; // called by the audio control thread
	virtual void Enumerate(std::vector<AudioDeviceInfo>& out) = 0;
};
std::unique_ptr<AudioDevice> CreateAudioDevice(); // defined by the selected backend file
std::unique_ptr<AudioDevice> CreateNullAudioDevice(); // always built
```

**Four rules make both libraries fit:**
1. **A fixed mixer format.** The mixer always renders 48 kHz stereo float, and the backend converts to whatever the device uses (both libraries convert). A device reopened at another sample rate never disturbs voices, positions or sample-time start commands.
2. **The callback may ask for any frame count.** SDL3 asks for a varying amount; miniaudio usually asks for one fixed period. The mixer renders fixed internal blocks of 256 frames into a small ring, and the callback copies from that ring. This also fixes effect and scheduling timing independently of the device.
3. **Events only post.** Backends push device events into a queue, and the audio control thread drains it and reopens the device if needed. A backend with `FollowsDefault` does nothing on a default-device change. The callback never starts or stops a device (miniaudio #777).
4. **The backend owns library state.** For miniaudio that is the context. For SDL3 it is `SDL_InitSubSystem(SDL_INIT_AUDIO)`, which is reference-counted, so a later SDL gamepad backend can share it.

**The null device** is always built. It calls the render function from a timer thread at the mixer rate. It serves three purposes:
- the silent fallback when no device exists (condition 3);
- CPU tests that run the mixer without audio hardware in CI;
- headless runs.

**What the interface does not hide:**

| Leak | How it is handled |
| --- | --- |
| **Latency control.** miniaudio honours the buffer size and offers exclusive mode; SDL3 may ignore the size | `preferredFrames` is only a hint, and exclusive mode is an optional capability. Nothing above the device relies on either |
| **Decoders are a separate choice** | A separate seam inside the audio importer, not part of the device. miniaudio's decoders stay under both backends (option C) |
| **Build and setup cost** | An interface does not remove SDL3's source tree or the Linux development headers; it only makes the switch a small job |
| **Where SDL3's device events arrive** | SDL delivers device add and remove events through its event queue. Before an SDL backend is written, check whether that queue can be drained off the main thread; if not, poll the device list on the control thread |
| **Platform-specific quirks** (Bluetooth profiles, PipeWire latency) | Handled inside each backend, and covered by the condition 5 device tests run against each backend |

**Cost:**
- the interface;
- one backend file (miniaudio, about 300–500 lines with the reopen path);
- the null device, about 100 lines.

Nothing is thrown away: the interface, the null device and the mixer stay whichever backend runs. Switching to SDL3 later means one new backend file and a CMake option. Both backends need not be built at once. CI builds the selected one.

**What this changes for A1:** the important decision becomes the interface and its four rules. The first backend is a low-stakes choice that can be reversed later.

**Decided and built (2026-11-25).** The owner chose the interface with miniaudio as the first backend. The built interface is narrower than the sketch: no capabilities, no device listing or selection, and two events, `Lost` and `Moved`. Those parts are added when a settings screen or a second backend needs them. Record: [GameServicesDesign.md](GameServicesDesign.md#c8-first-part--the-audio-device-layer-2026-11-25).

### Later: spatial audio

**Steam Audio** 4.8.1 (Apache-2.0 since 4.5.2): HRTF, occlusion, reflections and pathing through a C API. It plugs into our mixer as a per-voice effect, and was used in Half-Life: Alyx. Its build dependencies must be checked before vendoring.

---

## Risks

- **miniaudio has one main maintainer.** It is public domain, so a fork is always possible. Version 0.12 will change the file split and some APIs; we pin 0.11.26 and swap on upgrade, keeping every call in one file. Device-change handling has open issues, so we own the reopen logic (see [Is miniaudio enough?](#is-miniaudio-enough-owner-question-2026-11-25)).
- **Linux audio** runs through PipeWire's Pulse layer until miniaudio 0.12; this needs testing on Linux (M4).
- **The audio thread:** decoding and streaming stay on our workers, the mix callback stays lock-free, and buffers retired by a reload are freed only after the audio thread lets go.
- **Untrusted files:** stb_vorbis and font parsers have had memory-safety bugs; fuzz them if mods ever arrive.
- **License notices:** FreeType's credit clause and HarfBuzz's notice need a third-party notices file shipped with games, next to Jolt's license.
- **Release cadence:** HarfBuzz releases every few weeks (pin and update deliberately); Yoga has had no tag for a year.
- **Build files:** msdf-atlas-gen's CMake defaults to vcpkg. We confirm it configures from outside without patching before vendoring it.
- **Small text:** MTSDF may be too soft at small sizes, forcing a second text path; measure early.

## Decisions for the owner

To settle one at a time:

| # | Decision | Recommendation |
| --- | --- | --- |
| U1 | **Decided 2026-11-25 (owner): "Let's build own stack".** A full UI framework, or our own stack (FD-31) | Our own stack, with libraries for text and layout (U2, U3, U5) |
| U2 | **Decided 2026-11-25 (owner): option D as the target, "since it promises the best outcome".** Text libraries: HarfBuzz, FreeType, msdfgen and msdf-atlas-gen as runtime libraries, or only the offline atlas tool | D: all four at runtime, with atlases filled on demand. MTSDF by default, plus a hinted-bitmap path for small screen text and color emoji, under the containment rules in [D's weaknesses](#u2-options-in-detail-owner-question-2026-11-25). The format, glyph keys and pages carry both paths from the start. MTSDF is built first in C7, and the bitmap path follows the small-text measurement or a game's need for emoji |
| U3 | **Decided 2026-11-25 (owner): option D.** Layout: Yoga, Clay, our own, Yoga plus our own containers, or Taffy ([U3 options in detail](#u3-options-in-detail-2026-11-25)) | D: Yoga 3.2.1 for flex layout, plus our own grid, virtualized list, scroll box and scale box as Yoga leaves that follow Yoga's direction and sizing rules. Each context's tree lays out on one worker (UIDesign.md section 8) |
| U4 | **Decided 2026-11-25 (owner): "supporting multiple languages not only kor or eng".** Which languages the text stack must support | Every script: Latin, Cyrillic and Greek; Korean, Japanese and Chinese; right-to-left Arabic and Hebrew; complex scripts such as Devanagari and Thai. Shaping, glyph atlases filled on demand and font fallback are part of the base design, not later additions |
| U5 | **Decided 2026-11-25 (owner): option A.** **New, from U4:** the Unicode text processing that many languages need beyond shaping: bidirectional text, line breaking, grapheme clusters and font fallback (ICU, or smaller libraries such as SheenBidi and libunibreak; [U5 options in detail](#u5-options-in-detail-2026-11-25)) | A: ICU4C 79.1, vendored unmodified. `common` in C7 (bidi, scripts, breaks with dictionaries, normalization); `i18n` when string tables arrive (plurals, number formatting). Data filtered per game by language set. Adopted only if it builds unmodified |
| A1 | **Decided 2026-11-25 (owner): the interface with miniaudio as the first backend; built the same day.** Device layer: a private `AudioDevice` interface with a null device, and the first backend: miniaudio 0.11.26 (A), SDL3 with other decoders (B), or SDL3 with miniaudio decoding only (C) | The interface and its four rules ([Hiding the device layer](#hiding-the-device-layer-owner-question-2026-11-25)), with miniaudio as the first backend under the six conditions in [Is miniaudio enough?](#is-miniaudio-enough-owner-question-2026-11-25). An SDL3 backend is added when gamepads are planned or device tests fail ([miniaudio or SDL3?](#miniaudio-or-sdl3-audio-owner-question-2026-11-25)) |
| A2 | **Decided 2026-11-25 (owner): "Implements our own mixer"; built the same day ([record](GameServicesDesign.md#c8-second-part--the-engine-mixer-2026-11-25)).** Mixer: our own over miniaudio's parts (B), or `ma_engine` (A), or middleware | B, the mainstream owned-mixer design |
| A3 | **Delegated 2026-11-25; built.** Voice management: per-group concurrency rules, audibility-ranked virtual voices and steal fades in C8 | Yes, in C8 (Unreal's model, simplified) |
| A4 | **Delegated 2026-11-25; the next part of C8.** Games play sound-event assets (variations, ranges, concurrency, bus), not raw buffers | Yes, from C8 |
| A5 | **Delegated 2026-11-25; built (six fixed buses, Dialogue ducks Music and Ambience).** Buses: volumes and a bus-to-bus duck in C8; effect slots and sends designed now, built later | Yes |
| A6 | **Delegated 2026-11-25; WAV, FLAC and MP3 built; Ogg Vorbis through stb_vorbis 1.25 built at the owner's request.** Formats now: WAV, FLAC, MP3 (in miniaudio) plus Ogg Vorbis (stb_vorbis); Opus later | As listed |

Each adopted library still needs the owner's approval of its exact source and release before download, as with Jolt.
