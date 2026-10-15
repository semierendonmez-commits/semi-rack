#pragma once
#include <rack.hpp>

// Early Cyber x Eurorack palette. Every colour is opaque and pre-blended:
// MetaModule ignores alpha on text and RGB565 makes faint blends muddy.
namespace semi {
namespace theme {

inline NVGcolor cyan() { return nvgRGB(0x4f, 0xe3, 0xd6); }
inline NVGcolor cyanDim() { return nvgRGB(0x1e, 0x6b, 0x66); }
inline NVGcolor cyanDeep() { return nvgRGB(0x16, 0x3a, 0x3a); }
inline NVGcolor magenta() { return nvgRGB(0xff, 0x4f, 0x9a); }
inline NVGcolor magentaDeep() { return nvgRGB(0x2a, 0x12, 0x20); }
inline NVGcolor amber() { return nvgRGB(0xe8, 0xb0, 0x4a); }
inline NVGcolor text() { return nvgRGB(0x9d, 0xb5, 0xb2); }
inline NVGcolor dim() { return nvgRGB(0x5c, 0x73, 0x80); }
inline NVGcolor grid() { return nvgRGB(0x16, 0x22, 0x2b); }
inline NVGcolor cell() { return nvgRGB(0x0f, 0x1a, 0x20); }
inline NVGcolor screen() { return nvgRGB(0x05, 0x08, 0x0a); }
inline NVGcolor panel() { return nvgRGB(0x12, 0x19, 0x20); }

// Hex strings for the panel generator and SVG components (keep in sync).
constexpr const char* kCyanHex = "#4FE3D6";
constexpr const char* kMagentaHex = "#FF4F9A";

} // namespace theme
} // namespace semi
