// BREAKMED — Rack / MetaModule port of BREAKMED, the generative tracker jungle break slicer
// (engine: engines/breakmed, vendored from semierendonmez-commits/breakmed Source/engine; one
// change: Engine::captureSeconds, set to 0 here: no DAW to drag a capture into).
//
// A break is cut at its transients (TRANS / EQUAL / GRID), every hit rolls R = 0..127 that sends
// it through one of seven chains (BASE, P.DN, P.UP, SUB, GLCH, REV, DRIL) by the chain weights.
// Playing it:
//   * PLAY on + CLK patched: the built-in player runs the break in its own order, locked to the
//     clock (CLK = 1/16 by default, RST = next clock is beat 1, tempo from the clock period).
//   * PLAY on, no CLK: the player free-runs at the break's own tempo.
//   * GATE: a rising edge plays a slice (SLICE = 1 V/oct from C1 = slice 00, i.e. 1/12 V per
//     slice; unpatched = the next slice in order), the falling edge ends it when GATE mode
//     (menu) is strict. This is the plugin's MIDI input.
// Outputs: L / R audio (5 V = full scale), TRIG on every hit, VAR on every hit that left BASE,
// SLC = the slice of the last hit (1/12 V per slice, same scale as SLICE).
// Breaks: WAV from the menu (30 s max, mono-summed like the plugin), path saved in the patch.
// Until one is loaded a synthetic two-bar break plays (generated, nothing embedded).
// Dropped: the capture / drag-out (DAW only), DRAG MIDI, manual slice editing / undo, IFF / RAW
// files, the computer keyboard, the themes.

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/Sample.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/BreakmedLayout.hpp"
#include "SlicerCommon.hpp"
#include "breakmed/Engine.h"

#include <array>
#include <atomic>
#include <memory>

namespace L = semi::layout::breakmed;
using semi::Screen;

namespace {

constexpr int kBlock = 32;          // engine block; adds 32 samples of latency
constexpr float kVoltsPerUnit = 5.f;
constexpr int kRoot = 36;           // C1 = slice 00, as in the plugin
constexpr int kRows = 6;            // hit.log rows on the screen

const std::vector<std::string> kSliceModes = {"TRANS (onsets)", "EQUAL", "GRID (1/16 to onsets)"};
const std::vector<std::string> kBarsNames = {"AUTO", "1 bar", "2 bars", "4 bars", "8 bars"};
constexpr int kBarsValues[] = {0, 1, 2, 4, 8};

// The chain colours (opaque, the series palette + three extra hues), index = bm::Chain.
NVGcolor chainColor(int c) {
	switch (c) {
		case 0: return semi::theme::cyan();
		case 1: return semi::theme::amber();
		case 2: return nvgRGB(0xf2, 0xd8, 0x8e);
		case 3: return nvgRGB(0x6f, 0x9f, 0xff);
		case 4: return semi::theme::magenta();
		case 5: return nvgRGB(0xb4, 0x8c, 0xff);
		case 6: return nvgRGB(0xff, 0x6a, 0x4a);
		default: return semi::theme::text();
	}
}

std::string noteName(int n) {
	static const char* names[12] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
	return std::string(names[((n % 12) + 12) % 12]) + std::to_string(n / 12 - 2);
}

// What the builder hands to the audio thread.
struct Built {
	std::shared_ptr<bm::Prepared> prep;
};

struct Snapshot {
	struct Row {
		int slice = 0, note = 0, chain = 0, semis = 0, r = 0;
		uint32_t serial = 0;
	};
	Row rows[kRows];
	int nRows = 0;
	slc::Heads heads;
	int curSlice = -1;
	int mode = 0;        // 0 stopped, 1 clock, 2 free
	float bpm = 0.f;
	int slices = 0, bars = 2;
	bool lock = false;
};

} // namespace

struct Breakmed : Module {
	enum ParamId {
		ENUMS(W_PARAM, bm::kChains),
		THRES_PARAM, SRATE_PARAM, PITCH_PARAM, MEMORY_PARAM, LEVEL_PARAM,
		PLAY_PARAM, LOCK_PARAM, DICE_PARAM, ANCHOR_PARAM,
		PARAMS_LEN
	};
	enum InputId { CLK_INPUT, RST_INPUT, GATE_INPUT, SLICE_INPUT, DICE_INPUT, VARY_INPUT, INPUTS_LEN };
	enum OutputId { OUT_L_OUTPUT, OUT_R_OUTPUT, TRIG_OUTPUT, VAR_OUTPUT, SLC_OUTPUT, OUTPUTS_LEN };
	enum LightId { ENUMS(CHAIN_LIGHT, bm::kChains), PLAY_LIGHT, LOCK_LIGHT, ANCHOR_DB_LIGHT, ANCHOR_K_LIGHT, LIGHTS_LEN };

	bm::Engine engine;
	bm::Params P;
	std::vector<bm::Event> events;

	// ---- state saved in the patch (menu + buttons)
	bool playOn = true, lockOn = false;
	int anchor = 0;                 // bm::Anchor
	uint32_t seed = 0x5eed;
	int lockBars = 1;               // index: 1 2 4 8
	int filter = 0, bitsSel = 0, drillRate = 1;
	bool interp = false, gate = true, thru = false, choke = true, stretch = true;
	bool down[3] = {true, true, true}, up[2] = {true, true};
	float cycleMs = 35.f, grainMs = 80.f, flux = 0.5f, gspeed = 0.5f, space = 0.6f, dloop = 0.5f, dpan = 1.f;
	float attackMs = 0.f, releaseMs = 4.f, fine = 0.f;
	int sliceMode = 0, barsSel = 0;
	bool zeroSnap = true;
	std::string breakPath;          // "" = synthetic break

	// ---- loading (builder thread owns the cache)
	semi::Slot<Built> slot;
	struct Cache {
		std::string path = "\x01";  // never a real path: forces the first decode
		std::vector<float> mono;
		double rate = 44100.0;
		std::string name;
		bm::Onsets onsets;
	} cache;
	semi::TripleBuffer<slc::WaveView> wave;
	std::atomic<bool> rebuildWanted{false};
	bool requested = false;
	int lastKey = 0;
	float keyStable = 0.f;
	bool keyPending = false;
	int numSlices = 0, curBars = 2;

	// ---- clock / gate
	slc::ClockFollower clock;
	dsp::SchmittTrigger gateTrig, diceTrig;
	dsp::BooleanTrigger playBtn, lockBtn, diceBtn, anchorBtn;
	bool gateHigh = false;
	int gateNote = -1, gateStep = 0;
	int mode = 0;
	double clkPpq = 0.0, clkBpm = 0.0;
	bool clkRun = false;

	// ---- block adapter + outputs
	float outBuf[2][kBlock]{};
	float blockL[kBlock]{}, blockR[kBlock]{};
	int pos = 0;
	uint32_t seenSerial = 0;
	dsp::PulseGenerator trigPulse, varPulse;
	float slcVolts = 0.f;
	uint32_t rng = 0x2545f491u;
	float flash[bm::kChains]{};

	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;
	// last: destroyed first, so its thread (MetaModule) stops before anything it builds into goes
	semi::Loader<Built> loader;

	~Breakmed() override {
		// background jobs first (and wait for a run in flight): they write into this module
		loader.stopAndWait();
	}

	Breakmed() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		static const char* names[bm::kChains] = {"Base", "Pitch down", "Pitch up", "Sub octave", "Glitch", "Reverse", "Drill"};
		static const int defs[bm::kChains] = {55, 22, 12, 6, 4, 4, 3};
		for (int c = 0; c < bm::kChains; c++)
			configParam(W_PARAM + c, 0.f, 100.f, (float) defs[c], std::string(names[c]) + " weight")->snapEnabled = true;
		configParam(THRES_PARAM, 2.f, 64.f, 12.f, "Slice threshold (TRANS: 2 = every onset, 64 = strongest; EQUAL: parts)")->snapEnabled = true;
		{
			std::vector<std::string> rates;
			for (int i = 0; i < bm::kNumRates; i++)
				rates.push_back(i == bm::kRateFull ? std::string("FULL") : bm::paulaRateName(i) + " " + std::to_string((int) bm::paulaRate(i, 0)) + " Hz");
			configSwitch(SRATE_PARAM, 0.f, (float) bm::kRateFull, (float) bm::defaultRateIndex(), "Paula sample rate", rates);
		}
		configParam(PITCH_PARAM, -24.f, 24.f, 0.f, "Pitch", " st")->snapEnabled = true;
		configParam(MEMORY_PARAM, 0.f, 100.f, 40.f, "Memory (a variation falls back to BASE)", " %");
		configParam(LEVEL_PARAM, 0.f, 64.f, 56.f, "Volume (tracker 00-40 hex)")->snapEnabled = true;
		configButton(PLAY_PARAM, "Play (built-in player: the break in its own order)");
		configButton(LOCK_PARAM, "Vary lock (same variation at the same place)");
		configButton(DICE_PARAM, "Dice (new seed)");
		configButton(ANCHOR_PARAM, "Anchor (OFF / DOWNBEAT / KICKS)");
		configInput(CLK_INPUT, "Clock (1/16 by default)");
		configInput(RST_INPUT, "Reset (next clock = beat 1)");
		configInput(GATE_INPUT, "Gate (plays a slice)");
		configInput(SLICE_INPUT, "Slice (1 V/oct, 0 V = slice 00; unpatched = next slice)");
		configInput(DICE_INPUT, "Dice trigger (new seed)");
		configInput(VARY_INPUT, "Vary CV (scales the six variation weights, +-5 V = x0..x2)");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		configOutput(TRIG_OUTPUT, "Hit trigger");
		configOutput(VAR_OUTPUT, "Variation trigger (hit left BASE)");
		configOutput(SLC_OUTPUT, "Slice of the last hit (1/12 V per slice)");
		static const char* cl[bm::kChains] = {"BASE", "P.DN", "P.UP", "SUB", "GLCH", "REV", "DRIL"};
		for (int c = 0; c < bm::kChains; c++)
			configLight(CHAIN_LIGHT + c, std::string(cl[c]) + " hit");

		events.reserve(256);
		engine.captureSeconds = 0.0;
		engine.prepareToPlay(44100.0, kBlock);
		loader.attach(this, &slot, [this](const std::string& path, Built& b) { return build(path, b); });
#ifdef METAMODULE
		requestBreak("");
#endif
	}

	void onAdd(const AddEvent& e) override {
		Module::onAdd(e);
		if (!requested)
			requestBreak(breakPath);   // Rack: a new module (dataFromJson already requested a saved one)
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		engine.prepareToPlay(e.sampleRate, kBlock);
		pos = 0;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		playOn = true;
		lockOn = false;
		anchor = 0;
		seed = 0x5eed;
		engine.allNotesOff();
	}

	void requestBreak(const std::string& path) {
		requested = true;
		breakPath = path;
		loader.request(path);
	}

	// ---- builder (Rack: UI thread; MetaModule: the loader's AsyncThread) -----------------------
	bool build(const std::string& path, Built& out) {
		if (path != cache.path) {
			slc::Break b;
			if (path.empty())
				slc::synthBreak(b);
			else if (!slc::loadBreak(path, b, false))
				return false;
			std::vector<float> mono = b.mono();
			float peak = 0.f;
			for (float v : mono)
				peak = std::max(peak, std::abs(v));
			if (peak < 1e-5f)
				return false;
			for (auto& v : mono)
				v *= 0.98f / peak;   // 8-bit needs the whole range (as the plugin)
			cache.mono = std::move(mono);
			cache.rate = b.rate;
			cache.name = b.name;
			cache.onsets = bm::detectOnsets(cache.mono.data(), (int) cache.mono.size(), cache.rate);
			cache.path = path;
		}
		const int n = (int) cache.mono.size();
		const int bars = barsSel == 0 ? bm::autoBars((double) n / cache.rate) : kBarsValues[clamp(barsSel, 1, 4)];
		const int thres = clamp((int) std::round(params[THRES_PARAM].getValue()), 2, 64);
		std::vector<int> starts;
		switch (sliceMode) {
			case 1: starts = bm::equalSlices(n, thres); break;
			case 2: starts = bm::gridSlices(cache.onsets, n, bars); break;
			default: starts = bm::transientSlices(cache.onsets, n, cache.rate, thres); break;
		}
		if (zeroSnap && sliceMode != 1)
			for (auto& p : starts)
				if (p > 0)
					p = bm::snapToZero(cache.mono.data(), n, p, (int) (cache.rate * 0.002));
		bm::PrepareSpec spec;
		spec.mono = cache.mono.data();
		spec.n = n;
		spec.srcRate = cache.rate;
		spec.starts = starts;
		spec.roles = bm::classify(cache.mono.data(), n, cache.rate, starts);
		spec.bars = bars;
		spec.rateIndex = clamp((int) std::round(params[SRATE_PARAM].getValue()), 0, bm::kRateFull);
		static const int bitsTab[3] = {8, 12, 16};
		spec.bits = bitsTab[clamp(bitsSel, 0, 2)];
		spec.space = space;
		out.prep = bm::prepare(spec);

		// the screen's copy
		const bm::Prepared& p = *out.prep;
		slc::WaveView& v = wave.writeSlot();
		v.setWave(cache.mono.data(), n);
		const int ns = std::min(p.numSlices(), slc::WaveView::kMarks);
		const float len = (float) std::max<size_t>(1, p.pcm.size());
		for (int i = 0; i < ns; i++) {
			v.mark[i] = p.start[(size_t) i] / len;
			v.tag[i] = (uint8_t) clamp(p.role[(size_t) i], 0, 3);
		}
		v.marks = ns;
		v.beats = bars * 4;
		v.setName(cache.name);
		wave.publish();
		return true;
	}

	// a change detector, not a value: unsigned so the multiply wraps instead of overflowing (UB)
	int buildKey() {
		uint32_t k = (uint32_t) (int) std::round(params[THRES_PARAM].getValue());
		k = k * 41u + (uint32_t) (int) std::round(params[SRATE_PARAM].getValue());
		k = k * 7u + (uint32_t) bitsSel;
		k = k * 101u + (uint32_t) (int) std::round(space * 100.f);
		k = k * 5u + (uint32_t) sliceMode;
		k = k * 7u + (uint32_t) barsSel;
		k = k * 2u + (uint32_t) (zeroSnap ? 1 : 0);
		return (int) k;
	}

	// ---- patch ------------------------------------------------------------------------------
	json_t* dataToJson() override {
		json_t* r = json_object();
		json_object_set_new(r, "path", json_string(breakPath.c_str()));
		json_object_set_new(r, "play", json_boolean(playOn));
		json_object_set_new(r, "lock", json_boolean(lockOn));
		json_object_set_new(r, "anchor", json_integer(anchor));
		json_object_set_new(r, "seed", json_integer(seed));
		json_object_set_new(r, "lockBars", json_integer(lockBars));
		json_object_set_new(r, "filter", json_integer(filter));
		json_object_set_new(r, "bits", json_integer(bitsSel));
		json_object_set_new(r, "drillRate", json_integer(drillRate));
		json_object_set_new(r, "interp", json_boolean(interp));
		json_object_set_new(r, "gate", json_boolean(gate));
		json_object_set_new(r, "thru", json_boolean(thru));
		json_object_set_new(r, "choke", json_boolean(choke));
		json_object_set_new(r, "stretch", json_boolean(stretch));
		for (int i = 0; i < 3; i++)
			json_object_set_new(r, string::f("dn%d", i + 1).c_str(), json_boolean(down[i]));
		for (int i = 0; i < 2; i++)
			json_object_set_new(r, string::f("up%d", i + 1).c_str(), json_boolean(up[i]));
		json_object_set_new(r, "cycle", json_real(cycleMs));
		json_object_set_new(r, "grain", json_real(grainMs));
		json_object_set_new(r, "flux", json_real(flux));
		json_object_set_new(r, "gspeed", json_real(gspeed));
		json_object_set_new(r, "space", json_real(space));
		json_object_set_new(r, "dloop", json_real(dloop));
		json_object_set_new(r, "dpan", json_real(dpan));
		json_object_set_new(r, "attack", json_real(attackMs));
		json_object_set_new(r, "release", json_real(releaseMs));
		json_object_set_new(r, "fine", json_real(fine));
		json_object_set_new(r, "sliceMode", json_integer(sliceMode));
		json_object_set_new(r, "bars", json_integer(barsSel));
		json_object_set_new(r, "zeroSnap", json_boolean(zeroSnap));
		json_object_set_new(r, "clock", json_integer(clock.mode));
		return r;
	}

	void dataFromJson(json_t* r) override {
		auto b = [&](const char* k, bool& v) { if (json_t* j = json_object_get(r, k)) v = json_boolean_value(j); };
		auto i = [&](const char* k, int& v, int lo, int hi) { if (json_t* j = json_object_get(r, k)) v = clamp((int) json_integer_value(j), lo, hi); };
		auto f = [&](const char* k, float& v, float lo, float hi) { if (json_t* j = json_object_get(r, k)) v = clamp((float) json_number_value(j), lo, hi); };
		b("play", playOn);
		b("lock", lockOn);
		i("anchor", anchor, 0, 2);
		if (json_t* j = json_object_get(r, "seed"))
			seed = (uint32_t) json_integer_value(j);
		i("lockBars", lockBars, 0, 3);
		i("filter", filter, 0, 2);
		i("bits", bitsSel, 0, 2);
		i("drillRate", drillRate, 0, 2);
		b("interp", interp);
		b("gate", gate);
		b("thru", thru);
		b("choke", choke);
		b("stretch", stretch);
		for (int k = 0; k < 3; k++)
			b(string::f("dn%d", k + 1).c_str(), down[k]);
		for (int k = 0; k < 2; k++)
			b(string::f("up%d", k + 1).c_str(), up[k]);
		f("cycle", cycleMs, 5.f, 200.f);
		f("grain", grainMs, 20.f, 200.f);
		f("flux", flux, 0.f, 1.f);
		f("gspeed", gspeed, 0.1f, 1.f);
		f("space", space, 0.f, 1.f);
		f("dloop", dloop, 0.05f, 1.f);
		f("dpan", dpan, 0.f, 1.f);
		f("attack", attackMs, 0.f, 500.f);
		f("release", releaseMs, 0.f, 2000.f);
		f("fine", fine, -99.f, 99.f);
		i("sliceMode", sliceMode, 0, 2);
		i("bars", barsSel, 0, 4);
		b("zeroSnap", zeroSnap);
		i("clock", clock.mode, 0, slc::ClockFollower::kNumModes - 1);
		std::string path;
		if (json_t* j = json_object_get(r, "path"))
			path = json_string_value(j);
		lastKey = buildKey();
		requestBreak(path);
	}

	// ---- audio ------------------------------------------------------------------------------
	void fillParams() {
		for (int c = 0; c < bm::kChains; c++)
			P.weight[(size_t) c] = params[W_PARAM + c].getValue();
		if (inputs[VARY_INPUT].isConnected()) {
			const float k = clamp(1.f + inputs[VARY_INPUT].getVoltage() / 5.f, 0.f, 2.f);
			for (int c = 1; c < bm::kChains; c++)
				P.weight[(size_t) c] *= k;
		}
		P.volume = params[LEVEL_PARAM].getValue();
		P.pitch = params[PITCH_PARAM].getValue();
		P.fine = fine;
		P.attackMs = attackMs;
		P.releaseMs = releaseMs;
		P.interp = interp;
		P.filter = (bm::Filter) filter;
		P.gate = gate;
		P.thru = thru;
		P.choke = choke;
		P.velocity = false;
		P.root = kRoot;
		P.whiteKeys = false;
		for (int k = 0; k < 3; k++)
			P.down[(size_t) k] = down[k];
		for (int k = 0; k < 2; k++)
			P.up[(size_t) k] = up[k];
		P.pitchStretch = stretch;
		P.cycleMs = cycleMs;
		P.grainMs = grainMs;
		P.flux = flux;
		P.glitchSpeed = gspeed;
		P.drillRate = drillRate;
		P.drillLoop = dloop;
		P.drillPan = dpan;
		P.anchor = (bm::Anchor) anchor;
		P.memory = params[MEMORY_PARAM].getValue() * 0.01f;
		P.lock = lockOn;
		static const int lb[4] = {1, 2, 4, 8};
		P.lockBars = lb[clamp(lockBars, 0, 3)];
		P.seed = seed;
		const bool clkConn = inputs[CLK_INPUT].isConnected();
		P.play = playOn;
		P.syncHost = clkConn;
		P.captureBars = 1;
	}

	void runBlock() {
		if (slot.poll()) {
			engine.setPrepared(slot.active->prep.get());
			numSlices = slot.active->prep->numSlices();
			curBars = slot.active->prep->bars;
			gateStep = 0;
			gateNote = -1;
		}
		fillParams();
		bm::Transport t;
		t.valid = clkRun;
		t.playing = clkRun;
		t.ppq = clkPpq;
		t.bpm = clkRun ? clkBpm : 120.0;
		engine.process(blockL, blockR, kBlock, events, P, t);
		events.clear();
		for (int i = 0; i < kBlock; i++) {
			outBuf[0][i] = blockL[i];
			outBuf[1][i] = blockR[i];
		}

		// hits of this block -> TRIG / VAR / SLC
		const uint32_t s = engine.logSerial.load(std::memory_order_acquire);
		if (s != seenSerial) {
			const uint32_t from = s - seenSerial > (uint32_t) bm::Engine::kLog ? s - bm::Engine::kLog + 1 : seenSerial + 1;
			for (uint32_t k = from; k != s + 1; k++) {
				const bm::Hit& h = engine.log[k % bm::Engine::kLog];
				trigPulse.trigger(1e-3f);
				if (h.chain != bm::Base)
					varPulse.trigger(1e-3f);
				slcVolts = h.slice / 12.f;
				flash[clamp(h.chain, 0, bm::kChains - 1)] = 1.f;
			}
			seenSerial = s;
		}
	}

	void pushEvent(bool on, int note) {
		if (events.size() >= events.capacity())
			return;
		bm::Event e;
		e.offset = pos;
		e.on = on;
		e.note = note;
		e.velocity = 1.f;
		events.push_back(e);
	}

	void process(const ProcessArgs& args) override {
		// buttons
		if (playBtn.process(params[PLAY_PARAM].getValue() > 0.f))
			playOn = !playOn;
		if (lockBtn.process(params[LOCK_PARAM].getValue() > 0.f))
			lockOn = !lockOn;
		if (anchorBtn.process(params[ANCHOR_PARAM].getValue() > 0.f))
			anchor = (anchor + 1) % 3;
		const bool dice = diceBtn.process(params[DICE_PARAM].getValue() > 0.f)
		                  | diceTrig.process(inputs[DICE_INPUT].getVoltage(), 0.1f, 1.f);
		if (dice) {
			rng ^= rng << 13;
			rng ^= rng >> 17;
			rng ^= rng << 5;
			seed = rng & 0xffffu;
		}

		// clock
		clkRun = clock.process(args.sampleTime, inputs[CLK_INPUT], inputs[RST_INPUT], clkPpq, clkBpm);
		mode = clkRun ? 1 : (!inputs[CLK_INPUT].isConnected() && playOn ? 2 : 0);

		// gate -> note on / off for the slice
		if (inputs[GATE_INPUT].isConnected()) {
			const float g = inputs[GATE_INPUT].getVoltage();
			if (gateTrig.process(g, 0.1f, 1.f)) {
				if (gateNote >= 0)
					pushEvent(false, gateNote);
				int slice;
				if (inputs[SLICE_INPUT].isConnected())
					slice = clamp((int) std::round(inputs[SLICE_INPUT].getVoltage() * 12.f), 0, bm::kMaxSlices - 1);
				else
					slice = numSlices > 0 ? (gateStep++) % numSlices : 0;
				gateNote = bm::noteForSlice(slice, kRoot, false);
				pushEvent(true, gateNote);
				gateHigh = true;
			}
			else if (gateHigh && !gateTrig.isHigh()) {
				gateHigh = false;
				if (gateNote >= 0)
					pushEvent(false, gateNote);
				gateNote = -1;
			}
		}

		outputs[OUT_L_OUTPUT].setVoltage(outBuf[0][pos] * kVoltsPerUnit);
		outputs[OUT_R_OUTPUT].setVoltage(outBuf[1][pos] * kVoltsPerUnit);
		if (++pos >= kBlock) {
			pos = 0;
			runBlock();
		}
		outputs[TRIG_OUTPUT].setVoltage(trigPulse.process(args.sampleTime) ? 10.f : 0.f);
		outputs[VAR_OUTPUT].setVoltage(varPulse.process(args.sampleTime) ? 10.f : 0.f);
		outputs[SLC_OUTPUT].setVoltage(slcVolts);

		if (ui.tick(args.sampleTime))
			publishUi(args.sampleTime);
	}

	void publishUi(float dt) {
		const float period = 1.f / semi::platform::kUiRateHz;
		for (int c = 0; c < bm::kChains; c++) {
			lights[CHAIN_LIGHT + c].setBrightness(flash[c]);
			flash[c] *= 0.72f;
		}
		lights[PLAY_LIGHT].setBrightness(playOn ? 1.f : 0.f);
		lights[LOCK_LIGHT].setBrightness(lockOn ? 1.f : 0.f);
		lights[ANCHOR_DB_LIGHT].setBrightness(anchor == 1 ? 1.f : 0.f);
		lights[ANCHOR_K_LIGHT].setBrightness(anchor == 2 ? 1.f : 0.f);

		// rebuild when a slicing / sampler setting has rested for 150 ms
		const int key = buildKey();
		if (key != lastKey) {
			lastKey = key;
			keyStable = 0.f;
			keyPending = true;
		}
		else if (keyPending) {
			keyStable += period;
			if (keyStable >= 0.15f) {
				keyPending = false;
#ifdef METAMODULE
				loader.hasQueued.store(true, std::memory_order_release);   // rebuild the queued path
#else
				rebuildWanted.store(true);
#endif
			}
		}
		(void) dt;

		Snapshot& s = snap.writeSlot();
		const uint32_t last = engine.logSerial.load(std::memory_order_acquire);
		s.nRows = 0;
		for (int k = kRows - 1; k >= 0; k--) {
			if ((uint32_t) k >= last)
				continue;
			const bm::Hit& h = engine.log[(last - (uint32_t) k) % bm::Engine::kLog];
			Snapshot::Row& r = s.rows[s.nRows++];
			r.slice = h.slice;
			r.note = h.note;
			r.chain = h.chain;
			r.semis = h.semis;
			r.r = h.r;
			r.serial = h.serial;
		}
		s.heads.n = 0;
		s.curSlice = -1;
		for (int k = 0; k < bm::kVoices && s.heads.n < slc::Heads::kMax; k++) {
			const int sl = engine.view[(size_t) k].slice.load(std::memory_order_relaxed);
			if (sl < 0)
				continue;
			s.heads.pos[s.heads.n] = engine.view[(size_t) k].pos.load(std::memory_order_relaxed);
			s.heads.col[s.heads.n] = (uint8_t) engine.view[(size_t) k].chain.load(std::memory_order_relaxed);
			s.heads.n++;
			s.curSlice = sl;
		}
		if (last > 0)
			s.curSlice = engine.log[last % bm::Engine::kLog].slice;
		s.mode = mode;
		s.bpm = mode == 1 ? (float) clkBpm : 0.f;
		s.slices = numSlices;
		s.bars = curBars;
		s.lock = lockOn;
		snap.publish();
	}
};

// break.view: waveform, slice markers with role letters (K S H G), beat grid, playheads in their
// chain colours. Changes only when a hit starts / moves (playheads at <= 20 Hz on MetaModule).
struct BreakmedWaveScreen : Screen {
	Breakmed* module = nullptr;
	slc::WaveView demoWave;
	Snapshot demo;

	BreakmedWaveScreen() {
		slc::Break b;
		slc::synthBreak(b, 8000.0);
		demoWave.setWave(b.l.data(), b.frames());
		static const int hits[] = {0, 2, 4, 6, 7, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 27, 28, 30};
		static const uint8_t roles[] = {0, 2, 1, 2, 3, 2, 0, 1, 2, 0, 2, 1, 2, 2, 0, 3, 1, 2};
		demoWave.marks = 18;
		for (int i = 0; i < 18; i++) {
			demoWave.mark[i] = hits[i] / 32.f;
			demoWave.tag[i] = roles[i];
		}
		demoWave.beats = 8;
		demoWave.setName("SYNTH BREAK");
		demo.heads.n = 2;
		demo.heads.pos[0] = 12.3f / 32.f;
		demo.heads.col[0] = 0;
		demo.heads.pos[1] = 26.4f / 32.f;
		demo.heads.col[1] = 4;
		demo.curSlice = 7;
		demo.slices = 18;
		demo.bars = 2;
	}

	void drawScreen(NVGcontext* vg) override {
		const slc::WaveView& v = module ? module->wave.read() : demoWave;
		const Snapshot& s = module ? module->snap.read() : demo;
		static const NVGcolor tagCols[8] = {semi::theme::cyan(), semi::theme::amber(), semi::theme::dim(), semi::theme::text(),
		                                    semi::theme::text(), semi::theme::text(), semi::theme::text(), semi::theme::text()};
		NVGcolor headCols[8];
		for (int i = 0; i < 8; i++)
			headCols[i] = chainColor(i);
		char right[24];
		std::snprintf(right, sizeof right, "%02X SL %dB", s.slices, s.bars);
		slc::drawWave(*this, vg, box.size.x, box.size.y, v, s.heads, "KSHG", tagCols, headCols, s.curSlice, right);
	}
};

// hit.log: every hit as a tracker row (slice, note, chain, the tracker command it stands for,
// R), newest at the bottom on the cursor bar. Header: status (CLK bpm / FREE / STOP / LOCK).
struct LogScreen : Screen {
	Breakmed* module = nullptr;
	Snapshot demo;

	LogScreen() {
		static const int sl[kRows] = {4, 5, 6, 7, 8, 9}, ch[kRows] = {0, 1, 0, 6, 0, 2}, se[kRows] = {0, -2, 0, 0, 0, 1};
		static const int rr[kRows] = {0x12, 0x5a, 0x33, 0x7d, 0x08, 0x61};
		for (int i = 0; i < kRows; i++) {
			demo.rows[i].slice = sl[i];
			demo.rows[i].note = kRoot + sl[i];
			demo.rows[i].chain = ch[i];
			demo.rows[i].semis = se[i];
			demo.rows[i].r = rr[i];
			demo.rows[i].serial = 0x40 + i;
		}
		demo.nRows = kRows;
		demo.mode = 1;
		demo.bpm = 172.f;
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		const float pad = 3.f;
		const float fs = std::max(9.f, semi::platform::kMinText);
		const float rowH = (h - 2.f * pad - fs - 2.f) / kRows;
		const float cw = fs * 0.54f;   // ShareTechMono advance
		// columns (in characters): ROW SL NOTE CHAIN FX R
		const float cx[6] = {pad, pad + 3.5f * cw, pad + 7.f * cw, pad + 12.f * cw, pad + 18.f * cw, pad + 23.f * cw};

		static const char* hdr[6] = {"ROW", "SL", "NOTE", "CHAIN", "FX", "R"};
		for (int i = 0; i < 6; i++)
			text(vg, cx[i], pad - 0.5f, hdr[i], fs, semi::theme::dim());
		char st[24];
		NVGcolor sc = semi::theme::dim();
		if (s.mode == 1) {
			std::snprintf(st, sizeof st, "CLK %3.0f", s.bpm);
			sc = semi::theme::cyan();
		}
		else if (s.mode == 2) {
			std::snprintf(st, sizeof st, "FREE");
			sc = semi::theme::cyan();
		}
		else {
			std::snprintf(st, sizeof st, "STOP");
		}
		text(vg, w - pad, pad - 0.5f, s.lock ? std::string("LOCK ") + st : std::string(st), fs, s.lock ? semi::theme::magenta() : sc,
		     NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		const float y0 = pad + fs + 1.5f;
		line(vg, pad, y0 - 0.8f, w - pad, y0 - 0.8f, semi::theme::grid());

		// the cursor bar on the newest row
		const float curY = y0 + (kRows - 1) * rowH;
		fillRect(vg, 1.f, curY, w - 2.f, rowH, semi::theme::cyanDeep());

		const int first = kRows - s.nRows;
		for (int k = 0; k < s.nRows; k++) {
			const Snapshot::Row& r = s.rows[k];
			const float y = y0 + (first + k) * rowH + 0.5f * rowH;
			const bool cur = first + k == kRows - 1;
			const NVGcolor ink = cur ? semi::theme::cyan() : semi::theme::text();
			char b[12];
			const int al = NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE;
			std::snprintf(b, sizeof b, "%02X", (unsigned) (r.serial & 0xff));
			text(vg, cx[0], y, b, fs, semi::theme::dim(), al);
			std::snprintf(b, sizeof b, "%02X", r.slice & 0xff);
			text(vg, cx[1], y, b, fs, ink, al);
			text(vg, cx[2], y, noteName(r.note), fs, ink, al);
			text(vg, cx[3], y, bm::chainName(r.chain), fs, chainColor(r.chain), al);
			text(vg, cx[4], y, bm::chainFx(r.chain, r.semis), fs, r.chain == 0 ? semi::theme::dim() : semi::theme::amber(), al);
			std::snprintf(b, sizeof b, "%02X", r.r & 0x7f);
			text(vg, cx[5], y, b, fs, semi::theme::dim(), al);
		}
	}
};

// ---- menu helpers (MetaModule-safe: fixed steps, no sliders) ---------------------------------
namespace {

void addSteps(Menu* menu, const std::string& label, float* v, const std::vector<float>& steps, const char* fmt) {
	std::vector<std::string> labels;
	for (float s : steps)
		labels.push_back(string::f(fmt, s));
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

void addIndex(Menu* menu, const std::string& label, const std::vector<std::string>& labels, int* v) {
	menu->addChild(createIndexSubmenuItem(label, labels, [=]() { return (size_t) *v; }, [=](size_t i) { *v = (int) i; }));
}

} // namespace

struct BreakmedWidget : ModuleWidget {
	BreakmedWidget(Breakmed* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Breakmed.svg")));

		auto* wv = semi::createScreenMm<BreakmedWaveScreen>(Vec(L::WAVE.x, L::WAVE.y), Vec(L::WAVE.w, L::WAVE.h));
		wv->module = module;
		addChild(wv);
		auto* lg = semi::createScreenMm<LogScreen>(Vec(L::LOG.x, L::LOG.y), Vec(L::LOG.w, L::LOG.h));
		lg->module = module;
		addChild(lg);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		const L::P leds[bm::kChains] = {L::CH_LED0, L::CH_LED1, L::CH_LED2, L::CH_LED3, L::CH_LED4, L::CH_LED5, L::CH_LED6};
		const L::P knobs[bm::kChains] = {L::W0, L::W1, L::W2, L::W3, L::W4, L::W5, L::W6};
		for (int c = 0; c < bm::kChains; c++) {
			if (c == 0)
				addChild(createLightCentered<semi::CyanLedSmall>(at(leds[c]), module, Breakmed::CHAIN_LIGHT + c));
			else if (c >= 4)
				addChild(createLightCentered<semi::MagentaLedSmall>(at(leds[c]), module, Breakmed::CHAIN_LIGHT + c));
			else
				addChild(createLightCentered<semi::AmberLedSmall>(at(leds[c]), module, Breakmed::CHAIN_LIGHT + c));
			if (c == bm::kChains - 1)
				addParam(createParamCentered<semi::KnobSHot>(at(knobs[c]), module, Breakmed::W_PARAM + c));
			else
				addParam(createParamCentered<semi::KnobS>(at(knobs[c]), module, Breakmed::W_PARAM + c));
		}
		addParam(createParamCentered<semi::KnobM>(at(L::THRES), module, Breakmed::THRES_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::SRATE), module, Breakmed::SRATE_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::PITCH), module, Breakmed::PITCH_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::MEMORY), module, Breakmed::MEMORY_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::LEVEL), module, Breakmed::LEVEL_PARAM));

		addParam(createParamCentered<semi::PadCyan>(at(L::PLAY), module, Breakmed::PLAY_PARAM));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::PLAY_LED), module, Breakmed::PLAY_LIGHT));
		addParam(createParamCentered<semi::Button>(at(L::LOCK), module, Breakmed::LOCK_PARAM));
		addChild(createLightCentered<semi::MagentaLedSmall>(at(L::LOCK_LED), module, Breakmed::LOCK_LIGHT));
		addParam(createParamCentered<semi::Button>(at(L::DICE), module, Breakmed::DICE_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::ANCHOR), module, Breakmed::ANCHOR_PARAM));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::ANCHOR_DB), module, Breakmed::ANCHOR_DB_LIGHT));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::ANCHOR_K), module, Breakmed::ANCHOR_K_LIGHT));

		addInput(createInputCentered<semi::Jack>(at(L::CLK), module, Breakmed::CLK_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RST), module, Breakmed::RST_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::GATE), module, Breakmed::GATE_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::SLICE), module, Breakmed::SLICE_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::DICE_IN), module, Breakmed::DICE_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::VARY_CV), module, Breakmed::VARY_INPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_L), module, Breakmed::OUT_L_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_R), module, Breakmed::OUT_R_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::TRIG), module, Breakmed::TRIG_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::VAR), module, Breakmed::VAR_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::SLC), module, Breakmed::SLC_OUTPUT));
	}

#ifndef METAMODULE
	// Rack: rebuilds after a slicing / sampler change run here, on the UI thread.
	void step() override {
		if (auto* m = dynamic_cast<Breakmed*>(module))
			if (m->rebuildWanted.exchange(false))
				m->loader.request(m->breakPath);
		ModuleWidget::step();
	}
#endif

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Breakmed*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel(std::string("Break: ") + (m->breakPath.empty() ? "synthetic" : semi::fileName(m->breakPath))
		                               + (m->loader.failed ? "  (load failed)" : "")));
		menu->addChild(createMenuItem("Load break (WAV)...", "", [=]() {
			semi::openFileDialog("WAV:wav,WAV", semi::dirName(m->breakPath), [=](const std::string& p) { m->requestBreak(p); });
		}));
		menu->addChild(createMenuItem("Use the synthetic break", "", [=]() { m->requestBreak(""); }));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Slicing"));
		addIndex(menu, "Slice mode", kSliceModes, &m->sliceMode);
		addIndex(menu, "Break length", kBarsNames, &m->barsSel);
		menu->addChild(createBoolPtrMenuItem("Snap to zero crossings", "", &m->zeroSnap));

		menu->addChild(createMenuLabel("Paula"));
		addIndex(menu, "Bits", {"8", "12", "16"}, &m->bitsSel);
		menu->addChild(createBoolPtrMenuItem("Interpolation", "", &m->interp));
		addIndex(menu, "Amiga filter", {"OFF", "A500", "LED"}, &m->filter);
		addSteps(menu, "Finetune", &m->fine, {-99.f, -50.f, -25.f, -10.f, 0.f, 10.f, 25.f, 50.f, 99.f}, "%+.0f ct");
		addSteps(menu, "Attack", &m->attackMs, {0.f, 1.f, 2.f, 5.f, 10.f, 20.f, 50.f, 100.f}, "%.0f ms");
		addSteps(menu, "Release", &m->releaseMs, {0.f, 2.f, 4.f, 10.f, 25.f, 50.f, 100.f, 250.f, 500.f}, "%.0f ms");
		menu->addChild(createBoolPtrMenuItem("Gate (note-off ends the hit)", "", &m->gate));
		menu->addChild(createBoolPtrMenuItem("THRU (play past the slice end)", "", &m->thru));
		menu->addChild(createBoolPtrMenuItem("Choke (a new hit cuts the last)", "", &m->choke));

		menu->addChild(createSubmenuItem("Chains", "", [=](Menu* sub) {
			sub->addChild(createBoolPtrMenuItem("P.DN -1", "", &m->down[0]));
			sub->addChild(createBoolPtrMenuItem("P.DN -2", "", &m->down[1]));
			sub->addChild(createBoolPtrMenuItem("P.DN -3", "", &m->down[2]));
			sub->addChild(createBoolPtrMenuItem("P.UP +1", "", &m->up[0]));
			sub->addChild(createBoolPtrMenuItem("P.UP +2", "", &m->up[1]));
			sub->addChild(createIndexSubmenuItem("Pitch mode", {"STRETCH (time kept)", "REPITCH"},
				[=]() { return (size_t) (m->stretch ? 0 : 1); }, [=](size_t i) { m->stretch = i == 0; }));
			addSteps(sub, "Cycle", &m->cycleMs, {5.f, 10.f, 20.f, 35.f, 50.f, 80.f, 120.f, 200.f}, "%.0f ms");
			addSteps(sub, "Glitch grain", &m->grainMs, {20.f, 40.f, 60.f, 80.f, 120.f, 160.f, 200.f}, "%.0f ms");
			addSteps(sub, "Glitch flux", &m->flux, {0.f, 0.25f, 0.5f, 0.75f, 1.f}, "%.2f");
			addSteps(sub, "Glitch speed", &m->gspeed, {0.1f, 0.25f, 0.5f, 0.75f, 1.f}, "%.2f");
			addIndex(sub, "Drill rate", {"1/8", "1/16", "1/32"}, &m->drillRate);
			addSteps(sub, "Drill space (reverb)", &m->space, {0.f, 0.2f, 0.4f, 0.6f, 0.8f, 1.f}, "%.1f");
			addSteps(sub, "Drill loop", &m->dloop, {0.05f, 0.125f, 0.25f, 0.5f, 0.75f, 1.f}, "%.3f x 1/16");
			addSteps(sub, "Drill pan", &m->dpan, {0.f, 0.25f, 0.5f, 0.75f, 1.f}, "%.2f");
		}));

		menu->addChild(createMenuLabel("Generator"));
		addIndex(menu, "Lock length", {"1 bar", "2 bars", "4 bars", "8 bars"}, &m->lockBars);
		menu->addChild(createMenuLabel(string::f("Seed %04X", (unsigned) (m->seed & 0xffff))));

		menu->addChild(createMenuLabel("Clock"));
		addIndex(menu, "CLK input", slc::ClockFollower::modeNames(), &m->clock.mode);
	}
};

Model* modelBreakmed = createModel<Breakmed, BreakmedWidget>("Breakmed");
