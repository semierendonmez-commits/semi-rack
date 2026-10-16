// ACID — Rack / MetaModule port of Schwung Acid, "TB-3PO x 303" (engine: engines/acid, vendored from
// semierendonmez-commits/acid Source/engine with only its JUCE types replaced; see AcidEngine.h).
//
// The sequencer is the Phazerville TB-3PO algorithm (Logarhythm1 / djphazer, GPL-3.0; rebuilt as
// Tb3poCore), the voice is schwung-303 (Open303 by Robin Schmidt, MIT + Devilfish mods + Soft / RAT
// drive, charlesvestal/schwung-303, GPL-3.0), its DSP compiled unchanged.
//
// Position (no host transport in Rack): CLK (1/16 by default, menu for others) + RST give the ppq the
// engine locks its 16th grid to, exactly as it locks to a DAW; with CLK unpatched an internal tempo
// can run it (menu). FILL = the plugin's play-mode note input: in GATE mode the line plays while FILL
// is high, in TRIGGER mode each rising edge opens a window (menu), retriggering from step 1.
//
// Outputs: the 303 (OUT, +-5 V) and the line as CV: V/OCT (1 V/oct, C4 = 0 V), GATE (10 V; held
// through slides, as the 303 sees it), ACC (10 V on accented notes), SLIDE (10 V while the sounding
// step glides into the next). V/OCT in transposes the line (and the 303) in semitones.
//
// The engine runs in 32-sample blocks (adds 32 samples of latency to every output, audio and CV
// alike, so they stay aligned). Everything happens on the audio thread; menu commands (undo, banks)
// are handed over through an atomic.

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/AcidLayout.hpp"
#include "acid/AcidEngine.h"

#include <atomic>
#include <cstdio>

namespace L = semi::layout::acid;
using semi::Screen;

namespace {

constexpr int kBlock = 32;
constexpr int kSteps = acid::Tb3poCore::maxSteps;
constexpr int kBanks = acid::Tb3poCore::numBanks;
constexpr int kNumFloat = acid::Voice303::numFloat;

const std::vector<std::string> kRootNames = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const std::vector<std::string> kDirNames = {"Forward", "Reverse", "Ping-pong", "Random"};
const std::vector<std::string> kAutoModes = {"Off", "Mutate", "New"};
const std::vector<std::string> kAutoBars = {"1 bar", "2 bars", "4 bars", "8 bars", "16 bars", "On every FILL"};
constexpr int kAutoBarValues[] = {1, 2, 4, 8, 16, 0};
const std::vector<std::string> kPlayModes = {"Always", "Gate (while FILL is high)", "Trigger (window per FILL)"};
const std::vector<std::string> kTrigLens = {"1/4", "1/2", "1 bar", "2 bars", "4 bars", "8 bars"};
constexpr int kTrigLenSteps[] = {4, 8, 16, 32, 64, 128};
const std::vector<std::string> kPct = {"0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"};
const std::vector<std::string> kLfoModes = {"Trig (new value per note)", "S&H (new value per rate)"};
const std::vector<std::string> kLfoRates = {"1/32", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars"};
const std::vector<std::string> kSmoothNames = {"0 ms", "20 ms", "50 ms", "150 ms", "500 ms", "2000 ms"};
constexpr float kSmoothMs[] = {0.f, 20.f, 50.f, 150.f, 500.f, 2000.f};
const std::vector<std::string> kClockNames = {"1/16 (4 PPQN)", "1/8 (2 PPQN)", "1/4 (1 PPQN)", "24 PPQN"};
constexpr float kClockPpqn[] = {4.f, 2.f, 1.f, 24.f};
const std::vector<std::string> kTempoNames = {"Off (CLK only)", "120 BPM", "128 BPM", "132 BPM", "138 BPM", "145 BPM"};
constexpr float kTempos[] = {0.f, 120.f, 128.f, 132.f, 138.f, 145.f};
// 303 controls with a random LFO in the menu (indices into Voice303::floatKeys)
constexpr int kLfoTargets[] = {0, 1, 2, 3, 4};
const char* const kLfoTargetNames[] = {"Cutoff", "Resonance", "Env mod", "Decay", "Accent"};

// Menu commands for the audio thread.
enum Cmd { CMD_NONE = 0, CMD_UNDO = 1, CMD_STORE = 2, CMD_RECALL = 3, CMD_NEW = 4, CMD_MUTATE = 5 };

// What is saved in the patch (published by the audio thread, read by dataToJson).
struct Saved {
	acid::Tb3poCore::Pattern pattern;
	std::array<acid::Tb3poCore::Pattern, kBanks> banks{};
	std::array<bool, kBanks> filled{};
	acid::Tb3poCore::GenParams frozen;
};

struct Snapshot {
	acid::Tb3poCore::Step steps[kSteps];
	bool plays[kSteps] = {};
	int length = 16, position = 0, scaleLen = 7, density = 4;
	int root = 9, scale = 0;
	unsigned seed = 0x3B03;
	int clock = 0;  // 0 stopped, 1 CLK, 2 internal
	bool armed = true;
	int playMode = 0;
	float bpm = 0.f;
	int pendingRecall = -1;
};

} // namespace

struct Acid : Module {
	enum ParamId {
		DENSITY_PARAM, ACC_ODDS_PARAM, SLIDE_ODDS_PARAM, OCT_ODDS_PARAM, NEW_PARAM, MUTATE_PARAM,
		CUTOFF_PARAM, RESO_PARAM, ENVMOD_PARAM, DECAY_PARAM, ACCENT_PARAM, DRIVE_PARAM, GATE_PARAM, LEVEL_PARAM,
		PARAMS_LEN
	};
	enum InputId { CLK_INPUT, RST_INPUT, VOCT_INPUT, FILL_INPUT, CUT_INPUT, INPUTS_LEN };
	enum OutputId { OUT_OUTPUT, VOCT_OUTPUT, GATE_OUTPUT, ACC_OUTPUT, SLIDE_OUTPUT, OUTPUTS_LEN };
	enum LightId { LIGHTS_LEN };

	acid::AcidEngine engine;
	acid::EngineSettings settings;

	// ---- menu state (saved in the patch) -------------------------------------------------------
	int scale = 0, root = 9, octave = 2, transpose = 0, length = 16, direction = 0;
	bool live = true;
	int autoMode = 0, autoBarsIdx = 2;
	int playMode = 0, trigLenIdx = 2;
	bool retrig = true;
	int waveform = 0, driveModel = 0;
	bool devil = false;
	// 303 controls that are not panel knobs (normalised 0..1, the module's defaults)
	float volume = 0.8f, tuning = 0.5f, normalDecay = 0.5f, accentDecay = 0.5f, fbHpf = 0.5f;
	float softAttack = 0.f, slideTime = 0.3f, shaper = 0.f, driveMix = 1.f;
	struct Lfo {
		int depth = 0, mode = 1, rate = 3, smooth = 0;
	} lfo[5];
	int clockIdx = 0, tempoIdx = 0;

	std::atomic<int> cmd{CMD_NONE};

	// ---- audio thread -------------------------------------------------------------------------
	float outBuf[kBlock] = {};
	float renderBuf[kBlock] = {};
	acid::MidiIn inEv[8];
	int nIn = 0;
	int pos = 0;
	acid::HostTime blockHost;
	struct CvEv {
		int time;
		uint8_t b[3];
	} cvEv[64];
	int nCv = 0, cvIdx = 0;
	bool blockSlide = false;

	dsp::SchmittTrigger clkTrig, rstTrig, fillTrig;
	dsp::BooleanTrigger newBtn, mutBtn;
	int count = -1;
	float sinceClk = 0.f, period = 0.f;
	bool clkSeen = false;
	int clockMode = 0;
	float bpm = 0.f;

	// CV state
	int heldNote = -1;
	bool gateHigh = false, accOn = false, slideOn = false;
	float voct = 0.f, sinceOff = 1.f, lowTimer = 0.f;

	int appliedWave = -1, appliedDevil = -1, appliedModel = -1;
	Saved load;
	std::atomic<bool> loadPending{false};

	semi::TripleBuffer<Snapshot> snap;
	semi::TripleBuffer<Saved> saved;
	semi::UiDivider ui;

	Acid() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		auto* dens = configParam(DENSITY_PARAM, -7.f, 7.f, 4.f, "Density (-7 root only, dense .. 0 sparsest .. +7 dense)");
		dens->snapEnabled = true;
		configParam(ACC_ODDS_PARAM, 0.f, 1.f, 0.5f, "Accent odds", " %", 0.f, 100.f);
		configParam(SLIDE_ODDS_PARAM, 0.f, 1.f, 0.5f, "Slide odds", " %", 0.f, 100.f);
		configParam(OCT_ODDS_PARAM, 0.f, 1.f, 0.4f, "Octave jumps", " %", 0.f, 100.f);
		configButton(NEW_PARAM, "New (new seed)");
		configButton(MUTATE_PARAM, "Mutate (re-roll ~1/4 of the steps)");
		configParam(CUTOFF_PARAM, 0.f, 1.f, 0.5f, "Cutoff", " %", 0.f, 100.f);
		configParam(RESO_PARAM, 0.f, 1.f, 0.5f, "Resonance", " %", 0.f, 100.f);
		configParam(ENVMOD_PARAM, 0.f, 1.f, 0.5f, "Env mod", " %", 0.f, 100.f);
		configParam(DECAY_PARAM, 0.f, 1.f, 0.5f, "Decay", " %", 0.f, 100.f);
		configParam(ACCENT_PARAM, 0.f, 1.f, 0.5f, "Accent", " %", 0.f, 100.f);
		configParam(DRIVE_PARAM, 0.f, 1.f, 0.f, "Drive", " %", 0.f, 100.f);
		configParam(GATE_PARAM, 0.05f, 1.f, 0.5f, "Gate length", " % of a 16th", 0.f, 100.f);
		configParam(LEVEL_PARAM, -36.f, 6.f, 0.f, "Level", " dB");
		configInput(CLK_INPUT, "Clock (1/16 notes by default)");
		configInput(RST_INPUT, "Reset (next clock = step 1)");
		configInput(VOCT_INPUT, "Transpose (V/OCT, semitones)");
		configInput(FILL_INPUT, "Fill gate (play mode gate / trigger, menu)");
		configInput(CUT_INPUT, "Cutoff CV (+-5 V = +-50 %)");
		configOutput(OUT_OUTPUT, "303 audio");
		configOutput(VOCT_OUTPUT, "Line V/OCT");
		configOutput(GATE_OUTPUT, "Line gate");
		configOutput(ACC_OUTPUT, "Accent gate");
		configOutput(SLIDE_OUTPUT, "Slide gate");
		engine.prepare(44100.0);
		updateSettings();
		engine.refresh(settings);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		engine.prepare(e.sampleRate);
		appliedWave = appliedDevil = appliedModel = -1;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		scale = 0; root = 9; octave = 2; transpose = 0; length = 16; direction = 0;
		live = true; autoMode = 0; autoBarsIdx = 2; playMode = 0; trigLenIdx = 2; retrig = true;
		waveform = 0; driveModel = 0; devil = false;
		volume = 0.8f; tuning = 0.5f; normalDecay = 0.5f; accentDecay = 0.5f; fbHpf = 0.5f;
		softAttack = 0.f; slideTime = 0.3f; shaper = 0.f; driveMix = 1.f;
		for (auto& l : lfo)
			l = Lfo();
		clockIdx = tempoIdx = 0;
	}

	// ---- settings -------------------------------------------------------------------------------

	void updateSettings() {
		acid::EngineSettings& s = settings;
		s.gen.density = clamp((int) std::round(params[DENSITY_PARAM].getValue()), -7, 7);
		s.gen.accent = params[ACC_ODDS_PARAM].getValue();
		s.gen.slide = params[SLIDE_ODDS_PARAM].getValue();
		s.gen.octaves = params[OCT_ODDS_PARAM].getValue();
		s.scale = clamp(scale, 0, acid::tb3poNumScales - 1);
		s.gen.scaleLen = acid::tb3poScales[s.scale].len;
		s.live = live;
		s.root = root;
		s.baseOctave = octave;
		int tr = transpose;
		if (inputs[VOCT_INPUT].isConnected())
			tr += (int) std::lround(inputs[VOCT_INPUT].getVoltage() * 12.f);
		s.transpose = clamp(tr, -48, 48);
		s.length = clamp(length, 1, kSteps);
		s.direction = direction;
		s.gate = params[GATE_PARAM].getValue();
		s.channel = 1;
		s.midiOut = true;
		s.masterGain = std::pow(10.f, params[LEVEL_PARAM].getValue() / 20.f);
		s.internalRun = kTempos[tempoIdx] > 0.f && !inputs[CLK_INPUT].isConnected();
		s.internalBpm = kTempos[tempoIdx] > 0.f ? kTempos[tempoIdx] : 128.0;
		s.autoMode = autoMode;
		s.autoBars = kAutoBarValues[clamp(autoBarsIdx, 0, 5)];
		s.playMode = clamp(playMode, 0, 2);
		s.trigSteps = kTrigLenSteps[clamp(trigLenIdx, 0, 5)];
		s.retrig = retrig;
		s.trigChannel = 0;

		float cut = params[CUTOFF_PARAM].getValue();
		if (inputs[CUT_INPUT].isConnected())
			cut += inputs[CUT_INPUT].getVoltage() / 10.f;
		const float base[kNumFloat] = {clamp(cut, 0.f, 1.f), params[RESO_PARAM].getValue(), params[ENVMOD_PARAM].getValue(),
		                               params[DECAY_PARAM].getValue(), params[ACCENT_PARAM].getValue(), volume, tuning,
		                               normalDecay, accentDecay, fbHpf, softAttack, slideTime, shaper,
		                               params[DRIVE_PARAM].getValue(), driveMix};
		for (int i = 0; i < kNumFloat; i++) {
			s.base[(size_t) i] = base[i];
			s.lfo[(size_t) i] = acid::LfoSettings();
			s.lfo[(size_t) i].depth = 0.f;
		}
		for (int k = 0; k < 5; k++) {
			auto& l = s.lfo[(size_t) kLfoTargets[k]];
			l.depth = lfo[k].depth / 10.f;
			l.mode = lfo[k].mode;
			l.rate = lfo[k].rate;
			l.smoothMs = kSmoothMs[clamp(lfo[k].smooth, 0, 5)];
		}

		if (devil != (appliedDevil == 1)) {
			engine.voice().setParamInt("devil_mod_switch", devil ? 1 : 0);
			appliedDevil = devil ? 1 : 0;
			engine.invalidateMod();
		}
		if (waveform != appliedWave) {
			engine.voice().setParamInt("waveform", waveform);
			appliedWave = waveform;
		}
		if (driveModel != appliedModel) {
			engine.voice().setParamInt("drive_model", driveModel);
			appliedModel = driveModel;
		}
	}

	// ---- JSON -----------------------------------------------------------------------------------

	static json_t* patternToJson(const acid::Tb3poCore::Pattern& p) {
		json_t* o = json_object();
		json_object_set_new(o, "seed", json_integer(p.seed));
		json_t* salt = json_array();
		json_t* edits = json_array();
		for (int i = 0; i < kSteps; i++) {
			json_array_append_new(salt, json_integer((json_int_t) p.salt[(size_t) i]));
			const auto& e = p.edits[(size_t) i];
			json_array_append_new(edits, json_integer(e.on ? (1 | (e.kind << 1) | (e.degree << 3) | ((e.octave + 1) << 8)) : 0));
		}
		json_object_set_new(o, "salt", salt);
		json_object_set_new(o, "edits", edits);
		return o;
	}

	static void patternFromJson(json_t* o, acid::Tb3poCore::Pattern& p) {
		if (!json_is_object(o))
			return;
		if (json_t* s = json_object_get(o, "seed"))
			p.seed = (uint16_t) json_integer_value(s);
		json_t* salt = json_object_get(o, "salt");
		json_t* edits = json_object_get(o, "edits");
		for (int i = 0; i < kSteps; i++) {
			if (json_t* j = json_array_get(salt, i))
				p.salt[(size_t) i] = (uint32_t) json_integer_value(j);
			if (json_t* j = json_array_get(edits, i)) {
				const int v = (int) json_integer_value(j);
				auto& e = p.edits[(size_t) i];
				e.on = v & 1;
				e.kind = (uint8_t) ((v >> 1) & 3);
				e.degree = (uint8_t) clamp((v >> 3) & 31, 0, 11);
				e.octave = (int8_t) clamp(((v >> 8) & 3) - 1, -1, 1);
			}
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		const Saved& sv = saved.read();
		json_object_set_new(root, "pattern", patternToJson(sv.pattern));
		json_t* banks = json_array();
		for (int b = 0; b < kBanks; b++)
			json_array_append_new(banks, sv.filled[(size_t) b] ? patternToJson(sv.banks[(size_t) b]) : json_null());
		json_object_set_new(root, "banks", banks);
		json_t* fz = json_array();
		json_array_append_new(fz, json_real(sv.frozen.accent));
		json_array_append_new(fz, json_real(sv.frozen.slide));
		json_array_append_new(fz, json_real(sv.frozen.octaves));
		json_object_set_new(root, "frozen", fz);

		auto seti = [&](const char* k, int v) { json_object_set_new(root, k, json_integer(v)); };
		auto setf = [&](const char* k, float v) { json_object_set_new(root, k, json_real(v)); };
		seti("scale", scale); seti("root", this->root); seti("octave", octave); seti("transpose", transpose);
		seti("length", length); seti("direction", direction); seti("live", live); seti("autoMode", autoMode);
		seti("autoBars", autoBarsIdx); seti("playMode", playMode); seti("trigLen", trigLenIdx); seti("retrig", retrig);
		seti("waveform", waveform); seti("driveModel", driveModel); seti("devil", devil);
		setf("volume", volume); setf("tuning", tuning); setf("normalDecay", normalDecay); setf("accentDecay", accentDecay);
		setf("fbHpf", fbHpf); setf("softAttack", softAttack); setf("slideTime", slideTime); setf("shaper", shaper);
		setf("driveMix", driveMix);
		json_t* lf = json_array();
		for (auto& l : lfo) {
			json_t* e = json_array();
			json_array_append_new(e, json_integer(l.depth));
			json_array_append_new(e, json_integer(l.mode));
			json_array_append_new(e, json_integer(l.rate));
			json_array_append_new(e, json_integer(l.smooth));
			json_array_append_new(lf, e);
		}
		json_object_set_new(root, "lfo", lf);
		seti("clock", clockIdx); seti("tempo", tempoIdx);
		return root;
	}

	void dataFromJson(json_t* root) override {
		auto geti = [&](const char* k, int& v, int lo, int hi) {
			if (json_t* j = json_object_get(root, k))
				v = clamp((int) json_integer_value(j), lo, hi);
		};
		auto getb = [&](const char* k, bool& v) {
			if (json_t* j = json_object_get(root, k))
				v = json_integer_value(j) != 0;
		};
		auto getf = [&](const char* k, float& v) {
			if (json_t* j = json_object_get(root, k))
				v = clamp((float) json_number_value(j), 0.f, 1.f);
		};
		geti("scale", scale, 0, acid::tb3poNumScales - 1); geti("root", this->root, 0, 11); geti("octave", octave, 0, 5);
		geti("transpose", transpose, -24, 24); geti("length", length, 1, kSteps); geti("direction", direction, 0, 3);
		getb("live", live); geti("autoMode", autoMode, 0, 2); geti("autoBars", autoBarsIdx, 0, 5);
		geti("playMode", playMode, 0, 2); geti("trigLen", trigLenIdx, 0, 5); getb("retrig", retrig);
		geti("waveform", waveform, 0, 1); geti("driveModel", driveModel, 0, 1); getb("devil", devil);
		getf("volume", volume); getf("tuning", tuning); getf("normalDecay", normalDecay); getf("accentDecay", accentDecay);
		getf("fbHpf", fbHpf); getf("softAttack", softAttack); getf("slideTime", slideTime); getf("shaper", shaper);
		getf("driveMix", driveMix);
		if (json_t* lf = json_object_get(root, "lfo"))
			for (int k = 0; k < 5; k++)
				if (json_t* e = json_array_get(lf, k)) {
					lfo[k].depth = clamp((int) json_integer_value(json_array_get(e, 0)), 0, 10);
					lfo[k].mode = clamp((int) json_integer_value(json_array_get(e, 1)), 0, 1);
					lfo[k].rate = clamp((int) json_integer_value(json_array_get(e, 2)), 0, 7);
					lfo[k].smooth = clamp((int) json_integer_value(json_array_get(e, 3)), 0, 5);
				}
		geti("clock", clockIdx, 0, 3); geti("tempo", tempoIdx, 0, (int) kTempoNames.size() - 1);

		if (loadPending.load(std::memory_order_acquire))
			return;  // the previous load has not been taken yet
		Saved l;
		patternFromJson(json_object_get(root, "pattern"), l.pattern);
		if (json_t* banks = json_object_get(root, "banks"))
			for (int b = 0; b < kBanks; b++) {
				json_t* o = json_array_get(banks, b);
				l.filled[(size_t) b] = json_is_object(o);
				if (l.filled[(size_t) b])
					patternFromJson(o, l.banks[(size_t) b]);
			}
		l.frozen.accent = l.frozen.slide = 0.5f;
		l.frozen.octaves = 0.4f;
		if (json_t* fz = json_object_get(root, "frozen")) {
			l.frozen.accent = clamp((float) json_number_value(json_array_get(fz, 0)), 0.f, 1.f);
			l.frozen.slide = clamp((float) json_number_value(json_array_get(fz, 1)), 0.f, 1.f);
			l.frozen.octaves = clamp((float) json_number_value(json_array_get(fz, 2)), 0.f, 1.f);
		}
		load = l;
		loadPending.store(true, std::memory_order_release);
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

	// ---- block ----------------------------------------------------------------------------------

	void runCommands() {
		if (loadPending.load(std::memory_order_acquire)) {
			auto& core = engine.core();
			core.pattern = load.pattern;
			core.banks = load.banks;
			core.filled = load.filled;
			engine.setFrozen(load.frozen);
			engine.refresh(settings);
			loadPending.store(false, std::memory_order_release);
		}
		const int c = cmd.exchange(CMD_NONE);
		if (c == CMD_NONE)
			return;
		engine.setEntropy(random::u32());
		const int op = c & 15, bank = c >> 4;
		switch (op) {
			case CMD_UNDO: engine.cmdUndo(); break;
			case CMD_STORE: engine.cmdStore(bank); break;
			case CMD_RECALL: engine.cmdRecall(bank); break;
			case CMD_NEW: engine.cmdNew(settings); break;
			case CMD_MUTATE: engine.cmdMutate(settings); break;
			default: break;
		}
	}

	void runBlock() {
		updateSettings();
		runCommands();
		engine.process(renderBuf, kBlock, inEv, nIn, blockHost, settings);
		nIn = 0;
		for (int i = 0; i < kBlock; i++)
			outBuf[i] = renderBuf[i];
		nCv = 0;
		cvIdx = 0;
		for (int i = 0; i < engine.eventCount() && nCv < 64; i++) {
			const auto& e = engine.event(i);
			cvEv[nCv++] = {e.time, {e.b[0], e.b[1], e.b[2]}};
		}
		const int st = clamp(engine.position(), 0, kSteps - 1);
		blockSlide = engine.core().step(st).slide;
	}

	void applyCv(const CvEv& e) {
		const uint8_t status = e.b[0] & 0xF0;
		if (status == 0x90 && e.b[2] > 0) {
			if (!gateHigh && sinceOff < 0.001f)
				lowTimer = 0.001f;  // a note right after an off: keep a visible gap
			heldNote = e.b[1];
			gateHigh = true;
			voct = (heldNote - 60) / 12.f;
			accOn = e.b[2] >= 100;
			slideOn = blockSlide;
		}
		else if (status == 0x80 || status == 0x90) {
			if (e.b[1] == heldNote) {
				gateHigh = false;
				accOn = false;
				slideOn = false;
				heldNote = -1;
				sinceOff = 0.f;
			}
		}
	}

	void process(const ProcessArgs& args) override {
		if (newBtn.process(params[NEW_PARAM].getValue() > 0.f))
			cmd = CMD_NEW;
		if (mutBtn.process(params[MUTATE_PARAM].getValue() > 0.f))
			cmd = CMD_MUTATE;

		double ppq = 0.0;
		const bool ok = clockPosition(args.sampleTime, ppq);
		if (pos == 0) {
			blockHost.valid = ok;
			blockHost.playing = ok;
			blockHost.ppq = ppq;
			blockHost.bpm = ok ? bpm : 120.0;
		}

		// FILL -> the plugin's play-mode note input
		const bool wasHigh = fillTrig.isHigh();
		fillTrig.process(inputs[FILL_INPUT].getVoltage(), 0.1f, 1.f);
		if (fillTrig.isHigh() != wasHigh && nIn < 8) {
			acid::MidiIn& m = inEv[nIn++];
			m.time = pos;
			m.numBytes = 3;
			m.data[0] = fillTrig.isHigh() ? 0x90 : 0x80;
			m.data[1] = 60;
			m.data[2] = fillTrig.isHigh() ? 100 : 0;
		}

		// outputs of the previous block, sample by sample
		while (cvIdx < nCv && cvEv[cvIdx].time <= pos)
			applyCv(cvEv[cvIdx++]);
		sinceOff += args.sampleTime;
		bool g = gateHigh;
		if (lowTimer > 0.f) {
			lowTimer -= args.sampleTime;
			g = false;
		}
		outputs[OUT_OUTPUT].setVoltage(clamp(outBuf[pos] * 5.f, -12.f, 12.f));
		outputs[VOCT_OUTPUT].setVoltage(voct);
		outputs[GATE_OUTPUT].setVoltage(g ? 10.f : 0.f);
		outputs[ACC_OUTPUT].setVoltage(g && accOn ? 10.f : 0.f);
		outputs[SLIDE_OUTPUT].setVoltage(gateHigh && slideOn ? 10.f : 0.f);

		if (++pos >= kBlock) {
			pos = 0;
			while (cvIdx < nCv)
				applyCv(cvEv[cvIdx++]);
			runBlock();
		}

		if (ui.tick(args.sampleTime)) {
			const auto clk = engine.clock();
			clockMode = clk == acid::AcidEngine::Clock::host ? 1 : clk == acid::AcidEngine::Clock::internal ? 2 : 0;
			Snapshot& s = snap.writeSlot();
			auto& core = engine.core();
			const int Ln = clamp(settings.length, 1, kSteps);
			for (int i = 0; i < kSteps; i++) {
				s.steps[i] = core.step(i);
				const int prev = settings.direction == 0 ? (i + Ln - 1) % Ln : (settings.direction == 1 ? (i + 1) % Ln : -1);
				s.plays[i] = core.plays(i, prev);
			}
			s.length = Ln;
			s.position = engine.position();
			s.scaleLen = settings.gen.scaleLen;
			s.density = settings.gen.density;
			s.root = settings.root;
			s.scale = settings.scale;
			s.seed = core.pattern.seed;
			s.clock = clockMode;
			s.armed = engine.armed();
			s.playMode = settings.playMode;
			s.bpm = (float) engine.tempo();
			s.pendingRecall = engine.pendingRecall();
			snap.publish();

			Saved& sv = saved.writeSlot();
			sv.pattern = core.pattern;
			sv.banks = core.banks;
			sv.filled = core.filled;
			sv.frozen = engine.frozen();
			saved.publish();
		}
	}
};

// ---- screen -----------------------------------------------------------------------------------

// The TB-3PO line: one column per step (length up to 32). A block per sounding step at its pitch
// (scale degree + octave), magenta when accented, an amber line into the next column when it slides, a dim
// tick on rests. The current step is underlined in amber; steps past LENGTH are dimmed out.
// Footer: density, key, seed and the clock.
struct GridScreen : Screen {
	Acid* module = nullptr;
	Snapshot demo;

	GridScreen() {
		acid::Tb3poCore core;
		acid::Tb3poCore::GenParams g;
		g.density = 4;
		core.generate(g);
		for (int i = 0; i < kSteps; i++) {
			demo.steps[i] = core.step(i);
			demo.plays[i] = core.plays(i, (i + 15) % 16);
		}
		demo.length = 16;
		demo.position = 5;
		demo.clock = 1;
		demo.bpm = 132.f;
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		const float pad = 3.f;
		const float fs = std::max(9.f, semi::platform::kMinText + 1.f);
		const int cols = s.length <= 16 ? 16 : 32;
		const float gx0 = pad, gx1 = w - pad;
		const float gy0 = pad, gy1 = h - pad - fs - 4.f;
		const float cw = (gx1 - gx0) / cols;
		const int sl = std::max(1, s.scaleLen);
		// pitch rows: one octave below to one above the scale (3 * scaleLen rows)
		const int rows = 3 * sl;
		const float rh = (gy1 - gy0) / rows;
		const float bh = std::max(2.6f, rh);
		auto yOf = [&](const acid::Tb3poCore::Step& st) {
			const int r = clamp(st.degree % sl + (st.octave + 1) * sl, 0, rows - 1);
			return gy1 - (r + 1) * rh + (rh - bh) * 0.5f;
		};

		// octave guides
		fillRect(vg, gx0, gy1 - sl * rh, gx1 - gx0, 1.f, semi::theme::grid());
		fillRect(vg, gx0, gy1 - 2 * sl * rh, gx1 - gx0, 1.f, semi::theme::grid());

		const float bw = std::max(1.8f, cw - 1.2f);
		for (int pass = 0; pass < 4; pass++) {  // 0 rests, 1 notes, 2 accents, 3 slides
			nvgBeginPath(vg);
			bool any = false;
			for (int i = 0; i < cols; i++) {
				const auto& st = s.steps[i];
				const float x = gx0 + i * cw + 0.6f;
				const bool in = i < s.length;
				if (!in)
					continue;
				if (pass == 0 && !s.plays[i]) {
					nvgRect(vg, x, gy1 - 2.f, bw, 2.f);
					any = true;
				}
				else if (pass == 1 && s.plays[i] && !st.accent) {
					nvgRect(vg, x, yOf(st), bw, bh);
					any = true;
				}
				else if (pass == 2 && s.plays[i] && st.accent) {
					nvgRect(vg, x, yOf(st), bw, bh);
					any = true;
				}
				else if (pass == 3 && s.plays[i] && st.slide && i + 1 < s.length) {
					// the glide: a line from this note's end into the next note
					nvgMoveTo(vg, x + bw, yOf(st) + bh * 0.5f);
					nvgLineTo(vg, x + cw, yOf(s.steps[i + 1]) + bh * 0.5f);
					any = true;
				}
			}
			if (any && pass == 3) {
				nvgStrokeColor(vg, semi::theme::amber());
				nvgStrokeWidth(vg, semi::platform::kStroke);
				nvgStroke(vg);
			}
			else if (any) {
				nvgFillColor(vg, pass == 0 ? semi::theme::cyanDim()
				                 : pass == 1 ? semi::theme::cyan()
				                             : semi::theme::magenta());
				nvgFill(vg);
			}
		}
		// beyond LENGTH
		if (s.length < cols)
			fillRect(vg, gx0 + s.length * cw, gy1 - 1.f, (cols - s.length) * cw, 1.f, semi::theme::grid());
		// playhead
		if (s.clock != 0 && s.position >= 0 && s.position < cols)
			fillRect(vg, gx0 + s.position * cw + 0.6f, gy1 + 1.2f, bw, 2.2f, s.armed ? semi::theme::amber() : semi::theme::dim());

		// footer
		static const char* roots[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
		static const char* scales[10] = {"MIN", "MAJ", "DOR", "PHR", "HMIN", "MIX", "mPNT", "MPNT", "BLUE", "CHR"};
		char buf[48];
		std::snprintf(buf, sizeof buf, "D%+d %s %s %04X", s.density, roots[clamp(s.root, 0, 11)], scales[clamp(s.scale, 0, 9)], s.seed);
		const float ty = h - pad;
		text(vg, pad, ty, buf, fs, semi::theme::text(), NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
		NVGcolor sc = semi::theme::dim();
		if (s.clock == 0)
			std::snprintf(buf, sizeof buf, "NO CLOCK");
		else if (s.playMode != 0 && !s.armed)
			std::snprintf(buf, sizeof buf, "FILL");
		else {
			std::snprintf(buf, sizeof buf, "%s %d", s.clock == 2 ? "INT" : "CLK", (int) std::lround(s.bpm));
			sc = semi::theme::cyan();
		}
		if (s.pendingRecall >= 0) {
			std::snprintf(buf, sizeof buf, "BANK %d>", s.pendingRecall + 1);
			sc = semi::theme::amber();
		}
		text(vg, w - pad, ty, buf, fs, sc, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
	}
};

struct AcidWidget : ModuleWidget {
	AcidWidget(Acid* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Acid.svg")));

		auto* scr = semi::createScreenMm<GridScreen>(Vec(L::GRID.x, L::GRID.y), Vec(L::GRID.w, L::GRID.h));
		scr->module = module;
		addChild(scr);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		auto* dens = createParamCentered<semi::KnobM>(at(L::DENSITY), module, Acid::DENSITY_PARAM);
		dens->snap = true;
		addParam(dens);
		addParam(createParamCentered<semi::KnobS>(at(L::ACC_ODDS), module, Acid::ACC_ODDS_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::SLIDE_ODDS), module, Acid::SLIDE_ODDS_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::OCT_ODDS), module, Acid::OCT_ODDS_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::NEW), module, Acid::NEW_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::MUTATE), module, Acid::MUTATE_PARAM));
		addParam(createParamCentered<semi::KnobL>(at(L::CUTOFF), module, Acid::CUTOFF_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::RESO), module, Acid::RESO_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::ENVMOD), module, Acid::ENVMOD_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::DECAY), module, Acid::DECAY_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::ACCENT), module, Acid::ACCENT_PARAM));
		addParam(createParamCentered<semi::KnobMHot>(at(L::DRIVE), module, Acid::DRIVE_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::GATE), module, Acid::GATE_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::LEVEL), module, Acid::LEVEL_PARAM));

		addInput(createInputCentered<semi::Jack>(at(L::CLK), module, Acid::CLK_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RST), module, Acid::RST_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::VOCT_IN), module, Acid::VOCT_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::FILL), module, Acid::FILL_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::CUT_CV), module, Acid::CUT_INPUT));

		addOutput(createOutputCentered<semi::Jack>(at(L::OUT), module, Acid::OUT_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::VOCT_OUT), module, Acid::VOCT_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::GATE_OUT), module, Acid::GATE_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::ACC_OUT), module, Acid::ACC_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::SLIDE_OUT), module, Acid::SLIDE_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Acid*>(module);
		if (!m)
			return;
		auto idx = [=](const char* name, const std::vector<std::string>& labels, int* v) {
			return createIndexSubmenuItem(name, labels, [=]() { return (size_t) *v; }, [=](size_t i) { *v = (int) i; });
		};
		auto pct = [=](const char* name, float* v) {
			return createIndexSubmenuItem(name, kPct, [=]() { return (size_t) clamp((int) std::lround(*v * 10.f), 0, 10); },
			                              [=](size_t i) { *v = i / 10.f; });
		};
		std::vector<std::string> scales;
		for (int i = 0; i < acid::tb3poNumScales; i++)
			scales.push_back(acid::tb3poScales[i].name);
		std::vector<std::string> octs = {"0", "1", "2", "3", "4", "5"};
		std::vector<std::string> trans;
		for (int t = -24; t <= 24; t++)
			trans.push_back((t > 0 ? "+" : "") + std::to_string(t));
		std::vector<std::string> lens;
		for (int l = 1; l <= kSteps; l++)
			lens.push_back(std::to_string(l));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Line"));
		menu->addChild(idx("Scale", scales, &m->scale));
		menu->addChild(idx("Root", kRootNames, &m->root));
		menu->addChild(idx("Octave", octs, &m->octave));
		menu->addChild(createIndexSubmenuItem("Transpose", trans, [=]() { return (size_t) (m->transpose + 24); },
		                                      [=](size_t i) { m->transpose = (int) i - 24; }));
		menu->addChild(createIndexSubmenuItem("Length", lens, [=]() { return (size_t) (m->length - 1); },
		                                      [=](size_t i) { m->length = (int) i + 1; }));
		menu->addChild(idx("Direction", kDirNames, &m->direction));
		menu->addChild(createBoolPtrMenuItem("Live shape (odds knobs reshape the line now)", "", &m->live));
		menu->addChild(idx("Auto", kAutoModes, &m->autoMode));
		menu->addChild(idx("Auto every", kAutoBars, &m->autoBarsIdx));
		menu->addChild(createMenuItem("Undo", "", [=]() { m->cmd = CMD_UNDO; }));
		menu->addChild(createSubmenuItem("Store in bank", "", [=](Menu* sub) {
			for (int b = 0; b < kBanks; b++)
				sub->addChild(createMenuItem("Bank " + std::to_string(b + 1), "", [=]() { m->cmd = CMD_STORE | (b << 4); }));
		}));
		menu->addChild(createSubmenuItem("Recall bank (next bar while running)", "", [=](Menu* sub) {
			for (int b = 0; b < kBanks; b++)
				sub->addChild(createMenuItem("Bank " + std::to_string(b + 1), "", [=]() { m->cmd = CMD_RECALL | (b << 4); }));
		}));

		menu->addChild(createMenuLabel("Fill (play mode)"));
		menu->addChild(idx("Play", kPlayModes, &m->playMode));
		menu->addChild(idx("Trigger length", kTrigLens, &m->trigLenIdx));
		menu->addChild(createBoolPtrMenuItem("Fill restarts at step 1", "", &m->retrig));

		menu->addChild(createMenuLabel("303"));
		menu->addChild(idx("Waveform", {"Saw", "Square"}, &m->waveform));
		menu->addChild(idx("Drive model", {"Soft", "RAT"}, &m->driveModel));
		menu->addChild(pct("Drive mix", &m->driveMix));
		menu->addChild(pct("Volume", &m->volume));
		menu->addChild(pct("Tuning (400-480 Hz)", &m->tuning));
		menu->addChild(createBoolPtrMenuItem("Devilfish", "", &m->devil));
		menu->addChild(createSubmenuItem("Devilfish controls", "", [=](Menu* sub) {
			sub->addChild(pct("Normal decay", &m->normalDecay));
			sub->addChild(pct("Accent decay", &m->accentDecay));
			sub->addChild(pct("Feedback HPF", &m->fbHpf));
			sub->addChild(pct("Soft attack", &m->softAttack));
			sub->addChild(pct("Slide time", &m->slideTime));
			sub->addChild(pct("Shaper drive", &m->shaper));
		}));
		menu->addChild(createSubmenuItem("Random LFOs", "", [=](Menu* sub) {
			for (int k = 0; k < 5; k++) {
				const std::string n = kLfoTargetNames[k];
				sub->addChild(createMenuLabel(n));
				sub->addChild(idx((n + " depth").c_str(), kPct, &m->lfo[k].depth));
				sub->addChild(idx((n + " mode").c_str(), kLfoModes, &m->lfo[k].mode));
				sub->addChild(idx((n + " rate").c_str(), kLfoRates, &m->lfo[k].rate));
				sub->addChild(idx((n + " smooth").c_str(), kSmoothNames, &m->lfo[k].smooth));
			}
		}));

		menu->addChild(createMenuLabel("Clock"));
		menu->addChild(idx("CLK resolution", kClockNames, &m->clockIdx));
		menu->addChild(idx("Internal tempo (CLK unpatched)", kTempoNames, &m->tempoIdx));
	}
};

Model* modelAcid = createModel<Acid, AcidWidget>("Acid");
