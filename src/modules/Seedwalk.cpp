// SEEDWALK — Rack / MetaModule port of SEEDWALK (engine: engines/seedwalk, vendored from
// semierendonmez-commits/seedwalk Source/engine; namespace sw, see the notes at the top of
// engines/seedwalk/Pinball.h and Rate.h).
//
// Pinball 2's Markov ball walks sixteen pinned rete nets. When the ball fires a step, rete
// glides from where it is to that step's net (the 8 x 8 matrix interpolated, the integrators
// never reset). The 17 x 17 path matrix is drawn by SHAPE from the nets' heat and distance
// (chain, cool down, descent, nearest ...), so the walk goes from chaotic nets to calm ones.
//
// Panel:   STEP (step time, or clocks per step with CLK patched), GLIDE, CHAOS (drive: every
//          net's SCALE, hotter = more chaotic), SEED (the sixteen nets), LVL, FROM / TO (the
//          walk's range); SHAPE, SPREAD, DIP, EXCITE, FX (artifex mix), FX MODE; PIN, HOLD, RND.
// Jacks:   CLK (1/16 clock; a step = STEP clocks), RST, CHAOS CV, X / Y (pick a cell of the
//          4 x 4 grid: a change sends the ball there), IN (audio into excite), HOLD (gate),
//          RND (trigger), FROM / TO CV (1.5 nets per volt); out L / R (+-5 V), NET (current
//          net, 1/12 V per net), EOC (the walk is back where it started).
// Menu:    factory walks, glide curve, keep size, clip, internal tempo, Beads, RND scope,
//          randomise one net.
//
// Semi-rack additions (not in the plugin):
//   RANGE   the ball only visits nets FROM..TO. Every shape is drawn on that subset (the survey
//           restricted to it, the shape's matrix mapped back), rows of nets outside lead back
//           in. FROM > TO wraps round 16 > 1 (FROM 15, TO 2 = 15 16 1 2). FROM == TO sits on
//           that net. Two nets ping-pong for every shape but CLEAR. X / Y outside the range
//           land on a net inside it.
//   HOLD    latch (button) or gate: the ball stops, the glide freezes where it is (mid-glide
//           too) and the clock is ignored; jumps and resets wait. Release carries on from there.
//           PIN works while held.
//   RND     re-rolls the nets in the range (menu: all 16) from the walk's recipe with fresh
//           seeds and heats, and re-rolls the RANDOM shape. JUMP (the last shape): every step
//           jumps to any other net of the range, equally likely.
// Old patches load unchanged: params / ports are appended, FROM 1 TO 16 is the old walk.
// Rack audio is +-5 V; the engine works in +-1.

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/SeedwalkLayout.hpp"
#include "seedwalk/Engine.h"
#include "seedwalk/Range.h"

namespace L = semi::layout::seedwalk;
using semi::Screen;

namespace {

constexpr int kBlock = 32;          // engine block; adds 32 samples of latency
constexpr float kVoltsPerUnit = 5.f;
constexpr int kHist = 8;

const double kTmult[] = {0.25, 0.5, 1, 2, 4, 8, 16, 32, 64};
const char* const kTmultNames[] = {"1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS", "4 BARS"};
const char* const kShapeNames[] = {"CHAIN", "COOL DOWN", "WARM UP", "HEAT DRUNK", "DESCENT", "ASCENT",
                                   "NEAREST", "FARTHEST", "DRUNK", "RANDOM", "CLEAR", "JUMP"};
constexpr int kNumShapes = sw::range::kNumShapes;   // the plugin's + JUMP
constexpr float kNetsPerVolt = 1.5f;         // FROM / TO CV
const float kBpms[] = {60.f, 90.f, 120.f, 140.f, 160.f, 174.f};

// how the sixteen nets are made from SEED (the plugin's factory recipe, Presets.cpp)
struct NetSpec {
	float heatLo = 0.6f, heatHi = 3.6f;
	float leakLo = 0.1f, leakHi = 0.7f;
	float gainLo = 0.4f, gainHi = 1.0f;
	float lvl = 0.6f;
	int scramble = 7;      // the heat of step s is rank (s * scramble) % 16
	bool retic = false;    // RETICULUM's nets as its banks left them
};

struct FxState {
	float axTime = 0.6f, axFbk = 0.35f, axAmt = 0.35f, axStereo = 0.2f;
	bool bdOn = false;
	int bdQ = 0;
	float bdMix = 0.5f, bdVerb = 0.f, bdDens = 0.3f, bdPitch = 0.f, bdSize = 0.5f, bdFbk = 0.f;
};

// one factory walk: knobs + the hidden state
struct Walk {
	int seed = 1;
	NetSpec ns;
	float open = 0.f;
	int tmult = 4;
	float glide = 0.75f, dip = 0.f, spread = 0.4f, excite = 0.5f;
	int curve = sw::cSmooth;
	const char* pat = "................";
	int vel = 100, prob = 100;
	double dur = 2.0, delay = 0.0;
	int delayProb = 100;
	int shape = sw::walk::shChain;
	int axMode = 0;
	float fx = 0.4f;
	FxState f;
};

struct WalkPreset {
	const char* name;
	void (*apply)(Walk&);
};

// SEEDWALK's ten factory walks (Source/Presets.cpp), as data
const WalkPreset kWalks[] = {
	{"Init Walk", [](Walk& w) {
		 w.seed = 1; w.ns.heatLo = 0.6f; w.ns.heatHi = 3.6f;
		 w.tmult = 4; w.glide = 0.75f; w.curve = sw::cSmooth;
		 w.pat = "1.......1......."; w.vel = 90; w.dur = 1.0;
		 w.shape = sw::walk::shChain;
	 }},
	{"Cool Down", [](Walk& w) {
		 w.seed = 2; w.ns.heatLo = 0.5f; w.ns.heatHi = 4.0f;
		 w.tmult = 5; w.glide = 0.9f;
		 w.pat = "1111111111111111"; w.vel = 70; w.dur = 1.0;
		 w.shape = sw::walk::shCoolDown;
		 w.fx = 0.35f; w.f.axFbk = 0.45f;
	 }},
	{"Descent", [](Walk& w) {
		 w.seed = 3; w.ns = {0.4f, 3.8f, 0.0f, 0.6f};
		 w.tmult = 4; w.glide = 0.6f; w.dip = 0.2f; w.spread = 0.35f;
		 w.pat = "2...6...2...6..."; w.prob = 70; w.dur = 1.0;
		 w.shape = sw::walk::shDescent;
		 w.f.bdOn = true; w.f.bdQ = 1; w.f.bdMix = 0.35f; w.f.bdVerb = 0.4f; w.f.bdDens = 0.2f;
	 }},
	{"Through Silence", [](Walk& w) {
		 w.seed = 4; w.ns = {0.8f, 3.4f, 0.2f, 0.8f, 0.5f, 1.0f};
		 w.tmult = 6; w.glide = 1.f; w.curve = sw::cLinear; w.dip = 0.95f;
		 w.shape = sw::walk::shHeatDrunk;
		 w.fx = 0.45f; w.f.axTime = 0.8f; w.f.axFbk = 0.55f;
	 }},
	{"Edge Bells", [](Walk& w) {
		 w.seed = 5; w.ns = {0.15f, 0.75f, 0.0f, 0.35f, 0.3f, 1.0f, 1.0f, 5};
		 w.tmult = 3; w.glide = 0.3f; w.excite = 0.8f; w.spread = 0.25f;
		 w.pat = "1.1.11.1.1.11.1."; w.vel = 110; w.prob = 85; w.dur = 2.0;
		 w.shape = sw::walk::shNearest;
		 w.fx = 0.45f; w.f.axTime = 0.45f; w.f.axFbk = 0.5f; w.f.axStereo = 0.5f;
	 }},
	{"Snap Grid", [](Walk& w) {
		 w.seed = 6; w.ns = {2.0f, 4.2f, 0.3f, 0.9f};
		 w.tmult = 2; w.glide = 0.f;
		 w.pat = "27.2.7..2..72.7."; w.prob = 80; w.dur = 1.0;
		 w.shape = sw::walk::shRandom;
		 w.axMode = 5; w.f.axAmt = 0.6f; w.fx = 0.7f; w.f.axTime = 0.4f;
	 }},
	{"Frozen Grains", [](Walk& w) {
		 w.seed = 7; w.ns.heatLo = 1.0f; w.ns.heatHi = 3.6f;
		 w.tmult = 5; w.glide = 0.8f;
		 w.pat = "6...8...6...8..."; w.dur = 6.0;
		 w.shape = sw::walk::shFarthest;
		 w.fx = 0.f;
		 w.f.bdOn = true; w.f.bdQ = 3; w.f.bdMix = 0.65f; w.f.bdPitch = -12.f; w.f.bdSize = 0.6f;
		 w.f.bdDens = 0.65f; w.f.bdFbk = 0.35f; w.f.bdVerb = 0.3f;
	 }},
	{"Reticulum Nets", [](Walk& w) {
		 w.seed = 8; w.ns.retic = true; w.open = 0.6f;
		 w.tmult = 3; w.glide = 0.4f;
		 w.pat = "2.2.2.2.2.2.2.2."; w.prob = 60; w.dur = 1.0; w.delay = 1.0; w.delayProb = 40;
		 w.shape = sw::walk::shDrunk;
		 w.f.axAmt = 0.35f; w.f.axFbk = 0.4f; w.f.axTime = 0.7f; w.fx = 1.f;
	 }},
	{"Slow Orbit", [](Walk& w) {
		 w.seed = 9; w.ns = {0.9f, 2.6f, 0.0f, 0.5f, 0.3f, 0.9f, 0.6f, 3};
		 w.tmult = 8; w.glide = 1.f; w.curve = sw::cLate;
		 w.pat = "1111111111111111"; w.vel = 60; w.dur = 8.0;
		 w.shape = sw::walk::shAscent;
		 w.fx = 0.f;
		 w.f.bdOn = true; w.f.bdQ = 0; w.f.bdMix = 0.4f; w.f.bdVerb = 0.7f; w.f.bdDens = 0.4f;
	 }},
	{"Hold And Dip", [](Walk& w) {
		 w.seed = 10; w.ns.heatLo = 0.7f; w.ns.heatHi = 3.8f;
		 w.tmult = 4; w.glide = 1.f; w.curve = sw::cLinear;
		 w.pat = "3.4.3.43.4.3.4.4"; w.prob = 75; w.dur = 2.0;
		 w.shape = sw::walk::shNearest;
		 w.axMode = 6; w.f.axAmt = 0.4f; w.fx = 0.5f; w.f.axFbk = 0.3f;
	 }},
};
constexpr int kNumWalks = sizeof(kWalks) / sizeof(kWalks[0]);

struct Snapshot {
	int from = -1, to = 0, shape = 0;
	uint16_t nextMask = 0;
	float morph = 1.f, glide = 0.75f, heatNow = 0.f;
	float heat[sw::kSteps]{};      // the nets' heat, 0..1 over the walk's range
	int8_t hist[kHist]{};          // the last arrivals, oldest first
	int histN = 0;
	bool holding = false, clocked = false, pinned = false;
	bool held = false;             // the HOLD latch / gate
	int rFrom = 0, rTo = sw::kSteps - 1;
	uint16_t rangeMask = 0xffff;
};

} // namespace

struct Seedwalk : Module {
	enum ParamId { STEP_PARAM, GLIDE_PARAM, CHAOS_PARAM, SEED_PARAM, LVL_PARAM,
	               SHAPE_PARAM, SPREAD_PARAM, DIP_PARAM, EXCITE_PARAM, FX_PARAM, FXMODE_PARAM,
	               PIN_PARAM, FROM_PARAM, TO_PARAM, HOLD_PARAM, RND_PARAM, PARAMS_LEN };
	enum InputId { CLK_INPUT, RST_INPUT, CHAOS_INPUT, X_INPUT, Y_INPUT, IN_INPUT,
	               HOLD_INPUT, RND_INPUT, FROM_INPUT, TO_INPUT, INPUTS_LEN };
	enum OutputId { L_OUTPUT, R_OUTPUT, NET_OUTPUT, EOC_OUTPUT, OUTPUTS_LEN };
	enum LightId { PIN_LIGHT, HOLD_LIGHT, RND_LIGHT, LIGHTS_LEN };

	sw::Engine engine;
	sw::walk::Survey survey;

	// hidden state (json): the walk's recipe, fx and options
	NetSpec netSpec;
	FxState fx;
	float open = 0.f;
	int curve = sw::cSmooth;
	bool keepNorm = true;
	int clip = 1;
	int bpmIndex = 2;
	int walkIndex = 0;
	int netSeed = -1;              // the SEED the nets were made from
	bool holdLatch = false;        // HOLD button state (saved)
	bool rndAll = false;           // RND re-rolls all 16 nets instead of the range
	uint32_t shapeSalt = 0;        // RND re-rolls the RANDOM shape (0 = the plugin's matrix)

	// requests from the UI thread
	std::atomic<int> walkRequest{-1};
	std::atomic<uint32_t> rndNetRequest{0};   // nets to re-roll, a bit per net

	// range / hold / random
	int rFrom = 0, rTo = sw::kSteps - 1, lastFrom = -1, lastTo = -1;
	int rIdx[sw::kSteps]{};
	int rN = sw::kSteps;
	uint16_t rangeMask = 0xffff;
	uint16_t rndMask = 0;          // nets still to re-roll (one a block)
	bool held = false;
	uint32_t lastSalt = 0;
	sw::Rng rng{1u};
	sw::walk::Survey sub;          // the survey restricted to the range
	dsp::BooleanTrigger holdTrig, rndBtnTrig;
	dsp::SchmittTrigger holdGate, rndTrig;
	float rndFlash = 0.f;

	// background jobs, one stage per block: make nets -> survey -> shape
	int regenIdx = sw::kSteps;     // < 16: still making nets
	int surveyIdx = 2 * sw::kSteps;
	bool needShape = false;
	int lastShape = -1;
	float lastSpread = -1.f;
	float heatNorm[sw::kSteps]{};

	// audio block
	float inBuf[2][kBlock]{}, outBuf[2][kBlock]{};
	int pos = 0;
	double preparedSr = 0.0;

	// clock / transport
	dsp::SchmittTrigger clkTrig, rstTrig;
	dsp::BooleanTrigger pinTrig;
	bool pinRequest = false, rndRequest = false;
	int64_t sampleCount = 0, lastEdge = -1;
	double clockPeriod = 0.0;
	int clockCount = 0;
	int lastXY = -1;

	// walk telemetry
	uint32_t seenArrivals = 0, seenResets = 0;
	int firstStep = -1;
	int8_t hist[kHist]{};
	int histN = 0;
	float netVolts = 0.f;
	dsp::PulseGenerator eocPulse;
	float pinFlash = 0.f;

	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;

	Seedwalk() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configSwitch(STEP_PARAM, 0.f, 8.f, 4.f, "Step time",
		             {"1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars"});
		configParam(GLIDE_PARAM, 0.f, 100.f, 75.f, "Glide", " %");
		configParam(CHAOS_PARAM, -50.f, 50.f, 0.f, "Chaos", " %");
		configParam(SEED_PARAM, 1.f, 99.f, 1.f, "Net seed");
		paramQuantities[SEED_PARAM]->snapEnabled = true;
		configParam(LVL_PARAM, -24.f, 12.f, -3.f, "Level", " dB");
		configSwitch(SHAPE_PARAM, 0.f, kNumShapes - 1, 0.f, "Walk shape",
		             {"Chain", "Cool down", "Warm up", "Heat drunk", "Descent", "Ascent", "Nearest", "Farthest",
		              "Drunk", "Random", "Clear", "Jump (random, in range)"});
		configParam(SPREAD_PARAM, 0.05f, 1.5f, 0.4f, "Spread");
		configParam(DIP_PARAM, 0.f, 100.f, 0.f, "Dip", " %");
		configParam(EXCITE_PARAM, 0.f, 100.f, 50.f, "Excite", " %");
		configParam(FX_PARAM, 0.f, 100.f, 40.f, "FX mix", " %");
		configSwitch(FXMODE_PARAM, 0.f, 8.f, 0.f, "FX mode",
		             {"Delay", "Flanger", "Freezer", "Panner", "Crusher", "Slicer", "Pitcher", "Replayer", "Shifter"});
		configButton(PIN_PARAM, "Pin net");
		configParam(FROM_PARAM, 1.f, 16.f, 1.f, "Range from (net)");
		paramQuantities[FROM_PARAM]->snapEnabled = true;
		configParam(TO_PARAM, 1.f, 16.f, 16.f, "Range to (net)");
		paramQuantities[TO_PARAM]->snapEnabled = true;
		configButton(HOLD_PARAM, "Hold (latch)");
		configButton(RND_PARAM, "Randomise nets");
		paramQuantities[FROM_PARAM]->description = "First net of the walk; above TO the range wraps round 16 > 1";
		paramQuantities[TO_PARAM]->description = "Last net of the walk; FROM = TO sits on one net";
		paramQuantities[HOLD_PARAM]->description = "The ball stops and the net freezes where it is, mid-glide too; press again to go on";
		paramQuantities[RND_PARAM]->description = "Re-rolls the nets in the range (menu: all 16) and the RANDOM shape";
		paramQuantities[STEP_PARAM]->description = "One step of the ball; with CLK patched, STEP x 1/16 clocks per step";
		paramQuantities[GLIDE_PARAM]->description = "Share of the step the net takes to arrive (0: it jumps)";
		paramQuantities[CHAOS_PARAM]->description = "Drive added to every net's SCALE: hotter = more chaotic";
		paramQuantities[SEED_PARAM]->description = "Makes the sixteen nets (calm to hot); turning it re-makes them";
		paramQuantities[SHAPE_PARAM]->description = "Draws the 17 x 17 path matrix from the nets' heat and distance";
		paramQuantities[SPREAD_PARAM]->description = "How sharply NEAREST / DESCENT prefer the near nets";
		paramQuantities[DIP_PARAM]->description = "The nodes close in the middle of every glide: the walk passes through silence";
		paramQuantities[EXCITE_PARAM]->description = "Lane bursts (and IN) into rete's excite";
		paramQuantities[PIN_PARAM]->description = "Pins where the net is now (mid-glide too) into the net it glides to";
		configInput(CLK_INPUT, "Clock (1/16; a step = STEP clocks)");
		configInput(RST_INPUT, "Reset (relaunch the ball)");
		configInput(CHAOS_INPUT, "Chaos CV (10 %/V)");
		configInput(X_INPUT, "X: grid column, 0-10 V (a change sends the ball there)");
		configInput(Y_INPUT, "Y: grid row, 0-10 V (a change sends the ball there)");
		configInput(IN_INPUT, "Audio into excite");
		configInput(HOLD_INPUT, "Hold gate (high = held)");
		configInput(RND_INPUT, "Randomise nets (trigger)");
		configInput(FROM_INPUT, "Range from CV (1.5 nets/V)");
		configInput(TO_INPUT, "Range to CV (1.5 nets/V)");
		configOutput(L_OUTPUT, "Left");
		configOutput(R_OUTPUT, "Right");
		configOutput(NET_OUTPUT, "Current net (1/12 V per net, 0-1.25 V)");
		configOutput(EOC_OUTPUT, "End of cycle (the walk is back at its first net)");
		configBypass(IN_INPUT, L_OUTPUT);
		configBypass(IN_INPUT, R_OUTPUT);

		rng = sw::Rng(random::u32() | 1u);
		rN = sw::range::nets(rFrom, rTo, rIdx);
		engine.prepare(48000.0, kBlock);
		preparedSr = 48000.0;
		applyWalk(0);
		while (runJob()) {}
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		engine.prepare(e.sampleRate, kBlock);
		preparedSr = e.sampleRate;
		pos = 0;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		holdLatch = false;
		rndAll = false;
		shapeSalt = 0;
		walkRequest.store(0);
	}

	// ---- the walk's recipe ------------------------------------------------------------

	// knobs and hidden state from a factory walk (audio thread, or the constructor)
	void applyWalk(int index) {
		walkIndex = clamp(index, 0, kNumWalks - 1);
		Walk w;
		kWalks[walkIndex].apply(w);
		netSpec = w.ns;
		fx = w.f;
		open = w.open;
		curve = w.curve;
		keepNorm = true;
		params[STEP_PARAM].setValue((float) w.tmult);
		params[GLIDE_PARAM].setValue(w.glide * 100.f);
		params[CHAOS_PARAM].setValue(0.f);
		params[SEED_PARAM].setValue((float) w.seed);
		params[SHAPE_PARAM].setValue((float) w.shape);
		params[SPREAD_PARAM].setValue(w.spread);
		params[DIP_PARAM].setValue(w.dip * 100.f);
		params[EXCITE_PARAM].setValue(w.excite * 100.f);
		params[FX_PARAM].setValue(w.fx * 100.f);
		params[FXMODE_PARAM].setValue((float) w.axMode);
		for (int s = 0; s < sw::kSteps; s++) {
			sw::pinball::StepParams st;
			const char c = w.pat[s];
			st.trig = c == '.' ? 0 : (c == 'm' ? sw::pinball::kTrigMute : (c - '0'));
			st.velocity = w.vel;
			st.trigProb = w.prob;
			st.duration = w.dur;
			st.delay = w.delay;
			st.delayProb = w.delayProb;
			engine.steps[s].store(st);
		}
		netSeed = w.seed;
		shapeSalt = 0;
		regenIdx = 0;              // make the nets, then survey, then the shape
		lastShape = -1;
		engine.requestReset();
		firstStep = -1;
		histN = 0;
	}

	// net s from SEED and the recipe (the plugin's makeNets, one net at a time)
	void makeNet(int s) {
		sw::Net net;
		if (netSpec.retic) {
			net.seed = 0x5eedf00du + (uint32_t) (s + 16 * (netSeed - 8)) * 0x01000193u;
			if (!net.seed)
				net.seed = 1;
			for (auto& g : net.gain)
				g = 0.5f;
			net.leak = 0.5f;
			net.scale = 0.67f;
			net.lvl = 0.f;
			engine.nets[s].store(net);
			return;
		}
		// the generator's stream: replay it up to net s (cheap: 10 draws a net)
		sw::Rng r((uint32_t) netSeed * 2654435761u + 17u);
		for (int k = 0; k <= s; k++) {
			net.seed = r.u32() | 1u;
			for (auto& g : net.gain)
				g = netSpec.gainLo + (netSpec.gainHi - netSpec.gainLo) * r.uniform();
			net.leak = netSpec.leakLo + (netSpec.leakHi - netSpec.leakLo) * r.uniform();
		}
		net.lvl = netSpec.lvl;
		const int rank = (s * netSpec.scramble) % sw::kSteps;
		const float heat = netSpec.heatLo + (netSpec.heatHi - netSpec.heatLo) * (float) rank / (float) (sw::kSteps - 1);
		engine.nets[s].store(sw::walk::tuneHeat(net, heat, open, 0.f));
	}

	// net s re-rolled (RND): the walk's recipe with a fresh seed, gains, leak and heat
	void makeRandomNet(int s) {
		sw::Net net;
		net.seed = rng.u32() | 1u;
		if (netSpec.retic) {
			for (auto& g : net.gain)
				g = 0.5f;
			net.leak = 0.5f;
			net.scale = 0.67f;
			net.lvl = 0.f;
			engine.nets[s].store(net);
			return;
		}
		for (auto& g : net.gain)
			g = netSpec.gainLo + (netSpec.gainHi - netSpec.gainLo) * rng.uniform();
		net.leak = netSpec.leakLo + (netSpec.leakHi - netSpec.leakLo) * rng.uniform();
		net.lvl = netSpec.lvl;
		const float heat = netSpec.heatLo + (netSpec.heatHi - netSpec.heatLo) * rng.uniform();
		engine.nets[s].store(sw::walk::tuneHeat(net, heat, open, 0.f));
	}

	// The path matrix for SHAPE on the range (engines/seedwalk/Range.h)
	void buildShape(int shape, float spread) {
		const uint32_t seed = ((uint32_t) netSeed * 0x9e3779b9u ^ shapeSalt) | 1u;
		const sw::walk::Pin m = sw::range::shape(shape, survey, sub, rIdx, rN, spread, seed);
		for (int x = 0; x < sw::walk::kStates; x++)
			for (int y = 0; y < sw::walk::kStates; y++)
				engine.seq.matrix[x][y].store(m[x][y], std::memory_order_relaxed);
	}

	// One stage of the background work; returns true while there is more to do.
	// Spread over blocks so no single block pays for a whole survey on MetaModule.
	bool runJob() {
		constexpr int N = sw::kSteps;
		if (regenIdx < N) {
			makeNet(regenIdx++);
			if (regenIdx == N)
				surveyIdx = 0;
			return true;
		}
		if (rndMask) {
			int s = 0;
			while (!(rndMask >> s & 1))
				s++;
			rndMask &= (uint16_t) ~(1u << s);
			makeRandomNet(s);
			if (!rndMask)
				surveyIdx = 0;
			return true;
		}
		if (surveyIdx < N) {
			const int i = surveyIdx++;
			survey.prof[i] = sw::walk::profile(engine.nets[i].load(), open, 0.f);
			return true;
		}
		if (surveyIdx < 2 * N) {
			const int i = surveyIdx++ - N;
			const sw::Net a = engine.nets[i].load();
			for (int j = 0; j < N; j++) {
				if (i == j) {
					survey.chr[i][j] = survey.dist[i][j] = 0.f;
					continue;
				}
				const sw::Net b = engine.nets[j].load();
				const float c = sw::walk::character(a, survey.prof[i], b, survey.prof[j]);
				survey.chr[i][j] = c;
				survey.dist[i][j] = std::fabs(survey.prof[i].heat - survey.prof[j].heat) / 1.5f + c;
			}
			if (surveyIdx == 2 * N) {
				float lo = 1e9f, hi = -1e9f;
				for (auto& p : survey.prof) {
					lo = std::min(lo, p.heat);
					hi = std::max(hi, p.heat);
				}
				if (hi - lo < 0.5f) {
					const float c = 0.5f * (lo + hi);
					lo = c - 0.25f;
					hi = c + 0.25f;
				}
				survey.heatLo = lo;
				survey.heatHi = hi;
				for (int k = 0; k < N; k++)
					heatNorm[k] = (survey.prof[k].heat - lo) / (hi - lo);
				needShape = true;
			}
			return true;
		}
		const int shape = clamp((int) std::round(params[SHAPE_PARAM].getValue()), 0, kNumShapes - 1);
		const float spread = params[SPREAD_PARAM].getValue();
		if (needShape || shape != lastShape || std::fabs(spread - lastSpread) > 1e-3f || rFrom != lastFrom
		    || rTo != lastTo || shapeSalt != lastSalt) {
			buildShape(shape, spread);
			needShape = false;
			lastShape = shape;
			lastSpread = spread;
			lastFrom = rFrom;
			lastTo = rTo;
			lastSalt = shapeSalt;
		}
		return false;
	}

	// ---- json -------------------------------------------------------------------------

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "walk", json_integer(walkIndex));
		json_object_set_new(root, "netSeed", json_integer(netSeed));
		json_object_set_new(root, "curve", json_integer(curve));
		json_object_set_new(root, "keep", json_boolean(keepNorm));
		json_object_set_new(root, "clip", json_integer(clip));
		json_object_set_new(root, "bpm", json_integer(bpmIndex));
		json_object_set_new(root, "open", json_real(open));
		json_object_set_new(root, "hold", json_boolean(holdLatch));
		json_object_set_new(root, "rndAll", json_boolean(rndAll));
		json_object_set_new(root, "salt", json_integer(shapeSalt));
		json_t* ns = json_array();
		for (float v : {netSpec.heatLo, netSpec.heatHi, netSpec.leakLo, netSpec.leakHi, netSpec.gainLo, netSpec.gainHi,
		                netSpec.lvl, (float) netSpec.scramble, netSpec.retic ? 1.f : 0.f})
			json_array_append_new(ns, json_real(v));
		json_object_set_new(root, "netSpec", ns);
		json_t* f = json_array();
		for (float v : {fx.axTime, fx.axFbk, fx.axAmt, fx.axStereo, fx.bdOn ? 1.f : 0.f, (float) fx.bdQ, fx.bdMix, fx.bdVerb,
		                fx.bdDens, fx.bdPitch, fx.bdSize, fx.bdFbk})
			json_array_append_new(f, json_real(v));
		json_object_set_new(root, "fx", f);
		json_t* nets = json_array();
		for (int s = 0; s < sw::kSteps; s++) {
			const sw::Net n = engine.nets[s].load();
			json_t* a = json_array();
			json_array_append_new(a, json_integer(n.seed));
			for (float g : n.gain)
				json_array_append_new(a, json_real(g));
			json_array_append_new(a, json_real(n.leak));
			json_array_append_new(a, json_real(n.scale));
			json_array_append_new(a, json_real(n.lvl));
			json_array_append_new(nets, a);
		}
		json_object_set_new(root, "nets", nets);
		json_t* steps = json_array();
		for (int s = 0; s < sw::kSteps; s++) {
			const sw::pinball::StepParams p = engine.steps[s].load();
			json_t* a = json_array();
			for (double v : {(double) p.trig, (double) p.velocity, (double) p.trigProb, p.duration, p.delay, (double) p.delayProb})
				json_array_append_new(a, json_real(v));
			json_array_append_new(steps, a);
		}
		json_object_set_new(root, "steps", steps);
		return root;
	}

	static float num(json_t* a, size_t i, float def) {
		json_t* v = json_array_get(a, i);
		return v ? (float) json_number_value(v) : def;
	}

	void dataFromJson(json_t* root) override {
		walkRequest.store(-1);
		if (json_t* v = json_object_get(root, "walk"))
			walkIndex = clamp((int) json_integer_value(v), 0, kNumWalks - 1);
		if (json_t* v = json_object_get(root, "curve"))
			curve = clamp((int) json_integer_value(v), 0, sw::kCurves - 1);
		if (json_t* v = json_object_get(root, "keep"))
			keepNorm = json_is_true(v);
		if (json_t* v = json_object_get(root, "clip"))
			clip = clamp((int) json_integer_value(v), 0, 2);
		if (json_t* v = json_object_get(root, "bpm"))
			bpmIndex = clamp((int) json_integer_value(v), 0, 5);
		if (json_t* v = json_object_get(root, "open"))
			open = clamp((float) json_number_value(v), 0.f, 1.f);
		holdLatch = json_is_true(json_object_get(root, "hold"));
		rndAll = json_is_true(json_object_get(root, "rndAll"));
		shapeSalt = 0;
		if (json_t* v = json_object_get(root, "salt"))
			shapeSalt = (uint32_t) json_integer_value(v);
		if (json_t* a = json_object_get(root, "netSpec")) {
			netSpec.heatLo = num(a, 0, netSpec.heatLo);
			netSpec.heatHi = num(a, 1, netSpec.heatHi);
			netSpec.leakLo = num(a, 2, netSpec.leakLo);
			netSpec.leakHi = num(a, 3, netSpec.leakHi);
			netSpec.gainLo = num(a, 4, netSpec.gainLo);
			netSpec.gainHi = num(a, 5, netSpec.gainHi);
			netSpec.lvl = num(a, 6, netSpec.lvl);
			netSpec.scramble = clamp((int) num(a, 7, (float) netSpec.scramble), 1, 15);
			netSpec.retic = num(a, 8, 0.f) > 0.5f;
		}
		if (json_t* a = json_object_get(root, "fx")) {
			fx.axTime = num(a, 0, fx.axTime);
			fx.axFbk = num(a, 1, fx.axFbk);
			fx.axAmt = num(a, 2, fx.axAmt);
			fx.axStereo = num(a, 3, fx.axStereo);
			fx.bdOn = num(a, 4, 0.f) > 0.5f;
			fx.bdQ = clamp((int) num(a, 5, 0.f), 0, 3);
			fx.bdMix = num(a, 6, fx.bdMix);
			fx.bdVerb = num(a, 7, fx.bdVerb);
			fx.bdDens = num(a, 8, fx.bdDens);
			fx.bdPitch = num(a, 9, fx.bdPitch);
			fx.bdSize = num(a, 10, fx.bdSize);
			fx.bdFbk = num(a, 11, fx.bdFbk);
		}
		bool haveNets = false;
		if (json_t* nets = json_object_get(root, "nets")) {
			for (int s = 0; s < sw::kSteps && s < (int) json_array_size(nets); s++) {
				json_t* a = json_array_get(nets, s);
				sw::Net n;
				json_t* sd = json_array_get(a, 0);
				n.seed = sd ? (uint32_t) json_integer_value(sd) : 1u;
				for (int k = 0; k < sw::kNodes; k++)
					n.gain[k] = clamp(num(a, 1 + k, 0.8f), 0.f, 1.f);
				n.leak = clamp(num(a, 9, 0.5f), 0.f, 1.f);
				n.scale = clamp(num(a, 10, 0.6f), 0.f, 1.f);
				n.lvl = clamp(num(a, 11, 0.5f), 0.f, 1.f);
				engine.nets[s].store(n);
				haveNets = true;
			}
		}
		if (json_t* steps = json_object_get(root, "steps")) {
			for (int s = 0; s < sw::kSteps && s < (int) json_array_size(steps); s++) {
				json_t* a = json_array_get(steps, s);
				sw::pinball::StepParams p;
				p.trig = clamp((int) num(a, 0, 0.f), 0, 9);
				p.velocity = clamp((int) num(a, 1, 100.f), 0, 127);
				p.trigProb = clamp((int) num(a, 2, 100.f), 0, 100);
				p.duration = clamp(num(a, 3, 2.f), 0.f, 64.f);
				p.delay = clamp(num(a, 4, 0.f), 0.f, 64.f);
				p.delayProb = clamp((int) num(a, 5, 100.f), 0, 100);
				engine.steps[s].store(p);
			}
		}
		if (json_t* v = json_object_get(root, "netSeed"))
			netSeed = (int) json_integer_value(v);
		if (haveNets) {
			regenIdx = sw::kSteps;   // keep the saved (pinned) nets
			surveyIdx = 0;
		}
		else {
			regenIdx = 0;
		}
		lastShape = -1;
	}

	// ---- audio --------------------------------------------------------------------------

	sw::Params makeParams(float sampleRate) {
		sw::Params p;
		p.run = sw::rAlways;
		const int ti = clamp((int) std::round(params[STEP_PARAM].getValue()), 0, 8);
		p.timeMult = kTmult[ti];
		p.patternBars = 0.0;
		p.startStep = 1;
		p.hold = held;
		p.glide = params[GLIDE_PARAM].getValue() * 0.01f;
		p.curve = curve;
		p.dip = params[DIP_PARAM].getValue() * 0.01f;
		p.keepNorm = keepNorm;
		p.open = open;
		p.drive = clamp((params[CHAOS_PARAM].getValue() + inputs[CHAOS_INPUT].getVoltage() * 10.f) * 0.01f, -0.5f, 0.5f);
		p.excite = params[EXCITE_PARAM].getValue() * 0.01f;
		p.ext = inputs[IN_INPUT].isConnected() ? p.excite : 0.f;
		const float mix = params[FX_PARAM].getValue() * 0.01f;
		p.axOn = mix > 1e-3f;
		p.axMode = clamp((int) std::round(params[FXMODE_PARAM].getValue()), 0, 8);
		p.axTime = fx.axTime;
		p.axFbk = fx.axFbk;
		p.axAmt = fx.axAmt;
		p.axFilter = 0.f;
		p.axStereo = fx.axStereo;
		p.axGain = 1.f;
		p.axMix = mix;
		p.bdOn = fx.bdOn;
		p.bdQuality = fx.bdQ;
		p.bdTime = 0.05f;
		p.bdSize = fx.bdSize;
		p.bdShape = 0.5f;
		p.bdDensity = fx.bdDens;
		p.bdPitch = fx.bdPitch;
		p.bdFbk = fx.bdFbk;
		p.bdVerb = fx.bdVerb;
		p.bdRand = 0.f;
		p.bdMix = fx.bdMix;
		p.bdGain = 1.f;
		p.bdFreeze = false;
		p.order = sw::oAxBd;
		p.level = params[LVL_PARAM].getValue();
		p.width2 = 1.f;
		p.clip = clip;
		p.bpm = kBpms[bpmIndex];
		// a patched clock sets the tempo: a step (STEP clocks) lasts sixteenth x timeMult
		if (engine.seq.extClock && clockPeriod > 0.0) {
			const double stepSamples = clocksPerStep() * clockPeriod;
			const double sixteenth = stepSamples / p.timeMult;
			p.bpm = clamp((float) (15.0 * sampleRate / sixteenth), 1.f, 2000.f);
		}
		p.ppq = 0.0;
		p.playing = false;
		return p;
	}

	int clocksPerStep() {
		const int ti = clamp((int) std::round(params[STEP_PARAM].getValue()), 0, 8);
		return std::max(1, (int) std::round(kTmult[ti]));
	}

	void runBlock(float sampleRate) {
		if (int w = walkRequest.exchange(-1); w >= 0)
			applyWalk(w);

		// SEED: a new set of nets
		const int seedK = (int) std::round(params[SEED_PARAM].getValue());
		if (seedK != netSeed) {
			netSeed = seedK;
			regenIdx = 0;
		}
		// the range (knob + CV), RND (button / trigger / menu)
		const int f = clamp((int) std::round(params[FROM_PARAM].getValue() + inputs[FROM_INPUT].getVoltage() * kNetsPerVolt), 1, 16) - 1;
		const int t = clamp((int) std::round(params[TO_PARAM].getValue() + inputs[TO_INPUT].getVoltage() * kNetsPerVolt), 1, 16) - 1;
		if (f != rFrom || t != rTo) {
			rFrom = f;
			rTo = t;
			rN = sw::range::nets(rFrom, rTo, rIdx);
			rangeMask = sw::range::mask(rIdx, rN);
		}
		if (rndRequest) {
			rndRequest = false;
			rndMask |= rndAll ? (uint16_t) 0xffff : rangeMask;
			shapeSalt = rng.u32() | 1u;
			rndFlash = 0.35f;
		}
		if (const uint32_t one = rndNetRequest.exchange(0); one) {
			rndMask |= (uint16_t) one;
			rndFlash = 0.35f;
		}
		runJob();

		if (pinRequest) {
			pinRequest = false;
			const int to = engine.vis.toStep.load(std::memory_order_relaxed);
			if (to >= 0 && to < sw::kSteps) {
				sw::Net n = engine.currentNet();
				n.scale = clamp(n.scale, 0.f, 1.f);
				engine.nets[to].store(n);
				surveyIdx = 0;
				pinFlash = 0.35f;
			}
		}

		// X / Y pick a cell of the grid
		if (inputs[X_INPUT].isConnected() || inputs[Y_INPUT].isConnected()) {
			const int col = clamp((int) (inputs[X_INPUT].getVoltage() * 0.4f), 0, 3);
			const int row = clamp((int) (inputs[Y_INPUT].getVoltage() * 0.4f), 0, 3);
			const int cell = row * 4 + col;
			if (cell != lastXY) {
				lastXY = cell;
				engine.requestJump((rangeMask >> cell & 1) ? cell : rIdx[cell % rN]);
			}
		}
		else
			lastXY = -1;

		const sw::Params p = makeParams(sampleRate);
		const bool hasIn = inputs[IN_INPUT].isConnected();
		engine.process(p, hasIn ? inBuf[0] : nullptr, hasIn ? inBuf[1] : nullptr, outBuf[0], outBuf[1], kBlock, nullptr);

		// arrivals: history, NET, EOC
		const uint32_t resets = engine.vis.resets.load(std::memory_order_relaxed);
		if (resets != seenResets) {
			seenResets = resets;
			firstStep = -1;
		}
		const uint32_t arr = engine.vis.arrivals.load(std::memory_order_relaxed);
		if (arr != seenArrivals) {
			seenArrivals = arr;
			const int to = engine.vis.toStep.load(std::memory_order_relaxed);
			if (to >= 0) {
				netVolts = to / 12.f;
				if (firstStep < 0)
					firstStep = to;
				else if (to == firstStep)
					eocPulse.trigger(1e-3f);
				if (histN < kHist)
					hist[histN++] = (int8_t) to;
				else {
					for (int k = 1; k < kHist; k++)
						hist[k - 1] = hist[k];
					hist[kHist - 1] = (int8_t) to;
				}
			}
		}
	}

	void process(const ProcessArgs& args) override {
		if (args.sampleRate != preparedSr) {   // MetaModule may not send the event before the first block
			engine.prepare(args.sampleRate, kBlock);
			preparedSr = args.sampleRate;
		}
		++sampleCount;

		// HOLD: the latch or the gate
		if (holdTrig.process(params[HOLD_PARAM].getValue() > 0.f))
			holdLatch = !holdLatch;
		if (inputs[HOLD_INPUT].isConnected())
			holdGate.process(inputs[HOLD_INPUT].getVoltage(), 0.1f, 1.f);
		held = holdLatch || (inputs[HOLD_INPUT].isConnected() && holdGate.isHigh());
		const bool rndBtn = rndBtnTrig.process(params[RND_PARAM].getValue() > 0.f);
		const bool rndIn = rndTrig.process(inputs[RND_INPUT].getVoltage(), 0.1f, 1.f);
		if (rndBtn || rndIn)
			rndRequest = true;

		// clock: a step ends after STEP clocks (1/16 each); held, the clock is only measured
		const bool clocked = inputs[CLK_INPUT].isConnected();
		engine.seq.extClock = clocked;
		if (clocked && clkTrig.process(inputs[CLK_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (lastEdge >= 0)
				clockPeriod = (double) (sampleCount - lastEdge);
			lastEdge = sampleCount;
			if (!held && ++clockCount >= clocksPerStep()) {
				clockCount = 0;
				engine.seq.clockEdge();
			}
		}
		if (rstTrig.process(inputs[RST_INPUT].getVoltage(), 0.1f, 1.f)) {
			engine.requestReset();
			clockCount = 0;
			histN = 0;
		}
		if (pinTrig.process(params[PIN_PARAM].getValue() > 0.f))
			pinRequest = true;

		const float in = inputs[IN_INPUT].getVoltage();
		inBuf[0][pos] = in / kVoltsPerUnit;
		inBuf[1][pos] = in / kVoltsPerUnit;
		outputs[L_OUTPUT].setVoltage(outBuf[0][pos] * kVoltsPerUnit);
		outputs[R_OUTPUT].setVoltage(outBuf[1][pos] * kVoltsPerUnit);
		if (++pos >= kBlock) {
			pos = 0;
			runBlock(args.sampleRate);
		}
		outputs[NET_OUTPUT].setVoltage(netVolts);
		outputs[EOC_OUTPUT].setVoltage(eocPulse.process(args.sampleTime) ? 10.f : 0.f);

		if (ui.tick(args.sampleTime)) {
			pinFlash = std::max(0.f, pinFlash - 1.f / semi::platform::kUiRateHz);
			lights[PIN_LIGHT].setBrightness(pinFlash > 0.f ? 1.f : 0.f);
			rndFlash = std::max(0.f, rndFlash - 1.f / semi::platform::kUiRateHz);
			lights[RND_LIGHT].setBrightness(rndFlash > 0.f ? 1.f : 0.f);
			lights[HOLD_LIGHT].setBrightness(held ? 1.f : 0.f);
			publish(clocked);
		}
	}

	void publish(bool clocked) {
		Snapshot& s = snap.writeSlot();
		const auto& v = engine.vis;
		s.from = v.fromStep.load(std::memory_order_relaxed);
		s.to = v.toStep.load(std::memory_order_relaxed);
		s.morph = v.morph.load(std::memory_order_relaxed);
		s.heatNow = v.heatNow.load(std::memory_order_relaxed);
		s.holding = v.holding.load(std::memory_order_relaxed);
		s.glide = params[GLIDE_PARAM].getValue() * 0.01f;
		s.shape = clamp((int) std::round(params[SHAPE_PARAM].getValue()), 0, kNumShapes - 1);
		s.held = held;
		s.rFrom = rFrom;
		s.rTo = rTo;
		s.rangeMask = rangeMask;
		s.clocked = clocked;
		s.pinned = pinFlash > 0.f;
		for (int k = 0; k < sw::kSteps; k++)
			s.heat[k] = heatNorm[k];
		for (int k = 0; k < histN; k++)
			s.hist[k] = hist[k];
		s.histN = histN;
		// the likeliest next nets: the heaviest moves in the current net's row
		s.nextMask = 0;
		if (s.to >= 0) {
			const auto& row = engine.seq.matrix[s.to + 1];
			float best = 0.f;
			for (int y = 1; y < sw::walk::kStates; y++)
				best = std::max(best, row[y].load(std::memory_order_relaxed));
			if (best > 0.f)
				for (int y = 1; y < sw::walk::kStates; y++)
					if (row[y].load(std::memory_order_relaxed) >= best * 0.999f)
						s.nextMask |= (uint16_t) (1u << (y - 1));
		}
		snap.publish();
	}
};

// The sixteen nets as a 4 x 4 grid: number + heat bar per cell; the net rete glides to in
// magenta, the likeliest next in amber, the walked path in cyan; the glide as a dashed line
// with the ball on it. Nets outside FROM..TO are dimmed, FROM / TO carry cyan brackets; HOLD
// turns the ball and the glide readout amber. Changes only on arrivals and (20x/s at most)
// while a glide moves.
struct NetsScreen : Screen {
	Seedwalk* module = nullptr;
	Snapshot demo;

	NetsScreen() {
		demo.from = 2;
		demo.to = 6;
		demo.morph = 0.55f;
		demo.nextMask = 1u << 11;
		demo.heatNow = 1.9f;
		const int8_t h[] = {0, 5, 9, 2, 6};
		for (int k = 0; k < 5; k++)
			demo.hist[k] = h[k];
		demo.histN = 5;
		for (int k = 0; k < sw::kSteps; k++)
			demo.heat[k] = (float) ((k * 7) % 16) / 15.f;
		demo.rFrom = 1;
		demo.rTo = 11;
		demo.rangeMask = 0x0ffe;
	}

	static NVGcolor offCell() { return nvgRGB(0x09, 0x0e, 0x12); }
	static NVGcolor offText() { return nvgRGB(0x2c, 0x38, 0x42); }

	// "[" on the left edge of FROM's cell, "]" on the right edge of TO's
	static void bracket(NVGcontext* vg, float x, float y, float h, float dir, NVGcolor c) {
		const float tick = 2.2f;
		nvgBeginPath(vg);
		nvgMoveTo(vg, x + dir * tick, y);
		nvgLineTo(vg, x, y);
		nvgLineTo(vg, x, y + h);
		nvgLineTo(vg, x + dir * tick, y + h);
		nvgStrokeColor(vg, c);
		nvgStrokeWidth(vg, semi::platform::kStroke);
		nvgStroke(vg);
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		const float pad = 3.f, head = 13.f, gap = 3.f;
		const float gx = pad, gy = head + 2.f, gw = w - 2 * pad, gh = h - gy - pad;
		const float cw = (gw - 3 * gap) / 4.f, ch = (gh - 3 * gap) / 4.f;
		auto cx = [&](int i) { return gx + (i % 4) * (cw + gap); };
		auto cy = [&](int i) { return gy + (i / 4) * (ch + gap); };
		auto mx = [&](int i) { return cx(i) + cw * 0.5f; };
		auto my = [&](int i) { return cy(i) + ch * 0.5f + 1.f; };

		uint16_t walked = 0;
		for (int k = 0; k < s.histN; k++)
			if (s.hist[k] >= 0 && s.hist[k] < sw::kSteps)
				walked |= (uint16_t) (1u << s.hist[k]);

		char buf[40];
		for (int i = 0; i < sw::kSteps; i++) {
			const bool in = s.rangeMask >> i & 1;
			const bool cur = i == s.to;
			const bool next = !cur && (s.nextMask >> i & 1);
			const bool path = !cur && !next && in && (walked >> i & 1);
			const float x = cx(i), y = cy(i);
			if (!in && !cur && !next) {
				// outside the range: dimmed, the heat bar as a grey sliver
				fillRect(vg, x, y, cw, ch, offCell());
				std::snprintf(buf, sizeof buf, "%02d", i + 1);
				text(vg, x + 3.f, y + 2.f, buf, 9.f, offText());
				const float bx = x + 3.f, by = y + ch - 6.f, bw = cw - 6.f;
				fillRect(vg, bx, by, std::max(2.f, bw * clamp(s.heat[i], 0.f, 1.f)), 3.f, semi::theme::grid());
				continue;
			}
			fillRect(vg, x, y, cw, ch, cur ? semi::theme::magentaDeep() : semi::theme::cell());
			if (cur || next || path) {
				nvgBeginPath(vg);
				nvgRect(vg, x + 1.f, y + 1.f, cw - 2.f, ch - 2.f);
				nvgStrokeColor(vg, cur ? semi::theme::magenta() : next ? semi::theme::amber() : semi::theme::cyanDim());
				nvgStrokeWidth(vg, semi::platform::kStroke);
				nvgStroke(vg);
			}
			std::snprintf(buf, sizeof buf, "%02d", i + 1);
			text(vg, x + 3.f, y + 2.f, buf, 9.f,
			     cur ? semi::theme::magenta() : next ? semi::theme::amber() : path ? semi::theme::cyan() : semi::theme::dim());
			const float bx = x + 3.f, by = y + ch - 6.f, bw = cw - 6.f;
			fillRect(vg, bx, by, bw, 3.f, semi::theme::grid());
			const float e = clamp(s.heat[i], 0.f, 1.f);
			fillRect(vg, bx, by, std::max(2.f, bw * e), 3.f,
			         cur ? semi::theme::magenta() : e > 0.66f ? semi::theme::amber() : semi::theme::cyanDim());
		}

		// the range's ends
		if (s.rangeMask != 0xffff || s.rFrom != 0) {
			const NVGcolor bc = semi::theme::cyan();
			bracket(vg, cx(s.rFrom) - 1.5f, cy(s.rFrom) - 1.f, ch + 2.f, 1.f, bc);
			bracket(vg, cx(s.rTo) + cw + 1.5f, cy(s.rTo) - 1.f, ch + 2.f, -1.f, bc);
		}

		// the walked path, oldest to newest
		if (s.histN >= 2) {
			float xs[kHist], ys[kHist];
			int n = 0;
			for (int k = 0; k < s.histN; k++)
				if (s.hist[k] >= 0 && s.hist[k] < sw::kSteps) {
					xs[n] = mx(s.hist[k]);
					ys[n] = my(s.hist[k]);
					n++;
				}
			path(vg, xs, ys, n, semi::theme::cyanDim(), semi::platform::kThinStroke);
		}

		// the glide: dashed from -> to, the ball on it
		float bx = s.to >= 0 ? mx(s.to) : w * 0.5f, by = s.to >= 0 ? my(s.to) : h * 0.5f;
		if (s.from >= 0 && s.to >= 0 && s.from != s.to) {
			const float x0 = mx(s.from), y0 = my(s.from), x1 = mx(s.to), y1 = my(s.to);
			dashed(vg, x0, y0, x1, y1, semi::theme::magenta(), 3.f, semi::platform::kThinStroke);
			const float t = clamp(s.morph, 0.f, 1.f);
			bx = x0 + (x1 - x0) * t;
			by = y0 + (y1 - y0) * t;
		}
		if (s.to >= 0) {
			nvgBeginPath(vg);
			nvgCircle(vg, bx, by, 3.2f);
			nvgFillColor(vg, s.holding || s.held ? semi::theme::amber() : semi::theme::magenta());
			nvgFill(vg);
		}

		// header: NET a > b, GLIDE %, heat, shape
		char head1[40];
		if (s.from >= 0 && s.to >= 0)
			std::snprintf(head1, sizeof head1, "NET %02d>%02d", s.from + 1, s.to + 1);
		else
			std::snprintf(head1, sizeof head1, "NET %02d", s.to + 1);
		text(vg, pad, 2.f, head1, 10.f, semi::theme::magenta());
		const int pct = (int) std::round(clamp(s.morph, 0.f, 1.f) * 100.f);
		if (s.held) {
			std::snprintf(buf, sizeof buf, "HOLD %d%%", pct);
			const float tw = 0.54f * 10.f * std::strlen(buf) + 4.f;
			fillRect(vg, w * 0.44f - tw * 0.5f, 1.f, tw, 11.f, semi::theme::amber());
			text(vg, w * 0.44f, 2.f, buf, 10.f, semi::theme::screen(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		}
		else {
			std::snprintf(buf, sizeof buf, "GLIDE %d%%", pct);
			text(vg, w * 0.44f, 2.f, buf, 10.f, semi::theme::text(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		}
		const char* shapeName = kShapeNames[clamp(s.shape, 0, kNumShapes - 1)];
		if (s.rangeMask != 0xffff || s.rFrom != 0)
			std::snprintf(buf, sizeof buf, "%02d-%02d %s", s.rFrom + 1, s.rTo + 1, shapeName);
		else
			std::snprintf(buf, sizeof buf, "%s", shapeName);
		text(vg, w - pad, 2.f, buf, 10.f, s.clocked ? semi::theme::cyan() : semi::theme::dim(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
	}
};

// CALM <-> CHAOS: the heat of the net rete is in now (loop gain, log10), as LED segments.
struct MeterScreen : Screen {
	Seedwalk* module = nullptr;
	Snapshot demo;

	MeterScreen() { demo.heatNow = 1.9f; }

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		const float pad = 3.f;
		text(vg, pad, h * 0.5f, "CALM", 9.f, semi::theme::cyan(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		text(vg, w - pad, h * 0.5f, "CHAOS", 9.f, semi::theme::magenta(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
		constexpr int kSeg = 14;
		const float x0 = 26.f, x1 = w - 32.f, gap = 2.f;
		const float sw_ = (x1 - x0 - gap * (kSeg - 1)) / kSeg;
		const float sh = h - 10.f;
		// heat -0.5 (dies away) .. 3.5 (clipped chaos); the edge sits near 0.5
		const float v = clamp((s.heatNow + 0.5f) / 4.f, 0.f, 1.f);
		const int lit = (int) std::round(v * kSeg);
		for (int k = 0; k < kSeg; k++) {
			const float t = (k + 0.5f) / kSeg;
			NVGcolor on = t < 0.4f ? semi::theme::cyan() : t < 0.72f ? semi::theme::amber() : semi::theme::magenta();
			NVGcolor off = t < 0.4f ? semi::theme::cyanDeep() : t < 0.72f ? semi::theme::grid() : semi::theme::magentaDeep();
			fillRect(vg, x0 + k * (sw_ + gap), (h - sh) * 0.5f, sw_, sh, k < lit ? on : off);
		}
	}
};

struct SeedwalkWidget : ModuleWidget {
	SeedwalkWidget(Seedwalk* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Seedwalk.svg")));

		auto* nets = semi::createScreenMm<NetsScreen>(Vec(L::NETS.x, L::NETS.y), Vec(L::NETS.w, L::NETS.h));
		nets->module = module;
		addChild(nets);
		auto* meter = semi::createScreenMm<MeterScreen>(Vec(L::METER.x, L::METER.y), Vec(L::METER.w, L::METER.h));
		meter->module = module;
		addChild(meter);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		addParam(createParamCentered<semi::Button>(at(L::PIN), module, Seedwalk::PIN_PARAM));
		addChild(createLightCentered<semi::MagentaLedSmall>(at(L::PIN_LED), module, Seedwalk::PIN_LIGHT));
		addParam(createParamCentered<semi::Button>(at(L::HOLD), module, Seedwalk::HOLD_PARAM));
		addChild(createLightCentered<semi::AmberLedSmall>(at(L::HOLD_LED), module, Seedwalk::HOLD_LIGHT));
		addParam(createParamCentered<semi::Button>(at(L::RND), module, Seedwalk::RND_PARAM));
		addChild(createLightCentered<semi::MagentaLedSmall>(at(L::RND_LED), module, Seedwalk::RND_LIGHT));

		auto snapKnob = [&](app::ParamWidget* k) {
			if (auto* kn = dynamic_cast<app::Knob*>(k))
				kn->snap = true;
			addParam(k);
		};
		snapKnob(createParamCentered<semi::KnobM>(at(L::STEP), module, Seedwalk::STEP_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::GLIDE), module, Seedwalk::GLIDE_PARAM));
		addParam(createParamCentered<semi::KnobMHot>(at(L::CHAOS), module, Seedwalk::CHAOS_PARAM));
		snapKnob(createParamCentered<semi::KnobM>(at(L::SEED), module, Seedwalk::SEED_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::LVL), module, Seedwalk::LVL_PARAM));
		snapKnob(createParamCentered<semi::KnobM>(at(L::FROM), module, Seedwalk::FROM_PARAM));
		snapKnob(createParamCentered<semi::KnobM>(at(L::TO), module, Seedwalk::TO_PARAM));
		snapKnob(createParamCentered<semi::KnobS>(at(L::SHAPE), module, Seedwalk::SHAPE_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::SPREAD), module, Seedwalk::SPREAD_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::DIP), module, Seedwalk::DIP_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::EXCITE), module, Seedwalk::EXCITE_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::FX), module, Seedwalk::FX_PARAM));
		snapKnob(createParamCentered<semi::KnobS>(at(L::FXMODE), module, Seedwalk::FXMODE_PARAM));

		addInput(createInputCentered<semi::Jack>(at(L::CLK), module, Seedwalk::CLK_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RST), module, Seedwalk::RST_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::CHAOS_CV), module, Seedwalk::CHAOS_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::X), module, Seedwalk::X_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::Y), module, Seedwalk::Y_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::IN), module, Seedwalk::IN_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::HOLD_IN), module, Seedwalk::HOLD_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RND_IN), module, Seedwalk::RND_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::FROM_CV), module, Seedwalk::FROM_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::TO_CV), module, Seedwalk::TO_INPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_L), module, Seedwalk::L_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_R), module, Seedwalk::R_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::NET_OUT), module, Seedwalk::NET_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::EOC), module, Seedwalk::EOC_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Seedwalk*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		std::vector<std::string> walks;
		for (const auto& w : kWalks)
			walks.push_back(w.name);
		menu->addChild(createIndexSubmenuItem("Factory walk", walks,
		                                      [=]() { return (size_t) m->walkIndex; },
		                                      [=](size_t i) { m->walkRequest.store((int) i); }));
		menu->addChild(createIndexSubmenuItem("Glide curve", {"Linear", "Smooth", "Late", "Early"},
		                                      [=]() { return (size_t) m->curve; },
		                                      [=](size_t i) { m->curve = (int) i; }));
		menu->addChild(createBoolMenuItem("Keep the matrix's size through a glide", "",
		                                  [=]() { return m->keepNorm; },
		                                  [=](bool b) { m->keepNorm = b; }));
		menu->addChild(createIndexSubmenuItem("Output clip", {"Off", "Soft", "Hard"},
		                                      [=]() { return (size_t) m->clip; },
		                                      [=](size_t i) { m->clip = (int) i; }));
		menu->addChild(createIndexSubmenuItem("Tempo without CLK", {"60", "90", "120", "140", "160", "174"},
		                                      [=]() { return (size_t) m->bpmIndex; },
		                                      [=](size_t i) { m->bpmIndex = (int) i; }));
		menu->addChild(createIndexSubmenuItem("RND re-rolls", {"The nets in FROM-TO", "All 16 nets"},
		                                      [=]() { return (size_t) (m->rndAll ? 1 : 0); },
		                                      [=](size_t i) { m->rndAll = i == 1; }));
		menu->addChild(createSubmenuItem("Randomise one net", "", [=](Menu* sub) {
			for (int k = 0; k < sw::kSteps; k++)
				sub->addChild(createMenuItem(string::f("Net %02d", k + 1), "",
				                             [=]() { m->rndNetRequest.fetch_or(1u << k); }));
		}));
		menu->addChild(createBoolMenuItem("Beads", "",
		                                  [=]() { return m->fx.bdOn; },
		                                  [=](bool b) { m->fx.bdOn = b; }));
		menu->addChild(createIndexSubmenuItem("Beads quality", {"Hi-fi", "Clean tape", "Lo-fi", "Dirty tape"},
		                                      [=]() { return (size_t) m->fx.bdQ; },
		                                      [=](size_t i) { m->fx.bdQ = (int) i; }));
	}
};

Model* modelSeedwalk = createModel<Seedwalk, SeedwalkWidget>("Seedwalk");
