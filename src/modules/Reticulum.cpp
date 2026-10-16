// RETICULUM — Rack / MetaModule port of RETICULUM (engine: engines/reticulum, unchanged from
// semierendonmez-commits/reticulum Source/engine).
//
// The patch (as in the plugin): MANUAL GATE (pad, GATE in, its own clock) re-rolls rete's
// matrix, the two RANDOM VALUES banks and MG-16; the banks drive the eight node gains; rete's
// L/R goes through artifex and (optionally) Beads to the outputs. LEAK / SCALE / LVL and the
// node gains carry one modulator each (RAND / WALK / DRUNK), sharing one mode and one rate here.
// Rack audio is +-5 V = engine +-1. EXC is in volts (the plug-in's EXT column), into excite.
//
// What the panel does not hold is in the context menu (and saved with the patch): the gate
// destinations, the clock division, the banks' ranges, MG-16, artifex and Beads settings,
// the output stage, the scope view.

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/ReticulumLayout.hpp"
#include "reticulum/Engine.h"

#include <cstdio>

namespace L = semi::layout::reticulum;
using semi::Screen;

namespace {

constexpr int kBlock = 32;          // engine block (= its internal chunk); adds 32 samples of latency
constexpr float kVoltsPerUnit = 5.f;
constexpr int kPts = 128;           // scope points per snapshot (decimated again when drawn)
constexpr int kScopeDecim = 3;      // one scope point every 3 samples
constexpr uint32_t kInitSeed = 0x5eedf00du;

struct Snapshot {
	float x[kPts]{}, y[kPts]{};     // scope, engine units (zoom applied when drawn)
	float zoom = 1.f;
	int xy = 0;                     // 0 L/R, 1 M/S
	float m[rt::kNodes][rt::kNodes]{};
	float rho = 0.f;
	uint32_t seed = kInitSeed;
	float act[rt::kNodes]{};        // node activity 0..1
	bool gate = false;
	uint32_t reseeds = 0;
};

// rho|M|: the largest eigenvalue of |M| by power iteration (as on the plug-in's NET view).
float spectralRadius(const float m[rt::kNodes][rt::kNodes]) {
	float v[rt::kNodes];
	for (float& x : v) x = 1.f;
	float rho = 0.f;
	for (int it = 0; it < 40; ++it) {
		float nv[rt::kNodes]{}, norm = 0.f;
		for (int i = 0; i < rt::kNodes; ++i) {
			for (int j = 0; j < rt::kNodes; ++j) nv[i] += std::fabs(m[i][j]) * v[j];
			norm = std::max(norm, nv[i]);
		}
		for (int i = 0; i < rt::kNodes; ++i) v[i] = nv[i] / std::max(1e-6f, norm);
		rho = norm;
	}
	return rho;
}

// Everything that is not a knob. Saved in dataToJson, edited in the context menu.
struct Settings {
	int lfoMode = rt::mOff;          // panel button: OFF / RAND / WALK / DRUNK
	int clkMode = rt::cOff;          // panel button: HAND / FREE / SYNC
	int modSync = rt::sFree;         // modulator clock
	uint32_t modMask = (1u << rt::kTargets) - 1; // which of N1..N8 LEAK SCALE LVL are modulated
	int clkDiv = 4;                  // SYNC division (index into kClockDivs, 4 = 1/4)
	float chance = 1.f, width = 0.5f, length = 0.f;
	int dest[rt::kDests] = {rt::gGate, rt::gGate, rt::gGate, rt::gGate, rt::gGate, rt::gOff, rt::gGate, rt::gOff};
	int rangeA = 0, rangeB = 0;
	float cvDepth = 1.f, mgRange = 1.f, mgBias = 0.f;
	bool mgSH = true, mgUni = false, mgExcite = false;
	int axMode = 0;
	float axTime = 0.65f, axFbk = 0.35f, axFilter = 0.f, axStereo = 0.f;
	int bdQuality = 0;
	float bdTime = 0.05f, bdSize = 0.5f, bdShape = 0.5f, bdDensity = 0.3f, bdPitch = 0.f, bdFbk = 0.f, bdVerb = 0.f, bdRand = 0.f;
	bool bdFreeze = false;
	int order = rt::oAxBd, clip = 1;
	float width2 = 1.f;
	float bpm = 120.f;               // tempo when CLK is not patched
	int ppqn = 0;                    // CLK pulses per beat (index into kPpqn)
	int scopeXY = 0, scopeSmooth = 1;
};

const std::vector<std::string> kModModes = {"OFF", "RANDOM", "WALK", "DRUNK"};
const std::vector<std::string> kModSyncs = {"FREE (RATE)", "GATE", "4 BAR", "2 BAR", "1 BAR", "1/2", "1/4", "1/8", "1/16", "1/32", "1/4T", "1/8T", "1/16T"};
const std::vector<std::string> kClockDivs = {"4 BAR", "2 BAR", "1 BAR", "1/2", "1/4", "1/8", "1/16", "1/32", "1/4T", "1/8T", "1/16T"};
const std::vector<std::string> kClockModes = {"HAND", "FREE", "SYNC"};
const std::vector<std::string> kGateOuts = {"--", "GATE", "/GATE", "EXT", "/EXT", "TRIG", "LATCH", "/LATCH"};
const std::vector<std::string> kDestNames = {"RETE.RND (new matrix)", "RND.A (roll bank A)", "RND.B (roll bank B)", "MG.16 (new modulations)",
                                             "MOD.CLK (modulators on GATE)", "ARTFX.TRG", "BEADS.SEED", "BEADS.FRZ (held)"};
const std::vector<std::string> kRanges = {"0..10V", "+-5V", "0..5V", "+-10V", "+-1V", "0..1V"};
const std::vector<std::string> kAxModes = {"DELAY", "FLANGER", "FREEZER", "PANNER", "CRUSHER", "SLICER", "PITCHER", "REPLAYER", "SHIFTER"};
const std::vector<std::string> kBdQualities = {"HI-FI", "CLEAN TAPE", "LO-FI", "DIRTY TAPE"};
const std::vector<std::string> kOrders = {"ARTFX > BEADS", "BEADS > ARTFX", "ARTFX || BEADS"};
const std::vector<std::string> kClips = {"OFF", "SOFT", "HARD"};
const std::vector<std::string> kTargetNames = {"N1", "N2", "N3", "N4", "N5", "N6", "N7", "N8", "LEAK", "SCALE", "LVL"};
const int kPpqn[] = {1, 2, 4, 8, 24};
const std::vector<std::string> kPpqnNames = {"1 per beat", "2 per beat", "4 per beat (16ths)", "8 per beat", "24 per beat"};

} // namespace

struct Reticulum : Module {
	enum ParamId {
		ENUMS(NODE_PARAM, 8),
		LEAK_PARAM, SCALE_PARAM, LVL_PARAM,
		RND_PARAM, RATE_PARAM,
		CLK_RATE_PARAM, FLUX_PARAM, ARTFX_PARAM, BEADS_PARAM, LEVEL_PARAM,
		LFO_MODE_PARAM, CLK_MODE_PARAM, GATE_PARAM,
		PARAMS_LEN
	};
	enum InputId { GATE_INPUT, CLK_INPUT, LEAK_CV_INPUT, SCALE_CV_INPUT, EXC_INPUT, INPUTS_LEN };
	enum OutputId { OUT_L_OUTPUT, OUT_R_OUTPUT, OUTPUTS_LEN };
	enum LightId { LFO_RAND_LIGHT, LFO_WALK_LIGHT, LFO_DRUNK_LIGHT, CLK_HAND_LIGHT, CLK_FREE_LIGHT, CLK_SYNC_LIGHT, LIGHTS_LEN };

	rt::Engine engine;
	rt::Params p;
	Settings set;
	dsp::BooleanTrigger lfoTrig, clkModeTrig;
	dsp::SchmittTrigger gateIn, clkIn;

	float inBuf[kBlock]{}, outBuf[2][kBlock]{};
	int pos = 0;

	// external clock -> host transport for the engine (SYNC and tempo-synced modulators)
	double extBeat = 0.0;
	float sinceEdge = 0.f, edgePeriod = 0.5f;
	int edges = 0;

	// scope
	float ringX[kPts]{}, ringY[kPts]{};
	int ringW = 0, decim = 0;
	float smx = 0.f, smy = 0.f, scopePeak = 0.f, zoom = 1.f;

	// matrix view (recomputed when the seed changes)
	uint32_t shownSeed = 0;
	float mat[rt::kNodes][rt::kNodes]{};
	float rho = 0.f;

	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;

	Reticulum() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int i = 0; i < 8; i++)
			configParam(NODE_PARAM + i, 0.f, 1.f, 0.5f, string::f("Node %d gain", i + 1), "%", 0.f, 100.f);
		configParam(LEAK_PARAM, 0.f, 1.f, 0.5f, "Leak", "%", 0.f, 100.f);
		configParam(SCALE_PARAM, 0.f, 1.f, 0.67f, "Scale (matrix drive)", "x", 1000.f, 1.f);
		configParam(LVL_PARAM, 0.f, 1.f, 0.f, "Lvl (excite)", "%", 0.f, 100.f);
		configParam(RND_PARAM, 0.f, 1.f, 0.5f, "Modulation depth", "%", 0.f, 100.f);
		configParam(RATE_PARAM, 0.f, 1.f, std::log(100.f) / std::log(4000.f), "Modulation rate", " Hz", 4000.f, 0.01f);
		configParam(CLK_RATE_PARAM, 0.f, 1.f, std::log(20.f) / std::log(600.f), "Gate clock rate", " Hz", 600.f, 0.05f);
		configParam(FLUX_PARAM, 0.f, 1.f, 0.f, "Gate clock flux", "%", 0.f, 100.f);
		configParam(ARTFX_PARAM, 0.f, 1.f, 0.3f, "Artifex amount", "%", 0.f, 100.f);
		configParam(BEADS_PARAM, 0.f, 1.f, 0.f, "Beads mix", "%", 0.f, 100.f);
		configParam(LEVEL_PARAM, -24.f, 12.f, 0.f, "Level", " dB");
		configButton(LFO_MODE_PARAM, "Modulator mode (OFF / RAND / WALK / DRUNK)");
		configButton(CLK_MODE_PARAM, "Gate clock (HAND / FREE / SYNC)");
		configButton(GATE_PARAM, "Gate pad");
		configInput(GATE_INPUT, "Gate");
		configInput(CLK_INPUT, "Clock (tempo for SYNC and synced modulators)");
		configInput(LEAK_CV_INPUT, "Leak CV (10 %/V)");
		configInput(SCALE_CV_INPUT, "Scale CV (10 %/V)");
		configInput(EXC_INPUT, "Excite (volts, scaled by LVL)");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		engine.prepare(48000.0, kBlock);
		engine.reseedNow(kInitSeed);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		engine.prepare(e.sampleRate, kBlock);
		pos = 0;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		set = Settings{};
		engine.reset();
		engine.reseedNow(kInitSeed);
	}

	// The plug-in's RANDOMIZE (nodes + core) at full amount, and a new matrix.
	void onRandomize(const RandomizeEvent& e) override {
		for (int i = 0; i < 8; i++)
			params[NODE_PARAM + i].setValue(0.05f + 0.95f * random::uniform());
		params[LEAK_PARAM].setValue(0.1f + 0.8f * random::uniform());
		params[SCALE_PARAM].setValue(0.45f + 0.5f * random::uniform());   // below ~0.45 the net mostly falls silent
		params[LVL_PARAM].setValue(random::uniform());
		engine.reseedNow(random::u32() | 1u);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		auto i = [&](const char* k, int v) { json_object_set_new(root, k, json_integer(v)); };
		auto f = [&](const char* k, float v) { json_object_set_new(root, k, json_real(v)); };
		i("seed", (json_int_t) engine.seed());
		i("lfoMode", set.lfoMode); i("clkMode", set.clkMode); i("modSync", set.modSync); i("modMask", (json_int_t) set.modMask);
		i("clkDiv", set.clkDiv); f("chance", set.chance); f("width", set.width); f("length", set.length);
		json_t* d = json_array();
		for (int k = 0; k < rt::kDests; k++) json_array_append_new(d, json_integer(set.dest[k]));
		json_object_set_new(root, "dest", d);
		i("rangeA", set.rangeA); i("rangeB", set.rangeB); f("cvDepth", set.cvDepth);
		f("mgRange", set.mgRange); f("mgBias", set.mgBias); i("mgSH", set.mgSH); i("mgUni", set.mgUni); i("mgExcite", set.mgExcite);
		i("axMode", set.axMode); f("axTime", set.axTime); f("axFbk", set.axFbk); f("axFilter", set.axFilter); f("axStereo", set.axStereo);
		i("bdQuality", set.bdQuality); f("bdTime", set.bdTime); f("bdSize", set.bdSize); f("bdShape", set.bdShape);
		f("bdDensity", set.bdDensity); f("bdPitch", set.bdPitch); f("bdFbk", set.bdFbk); f("bdVerb", set.bdVerb);
		f("bdRand", set.bdRand); i("bdFreeze", set.bdFreeze);
		i("order", set.order); i("clip", set.clip); f("width2", set.width2); f("bpm", set.bpm); i("ppqn", set.ppqn);
		i("scopeXY", set.scopeXY); i("scopeSmooth", set.scopeSmooth);
		return root;
	}

	void dataFromJson(json_t* root) override {
		auto gi = [&](const char* k, int& v, int lo, int hi) {
			if (json_t* j = json_object_get(root, k)) v = clamp((int) json_integer_value(j), lo, hi);
		};
		auto gb = [&](const char* k, bool& v) {
			if (json_t* j = json_object_get(root, k)) v = json_integer_value(j) != 0;
		};
		auto gf = [&](const char* k, float& v, float lo, float hi) {
			if (json_t* j = json_object_get(root, k)) v = clamp((float) json_number_value(j), lo, hi);
		};
		Settings s;
		gi("lfoMode", s.lfoMode, 0, rt::kModModes - 1); gi("clkMode", s.clkMode, 0, rt::kClockModes - 1);
		gi("modSync", s.modSync, 0, rt::kModSyncs - 1);
		if (json_t* j = json_object_get(root, "modMask")) s.modMask = (uint32_t) json_integer_value(j) & ((1u << rt::kTargets) - 1);
		gi("clkDiv", s.clkDiv, 0, (int) kClockDivs.size() - 1);
		gf("chance", s.chance, 0.f, 1.f); gf("width", s.width, 0.01f, 0.99f); gf("length", s.length, 0.f, 10.f);
		if (json_t* d = json_object_get(root, "dest"))
			for (int k = 0; k < rt::kDests && k < (int) json_array_size(d); k++)
				s.dest[k] = clamp((int) json_integer_value(json_array_get(d, k)), 0, rt::kGateOuts - 1);
		gi("rangeA", s.rangeA, 0, 5); gi("rangeB", s.rangeB, 0, 5); gf("cvDepth", s.cvDepth, 0.f, 2.f);
		gf("mgRange", s.mgRange, 0.f, 1.f); gf("mgBias", s.mgBias, -1.f, 1.f); gb("mgSH", s.mgSH); gb("mgUni", s.mgUni); gb("mgExcite", s.mgExcite);
		gi("axMode", s.axMode, 0, 8); gf("axTime", s.axTime, 0.f, 1.f); gf("axFbk", s.axFbk, 0.f, 1.f);
		gf("axFilter", s.axFilter, -1.f, 1.f); gf("axStereo", s.axStereo, 0.f, 1.f);
		gi("bdQuality", s.bdQuality, 0, 3); gf("bdTime", s.bdTime, 0.f, 1.f); gf("bdSize", s.bdSize, 0.f, 1.f);
		gf("bdShape", s.bdShape, 0.f, 1.f); gf("bdDensity", s.bdDensity, 0.f, 1.f); gf("bdPitch", s.bdPitch, -24.f, 24.f);
		gf("bdFbk", s.bdFbk, 0.f, 1.f); gf("bdVerb", s.bdVerb, 0.f, 1.f); gf("bdRand", s.bdRand, 0.f, 1.f); gb("bdFreeze", s.bdFreeze);
		gi("order", s.order, 0, rt::kOrders - 1); gi("clip", s.clip, 0, 2); gf("width2", s.width2, 0.f, 2.f);
		gf("bpm", s.bpm, 30.f, 300.f); gi("ppqn", s.ppqn, 0, 4);
		gi("scopeXY", s.scopeXY, 0, 1); gi("scopeSmooth", s.scopeSmooth, 0, 2);
		set = s;
		if (json_t* j = json_object_get(root, "seed"))
			engine.reseedNow((uint32_t) json_integer_value(j));
	}

	void fillParams() {
		for (int i = 0; i < 8; i++) p.gain[i] = params[NODE_PARAM + i].getValue();
		p.leak = clamp(params[LEAK_PARAM].getValue() + inputs[LEAK_CV_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		p.scale = clamp(params[SCALE_PARAM].getValue() + inputs[SCALE_CV_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		p.lvl = params[LVL_PARAM].getValue();
		const float rate = 0.01f * std::pow(4000.f, params[RATE_PARAM].getValue());
		for (int t = 0; t < rt::kTargets; t++) {
			rt::ModSettings& m = p.mod[t];
			m.mode = (set.modMask >> t) & 1 ? set.lfoMode : rt::mOff;
			m.sync = set.modSync;
			m.rate = rate;
			m.depth = 1.f;
		}
		p.modAmount = params[RND_PARAM].getValue();

		p.rangeA = set.rangeA;
		p.rangeB = set.rangeB;
		p.cvDepth = set.cvDepth;
		// the wire as patched: A1..A7 -> N1..N7, B1 -> N8; EXC jack and (optionally) MG 1+2 into excite
		for (int k = 0; k < 7; k++) p.wire[k] = 1u << (rt::kColA + k);
		p.wire[7] = 1u << rt::kColB;
		p.wire[rt::kNodes] = (inputs[EXC_INPUT].isConnected() ? 1u << rt::kColExt : 0u)
		                     | (set.mgExcite ? (1u << rt::kColMg) | (1u << (rt::kColMg + 1)) : 0u);
		p.mgRange = set.mgRange;
		p.mgBias = set.mgBias;
		p.mgSH = set.mgSH;
		p.mgUni = set.mgUni;

		p.pad = params[GATE_PARAM].getValue() > 0.f;
		p.length = set.length;
		p.clockMode = set.clkMode;
		p.clockHz = 0.05f * std::pow(600.f, params[CLK_RATE_PARAM].getValue());
		p.clockSync = rt::s4b + set.clkDiv;
		p.chance = set.chance;
		p.flux = params[FLUX_PARAM].getValue();
		p.width = set.width;
		for (int d = 0; d < rt::kDests; d++) p.dest[d] = set.dest[d];

		p.axAmt = params[ARTFX_PARAM].getValue();
		p.axOn = p.axAmt > 1e-4f;
		p.axMode = set.axMode;
		p.axTime = set.axTime;
		p.axFbk = set.axFbk;
		p.axFilter = set.axFilter;
		p.axStereo = set.axStereo;
		p.axGain = 1.f;
		p.axMix = 1.f;

		p.bdMix = params[BEADS_PARAM].getValue();
		p.bdOn = p.bdMix > 1e-4f;
		p.bdQuality = set.bdQuality;
		p.bdTime = set.bdTime;
		p.bdSize = set.bdSize;
		p.bdShape = set.bdShape;
		p.bdDensity = set.bdDensity;
		p.bdPitch = set.bdPitch;
		p.bdFbk = set.bdFbk;
		p.bdVerb = set.bdVerb;
		p.bdRand = set.bdRand;
		p.bdGain = 1.f;
		p.bdFreeze = set.bdFreeze;

		p.order = set.order;
		p.level = params[LEVEL_PARAM].getValue();
		p.width2 = set.width2;
		p.clip = set.clip;

		// transport: CLK patched and running -> its tempo and position; otherwise the engine's own
		const int ppqn = kPpqn[set.ppqn];
		if (inputs[CLK_INPUT].isConnected() && edges >= 2 && sinceEdge < 4.f * edgePeriod + 0.5f) {
			p.playing = true;
			p.bpm = (double) clamp(60.f / (edgePeriod * ppqn), 20.f, 600.f);
			p.ppq = extBeat + std::min(sinceEdge / edgePeriod, 0.999f) / ppqn;
		} else {
			p.playing = false;
			p.bpm = set.bpm;
		}
	}

	void runBlock() {
		fillParams();
		float* in = inputs[EXC_INPUT].isConnected() ? inBuf : nullptr;
		engine.process(p, in, in, outBuf[0], outBuf[1], kBlock);

		// scope: one smoothed point every kScopeDecim samples
		const float k = set.scopeSmooth == 0 ? 1.f : (set.scopeSmooth == 1 ? 0.22f : 0.05f);
		for (int i = 0; i < kBlock; i++) {
			float x = outBuf[0][i], y = outBuf[1][i];
			if (set.scopeXY) {
				const float s = (x - y) * 0.7071f, m = (x + y) * 0.7071f;
				x = s;
				y = m;
			}
			smx += (x - smx) * k;
			smy += (y - smy) * k;
			scopePeak = std::max(scopePeak, std::max(std::fabs(x), std::fabs(y)));
			if (++decim >= kScopeDecim) {
				decim = 0;
				ringX[ringW] = smx;
				ringY[ringW] = smy;
				ringW = (ringW + 1) % kPts;
			}
		}
	}

	void process(const ProcessArgs& args) override {
		if (lfoTrig.process(params[LFO_MODE_PARAM].getValue() > 0.f))
			set.lfoMode = (set.lfoMode + 1) % rt::kModModes;
		if (clkModeTrig.process(params[CLK_MODE_PARAM].getValue() > 0.f))
			set.clkMode = (set.clkMode + 1) % rt::kClockModes;

		gateIn.process(inputs[GATE_INPUT].getVoltage(), 0.1f, 1.f);
		engine.setMidiGate(gateIn.isHigh());

		sinceEdge += args.sampleTime;
		if (clkIn.process(inputs[CLK_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (edges > 0 && sinceEdge > 1e-3f)
				edgePeriod = sinceEdge;
			if (edges > 0)
				extBeat += 1.0 / kPpqn[set.ppqn];
			edges++;
			sinceEdge = 0.f;
		}
		if (!inputs[CLK_INPUT].isConnected())
			edges = 0;

		inBuf[pos] = inputs[EXC_INPUT].getVoltage() / kVoltsPerUnit;
		outputs[OUT_L_OUTPUT].setVoltage(outBuf[0][pos] * kVoltsPerUnit);
		outputs[OUT_R_OUTPUT].setVoltage(outBuf[1][pos] * kVoltsPerUnit);
		if (++pos >= kBlock) {
			pos = 0;
			runBlock();
		}

		if (ui.tick(args.sampleTime))
			publish();
	}

	void publish() {
		for (int i = 0; i < 3; i++) {
			lights[LFO_RAND_LIGHT + i].setBrightness(set.lfoMode == i + 1 ? 1.f : 0.f);
			lights[CLK_HAND_LIGHT + i].setBrightness(set.clkMode == i ? 1.f : 0.f);
		}

		const uint32_t seed = engine.vis.seed.load(std::memory_order_relaxed);
		if (seed != shownSeed) {
			shownSeed = seed;
			rt::Rete::matrixFor(seed, mat);
			rho = spectralRadius(mat);
		}

		// auto zoom as on the plug-in: quiet nets fill the screen too (x1 .. x16)
		const float target = clamp(0.85f / std::max(scopePeak, 1e-4f), 1.f, 16.f);
		zoom += (target - zoom) * (target < zoom ? 0.5f : 0.08f);
		scopePeak = 0.f;

		Snapshot& s = snap.writeSlot();
		for (int i = 0; i < kPts; i++) {
			const int j = (ringW + i) % kPts;
			s.x[i] = ringX[j];
			s.y[i] = ringY[j];
		}
		s.zoom = zoom;
		s.xy = set.scopeXY;
		std::memcpy(s.m, mat, sizeof mat);
		s.rho = rho;
		s.seed = seed;
		for (int k = 0; k < rt::kNodes; k++)
			s.act[k] = clamp(engine.vis.node[k].load(std::memory_order_relaxed) * 1.6f, 0.f, 1.f);
		s.gate = engine.vis.gateIn.load(std::memory_order_relaxed);
		s.reseeds = engine.vis.reseeds.load(std::memory_order_relaxed);
		snap.publish();
	}
};

// ---------------------------------------------------------------------------- screens

namespace {

void demoScope(Snapshot& s) {
	for (int i = 0; i < kPts; i++) {
		const float t = (float) i / kPts * 2.f * M_PI;
		const float n = std::sin(i * 12.9898f) * 0.06f;
		s.x[i] = 0.55f * std::sin(3.f * t) + n;
		s.y[i] = 0.5f * std::sin(2.f * t + 0.6f) - n;
	}
	s.zoom = 1.3f;
	rt::Rete::matrixFor(kInitSeed, s.m);
	s.rho = spectralRadius(s.m);
	s.seed = kInitSeed;
	const float act[rt::kNodes] = {0.9f, 0.4f, 0.7f, 0.2f, 1.f, 0.55f, 0.3f, 0.8f};
	for (int k = 0; k < rt::kNodes; k++) s.act[k] = act[k];
	s.gate = true;
}

} // namespace

// The output as X / Y (L/R or M/S), auto-zoomed. Changes only when the snapshot does.
struct ReticulumScopeScreen : Screen {
	Reticulum* module = nullptr;
	Snapshot demo;

	ReticulumScopeScreen() { demoScope(demo); }

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		const float cx = w * 0.5f, cy = h * 0.5f, half = std::min(w, h) * 0.5f - 4.f;
		const float sc = half * 0.92f * s.zoom;

		line(vg, cx, 4.f, cx, h - 4.f, semi::theme::grid());
		line(vg, 4.f, cy, w - 4.f, cy, semi::theme::grid());
		dashed(vg, cx - half * 0.5f, cy - half * 0.5f, cx + half * 0.5f, cy - half * 0.5f, semi::theme::grid());
		dashed(vg, cx - half * 0.5f, cy + half * 0.5f, cx + half * 0.5f, cy + half * 0.5f, semi::theme::grid());

		float xs[kPts], ys[kPts];
		float mx = 0.f, my = 0.f;
		for (int i = 0; i < kPts; i++) {
			xs[i] = cx + clamp(s.x[i] * sc, -half * 1.05f, half * 1.05f);
			ys[i] = cy - clamp(s.y[i] * sc, -half * 1.05f, half * 1.05f);
			mx += s.x[i];
			my += s.y[i];
		}
		path(vg, xs, ys, kPts, semi::theme::cyanDim(), semi::platform::kThinStroke);
		// the newest quarter brighter, the newest points magenta
		const int q = kPts / 4;
		path(vg, xs + kPts - q, ys + kPts - q, q, semi::theme::cyan());
		path(vg, xs + kPts - 6, ys + kPts - 6, 6, semi::theme::magenta());

		// centroid
		const float px = cx + clamp(mx / kPts * sc, -half, half), py = cy - clamp(my / kPts * sc, -half, half);
		line(vg, px - 4.f, py, px + 4.f, py, semi::theme::amber(), semi::platform::kStroke);
		line(vg, px, py - 4.f, px, py + 4.f, semi::theme::amber(), semi::platform::kStroke);

		text(vg, 4.f, 3.f, s.xy ? "M/S" : "L/R", 10.f, semi::theme::dim());
		char buf[16];
		std::snprintf(buf, sizeof buf, "x%.1f", s.zoom);
		text(vg, w - 4.f, 3.f, buf, 10.f, semi::theme::text(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
	}
};

// 8x8 heat map of the matrix the seed makes (cyan +, magenta -), rho|M|, the seed,
// the gate and each node's activity.
struct ReticulumMatrixScreen : Screen {
	Reticulum* module = nullptr;
	Snapshot demo;

	ReticulumMatrixScreen() { demoScope(demo); }

	static NVGcolor shade(float w) {
		// four levels per sign keep frames stable and the colours opaque
		const float a = std::min(1.f, std::fabs(w));
		const float lv = std::ceil(a * 4.f) / 4.f;
		return nvgLerpRGBA(semi::theme::cell(), w >= 0.f ? semi::theme::cyan() : semi::theme::magenta(), 0.15f + 0.85f * lv);
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		const float pad = 3.f;
		const float side = h - 2.f * pad;
		const float c = side / rt::kNodes;
		for (int i = 0; i < rt::kNodes; i++)
			for (int j = 0; j < rt::kNodes; j++)
				fillRect(vg, pad + j * c, pad + i * c, c - 1.f, c - 1.f, shade(s.m[i][j]));

		const float tx = pad + side + 5.f;
		char buf[24];
		std::snprintf(buf, sizeof buf, "RHO %.2f", s.rho);
		text(vg, tx, pad, buf, 10.f, semi::theme::text());
		text(vg, tx, pad + 11.f, "SEED", 9.f, semi::theme::dim());
		std::snprintf(buf, sizeof buf, "%08X", (unsigned) s.seed);
		text(vg, tx, pad + 21.f, buf, 10.f, semi::theme::amber());

		// gate block + node activity bars
		const float by = pad + 33.f, bh = h - pad - by;
		fillRect(vg, tx, by, 6.f, bh, s.gate ? semi::theme::magenta() : semi::theme::magentaDeep());
		const float bx0 = tx + 9.f, bw = std::max(2.6f, (w - pad - bx0) / rt::kNodes - 1.f);
		for (int k = 0; k < rt::kNodes; k++) {
			const float x = bx0 + k * (bw + 1.f);
			const float hh = std::round(bh * s.act[k]);
			fillRect(vg, x, by, bw, bh, semi::theme::cell());
			if (hh > 0.f)
				fillRect(vg, x, by + bh - hh, bw, hh, semi::theme::cyan());
		}
	}
};

// ---------------------------------------------------------------------------- menu helpers

namespace {

// A float setting in the context menu: a slider in Rack, a list of steps on MetaModule
// (whose Options page shows submenus reliably).
struct FloatSetting : Quantity {
	float* v;
	float lo, hi, def, mult;
	std::string label, unit;
	FloatSetting(float* v, float lo, float hi, float def, std::string label, std::string unit, float mult)
	    : v(v), lo(lo), hi(hi), def(def), mult(mult), label(std::move(label)), unit(std::move(unit)) {}
	void setValue(float x) override { *v = clamp(x, lo, hi); }
	float getValue() override { return *v; }
	float getMinValue() override { return lo; }
	float getMaxValue() override { return hi; }
	float getDefaultValue() override { return def; }
	float getDisplayValue() override { return *v * mult; }
	void setDisplayValue(float d) override { setValue(d / mult); }
	std::string getLabel() override { return label; }
	std::string getUnit() override { return unit; }
};

#ifndef METAMODULE
struct SettingSlider : ui::Slider {
	explicit SettingSlider(Quantity* q) {
		quantity = q;
		box.size.x = 220.f;
	}
	~SettingSlider() override { delete quantity; }
};
#endif

void addFloat(Menu* menu, std::string label, float* v, float lo, float hi, float def, std::string unit = "%", float mult = 100.f) {
#ifdef METAMODULE
	constexpr int kSteps = 11;
	std::vector<std::string> labels;
	for (int i = 0; i < kSteps; i++) {
		const float x = lo + (hi - lo) * i / (kSteps - 1);
		labels.push_back(string::f("%.3g%s", x * mult, unit.c_str()));
	}
	menu->addChild(createIndexSubmenuItem(label, labels,
		[=]() { return (size_t) clamp((int) std::round((*v - lo) / (hi - lo) * (kSteps - 1)), 0, kSteps - 1); },
		[=](size_t i) { *v = lo + (hi - lo) * i / (kSteps - 1); }));
#else
	menu->addChild(new SettingSlider(new FloatSetting(v, lo, hi, def, label, unit, mult)));
#endif
}

template <typename T>
void addIndex(Menu* menu, std::string label, const std::vector<std::string>& labels, T* v) {
	menu->addChild(createIndexSubmenuItem(label, labels,
		[=]() { return (size_t) *v; },
		[=](size_t i) { *v = (T) i; }));
}

} // namespace

struct ReticulumWidget : ModuleWidget {
	ReticulumWidget(Reticulum* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Reticulum.svg")));

		auto* scope = semi::createScreenMm<ReticulumScopeScreen>(Vec(L::SCOPE.x, L::SCOPE.y), Vec(L::SCOPE.w, L::SCOPE.h));
		scope->module = module;
		addChild(scope);
		auto* matrix = semi::createScreenMm<ReticulumMatrixScreen>(Vec(L::MATRIX.x, L::MATRIX.y), Vec(L::MATRIX.w, L::MATRIX.h));
		matrix->module = module;
		addChild(matrix);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::LFO_RAND), module, Reticulum::LFO_RAND_LIGHT));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::LFO_WALK), module, Reticulum::LFO_WALK_LIGHT));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::LFO_DRUNK), module, Reticulum::LFO_DRUNK_LIGHT));
		addParam(createParamCentered<semi::Button>(at(L::LFO_MODE), module, Reticulum::LFO_MODE_PARAM));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::CLK_HAND), module, Reticulum::CLK_HAND_LIGHT));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::CLK_FREE), module, Reticulum::CLK_FREE_LIGHT));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::CLK_SYNC), module, Reticulum::CLK_SYNC_LIGHT));
		addParam(createParamCentered<semi::Button>(at(L::CLK_MODE), module, Reticulum::CLK_MODE_PARAM));
		addParam(createParamCentered<semi::PadMagenta>(at(L::GATE), module, Reticulum::GATE_PARAM));

		const L::P nodes[8] = {L::N1, L::N2, L::N3, L::N4, L::N5, L::N6, L::N7, L::N8};
		for (int i = 0; i < 8; i++)
			addParam(createParamCentered<semi::KnobS>(at(nodes[i]), module, Reticulum::NODE_PARAM + i));
		addParam(createParamCentered<semi::KnobM>(at(L::LEAK), module, Reticulum::LEAK_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::SCALE), module, Reticulum::SCALE_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::LVL), module, Reticulum::LVL_PARAM));
		addParam(createParamCentered<semi::KnobMHot>(at(L::RND), module, Reticulum::RND_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::RATE), module, Reticulum::RATE_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::FREE), module, Reticulum::CLK_RATE_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::FLUX), module, Reticulum::FLUX_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::ARTFX), module, Reticulum::ARTFX_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::BEADS), module, Reticulum::BEADS_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::LEVEL), module, Reticulum::LEVEL_PARAM));

		addInput(createInputCentered<semi::Jack>(at(L::GATE_IN), module, Reticulum::GATE_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::CLK_IN), module, Reticulum::CLK_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::LEAK_CV), module, Reticulum::LEAK_CV_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::SCALE_CV), module, Reticulum::SCALE_CV_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::EXC_IN), module, Reticulum::EXC_INPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_L), module, Reticulum::OUT_L_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_R), module, Reticulum::OUT_R_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Reticulum*>(module);
		if (!m)
			return;
		Settings* s = &m->set;

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel(string::f("Seed %08X", (unsigned) m->engine.seed())));
		menu->addChild(createMenuItem("Reseed (new matrix)", "", [=]() { m->engine.reseedNow(random::u32() | 1u); }));
		menu->addChild(createMenuItem("Tap the gate", "", [=]() { m->engine.fire(); }));
		menu->addChild(createMenuItem("Push banks A, B and MG-16", "", [=]() {
			m->engine.push(0);
			m->engine.push(1);
			m->engine.push(2);
		}));

		menu->addChild(createSubmenuItem("Modulators", "", [=](Menu* sub) {
			addIndex(sub, "Mode", kModModes, &s->lfoMode);
			addIndex(sub, "Clock", kModSyncs, &s->modSync);
			sub->addChild(new MenuSeparator);
			sub->addChild(createMenuLabel("Targets"));
			for (int t = 0; t < rt::kTargets; t++)
				sub->addChild(createCheckMenuItem(kTargetNames[t], "",
					[=]() { return (s->modMask >> t) & 1; },
					[=]() { s->modMask ^= 1u << t; }));
		}));

		menu->addChild(createSubmenuItem("Gate", "", [=](Menu* sub) {
			addIndex(sub, "Clock", kClockModes, &s->clkMode);
			addIndex(sub, "SYNC division", kClockDivs, &s->clkDiv);
			addFloat(sub, "Chance", &s->chance, 0.f, 1.f, 1.f);
			addFloat(sub, "Clock width", &s->width, 0.01f, 0.99f, 0.5f);
			addFloat(sub, "Length (EXT outputs)", &s->length, 0.f, 10.f, 0.f, " s", 1.f);
			addIndex(sub, "CLK input", kPpqnNames, &s->ppqn);
			addFloat(sub, "Tempo (CLK unpatched)", &s->bpm, 30.f, 300.f, 120.f, " BPM", 1.f);
			sub->addChild(new MenuSeparator);
			sub->addChild(createMenuLabel("Destinations"));
			for (int d = 0; d < rt::kDests; d++)
				addIndex(sub, kDestNames[d], kGateOuts, &s->dest[d]);
		}));

		menu->addChild(createSubmenuItem("Random sources", "", [=](Menu* sub) {
			addIndex(sub, "Bank A range", kRanges, &s->rangeA);
			addIndex(sub, "Bank B range", kRanges, &s->rangeB);
			addFloat(sub, "Node CV depth", &s->cvDepth, 0.f, 2.f, 1.f);
			addFloat(sub, "MG-16 range", &s->mgRange, 0.f, 1.f, 1.f);
			addFloat(sub, "MG-16 bias", &s->mgBias, -1.f, 1.f, 0.f, "", 1.f);
			sub->addChild(createBoolPtrMenuItem("MG-16 S&H", "", &s->mgSH));
			sub->addChild(createBoolPtrMenuItem("MG-16 unipolar", "", &s->mgUni));
			sub->addChild(createBoolPtrMenuItem("MG-16 1+2 into excite", "", &s->mgExcite));
		}));

		menu->addChild(createSubmenuItem("Artifex", "", [=](Menu* sub) {
			addIndex(sub, "Mode", kAxModes, &s->axMode);
			addFloat(sub, "Time", &s->axTime, 0.f, 1.f, 0.65f);
			addFloat(sub, "Feedback", &s->axFbk, 0.f, 1.f, 0.35f);
			addFloat(sub, "Filter", &s->axFilter, -1.f, 1.f, 0.f, "", 1.f);
			addFloat(sub, "Stereo", &s->axStereo, 0.f, 1.f, 0.f);
		}));

		menu->addChild(createSubmenuItem("Beads", "", [=](Menu* sub) {
			addIndex(sub, "Quality", kBdQualities, &s->bdQuality);
			addFloat(sub, "Time", &s->bdTime, 0.f, 1.f, 0.05f);
			addFloat(sub, "Size", &s->bdSize, 0.f, 1.f, 0.5f);
			addFloat(sub, "Shape", &s->bdShape, 0.f, 1.f, 0.5f);
			addFloat(sub, "Density", &s->bdDensity, 0.f, 1.f, 0.3f);
			addFloat(sub, "Pitch", &s->bdPitch, -24.f, 24.f, 0.f, " st", 1.f);
			addFloat(sub, "Feedback", &s->bdFbk, 0.f, 1.f, 0.f);
			addFloat(sub, "Reverb", &s->bdVerb, 0.f, 1.f, 0.f);
			addFloat(sub, "Random", &s->bdRand, 0.f, 1.f, 0.f);
			sub->addChild(createBoolPtrMenuItem("Freeze", "", &s->bdFreeze));
		}));

		menu->addChild(createSubmenuItem("Output", "", [=](Menu* sub) {
			addIndex(sub, "FX order", kOrders, &s->order);
			addIndex(sub, "Clip", kClips, &s->clip);
			addFloat(sub, "Stereo width", &s->width2, 0.f, 2.f, 1.f);
			addIndex(sub, "Scope", std::vector<std::string>{"X/Y = L/R", "X/Y = S/M"}, &s->scopeXY);
			addIndex(sub, "Scope smoothing", std::vector<std::string>{"RAW", "LO", "HI"}, &s->scopeSmooth);
		}));
	}
};

Model* modelReticulum = createModel<Reticulum, ReticulumWidget>("Reticulum");
