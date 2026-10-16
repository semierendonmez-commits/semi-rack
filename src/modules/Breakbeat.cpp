// BREAKBEAT — Rack / MetaModule port of Breakbeat, the desktop port of mestela/schwung-breakbeat
// (engine: engines/breakbeat — the plugin's BreakbeatEngine without JUCE, upstream's slice
// selection compiled unchanged, plus BBCut cut procedures ported from bencodec/BBenCut).
//
// The loop is cut into 8 / 16 / 32 equal slices and re-sequenced on every 1/8 (the slice grid)
// with the module's biased-random decisions: COMPLEX(ity), ANCHOR, ROLL, the phrase's fill bar
// (FILL) and its chance of switching to loop B (B CHANCE), and per-bar retrigger odds 2X / 3X /
// 4X / 8X. CUT picks the decisions: MOVE (the original) or a BBCut procedure — CUTPROC11 (classic
// odd-block repeats with end-of-phrase stutters), WARPCUT (straight cuts / even or accelerating
// rolls), SQPUSHER1 (8th / 16th groups and transcribed fills), CHOOSECUT (1 / 1.5-beat cuts and
// rolls). In the BBCut modes COMPLEX sets the roll / repeat chance (SQPUSHER1: activity), ROLL the
// roll shape (CUTPROC11 speed, WARPCUT share of accelerating rolls, SQPUSHER1 fill scramble,
// CHOOSECUT longest roll), SCRAMBLE how often a block reads from a random eighth instead of its
// own place. ANCHOR and the retrigger odds belong to MOVE.
//   * CLK (1/16 by default) + RST: the song position (the plugin's host transport).
//   * No CLK: RUN (pad or gate) plays at the menu tempo.
//   * GATE + SLICE: jump to a slice (1/12 V per slice from 0 V; the plugin's pads 36-43).
// Outputs: L / R (5 V = full scale), TRIG on every hit (retriggers too), ROLL gate during rolls /
// retriggers, SLC = slice of the last hit (1/12 V per slice), B = 10 V while loop B plays.
// Loops: WAV from the menu (30 s max; stereo in Rack, mono on MetaModule), paths saved in the
// patch. Until loop A is loaded a synthetic two-bar break plays.
// Dropped: the four FX lanes (Performance FX, Ambiotica, Bungee) and the six LFOs (DAW-side
// effects; use Rack modules), the step-sequencer editor (the phrase is the Move-style one: a fill
// bar with its B chance), the sample library / random samples, MIDI out (TRIG / SLC / B instead),
// presets, themes.

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/Sample.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/BreakbeatLayout.hpp"
#include "SlicerCommon.hpp"
#include "breakbeat/BreakbeatEngine.h"

#include <array>
#include <atomic>
#include <memory>

namespace L = semi::layout::breakbeat;
using semi::Screen;

namespace {

constexpr int kBlock = 32;          // engine block; adds 32 samples of latency
constexpr float kVoltsPerUnit = 5.f;
constexpr int kSnapCuts = 96;

const std::vector<std::string> kPhraseNames = {"OFF", "2 bars", "4 bars", "8 bars", "16 bars"};
constexpr int kPhraseBars[] = {0, 2, 4, 8, 16};
const std::vector<std::string> kLenNames = {"AUTO", "1/4 bar", "1/2 bar", "1 bar", "2 bars", "4 bars", "8 bars"};
constexpr float kLenBars[] = {0.f, 0.25f, 0.5f, 1.f, 2.f, 4.f, 8.f};
const std::vector<std::string> kSliceNames = {"AUTO", "8", "16", "32"};
constexpr int kSliceCounts[] = {0, 8, 16, 32};
const std::vector<std::string> kLockNames = {"OFF", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars"};
constexpr int kLockBars[] = {0, 1, 2, 4, 8, 16};

struct Built {
	int loaded = 0;
};

struct Snapshot {
	bbeat::CutRecord cuts[kSnapCuts];
	int nCuts = 0;
	double ppq = 0.0;
	bool running = false;
	int mode = 0;          // 0 stopped, 1 clock, 2 internal
	int cutMode = 0;
	float bpm = 0.f, loopBars = 1.f;
	int slices = 8, curSlice = 0, loop = 0, step = 0, steps = 1;
	float fill = 0.f;
	slc::Heads heads;
};

} // namespace

struct Breakbeat : Module {
	enum ParamId {
		COMPLEX_PARAM, ANCHOR_PARAM, ROLL_PARAM, FILL_PARAM, BCH_PARAM,
		R2_PARAM, R3_PARAM, R4_PARAM, R8_PARAM, SCRAMBLE_PARAM, GATE_PARAM,
		MODE_PARAM, RUN_PARAM,
		PARAMS_LEN
	};
	enum InputId { CLK_INPUT, RST_INPUT, RUN_INPUT, CPLX_INPUT, GATE_INPUT, SLICE_INPUT, INPUTS_LEN };
	enum OutputId { OUT_L_OUTPUT, OUT_R_OUTPUT, TRIG_OUTPUT, ROLL_OUTPUT, SLC_OUTPUT, LOOPB_OUTPUT, OUTPUTS_LEN };
	enum LightId { ENUMS(MODE_LIGHT, bbeat::NumCutModes), RUN_LIGHT, LIGHTS_LEN };

	bbeat::BreakbeatEngine engine;
	bbeat::EngineParams P;

	// ---- saved state
	int cutMode = bbeat::CutMove;
	bool runOn = false;
	int phraseSel = 2;
	int lenSel[2] = {0, 0}, slicesSel[2] = {0, 0};
	int speedSel = 1;           // x1/2, x1, x2
	bool tape = true, declick = true;
	float chaos = 0.f, decay = 0.f;
	int lockSel = 0;
	float internalBpm = 170.f;
	int bbPhraseBars = 3, sdivSel = 1;
	float warpAccel = 0.9f;
	std::string path[2];        // A ("" = synthetic), B ("" = none)

	// ---- loading
	semi::Slot<Built> slot;
	std::string cachePath[2] = {"\x01", "\x01"};
	semi::TripleBuffer<slc::WaveView> wave[2];
	std::atomic<float> autoBars[2];
	std::atomic<int> autoSlices[2];
	std::atomic<bool> hasB{false};
	bool requested = false;

	// ---- clock / pads
	slc::ClockFollower clock;
	dsp::SchmittTrigger gateTrig, runGate;
	dsp::BooleanTrigger modeBtn, runBtn;
	int gateStep = 0, mode = 0;
	double clkPpq = 0.0, clkBpm = 0.0;
	bool clkRun = false;
	bbeat::Pad pads[16];
	int numPads = 0;

	// ---- block adapter / outputs
	float outBuf[2][kBlock]{};
	float blockL[kBlock]{}, blockR[kBlock]{};
	int pos = 0;
	dsp::PulseGenerator trigPulse;
	float slcVolts = 0.f;
	bool rollGate = false;

	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;
	// last: destroyed first, so its thread (MetaModule) stops before anything it builds into goes
	semi::Loader<Built> loader;

	Breakbeat() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(COMPLEX_PARAM, 0.f, 1.f, 0.5f, "Complexity (BBCut: roll / repeat chance, SQPUSHER1 activity)", " %", 0.f, 100.f);
		configParam(ANCHOR_PARAM, 0.f, 1.f, 0.f, "Anchor (MOVE: keep 1 and 3 in place)", " %", 0.f, 100.f);
		configParam(ROLL_PARAM, 0.f, 1.f, 0.f, "Roll (MOVE: stay on a slice; BBCut: roll shape)", " %", 0.f, 100.f);
		configParam(FILL_PARAM, 0.f, 1.f, 0.f, "Fill (the phrase's last bar)", " %", 0.f, 100.f);
		configParam(BCH_PARAM, 0.f, 1.f, 0.f, "B chance (the fill bar plays loop B)", " %", 0.f, 100.f);
		configParam(R2_PARAM, 0.f, 1.f, 0.f, "Retrigger 2x odds per bar (MOVE)", " %", 0.f, 100.f);
		configParam(R3_PARAM, 0.f, 1.f, 0.f, "Retrigger 3x odds per bar (MOVE)", " %", 0.f, 100.f);
		configParam(R4_PARAM, 0.f, 1.f, 0.f, "Retrigger 4x odds per bar (MOVE)", " %", 0.f, 100.f);
		configParam(R8_PARAM, 0.f, 1.f, 0.f, "Retrigger 8x odds per bar (MOVE)", " %", 0.f, 100.f);
		configParam(SCRAMBLE_PARAM, 0.f, 1.f, 0.5f, "Scramble (BBCut: blocks from a random eighth)", " %", 0.f, 100.f);
		configParam(GATE_PARAM, 0.02f, 1.f, 1.f, "Gate (part of each hit at full level)", " %", 0.f, 100.f);
		configButton(MODE_PARAM, "Cut mode (MOVE / CUTPROC11 / WARPCUT / SQPUSHER1 / CHOOSECUT)");
		configButton(RUN_PARAM, "Run (internal clock when CLK is not patched)");
		configInput(CLK_INPUT, "Clock (1/16 by default)");
		configInput(RST_INPUT, "Reset (next clock = bar 1)");
		configInput(RUN_INPUT, "Run gate (internal clock)");
		configInput(CPLX_INPUT, "Complexity CV (+-5 V = +-50 %)");
		configInput(GATE_INPUT, "Gate (jump to a slice)");
		configInput(SLICE_INPUT, "Slice (1/12 V per slice from 0 V; unpatched = next slice)");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		configOutput(TRIG_OUTPUT, "Hit trigger");
		configOutput(ROLL_OUTPUT, "Roll / retrigger gate");
		configOutput(SLC_OUTPUT, "Slice of the last hit (1/12 V per slice)");
		configOutput(LOOPB_OUTPUT, "Loop B gate");
		for (int i = 0; i < bbeat::NumCutModes; i++)
			configLight(MODE_LIGHT + i, bbeat::cutModeName(i));
		for (int i = 0; i < 2; i++) {
			autoBars[i].store(2.f);
			autoSlices[i].store(16);
		}
		engine.prepare(44100.0, kBlock);
		loader.attach(this, &slot, [this](const std::string& req, Built& b) { return build(req, b); });
#ifdef METAMODULE
		requestLoops();
#endif
	}

	~Breakbeat() override {
		engine.collectGarbage();
	}

	void onAdd(const AddEvent& e) override {
		Module::onAdd(e);
		if (!requested)
			requestLoops();
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		engine.prepare(e.sampleRate, kBlock);
		pos = 0;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		cutMode = bbeat::CutMove;
		runOn = false;
	}

	void requestLoops() {
		requested = true;
		loader.request(path[0] + "\n" + path[1]);
	}

	// ---- builder (Rack: UI thread; MetaModule: the loader's AsyncThread) ------------------------
	bool build(const std::string& req, Built& out) {
		engine.collectGarbage();
		const size_t nl = req.find('\n');
		const std::string want[2] = {req.substr(0, nl), nl == std::string::npos ? std::string() : req.substr(nl + 1)};
		bool ok = true;
		for (int s = 0; s < 2; s++) {
			if (want[s] == cachePath[s])
				continue;
			if (s == 1 && want[s].empty()) {
				engine.clearSlot(1);
				hasB = false;
				cachePath[1] = "";
				slc::WaveView& v = wave[1].writeSlot();
				v = slc::WaveView();
				wave[1].publish();
				continue;
			}
			slc::Break b;
			if (want[s].empty())
				slc::synthBreak(b);
#ifdef METAMODULE
			else if (!slc::loadBreak(want[s], b, false)) {
#else
			else if (!slc::loadBreak(want[s], b, true)) {
#endif
				ok = false;
				continue;
			}
			auto d = std::make_unique<bbeat::SampleData>();
			d->totalFrames = b.frames();
			d->numChannels = b.r.empty() ? 1 : 2;
			d->name = b.name;
			// the engine reads at its own rate: resample to 48 kHz-equivalent is not needed, the
			// playback rate follows the loop length in bars (frames per beat)
			d->l = std::move(b.l);
			d->r = std::move(b.r);
			// AUTO grid: the length whose tempo is nearest 160 BPM, one slice per eighth
			const double secs = (double) d->totalFrames / b.rate;
			static const float cands[] = {0.25f, 0.5f, 1.f, 2.f, 4.f, 8.f};
			float best = 1.f;
			double bestErr = 1e9;
			for (float c : cands) {
				const double bpm = c * 4.0 * 60.0 / std::max(1e-3, secs);
				if (bpm < 60.0 || bpm > 240.0)
					continue;
				const double e = std::abs(std::log2(bpm / 160.0));
				if (e < bestErr) {
					bestErr = e;
					best = c;
				}
			}
			const int sl = best * 8.f >= 24.f ? 32 : best * 8.f >= 12.f ? 16 : 8;
			autoBars[s].store(best);
			autoSlices[s].store(sl);
			slc::WaveView& v = wave[s].writeSlot();
			if (d->numChannels > 1) {
				std::vector<float> mono((size_t) d->totalFrames);
				for (int i = 0; i < d->totalFrames; i++)
					mono[(size_t) i] = 0.5f * (d->l[(size_t) i] + d->r[(size_t) i]);
				v.setWave(mono.data(), d->totalFrames);
			}
			else {
				v.setWave(d->l.data(), d->totalFrames);
			}
			v.setName(d->name);
			wave[s].publish();
			engine.submitSample(s, std::move(d), false);
			if (s == 1)
				hasB = true;
			cachePath[s] = want[s];
		}
		out.loaded = ok ? 1 : 0;
		return ok;
	}

	// ---- patch -----------------------------------------------------------------------------------
	json_t* dataToJson() override {
		json_t* r = json_object();
		json_object_set_new(r, "pathA", json_string(path[0].c_str()));
		json_object_set_new(r, "pathB", json_string(path[1].c_str()));
		json_object_set_new(r, "cutMode", json_integer(cutMode));
		json_object_set_new(r, "run", json_boolean(runOn));
		json_object_set_new(r, "phrase", json_integer(phraseSel));
		json_object_set_new(r, "lenA", json_integer(lenSel[0]));
		json_object_set_new(r, "lenB", json_integer(lenSel[1]));
		json_object_set_new(r, "slicesA", json_integer(slicesSel[0]));
		json_object_set_new(r, "slicesB", json_integer(slicesSel[1]));
		json_object_set_new(r, "speed", json_integer(speedSel));
		json_object_set_new(r, "tape", json_boolean(tape));
		json_object_set_new(r, "declick", json_boolean(declick));
		json_object_set_new(r, "chaos", json_real(chaos));
		json_object_set_new(r, "decay", json_real(decay));
		json_object_set_new(r, "lock", json_integer(lockSel));
		json_object_set_new(r, "bpm", json_real(internalBpm));
		json_object_set_new(r, "bbPhrase", json_integer(bbPhraseBars));
		json_object_set_new(r, "sdiv", json_integer(sdivSel));
		json_object_set_new(r, "accel", json_real(warpAccel));
		json_object_set_new(r, "clock", json_integer(clock.mode));
		return r;
	}

	void dataFromJson(json_t* r) override {
		auto b = [&](const char* k, bool& v) { if (json_t* j = json_object_get(r, k)) v = json_boolean_value(j); };
		auto i = [&](const char* k, int& v, int lo, int hi) { if (json_t* j = json_object_get(r, k)) v = clamp((int) json_integer_value(j), lo, hi); };
		auto f = [&](const char* k, float& v, float lo, float hi) { if (json_t* j = json_object_get(r, k)) v = clamp((float) json_number_value(j), lo, hi); };
		i("cutMode", cutMode, 0, bbeat::NumCutModes - 1);
		b("run", runOn);
		i("phrase", phraseSel, 0, 4);
		i("lenA", lenSel[0], 0, 6);
		i("lenB", lenSel[1], 0, 6);
		i("slicesA", slicesSel[0], 0, 3);
		i("slicesB", slicesSel[1], 0, 3);
		i("speed", speedSel, 0, 2);
		b("tape", tape);
		b("declick", declick);
		f("chaos", chaos, 0.f, 1.f);
		f("decay", decay, 0.f, 1.f);
		i("lock", lockSel, 0, 5);
		f("bpm", internalBpm, 60.f, 240.f);
		i("bbPhrase", bbPhraseBars, 1, 4);
		i("sdiv", sdivSel, 0, 3);
		f("accel", warpAccel, 0.5f, 0.99f);
		i("clock", clock.mode, 0, slc::ClockFollower::kNumModes - 1);
		path[0] = path[1] = "";
		if (json_t* j = json_object_get(r, "pathA"))
			path[0] = json_string_value(j);
		if (json_t* j = json_object_get(r, "pathB"))
			path[1] = json_string_value(j);
		requestLoops();
	}

	// ---- audio -----------------------------------------------------------------------------------
	void fillParams() {
		const float cplx = clamp(params[COMPLEX_PARAM].getValue() + inputs[CPLX_INPUT].getVoltage() / 10.f, 0.f, 1.f);
		const float roll = params[ROLL_PARAM].getValue();
		P.complexity = cplx;
		P.anchor = params[ANCHOR_PARAM].getValue();
		P.roll = roll;
		P.intensity = 1.f;
		P.chaos = chaos;
		P.gate = params[GATE_PARAM].getValue();
		P.decay = decay;
		P.lockBars = kLockBars[clamp(lockSel, 0, 5)];
		const int bars = kPhraseBars[clamp(phraseSel, 0, 4)];
		P.seqSteps = bars > 0 ? bars : 1;
		P.stepBars = 1.f;
		P.globalSpeed = clamp(speedSel, 0, 2) + 1;
		P.tapeSpeed = tape;
		P.retrig = {params[R2_PARAM].getValue(), params[R3_PARAM].getValue(), params[R4_PARAM].getValue(), params[R8_PARAM].getValue()};
		P.declick = declick;
		P.internalRun = !inputs[CLK_INPUT].isConnected() && (inputs[RUN_INPUT].isConnected() ? runGate.isHigh() : runOn);
		P.internalBpm = internalBpm;
		P.cutMode = cutMode;
		P.scramble = params[SCRAMBLE_PARAM].getValue();
		bbeat::bbcut::Params& bp = P.bbcut;
		static const int sdivs[4] = {6, 8, 12, 16};
		bp.phraseBeats = 4.f * (float) clamp(bbPhraseBars, 1, 4);
		bp.sdiv = sdivs[clamp(sdivSel, 0, 3)];
		bp.repeatsChance = cplx;
		bp.repeatsSpeed = 2 + (int) std::round(roll * 2.f);
		bp.repeatsArea = 0.5f;
		bp.straightChance = 1.f - cplx;
		bp.arithChance = 1.f - roll;
		bp.accelChance = 0.6f;
		bp.accel = warpAccel;
		bp.activity = cplx;
		bp.fillFreq = 4;
		bp.fillScramble = roll;
		bp.rollChance = cplx;
		bp.rollAllowed = 1.f + 3.f * roll;
	}

	void updateGridAndSteps() {
		for (int s = 0; s < 2; s++) {
			const float bars = lenSel[s] > 0 ? kLenBars[clamp(lenSel[s], 0, 6)] : autoBars[s].load();
			const int slices = slicesSel[s] > 0 ? kSliceCounts[clamp(slicesSel[s], 0, 3)] : autoSlices[s].load();
			engine.setSlotGrid(s, bars, slices);
		}
		const int bars = kPhraseBars[clamp(phraseSel, 0, 4)];
		const int n = bars > 0 ? bars : 1;
		const int fill = (int) std::round(params[FILL_PARAM].getValue() * 100.f);
		const int bch = (int) std::round(params[BCH_PARAM].getValue() * 100.f);
		for (int i = 0; i < n; i++) {
			bbeat::Step st;
			if (bars > 0 && i == n - 1) {
				st.fill = fill;
				st.bChance = hasB ? bch : 0;
			}
			engine.setStep(i, st);
		}
	}

	void runBlock() {
		slot.poll();   // only frees the hand-over object: the engine has its own sample exchange
		fillParams();
		bbeat::TransportInfo t;
		t.hostPlaying = clkRun;
		t.hasPpq = clkRun;
		t.ppq = clkPpq;
		t.bpm = clkRun ? clkBpm : 0.0;
		engine.process(blockL, blockR, kBlock, P, t, pads, numPads);
		numPads = 0;
		for (int i = 0; i < kBlock; i++) {
			outBuf[0][i] = blockL[i];
			outBuf[1][i] = blockR[i];
		}
		for (int h = 0; h < engine.numHits(); h++) {
			trigPulse.trigger(1e-3f);
			slcVolts = engine.hit(h).slice / 12.f;
		}
		const bool running = engine.status.running.load(std::memory_order_relaxed);
		const int w = engine.cutWrite;
		const bool rollCut = w > 0 && engine.cutLog[(size_t) ((w - 1) % bbeat::BreakbeatEngine::cutHistory)].roll;
		rollGate = running && (engine.status.retrigDiv.load(std::memory_order_relaxed) > 1 || rollCut);
	}

	void process(const ProcessArgs& args) override {
		if (modeBtn.process(params[MODE_PARAM].getValue() > 0.f))
			cutMode = (cutMode + 1) % bbeat::NumCutModes;
		if (runBtn.process(params[RUN_PARAM].getValue() > 0.f))
			runOn = !runOn;
		if (inputs[RUN_INPUT].isConnected())
			runGate.process(inputs[RUN_INPUT].getVoltage(), 0.1f, 1.f);

		clkRun = clock.process(args.sampleTime, inputs[CLK_INPUT], inputs[RST_INPUT], clkPpq, clkBpm);
		const bool internal = !inputs[CLK_INPUT].isConnected() && (inputs[RUN_INPUT].isConnected() ? runGate.isHigh() : runOn);
		mode = clkRun ? 1 : internal ? 2 : 0;

		if (inputs[GATE_INPUT].isConnected() && gateTrig.process(inputs[GATE_INPUT].getVoltage(), 0.1f, 1.f) && numPads < 16) {
			const int n = engine.status.numSlices.load(std::memory_order_relaxed);
			int slice;
			if (inputs[SLICE_INPUT].isConnected())
				slice = clamp((int) std::round(inputs[SLICE_INPUT].getVoltage() * 12.f), 0, 31);
			else
				slice = (gateStep++) % std::max(1, n);
			pads[numPads++] = {pos, slice};
		}

		outputs[OUT_L_OUTPUT].setVoltage(outBuf[0][pos] * kVoltsPerUnit);
		outputs[OUT_R_OUTPUT].setVoltage(outBuf[1][pos] * kVoltsPerUnit);
		if (++pos >= kBlock) {
			pos = 0;
			runBlock();
		}
		outputs[TRIG_OUTPUT].setVoltage(trigPulse.process(args.sampleTime) ? 10.f : 0.f);
		outputs[ROLL_OUTPUT].setVoltage(rollGate ? 10.f : 0.f);
		outputs[SLC_OUTPUT].setVoltage(slcVolts);
		outputs[LOOPB_OUTPUT].setVoltage(engine.status.running.load(std::memory_order_relaxed)
		                                 && engine.status.loop.load(std::memory_order_relaxed) == 1 ? 10.f : 0.f);

		if (ui.tick(args.sampleTime))
			publishUi();
	}

	void publishUi() {
		updateGridAndSteps();
		for (int i = 0; i < bbeat::NumCutModes; i++)
			lights[MODE_LIGHT + i].setBrightness(cutMode == i ? 1.f : 0.f);
		lights[RUN_LIGHT].setBrightness(mode == 2 ? 1.f : (runOn ? 0.3f : 0.f));

		Snapshot& s = snap.writeSlot();
		const int w = engine.cutWrite;
		const int n = std::min(w, std::min(kSnapCuts, bbeat::BreakbeatEngine::cutHistory));
		for (int k = 0; k < n; k++)
			s.cuts[k] = engine.cutLog[(size_t) ((w - n + k) % bbeat::BreakbeatEngine::cutHistory)];
		s.nCuts = n;
		const auto& st = engine.status;
		s.running = st.running.load(std::memory_order_relaxed);
		s.ppq = st.ppq.load(std::memory_order_relaxed);
		s.mode = mode;
		s.cutMode = cutMode;
		s.bpm = st.bpm.load(std::memory_order_relaxed);
		s.loopBars = st.loopBars.load(std::memory_order_relaxed);
		s.slices = st.numSlices.load(std::memory_order_relaxed);
		s.curSlice = st.slice.load(std::memory_order_relaxed);
		s.loop = st.loop.load(std::memory_order_relaxed);
		s.step = st.step.load(std::memory_order_relaxed);
		s.steps = st.numSteps.load(std::memory_order_relaxed);
		s.fill = st.fill.load(std::memory_order_relaxed);
		s.heads.n = s.running ? 1 : 0;
		s.heads.pos[0] = st.playhead.load(std::memory_order_relaxed);
		s.heads.col[0] = (uint8_t) s.loop;
		snap.publish();
	}
};

// The cut display: two bars of song time across, the loop position each cut reads from down
// (top = the loop's start), one bar per cut; rolls / retriggers magenta, loop B amber, the fill
// bar's cuts outlined. Beat grid, playhead. Moves at most once per published snapshot.
struct CutScreen : Screen {
	Breakbeat* module = nullptr;
	Snapshot demo;

	CutScreen() {
		// a CUTPROC11-like phrase: 3+3, 3+3, 1+1, then a stutter
		const float len[] = {0.75f, 0.75f, 0.75f, 0.75f, 0.25f, 0.25f, 0.5f, 0.5f, 0.5f, 0.5f, 0.25f, 0.25f, 0.25f, 0.25f, 0.125f, 0.125f, 0.125f, 0.125f};
		const float off[] = {0.f, 0.f, 0.375f, 0.375f, 0.75f, 0.75f, 0.5f, 0.5f, 0.0625f, 0.0625f, 0.25f, 0.25f, 0.875f, 0.875f, 0.9f, 0.9f, 0.9f, 0.9f};
		double t = 0.0;
		for (int i = 0; i < 18; i++) {
			demo.cuts[i].ppq = t;
			demo.cuts[i].beats = len[i];
			demo.cuts[i].offset = off[i];
			demo.cuts[i].roll = i >= 14;
			t += len[i];
		}
		demo.nCuts = 18;
		demo.ppq = 6.1;
		demo.running = true;
		demo.mode = 1;
		demo.cutMode = bbeat::CutProc11;
		demo.bpm = 170.f;
		demo.loopBars = 2.f;
		demo.slices = 16;
		demo.step = 1;
		demo.steps = 4;
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		const float pad = 2.5f;
		const float fs = std::max(9.f, semi::platform::kMinText);
		const float x0 = pad, x1 = w - pad, y0 = pad + fs + 2.f, y1 = h - pad;
		constexpr double kWin = 8.0;   // beats across
		const double ws = std::floor(std::max(0.0, s.ppq) / kWin) * kWin;
		auto X = [&](double b) { return x0 + (float) ((b - ws) / kWin) * (x1 - x0); };

		for (int b = 0; b <= (int) kWin; b++)
			line(vg, X(ws + b), y0, X(ws + b), y1, b % 4 == 0 ? semi::theme::cyanDeep() : semi::theme::grid());

		const float barH = std::max(semi::platform::kStroke + 0.6f, (y1 - y0) / std::max(8, s.slices) - 0.6f);
		for (int k = 0; k < s.nCuts; k++) {
			const bbeat::CutRecord& c = s.cuts[k];
			const double a = c.ppq, b = c.ppq + c.beats;
			if (b <= ws || a >= ws + kWin)
				continue;
			const float xa = std::max(x0, X(a)), xb = std::min(x1, X(b));
			const float y = y0 + c.offset * (y1 - y0 - barH);
			NVGcolor col = c.roll ? semi::theme::magenta() : c.loop ? semi::theme::amber() : semi::theme::cyan();
			fillRect(vg, xa, y, std::max(1.2f, xb - xa - 0.8f), barH, col);
			if (c.fill)
				line(vg, xa, y1 - 0.8f, std::max(xa + 1.f, xb - 0.8f), y1 - 0.8f, semi::theme::magenta());
		}
		if (s.running)
			line(vg, X(s.ppq), y0, X(s.ppq), y1, semi::theme::text(), semi::platform::kStroke);

		// header: cut mode, bar.beat / phrase step, tempo
		text(vg, pad, pad - 0.5f, bbeat::cutModeName(s.cutMode), fs, s.cutMode == 0 ? semi::theme::cyan() : semi::theme::magenta());
		char b[64];
		if (s.running) {
			const int bar = (int) std::floor(s.ppq / 4.0);
			std::snprintf(b, sizeof b, "%d.%d  %d/%d%s", bar + 1, (int) std::fmod(std::floor(s.ppq), 4.0) + 1, s.step + 1, s.steps,
			              s.fill > 0.f ? " FILL" : "");
			text(vg, w * 0.38f, pad - 0.5f, b, fs, s.fill > 0.f ? semi::theme::magenta() : semi::theme::text());
			std::snprintf(b, sizeof b, "%s %3.0f", s.mode == 2 ? "RUN" : "CLK", s.bpm);
		}
		else {
			std::snprintf(b, sizeof b, "%s", module && module->inputs[Breakbeat::CLK_INPUT].isConnected() ? "WAIT CLK" : "STOP");
		}
		text(vg, w - pad, pad - 0.5f, b, fs, s.running ? semi::theme::text() : semi::theme::dim(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
	}
};

// The loop playing (A or B): waveform, its slice grid, the playhead, the slice playing.
struct LoopScreen : Screen {
	Breakbeat* module = nullptr;
	slc::WaveView demoWave;
	Snapshot demo;
	slc::WaveView view;

	LoopScreen() {
		slc::Break b;
		slc::synthBreak(b, 8000.0);
		demoWave.setWave(b.l.data(), b.frames());
		demoWave.setName("SYNTH BREAK");
		demo.slices = 16;
		demo.curSlice = 5;
		demo.heads.n = 1;
		demo.heads.pos[0] = 5.4f / 16.f;
		demo.loopBars = 2.f;
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const int loop = s.loop & 1;
		view = module ? module->wave[loop].read() : demoWave;
		if (module && loop == 1 && !view.valid)
			view = module->wave[0].read();
		const int n = clamp(s.slices, 1, slc::WaveView::kMarks);
		view.marks = n;
		for (int i = 0; i < n; i++) {
			view.mark[i] = (float) i / n;
			view.tag[i] = 0;
		}
		view.beats = 0;
		static const NVGcolor tagCols[8] = {semi::theme::dim(), semi::theme::dim(), semi::theme::dim(), semi::theme::dim(),
		                                    semi::theme::dim(), semi::theme::dim(), semi::theme::dim(), semi::theme::dim()};
		const NVGcolor headCols[8] = {semi::theme::cyan(), semi::theme::amber(), semi::theme::cyan(), semi::theme::cyan(),
		                              semi::theme::cyan(), semi::theme::cyan(), semi::theme::cyan(), semi::theme::cyan()};
		char right[48];
		std::snprintf(right, sizeof right, "%s %d/%g", loop ? "B" : "A", s.slices, s.loopBars);
		slc::drawWave(*this, vg, box.size.x, box.size.y, view, s.heads, nullptr, tagCols, headCols, s.curSlice, right);
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

} // namespace

struct BreakbeatWidget : ModuleWidget {
	BreakbeatWidget(Breakbeat* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Breakbeat.svg")));

		auto* cs = semi::createScreenMm<CutScreen>(Vec(L::CUTS.x, L::CUTS.y), Vec(L::CUTS.w, L::CUTS.h));
		cs->module = module;
		addChild(cs);
		auto* ls = semi::createScreenMm<LoopScreen>(Vec(L::WAVE.x, L::WAVE.y), Vec(L::WAVE.w, L::WAVE.h));
		ls->module = module;
		addChild(ls);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		addParam(createParamCentered<semi::Button>(at(L::MODE), module, Breakbeat::MODE_PARAM));
		const L::P leds[bbeat::NumCutModes] = {L::MODE_LED0, L::MODE_LED1, L::MODE_LED2, L::MODE_LED3, L::MODE_LED4};
		for (int i = 0; i < bbeat::NumCutModes; i++) {
			if (i == 0)
				addChild(createLightCentered<semi::CyanLedSmall>(at(leds[i]), module, Breakbeat::MODE_LIGHT + i));
			else
				addChild(createLightCentered<semi::MagentaLedSmall>(at(leds[i]), module, Breakbeat::MODE_LIGHT + i));
		}
		addParam(createParamCentered<semi::PadCyan>(at(L::RUN), module, Breakbeat::RUN_PARAM));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::RUN_LED), module, Breakbeat::RUN_LIGHT));

		addParam(createParamCentered<semi::KnobMHot>(at(L::COMPLEX), module, Breakbeat::COMPLEX_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::ANCHOR), module, Breakbeat::ANCHOR_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::ROLL), module, Breakbeat::ROLL_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::FILL), module, Breakbeat::FILL_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::BCH), module, Breakbeat::BCH_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::R2), module, Breakbeat::R2_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::R3), module, Breakbeat::R3_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::R4), module, Breakbeat::R4_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::R8), module, Breakbeat::R8_PARAM));
		addParam(createParamCentered<semi::KnobSHot>(at(L::SCRAMBLE), module, Breakbeat::SCRAMBLE_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::GATE), module, Breakbeat::GATE_PARAM));

		addInput(createInputCentered<semi::Jack>(at(L::CLK), module, Breakbeat::CLK_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RST), module, Breakbeat::RST_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RUN_IN), module, Breakbeat::RUN_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::CPLX_CV), module, Breakbeat::CPLX_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::GATE_IN), module, Breakbeat::GATE_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::SLICE), module, Breakbeat::SLICE_INPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_L), module, Breakbeat::OUT_L_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_R), module, Breakbeat::OUT_R_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::TRIG), module, Breakbeat::TRIG_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::ROLL_OUT), module, Breakbeat::ROLL_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::SLC), module, Breakbeat::SLC_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::LOOPB), module, Breakbeat::LOOPB_OUTPUT));
	}

#ifndef METAMODULE
	// Rack: free the loops the audio thread swapped out (UI thread = the builder's thread).
	void step() override {
		if (auto* m = dynamic_cast<Breakbeat*>(module))
			m->engine.collectGarbage();
		ModuleWidget::step();
	}
#endif

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Breakbeat*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel(std::string("Loop A: ") + (m->path[0].empty() ? "synthetic" : semi::fileName(m->path[0]))));
		menu->addChild(createMenuLabel(std::string("Loop B: ") + (m->path[1].empty() ? "none" : semi::fileName(m->path[1]))
		                               + (m->loader.failed ? "  (last load failed)" : "")));
		menu->addChild(createMenuItem("Load loop A (WAV)...", "", [=]() {
			semi::openFileDialog("WAV:wav,WAV", semi::dirName(m->path[0]), [=](const std::string& p) {
				m->path[0] = p;
				m->requestLoops();
			});
		}));
		menu->addChild(createMenuItem("Load loop B (WAV)...", "", [=]() {
			semi::openFileDialog("WAV:wav,WAV", semi::dirName(m->path[1].empty() ? m->path[0] : m->path[1]), [=](const std::string& p) {
				m->path[1] = p;
				m->requestLoops();
			});
		}));
		menu->addChild(createMenuItem("Loop A: synthetic break", "", [=]() {
			m->path[0] = "";
			m->requestLoops();
		}));
		menu->addChild(createMenuItem("Clear loop B", "", [=]() {
			m->path[1] = "";
			m->requestLoops();
		}));

		menu->addChild(new MenuSeparator);
		menu->addChild(createSubmenuItem("Loops", "", [=](Menu* sub) {
			addIndex(sub, "Loop A length", kLenNames, &m->lenSel[0]);
			addIndex(sub, "Loop A slices", kSliceNames, &m->slicesSel[0]);
			addIndex(sub, "Loop B length", kLenNames, &m->lenSel[1]);
			addIndex(sub, "Loop B slices", kSliceNames, &m->slicesSel[1]);
			addIndex(sub, "Speed", {"x1/2", "x1", "x2"}, &m->speedSel);
			sub->addChild(createIndexSubmenuItem("Speed mode", {"TAPE (pitch follows)", "CHOP (pitch kept)"},
				[=]() { return (size_t) (m->tape ? 0 : 1); }, [=](size_t i) { m->tape = i == 0; }));
			sub->addChild(createBoolPtrMenuItem("Declick (2 ms crossfade)", "", &m->declick));
			addSteps(sub, "Decay (after the gate)", &m->decay, {0.f, 0.1f, 0.25f, 0.5f, 0.75f, 1.f}, "%.0f %%", 100.f);
		}));
		menu->addChild(createSubmenuItem("Phrase", "", [=](Menu* sub) {
			addIndex(sub, "Phrase (fill on the last bar)", kPhraseNames, &m->phraseSel);
			addSteps(sub, "Chaos", &m->chaos, {0.f, 0.1f, 0.25f, 0.5f, 0.75f, 1.f}, "%.0f %%", 100.f);
			addIndex(sub, "Pattern lock (MOVE)", kLockNames, &m->lockSel);
		}));
		menu->addChild(createSubmenuItem("BBCut", "", [=](Menu* sub) {
			addIndex(sub, "Phrase (CUTPROC11 / WARP / CHOOSE)", {"1 bar", "2 bars", "3 bars", "4 bars"}, &m->bbPhraseBars, 1);
			addIndex(sub, "CUTPROC11 subdivision", {"6", "8", "12", "16"}, &m->sdivSel);
			addSteps(sub, "WARPCUT acceleration", &m->warpAccel, {0.7f, 0.8f, 0.85f, 0.9f, 0.95f}, "%.2f");
		}));
		menu->addChild(createSubmenuItem("Clock", "", [=](Menu* sub) {
			addIndex(sub, "CLK input", slc::ClockFollower::modeNames(), &m->clock.mode);
			addSteps(sub, "Internal tempo (RUN)", &m->internalBpm, {90.f, 120.f, 140.f, 160.f, 170.f, 174.f, 180.f}, "%.0f BPM");
		}));
	}
};

Model* modelBreakbeat = createModel<Breakbeat, BreakbeatWidget>("Breakbeat");
