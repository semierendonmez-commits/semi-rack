// DUBPLATE — Rack / MetaModule port of DUBPLATE, the genre arrangement builder (engine:
// engines/dubplate, the plugin's model + generators ported JUCE-free, and Sequencer, the
// step logic of its ArrangementEngine without the audio).
//
// What it is here: the ARRANGEMENT core as a sequencer. A genre template (jungle, DnB, UK
// garage, speed garage) gives sections (intro / build / drop / breakdown / outro, each with
// an energy and phrase-locked FX events) and role lanes (16-step patterns, on / off per
// section). The module walks it on CLK and fires one gate per role group:
//   KICK (kick, kick sub, punch)  SNR (snare, clap)  HAT (closed, shuffle, ride)  OHAT
//   BRK (break, break chop; SLICE = the slice it plays, 0..15 -> 0..9.4 V)  PERC  FX (riser,
//   impact)  SUB  BASS (bass, reese, organ)  BS2 (bass 2 variation)  MUSIC (stab, vox, pad)
//   V/OCT = pitch of the last bass-bus note (root from the menu + the pattern's steps)
//   EVT = gate while a phrase-locked FX event runs (sweep, rewind, tape stop, throws ...)
//   CLK = the 16th clock it runs on; BUS = FamilyBus: POS (bar in the arrangement),
//   ENERGY (section energy x ENERGY), SCENE (section), EVENT (phrase / section lines).
// Fills, variations (BASS 2, CHOP), drop-outs, snare rolls, half-time and swing act on
// the gates exactly where the plugin acts on its samples.
//
// Clock: CLK = 1/16 (4 PPQN, menu for others); the first clock after RST is bar 1.
// Without CLK the module runs on TEMPO (set to the genre's tempo when a template loads).
// RUN (pad / trigger) starts and stops. NEXT jumps to the next section on the next
// 4-bar line (menu), FILL forces a fill on the next bar, MUT mutates.
//
// Dropped from the plugin (no audio in this module): sample lanes and their analysis,
// the synth kit, lane strips / buses / sidechain / master chain, the event FX audio,
// recording, export (WAV / stems / MIDI / markers), key detection. DAW-in-a-module is
// out of scope; patch samplers / drums to the gates instead.
//
// Threads: every edit (template, form, VIP, mutate, generators, menu settings) is applied
// to a worker-owned copy of the arrangement (Rack: widget step() on the UI thread,
// MetaModule: an AsyncThread) and handed to process() through a semi::Slot, so
// process() never allocates.

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/FamilyBusCV.hpp"
#include "../common/Sample.hpp"   // semi::Slot
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/DubplateLayout.hpp"
#include "dubplate/GenreTemplates.h"
#include "dubplate/PatternGenerator.h"
#include "dubplate/Sequencer.h"

#include <cctype>

#ifdef METAMODULE
#include "threads/async_thread.hh"
#endif

namespace L = semi::layout::dubplate;
namespace bus = semi::bus;
namespace dp = ::dubplate;
using semi::Screen;

namespace {

enum Gate { G_KICK, G_SNARE, G_HAT, G_OHAT, G_BREAK, G_PERC, G_FX, G_SUB, G_BASS, G_BASS2, G_MUSIC, NUM_GATES };
const char* const kGateNames[NUM_GATES] = {"Kick (kick, kick sub, punch)", "Snare (snare, clap)", "Hat (closed, shuffle, ride)",
                                           "Open hat", "Break (break, break chop)", "Perc", "FX (riser, impact)", "Sub",
                                           "Bass (bass, reese, organ bass)", "Bass 2 (variation)", "Music (stab, vocal chop, pad)"};

int gateOf(dp::Role r) {
	using R = dp::Role;
	switch (r) {
		case R::Kick: case R::KickSub: case R::Punch: return G_KICK;
		case R::Snare: case R::Clap: return G_SNARE;
		case R::HatClosed: case R::ShuffleHat: case R::Ride: return G_HAT;
		case R::HatOpen: return G_OHAT;
		case R::Break: case R::BreakChop: return G_BREAK;
		case R::Perc: return G_PERC;
		case R::Riser: case R::Impact: return G_FX;
		case R::Sub: return G_SUB;
		case R::Bass: case R::Reese: case R::OrganBass: return G_BASS;
		case R::Bass2: return G_BASS2;
		default: return G_MUSIC;
	}
}

constexpr int kMaxSecView = 24;
constexpr int kMaxLaneView = 14;
constexpr int kMaxPending = 96;

struct SecView {
	uint8_t type = 0;
	int16_t bars = 16;
	float energy = 0.5f;
	uint32_t events = 0;
	char name[12] = {};
};

struct LaneView {
	uint8_t role = 0;
	uint32_t cells = 0;   // bit per section (first 32)
};

struct Snapshot {
	int genre = 0;
	char name[28] = {};
	float bpm = 165.f;
	int mode = 0;          // 0 stopped, 1 internal, 2 external clock
	int numSec = 0, numLanes = 0, totalBars = 1;
	SecView sec[kMaxSecView];
	LaneView lane[kMaxLaneView];
	int curSection = -1;
	float posBars = -1.f;  // arrangement bars, -1 = not started
	bool fill = false;
	int eventBit = -1;
	float eventProg = 0.f;
	uint16_t hitMask = 0;
	uint16_t activeMask = 0;
	int pendingJump = -1;
	float energy = 0.5f;
};

const std::vector<std::string> kGenreNames = {"Jungle", "Drum & Bass", "UK Garage", "Speed Garage"};
const std::vector<std::string> kFillProfiles = {"Amen edit (reverse, stutter, pitch)", "Snare roll", "Hat skip / kick shuffle", "Bass / organ variation"};
const std::vector<std::string> kFillEvery = {"4 bars", "8 bars", "16 bars", "32 bars"};
const int kFillEveryBars[] = {4, 8, 16, 32};
const std::vector<std::string> kFillProb = {"0%", "10%", "20%", "35%", "50%", "65%", "80%", "100%"};
const float kFillProbVal[] = {0.f, 0.1f, 0.2f, 0.35f, 0.5f, 0.65f, 0.8f, 1.f};
const std::vector<std::string> kAltNames = {"Every 8 bars A/B", "Drop 2 only", "Random", "Manual (always)"};
const std::vector<std::string> kQuantNames = {"1 bar", "2 bars", "4 bars", "8 bars", "16 bars"};
const int kQuantBars[] = {1, 2, 4, 8, 16};
const std::vector<std::string> kGateLenNames = {"Trigger (2 ms)", "Half step", "Full step"};
const std::vector<std::string> kPpqnNames = {"1 (quarter notes)", "2 (8ths)", "4 (16ths)", "8", "24 (MIDI clock)"};
const int kPpqn[] = {1, 2, 4, 8, 24};
const std::vector<std::string> kRootNames = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const std::vector<std::string> kMutateNames = {"Light (25%)", "Medium (50%)", "Heavy (100%)"};
const float kMutateAmt[] = {0.25f, 0.5f, 1.f};

// ---- JSON for the arrangement (the plugin's toVar / fromVar fields, minus samples)
json_t* arrToJson(const dp::Arrangement& a) {
	json_t* r = json_object();
	json_object_set_new(r, "name", json_string(a.name.c_str()));
	json_object_set_new(r, "genre", json_integer((int) a.genre));
	json_object_set_new(r, "bpm", json_real(a.bpm));
	json_object_set_new(r, "swing", json_real(a.swing));
	json_object_set_new(r, "phraseBars", json_integer(a.phraseBars));
	json_object_set_new(r, "seed", json_integer((json_int_t) a.seed));
	json_object_set_new(r, "eventLength", json_real(a.eventLength));
	json_object_set_new(r, "nextUid", json_integer(a.nextUid));
	json_t* f = json_object();
	json_object_set_new(f, "profile", json_integer(a.fill.profile));
	json_object_set_new(f, "probability", json_real(a.fill.probability));
	json_object_set_new(f, "every", json_integer(a.fill.every));
	json_object_set_new(f, "density", json_real(a.fill.density));
	std::string g;
	for (auto& row : a.fill.grid)
		for (bool b : row)
			g += b ? '1' : '0';
	json_object_set_new(f, "grid", json_string(g.c_str()));
	json_object_set_new(r, "fill", f);
	json_t* secs = json_array();
	for (auto& s : a.sections) {
		json_t* o = json_object();
		json_object_set_new(o, "name", json_string(s.name.c_str()));
		json_object_set_new(o, "type", json_integer((int) s.type));
		json_object_set_new(o, "bars", json_integer(s.bars));
		json_object_set_new(o, "energy", json_real(s.energy));
		json_object_set_new(o, "events", json_integer(s.events));
		json_array_append_new(secs, o);
	}
	json_object_set_new(r, "sections", secs);
	json_t* lanes = json_array();
	for (auto& l : a.lanes) {
		json_t* o = json_object();
		json_object_set_new(o, "uid", json_integer(l.uid));
		json_object_set_new(o, "role", json_integer((int) l.role));
		char pat[33], pit[64];
		std::string pitches;
		for (int i = 0; i < 16; i++) {
			std::snprintf(pat + i * 2, 3, "%02x", l.pattern[(size_t) i]);
			std::snprintf(pit, sizeof pit, "%d,", (int) l.stepPitch[(size_t) i]);
			pitches += pit;
		}
		json_object_set_new(o, "pattern", json_string(pat));
		json_object_set_new(o, "stepPitch", json_string(pitches.c_str()));
		json_object_set_new(o, "variationOf", json_integer(l.variationOf));
		json_object_set_new(o, "alt", json_integer((int) l.alt));
		std::string cells;
		for (auto& c : l.cells) {
			std::snprintf(pit, sizeof pit, "%d,", c.on ? (int) std::lround(c.level * 100.f) : 0);
			cells += pit;
		}
		json_object_set_new(o, "cells", json_string(cells.c_str()));
		json_object_set_new(o, "pitch", json_real(l.pitch));
		json_object_set_new(o, "mute", json_boolean(l.mute));
		json_object_set_new(o, "solo", json_boolean(l.solo));
		json_object_set_new(o, "locked", json_boolean(l.locked));
		json_array_append_new(lanes, o);
	}
	json_object_set_new(r, "lanes", lanes);
	return r;
}

bool arrFromJson(json_t* r, dp::Arrangement& a) {
	if (!json_is_object(r))
		return false;
	auto num = [](json_t* o, const char* k, double def) {
		json_t* j = json_object_get(o, k);
		return j && json_is_number(j) ? json_number_value(j) : def;
	};
	auto str = [](json_t* o, const char* k) {
		json_t* j = json_object_get(o, k);
		return std::string(j && json_is_string(j) ? json_string_value(j) : "");
	};
	auto boo = [](json_t* o, const char* k) {
		json_t* j = json_object_get(o, k);
		return j && json_is_true(j);
	};
	dp::Arrangement x;
	x.name = str(r, "name");
	x.genre = (dp::Genre) clamp((int) num(r, "genre", 0), 0, (int) dp::Genre::numGenres - 1);
	x.bpm = num(r, "bpm", 165.0);
	x.swing = (float) num(r, "swing", 50.0);
	x.phraseBars = (int) num(r, "phraseBars", 16);
	x.seed = (uint32_t) num(r, "seed", 2417);
	x.eventLength = (float) num(r, "eventLength", 1.0);
	x.nextUid = (int) num(r, "nextUid", 1);
	if (json_t* f = json_object_get(r, "fill")) {
		x.fill.profile = clamp((int) num(f, "profile", 0), 0, 3);
		x.fill.probability = (float) num(f, "probability", 0.35);
		x.fill.every = std::max(1, (int) num(f, "every", 8));
		x.fill.density = (float) num(f, "density", 0.6);
		const std::string g = str(f, "grid");
		for (int i = 0; i < 64 && i < (int) g.size(); i++)
			x.fill.grid[(size_t) (i / 16)][(size_t) (i % 16)] = g[(size_t) i] == '1';
	}
	json_t* secs = json_object_get(r, "sections");
	size_t i;
	json_t* o;
	json_array_foreach(secs, i, o) {
		dp::Section s;
		s.name = str(o, "name");
		s.type = (dp::SectionType) clamp((int) num(o, "type", 2), 0, (int) dp::SectionType::numTypes - 1);
		s.bars = clamp((int) num(o, "bars", 16), 1, 256);
		s.energy = (float) num(o, "energy", 0.5);
		s.events = (uint32_t) num(o, "events", 0);
		x.sections.push_back(s);
	}
	json_t* lanes = json_object_get(r, "lanes");
	json_array_foreach(lanes, i, o) {
		dp::Lane l;
		l.uid = (int) num(o, "uid", x.nextUid);
		l.role = dp::roleFromIndex((int) num(o, "role", (int) dp::Role::Perc));
		l.bus = dp::roleInfo(l.role).bus;
		const std::string pat = str(o, "pattern");
		for (int k = 0; k < 16 && k * 2 + 1 < (int) pat.size(); k++)
			l.pattern[(size_t) k] = (uint8_t) std::strtol(pat.substr((size_t) k * 2, 2).c_str(), nullptr, 16);
		const std::string pit = str(o, "stepPitch");
		const char* p = pit.c_str();
		for (int k = 0; k < 16 && *p; k++) {
			char* end = nullptr;
			l.stepPitch[(size_t) k] = (int8_t) clamp((int) std::strtol(p, &end, 10), -48, 48);
			p = (end && *end == ',') ? end + 1 : (end ? end : p + 1);
		}
		l.variationOf = (int) num(o, "variationOf", -1);
		l.alt = (dp::AltRule) clamp((int) num(o, "alt", 0), 0, 3);
		const std::string cells = str(o, "cells");
		const char* c = cells.c_str();
		while (*c) {
			char* end = nullptr;
			const long v = std::strtol(c, &end, 10);
			if (end == c)
				break;
			dp::Cell cell;
			cell.on = v > 0;
			cell.level = cell.on ? v / 100.f : 1.f;
			l.cells.push_back(cell);
			c = *end == ',' ? end + 1 : end;
		}
		l.pitch = (float) num(o, "pitch", 0.0);
		l.mute = boo(o, "mute");
		l.solo = boo(o, "solo");
		l.locked = boo(o, "locked");
		x.nextUid = std::max(x.nextUid, l.uid + 1);
		x.lanes.push_back(std::move(l));
	}
	if (x.sections.empty())
		return false;
	x.resizeCells();
	a = std::move(x);
	return true;
}

} // namespace

struct Dubplate : Module {
	enum ParamId { GENRE_PARAM, FORM_PARAM, VIP_PARAM, MUTATE_PARAM, ENERGY_PARAM, CHAOS_PARAM, FILL_PARAM, SWING_PARAM, TEMPO_PARAM, RUN_PARAM, PARAMS_LEN };
	enum InputId { CLK_INPUT, RST_INPUT, RUN_INPUT, NEXT_INPUT, MUT_INPUT, FILL_INPUT, NRG_INPUT, CHS_INPUT, INPUTS_LEN };
	enum OutputId { ENUMS(GATE_OUTPUT, NUM_GATES), SLICE_OUTPUT, VOCT_OUTPUT, EVT_OUTPUT, CLK_OUTPUT, BUS_OUTPUT, OUTPUTS_LEN };
	enum LightId { ENUMS(GENRE_LIGHT, 4), RUN_LIGHT, CLK_LIGHT, LIGHTS_LEN };

	// ---- worker side (edits) ------------------------------------------------------
	enum Cmd : uint32_t {
		C_GENRE = 1 << 0, C_FORM = 1 << 1, C_VIP = 1 << 2, C_MUTATE = 1 << 3, C_DRUMS = 1 << 4, C_BASS = 1 << 5,
		C_FILLPROF = 1 << 6, C_FILLEVERY = 1 << 7, C_FILLPROB = 1 << 8, C_ALT = 1 << 9, C_BLANKFORM = 1 << 10,
	};
	std::atomic<uint32_t> cmds{0};
	std::atomic<int> wantGenre{0}, wantFillProf{0}, wantFillEvery{1}, wantFillProb{3}, wantAlt{0};
	std::atomic_flag editBusy = ATOMIC_FLAG_INIT;
	dp::Arrangement edit;                // worker-owned
	std::atomic<int> genreShown{0};
	std::atomic<int> fillProfShown{0}, fillEveryShown{1}, fillProbShown{3}, altShown{0};
	semi::Slot<dp::Arrangement> slot;

	// ---- audio side ---------------------------------------------------------------------
	dp::Sequencer seq;
	dp::Macros macros;
	dp::Hit hits[dp::Sequencer::maxHits];
	struct Pending { float left; int gate; float vel, semis; int slice; int lane; };
	Pending pend[kMaxPending];
	int numPend = 0;

	// options (menu, saved)
	bool loopArr = true, velGates = false;
	int quant = 2, gateLen = 1, ppqnIdx = 2, root = 0, mutateIdx = 1;
	std::atomic<int> jumpRequest{-1};    // menu: section to jump to

	bool running = true;
	bool seqLoop = true;
	long long absStep = -1;
	double phase = 1.0;                  // internal clock: 16ths, >= 1 = step due
	dsp::SchmittTrigger clkTrig, rstTrig, runTrig, nextTrig, mutTrig, fillTrig;
	dsp::BooleanTrigger genreBtn, formBtn, vipBtn, mutateBtn, runBtn;
	float sinceClk = 0.f, clkPeriod = 0.f;   // seconds
	int clkCount = 0;
	float sinceStep = 0.f, stepPeriod = 0.1f;  // seconds
	int subStepsLeft = 0;                // slow clocks (PPQN < 4): steps still to make
	float subStepTimer = 0.f;

	float gateLeft[NUM_GATES] = {}, gapLeft[NUM_GATES] = {}, gateVel[NUM_GATES] = {};
	float sliceV = 0.f, voct = 0.f;
	dsp::PulseGenerator clkOut, eventPulse;
	float eventVolts = 0.f;
	float laneFlash[dp::Sequencer::maxLanes] = {};
	int evtCounter = 0;
	dp::EventState evNow;

	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;

#ifdef METAMODULE
	MetaModule::AsyncThread worker{this, [this]() { serviceEdits(); }};
#endif

	Dubplate() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configButton(GENRE_PARAM, "Genre template (next)");
		configButton(FORM_PARAM, "New song form");
		configButton(VIP_PARAM, "VIP form (alternative arrangement)");
		configButton(MUTATE_PARAM, "Mutate");
		configParam(ENERGY_PARAM, 0.f, 1.f, 0.5f, "Energy macro (below 50% mutes the upper layers)", " %", 0.f, 100.f);
		configParam(CHAOS_PARAM, 0.f, 1.f, 0.3f, "Chaos (fill probability)", " %", 0.f, 100.f);
		configParam(FILL_PARAM, 0.f, 1.f, 0.35f, "Fill density", " %", 0.f, 100.f);
		configParam(SWING_PARAM, 50.f, 75.f, 51.f, "Swing", " %");
		configParam(TEMPO_PARAM, 60.f, 200.f, 165.f, "Tempo (internal clock)", " BPM");
		configButton(RUN_PARAM, "Run / stop");
		configInput(CLK_INPUT, "Clock (16ths by default)");
		configInput(RST_INPUT, "Reset (next clock = bar 1)");
		configInput(RUN_INPUT, "Run / stop trigger");
		configInput(NEXT_INPUT, "Next section (quantised)");
		configInput(MUT_INPUT, "Mutate trigger");
		configInput(FILL_INPUT, "Fill on the next bar");
		configInput(NRG_INPUT, "Energy CV (10%/V)");
		configInput(CHS_INPUT, "Chaos CV (10%/V)");
		for (int g = 0; g < NUM_GATES; g++)
			configOutput(GATE_OUTPUT + g, kGateNames[g]);
		configOutput(SLICE_OUTPUT, "Break slice (0..15 -> 0..9.4 V)");
		configOutput(VOCT_OUTPUT, "Bass V/OCT (last sub / bass note)");
		configOutput(EVT_OUTPUT, "FX event gate");
		configOutput(CLK_OUTPUT, "16th clock");
		configOutput(BUS_OUTPUT, "FamilyBus (POS, ENERGY, SCENE, EVENT)");
		configLight(RUN_LIGHT, "Running");
		configLight(CLK_LIGHT, "External clock");
		for (int g = 0; g < 4; g++)
			configLight(GENRE_LIGHT + g, kGenreNames[(size_t) g]);

		edit = dp::GenreTemplates::makeTemplate(dp::Genre::Jungle);
		params[TEMPO_PARAM].setValue((float) edit.bpm);
		params[SWING_PARAM].setValue(edit.swing);
		publishEdit();
#ifdef METAMODULE
		worker.start();
#endif
	}

	// ---- worker ---------------------------------------------------------------------------
	void lockEdit() {
		while (editBusy.test_and_set(std::memory_order_acquire)) {
		}
	}
	void unlockEdit() { editBusy.clear(std::memory_order_release); }

	// caller holds editBusy
	void publishEdit() {
		genreShown = (int) edit.genre;
		fillProfShown = edit.fill.profile;
		int e = 1;
		for (int i = 0; i < 4; i++)
			if (kFillEveryBars[i] == edit.fill.every)
				e = i;
		fillEveryShown = e;
		int p = 0;
		for (int i = 0; i < 8; i++)
			if (std::fabs(kFillProbVal[i] - edit.fill.probability) < std::fabs(kFillProbVal[p] - edit.fill.probability))
				p = i;
		fillProbShown = p;
		for (auto& l : edit.lanes)
			if (l.variationOf >= 0)
				altShown = (int) l.alt;
		slot.offer(new dp::Arrangement(edit));
	}

	static uint32_t nextSeed(uint32_t s) { return s * 1664525u + 1013904223u; }

	// Rack: the widget's step() (UI thread). MetaModule: the AsyncThread.
	void serviceEdits() {
		slot.collect();
		if (!cmds.load(std::memory_order_acquire))
			return;
		if (editBusy.test_and_set(std::memory_order_acquire))
			return;
		const uint32_t c = cmds.exchange(0, std::memory_order_acq_rel);
		if (c & C_GENRE) {
			const int g = clamp(wantGenre.load(), 0, 3);
			edit = dp::GenreTemplates::makeTemplate((dp::Genre) g);
			params[TEMPO_PARAM].setValue((float) edit.bpm);
			params[SWING_PARAM].setValue(edit.swing);
		}
		if (c & C_FORM) {
			edit.seed = nextSeed(edit.seed);
			dp::PatternGenerator::generateForm(edit, edit.seed, false);
		}
		if (c & C_VIP) {
			edit.seed = nextSeed(edit.seed);
			dp::PatternGenerator::generateForm(edit, edit.seed, true);
		}
		if (c & C_MUTATE) {
			edit.seed = nextSeed(edit.seed);
			dp::PatternGenerator::mutate(edit, kMutateAmt[clamp(mutateIdx, 0, 2)], edit.seed);
		}
		if (c & C_DRUMS) {
			edit.seed = nextSeed(edit.seed);
			dp::PatternGenerator::generateDrums(edit, 0.5f + 0.5f * params[CHAOS_PARAM].getValue(), edit.seed);
		}
		if (c & C_BASS) {
			edit.seed = nextSeed(edit.seed);
			dp::PatternGenerator::generateBassline(edit, 0.5f + 0.5f * params[CHAOS_PARAM].getValue(), edit.seed);
		}
		if (c & C_FILLPROF) {
			edit.fill.profile = clamp(wantFillProf.load(), 0, 3);
			edit.fill.grid = dp::PatternGenerator::defaultFillGrid(edit.fill.profile);
		}
		if (c & C_FILLEVERY)
			edit.fill.every = kFillEveryBars[clamp(wantFillEvery.load(), 0, 3)];
		if (c & C_FILLPROB)
			edit.fill.probability = kFillProbVal[clamp(wantFillProb.load(), 0, 7)];
		if (c & C_ALT)
			for (auto& l : edit.lanes)
				if (l.variationOf >= 0)
					l.alt = (dp::AltRule) clamp(wantAlt.load(), 0, 3);
		publishEdit();
		unlockEdit();
	}

	void command(uint32_t c) { cmds.fetch_or(c, std::memory_order_acq_rel); }

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		loopArr = true;
		velGates = false;
		quant = 2;
		gateLen = 1;
		ppqnIdx = 2;
		root = 0;
		mutateIdx = 1;
		running = true;
		absStep = -1;
		wantGenre = 0;
		command(C_GENRE);
	}

	json_t* dataToJson() override {
		json_t* root_ = json_object();
		lockEdit();
		json_object_set_new(root_, "arrangement", arrToJson(edit));
		unlockEdit();
		json_object_set_new(root_, "loop", json_boolean(loopArr));
		json_object_set_new(root_, "velGates", json_boolean(velGates));
		json_object_set_new(root_, "quant", json_integer(quant));
		json_object_set_new(root_, "gateLen", json_integer(gateLen));
		json_object_set_new(root_, "ppqn", json_integer(ppqnIdx));
		json_object_set_new(root_, "root", json_integer(root));
		json_object_set_new(root_, "mutate", json_integer(mutateIdx));
		json_object_set_new(root_, "running", json_boolean(running));
		return root_;
	}

	void dataFromJson(json_t* r) override {
		auto geti = [&](const char* k, int& v, int lo, int hi) {
			if (json_t* j = json_object_get(r, k))
				v = clamp((int) json_integer_value(j), lo, hi);
		};
		if (json_t* j = json_object_get(r, "loop"))
			loopArr = json_boolean_value(j);
		if (json_t* j = json_object_get(r, "velGates"))
			velGates = json_boolean_value(j);
		if (json_t* j = json_object_get(r, "running"))
			running = json_boolean_value(j);
		geti("quant", quant, 0, 4);
		geti("gateLen", gateLen, 0, 2);
		geti("ppqn", ppqnIdx, 0, 4);
		geti("root", root, 0, 11);
		geti("mutate", mutateIdx, 0, 2);
		if (json_t* a = json_object_get(r, "arrangement")) {
			lockEdit();
			dp::Arrangement x;
			if (arrFromJson(a, x)) {
				edit = std::move(x);
				publishEdit();
			}
			unlockEdit();
		}
	}

	// ---- audio ---------------------------------------------------------------------------
	float gateSeconds() const {
		switch (gateLen) {
			case 0: return 0.002f;
			case 2: return std::max(0.002f, stepPeriod * 0.95f);
			default: return std::max(0.002f, stepPeriod * 0.5f);
		}
	}

	void fire(const Pending& p) {
		const int g = p.gate;
		if (gateLeft[g] > 0.f) {
			gapLeft[g] = 0.001f;
			gateLeft[g] = 0.001f + gateSeconds();
		}
		else {
			gapLeft[g] = 0.f;
			gateLeft[g] = gateSeconds();
		}
		gateVel[g] = p.vel;
		if (p.slice >= 0)
			sliceV = p.slice * 10.f / 16.f;
		if (g == G_SUB || g == G_BASS || g == G_BASS2)
			voct = (root + p.semis) / 12.f;
		if (p.lane >= 0 && p.lane < dp::Sequencer::maxLanes)
			laneFlash[p.lane] = 0.08f;
	}

	void doStep() {
		const dp::Arrangement* a = seq.arrangement();
		absStep++;
		clkOut.trigger(0.002f);
		sinceStep = 0.f;
		if (!a)
			return;
		const int n = seq.step(absStep, macros, hits, dp::Sequencer::maxHits);
		for (int i = 0; i < n; i++) {
			const dp::Hit& h = hits[i];
			Pending p{h.delay * stepPeriod, gateOf(a->lanes[(size_t) h.lane].role), h.vel, h.semis, h.slice, h.lane};
			if (p.left <= 0.f)
				fire(p);
			else if (numPend < kMaxPending)
				pend[numPend++] = p;
		}
		// FamilyBus EVENT on bar lines: section start 10 V, 16-bar phrase 7.5, 8 -> 5, 4 -> 2.5
		if (absStep % 16 == 0) {
			long long arrStep = -1;
			const int si = seq.sectionAt(absStep, &arrStep);
			if (si >= 0) {
				const int bar = (int) (arrStep / 16);
				const int inSec = bar - seq.timeline().sectionStart(si);
				int line = 0;
				if (inSec == 0)
					line = 32;
				else if (inSec % 16 == 0)
					line = 16;
				else if (inSec % 8 == 0)
					line = 8;
				else if (inSec % 4 == 0)
					line = 4;
				if (line) {
					eventVolts = bus::turnToVolts(line);
					eventPulse.trigger(bus::kPulseSec);
				}
			}
		}
	}

	void process(const ProcessArgs& args) override {
		const float dt = args.sampleTime;
		if (slot.poll() || loopArr != seqLoop) {
			seqLoop = loopArr;
			seq.setArrangement(slot.active, loopArr);
		}

		// buttons and triggers
		if (genreBtn.process(params[GENRE_PARAM].getValue() > 0.f)) {
			wantGenre = (genreShown.load() + 1) % 4;
			command(C_GENRE);
		}
		if (formBtn.process(params[FORM_PARAM].getValue() > 0.f))
			command(C_FORM);
		if (vipBtn.process(params[VIP_PARAM].getValue() > 0.f))
			command(C_VIP);
		if (mutateBtn.process(params[MUTATE_PARAM].getValue() > 0.f) | mutTrig.process(inputs[MUT_INPUT].getVoltage(), 0.1f, 1.f))
			command(C_MUTATE);
		if (runBtn.process(params[RUN_PARAM].getValue() > 0.f) | runTrig.process(inputs[RUN_INPUT].getVoltage(), 0.1f, 1.f))
			running = !running;
		if (nextTrig.process(inputs[NEXT_INPUT].getVoltage(), 0.1f, 1.f) && seq.arrangement()) {
			const int pj = seq.pendingJumpSection();
			const int cur = pj >= 0 ? pj : std::max(0, seq.sectionAt(std::max(0LL, absStep)));
			seq.requestJump((cur + 1) % std::max(1, seq.timeline().numSections), kQuantBars[quant]);
		}
		const int jr = jumpRequest.exchange(-1);
		if (jr >= 0)
			seq.requestJump(jr, kQuantBars[quant]);
		if (fillTrig.process(inputs[FILL_INPUT].getVoltage(), 0.1f, 1.f))
			seq.requestFill(std::max(0LL, absStep));

		macros.energy = clamp(params[ENERGY_PARAM].getValue() + inputs[NRG_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		macros.chaos = clamp(params[CHAOS_PARAM].getValue() + inputs[CHS_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		macros.fill = params[FILL_PARAM].getValue();
		macros.swing = params[SWING_PARAM].getValue();

		// reset
		if (rstTrig.process(inputs[RST_INPUT].getVoltage(), 0.1f, 1.f)) {
			absStep = -1;
			seq.resetPosition();
			phase = 1.0;
			clkCount = 0;
			subStepsLeft = 0;
			numPend = 0;
		}

		// clock
		const bool ext = inputs[CLK_INPUT].isConnected();
		sinceClk += dt;
		sinceStep += dt;
		if (ext) {
			const int ppqn = kPpqn[ppqnIdx];
			if (clkTrig.process(inputs[CLK_INPUT].getVoltage(), 0.1f, 1.f)) {
				if (sinceClk < 4.f)
					clkPeriod = sinceClk;
				sinceClk = 0.f;
				if (running) {
					if (ppqn >= 4) {
						const int per = ppqn / 4;
						if (clkCount % per == 0) {
							if (clkPeriod > 0.f)
								stepPeriod = clkPeriod * per;
							doStep();
						}
						clkCount++;
					}
					else {
						// slow clocks: 4 / ppqn steps per clock, spread over the clock period
						const int per = 4 / ppqn;
						if (clkPeriod > 0.f)
							stepPeriod = clkPeriod / per;
						doStep();
						subStepsLeft = per - 1;
						subStepTimer = 0.f;
					}
				}
			}
			if (subStepsLeft > 0 && running) {
				subStepTimer += dt;
				if (subStepTimer >= stepPeriod) {
					subStepTimer -= stepPeriod;
					subStepsLeft--;
					doStep();
				}
			}
		}
		else if (running) {
			const float bpm = params[TEMPO_PARAM].getValue();
			stepPeriod = 15.f / bpm;
			phase += dt / stepPeriod;
			if (phase >= 1.0) {
				phase -= std::floor(phase);
				doStep();
			}
		}

		// scheduled hits (swing, rolls)
		for (int i = 0; i < numPend;) {
			pend[i].left -= dt;
			if (pend[i].left <= 0.f) {
				fire(pend[i]);
				pend[i] = pend[--numPend];
			}
			else
				i++;
		}

		// gates
		for (int g = 0; g < NUM_GATES; g++) {
			bool on = false;
			if (gateLeft[g] > 0.f) {
				gateLeft[g] -= dt;
				if (gapLeft[g] > 0.f)
					gapLeft[g] -= dt;
				else
					on = true;
			}
			outputs[GATE_OUTPUT + g].setVoltage(on ? (velGates ? 10.f * gateVel[g] : 10.f) : 0.f);
		}
		outputs[SLICE_OUTPUT].setVoltage(sliceV);
		outputs[VOCT_OUTPUT].setVoltage(voct);
		outputs[CLK_OUTPUT].setVoltage(clkOut.process(dt) ? 10.f : 0.f);

		// position, events, bus (events checked every 32 samples)
		const float frac = absStep >= 0 ? clamp(sinceStep / std::max(1e-4f, stepPeriod), 0.f, 0.999f) : 0.f;
		if (++evtCounter >= 32) {
			evtCounter = 0;
			evNow = absStep >= 0 && seq.arrangement() ? seq.eventAt((absStep + frac) / 4.0) : dp::EventState{};
		}
		outputs[EVT_OUTPUT].setVoltage(evNow.bit >= 0 ? 10.f : 0.f);

		long long arrStep = -1;
		const int si = absStep >= 0 ? seq.sectionAt(absStep, &arrStep) : -1;
		const dp::Arrangement* a = seq.arrangement();
		outputs[BUS_OUTPUT].setChannels(bus::kChannels);
		const float bars = arrStep >= 0 ? (arrStep + frac) / 16.f : 0.f;
		outputs[BUS_OUTPUT].setVoltage(bus::posToVolts(bars), bus::kPos);
		const float secEnergy = (si >= 0 && a) ? a->sections[(size_t) si].energy : 0.f;
		outputs[BUS_OUTPUT].setVoltage(clamp(secEnergy * macros.energy * 2.f, 0.f, 1.f) * 10.f, bus::kEnergy);
		outputs[BUS_OUTPUT].setVoltage(bus::sceneToVolts(std::max(0, si)), bus::kScene);
		outputs[BUS_OUTPUT].setVoltage(eventPulse.process(dt) ? eventVolts : 0.f, bus::kEvent);

		for (int l = 0; l < dp::Sequencer::maxLanes; l++)
			if (laneFlash[l] > 0.f)
				laneFlash[l] -= dt;

		if (ui.tick(dt))
			publish(si, bars, ext);
	}

	void publish(int si, float bars, bool ext) {
		const int g = genreShown.load();
		for (int i = 0; i < 4; i++)
			lights[GENRE_LIGHT + i].setBrightness(i == g ? 1.f : 0.f);
		lights[RUN_LIGHT].setBrightness(running ? 1.f : 0.f);
		lights[CLK_LIGHT].setBrightness(ext ? 1.f : 0.f);

		Snapshot& s = snap.writeSlot();
		const dp::Arrangement* a = seq.arrangement();
		s.genre = g;
		s.mode = !running ? 0 : (ext ? 2 : 1);
		s.bpm = ext ? (stepPeriod > 0.f ? 15.f / stepPeriod : 0.f) : params[TEMPO_PARAM].getValue();
		s.curSection = si;
		s.posBars = absStep >= 0 ? bars : -1.f;
		s.eventBit = evNow.bit;
		s.eventProg = evNow.progress;
		s.pendingJump = seq.pendingJumpSection();
		s.energy = macros.energy;
		s.fill = absStep >= 0 && seq.isFillBar(absStep, macros);
		if (a) {
			std::snprintf(s.name, sizeof s.name, "%s", a->name.c_str());
			s.totalBars = a->totalBars();
			s.numSec = std::min((int) a->sections.size(), kMaxSecView);
			for (int i = 0; i < s.numSec; i++) {
				const dp::Section& sec = a->sections[(size_t) i];
				s.sec[i].type = (uint8_t) sec.type;
				s.sec[i].bars = (int16_t) sec.bars;
				s.sec[i].energy = sec.energy;
				s.sec[i].events = sec.events;
				std::snprintf(s.sec[i].name, sizeof s.sec[i].name, "%s", sec.name.c_str());
			}
			s.numLanes = std::min((int) a->lanes.size(), kMaxLaneView);
			s.hitMask = 0;
			s.activeMask = 0;
			for (int l = 0; l < s.numLanes; l++) {
				const dp::Lane& ln = a->lanes[(size_t) l];
				s.lane[l].role = (uint8_t) ln.role;
				uint32_t m = 0;
				for (int c = 0; c < (int) ln.cells.size() && c < 32; c++)
					if (ln.cells[(size_t) c].on)
						m |= 1u << c;
				s.lane[l].cells = m;
				if (laneFlash[l] > 0.f)
					s.hitMask |= (uint16_t) (1u << l);
				if (absStep >= 0 && seq.laneActive(l, absStep, macros))
					s.activeMask |= (uint16_t) (1u << l);
			}
		}
		snap.publish();
	}
};

// The arrangement: a header (genre, template, bar, tempo, the running FX event), the
// section timeline (block width = bars, fill height = energy, event marks) and under it
// the role x section matrix (filled = the role plays in that section; amber = it just
// fired). One playhead runs through both. Changes at most 20x/s on MetaModule.
struct ArrangeScreen : Screen {
	Dubplate* module = nullptr;
	Snapshot demo;

	ArrangeScreen() {
		dp::Arrangement a = dp::GenreTemplates::makeTemplate(dp::Genre::Jungle);
		std::snprintf(demo.name, sizeof demo.name, "%s", a.name.c_str());
		demo.mode = 1;
		demo.totalBars = a.totalBars();
		demo.numSec = std::min((int) a.sections.size(), kMaxSecView);
		for (int i = 0; i < demo.numSec; i++) {
			demo.sec[i].type = (uint8_t) a.sections[(size_t) i].type;
			demo.sec[i].bars = (int16_t) a.sections[(size_t) i].bars;
			demo.sec[i].energy = a.sections[(size_t) i].energy;
			demo.sec[i].events = a.sections[(size_t) i].events;
			std::snprintf(demo.sec[i].name, sizeof demo.sec[i].name, "%s", a.sections[(size_t) i].name.c_str());
		}
		demo.numLanes = std::min((int) a.lanes.size(), kMaxLaneView);
		for (int l = 0; l < demo.numLanes; l++) {
			demo.lane[l].role = (uint8_t) a.lanes[(size_t) l].role;
			for (int c = 0; c < (int) a.lanes[(size_t) l].cells.size() && c < 32; c++)
				if (a.lanes[(size_t) l].cells[(size_t) c].on)
					demo.lane[l].cells |= 1u << c;
		}
		demo.curSection = 2;
		demo.posBars = 30.5f;
		demo.hitMask = 0x5;
		demo.activeMask = 0xff;
	}

	void step() override {
		Screen::step();
#ifndef METAMODULE
		if (module)
			module->serviceEdits();   // Rack: edits run here, on the UI thread (MetaModule: Dubplate::worker)
#endif
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		const float pad = 3.f;
		const float fs = std::max(8.f, semi::platform::kMinText);
		const float cw = fs * 0.54f;
		char buf[48];

		// header
		static const char* gshort[] = {"JUNGLE", "DNB", "UKG", "SPEED G"};
		text(vg, pad, pad, gshort[clamp(s.genre, 0, 3)], fs, semi::theme::cyan());
		text(vg, pad + cw * 8.f, pad, s.name, fs, semi::theme::text());
		if (s.posBars >= 0.f)
			std::snprintf(buf, sizeof buf, "BAR %03d/%03d %s %3.0f", (int) s.posBars + 1, s.totalBars, s.mode == 2 ? "EXT" : "BPM", s.bpm);
		else
			std::snprintf(buf, sizeof buf, "%s %3.0f", s.mode == 0 ? "STOP" : (s.mode == 2 ? "EXT" : "BPM"), s.bpm);
		text(vg, w - pad, pad, buf, fs, s.mode == 0 ? semi::theme::dim() : semi::theme::amber(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);

		// timeline geometry (shared with the matrix so the columns line up)
		const float labelW = cw * 3.f + 4.f;
		const float x0 = pad + labelW, x1 = w - pad;
		const float total = (float) std::max(1, s.totalBars);
		auto X = [&](float bar) { return x0 + (x1 - x0) * bar / total; };
		const float ty0 = pad + fs + 3.f, ty1 = ty0 + 24.f;
		float barAt = 0.f;
		for (int i = 0; i < s.numSec; i++) {
			const SecView& sv = s.sec[i];
			const float a = X(barAt) + 0.8f, b = X(barAt + sv.bars) - 0.8f;
			const bool cur = i == s.curSection;
			const float eh = (ty1 - ty0) * clamp(sv.energy, 0.f, 1.f);
			fillRect(vg, a, ty0, b - a, ty1 - ty0, semi::theme::cell());
			fillRect(vg, a, ty1 - eh, b - a, eh, cur ? semi::theme::cyanDim() : semi::theme::cyanDeep());
			line(vg, a, ty1 - eh, b, ty1 - eh, cur ? semi::theme::cyan() : semi::theme::cyanDim(), semi::platform::kStroke);
			if (sv.events)
				fillRect(vg, b - 3.f, ty0, 3.f, 3.f, semi::theme::magenta());
			if (i == s.pendingJump)
				fillRect(vg, a, ty0, 3.f, ty1 - ty0, semi::theme::amber());
			// name, as much as fits
			const int fit = (int) ((b - a - 2.f) / cw);
			if (fit >= 2) {
				char nm[12];
				int k = 0;
				for (; k < fit && k < 11 && sv.name[k]; k++)
					nm[k] = (char) std::toupper((unsigned char) sv.name[k]);
				nm[k] = 0;
				text(vg, a + 1.5f, ty0 + 1.f, nm, fs, cur ? semi::theme::text() : semi::theme::dim());
			}
			barAt += sv.bars;
		}

		// matrix
		static const char* lshort[] = {"KCK", "KSB", "PNC", "SNR", "CLP", "HHC", "HHO", "SHF", "RDE", "BRK", "CHP",
		                               "PRC", "SUB", "BAS", "BS2", "RSE", "ORG", "STB", "VOX", "PAD", "RSR", "IMP"};
		const float my0 = ty1 + 4.f, my1 = h - pad;
		const float rowH = s.numLanes > 0 ? (my1 - my0) / s.numLanes : 0.f;
		for (int l = 0; l < s.numLanes; l++) {
			const float y = my0 + l * rowH;
			const bool hit = (s.hitMask >> l) & 1;
			const bool act = (s.activeMask >> l) & 1;
			text(vg, pad, y + rowH * 0.5f, lshort[std::min<int>(s.lane[l].role, 21)], fs,
			     hit ? semi::theme::amber() : (act ? semi::theme::text() : semi::theme::dim()), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			barAt = 0.f;
			for (int i = 0; i < s.numSec; i++) {
				const float a = X(barAt) + 0.8f, b = X(barAt + s.sec[i].bars) - 0.8f;
				barAt += s.sec[i].bars;
				if (!((s.lane[l].cells >> i) & 1))
					continue;
				NVGcolor c = semi::theme::cyanDeep();
				if (i == s.curSection)
					c = hit ? semi::theme::amber() : (act ? semi::theme::cyan() : semi::theme::cyanDim());
				fillRect(vg, a, y + 0.6f, b - a, std::max(1.5f, rowH - 1.2f), c);
			}
		}

		// playhead
		if (s.posBars >= 0.f) {
			const float px = X(std::fmod(s.posBars, total));
			line(vg, px, ty0, px, my1, semi::theme::amber(), semi::platform::kStroke);
		}

		// running event / fill over the timeline's right end
		if (s.eventBit >= 0 || s.fill) {
			const char* ev = s.eventBit >= 0 ? dp::eventShortName(s.eventBit) : "FILL";
			const float tw = cw * (std::strlen(ev) + 1.f);
			fillRect(vg, x1 - tw - 1.f, ty1 - fs - 2.f, tw + 1.f, fs + 2.f, semi::theme::screen());
			text(vg, x1 - 1.f, ty1 - 1.f, ev, fs, s.eventBit >= 0 ? semi::theme::magenta() : semi::theme::amber(), NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
		}
	}
};

struct DubplateWidget : ModuleWidget {
	DubplateWidget(Dubplate* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Dubplate.svg")));

		auto* scr = semi::createScreenMm<ArrangeScreen>(Vec(L::ARRANGE.x, L::ARRANGE.y), Vec(L::ARRANGE.w, L::ARRANGE.h));
		scr->module = module;
		addChild(scr);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		const L::P gl[4] = {L::G_JUNGLE, L::G_DNB, L::G_UKG, L::G_SPEED};
		for (int i = 0; i < 4; i++)
			addChild(createLightCentered<semi::CyanLedSmall>(at(gl[i]), module, Dubplate::GENRE_LIGHT + i));
		addParam(createParamCentered<semi::Button>(at(L::GENRE), module, Dubplate::GENRE_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::FORM), module, Dubplate::FORM_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::VIP), module, Dubplate::VIP_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::MUTATE), module, Dubplate::MUTATE_PARAM));

		addParam(createParamCentered<semi::KnobL>(at(L::ENERGY), module, Dubplate::ENERGY_PARAM));
		addParam(createParamCentered<semi::KnobMHot>(at(L::CHAOS), module, Dubplate::CHAOS_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::FILL), module, Dubplate::FILL_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::SWING), module, Dubplate::SWING_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::TEMPO), module, Dubplate::TEMPO_PARAM));
		addParam(createParamCentered<semi::PadCyan>(at(L::RUN), module, Dubplate::RUN_PARAM));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::RUN_LED), module, Dubplate::RUN_LIGHT));
		addChild(createLightCentered<semi::AmberLedSmall>(at(L::CLK_LED), module, Dubplate::CLK_LIGHT));

		addInput(createInputCentered<semi::Jack>(at(L::CLK), module, Dubplate::CLK_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RST), module, Dubplate::RST_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RUN_IN), module, Dubplate::RUN_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::NEXT), module, Dubplate::NEXT_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::MUT_IN), module, Dubplate::MUT_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::FILL_IN), module, Dubplate::FILL_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::NRG_CV), module, Dubplate::NRG_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::CHS_CV), module, Dubplate::CHS_INPUT));

		const L::P outs[NUM_GATES] = {L::KICK, L::SNARE, L::HAT, L::OHAT, L::BREAK, L::PERC, L::FX, L::SUB, L::BASS, L::BASS2, L::MUSIC};
		for (int g = 0; g < NUM_GATES; g++)
			addOutput(createOutputCentered<semi::Jack>(at(outs[g]), module, Dubplate::GATE_OUTPUT + g));
		addOutput(createOutputCentered<semi::Jack>(at(L::SLICE), module, Dubplate::SLICE_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::VOCT), module, Dubplate::VOCT_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::EVT), module, Dubplate::EVT_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::CLK_OUT), module, Dubplate::CLK_OUTPUT));
		addOutput(createOutputCentered<semi::BusJack>(at(L::BUS), module, Dubplate::BUS_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Dubplate*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Genre template", kGenreNames,
		                                      [=]() { return (size_t) m->genreShown.load(); },
		                                      [=](size_t i) { m->wantGenre = (int) i; m->command(Dubplate::C_GENRE); }));
		menu->addChild(createSubmenuItem("Generate", "", [=](Menu* sub) {
			sub->addChild(createMenuItem("New song form", "", [=]() { m->command(Dubplate::C_FORM); }));
			sub->addChild(createMenuItem("VIP form", "", [=]() { m->command(Dubplate::C_VIP); }));
			sub->addChild(createMenuItem("Drum patterns", "", [=]() { m->command(Dubplate::C_DRUMS); }));
			sub->addChild(createMenuItem("Bassline", "", [=]() { m->command(Dubplate::C_BASS); }));
			sub->addChild(createMenuItem("Mutate", "", [=]() { m->command(Dubplate::C_MUTATE); }));
		}));
		menu->addChild(createIndexSubmenuItem("Mutate amount", kMutateNames,
		                                      [=]() { return (size_t) m->mutateIdx; }, [=](size_t i) { m->mutateIdx = (int) i; }));

		menu->addChild(createMenuLabel("Fills and variations"));
		menu->addChild(createIndexSubmenuItem("Fill profile", kFillProfiles,
		                                      [=]() { return (size_t) m->fillProfShown.load(); },
		                                      [=](size_t i) { m->wantFillProf = (int) i; m->command(Dubplate::C_FILLPROF); }));
		menu->addChild(createIndexSubmenuItem("Fill every", kFillEvery,
		                                      [=]() { return (size_t) m->fillEveryShown.load(); },
		                                      [=](size_t i) { m->wantFillEvery = (int) i; m->command(Dubplate::C_FILLEVERY); }));
		menu->addChild(createIndexSubmenuItem("Fill probability", kFillProb,
		                                      [=]() { return (size_t) m->fillProbShown.load(); },
		                                      [=](size_t i) { m->wantFillProb = (int) i; m->command(Dubplate::C_FILLPROB); }));
		menu->addChild(createIndexSubmenuItem("Variation lanes (BASS 2, CHOP)", kAltNames,
		                                      [=]() { return (size_t) m->altShown.load(); },
		                                      [=](size_t i) { m->wantAlt = (int) i; m->command(Dubplate::C_ALT); }));

		menu->addChild(createMenuLabel("Performance"));
		const Snapshot& s = m->snap.read();
		std::vector<std::string> secs;
		for (int i = 0; i < s.numSec; i++)
			secs.push_back(string::f("%d %s", i + 1, s.sec[i].name));
		if (!secs.empty())
			menu->addChild(createIndexSubmenuItem("Jump to section", secs,
			                                      [=]() { return (size_t) std::max(0, m->snap.read().curSection); },
			                                      [=](size_t i) { m->jumpRequest = (int) i; }));
		menu->addChild(createIndexSubmenuItem("Jump / NEXT quantise", kQuantNames,
		                                      [=]() { return (size_t) m->quant; }, [=](size_t i) { m->quant = (int) i; }));
		menu->addChild(createBoolPtrMenuItem("Loop the arrangement", "", &m->loopArr));

		menu->addChild(createMenuLabel("Outputs"));
		menu->addChild(createIndexSubmenuItem("Gate length", kGateLenNames,
		                                      [=]() { return (size_t) m->gateLen; }, [=](size_t i) { m->gateLen = (int) i; }));
		menu->addChild(createBoolPtrMenuItem("Gate height = velocity", "", &m->velGates));
		menu->addChild(createIndexSubmenuItem("Bass V/OCT root", kRootNames,
		                                      [=]() { return (size_t) m->root; }, [=](size_t i) { m->root = (int) i; }));
		menu->addChild(createIndexSubmenuItem("Clock input PPQN", kPpqnNames,
		                                      [=]() { return (size_t) m->ppqnIdx; }, [=](size_t i) { m->ppqnIdx = (int) i; m->clkCount = 0; }));
	}
};

Model* modelDubplate = createModel<Dubplate, DubplateWidget>("Dubplate");
