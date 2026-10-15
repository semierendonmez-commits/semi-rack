#pragma once
#include <cmath>

// FamilyBus CV — the cable form of FamilyBus v1 for Rack and MetaModule.
//
// The DAW plugins share a memory segment (/semi.family.v1). Rack modules can't
// rely on that and MetaModule modules can't talk at all except through cables,
// so the series carries the same meanings on ONE 4-channel poly cable
// (MetaModule caps poly cables at 4 voices, so 4 is the hard limit).
//
//   ch 1  POS     song position inside a 32-bar cycle: 10 V * (bar + phase) / 32
//                 -> bar = floor(v / 10 * 32), phase = fract(...)
//   ch 2  ENERGY  EPOCH energy role, 0..10 V  (FamilyBus value 0..1 x 10)
//   ch 3  SCENE   section / scene index, 0.5 V per step (0..19)
//   ch 4  EVENT   1 ms pulse on a phrase turn (FamilyBus EvTurn); its height
//                 says which line ended: 4 bars 2.5 V, 8 -> 5 V, 16 -> 7.5 V,
//                 32 -> 10 V. Receivers treat > 1 V as a trigger.
//
// Missing channels read 0 V, so a mono cable into a BUS input still drives
// POS. Senders always set 4 channels.

namespace semi {
namespace bus {

constexpr int kChannels = 4;
constexpr int kPos = 0, kEnergy = 1, kScene = 2, kEvent = 3;
constexpr float kBarsPerCycle = 32.f;
constexpr float kSceneStep = 0.5f;
constexpr float kPulseSec = 0.001f;

inline float posToVolts(float barsInCycle) { return 10.f * std::fmod(barsInCycle, kBarsPerCycle) / kBarsPerCycle; }
inline float voltsToBars(float v) { return v / 10.f * kBarsPerCycle; }
inline float sceneToVolts(int scene) { return kSceneStep * scene; }
inline int voltsToScene(float v) { return (int) std::floor(v / kSceneStep + 0.5f); }

// Turn line (4/8/16/32 bars) <-> pulse height.
inline float turnToVolts(int lineBars) {
	switch (lineBars) {
		case 4: return 2.5f;
		case 8: return 5.f;
		case 16: return 7.5f;
		default: return 10.f;
	}
}
inline int voltsToTurn(float v) {
	if (v < 1.f) return 0;
	if (v < 3.75f) return 4;
	if (v < 6.25f) return 8;
	if (v < 8.75f) return 16;
	return 32;
}

} // namespace bus
} // namespace semi
