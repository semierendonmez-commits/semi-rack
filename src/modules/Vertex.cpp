// VERTEX — Rack / MetaModule port of VERTEX, the plugin rebuild of Vertex 1.3 by aorist
// (aorist.io; a Max for Live instrument in the spirit of a Drummer From Another Mother).
// Engine: engines/vertex, unchanged from semierendonmez-commits/vertex Source/engine; the filter
// is the plugin's port of sm.sallenkey~ (Pete Dowling & Matt Jackson, Surreal Machines).
// The device carries no licence text: ask the author before any public release.
//
// Voice: two oscillators, noise, external IN, Sallen & Key filter, three envelopes, played by four
// 8-step rows (PITCH, VEL, MOD 1, MOD 2). The 2 x 8 step knobs edit PITCH + VEL or MOD 1 + MOD 2
// (ROWS button); the screen shows all four rows.
//
// Clock (no host transport in Rack):
//   * CLK patched: 1/16 notes by default (menu: PPQN). The first edge after RST is step 1.
//     The master clock menu (2n..128n) divides it as in the device; the rows step from it.
//   * CLK not patched: internal clock at TEMPO. RUN starts / stops (stopped = back to step 1).
//   * TRIG: every rising edge is a note-on (pitch from V/OCT, velocity from VEL). It always sets
//     the note / MIDI velocity; with Master clock = EXT it is also the trigger (the device's EXT).
// Rack audio is +-5 V; 5 V = 1.0 for the engine. OUT is mono, as the device.
//
// Port choices (menu-held, saved in the patch): the plugin's LFOs, evolve and themes are not
// ported (Rack has its own modulation); V/OCT follow per oscillator defaults ON (neutral while
// V/OCT is unpatched), external input level defaults to 100 % (neutral while IN is unpatched).

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/VertexLayout.hpp"
#include "vertex/Engine.h"
#include "vertex/Factory.h"

namespace L = semi::layout::vertex;
using semi::Screen;

namespace {

constexpr int kBlock = 16;           // engine block; adds 16 samples of latency
constexpr float kVoltsPerUnit = 5.f;
constexpr int kScopePts = 64;

const std::vector<std::string> kShapeNames = {"TRI", "SQU", "SIN", "SAW"};
const std::vector<std::string> kSrcNames = {"NONE", "MOD 1", "MOD 2", "VEL", "PITCH", "MIDI VEL (VEL in)"};
const std::vector<std::string> kClockNames = {"2n", "4n", "8n", "16n", "32n", "64n", "128n", "EXT (TRIG)"};
const std::vector<std::string> kRateNames = {"x 1/2n", "x 1/4n", "x 1/8n", "x 1/16n (same)", "x 1/32n", "x 1/64n", "x 1/128n"};
const std::vector<std::string> kRowNames = {"PITCH", "VEL", "MOD 1", "MOD 2"};
const std::vector<std::string> kPpqnNames = {"1 (quarter notes)", "2 (8ths)", "4 (16ths)", "8 (32nds)", "24 (DIN sync)"};
constexpr int kPpqn[] = {1, 2, 4, 8, 24};
const std::vector<std::string> kNoteNames = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// knob position <-> value on the device's power curves
inline float expoToVal(float n, float lo, float hi, float ex) { return lo + (hi - lo) * std::pow(clamp(n, 0.f, 1.f), ex); }
inline float valToExpo(float v, float lo, float hi, float ex) { return std::pow(clamp((v - lo) / (hi - lo), 0.f, 1.f), 1.f / ex); }

struct ExpoQuantity : ParamQuantity {
	float lo = 0.f, hi = 1.f, ex = 1.f;
	float getDisplayValue() override { return expoToVal(getValue(), lo, hi, ex); }
	void setDisplayValue(float d) override { setValue(valToExpo(d, lo, hi, ex)); }
};

struct CutoffQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		const float hz = 440.f * std::exp2((getValue() - 69.f) / 12.f);
		return hz >= 1000.f ? string::f("%.2f kHz (%.0f)", hz / 1000.f, getValue()) : string::f("%.0f Hz (%.0f)", hz, getValue());
	}
};

struct Snapshot {
	float steps[vx::numSeq][8] = {};
	int len[vx::numSeq] = {8, 8, 8, 8};
	int cur[vx::numSeq] = {};
	int page = 0;
	bool running = false;
	int shape[2] = {1, 1};
	float freq[2] = {130.8f, 130.8f};
	float envA = 0.f, envP = 0.f, envF = 0.f, cutoff = 140.f;
	float scope[kScopePts] = {};
	float peak = 0.f;
};

} // namespace

struct Vertex : Module {
	enum ParamId {
		O1_FREQ_PARAM, O1_EG_PARAM, O1_LVL_PARAM, NOISE_PARAM, O2_FREQ_PARAM, O2_EG_PARAM, O2_LVL_PARAM, FM_PARAM,
		ATTACK_PARAM, P_DECAY_PARAM, A_DECAY_PARAM, VOLUME_PARAM, CUTOFF_PARAM, RES_PARAM, F_ENV_PARAM, F_DECAY_PARAM,
		ENUMS(STEP_A_PARAM, 8), ENUMS(STEP_B_PARAM, 8),
		TEMPO_PARAM, O1_SHAPE_PARAM, O2_SHAPE_PARAM, PAGE_PARAM, RUN_PARAM,
		PARAMS_LEN
	};
	enum InputId { CLK_INPUT, RST_INPUT, TRIG_INPUT, VOCT_INPUT, VEL_INPUT, IN_INPUT, VCF_CV_INPUT, FM_CV_INPUT, INPUTS_LEN };
	enum OutputId { OUT_OUTPUT, TRIG_OUTPUT, PITCH_OUTPUT, VEL_OUTPUT, AMP_EG_OUTPUT, FLT_EG_OUTPUT, OUTPUTS_LEN };
	enum LightId {
		ENUMS(O1_SHAPE_LIGHT, 4), ENUMS(O2_SHAPE_LIGHT, 4), PAGE_PV_LIGHT, PAGE_M_LIGHT, RUN_LIGHT, ENUMS(STEP_LIGHT, 8),
		LIGHTS_LEN
	};

	vx::Engine engine;
	vx::Params cfg = vx::initParams();   // everything that is not a panel knob lives here (+ the 4 rows)
	int page = 0;                        // 0: step knobs = PITCH + VEL, 1: MOD 1 + MOD 2
	bool runFlag = true;
	bool vocFollow[2] = {true, true};
	float outDb = 0.f;
	bool softClip = true;
	int ppqnIdx = 2;
	int preset = 0;

	dsp::BooleanTrigger shapeTrig[2], pageTrig, runTrig;
	dsp::SchmittTrigger clkTrig, rstTrig, trigTrig;
	dsp::PulseGenerator trigPulse;

	// clock
	int clkCount = -1;           // CLK edges since reset (-1 = waiting for the first)
	float sinceClk = 0.f, period = 0.f;
	bool clkSeen = false;
	double freePpq = 0.0;
	bool pendingRst = false;

	// block adapter
	float inBuf[kBlock]{}, outBuf[kBlock]{};
	vx::MidiEvt events[kBlock * 2];
	int numEvents = 0;
	int pos = 0;
	int lastNote = 60;
	uint32_t lastTriggers = 0;
	float peak = 0.f;

	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;

	Vertex() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		auto expo = [&](int id, const char* name, float lo, float hi, float def, float ex, const char* unit) {
			auto* q = configParam<ExpoQuantity>(id, 0.f, 1.f, valToExpo(def, lo, hi, ex), name, unit);
			q->lo = lo;
			q->hi = hi;
			q->ex = ex;
		};
		expo(O1_FREQ_PARAM, "Osc 1 frequency", 5.f, 6200.f, 130.8f, 4.5f, " Hz");
		configParam(O1_EG_PARAM, -600.f, 600.f, 0.f, "Osc 1 pitch envelope", " Hz");
		configParam(O1_LVL_PARAM, 0.f, 1.f, 0.f, "Osc 1 level", " %", 0.f, 100.f);
		configParam(NOISE_PARAM, 0.f, 1.f, 0.f, "Noise level", " %", 0.f, 100.f);
		expo(O2_FREQ_PARAM, "Osc 2 frequency", 5.f, 6200.f, 130.8f, 4.5f, " Hz");
		configParam(O2_EG_PARAM, -600.f, 600.f, 0.f, "Osc 2 pitch envelope", " Hz");
		configParam(O2_LVL_PARAM, 0.f, 1.f, 1.f, "Osc 2 level", " %", 0.f, 100.f);
		configParam(FM_PARAM, 0.f, 600.f, 0.f, "Osc 1 -> Osc 2 FM", " Hz");
		expo(ATTACK_PARAM, "Attack (all envelopes)", 0.f, 2000.f, 0.f, 1.75f, " ms");
		expo(P_DECAY_PARAM, "Pitch envelope decay", 18.f, 2000.f, 150.f, 1.75f, " ms");
		expo(A_DECAY_PARAM, "Amp envelope decay", 18.f, 2000.f, 150.f, 1.75f, " ms");
		configParam(VOLUME_PARAM, 0.f, 2.f, 1.f, "Volume", " %", 0.f, 100.f);
		configParam<CutoffQuantity>(CUTOFF_PARAM, 0.f, 140.f, 140.f, "Cutoff");
		configParam(RES_PARAM, 0.f, 1.f, 0.3f, "Resonance", " %", 0.f, 100.f);
		configParam(F_ENV_PARAM, -150.f, 150.f, 0.f, "Filter envelope", " st");
		expo(F_DECAY_PARAM, "Filter envelope decay", 18.f, 2000.f, 150.f, 1.75f, " ms");
		for (int i = 0; i < 8; i++) {
			configParam(STEP_A_PARAM + i, 0.f, 1.f, 0.5f, string::f("Row A (PITCH / MOD 1) step %d", i + 1), " %", 0.f, 100.f);
			configParam(STEP_B_PARAM + i, 0.f, 1.f, 1.f, string::f("Row B (VEL / MOD 2) step %d", i + 1), " %", 0.f, 100.f);
		}
		configParam(TEMPO_PARAM, 40.f, 240.f, 120.f, "Tempo (CLK unpatched)", " BPM");
		configButton(O1_SHAPE_PARAM, "Osc 1 shape (TRI / SQU / SIN / SAW)");
		configButton(O2_SHAPE_PARAM, "Osc 2 shape (TRI / SQU / SIN / SAW)");
		configButton(PAGE_PARAM, "Step knobs edit PITCH + VEL / MOD 1 + MOD 2");
		configButton(RUN_PARAM, "Run / stop");
		configInput(CLK_INPUT, "Clock (1/16 notes by default)");
		configInput(RST_INPUT, "Reset (next clock = step 1)");
		configInput(TRIG_INPUT, "Trigger (note-on; the trigger with Master clock = EXT)");
		configInput(VOCT_INPUT, "V/OCT (0 V = C4, sampled at TRIG or when it changes)");
		configInput(VEL_INPUT, "Velocity (0-10 V, the M mod source)");
		configInput(IN_INPUT, "External audio (mix / FM / filter mod)");
		configInput(VCF_CV_INPUT, "Cutoff CV (12 st/V)");
		configInput(FM_CV_INPUT, "FM CV (60 Hz/V)");
		configOutput(OUT_OUTPUT, "Audio");
		configOutput(TRIG_OUTPUT, "Trigger");
		configOutput(PITCH_OUTPUT, "PITCH row (0-10 V)");
		configOutput(VEL_OUTPUT, "VEL row (0-10 V)");
		configOutput(AMP_EG_OUTPUT, "Amp envelope (0-10 V)");
		configOutput(FLT_EG_OUTPUT, "Filter envelope (0-10 V)");
		cfg.extLevel = 1.0;
		engine.prepare(44100.0);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		engine.prepare(e.sampleRate);
		pos = 0;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		cfg = vx::initParams();
		cfg.extLevel = 1.0;
		page = 0;
		runFlag = true;
		vocFollow[0] = vocFollow[1] = true;
		outDb = 0.f;
		softClip = true;
		ppqnIdx = 2;
		preset = 0;
		clkCount = -1;
		freePpq = 0.0;
		engine.reset();
	}

	// ---- step knobs <-> rows -----------------------------------------------------------------------
	int rowA() const { return page == 0 ? vx::seqPitch : vx::seqMod1; }
	int rowB() const { return page == 0 ? vx::seqVel : vx::seqMod2; }

	void pullKnobs() {
		for (int i = 0; i < 8; i++) {
			cfg.step[rowA()][i] = params[STEP_A_PARAM + i].getValue();
			cfg.step[rowB()][i] = params[STEP_B_PARAM + i].getValue();
		}
	}
	void pushKnobs() {
		for (int i = 0; i < 8; i++) {
			params[STEP_A_PARAM + i].setValue((float) cfg.step[rowA()][i]);
			params[STEP_B_PARAM + i].setValue((float) cfg.step[rowB()][i]);
		}
	}
	void setPage(int p) {
		pullKnobs();
		page = p & 1;
		pushKnobs();
	}

	// ---- presets -------------------------------------------------------------------------------------
	void applyPreset(int i) {
		preset = clamp(i, 0, vx::numFactory() - 1);
		const vx::FactoryPreset& f = vx::factory(preset);
		cfg = f.params;
		cfg.extLevel = 1.0;
		auto setExpo = [&](int id, float v) {
			auto* q = dynamic_cast<ExpoQuantity*>(paramQuantities[id]);
			params[id].setValue(q ? valToExpo(v, q->lo, q->hi, q->ex) : v);
		};
		setExpo(O1_FREQ_PARAM, (float) cfg.osc[0].freq);
		setExpo(O2_FREQ_PARAM, (float) cfg.osc[1].freq);
		params[O1_EG_PARAM].setValue((float) cfg.osc[0].env);
		params[O2_EG_PARAM].setValue((float) cfg.osc[1].env);
		params[O1_LVL_PARAM].setValue((float) cfg.osc[0].level);
		params[O2_LVL_PARAM].setValue((float) cfg.osc[1].level);
		params[NOISE_PARAM].setValue((float) cfg.noise);
		params[FM_PARAM].setValue((float) cfg.fm);
		setExpo(ATTACK_PARAM, (float) cfg.attack);
		setExpo(P_DECAY_PARAM, (float) cfg.pDecay);
		setExpo(A_DECAY_PARAM, (float) cfg.aDecay);
		params[VOLUME_PARAM].setValue((float) cfg.volume);
		params[CUTOFF_PARAM].setValue((float) cfg.cutoff);
		params[RES_PARAM].setValue((float) cfg.res);
		params[F_ENV_PARAM].setValue((float) cfg.fEnv);
		setExpo(F_DECAY_PARAM, (float) cfg.fDecay);
		params[TEMPO_PARAM].setValue((float) std::clamp(f.bpm, 40.0, 240.0));
		pushKnobs();
	}

	void randomizeRows(bool mods) {
		pullKnobs();
		const int r0 = mods ? vx::seqMod1 : vx::seqPitch;
		for (int r = r0; r < r0 + 2; r++)
			for (int i = 0; i < 8; i++)
				cfg.step[r][i] = random::uniform();
		pushKnobs();
	}

	// ---- JSON ------------------------------------------------------------------------------------------
	json_t* dataToJson() override {
		pullKnobs();
		json_t* root = json_object();
		json_t* rows = json_array();
		for (int r = 0; r < vx::numSeq; r++)
			for (int i = 0; i < 8; i++)
				json_array_append_new(rows, json_real(cfg.step[r][i]));
		json_object_set_new(root, "rows", rows);
		json_t* lens = json_array();
		json_t* rates = json_array();
		for (int r = 0; r < vx::numSeq; r++) {
			json_array_append_new(lens, json_integer(cfg.length[r]));
			json_array_append_new(rates, json_integer(cfg.rate[r]));
		}
		json_object_set_new(root, "lengths", lens);
		json_object_set_new(root, "rates", rates);
		auto seti = [&](const char* k, int v) { json_object_set_new(root, k, json_integer(v)); };
		auto setr = [&](const char* k, double v) { json_object_set_new(root, k, json_real(v)); };
		seti("page", page);
		seti("run", runFlag);
		seti("preset", preset);
		seti("ppqn", ppqnIdx);
		seti("clock", cfg.clock);
		setr("swing", cfg.swing);
		setr("prange", cfg.pitchRange);
		char key[24];
		for (int o = 0; o < 2; o++) {
			const vx::OscParams& op = cfg.osc[o];
			std::snprintf(key, sizeof key, "o%d_shape", o);
			seti(key, op.shape);
			std::snprintf(key, sizeof key, "o%d_lvlsrc", o);
			seti(key, op.levelSrc);
			std::snprintf(key, sizeof key, "o%d_envsrc", o);
			seti(key, op.envSrc);
			std::snprintf(key, sizeof key, "o%d_int", o);
			seti(key, op.intPitch);
			std::snprintf(key, sizeof key, "o%d_voct", o);
			seti(key, vocFollow[o]);
			std::snprintf(key, sizeof key, "o%d_quant", o);
			seti(key, op.quantize);
			std::snprintf(key, sizeof key, "o%d_glide", o);
			seti(key, op.glide);
			std::snprintf(key, sizeof key, "o%d_fine", o);
			setr(key, op.fine);
		}
		seti("fmSrc", cfg.fmSrc);
		seti("fmFrom", cfg.fmFrom);
		setr("glide", cfg.glide);
		seti("glideSrc", cfg.glideSrc);
		seti("sync", cfg.sync);
		seti("syncMode", cfg.syncMode);
		seti("root", cfg.root);
		seti("scale", cfg.scale);
		seti("ftype", cfg.filterType);
		seti("cutoffSrc", cfg.cutoffSrc);
		seti("resSrc", cfg.resSrc);
		seti("fDecaySrc", cfg.fDecaySrc);
		seti("fEnvSrc", cfg.fEnvSrc);
		setr("fMod", cfg.fMod);
		seti("fModSrc", cfg.fModSrc);
		seti("fModFrom", cfg.fModFrom);
		seti("slope", cfg.slope);
		seti("order", cfg.order);
		setr("drive", cfg.drive);
		seti("aDecaySrc", cfg.aDecaySrc);
		seti("pDecaySrc", cfg.pDecaySrc);
		seti("attackSrc", cfg.attackSrc);
		setr("curve", cfg.curve);
		seti("volumeSrc", cfg.volumeSrc);
		seti("noiseSrc", cfg.noiseSrc);
		seti("noiseColour", cfg.noiseColour);
		setr("extLevel", cfg.extLevel);
		setr("outDb", outDb);
		seti("softClip", softClip);
		return root;
	}

	void dataFromJson(json_t* root) override {
		if (json_t* rows = json_object_get(root, "rows"))
			for (int r = 0; r < vx::numSeq; r++)
				for (int i = 0; i < 8; i++)
					if (json_t* v = json_array_get(rows, r * 8 + i))
						cfg.step[r][i] = clamp((float) json_number_value(v), 0.f, 1.f);
		json_t* lens = json_object_get(root, "lengths");
		json_t* rates = json_object_get(root, "rates");
		for (int r = 0; r < vx::numSeq; r++) {
			if (json_t* v = lens ? json_array_get(lens, r) : nullptr)
				cfg.length[r] = clamp((int) json_integer_value(v), 1, 8);
			if (json_t* v = rates ? json_array_get(rates, r) : nullptr)
				cfg.rate[r] = clamp((int) json_integer_value(v), 0, vx::kNumRates - 1);
		}
		auto geti = [&](const char* k, int lo, int hi, int def) {
			json_t* j = json_object_get(root, k);
			return j ? clamp((int) json_integer_value(j), lo, hi) : def;
		};
		auto getr = [&](const char* k, double lo, double hi, double def) {
			json_t* j = json_object_get(root, k);
			return j ? std::clamp(json_number_value(j), lo, hi) : def;
		};
		page = geti("page", 0, 1, 0);
		runFlag = geti("run", 0, 1, 1);
		preset = geti("preset", 0, vx::numFactory() - 1, 0);
		ppqnIdx = geti("ppqn", 0, 4, 2);
		cfg.clock = geti("clock", 0, vx::kNumRates, 3);
		cfg.swing = getr("swing", 0.0, 0.5, 0.0);
		cfg.pitchRange = getr("prange", 0.0, 1.0, 0.5);
		char key[24];
		for (int o = 0; o < 2; o++) {
			vx::OscParams& op = cfg.osc[o];
			std::snprintf(key, sizeof key, "o%d_shape", o);
			op.shape = geti(key, 0, 3, 1);
			std::snprintf(key, sizeof key, "o%d_lvlsrc", o);
			op.levelSrc = geti(key, 0, vx::numSrc - 1, 0);
			std::snprintf(key, sizeof key, "o%d_envsrc", o);
			op.envSrc = geti(key, 0, vx::numSrc - 1, 0);
			std::snprintf(key, sizeof key, "o%d_int", o);
			op.intPitch = geti(key, 0, 1, o == 1);
			std::snprintf(key, sizeof key, "o%d_voct", o);
			vocFollow[o] = geti(key, 0, 1, 1);
			std::snprintf(key, sizeof key, "o%d_quant", o);
			op.quantize = geti(key, 0, 1, 0);
			std::snprintf(key, sizeof key, "o%d_glide", o);
			op.glide = geti(key, 0, 1, 1);
			std::snprintf(key, sizeof key, "o%d_fine", o);
			op.fine = getr(key, -12.0, 12.0, 0.0);
		}
		cfg.fmSrc = geti("fmSrc", 0, vx::numSrc - 1, 0);
		cfg.fmFrom = geti("fmFrom", 0, 1, 0);
		cfg.glide = getr("glide", 0.0, 1.0, 0.0);
		cfg.glideSrc = geti("glideSrc", 0, vx::numSrc - 1, 0);
		cfg.sync = geti("sync", 0, 1, 0);
		cfg.syncMode = geti("syncMode", 0, 1, 0);
		cfg.root = geti("root", 0, 11, 0);
		cfg.scale = geti("scale", 0, vx::Engine::numScales() - 1, 0);
		cfg.filterType = geti("ftype", 0, 2, 0);
		cfg.cutoffSrc = geti("cutoffSrc", 0, vx::numSrc - 1, 0);
		cfg.resSrc = geti("resSrc", 0, vx::numSrc - 1, 0);
		cfg.fDecaySrc = geti("fDecaySrc", 0, vx::numSrc - 1, 0);
		cfg.fEnvSrc = geti("fEnvSrc", 0, vx::numSrc - 1, vx::srcVel);
		cfg.fMod = getr("fMod", -150.0, 150.0, 0.0);
		cfg.fModSrc = geti("fModSrc", 0, vx::numSrc - 1, 0);
		cfg.fModFrom = geti("fModFrom", 0, 3, 0);
		cfg.slope = geti("slope", 0, 1, 0);
		cfg.order = geti("order", 0, 1, 0);
		cfg.drive = getr("drive", 0.0, 1.0, 0.0);
		cfg.aDecaySrc = geti("aDecaySrc", 0, vx::numSrc - 1, 0);
		cfg.pDecaySrc = geti("pDecaySrc", 0, vx::numSrc - 1, 0);
		cfg.attackSrc = geti("attackSrc", 0, vx::numSrc - 1, 0);
		cfg.curve = getr("curve", -1.0, 0.0, -0.606299);
		cfg.volumeSrc = geti("volumeSrc", 0, vx::numSrc - 1, vx::srcVel);
		cfg.noiseSrc = geti("noiseSrc", 0, vx::numSrc - 1, 0);
		cfg.noiseColour = geti("noiseColour", 0, 1, 0);
		cfg.extLevel = getr("extLevel", 0.0, 1.0, 1.0);
		outDb = (float) getr("outDb", -24.0, 12.0, 0.0);
		softClip = geti("softClip", 0, 1, 1);
		// the step knobs were restored with the params; they show `page`
		pushKnobs();
	}

	// ---- clock --------------------------------------------------------------------------------------------
	// Fills the transport for the next block. Never lets the position run past the next clock
	// edge (that would double-trigger when the edge arrives).
	void blockTransport(float sr, vx::Transport& tr) {
		const bool ext = inputs[CLK_INPUT].isConnected();
		if (!ext) {
			const double bpm = params[TEMPO_PARAM].getValue();
			tr.running = runFlag;
			tr.hostPlaying = runFlag;
			tr.ppq = freePpq;
			tr.ppqPerSample = bpm / 60.0 / sr;
			if (runFlag)
				freePpq += kBlock * tr.ppqPerSample;
			else
				freePpq = 0.0;   // stopped: the next start is a downbeat
			return;
		}
		const int ppqn = kPpqn[ppqnIdx];
		const bool alive = clkCount >= 0 && period > 0.f;
		tr.running = runFlag && alive;
		tr.hostPlaying = tr.running;
		if (!alive) {
			tr.ppq = std::max(0, clkCount) / (double) ppqn;
			tr.ppqPerSample = 0.0;
			return;
		}
		const double beatPerClk = 1.0 / ppqn;
		const double frac = std::min(sinceClk / period, 0.999f);
		tr.ppq = (clkCount + frac) * beatPerClk;
		const double rate = beatPerClk / period / sr;
		const double room = (clkCount + 0.9995) * beatPerClk - tr.ppq;
		tr.ppqPerSample = std::max(0.0, std::min(rate, room / kBlock));
	}

	void processClockEdges(float dt) {
		sinceClk += dt;
		if (rstTrig.process(inputs[RST_INPUT].getVoltage(), 0.1f, 1.f)) {
			clkCount = -1;
			freePpq = 0.0;
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
			clkSeen = false;   // clock stopped: wait for a fresh period
			period = 0.f;
		}
	}

	// ---- engine ---------------------------------------------------------------------------------------------
	void runBlock(float sr) {
		pullKnobs();
		vx::Params& p = cfg;
		auto expo = [&](int id) {
			auto* q = static_cast<ExpoQuantity*>(paramQuantities[id]);
			return (double) expoToVal(params[id].getValue(), q->lo, q->hi, q->ex);
		};
		p.osc[0].freq = expo(O1_FREQ_PARAM);
		p.osc[1].freq = expo(O2_FREQ_PARAM);
		p.osc[0].env = params[O1_EG_PARAM].getValue();
		p.osc[1].env = params[O2_EG_PARAM].getValue();
		p.osc[0].level = params[O1_LVL_PARAM].getValue();
		p.osc[1].level = params[O2_LVL_PARAM].getValue();
		p.osc[0].extPitch = vocFollow[0];
		p.osc[1].extPitch = vocFollow[1];
		p.noise = params[NOISE_PARAM].getValue();
		p.fm = clamp(params[FM_PARAM].getValue() + inputs[FM_CV_INPUT].getVoltage() * 60.f, 0.f, 600.f);
		p.attack = expo(ATTACK_PARAM);
		p.pDecay = expo(P_DECAY_PARAM);
		p.aDecay = expo(A_DECAY_PARAM);
		p.fDecay = expo(F_DECAY_PARAM);
		p.volume = params[VOLUME_PARAM].getValue();
		p.cutoff = clamp(params[CUTOFF_PARAM].getValue() + inputs[VCF_CV_INPUT].getVoltage() * 12.f, 0.f, 140.f);
		p.res = params[RES_PARAM].getValue();
		p.fEnv = params[F_ENV_PARAM].getValue();

		vx::Transport tr;
		blockTransport(sr, tr);
		engine.process(p, tr, events, numEvents, inputs[IN_INPUT].isConnected() ? inBuf : nullptr, outBuf, kBlock);
		numEvents = 0;

		const float g = std::pow(10.f, outDb / 20.f);
		for (int i = 0; i < kBlock; i++) {
			float y = outBuf[i] * g;
			if (softClip)
				y = std::tanh(y);
			outBuf[i] = y;
			peak = std::max(peak, std::abs(y));
		}
		const uint32_t t = engine.triggers.load(std::memory_order_relaxed);
		if (t != lastTriggers) {
			lastTriggers = t;
			trigPulse.trigger(1e-3f);
		}
	}

	void process(const ProcessArgs& args) override {
		for (int o = 0; o < 2; o++)
			if (shapeTrig[o].process(params[O1_SHAPE_PARAM + o].getValue() > 0.f))
				cfg.osc[o].shape = (cfg.osc[o].shape + 1) % 4;
		if (pageTrig.process(params[PAGE_PARAM].getValue() > 0.f))
			setPage(page ^ 1);
		if (runTrig.process(params[RUN_PARAM].getValue() > 0.f))
			runFlag = !runFlag;

		processClockEdges(args.sampleTime);

		// notes: TRIG = note-on; V/OCT changes re-pitch (outside EXT mode) without a trigger
		const bool vocConn = inputs[VOCT_INPUT].isConnected();
		const int note = vocConn ? clamp((int) std::round(60.f + 12.f * inputs[VOCT_INPUT].getVoltage()), 0, 127) : 60;
		const float vel = inputs[VEL_INPUT].isConnected() ? clamp(inputs[VEL_INPUT].getVoltage() / 10.f, 0.f, 1.f) : 1.f;
		const bool trig = trigTrig.process(inputs[TRIG_INPUT].getVoltage(), 0.1f, 1.f);
		if (numEvents < kBlock * 2) {
			if (trig)
				events[numEvents++] = {pos, note, std::max(vel, 1e-3f)};
			else if (note != lastNote && cfg.clock < vx::kNumRates)
				events[numEvents++] = {pos, note, std::max(vel, 1e-3f)};
		}
		if (trig || cfg.clock < vx::kNumRates)
			lastNote = note;

		inBuf[pos] = inputs[IN_INPUT].getVoltage() / kVoltsPerUnit;
		outputs[OUT_OUTPUT].setVoltage(outBuf[pos] * kVoltsPerUnit);
		if (++pos >= kBlock) {
			pos = 0;
			runBlock(args.sampleRate);
			outputs[PITCH_OUTPUT].setVoltage(10.f * engine.srcNow[vx::srcPitch].load(std::memory_order_relaxed));
			outputs[VEL_OUTPUT].setVoltage(10.f * engine.srcNow[vx::srcVel].load(std::memory_order_relaxed));
			outputs[AMP_EG_OUTPUT].setVoltage(10.f * engine.envAmp.load(std::memory_order_relaxed));
			outputs[FLT_EG_OUTPUT].setVoltage(10.f * engine.envFilt.load(std::memory_order_relaxed));
		}
		outputs[TRIG_OUTPUT].setVoltage(trigPulse.process(args.sampleTime) ? 10.f : 0.f);

		if (ui.tick(args.sampleTime)) {
			const bool running = inputs[CLK_INPUT].isConnected() ? (runFlag && period > 0.f) : runFlag;
			for (int o = 0; o < 2; o++)
				for (int s = 0; s < 4; s++)
					lights[(o ? O2_SHAPE_LIGHT : O1_SHAPE_LIGHT) + s].setBrightness(cfg.osc[o].shape == s ? 1.f : 0.f);
			lights[PAGE_PV_LIGHT].setBrightness(page == 0 ? 1.f : 0.f);
			lights[PAGE_M_LIGHT].setBrightness(page == 1 ? 1.f : 0.f);
			lights[RUN_LIGHT].setBrightness(running ? 1.f : 0.f);
			const int curA = engine.curStep[rowA()].load(std::memory_order_relaxed);
			for (int i = 0; i < 8; i++)
				lights[STEP_LIGHT + i].setBrightness(running && i == curA ? 1.f : 0.f);

			Snapshot& s = snap.writeSlot();
			for (int r = 0; r < vx::numSeq; r++) {
				for (int i = 0; i < 8; i++)
					s.steps[r][i] = (float) cfg.step[r][i];
				s.len[r] = cfg.length[r];
				s.cur[r] = engine.curStep[r].load(std::memory_order_relaxed);
			}
			s.page = page;
			s.running = running;
			for (int o = 0; o < 2; o++) {
				s.shape[o] = cfg.osc[o].shape;
				s.freq[o] = engine.freqNow[o].load(std::memory_order_relaxed);
			}
			s.envA = engine.envAmp.load(std::memory_order_relaxed);
			s.envP = engine.envPitch.load(std::memory_order_relaxed);
			s.envF = engine.envFilt.load(std::memory_order_relaxed);
			s.cutoff = engine.cutoffNow.load(std::memory_order_relaxed);
			// the last kScopePts x 8 scope samples (the engine writes every 4th sample)
			const int at = engine.scopeAt.load(std::memory_order_relaxed);
			for (int i = 0; i < kScopePts; i++) {
				const int k = (at - (kScopePts - i) * 4 + vx::Engine::kScope * 4) % vx::Engine::kScope;
				s.scope[i] = engine.scope[k].load(std::memory_order_relaxed);
			}
			s.peak = peak;
			snap.publish();
			peak = 0.f;
		}
	}
};

// ---------------------------------------------------------------------------------------------- screens

// The four rows as bar cells (KAZU-style step columns): value = bar height, the playing step in
// amber, steps past the row's length dimmed, the rows the knobs edit marked in the left gutter.
struct SeqScreen : Screen {
	Vertex* module = nullptr;
	Snapshot demo;

	SeqScreen() {
		const float d[4][8] = {{0.5f, 0.7f, 0.3f, 0.9f, 0.5f, 0.2f, 0.8f, 0.4f},
		                       {1.f, 0.3f, 0.7f, 0.f, 0.9f, 0.4f, 0.6f, 0.2f},
		                       {0.1f, 0.9f, 0.5f, 0.3f, 1.f, 0.f, 0.6f, 0.4f},
		                       {0.f, 0.4f, 0.8f, 0.2f, 0.f, 0.6f, 0.3f, 0.9f}};
		for (int r = 0; r < 4; r++)
			for (int i = 0; i < 8; i++)
				demo.steps[r][i] = d[r][i];
		demo.len[2] = 5;
		demo.len[3] = 7;
		demo.cur[0] = demo.cur[1] = 3;
		demo.cur[2] = 2;
		demo.cur[3] = 6;
		demo.running = true;
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		const float pad = 3.f, labW = 26.f;
		const float rowH = (h - 2.f * pad) / 4.f;
		const float cellW = (w - pad * 2.f - labW) / 8.f;
		static const char* names[] = {"PITCH", "VEL", "MOD 1", "MOD 2"};
		for (int r = 0; r < 4; r++) {
			const float y0 = pad + r * rowH;
			const bool edited = (s.page == 0) ? r < 2 : r >= 2;
			const NVGcolor col = r == 1 ? semi::theme::magenta() : semi::theme::cyan();
			if (edited)
				fillRect(vg, pad, y0 + 1.f, 2.f, rowH - 2.f, semi::theme::amber());
			text(vg, pad + 4.f, y0 + rowH * 0.5f, names[r], 8.f, edited ? semi::theme::text() : semi::theme::dim(),
			     NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			for (int i = 0; i < 8; i++) {
				const float x = pad + labW + i * cellW;
				const float cw = cellW - 2.f, ch = rowH - 2.f;
				const bool in = i < s.len[r];
				fillRect(vg, x, y0 + 1.f, cw, ch, semi::theme::cell());
				if (!in)
					continue;
				const float v = clamp(s.steps[r][i], 0.f, 1.f);
				const bool playing = s.running && i == s.cur[r];
				const float bh = std::max(1.5f, ch * v);
				fillRect(vg, x, y0 + 1.f + ch - bh, cw, bh, playing ? semi::theme::amber() : (edited ? col : semi::theme::cyanDim()));
			}
		}
	}
};

// Output scope + the three envelopes + the oscillators' live frequencies and shapes.
struct ScopeScreen : Screen {
	Vertex* module = nullptr;
	Snapshot demo;

	ScopeScreen() {
		for (int i = 0; i < kScopePts; i++)
			demo.scope[i] = 0.6f * std::sin(i * 0.45f) * std::exp(-i * 0.03f);
		demo.envA = 0.7f;
		demo.envP = 0.35f;
		demo.envF = 0.5f;
		demo.freq[1] = 131.f;
		demo.freq[0] = 262.f;
		demo.shape[0] = 0;
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		const float pad = 3.f;
		const float textH = 10.f;
		const float barW = 4.f;
		const float sx0 = pad, sx1 = w - pad - 3.f * (barW + 2.f);
		const float sy0 = pad + textH, sy1 = h - pad;
		const float mid = (sy0 + sy1) * 0.5f, amp = (sy1 - sy0) * 0.5f;
		line(vg, sx0, mid, sx1, mid, semi::theme::grid());
		float ys[kScopePts];
		for (int i = 0; i < kScopePts; i++)
			ys[i] = mid - amp * clamp(s.scope[i], -1.f, 1.f);
		polyline(vg, ys, kScopePts, sx0, sx1 - sx0, semi::theme::cyan());
		const float env[3] = {s.envA, s.envP, s.envF};
		const NVGcolor cols[3] = {semi::theme::cyan(), semi::theme::amber(), semi::theme::magenta()};
		for (int e = 0; e < 3; e++) {
			const float x = sx1 + 2.f + e * (barW + 2.f);
			fillRect(vg, x, sy0, barW, sy1 - sy0, semi::theme::cell());
			const float bh = (sy1 - sy0) * clamp(env[e], 0.f, 1.f);
			fillRect(vg, x, sy1 - bh, barW, bh, cols[e]);
		}
		char buf[40];
		auto hz = [](float f) { return f >= 1000.f ? string::f("%.1fk", f / 1000.f) : string::f("%.0f", f); };
		std::snprintf(buf, sizeof buf, "%s %s", kShapeNames[clamp(s.shape[0], 0, 3)].c_str(), hz(s.freq[0]).c_str());
		text(vg, pad, pad - 1.f, buf, 8.f, semi::theme::text());
		std::snprintf(buf, sizeof buf, "%s %s", kShapeNames[clamp(s.shape[1], 0, 3)].c_str(), hz(s.freq[1]).c_str());
		text(vg, w - pad, pad - 1.f, buf, 8.f, semi::theme::text(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
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

// A float as a fixed-step submenu (works on both platforms).
void addSteps(Menu* menu, std::string label, double* v, double lo, double hi, int steps, const char* fmt, double mult = 1.0) {
	std::vector<std::string> labels;
	for (int i = 0; i < steps; i++)
		labels.push_back(string::f(fmt, (lo + (hi - lo) * i / (steps - 1)) * mult));
	menu->addChild(createIndexSubmenuItem(label, labels,
		[=]() { return (size_t) clamp((int) std::round((*v - lo) / (hi - lo) * (steps - 1)), 0, steps - 1); },
		[=](size_t i) { *v = lo + (hi - lo) * i / (steps - 1); }));
}

void addBool(Menu* menu, std::string label, bool* v) {
	menu->addChild(createBoolPtrMenuItem(label, "", v));
}

} // namespace

struct VertexWidget : ModuleWidget {
	VertexWidget(Vertex* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Vertex.svg")));

		auto* seq = semi::createScreenMm<SeqScreen>(Vec(L::SEQ.x, L::SEQ.y), Vec(L::SEQ.w, L::SEQ.h));
		seq->module = module;
		addChild(seq);
		auto* scope = semi::createScreenMm<ScopeScreen>(Vec(L::SCOPE.x, L::SCOPE.y), Vec(L::SCOPE.w, L::SCOPE.h));
		scope->module = module;
		addChild(scope);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		const L::P o1[4] = {L::O1_TRI, L::O1_SQU, L::O1_SIN, L::O1_SAW};
		const L::P o2[4] = {L::O2_TRI, L::O2_SQU, L::O2_SIN, L::O2_SAW};
		for (int i = 0; i < 4; i++) {
			addChild(createLightCentered<semi::CyanLedSmall>(at(o1[i]), module, Vertex::O1_SHAPE_LIGHT + i));
			addChild(createLightCentered<semi::CyanLedSmall>(at(o2[i]), module, Vertex::O2_SHAPE_LIGHT + i));
		}
		addParam(createParamCentered<semi::Button>(at(L::O1_SHAPE), module, Vertex::O1_SHAPE_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::O2_SHAPE), module, Vertex::O2_SHAPE_PARAM));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::PAGE_PV), module, Vertex::PAGE_PV_LIGHT));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::PAGE_M), module, Vertex::PAGE_M_LIGHT));
		addParam(createParamCentered<semi::Button>(at(L::PAGE), module, Vertex::PAGE_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::RUN), module, Vertex::RUN_PARAM));
		addChild(createLightCentered<semi::AmberLedSmall>(at(L::RUN_LED), module, Vertex::RUN_LIGHT));

		const L::P rowA[8] = {L::O1_FREQ, L::O1_EG, L::O1_LVL, L::NOISE, L::O2_FREQ, L::O2_EG, L::O2_LVL, L::FM};
		const L::P rowB[8] = {L::ATTACK, L::P_DECAY, L::A_DECAY, L::VOLUME, L::CUTOFF, L::RES, L::F_ENV, L::F_DECAY};
		for (int i = 0; i < 8; i++) {
			if (i == 7)
				addParam(createParamCentered<semi::KnobMHot>(at(rowA[i]), module, Vertex::O1_FREQ_PARAM + i));
			else
				addParam(createParamCentered<semi::KnobM>(at(rowA[i]), module, Vertex::O1_FREQ_PARAM + i));
			if (i == 5)
				addParam(createParamCentered<semi::KnobMHot>(at(rowB[i]), module, Vertex::ATTACK_PARAM + i));
			else
				addParam(createParamCentered<semi::KnobM>(at(rowB[i]), module, Vertex::ATTACK_PARAM + i));
		}
		addParam(createParamCentered<semi::KnobS>(at(L::TEMPO), module, Vertex::TEMPO_PARAM));
		const L::P sa[8] = {L::STEP_A1, L::STEP_A2, L::STEP_A3, L::STEP_A4, L::STEP_A5, L::STEP_A6, L::STEP_A7, L::STEP_A8};
		const L::P sb[8] = {L::STEP_B1, L::STEP_B2, L::STEP_B3, L::STEP_B4, L::STEP_B5, L::STEP_B6, L::STEP_B7, L::STEP_B8};
		const L::P sl[8] = {L::STEP_LED1, L::STEP_LED2, L::STEP_LED3, L::STEP_LED4, L::STEP_LED5, L::STEP_LED6, L::STEP_LED7, L::STEP_LED8};
		for (int i = 0; i < 8; i++) {
			addParam(createParamCentered<semi::KnobS>(at(sa[i]), module, Vertex::STEP_A_PARAM + i));
			addParam(createParamCentered<semi::KnobS>(at(sb[i]), module, Vertex::STEP_B_PARAM + i));
			addChild(createLightCentered<semi::AmberLedSmall>(at(sl[i]), module, Vertex::STEP_LIGHT + i));
		}

		const L::P ins[8] = {L::CLK, L::RST, L::TRIG, L::VOCT, L::VEL, L::IN, L::VCF_CV, L::FM_CV};
		for (int i = 0; i < 8; i++)
			addInput(createInputCentered<semi::Jack>(at(ins[i]), module, Vertex::CLK_INPUT + i));
		const L::P outs[6] = {L::OUT, L::TRIG_OUT, L::PITCH_OUT, L::VEL_OUT, L::AMP_EG, L::FLT_EG};
		for (int i = 0; i < 6; i++)
			addOutput(createOutputCentered<semi::Jack>(at(outs[i]), module, Vertex::OUT_OUTPUT + i));
	}

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Vertex*>(module);
		if (!m)
			return;
		vx::Params* p = &m->cfg;

		menu->addChild(new MenuSeparator);
		std::vector<std::string> presets;
		for (int i = 0; i < vx::numFactory(); i++)
			presets.push_back(vx::factory(i).name);
		menu->addChild(createIndexSubmenuItem("Factory preset", presets,
			[=]() { return (size_t) m->preset; },
			[=](size_t i) { m->applyPreset((int) i); }));
		menu->addChild(createMenuItem("Roll PITCH + VEL rows", "", [=]() { m->randomizeRows(false); }));
		menu->addChild(createMenuItem("Roll MOD 1 + MOD 2 rows", "", [=]() { m->randomizeRows(true); }));

		menu->addChild(createSubmenuItem("Clock", "", [=](Menu* sub) {
			addIndex(sub, "Master clock", kClockNames, &p->clock);
			addIndex(sub, "CLK input PPQN", kPpqnNames, &m->ppqnIdx);
			addSteps(sub, "Swing", &p->swing, 0.0, 0.5, 11, "%.0f %%", 100.0);
		}));

		menu->addChild(createSubmenuItem("Sequencer rows", "", [=](Menu* sub) {
			std::vector<std::string> lens;
			for (int i = 1; i <= 8; i++)
				lens.push_back(string::f("%d", i));
			for (int r = 0; r < vx::numSeq; r++) {
				sub->addChild(createIndexSubmenuItem(kRowNames[r] + " length", lens,
					[=]() { return (size_t) (p->length[r] - 1); },
					[=](size_t i) { p->length[r] = (int) i + 1; }));
				addIndex(sub, kRowNames[r] + " rate", kRateNames, &p->rate[r]);
			}
			addSteps(sub, "Pitch range", &p->pitchRange, 0.0, 1.0, 11, "%.0f %%", 100.0);
		}));

		for (int o = 0; o < 2; o++) {
			menu->addChild(createSubmenuItem(string::f("Oscillator %d", o + 1), "", [=](Menu* sub) {
				vx::OscParams* op = &p->osc[o];
				addIndex(sub, "Shape", kShapeNames, &op->shape);
				addBool(sub, "Pitch row (INT)", &op->intPitch);
				addBool(sub, "Follow V/OCT", &m->vocFollow[o]);
				addBool(sub, "Quantize to scale", &op->quantize);
				addBool(sub, "Glide", &op->glide);
				addSteps(sub, "Fine", &op->fine, -12.0, 12.0, 25, "%+.0f st");
				addIndex(sub, "Level mod source", kSrcNames, &op->levelSrc);
				addIndex(sub, "Pitch env mod source", kSrcNames, &op->envSrc);
			}));
		}

		menu->addChild(createSubmenuItem("FM / sync / glide / scale", "", [=](Menu* sub) {
			addIndex(sub, "FM from", std::vector<std::string>{"OSC 1", "IN"}, &p->fmFrom);
			addIndex(sub, "FM mod source", kSrcNames, &p->fmSrc);
			sub->addChild(createCheckMenuItem("Sync osc 2 to osc 1", "", [=]() { return p->sync; }, [=]() { p->sync = !p->sync; }));
			addIndex(sub, "Sync mode", std::vector<std::string>{"PATCH (as the device)", "HARD"}, &p->syncMode);
			addSteps(sub, "Glide time", &p->glide, 0.0, 1.0, 11, "%.1f s");
			addIndex(sub, "Glide mod source", kSrcNames, &p->glideSrc);
			addIndex(sub, "Scale root", kNoteNames, &p->root);
			std::vector<std::string> scales;
			for (int i = 0; i < vx::Engine::numScales(); i++)
				scales.push_back(vx::Engine::scaleName(i));
			addIndex(sub, "Scale", scales, &p->scale);
		}));

		menu->addChild(createSubmenuItem("Filter", "", [=](Menu* sub) {
			addIndex(sub, "Type", std::vector<std::string>{"LP", "BP", "HP"}, &p->filterType);
			addIndex(sub, "Slope", std::vector<std::string>{"12 dB", "24 dB"}, &p->slope);
			addSteps(sub, "Saturation drive", &p->drive, 0.0, 1.0, 11, "%.0f %%", 100.0);
			addIndex(sub, "Saturator position", std::vector<std::string>{"PRE", "POST"}, &p->order);
			addSteps(sub, "Filter mod amount", &p->fMod, -150.0, 150.0, 13, "%+.0f st");
			addIndex(sub, "Filter mod from", std::vector<std::string>{"NOISE", "OSC 1", "OSC 2", "IN"}, &p->fModFrom);
			addIndex(sub, "Filter mod source", kSrcNames, &p->fModSrc);
			addIndex(sub, "Cutoff mod source", kSrcNames, &p->cutoffSrc);
			addIndex(sub, "Resonance mod source", kSrcNames, &p->resSrc);
			addIndex(sub, "Filter env mod source", kSrcNames, &p->fEnvSrc);
			addIndex(sub, "Filter decay mod source", kSrcNames, &p->fDecaySrc);
		}));

		menu->addChild(createSubmenuItem("Amp / envelopes / noise", "", [=](Menu* sub) {
			addIndex(sub, "Volume mod source", kSrcNames, &p->volumeSrc);
			addIndex(sub, "Amp decay mod source", kSrcNames, &p->aDecaySrc);
			addIndex(sub, "Pitch decay mod source", kSrcNames, &p->pDecaySrc);
			addIndex(sub, "Attack mod source", kSrcNames, &p->attackSrc);
			addSteps(sub, "Decay curve", &p->curve, -1.0, 0.0, 11, "%.1f");
			addIndex(sub, "Noise mod source", kSrcNames, &p->noiseSrc);
			addIndex(sub, "Noise colour", std::vector<std::string>{"WHITE", "PINK"}, &p->noiseColour);
			addSteps(sub, "IN level", &p->extLevel, 0.0, 1.0, 11, "%.0f %%", 100.0);
		}));

		menu->addChild(createSubmenuItem("Output", "", [=](Menu* sub) {
			std::vector<std::string> trims;
			for (int d = -24; d <= 12; d += 3)
				trims.push_back(string::f("%+d dB", d));
			sub->addChild(createIndexSubmenuItem("Trim", trims,
				[=]() { return (size_t) clamp((int) std::round((m->outDb + 24.f) / 3.f), 0, 12); },
				[=](size_t i) { m->outDb = -24.f + 3.f * (float) i; }));
			addBool(sub, "Soft clip", &m->softClip);
		}));
	}
};

Model* modelVertex = createModel<Vertex, VertexWidget>("Vertex");
