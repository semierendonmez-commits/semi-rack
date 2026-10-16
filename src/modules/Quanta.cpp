// QUANTA — Rack / MetaModule port of QUANTA, a microsound particle synth after Curtis Roads
// ("Microsound", 2001) with an arrangement generator built on structural habits of Ryoji Ikeda's
// work. Engine: engines/quanta (semierendonmez-commits/quanta Source/engine; changes marked
// "SemiFamily" there: no <sstream>, a smaller particle pool on MetaModule, setSlotHz()).
//
// VOICE (the plugin's SYNTH page): particle TYPE / WAVE / DENSITY / DUR / SHAPE (the type's own
// parameter: GRAIN sample position, GLISSON glide, GRAINLET cycles, TRAINLET impulses, PULSAR
// duty) / MASK / JITTER (time, pitch and amp together) / SPREAD / TUNE, SYNC, WINDOW, ADSR.
//   V/OCT + GATE play it (polyphonic, up to 8 channels = the plugin's 8 MIDI voices; 0 V = C4).
//   GATE unpatched: the voice runs free at V/OCT (menu: off).
// ARRANGEMENT: eight channels (KICK, CLICK, DATA A/B, NOISE, SINE LO/HI, CLOUD) in blocks, from a
//   FORM (ACCUMULATE .. ARC), a seed (REGEN) and EXTREMITY / SILENCE / FILLS; MOTION moves patterns
//   inside long blocks. ARR on: it plays from CLK (1/16 notes by default) or, with CLK unpatched,
//   from the menu tempo. RST restarts the piece. FX + HOLD (or the FX gate) = the plugin's perform pads.
//   Menu: length, block size, mutation, cut quantize, loop, play mode, channels (mute, level, tune,
//   role, register, sound).
// Threads: the arrangement is generated off the audio thread (Rack: the widget's step(); MetaModule:
// an AsyncThread) and handed over through semi::Slot; samples load through semi::Loader.
// Rack audio is +-5 V; 5 V = 1.0 for the engine (its safety limiter holds -0.3 dBFS).

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/Sample.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/QuantaLayout.hpp"
#include "quanta/Arrange.h"
#include "quanta/Particles.h"

#include <atomic>

namespace L = semi::layout::quanta;
using semi::Screen;

namespace {

constexpr int kBlock = 32;           // engine block; adds 32 samples of latency
constexpr float kVoltsPerUnit = 5.f;
constexpr int kVoices = 8;
constexpr int kCols = 96;            // barcode columns
#ifdef METAMODULE
constexpr int kDots = 48;            // particles drawn per frame
constexpr size_t kMaxSampleFrames = 48000 * 20;
#else
constexpr int kDots = 160;
constexpr size_t kMaxSampleFrames = 48000 * 60;
#endif
constexpr float kCloudSpan = 3.f;    // seconds of particles on the cloud screen

const std::vector<std::string> kTypeNames = {"GRAIN", "GLISSON", "GRAINLET", "TRAINLET", "PULSAR"};
const std::vector<std::string> kWaveNames = {"SINE", "SAW", "SQUARE", "NOISE", "SAMPLE"};
const std::vector<std::string> kWinNames = {"GAUSS", "HANN", "EXPODEC", "REXPODEC", "TRAPEZ", "RECT"};
const std::vector<std::string> kSyncNames = {"FREE", "1/4", "1/8", "1/16", "1/32", "1/64", "1/4T", "1/8T", "1/16T", "1/32T"};
const std::vector<std::string> kFormNames = {"ACCUMULATE", "ALTERNATE", "PERMUTE", "CODEX", "MARKOV", "ARC"};
const std::vector<std::string> kRoleNames = {"SINE", "PULSE", "DATA", "NOISE", "CLOUD"};
const std::vector<std::string> kRegNames = {"LOW", "MID", "HIGH"};
const std::vector<std::string> kPpqnNames = {"1 (quarter notes)", "2 (8ths)", "4 (16ths)", "8 (32nds)", "24 (DIN sync)"};
constexpr int kPpqn[] = {1, 2, 4, 8, 24};
constexpr int kLengths[] = {16, 32, 64, 128, 256, 512};
constexpr int kBlockSizes[] = {1, 2, 4, 8, 16, 32};
constexpr int kMutations[] = {0, 1, 2, 3, 4, 6, 8, 12, 16};
constexpr float kTempos[] = {60, 70, 80, 90, 100, 110, 120, 128, 135, 140, 150, 160, 170, 174, 180};

// The plugin's perform pads (QuantaProcessor::pad): fx mode, repeat slice (beats), ramp (beats).
struct Pad { const char* name; int fx; double slice; double ramp; };
const Pad kPads[10] = {
	{"RPT 1/8", qn::FxRepeat, 0.5, 1}, {"RPT 1/16", qn::FxRepeat, 0.25, 1}, {"RPT 1/32", qn::FxRepeat, 0.125, 1},
	{"REVERSE", qn::FxReverse, 1.0, 1}, {"TAPE STOP", qn::FxTape, 0.5, 1.0}, {"HP SWEEP", qn::FxHpf, 0.5, 4.0},
	{"LP SWEEP", qn::FxLpf, 0.5, 4.0}, {"CRUSH", qn::FxCrush, 0.5, 2.0}, {"THROW", qn::FxThrow, 0.5, 1}, {"GATE", qn::FxGate, 0.5, 1}};

inline float expoToVal(float n, float lo, float hi, float ex) { return lo + (hi - lo) * std::pow(clamp(n, 0.f, 1.f), ex); }
inline float valToExpo(float v, float lo, float hi, float ex) { return std::pow(clamp((v - lo) / (hi - lo), 0.f, 1.f), 1.f / ex); }

struct ExpoQuantity : ParamQuantity {
	float lo = 0.f, hi = 1.f, ex = 1.f;
	float getDisplayValue() override { return expoToVal(getValue(), lo, hi, ex); }
	void setDisplayValue(float d) override { setValue(valToExpo(d, lo, hi, ex)); }
};

// SHAPE: the selected particle type's own parameter (normalised knob 0..1).
struct ShapeMap {
	static const char* name(int type) {
		static const char* n[] = {"Sample position", "Glide", "Cycles", "Impulses", "Duty cycle"};
		return n[clamp(type, 0, 4)];
	}
	static const char* tag(int type) {
		static const char* n[] = {"POS", "GLIDE", "CYC", "IMP", "DUTY"};
		return n[clamp(type, 0, 4)];
	}
	static float value(int type, float n) {
		switch (type) {
			case qn::Grain: return 100.f * n;
			case qn::Glisson: return 24.f * n;
			case qn::Grainlet:
			case qn::Trainlet: return 1.f + 63.f * n * n;
			default: return 1.f + 99.f * n;
		}
	}
	static const char* unit(int type) {
		static const char* u[] = {" %", " st", "", "", " %"};
		return u[clamp(type, 0, 4)];
	}
	static float defaultNorm(int type) {
		switch (type) {
			case qn::Grain: return 0.f;
			case qn::Glisson: return 7.f / 24.f;
			case qn::Grainlet:
			case qn::Trainlet: return std::sqrt(7.f / 63.f);   // 8
			default: return 24.f / 99.f;                        // 25 %
		}
	}
};

struct ShapeQuantity : ParamQuantity {
	const int* type = nullptr;
	int t() const { return type ? *type : (int) qn::Pulsar; }
	std::string getLabel() override { return std::string("Shape: ") + ShapeMap::name(t()); }
	std::string getDisplayValueString() override {
		const float v = ShapeMap::value(t(), getValue());
		return string::f(t() == qn::Grainlet || t() == qn::Trainlet ? "%.1f%s" : "%.0f%s", v, ShapeMap::unit(t()));
	}
};

// ---- the arrangement as the worker hands it over: the blocks + the barcode columns for the screen
struct Barcode {
	uint8_t mask[kCols] = {};      // channel bits
	uint8_t energy[kCols] = {};    // 0..255
	uint8_t flags[kCols] = {};     // 1 = inside a fill, 2 = first column of a block
	int blocks = 0;
	float totalBeats = 0.f;
};

struct ArrPack {
	qn::Arrangement arr;
	Barcode bar;
	uint64_t hash = 0;
};

void buildBarcode(ArrPack& p) {
	const qn::Arrangement& a = p.arr;
	Barcode& b = p.bar;
	b.blocks = (int) a.blocks.size();
	const double total = a.totalBeats();
	b.totalBeats = (float) total;
	if (total <= 0.0)
		return;
	size_t blk = 0;
	double start = 0.0;
	int prev = -1;
	for (int c = 0; c < kCols; c++) {
		const double beat = (c + 0.5) / kCols * total;
		while (blk + 1 < a.blocks.size() && beat >= start + a.blocks[blk].beats) {
			start += a.blocks[blk].beats;
			blk++;
		}
		const qn::Block& B = a.blocks[blk];
		uint8_t m = 0;
		for (int l = 0; l < qn::kLanes; l++)
			if (B.lanes[l].type != qn::BNone)
				m |= (uint8_t) (1u << l);
		b.mask[c] = m;
		b.energy[c] = (uint8_t) clamp((int) std::round(B.energy * 255.f), 0, 255);
		uint8_t f = 0;
		if (B.fill != qn::FillNone && beat - start >= B.beats - B.fillBeats)
			f |= 1;
		if ((int) blk != prev)
			f |= 2;
		b.flags[c] = f;
		prev = (int) blk;
	}
}

struct QSample {
	std::shared_ptr<const qn::SampleData> sp;
};

struct Dot {
	float age, hz, amp, durMs;
	uint8_t type;
};

struct Snapshot {
	// voice
	int type = qn::Pulsar, wave = qn::Sine, window = qn::Gauss, sync = 0;
	float density = 40.f, durMs = 25.f, shape = 25.f;
	int particles = 0;
	float limGr = 1.f;
	Dot dots[kDots];
	int nDots = 0;
	// arrangement
	Barcode bar;
	bool arrOn = false, playing = false;
	float playhead = -1.f;   // 0..1 of the piece
	int block = -1, fill = 0, form = 5, seed = 0;
	float bpm = 120.f;
	bool hasArr = false;
};

} // namespace

struct Quanta : Module {
	enum ParamId {
		DENSITY_PARAM, DUR_PARAM, SHAPE_PARAM, MASK_PARAM, JITTER_PARAM, SPREAD_PARAM, TUNE_PARAM, WAVE_PARAM,
		FORM_PARAM, EXTREM_PARAM, SILENCE_PARAM, FILLS_PARAM, MOTION_PARAM, ARR_LVL_PARAM, FX_PARAM, OUT_PARAM,
		SYNC_PARAM, WINDOW_PARAM, ATK_PARAM, DEC_PARAM, SUS_PARAM, REL_PARAM,
		HOLD_PARAM, TYPE_PARAM, ARR_PARAM, REGEN_PARAM,
		PARAMS_LEN
	};
	enum InputId { VOCT_INPUT, GATE_INPUT, CLK_INPUT, RST_INPUT, DENS_CV_INPUT, DUR_CV_INPUT, FX_GATE_INPUT, REGEN_INPUT, INPUTS_LEN };
	enum OutputId { OUT_L_OUTPUT, OUT_R_OUTPUT, BLK_OUTPUT, NRG_OUTPUT, EOC_OUTPUT, OUTPUTS_LEN };
	enum LightId { ENUMS(TYPE_LIGHT, 5), ARR_LIGHT, CLK_LIGHT, SMP_LIGHT, FILL_LIGHT, LIGHTS_LEN };

	qn::Engine engine;
	qn::Player player;
	qn::Scene synth;

	// ---- state outside the knobs (menu + JSON)
	int type = qn::Pulsar;
	float shapeStore[qn::kNumTypes] = {};
	bool arrOn = false;
	std::atomic<int> seed{0};
	int lengthIdx = 2, blockIdx = 2, mutIdx = 2, cutq = qn::CutBar;
	bool pow2 = true, loop = true;
	float fxMix = 50.f;
	int playMode = 0;          // 0 AUTO, 1 GATE + V/OCT play / transpose the arrangement
	bool freeRun = true;       // GATE unpatched: the voice drones
	int glideDir = qn::Up;
	float chroma = 50.f;
	int ppqnIdx = 2, tempoIdx = 6;
	struct Channel {
		int role = qn::BSine, reg = qn::RegMid;
		float level = 75.f, tune = 0.f;
		bool mute = false;
	};
	Channel ch[qn::kLanes];

	// ---- worker / hand-over
	semi::Slot<ArrPack> arrSlot;
	std::atomic_flag workerBusy = ATOMIC_FLAG_INIT;
	uint64_t builtHash = 0;
	semi::Slot<QSample> sampleSlot;
	semi::Loader<QSample> loader;
	std::atomic<bool> hasSample{false};
#ifdef METAMODULE
	MetaModule::AsyncThread worker{this, [this]() { serviceWorker(); }};
#endif

	// ---- audio-thread state
	dsp::BooleanTrigger typeTrig, arrTrig, regenTrig;
	dsp::SchmittTrigger clkTrig, rstTrig, regenInTrig;
	bool gateOn[kVoices] = {};
	float voiceHz[kVoices] = {};
	bool droning = false;
	int clkCount = -1;
	float sinceClk = 0.f, period = 0.f;
	bool clkSeen = false;
	double freePpq = 0.0;
	bool fxHeld = false;
	int fxPad = -1;
	double padStart = 0.0;
	int lastBlock = -1;
	double lastPos = 0.0;
	dsp::PulseGenerator blkPulse, eocPulse;
	float energyNow = 0.f;

	float outBuf[2][kBlock]{};
	int pos = 0;

	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;

	Quanta() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		auto expo = [&](int id, const char* name, float lo, float hi, float def, float ex, const char* unit) {
			auto* q = configParam<ExpoQuantity>(id, 0.f, 1.f, valToExpo(def, lo, hi, ex), name, unit);
			q->lo = lo;
			q->hi = hi;
			q->ex = ex;
		};
		expo(DENSITY_PARAM, "Density", 0.1f, 2000.f, 40.f, 4.f, " /s");
		expo(DUR_PARAM, "Duration", 0.5f, 200.f, 25.f, 2.5f, " ms");
		auto* sq = configParam<ShapeQuantity>(SHAPE_PARAM, 0.f, 1.f, ShapeMap::defaultNorm(qn::Pulsar), "Shape");
		sq->type = &type;
		configParam(MASK_PARAM, 0.f, 100.f, 0.f, "Masking (stochastic)", " %");
		configParam(JITTER_PARAM, 0.f, 1.f, 0.f, "Jitter (time, pitch, amp)", " %", 0.f, 100.f);
		configParam(SPREAD_PARAM, 0.f, 100.f, 50.f, "Stereo spread", " %");
		configParam(TUNE_PARAM, -24.f, 24.f, 0.f, "Tune", " st");
		configSwitch(WAVE_PARAM, 0.f, 4.f, 0.f, "Waveform", kWaveNames);
		configSwitch(FORM_PARAM, 0.f, 5.f, 5.f, "Form", kFormNames);
		configParam(EXTREM_PARAM, 0.f, 100.f, 40.f, "Extremity", " %");
		configParam(SILENCE_PARAM, 0.f, 100.f, 10.f, "Silence", " %");
		configParam(FILLS_PARAM, 0.f, 100.f, 50.f, "Fills", " %");
		configParam(MOTION_PARAM, 0.f, 100.f, 25.f, "Motion", " %");
		configParam(ARR_LVL_PARAM, 0.f, 1.f, 1.f, "Arrangement level", " %", 0.f, 100.f);
		std::vector<std::string> fx = {"OFF"};
		for (const Pad& p : kPads)
			fx.push_back(p.name);
		configSwitch(FX_PARAM, 0.f, 10.f, 1.f, "Perform FX (HOLD / FX gate)", fx);
		configParam(OUT_PARAM, -60.f, 6.f, -6.f, "Output", " dB");
		configSwitch(SYNC_PARAM, 0.f, 9.f, 0.f, "Sync", kSyncNames);
		configSwitch(WINDOW_PARAM, 0.f, 5.f, 0.f, "Window", kWinNames);
		expo(ATK_PARAM, "Attack", 0.f, 2000.f, 5.f, 2.857f, " ms");
		expo(DEC_PARAM, "Decay", 1.f, 4000.f, 200.f, 2.857f, " ms");
		configParam(SUS_PARAM, 0.f, 100.f, 80.f, "Sustain", " %");
		expo(REL_PARAM, "Release", 1.f, 8000.f, 300.f, 2.857f, " ms");
		configButton(HOLD_PARAM, "Perform FX hold");
		configButton(TYPE_PARAM, "Particle type");
		configButton(ARR_PARAM, "Arrangement on / off");
		configButton(REGEN_PARAM, "Regenerate (new seed)");
		configInput(VOCT_INPUT, "V/OCT (0 V = C4; polyphonic)");
		configInput(GATE_INPUT, "Gate (polyphonic, up to 8 voices)");
		configInput(CLK_INPUT, "Clock (1/16 notes by default)");
		configInput(RST_INPUT, "Reset (restart the piece)");
		configInput(DENS_CV_INPUT, "Density CV (x2 per volt)");
		configInput(DUR_CV_INPUT, "Duration CV (x2 per volt)");
		configInput(FX_GATE_INPUT, "Perform FX gate");
		configInput(REGEN_INPUT, "Regenerate trigger");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		configOutput(BLK_OUTPUT, "Block change trigger");
		configOutput(NRG_OUTPUT, "Block energy (0-10 V)");
		configOutput(EOC_OUTPUT, "End of the piece trigger");
		configLight(CLK_LIGHT, "Clock");
		configLight(SMP_LIGHT, "Sample loaded");
		configLight(FILL_LIGHT, "Fill playing");

		for (int t = 0; t < qn::kNumTypes; t++)
			shapeStore[t] = ShapeMap::defaultNorm(t);
		resetChannels();
		engine.prepare(44100.0);
		player.reset();
		loader.attach(this, &sampleSlot, [](const std::string& path, QSample& out) { return loadSample(path, out); });

		serviceWorker();   // first arrangement, synchronously
		arrSlot.poll();
#ifdef METAMODULE
		worker.start();
#endif
	}

	void resetChannels() {
		const auto d = qn::defaultChannels();
		for (int c = 0; c < qn::kLanes; c++) {
			ch[c] = Channel();
			ch[c].role = d[c].role;
			ch[c].reg = d[c].reg;
			ch[c].level = c == 0 ? 85.f : 75.f;
			player.scenes[c] = qn::factoryScene(d[c].role, d[c].reg);
		}
	}

	static bool loadSample(const std::string& path, QSample& out) {
		semi::Sample s;
		if (!semi::readWav(path, s, kMaxSampleFrames))
			return false;
		auto sd = std::make_shared<qn::SampleData>();
		sd->sr = s.rate;
		sd->data.resize(s.frames);
		for (size_t i = 0; i < s.frames; i++)
			sd->data[i] = s.mono(i);
		out.sp = sd;
		return true;
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		engine.prepare(e.sampleRate);
		player.reset();
		pos = 0;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		type = qn::Pulsar;
		for (int t = 0; t < qn::kNumTypes; t++)
			shapeStore[t] = ShapeMap::defaultNorm(t);
		arrOn = false;
		seed = 0;
		lengthIdx = 2;
		blockIdx = 2;
		mutIdx = 2;
		cutq = qn::CutBar;
		pow2 = true;
		loop = true;
		fxMix = 50.f;
		playMode = 0;
		freeRun = true;
		glideDir = qn::Up;
		chroma = 50.f;
		ppqnIdx = 2;
		tempoIdx = 6;
		resetChannels();
		clkCount = -1;
		freePpq = 0.0;
		engine.reset();
		player.reset();
	}

	// ---- worker side (Rack: widget step; MetaModule: AsyncThread) ------------------------------------
	qn::GenParams makeGen() {
		qn::GenParams g;
		g.form = clamp((int) std::round(params[FORM_PARAM].getValue()), 0, qn::kNumForms - 1);
		g.lengthBars = kLengths[clamp(lengthIdx, 0, 5)];
		g.blockBars = kBlockSizes[clamp(blockIdx, 0, 5)];
		g.pow2 = pow2;
		g.seed = seed.load(std::memory_order_relaxed);
		g.mutation = kMutations[clamp(mutIdx, 0, 8)];
		g.extremity = std::round(params[EXTREM_PARAM].getValue());
		g.silence = std::round(params[SILENCE_PARAM].getValue());
		g.cutq = cutq;
		g.fills = std::round(params[FILLS_PARAM].getValue());
		g.fxMix = fxMix;
		for (int c = 0; c < qn::kLanes; c++) {
			g.channels[c].role = ch[c].role;
			g.channels[c].reg = ch[c].reg;
		}
		return g;
	}

	void serviceWorker() {
		if (workerBusy.test_and_set(std::memory_order_acquire))
			return;
		arrSlot.collect();
		const qn::GenParams g = makeGen();
		const uint64_t h = g.hash();
		if (h != builtHash) {
			builtHash = h;
			auto* p = new ArrPack;
			p->arr = qn::generate(g);
			p->hash = h;
			buildBarcode(*p);
			arrSlot.offer(p);
		}
		workerBusy.clear(std::memory_order_release);
	}

	// ---- JSON --------------------------------------------------------------------------------------------
	static json_t* sceneToJson(const qn::Scene& s) {
		json_t* j = json_object();
		json_object_set_new(j, "type", json_integer(s.type));
		json_object_set_new(j, "density", json_real(s.density));
		json_object_set_new(j, "sync", json_integer(s.sync));
		json_object_set_new(j, "dur", json_real(s.durMs));
		json_object_set_new(j, "wave", json_integer(s.wave));
		json_object_set_new(j, "win", json_integer(s.window));
		json_object_set_new(j, "duty", json_real(s.duty));
		json_object_set_new(j, "mask", json_real(s.mask));
		json_object_set_new(j, "glide", json_real(s.glide));
		json_object_set_new(j, "gdir", json_integer(s.glideDir));
		json_object_set_new(j, "cycles", json_real(s.cycles));
		json_object_set_new(j, "impulses", json_real(s.impulses));
		json_object_set_new(j, "chroma", json_real(s.chroma));
		json_object_set_new(j, "jt", json_real(s.jitTime));
		json_object_set_new(j, "jp", json_real(s.jitPitch));
		json_object_set_new(j, "ja", json_real(s.jitAmp));
		json_object_set_new(j, "spread", json_real(s.spread));
		return j;
	}
	static void sceneFromJson(json_t* j, qn::Scene& s) {
		auto gi = [&](const char* k, int& v, int lo, int hi) {
			if (json_t* x = json_object_get(j, k))
				v = clamp((int) json_integer_value(x), lo, hi);
		};
		auto gf = [&](const char* k, float& v, float lo, float hi) {
			if (json_t* x = json_object_get(j, k))
				v = clamp((float) json_number_value(x), lo, hi);
		};
		gi("type", s.type, 0, qn::kNumTypes - 1);
		gf("density", s.density, 0.1f, 2000.f);
		gi("sync", s.sync, 0, qn::kNumSync - 1);
		gf("dur", s.durMs, 0.5f, 200.f);
		gi("wave", s.wave, 0, qn::kNumWaves - 1);
		gi("win", s.window, 0, qn::kNumWins - 1);
		gf("duty", s.duty, 1.f, 100.f);
		gf("mask", s.mask, 0.f, 100.f);
		gf("glide", s.glide, 0.f, 24.f);
		gi("gdir", s.glideDir, 0, 2);
		gf("cycles", s.cycles, 1.f, 64.f);
		gf("impulses", s.impulses, 1.f, 64.f);
		gf("chroma", s.chroma, 0.f, 100.f);
		gf("jt", s.jitTime, 0.f, 100.f);
		gf("jp", s.jitPitch, 0.f, 24.f);
		gf("ja", s.jitAmp, 0.f, 100.f);
		gf("spread", s.spread, 0.f, 100.f);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		auto seti = [&](const char* k, int v) { json_object_set_new(root, k, json_integer(v)); };
		auto setr = [&](const char* k, double v) { json_object_set_new(root, k, json_real(v)); };
		shapeStore[type] = params[SHAPE_PARAM].getValue();
		seti("type", type);
		json_t* shapes = json_array();
		for (float v : shapeStore)
			json_array_append_new(shapes, json_real(v));
		json_object_set_new(root, "shapes", shapes);
		seti("arr", arrOn);
		seti("seed", seed.load());
		seti("length", lengthIdx);
		seti("block", blockIdx);
		seti("mutation", mutIdx);
		seti("cutq", cutq);
		seti("pow2", pow2);
		seti("loop", loop);
		setr("fxMix", fxMix);
		seti("playMode", playMode);
		seti("freeRun", freeRun);
		seti("glideDir", glideDir);
		setr("chroma", chroma);
		seti("ppqn", ppqnIdx);
		seti("tempo", tempoIdx);
		json_t* chans = json_array();
		for (int c = 0; c < qn::kLanes; c++) {
			json_t* j = json_object();
			json_object_set_new(j, "role", json_integer(ch[c].role));
			json_object_set_new(j, "reg", json_integer(ch[c].reg));
			json_object_set_new(j, "level", json_real(ch[c].level));
			json_object_set_new(j, "tune", json_real(ch[c].tune));
			json_object_set_new(j, "mute", json_boolean(ch[c].mute));
			json_object_set_new(j, "sound", sceneToJson(player.scenes[c]));
			json_array_append_new(chans, j);
		}
		json_object_set_new(root, "channels", chans);
		if (!loader.lastPath.empty() && hasSample)
			json_object_set_new(root, "sample", json_string(loader.lastPath.c_str()));
		return root;
	}

	void dataFromJson(json_t* root) override {
		auto geti = [&](const char* k, int lo, int hi, int def) {
			json_t* j = json_object_get(root, k);
			return j ? clamp((int) json_integer_value(j), lo, hi) : def;
		};
		auto getf = [&](const char* k, float lo, float hi, float def) {
			json_t* j = json_object_get(root, k);
			return j ? clamp((float) json_number_value(j), lo, hi) : def;
		};
		type = geti("type", 0, qn::kNumTypes - 1, qn::Pulsar);
		if (json_t* a = json_object_get(root, "shapes"))
			for (int t = 0; t < qn::kNumTypes; t++)
				if (json_t* v = json_array_get(a, t))
					shapeStore[t] = clamp((float) json_number_value(v), 0.f, 1.f);
		arrOn = geti("arr", 0, 1, 0);
		seed = geti("seed", 0, 9999, 0);
		lengthIdx = geti("length", 0, 5, 2);
		blockIdx = geti("block", 0, 5, 2);
		mutIdx = geti("mutation", 0, 8, 2);
		cutq = geti("cutq", 0, 2, qn::CutBar);
		pow2 = geti("pow2", 0, 1, 1);
		loop = geti("loop", 0, 1, 1);
		fxMix = getf("fxMix", 0.f, 100.f, 50.f);
		playMode = geti("playMode", 0, 1, 0);
		freeRun = geti("freeRun", 0, 1, 1);
		glideDir = geti("glideDir", 0, 2, qn::Up);
		chroma = getf("chroma", 0.f, 100.f, 50.f);
		ppqnIdx = geti("ppqn", 0, 4, 2);
		tempoIdx = geti("tempo", 0, (int) (sizeof kTempos / sizeof kTempos[0]) - 1, 6);
		if (json_t* chans = json_object_get(root, "channels"))
			for (int c = 0; c < qn::kLanes; c++)
				if (json_t* j = json_array_get(chans, c)) {
					if (json_t* v = json_object_get(j, "role"))
						ch[c].role = clamp((int) json_integer_value(v), 0, qn::kNumBlockTypes - 1);
					if (json_t* v = json_object_get(j, "reg"))
						ch[c].reg = clamp((int) json_integer_value(v), 0, 2);
					if (json_t* v = json_object_get(j, "level"))
						ch[c].level = clamp((float) json_number_value(v), 0.f, 100.f);
					if (json_t* v = json_object_get(j, "tune"))
						ch[c].tune = clamp((float) json_number_value(v), -24.f, 24.f);
					if (json_t* v = json_object_get(j, "mute"))
						ch[c].mute = json_boolean_value(v);
					if (json_t* v = json_object_get(j, "sound"))
						sceneFromJson(v, player.scenes[c]);
				}
		if (json_t* s = json_object_get(root, "sample"))
			loader.request(json_string_value(s));
		params[SHAPE_PARAM].setValue(shapeStore[type]);
	}

	// ---- actions (any thread that owns the module: menu, process) ------------------------------------------
	void setType(int t) {
		shapeStore[type] = params[SHAPE_PARAM].getValue();
		type = (t % qn::kNumTypes + qn::kNumTypes) % qn::kNumTypes;
		params[SHAPE_PARAM].setValue(shapeStore[type]);
	}
	void newSeed() { seed = (int) (random::u32() % 10000u); }
	void copyVoiceToChannel(int c) { player.scenes[c] = synth; }
	void resetChannelSound(int c) { player.scenes[c] = qn::factoryScene(ch[c].role, ch[c].reg); }
	void clearSample() {
		sampleSlot.collect();
		sampleSlot.offer(new QSample);
		loader.lastPath.clear();
	}

	// ---- clock ----------------------------------------------------------------------------------------------
	void processClockEdges(float dt) {
		sinceClk += dt;
		if (rstTrig.process(inputs[RST_INPUT].getVoltage(), 0.1f, 1.f)) {
			clkCount = -1;
			freePpq = 0.0;
			player.reset();
		}
		if (clkTrig.process(inputs[CLK_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (clkSeen)
				period = sinceClk;
			clkSeen = true;
			clkCount++;
			sinceClk = 0.f;
		}
		if (!inputs[CLK_INPUT].isConnected()) {
			clkSeen = false;
			period = 0.f;
		}
		else if (period > 0.f && sinceClk > 2.5f * period) {
			clkSeen = false;
			period = 0.f;
		}
	}

	// position (beats) + tempo for the next block; false = stopped
	bool blockTransport(float sr, double& ppq, double& bpm) {
		if (!inputs[CLK_INPUT].isConnected()) {
			bpm = kTempos[clamp(tempoIdx, 0, (int) (sizeof kTempos / sizeof kTempos[0]) - 1)];
			ppq = freePpq;
			freePpq += kBlock * bpm / 60.0 / sr;
			return true;
		}
		const int ppqn = kPpqn[ppqnIdx];
		if (clkCount < 0 || period <= 0.f) {
			ppq = std::max(0, clkCount) / (double) ppqn;
			bpm = 120.0;
			return false;
		}
		const double frac = std::min(sinceClk / period, 0.999f);
		ppq = (clkCount + frac) / ppqn;
		bpm = std::clamp(60.0 / (period * ppqn), 20.0, 400.0);
		return true;
	}

	// ---- audio ----------------------------------------------------------------------------------------------
	// a power-curve knob's value from the param itself (the quantity only holds the curve)
	float expoValue(int id) {
		const auto* q = static_cast<const ExpoQuantity*>(paramQuantities[id]);
		return expoToVal(params[id].getValue(), q->lo, q->hi, q->ex);
	}

	void buildSynthScene() {
		auto ex = [&](int id) { return expoValue(id); };
		qn::Scene& s = synth;
		s.type = type;
		s.density = clamp(ex(DENSITY_PARAM) * std::exp2(clamp(inputs[DENS_CV_INPUT].getVoltage(), -10.f, 10.f)), 0.1f, 2000.f);
		s.durMs = clamp(ex(DUR_PARAM) * std::exp2(clamp(inputs[DUR_CV_INPUT].getVoltage(), -10.f, 10.f)), 0.5f, 200.f);
		s.sync = clamp((int) std::round(params[SYNC_PARAM].getValue()), 0, qn::kNumSync - 1);
		s.wave = clamp((int) std::round(params[WAVE_PARAM].getValue()), 0, qn::kNumWaves - 1);
		s.window = clamp((int) std::round(params[WINDOW_PARAM].getValue()), 0, qn::kNumWins - 1);
		s.mask = params[MASK_PARAM].getValue();
		const float j = params[JITTER_PARAM].getValue();
		s.jitTime = 100.f * j;
		s.jitPitch = 24.f * j * j;
		s.jitAmp = 100.f * j;
		s.spread = params[SPREAD_PARAM].getValue();
		s.fineCents = params[TUNE_PARAM].getValue() * 100.f;
		s.glideDir = glideDir;
		s.chroma = chroma;
		shapeStore[type] = params[SHAPE_PARAM].getValue();
		s.samplePos = ShapeMap::value(qn::Grain, shapeStore[qn::Grain]);
		s.glide = ShapeMap::value(qn::Glisson, shapeStore[qn::Glisson]);
		s.cycles = ShapeMap::value(qn::Grainlet, shapeStore[qn::Grainlet]);
		s.impulses = ShapeMap::value(qn::Trainlet, shapeStore[qn::Trainlet]);
		s.duty = ShapeMap::value(qn::Pulsar, shapeStore[qn::Pulsar]);
	}

	// GATE / V/OCT -> the 8 MIDI voice slots (or the free-running drone on slot 0)
	void updateVoices(bool toArrangement) {
		const bool gateConn = inputs[GATE_INPUT].isConnected();
		const int nGate = gateConn ? std::min(kVoices, std::max(1, inputs[GATE_INPUT].getChannels())) : 0;
		auto hzOf = [&](int c) {
			return 261.6255653f * std::exp2(clamp(inputs[VOCT_INPUT].getPolyVoltage(c), -6.f, 6.f));
		};
		const bool drone = !gateConn && freeRun && !toArrangement;
		if (drone != droning) {
			droning = drone;
			if (drone) {
				voiceHz[0] = hzOf(0);
				engine.noteOn(0, voiceHz[0], 0.8f);
			}
			else {
				engine.noteOff(0);
			}
		}
		if (drone) {
			const float hz = hzOf(0);
			if (std::abs(hz - voiceHz[0]) > voiceHz[0] * 1e-4f) {
				voiceHz[0] = hz;
				engine.setSlotHz(0, hz);
			}
			return;
		}
		for (int c = 0; c < kVoices; c++) {
			const bool high = !toArrangement && c < nGate && inputs[GATE_INPUT].getVoltage(c) >= 1.f;
			if (high && !gateOn[c]) {
				voiceHz[c] = hzOf(c);
				engine.noteOn(c, voiceHz[c], 0.8f);
			}
			else if (!high && gateOn[c]) {
				engine.noteOff(c);
			}
			else if (high) {
				const float hz = hzOf(c);
				if (std::abs(hz - voiceHz[c]) > voiceHz[c] * 1e-4f) {
					voiceHz[c] = hz;
					engine.setSlotHz(c, hz);
				}
			}
			gateOn[c] = high;
		}
	}

	void runBlock(float sr) {
		if (arrSlot.poll())
			lastBlock = -1;
		if (sampleSlot.poll()) {
			engine.setSample(sampleSlot.active->sp);
			hasSample = (bool) sampleSlot.active->sp;
		}
		const ArrPack* pack = arrSlot.active;

		buildSynthScene();
		for (int c = 0; c < qn::kLanes; c++) {
			player.mix[c].gain = ch[c].mute ? 0.f : ch[c].level * 0.0125f * params[ARR_LVL_PARAM].getValue();
			player.mix[c].tune = ch[c].tune;
			player.scenes[c].samplePos = synth.samplePos;
		}
		player.motion = params[MOTION_PARAM].getValue();

		double ppq = 0.0, bpm = 120.0;
		const bool running = blockTransport(sr, ppq, bpm);

		const bool noteMode = arrOn && playMode == 1;
		bool anyGate = false;
		const int nGate = inputs[GATE_INPUT].isConnected() ? std::max(1, inputs[GATE_INPUT].getChannels()) : 0;
		for (int c = 0; c < nGate; c++)
			anyGate |= inputs[GATE_INPUT].getVoltage(c) >= 1.f;
		updateVoices(noteMode);
		const bool active = arrOn && pack && !pack->arr.blocks.empty() && (noteMode ? anyGate : running);
		const float transpose = noteMode && anyGate ? std::round(12.f * inputs[VOCT_INPUT].getVoltage()) : 0.f;

		// perform pads: FX selects the pad, HOLD / FX gate holds it
		qn::ManualFx manual;
		const int sel = clamp((int) std::round(params[FX_PARAM].getValue()), 0, 10) - 1;
		const bool held = sel >= 0 && (params[HOLD_PARAM].getValue() > 0.f || inputs[FX_GATE_INPUT].getVoltage() >= 1.f);
		if (held) {
			if (!fxHeld || sel != fxPad) {
				fxPad = sel;
				padStart = ppq;
				manual.trigger = true;
			}
			const Pad& pd = kPads[fxPad];
			manual.mode = pd.fx;
			manual.sliceBeats = pd.slice;
			manual.progress = (float) std::clamp((ppq - padStart) / pd.ramp, 0.0, 1.0);
		}
		else {
			fxPad = -1;
		}
		fxHeld = held;

		qn::Engine::Block eb;
		eb.synth = &synth;
		eb.env.a = expoValue(ATK_PARAM);
		eb.env.d = expoValue(DEC_PARAM);
		eb.env.s = params[SUS_PARAM].getValue() * 0.01f;
		eb.env.r = expoValue(REL_PARAM);
		eb.bpm = bpm;
		eb.ppq = ppq;
		eb.outDb = params[OUT_PARAM].getValue();
		eb.sampleRate = sr;
		static const qn::Arrangement empty;
		player.render(engine, outBuf[0], outBuf[1], kBlock, eb, pack ? pack->arr : empty, active, transpose, loop, manual);

		// BLOCK / ENERGY / END outputs
		const int blk = active ? player.playingBlock.load(std::memory_order_relaxed) : -1;
		const double p = player.playingPos.load(std::memory_order_relaxed);
		if (blk >= 0 && blk != lastBlock)
			blkPulse.trigger(1e-3f);
		if (active && lastBlock >= 0 && p + 1e-9 < lastPos)
			eocPulse.trigger(1e-3f);
		if (active && pack && blk < 0 && lastBlock >= 0)
			eocPulse.trigger(1e-3f);   // no loop: the piece ended
		lastBlock = blk;
		lastPos = p;
		energyNow = (pack && blk >= 0 && blk < (int) pack->arr.blocks.size()) ? pack->arr.blocks[blk].energy : 0.f;
	}

	void process(const ProcessArgs& args) override {
		if (typeTrig.process(params[TYPE_PARAM].getValue() > 0.f))
			setType(type + 1);
		if (arrTrig.process(params[ARR_PARAM].getValue() > 0.f)) {
			arrOn = !arrOn;
			if (arrOn && !inputs[CLK_INPUT].isConnected())
				freePpq = 0.0;   // free tempo: the piece starts at the top
		}
		if (regenTrig.process(params[REGEN_PARAM].getValue() > 0.f) | regenInTrig.process(inputs[REGEN_INPUT].getVoltage(), 0.1f, 1.f))
			newSeed();
		processClockEdges(args.sampleTime);

		outputs[OUT_L_OUTPUT].setVoltage(outBuf[0][pos] * kVoltsPerUnit);
		outputs[OUT_R_OUTPUT].setVoltage(outBuf[1][pos] * kVoltsPerUnit);
		if (++pos >= kBlock) {
			pos = 0;
			runBlock(args.sampleRate);
		}
		outputs[BLK_OUTPUT].setVoltage(blkPulse.process(args.sampleTime) ? 10.f : 0.f);
		outputs[EOC_OUTPUT].setVoltage(eocPulse.process(args.sampleTime) ? 10.f : 0.f);
		outputs[NRG_OUTPUT].setVoltage(10.f * energyNow);

		if (ui.tick(args.sampleTime))
			publish();
	}

	void publish() {
		for (int t = 0; t < qn::kNumTypes; t++)
			lights[TYPE_LIGHT + t].setBrightness(type == t ? 1.f : 0.f);
		const bool clkOk = inputs[CLK_INPUT].isConnected() && period > 0.f;
		const int fill = arrOn ? player.activeFill.load(std::memory_order_relaxed) : 0;
		lights[ARR_LIGHT].setBrightness(arrOn ? 1.f : 0.f);
		lights[CLK_LIGHT].setBrightness(clkOk ? 1.f : 0.f);
		lights[SMP_LIGHT].setBrightness(hasSample ? 1.f : 0.f);
		lights[FILL_LIGHT].setBrightness(fill != qn::FillNone || fxPad >= 0 ? 1.f : 0.f);

		Snapshot& s = snap.writeSlot();
		s.type = synth.type;
		s.wave = synth.wave;
		s.window = synth.window;
		s.sync = synth.sync;
		s.density = synth.density;
		s.durMs = synth.durMs;
		s.shape = ShapeMap::value(type, shapeStore[type]);
		s.particles = engine.activeParticles.load(std::memory_order_relaxed);
		s.limGr = engine.limiterGr.load(std::memory_order_relaxed);
		// newest particles first, within the time span
		const double now = engine.clockSec.load(std::memory_order_relaxed);
		const int w0 = engine.eventW.load(std::memory_order_acquire);
		constexpr int N = qn::Engine::kEvents;
		int n = 0;
		for (int k = 1; k <= N && n < kDots; k++) {
			const qn::SpawnEvent& ev = engine.events[(size_t) ((w0 - k + 2 * N) % N)];
			const float age = (float) (now - ev.tSec);
			if (age > kCloudSpan || age < -0.05f || ev.hz <= 0.f)
				break;
			s.dots[n++] = {age, ev.hz, ev.amp, ev.durMs, (uint8_t) ev.type};
		}
		s.nDots = n;
		const ArrPack* pack = arrSlot.active;
		s.hasArr = pack != nullptr;
		if (pack)
			s.bar = pack->bar;
		s.arrOn = arrOn;
		const int blk = player.playingBlock.load(std::memory_order_relaxed);
		s.playing = arrOn && lastBlock >= 0;
		s.block = s.playing ? blk : -1;
		s.playhead = (s.playing && pack && pack->bar.totalBeats > 0.f)
		                 ? (float) (player.playingPos.load(std::memory_order_relaxed) / pack->bar.totalBeats)
		                 : -1.f;
		s.fill = fill;
		s.form = clamp((int) std::round(params[FORM_PARAM].getValue()), 0, qn::kNumForms - 1);
		s.seed = seed.load(std::memory_order_relaxed);
		snap.publish();
	}
};

// ---------------------------------------------------------------------------------------------- screens

namespace {

NVGcolor typeColor(int t) {
	switch (t) {
		case qn::Grain: return semi::theme::cyan();
		case qn::Glisson: return semi::theme::amber();
		case qn::Grainlet: return semi::theme::text();
		case qn::Trainlet: return semi::theme::magenta();
		default: return nvgRGB(0x9a, 0xf2, 0xea);   // pulsar: pale cyan
	}
}

} // namespace

// The particle cloud (the plugin's PARTICLE view): every spawned particle as a bar at its time
// (right = now, 3 s span) and pitch (log, 20 Hz .. 20 kHz), length = its duration, height and
// brightness = amplitude, colour = particle type.
struct CloudScreen : Screen {
	Quanta* module = nullptr;
	Snapshot demo;

	CloudScreen() {
		uint32_t r = 7;
		auto rnd = [&]() { r = r * 1664525u + 1013904223u; return (r >> 8) / 16777216.f; };
		demo.type = qn::Glisson;
		demo.density = 120.f;
		demo.durMs = 40.f;
		demo.shape = 12.f;
		demo.nDots = std::min(kDots, 48);
		for (int i = 0; i < demo.nDots; i++)
			demo.dots[i] = {kCloudSpan * i / demo.nDots, 200.f * std::exp2(4.f * rnd()), 0.1f + 0.3f * rnd(), 20.f + 60.f * rnd(),
			                (uint8_t) (i % 3 == 0 ? qn::Pulsar : qn::Glisson)};
		demo.particles = 9;
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		const float pad = 3.f, head = 11.f;
		const float x0 = pad, x1 = w - pad, y0 = pad + head, y1 = h - pad;
		auto Y = [&](float hz) {
			const float t = std::log2(clamp(hz, 20.f, 20000.f) / 20.f) / std::log2(1000.f);
			return y1 - t * (y1 - y0);
		};
		for (float hz : {100.f, 1000.f, 10000.f}) {
			const float y = Y(hz);
			line(vg, x0, y, x1, y, semi::theme::grid());
			text(vg, x0 + 1.f, y - 1.f, hz >= 1000.f ? string::f("%.0fK", hz / 1000.f) : "100", 7.f, semi::theme::dim(),
			     NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
		}
		const float barH = std::max(2.f, semi::platform::kStroke);
		for (int i = s.nDots - 1; i >= 0; i--) {
			const Dot& d = s.dots[i];
			const float x = x1 - d.age / kCloudSpan * (x1 - x0);
			const float len = std::max(1.5f, d.durMs * 0.001f / kCloudSpan * (x1 - x0));
			const float y = Y(d.hz);
			const float a = clamp(d.amp * 4.f, 0.f, 1.f);
			NVGcolor c = typeColor(d.type);
			// pre-blended toward the glass (no alpha on MetaModule)
			const float k = 0.35f + 0.65f * a * (1.f - 0.6f * d.age / kCloudSpan);
			c = nvgRGBf(c.r * k, c.g * k, c.b * k);
			const float hh = barH * (0.7f + 0.8f * a);
			fillRect(vg, x, y - hh * 0.5f, std::min(len, x1 - x), hh, c);
		}
		static const char* syncs[] = {"FREE", "1/4", "1/8", "1/16", "1/32", "1/64", "1/4T", "1/8T", "1/16T", "1/32T"};
		std::string rate = s.sync > 0 ? syncs[clamp(s.sync, 0, 9)] : string::f("%.0f/s", s.density);
		std::string hdr = kTypeNames[clamp(s.type, 0, 4)] + " " + kWaveNames[clamp(s.wave, 0, 4)] + " " + rate;
		text(vg, pad, pad - 1.f, hdr, 8.f, typeColor(s.type));
		std::string right = string::f("%s %.*f  P%d", ShapeMap::tag(s.type), s.type == qn::Grainlet || s.type == qn::Trainlet ? 1 : 0,
		                              s.shape, s.particles);
		text(vg, w - pad, pad - 1.f, right, 8.f, s.limGr < 0.98f ? semi::theme::magenta() : semi::theme::text(),
		     NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
	}
};

// The piece as a barcode (the plugin's PERFORM barcode): 8 channel rows x time, a lit cell =
// the channel plays in that block (brighter = more energy), block edges as gaps, fills as
// magenta ticks under the rows, the playhead in amber.
struct BarcodeScreen : Screen {
	Quanta* module = nullptr;
	Snapshot demo;

	BarcodeScreen() {
		qn::GenParams g;
		g.lengthBars = 64;
		ArrPack p;
		p.arr = qn::generate(g);
		buildBarcode(p);
		demo.bar = p.bar;
		demo.hasArr = true;
		demo.arrOn = true;
		demo.playing = true;
		demo.playhead = 0.37f;
		demo.block = 5;
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		const float pad = 3.f, head = 11.f, foot = 3.f;
		const float x0 = pad, x1 = w - pad, y0 = pad + head, y1 = h - pad - foot;
		const float rowH = (y1 - y0) / qn::kLanes;
		const float colW = (x1 - x0) / kCols;
		if (s.hasArr) {
			for (int c = 0; c < kCols; c++) {
				const float x = x0 + c * colW;
				const bool edge = (s.bar.flags[c] & 2) && c > 0;
				const float cx = edge ? x + std::min(1.f, colW * 0.5f) : x;
				const float cw = x + colW - cx;
				const float e = s.bar.energy[c] / 255.f;
				const float k = (s.arrOn ? 0.45f + 0.55f * e : 0.3f);
				for (int l = 0; l < qn::kLanes; l++) {
					if (!((s.bar.mask[c] >> l) & 1))
						continue;
					NVGcolor col = l == 0 || l == 1 ? semi::theme::magenta() : semi::theme::cyan();
					col = nvgRGBf(col.r * k, col.g * k, col.b * k);
					fillRect(vg, cx, y0 + l * rowH + 0.5f, cw, rowH - 1.f, col);
				}
				if (s.bar.flags[c] & 1)
					fillRect(vg, cx, y1 + 1.f, cw, foot - 0.5f, semi::theme::magenta());
			}
			if (s.playhead >= 0.f) {
				const float x = x0 + clamp(s.playhead, 0.f, 1.f) * (x1 - x0);
				line(vg, x, y0 - 1.f, x, y1 + foot, semi::theme::amber(), semi::platform::kStroke);
			}
		}
		text(vg, pad, pad - 1.f, kFormNames[clamp(s.form, 0, 5)], 8.f, s.arrOn ? semi::theme::text() : semi::theme::dim());
		std::string right;
		if (!s.arrOn)
			right = "ARR OFF";
		else if (s.fill != qn::FillNone)
			right = qn::fillName(s.fill);
		else if (s.block >= 0)
			right = string::f("%d/%d", s.block + 1, s.bar.blocks);
		else
			right = string::f("S%d", s.seed);
		text(vg, w - pad, pad - 1.f, right, 8.f, s.fill != qn::FillNone && s.arrOn ? semi::theme::magenta() : semi::theme::amber(),
		     NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
	}
};

// ---------------------------------------------------------------------------------------------- menus

namespace {

template <typename T>
void addIndex(Menu* menu, std::string label, const std::vector<std::string>& labels, T* v) {
	menu->addChild(createIndexSubmenuItem(label, labels,
		[=]() { return (size_t) *v; },
		[=](size_t i) { *v = (T) i; }));
}

void addSteps(Menu* menu, std::string label, float* v, float lo, float hi, int steps, const char* fmt) {
	std::vector<std::string> labels;
	for (int i = 0; i < steps; i++)
		labels.push_back(string::f(fmt, lo + (hi - lo) * i / (steps - 1)));
	menu->addChild(createIndexSubmenuItem(label, labels,
		[=]() { return (size_t) clamp((int) std::round((*v - lo) / (hi - lo) * (steps - 1)), 0, steps - 1); },
		[=](size_t i) { *v = lo + (hi - lo) * i / (steps - 1); }));
}

template <typename T, size_t N>
std::vector<std::string> numberLabels(const T (&a)[N], const char* fmt) {
	std::vector<std::string> v;
	for (size_t i = 0; i < N; i++)
		v.push_back(string::f(fmt, a[i]));
	return v;
}

} // namespace

struct QuantaWidget : ModuleWidget {
	QuantaWidget(Quanta* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Quanta.svg")));

		auto* cloud = semi::createScreenMm<CloudScreen>(Vec(L::CLOUD.x, L::CLOUD.y), Vec(L::CLOUD.w, L::CLOUD.h));
		cloud->module = module;
		addChild(cloud);
		auto* bar = semi::createScreenMm<BarcodeScreen>(Vec(L::BARCODE.x, L::BARCODE.y), Vec(L::BARCODE.w, L::BARCODE.h));
		bar->module = module;
		addChild(bar);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		const L::P types[5] = {L::T_GRAIN, L::T_GLIS, L::T_GLET, L::T_TRAIN, L::T_PULS};
		for (int i = 0; i < 5; i++)
			addChild(createLightCentered<semi::CyanLedSmall>(at(types[i]), module, Quanta::TYPE_LIGHT + i));
		addParam(createParamCentered<semi::Button>(at(L::TYPE), module, Quanta::TYPE_PARAM));
		addChild(createLightCentered<semi::AmberLedSmall>(at(L::ARR_LED), module, Quanta::ARR_LIGHT));
		addParam(createParamCentered<semi::Button>(at(L::ARR), module, Quanta::ARR_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::REGEN), module, Quanta::REGEN_PARAM));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::CLK_LED), module, Quanta::CLK_LIGHT));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::SMP_LED), module, Quanta::SMP_LIGHT));
		addChild(createLightCentered<semi::MagentaLedSmall>(at(L::FILL_LED), module, Quanta::FILL_LIGHT));

		const L::P rowA[8] = {L::DENSITY, L::DUR, L::SHAPE, L::MASK, L::JITTER, L::SPREAD, L::TUNE, L::WAVE};
		const L::P rowB[8] = {L::FORM, L::EXTREM, L::SILENCE, L::FILLS, L::MOTION, L::ARR_LVL, L::FX, L::OUT};
		for (int i = 0; i < 8; i++) {
			const int idA = Quanta::DENSITY_PARAM + i, idB = Quanta::FORM_PARAM + i;
			ParamWidget* a = (idA == Quanta::JITTER_PARAM) ? (ParamWidget*) createParamCentered<semi::KnobMHot>(at(rowA[i]), module, idA)
			                                               : (ParamWidget*) createParamCentered<semi::KnobM>(at(rowA[i]), module, idA);
			ParamWidget* b = (idB == Quanta::EXTREM_PARAM || idB == Quanta::FX_PARAM)
			                     ? (ParamWidget*) createParamCentered<semi::KnobMHot>(at(rowB[i]), module, idB)
			                     : (ParamWidget*) createParamCentered<semi::KnobM>(at(rowB[i]), module, idB);
			if (idA == Quanta::WAVE_PARAM)
				static_cast<Knob*>(a)->snap = true;
			if (idB == Quanta::FORM_PARAM || idB == Quanta::FX_PARAM)
				static_cast<Knob*>(b)->snap = true;
			addParam(a);
			addParam(b);
		}
		const L::P rowC[6] = {L::SYNC, L::WINDOW, L::ATK, L::DEC, L::SUS, L::REL};
		for (int i = 0; i < 6; i++) {
			auto* k = createParamCentered<semi::KnobS>(at(rowC[i]), module, Quanta::SYNC_PARAM + i);
			if (i < 2)
				k->snap = true;
			addParam(k);
		}
		addParam(createParamCentered<semi::PadMagenta>(at(L::HOLD), module, Quanta::HOLD_PARAM));

		const L::P ins[8] = {L::VOCT, L::GATE, L::CLK, L::RST, L::DENS_CV, L::DUR_CV, L::FX_GATE, L::REGEN_IN};
		for (int i = 0; i < 8; i++)
			addInput(createInputCentered<semi::Jack>(at(ins[i]), module, Quanta::VOCT_INPUT + i));
		const L::P outs[5] = {L::OUT_L, L::OUT_R, L::BLK, L::NRG, L::EOC};
		for (int i = 0; i < 5; i++)
			addOutput(createOutputCentered<semi::Jack>(at(outs[i]), module, Quanta::OUT_L_OUTPUT + i));
	}

#ifndef METAMODULE
	void step() override {
		if (auto* m = dynamic_cast<Quanta*>(module))
			m->serviceWorker();
		ModuleWidget::step();
	}
#endif

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Quanta*>(module);
		if (!m)
			return;

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel(string::f("Seed %d", m->seed.load())));
		menu->addChild(createMenuItem("Regenerate (new seed)", "", [=]() { m->newSeed(); }));

		menu->addChild(createSubmenuItem("Arrangement", "", [=](Menu* sub) {
			addIndex(sub, "Length", numberLabels(kLengths, "%d bars"), &m->lengthIdx);
			addIndex(sub, "Block size", numberLabels(kBlockSizes, "%d bars"), &m->blockIdx);
			sub->addChild(createBoolPtrMenuItem("Power-of-two blocks", "", &m->pow2));
			addIndex(sub, "Mutation", numberLabels(kMutations, "%d bits"), &m->mutIdx);
			addIndex(sub, "Cut quantize", std::vector<std::string>{"STEP", "BEAT", "BAR"}, &m->cutq);
			addSteps(sub, "Fill FX mix", &m->fxMix, 0.f, 100.f, 11, "%.0f %%");
			sub->addChild(createBoolPtrMenuItem("Loop", "", &m->loop));
			addIndex(sub, "Play mode", std::vector<std::string>{"AUTO (clock)", "GATE + V/OCT (play / transpose)"}, &m->playMode);
		}));

		menu->addChild(createSubmenuItem("Channels", "", [=](Menu* sub) {
			for (int c = 0; c < qn::kLanes; c++) {
				const auto d = qn::defaultChannels()[c];
				std::string name = (m->ch[c].role == d.role && m->ch[c].reg == d.reg)
				                       ? qn::defaultChannelName(c)
				                       : std::string(qn::blockTypeName(m->ch[c].role)) + " " + qn::regName(m->ch[c].reg);
				sub->addChild(createSubmenuItem(string::f("%d %s", c + 1, name.c_str()), m->ch[c].mute ? "muted" : "", [=](Menu* cm) {
					cm->addChild(createBoolPtrMenuItem("Mute", "", &m->ch[c].mute));
					addSteps(cm, "Level", &m->ch[c].level, 0.f, 100.f, 11, "%.0f %%");
					addSteps(cm, "Tune", &m->ch[c].tune, -24.f, 24.f, 17, "%+.0f st");
					cm->addChild(createIndexSubmenuItem("Role", kRoleNames,
						[=]() { return (size_t) m->ch[c].role; },
						[=](size_t i) { m->ch[c].role = (int) i; m->resetChannelSound(c); }));
					cm->addChild(createIndexSubmenuItem("Register", kRegNames,
						[=]() { return (size_t) m->ch[c].reg; },
						[=](size_t i) { m->ch[c].reg = (int) i; m->resetChannelSound(c); }));
					cm->addChild(createMenuItem("Copy the voice's sound here", "", [=]() { m->copyVoiceToChannel(c); }));
					cm->addChild(createMenuItem("Reset sound", "", [=]() { m->resetChannelSound(c); }));
				}));
			}
		}));

		menu->addChild(createSubmenuItem("Voice", "", [=](Menu* sub) {
			sub->addChild(createBoolPtrMenuItem("Free-running without GATE", "", &m->freeRun));
			addIndex(sub, "Glide direction (GLISSON)", std::vector<std::string>{"UP", "DOWN", "RANDOM"}, &m->glideDir);
			addSteps(sub, "Chroma (TRAINLET)", &m->chroma, 0.f, 100.f, 11, "%.0f %%");
		}));

		menu->addChild(createSubmenuItem("Clock", "", [=](Menu* sub) {
			addIndex(sub, "CLK input PPQN", kPpqnNames, &m->ppqnIdx);
			addIndex(sub, "Tempo (CLK unpatched)", numberLabels(kTempos, "%.0f BPM"), &m->tempoIdx);
		}));

		menu->addChild(createSubmenuItem("Sample (WAVE = SAMPLE)", "", [=](Menu* sub) {
			const std::string cur = m->hasSample ? semi::fileName(m->loader.lastPath) : std::string("none");
			sub->addChild(createMenuLabel("Loaded: " + cur));
			sub->addChild(createMenuItem("Load WAV...", "", [=]() {
				semi::openFileDialog("WAV:wav,WAV", semi::dirName(m->loader.lastPath), [=](const std::string& path) { m->loader.request(path); });
			}));
			sub->addChild(createMenuItem("Clear", "", [=]() { m->clearSample(); }));
		}));
	}
};

Model* modelQuanta = createModel<Quanta, QuantaWidget>("Quanta");
