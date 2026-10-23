// DROP II — Rack / MetaModule port of DROP II (engine: engines/drop2, vendored unchanged from
// semierendonmez-commits/drop2 Source/engine).
//
// From drop 1 (drums + bass) and the IDENTITY knob it writes drop 2: half-time, kick shift,
// bass lock, register flip and a call / response split of the bass. IDENTITY 1 = drop 1 again,
// 0 = everything the moves allow. Four variants (A-D) are generated per seed, as in the plugin.
//
// Drop 1 comes from the built-in example (the plugin's demo drop) or is LEARNED from live CV:
// press LEARN, and from the next bar line the KICK / SNR / HAT gates and the bass V/OCT + GATE
// are recorded for N bars (menu). A lane that stays silent keeps its previous notes.
//
// Position (Rack has no transport): CLK (1/16 by default, menu for others) + RST, or an internal
// tempo from the menu when CLK is not patched. The drop loops over its own length (whole bars).
//
// Outputs: KICK / SNR / HAT gates (10 V, held for the note length), the call voice and the
// response voice as V/OCT (1 V/oct, C4 = 0 V) + GATE, and KIT = the plugin's preview kit
// (sine kick, noise snare / hat, saw and pulse basses) so drop 2 can be heard without patching.
//
// Threads: generation allocates (std::vector, std::set), so it never runs in process(). Rack: the
// widget's step() (UI thread); MetaModule: an AsyncThread. A finished Playback is handed to the
// audio thread through semi::Slot. process() never allocates.

#include "../plugin.hpp"
#include "../common/AsyncJob.hpp"
#include "../common/Components.hpp"
#include "../common/Sample.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/Drop2Layout.hpp"
#include "drop2/Preview.h"
#include "drop2/Transform.h"

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdio>

#ifdef METAMODULE
#include "threads/async_thread.hh"
#endif

namespace L = semi::layout::drop2;
using semi::Screen;

namespace {

constexpr int kVariants = 4;
constexpr int kMoves = 5;  // HALF, KICK, LOCK, FLIP, SPLIT
constexpr int kMaxRec = 512;
constexpr int kMaxHits = 256;
constexpr int kMaxBass = 128;

const std::vector<std::string> kPresetNames = {"SWITCH-UP", "HALF-TIME DROP", "ROLLER FLIP", "CALL & RESPONSE", "FULL VIP", "DROP 1 AGAIN"};
const std::vector<std::string> kPctNames = {"0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"};
const std::vector<std::string> kClockNames = {"1/16 (4 PPQN)", "1/8 (2 PPQN)", "1/4 (1 PPQN)", "24 PPQN"};
constexpr float kClockPpqn[] = {4.f, 2.f, 1.f, 24.f};
const std::vector<std::string> kTempoNames = {"Off (CLK only)", "140 BPM", "160 BPM", "170 BPM", "172 BPM", "174 BPM", "180 BPM"};
constexpr float kTempos[] = {0.f, 140.f, 160.f, 170.f, 172.f, 174.f, 180.f};
const std::vector<std::string> kRecNames = {"1 bar", "2 bars", "4 bars", "8 bars"};
constexpr int kRecBars[] = {1, 2, 4, 8};
const std::vector<std::string> kLevelNames = {"Off", "-12 dB", "-6 dB", "-3 dB", "0 dB", "+6 dB"};
constexpr float kLevelDb[] = {-200.f, -12.f, -6.f, -3.f, 0.f, 6.f};

// Everything that is not a panel knob (menu, saved in the patch). Defaults = the plugin's.
struct Cfg {
	bool moves[kMoves] = {false, true, true, false, false};
	bool htHats = true;
	int kickAmt = 7, kickMax = 1;     // kickMax index: 1/16, 2/16, 3/16
	int lockAmt = 8, lockTo = 0;
	int flipMode = 2, flipAmt = 6;
	int splitUnit = 1, interval = 0;
	bool operator==(const Cfg& o) const {
		for (int i = 0; i < kMoves; i++)
			if (moves[i] != o.moves[i])
				return false;
		return htHats == o.htHats && kickAmt == o.kickAmt && kickMax == o.kickMax && lockAmt == o.lockAmt &&
		       lockTo == o.lockTo && flipMode == o.flipMode && flipAmt == o.flipAmt && splitUnit == o.splitUnit &&
		       interval == o.interval;
	}
};

// The plugin's factory presets (Source/PluginProcessor.cpp factories()) over the defaults.
Cfg makePreset(int i, float& identity) {
	Cfg c;
	identity = 0.6f;
	switch (i) {
		case 1: c.moves[0] = true; identity = 0.5f; c.moves[1] = false; c.moves[2] = true; c.lockAmt = 6; break;
		case 2: c.moves[3] = true; c.flipMode = 2; c.flipAmt = 8; c.kickAmt = 4; identity = 0.55f; break;
		case 3: c.moves[4] = true; c.splitUnit = 1; c.interval = 3; c.moves[2] = true; identity = 0.6f; break;
		case 4:
			c.moves[0] = true; c.moves[3] = true; c.flipMode = 3; c.moves[4] = true; c.splitUnit = 2; c.interval = 4;
			identity = 0.3f; c.kickAmt = 9; c.lockAmt = 10; c.lockTo = 1;
			break;
		case 5: identity = 1.f; break;
		default: break;
	}
	return c;
}

dr2::Clip demoDrums() {  // the plugin's demo drop (PluginProcessor.cpp demoDrums)
	dr2::Clip c;
	c.length = 16.0;
	for (int bar = 0; bar < 4; ++bar) {
		const double o = bar * 4.0;
		c.notes.push_back({o + 0.0, 0.25, 36, 1.0f});
		c.notes.push_back({o + 2.5, 0.25, 36, 0.9f});
		if (bar % 2 == 1)
			c.notes.push_back({o + 3.25, 0.25, 36, 0.7f});
		c.notes.push_back({o + 1.0, 0.25, 38, 1.0f});
		c.notes.push_back({o + 3.0, 0.25, 38, 1.0f});
		c.notes.push_back({o + 1.75, 0.25, 38, 0.3f});
		if (bar == 3)
			c.notes.push_back({o + 3.5, 0.25, 38, 0.6f});
		for (int k = 0; k < 8; ++k)
			c.notes.push_back({o + k * 0.5 + 0.0, 0.125, (k % 4 == 3) ? 46 : 42, k % 2 ? 0.45f : 0.65f});
	}
	c.sort();
	return c;
}

dr2::Clip demoBass() {
	dr2::Clip c;
	c.length = 16.0;
	const int line[] = {31, 31, 34, 29};
	for (int bar = 0; bar < 4; ++bar) {
		const double o = bar * 4.0;
		c.notes.push_back({o + 0.0, 1.5, line[bar], 0.9f});
		c.notes.push_back({o + 1.75, 0.5, line[bar] + 12, 0.8f});
		c.notes.push_back({o + 2.4, 1.0, line[bar], 0.85f});
		if (bar % 2 == 1)
			c.notes.push_back({o + 3.5, 0.4, line[bar] + 7, 0.7f});
	}
	return c;
}

// ---- what the screen draws (fixed size, copied into the snapshot) --------------------------

struct Hit {
	uint16_t step;  // 1/16 from the loop start
	uint8_t role, tag;
};
struct BNote {
	uint16_t start, len;  // 1/16
	uint8_t pitch, voice, tag;
};
struct View {
	int steps = 64;
	int nHits = 0, nBass = 0;
	float id = 1.f, idD = 1.f, idB = 1.f;
	Hit hits[kMaxHits];
	BNote bass[kMaxBass];
};

// ---- playback (built off the audio thread) ---------------------------------------------------

struct Ev {
	double t;
	float vel;
	uint8_t lane;   // 0 drums, 1 call, 2 response
	uint8_t pitch;
	uint8_t role;   // dr2::Role for drums
	bool on;
};

struct Playback {
	double length = 16.0;
	std::vector<Ev> lists[1 + kVariants];  // 0 = drop 1, 1..4 = drop 2 variants A..D
	View views[1 + kVariants];
};

void buildView(View& v, const dr2::Clip& drums, const dr2::Clip* bassA, const dr2::Clip* bassB,
               const dr2::DrumRoles& roles, double length) {
	v.steps = std::max(16, (int) std::lround(length * 4.0));
	v.nHits = 0;
	v.nBass = 0;
	for (const auto& n : drums.notes) {
		if (v.nHits >= kMaxHits)
			break;
		const int st = (int) std::floor(n.start * 4.0 + 0.5);
		if (st < 0 || st >= v.steps)
			continue;
		v.hits[v.nHits++] = {(uint16_t) st, (uint8_t) roles.of(n.pitch), (uint8_t) n.tag};
	}
	const dr2::Clip* lanes[2] = {bassA, bassB};
	for (int l = 0; l < 2; l++) {
		if (!lanes[l])
			continue;
		for (const auto& n : lanes[l]->notes) {
			if (v.nBass >= kMaxBass)
				break;
			const int st = (int) std::floor(n.start * 4.0 + 0.5);
			if (st < 0 || st >= v.steps)
				continue;
			const int len = std::max(1, (int) std::lround(n.length * 4.0));
			v.bass[v.nBass++] = {(uint16_t) st, (uint16_t) std::min(len, v.steps - st), (uint8_t) clamp(n.pitch, 0, 127),
			                     (uint8_t) l, (uint8_t) n.tag};
		}
	}
}

// One list of note on / off events over the loop (the plugin's publish() addClip).
void addClip(std::vector<Ev>& out, const dr2::Clip& c, int lane, double length, const dr2::DrumRoles& roles) {
	const double cl = std::max(1.0, c.length);
	const int reps = std::max(1, (int) std::floor(length / cl + 1.0e-9));
	for (int r = 0; r < reps; ++r)
		for (const auto& n : c.notes) {
			const double t = n.start + r * cl;
			if (t >= length - 1.0e-9)
				continue;
			const double e = std::min(length, t + std::max(0.02, n.length));
			const uint8_t role = lane == 0 ? (uint8_t) roles.of(n.pitch) : (uint8_t) dr2::Role::Perc;
			const uint8_t p = (uint8_t) clamp(n.pitch, 0, 127);
			out.push_back({t, n.velocity, (uint8_t) lane, p, role, true});
			out.push_back({e, 0.f, (uint8_t) lane, p, role, false});
		}
}

void sortEvents(std::vector<Ev>& v) {
	std::stable_sort(v.begin(), v.end(), [](const Ev& a, const Ev& b) { return a.t < b.t || (a.t == b.t && !a.on && b.on); });
}

struct RecNote {
	double start, len;
	int pitch;
	float vel;
};

struct Snapshot {
	View view;
	float playhead = -1.f;  // 0..1 of the loop, -1 = stopped
	int mode = 0;           // 0 stopped, 1 CLK, 2 internal
	int learn = 0;          // 0 idle, 1 armed, 2 recording
	int recBar = 0, recBars = 4;
	int variant = 0;
	bool drop1 = false;
	float bpm = 0.f;
};

} // namespace

struct Drop2 : Module {
	enum ParamId {
		IDENTITY_PARAM, VARIANT_PARAM, NEW_PARAM, PLAY_PARAM, LEARN_PARAM,
		ENUMS(MOVE_PARAM, kMoves),
		PARAMS_LEN
	};
	enum InputId { CLK_INPUT, RST_INPUT, ID_INPUT, KICK_INPUT, SNARE_INPUT, HAT_INPUT, BASS_VOCT_INPUT, BASS_GATE_INPUT, INPUTS_LEN };
	enum OutputId { KICK_OUTPUT, SNARE_OUTPUT, HAT_OUTPUT, CALL_VOCT_OUTPUT, CALL_GATE_OUTPUT, RESP_VOCT_OUTPUT, RESP_GATE_OUTPUT, AUDIO_OUTPUT, OUTPUTS_LEN };
	enum LightId { ENUMS(MOVE_LIGHT, kMoves), PLAY_LIGHT, LEARN_LIGHT, LIGHTS_LEN };

	// ---- state shared with the worker (menu / JSON; written on the UI side) ---------------------
	Cfg cfg;
	int preset = 0;
	std::atomic<int> seed{1};
	bool drop1 = false;
	int clockIdx = 0, tempoIdx = 0, recIdx = 2, levelIdx = 3;

	// sources: owned by the worker side, guarded by `busy`
	dr2::Clip srcDrums = demoDrums(), srcBass = demoBass();
	int srcVersion = 0;
	std::atomic_flag busy = ATOMIC_FLAG_INIT;

	// the worker's last build
	struct Request {
		int identity = -1;  // x 1000
		Cfg cfg;
		int seed = 0, version = -1;
		bool operator!=(const Request& o) const {
			return identity != o.identity || !(cfg == o.cfg) || seed != o.seed || version != o.version;
		}
	};
	Request built;
	std::atomic<float> identityNow{0.6f};

	semi::Slot<Playback> slot;
#ifdef METAMODULE
	semi::AsyncJob worker{this, [this]() { service(); }};
#endif

	// ---- audio thread -------------------------------------------------------------------------
	dsp::SchmittTrigger clkTrig, rstTrig;
	dsp::BooleanTrigger moveBtn[kMoves], newBtn, playBtn, learnBtn;
	int count = -1;  // clock pulses since reset, -1 = waiting for the first
	float sinceClk = 0.f, period = 0.f;
	bool clkSeen = false;
	double internalPpq = 0.0;
	double ppqNow = 0.0;
	bool running = false;
	int mode = 0;
	float bpm = 0.f;

	int listSel = -1;  // which list idx refers to (-2 = none)
	size_t idx = 0;
	double lastPos = 0.0;
	const Playback* lastPb = nullptr;

	struct DrumGate {
		int count = 0;
		float low = 0.f;  // retrigger gap left (s)
	} drumGate[3];
	struct BassGate {
		bool on = false;
		int pitch = -1;
		float volts = 0.f;
		float low = 0.f;
	} bassGate[2];

	dr2::Preview kit;
	float kitGain = 0.7f;

	// LEARN
	int learn = 0;  // 0 idle, 1 armed, 2 recording
	double recStart = 0.0;
	int recBars = 4;
	dsp::SchmittTrigger recTrig[4];  // kick, snare, hat, bass gate
	bool recHigh[4] = {};
	int recOpen[4] = {-1, -1, -1, -1};
	int recSemi = 0;
	RecNote rec[2][kMaxRec];
	int recN[2] = {0, 0};
	std::atomic<bool> recDone{false};
	long lastBar = LONG_MIN;

	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;
	float blinkT = 0.f;

	~Drop2() override {
		// background jobs first (and wait for a run in flight): they write into this module
#ifdef METAMODULE
		worker.stopAndWait();
#endif
	}

	Drop2() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(IDENTITY_PARAM, 0.f, 1.f, 0.6f, "Identity", " %", 0.f, 100.f);
		configSwitch(VARIANT_PARAM, 0.f, 3.f, 0.f, "Variant", {"A", "B", "C", "D"});
		configButton(NEW_PARAM, "New seed");
		configButton(PLAY_PARAM, "Play drop 1 / drop II");
		configButton(LEARN_PARAM, "Learn drop 1 from the inputs");
		static const char* moveNames[kMoves] = {"Half-time", "Kick shift", "Bass lock", "Register flip", "Call / response"};
		for (int i = 0; i < kMoves; i++) {
			configButton(MOVE_PARAM + i, moveNames[i]);
			configLight(MOVE_LIGHT + i, moveNames[i]);
		}
		configLight(PLAY_LIGHT, "Playing drop 1");
		configLight(LEARN_LIGHT, "Learn (blinks: waiting for the bar line, on: recording)");
		configInput(CLK_INPUT, "Clock (1/16 notes by default)");
		configInput(RST_INPUT, "Reset (next clock = bar 1)");
		configInput(ID_INPUT, "Identity CV (0-10 V adds 0-100 %)");
		configInput(KICK_INPUT, "Learn: kick gate");
		configInput(SNARE_INPUT, "Learn: snare gate");
		configInput(HAT_INPUT, "Learn: hat gate");
		configInput(BASS_VOCT_INPUT, "Learn: bass V/OCT");
		configInput(BASS_GATE_INPUT, "Learn: bass gate");
		configOutput(KICK_OUTPUT, "Kick gate");
		configOutput(SNARE_OUTPUT, "Snare gate");
		configOutput(HAT_OUTPUT, "Hat / perc gate");
		configOutput(CALL_VOCT_OUTPUT, "Call bass V/OCT");
		configOutput(CALL_GATE_OUTPUT, "Call bass gate");
		configOutput(RESP_VOCT_OUTPUT, "Response bass V/OCT");
		configOutput(RESP_GATE_OUTPUT, "Response bass gate");
		configOutput(AUDIO_OUTPUT, "Preview kit audio");
		kit.prepare(44100.0);
		service();  // first build, so the module plays at once
#ifdef METAMODULE
		worker.start();
#endif
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override { kit.prepare(e.sampleRate); }

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		lock();
		cfg = Cfg();
		srcDrums = demoDrums();
		srcBass = demoBass();
		srcVersion++;
		unlock();
		preset = 0;
		seed = 1;
		drop1 = false;
		clockIdx = tempoIdx = 0;
		recIdx = 2;
		levelIdx = 3;
	}

	void lock() {
		while (busy.test_and_set(std::memory_order_acquire)) {
		}
	}
	void unlock() { busy.clear(std::memory_order_release); }

	void applyPreset(int i) {
		preset = clamp(i, 0, (int) kPresetNames.size() - 1);
		float id = 0.6f;
		cfg = makePreset(preset, id);
		params[IDENTITY_PARAM].setValue(id);
	}

	// ---- worker side ----------------------------------------------------------------------------

	dr2::Settings settingsFor(const Cfg& c, float identity) const {
		dr2::Settings s;
		s.identity = identity;
		s.halftime = c.moves[0];
		s.halftimeHats = c.htHats;
		s.kickShift = c.moves[1];
		s.kickAmount = c.kickAmt / 10.f;
		s.kickMax = c.kickMax + 1;
		s.bassLock = c.moves[2];
		s.lockAmount = c.lockAmt / 10.f;
		s.lockTo = c.lockTo;
		s.flip = c.moves[3];
		s.flipMode = c.flipMode;
		s.flipAmount = c.flipAmt / 10.f;
		s.split = c.moves[4];
		s.splitUnit = c.splitUnit;
		s.respInterval = dr2::kIntervals[clamp(c.interval, 0, 6)];
		return s;
	}

	// Takes a finished LEARN recording (the audio thread has stopped writing: recDone is set).
	void consumeRecording() {
		const double len = recBars * 4.0;
		for (int lane = 0; lane < 2; lane++) {
			if (recN[lane] <= 0)
				continue;  // a silent lane keeps what it had
			dr2::Clip c;
			c.length = std::clamp(std::ceil(len / 4.0 - 1.0e-9) * 4.0, 4.0, 64.0);  // setSource()
			for (int i = 0; i < recN[lane]; i++) {
				const RecNote& r = rec[lane][i];
				if (r.start >= c.length)
					continue;
				c.notes.push_back({r.start, std::max(0.02, r.len), r.pitch, r.vel});
			}
			c.sort();
			(lane == 0 ? srcDrums : srcBass) = std::move(c);
		}
		srcVersion++;
		recDone.store(false, std::memory_order_release);
	}

	// Rack: the widget's step() (UI thread). MetaModule: the AsyncThread.
	void service() {
		if (busy.test_and_set(std::memory_order_acquire))
			return;
		slot.collect();
		if (recDone.load(std::memory_order_acquire))
			consumeRecording();
		Request r;
		r.identity = (int) std::lround(clamp(identityNow.load(), 0.f, 1.f) * 200.f) * 5;  // 0.5 % steps
		r.cfg = cfg;
		r.seed = seed.load();
		r.version = srcVersion;
		if (r != built) {
			build(r);
			built = r;
		}
		busy.clear(std::memory_order_release);
	}

	void build(const Request& r) {
		auto* pb = new Playback;
		const dr2::Settings s = settingsFor(r.cfg, r.identity / 1000.f);
		pb->length = std::max(4.0, std::max(srcDrums.length, srcBass.length));  // loopLength()
		const auto roles = dr2::detectRoles(srcDrums);

		addClip(pb->lists[0], srcDrums, 0, pb->length, roles);
		addClip(pb->lists[0], srcBass, 1, pb->length, roles);
		sortEvents(pb->lists[0]);
		buildView(pb->views[0], srcDrums, &srcBass, nullptr, roles, pb->length);
		pb->views[0].id = pb->views[0].idD = pb->views[0].idB = 1.f;

		for (int v = 0; v < kVariants; ++v) {
			const auto res = dr2::generate(srcDrums, srcBass, s, (std::uint32_t) r.seed * 31u + (std::uint32_t) v * 7919u + 1u);
			auto& list = pb->lists[1 + v];
			addClip(list, res.drums, 0, pb->length, roles);
			addClip(list, res.bassA, 1, pb->length, roles);
			addClip(list, res.bassB, 2, pb->length, roles);
			sortEvents(list);
			View& view = pb->views[1 + v];
			buildView(view, res.drums, &res.bassA, &res.bassB, roles, pb->length);
			view.id = res.identity;
			view.idD = res.idDrums;
			view.idB = res.idBass;
		}
		slot.offer(pb);
	}

	// ---- JSON -----------------------------------------------------------------------------------

	static json_t* clipToJson(const dr2::Clip& c) {
		json_t* o = json_object();
		json_object_set_new(o, "length", json_real(c.length));
		json_t* a = json_array();
		for (const auto& n : c.notes) {
			json_t* e = json_array();
			json_array_append_new(e, json_real(n.start));
			json_array_append_new(e, json_real(n.length));
			json_array_append_new(e, json_integer(n.pitch));
			json_array_append_new(e, json_real(n.velocity));
			json_array_append_new(a, e);
		}
		json_object_set_new(o, "notes", a);
		return o;
	}

	static bool clipFromJson(json_t* o, dr2::Clip& c) {
		if (!json_is_object(o))
			return false;
		dr2::Clip out;
		if (json_t* l = json_object_get(o, "length"))
			out.length = std::clamp(json_number_value(l), 4.0, 64.0);
		json_t* a = json_object_get(o, "notes");
		if (!json_is_array(a))
			return false;
		size_t i;
		json_t* e;
		json_array_foreach(a, i, e) {
			if (!json_is_array(e) || json_array_size(e) < 4 || out.notes.size() >= 1024)
				continue;
			dr2::Note n;
			n.start = std::clamp(json_number_value(json_array_get(e, 0)), 0.0, 64.0);
			n.length = std::clamp(json_number_value(json_array_get(e, 1)), 0.02, 64.0);
			n.pitch = clamp((int) json_integer_value(json_array_get(e, 2)), 0, 127);
			n.velocity = clamp((float) json_number_value(json_array_get(e, 3)), 0.f, 1.f);
			out.notes.push_back(n);
		}
		out.sort();
		c = std::move(out);
		return true;
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "preset", json_integer(preset));
		json_object_set_new(root, "seed", json_integer(seed.load()));
		json_object_set_new(root, "drop1", json_boolean(drop1));
		json_t* mv = json_array();
		for (int i = 0; i < kMoves; i++)
			json_array_append_new(mv, json_boolean(cfg.moves[i]));
		json_object_set_new(root, "moves", mv);
		json_object_set_new(root, "htHats", json_boolean(cfg.htHats));
		json_object_set_new(root, "kickAmt", json_integer(cfg.kickAmt));
		json_object_set_new(root, "kickMax", json_integer(cfg.kickMax));
		json_object_set_new(root, "lockAmt", json_integer(cfg.lockAmt));
		json_object_set_new(root, "lockTo", json_integer(cfg.lockTo));
		json_object_set_new(root, "flipMode", json_integer(cfg.flipMode));
		json_object_set_new(root, "flipAmt", json_integer(cfg.flipAmt));
		json_object_set_new(root, "splitUnit", json_integer(cfg.splitUnit));
		json_object_set_new(root, "interval", json_integer(cfg.interval));
		json_object_set_new(root, "clock", json_integer(clockIdx));
		json_object_set_new(root, "tempo", json_integer(tempoIdx));
		json_object_set_new(root, "rec", json_integer(recIdx));
		json_object_set_new(root, "level", json_integer(levelIdx));
		lock();
		json_object_set_new(root, "drums", clipToJson(srcDrums));
		json_object_set_new(root, "bass", clipToJson(srcBass));
		unlock();
		return root;
	}

	void dataFromJson(json_t* root) override {
		auto geti = [&](const char* k, int& v, int lo, int hi) {
			if (json_t* j = json_object_get(root, k))
				v = clamp((int) json_integer_value(j), lo, hi);
		};
		auto getb = [&](const char* k, bool& v) {
			if (json_t* j = json_object_get(root, k))
				v = json_boolean_value(j);
		};
		geti("preset", preset, 0, (int) kPresetNames.size() - 1);
		int sd = seed.load();
		geti("seed", sd, 1, 9999);
		seed = sd;
		getb("drop1", drop1);
		if (json_t* mv = json_object_get(root, "moves"))
			for (int i = 0; i < kMoves; i++)
				if (json_t* j = json_array_get(mv, i))
					cfg.moves[i] = json_boolean_value(j);
		getb("htHats", cfg.htHats);
		geti("kickAmt", cfg.kickAmt, 0, 10);
		geti("kickMax", cfg.kickMax, 0, 2);
		geti("lockAmt", cfg.lockAmt, 0, 10);
		geti("lockTo", cfg.lockTo, 0, 1);
		geti("flipMode", cfg.flipMode, 0, 3);
		geti("flipAmt", cfg.flipAmt, 0, 10);
		geti("splitUnit", cfg.splitUnit, 0, 2);
		geti("interval", cfg.interval, 0, 6);
		geti("clock", clockIdx, 0, 3);
		geti("tempo", tempoIdx, 0, (int) kTempoNames.size() - 1);
		geti("rec", recIdx, 0, 3);
		geti("level", levelIdx, 0, (int) kLevelNames.size() - 1);
		dr2::Clip d, b;
		const bool hd = clipFromJson(json_object_get(root, "drums"), d);
		const bool hb = clipFromJson(json_object_get(root, "bass"), b);
		lock();
		if (hd)
			srcDrums = std::move(d);
		if (hb)
			srcBass = std::move(b);
		srcVersion++;
		unlock();
	}

	// ---- position -------------------------------------------------------------------------------

	bool clockPosition(float dt, double& ppq) {
		sinceClk += dt;
		if (inputs[RST_INPUT].isConnected() && rstTrig.process(inputs[RST_INPUT].getVoltage(), 0.1f, 1.f)) {
			count = -1;
			internalPpq = 0.0;
		}
		const float ppqn = kClockPpqn[clockIdx];
		if (!inputs[CLK_INPUT].isConnected()) {
			clkSeen = false;
			period = 0.f;
			count = -1;
			const float tempo = kTempos[tempoIdx];
			if (tempo <= 0.f)
				return false;
			bpm = tempo;
			ppq = internalPpq;
			internalPpq += dt * tempo / 60.f;
			mode = 2;
			return true;
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
		bpm = 60.f / (ppqn * period);
		ppq = (count + std::min(sinceClk / period, 0.999f)) / ppqn;
		mode = 1;
		return true;
	}

	// ---- playback -------------------------------------------------------------------------------

	void allOff() {
		for (auto& g : drumGate)
			g.count = 0;
		for (int l = 0; l < 2; l++) {
			if (bassGate[l].on)
				kit.noteOff(l == 0 ? dr2::Preview::BassA : dr2::Preview::BassB, bassGate[l].pitch);
			bassGate[l].on = false;
		}
	}

	void fire(const Ev& e) {
		if (e.lane == 0) {
			const dr2::Role role = (dr2::Role) e.role;
			const int out = role == dr2::Role::Kick ? 0 : role == dr2::Role::Snare ? 1 : 2;
			DrumGate& g = drumGate[out];
			if (e.on) {
				if (g.count > 0)
					g.low = 0.001f;  // retrigger
				g.count++;
				kit.noteOn(dr2::Preview::Drums, role, e.pitch, e.vel);
			}
			else {
				g.count = std::max(0, g.count - 1);
			}
			return;
		}
		const int l = e.lane - 1;
		BassGate& g = bassGate[l];
		const auto lane = l == 0 ? dr2::Preview::BassA : dr2::Preview::BassB;
		if (e.on) {
			if (g.on)
				g.low = 0.001f;
			g.on = true;
			g.pitch = e.pitch;
			g.volts = (e.pitch - 60) / 12.f;
			kit.noteOn(lane, dr2::Role::Perc, e.pitch, e.vel);
		}
		else if (g.on && g.pitch == e.pitch) {
			g.on = false;
			kit.noteOff(lane, e.pitch);
		}
	}

	void play(const Playback* pb, double ppq) {
		const int sel = drop1 ? 0 : 1 + clamp((int) std::round(params[VARIANT_PARAM].getValue()), 0, kVariants - 1);
		const std::vector<Ev>& list = pb->lists[sel];
		const double len = pb->length;
		const double pos = std::fmod(std::max(0.0, ppq), len);
		auto seek = [&](double p) {
			idx = (size_t) (std::lower_bound(list.begin(), list.end(), p, [](const Ev& e, double v) { return e.t < v; }) - list.begin());
		};
		if (pb != lastPb || sel != listSel) {
			allOff();
			seek(pos);
			lastPb = pb;
			listSel = sel;
		}
		else if (pos < lastPos) {
			if (lastPos - pos > len * 0.5) {
				while (idx < list.size())
					fire(list[idx++]);  // wrap: finish the loop (the offs at its end)
				idx = 0;
			}
			else {
				allOff();  // a jump backwards
				seek(pos);
			}
		}
		while (idx < list.size() && list[idx].t <= pos)
			fire(list[idx++]);
		lastPos = pos;
	}

	// ---- LEARN ----------------------------------------------------------------------------------

	void recordInputs(double rel) {
		static const int pitches[3] = {36, 38, 42};
		for (int k = 0; k < 4; k++) {
			const int inId = k == 3 ? BASS_GATE_INPUT : KICK_INPUT + k;
			const float v = inputs[inId].getVoltage();
			const bool was = recHigh[k];
			recTrig[k].process(v, 0.1f, 1.f);
			const bool high = recTrig[k].isHigh();
			const int lane = k == 3 ? 1 : 0;
			if (k == 3 && high && was && recOpen[3] >= 0) {
				// legato: the bass pitch moved under a held gate = a new note
				const float semis = inputs[BASS_VOCT_INPUT].getVoltage() * 12.f;
				if (std::abs(semis - recSemi) > 0.6f) {
					RecNote& o = rec[1][recOpen[3]];
					o.len = std::max(0.02, rel - o.start);
					recOpen[3] = -1;
					if (recN[1] < kMaxRec) {
						recSemi = (int) std::lround(semis);
						rec[1][recN[1]] = {rel, -1.0, clamp(60 + recSemi, 0, 127), o.vel};
						recOpen[3] = recN[1]++;
					}
				}
			}
			if (high && !was) {
				if (recN[lane] < kMaxRec) {
					const float vel = clamp(v / 10.f, 0.1f, 1.f);
					int pitch = 0;
					if (k == 3) {
						recSemi = (int) std::lround(inputs[BASS_VOCT_INPUT].getVoltage() * 12.f);
						pitch = clamp(60 + recSemi, 0, 127);
					}
					else {
						pitch = pitches[k];
					}
					rec[lane][recN[lane]] = {rel, -1.0, pitch, vel};
					recOpen[k] = recN[lane]++;
				}
			}
			else if (!high && was && recOpen[k] >= 0) {
				RecNote& o = rec[lane][recOpen[k]];
				o.len = std::max(0.02, rel - o.start);
				recOpen[k] = -1;
			}
			recHigh[k] = high;
		}
	}

	void finishRecording(double end) {
		for (int k = 0; k < 4; k++) {
			if (recOpen[k] >= 0) {
				RecNote& o = rec[k == 3 ? 1 : 0][recOpen[k]];
				o.len = std::max(0.02, end - o.start);
				recOpen[k] = -1;
			}
		}
		learn = 0;
		recDone.store(true, std::memory_order_release);
	}

	void learnStep(bool ok, double ppq) {
		if (!ok) {
			if (learn == 2)
				learn = 0;  // the clock stopped: the take is dropped
			lastBar = LONG_MIN;
			return;
		}
		const long bar = (long) std::floor(ppq / 4.0);
		const bool barLine = lastBar != LONG_MIN && bar != lastBar;
		lastBar = bar;
		if (learn == 1 && barLine) {
			learn = 2;
			recStart = bar * 4.0;
			recN[0] = recN[1] = 0;
			for (int k = 0; k < 4; k++) {
				recOpen[k] = -1;
				recHigh[k] = false;
				recTrig[k].reset();
			}
		}
		if (learn == 2) {
			const double rel = ppq - recStart;
			if (rel >= recBars * 4.0 || rel < 0.0)
				finishRecording(recBars * 4.0);
			else
				recordInputs(rel);
		}
	}

	// ---- process --------------------------------------------------------------------------------

	void process(const ProcessArgs& args) override {
		// buttons
		for (int i = 0; i < kMoves; i++)
			if (moveBtn[i].process(params[MOVE_PARAM + i].getValue() > 0.f))
				cfg.moves[i] = !cfg.moves[i];
		if (newBtn.process(params[NEW_PARAM].getValue() > 0.f))
			seed = (int) (random::u32() % 9999u) + 1;
		if (playBtn.process(params[PLAY_PARAM].getValue() > 0.f))
			drop1 = !drop1;
		if (learnBtn.process(params[LEARN_PARAM].getValue() > 0.f)) {
			if (learn != 0)
				learn = 0;
			else if (!recDone.load(std::memory_order_acquire)) {
				learn = 1;
				recBars = kRecBars[recIdx];
			}
		}

		float id = params[IDENTITY_PARAM].getValue();
		if (inputs[ID_INPUT].isConnected())
			id += inputs[ID_INPUT].getVoltage() / 10.f;
		identityNow.store(clamp(id, 0.f, 1.f), std::memory_order_relaxed);

		double ppq = 0.0;
		const bool ok = clockPosition(args.sampleTime, ppq);
		if (!ok)
			mode = 0;
		ppqNow = ppq;

		slot.poll();
		const Playback* pb = slot.active;
		if (ok && pb) {
			if (!running) {
				lastPb = nullptr;  // re-seek
			}
			play(pb, ppq);
		}
		else if (running) {
			allOff();
		}
		running = ok && pb;

		learnStep(ok, ppq);

		// gates
		const float dt = args.sampleTime;
		for (int k = 0; k < 3; k++) {
			DrumGate& g = drumGate[k];
			bool high = g.count > 0;
			if (g.low > 0.f) {
				g.low -= dt;
				high = false;
			}
			outputs[KICK_OUTPUT + k].setVoltage(high ? 10.f : 0.f);
		}
		for (int l = 0; l < 2; l++) {
			BassGate& g = bassGate[l];
			bool high = g.on;
			if (g.low > 0.f) {
				g.low -= dt;
				high = false;
			}
			outputs[(l == 0 ? CALL_VOCT_OUTPUT : RESP_VOCT_OUTPUT)].setVoltage(g.volts);
			outputs[(l == 0 ? CALL_GATE_OUTPUT : RESP_GATE_OUTPUT)].setVoltage(high ? 10.f : 0.f);
		}

		// preview kit
		if (outputs[AUDIO_OUTPUT].isConnected() && levelIdx > 0) {
			float l = 0.f, r = 0.f;
			kit.render(&l, &r, 0, 1, kitGain);
			outputs[AUDIO_OUTPUT].setVoltage(l * 5.f);
		}
		else {
			outputs[AUDIO_OUTPUT].setVoltage(0.f);
		}

		if (ui.tick(args.sampleTime)) {
			kitGain = std::pow(10.f, kLevelDb[levelIdx] / 20.f) * 0.7f;
			for (int i = 0; i < kMoves; i++)
				lights[MOVE_LIGHT + i].setBrightness(cfg.moves[i] ? 1.f : 0.f);
			lights[PLAY_LIGHT].setBrightness(drop1 ? 1.f : 0.f);
			blinkT += 1.f / semi::platform::kUiRateHz;
			const bool blink = std::fmod(ok ? ppq : (double) blinkT * 2.0, 1.0) < 0.5;
			lights[LEARN_LIGHT].setBrightness(learn == 2 ? 1.f : (learn == 1 && blink) ? 1.f : 0.f);

			Snapshot& s = snap.writeSlot();
			const int sel = drop1 ? 0 : 1 + clamp((int) std::round(params[VARIANT_PARAM].getValue()), 0, kVariants - 1);
			if (pb) {
				s.view = pb->views[sel];
				s.playhead = ok ? (float) (std::fmod(std::max(0.0, ppq), pb->length) / pb->length) : -1.f;
			}
			else {
				s.view.nHits = s.view.nBass = 0;
				s.playhead = -1.f;
			}
			s.mode = ok ? mode : 0;
			s.learn = learn;
			s.recBars = learn == 2 ? recBars : kRecBars[recIdx];
			s.recBar = learn == 2 ? clamp((int) std::floor((ppq - recStart) / 4.0), 0, recBars - 1) : 0;
			s.variant = sel - 1;
			s.drop1 = drop1;
			s.bpm = ok ? bpm : 0.f;
			snap.publish();
		}
	}
};

// ---- screen -----------------------------------------------------------------------------------

// The drop: three drum lanes (kick / snare / hats, one cell per hit on the 1/16 grid) over a bass
// roll (call = cyan, response = magenta). What DROP II changed is amber. Bar lines every 16
// steps, the playhead in amber. Header: which drop, variant, measured identity, clock.
struct DropScreen : Screen {
	Drop2* module = nullptr;
	Snapshot demo;

	DropScreen() {
		// the demo drop as drop 2 of the default settings, built once for the browser
		const dr2::Clip d = demoDrums(), b = demoBass();
		dr2::Settings s;
		s.identity = 0.5f;
		s.split = true;
		s.respInterval = 3;
		const auto roles = dr2::detectRoles(d);
		const auto r = dr2::generate(d, b, s, 32u);
		buildView(demo.view, r.drums, &r.bassA, &r.bassB, roles, 16.0);
		demo.view.id = r.identity;
		demo.view.idD = r.idDrums;
		demo.view.idB = r.idBass;
		demo.playhead = 0.36f;
		demo.mode = 1;
		demo.bpm = 172.f;
	}

	// rectangles of one colour in one path (cheap on MetaModule)
	struct Batch {
		NVGcontext* vg;
		bool open = false;
		void add(float x, float y, float w, float h) {
			if (!open) {
				nvgBeginPath(vg);
				open = true;
			}
			nvgRect(vg, x, y, w, h);
		}
		void fill(NVGcolor c) {
			if (open) {
				nvgFillColor(vg, c);
				nvgFill(vg);
			}
			open = false;
		}
	};

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const View& v = s.view;
		const float w = box.size.x, h = box.size.y;
		const float pad = 3.f;
		const float fs = std::max(9.f, semi::platform::kMinText + 1.f);
		const float labelW = 12.f;
		const float gx0 = pad + labelW, gx1 = w - pad;
		const float top = pad + fs + 3.f;
		const float drumH = (h - top - pad) * 0.34f;
		const float rowH = drumH / 3.f;
		const float by0 = top + drumH + 4.f, by1 = h - pad;
		const int steps = std::max(16, v.steps);
		const float cw = (gx1 - gx0) / steps;
		auto xOf = [&](int st) { return gx0 + st * cw; };

		// header
		char buf[48];
		if (s.drop1)
			std::snprintf(buf, sizeof buf, "DROP 1");
		else
			std::snprintf(buf, sizeof buf, "DROP II %c", 'A' + clamp(s.variant, 0, 3));
		text(vg, pad, pad, buf, fs, s.drop1 ? semi::theme::text() : semi::theme::cyan());
		if (!s.drop1) {
			std::snprintf(buf, sizeof buf, "ID %d%%", (int) std::lround(v.id * 100.f));
			text(vg, w * 0.5f, pad, buf, fs, semi::theme::magenta(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		}
		NVGcolor sc = semi::theme::dim();
		if (s.learn == 1) {
			std::snprintf(buf, sizeof buf, "LEARN @BAR");
			sc = semi::theme::amber();
		}
		else if (s.learn == 2) {
			std::snprintf(buf, sizeof buf, "REC %d/%d", s.recBar + 1, s.recBars);
			sc = semi::theme::magenta();
		}
		else if (s.mode == 0)
			std::snprintf(buf, sizeof buf, "NO CLOCK");
		else
			std::snprintf(buf, sizeof buf, "%s %d", s.mode == 2 ? "INT" : "CLK", (int) std::lround(s.bpm));
		text(vg, w - pad, pad, buf, fs, sc, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);

		// bar lines
		Batch bt{vg};
		for (int st = 0; st <= steps; st += 16)
			bt.add(xOf(st) - 0.5f, top, 1.f, h - top - pad);
		bt.fill(semi::theme::grid());

		// drum lanes
		static const char* lbl[3] = {"K", "S", "H"};
		for (int r = 0; r < 3; r++)
			text(vg, gx0 - 3.f, top + rowH * (r + 0.5f), lbl[r], 8.f, semi::theme::dim(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
		const float hw = std::max(1.6f, cw - 0.6f);
		for (int pass = 0; pass < 3; pass++) {  // 0 same, 1 changed, 2 hats (dim)
			for (int i = 0; i < v.nHits; i++) {
				const Hit& hit = v.hits[i];
				const int row = hit.role == (int) dr2::Role::Kick ? 0 : hit.role == (int) dr2::Role::Snare ? 1 : 2;
				const bool changed = hit.tag != dr2::Same;
				const int p = changed ? 1 : (row == 2 ? 2 : 0);
				if (p != pass)
					continue;
				bt.add(xOf(hit.step), top + row * rowH + 0.8f, hw, rowH - 1.6f);
			}
			bt.fill(pass == 0 ? semi::theme::cyan() : pass == 1 ? semi::theme::amber() : semi::theme::cyanDim());
		}

		// bass roll
		int lo = 127, hi = 0;
		for (int i = 0; i < v.nBass; i++) {
			lo = std::min(lo, (int) v.bass[i].pitch);
			hi = std::max(hi, (int) v.bass[i].pitch);
		}
		text(vg, gx0 - 3.f, (by0 + by1) * 0.5f, "B", 8.f, semi::theme::dim(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
		if (v.nBass > 0) {
			lo -= 1;
			hi += 1;
			const float span = (float) std::max(6, hi - lo);
			const float nh = std::max(2.4f, (by1 - by0) / (span + 1.f));
			for (int pass = 0; pass < 3; pass++) {  // call same, changed, response
				for (int i = 0; i < v.nBass; i++) {
					const BNote& n = v.bass[i];
					const int p = n.voice == 1 ? 2 : (n.tag != dr2::Same ? 1 : 0);
					if (p != pass)
						continue;
					const float y = by1 - nh - (n.pitch - lo) / span * (by1 - by0 - nh);
					bt.add(xOf(n.start), y, std::max(1.6f, n.len * cw - 0.6f), nh);
				}
				bt.fill(pass == 0 ? semi::theme::cyan() : pass == 1 ? semi::theme::amber() : semi::theme::magenta());
			}
		}
		else {
			text(vg, (gx0 + gx1) * 0.5f, (by0 + by1) * 0.5f, "NO BASS", fs, semi::theme::dim(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		}

		// playhead
		if (s.playhead >= 0.f) {
			const float x = gx0 + s.playhead * (gx1 - gx0);
			fillRect(vg, x - 0.8f, top, 1.6f, h - top - pad, semi::theme::amber());
		}
	}
};

struct Drop2Widget : ModuleWidget {
	Drop2Widget(Drop2* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Drop2.svg")));

		auto* scr = semi::createScreenMm<DropScreen>(Vec(L::VIEW.x, L::VIEW.y), Vec(L::VIEW.w, L::VIEW.h));
		scr->module = module;
		addChild(scr);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		const L::P moves[kMoves] = {L::MOVE0, L::MOVE1, L::MOVE2, L::MOVE3, L::MOVE4};
		const L::P leds[kMoves] = {L::MOVE_LED0, L::MOVE_LED1, L::MOVE_LED2, L::MOVE_LED3, L::MOVE_LED4};
		for (int i = 0; i < kMoves; i++) {
			addParam(createParamCentered<semi::Button>(at(moves[i]), module, Drop2::MOVE_PARAM + i));
			addChild(createLightCentered<semi::CyanLedSmall>(at(leds[i]), module, Drop2::MOVE_LIGHT + i));
		}
		addParam(createParamCentered<semi::KnobLHot>(at(L::IDENTITY), module, Drop2::IDENTITY_PARAM));
		auto* var = createParamCentered<semi::KnobM>(at(L::VARIANT), module, Drop2::VARIANT_PARAM);
		var->snap = true;
		addParam(var);
		addParam(createParamCentered<semi::Button>(at(L::NEW), module, Drop2::NEW_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::PLAY), module, Drop2::PLAY_PARAM));
		addChild(createLightCentered<semi::AmberLedSmall>(at(L::PLAY_LED), module, Drop2::PLAY_LIGHT));
		addParam(createParamCentered<semi::Button>(at(L::LEARN), module, Drop2::LEARN_PARAM));
		addChild(createLightCentered<semi::MagentaLedSmall>(at(L::LEARN_LED), module, Drop2::LEARN_LIGHT));

		addInput(createInputCentered<semi::Jack>(at(L::CLK), module, Drop2::CLK_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RST), module, Drop2::RST_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::ID_CV), module, Drop2::ID_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::KICK_IN), module, Drop2::KICK_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::SNARE_IN), module, Drop2::SNARE_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::HAT_IN), module, Drop2::HAT_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::BASS_VOCT), module, Drop2::BASS_VOCT_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::BASS_GATE), module, Drop2::BASS_GATE_INPUT));

		addOutput(createOutputCentered<semi::Jack>(at(L::KICK_OUT), module, Drop2::KICK_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::SNARE_OUT), module, Drop2::SNARE_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::HAT_OUT), module, Drop2::HAT_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::CALL_VOCT), module, Drop2::CALL_VOCT_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::CALL_GATE), module, Drop2::CALL_GATE_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::RESP_VOCT), module, Drop2::RESP_VOCT_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::RESP_GATE), module, Drop2::RESP_GATE_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::AUDIO), module, Drop2::AUDIO_OUTPUT));
	}

#ifndef METAMODULE
	// Rack: generation runs here, on the UI thread (MetaModule: Drop2::worker).
	void step() override {
		if (auto* m = dynamic_cast<Drop2*>(module))
			m->service();
		ModuleWidget::step();
	}
#endif

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Drop2*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Preset", kPresetNames, [=]() { return (size_t) m->preset; },
		                                      [=](size_t i) { m->applyPreset((int) i); }));
		menu->addChild(createMenuItem("New seed", "", [=]() { m->seed = (int) (random::u32() % 9999u) + 1; }));

		menu->addChild(createMenuLabel("Moves"));
		menu->addChild(createBoolPtrMenuItem("Half-time keeps the hats", "", &m->cfg.htHats));
		menu->addChild(createIndexSubmenuItem("Kick shift amount", kPctNames, [=]() { return (size_t) m->cfg.kickAmt; },
		                                      [=](size_t i) { m->cfg.kickAmt = (int) i; }));
		menu->addChild(createIndexSubmenuItem("Kick shift max", {"1/16", "2/16", "3/16"}, [=]() { return (size_t) m->cfg.kickMax; },
		                                      [=](size_t i) { m->cfg.kickMax = (int) i; }));
		menu->addChild(createIndexSubmenuItem("Bass lock amount", kPctNames, [=]() { return (size_t) m->cfg.lockAmt; },
		                                      [=](size_t i) { m->cfg.lockAmt = (int) i; }));
		menu->addChild(createIndexSubmenuItem("Bass lock to", {"Kicks", "Kick + snare"}, [=]() { return (size_t) m->cfg.lockTo; },
		                                      [=](size_t i) { m->cfg.lockTo = (int) i; }));
		menu->addChild(createIndexSubmenuItem("Flip mode", {"Up 8va", "Down 8va", "Alternate", "Invert"},
		                                      [=]() { return (size_t) m->cfg.flipMode; }, [=](size_t i) { m->cfg.flipMode = (int) i; }));
		menu->addChild(createIndexSubmenuItem("Flip amount", kPctNames, [=]() { return (size_t) m->cfg.flipAmt; },
		                                      [=](size_t i) { m->cfg.flipAmt = (int) i; }));
		menu->addChild(createIndexSubmenuItem("Call unit", {"1/2 bar", "1 bar", "2 bars"}, [=]() { return (size_t) m->cfg.splitUnit; },
		                                      [=](size_t i) { m->cfg.splitUnit = (int) i; }));
		menu->addChild(createIndexSubmenuItem("Response interval", {"Unison", "+3", "+5", "+7", "+12", "-5", "-12"},
		                                      [=]() { return (size_t) m->cfg.interval; }, [=](size_t i) { m->cfg.interval = (int) i; }));

		menu->addChild(createMenuLabel("Drop 1"));
		menu->addChild(createIndexSubmenuItem("Learn length", kRecNames, [=]() { return (size_t) m->recIdx; },
		                                      [=](size_t i) { m->recIdx = (int) i; }));
		menu->addChild(createMenuItem("Load the example drop", "", [=]() {
			m->lock();
			m->srcDrums = demoDrums();
			m->srcBass = demoBass();
			m->srcVersion++;
			m->unlock();
		}));
		menu->addChild(createMenuItem("Clear drums", "", [=]() {
			m->lock();
			dr2::Clip c;
			c.length = std::max(4.0, std::max(m->srcDrums.length, m->srcBass.length));
			m->srcDrums = c;
			m->srcVersion++;
			m->unlock();
		}));
		menu->addChild(createMenuItem("Clear bass", "", [=]() {
			m->lock();
			dr2::Clip c;
			c.length = std::max(4.0, std::max(m->srcDrums.length, m->srcBass.length));
			m->srcBass = c;
			m->srcVersion++;
			m->unlock();
		}));

		menu->addChild(createMenuLabel("Clock"));
		menu->addChild(createIndexSubmenuItem("CLK resolution", kClockNames, [=]() { return (size_t) m->clockIdx; },
		                                      [=](size_t i) { m->clockIdx = (int) i; }));
		menu->addChild(createIndexSubmenuItem("Internal tempo (CLK unpatched)", kTempoNames, [=]() { return (size_t) m->tempoIdx; },
		                                      [=](size_t i) { m->tempoIdx = (int) i; }));
		menu->addChild(createIndexSubmenuItem("Preview kit level", kLevelNames, [=]() { return (size_t) m->levelIdx; },
		                                      [=](size_t i) { m->levelIdx = (int) i; }));
	}
};

Model* modelDrop2 = createModel<Drop2, Drop2Widget>("Drop2");
