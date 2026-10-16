// FLOATER — Rack / MetaModule port of FLOATER v0.7 ("a floater case in a plug-in"; engine: engines/floater,
// vendored from semierendonmez-commits/floater Source/engine, Mutable Instruments Plaits / Braids / Clouds /
// Beads, Parasites, CloudSeed and the Drum Buss model from engines/thirdparty).
//
// The whole case runs: master clock -> rotating divider -> eight lanes (Euclidean or drawn, chance, bursts,
// LOCK, conditions, logic, VARY) -> seven module slots (own voices, 24 Plaits engines, 31 Braids shapes,
// REEL SPLICE) + SCATTER -> SMR -> ZIGZAG -> SPACE -> REEL -> Drum Buss, the pin matrix, SEQ, the DRONE,
// RESONATOR, ARC and FEEDBACK. The plug-in's patch layer (parameters, 34 factory patches, NEW PATCH,
// MUTATE, locks) is engines/floater/Case.
//
// Panel = the playable core: lane select (button per lane, hit LED) + the selected lane's ring and module,
// clock / reel / fx macros, NEW PATCH and MUTATE. Menu-only (saved in the patch): everything else of the
// plug-in's pages (model per slot, X-MOD, SUSTAIN, pan, SCATTER model and its other controls, the pin matrix,
// CV sources, SEQ, ZIGZAG / SMR / SPACE / REEL / FEEDBACK details, DRONE, RESONATOR, ARC, bus, clock extras,
// conditions and step locks are kept from patches but can only be cleared, not drawn). Dropped: the CASE
// view, scope/spectrum, patch notes, DRAG MIX / STEMS, user preset files, themes, MIDI (CV/gates instead).
//
// Position: CLK = 1/16 by default (menu: 1/8, 1/4, 24 PPQN), RST = bar 1. Without CLK the module runs its
// own clock at BPM while RUN is on. The engine always gets a position from the module, so its own internal
// clock (the plug-in's RUN with the host stopped) is never used.
// Audio: engine +-1 = +-5 V. Gates: 10 V, 5 ms, one per lane (bursts included), aligned with the audio.

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/FloaterLayout.hpp"
#include "floater/Case.h"
#include "floater/Engine.h"

#include <cstring>

namespace L = semi::layout::floater;
using semi::Screen;

namespace {

constexpr int kBlock = 64;               // engine block: 64 samples of latency
constexpr float kVolts = 5.f;
constexpr int kPeakCols = 96;
constexpr int kTracePts = 24;

struct Snapshot {
	// lanes
	int selected = 0;
	int steps[flt::kLanes]{}, step[flt::kLanes]{}, div[flt::kLanes]{}, model[flt::kVoices]{};
	uint32_t live[flt::kLanes]{}, base[flt::kLanes]{}, conds[flt::kLanes]{};
	bool on[flt::kLanes]{}, draw[flt::kLanes]{};
	uint8_t flash[flt::kLanes]{};
	int scatModel = 0;
	// clock
	bool running = false, clocked = false;
	float bpm = 150.f;
	double ppq = 0.0;
	int rotation = 0, turnTier = -1, turnIn = 0;
	char preset[48] = "";
	bool modified = false;
	// reel
	float peaks[kPeakCols]{};
	float genePos[flt::Reel::kGenes]{};
	int splices = 0, genes = 0, broken = 0;
	bool reelValid = false, held = false, breaking = false;
	float reelChaos = 0.f;
	// pins
	int8_t pins[flt::kSources][flt::kDests]{};
	float trace[flt::kSources][kTracePts]{};
	float dest[flt::kDests]{};
};

const char* kLaneShort[flt::kLanes] = {"KICK", "MESH", "FM", "OPTO", "HATS", "ZAP", "STRK", "SCAT"};

// The bound knobs: which Case parameter each panel knob edits (lane / slot knobs follow the selection).
enum Bind { B_STEPS, B_HITS, B_OFFSET, B_DIV, B_CHANCE, B_BURST, B_LOCK, B_VARY,
            B_TUNE, B_DECAY, B_COLOR, B_HARM, B_MORPH, B_LEVEL, B_SEND,
            B_BPM, B_ROTATE, B_DLY, B_SPACE, B_CHAOS, B_VARI, B_SWING, B_TURNS, B_COUNT };

std::string bindId(int b, int lane) {
	using namespace flt;
	const bool scat = lane == scatter;
	switch (b) {
		case B_STEPS: return ids::steps(lane);
		case B_HITS: return ids::hits(lane);
		case B_OFFSET: return ids::offs(lane);
		case B_DIV: return ids::div(lane);
		case B_CHANCE: return ids::chance(lane);
		case B_BURST: return ids::burst(lane);
		case B_LOCK: return ids::lock(lane);
		case B_VARY: return ids::vary(lane);
		// on lane 8 the module knobs play SCATTER
		case B_TUNE: return scat ? ids::spitch : ids::tune(lane);
		case B_DECAY: return scat ? ids::ssize : ids::decay(lane);
		case B_COLOR: return scat ? ids::stex : ids::color(lane);
		case B_HARM: return scat ? ids::spos : ids::harm(lane);
		case B_MORPH: return scat ? ids::sdens : ids::morph(lane);
		case B_LEVEL: return scat ? ids::smix : ids::lvl(lane);
		case B_SEND: return scat ? ids::sfeed : ids::send(lane);
		case B_BPM: return ids::bpm;
		case B_ROTATE: return ids::rotate;
		case B_DLY: return ids::dmix;
		case B_SPACE: return ids::vmix;
		case B_CHAOS: return ids::rchaos;
		case B_VARI: return ids::rvs;
		case B_SWING: return ids::swing;
		case B_TURNS: return ids::turns;
	}
	return "";
}

// What a module knob does on the model in the slot (the plug-in renames its knobs the same way).
std::string bindLabel(int b, int lane, int model) {
	using namespace flt;
	if (lane == scatter) {
		static const char* n[] = {"SCATTER PITCH", "SCATTER SIZE", "SCATTER TEXTURE", "SCATTER POSITION", "SCATTER DENSITY", "SCATTER MIX", "SCATTER FEEDBACK"};
		return n[b - B_TUNE];
	}
	const std::string slot = std::string(laneNames()[(size_t) lane]) + " ";
	if (b == B_TUNE) return slot + "TUNE";
	if (b == B_LEVEL) return slot + "LEVEL";
	if (b == B_SEND) return slot + "SEND";
	if (isPlaits(model)) {
		const auto& m = plaitsModels()[(size_t) (model - kFirstPlaits)];
		if (b == B_DECAY) return slot + "DECAY (LPG)";
		if (b == B_COLOR) return slot + m.timbre;
		if (b == B_HARM) return slot + m.harm;
		return slot + m.morph;
	}
	if (isBraids(model)) {
		if (b == B_DECAY) return slot + "DECAY";
		if (b == B_COLOR) return slot + "TIMBRE";
		if (b == B_HARM) return slot + "COLOR";
		return slot + "FILTER";
	}
	if (isReel(model)) {
		if (b == B_DECAY) return slot + "LOOP DECAY";
		if (b == B_COLOR) return slot + "WHERE";
		if (b == B_HARM) return slot + "SLICE";
		return slot + "FILTER";
	}
	const auto& n = nativeLabels()[(size_t) lane];
	if (b == B_DECAY) return slot + "DECAY";
	if (b == B_COLOR) return slot + n.color;
	if (b == B_HARM) return slot + n.harm;
	return slot + n.morph;
}

} // namespace

// ---------------------------------------------------------------------------- knob labels that follow the model

struct FloaterQuantity : ParamQuantity {
	int bind = 0;
	std::string getLabel() override;
};

struct Floater : Module {
	enum ParamId {
		ENUMS(SEL_PARAM, 8),
		STEPS_PARAM, HITS_PARAM, OFFSET_PARAM, DIV_PARAM, CHANCE_PARAM, BURST_PARAM, LOCK_PARAM, VARY_PARAM,
		TUNE_PARAM, DECAY_PARAM, COLOR_PARAM, HARM_PARAM, MORPH_PARAM, LEVEL_PARAM, SEND_PARAM,
		BPM_PARAM, ROTATE_PARAM, DLY_PARAM, SPACE_PARAM, CHAOS_PARAM, VARI_PARAM, SWING_PARAM, TURNS_PARAM,
		STRIKE_PARAM, NEW_PARAM, MUTATE_PARAM, RUN_PARAM,
		PARAMS_LEN
	};
	enum InputId { CLK_INPUT, RST_INPUT, RUN_INPUT, STRIKE_INPUT, NEW_INPUT, MUTATE_INPUT, CHAOS_INPUT, ROTATE_INPUT, INPUTS_LEN };
	enum OutputId { OUT_L_OUTPUT, OUT_R_OUTPUT, ENUMS(GATE_OUTPUT, 8), CV_OUTPUT, OUTPUTS_LEN };
	enum LightId { ENUMS(HIT_LIGHT, 8), RUN_LIGHT, LIGHTS_LEN };

	flt::Case cs;
	flt::Engine engine;

	// ---- panel <-> Case
	int selected = 0;
	int bindIndex[B_COUNT]{};
	float knobLast[B_COUNT]{};
	uint32_t seenSerial = 0;
	bool pullAll = true;

	// ---- requests from the menu / buttons (run at the start of a block, on the audio thread)
	std::atomic<int> reqFactory{-1};
	std::atomic<bool> reqNew{false}, reqMutate{false};
	dsp::BooleanTrigger selTrig[8], strikeBtn, newBtn, mutBtn, runBtn;
	dsp::SchmittTrigger strikeIn[8], newIn, mutIn, rstIn, clkIn;

	// ---- transport
	bool running = true;
	int clockMode = 0;          // CLK: 0 = 1/16, 1 = 1/8, 2 = 1/4, 3 = 24 PPQN
	double ownPpq = 0.0;
	int64_t clkCount = -1;
	float sinceClk = 0.f, period = 0.f;
	bool clkSeen = false, wasClocked = false;
	double lastPpq = 0.0;
	float lastBpm = 150.f;

	// ---- block adapter
	float outBuf[2][kBlock]{};
	int pos = 0;
	struct Pending { int lane, offset; };
	Pending hits[256];
	int numHits = 0;
	int gateLeft[8]{};
	int flashFrames[8]{};
	int cvSource = flt::seq;

	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;
	uint32_t uiFired[flt::kLanes]{};
	float sampleRate = 44100.f;

	Floater() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int i = 0; i < 8; i++)
			configButton(SEL_PARAM + i, std::string("Select lane ") + kLaneShort[i]);
		configParam(STEPS_PARAM, 1.f, 32.f, 16.f, "Lane steps")->snapEnabled = true;
		configParam(HITS_PARAM, 0.f, 32.f, 4.f, "Lane hits")->snapEnabled = true;
		configParam(OFFSET_PARAM, 0.f, 31.f, 0.f, "Lane offset")->snapEnabled = true;
		configParam(DIV_PARAM, 1.f, 8.f, 1.f, "Lane division", "", 0.f, 1.f)->snapEnabled = true;
		configParam(CHANCE_PARAM, 0.f, 1.f, 1.f, "Lane chance", " %", 0.f, 100.f);
		configParam(BURST_PARAM, 0.f, 1.f, 0.f, "Lane burst", " %", 0.f, 100.f);
		configParam(LOCK_PARAM, 0.f, 1.f, 1.f, "Lane lock (Turing: 100% loops as set)", " %", 0.f, 100.f);
		configParam(VARY_PARAM, 0.f, 1.f, 0.f, "Lane vary", " %", 0.f, 100.f);
		const char* mk[7] = {"Tune", "Decay", "Color", "Harm", "Morph", "Level", "Send"};
		const float md[7] = {0.5f, 0.4f, 0.5f, 0.5f, 0.5f, 0.8f, 0.2f};
		for (int k = 0; k < 7; k++)
			configParam<FloaterQuantity>(TUNE_PARAM + k, 0.f, 1.f, md[k], mk[k], " %", 0.f, 100.f)->bind = B_TUNE + k;
		configParam(BPM_PARAM, 40.f, 220.f, 150.f, "Tempo (without CLK)", " BPM");
		configParam(ROTATE_PARAM, 0.f, 7.f, 0.f, "Rotate (the divider)")->snapEnabled = true;
		configParam(DLY_PARAM, 0.f, 1.f, 0.3f, "ZIGZAG mix", " %", 0.f, 100.f);
		configParam(SPACE_PARAM, 0.f, 1.f, 0.25f, "SPACE mix", " %", 0.f, 100.f);
		configParam(CHAOS_PARAM, 0.f, 1.f, 0.f, "REEL order <> chaos", " %", 0.f, 100.f);
		configParam(VARI_PARAM, 0.f, 1.f, 0.75f, "REEL varispeed (75% = as recorded)", " %", 0.f, 100.f);
		configParam(SWING_PARAM, 0.f, 0.5f, 0.f, "Swing", " %", 0.f, 200.f);
		configParam(TURNS_PARAM, 0.f, 1.f, 0.f, "Turns (fills before 4/8/16/32)", " %", 0.f, 100.f);
		configButton(STRIKE_PARAM, "Strike the selected lane");
		configButton(NEW_PARAM, "NEW PATCH");
		configButton(MUTATE_PARAM, "MUTATE");
		configButton(RUN_PARAM, "Run (own clock)");
		configInput(CLK_INPUT, "Clock (1/16 by default, see menu)");
		configInput(RST_INPUT, "Reset (next clock = bar 1)");
		configInput(RUN_INPUT, "Run gate (high = running)");
		configInput(STRIKE_INPUT, "Strike: mono = the selected lane; poly = channel n strikes lane n");
		configInput(NEW_INPUT, "NEW PATCH trigger");
		configInput(MUTATE_INPUT, "MUTATE trigger");
		configInput(CHAOS_INPUT, "REEL chaos CV (+10% per V)");
		configInput(ROTATE_INPUT, "Rotate CV (one step per V)");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		for (int i = 0; i < 8; i++)
			configOutput(GATE_OUTPUT + i, std::string("Gate ") + kLaneShort[i] + " (every hit, bursts included)");
		configOutput(CV_OUTPUT, "CV (a matrix source, +-5 V; menu)");
		for (int i = 0; i < 8; i++)
			configLight(HIT_LIGHT + i, std::string(kLaneShort[i]) + " hit");
		cs.seedRandom((uint32_t) random::u32());
		engine.prepare(44100.0, kBlock);
		rebind();
	}

	// ---- binding ---------------------------------------------------------------------

	static int paramFor(int b) {
		static const int map[B_COUNT] = {STEPS_PARAM, HITS_PARAM, OFFSET_PARAM, DIV_PARAM, CHANCE_PARAM, BURST_PARAM, LOCK_PARAM, VARY_PARAM,
		                                 TUNE_PARAM, DECAY_PARAM, COLOR_PARAM, HARM_PARAM, MORPH_PARAM, LEVEL_PARAM, SEND_PARAM,
		                                 BPM_PARAM, ROTATE_PARAM, DLY_PARAM, SPACE_PARAM, CHAOS_PARAM, VARI_PARAM, SWING_PARAM, TURNS_PARAM};
		return map[b];
	}

	// look the Case indices up for the selection (not on every block: string work)
	void rebind() {
		for (int b = 0; b < B_COUNT; b++)
			bindIndex[b] = flt::Case::index(bindId(b, selected));
		pullAll = true;
	}

	// Case -> knobs (after a patch change or a new selection)
	void pull() {
		for (int b = 0; b < B_COUNT; b++) {
			const float v = cs.get(bindIndex[b]);
			params[paramFor(b)].setValue(v);
			knobLast[b] = v;
		}
		pullAll = false;
		seenSerial = cs.serial.load();
	}

	// knobs -> Case (only what moved)
	void push() {
		for (int b = 0; b < B_COUNT; b++) {
			const float v = params[paramFor(b)].getValue();
			if (v != knobLast[b]) {
				cs.set(bindIndex[b], v);
				knobLast[b] = v;
			}
		}
	}

	void select(int lane) {
		selected = clamp(lane, 0, 7);
		rebind();
	}

	// ---- Rack -------------------------------------------------------------------------

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		sampleRate = e.sampleRate;
		engine.prepare(e.sampleRate, kBlock);
		pos = 0;
		numHits = 0;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		reqFactory = 0;
		running = true;
		select(0);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_t* vals = json_object();
		for (const auto& kv : cs.snapshotValues())
			json_object_set_new(vals, kv.first.c_str(), json_real(kv.second));
		json_object_set_new(root, "case", vals);
		json_t* lk = json_array();
		for (int i = 0; i < flt::Case::numParams(); i++)
			if (cs.isLocked(i))
				json_array_append_new(lk, json_string(flt::Case::defs()[(size_t) i].id.c_str()));
		json_object_set_new(root, "locks", lk);
		json_object_set_new(root, "preset", json_string(cs.presetName));
		json_object_set_new(root, "modified", json_boolean(cs.modified));
		json_object_set_new(root, "factory", json_integer(cs.factoryIndex));
		json_object_set_new(root, "selected", json_integer(selected));
		json_object_set_new(root, "running", json_boolean(running));
		json_object_set_new(root, "clockMode", json_integer(clockMode));
		json_object_set_new(root, "cvSource", json_integer(cvSource));
		return root;
	}

	void dataFromJson(json_t* root) override {
		if (json_t* vals = json_object_get(root, "case")) {
			std::map<std::string, float> m;
			const char* key;
			json_t* v;
			json_object_foreach(vals, key, v) m[key] = (float) json_number_value(v);
			cs.applyValues(m);
			auto it = m.find(flt::ids::bpm);
			if (it != m.end())
				cs.set(flt::ids::bpm, it->second);
		}
		if (json_t* lk = json_object_get(root, "locks")) {
			for (int i = 0; i < flt::Case::numParams(); i++)
				cs.setLocked(i, false);
			size_t i;
			json_t* v;
			json_array_foreach(lk, i, v) cs.setLocked(flt::Case::index(json_string_value(v)), true);
		}
		if (json_t* p = json_object_get(root, "preset"))
			cs.setName(json_string_value(p));
		if (json_t* p = json_object_get(root, "modified"))
			cs.modified = json_boolean_value(p);
		if (json_t* p = json_object_get(root, "factory"))
			cs.factoryIndex = (int) json_integer_value(p);
		if (json_t* p = json_object_get(root, "running"))
			running = json_boolean_value(p);
		if (json_t* p = json_object_get(root, "clockMode"))
			clockMode = clamp((int) json_integer_value(p), 0, 3);
		if (json_t* p = json_object_get(root, "cvSource"))
			cvSource = clamp((int) json_integer_value(p), 0, flt::kSources - 1);
		reqFactory = -1;
		json_t* sel = json_object_get(root, "selected");
		select(sel ? (int) json_integer_value(sel) : 0);
		pull();
	}

	// ---- position ---------------------------------------------------------------------

	// returns true while running; ppq / bpm of this block's first sample
	bool position(float dt, double& ppq, double& bpm) {
		const bool runGate = inputs[RUN_INPUT].isConnected() ? inputs[RUN_INPUT].getVoltage() > 1.f : running;
		static const double ppqPerPulse[4] = {0.25, 0.5, 1.0, 1.0 / 24.0};
		const double pp = ppqPerPulse[clockMode];
		const bool rst = rstIn.process(inputs[RST_INPUT].getVoltage(), 0.1f, 1.f);
		if (inputs[CLK_INPUT].isConnected()) {
			sinceClk += dt;
			if (rst)
				clkCount = -1;
			if (clkIn.process(inputs[CLK_INPUT].getVoltage(), 0.1f, 1.f)) {
				if (clkSeen)
					period = sinceClk;
				clkSeen = true;
				clkCount++;
				sinceClk = 0.f;
			}
			if (period > 0.f && sinceClk > 2.5f * period * (clockMode == 3 ? 6.f : 1.f)) {
				clkSeen = false;
				period = 0.f;
			}
			wasClocked = true;
			if (clkCount < 0 || period <= 0.f || !runGate)
				return false;
			bpm = 60.0 * pp / period;
			ppq = (clkCount + std::min(sinceClk / period, 0.999f)) * pp;
			return true;
		}
		if (wasClocked) {
			wasClocked = false;
			clkSeen = false;
			period = 0.f;
			clkCount = -1;
		}
		if (rst)
			ownPpq = 0.0;
		bpm = cs.get(bindIndex[B_BPM]);
		ppq = ownPpq;
		if (!runGate)
			return false;
		return true;
	}

	// ---- the block ----------------------------------------------------------------------

	void runBlock(const ProcessArgs& args) {
		// requests: factory patch, NEW PATCH, MUTATE (allocation-free, see Case.h)
		const int f = reqFactory.exchange(-1);
		if (f >= 0)
			cs.loadFactory(f);
		if (reqNew.exchange(false))
			cs.newPatch();
		if (reqMutate.exchange(false))
			cs.mutate(0.25f);
		if (pullAll || cs.serial.load() != seenSerial)
			pull();
		push();

		double ppq = 0.0, bpm = 120.0;
		const bool playing = position(args.sampleTime * kBlock, ppq, bpm);
		flt::Params p = cs.currentParams();
		p.run = false;   // the module owns the transport
		p.reelChaos = clamp(p.reelChaos + inputs[CHAOS_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		if (inputs[ROTATE_INPUT].isConnected())
			p.rotate = ((p.rotate + (int) std::floor(inputs[ROTATE_INPUT].getVoltage())) % 8 + 8) % 8;
		engine.setParams(p);

		flt::Transport t;
		t.playing = playing;
		t.hostHasPosition = playing;
		t.bpm = std::min(400.0, std::max(20.0, bpm));
		t.ppq = ppq;
		lastPpq = ppq;
		lastBpm = (float) t.bpm;
		engine.process(outBuf[0], outBuf[1], kBlock, t);
		if (playing && !inputs[CLK_INPUT].isConnected())
			ownPpq += kBlock * t.bpm / 60.0 / args.sampleRate;

		numHits = 0;
		for (int k = 0; k < engine.numFiredHits() && numHits < 256; k++) {
			const auto& h = engine.firedHits()[k];
			hits[numHits++] = {h.lane, clamp(h.offset, 0, kBlock - 1)};
		}
	}

	void process(const ProcessArgs& args) override {
		// buttons
		for (int i = 0; i < 8; i++)
			if (selTrig[i].process(params[SEL_PARAM + i].getValue() > 0.f))
				select(i);
		if (runBtn.process(params[RUN_PARAM].getValue() > 0.f))
			running = !running;
		if (newBtn.process(params[NEW_PARAM].getValue() > 0.f) | newIn.process(inputs[NEW_INPUT].getVoltage(), 0.1f, 1.f))
			reqNew = true;
		if (mutBtn.process(params[MUTATE_PARAM].getValue() > 0.f) | mutIn.process(inputs[MUTATE_INPUT].getVoltage(), 0.1f, 1.f))
			reqMutate = true;
		// strikes, sample-accurate within the next block
		if (strikeBtn.process(params[STRIKE_PARAM].getValue() > 0.f))
			engine.hit(selected, 0.95f, pos);
		const int sc = inputs[STRIKE_INPUT].getChannels();
		for (int c = 0; c < std::min(sc, 8); c++)
			if (strikeIn[c].process(inputs[STRIKE_INPUT].getVoltage(c), 0.1f, 1.f))
				engine.hit(sc == 1 ? selected : c, 0.95f, pos);

		// the block adapter: what goes out is the block before
		outputs[OUT_L_OUTPUT].setVoltage(outBuf[0][pos] * kVolts);
		outputs[OUT_R_OUTPUT].setVoltage(outBuf[1][pos] * kVolts);
		const int gateLen = (int) (0.005f * args.sampleRate);
		for (int k = 0; k < numHits; k++)
			if (hits[k].offset == pos) {
				gateLeft[hits[k].lane] = gateLen;
				flashFrames[hits[k].lane] = 3;
			}
		for (int i = 0; i < 8; i++) {
			outputs[GATE_OUTPUT + i].setVoltage(gateLeft[i] > 0 ? 10.f : 0.f);
			if (gateLeft[i] > 0)
				gateLeft[i]--;
		}
		if (++pos >= kBlock) {
			pos = 0;
			runBlock(args);
		}
		outputs[CV_OUTPUT].setVoltage(clamp(engine.sourceValue(cvSource), -1.f, 1.f) * 5.f);

		if (ui.tick(args.sampleTime))
			publish();
	}

	void publish() {
		const auto& D = engine.display;
		Snapshot& s = snap.writeSlot();
		s.selected = selected;
		const flt::Params& P = engine.getParams();
		for (int i = 0; i < flt::kLanes; i++) {
			const auto& l = P.lanes[(size_t) i];
			s.steps[i] = clamp(l.draw ? l.steps : l.steps, 1, 32);
			s.step[i] = D.step[(size_t) i].load(std::memory_order_relaxed);
			s.div[i] = D.div[(size_t) i].load(std::memory_order_relaxed);
			s.live[i] = D.live[(size_t) i].load(std::memory_order_relaxed);
			uint32_t base = 0, cm = 0;
			for (int st = 0; st < s.steps[i]; st++) {
				if (flt::laneHits(l, s.steps[i], st))
					base |= 1u << st;
				if (l.cond[(size_t) st] != 0)
					cm |= 1u << st;
			}
			s.base[i] = base;
			s.conds[i] = cm;
			s.on[i] = l.on;
			s.draw[i] = l.draw;
			const uint32_t fired = (uint32_t) D.fired[(size_t) i].load(std::memory_order_relaxed);
			if (fired != uiFired[i]) {
				uiFired[i] = fired;
				flashFrames[i] = std::max(flashFrames[i], 3);
			}
			s.flash[i] = (uint8_t) std::max(0, flashFrames[i]);
			if (flashFrames[i] > 0)
				flashFrames[i]--;
			lights[HIT_LIGHT + i].setBrightness(s.flash[i] > 0 ? 1.f : (i == selected ? 0.25f : 0.f));
		}
		for (int v = 0; v < flt::kVoices; v++)
			s.model[v] = P.voices[(size_t) v].model;
		s.scatModel = P.scatModel;
		s.running = D.running.load(std::memory_order_relaxed);
		s.clocked = inputs[CLK_INPUT].isConnected();
		s.bpm = lastBpm;
		s.ppq = lastPpq;
		s.rotation = D.rotation.load(std::memory_order_relaxed);
		s.turnTier = D.turnTier.load(std::memory_order_relaxed);
		s.turnIn = D.turnIn.load(std::memory_order_relaxed);
		std::memcpy(s.preset, cs.presetName, sizeof s.preset);
		s.modified = cs.modified;
		lights[RUN_LIGHT].setBrightness(s.running ? 1.f : 0.f);

		// the reel: the splice window if there is one, else the last seconds of tape
		const auto& R = engine.reelDisplay();
		const int cap = std::max(1, R.peakCap.load(std::memory_order_relaxed));
		const int64_t rec = R.rec.load(std::memory_order_relaxed);
		s.reelValid = R.valid.load(std::memory_order_relaxed);
		s.splices = R.splices.load(std::memory_order_relaxed);
		s.held = R.held.load(std::memory_order_relaxed);
		s.breaking = R.breaking.load(std::memory_order_relaxed);
		s.genes = R.genes.load(std::memory_order_relaxed);
		s.broken = R.broken.load(std::memory_order_relaxed);
		s.reelChaos = P.reelChaos;
		int64_t c0, c1;
		if (s.reelValid) {
			c0 = R.winStart.load(std::memory_order_relaxed) / flt::Reel::kPeak;
			c1 = c0 + (int64_t) s.splices * R.spliceLen.load(std::memory_order_relaxed) / flt::Reel::kPeak;
		}
		else {
			c1 = rec / flt::Reel::kPeak;
			c0 = c1 - std::min<int64_t>(cap - 1, (int64_t) (4.0 * sampleRate) / flt::Reel::kPeak);
		}
		const int64_t span = std::max<int64_t>(1, c1 - c0);
		for (int k = 0; k < kPeakCols; k++) {
			float pk = 0.f;
			const int64_t a = c0 + span * k / kPeakCols, b = std::max(a + 1, c0 + span * (k + 1) / kPeakCols);
			for (int64_t c = a; c < b; c++)
				if (c >= 0)
					pk = std::max(pk, R.peaks[(size_t) (c % cap)]);
			s.peaks[k] = pk;
		}
		for (int g = 0; g < flt::Reel::kGenes; g++)
			s.genePos[g] = R.voicePos[(size_t) g].load(std::memory_order_relaxed);

		// the pin matrix and the sources' last seconds
		for (int so = 0; so < flt::kSources; so++) {
			for (int d = 0; d < flt::kDests; d++)
				s.pins[so][d] = (int8_t) P.pins[(size_t) so][(size_t) d];
			const int w = D.traceWrite.load(std::memory_order_acquire);
			for (int k = 0; k < kTracePts; k++) {
				const int idx = (w - (kTracePts - k) * 8 + flt::Display::kTrace * 4) % flt::Display::kTrace;
				s.trace[so][k] = D.trace[(size_t) so][(size_t) idx];
			}
		}
		for (int d = 0; d < flt::kDests; d++)
			s.dest[d] = D.dest[(size_t) d].load(std::memory_order_relaxed);
		snap.publish();
	}
};

std::string FloaterQuantity::getLabel() {
	auto* m = dynamic_cast<Floater*>(module);
	if (!m || bind < B_TUNE || bind > B_SEND)
		return ParamQuantity::getLabel();
	const int lane = m->selected;
	const int model = lane < flt::kVoices ? (int) m->cs.get(flt::ids::model(lane)) : 0;
	return bindLabel(bind, lane, model);
}

// ---------------------------------------------------------------------------- screens

// The eight lanes as the plug-in's clock chain: steps (filled = hits as LOCK has them now, magenta frame =
// a step LOCK flipped, small mark = a condition), the playhead, the hit flash, division and model.
struct LanesScreen : Screen {
	Floater* module = nullptr;
	Snapshot demo;
	LanesScreen() {
		const int st[8] = {16, 16, 16, 12, 16, 16, 16, 16}, hi[8] = {3, 2, 5, 5, 11, 3, 3, 2}, of[8] = {0, 4, 2, 0, 0, 3, 0, 6};
		for (int i = 0; i < 8; i++) {
			flt::LaneParams l;
			l.hits = hi[i];
			l.offset = of[i];
			demo.steps[i] = st[i];
			for (int k = 0; k < st[i]; k++)
				if (flt::laneHits(l, st[i], k))
					demo.base[i] |= 1u << k;
			demo.live[i] = demo.base[i];
			demo.step[i] = (5 + i) % st[i];
			demo.div[i] = i == 6 ? 2 : (i == 7 ? 4 : 1);
			demo.on[i] = true;
		}
		demo.live[2] ^= 1u << 9;
		demo.conds[4] = 1u << 6;
		demo.flash[0] = 2;
	}
	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		const float rowH = h / 8.f;
		const float nameW = 26.f, infoW = 58.f;
		const float gx = nameW + 2.f, gw = w - nameW - infoW - 6.f;
		const float cw = gw / 32.f;
		for (int i = 0; i < 8; i++) {
			const float y = rowH * i;
			const bool sel = i == s.selected;
			if (sel)
				fillRect(vg, 0.f, y + 0.5f, w, rowH - 1.f, semi::theme::cyanDeep());
			const NVGcolor nameCol = !s.on[i] ? semi::theme::dim() : (s.flash[i] ? semi::theme::amber() : (sel ? semi::theme::cyan() : semi::theme::text()));
			text(vg, 3.f, y + rowH * 0.5f, kLaneShort[i], 9.f, nameCol, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			const int n = clamp(s.steps[i], 1, 32);
			const float ch = rowH - 6.f;
			for (int k = 0; k < n; k++) {
				const float x = gx + cw * k;
				const bool hit = (s.live[i] >> k) & 1u;
				const bool flipped = ((s.live[i] ^ s.base[i]) >> k) & 1u;
				NVGcolor c = hit ? (s.on[i] ? semi::theme::cyan() : semi::theme::cyanDim()) : semi::theme::cell();
				if (hit && k == s.step[i] && s.on[i])
					c = semi::theme::amber();
				fillRect(vg, x + 0.6f, y + 3.f, cw - 1.2f, ch, c);
				if (flipped)
					fillRect(vg, x + 0.6f, y + 3.f + ch - 2.f, cw - 1.2f, 2.f, semi::theme::magenta());
				if ((s.conds[i] >> k) & 1u)
					fillRect(vg, x + cw * 0.5f - 1.f, y + 1.f, 2.f, 2.f, semi::theme::magenta());
			}
			// the playhead
			if (s.on[i] && s.step[i] >= 0 && s.step[i] < n)
				line(vg, gx + cw * s.step[i], y + 2.f, gx + cw * (s.step[i] + 1), y + 2.f, semi::theme::amber());
			char buf[40];
			std::snprintf(buf, sizeof buf, "/%d %s", std::max(1, s.div[i]), modelShort(s, i).c_str());
			text(vg, w - infoW, y + rowH * 0.5f, buf, 8.f, sel ? semi::theme::cyan() : semi::theme::dim(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		}
	}
	static std::string modelShort(const Snapshot& s, int lane) {
		if (lane == flt::scatter) {
			static const char* n[] = {"GRAINS", "CLD GRAN", "CLD STR", "CLD LOOP", "CLD SPEC", "OLIVERB", "RESON", "BDS HIFI", "BDS CLN", "BDS LOFI", "BDS DRTY"};
			return n[clamp(s.scatModel, 0, 10)];
		}
		const int m = s.model[lane];
		if (flt::isPlaits(m))
			return std::string("P ") + flt::plaitsModels()[(size_t) (m - flt::kFirstPlaits)].name;
		if (flt::isBraids(m))
			return std::string("B ") + flt::braidsModels()[(size_t) (m - flt::kFirstBraids)].name;
		if (flt::isReel(m))
			return "REEL";
		return flt::nativeLabels()[(size_t) lane].model;
	}
};

// The REEL: the splice window (one splice a bar) with the genes reading it, or the tape rolling in.
struct ReelScreen : Screen {
	Floater* module = nullptr;
	Snapshot demo;
	ReelScreen() {
		for (int k = 0; k < kPeakCols; k++)
			demo.peaks[k] = 0.25f + 0.6f * std::fabs(std::sin(k * 0.37f)) * ((k % 12) < 3 ? 1.f : 0.5f);
		demo.reelValid = true;
		demo.splices = 2;
		demo.genePos[0] = 0.3f;
		demo.genePos[1] = 0.71f;
		for (int g = 2; g < flt::Reel::kGenes; g++)
			demo.genePos[g] = -1.f;
		demo.bpm = 150.f;
		demo.ppq = 45.5;
		std::snprintf(demo.preset, sizeof demo.preset, "Floor Patch");
		demo.running = true;
		demo.reelChaos = 0.4f;
	}
	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		char buf[64];
		const int bar = (int) std::floor(s.ppq / 4.0) + 1;
		const int beat = (int) std::floor(std::fmod(s.ppq, 4.0)) + 1;
		std::snprintf(buf, sizeof buf, "%s %.1f  %d.%d", s.clocked ? "CLK" : (s.running ? "RUN" : "STOP"), s.bpm, bar, beat);
		text(vg, 3.f, 2.f, buf, 9.f, s.running ? semi::theme::text() : semi::theme::dim());
		std::snprintf(buf, sizeof buf, "%s%s", s.preset, s.modified ? "*" : "");
		text(vg, w - 3.f, 2.f, buf, 9.f, semi::theme::cyan(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		const float y0 = 14.f, y1 = h - 12.f, mid = 0.5f * (y0 + y1), half = 0.5f * (y1 - y0);
		const float cw = (w - 6.f) / kPeakCols;
		for (int k = 0; k < kPeakCols; k++) {
			const float a = std::min(1.f, s.peaks[k]) * half;
			if (a > 0.3f)
				fillRect(vg, 3.f + cw * k, mid - a, std::max(1.f, cw - 0.4f), 2.f * a, s.breaking ? semi::theme::magenta() : semi::theme::cyanDim());
		}
		if (s.reelValid && s.splices > 0) {
			for (int k = 1; k < s.splices; k++) {
				const float x = 3.f + (w - 6.f) * k / s.splices;
				line(vg, x, y0, x, y1, semi::theme::text());
			}
			for (int g = 0; g < flt::Reel::kGenes; g++)
				if (s.genePos[g] >= 0.f) {
					const float x = 3.f + (w - 6.f) * clamp(s.genePos[g], 0.f, 1.f);
					line(vg, x, y0, x, y1, semi::theme::amber(), semi::platform::kStroke);
				}
		}
		std::snprintf(buf, sizeof buf, "%s%d SPL  CHAOS %d%%", s.held ? "HOLD " : "", s.reelValid ? s.splices : 0, (int) std::round(s.reelChaos * 100.f));
		text(vg, 3.f, h - 2.f, buf, 8.f, semi::theme::dim(), NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
		std::snprintf(buf, sizeof buf, "%d/%d BROKE", s.broken, s.genes);
		text(vg, w - 3.f, h - 2.f, buf, 8.f, s.breaking ? semi::theme::magenta() : semi::theme::dim(), NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
	}
};

// The pin matrix: ten CV sources (each row draws its last seconds) by fourteen jacks.
struct PinsScreen : Screen {
	Floater* module = nullptr;
	Snapshot demo;
	PinsScreen() {
		for (int so = 0; so < flt::kSources; so++)
			for (int k = 0; k < kTracePts; k++)
				demo.trace[so][k] = 0.6f * std::sin(k * (0.2f + 0.07f * so) + so);
		demo.pins[flt::pong][flt::fmIndex] = 1;
		demo.pins[flt::stepped][flt::strNote] = 1;
		demo.pins[flt::forge][flt::kPitch] = 1;
		demo.pins[flt::chaos][flt::zapSweep] = -1;
	}
	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		const float labelW = 22.f, traceW = 26.f;
		const float gx = labelW + traceW + 4.f, gw = w - gx - 2.f;
		const float rowH = (h - 2.f) / flt::kSources;
		const float cw = gw / flt::kDests;
		for (int so = 0; so < flt::kSources; so++) {
			const float y = 1.f + rowH * so, cy = y + rowH * 0.5f;
			bool used = false;
			for (int d = 0; d < flt::kDests; d++)
				used |= s.pins[so][d] != 0;
			static const char* sn[] = {"SMTH", "STEP", "PONG", "LFO", "FRGE", "CHAO", "WOGL", "SEQ", "DRON", "ARC"};
			text(vg, 2.f, cy, sn[so], 7.f, used ? semi::theme::text() : semi::theme::dim(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			float ys[kTracePts];
			for (int k = 0; k < kTracePts; k++)
				ys[k] = cy - clamp(s.trace[so][k], -1.f, 1.f) * (rowH * 0.45f);
			polyline(vg, ys, kTracePts, labelW, traceW, used ? semi::theme::cyan() : semi::theme::cyanDim(), semi::platform::kThinStroke);
			for (int d = 0; d < flt::kDests; d++) {
				const float x = gx + cw * (d + 0.5f);
				const int p = s.pins[so][d];
				nvgBeginPath(vg);
				nvgCircle(vg, x, cy, std::min(cw, rowH) * (p ? 0.38f : 0.18f));
				nvgFillColor(vg, p > 0 ? semi::theme::amber() : (p < 0 ? semi::theme::magenta() : semi::theme::grid()));
				nvgFill(vg);
			}
		}
	}
};

// ---------------------------------------------------------------------------- menus

namespace {

#ifndef METAMODULE
struct CaseQuantity : Quantity {
	Floater* m;
	int idx;
	float mult;
	std::string unit;
	CaseQuantity(Floater* m, int idx, float mult, std::string unit) : m(m), idx(idx), mult(mult), unit(std::move(unit)) {}
	const flt::ParamDef& def() { return flt::Case::defs()[(size_t) idx]; }
	void setValue(float x) override { m->cs.set(idx, x); m->cs.touch(); }
	float getValue() override { return m->cs.get(idx); }
	float getMinValue() override { return def().min; }
	float getMaxValue() override { return def().max; }
	float getDefaultValue() override { return def().def; }
	float getDisplayValue() override { return getValue() * mult; }
	void setDisplayValue(float d) override { setValue(d / mult); }
	std::string getLabel() override { return def().name; }
	std::string getUnit() override { return unit; }
};
struct CaseSlider : ui::Slider {
	explicit CaseSlider(Quantity* q) {
		quantity = q;
		box.size.x = 220.f;
	}
	~CaseSlider() override { delete quantity; }
};
#endif

// One Case parameter in the menu: a list for choices and small integers, a toggle for booleans,
// a slider (Rack) or 11 steps (MetaModule) for continuous values.
void caseItem(Menu* menu, Floater* m, const std::string& id, std::string label = "") {
	const int idx = flt::Case::index(id);
	if (idx < 0)
		return;
	const auto& d = flt::Case::defs()[(size_t) idx];
	if (label.empty())
		label = d.name;
	auto set = [=](float v) { m->cs.set(idx, v); m->cs.touch(); };
	switch (d.kind) {
		case flt::ParamDef::boolean:
			menu->addChild(createBoolMenuItem(label, "", [=]() { return m->cs.get(idx) > 0.5f; }, [=](bool b) { set(b ? 1.f : 0.f); }));
			return;
		case flt::ParamDef::choice:
			menu->addChild(createIndexSubmenuItem(label, d.choices, [=]() { return (size_t) m->cs.get(idx); }, [=](size_t i) { set((float) i); }));
			return;
		case flt::ParamDef::integer: {
			std::vector<std::string> labels;
			for (int v = (int) d.min; v <= (int) d.max; v++)
				labels.push_back(std::to_string(v));
			const int lo = (int) d.min;
			menu->addChild(createIndexSubmenuItem(label, labels, [=]() { return (size_t) ((int) m->cs.get(idx) - lo); }, [=](size_t i) { set((float) (lo + (int) i)); }));
			return;
		}
		case flt::ParamDef::real: {
			const bool unit01 = d.min == 0.f && d.max == 1.f;
#ifdef METAMODULE
			constexpr int kSteps = 11;
			std::vector<std::string> labels;
			for (int i = 0; i < kSteps; i++) {
				const float x = d.min + (d.max - d.min) * i / (kSteps - 1);
				labels.push_back(unit01 ? string::f("%d%%", (int) std::round(x * 100.f)) : string::f("%.2f", x));
			}
			menu->addChild(createIndexSubmenuItem(label, labels,
				[=]() { return (size_t) clamp((int) std::round((m->cs.get(idx) - d.min) / (d.max - d.min) * (kSteps - 1)), 0, kSteps - 1); },
				[=](size_t i) { set(d.min + (d.max - d.min) * i / (kSteps - 1)); }));
#else
			menu->addChild(new CaseSlider(new CaseQuantity(m, idx, unit01 ? 100.f : 1.f, unit01 ? "%" : "")));
#endif
			return;
		}
	}
}

} // namespace

struct FloaterWidget : ModuleWidget {
	FloaterWidget(Floater* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Floater.svg")));

		auto* lanes = semi::createScreenMm<LanesScreen>(Vec(L::LANES.x, L::LANES.y), Vec(L::LANES.w, L::LANES.h));
		lanes->module = module;
		addChild(lanes);
		auto* reel = semi::createScreenMm<ReelScreen>(Vec(L::REEL.x, L::REEL.y), Vec(L::REEL.w, L::REEL.h));
		reel->module = module;
		addChild(reel);
		auto* pins = semi::createScreenMm<PinsScreen>(Vec(L::PINS.x, L::PINS.y), Vec(L::PINS.w, L::PINS.h));
		pins->module = module;
		addChild(pins);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		const L::P sel[8] = {L::SEL1, L::SEL2, L::SEL3, L::SEL4, L::SEL5, L::SEL6, L::SEL7, L::SEL8};
		const L::P hit[8] = {L::HIT1, L::HIT2, L::HIT3, L::HIT4, L::HIT5, L::HIT6, L::HIT7, L::HIT8};
		for (int i = 0; i < 8; i++) {
			addParam(createParamCentered<semi::Button>(at(sel[i]), module, Floater::SEL_PARAM + i));
			addChild(createLightCentered<semi::CyanLedSmall>(at(hit[i]), module, Floater::HIT_LIGHT + i));
		}
		knob<semi::KnobM>(at(L::STEPS), Floater::STEPS_PARAM, B_STEPS);
		knob<semi::KnobM>(at(L::HITS), Floater::HITS_PARAM, B_HITS);
		knob<semi::KnobM>(at(L::OFFSET), Floater::OFFSET_PARAM, B_OFFSET);
		knob<semi::KnobM>(at(L::DIV), Floater::DIV_PARAM, B_DIV);
		knob<semi::KnobM>(at(L::CHANCE), Floater::CHANCE_PARAM, B_CHANCE);
		knob<semi::KnobMHot>(at(L::BURST), Floater::BURST_PARAM, B_BURST);
		knob<semi::KnobM>(at(L::LOCK), Floater::LOCK_PARAM, B_LOCK);
		knob<semi::KnobM>(at(L::VARY), Floater::VARY_PARAM, B_VARY);
		knob<semi::KnobM>(at(L::TUNE), Floater::TUNE_PARAM, B_TUNE);
		knob<semi::KnobM>(at(L::DECAY), Floater::DECAY_PARAM, B_DECAY);
		knob<semi::KnobM>(at(L::COLOR), Floater::COLOR_PARAM, B_COLOR);
		knob<semi::KnobM>(at(L::HARM), Floater::HARM_PARAM, B_HARM);
		knob<semi::KnobM>(at(L::MORPH), Floater::MORPH_PARAM, B_MORPH);
		knob<semi::KnobM>(at(L::LEVEL), Floater::LEVEL_PARAM, B_LEVEL);
		knob<semi::KnobM>(at(L::SEND), Floater::SEND_PARAM, B_SEND);
		knob<semi::KnobM>(at(L::BPM), Floater::BPM_PARAM, B_BPM);
		knob<semi::KnobM>(at(L::ROTATE), Floater::ROTATE_PARAM, B_ROTATE);
		knob<semi::KnobM>(at(L::DLY), Floater::DLY_PARAM, B_DLY);
		knob<semi::KnobM>(at(L::SPACE), Floater::SPACE_PARAM, B_SPACE);
		knob<semi::KnobLHot>(at(L::CHAOS), Floater::CHAOS_PARAM, B_CHAOS);
		knob<semi::KnobM>(at(L::VARI), Floater::VARI_PARAM, B_VARI);
		knob<semi::KnobS>(at(L::SWING), Floater::SWING_PARAM, B_SWING);
		knob<semi::KnobS>(at(L::TURNS), Floater::TURNS_PARAM, B_TURNS);
		addParam(createParamCentered<semi::PadCyan>(at(L::STRIKE), module, Floater::STRIKE_PARAM));
		addParam(createParamCentered<semi::PadMagenta>(at(L::NEW), module, Floater::NEW_PARAM));
		addParam(createParamCentered<semi::PadCyan>(at(L::MUTATE), module, Floater::MUTATE_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::RUN), module, Floater::RUN_PARAM));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::RUN_LED), module, Floater::RUN_LIGHT));

		addInput(createInputCentered<semi::Jack>(at(L::CLK), module, Floater::CLK_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RST), module, Floater::RST_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RUN_IN), module, Floater::RUN_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::STRK_IN), module, Floater::STRIKE_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::NEW_IN), module, Floater::NEW_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::MUT_IN), module, Floater::MUTATE_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::CHAOS_IN), module, Floater::CHAOS_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::ROT_IN), module, Floater::ROTATE_INPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_L), module, Floater::OUT_L_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_R), module, Floater::OUT_R_OUTPUT));
		const L::P gates[8] = {L::GATE1, L::GATE2, L::GATE3, L::GATE4, L::GATE5, L::GATE6, L::GATE7, L::GATE8};
		for (int i = 0; i < 8; i++)
			addOutput(createOutputCentered<semi::Jack>(at(gates[i]), module, Floater::GATE_OUTPUT + i));
		addOutput(createOutputCentered<semi::Jack>(at(L::CV_OUT), module, Floater::CV_OUTPUT));
	}

	template <class K>
	void knob(Vec p, int param, int bind) {
		auto* k = createParamCentered<K>(p, module, param);
		addParam(k);
		(void) bind;
	}

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Floater*>(module);
		if (!m)
			return;
		using namespace flt;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel(std::string("Patch: ") + m->cs.presetName + (m->cs.modified ? "*" : "")));

		// factory patches by category
		menu->addChild(createSubmenuItem("Factory patches", "", [=](Menu* sub) {
			const auto& f = Case::factoryPresets();
			for (const auto& cat : Case::presetCategories())
				sub->addChild(createSubmenuItem(cat, "", [=](Menu* sub2) {
					const auto& ff = Case::factoryPresets();
					for (int i = 0; i < (int) ff.size(); i++)
						if (ff[(size_t) i].category == cat)
							sub2->addChild(createMenuItem(ff[(size_t) i].name, m->cs.factoryIndex == i ? CHECKMARK_STRING : "", [=]() { m->reqFactory = i; }));
				}));
			(void) f;
		}));
		menu->addChild(createMenuItem("NEW PATCH", "", [=]() { m->reqNew = true; }));
		menu->addChild(createMenuItem("MUTATE", "", [=]() { m->reqMutate = true; }));

		// the selected lane and its module
		const int ln = m->selected;
		menu->addChild(createSubmenuItem(std::string("Lane ") + laneNames()[(size_t) ln], "", [=](Menu* sub) {
			caseItem(sub, m, ids::lon(ln), "On");
			sub->addChild(createBoolMenuItem("Drawn steps (off: Euclidean)", "", [=]() { return m->cs.get(ids::draw(ln)) > 0.5f; },
				[=](bool b) { m->cs.setLaneDraw(ln, b); m->cs.touch(); }));
			sub->addChild(createSubmenuItem("Draw: toggle a step", "", [=](Menu* sub2) {
				const int n = (int) m->cs.get(ids::steps(ln));
				for (int st = 0; st < n; st++)
					sub2->addChild(createMenuItem(string::f("Step %d", st + 1), (m->cs.pattern(ln) >> st) & 1u ? "x" : "",
						[=]() { m->cs.toggleStep(ln, st); m->cs.touch(); }));
			}));
			caseItem(sub, m, ids::logic(ln), "Logic");
			caseItem(sub, m, ids::rprot(ln), "Protect (goes round the REEL)");
			caseItem(sub, m, ids::dact(ln), "Drone act");
			sub->addChild(createMenuItem("Clear conditions and step locks", "", [=]() { m->cs.clearStepData(ln); }));
			sub->addChild(createMenuItem("Unflip (forget what LOCK has walked)", "", [=]() { m->cs.resetMutations(); }));
			if (ln < kVoices) {
				sub->addChild(new MenuSeparator);
				sub->addChild(createSubmenuItem("Model", "", [=](Menu* sub2) {
					const auto names = modelNames(ln);
					const int idx = Case::index(ids::model(ln));
					sub2->addChild(createMenuItem(names[0], (int) m->cs.get(idx) == 0 ? CHECKMARK_STRING : "", [=]() { m->cs.set(idx, 0.f); m->cs.touch(); }));
					sub2->addChild(createSubmenuItem("Plaits", "", [=](Menu* s3) {
						for (int k = kFirstPlaits; k < kFirstBraids; k++)
							s3->addChild(createMenuItem(names[(size_t) k], (int) m->cs.get(idx) == k ? CHECKMARK_STRING : "", [=]() { m->cs.set(idx, (float) k); m->cs.touch(); }));
					}));
					sub2->addChild(createSubmenuItem("Braids", "", [=](Menu* s3) {
						for (int k = kFirstBraids; k < kReelModel; k++)
							s3->addChild(createMenuItem(names[(size_t) k], (int) m->cs.get(idx) == k ? CHECKMARK_STRING : "", [=]() { m->cs.set(idx, (float) k); m->cs.touch(); }));
					}));
					sub2->addChild(createMenuItem(names[(size_t) kReelModel], (int) m->cs.get(idx) == kReelModel ? CHECKMARK_STRING : "", [=]() { m->cs.set(idx, (float) kReelModel); m->cs.touch(); }));
				}));
				caseItem(sub, m, ids::pan(ln), "Pan");
				caseItem(sub, m, ids::xmod(ln), "X-MOD");
				caseItem(sub, m, ids::xsrc(ln), "X-MOD from");
				caseItem(sub, m, ids::xamt(ln), "X-MOD amount");
				caseItem(sub, m, ids::sus(ln), "Sustain (holds its note)");
			}
			else {
				sub->addChild(new MenuSeparator);
				caseItem(sub, m, ids::smodel, "SCATTER model");
				caseItem(sub, m, ids::sspread, "Spread");
				caseItem(sub, m, ids::scpitch, "Clouds / Beads pitch (st)");
				caseItem(sub, m, ids::sbverb, "Beads reverb");
				caseItem(sub, m, ids::sfreeze, "Freeze");
			}
		}));

		menu->addChild(createSubmenuItem("Clock", "", [=](Menu* sub) {
			sub->addChild(createIndexSubmenuItem("CLK input", {"1/16", "1/8", "1/4", "24 PPQN"},
				[=]() { return (size_t) m->clockMode; }, [=](size_t i) { m->clockMode = (int) i; }));
			caseItem(sub, m, ids::loose, "Free (off the grid)");
			caseItem(sub, m, ids::ebb, "Ebb");
			caseItem(sub, m, ids::loop, "Loop");
			sub->addChild(createMenuItem(string::f("Seed %d: new seed", (int) m->cs.get(ids::seed)), "", [=]() {
				m->cs.set(ids::seed, (float) (random::u32() % 10000u));
			}));
			caseItem(sub, m, ids::bmax, "Burst max");
			caseItem(sub, m, ids::bshape, "Burst shape");
			caseItem(sub, m, ids::scale, "Scale");
			caseItem(sub, m, ids::wall, "Wall (silence before 16 / 32)");
		}));

		menu->addChild(createSubmenuItem("Pin matrix", "", [=](Menu* sub) {
			for (int so = 0; so < kSources; so++)
				sub->addChild(createSubmenuItem(sourceNames()[(size_t) so], "", [=](Menu* sub2) {
					caseItem(sub2, m, ids::rate(so), "Rate");
					caseItem(sub2, m, ids::depth(so), "Depth");
					sub2->addChild(new MenuSeparator);
					for (int d = 0; d < kDests; d++)
						sub2->addChild(createIndexSubmenuItem(destNames()[(size_t) d], {"-", "off", "+"},
							[=]() { return (size_t) ((int) m->cs.get(ids::pin(so, d)) + 1); },
							[=](size_t i) { m->cs.set(ids::pin(so, d), (float) ((int) i - 1)); }));
				}));
			sub->addChild(createIndexSubmenuItem("CV output", std::vector<std::string>(sourceNames().begin(), sourceNames().end()),
				[=]() { return (size_t) m->cvSource; }, [=](size_t i) { m->cvSource = (int) i; }));
		}));

		menu->addChild(createSubmenuItem("SEQ", "", [=](Menu* sub) {
			caseItem(sub, m, ids::seqlen, "Length");
			caseItem(sub, m, ids::seqx, "X by");
			caseItem(sub, m, ids::seqy, "Y by (Cartesian)");
			caseItem(sub, m, ids::seqglide, "Glide");
			sub->addChild(createMenuItem("Randomise the 16 values", "", [=]() {
				for (int k = 0; k < 16; k++)
					m->cs.setSeqValue(k, random::uniform() * 2.f - 1.f);
			}));
		}));

		menu->addChild(createSubmenuItem("Effects", "", [=](Menu* sub) {
			sub->addChild(createSubmenuItem("ZIGZAG (delay)", "", [=](Menu* s) {
				for (const char* id : {ids::ddiv, ids::dfb, ids::dbreak, ids::dstyle, ids::dmach, ids::drate})
					caseItem(s, m, id);
			}));
			sub->addChild(createSubmenuItem("SMR", "", [=](Menu* s) {
				for (const char* id : {ids::mmix, ids::mfreq, ids::mq, ids::mspread, ids::mglide, ids::mscale, ids::mrot})
					caseItem(s, m, id);
			}));
			sub->addChild(createSubmenuItem("SPACE (reverb)", "", [=](Menu* s) {
#ifndef METAMODULE
				caseItem(s, m, ids::vmodel);
#endif
				for (const char* id : {ids::vsize, ids::vdecay, ids::vdamp, ids::pgm, ids::vinf})
					caseItem(s, m, id);
			}));
			sub->addChild(createSubmenuItem("REEL", "", [=](Menu* s) {
				for (const char* id : {ids::rlen, ids::rgene, ids::rslide, ids::rmorph, ids::rorg, ids::rorgby, ids::rsplit, ids::rduck, ids::rduckby, ids::rhold, ids::ranchor})
					caseItem(s, m, id);
				caseItem(s, m, ids::rprot(kLanes), "Drone + resonator protected");
			}));
			sub->addChild(createSubmenuItem("FEEDBACK", "", [=](Menu* s) {
				caseItem(s, m, ids::fdbk);
				caseItem(s, m, ids::fdbkt);
			}));
			sub->addChild(createSubmenuItem("Bus (Drum Buss)", "", [=](Menu* s) {
				for (const char* id : {ids::bcomp, ids::bchar, ids::drive, ids::crunch, ids::trans, ids::boom, ids::level})
					caseItem(s, m, id);
			}));
		}));

		menu->addChild(createSubmenuItem("DRONE / RESONATOR / ARC", "", [=](Menu* sub) {
			for (const char* id : {ids::dlevel, ids::dpitch, ids::dratio, ids::dfine, ids::dfmab, ids::dfmba, ids::dring, ids::dsync, ids::dfold,
			                       ids::dshape, ids::dbright, ids::dchaos, ids::dsend, ids::dduck, ids::darc, ids::dpcv, ids::dfcv, ids::dfoldcv, ids::dlcv})
				caseItem(sub, m, id);
			sub->addChild(new MenuSeparator);
			for (const char* id : {ids::rmix, ids::rdecay, ids::rbright, ids::arclen, ids::arcshape})
				caseItem(sub, m, id);
		}));

		menu->addChild(createSubmenuItem("Lock from random", "", [=](Menu* sub) {
			sub->addChild(createMenuLabel("NEW PATCH / MUTATE leave these alone"));
			for (int b = 0; b < B_COUNT; b++) {
				const int idx = m->bindIndex[b];
				if (idx < 0)
					continue;
				sub->addChild(createBoolMenuItem(Case::defs()[(size_t) idx].name, "", [=]() { return m->cs.isLocked(idx); },
					[=](bool on) { m->cs.setLocked(idx, on); }));
			}
		}));
	}
};

Model* modelFloater = createModel<Floater, FloaterWidget>("Floater");
