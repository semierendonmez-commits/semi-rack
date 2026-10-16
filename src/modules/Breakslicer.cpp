// BREAKSLICER — Rack / MetaModule port of BREAKSLICER, the generative tracker break slicer
// (engine: engines/breakslicer, vendored from semierendonmez-commits/breakslicer Source/engine;
// one change: the pad router is seeded explicitly, no std::random_device on MetaModule).
//
// The break is analysed (onset detection, K / S / G / H classes), a Markov chain writes a tracker
// pattern of 16 / 32 / 64 rows from the break's own hits plus recuts (CHOP), with 09xx offsets,
// Rxy retriggers, drills and a routing matrix (PASS / PITCH / SUB / REV / DRILL), played through
// a vintage sampler (CLEAN 16, AKAI S950, AKAI S1000, AMIGA PAULA).
//   * CLK (1/16 by default) + RST drive the pattern; the tempo is the clock's.
//   * No CLK: RUN (pad or gate) runs the internal clock at the menu tempo.
//   * GATE + SLICE (1/12 V per slice from 0 V) play slices directly, each through its own chain
//     (the plugin's MIDI notes 36 and up).
// Outputs: L / R (5 V = full scale), ROW (a trigger on every pattern row), and KICK / SNARE /
// GHOST / HAT triggers for every hit of that class (the plugin's MIDI out, drum notes).
// Breaks: WAV from the menu (30 s max; stereo in Rack, mono-summed on MetaModule to save RAM),
// path saved in the patch. Until one is loaded a synthetic two-bar break plays.
// Dropped: the four LFOs and mod slots (patch CV instead: CHOP has an input), the randomiser /
// evolve / undo, pattern cell editing and pinning, presets (they name embedded breaks), MIDI out
// as MIDI (class triggers instead), themes, the scope.

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/Sample.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/BreakslicerLayout.hpp"
#include "SlicerCommon.hpp"
#include "breakslicer/Engine.h"

#include <array>
#include <atomic>
#include <memory>

namespace L = semi::layout::breakslicer;
using semi::Screen;

namespace {

constexpr int kBlock = 32;          // engine block; adds 32 samples of latency
constexpr float kVoltsPerUnit = 5.f;
constexpr int kPageRows = 16;       // rows on screen (two columns of eight)

// Choice tables from the plugin (Source/Params.h).
const std::vector<std::string> kOdfNames = {"SF", "WPD", "RCD", "QMF", "HYBRID"};
const std::vector<std::string> kSliceModes = {"ONSETS", "GRID 8", "GRID 16", "GRID 32"};
const std::vector<std::string> kBarNames = {"AUTO", "1 BAR", "2 BARS", "4 BARS", "8 BARS"};
constexpr int kBarValues[] = {0, 1, 2, 4, 8};
const std::vector<std::string> kFillNames = {"OFF", "EVERY BAR", "2 BARS", "4 BARS", "8 BARS"};
constexpr int kFillValues[] = {0, 1, 2, 4, 8};
const std::vector<std::string> kRvolNames = {"ANY", "DECAY", "BOOST", "EXP"};
const std::vector<std::string> kRowNames = {"16", "32", "64"};
constexpr int kRowValues[] = {16, 32, 64};
const std::vector<std::string> kSpeedNames = {"1/32", "1/16", "1/8"};
constexpr int kSpeedValues[] = {3, 6, 12};
const std::vector<std::string> kMachines = {"CLEAN 16", "AKAI S950", "AKAI S1000", "AMIGA PAULA"};
const std::vector<std::string> kStyles = {"FROM THIS BREAK", "ROLLER", "AMEN CHOPS", "STEPPER", "GHOST TRAIL",
                                          "SNARE ROLLS", "KICK DRIVE", "HAT SHUFFLE", "EVEN"};

using Matrix = std::array<std::array<float, 4>, 4>;
// BreakSlicerAudioProcessor::styleMatrix (index 0, learned, is built from the kit).
Matrix styleMatrix(int index) {
	static const Matrix styles[] = {
		{},
		{{{.10f, .55f, .10f, .25f}, {.45f, .05f, .25f, .25f}, {.35f, .35f, .10f, .20f}, {.40f, .35f, .15f, .10f}}},
		{{{.15f, .40f, .20f, .25f}, {.40f, .10f, .30f, .20f}, {.30f, .30f, .20f, .20f}, {.35f, .35f, .15f, .15f}}},
		{{{.05f, .70f, .05f, .20f}, {.70f, .05f, .05f, .20f}, {.50f, .30f, .05f, .15f}, {.45f, .45f, .05f, .05f}}},
		{{{.10f, .20f, .55f, .15f}, {.15f, .05f, .65f, .15f}, {.20f, .25f, .40f, .15f}, {.15f, .20f, .50f, .15f}}},
		{{{.10f, .70f, .10f, .10f}, {.05f, .80f, .10f, .05f}, {.10f, .70f, .10f, .10f}, {.10f, .70f, .10f, .10f}}},
		{{{.55f, .25f, .05f, .15f}, {.60f, .10f, .15f, .15f}, {.55f, .20f, .10f, .15f}, {.55f, .20f, .10f, .15f}}},
		{{{.10f, .25f, .10f, .55f}, {.15f, .05f, .15f, .65f}, {.15f, .20f, .10f, .55f}, {.20f, .25f, .10f, .45f}}},
		{{{.25f, .25f, .25f, .25f}, {.25f, .25f, .25f, .25f}, {.25f, .25f, .25f, .25f}, {.25f, .25f, .25f, .25f}}},
	};
	return styles[clamp(index, 1, 8)];
}

NVGcolor clsColor(int c) {
	switch (c & 3) {
		case gtbs::Kick: return semi::theme::amber();
		case gtbs::Snare: return semi::theme::magenta();
		case gtbs::Ghost: return nvgRGB(0xc8, 0x9a, 0x5a);
		default: return semi::theme::text();
	}
}

std::string hex2(int v) { return string::f("%02X", v & 0xff); }
std::string hex1(int v) { return string::f("%X", v & 0xf); }

// The tracker columns, as the plugin's pattern view prints them.
std::string noteText(const gtbs::Cell& c) {
	if (c.trig == 2) return "^^^^";
	if (c.trig != 1) return "....";
	return std::string(1, gtbs::clsLetter(c.cls)) + (c.natural ? "-" : "*") + hex2(c.slice);
}
std::string rxyText(const gtbs::Cell& c) {
	if (c.trig != 1 || c.ry == 0) return "....";
	const char m = c.rmode == gtbs::RvDecay ? '-' : c.rmode == gtbs::RvBoost ? '+' : c.rmode == gtbs::RvExp ? 'e' : '0';
	return std::string(1, c.drill ? 'D' : 'R') + std::string(1, m) + hex1(c.ramt) + hex1(c.ry);
}
std::string fxText(const gtbs::Cell& c) {
	if (c.trig != 1) return "...";
	if (c.chain == gtbs::PitchShift) return std::string(c.pitch >= 0 ? "P+" : "P-") + hex1(std::abs(c.pitch));
	return gtbs::chainShort(c.chain);
}

struct Built {
	std::shared_ptr<const gtbs::Kit> kit;
};

struct Snapshot {
	gtbs::PatternView pattern;
	int row = -1, pass = 0, variant = 0;
	bool playing = false;
	int mode = 0;          // 0 stopped, 1 clock, 2 internal
	float bpm = 0.f;
	int machine = 1;
	bool lock = false;
	int speed = 6;
	slc::Heads heads;
	int curSlice = -1;
	int slices = 0;
};

} // namespace

struct Breakslicer : Module {
	enum ParamId {
		CHOP_PARAM, KEEP_PARAM, DENSITY_PARAM, VARY_PARAM, LEVEL_PARAM,
		ROLLS_PARAM, DRILL_PARAM, FILLS_PARAM, SWING_PARAM, RATE_PARAM,
		MACHINE_PARAM, NEW_PARAM, LOCK_PARAM, RUN_PARAM,
		PARAMS_LEN
	};
	enum InputId { CLK_INPUT, RST_INPUT, RUN_INPUT, CHOP_INPUT, NEW_INPUT, GATE_INPUT, SLICE_INPUT, INPUTS_LEN };
	enum OutputId { OUT_L_OUTPUT, OUT_R_OUTPUT, ROW_OUTPUT, KICK_OUTPUT, SNARE_OUTPUT, GHOST_OUTPUT, HAT_OUTPUT, OUTPUTS_LEN };
	enum LightId { ENUMS(MACHINE_LIGHT, 4), LOCK_LIGHT, RUN_LIGHT, LIGHTS_LEN };

	gtbs::Engine engine;
	gtbs::GenParams gp;
	gtbs::PlayParams pp;

	// ---- saved state (menu + buttons)
	int machine = gtbs::AkaiS950;
	bool lockOn = false, runOn = false;
	int seed = 1;
	// slicing / analysis (rebuild)
	int odf = gtbs::OdfHybrid, sliceMode = 0, barsSel = 0;
	float sens = 0.6f, lambda = 10.f, alpha = 0.25f, minGap = 45.f;
	bool backtrack = true;
	float driveDb = 0.f;
	// generator
	bool snapOffsets = true;
	int fillEvery = 3, rvol = 0, ramt = 4, drillY = 6, rowsSel = 1, speedSel = 1, pitch = 7, style = 2;
	bool pitchBoth = true;
	float shuffle = 0.15f, barFeel = 0.25f, sweep = 0.f, human = 0.2f;
	float weights[gtbs::NumChains] = {0.55f, 0.25f, 0.08f, 0.06f, 0.06f};
	Matrix markov = styleMatrix(2);
	// sound
	float mfilter = 0.85f, revSize = 0.6f, revDamp = 0.35f, revMix = 0.6f, gate = 1.f, decayMs = 40.f, xfadeMs = 2.f;
	int amiga = 0, fit = 0;
	bool led = false;
	float internalBpm = 170.f;
	std::string breakPath;

	// ---- loading (the builder owns the cache; Rack: UI thread, MetaModule: AsyncThread)
	semi::Slot<Built> slot;
	struct Cache {
		std::string path = "\x01";
		std::shared_ptr<gtbs::Sample> raw;           // decoded break
		std::shared_ptr<const gtbs::Sample> analysed; // + analysis, shared by every kit of it
		gtbs::AnalysisParams ap;
		int serial = 0, kitSerial = 0;
		std::shared_ptr<const gtbs::Kit> keep[3];   // the last kits: voices fade out of them after a swap
	} cache;
	semi::TripleBuffer<slc::WaveView> wave;
	float learned[4][4] = {};   // written by the builder, read by the menu (FROM THIS BREAK)
	std::atomic<bool> rebuildWanted{false};
	bool requested = false;
	int lastKey = 0;
	float keyStable = 0.f;
	bool keyPending = false;
	int numSlices = 0;

	// ---- clock / gate / triggers
	slc::ClockFollower clock;
	dsp::SchmittTrigger gateTrig, newTrig, runGate;
	dsp::BooleanTrigger machineBtn, newBtn, lockBtn, runBtn;
	int gateStep = 0, mode = 0;
	double clkPpq = 0.0, clkBpm = 0.0;
	bool clkRun = false;
	gtbs::PadEvent pads[16];
	int numPads = 0;

	// ---- block adapter / outputs
	float outBuf[2][kBlock]{};
	float blockL[kBlock]{}, blockR[kBlock]{};
	int pos = 0;
	int lastRow = -1;
	dsp::PulseGenerator rowPulse, clsPulse[4];
	gtbs::PatternView pattern;
	uint32_t rng = 0x6d2b79f5u;

	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;
	// last: destroyed first, so its thread (MetaModule) stops before anything it builds into goes
	semi::Loader<Built> loader;

	Breakslicer() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(CHOP_PARAM, 0.f, 1.f, 0.25f, "Chop (how much of the break is recut)", " %", 0.f, 100.f);
		configParam(KEEP_PARAM, 0.f, 1.f, 0.6f, "Keep beats", " %", 0.f, 100.f);
		configParam(DENSITY_PARAM, 0.f, 1.f, 0.5f, "Density (50 % = the break's own hits)", " %", 0.f, 100.f);
		configParam(VARY_PARAM, 0.f, 1.f, 0.f, "Variation", " %", 0.f, 100.f);
		configParam(LEVEL_PARAM, -36.f, 6.f, 0.f, "Level", " dB");
		configParam(ROLLS_PARAM, 0.f, 1.f, 0.12f, "Rolls (Rxy retrigger chance)", " %", 0.f, 100.f);
		configParam(DRILL_PARAM, 0.f, 1.f, 0.05f, "Drill chance", " %", 0.f, 100.f);
		configParam(FILLS_PARAM, 0.f, 1.f, 0.35f, "Fills", " %", 0.f, 100.f);
		configParam(SWING_PARAM, 0.f, 1.f, 0.f, "Swing", " %", 0.f, 100.f);
		configParam(RATE_PARAM, 3.5f, 48.f, 31.25f, "Sampler clock (S950 7.5-48, Paula 3.5-28.8)", " kHz");
		configButton(MACHINE_PARAM, "Sampler (CLEAN 16 / S950 / S1000 / PAULA)");
		configButton(NEW_PARAM, "New pattern (same settings, new dice)");
		configButton(LOCK_PARAM, "Pattern lock");
		configButton(RUN_PARAM, "Run (internal clock when CLK is not patched)");
		configInput(CLK_INPUT, "Clock (1/16 by default)");
		configInput(RST_INPUT, "Reset (next clock = row 00)");
		configInput(RUN_INPUT, "Run gate (internal clock)");
		configInput(CHOP_INPUT, "Chop CV (+-5 V = +-50 %)");
		configInput(NEW_INPUT, "New pattern trigger");
		configInput(GATE_INPUT, "Gate (plays a slice)");
		configInput(SLICE_INPUT, "Slice (1/12 V per slice from 0 V; unpatched = next slice)");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		configOutput(ROW_OUTPUT, "Row trigger");
		configOutput(KICK_OUTPUT, "Kick hit trigger");
		configOutput(SNARE_OUTPUT, "Snare hit trigger");
		configOutput(GHOST_OUTPUT, "Ghost hit trigger");
		configOutput(HAT_OUTPUT, "Hat hit trigger");
		configLight(MACHINE_LIGHT + 0, "CLEAN 16");
		configLight(MACHINE_LIGHT + 1, "AKAI S950");
		configLight(MACHINE_LIGHT + 2, "AKAI S1000");
		configLight(MACHINE_LIGHT + 3, "AMIGA PAULA");

		engine.prepare(44100.0, kBlock);
		loader.attach(this, &slot, [this](const std::string& path, Built& b) { return build(path, b); });
#ifdef METAMODULE
		requestBreak("");
#endif
	}

	void onAdd(const AddEvent& e) override {
		Module::onAdd(e);
		if (!requested)
			requestBreak(breakPath);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		engine.prepare(e.sampleRate, kBlock);
		pos = 0;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		machine = gtbs::AkaiS950;
		lockOn = runOn = false;
		seed = 1;
		engine.reset();
	}

	void requestBreak(const std::string& path) {
		requested = true;
		breakPath = path;
		loader.request(path);
	}

	// ---- builder ---------------------------------------------------------------------------------
	bool build(const std::string& path, Built& out) {
		if (path != cache.path) {
			slc::Break b;
			if (path.empty())
				slc::synthBreak(b);
#ifdef METAMODULE
			else if (!slc::loadBreak(path, b, false))
#else
			else if (!slc::loadBreak(path, b, true))
#endif
				return false;
			auto s = std::make_shared<gtbs::Sample>();
			s->name = b.name;
			s->rate = b.rate;
			s->frames = b.frames();
			s->l = std::move(b.l);
			s->r = std::move(b.r);
			s->serial = ++cache.serial;
			cache.raw = s;
			cache.analysed.reset();
			cache.path = path;
		}
		gtbs::AnalysisParams ap;
		ap.odf = odf;
		ap.sensitivity = sens;
		ap.lambda = lambda;
		ap.alpha = alpha;
		ap.minGapMs = minGap;
		ap.backtrack = backtrack;
		if (!cache.analysed || !(ap == cache.ap)) {
			auto withAn = std::make_shared<gtbs::Sample>(*cache.raw);
			withAn->an = gtbs::analyse(*cache.raw, ap);
			cache.analysed = withAn;
			cache.ap = ap;
		}
		gtbs::MachineParams mp;
		mp.machine = machine;
		mp.rateKHz = params[RATE_PARAM].getValue();
		mp.driveDb = driveDb;
		std::unique_ptr<gtbs::Kit> k = gtbs::buildKit(cache.analysed, sliceMode, kBarValues[clamp(barsSel, 0, 4)], mp, clkBpm);
		k->serial = ++cache.kitSerial;
		std::shared_ptr<const gtbs::Kit> kit(k.release());
		cache.keep[2] = cache.keep[1];
		cache.keep[1] = cache.keep[0];
		cache.keep[0] = kit;
		out.kit = kit;

		// learned Markov matrix (style FROM THIS BREAK)
		float m[4][4];
		for (auto& row : m)
			for (float& v : row)
				v = 0.3f;
		const auto& c = kit->slices.cls;
		for (size_t i = 0; i < c.size(); ++i)
			m[c[i] & 3][c[(i + 1) % c.size()] & 3] += 1.f;
		for (int i = 0; i < 4; i++) {
			float sum = 0.f;
			for (int j = 0; j < 4; j++)
				sum += m[i][j];
			for (int j = 0; j < 4; j++)
				learned[i][j] = m[i][j] / sum;
		}

		// the screen's copy
		const gtbs::Sample& s = *cache.analysed;
		slc::WaveView& v = wave.writeSlot();
		if (s.r.empty())
			v.setWave(s.l.data(), s.frames);
		else {
			std::vector<float> mono((size_t) s.frames);
			for (int i = 0; i < s.frames; i++)
				mono[(size_t) i] = 0.5f * (s.l[(size_t) i] + s.r[(size_t) i]);
			v.setWave(mono.data(), s.frames);
		}
		const int ns = std::min(kit->numSlices(), slc::WaveView::kMarks);
		for (int i = 0; i < ns; i++) {
			v.mark[i] = (float) kit->slices.starts[(size_t) i] / (float) std::max(1, s.frames);
			v.tag[i] = kit->slices.cls[(size_t) i] & 3;
		}
		v.marks = ns;
		v.beats = kit->bars * 4;
		v.setName(s.name);
		wave.publish();
		return true;
	}

	int buildKey() {
		int k = (int) std::round(params[RATE_PARAM].getValue() * 4.f);
		k = k * 5 + machine;
		k = k * 6 + odf;
		k = k * 5 + sliceMode;
		k = k * 6 + barsSel;
		k = k * 101 + (int) std::round(sens * 100.f);
		k = k * 41 + (int) std::round(lambda * 2.f);
		k = k * 41 + (int) std::round(alpha * 100.f);
		k = k * 151 + (int) std::round(minGap);
		k = k * 2 + (backtrack ? 1 : 0);
		k = k * 37 + (int) std::round(driveDb * 2.f);
		return k;
	}

	void applyStyle(int i) {
		style = clamp(i, 0, (int) kStyles.size() - 1);
		if (style == 0) {
			for (int a = 0; a < 4; a++)
				for (int b = 0; b < 4; b++)
					markov[(size_t) a][(size_t) b] = learned[a][b];
		}
		else {
			markov = styleMatrix(style);
		}
	}

	// ---- patch -----------------------------------------------------------------------------------
	json_t* dataToJson() override {
		json_t* r = json_object();
		json_object_set_new(r, "path", json_string(breakPath.c_str()));
		json_object_set_new(r, "machine", json_integer(machine));
		json_object_set_new(r, "lock", json_boolean(lockOn));
		json_object_set_new(r, "run", json_boolean(runOn));
		json_object_set_new(r, "seed", json_integer(seed));
		json_object_set_new(r, "odf", json_integer(odf));
		json_object_set_new(r, "sliceMode", json_integer(sliceMode));
		json_object_set_new(r, "bars", json_integer(barsSel));
		json_object_set_new(r, "sens", json_real(sens));
		json_object_set_new(r, "lambda", json_real(lambda));
		json_object_set_new(r, "alpha", json_real(alpha));
		json_object_set_new(r, "mingap", json_real(minGap));
		json_object_set_new(r, "backtrack", json_boolean(backtrack));
		json_object_set_new(r, "drive", json_real(driveDb));
		json_object_set_new(r, "snap", json_boolean(snapOffsets));
		json_object_set_new(r, "fillEvery", json_integer(fillEvery));
		json_object_set_new(r, "rvol", json_integer(rvol));
		json_object_set_new(r, "ramt", json_integer(ramt));
		json_object_set_new(r, "drillY", json_integer(drillY));
		json_object_set_new(r, "rows", json_integer(rowsSel));
		json_object_set_new(r, "speed", json_integer(speedSel));
		json_object_set_new(r, "pitch", json_integer(pitch));
		json_object_set_new(r, "pitchBoth", json_boolean(pitchBoth));
		json_object_set_new(r, "style", json_integer(style));
		json_object_set_new(r, "shuffle", json_real(shuffle));
		json_object_set_new(r, "barFeel", json_real(barFeel));
		json_object_set_new(r, "sweep", json_real(sweep));
		json_object_set_new(r, "human", json_real(human));
		json_t* w = json_array();
		for (float x : weights)
			json_array_append_new(w, json_real(x));
		json_object_set_new(r, "weights", w);
		json_t* mk = json_array();
		for (auto& row : markov)
			for (float x : row)
				json_array_append_new(mk, json_real(x));
		json_object_set_new(r, "markov", mk);
		json_object_set_new(r, "mfilter", json_real(mfilter));
		json_object_set_new(r, "revSize", json_real(revSize));
		json_object_set_new(r, "revDamp", json_real(revDamp));
		json_object_set_new(r, "revMix", json_real(revMix));
		json_object_set_new(r, "gate", json_real(gate));
		json_object_set_new(r, "decay", json_real(decayMs));
		json_object_set_new(r, "xfade", json_real(xfadeMs));
		json_object_set_new(r, "amiga", json_integer(amiga));
		json_object_set_new(r, "fit", json_integer(fit));
		json_object_set_new(r, "led", json_boolean(led));
		json_object_set_new(r, "bpm", json_real(internalBpm));
		json_object_set_new(r, "clock", json_integer(clock.mode));
		return r;
	}

	void dataFromJson(json_t* r) override {
		auto b = [&](const char* k, bool& v) { if (json_t* j = json_object_get(r, k)) v = json_boolean_value(j); };
		auto i = [&](const char* k, int& v, int lo, int hi) { if (json_t* j = json_object_get(r, k)) v = clamp((int) json_integer_value(j), lo, hi); };
		auto f = [&](const char* k, float& v, float lo, float hi) { if (json_t* j = json_object_get(r, k)) v = clamp((float) json_number_value(j), lo, hi); };
		i("machine", machine, 0, 3);
		b("lock", lockOn);
		b("run", runOn);
		i("seed", seed, 1, 9999);
		i("odf", odf, 0, gtbs::NumOdf - 1);
		i("sliceMode", sliceMode, 0, 3);
		i("bars", barsSel, 0, 4);
		f("sens", sens, 0.f, 1.f);
		f("lambda", lambda, 1.f, 20.f);
		f("alpha", alpha, 0.1f, 0.4f);
		f("mingap", minGap, 20.f, 150.f);
		b("backtrack", backtrack);
		f("drive", driveDb, 0.f, 18.f);
		b("snap", snapOffsets);
		i("fillEvery", fillEvery, 0, 4);
		i("rvol", rvol, 0, 3);
		i("ramt", ramt, 1, 8);
		i("drillY", drillY, 1, 15);
		i("rows", rowsSel, 0, 2);
		i("speed", speedSel, 0, 2);
		i("pitch", pitch, 1, 12);
		b("pitchBoth", pitchBoth);
		i("style", style, 0, (int) kStyles.size() - 1);
		f("shuffle", shuffle, 0.f, 1.f);
		f("barFeel", barFeel, 0.f, 1.f);
		f("sweep", sweep, -24.f, 24.f);
		f("human", human, 0.f, 1.f);
		if (json_t* w = json_object_get(r, "weights"))
			for (int k = 0; k < gtbs::NumChains && k < (int) json_array_size(w); k++)
				weights[k] = clamp((float) json_number_value(json_array_get(w, k)), 0.f, 1.f);
		if (json_t* mk = json_object_get(r, "markov"))
			for (int k = 0; k < 16 && k < (int) json_array_size(mk); k++)
				markov[(size_t) (k / 4)][(size_t) (k % 4)] = clamp((float) json_number_value(json_array_get(mk, k)), 0.f, 1.f);
		f("mfilter", mfilter, 0.f, 1.f);
		f("revSize", revSize, 0.f, 1.f);
		f("revDamp", revDamp, 0.f, 1.f);
		f("revMix", revMix, 0.f, 1.f);
		f("gate", gate, 0.05f, 1.f);
		f("decay", decayMs, 2.f, 400.f);
		f("xfade", xfadeMs, 0.2f, 12.f);
		i("amiga", amiga, 0, 1);
		i("fit", fit, 0, 1);
		b("led", led);
		f("bpm", internalBpm, 60.f, 240.f);
		i("clock", clock.mode, 0, slc::ClockFollower::kNumModes - 1);
		std::string path;
		if (json_t* j = json_object_get(r, "path"))
			path = json_string_value(j);
		lastKey = buildKey();
		requestBreak(path);
	}

	// ---- audio -----------------------------------------------------------------------------------
	void fillParams() {
		const float chopCv = inputs[CHOP_INPUT].getVoltage() / 10.f;
		gp.complexity = clamp(params[CHOP_PARAM].getValue() + chopCv, 0.f, 1.f);
		gp.anchor = params[KEEP_PARAM].getValue();
		gp.density = params[DENSITY_PARAM].getValue();
		gp.shuffle = shuffle;
		gp.barFeel = barFeel;
		gp.retrig = params[ROLLS_PARAM].getValue();
		gp.drill = params[DRILL_PARAM].getValue();
		gp.fill = params[FILLS_PARAM].getValue();
		gp.fillEvery = kFillValues[clamp(fillEvery, 0, 4)];
		gp.mutate = params[VARY_PARAM].getValue();
		gp.retrigVol = rvol;
		gp.retrigAmt = ramt;
		gp.drillY = drillY;
		gp.sweep = sweep;
		gp.human = human;
		for (int c = 0; c < gtbs::NumChains; c++)
			gp.weights[(size_t) c] = weights[c];
		for (int a = 0; a < 4; a++)
			for (int b = 0; b < 4; b++)
				gp.markov.p[(size_t) a][(size_t) b] = markov[(size_t) a][(size_t) b];
		gp.pitchSemis = pitch;
		gp.pitchBoth = pitchBoth;
		gp.rows = kRowValues[clamp(rowsSel, 0, 2)];
		gp.speed = kSpeedValues[clamp(speedSel, 0, 2)];
		gp.lock = lockOn;
		gp.seed = (uint32_t) seed;
		gp.snap = snapOffsets;

		// the controls as set, without CV: when these change the whole pattern follows at once,
		// CV only rewrites rows as the playhead reaches them (as the plugin does for its LFOs)
		uint32_t h = 0x5EEDu;
		auto q = [](float v) { return (uint32_t) std::lround(v * 40.0f); };
		for (float v : {barFeel, params[CHOP_PARAM].getValue(), gp.anchor, gp.density, shuffle, gp.retrig, gp.drill, gp.fill, human,
		                weights[0], weights[1], weights[2], weights[3], weights[4]})
			h = gtbs::hash32(h, q(v));
		h = gtbs::hash32(h, (uint32_t) std::lround(sweep) + 100u, gp.seed);
		for (auto& row : markov)
			for (float v : row)
				h = gtbs::hash32(h, q(v));
		h = gtbs::hash32(h, (uint32_t) (gp.fillEvery * 1000 + gp.retrigVol * 100 + gp.retrigAmt * 10),
		                 (uint32_t) (gp.drillY * 100 + gp.pitchSemis + (gp.pitchBoth ? 50 : 0)));
		gp.userKey = h | 1u;

		pp.swing = params[SWING_PARAM].getValue();
		pp.gate = gate;
		pp.decayMs = decayMs;
		pp.xfadeMs = xfadeMs;
		pp.level = std::pow(10.f, params[LEVEL_PARAM].getValue() / 20.f);
		pp.revSize = revSize;
		pp.revDamp = revDamp;
		pp.revReturn = revMix;
		pp.fit = fit;
		pp.mfilter = mfilter;
		pp.amigaModel = amiga;
		pp.led = led;
		const bool runIn = inputs[RUN_INPUT].isConnected() ? runGate.isHigh() : runOn;
		pp.run = !inputs[CLK_INPUT].isConnected() && runIn;
		pp.internalBpm = internalBpm;
	}

	void runBlock() {
		if (slot.poll()) {
			engine.setKit(slot.active->kit.get());
			numSlices = slot.active->kit->numSlices();
			gateStep = 0;
		}
		fillParams();
		gtbs::Transport t;
		t.valid = clkRun;
		t.playing = clkRun;
		t.ppq = clkPpq;
		t.bpm = clkRun ? clkBpm : 120.0;
		engine.process(blockL, blockR, kBlock, t, gp, pp, pads, numPads);
		numPads = 0;
		for (int i = 0; i < kBlock; i++) {
			outBuf[0][i] = blockL[i];
			outBuf[1][i] = blockR[i];
		}
		for (int h = 0; h < engine.numHits(); h++) {
			const gtbs::HitEvent& e = engine.hit(h);
			if (!e.repeat)
				clsPulse[e.cls & 3].trigger(1e-3f);
		}
		const int row = engine.viewRow.load(std::memory_order_relaxed);
		const bool playing = engine.viewPlaying.load(std::memory_order_relaxed) != 0;
		if (playing && row != lastRow)
			rowPulse.trigger(1e-3f);
		lastRow = playing ? row : -1;
	}

	void process(const ProcessArgs& args) override {
		if (machineBtn.process(params[MACHINE_PARAM].getValue() > 0.f))
			machine = (machine + 1) % gtbs::NumMachines;
		if (lockBtn.process(params[LOCK_PARAM].getValue() > 0.f))
			lockOn = !lockOn;
		if (runBtn.process(params[RUN_PARAM].getValue() > 0.f))
			runOn = !runOn;
		if (inputs[RUN_INPUT].isConnected())
			runGate.process(inputs[RUN_INPUT].getVoltage(), 0.1f, 1.f);
		if (newBtn.process(params[NEW_PARAM].getValue() > 0.f) | newTrig.process(inputs[NEW_INPUT].getVoltage(), 0.1f, 1.f)) {
			rng ^= rng << 13;
			rng ^= rng >> 17;
			rng ^= rng << 5;
			seed = 1 + (int) (rng % 9999u);
		}

		clkRun = clock.process(args.sampleTime, inputs[CLK_INPUT], inputs[RST_INPUT], clkPpq, clkBpm);
		const bool internal = !inputs[CLK_INPUT].isConnected() && (inputs[RUN_INPUT].isConnected() ? runGate.isHigh() : runOn);
		mode = clkRun ? 1 : internal ? 2 : 0;

		if (inputs[GATE_INPUT].isConnected() && gateTrig.process(inputs[GATE_INPUT].getVoltage(), 0.1f, 1.f) && numPads < 16) {
			int slice;
			if (inputs[SLICE_INPUT].isConnected())
				slice = clamp((int) std::round(inputs[SLICE_INPUT].getVoltage() * 12.f), 0, gtbs::kMaxSlices - 1);
			else
				slice = numSlices > 0 ? (gateStep++) % numSlices : 0;
			pads[numPads++] = {pos, slice, 1.f, true};
		}

		outputs[OUT_L_OUTPUT].setVoltage(outBuf[0][pos] * kVoltsPerUnit);
		outputs[OUT_R_OUTPUT].setVoltage(outBuf[1][pos] * kVoltsPerUnit);
		if (++pos >= kBlock) {
			pos = 0;
			runBlock();
		}
		outputs[ROW_OUTPUT].setVoltage(rowPulse.process(args.sampleTime) ? 10.f : 0.f);
		for (int c = 0; c < 4; c++)
			outputs[KICK_OUTPUT + c].setVoltage(clsPulse[c].process(args.sampleTime) ? 10.f : 0.f);

		if (ui.tick(args.sampleTime))
			publishUi();
	}

	void publishUi() {
		for (int i = 0; i < 4; i++)
			lights[MACHINE_LIGHT + i].setBrightness(machine == i ? 1.f : 0.f);
		lights[LOCK_LIGHT].setBrightness(lockOn ? 1.f : 0.f);
		lights[RUN_LIGHT].setBrightness(mode == 2 ? 1.f : (runOn ? 0.3f : 0.f));

		const int key = buildKey();
		if (key != lastKey) {
			lastKey = key;
			keyStable = 0.f;
			keyPending = true;
		}
		else if (keyPending) {
			keyStable += 1.f / semi::platform::kUiRateHz;
			if (keyStable >= 0.15f) {
				keyPending = false;
#ifdef METAMODULE
				loader.hasQueued.store(true, std::memory_order_release);
#else
				rebuildWanted.store(true);
#endif
			}
		}

		engine.patternOut.read(pattern);
		Snapshot& s = snap.writeSlot();
		s.pattern = pattern;
		s.playing = engine.viewPlaying.load(std::memory_order_relaxed) != 0;
		s.row = s.playing ? engine.viewRow.load(std::memory_order_relaxed) : -1;
		s.pass = engine.viewPass.load(std::memory_order_relaxed);
		s.variant = engine.viewVariant.load(std::memory_order_relaxed);
		s.mode = mode;
		s.bpm = mode == 1 ? (float) clkBpm : mode == 2 ? internalBpm : 0.f;
		s.machine = machine;
		s.lock = lockOn;
		s.speed = gp.speed;
		s.slices = numSlices;
		const gtbs::Kit* kit = engine.currentKit();
		const float frames = kit ? (float) std::max(1, kit->sample->frames) : 1.f;
		s.heads.n = 0;
		for (int k = 0; k < gtbs::Engine::kViews && s.heads.n < slc::Heads::kMax; k++) {
			const float h = engine.viewHead[(size_t) k].load(std::memory_order_relaxed);
			if (h < 0.f)
				continue;
			s.heads.pos[s.heads.n] = h / frames;
			s.heads.col[s.heads.n] = (uint8_t) (s.heads.n == 0 ? 0 : 1);
			s.heads.n++;
		}
		s.curSlice = engine.lastHitSlice.load(std::memory_order_relaxed);
		snap.publish();
	}
};

// The pattern: 16 rows a page in two columns (ROW NOTE 09xx Rxy FX VOL), the playing row on the
// cursor bar, pages flip as it plays. Header: row / length, pass, clock, sampler, VAR / FILL.
struct PatternScreen : Screen {
	Breakslicer* module = nullptr;
	Snapshot demo;

	PatternScreen() {
		demo.pattern.rows = 32;
		static const int cls[] = {0, 3, 2, 3, 1, 3, 2, 3};
		for (int r = 0; r < 32; r++) {
			gtbs::Cell& c = demo.pattern.cells[(size_t) r];
			c.trig = r % 4 == 3 && r % 8 != 7 ? 0 : 1;
			c.slice = (int16_t) (r % 18);
			c.cls = (uint8_t) cls[r % 8];
			c.off09 = (uint8_t) (r * 8);
			c.natural = r % 5 != 2;
			c.vol = (uint8_t) (r % 2 ? 0x30 : 0x40);
			if (r == 13) { c.ry = 4; c.ramt = 3; c.rmode = gtbs::RvDecay; }
			if (r == 22) { c.ry = 6; c.ramt = 2; c.drill = 1; c.rmode = gtbs::RvExp; }
			if (r == 9) c.chain = gtbs::SubOctave;
			if (r == 18) { c.chain = gtbs::PitchShift; c.pitch = 7; }
			if (r == 27) c.chain = gtbs::ReverseBuffer;
		}
		demo.row = 13;
		demo.playing = true;
		demo.mode = 1;
		demo.bpm = 172.f;
		demo.pass = 3;
		demo.machine = 1;
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		const float pad = 2.5f;
		const float fs = std::max(9.f, semi::platform::kMinText);
		const float cw = fs * 0.54f;
		const int rows = std::max(1, s.pattern.rows);
		const int cur = s.row >= 0 ? s.row % rows : -1;
		const int page = cur >= 0 ? cur / kPageRows : 0;
		const int first = page * kPageRows;

		// header
		char b[48];
		std::snprintf(b, sizeof b, "%s/%02X", cur >= 0 ? hex2(cur).c_str() : "--", rows);
		text(vg, pad, pad - 0.5f, b, fs, semi::theme::cyan());
		static const char* mach[] = {"CLEAN", "S950", "S1000", "PAULA"};
		if (s.mode == 1)
			std::snprintf(b, sizeof b, "CLK %3.0f  %s", s.bpm, mach[s.machine & 3]);
		else if (s.mode == 2)
			std::snprintf(b, sizeof b, "RUN %3.0f  %s", s.bpm, mach[s.machine & 3]);
		else
			std::snprintf(b, sizeof b, "STOP  %s", mach[s.machine & 3]);
		text(vg, w - pad, pad - 0.5f, b, fs, s.mode ? semi::theme::text() : semi::theme::dim(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		const char* tag = s.lock ? "LOCK" : s.variant == 2 ? "FILL" : s.variant == 1 ? "VAR" : nullptr;
		if (tag && s.playing)
			text(vg, pad + 8.f * cw, pad - 0.5f, tag, fs, semi::theme::magenta());
		else if (s.playing) {
			std::snprintf(b, sizeof b, "PASS %d", s.pass);
			text(vg, pad + 8.f * cw, pad - 0.5f, b, fs, semi::theme::dim());
		}

		const float y0 = pad + fs + 2.f;
		line(vg, pad, y0 - 1.f, w - pad, y0 - 1.f, semi::theme::grid());
		const float rowH = (h - y0 - pad) / 8.f;
		const float colW = (w - 2.f * pad) / 2.f;
		const int rowsPerBeat = std::max(1, 24 / std::max(1, s.speed));
		for (int k = 0; k < kPageRows && first + k < rows; k++) {
			const int r = first + k;
			const float x = pad + (k / 8) * colW;
			const float y = y0 + (k % 8) * rowH;
			const float ym = y + 0.5f * rowH;
			if (r == cur)
				fillRect(vg, x - 1.f, y, colW - 2.f, rowH, semi::theme::cyanDeep());
			const gtbs::Cell& c = s.pattern.cells[(size_t) r];
			const bool on = c.trig == 1;
			const int al = NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE;
			text(vg, x, ym, hex2(r), fs, r % rowsPerBeat == 0 ? semi::theme::text() : semi::theme::dim(), al);
			text(vg, x + 3.f * cw, ym, noteText(c), fs, on ? clsColor(c.cls) : semi::theme::dim(), al);
			text(vg, x + 8.f * cw, ym, on ? "09" + hex2(c.off09) : std::string("...."), fs, on ? semi::theme::text() : semi::theme::dim(), al);
			text(vg, x + 13.f * cw, ym, rxyText(c), fs,
			     on && c.ry > 0 ? (c.drill ? semi::theme::magenta() : semi::theme::amber()) : semi::theme::dim(), al);
			text(vg, x + 18.f * cw, ym, fxText(c), fs, on && c.chain != gtbs::PassThrough ? semi::theme::amber() : semi::theme::dim(), al);
			text(vg, x + 22.f * cw, ym, on ? hex2(c.vol) : std::string(".."), fs, on ? semi::theme::text() : semi::theme::dim(), al);
		}
		// column divider
		line(vg, pad + colW - 1.5f, y0, pad + colW - 1.5f, h - pad, semi::theme::grid());
	}
};

// The break: waveform, slices with their class tags (K S G H), beat grid, playheads.
struct BreakScreen : Screen {
	Breakslicer* module = nullptr;
	slc::WaveView demoWave;
	Snapshot demo;

	BreakScreen() {
		slc::Break b;
		slc::synthBreak(b, 8000.0);
		demoWave.setWave(b.l.data(), b.frames());
		static const int hits[] = {0, 2, 4, 6, 7, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 27, 28, 30};
		static const uint8_t cls[] = {0, 3, 1, 3, 2, 3, 0, 1, 3, 0, 3, 1, 3, 3, 0, 2, 1, 3};
		demoWave.marks = 18;
		for (int i = 0; i < 18; i++) {
			demoWave.mark[i] = hits[i] / 32.f;
			demoWave.tag[i] = cls[i];
		}
		demoWave.beats = 8;
		demoWave.setName("SYNTH BREAK");
		demo.heads.n = 1;
		demo.heads.pos[0] = 13.4f / 32.f;
		demo.curSlice = 7;
		demo.slices = 18;
	}

	void drawScreen(NVGcontext* vg) override {
		const slc::WaveView& v = module ? module->wave.read() : demoWave;
		const Snapshot& s = module ? module->snap.read() : demo;
		NVGcolor tagCols[8];
		for (int i = 0; i < 8; i++)
			tagCols[i] = clsColor(i);
		NVGcolor headCols[8];
		for (int i = 0; i < 8; i++)
			headCols[i] = i == 0 ? semi::theme::cyan() : semi::theme::amber();
		char right[16];
		std::snprintf(right, sizeof right, "%02X SL", s.slices);
		slc::drawWave(*this, vg, box.size.x, box.size.y, v, s.heads, "KSGH", tagCols, headCols, s.curSlice, right);
	}
};

namespace {

void addSteps(Menu* menu, const std::string& label, float* v, const std::vector<float>& steps, const char* fmt, float mult = 1.f) {
	std::vector<std::string> labels;
	for (float s : steps)
		labels.push_back(string::f(fmt, s * mult));
	menu->addChild(createIndexSubmenuItem(label, labels,
		[=]() {
			size_t best = 0;
			for (size_t i = 1; i < steps.size(); i++)
				if (std::abs(steps[i] - *v) < std::abs(steps[best] - *v))
					best = i;
			return best;
		},
		[=](size_t i) { *v = steps[i]; }));
}

void addIndex(Menu* menu, const std::string& label, const std::vector<std::string>& labels, int* v, int offset = 0) {
	menu->addChild(createIndexSubmenuItem(label, labels, [=]() { return (size_t) (*v - offset); }, [=](size_t i) { *v = (int) i + offset; }));
}

const std::vector<float> kPct = {0.f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.f};

} // namespace

struct BreakslicerWidget : ModuleWidget {
	BreakslicerWidget(Breakslicer* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Breakslicer.svg")));

		auto* ps = semi::createScreenMm<PatternScreen>(Vec(L::PATTERN.x, L::PATTERN.y), Vec(L::PATTERN.w, L::PATTERN.h));
		ps->module = module;
		addChild(ps);
		auto* bs = semi::createScreenMm<BreakScreen>(Vec(L::WAVE.x, L::WAVE.y), Vec(L::WAVE.w, L::WAVE.h));
		bs->module = module;
		addChild(bs);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		addParam(createParamCentered<semi::KnobMHot>(at(L::CHOP), module, Breakslicer::CHOP_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::KEEP), module, Breakslicer::KEEP_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::DENSITY), module, Breakslicer::DENSITY_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::VARY), module, Breakslicer::VARY_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::LEVEL), module, Breakslicer::LEVEL_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::ROLLS), module, Breakslicer::ROLLS_PARAM));
		addParam(createParamCentered<semi::KnobSHot>(at(L::DRILL), module, Breakslicer::DRILL_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::FILLS), module, Breakslicer::FILLS_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::SWING), module, Breakslicer::SWING_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::RATE), module, Breakslicer::RATE_PARAM));

		addParam(createParamCentered<semi::Button>(at(L::MACHINE), module, Breakslicer::MACHINE_PARAM));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::M_CLEAN), module, Breakslicer::MACHINE_LIGHT + 0));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::M_S950), module, Breakslicer::MACHINE_LIGHT + 1));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::M_S1000), module, Breakslicer::MACHINE_LIGHT + 2));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::M_PAULA), module, Breakslicer::MACHINE_LIGHT + 3));
		addParam(createParamCentered<semi::Button>(at(L::NEW), module, Breakslicer::NEW_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::LOCK), module, Breakslicer::LOCK_PARAM));
		addChild(createLightCentered<semi::MagentaLedSmall>(at(L::LOCK_LED), module, Breakslicer::LOCK_LIGHT));
		addParam(createParamCentered<semi::PadCyan>(at(L::RUN), module, Breakslicer::RUN_PARAM));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::RUN_LED), module, Breakslicer::RUN_LIGHT));

		addInput(createInputCentered<semi::Jack>(at(L::CLK), module, Breakslicer::CLK_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RST), module, Breakslicer::RST_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RUN_IN), module, Breakslicer::RUN_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::CHOP_CV), module, Breakslicer::CHOP_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::NEW_IN), module, Breakslicer::NEW_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::GATE), module, Breakslicer::GATE_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::SLICE), module, Breakslicer::SLICE_INPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_L), module, Breakslicer::OUT_L_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_R), module, Breakslicer::OUT_R_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::ROW), module, Breakslicer::ROW_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::KICK), module, Breakslicer::KICK_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::SNARE), module, Breakslicer::SNARE_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::GHOST), module, Breakslicer::GHOST_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::HAT), module, Breakslicer::HAT_OUTPUT));
	}

#ifndef METAMODULE
	void step() override {
		if (auto* m = dynamic_cast<Breakslicer*>(module))
			if (m->rebuildWanted.exchange(false))
				m->loader.request(m->breakPath);
		ModuleWidget::step();
	}
#endif

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Breakslicer*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel(std::string("Break: ") + (m->breakPath.empty() ? "synthetic" : semi::fileName(m->breakPath))
		                               + (m->loader.failed ? "  (load failed)" : "")));
		menu->addChild(createMenuItem("Load break (WAV)...", "", [=]() {
			semi::openFileDialog("WAV:wav,WAV", semi::dirName(m->breakPath), [=](const std::string& p) { m->requestBreak(p); });
		}));
		menu->addChild(createMenuItem("Use the synthetic break", "", [=]() { m->requestBreak(""); }));
		menu->addChild(createMenuLabel(string::f("Pattern seed %d", m->seed)));

		menu->addChild(new MenuSeparator);
		menu->addChild(createSubmenuItem("Pattern", "", [=](Menu* sub) {
			addIndex(sub, "Pattern length", kRowNames, &m->rowsSel);
			addIndex(sub, "Row length", kSpeedNames, &m->speedSel);
			addIndex(sub, "Fills every", kFillNames, &m->fillEvery);
			addIndex(sub, "Tempo fit", {"CHOP (slices at their own speed)", "TAPE (speed and pitch follow)"}, &m->fit);
			sub->addChild(createBoolPtrMenuItem("09xx snap to onsets", "", &m->snapOffsets));
			addSteps(sub, "Shuffle", &m->shuffle, kPct, "%.0f %%", 100.f);
			addSteps(sub, "Human", &m->human, kPct, "%.0f %%", 100.f);
		}));
		menu->addChild(createSubmenuItem("Groove (Markov)", "", [=](Menu* sub) {
			sub->addChild(createIndexSubmenuItem("Style", kStyles, [=]() { return (size_t) m->style; }, [=](size_t i) { m->applyStyle((int) i); }));
			addSteps(sub, "Bar feel", &m->barFeel, kPct, "%.0f %%", 100.f);
		}));
		menu->addChild(createSubmenuItem("FX chains", "", [=](Menu* sub) {
			static const char* names[gtbs::NumChains] = {"PASS weight", "PITCH weight", "SUB weight", "REV weight", "DRILL weight"};
			for (int c = 0; c < gtbs::NumChains; c++)
				addSteps(sub, names[c], &m->weights[c], kPct, "%.0f %%", 100.f);
			std::vector<std::string> semis;
			for (int i = 1; i <= 12; i++)
				semis.push_back(string::f("%d st", i));
			addIndex(sub, "Pitch", semis, &m->pitch, 1);
			sub->addChild(createBoolPtrMenuItem("Pitch up and down", "", &m->pitchBoth));
		}));
		menu->addChild(createSubmenuItem("Rolls + reverb", "", [=](Menu* sub) {
			addIndex(sub, "Roll volume", kRvolNames, &m->rvol);
			addIndex(sub, "Roll amount", {"1", "2", "3", "4", "5", "6", "7", "8"}, &m->ramt, 1);
			addIndex(sub, "Drill pitch (y)", {"1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F"}, &m->drillY, 1);
			addSteps(sub, "Sweep", &m->sweep, {-24.f, -12.f, -7.f, -5.f, 0.f, 5.f, 7.f, 12.f, 24.f}, "%+.0f st");
			addSteps(sub, "Reverb size", &m->revSize, kPct, "%.0f %%", 100.f);
			addSteps(sub, "Reverb damp", &m->revDamp, kPct, "%.0f %%", 100.f);
			addSteps(sub, "Reverb return", &m->revMix, kPct, "%.0f %%", 100.f);
		}));
		menu->addChild(createSubmenuItem("Sampler", "", [=](Menu* sub) {
			addIndex(sub, "Machine", kMachines, &m->machine);
			addSteps(sub, "S950 filter", &m->mfilter, kPct, "%.0f %%", 100.f);
			addSteps(sub, "Drive", &m->driveDb, {0.f, 3.f, 6.f, 9.f, 12.f, 18.f}, "+%.0f dB");
			addIndex(sub, "Amiga model", {"A500 (4.4 kHz RC)", "A1200 (none)"}, &m->amiga);
			sub->addChild(createBoolPtrMenuItem("Amiga LED filter", "", &m->led));
		}));
		menu->addChild(createSubmenuItem("Slicing", "", [=](Menu* sub) {
			addIndex(sub, "Onset function", kOdfNames, &m->odf);
			addIndex(sub, "Slices", kSliceModes, &m->sliceMode);
			addIndex(sub, "Break length", kBarNames, &m->barsSel);
			addSteps(sub, "Sensitivity", &m->sens, kPct, "%.0f %%", 100.f);
			addSteps(sub, "Compression (lambda)", &m->lambda, {1.f, 5.f, 10.f, 15.f, 20.f}, "%.0f");
			addSteps(sub, "Threshold weight (alpha)", &m->alpha, {0.1f, 0.15f, 0.2f, 0.25f, 0.3f, 0.35f, 0.4f}, "%.2f");
			addSteps(sub, "Minimum gap", &m->minGap, {20.f, 30.f, 45.f, 60.f, 80.f, 100.f, 150.f}, "%.0f ms");
			sub->addChild(createBoolPtrMenuItem("Backtrack to the attack", "", &m->backtrack));
		}));
		menu->addChild(createSubmenuItem("Output", "", [=](Menu* sub) {
			addSteps(sub, "Gate", &m->gate, {0.05f, 0.1f, 0.25f, 0.5f, 0.75f, 1.f}, "%.0f %%", 100.f);
			addSteps(sub, "Decay", &m->decayMs, {2.f, 10.f, 20.f, 40.f, 80.f, 160.f, 400.f}, "%.0f ms");
			addSteps(sub, "Crossfade", &m->xfadeMs, {0.2f, 0.5f, 1.f, 2.f, 4.f, 8.f, 12.f}, "%.1f ms");
		}));
		menu->addChild(createSubmenuItem("Clock", "", [=](Menu* sub) {
			addIndex(sub, "CLK input", slc::ClockFollower::modeNames(), &m->clock.mode);
			addSteps(sub, "Internal tempo (RUN)", &m->internalBpm, {90.f, 120.f, 140.f, 160.f, 170.f, 174.f, 180.f}, "%.0f BPM");
		}));
	}
};

Model* modelBreakslicer = createModel<Breakslicer, BreakslicerWidget>("Breakslicer");
