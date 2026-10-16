// NEUROSTACK — Rack / MetaModule port of NEUROSTACK (engine: engines/neurostack, vendored from
// semierendonmez-commits/neurostack Source/engine; namespaces moved to nst::neuro / nst::walk).
//
// Eight neuro bass layers on one master phase, switched by the Pinball 2 Markov walk, split at
// SPLIT x f0 over one clean sine sub. The plugin's host transport becomes CLK / RST (1/16 by
// default; unpatched: the module free-runs at the menu tempo), MIDI becomes V/OCT + GATE (the
// fractional part of V/OCT is sent as pitch bend, so a glide on the cable is followed), the
// sidechain becomes the DUCK input. TONE / MOTION / GRIT on the panel offset all eight slots;
// each slot's own model and values, the 16 steps, the matrix shape, the rhythm and the rest of
// the plugin's parameters live in the context menu (saved with the patch). The 21 factory
// presets and 17 rhythms of the plugin are included.
//
// Structure follows NEXUSCLIP: 32-sample block adapter, TripleBuffer snapshot via UiDivider,
// Screen subclasses with a demo snapshot, layout header from tools/panels/neurostack.py.

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/NeurostackLayout.hpp"
#include "neurostack/Neuro.h"

#include <array>
#include <cstdio>
#include <cstring>

namespace L = semi::layout::neurostack;
namespace N = nst::neuro;
using semi::Screen;

namespace {

constexpr int kBlock = 32;
constexpr float kVoltsPerUnit = 5.f;
constexpr int kStates = N::kSteps + 1;

// ---------------------------------------------------------------- the plugin's lists (Params.h)
const std::vector<std::string> kTimeNames{"x1/4", "x1/2", "x3/4", "x1", "x3/2", "x2", "x3", "x4", "1/2 BAR", "1 BAR", "2 BARS", "4 BARS"};
constexpr float kTimeValues[]{0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f};
const std::vector<std::string> kSourceNames{"GATE (MIDI)", "SELF", "LATCH"};
const std::vector<std::string> kResetNames{"NEVER", "1/2 BAR", "1 BAR", "2 BARS", "4 BARS", "8 BARS", "16 BARS", "32 BARS", "64 BARS"};
constexpr float kResetValues[]{0.0f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f};
const std::vector<std::string> kDriftNames{"1 BAR", "2 BARS", "4 BARS", "8 BARS", "16 BARS"};
constexpr float kDriftValues[]{1.0f, 2.0f, 4.0f, 8.0f, 16.0f};
const std::vector<std::string> kScaleNames{"MINOR", "PHRYGIAN", "DORIAN", "HARM MINOR", "MAJOR", "CHROMATIC"};
const std::vector<std::string> kRateNames{"1/16", "1/8 (HALF)"};
const std::vector<std::string> kArcNames{"8 BARS", "16 BARS", "32 BARS"};
constexpr float kArcValues[]{8.0f, 16.0f, 32.0f};
const std::vector<std::string> kKickNames{"OFF", "DNB 1 + 3&", "2-STEP", "4 x 4", "HALF-TIME"};
const std::vector<std::string> kDurUnitNames{"STEPS", "1/16 (DEVICE)"};
const std::vector<std::string> kSubOctNames{"UNISON", "-1 OCT"};
const std::vector<std::string> kShapeNames{"RUN", "SWITCH-UP", "STUTTER", "BACK+FORTH", "HALF-BAR", "RANDOM", "CLEAR"};
const std::vector<std::string> kShapeLong{"the plain run (the device's default)", "switch-up (jumps to the same place in other beats)",
                                          "stutter (steps repeat)", "back and forth", "half-bar swap", "random weights",
                                          "clear (the ball stops)"};
enum Shape { ShapeRun = 0, ShapeSwitchUp, ShapeStutter, ShapeBackForth, ShapeHalfBar, ShapeRandom, ShapeClear, NumShapes };

// Clock: pulses per quarter note (CLK menu), and the free-running tempo when CLK is unpatched.
const std::vector<std::string> kPpqNames{"1/16 (4 PPQN)", "1/8 (2 PPQN)", "1/4 (1 PPQN)", "24 PPQN"};
constexpr float kPpqValues[]{4.f, 2.f, 1.f, 24.f};
const std::vector<std::string> kBpmNames{"120", "140", "160", "170", "172", "174", "176", "180"};
constexpr float kBpmValues[]{120.f, 140.f, 160.f, 170.f, 172.f, 174.f, 176.f, 180.f};

// ---------------------------------------------------------------- rhythms (PluginProcessor.cpp)
// a 16th per character over two bars: x note, - held, . space; notes { cell, semis, slide, bend, vel (0 = 100) }
struct RhythmDef {
	const char* name;
	const char* cells;
	std::vector<std::array<int, 5>> notes;
};
const std::vector<RhythmDef>& rhythmLibrary() {
	static const std::vector<RhythmDef> lib{
	    {"ROLLER", "x--.......x--...x--.......x-x-..", {{28, 12, 0, 0, 0}}},
	    {"ROLLER LONG", "x-------..x-----x-------....x---", {{16, -2, 1, 0, 0}, {28, 3, 1, 0, 0}}},
	    {"JUNGLE REESE", "x-----------x---x-------x-------", {{12, 3, 1, 0, 0}, {16, -2, 1, 0, 0}, {24, 5, 1, 0, 0}}},
	    {"HALF-TIME", "x-----........x.x-----..........", {{14, 7, 0, 0, 0}}},
	    {"UKG 2-STEP", "x-.x-.....x-.x..x-.x-.....x-.x-.", {{3, 12, 0, 0, 0}, {19, 12, 0, 0, 0}, {29, 10, 1, 0, 0}}},
	    {"SPEED GARAGE", "..x-..x-..x-..x-..x-..x-..x-..x-", {{18, 12, 0, 0, 0}, {22, 12, 0, 0, 0}, {30, 7, 0, 0, 0}}},
	    {"ACID", "x.xx-x.xx.x-xx.xx.xx-x.xx.x-x.x.",
	     {{3, 12, 1, 0, 0}, {7, 12, 0, 0, 0}, {13, 3, 1, 0, 0}, {19, 12, 1, 0, 0}, {23, 10, 0, 0, 0}, {28, 7, 1, 0, 0}}},
	    {"STEPPER", "x-..x-..x-..x-..x-..x-..x-..x...", {{24, 12, 0, 0, 0}, {28, 7, 0, 0, 0}}},
	    {"DRONE (ALWAYS ON)", "x-------------------------------", {}},
	    {"DRONE + ANSWER", "x--------.......x-----------....", {{0, 0, 0, 0, 110}, {16, 0, 0, 0, 90}}},
	    {"ROLLING 8THS +12", "x-x-x-x-x-x-x-x-x-x-x-x-x-x-x-x-",
	     {{0, -1, 0, 0, 0}, {2, -1, 0, 0, 80}, {4, 0, 0, 0, 0}, {6, 3, 0, 0, 80}, {8, 12, 0, 0, 0}, {10, 12, 0, 0, 80},
	      {12, 12, 0, 0, 0}, {14, 12, 0, 0, 80}, {16, 0, 0, 0, 0}, {18, 0, 0, 0, 80}, {20, 0, 0, 0, 0}, {22, 0, 0, 0, 80},
	      {24, 12, 0, 0, 0}, {26, 12, 0, 0, 80}, {28, 3, 0, 0, 0}, {30, 5, 0, 0, 80}}},
	    {"DOTTED ROLLER", "x---x-----x-----x----.x----.x---",
	     {{0, 5, 0, 0, 0}, {4, -2, 1, 0, 0}, {10, -2, 0, 0, 0}, {16, 0, 0, 0, 0}, {22, 0, 0, 0, 0}, {28, 0, 0, 0, 0}}},
	    {"SPACE ON ONE", "....x----.x----.x----.x---------", {{4, 0, 0, 0, 0}, {10, -4, 0, 0, 0}, {16, 3, 0, 0, 0}, {22, -4, 0, 0, 0}}},
	    {"GAP ROLLER", "x----x---x-x---.................", {{0, 0, 0, 0, 0}, {5, 2, 0, 0, 0}, {9, -1, 0, 0, 0}, {11, 0, 0, 0, 0}}},
	    {"HALF-BAR STABS", "x-----......x------x----........", {{0, 3, 0, 0, 0}, {12, 3, 0, 0, 0}, {19, 2, 0, 0, 0}}},
	    {"808 DROPS", "x-----------....x-------x-------", {{0, 0, 0, 12, 120}, {16, -2, 0, 7, 0}, {24, 3, 0, 12, 0}}},
	    {"GARAGE DIVES", "..x-..x-..x-..x-..x-..x-..x-..x-",
	     {{2, 0, 0, 7, 0}, {10, 0, 0, 7, 0}, {18, 0, 0, 7, 0}, {26, 3, 0, 12, 0}, {30, 5, 0, 0, 0}}},
	};
	return lib;
}

// ---------------------------------------------------------------- presets (PluginProcessor.cpp)
struct PV {
	const char* id;
	float v;
};
struct PresetDef {
	const char* name;
	const char* pattern;
	int shape, rhythm;
	std::vector<PV> values;
};
const std::vector<PresetDef>& presetLibrary() {
	static const std::vector<PresetDef> lib = [] {
		std::vector<PresetDef> l{
		    {"Minimal Roller", "R2^ . W2 . R1v G1^ R2 . T2^ . R2 . W1 G1 R2^ .", ShapeSwitchUp, 0,
		     {{"a1tone", 30}, {"a1motion", 55}, {"a2tone", 35}, {"a2motion", 25}, {"a3tone", 35}, {"drift", 30}, {"xfade", 12}, {"release", 90}}},
		    {"Long Roller Reese", "R4^ . . . R2v . G2^ . R4 . . . W2^ . T2 .", ShapeRun, 1,
		     {{"time", 9}, {"a1tone", 25}, {"a1motion", 70}, {"glide", 30}, {"drift", 40}, {"driftBars", 3}, {"xfade", 18}}},
		    {"Jungle Reese Slide", "R1^ R1v H1 R1^ G1 R1 T1v R1^ R1 H1 G1^ R1v R1 T1 R1^ G1", ShapeHalfBar, 2,
		     {{"time", 11}, {"a1tone", 40}, {"a1motion", 80}, {"a5tone", 35}, {"glide", 70}, {"drift", 20}, {"resetBars", 0}}},
		    {"Half-Time Growl", "G2^ . T2 . G1v W1^ G2 . T2^ . G2 . W1 T1 G2v .", ShapeSwitchUp, 3,
		     {{"a3tone", 60}, {"a6motion", 60}, {"drift", 35}, {"xfade", 15}}},
		    {"UKG Two-Step Wobble", "W2^ . R2 . W1 T1 W2v . R2^ . W2 . T1 W1 R2 .", ShapeSwitchUp, 4,
		     {{"time", 9}, {"root", 33}, {"a2tone", 40}, {"a2motion", 40}, {"release", 35}, {"drift", 20}, {"xover", 3}}},
		    {"Speed Garage Organ-ish", "F2 . W2^ . F1 S1 F2v . W2 . F2^ . T1 F1 W2 .", ShapeBackForth, 5,
		     {{"time", 9}, {"root", 33}, {"a4tone", 30}, {"a4motion", 55}, {"release", 40}}},
		    {"Acid Switch", "S2^ . F2 . S1 W1^ S2 . F2v . S2^ . N1 S1 F2 .", ShapeSwitchUp, 6,
		     {{"time", 9}, {"a7tone", 35}, {"a7motion", 45}, {"glide", 25}, {"drift", 45}, {"driftBars", 1}, {"release", 30}}},
		    {"Stepper Drone", "R4^ . . . T4v . . . G4^ . . . R4 . . .", ShapeRun, 8,
		     {{"time", 10}, {"a1tone", 20}, {"drift", 50}, {"driftBars", 3}, {"xfade", 30}}},
		    {"Data: Drone + Answer", "R4^ . . . H4 . . . R4v . . . T4^ . . .", ShapeRun, 9,
		     {{"time", 10}, {"a5model", (float) N::SemiSines}, {"a5attack", 180}, {"arcamt", 60}, {"arcbars", 1}, {"variation", 30},
		      {"duck", 35}, {"xfade", 25}, {"a1tone", 25}}},
		    {"Data: Rolling 8ths +12", "R2 . G2 . R2 . W2 . R2 . F2 . R2 . G2 .", ShapeSwitchUp, 10,
		     {{"time", 9}, {"root", 35}, {"gate", 72}, {"veltone", 60}, {"duck", 50}, {"snaregap", 30}, {"a1tone", 35}, {"variation", 25}, {"budget", 2}}},
		    {"Data: Dotted Roller", "R2^ . W2 . R2 . T2v . R2 . G2^ . R2 . W2 .", ShapeHalfBar, 11,
		     {{"time", 9}, {"root", 31}, {"a1attack", 40}, {"glide", 40}, {"duck", 40}, {"arcamt", 40}, {"a2motion", 55}}},
		    {"Data: Space On One", "G2^ . R2 . G2 . T2 . G2v . R2 . W2 . G2 .", ShapeSwitchUp, 12,
		     {{"time", 9}, {"root", 31}, {"variation", 45}, {"budget", 3}, {"a3tone", 45}, {"duck", 45}, {"snaregap", 25}}},
		    {"Data: Gap Roller", "R4^ . . . R4v . . . W4 . . . R4 . . .", ShapeRun, 13,
		     {{"time", 9}, {"root", 29}, {"scale", 3}, {"budget", 1}, {"variation", 50}, {"a1tone", 30}, {"a1motion", 60}, {"duck", 30}}},
		    {"Data: Half-Bar Stabs", "R2 . H2 . R2 . H2 . T2 . H2 . R2 . H2 .", ShapeBackForth, 14,
		     {{"time", 9}, {"a5model", (float) N::Hollow}, {"a5attack", 120}, {"a5level", 65}, {"gate", 85}, {"arcamt", 35}, {"duck", 40}}},
		    {"Jungle 808 Half-Rate", "R4 . . . W2 . G2 . R4 . . . F2 . W2 .", ShapeSwitchUp, 15,
		     {{"time", 10}, {"rhyrate", 1}, {"a1model", (float) N::Drop808}, {"a2model", (float) N::SolidFm}, {"a3model", (float) N::Hollow},
		      {"a4model", (float) N::SemiSines}, {"bendtime", 180}, {"kick", 4}, {"duck", 50}, {"a1tone", 40}, {"a1level", 60}, {"variation", 30}}},
		    {"Speed Garage Dives", "R2 . W2 . R2 . G2 . R2 . W2 . R2 . T2 .", ShapeSwitchUp, 16,
		     {{"time", 9}, {"root", 33}, {"a1model", (float) N::SolidFm}, {"a2model", (float) N::Drop808}, {"bendtime", 90}, {"gate", 75},
		      {"kick", 2}, {"duck", 45}, {"release", 35}}},
		};
		// fast: every 16th a switch, no rhythm (the first version)
		const std::vector<PV> fast{{"time", 3}, {"resetBars", 2}, {"rhythm", 0}, {"durunit", 1}, {"drift", 0}};
		auto withFast = [&](std::vector<PV> v) {
			for (auto& f : fast) {
				bool has = false;
				for (auto& x : v)
					has |= !std::strcmp(x.id, f.id);
				if (!has)
					v.push_back(f);
			}
			return v;
		};
		l.push_back({"Fast: Switch-Up Stack", "R3 . . W2 . F.5 G2 . R2 . T2 . S1 N1 H1+ M", ShapeSwitchUp, -1, withFast({})});
		l.push_back({"Fast: Pinball Neuro", "G2 . F.5 R1 . W2 . N1 G2 . S1 F.5 T2 . H1+ R1", ShapeSwitchUp, -1,
		             withFast({{"xover", 3}, {"drive", 40}, {"seed", 23}})});
		l.push_back({"Fast: Stutter Growl", "G1 . T1 . G1 G1 . T2 . G1 . N1 G1 . T1 M", ShapeStutter, -1,
		             withFast({{"a3tone", 70}, {"a6motion", 25}, {"xfade", 2}})});
		l.push_back({"Fast: Stacked (Reese Never Chokes)", "R16 . . . G2 . . . T2 . . . S2 . F1 M", ShapeRun, -1,
		             withFast({{"a1choke", 0}, {"a1level", 55}, {"xfade", 8}})});
		return l;
	}();
	return lib;
}

// ---------------------------------------------------------------- the menu-side state
struct SlotSet {
	int model = 0;
	float level = 50.f, tone = 50.f, motion = 40.f, grit = 30.f, attack = 0.f; // plugin units (%, ms)
	bool mute = false, choke = true;
};

struct Settings {
	std::array<SlotSet, N::NumArts> slot;
	int source = N::SrcSelf, root = 31, octave = 0;
	float glide = 0.f, release = 60.f;
	float subDrive = 10.f, body = 0.f;
	int subOct = 0;
	float swing = 0.f;
	int start = 1, resetIdx = 7, seed = 1, durUnit = 0, driftIdx = 2;
	bool rhythmOn = true;
	int scale = 0, budget = 2;
	float gate = 100.f, velTone = 40.f, bendTime = 120.f;
	int rhyRate = 0;
	float arcAmt = 0.f;
	int arcIdx = 1, kick = N::KickDnb;
	float duck = 0.f, duckRel = 100.f, snareGap = 0.f;
	Settings() {
		const float tones[]{45, 55, 50, 55, 50, 55, 45, 50};
		const float motions[]{40, 45, 45, 35, 35, 35, 50, 40};
		const float grits[]{25, 30, 35, 20, 20, 25, 35, 30};
		for (int a = 0; a < N::NumArts; a++) {
			slot[(size_t) a].model = a;
			slot[(size_t) a].tone = tones[a];
			slot[(size_t) a].motion = motions[a];
			slot[(size_t) a].grit = grits[a];
		}
	}
};

// ---------------------------------------------------------------- display
struct Snapshot {
	uint8_t w[kStates][kStates]{}; // matrix weights 0..100
	int8_t trig[N::kSteps]{};      // per step: 0 HOLD, 1..8 slot, 9 MUTE
	int8_t from = -1, step = -1;   // last move of the ball
	uint8_t cells[N::kRhythm]{};   // type | changed << 2 | oct << 3
	int8_t cell = -1;
	bool rhythmOn = true, running = false;
	int8_t shape = 0;
	int8_t preset = 0;
	float up[48]{}, low[48]{}; // one period of the upper band / the low end against the master phase
	float f0 = 55.f, xover = 137.f, sub = 0.f;
	float env[N::NumArts]{}, phase[N::NumArts]{}, coh[N::NumArts]{};
	int8_t model[N::NumArts]{};
	int8_t art = -1;
};

NVGcolor slotColor(int a) {
	static const NVGcolor c[8] = {nvgRGB(0x4f, 0xe3, 0xd6), nvgRGB(0xff, 0x4f, 0x9a), nvgRGB(0xe8, 0xb0, 0x4a), nvgRGB(0x7f, 0xa8, 0xff),
	                              nvgRGB(0xa0, 0xe3, 0x6a), nvgRGB(0xff, 0x8a, 0x4f), nvgRGB(0xc0, 0x8c, 0xff), nvgRGB(0xd8, 0xe0, 0xe0)};
	return c[a & 7];
}

NVGcolor mix(NVGcolor a, NVGcolor b, float t) {
	t = clamp(t, 0.f, 1.f);
	return nvgRGBf(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t);
}

} // namespace

struct Neurostack : Module {
	enum ParamId {
		TIME_PARAM, XFADE_PARAM, TONE_PARAM, MOTION_PARAM, GRIT_PARAM, DRIVE_PARAM,
		SPLIT_PARAM, SUB_PARAM, VARIATION_PARAM, DRIFT_PARAM, WIDTH_PARAM, LEVEL_PARAM,
		RHY_PARAM, SHAPE_PARAM, PRESET_PARAM,
		PARAMS_LEN
	};
	enum InputId { VOCT_INPUT, GATE_INPUT, CLK_INPUT, RST_INPUT, TONE_CV_INPUT, SC_INPUT, INPUTS_LEN };
	enum OutputId { OUT_L_OUTPUT, OUT_R_OUTPUT, TRIG_OUTPUT, ART_OUTPUT, PITCH_OUTPUT, OUTPUTS_LEN };
	enum LightId { RHY_LIGHT, RUN_LIGHT, LIGHTS_LEN };

	N::Engine engine;
	N::Brain brain;
	Settings set;
	std::atomic<int> shape{ShapeSwitchUp};
	std::atomic<int> presetIdx{0};
	std::atomic<int> presetRequest{-1};
	int ppqIdx = 0, bpmIdx = 5;
	uint32_t rng = 0x9E3779B9u;

	// block adapter
	float outBuf[2][kBlock]{}, scBuf[kBlock]{};
	std::array<N::MidiEvent, 24> evs{};
	int numEvs = 0;
	int pos = 0;

	// clock
	dsp::SchmittTrigger clkTrig, rstTrig, gateTrig;
	dsp::BooleanTrigger rhyBtn, shapeBtn, presetBtn;
	dsp::PulseGenerator trigPulse;
	int64_t sinceClk = 0;
	float period = 0.f;
	int64_t tick = -1;
	bool armed = false, jumpBack = false, clkActive = false;
	double lastPos1 = 0.0;
	float sr = 44100.f;

	// keys
	int curNote = -1;
	float lastFrac = 0.f;
	float artVolts = 0.f;
	int8_t lastFrom = -1, lastStep = -1, lastArt = -1;

	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;

	Neurostack() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configSwitch(TIME_PARAM, 0.f, 11.f, 10.f, "Time (how often the ball moves)", kTimeNames);
		configParam(XFADE_PARAM, 0.f, 1.f, std::log(8.f) / std::log(80.f), "Crossfade (the switch)", " ms", 80.f, 0.5f);
		configParam(TONE_PARAM, -0.5f, 0.5f, 0.f, "Tone (all slots)", " %", 0.f, 100.f);
		configParam(MOTION_PARAM, -0.5f, 0.5f, 0.f, "Motion (all slots)", " %", 0.f, 100.f);
		configParam(GRIT_PARAM, -0.5f, 0.5f, 0.f, "Grit (all slots)", " %", 0.f, 100.f);
		configParam(DRIVE_PARAM, 0.f, 1.f, 0.25f, "Drive", " %", 0.f, 100.f);
		configParam(SPLIT_PARAM, 1.5f, 6.f, 2.5f, "Split", " x f0");
		configParam(SUB_PARAM, 0.f, 1.f, 0.7f, "Sub", " %", 0.f, 100.f);
		configParam(VARIATION_PARAM, 0.f, 1.f, 0.35f, "Variation (rhythm edits)", " %", 0.f, 100.f);
		configParam(DRIFT_PARAM, 0.f, 1.f, 0.25f, "Drift (slow tone / motion LFO)", " %", 0.f, 100.f);
		configParam(WIDTH_PARAM, 0.f, 1.f, 0.35f, "Width (above the split)", " %", 0.f, 100.f);
		configParam(LEVEL_PARAM, -24.f, 6.f, -9.f, "Level", " dB");
		configButton(RHY_PARAM, "Rhythm on / off");
		configButton(SHAPE_PARAM, "Next matrix shape");
		configButton(PRESET_PARAM, "Next preset");
		configInput(VOCT_INPUT, "V/OCT")->description = "GATE source: the note (0 V = C4). SELF: transposes ROOT. Fractions = bend";
		configInput(GATE_INPUT, "Gate")->description = "GATE source: holds the note. LATCH: a new gate latches the note";
		configInput(CLK_INPUT, "Clock")->description = "1/16 pulses by default (menu). Unpatched: free-running at the menu tempo";
		configInput(RST_INPUT, "Reset")->description = "The Pinball back to its start step; with CLK the next pulse is step 1";
		configInput(TONE_CV_INPUT, "Tone CV (10 %/V)");
		configInput(SC_INPUT, "Duck sidechain")->description = "Patched: the kick that ducks the sub (else the menu's kick template)";
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		configOutput(TRIG_OUTPUT, "Switch trigger")->description = "10 V, 5 ms, every time the Pinball fires a step";
		configOutput(ART_OUTPUT, "Layer")->description = "The slot the last step fired: 0 V = slot 1 ... 7 V = slot 8";
		configOutput(PITCH_OUTPUT, "F0 (V/OCT)")->description = "The bass's fundamental, 0 V = C4 (glides and step octaves included)";
		configLight(RHY_LIGHT, "Rhythm");
		configLight(RUN_LIGHT, "Clock running");
		engine.prepare(44100.0, kBlock);
		loadPreset(0);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		sr = e.sampleRate;
		engine.prepare(e.sampleRate, kBlock);
		pos = 0;
		numEvs = 0;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		ppqIdx = 0;
		bpmIdx = 5;
		loadPreset(0);
		engine.reset();
	}

	// ------------------------------------------------------------------ random (menus, shapes)
	uint32_t rnd() {
		rng ^= rng << 13;
		rng ^= rng >> 17;
		rng ^= rng << 5;
		return rng;
	}
	float rndUnit() { return (rnd() >> 8) * (1.f / 16777216.f); }

	// ------------------------------------------------------------------ the Pinball (the plugin's processor)
	void matrixShape(int s) {
		constexpr int n = N::kSteps;
		auto& M = brain.matrix;
		M.clear();
		auto nextOf = [](int x) { return x < n ? x + 1 : 1; };
		M.set(0, 1, 100.f);
		for (int x = 1; x <= n; x++) {
			switch (s) {
				case ShapeRun: M.set(x, nextOf(x), 100.f); break;
				case ShapeSwitchUp:
					M.set(x, nextOf(x), 100.f);
					for (int y = 1; y <= n; y++)
						if (y != nextOf(x) && (y - 1) % 4 == (nextOf(x) - 1) % 4)
							M.set(x, y, 28.f);
					break;
				case ShapeStutter: M.set(x, nextOf(x), 100.f); M.set(x, x, 45.f); break;
				case ShapeBackForth: M.set(x, nextOf(x), 70.f); M.set(x, x > 1 ? x - 1 : n, 30.f); break;
				case ShapeHalfBar: M.set(x, nextOf(x), 75.f); M.set(x, (x + 8) % n + 1, 25.f); break;
				case ShapeRandom:
					for (int y = 1; y <= n; y++)
						if (rndUnit() < 0.2f)
							M.set(x, y, (float) (5 + rnd() % 96));
					M.set(x, nextOf(x), std::max(M.get(x, nextOf(x)), 40.f));
					break;
				default: break;
			}
		}
		if (s == ShapeClear)
			M.clear();
		shape = s;
	}

	void setStep(int i, int trig, int oct, float vel, float prob, float dur, float delay, float dprob, float mod) {
		auto& s = brain.steps[(size_t) i];
		s.trig.store(clamp(trig, 0, 9));
		s.oct.store(clamp(oct, -2, 2));
		s.vel.store(clamp(vel, 0.f, 127.f));
		s.prob.store(clamp(prob, 0.f, 100.f));
		s.dur.store(clamp(dur, 0.25f, 16.f));
		s.delay.store(clamp(delay, 0.f, 4.f));
		s.dprob.store(clamp(dprob, 0.f, 100.f));
		s.mod.store(clamp(mod, -1.f, 1.f));
	}

	// "R4 . . W2 . F.5 G2 . R2+ . T2 . S1 N1 H1 M": letter = slot (R W G F H T S N), '.' hold, 'M' mute,
	// the number is the duration, + / - an octave, ^ / v a sweep of TONE up / down over the step.
	void parsePattern(const char* pattern) {
		static const char* letters = "RWGFHTSN";
		const char* p = pattern;
		for (int s = 0; s < N::kSteps; s++) {
			while (*p == ' ')
				p++;
			char tok[16] = ".";
			int k = 0;
			while (*p && *p != ' ' && k < 15)
				tok[k++] = *p++;
			if (k > 0)
				tok[k] = 0;
			int trig = N::kTrigHold, oct = 0;
			float dur = 1.f, mod = 0.f;
			if (tok[0] == 'M')
				trig = N::kTrigMute;
			else if (std::strcmp(tok, ".") != 0) {
				const char* f = std::strchr(letters, tok[0]);
				trig = f ? (int) (f - letters) + 1 : N::kTrigHold;
				char num[16];
				int nn = 0;
				for (int i = 1; tok[i] && nn < 15; i++)
					if ((tok[i] >= '0' && tok[i] <= '9') || tok[i] == '.')
						num[nn++] = tok[i];
				num[nn] = 0;
				if (nn > 0)
					dur = (float) std::atof(num);
				if (std::strchr(tok, '+'))
					oct = 1;
				if (std::strchr(tok, '-'))
					oct = -1;
				if (std::strchr(tok, '^'))
					mod = 0.7f;
				if (std::strchr(tok, 'v'))
					mod = -0.6f;
			}
			setStep(s, trig, oct, 100.f, 100.f, dur, 0.f, 100.f, mod);
		}
	}

	void loadRhythm(int index) {
		const auto& lib = rhythmLibrary();
		if (index < 0 || index >= (int) lib.size())
			return;
		const auto& d = lib[(size_t) index];
		for (int i = 0; i < N::kRhythm; i++) {
			const char c = d.cells[i];
			auto& cell = brain.rhythm[(size_t) i];
			cell.type.store(c == 'x' ? N::CellHit : c == '-' ? N::CellTie : N::CellRest);
			cell.semis.store(0);
			cell.slide.store(0);
			cell.bend.store(0);
			cell.vel.store(100);
		}
		for (auto& n : d.notes) {
			auto& c = brain.rhythm[(size_t) n[0]];
			c.semis.store(n[1]);
			c.slide.store(n[2]);
			c.bend.store(n[3]);
			c.vel.store(n[4] > 0 ? n[4] : 100);
		}
		int semis = 0; // a held note keeps the pitch of the note it holds
		for (int i = 0; i < N::kRhythm; i++) {
			auto& cell = brain.rhythm[(size_t) i];
			if (cell.type.load() == N::CellHit)
				semis = cell.semis.load();
			else if (cell.type.load() == N::CellTie)
				cell.semis.store(semis);
			else
				cell.vel.store(100);
		}
		brain.rhythmLen.store(N::kRhythm);
		rhythmIdx = index;
	}
	std::atomic<int> rhythmIdx{0};

	void randomizeTrigs() {
		for (auto& s : brain.steps)
			s.trig.store((int) (rnd() % 10));
	}

	// ------------------------------------------------------------------ presets
	static float msToXfadeParam(float ms) { return clamp(std::log(clamp(ms, 0.5f, 40.f) / 0.5f) / std::log(80.f), 0.f, 1.f); }
	float xfadeMs() { return 0.5f * std::pow(80.f, params[XFADE_PARAM].getValue()); }

	// Sets one value by the plugin's parameter id, in the plugin's units.
	void setValue(const char* id, float v) {
		if (id[0] == 'a' && id[1] >= '1' && id[1] <= '8') {
			auto& s = set.slot[(size_t) (id[1] - '1')];
			const char* w = id + 2;
			if (!std::strcmp(w, "level")) s.level = v;
			else if (!std::strcmp(w, "tone")) s.tone = v;
			else if (!std::strcmp(w, "motion")) s.motion = v;
			else if (!std::strcmp(w, "grit")) s.grit = v;
			else if (!std::strcmp(w, "attack")) s.attack = v;
			else if (!std::strcmp(w, "model")) s.model = clamp((int) v, 0, N::NumModels - 1);
			else if (!std::strcmp(w, "mute")) s.mute = v > 0.5f;
			else if (!std::strcmp(w, "choke")) s.choke = v > 0.5f;
			return;
		}
		auto is = [&](const char* k) { return !std::strcmp(id, k); };
		if (is("time")) params[TIME_PARAM].setValue(clamp(v, 0.f, 11.f));
		else if (is("xfade")) params[XFADE_PARAM].setValue(msToXfadeParam(v));
		else if (is("drive")) params[DRIVE_PARAM].setValue(v * 0.01f);
		else if (is("xover")) params[SPLIT_PARAM].setValue(clamp(v, 1.5f, 6.f));
		else if (is("sub")) params[SUB_PARAM].setValue(v * 0.01f);
		else if (is("variation")) params[VARIATION_PARAM].setValue(v * 0.01f);
		else if (is("drift")) params[DRIFT_PARAM].setValue(v * 0.01f);
		else if (is("width")) params[WIDTH_PARAM].setValue(v * 0.01f);
		else if (is("level")) params[LEVEL_PARAM].setValue(clamp(v, -24.f, 6.f));
		else if (is("source")) set.source = clamp((int) v, 0, 2);
		else if (is("root")) set.root = clamp((int) v, 24, 60);
		else if (is("octave")) set.octave = clamp((int) v, -2, 2);
		else if (is("glide")) set.glide = v;
		else if (is("release")) set.release = v;
		else if (is("subdrive")) set.subDrive = v;
		else if (is("suboct")) set.subOct = (int) v;
		else if (is("body")) set.body = v;
		else if (is("swing")) set.swing = v;
		else if (is("start")) set.start = clamp((int) v, 1, 16);
		else if (is("resetBars")) set.resetIdx = clamp((int) v, 0, 8);
		else if (is("seed")) set.seed = std::max(1, (int) v);
		else if (is("durunit")) set.durUnit = (int) v;
		else if (is("rhythm")) set.rhythmOn = v > 0.5f;
		else if (is("driftBars")) set.driftIdx = clamp((int) v, 0, 4);
		else if (is("scale")) set.scale = clamp((int) v, 0, N::NumScales - 1);
		else if (is("budget")) set.budget = clamp((int) v, 1, 8);
		else if (is("gate")) set.gate = v;
		else if (is("veltone")) set.velTone = v;
		else if (is("bendtime")) set.bendTime = v;
		else if (is("rhyrate")) set.rhyRate = (int) v;
		else if (is("arcamt")) set.arcAmt = v;
		else if (is("arcbars")) set.arcIdx = clamp((int) v, 0, 2);
		else if (is("kick")) set.kick = clamp((int) v, 0, N::NumKicks - 1);
		else if (is("duck")) set.duck = v;
		else if (is("duckrel")) set.duckRel = v;
		else if (is("snaregap")) set.snareGap = v;
	}

	// The plugin's loadFactory: every parameter to its default, the preset's values on top,
	// its steps, matrix shape and rhythm. Called from the constructor / onReset / process().
	void loadPreset(int index) {
		const auto& lib = presetLibrary();
		if (index < 0 || index >= (int) lib.size())
			return;
		const auto& p = lib[(size_t) index];
		set = Settings();
		for (int i = 0; i < RHY_PARAM; i++)
			params[i].setValue(paramQuantities[i]->getDefaultValue());
		for (auto& v : p.values)
			setValue(v.id, v.v);
		parsePattern(p.pattern);
		matrixShape(p.shape);
		if (p.rhythm >= 0)
			loadRhythm(p.rhythm);
		presetIdx = index;
	}

	// ------------------------------------------------------------------ patch state
	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "preset", json_integer(presetIdx));
		json_object_set_new(root, "rhythmIdx", json_integer(rhythmIdx));
		json_object_set_new(root, "shape", json_integer(shape));
		json_object_set_new(root, "ppq", json_integer(ppqIdx));
		json_object_set_new(root, "bpm", json_integer(bpmIdx));
		json_t* s = json_object();
		auto I = [&](const char* k, int v) { json_object_set_new(s, k, json_integer(v)); };
		auto F = [&](const char* k, float v) { json_object_set_new(s, k, json_real(v)); };
		I("source", set.source); I("root", set.root); I("octave", set.octave); F("glide", set.glide); F("release", set.release);
		F("subdrive", set.subDrive); I("suboct", set.subOct); F("body", set.body); F("swing", set.swing); I("start", set.start);
		I("resetBars", set.resetIdx); I("seed", set.seed); I("durunit", set.durUnit); I("driftBars", set.driftIdx);
		I("rhythm", set.rhythmOn); I("scale", set.scale); I("budget", set.budget); F("gate", set.gate); F("veltone", set.velTone);
		F("bendtime", set.bendTime); I("rhyrate", set.rhyRate); F("arcamt", set.arcAmt); I("arcbars", set.arcIdx); I("kick", set.kick);
		F("duck", set.duck); F("duckrel", set.duckRel); F("snaregap", set.snareGap);
		for (int a = 0; a < N::NumArts; a++) {
			const auto& sl = set.slot[(size_t) a];
			char k[16];
			auto key = [&](const char* w) { std::snprintf(k, sizeof k, "a%d%s", a + 1, w); return k; };
			I(key("model"), sl.model); F(key("level"), sl.level); F(key("tone"), sl.tone); F(key("motion"), sl.motion);
			F(key("grit"), sl.grit); F(key("attack"), sl.attack); I(key("mute"), sl.mute); I(key("choke"), sl.choke);
		}
		json_object_set_new(root, "settings", s);

		json_t* steps = json_array();
		for (const auto& st : brain.steps) {
			json_t* a = json_array();
			json_array_append_new(a, json_integer(st.trig.load()));
			json_array_append_new(a, json_integer(st.oct.load()));
			for (float v : {st.vel.load(), st.prob.load(), st.dur.load(), st.delay.load(), st.dprob.load(), st.mod.load()})
				json_array_append_new(a, json_real(v));
			json_array_append_new(steps, a);
		}
		json_object_set_new(root, "steps", steps);
		json_t* mat = json_array();
		for (int x = 0; x < kStates; x++)
			for (int y = 0; y < kStates; y++)
				json_array_append_new(mat, json_integer((int) std::round(brain.matrix.get(x, y))));
		json_object_set_new(root, "matrix", mat);
		json_t* rh = json_array();
		for (const auto& c : brain.rhythm) {
			json_t* a = json_array();
			for (int v : {c.type.load(), c.semis.load(), c.slide.load(), c.vel.load(), c.bend.load()})
				json_array_append_new(a, json_integer(v));
			json_array_append_new(rh, a);
		}
		json_object_set_new(root, "rhythm", rh);
		json_object_set_new(root, "rhythmLen", json_integer(brain.rhythmLen.load()));
		return root;
	}

	void dataFromJson(json_t* root) override {
		auto gi = [](json_t* o, const char* k, int def) {
			json_t* j = json_object_get(o, k);
			return j ? (int) json_integer_value(j) : def;
		};
		presetIdx = clamp(gi(root, "preset", 0), 0, (int) presetLibrary().size() - 1);
		rhythmIdx = gi(root, "rhythmIdx", 0);
		shape = clamp(gi(root, "shape", 0), 0, NumShapes - 1);
		ppqIdx = clamp(gi(root, "ppq", 0), 0, 3);
		bpmIdx = clamp(gi(root, "bpm", 5), 0, 7);
		if (json_t* s = json_object_get(root, "settings")) {
			const char* key;
			json_t* v;
			json_object_foreach(s, key, v) {
				setValue(key, (float) json_number_value(v));
			}
		}
		if (json_t* steps = json_object_get(root, "steps")) {
			for (int i = 0; i < N::kSteps && i < (int) json_array_size(steps); i++) {
				json_t* a = json_array_get(steps, i);
				auto f = [&](int k, float def) { json_t* j = json_array_get(a, k); return j ? (float) json_number_value(j) : def; };
				setStep(i, (int) f(0, 0), (int) f(1, 0), f(2, 100), f(3, 100), f(4, 1), f(5, 0), f(6, 100), f(7, 0));
			}
		}
		if (json_t* mat = json_object_get(root, "matrix")) {
			if ((int) json_array_size(mat) == kStates * kStates)
				for (int x = 0; x < kStates; x++)
					for (int y = 0; y < kStates; y++)
						brain.matrix.set(x, y, clamp((float) json_number_value(json_array_get(mat, x * kStates + y)), 0.f, 100.f));
		}
		if (json_t* rh = json_object_get(root, "rhythm")) {
			for (int i = 0; i < N::kRhythm && i < (int) json_array_size(rh); i++) {
				json_t* a = json_array_get(rh, i);
				auto g = [&](int k, int def) { json_t* j = json_array_get(a, k); return j ? (int) json_integer_value(j) : def; };
				auto& c = brain.rhythm[(size_t) i];
				c.type.store(clamp(g(0, 0), 0, 2));
				c.semis.store(clamp(g(1, 0), -24, 24));
				c.slide.store(g(2, 0) ? 1 : 0);
				c.vel.store(clamp(g(3, 100), 1, 127));
				c.bend.store(clamp(g(4, 0), -24, 24));
			}
		}
		brain.rhythmLen.store(gi(root, "rhythmLen", N::kRhythm) == 16 ? 16 : N::kRhythm);
	}

	// ------------------------------------------------------------------ audio
	N::Params engineParams() {
		N::Params p;
		const float toneOff = params[TONE_PARAM].getValue() + inputs[TONE_CV_INPUT].getVoltage() * 0.1f;
		const float motionOff = params[MOTION_PARAM].getValue();
		const float gritOff = params[GRIT_PARAM].getValue();
		for (int a = 0; a < N::NumArts; a++) {
			const auto& s = set.slot[(size_t) a];
			auto& ap = p.art[(size_t) a];
			ap.level = s.level * 0.01f;
			ap.tone = clamp(s.tone * 0.01f + toneOff, 0.f, 1.f);
			ap.motion = clamp(s.motion * 0.01f + motionOff, 0.f, 1.f);
			ap.grit = clamp(s.grit * 0.01f + gritOff, 0.f, 1.f);
			ap.attackMs = s.attack;
			ap.model = clamp(s.model, 0, N::NumModels - 1);
			ap.mute = s.mute;
			ap.choke = s.choke;
		}
		p.source = set.source;
		int root = set.root;
		if (set.source == N::SrcSelf && inputs[VOCT_INPUT].isConnected())
			root += (int) std::floor(inputs[VOCT_INPUT].getVoltage() * 12.f + 0.5f);
		p.root = clamp(root, 0, 120);
		p.octave = set.octave;
		p.glideMs = set.glide;
		p.bendRange = 1.f; // V/OCT fractions arrive as bend in semitones
		p.xfadeMs = xfadeMs();
		p.releaseMs = set.release;
		p.xover = params[SPLIT_PARAM].getValue();
		p.sub = params[SUB_PARAM].getValue();
		p.subDrive = set.subDrive * 0.01f;
		p.subOct = set.subOct == 1 ? -1 : 0;
		p.body = set.body * 0.01f;
		p.mult = kTimeValues[clamp((int) std::round(params[TIME_PARAM].getValue()), 0, 11)];
		p.rhythmOn = set.rhythmOn;
		p.durInSteps = set.durUnit == 0;
		p.drift = params[DRIFT_PARAM].getValue();
		p.driftBars = kDriftValues[clamp(set.driftIdx, 0, 4)];
		p.swing = set.swing * 0.01f;
		p.startStep = set.start;
		p.resetBars = kResetValues[clamp(set.resetIdx, 0, 8)];
		p.seed = (uint32_t) std::max(1, set.seed);
		p.drive = params[DRIVE_PARAM].getValue();
		p.width = params[WIDTH_PARAM].getValue();
		p.gain = std::pow(10.f, params[LEVEL_PARAM].getValue() / 20.f);
		p.scale = set.scale;
		p.variation = params[VARIATION_PARAM].getValue();
		p.budget = set.budget;
		p.gate = clamp(set.gate * 0.01f, 0.4f, 1.f);
		p.velTone = set.velTone * 0.01f;
		p.bendMs = set.bendTime;
		p.rhythmHalf = set.rhyRate == 1;
		p.arcAmt = set.arcAmt * 0.01f;
		p.arcBars = kArcValues[clamp(set.arcIdx, 0, 2)];
		p.kick = set.kick;
		p.duck = set.duck * 0.01f;
		p.duckRelMs = set.duckRel;
		p.duckSidechain = inputs[SC_INPUT].isConnected();
		p.snareGapMs = set.snareGap;
		return p;
	}

	void pushEvent(const N::MidiEvent& e) {
		if (numEvs < (int) evs.size())
			evs[(size_t) numEvs++] = e;
	}

	void runBlock() {
		N::Params p = engineParams();
		const bool clkCon = inputs[CLK_INPUT].isConnected();
		const float cpb = kPpqValues[clamp(ppqIdx, 0, 3)]; // clocks per beat
		const float step16 = 4.f / cpb;                     // 16ths per clock
		if (clkCon) {
			const float per = period > 0.f ? period : sr * 60.f / (174.f * cpb);
			p.bpm = std::clamp(60.0 * sr / (per * cpb), 20.0, 400.0);
			p.playing = clkActive && tick >= 0;
			p.run = N::RunHost;
			double pos16 = tick < 0 ? 0.0 : (double) tick * step16 + std::min(1.0, (double) sinceClk / per) * step16;
			if (!jumpBack && pos16 < lastPos1)
				pos16 = lastPos1; // never backwards except on a reset: the engine treats that as a jump
			jumpBack = false;
			p.ppq = pos16 / 4.0;
			lastPos1 = pos16 + kBlock / (60.0 / p.bpm * sr / 4.0);
		}
		else {
			p.bpm = kBpmValues[clamp(bpmIdx, 0, 7)];
			p.playing = false;
			p.run = N::RunAlways;
		}
		engine.process(brain, p, evs.data(), numEvs, outBuf[0], outBuf[1], kBlock, p.duckSidechain ? scBuf : nullptr);
		numEvs = 0;

		N::FireEvent fe;
		while (engine.popFire(fe)) {
			lastFrom = fe.from;
			lastStep = fe.step;
			if (fe.art >= 0) {
				lastArt = fe.art;
				artVolts = (float) fe.art;
			}
			trigPulse.trigger(0.005f);
		}
	}

	void process(const ProcessArgs& args) override {
		sr = args.sampleRate;
		if (int r = presetRequest.exchange(-1); r >= 0)
			loadPreset(r);
		if (rhyBtn.process(params[RHY_PARAM].getValue() > 0.f))
			set.rhythmOn = !set.rhythmOn;
		if (shapeBtn.process(params[SHAPE_PARAM].getValue() > 0.f))
			matrixShape((shape + 1) % NumShapes);
		if (presetBtn.process(params[PRESET_PARAM].getValue() > 0.f))
			loadPreset((presetIdx + 1) % (int) presetLibrary().size());

		// ---- clock / reset
		const bool clkCon = inputs[CLK_INPUT].isConnected();
		++sinceClk;
		if (rstTrig.process(inputs[RST_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (clkCon)
				armed = true;
			else
				engine.resetRequest = 1;
		}
		if (clkCon && clkTrig.process(inputs[CLK_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (tick >= 0 && sinceClk > 0 && sinceClk < (int64_t) (sr * 4.f))
				period = period > 0.f && std::abs(sinceClk - period) < 0.3f * period ? period + 0.5f * (sinceClk - period) : (float) sinceClk;
			sinceClk = 0;
			if (armed || tick < 0) {
				tick = 0;
				armed = false;
				jumpBack = true;
			}
			else
				++tick;
			clkActive = true;
		}
		const float cpb = kPpqValues[clamp(ppqIdx, 0, 3)];
		const float per = period > 0.f ? period : sr * 60.f / (174.f * cpb);
		if (clkActive && (float) sinceClk > std::max(3.f * per, 0.05f * sr))
			clkActive = false;

		// ---- V/OCT + GATE -> note events (fractions as bend)
		const float note = 60.f + 12.f * inputs[VOCT_INPUT].getVoltage();
		const int nq = (int) std::floor(note + 0.5f);
		const float frac = note - nq;
		if (inputs[GATE_INPUT].isConnected()) {
			const bool rise = gateTrig.process(inputs[GATE_INPUT].getVoltage(), 0.1f, 1.f);
			const bool high = gateTrig.isHigh();
			const uint8_t nn = (uint8_t) clamp(nq, 0, 127);
			if (rise) {
				if (curNote >= 0 && curNote != nn)
					pushEvent({pos, (uint8_t) curNote, 0, false, 0.f});
				pushEvent({pos, nn, 100, false, 0.f});
				curNote = nn;
			}
			else if (high && curNote >= 0 && nn != curNote) {
				pushEvent({pos, nn, 100, false, 0.f}); // legato: the new note first, then the old one off
				pushEvent({pos, (uint8_t) curNote, 0, false, 0.f});
				curNote = nn;
			}
			else if (!high && curNote >= 0) {
				pushEvent({pos, (uint8_t) curNote, 0, false, 0.f});
				curNote = -1;
			}
		}
		else if (curNote >= 0) {
			pushEvent({pos, (uint8_t) curNote, 0, false, 0.f});
			curNote = -1;
		}
		if (std::abs(frac - lastFrac) > 0.002f) {
			pushEvent({pos, 0, 0, true, frac});
			lastFrac = frac;
		}

		// ---- block adapter
		scBuf[pos] = inputs[SC_INPUT].getVoltage() / kVoltsPerUnit;
		outputs[OUT_L_OUTPUT].setVoltage(outBuf[0][pos] * kVoltsPerUnit);
		outputs[OUT_R_OUTPUT].setVoltage(outBuf[1][pos] * kVoltsPerUnit);
		if (++pos >= kBlock) {
			pos = 0;
			runBlock();
		}
		outputs[TRIG_OUTPUT].setVoltage(trigPulse.process(args.sampleTime) ? 10.f : 0.f);
		outputs[ART_OUTPUT].setVoltage(artVolts);
		outputs[PITCH_OUTPUT].setVoltage(std::log2(std::max(1.f, engine.currentF0()) / 261.6256f));

		if (ui.tick(args.sampleTime)) {
			lights[RHY_LIGHT].setBrightness(set.rhythmOn ? 1.f : 0.f);
			lights[RUN_LIGHT].setBrightness(clkCon ? (clkActive ? 1.f : 0.f) : 0.3f);
			publish(clkCon ? clkActive : true);
		}
	}

	void publish(bool running) {
		Snapshot& s = snap.writeSlot();
		for (int x = 0; x < kStates; x++)
			for (int y = 0; y < kStates; y++)
				s.w[x][y] = (uint8_t) clamp((int) std::lround(brain.matrix.get(x, y)), 0, 100);
		for (int i = 0; i < N::kSteps; i++)
			s.trig[i] = (int8_t) brain.steps[(size_t) i].trig.load(std::memory_order_relaxed);
		s.from = lastFrom;
		s.step = lastStep;
		const bool rhy = set.rhythmOn && running;
		for (int i = 0; i < N::kRhythm; i++) {
			if (rhy) {
				const int v = engine.viewVariant[(size_t) i].load(std::memory_order_relaxed);
				s.cells[i] = (uint8_t) ((v & 3) | (((v >> 10) & 1) << 2) | (((v >> 9) & 1) << 3));
			}
			else
				s.cells[i] = (uint8_t) brain.rhythm[(size_t) i].type.load(std::memory_order_relaxed);
		}
		s.cell = rhy ? (int8_t) engine.viewCell.load(std::memory_order_relaxed) : -1;
		s.rhythmOn = set.rhythmOn;
		s.running = running;
		s.shape = (int8_t) shape.load();
		s.preset = (int8_t) presetIdx.load();
		// one period against the master phase, from the last ~43 ms of the engine's scope ring
		float su[48] = {}, sl[48] = {};
		int cnt[48] = {};
		const int w = engine.scopeWrite.load(std::memory_order_relaxed);
		for (int k = 1; k <= 1024; k++) {
			const int i = (w - k + N::Engine::kScope) % N::Engine::kScope;
			const int b = clamp((int) (engine.scopePhase[(size_t) i] * 48.f), 0, 47);
			su[b] += engine.scopeUp[(size_t) i];
			sl[b] += engine.scopeLow[(size_t) i];
			cnt[b]++;
		}
		for (int b = 0; b < 48; b++) {
			s.up[b] = cnt[b] ? su[b] / cnt[b] : 0.f;
			s.low[b] = cnt[b] ? sl[b] / cnt[b] : 0.f;
		}
		s.f0 = engine.viewF0.load(std::memory_order_relaxed);
		s.xover = engine.viewXover.load(std::memory_order_relaxed);
		s.sub = engine.viewSub.load(std::memory_order_relaxed);
		for (int a = 0; a < N::NumArts; a++) {
			s.env[a] = engine.viewEnv[(size_t) a].load(std::memory_order_relaxed);
			s.phase[a] = engine.viewPhase[(size_t) a].load(std::memory_order_relaxed);
			s.coh[a] = engine.viewCoh[(size_t) a].load(std::memory_order_relaxed);
			s.model[a] = (int8_t) set.slot[(size_t) a].model;
		}
		s.art = lastArt;
		snap.publish();
	}
};

// ---------------------------------------------------------------------------------------- screens

// The Pinball: the 17 x 17 matrix (row = from, column = to; S = before step 1), cells in the
// colour of the slot their step fires, the ball's last move in amber; to the right, each step's
// slot; below, the 32-cell rhythm (the cell playing outlined, variation edits in magenta).
struct NeurostackMatrixScreen : Screen {
	Neurostack* module = nullptr;
	Snapshot demo;

	NeurostackMatrixScreen() {
		const int8_t trig[16]{1, 0, 2, 0, 1, 3, 1, 0, 6, 0, 1, 0, 2, 3, 1, 0};
		for (int x = 0; x < kStates; x++)
			demo.w[x][x < N::kSteps ? x + 1 : 1] = 100;
		for (int x = 1; x <= N::kSteps; x++)
			for (int y = 1; y <= N::kSteps; y++)
				if (y != (x < N::kSteps ? x + 1 : 1) && (y - 1) % 4 == x % 4)
					demo.w[x][y] = 28;
		std::memcpy(demo.trig, trig, sizeof trig);
		demo.from = 4;
		demo.step = 4;
		const char* r = "x--.......x--...x--.......x-x-..";
		for (int i = 0; i < 32; i++)
			demo.cells[i] = r[i] == 'x' ? 1 : r[i] == '-' ? 2 : 0;
		demo.cells[30] |= 4;
		demo.cell = 11;
		demo.shape = ShapeSwitchUp;
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		const float pad = 3.f;
		text(vg, pad, pad - 1.f, kShapeNames[(size_t) clamp((int) s.shape, 0, NumShapes - 1)], 9.f, semi::theme::text());
		if (s.step >= 0) {
			char buf[16];
			std::snprintf(buf, sizeof buf, "STEP %d", s.step + 1);
			text(vg, w - pad, pad - 1.f, buf, 9.f, semi::theme::amber(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		}
		const float top = pad + 11.f;
		const float rhyH = 16.f;
		const float gridH = h - top - rhyH - pad - 3.f;
		const float cell = std::min(gridH / kStates, (w - 2.f * pad - 8.f) / kStates);
		const float gx = pad, gy = top;
		fillRect(vg, gx, gy, cell * kStates, cell * kStates, semi::theme::cell());
		for (int y = 0; y < kStates; y++) {
			// the column y is the state "step y" (y = 0: S); its colour is the slot step y fires
			NVGcolor c = semi::theme::dim();
			if (y > 0) {
				const int t = s.trig[y - 1];
				c = t >= 1 && t <= 8 ? slotColor(t - 1) : t == N::kTrigMute ? semi::theme::magenta() : semi::theme::text();
			}
			for (int x = 0; x < kStates; x++) {
				const int v = s.w[x][y];
				if (v <= 0)
					continue;
				fillRect(vg, gx + y * cell + 0.5f, gy + x * cell + 0.5f, cell - 1.f, cell - 1.f,
				         mix(semi::theme::cell(), c, 0.25f + 0.75f * v / 100.f));
			}
		}
		if (s.step >= 0 && s.from >= -1) {
			const float bx = gx + (s.step + 1) * cell, by = gy + (s.from + 1) * cell;
			nvgBeginPath(vg);
			nvgRect(vg, bx + 0.6f, by + 0.6f, cell - 1.2f, cell - 1.2f);
			nvgStrokeColor(vg, semi::theme::amber());
			nvgStrokeWidth(vg, semi::platform::kThinStroke);
			nvgStroke(vg);
		}
		// each step's slot, next to the grid's rows (row s+1 = from step s)
		const float sx = gx + cell * kStates + 3.f;
		const float sw = std::max(3.f, w - pad - sx);
		for (int i = 0; i < N::kSteps; i++) {
			const int t = s.trig[i];
			const NVGcolor c = t >= 1 && t <= 8 ? slotColor(t - 1) : t == N::kTrigMute ? semi::theme::magenta() : semi::theme::grid();
			fillRect(vg, sx, gy + (i + 1) * cell + 0.5f, sw, cell - 1.f, i == s.step ? semi::theme::amber() : c);
		}
		// the rhythm: two rows of 16
		const float ry = h - pad - rhyH;
		const float cw = (w - 2.f * pad) / 16.f, ch = rhyH / 2.f;
		for (int i = 0; i < N::kRhythm; i++) {
			const int type = s.cells[i] & 3;
			const float x = pad + (i % 16) * cw, y = ry + (i / 16) * ch;
			NVGcolor c = type == N::CellHit ? (s.rhythmOn ? semi::theme::cyan() : semi::theme::dim())
			             : type == N::CellTie ? (s.rhythmOn ? semi::theme::cyanDim() : semi::theme::grid())
			                                  : semi::theme::cell();
			if (s.cells[i] & 8)
				c = semi::theme::amber(); // OCTAVE edit: up an octave
			fillRect(vg, x + 0.5f, y + 0.5f, cw - 1.f, ch - 1.f, c);
			if (s.cells[i] & 4)
				fillRect(vg, x + 0.5f, y + 0.5f, cw - 1.f, 2.f, semi::theme::magenta());
			if (i == s.cell) {
				nvgBeginPath(vg);
				nvgRect(vg, x + 0.6f, y + 0.6f, cw - 1.2f, ch - 1.2f);
				nvgStrokeColor(vg, semi::theme::amber());
				nvgStrokeWidth(vg, semi::platform::kThinStroke);
				nvgStroke(vg);
			}
		}
	}
};

// scope.lock: the upper band (bright) and the low end (dim, thick) against the master phase, two
// periods, so the wave stands still whatever the note; f0, the split, the slot sounding.
struct NeurostackScopeScreen : Screen {
	Neurostack* module = nullptr;
	Snapshot demo;

	NeurostackScopeScreen() {
		for (int b = 0; b < 48; b++) {
			const float ph = (b + 0.5f) / 48.f;
			demo.low[b] = 0.55f * std::sin(2.f * M_PI * ph);
			demo.up[b] = 0.25f * std::sin(2.f * M_PI * 3.f * ph) + 0.12f * std::sin(2.f * M_PI * 5.f * ph + 0.6f);
		}
		demo.f0 = 49.f;
		demo.xover = 122.5f;
		demo.art = 0;
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		const float pad = 3.f;
		char buf[48];
		std::snprintf(buf, sizeof buf, "F0 %.1f", s.f0);
		text(vg, pad, pad - 1.f, buf, 9.f, semi::theme::text());
		std::snprintf(buf, sizeof buf, "SPLIT %.0f", s.xover);
		text(vg, w - pad, pad - 1.f, buf, 9.f, semi::theme::magenta(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		const float y0 = pad + 11.f, y1 = h - pad - 10.f;
		const float mid = 0.5f * (y0 + y1), amp = 0.5f * (y1 - y0);
		line(vg, pad, mid, w - pad, mid, semi::theme::grid());
		line(vg, 0.5f * w, y0, 0.5f * w, y1, semi::theme::grid());
		float lo[96], up[96];
		for (int i = 0; i < 96; i++) {
			lo[i] = mid - amp * clamp(s.low[i % 48], -1.f, 1.f);
			up[i] = mid - amp * clamp(s.up[i % 48], -1.f, 1.f);
		}
		polyline(vg, lo, 96, pad, w - 2.f * pad, semi::theme::cyanDim(), semi::platform::kStroke * 1.6f);
		polyline(vg, up, 96, pad, w - 2.f * pad, semi::theme::cyan());
		const auto& lib = presetLibrary();
		const std::string name = lib[(size_t) clamp((int) s.preset, 0, (int) lib.size() - 1)].name;
		text(vg, pad, h - pad, name, 9.f, semi::theme::dim(), NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
		if (s.art >= 0) {
			text(vg, w - pad, h - pad, N::modelName(module ? s.model[s.art] : s.art), 9.f, slotColor(s.art),
			     NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
		}
	}
};

// The eight layers: each slot's model, its envelope (bar), and the phase of its fundamental
// against the sub (the tick; centred = in phase, its width = how much fundamental it has).
struct LayersScreen : Screen {
	Neurostack* module = nullptr;
	Snapshot demo;

	LayersScreen() {
		const float env[8]{0.9f, 0.f, 0.35f, 0.f, 0.f, 0.15f, 0.f, 0.f};
		for (int a = 0; a < 8; a++) {
			demo.env[a] = env[a];
			demo.coh[a] = env[a] > 0.f ? 0.8f : 0.f;
			demo.phase[a] = 0.2f * (a - 3);
			demo.model[a] = (int8_t) a;
		}
		demo.art = 0;
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		const float pad = 2.f;
		const float cw = (w - 2.f * pad) / N::NumArts;
		const float barTop = pad + 11.f, barBot = h - pad;
		for (int a = 0; a < N::NumArts; a++) {
			const float x = pad + a * cw;
			const NVGcolor c = slotColor(a);
			text(vg, x + 0.5f * cw, pad - 1.f, N::modelShort(s.model[a]), 9.f, a == s.art ? c : semi::theme::dim(),
			     NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
			fillRect(vg, x + 1.f, barTop, cw - 2.f, barBot - barTop, semi::theme::cell());
			const float e = clamp(s.env[a], 0.f, 1.f);
			if (e > 0.01f) {
				const float bh = (barBot - barTop) * e;
				fillRect(vg, x + 1.f, barBot - bh, cw - 2.f, bh, mix(semi::theme::cell(), c, a == s.art ? 1.f : 0.55f));
				// phase tick: offset from the column centre by the phase, width by coherence
				const float ph = clamp(s.phase[a] / (float) M_PI, -1.f, 1.f);
				const float cx = x + 0.5f * cw + ph * 0.45f * cw;
				const float tw = std::max(2.f, (cw - 2.f) * clamp(s.coh[a], 0.f, 1.f) * 0.6f);
				fillRect(vg, cx - 0.5f * tw, barBot - bh - 1.5f, tw, 3.f, semi::theme::text());
			}
		}
	}
};

// ---------------------------------------------------------------------------------------- menus
namespace {

template <typename T>
void addIndex(Menu* menu, const std::string& label, const std::vector<std::string>& labels, T* v, int offset = 0) {
	menu->addChild(createIndexSubmenuItem(label, labels, [=]() { return (size_t) clamp((int) *v - offset, 0, (int) labels.size() - 1); },
	                                      [=](size_t i) { *v = (T) ((int) i + offset); }));
}

// A float in fixed steps (works on MetaModule's menus): the nearest step is ticked.
void addSteps(Menu* menu, const std::string& label, const std::vector<float>& values, const char* fmt, float* v) {
	std::vector<std::string> labels;
	for (float x : values)
		labels.push_back(string::f(fmt, x));
	menu->addChild(createIndexSubmenuItem(label, labels,
	    [=]() {
		    size_t best = 0;
		    for (size_t i = 1; i < values.size(); i++)
			    if (std::abs(values[i] - *v) < std::abs(values[best] - *v))
				    best = i;
		    return best;
	    },
	    [=](size_t i) { *v = values[i]; }));
}

const std::vector<float> kPct{0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};

std::string noteName(int n) {
	static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	return string::f("%s%d (%d)", names[n % 12], n / 12 - 1, n);
}

} // namespace

struct NeurostackWidget : ModuleWidget {
	NeurostackWidget(Neurostack* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Neurostack.svg")));

		auto* mat = semi::createScreenMm<NeurostackMatrixScreen>(Vec(L::MATRIX.x, L::MATRIX.y), Vec(L::MATRIX.w, L::MATRIX.h));
		mat->module = module;
		addChild(mat);
		auto* scope = semi::createScreenMm<NeurostackScopeScreen>(Vec(L::SCOPE.x, L::SCOPE.y), Vec(L::SCOPE.w, L::SCOPE.h));
		scope->module = module;
		addChild(scope);
		auto* lay = semi::createScreenMm<LayersScreen>(Vec(L::LAYERS.x, L::LAYERS.y), Vec(L::LAYERS.w, L::LAYERS.h));
		lay->module = module;
		addChild(lay);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		addParam(createParamCentered<semi::Button>(at(L::RHY), module, Neurostack::RHY_PARAM));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::RHY_LED), module, Neurostack::RHY_LIGHT));
		addParam(createParamCentered<semi::Button>(at(L::SHAPE), module, Neurostack::SHAPE_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::PRESET), module, Neurostack::PRESET_PARAM));
		addChild(createLightCentered<semi::AmberLedSmall>(at(L::RUN_LED), module, Neurostack::RUN_LIGHT));

		auto* time = createParamCentered<semi::KnobM>(at(L::TIME), module, Neurostack::TIME_PARAM);
		time->snap = true;
		addParam(time);
		addParam(createParamCentered<semi::KnobM>(at(L::XFADE), module, Neurostack::XFADE_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::TONE), module, Neurostack::TONE_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::MOTION), module, Neurostack::MOTION_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::GRIT), module, Neurostack::GRIT_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::DRIVE), module, Neurostack::DRIVE_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::SPLIT), module, Neurostack::SPLIT_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::SUB), module, Neurostack::SUB_PARAM));
		addParam(createParamCentered<semi::KnobMHot>(at(L::VARIATION), module, Neurostack::VARIATION_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::DRIFT), module, Neurostack::DRIFT_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::WIDTH), module, Neurostack::WIDTH_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::LEVEL), module, Neurostack::LEVEL_PARAM));

		addInput(createInputCentered<semi::Jack>(at(L::VOCT), module, Neurostack::VOCT_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::GATE), module, Neurostack::GATE_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::CLK), module, Neurostack::CLK_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RST), module, Neurostack::RST_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::TONE_CV), module, Neurostack::TONE_CV_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::SC), module, Neurostack::SC_INPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_L), module, Neurostack::OUT_L_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_R), module, Neurostack::OUT_R_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::TRIG), module, Neurostack::TRIG_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::ART), module, Neurostack::ART_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::PITCH), module, Neurostack::PITCH_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Neurostack*>(module);
		if (!m)
			return;
		Settings* s = &m->set;
		menu->addChild(new MenuSeparator);

		std::vector<std::string> presets;
		for (auto& p : presetLibrary())
			presets.push_back(p.name);
		menu->addChild(createIndexSubmenuItem("Preset", presets, [=]() { return (size_t) m->presetIdx.load(); },
		                                      [=](size_t i) { m->presetRequest = (int) i; }));
		std::vector<std::string> rhythms;
		for (auto& r : rhythmLibrary())
			rhythms.push_back(r.name);
		menu->addChild(createIndexSubmenuItem("Rhythm", rhythms, [=]() { return (size_t) clamp(m->rhythmIdx.load(), 0, (int) rhythms.size() - 1); },
		                                      [=](size_t i) { m->loadRhythm((int) i); }));
		menu->addChild(createBoolPtrMenuItem("Rhythm on", "", &s->rhythmOn));
		menu->addChild(createIndexSubmenuItem("Matrix shape", kShapeLong, [=]() { return (size_t) m->shape.load(); },
		                                      [=](size_t i) { m->matrixShape((int) i); }));

		menu->addChild(createSubmenuItem("Steps", "", [=](Menu* sub) {
			std::vector<std::string> trigs{"HOLD"};
			for (int a = 0; a < N::NumArts; a++)
				trigs.push_back(string::f("%d %s", a + 1, N::modelName(s->slot[(size_t) a].model)));
			trigs.push_back("MUTE");
			for (int i = 0; i < N::kSteps; i++) {
				auto& st = m->brain.steps[(size_t) i];
				const int t = st.trig.load();
				const std::string right = t == N::kTrigHold ? "HOLD" : t == N::kTrigMute ? "MUTE" : N::modelShort(s->slot[(size_t) (t - 1)].model);
				sub->addChild(createSubmenuItem(string::f("Step %d", i + 1), right, [=](Menu* sm) {
					auto* stp = &m->brain.steps[(size_t) i];
					sm->addChild(createIndexSubmenuItem("Trig", trigs, [=]() { return (size_t) stp->trig.load(); },
					                                    [=](size_t k) { stp->trig.store((int) k); }));
					sm->addChild(createIndexSubmenuItem("Octave", {"-2", "-1", "0", "+1", "+2"}, [=]() { return (size_t) (stp->oct.load() + 2); },
					                                    [=](size_t k) { stp->oct.store((int) k - 2); }));
					const std::vector<float> durs{0.25f, 0.5f, 1, 2, 3, 4, 6, 8, 12, 16};
					std::vector<std::string> dl;
					for (float d : durs)
						dl.push_back(string::f("%g", d));
					sm->addChild(createIndexSubmenuItem("Duration", dl,
					    [=]() {
						    size_t b = 0;
						    for (size_t k = 1; k < durs.size(); k++)
							    if (std::abs(durs[k] - stp->dur.load()) < std::abs(durs[b] - stp->dur.load()))
								    b = k;
						    return b;
					    },
					    [=](size_t k) { stp->dur.store(durs[k]); }));
					const std::vector<float> pr{0, 25, 50, 75, 100};
					sm->addChild(createIndexSubmenuItem("Probability", {"0 %", "25 %", "50 %", "75 %", "100 %"},
					    [=]() { return (size_t) clamp((int) std::round(stp->prob.load() / 25.f), 0, 4); },
					    [=](size_t k) { stp->prob.store(pr[k]); }));
					const std::vector<float> sw{-0.6f, 0.f, 0.7f};
					sm->addChild(createIndexSubmenuItem("Sweep", {"TONE DOWN", "NONE", "TONE UP"},
					    [=]() { const float v = stp->mod.load(); return (size_t) (v < -0.05f ? 0 : v > 0.05f ? 2 : 1); },
					    [=](size_t k) { stp->mod.store(sw[k]); }));
				}));
			}
			sub->addChild(new MenuSeparator);
			sub->addChild(createMenuItem("Randomize trigs", "", [=]() { m->randomizeTrigs(); }));
			sub->addChild(createMenuItem("Default steps", "", [=]() { m->parsePattern("R3 . . W2 . F.5 G2 . R2 . T2 . S1 N1 H1+ M"); }));
		}));

		menu->addChild(createSubmenuItem("Slots", "", [=](Menu* sub) {
			std::vector<std::string> models;
			for (int k = 0; k < N::NumModels; k++)
				models.push_back(N::modelName(k));
			for (int a = 0; a < N::NumArts; a++) {
				SlotSet* sl = &s->slot[(size_t) a];
				sub->addChild(createSubmenuItem(string::f("Slot %d", a + 1), N::modelName(sl->model), [=](Menu* sm) {
					addIndex(sm, "Model", models, &sl->model);
					addSteps(sm, "Level", kPct, "%.0f %%", &sl->level);
					addSteps(sm, "Tone", kPct, "%.0f %%", &sl->tone);
					addSteps(sm, "Motion", kPct, "%.0f %%", &sl->motion);
					addSteps(sm, "Grit", kPct, "%.0f %%", &sl->grit);
					addSteps(sm, "Attack", {0, 20, 40, 80, 120, 180, 300, 500}, "%.0f ms", &sl->attack);
					sm->addChild(createBoolPtrMenuItem("Mute", "", &sl->mute));
					sm->addChild(createBoolPtrMenuItem("Choke (off: stacks)", "", &sl->choke));
				}));
			}
		}));

		menu->addChild(createSubmenuItem("Key", "", [=](Menu* sub) {
			addIndex(sub, "Source", kSourceNames, &s->source);
			std::vector<std::string> roots;
			for (int n = 24; n <= 60; n++)
				roots.push_back(noteName(n));
			addIndex(sub, "Root (SELF)", roots, &s->root, 24);
			addIndex(sub, "Octave", {"-2", "-1", "0", "+1", "+2"}, &s->octave, -2);
			addSteps(sub, "Glide", {0, 10, 30, 60, 120, 250, 500}, "%.0f ms", &s->glide);
			addSteps(sub, "Release", {5, 20, 40, 60, 90, 150, 300, 800}, "%.0f ms", &s->release);
		}));
		menu->addChild(createSubmenuItem("Walk", "", [=](Menu* sub) {
			addSteps(sub, "Swing", kPct, "%.0f %%", &s->swing);
			std::vector<std::string> starts;
			for (int i = 1; i <= 16; i++)
				starts.push_back(std::to_string(i));
			addIndex(sub, "Start step", starts, &s->start, 1);
			addIndex(sub, "Reset", kResetNames, &s->resetIdx);
			std::vector<std::string> seeds;
			for (int i = 1; i <= 32; i++)
				seeds.push_back(std::to_string(i));
			addIndex(sub, "Seed", seeds, &s->seed, 1);
			addIndex(sub, "Duration unit", kDurUnitNames, &s->durUnit);
		}));
		menu->addChild(createSubmenuItem("Groove", "", [=](Menu* sub) {
			addIndex(sub, "Scale", kScaleNames, &s->scale);
			addIndex(sub, "Edit budget (per 8 bars)", {"1", "2", "3", "4", "5", "6", "7", "8"}, &s->budget, 1);
			addSteps(sub, "Gate", {40, 50, 60, 70, 80, 90, 100}, "%.0f %%", &s->gate);
			addSteps(sub, "Velocity > tone", kPct, "%.0f %%", &s->velTone);
			addSteps(sub, "Bend time", {10, 30, 60, 90, 120, 180, 300, 600, 1000}, "%.0f ms", &s->bendTime);
			addIndex(sub, "Rhythm rate", kRateNames, &s->rhyRate);
		}));
		menu->addChild(createSubmenuItem("Phrase", "", [=](Menu* sub) {
			addSteps(sub, "Arc", kPct, "%.0f %%", &s->arcAmt);
			addIndex(sub, "Arc length", kArcNames, &s->arcIdx);
			addIndex(sub, "Drift rate", kDriftNames, &s->driftIdx);
		}));
		menu->addChild(createSubmenuItem("Kick / duck", "", [=](Menu* sub) {
			addIndex(sub, "Kick template", kKickNames, &s->kick);
			addSteps(sub, "Duck", kPct, "%.0f %%", &s->duck);
			addSteps(sub, "Duck release", {20, 50, 100, 150, 250, 400}, "%.0f ms", &s->duckRel);
			addSteps(sub, "Snare gap", {0, 10, 20, 30, 40, 60, 100, 200}, "%.0f ms", &s->snareGap);
		}));
		menu->addChild(createSubmenuItem("Low end", "", [=](Menu* sub) {
			addSteps(sub, "Sub drive", kPct, "%.0f %%", &s->subDrive);
			addIndex(sub, "Sub octave", kSubOctNames, &s->subOct);
			addSteps(sub, "Body", kPct, "%.0f %%", &s->body);
		}));
		menu->addChild(createSubmenuItem("Clock", "", [=](Menu* sub) {
			addIndex(sub, "CLK input", kPpqNames, &m->ppqIdx);
			addIndex(sub, "Tempo (CLK unpatched)", kBpmNames, &m->bpmIdx);
		}));
	}
};

Model* modelNeurostack = createModel<Neurostack, NeurostackWidget>("Neurostack");
