// CALL & RESPONSE — Rack / MetaModule port of the Call & Response plug-in
// (semierendonmez-commits/callresponse-plugin). The plug-in runs the original JavaScript engine of
// Side Brain's C&R Generator Max for Live device (sidebrain.net), itself a port of Nordl3's
// Call-Response-Generator (MIT, engines/callresponse/LICENSE.Nordl3.txt). MetaModule cannot afford
// a JS engine, so engines/callresponse/CrgEngine is a C++ port of that JavaScript (checked against
// the JS under node: tests/callresponse_engine.cpp). See CrgEngine.h for what is not ported
// (HARMONY / CHORD OUT, the Custom form editor, Live-only features).
//
// It writes a short CALL and an answering RESPONSE and plays them by the phrase mode (call then
// response, interleaved cells, forms like A / B / A / C with fixed letter variants, mutating lanes,
// DRIFT). Each lane is a mono voice: V/OCT (1 V/oct, C4 = 0 V), GATE (10 V; a tie or a slide
// overlaps the next note, so the gate stays high and only V/OCT moves = a glide on a mono synth),
// VEL (0-10 V).
//
// Clock: CLK (1/16 by default, menu) + RST, the phrase steps at RATE (menu, 1/16 by default) with
// swing. FILL = the plug-in's RUN MODE (gate: plays while high; trigger: a rising edge plays it for
// TRIG LENGTH). NEW in = NEW PAIR. V/OCT in transposes both lanes in semitones.
//
// Threads: generation and compilation allocate, so they run on the worker (Rack: the widget's
// step(); MetaModule: an AsyncThread), exactly as the plug-in prepared patterns on its message
// thread. A compiled cycle (Record) is handed to the audio thread through an atomic pointer; the
// audio thread retires old ones through a lock-free ring. process() never allocates.

#include "../plugin.hpp"
#include "../common/AsyncJob.hpp"
#include "../common/Components.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/CallResponseLayout.hpp"
#include "callresponse/CrgEngine.h"

#include <atomic>
#include <cstdio>

#ifdef METAMODULE
#include "threads/async_thread.hh"
#endif

namespace L = semi::layout::callresponse;
using semi::Screen;

namespace {

constexpr int kMaxSteps = crg::kMaxSteps;
constexpr int kQueue = 128;
constexpr int kRing = 32;

const std::vector<std::string> kClockNames = {"1/16 (4 PPQN)", "1/8 (2 PPQN)", "1/4 (1 PPQN)", "24 PPQN"};
constexpr float kClockPpqn[] = {4.f, 2.f, 1.f, 24.f};
const std::vector<std::string> kRunModes = {"Always", "Gate (plays while FILL is high)", "Trigger (FILL plays it for TRIG LENGTH)"};
const std::vector<std::string> kTrigLens = {"1 beat", "2 beats", "1 bar", "2 bars", "4 bars", "1 pass", "2 passes", "Whole form"};
const std::vector<std::string> kFillStart = {"Restart (step 1)", "In sync"};

// The plug-in's factory presets (engine settings only; the chord / sound parts are not ported).
struct Factory {
	const char* name;
	std::vector<std::pair<const char*, const char*>> v;
};
const std::vector<Factory>& factories() {
	static const std::vector<Factory> f = {
	    {"Init", {}},
	    {"Acid Workout", {{"style", "Classic Acid"}, {"motifType", "Acid nibble"}, {"responseStrategy", "Slide the answer"}, {"phraseMode", "Call then response"}, {"endingBehaviour", "Rise into next phrase"}, {"scale", "Natural Minor / Aeolian"}, {"key", "A"}}},
	    {"Squelch Q&A", {{"style", "Acid Question/Answer"}, {"motifType", "Two-note hook"}, {"responseStrategy", "Answer downward"}, {"phraseMode", "Alternate half bars"}, {"endingBehaviour", "Response more resolved than call"}, {"scale", "Phrygian"}, {"key", "E"}}},
	    {"Hypno Loop 32", {{"style", "Hypnotic Techno"}, {"motifType", "Pedal point motif"}, {"responseStrategy", "Echo"}, {"phraseMode", "Mutating call, stable response"}, {"endingBehaviour", "Ambiguous ending"}, {"scale", "Minor Pentatonic"}, {"key", "F"}, {"steps", "32"}}},
	    {"Dub Chords", {{"style", "Dub Techno"}, {"motifType", "Drone anchor"}, {"responseStrategy", "Legato answer"}, {"phraseMode", "Call then response"}, {"endingBehaviour", "Weak cadence"}, {"scale", "Dorian"}, {"key", "G"}}},
	    {"Jackin Stab", {{"style", "Jackin House"}, {"motifType", "House bounce"}, {"responseStrategy", "Accent the answer"}, {"phraseMode", "Alternate 2-step cells"}, {"endingBehaviour", "Strong cadence"}, {"scale", "Dorian"}, {"key", "C"}}},
	    {"Deep 7ths", {{"style", "Deep House"}, {"motifType", "Syncopated"}, {"responseStrategy", "Same rhythm + resolve"}, {"phraseMode", "Call, response, varied response"}, {"endingBehaviour", "Resolve to root"}, {"scale", "Natural Minor / Aeolian"}, {"key", "D"}}},
	    {"Hoover Riff", {{"style", "Hooverish Riff"}, {"motifType", "Rave stab figure"}, {"responseStrategy", "Higher answer"}, {"phraseMode", "A / B / A / C"}, {"endingBehaviour", "Rise into next phrase"}, {"scale", "Harmonic Minor"}, {"key", "A"}}},
	    {"Detroit Dawn", {{"style", "Detroit-ish"}, {"motifType", "Arp fragment"}, {"responseStrategy", "Answer upward"}, {"phraseMode", "Call, call, response"}, {"endingBehaviour", "Response more resolved than call"}, {"scale", "Dorian"}, {"key", "F"}}},
	    {"Trance Lift 32", {{"style", "Proto-Trance"}, {"motifType", "Rolling"}, {"responseStrategy", "Transpose motif up"}, {"phraseMode", "A / A' / B / B'"}, {"endingBehaviour", "Strong cadence"}, {"scale", "Natural Minor / Aeolian"}, {"key", "B"}, {"steps", "32"}}},
	    {"Changes Bass", {{"style", "Classic House"}, {"motifType", "Two-note hook"}, {"responseStrategy", "Same rhythm + resolve"}, {"phraseMode", "Call then response"}, {"endingBehaviour", "Resolve to root"}, {"scale", "Natural Minor / Aeolian"}, {"key", "A"}}},
	    {"Acid Changes", {{"style", "Acid Runner"}, {"motifType", "Acid nibble"}, {"responseStrategy", "Slide the answer"}, {"phraseMode", "Call, call, response"}, {"endingBehaviour", "Rise into next phrase"}, {"scale", "Dorian"}, {"key", "E"}}},
	    {"Sub Pressure", {{"style", "Raw Warehouse"}, {"motifType", "Pulse"}, {"responseStrategy", "Lower answer"}, {"phraseMode", "Call then response"}, {"endingBehaviour", "Hard stop"}, {"scale", "Phrygian"}, {"key", "F#"}}},
	    {"acid303 (device preset)", {}},
	};
	return f;
}

void setNamed(crg::Params& p, const std::string& k, const std::string& v) {
	using namespace crg;
	if (k == "style") p.style = indexOf(kStyles, v.c_str());
	else if (k == "motifType") p.motifType = indexOf(kMotifs, v.c_str());
	else if (k == "responseStrategy") p.responseStrategy = indexOf(kStrategies, v.c_str());
	else if (k == "phraseMode") p.phraseMode = indexOf(kModes, v.c_str());
	else if (k == "endingBehaviour") p.endingBehaviour = indexOf(kEndings, v.c_str());
	else if (k == "scale") p.scale = indexOf(kScales, v.c_str());
	else if (k == "key") p.key = indexOf(kKeys, v.c_str());
	else if (k == "steps") p.steps = std::atoi(v.c_str());
}

// One step of one lane as the screen draws it.
enum : uint8_t { F_ACTIVE = 1, F_ACCENT = 2, F_SLIDE = 4, F_TIE = 8 };
struct PassScr {
	uint8_t pitch[2][kMaxSteps];  // 255 = rest
	uint8_t flags[2][kMaxSteps];
	uint8_t laneOf[kMaxSteps];    // which lane plays step i in this pass
	char letter = 0;              // form letter of the pass ('A', 'B', 'a' = A' ...), 0 = none
};

// A compiled phrase cycle (the plug-in's Pattern + the engine's record).
struct Record {
	int rev = 0, when = 0, kind = 0;  // kind 0 edit, 1 pair, 2 evolve
	int total = 16, steps = 8, sync = 0, passes = 1;
	double ticks = 120.0;
	bool evolves = false;
	std::vector<crg::Row> rows;     // sorted by slot
	std::vector<int> slotIndex;     // total + 1
	std::vector<PassScr> scr;       // per pass
	crg::State snap, next;          // worker only
};

struct Snapshot {
	PassScr pass;
	int steps = 8, step = -1, passIdx = 0, passes = 1;
	int style = 0, motif = 0, answer = 0, mode = 0, key = 9, scale = 15, rate = 3;
	bool running = false, queued = false, fillOpen = true;
	int runMode = 0;
	float bpm = 0.f;
};

} // namespace

struct CallResponse : Module {
	enum ParamId {
		STYLE_PARAM, MOTIF_PARAM, ANSWER_PARAM, MODE_PARAM,
		DENSITY_PARAM, CONTRAST_PARAM, OCTJUMP_PARAM, DRIFT_PARAM,
		NEW_PARAM, MUTATE_PARAM, DICE_PARAM,
		PARAMS_LEN
	};
	enum InputId { CLK_INPUT, RST_INPUT, FILL_INPUT, VOCT_INPUT, NEW_INPUT, INPUTS_LEN };
	enum OutputId { C_VOCT_OUTPUT, C_GATE_OUTPUT, C_VEL_OUTPUT, R_VOCT_OUTPUT, R_GATE_OUTPUT, R_VEL_OUTPUT, OUTPUTS_LEN };
	enum LightId { NEW_LIGHT, LIGHTS_LEN };

	enum Cmd {
		CMD_NONE, CMD_NEW, CMD_MUTATE, CMD_DICE, CMD_UNDO, CMD_NEWCALL, CMD_NEWRESP, CMD_MUTCALL, CMD_MUTRESP,
		CMD_KEEPRHYTHM, CMD_KEEPPITCH, CMD_KEEPMOTIF, CMD_SHIFTL, CMD_SHIFTR, CMD_PRESET  // CMD_PRESET + index
	};

	// ---- menu state (UI side; the worker reads it, and writes it back after dice / presets) ------
	crg::Params menu = crg::Params::defaults();
	int clockIdx = 0, runMode = 0, trigLenIdx = 2, fillStart = 0;
	std::atomic<int> cmd{CMD_NONE};

	// ---- worker side (guarded by `busy`) -------------------------------------------------------
	crg::Engine eng{0x5EED};
	std::atomic_flag busy = ATOMIC_FLAG_INIT;
	bool force = true;           // compile without a parameter change (load, first run)
	int nextRev = 1, lastPosted = 0, seenActive = 0, successorFor = 0;
	std::vector<Record*> live;   // records posted and not yet retired
#ifdef METAMODULE
	semi::AsyncJob worker{this, [this]() { service(); }};
#endif

	// ---- hand-over ---------------------------------------------------------------------------
	std::atomic<Record*> pendingPtr{nullptr};
	Record* ring[kRing] = {};
	std::atomic<int> ringHead{0}, ringTail{0};
	std::atomic<int> activeRev{0}, activePass{0};
	std::atomic<bool> runningNow{false};

	// ---- audio thread -------------------------------------------------------------------------
	Record* active = nullptr;
	Record* held = nullptr;  // taken from pendingPtr, waiting for its cycle start
	dsp::SchmittTrigger clkTrig, rstTrig, fillTrig, newTrig;
	dsp::BooleanTrigger newBtn, mutBtn, diceBtn;
	int count = -1;
	float sinceClk = 0.f, period = 0.f;
	bool clkSeen = false;
	float bpm = 120.f;
	bool wasRunning = false;
	double nextBoundary = 0.0, lastPpq = 0.0;
	int counter = -1;
	bool gateOpen = true, restartReq = false;
	double trigRemain = 0.0;
	int64_t clock = 0;
	struct Pend {
		int64_t time;
		int pitch, vel, lane;
		bool on;
	} queue[kQueue];
	int nQueue = 0;
	struct Voice {
		bool on = false;
		int pitch = -1;
		float volts = 0.f, vel = 0.f, low = 0.f;
	} voice[2];

	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;

	CallResponse() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		const crg::Params d = crg::Params::defaults();
		configSwitch(STYLE_PARAM, 0.f, (float) crg::kStyles.size() - 1, (float) d.style, "Style", crg::kStyles);
		configSwitch(MOTIF_PARAM, 0.f, (float) crg::kMotifs.size() - 1, (float) d.motifType, "Motif", crg::kMotifs);
		configSwitch(ANSWER_PARAM, 0.f, (float) crg::kStrategies.size() - 1, (float) d.responseStrategy, "Answer", crg::kStrategies);
		configSwitch(MODE_PARAM, 0.f, (float) crg::kModes.size() - 1, (float) d.phraseMode, "Phrase mode", crg::kModes);
		configParam(DENSITY_PARAM, 0.f, 100.f, 52.f, "Density", " %");
		configParam(CONTRAST_PARAM, 0.f, 100.f, 50.f, "Contrast", " %");
		configParam(OCTJUMP_PARAM, 0.f, 100.f, 0.f, "Octave jump", " %");
		configParam(DRIFT_PARAM, 0.f, 100.f, 0.f, "Drift (both lanes mutate a little every pass)", " %");
		for (int i = DENSITY_PARAM; i <= DRIFT_PARAM; i++)
			paramQuantities[i]->snapEnabled = true;
		configButton(NEW_PARAM, "New pair");
		configButton(MUTATE_PARAM, "Mutate both");
		configButton(DICE_PARAM, "Dice (reroll the menus, new pair)");
		configLight(NEW_LIGHT, "New pair queued for the next phrase");
		configInput(CLK_INPUT, "Clock (1/16 notes by default)");
		configInput(RST_INPUT, "Reset (next clock = step 1)");
		configInput(FILL_INPUT, "Fill (run mode gate / trigger, menu)");
		configInput(VOCT_INPUT, "Transpose (V/OCT, semitones)");
		configInput(NEW_INPUT, "New pair trigger");
		configOutput(C_VOCT_OUTPUT, "Call V/OCT");
		configOutput(C_GATE_OUTPUT, "Call gate");
		configOutput(C_VEL_OUTPUT, "Call velocity (0-10 V)");
		configOutput(R_VOCT_OUTPUT, "Response V/OCT");
		configOutput(R_GATE_OUTPUT, "Response gate");
		configOutput(R_VEL_OUTPUT, "Response velocity (0-10 V)");
		eng.S.p = currentParams();
		eng.generate(0);
		service();  // first cycle, so the module plays at once
#ifdef METAMODULE
		worker.start();
#endif
	}

	~CallResponse() {
		// background jobs first (and wait for a run in flight): they write into this module
#ifdef METAMODULE
		worker.stopAndWait();
#endif
		// every record ever posted and not yet freed is in `live` (pending, held, active, retired)
		pendingPtr.store(nullptr);
		for (Record* r : live)
			delete r;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		lock();
		menu = crg::Params::defaults();
		eng.S = crg::State();
		eng.S.p = currentParams();
		eng.generate(0);
		force = true;
		unlock();
		clockIdx = runMode = fillStart = 0;
		trigLenIdx = 2;
	}

	void lock() {
		while (busy.test_and_set(std::memory_order_acquire)) {
		}
	}
	void unlock() { busy.clear(std::memory_order_release); }

	// ---- worker -------------------------------------------------------------------------------

	// The panel knobs + the menu, as the engine's parameters.
	crg::Params currentParams() {
		crg::Params p = menu;
		p.style = clamp((int) std::round(params[STYLE_PARAM].getValue()), 0, (int) crg::kStyles.size() - 1);
		p.motifType = clamp((int) std::round(params[MOTIF_PARAM].getValue()), 0, (int) crg::kMotifs.size() - 1);
		p.responseStrategy = clamp((int) std::round(params[ANSWER_PARAM].getValue()), 0, (int) crg::kStrategies.size() - 1);
		p.phraseMode = clamp((int) std::round(params[MODE_PARAM].getValue()), 0, (int) crg::kModes.size() - 1);
		p.density = std::round(params[DENSITY_PARAM].getValue());
		p.contrast = std::round(params[CONTRAST_PARAM].getValue());
		p.octaveJump = std::round(params[OCTJUMP_PARAM].getValue());
		p.mutateAll = std::round(params[DRIFT_PARAM].getValue());
		p.driftCall = eng.S.p.driftCall;  // owned by the engine (DRIFT sets both)
		p.driftResponse = eng.S.p.driftResponse;
		return p;
	}

	// After the engine changed its own parameters (dice, presets, articulation profile, undo).
	void writeBack() {
		const crg::Params& p = eng.S.p;
		params[STYLE_PARAM].setValue((float) p.style);
		params[MOTIF_PARAM].setValue((float) p.motifType);
		params[ANSWER_PARAM].setValue((float) p.responseStrategy);
		params[MODE_PARAM].setValue((float) p.phraseMode);
		params[DENSITY_PARAM].setValue((float) p.density);
		params[CONTRAST_PARAM].setValue((float) p.contrast);
		params[OCTJUMP_PARAM].setValue((float) p.octaveJump);
		params[DRIFT_PARAM].setValue((float) p.mutateAll);
		menu = p;
	}

	Record* findLive(int rev) {
		for (Record* r : live)
			if (r->rev == rev)
				return r;
		return nullptr;
	}

	void drainRetired() {
		int t = ringTail.load(std::memory_order_relaxed);
		while (t != ringHead.load(std::memory_order_acquire)) {
			Record* r = ring[t];
			live.erase(std::remove(live.begin(), live.end(), r), live.end());
			delete r;
			t = (t + 1) % kRing;
			ringTail.store(t, std::memory_order_release);
		}
	}

	void buildScreens(Record& r, const crg::Compiled& c) {
		r.passes = std::max(1, (int) c.views.size());
		const crg::Params& p = c.snap.p;
		const int mode = p.phraseMode;
		const bool abac = crg::kModes[(size_t) mode] == "A / B / A / C", aabb = crg::kModes[(size_t) mode] == "A / A' / B / B'";
		for (int pass = 0; pass < r.passes; pass++) {
			PassScr s{};
			const crg::PassView& v = c.views[(size_t) pass];
			for (int lane = 0; lane < 2; lane++) {
				const crg::Phrase& ph = (v.hasVariant && v.lane == lane) ? v.variant : (lane == 0 ? v.call : v.response);
				for (int i = 0; i < kMaxSteps; i++) {
					const int pt = i < (int) ph.size() ? eng.stepPitch(ph, i, p) : -1;
					s.pitch[lane][i] = pt < 0 ? 255 : (uint8_t) pt;
					uint8_t f = 0;
					if (pt >= 0) {
						const crg::Step& st = ph[(size_t) i];
						f = F_ACTIVE | (st.accent ? F_ACCENT : 0) | (st.slide ? F_SLIDE : 0) | (st.tie ? F_TIE : 0);
					}
					s.flags[lane][i] = f;
				}
			}
			// which lane owns each step: the rows of this pass tell, the plan otherwise
			for (int i = 0; i < kMaxSteps; i++)
				s.laneOf[i] = (uint8_t) v.lane;
			const int interleave = crg::kModes[(size_t) mode].find("Alternate") == 0;
			if (interleave) {
				const std::string& m = crg::kModes[(size_t) mode];
				const int cell = m.find("half") != std::string::npos ? std::max(1, c.steps / 2) : (m.find("2-step") != std::string::npos ? 2 : 1);
				const bool flip = m.find("response first") != std::string::npos;
				for (int i = 0; i < kMaxSteps; i++)
					s.laneOf[i] = (uint8_t) ((((i / cell) % 2 == 0) != flip) ? 0 : 1);
			}
			s.letter = abac ? "ABAC"[pass % 4] : aabb ? "AaBb"[pass % 4] : 0;  // lower case = primed
			r.scr.push_back(s);
		}
	}

	void post(const crg::State& from, int when, int kind) {
		crg::Compiled c = eng.compile(from);
		auto* r = new Record;
		r->rev = nextRev++;
		r->when = when;
		r->kind = kind;
		r->total = std::max(1, c.total);
		r->steps = c.steps;
		r->sync = c.sync;
		r->ticks = c.ticks;
		r->evolves = c.evolves;
		r->rows = c.rows;
		std::stable_sort(r->rows.begin(), r->rows.end(), [](const crg::Row& a, const crg::Row& b) { return a.slot < b.slot; });
		r->slotIndex.assign((size_t) r->total + 1, 0);
		size_t e = 0;
		for (int s = 0; s <= r->total; ++s) {
			while (e < r->rows.size() && r->rows[e].slot < s)
				++e;
			r->slotIndex[(size_t) s] = (int) e;
		}
		buildScreens(*r, c);
		r->snap = c.snap;
		r->next = c.next;
		live.push_back(r);
		lastPosted = r->rev;
		if (Record* old = pendingPtr.exchange(r)) {
			live.erase(std::remove(live.begin(), live.end(), old), live.end());
			delete old;  // never taken by the audio thread
		}
	}

	void runCommand(int c) {
		const bool next = menu.newPairTiming == 1 && runningNow.load();
		switch (c) {
			case CMD_NEW: eng.pushUndo(); eng.generate(0); post(eng.S, next ? 1 : 0, 1); return;
			case CMD_DICE: eng.dice(); writeBack(); post(eng.S, next ? 1 : 0, 1); return;
			case CMD_MUTATE: eng.mutate(0); break;
			case CMD_MUTCALL: eng.mutate(1); break;
			case CMD_MUTRESP: eng.mutate(2); break;
			case CMD_NEWCALL: eng.generate(1); break;
			case CMD_NEWRESP: eng.generate(2); break;
			case CMD_UNDO: if (eng.undo()) writeBack(); break;
			case CMD_KEEPRHYTHM: eng.keepRhythm(); break;
			case CMD_KEEPPITCH: eng.keepPitch(); break;
			case CMD_KEEPMOTIF: eng.keepMotif(); break;
			case CMD_SHIFTL: eng.shift(0, 1); break;
			case CMD_SHIFTR: eng.shift(0, -1); break;
			default:
				if (c >= CMD_PRESET) {
					const int i = c - CMD_PRESET;
					const auto& f = factories();
					if (i < 0 || i >= (int) f.size())
						return;
					eng.pushUndo();
					crg::Params p = crg::Params::defaults();
					p.newPairTiming = eng.S.p.newPairTiming;
					p.syncMode = eng.S.p.syncMode;
					eng.S.p = p;
					if (i == (int) f.size() - 1)
						eng.preset303();
					else {
						for (auto& kv : f[(size_t) i].v)
							setNamed(eng.S.p, kv.first, kv.second);
						eng.generate(0);
					}
					writeBack();
				}
				break;
		}
		post(eng.S, 0, 0);
	}

	// Rack: the widget's step() (UI thread). MetaModule: the AsyncThread.
	void service() {
		if (busy.test_and_set(std::memory_order_acquire))
			return;
		drainRetired();

		// the cycle that is playing now (nativecommit / nativepulse)
		const int act = activeRev.load(std::memory_order_acquire);
		Record* ar = act ? findLive(act) : nullptr;
		if (ar && act != seenActive) {
			seenActive = act;
			if (lastPosted <= act)  // not superseded by a later edit
				eng.S = ar->snap;
		}

		// parameter edits
		const crg::Params np = currentParams();
		if (np != eng.S.p) {
			const int artBefore = eng.S.p.articulationProfile;
			eng.applyParams(np);
			if (eng.S.p != np || eng.S.p.articulationProfile != artBefore)
				writeBack();
			post(eng.S, 0, 0);
		}
		else if (force) {
			post(eng.S, 0, 0);
		}
		force = false;

		const int c = cmd.exchange(CMD_NONE);
		if (c != CMD_NONE)
			runCommand(c);

		// an evolving cycle prepares its successor (nativePrepareSuccessor)
		if (ar && ar->evolves && runningNow.load() && lastPosted <= act && successorFor != act) {
			successorFor = act;
			post(ar->next, 1, 2);
		}
		busy.clear(std::memory_order_release);
	}

	// ---- JSON -----------------------------------------------------------------------------------

	static json_t* phraseToJson(const crg::Phrase& ph) {
		json_t* a = json_array();
		for (const auto& s : ph.s) {
			json_t* e = json_array();
			json_array_append_new(e, json_integer(s.active));
			json_array_append_new(e, json_real(std::isnan(s.degree) ? -1.0 : s.degree));
			json_array_append_new(e, json_real(s.gate));
			json_array_append_new(e, json_integer(s.accent | (s.slide << 1) | (s.tie << 2)));
			json_array_append_new(e, json_integer(s.oct));
			json_array_append_new(a, e);
		}
		return a;
	}
	static bool phraseFromJson(json_t* a, crg::Phrase& ph) {
		if (!json_is_array(a) || json_array_size(a) < 2 || json_array_size(a) > (size_t) kMaxSteps)
			return false;
		crg::Phrase out;
		size_t i;
		json_t* e;
		json_array_foreach(a, i, e) {
			crg::Step s;
			s.active = json_integer_value(json_array_get(e, 0)) != 0;
			const double d = json_number_value(json_array_get(e, 1));
			s.degree = d < 0 ? NAN : d;
			s.gate = std::clamp(json_number_value(json_array_get(e, 2)), 0.01, 2.0);
			const int f = (int) json_integer_value(json_array_get(e, 3));
			s.accent = f & 1;
			s.slide = f & 2;
			s.tie = f & 4;
			s.oct = clamp((int) json_integer_value(json_array_get(e, 4)), -1, 1);
			out.s.push_back(s);
		}
		ph = out;
		return true;
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		lock();
		const crg::Params p = eng.S.p;
		json_object_set_new(root, "call", phraseToJson(eng.S.call));
		json_object_set_new(root, "response", phraseToJson(eng.S.response));
		unlock();
		const int ints[] = {p.endingBehaviour, p.key, p.scale, p.rootOctave, p.steps, p.registerProfile, p.densityPreset,
		                    p.contourPreset, p.repetitionPreset, p.mutationAmount, p.mutationTarget, p.notePoolMode,
		                    p.articulationProfile, p.gateFeel, p.connectionFeel, p.slideProfile, p.accentProfile, p.rate,
		                    p.syncMode, p.newPairTiming};
		json_t* e = json_array();
		for (int v : ints)
			json_array_append_new(e, json_integer(v));
		json_object_set_new(root, "enums", e);
		const double nums[] = {p.noteLengthPct, p.baseVelocity, p.accentDepth, p.swing, p.resolve, p.driftCall, p.driftResponse};
		json_t* n = json_array();
		for (double v : nums)
			json_array_append_new(n, json_real(v));
		json_object_set_new(root, "nums", n);
		json_object_set_new(root, "clock", json_integer(clockIdx));
		json_object_set_new(root, "runMode", json_integer(runMode));
		json_object_set_new(root, "trigLen", json_integer(trigLenIdx));
		json_object_set_new(root, "fillStart", json_integer(fillStart));
		return root;
	}

	void dataFromJson(json_t* root) override {
		auto geti = [&](const char* k, int& v, int lo, int hi) {
			if (json_t* j = json_object_get(root, k))
				v = clamp((int) json_integer_value(j), lo, hi);
		};
		geti("clock", clockIdx, 0, 3);
		geti("runMode", runMode, 0, 2);
		geti("trigLen", trigLenIdx, 0, 7);
		geti("fillStart", fillStart, 0, 1);
		lock();
		crg::Params p = currentParams();
		if (json_t* e = json_object_get(root, "enums")) {
			int* f[] = {&p.endingBehaviour, &p.key, &p.scale, &p.rootOctave, &p.steps, &p.registerProfile, &p.densityPreset,
			            &p.contourPreset, &p.repetitionPreset, &p.mutationAmount, &p.mutationTarget, &p.notePoolMode,
			            &p.articulationProfile, &p.gateFeel, &p.connectionFeel, &p.slideProfile, &p.accentProfile, &p.rate,
			            &p.syncMode, &p.newPairTiming};
			const int hi[] = {(int) crg::kEndings.size(), 12, (int) crg::kScales.size(), 7, crg::kMaxSteps + 1, 3, 6, 8, 5, 5, 9,
			                  (int) crg::kNotePools.size(), (int) crg::kArticulation.size(), (int) crg::kGateFeels.size(),
			                  (int) crg::kConnections.size(), (int) crg::kSlides.size(), (int) crg::kAccents.size(), 6, 3, 2};
			for (int i = 0; i < 20; i++)
				if (json_t* j = json_array_get(e, i))
					*f[i] = clamp((int) json_integer_value(j), 0, hi[i] - 1);
			p.steps = clamp(p.steps, crg::kMinSteps, crg::kMaxSteps);
		}
		if (json_t* n = json_object_get(root, "nums")) {
			double* f[] = {&p.noteLengthPct, &p.baseVelocity, &p.accentDepth, &p.swing, &p.resolve, &p.driftCall, &p.driftResponse};
			for (int i = 0; i < 7; i++)
				if (json_t* j = json_array_get(n, i))
					*f[i] = json_number_value(j);
		}
		menu = p;
		eng.S.p = p;
		crg::Phrase c, r;
		if (phraseFromJson(json_object_get(root, "call"), c) && phraseFromJson(json_object_get(root, "response"), r) &&
		    (int) c.size() == p.steps && (int) r.size() == p.steps) {
			eng.S.call = c;
			eng.S.response = r;
			eng.S.variants[0].clear();
			eng.S.variants[1].clear();
		}
		else {
			eng.generate(0);
		}
		force = true;
		unlock();
	}

	// ---- clock ----------------------------------------------------------------------------------

	bool clockPosition(float dt, double& ppq) {
		sinceClk += dt;
		if (inputs[RST_INPUT].isConnected() && rstTrig.process(inputs[RST_INPUT].getVoltage(), 0.1f, 1.f))
			count = -1;
		if (!inputs[CLK_INPUT].isConnected()) {
			clkSeen = false;
			period = 0.f;
			count = -1;
			return false;
		}
		if (clkTrig.process(inputs[CLK_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (clkSeen)
				period = sinceClk;
			clkSeen = true;
			count++;
			sinceClk = 0.f;
		}
		if (period > 0.f && sinceClk > 2.5f * period) {
			clkSeen = false;
			period = 0.f;
		}
		if (count < 0 || period <= 0.f)
			return false;
		const float ppqn = kClockPpqn[clockIdx];
		bpm = 60.f / (ppqn * period);
		ppq = (count + std::min(sinceClk / period, 0.999f)) / ppqn;
		return true;
	}

	// ---- the plug-in's Sequencer, per sample --------------------------------------------------

	void retire(Record* r) {
		if (!r)
			return;
		const int h = ringHead.load(std::memory_order_relaxed);
		const int n = (h + 1) % kRing;
		if (n == ringTail.load(std::memory_order_acquire))
			return;  // full: leak rather than free on the audio thread (never in practice)
		ring[h] = r;
		ringHead.store(n, std::memory_order_release);
	}

	void activate(Record* p, bool fromCycleStart) {
		retire(active);
		active = p;
		if (fromCycleStart)
			counter = -1;
		else if (counter >= 0)
			counter %= active->total;
		activeRev.store(active->rev, std::memory_order_release);
	}

	void allOff() {
		nQueue = 0;
		for (auto& v : voice)
			v.on = false;
	}

	void push(const Pend& p) {
		if (nQueue < kQueue)
			queue[nQueue++] = p;
	}

	void doStep(double boundaryPpq, double samplesPerBeat) {
		if (held) {
			if (!active || held->when == 0) {
				Record* p = held;
				held = nullptr;
				activate(p, active == nullptr);
			}
			else {
				int upcoming = 0;
				if (active->sync == 1) {
					const double sb = active->ticks / 480.0;
					upcoming = (int) (((int64_t) std::llround(boundaryPpq / sb)) % active->total);
				}
				else
					upcoming = (counter + 1) % active->total;
				if (upcoming == 0) {
					Record* p = held;
					held = nullptr;
					activate(p, true);
				}
			}
		}
		if (!active)
			return;
		int slot;
		if (active->sync == 1) {
			const double sb = active->ticks / 480.0;
			const int64_t idx = std::llround(boundaryPpq / sb);
			slot = (int) (((idx % active->total) + active->total) % active->total);
			counter = slot;
		}
		else {
			counter = (counter + 1) % active->total;
			slot = counter;
		}
		activePass.store(slot / std::max(1, active->steps), std::memory_order_relaxed);
		if (!gateOpen)
			return;
		int transpose = 0;
		if (inputs[VOCT_INPUT].isConnected())
			transpose = (int) std::lround(inputs[VOCT_INPUT].getVoltage() * 12.f);
		const double spt = samplesPerBeat / 480.0;
		for (int e = active->slotIndex[(size_t) slot]; e < active->slotIndex[(size_t) slot + 1]; ++e) {
			const crg::Row& ev = active->rows[(size_t) e];
			const int64_t on = clock + std::llround(ev.swingTicks * spt);
			const int64_t off = on + std::max<int64_t>(1, std::llround(ev.durTicks * spt));
			const int pitch = clamp(ev.pitch + transpose, 0, 127);
			push({on, pitch, ev.vel, ev.lane, true});
			push({off, pitch, 0, ev.lane, false});
		}
	}

	void emitDue() {
		for (;;) {
			int best = -1;
			for (int i = 0; i < nQueue; ++i) {
				const Pend& q = queue[i];
				if (q.time > clock)
					continue;
				if (best < 0 || q.time < queue[best].time || (q.time == queue[best].time && !q.on && queue[best].on))
					best = i;
			}
			if (best < 0)
				break;
			const Pend q = queue[best];
			queue[best] = queue[--nQueue];
			Voice& v = voice[clamp(q.lane, 0, 1)];
			if (q.on) {
				if (v.on && v.pitch == q.pitch) {
					// retrigger: close the old note and drop its pending off
					v.low = 0.001f;
					for (int i = 0; i < nQueue; ++i)
						if (!queue[i].on && queue[i].pitch == q.pitch && queue[i].lane == q.lane) {
							queue[i] = queue[--nQueue];
							break;
						}
				}
				// a different pitch while the gate is high = legato (tie / slide overlap)
				v.on = true;
				v.pitch = q.pitch;
				v.volts = (q.pitch - 60) / 12.f;
				v.vel = q.vel / 127.f * 10.f;
			}
			else if (v.on && v.pitch == q.pitch) {
				v.on = false;
			}
		}
	}

	double trigLengthBeats() const {
		const double sb = active ? active->ticks / 480.0 : 0.25;
		switch (trigLenIdx) {
			case 0: return 1.0;
			case 1: return 2.0;
			case 2: return 4.0;
			case 3: return 8.0;
			case 4: return 16.0;
			case 5: return (active ? active->steps : 8) * sb;
			case 6: return (active ? active->steps : 8) * sb * 2;
			default: return (active ? active->total : 16) * sb;
		}
	}

	void process(const ProcessArgs& args) override {
		if (newBtn.process(params[NEW_PARAM].getValue() > 0.f) || newTrig.process(inputs[NEW_INPUT].getVoltage(), 0.1f, 1.f))
			cmd = CMD_NEW;
		if (mutBtn.process(params[MUTATE_PARAM].getValue() > 0.f))
			cmd = CMD_MUTATE;
		if (diceBtn.process(params[DICE_PARAM].getValue() > 0.f))
			cmd = CMD_DICE;

		// new cycle from the worker
		if (Record* p = pendingPtr.exchange(nullptr, std::memory_order_acq_rel)) {
			retire(held);
			held = p;
		}

		double ppq = 0.0;
		const bool ok = clockPosition(args.sampleTime, ppq);

		// FILL (the plug-in's RUN MODE)
		const bool wasFill = fillTrig.isHigh();
		fillTrig.process(inputs[FILL_INPUT].getVoltage(), 0.1f, 1.f);
		const bool rise = fillTrig.isHigh() && !wasFill;
		bool open = true;
		if (runMode == 1) {
			open = fillTrig.isHigh();
			if (rise && fillStart == 0)
				restartReq = true;
		}
		else if (runMode == 2) {
			if (rise) {
				trigRemain = trigLengthBeats();
				if (fillStart == 0)
					restartReq = true;
			}
			open = trigRemain > 0.0;
			if (ok)
				trigRemain -= std::max(0.0, ppq - lastPpq);
		}
		if (gateOpen && !open)
			allOff();
		gateOpen = open;

		if (!ok) {
			if (wasRunning) {
				allOff();
				wasRunning = false;
				counter = -1;
			}
			if (held && (held->when == 0 || !active)) {
				Record* p = held;
				held = nullptr;
				activate(p, true);
			}
		}
		else {
			const Record* ref = active ? active : held;
			const double stepBeats = ref ? ref->ticks / 480.0 : 0.25;
			if (!wasRunning) {
				wasRunning = true;
				counter = -1;
				nextBoundary = std::ceil(ppq / stepBeats - 1e-6) * stepBeats;
			}
			if (restartReq) {
				restartReq = false;
				counter = -1;
				nextBoundary = std::ceil(ppq / stepBeats - 1e-6) * stepBeats;
			}
			const double spb = args.sampleRate * 60.0 / std::max(1.f, bpm);
			int guard = 0;
			while (ppq >= nextBoundary - 1e-9 && guard++ < 8) {
				doStep(nextBoundary, spb);
				nextBoundary += active ? active->ticks / 480.0 : stepBeats;
			}
		}
		lastPpq = ppq;
		runningNow.store(ok, std::memory_order_relaxed);

		emitDue();
		clock++;

		for (int l = 0; l < 2; l++) {
			Voice& v = voice[l];
			bool g = v.on;
			if (v.low > 0.f) {
				v.low -= args.sampleTime;
				g = false;
			}
			outputs[l == 0 ? C_VOCT_OUTPUT : R_VOCT_OUTPUT].setVoltage(v.volts);
			outputs[l == 0 ? C_GATE_OUTPUT : R_GATE_OUTPUT].setVoltage(g ? 10.f : 0.f);
			outputs[l == 0 ? C_VEL_OUTPUT : R_VEL_OUTPUT].setVoltage(v.vel);
		}

		if (ui.tick(args.sampleTime)) {
			const bool queued = held && held->kind == 1 && held->when == 1;
			lights[NEW_LIGHT].setBrightness(queued ? 1.f : 0.f);
			Snapshot& s = snap.writeSlot();
			if (active) {
				const int pass = clamp(counter < 0 ? 0 : counter / std::max(1, active->steps), 0, (int) active->scr.size() - 1);
				s.pass = active->scr[(size_t) pass];
				s.steps = active->steps;
				s.passes = active->passes;
				s.passIdx = pass;
				s.step = ok && counter >= 0 ? counter % std::max(1, active->steps) : -1;
			}
			s.style = (int) params[STYLE_PARAM].getValue();
			s.motif = (int) params[MOTIF_PARAM].getValue();
			s.answer = (int) params[ANSWER_PARAM].getValue();
			s.mode = (int) params[MODE_PARAM].getValue();
			s.key = menu.key;
			s.scale = menu.scale;
			s.rate = menu.rate;
			s.running = ok;
			s.queued = queued;
			s.fillOpen = gateOpen;
			s.runMode = runMode;
			s.bpm = ok ? bpm : 0.f;
			snap.publish();
		}
	}
};

// ---- screens ----------------------------------------------------------------------------------

// The pair: the call roll (cyan) over the response roll (magenta), one column per step, a block at
// each note's pitch, accents brighter (amber), a bar into the next column on a tie / slide. The
// lane that plays each step in this pass is lit, the other dimmed; a pass strip shows the plan
// (C / R / letters) with the current pass, and the playhead runs under the rolls.
struct PhraseScreen : Screen {
	CallResponse* module = nullptr;
	Snapshot demo;

	PhraseScreen() {
		crg::Engine e(3);
		e.S.p.steps = 16;
		e.generate(0);
		const crg::Compiled c = e.compile(e.S);
		for (int lane = 0; lane < 2; lane++) {
			const crg::Phrase& ph = lane == 0 ? e.S.call : e.S.response;
			for (int i = 0; i < kMaxSteps; i++) {
				const int p = i < (int) ph.size() ? e.stepPitch(ph, i, e.S.p) : -1;
				demo.pass.pitch[lane][i] = p < 0 ? 255 : (uint8_t) p;
				demo.pass.flags[lane][i] = p < 0 ? 0 : (uint8_t) (F_ACTIVE | (ph[(size_t) i].accent ? F_ACCENT : 0) | (ph[(size_t) i].slide ? F_SLIDE : 0) | (ph[(size_t) i].tie ? F_TIE : 0));
			}
			demo.pass.laneOf[0] = 0;
		}
		for (int i = 0; i < kMaxSteps; i++)
			demo.pass.laneOf[i] = 0;
		demo.steps = 16;
		demo.passes = c.total / 16;
		demo.step = 6;
		demo.running = true;
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		const float pad = 3.f;
		const float fs = std::max(9.f, semi::platform::kMinText + 1.f);
		const int steps = clamp(s.steps, 2, kMaxSteps);
		const float stripH = fs + 2.f;
		const float gx0 = pad + 10.f, gx1 = w - pad;
		const float gy0 = pad, gy1 = h - pad - stripH - 2.f;
		const float laneH = (gy1 - gy0 - 3.f) / 2.f;
		const float cw = (gx1 - gx0) / steps;
		const float bw = std::max(1.6f, cw - 0.8f);

		for (int lane = 0; lane < 2; lane++) {
			const float y0 = gy0 + lane * (laneH + 3.f);
			text(vg, pad, y0 + laneH * 0.5f, lane == 0 ? "C" : "R", 9.f, lane == 0 ? semi::theme::cyan() : semi::theme::magenta(),
			     NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			fillRect(vg, gx0, y0 + laneH - 0.6f, gx1 - gx0, 0.8f, semi::theme::grid());
			int lo = 127, hi = 0;
			for (int i = 0; i < steps; i++)
				if (s.pass.pitch[lane][i] != 255) {
					lo = std::min(lo, (int) s.pass.pitch[lane][i]);
					hi = std::max(hi, (int) s.pass.pitch[lane][i]);
				}
			if (lo > hi)
				continue;
			const float span = (float) std::max(7, hi - lo + 1);
			const float nh = std::max(2.4f, laneH / span);
			auto yOf = [&](int p) { return y0 + laneH - nh - (p - lo) / span * (laneH - nh); };
			const NVGcolor on = lane == 0 ? semi::theme::cyan() : semi::theme::magenta();
			const NVGcolor dimC = lane == 0 ? semi::theme::cyanDim() : semi::theme::magentaDeep();
			for (int pass = 0; pass < 4; pass++) {  // 0 dimmed, 1 playing, 2 accents, 3 joins
				nvgBeginPath(vg);
				bool any = false;
				for (int i = 0; i < steps; i++) {
					const uint8_t f = s.pass.flags[lane][i];
					if (!(f & F_ACTIVE))
						continue;
					const bool plays = s.pass.laneOf[i] == lane;
					const float x = gx0 + i * cw + 0.4f, y = yOf(s.pass.pitch[lane][i]);
					if (pass == 0 && !plays) { nvgRect(vg, x, y, bw, nh); any = true; }
					else if (pass == 1 && plays && !(f & F_ACCENT)) { nvgRect(vg, x, y, bw, nh); any = true; }
					else if (pass == 2 && plays && (f & F_ACCENT)) { nvgRect(vg, x, y, bw, nh); any = true; }
					else if (pass == 3 && plays && (f & (F_SLIDE | F_TIE)) && i + 1 < steps) {
						nvgRect(vg, x + bw, y + nh * 0.5f - 0.7f, cw - bw + 0.4f, 1.4f);
						any = true;
					}
				}
				if (any) {
					nvgFillColor(vg, pass == 0 ? dimC : pass == 1 ? on : pass == 2 ? semi::theme::amber() : semi::theme::text());
					nvgFill(vg);
				}
			}
		}
		if (s.step >= 0 && s.step < steps)
			fillRect(vg, gx0 + s.step * cw + 0.4f, gy1 + 0.4f, bw, 1.8f, semi::theme::amber());

		// pass strip
		const float ty = h - pad;
		char buf[48];
		float x = pad;
		for (int p = 0; p < std::min(s.passes, 8); p++) {
			const bool cur = p == s.passIdx;
			if (cur && s.pass.letter)
				std::snprintf(buf, sizeof buf, "%c", s.pass.letter);
			else
				std::snprintf(buf, sizeof buf, "%d", p + 1);
			fillRect(vg, x, ty - stripH + 2.f, 9.f, stripH - 2.f, cur ? semi::theme::amber() : semi::theme::cell());
			text(vg, x + 4.5f, ty, buf, 8.f, cur ? semi::theme::screen() : semi::theme::dim(), NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
			x += 11.f;
		}
		NVGcolor sc = semi::theme::dim();
		if (!s.running)
			std::snprintf(buf, sizeof buf, "NO CLOCK");
		else if (!s.fillOpen)
			std::snprintf(buf, sizeof buf, "FILL WAIT");
		else if (s.queued) {
			std::snprintf(buf, sizeof buf, "NEW @PHRASE");
			sc = semi::theme::amber();
		}
		else {
			std::snprintf(buf, sizeof buf, "%s %s %d", crg::kKeys[(size_t) clamp(s.key, 0, 11)].c_str(), crg::kRates[(size_t) clamp(s.rate, 0, 5)].c_str(),
			              (int) std::lround(s.bpm));
			sc = semi::theme::cyan();
		}
		text(vg, w - pad, ty, buf, fs, sc, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
	}
};

// Two lines of names: style / motif, answer / mode (the four selector knobs).
struct NamesScreen : Screen {
	CallResponse* module = nullptr;
	Snapshot demo;

	NamesScreen() {
		const crg::Params d = crg::Params::defaults();
		demo.style = d.style;
		demo.motif = d.motifType;
		demo.answer = d.responseStrategy;
		demo.mode = d.phraseMode;
	}

	static std::string fit(std::string s, size_t n) {
		if (s.size() > n)
			s.resize(n);
		return s;
	}

	void drawScreen(NVGcontext* vg) override {
		// own snapshot read would steal the PhraseScreen's buffer: read params directly instead
		Snapshot s = demo;
		if (module) {
			s.style = (int) module->params[CallResponse::STYLE_PARAM].getValue();
			s.motif = (int) module->params[CallResponse::MOTIF_PARAM].getValue();
			s.answer = (int) module->params[CallResponse::ANSWER_PARAM].getValue();
			s.mode = (int) module->params[CallResponse::MODE_PARAM].getValue();
		}
		const float w = box.size.x, h = box.size.y;
		const float pad = 2.5f, fs = std::max(9.f, semi::platform::kMinText + 1.f);
		const size_t half = (size_t) std::max(8.f, (w * 0.5f - pad) / (fs * 0.54f)) - 1;
		auto at = [](const std::vector<std::string>& l, int i) { return l[(size_t) clamp(i, 0, (int) l.size() - 1)]; };
		text(vg, pad, pad, fit(at(crg::kStyles, s.style), half), fs, semi::theme::cyan());
		text(vg, w * 0.5f + pad, pad, fit(at(crg::kMotifs, s.motif), half), fs, semi::theme::text());
		text(vg, pad, h - pad, fit(at(crg::kStrategies, s.answer), half), fs, semi::theme::magenta(), NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
		text(vg, w * 0.5f + pad, h - pad, fit(at(crg::kModes, s.mode), half), fs, semi::theme::text(), NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
	}
};

struct CallResponseWidget : ModuleWidget {
	CallResponseWidget(CallResponse* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/CallResponse.svg")));

		auto* ps = semi::createScreenMm<PhraseScreen>(Vec(L::PHRASE.x, L::PHRASE.y), Vec(L::PHRASE.w, L::PHRASE.h));
		ps->module = module;
		addChild(ps);
		auto* ns = semi::createScreenMm<NamesScreen>(Vec(L::NAMES.x, L::NAMES.y), Vec(L::NAMES.w, L::NAMES.h));
		ns->module = module;
		addChild(ns);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		auto sel = [&](L::P p, int id) {
			auto* k = createParamCentered<semi::KnobM>(at(p), module, id);
			k->snap = true;
			addParam(k);
		};
		sel(L::STYLE, CallResponse::STYLE_PARAM);
		sel(L::MOTIF, CallResponse::MOTIF_PARAM);
		sel(L::ANSWER, CallResponse::ANSWER_PARAM);
		sel(L::MODE, CallResponse::MODE_PARAM);
		addParam(createParamCentered<semi::KnobM>(at(L::DENSITY), module, CallResponse::DENSITY_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::CONTRAST), module, CallResponse::CONTRAST_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::OCTJUMP), module, CallResponse::OCTJUMP_PARAM));
		addParam(createParamCentered<semi::KnobMHot>(at(L::DRIFT), module, CallResponse::DRIFT_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::NEW), module, CallResponse::NEW_PARAM));
		addChild(createLightCentered<semi::AmberLedSmall>(at(L::NEW_LED), module, CallResponse::NEW_LIGHT));
		addParam(createParamCentered<semi::Button>(at(L::MUTATE), module, CallResponse::MUTATE_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::DICE), module, CallResponse::DICE_PARAM));

		addInput(createInputCentered<semi::Jack>(at(L::CLK), module, CallResponse::CLK_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RST), module, CallResponse::RST_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::FILL), module, CallResponse::FILL_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::VOCT_IN), module, CallResponse::VOCT_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::NEW_IN), module, CallResponse::NEW_INPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::C_VOCT), module, CallResponse::C_VOCT_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::C_GATE), module, CallResponse::C_GATE_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::C_VEL), module, CallResponse::C_VEL_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::R_VOCT), module, CallResponse::R_VOCT_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::R_GATE), module, CallResponse::R_GATE_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::R_VEL), module, CallResponse::R_VEL_OUTPUT));
	}

#ifndef METAMODULE
	void step() override {
		if (auto* m = dynamic_cast<CallResponse*>(module))
			m->service();
		ModuleWidget::step();
	}
#endif

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<CallResponse*>(module);
		if (!m)
			return;
		crg::Params* p = &m->menu;
		auto idx = [=](const char* name, const std::vector<std::string>& labels, int* v) {
			return createIndexSubmenuItem(name, labels, [=]() { return (size_t) *v; }, [=](size_t i) { *v = (int) i; });
		};
		auto numMenu = [=](const char* name, double* v, double lo, double hi, double stepv, const char* unit) {
			std::vector<std::string> labels;
			for (double x = lo; x <= hi + 1e-9; x += stepv)
				labels.push_back(std::to_string((int) std::lround(x)) + unit);
			return createIndexSubmenuItem(name, labels, [=]() { return (size_t) clamp((int) std::lround((*v - lo) / stepv), 0, (int) labels.size() - 1); },
			                              [=](size_t i) { *v = lo + i * stepv; });
		};
		auto cmd = [=](const char* name, int c) { return createMenuItem(name, "", [=]() { m->cmd = c; }); };

		menu->addChild(new MenuSeparator);
		std::vector<std::string> presets;
		for (auto& f : factories())
			presets.push_back(f.name);
		menu->addChild(createSubmenuItem("Preset", "", [=](Menu* sub) {
			for (size_t i = 0; i < presets.size(); i++)
				sub->addChild(createMenuItem(presets[i], "", [=]() { m->cmd = CallResponse::CMD_PRESET + (int) i; }));
		}));
		menu->addChild(createSubmenuItem("Actions", "", [=](Menu* sub) {
			sub->addChild(cmd("New call", CallResponse::CMD_NEWCALL));
			sub->addChild(cmd("New response", CallResponse::CMD_NEWRESP));
			sub->addChild(cmd("Mutate call", CallResponse::CMD_MUTCALL));
			sub->addChild(cmd("Mutate response", CallResponse::CMD_MUTRESP));
			sub->addChild(cmd("Keep rhythm, new pitches", CallResponse::CMD_KEEPRHYTHM));
			sub->addChild(cmd("Keep pitches, new rhythm", CallResponse::CMD_KEEPPITCH));
			sub->addChild(cmd("Keep motif", CallResponse::CMD_KEEPMOTIF));
			sub->addChild(cmd("Shift left", CallResponse::CMD_SHIFTL));
			sub->addChild(cmd("Shift right", CallResponse::CMD_SHIFTR));
			sub->addChild(cmd("Undo", CallResponse::CMD_UNDO));
		}));

		std::vector<std::string> steps;
		for (int i = crg::kMinSteps; i <= crg::kMaxSteps; i++)
			steps.push_back(std::to_string(i));
		menu->addChild(createMenuLabel("Phrase"));
		menu->addChild(idx("Key", crg::kKeys, &p->key));
		menu->addChild(idx("Scale", crg::kScales, &p->scale));
		menu->addChild(idx("Root octave", crg::kOctaves, &p->rootOctave));
		menu->addChild(createIndexSubmenuItem("Steps", steps, [=]() { return (size_t) (p->steps - crg::kMinSteps); },
		                                      [=](size_t i) { p->steps = (int) i + crg::kMinSteps; }));
		menu->addChild(idx("Rate", crg::kRates, &p->rate));
		menu->addChild(numMenu("Swing", &p->swing, 0, 75, 5, " %"));
		menu->addChild(idx("Ending", crg::kEndings, &p->endingBehaviour));
		menu->addChild(numMenu("Resolve", &p->resolve, 0, 100, 10, " %"));

		menu->addChild(createSubmenuItem("Shape", "", [=](Menu* sub) {
			sub->addChild(idx("Register", crg::kRegisters, &p->registerProfile));
			sub->addChild(idx("Contour", crg::kContours, &p->contourPreset));
			sub->addChild(idx("Repetition", crg::kRepetitions, &p->repetitionPreset));
			sub->addChild(idx("Note pool", crg::kNotePools, &p->notePoolMode));
		}));
		menu->addChild(createSubmenuItem("Articulation", "", [=](Menu* sub) {
			sub->addChild(idx("Profile", crg::kArticulation, &p->articulationProfile));
			sub->addChild(idx("Gate", crg::kGateFeels, &p->gateFeel));
			sub->addChild(idx("Join", crg::kConnections, &p->connectionFeel));
			sub->addChild(idx("Slide", crg::kSlides, &p->slideProfile));
			sub->addChild(idx("Accent", crg::kAccents, &p->accentProfile));
			sub->addChild(numMenu("Note length", &p->noteLengthPct, 40, 200, 10, " %"));
			sub->addChild(numMenu("Base velocity", &p->baseVelocity, 7, 127, 10, ""));
			sub->addChild(numMenu("Accent depth", &p->accentDepth, 0, 100, 5, " %"));
		}));
		menu->addChild(createSubmenuItem("Mutation", "", [=](Menu* sub) {
			sub->addChild(idx("Mutate amount", crg::kMutationAmounts, &p->mutationAmount));
			sub->addChild(idx("Mutate target", crg::kMutationTargets, &p->mutationTarget));
			sub->addChild(idx("New pair timing", crg::kPairTimings, &p->newPairTiming));
		}));
		menu->addChild(createMenuLabel("Run"));
		menu->addChild(idx("Sync", {"Restart", "Timeline (from the clock count)"}, &p->syncMode));
		menu->addChild(idx("Run mode (FILL)", kRunModes, &m->runMode));
		menu->addChild(idx("Trigger length", kTrigLens, &m->trigLenIdx));
		menu->addChild(idx("Fill start", kFillStart, &m->fillStart));
		menu->addChild(idx("CLK resolution", kClockNames, &m->clockIdx));
	}
};

Model* modelCallResponse = createModel<CallResponse, CallResponseWidget>("CallResponse");
