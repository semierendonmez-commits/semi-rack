#pragma once
// Platform constants shared by every module.
//
// MetaModule draws dynamic widgets into per-widget pixel buffers on its GUI
// core and pushes only the changed pixels to the 240x320 RGB565 screen.
// Documented limits (SDK docs/graphic-displays.md) that shape everything below:
//   * frame rate is variable and slow: ~20 FPS with one module on screen,
//     lower when several modules are visible -> never rely on fast animation;
//   * the panel is shown 240 px high full-screen and 180 px zoomed out, i.e.
//     Rack px are scaled by 0.63 / 0.47 -> 1 px lines disappear;
//   * only drawLayer(layer 1) is rendered, drawing is clipped to the box,
//     text opacity is ignored, SVGs/textures/gradients are not drawn.
// See docs/METAMODULE_DISPLAY.md for the full rules.

namespace semi {
namespace platform {

#ifdef METAMODULE
constexpr bool kMetaModule = true;
// Rate at which modules publish display snapshots. Publishing faster than the
// screen can show only burns audio-core cycles.
constexpr float kUiRateHz = 20.f;
// Upper bound for points in any drawn polyline (scope, curves, timelines).
constexpr int kMaxPolyPoints = 48;
// Strokes in Rack px. 2.6 Rack px ~= 1.2 screen px when zoomed out (x0.47).
constexpr float kStroke = 2.6f;
constexpr float kThinStroke = 2.2f;
// Minimum text size in Rack px (>= ~5 screen px at full screen).
constexpr float kMinText = 9.f;
#else
constexpr bool kMetaModule = false;
constexpr float kUiRateHz = 60.f;
constexpr int kMaxPolyPoints = 256;
constexpr float kStroke = 1.5f;
constexpr float kThinStroke = 1.f;
constexpr float kMinText = 7.f;
#endif

} // namespace platform
} // namespace semi
