# Screens on MetaModule

How the series draws its screens so they look the same in VCV Rack and work well on the
MetaModule's own display. Sources: the MetaModule plugin SDK docs
(`docs/graphic-displays.md`, `docs/graphics.md`, `docs/limitations.md`) and the firmware v2.2 notes.

## What the hardware does

| | |
|---|---|
| Display | 320 × 240 px (shown as a 240 px tall panel), 16-bit RGB565 |
| Panel scale | full-screen: panel is 240 px high → Rack px × **0.63**; zoomed out: 180 px → × **0.47** |
| Frame rate | variable and slow: about **20 FPS** with one module on screen, less with several |
| Pipeline | each *dynamic* widget gets its own pixel buffer the size of its box; every frame the GUI core runs `step()`, `draw()`, `drawLayer()`; only **changed pixels** go to the LVGL back end |
| Layers | only layer 1 is drawn (`drawLayer(args, 1)`, after `draw()`) |
| Clipping | anything outside the widget box is cut off (text excepted) |
| Not drawn | SVGs (inside widgets), textures/images, gradients; text opacity is ignored |
| Text | built-in fonts only unless you ship a .ttf: DSEG7, DejaVuSans, Nunito-Bold, ShareTechMono |
| CPU | 800 MHz dual Cortex-A7; audio and GUI on separate cores |

Static art (faceplate, bezel, glass, knob/jack images) is PNG and never redrawn. Knobs, jacks,
lights and buttons are drawn by the MetaModule engine itself, not by our code.

## Rules in this repo

All of them live in `src/common/Platform.hpp`, `Screen.hpp` and `TripleBuffer.hpp`, so a new
module gets them for free by subclassing `semi::Screen`.

1. **Publish at the display rate, not the audio rate.** The module fills a
   `TripleBuffer<Snapshot>` from `process()` when `UiDivider::tick()` fires:
   `kUiRateHz` = 20 Hz on MetaModule, 60 Hz in Rack. Peaks are held between publishes
   (max over the period) so nothing is missed at the lower rate.
2. **Lock-free hand-over.** The audio core writes, the GUI core reads, neither blocks
   (triple buffer, one atomic). Screens never touch engine state.
3. **Draw only in `drawLayer(1)`** and scissor to the box. In Rack layer 1 is the light
   layer, so screens stay lit in a dark room, as on the plugins.
4. **Stable frames.** No free-running animation, no per-frame noise or blinking that is not
   driven by data. When the data does not change, the frame is pixel-identical and costs
   nothing to send. Moving parts (scope, playhead, marker) change at most 20× per second.
5. **Thick, few, opaque.** Strokes ≥ `kStroke` (2.6 Rack px on MetaModule ≈ 1.2 screen px
   zoomed out), polylines decimated to `kMaxPolyPoints` (48), colours pre-blended and opaque,
   text ≥ `kMinText`, ShareTechMono (built into both platforms, nothing to ship).
6. **Small boxes.** Each screen's buffer is `w × h × 2` bytes at panel scale; keep one screen
   per function, sized to the glass, instead of one widget covering the whole panel.
7. **No dashes from nvg.** Neither side supports dash patterns; `Screen::dashed()` builds
   them from segments.
8. **Fallbacks.** No module (Rack browser preview) → each screen draws its `demo` snapshot.
   Font failing to load → text is skipped, never a crash (`ensureFont()` checks the handle).

## Budget per module (MetaModule)

| module | screens | buffer (approx.) | moving content |
|---|---|---|---|
| NEXUSCLIP | transfer 44×25 mm | 52 KB | peak marker, GR text |
| RETICULUM | Lissajous 34×34 mm, node matrix | 70 KB | scope trail (≤ 48 points), 64 cells |
| SEEDWALK | 4×4 nets | 95 KB | current / next / path cells |
| EPOCH | timeline 54×32 mm, readouts | 80 KB | playhead only |
| TURNAROUND | bar map 54×20 mm | 50 KB | 1 cell per bar step |

Buffers are allocated only while the module is visible and the patch plays.

## Checking it

* `python3 tools/panelgen.py` renders `build/preview/<Module>.png` and the 240 px faceplate the
  MetaModule will show (`metamodule/assets/panels`).
* Build both targets (`make`, `cmake --build build/mm`); the MetaModule build checks every
  symbol against the firmware API and fails on anything missing.
* On hardware: firmware ≥ 2.0, load `dist/metamodule/SemiFamily.mmplugin` from SD/USB,
  open one module full-screen and then four at once; the screens should stay smooth at one
  and remain readable (only slower) at four.
