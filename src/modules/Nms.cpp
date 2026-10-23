// N.M.S. — Rack / MetaModule port of NMS, the modular rebuild of Tim Exile's NMS (New
// Modular System) Reaktor framework (engine: engines/nms, the plugin's JUCE-free module
// framework + Host, the audio side of its processor). Credits: engines/nms/NOTICE.md —
// NMS is Tim Exile's design (prelaunch prototype, commercial product, now Karst); this is
// a private rebuild: ask the author before any public release.
//
// One module plays a whole NMS instrument: a factory patch (Dub Techno, Glitch Percussion,
// Poly Bells, Fast Landscape Slow Water, Organic Breaks, Swarm Garden, Keys > Sawbird,
// Didimidi Drives, Blank Slate) of generators and FX on eight mixer-channel lanes plus the
// RET A / RET B send lanes, with the channel chains normalled and the patch cables of the
// plugin. The screen shows that routing: each lane's chain, the cables between lanes, the
// channel meters, the patch / pattern / position / energy.
//
// Panel: PATCH < > (loads off the audio thread), PATTERN < > RND (recall the dealt patterns
// with BLEND), WANDER on / off + speed, ROLL (randomiser MEDIUM), CHAOS (control noise),
// EVOLVE (a randomiser nudge every 1-16 bars), the 24 NMS macros (relative, centre = 0),
// TEMPO + RUN (TRANSPORT), LEVEL (output trim, soft clip).
// Inputs: CLK (16ths) drives tempo, start / stop and position (the plugin's host-locked
// mode), RST, RUN trigger, V/OCT + GATE play the keyboard (TRANSPORT KEY outs; notes at or
// above LOOP PACK's base recall patterns, as its MIDI keys), NEXT pattern, SEL (0-10 V
// selects a pattern), MOD A / MOD B (+-5 V on two macros chosen in the menu).
// Outputs: T1-T8 = each channel's first instrument's TRIG, CLK (16ths), BUS (POS, ENERGY,
// SCENE = pattern, EVENT on 4 / 8 / 16 / 32-bar lines), L / R.
//
// Not ported: the RACK editor (adding / moving / patching modules), the original Schwung
// modules SIGNAL / ESSAIM / BREAKBEAT / STRUCTOR / DISSOLVER (native modules stand in, see
// engines/nms/UpstreamStub.cpp), user presets, undo / locks, the 4 global LFOs, recorder,
// SCHAEFER sample loading (its built-in loop plays), DIDIMIDI MIDI out, themes.

#include "../plugin.hpp"
#include "../common/AsyncJob.hpp"
#include "../common/Components.hpp"
#include "../common/FamilyBusCV.hpp"
#include "../common/Sample.hpp"   // semi::Slot
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/NmsLayout.hpp"
#include "nms/Host.h"

#ifdef METAMODULE
#include "threads/async_thread.hh"
#endif

namespace L = semi::layout::nms;
namespace bus = semi::bus;
using semi::Screen;

namespace {

constexpr int kBlock = 32;
constexpr float kVoltsPerUnit = 5.f;
constexpr int kNumMacros = ::nms::kMacros;
constexpr int kEvolveBars[] = {0, 1, 2, 4, 8, 16};

struct Snapshot {
	bool loaded = false, busy = false, running = false, ext = false, wander = false, hasWander = false;
	char patch[28] = {};
	float bpm = 120.f;
	double pos16 = 0.0;
	int pattern = -1, numPatterns = 0;
	float blend = 1.f, energy = 0.f;
	float level[::nms::kChannels] = {};
	float master = 0.f;
	bool trig[::nms::kChannels] = {};
	::nms::RouteView view;
};

// Saved per-module control values, applied after a rebuild (patch load, sample rate).
struct SavedState {
	int patch = 0;
	std::vector<std::string> types;
	std::vector<std::vector<float>> norms;
	int pattern = -1;
	bool valid = false;
};

} // namespace

struct Nms : Module {
	enum ParamId { PATCH_PREV_PARAM, PATCH_NEXT_PARAM, PTN_PREV_PARAM, PTN_NEXT_PARAM, PTN_RND_PARAM, WANDER_PARAM, ROLL_PARAM, RUN_PARAM,
		           ENUMS(MACRO_PARAM, kNumMacros), TEMPO_PARAM, WSPEED_PARAM, CHAOS_PARAM, LEVEL_PARAM, BLEND_PARAM, EVOLVE_PARAM, PARAMS_LEN };
	enum InputId { CLK_INPUT, RST_INPUT, RUN_INPUT, VOCT_INPUT, GATE_INPUT, NEXT_INPUT, SEL_INPUT, MOD_A_INPUT, MOD_B_INPUT, INPUTS_LEN };
	enum OutputId { ENUMS(TRIG_OUTPUT, ::nms::kChannels), CLK_OUTPUT, BUS_OUTPUT, OUT_L_OUTPUT, OUT_R_OUTPUT, OUTPUTS_LEN };
	enum LightId { WANDER_LIGHT, RUN_LIGHT, LIGHTS_LEN };

	// ---- worker side -----------------------------------------------------------------
	semi::Slot<::nms::Host> slot;
	std::atomic<int> wantPatch{-1};
	std::atomic<bool> keepState{false};      // rebuild of the same patch keeping the controls (sample rate)
	std::atomic_flag workerBusy = ATOMIC_FLAG_INIT;
	SavedState pendingState;                 // guarded by workerBusy
	std::atomic<double> sampleRate{44100.0};
	std::atomic<int> patchShown{0};
	std::atomic<bool> building{false};

	// ---- audio side -----------------------------------------------------------------------
	::nms::Host* host = nullptr;
	float inL[kBlock] = {}, outL[kBlock] = {}, outR[kBlock] = {};
	float trigBuf[::nms::kChannels][kBlock] = {};
	int pos = 0;

	// clock
	dsp::SchmittTrigger clkTrig, rstTrig, runTrig, nextTrig, gateTrig;
	dsp::BooleanTrigger patchPrevBtn, patchNextBtn, ptnPrevBtn, ptnNextBtn, ptnRndBtn, wanderBtn, rollBtn, runBtn;
	long long clkEdges = 0;
	float sinceClk = 0.f, clkPeriod = 0.f;
	bool clkAlive = false;
	bool rstPending = false, gateHeld = false;
	int heldNote = -1, noteOnPending = -1, noteOffPending = -1;
	int nextPending = 0, lastSel = -2;
	int rollPending = 0;
	float lastTempo = -1.f, lastWSpeed = -1.f, lastBlend = -1.f;
	dsp::PulseGenerator clkOut, busEvent;
	float busEventV = 0.f;
	long long lastStep16 = -1;

	// menu (saved)
	int modA = 23, modB = 16;    // SPACE, TONE
	bool softClip = true;
	int evolveAmount = 0;

	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;

#ifdef METAMODULE
	semi::AsyncJob worker{this, [this]() { serviceBuild(); }};
#endif

	~Nms() override {
		// background jobs first (and wait for a run in flight): they write into this module
#ifdef METAMODULE
		worker.stopAndWait();
#endif
	}

	Nms() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configButton(PATCH_PREV_PARAM, "Previous factory patch");
		configButton(PATCH_NEXT_PARAM, "Next factory patch");
		configButton(PTN_PREV_PARAM, "Previous pattern");
		configButton(PTN_NEXT_PARAM, "Next pattern");
		configButton(PTN_RND_PARAM, "Random pattern");
		configButton(WANDER_PARAM, "WANDER play / stop");
		configButton(ROLL_PARAM, "Roll the dice (randomise MEDIUM)");
		configButton(RUN_PARAM, "Run / stop (TRANSPORT)");
		for (int m = 0; m < kNumMacros; m++)
			configParam(MACRO_PARAM + m, -1.f, 1.f, 0.f, string::f("Macro %d %s (%s)", m + 1, ::nms::macroName(m), ::nms::macroGroup(m)), " %", 0.f, 100.f);
		configParam(TEMPO_PARAM, 40.f, 240.f, 118.f, "Tempo (without CLK)", " BPM");
		configParam(WSPEED_PARAM, 0.f, 1.f, 0.35f, "WANDER speed (1 change / 32 bars .. / bar)", " %", 0.f, 100.f);
		configParam(CHAOS_PARAM, 0.f, 1.f, 0.f, "CHAOS amount (control noise)", " %", 0.f, 100.f);
		configParam(LEVEL_PARAM, -24.f, 12.f, 0.f, "Output trim", " dB");
		configParam(BLEND_PARAM, 0.f, 8.f, 0.5f, "Pattern blend time", " s");
		configSwitch(EVOLVE_PARAM, 0.f, 5.f, 0.f, "Evolve (randomiser every N bars)", {"Off", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars"});
		configInput(CLK_INPUT, "Clock (16ths; drives tempo, run and position)");
		configInput(RST_INPUT, "Reset");
		configInput(RUN_INPUT, "Run / stop trigger");
		configInput(VOCT_INPUT, "Keyboard V/OCT");
		configInput(GATE_INPUT, "Keyboard gate");
		configInput(NEXT_INPUT, "Next pattern trigger");
		configInput(SEL_INPUT, "Pattern select (0-10 V)");
		configInput(MOD_A_INPUT, "Macro CV A (+-5 V, macro in the menu)");
		configInput(MOD_B_INPUT, "Macro CV B (+-5 V, macro in the menu)");
		for (int c = 0; c < ::nms::kChannels; c++)
			configOutput(TRIG_OUTPUT + c, string::f("Channel %d trigger", c + 1));
		configOutput(CLK_OUTPUT, "16th clock");
		configOutput(BUS_OUTPUT, "FamilyBus (POS, ENERGY, SCENE = pattern, EVENT)");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		configLight(WANDER_LIGHT, "WANDER playing");
		configLight(RUN_LIGHT, "Running");

		wantPatch = 0;
		serviceBuild();   // first patch on the constructing thread
#ifdef METAMODULE
		worker.start();
#endif
	}

	// ---- worker ----------------------------------------------------------------------------
	static SavedState capture(const ::nms::Host& h) {
		SavedState s;
		s.patch = h.patchIndex;
		for (auto& m : h.rack.mods) {
			s.types.push_back(m->spec.id);
			std::vector<float> v(m->n);
			for (size_t i = 0; i < m->n; i++)
				v[i] = m->getNorm((int) i);
			s.norms.push_back(std::move(v));
		}
		s.pattern = h.currentPattern;
		s.valid = true;
		return s;
	}

	static void apply(::nms::Host& h, const SavedState& s) {
		if (!s.valid || s.types.size() != h.rack.mods.size())
			return;
		for (size_t k = 0; k < h.rack.mods.size(); k++) {
			auto& m = h.rack.mods[k];
			if (m->spec.id != s.types[k] || s.norms[k].size() != m->n)
				return;
		}
		for (size_t k = 0; k < h.rack.mods.size(); k++)
			for (size_t i = 0; i < h.rack.mods[k]->n; i++)
				h.rack.mods[k]->setNorm((int) i, s.norms[k][i]);
		if (s.pattern >= 0 && s.pattern < (int) h.patterns.size())
			h.currentPattern = s.pattern;
	}

	// Rack: the screen's step() (UI thread) and the constructor; MetaModule: the AsyncThread.
	void serviceBuild() {
		slot.collect();
		const int want = wantPatch.load();
		if (want < 0)
			return;
		if (workerBusy.test_and_set(std::memory_order_acquire))
			return;
		wantPatch = -1;
		building = true;
		auto* h = new ::nms::Host;
		if (h->build(want, sampleRate.load())) {
			if (keepState.exchange(false) || (pendingState.valid && pendingState.patch == h->patchIndex))
				apply(*h, pendingState);
			pendingState.valid = false;
			// panel knobs follow the patch's own TRANSPORT tempo / WANDER speed
			if (auto* t = h->find("TRANSPORT"))
				params[TEMPO_PARAM].setValue(h->param(t, "tempo"));
			if (auto* w = h->find("WANDER"))
				params[WSPEED_PARAM].setValue(h->param(w, "speed"));
			if (auto* p = h->find("PATTERNS"))
				params[BLEND_PARAM].setValue(h->param(p, "blend"));
			patchShown = h->patchIndex;
			slot.offer(h);
		}
		else
			delete h;
		building = false;
		workerBusy.clear(std::memory_order_release);
	}

	void requestPatch(int p) {
		const int n = (int) ::nms::factoryPatches().size();
		wantPatch = ((p % n) + n) % n;
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		sampleRate = e.sampleRate;
		// rebuild the current patch at the new rate, keeping the controls
		while (workerBusy.test_and_set(std::memory_order_acquire)) {
		}
		if (host) {
			pendingState = capture(*host);
			keepState = true;
		}
		workerBusy.clear(std::memory_order_release);
		wantPatch = patchShown.load();
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "patch", json_integer(patchShown.load()));
		json_object_set_new(root, "modA", json_integer(modA));
		json_object_set_new(root, "modB", json_integer(modB));
		json_object_set_new(root, "softClip", json_boolean(softClip));
		json_object_set_new(root, "evolveAmount", json_integer(evolveAmount));
		while (workerBusy.test_and_set(std::memory_order_acquire)) {
		}
		::nms::Host* h = slot.active;   // alive: retired hosts are only deleted under workerBusy
		if (h) {
			const SavedState s = capture(*h);
			json_t* mods = json_array();
			for (size_t k = 0; k < s.types.size(); k++) {
				json_t* o = json_object();
				json_object_set_new(o, "type", json_string(s.types[k].c_str()));
				json_t* v = json_array();
				for (float f : s.norms[k])
					json_array_append_new(v, json_real(f));
				json_object_set_new(o, "norms", v);
				json_array_append_new(mods, o);
			}
			json_object_set_new(root, "modules", mods);
			json_object_set_new(root, "pattern", json_integer(s.pattern));
		}
		workerBusy.clear(std::memory_order_release);
		return root;
	}

	void dataFromJson(json_t* root) override {
		auto geti = [&](const char* k, int& v, int lo, int hi) {
			if (json_t* j = json_object_get(root, k))
				v = clamp((int) json_integer_value(j), lo, hi);
		};
		geti("modA", modA, 0, kNumMacros - 1);
		geti("modB", modB, 0, kNumMacros - 1);
		geti("evolveAmount", evolveAmount, 0, 2);
		if (json_t* j = json_object_get(root, "softClip"))
			softClip = json_boolean_value(j);
		int patch = 0;
		geti("patch", patch, 0, (int) ::nms::factoryPatches().size() - 1);
		SavedState s;
		s.patch = patch;
		size_t i;
		json_t* o;
		json_array_foreach(json_object_get(root, "modules"), i, o) {
			json_t* t = json_object_get(o, "type");
			s.types.push_back(t && json_string_value(t) ? json_string_value(t) : "");
			std::vector<float> v;
			size_t k;
			json_t* f;
			json_array_foreach(json_object_get(o, "norms"), k, f) v.push_back((float) json_number_value(f));
			s.norms.push_back(std::move(v));
		}
		geti("pattern", s.pattern, -1, 64);
		s.valid = !s.types.empty();
		while (workerBusy.test_and_set(std::memory_order_acquire)) {
		}
		pendingState = std::move(s);
		workerBusy.clear(std::memory_order_release);
		wantPatch = patch;
#ifndef METAMODULE
		serviceBuild();
#endif
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		modA = 23;
		modB = 16;
		softClip = true;
		evolveAmount = 0;
		wantPatch = 0;
	}

	// ---- audio ---------------------------------------------------------------------------
	void renderBlock() {
		if (slot.poll()) {
			host = slot.active;
			lastTempo = lastWSpeed = lastBlend = -1.f;
			lastSel = -2;
		}
		if (!host) {
			std::fill(outL, outL + kBlock, 0.f);
			std::fill(outR, outR + kBlock, 0.f);
			return;
		}
		::nms::Host& h = *host;

		// panel -> host
		for (int m = 0; m < kNumMacros; m++)
			h.macro[m] = 0.5f + 0.5f * params[MACRO_PARAM + m].getValue();
		h.macro[modA] = clamp(h.macro[modA] + inputs[MOD_A_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		h.macro[modB] = clamp(h.macro[modB] + inputs[MOD_B_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		h.chaosKnob = params[CHAOS_PARAM].getValue();
		h.outDb = params[LEVEL_PARAM].getValue();
		h.softClip = softClip;
		h.evolveBars = kEvolveBars[clamp((int) std::round(params[EVOLVE_PARAM].getValue()), 0, 5)];
		h.evolveAmount = evolveAmount;
		::nms::Module* tr = h.find("TRANSPORT");
		::nms::Module* wa = h.find("WANDER");
		if (auto* c = h.find("CHAOS"))
			h.setParam(c, "amt", h.chaosKnob);
		const float tempo = params[TEMPO_PARAM].getValue();
		if (tempo != lastTempo) {
			lastTempo = tempo;
			h.setParam(tr, "tempo", tempo);
		}
		const float ws = params[WSPEED_PARAM].getValue();
		if (ws != lastWSpeed) {
			lastWSpeed = ws;
			h.setParam(wa, "speed", ws);
		}
		const float bl = params[BLEND_PARAM].getValue();
		if (bl != lastBlend) {
			lastBlend = bl;
			h.setParam(h.find("PATTERNS"), "blend", bl);
		}
		if (runPending) {
			runPending = false;
			h.setParam(tr, "run", h.param(tr, "run") > 0.5f ? 0.f : 1.f);
		}
		if (wanderPending) {
			wanderPending = false;
			h.setParam(wa, "play", h.param(wa, "play") > 0.5f ? 0.f : 1.f);
		}

		// keys, patterns
		if (rstPending) {
			rstPending = false;
			h.restart();
		}
		if (noteOffPending >= 0) {
			h.noteOff(noteOffPending);
			noteOffPending = -1;
		}
		if (noteOnPending >= 0) {
			h.noteOn(noteOnPending, 0.8f);
			noteOnPending = -1;
		}
		if (gateHeld)
			h.keyPitchCv(inputs[VOCT_INPUT].getVoltage() + 1.f);
		while (nextPending > 0) {
			nextPending--;
			h.nextPattern(1);
		}
		while (nextPending < 0) {
			nextPending++;
			h.nextPattern(-1);
		}
		if (rndPending) {
			rndPending = false;
			h.randomPattern();
		}
		if (rollPending) {
			rollPending = 0;
			h.randomize(1);
		}
		if (inputs[SEL_INPUT].isConnected() && !h.patterns.empty()) {
			const int n = (int) h.patterns.size();
			const int sel = std::min(n - 1, (int) (clamp(inputs[SEL_INPUT].getVoltage() / 10.f, 0.f, 1.f) * n));
			if (sel != lastSel) {
				lastSel = sel;
				h.recallBlended(sel);
			}
		}
		if (h.macroResetRequest) {
			h.macroResetRequest = false;
			for (int m = 0; m < kNumMacros; m++)
				params[MACRO_PARAM + m].setValue(0.f);
		}

		// transport from CLK
		::nms::ExtTransport ext;
		if (inputs[CLK_INPUT].isConnected()) {
			ext.known = true;
			ext.playing = clkAlive && clkEdges > 0;
			ext.bpm = clkPeriod > 0.f ? 15.0 / clkPeriod : tempo;
			const double frac = clkPeriod > 0.f ? std::min(0.999, (double) sinceClk / clkPeriod) : 0.0;
			ext.pos16 = (double) std::max(0LL, clkEdges - 1) + frac;
		}

		h.process(outL, outR, kBlock, ext);

		for (int c = 0; c < ::nms::kChannels; c++) {
			const int mi = h.view.trigMod[c], oi = h.view.trigOut[c];
			const float v = (mi >= 0 && oi >= 0) ? h.rack.outValue(mi, oi) : 0.f;
			std::fill(trigBuf[c], trigBuf[c] + kBlock, v > 0.5f ? 10.f : 0.f);
		}

		// 16th clock and bar-line events from the instrument's position
		const long long s16 = (long long) std::floor(h.ctx.pos16 + 1e-9);
		if (h.ctx.running && s16 != lastStep16) {
			lastStep16 = s16;
			clkOut.trigger(0.002f);
			if (s16 % 64 == 0) {
				const long long bar = s16 / 16;
				const int line = bar % 32 == 0 ? 32 : (bar % 16 == 0 ? 16 : (bar % 8 == 0 ? 8 : 4));
				busEventV = bus::turnToVolts(line);
				busEvent.trigger(bus::kPulseSec);
			}
		}
		if (!h.ctx.running)
			lastStep16 = -1;
	}

	bool runPending = false, wanderPending = false, rndPending = false;

	void process(const ProcessArgs& args) override {
		const float dt = args.sampleTime;
		if (patchPrevBtn.process(params[PATCH_PREV_PARAM].getValue() > 0.f))
			requestPatch(patchShown.load() - 1);
		if (patchNextBtn.process(params[PATCH_NEXT_PARAM].getValue() > 0.f))
			requestPatch(patchShown.load() + 1);
		if (ptnPrevBtn.process(params[PTN_PREV_PARAM].getValue() > 0.f))
			nextPending--;
		if (ptnNextBtn.process(params[PTN_NEXT_PARAM].getValue() > 0.f) | nextTrig.process(inputs[NEXT_INPUT].getVoltage(), 0.1f, 1.f))
			nextPending++;
		if (ptnRndBtn.process(params[PTN_RND_PARAM].getValue() > 0.f))
			rndPending = true;
		if (wanderBtn.process(params[WANDER_PARAM].getValue() > 0.f))
			wanderPending = true;
		if (rollBtn.process(params[ROLL_PARAM].getValue() > 0.f))
			rollPending = 1;
		if (runBtn.process(params[RUN_PARAM].getValue() > 0.f) | runTrig.process(inputs[RUN_INPUT].getVoltage(), 0.1f, 1.f))
			runPending = true;
		if (rstTrig.process(inputs[RST_INPUT].getVoltage(), 0.1f, 1.f)) {
			rstPending = true;
			clkEdges = 0;
		}

		// keyboard: V/OCT 0 V = C4 = MIDI 60
		const bool g = inputs[GATE_INPUT].getVoltage() >= 1.f;
		if (g && !gateHeld) {
			heldNote = clamp((int) std::lround(inputs[VOCT_INPUT].getVoltage() * 12.f) + 60, 0, 127);
			noteOnPending = heldNote;
		}
		else if (!g && gateHeld && heldNote >= 0) {
			noteOffPending = heldNote;
			heldNote = -1;
		}
		gateHeld = g;

		// clock
		sinceClk += dt;
		if (clkTrig.process(inputs[CLK_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (clkEdges > 0 && sinceClk < 4.f)
				clkPeriod = sinceClk;
			sinceClk = 0.f;
			clkEdges++;
			clkAlive = true;
		}
		if (clkPeriod > 0.f && sinceClk > 2.5f * clkPeriod)
			clkAlive = false;

		outputs[OUT_L_OUTPUT].setVoltage(outL[pos] * kVoltsPerUnit);
		outputs[OUT_R_OUTPUT].setVoltage(outR[pos] * kVoltsPerUnit);
		for (int c = 0; c < ::nms::kChannels; c++)
			outputs[TRIG_OUTPUT + c].setVoltage(trigBuf[c][pos]);
		if (++pos >= kBlock) {
			pos = 0;
			renderBlock();
		}
		outputs[CLK_OUTPUT].setVoltage(clkOut.process(dt) ? 10.f : 0.f);

		outputs[BUS_OUTPUT].setChannels(bus::kChannels);
		const double p16 = host ? host->ctx.pos16 : 0.0;
		outputs[BUS_OUTPUT].setVoltage(bus::posToVolts((float) std::fmod(p16 / 16.0, (double) bus::kBarsPerCycle)), bus::kPos);
		outputs[BUS_OUTPUT].setVoltage(host ? clamp(host->energy, 0.f, 1.f) * 10.f : 0.f, bus::kEnergy);
		outputs[BUS_OUTPUT].setVoltage(bus::sceneToVolts(host ? std::max(0, host->currentPattern) : 0), bus::kScene);
		outputs[BUS_OUTPUT].setVoltage(busEvent.process(dt) ? busEventV : 0.f, bus::kEvent);

		if (ui.tick(dt))
			publish();
	}

	void publish() {
		Snapshot& s = snap.writeSlot();
		s.busy = building.load() || wantPatch.load() >= 0;
		s.loaded = host != nullptr;
		s.ext = inputs[CLK_INPUT].isConnected();
		if (host) {
			::nms::Host& h = *host;
			std::snprintf(s.patch, sizeof s.patch, "%s", h.patchName.c_str());
			s.running = h.ctx.running;
			s.bpm = (float) h.ctx.bpm;
			s.pos16 = h.ctx.pos16;
			s.pattern = h.currentPattern;
			s.numPatterns = (int) h.patterns.size();
			s.blend = h.blendPos;
			s.energy = h.energy;
			::nms::Module* wa = h.find("WANDER");
			s.hasWander = wa != nullptr;
			s.wander = wa && h.param(wa, "play") > 0.5f;
			if (auto* mx = h.find("MIXER")) {
				for (int c = 0; c < ::nms::kChannels; c++)
					s.level[c] = mx->disp[(size_t) c].load(std::memory_order_relaxed);
				s.master = mx->disp[8].load(std::memory_order_relaxed);
			}
			for (int c = 0; c < ::nms::kChannels; c++)
				s.trig[c] = trigBuf[c][0] > 0.f;
			s.view = h.view;
		}
		lights[WANDER_LIGHT].setBrightness(s.wander ? 1.f : 0.f);
		lights[RUN_LIGHT].setBrightness(s.running ? 1.f : 0.f);
		snap.publish();
	}
};

// The routing: one row per lane (CH 1-8, RET A, RET B) with its module chain left to right
// (instruments cyan, FX dim) closing in the channel meter; cables between lanes as magenta
// links in the left gutter; the top line holds the patch, the controller modules, tempo,
// bar / step, pattern n / N with its blend, and the ENERGY bus.
struct RoutingScreen : Screen {
	Nms* module = nullptr;

	// browser preview: the first factory patch, built once
	static const Snapshot& demoSnap() {
		static Snapshot demo = makeDemo();
		return demo;
	}
	static Snapshot makeDemo() {
		Snapshot demo;
		::nms::Host h;
		h.build(0, 48000.0);
		demo.loaded = true;
		demo.running = true;
		std::snprintf(demo.patch, sizeof demo.patch, "%s", h.patchName.c_str());
		demo.bpm = 118.f;
		demo.pos16 = 37.f;
		demo.pattern = 2;
		demo.numPatterns = (int) h.patterns.size();
		demo.energy = 0.6f;
		demo.view = h.view;
		for (int c = 0; c < 5; c++)
			demo.level[c] = 0.2f + 0.12f * c;
		demo.trig[0] = true;
		return demo;
	}

	void step() override {
		Screen::step();
#ifndef METAMODULE
		if (module)
			module->serviceBuild();   // Rack: patch builds run here (MetaModule: Nms::worker)
#endif
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demoSnap();
		const float w = box.size.x, h = box.size.y;
		const float pad = 3.f;
		const float fs = std::max(8.f, semi::platform::kMinText);
		const float cw = fs * 0.54f;
		char buf[64];

		if (!s.loaded) {
			text(vg, w * 0.5f, h * 0.5f, "BUILDING PATCH...", fs + 1.f, semi::theme::dim(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			return;
		}

		// header: patch, tempo / position, pattern, energy
		text(vg, pad, pad, s.busy ? "LOADING..." : s.patch, fs, s.busy ? semi::theme::amber() : semi::theme::cyan());
		const long long st = (long long) std::floor(s.pos16);
		std::snprintf(buf, sizeof buf, "%s %3.0f  %03lld.%02lld", s.ext ? "EXT" : "BPM", s.bpm, st / 16 + 1, st % 16 + 1);
		text(vg, w - pad, pad, buf, fs, s.running ? semi::theme::amber() : semi::theme::dim(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);

		const float y1 = pad + fs + 2.f;
		if (s.numPatterns > 0)
			std::snprintf(buf, sizeof buf, "PTN %02d/%02d", s.pattern + 1, s.numPatterns);
		else
			std::snprintf(buf, sizeof buf, "PTN --");
		text(vg, pad, y1, buf, fs, semi::theme::text());
		const float bx = pad + cw * 10.f, bw = 26.f;
		fillRect(vg, bx, y1 + 2.f, bw, fs - 4.f, semi::theme::cell());
		fillRect(vg, bx, y1 + 2.f, bw * clamp(s.blend, 0.f, 1.f), fs - 4.f, semi::theme::cyanDim());
		// controller modules
		float tx = bx + bw + cw;
		for (int i = 0; i < s.view.numTop && tx < w * 0.68f; i++) {
			const char* n = s.view.top[i];
			if (!std::strcmp(n, "TRANSPORT") || !std::strcmp(n, "MOD LFO"))
				continue;
			const NVGcolor c = (!std::strcmp(n, "WANDER") && s.wander) ? semi::theme::amber() : semi::theme::dim();
			text(vg, tx, y1, n, fs, c);
			tx += cw * (std::strlen(n) + 1.f);
		}
		// energy
		const float ew = 34.f, ex = w - pad - ew;
		text(vg, ex - 2.f, y1, "NRG", fs, semi::theme::dim(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		fillRect(vg, ex, y1 + 2.f, ew, fs - 4.f, semi::theme::cell());
		fillRect(vg, ex, y1 + 2.f, ew * clamp(s.energy, 0.f, 1.f), fs - 4.f, semi::theme::magenta());

		// lanes
		const float ly0 = y1 + fs + 3.f, ly1 = h - pad;
		const float rowH = (ly1 - ly0) / ::nms::kLanes;
		const float gutter = 12.f, labelW = cw * 2.f + 3.f;
		const float meterW = 26.f;
		const float cx0 = pad + gutter + labelW, cx1 = w - pad - meterW - 4.f;
		static const char* laneNames[::nms::kLanes] = {"1", "2", "3", "4", "5", "6", "7", "8", "RA", "RB"};
		for (int l = 0; l < ::nms::kLanes; l++) {
			const float y = ly0 + l * rowH, ym = y + rowH * 0.5f;
			const auto& ln = s.view.lanes[l];
			const bool trig = l < ::nms::kChannels && s.trig[l];
			text(vg, pad + gutter, ym, laneNames[l], fs, trig ? semi::theme::amber() : (ln.n ? semi::theme::text() : semi::theme::grid()),
			     NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			if (ln.n == 0) {
				line(vg, cx0, ym, cx1, ym, semi::theme::grid());
				continue;
			}
			// the chain: names separated by arrows, shrunk to fit
			float x = cx0;
			const float avail = cx1 - cx0;
			int total = 0;
			for (int k = 0; k < ln.n; k++)
				total += (int) std::strlen(ln.names[k]) + 2;
			const int maxChars = std::max(3, (int) (avail / cw) / std::max(1, ln.n) - 2);
			for (int k = 0; k < ln.n; k++) {
				char nm[12];
				std::snprintf(nm, sizeof nm, "%.*s", total * cw > avail ? maxChars : 10, ln.names[k]);
				text(vg, x, ym, nm, fs, ln.fx[k] ? semi::theme::dim() : semi::theme::cyan(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
				x += cw * std::strlen(nm);
				if (k + 1 < ln.n) {
					text(vg, x + cw * 0.3f, ym, ">", fs, semi::theme::grid(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
					x += cw * 1.6f;
				}
			}
			// channel meter (returns: no meter of their own)
			const float mx = w - pad - meterW;
			fillRect(vg, mx, y + rowH * 0.25f, meterW, rowH * 0.5f, semi::theme::cell());
			if (l < ::nms::kChannels) {
				const float lv = clamp(s.level[l], 0.f, 1.f);
				if (lv > 0.01f)
					fillRect(vg, mx, y + rowH * 0.25f, meterW * lv, rowH * 0.5f, lv > 0.9f ? semi::theme::amber() : semi::theme::cyanDim());
			}
		}
		// cables: links between lanes in the gutter (from the top line when the source has no lane)
		for (int i = 0; i < s.view.numCables; i++) {
			const auto& c = s.view.cables[i];
			if (c.dstLane < 0)
				continue;
			const float ya = c.srcLane >= 0 ? ly0 + (c.srcLane + 0.5f) * rowH : ly0;
			const float yb = ly0 + (c.dstLane + 0.5f) * rowH;
			const float gx = pad + 2.f + (i % 4) * 2.4f;
			const NVGcolor col = semi::theme::magenta();
			if (std::fabs(ya - yb) < 1.f) {
				fillRect(vg, gx, yb - 1.2f, 2.4f, 2.4f, col);
				continue;
			}
			line(vg, pad + gutter - 1.f, ya, gx, ya, col, semi::platform::kThinStroke);
			line(vg, gx, ya, gx, yb, col, semi::platform::kThinStroke);
			line(vg, gx, yb, pad + gutter - 1.f, yb, col, semi::platform::kThinStroke);
		}
	}
};

struct NmsWidget : ModuleWidget {
	NmsWidget(Nms* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Nms.svg")));

		auto* scr = semi::createScreenMm<RoutingScreen>(Vec(L::ROUTING.x, L::ROUTING.y), Vec(L::ROUTING.w, L::ROUTING.h));
		scr->module = module;
		addChild(scr);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		addParam(createParamCentered<semi::Button>(at(L::PATCH_PREV), module, Nms::PATCH_PREV_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::PATCH_NEXT), module, Nms::PATCH_NEXT_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::PTN_PREV), module, Nms::PTN_PREV_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::PTN_NEXT), module, Nms::PTN_NEXT_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::PTN_RND), module, Nms::PTN_RND_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::WANDER), module, Nms::WANDER_PARAM));
		addChild(createLightCentered<semi::AmberLedSmall>(at(L::WANDER_LED), module, Nms::WANDER_LIGHT));
		addParam(createParamCentered<semi::Button>(at(L::ROLL), module, Nms::ROLL_PARAM));
		addParam(createParamCentered<semi::PadCyan>(at(L::RUN), module, Nms::RUN_PARAM));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::RUN_LED), module, Nms::RUN_LIGHT));

		const L::P macros[kNumMacros] = {L::M1, L::M2, L::M3, L::M4, L::M5, L::M6, L::M7, L::M8, L::M9, L::M10, L::M11, L::M12,
		                                 L::M13, L::M14, L::M15, L::M16, L::M17, L::M18, L::M19, L::M20, L::M21, L::M22, L::M23, L::M24};
		for (int m = 0; m < kNumMacros; m++)
			addParam(createParamCentered<semi::KnobS>(at(macros[m]), module, Nms::MACRO_PARAM + m));
		addParam(createParamCentered<semi::KnobM>(at(L::TEMPO), module, Nms::TEMPO_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::WSPEED), module, Nms::WSPEED_PARAM));
		addParam(createParamCentered<semi::KnobMHot>(at(L::CHAOS), module, Nms::CHAOS_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::LEVEL), module, Nms::LEVEL_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::BLEND), module, Nms::BLEND_PARAM));
		auto* evo = createParamCentered<semi::KnobMHot>(at(L::EVOLVE), module, Nms::EVOLVE_PARAM);
		evo->snap = true;
		addParam(evo);

		addInput(createInputCentered<semi::Jack>(at(L::CLK), module, Nms::CLK_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RST), module, Nms::RST_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RUN_IN), module, Nms::RUN_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::VOCT), module, Nms::VOCT_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::GATE), module, Nms::GATE_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::NEXT), module, Nms::NEXT_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::SEL), module, Nms::SEL_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::MOD_A), module, Nms::MOD_A_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::MOD_B), module, Nms::MOD_B_INPUT));

		const L::P trigs[::nms::kChannels] = {L::TRIG1, L::TRIG2, L::TRIG3, L::TRIG4, L::TRIG5, L::TRIG6, L::TRIG7, L::TRIG8};
		for (int c = 0; c < ::nms::kChannels; c++)
			addOutput(createOutputCentered<semi::Jack>(at(trigs[c]), module, Nms::TRIG_OUTPUT + c));
		addOutput(createOutputCentered<semi::Jack>(at(L::CLK_OUT), module, Nms::CLK_OUTPUT));
		addOutput(createOutputCentered<semi::BusJack>(at(L::BUS), module, Nms::BUS_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_L), module, Nms::OUT_L_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_R), module, Nms::OUT_R_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Nms*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		std::vector<std::string> patches;
		for (auto& f : ::nms::factoryPatches())
			patches.push_back(string::f("%s  (%s)", f.name, f.category));
		menu->addChild(createIndexSubmenuItem("Factory patch", patches,
		                                      [=]() { return (size_t) m->patchShown.load(); },
		                                      [=](size_t i) { m->requestPatch((int) i); }));
		std::vector<std::string> macros;
		for (int i = 0; i < kNumMacros; i++)
			macros.push_back(string::f("%d %s (%s)", i + 1, ::nms::macroName(i), ::nms::macroGroup(i)));
		menu->addChild(createIndexSubmenuItem("MOD A -> macro", macros, [=]() { return (size_t) m->modA; }, [=](size_t i) { m->modA = (int) i; }));
		menu->addChild(createIndexSubmenuItem("MOD B -> macro", macros, [=]() { return (size_t) m->modB; }, [=](size_t i) { m->modB = (int) i; }));
		menu->addChild(createIndexSubmenuItem("Evolve amount", {"Nudge", "Medium", "A lot"},
		                                      [=]() { return (size_t) m->evolveAmount; }, [=](size_t i) { m->evolveAmount = (int) i; }));
		menu->addChild(createBoolPtrMenuItem("Soft clip", "", &m->softClip));
		menu->addChild(createMenuItem("Centre all macros", "", [=]() {
			for (int i = 0; i < kNumMacros; i++)
				m->params[Nms::MACRO_PARAM + i].setValue(0.f);
		}));
	}
};

Model* modelNms = createModel<Nms, NmsWidget>("Nms");
