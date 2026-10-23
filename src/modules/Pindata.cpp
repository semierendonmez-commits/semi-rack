// PINDATA — Rack / MetaModule port of PINDATA (IKEDAR × tst_ingp × JDAT played by the Pinball 2
// Markov sequencer, fed by any file). Engine: engines/pindata, vendored from
// semierendonmez-commits/pindata Source/engine (namespace pnd) plus engines/pindata/Hybrid.h, the
// plugin's processBlock without JUCE.
//
// Panel:   VISUAL screen (eight datamatic scenes: matrix, network, field, automata, buffers, barcode,
//          numbers, spectra) and the INFO strip; RUN / RST (manual reset) / HALT / SCN.
//          M row: MULT (time multiplier), SWING, PATTERN (bars), SCAN (hold/step/run/region),
//          STEP (step size), RATE (scan rows/s). S row: IKD / INGP / JDAT / MASTER levels, INGP SMOOTH
//          and TUNE, JDAT CLEAR chance, IKD MUTATE chance.
// Jacks:   CLK (1/16, PPQN in the menu), RST (relaunch + transport to bar 1), FIRE (poly: channel k
//          fires lane k, mono fires lane 1 — the plugin's "MIDI in fires lanes"), HEAD (0-10 V = read
//          head over the file), MULT CV (+-5 V = /4..x4), SCENE CV (0-10 V).
//          Out: L / R (+-5 V), the lanes' notes as GATE / PITCH (1 V/oct, C4 = 0 V) / VEL (0-10 V),
//          STEP (ball's step, 1/12 V per step), BUS (FamilyBus CV: POS, ENERGY, SCENE, EVENT on the
//          pattern resets).
// Menu:    data file (any file; WAV decoded for READ AS audio), the nine example texts, READ AS, factory
//          presets, clock, Pinball functions and matrix shapes, the eight lanes (destination, note,
//          choke), IKD / INGP / JDAT pages, mix switches, scene change.
// Dropped: MIDI ports (CV/gate instead), the GPU renderer, FPS / theme / window, image decoding
//          (images are read as bytes), user preset files (the patch keeps everything), the 3 scene variants.

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/FamilyBusCV.hpp"
#include "../common/Sample.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/PindataLayout.hpp"
#include "pindata/ExampleTexts.h"
#include "pindata/Hybrid.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace L = semi::layout::pindata;
using semi::Screen;

namespace {

constexpr int kBlock = 32;          // engine block; adds 32 samples of latency
constexpr float kVoltsPerUnit = 5.f;
#ifdef METAMODULE
constexpr size_t kMaxFileBytes = 256u * 1024u;     // MetaModule RAM is shared by the whole patch
constexpr size_t kMaxAudioFrames = 48000u * 2u;
#else
constexpr size_t kMaxFileBytes = 8u * 1024u * 1024u;
constexpr size_t kMaxAudioFrames = 48000u * 60u;
#endif

constexpr int kScenes = 8;
const char* const kSceneNames[kScenes] = {"MATRIX", "NETWORK", "FIELD", "AUTOMATA", "BUFFERS", "BARCODE", "NUMBERS", "SPECTRA"};
const float kPatternBars[] = {0.5f, 1.f, 2.f, 4.f, 8.f, 16.f, 32.f, 64.f};
constexpr int kNumPatterns = 8;
const int kPpqn[] = {1, 2, 4, 8, 24};
const float kBpms[] = {60.f, 90.f, 110.f, 120.f, 128.f, 140.f, 160.f, 174.f};

int patternIndex(double bars) {
	int best = 0;
	for (int i = 0; i < kNumPatterns; i++)
		if (std::fabs(kPatternBars[i] - bars) < std::fabs(kPatternBars[best] - bars))
			best = i;
	return best;
}

std::vector<std::string> names(std::initializer_list<const char*> l) {
	std::vector<std::string> v;
	for (auto* s : l)
		v.push_back(s);
	return v;
}

std::string noteName(int n) {
	static const char* const k[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	return string::f("%s%d", k[((n % 12) + 12) % 12], n / 12 - 1);
}

std::string hex64(uint64_t v) {
	char b[20];
	std::snprintf(b, sizeof b, "%08llX", (unsigned long long) v);
	return b;
}

// ---------------------------------------------------------------- snapshot for the screens
struct Snapshot {
	int scene = 0;
	bool flash = false, playing = false, clocked = false, running = true, halted = false;
	float bpm = 120.f;
	int sixteenth = 0;
	// pinball
	int step = -1, lastFrom = -1, lastTo = -1;
	int8_t path[16]{};
	int pathN = 0;
	uint8_t matrix[17][17]{};
	int8_t trig[16]{};
	uint8_t vel[16]{};
	uint8_t laneFlash = 0;   // lanes hit since the last frame
	uint16_t stepFlash = 0;
	// layers
	uint64_t ikdCells[9]{};
	float ikdEnv[9]{};
	int ikdStep = -1;
	uint64_t field[32]{};
	uint32_t gates = 0;
	int8_t rowStep[32]{};
	int cursorX = -1, cursorY = -1;
	int8_t jbuf[3][64]{};
	float jgate[3]{};
	float jphase = 0.f;
	float rmsIk = 0.f, rmsIg = 0.f, rmsJd = 0.f, rmsOut = 0.f;
	float scope[64]{};
	// data
	bool haveData = false;
	char name[40]{}, type[24]{}, note[40]{};
	uint64_t head = 0, nbits = 0, headByte = 0, fileSize = 0;
	float entropy = 0.f;
	uint8_t entMap[64]{};
	uint8_t hex[64]{};
	uint64_t bits[4]{};        // 256 bits under the head
	char str[2][30]{};
};

// One data load: a DataBlob built off the audio thread.
using Blob = pnd::DataBlob;

} // namespace

struct Pindata : Module {
	enum ParamId { RUN_PARAM, RESET_PARAM, HALT_PARAM, SCENE_PARAM,
	               MULT_PARAM, SWING_PARAM, PATTERN_PARAM, SCAN_PARAM, STEPSIZE_PARAM, RATE_PARAM,
	               IKD_PARAM, INGP_PARAM, JDAT_PARAM, MASTER_PARAM, SMOOTH_PARAM, TUNE_PARAM, CLEAR_PARAM, CHANCE_PARAM,
	               PARAMS_LEN };
	enum InputId { CLK_INPUT, RST_INPUT, FIRE_INPUT, HEAD_INPUT, MULT_INPUT, SCENE_INPUT, INPUTS_LEN };
	enum OutputId { L_OUTPUT, R_OUTPUT, GATE_OUTPUT, PITCH_OUTPUT, VEL_OUTPUT, STEP_OUTPUT, BUS_OUTPUT, OUTPUTS_LEN };
	enum LightId { RUN_LIGHT, HALT_LIGHT, LIGHTS_LEN };

	pnd::Hybrid engine;
	pnd::Settings cfg;              // hidden state (menus); the panel overrides its knob fields per block

	semi::Slot<Blob> slot;
	semi::Loader<Blob> loader;
	std::atomic<int> readAs{0};     // pdata::ReadAs, used by the loader

	// module state (json)
	bool running = true;
	int scene = 0, sceneAuto = 3, ppqnIdx = 2, bpmIdx = 3, autoEveryIdx = 3, factoryIndex = 0;
	bool flashOn = true;

	// requests from the UI thread
	std::atomic<int> factoryRequest{-1}, fnRequest{-1}, ikdFnRequest{-1}, ingpFnRequest{-1}, jdatFnRequest{-1};
	std::atomic<int64_t> headRequest{-1};

	// audio block
	float outBuf[2][kBlock]{};
	int pos = 0;
	double preparedSr = 0.0;

	// transport
	dsp::SchmittTrigger clkTrig, rstTrig, fireTrig[8];
	dsp::BooleanTrigger runBtn, resetBtn, haltBtn, sceneBtn;
	int64_t sampleCount = 0, lastEdge = -1;
	double clockPeriod = 0.0;
	int64_t edges = 0;
	double freePpq = 0.0;
	double ppqNow = 0.0;

	// outputs
	int gateLane = -1;
	uint32_t seenSerial[pnd::pinball::kLanes]{};
	bool retrig = false;
	float pitchV = 0.f, velV = 0.f;
	dsp::PulseGenerator eventPulse;
	float eventVolts = 0.f;
	uint32_t seenPatternEvents = 0;

	// visuals
	int lastBar = -1, lastHeadCv = -1;
	uint32_t seenCuts = 0, seenFlashes = 0, seenFires = 0, seenLaneHits[8]{};
	float flashLeft = 0.f;
	uint8_t laneFlashAcc = 0;
	uint16_t stepFlashAcc = 0;
	float scopeRing[256]{};
	int scopePos = 0;

	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;

	Pindata() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configButton(RUN_PARAM, "Run (internal transport / follow CLK)");
		configButton(RESET_PARAM, "Manual reset (the ball back to the starting step)");
		configButton(HALT_PARAM, "Halt (the ball stops until the next reset)");
		configButton(SCENE_PARAM, "Next scene");
		configParam(MULT_PARAM, -2.f, 2.f, 0.f, "Time multiplier", "x", 2.f);
		configParam(SWING_PARAM, 0.f, 100.f, 0.f, "Swing", " %");
		std::vector<std::string> pat;
		for (float b : kPatternBars)
			pat.push_back(b < 1.f ? "1/2 bar" : string::f("%g bar%s", b, b == 1.f ? "" : "s"));
		configSwitch(PATTERN_PARAM, 0.f, kNumPatterns - 1, 1.f, "Pattern length", pat);
		configSwitch(SCAN_PARAM, 0.f, 3.f, 0.f, "Scan", {"Hold", "Step", "Run", "Region"});
		configSwitch(STEPSIZE_PARAM, 0.f, 5.f, 2.f, "Step size", {"1 bit", "1 byte", "1 row", "CELLULA block", "PALIMPSEST field", "1 kbyte"});
		configParam(RATE_PARAM, std::log2(0.1f), 6.f, 1.f, "Scan rate", " rows/s", 2.f);
		configParam(IKD_PARAM, -60.f, 6.f, 0.f, "CELLULA level", " dB");
		configParam(INGP_PARAM, -60.f, 6.f, 0.f, "PALIMPSEST level", " dB");
		configParam(JDAT_PARAM, -60.f, 6.f, -3.f, "DATUM level", " dB");
		configParam(MASTER_PARAM, -60.f, 6.f, -3.f, "Master", " dB");
		configParam(SMOOTH_PARAM, 0.f, std::log2(2000.f), std::log2(15.f), "PALIMPSEST smooth", " ms", 2.f);
		configParam(TUNE_PARAM, -24.f, 24.f, 0.f, "PALIMPSEST tune", " st");
		configParam(CLEAR_PARAM, 0.f, 100.f, 40.f, "DATUM clear chance", " %");
		configParam(CHANCE_PARAM, 0.f, 100.f, 0.f, "CELLULA auto mutate chance", " %");
		paramQuantities[MULT_PARAM]->description = "Every step time and delay of the ball (x0.25 .. x4)";
		paramQuantities[PATTERN_PARAM]->description = "Every N bars the ball goes back to the starting step";
		paramQuantities[SCAN_PARAM]->description = "How the read head moves: hold, a STEP per ball step, RUN at RATE, each step its REGION";
		paramQuantities[STEPSIZE_PARAM]->description = "One move of the read head (scan step and the data step lanes)";
		paramQuantities[RATE_PARAM]->description = "Scan run: rows of 64 bits per second";
		paramQuantities[CLEAR_PARAM]->description = "Chance per DATUM tick that the three buffers are cleared";
		paramQuantities[CHANCE_PARAM]->description = "Every N CELLULA steps (menu) an unlocked voice mutates with this chance; 0 = off";
		configInput(CLK_INPUT, "Clock (1/16 by default)");
		configInput(RST_INPUT, "Reset (ball relaunch, transport to bar 1)");
		configInput(FIRE_INPUT, "Fire lanes (poly: channel k = lane k; mono = lane 1)");
		configInput(HEAD_INPUT, "Read head (0-10 V over the file)");
		configInput(MULT_INPUT, "Time multiplier CV (+-5 V = /4 .. x4)");
		configInput(SCENE_INPUT, "Scene CV (0-10 V)");
		configOutput(L_OUTPUT, "Left");
		configOutput(R_OUTPUT, "Right");
		configOutput(GATE_OUTPUT, "Lane gate (note length = step duration)");
		configOutput(PITCH_OUTPUT, "Lane note (1 V/oct, C4 = 0 V)");
		configOutput(VEL_OUTPUT, "Lane velocity (0-10 V)");
		configOutput(STEP_OUTPUT, "Ball step (1/12 V per step, step 1 = 0 V)");
		configOutput(BUS_OUTPUT, "FamilyBus (POS, ENERGY, SCENE, EVENT on pattern resets)");

		loader.attach(this, &slot, [this](const std::string& path, Blob& b) { return buildBlob(path, b); });
		engine.prepare(48000.0);
		preparedSr = 48000.0;
		applyFactory(0);
		loader.run("example:1");     // something to read before a file is loaded (text, no disk)
	}

	// ---- data ---------------------------------------------------------------------------

	// Loader thread (Rack: the UI thread; MetaModule: the async thread). No exceptions, every read checked.
	bool buildBlob(const std::string& path, Blob& b) {
		const auto as = (pnd::pdata::ReadAs) clamp(readAs.load(), 0, 4);
		if (path.rfind("example:", 0) == 0) {
			const int k = clamp(std::atoi(path.c_str() + 8), 1, pnd::examples::kCount);
			pnd::pdata::fromText(b, pnd::examples::text(k), string::f("example %d - %s", k, pnd::examples::name(k)), as);
			return true;
		}
		std::vector<uint8_t> bytes;
		if (!semi::readFileBytes(path, bytes, kMaxFileBytes))
			return false;
		const std::string name = semi::fileName(path);
		pnd::pdata::setBytes(b, bytes.data(), bytes.size(), kMaxFileBytes, name, path);
		{
			// the real size (readFileBytes stops at the cap)
			FILE* f = std::fopen(path.c_str(), "rb");
			if (f) {
				std::fseek(f, 0, SEEK_END);
				const long sz = std::ftell(f);
				std::fclose(f);
				if (sz > 0) {
					b.fileSize = (uint64_t) sz;
					b.truncated = b.fileSize > b.bytes.size();
				}
			}
		}
		// WAV decodes for READ AS auto / audio; other audio formats are read as bytes
		const bool isWav = bytes.size() > 12 && !std::memcmp(bytes.data(), "RIFF", 4) && !std::memcmp(bytes.data() + 8, "WAVE", 4);
		if (isWav && (as == pnd::pdata::ReadAs::Auto || as == pnd::pdata::ReadAs::Audio)) {
			semi::Sample s;
			if (semi::readWav(path, s, kMaxAudioFrames)) {
				std::vector<float> mono(s.frames);
				for (size_t i = 0; i < s.frames; i++)
					mono[i] = s.mono(i);
				pnd::pdata::build(b, as, &mono, s.rate, s.channels);
				return true;
			}
		}
		pnd::pdata::build(b, as);
		return true;
	}

	void loadPath(const std::string& path) { loader.request(path); }
	void reload() {
		if (!loader.lastPath.empty())
			loader.request(loader.lastPath);
	}

	// ---- factory presets (audio thread or constructor) ---------------------------------
	void applyFactory(int index) {
		factoryIndex = clamp(index, 0, pnd::kNumFactory - 1);
		const int ra = cfg.readAs;
		engine.loadFactory(factoryIndex, cfg);
		cfg.readAs = ra;
		params[MULT_PARAM].setValue((float) std::log2(cfg.mult));
		params[SWING_PARAM].setValue((float) cfg.swing * 100.f);
		params[PATTERN_PARAM].setValue((float) patternIndex(cfg.patternBars));
		params[SCAN_PARAM].setValue((float) cfg.scan);
		params[STEPSIZE_PARAM].setValue((float) cfg.stepSize);
		params[RATE_PARAM].setValue(std::log2(cfg.scanRate));
		params[IKD_PARAM].setValue(cfg.lvlIkdDb);
		params[INGP_PARAM].setValue(cfg.lvlIngpDb);
		params[JDAT_PARAM].setValue(cfg.lvlJdatDb);
		params[MASTER_PARAM].setValue(cfg.masterDb);
		params[SMOOTH_PARAM].setValue(std::log2(std::max(1.f, cfg.ingpSmooth)));
		params[TUNE_PARAM].setValue(cfg.ingpTune);
		params[CLEAR_PARAM].setValue((float) cfg.jdatClear);
		params[CHANCE_PARAM].setValue(cfg.ikdAuto > 0 ? (float) cfg.ikdChance : 0.f);
		if (cfg.ikdAuto > 0)
			autoEveryIdx = cfg.ikdAuto;
		engine.requestReset();
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		engine.prepare(e.sampleRate);
		preparedSr = e.sampleRate;
		pos = 0;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		running = true;
		scene = 0;
		factoryRequest.store(0);
	}

	// ---- json -----------------------------------------------------------------------------
	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "path", json_string(loader.lastPath.c_str()));
		json_object_set_new(root, "readAs", json_integer(readAs.load()));
		json_object_set_new(root, "running", json_boolean(running));
		json_object_set_new(root, "scene", json_integer(scene));
		json_object_set_new(root, "sceneAuto", json_integer(sceneAuto));
		json_object_set_new(root, "flash", json_boolean(flashOn));
		json_object_set_new(root, "ppqn", json_integer(ppqnIdx));
		json_object_set_new(root, "bpm", json_integer(bpmIdx));
		json_object_set_new(root, "autoEvery", json_integer(autoEveryIdx));
		json_object_set_new(root, "factory", json_integer(factoryIndex));

		const pnd::Settings& s = cfg;
		json_t* c = json_object();
		auto i = [&](const char* k, int v) { json_object_set_new(c, k, json_integer(v)); };
		auto f = [&](const char* k, double v) { json_object_set_new(c, k, json_real(v)); };
		auto b = [&](const char* k, bool v) { json_object_set_new(c, k, json_boolean(v)); };
		b("clip", s.clip); b("limit", s.limit); b("dcBlock", s.dcBlock); i("velDepth", s.velDepth); b("laneOut", s.laneOut);
		i("startStep", s.startStep);
		i("ikdClk", s.ikdClk); i("ikdDiv", s.ikdDiv); i("ikdFeed", s.ikdFeed); f("ikdSwing", s.ikdSwing);
		f("ikdWave", s.ikdWaveDb); f("ikdSpec", s.ikdSpecDb); f("ikdSine", s.ikdSineDb); b("ikdHold", s.ikdHold); b("ikdFeedSnd", s.ikdFeedSnd);
		i("ingpClk", s.ingpClk); i("ingpDiv", s.ingpDiv); i("ingpGens", s.ingpGens); i("ingpWalk", s.ingpWalk); i("ingpFeed", s.ingpFeed);
		f("ingpBpm", s.ingpBpm); f("ingpTilt", s.ingpTilt); f("ingpWidth", s.ingpWidth); f("ingpSwing", s.ingpSwing);
		b("ingpGen", s.ingpGen); b("ingpFeedBanks", s.ingpFeedBanks);
		i("jdatClk", s.jdatClk); i("jdatDiv", s.jdatDiv); i("jdatJump", s.jdatJump); i("jdatInterp", s.jdatInterp);
		i("jdatWrite", s.jdatWrite); i("jdatFeed", s.jdatFeed); i("jdatLoop", s.jdatLoop); f("jdatRate", s.jdatRate);
		b("jdatLink", s.jdatLink); b("jdatHold", s.jdatHold);
		json_t* a = json_array();
		for (int k = 0; k < 4; k++) {
			json_array_append_new(a, json_integer(s.jdatSrc[(size_t) k]));
			json_array_append_new(a, json_real(s.jdatLvlDb[(size_t) k]));
		}
		json_object_set_new(c, "jdatSrcLvl", a);
		json_object_set_new(root, "cfg", c);

		json_t* lanes = json_array();
		for (int l = 0; l < pnd::pinball::kLanes; l++) {
			json_t* e = json_array();
			json_array_append_new(e, json_integer(s.laneNote[(size_t) l]));
			json_array_append_new(e, json_integer(s.laneDest[(size_t) l]));
			json_array_append_new(e, json_boolean(s.laneChoke[(size_t) l]));
			json_array_append_new(lanes, e);
		}
		json_object_set_new(root, "lanes", lanes);

		json_t* steps = json_array();
		for (const auto& p : engine.pinball.steps) {
			json_t* e = json_array();
			for (double v : {(double) p.trig, (double) p.velocity, (double) p.trigProb, p.duration, p.delay, (double) p.delayProb})
				json_array_append_new(e, json_real(v));
			json_array_append_new(steps, e);
		}
		json_object_set_new(root, "steps", steps);
		json_t* mx = json_array();
		for (auto& row : engine.pinball.matrix)
			for (auto& w : row)
				json_array_append_new(mx, json_integer((int) std::lround(w.load())));
		json_object_set_new(root, "matrix", mx);

		// IKD rows (u64 as hex strings: jansson integers are signed)
		json_t* ik = json_object();
		json_t* master = json_array();
		for (auto& w : engine.ikdShared.master)
			json_array_append_new(master, json_string(hex64(w.load()).c_str()));
		json_object_set_new(ik, "master", master);
		json_t* voices = json_array();
		for (int v = 0; v < pnd::ryoz::kNumVoices; v++) {
			const auto& vs = engine.ikdShared.voices[(size_t) v];
			json_t* e = json_array();
			for (uint64_t w : {vs.pitch.load(), vs.cells.load(), vs.gain.load(), vs.decay.load(), vs.signs.load()})
				json_array_append_new(e, json_string(hex64(w).c_str()));
			json_array_append_new(e, json_integer(vs.rule.load()));
			json_array_append_new(e, json_real(vs.gateBaseMs.load()));
			json_array_append_new(e, json_boolean(engine.ikdLocks[(size_t) v]));
			json_array_append_new(voices, e);
		}
		json_object_set_new(ik, "voices", voices);
		json_object_set_new(root, "ikd", ik);

		// INGP banks + rule
		json_t* ig = json_object();
		json_object_set_new(ig, "rule", json_integer(engine.ingp.rule.load()));
		json_t* banks = json_array();
		for (int v = 0; v < pnd::ingp::V; v++) {
			json_array_append_new(banks, json_real(engine.ingp.freq[(size_t) v].load()));
			json_array_append_new(banks, json_real(engine.ingp.amp[(size_t) v].load()));
			json_array_append_new(banks, json_real(engine.ingp.pan[(size_t) v].load()));
			json_array_append_new(banks, json_integer(engine.ingp.len[(size_t) v].load()));
		}
		json_object_set_new(ig, "banks", banks);
		json_object_set_new(root, "ingp", ig);
		return root;
	}

	static double num(json_t* a, size_t i, double def) {
		json_t* v = json_array_get(a, i);
		return v ? json_number_value(v) : def;
	}
	static uint64_t hexAt(json_t* a, size_t i) {
		json_t* v = json_array_get(a, i);
		const char* s = v ? json_string_value(v) : nullptr;
		return s ? (uint64_t) std::strtoull(s, nullptr, 16) : 0;
	}

	void dataFromJson(json_t* root) override {
		factoryRequest.store(-1);
		auto gi = [&](json_t* o, const char* k, int& v, int lo, int hi) {
			if (json_t* j = json_object_get(o, k))
				v = clamp((int) json_integer_value(j), lo, hi);
		};
		auto gf = [&](json_t* o, const char* k, auto& v, double lo, double hi) {
			if (json_t* j = json_object_get(o, k))
				v = (std::remove_reference_t<decltype(v)>) std::clamp(json_number_value(j), lo, hi);
		};
		auto gb = [&](json_t* o, const char* k, bool& v) {
			if (json_t* j = json_object_get(o, k))
				v = json_is_true(j);
		};
		int ra = readAs.load();
		gi(root, "readAs", ra, 0, 4);
		readAs.store(ra);
		gb(root, "running", running);
		gi(root, "scene", scene, 0, kScenes - 1);
		gi(root, "sceneAuto", sceneAuto, 0, 6);
		gb(root, "flash", flashOn);
		gi(root, "ppqn", ppqnIdx, 0, 4);
		gi(root, "bpm", bpmIdx, 0, 7);
		gi(root, "autoEvery", autoEveryIdx, 1, 7);
		gi(root, "factory", factoryIndex, 0, pnd::kNumFactory - 1);

		pnd::Settings& s = cfg;
		if (json_t* c = json_object_get(root, "cfg")) {
			gb(c, "clip", s.clip); gb(c, "limit", s.limit); gb(c, "dcBlock", s.dcBlock); gi(c, "velDepth", s.velDepth, 0, 100);
			gb(c, "laneOut", s.laneOut); gi(c, "startStep", s.startStep, 1, 16);
			gi(c, "ikdClk", s.ikdClk, 0, 2); gi(c, "ikdDiv", s.ikdDiv, 0, 5); gi(c, "ikdFeed", s.ikdFeed, 0, 2);
			gf(c, "ikdSwing", s.ikdSwing, 0.0, 50.0);
			gf(c, "ikdWave", s.ikdWaveDb, -60.0, 6.0); gf(c, "ikdSpec", s.ikdSpecDb, -60.0, 6.0); gf(c, "ikdSine", s.ikdSineDb, -60.0, 6.0);
			gb(c, "ikdHold", s.ikdHold); gb(c, "ikdFeedSnd", s.ikdFeedSnd);
			gi(c, "ingpClk", s.ingpClk, 0, 2); gi(c, "ingpDiv", s.ingpDiv, 0, 5); gi(c, "ingpGens", s.ingpGens, 0, 8);
			gi(c, "ingpWalk", s.ingpWalk, 0, 1); gi(c, "ingpFeed", s.ingpFeed, 0, 2);
			gf(c, "ingpBpm", s.ingpBpm, 5.0, 300.0); gf(c, "ingpTilt", s.ingpTilt, -1.0, 1.0); gf(c, "ingpWidth", s.ingpWidth, 0.0, 2.0);
			gf(c, "ingpSwing", s.ingpSwing, 0.0, 0.5); gb(c, "ingpGen", s.ingpGen); gb(c, "ingpFeedBanks", s.ingpFeedBanks);
			gi(c, "jdatClk", s.jdatClk, 0, 2); gi(c, "jdatDiv", s.jdatDiv, 0, 5); gi(c, "jdatJump", s.jdatJump, 0, 100);
			gi(c, "jdatInterp", s.jdatInterp, 0, 2); gi(c, "jdatWrite", s.jdatWrite, 0, 3); gi(c, "jdatFeed", s.jdatFeed, 0, 2);
			gi(c, "jdatLoop", s.jdatLoop, 0, 5); gf(c, "jdatRate", s.jdatRate, 10.0, 1000.0);
			gb(c, "jdatLink", s.jdatLink); gb(c, "jdatHold", s.jdatHold);
			if (json_t* a = json_object_get(c, "jdatSrcLvl"))
				for (int k = 0; k < 4; k++) {
					s.jdatSrc[(size_t) k] = clamp((int) num(a, 2 * k, 3), 0, 3);
					s.jdatLvlDb[(size_t) k] = (float) std::clamp(num(a, 2 * k + 1, 0.0), -60.0, 6.0);
				}
		}
		if (json_t* lanes = json_object_get(root, "lanes"))
			for (int l = 0; l < pnd::pinball::kLanes && l < (int) json_array_size(lanes); l++) {
				json_t* e = json_array_get(lanes, l);
				s.laneNote[(size_t) l] = clamp((int) num(e, 0, 36 + l), 0, 127);
				s.laneDest[(size_t) l] = clamp((int) num(e, 1, pnd::kDefaultDest[l]), 0, pnd::dNumDests - 1);
				json_t* ch = json_array_get(e, 2);
				s.laneChoke[(size_t) l] = ch ? json_is_true(ch) : true;
			}
		if (json_t* steps = json_object_get(root, "steps"))
			for (int k = 0; k < pnd::pinball::kSteps && k < (int) json_array_size(steps); k++) {
				json_t* e = json_array_get(steps, k);
				auto& p = engine.pinball.steps[(size_t) k];
				p.trig = clamp((int) num(e, 0, 0), 0, 9);
				p.velocity = clamp((int) num(e, 1, 100), 0, 127);
				p.trigProb = clamp((int) num(e, 2, 100), 0, 100);
				p.duration = std::clamp(num(e, 3, 5.0), 0.0, 64.0);
				p.delay = std::clamp(num(e, 4, 0.0), 0.0, 64.0);
				p.delayProb = clamp((int) num(e, 5, 100), 0, 100);
			}
		if (json_t* mx = json_object_get(root, "matrix"))
			if (json_array_size(mx) == 17 * 17)
				for (int x = 0; x < 17; x++)
					for (int y = 0; y < 17; y++)
						engine.pinball.matrix[(size_t) x][(size_t) y].store((float) std::clamp(num(mx, (size_t) (x * 17 + y), 0.0), 0.0, 100.0));
		if (json_t* ik = json_object_get(root, "ikd")) {
			if (json_t* m = json_object_get(ik, "master"))
				for (int w = 0; w < pnd::ryoz::kMasterWords; w++)
					engine.ikdShared.master[(size_t) w].store(hexAt(m, (size_t) w));
			if (json_t* voices = json_object_get(ik, "voices"))
				for (int v = 0; v < pnd::ryoz::kNumVoices && v < (int) json_array_size(voices); v++) {
					json_t* e = json_array_get(voices, v);
					auto& vs = engine.ikdShared.voices[(size_t) v];
					vs.pitch.store(hexAt(e, 0));
					vs.cells.store(hexAt(e, 1));
					vs.gain.store(hexAt(e, 2));
					vs.decay.store(hexAt(e, 3));
					vs.signs.store(hexAt(e, 4));
					vs.rule.store(clamp((int) num(e, 5, 30), 0, 255));
					vs.gateBaseMs.store((float) num(e, 6, 100.0));
					json_t* lk = json_array_get(e, 7);
					engine.ikdLocks[(size_t) v] = lk && json_is_true(lk);
				}
			engine.ikdShared.loadSerial.fetch_add(1, std::memory_order_release);
		}
		if (json_t* ig = json_object_get(root, "ingp")) {
			if (json_t* r = json_object_get(ig, "rule"))
				engine.ingp.rule.store((uint32_t) json_integer_value(r));
			if (json_t* banks = json_object_get(ig, "banks"))
				for (int v = 0; v < pnd::ingp::V && (size_t) (4 * v + 3) < json_array_size(banks); v++) {
					engine.ingp.freq[(size_t) v].store((float) num(banks, (size_t) (4 * v), 0.5));
					engine.ingp.amp[(size_t) v].store((float) num(banks, (size_t) (4 * v + 1), 0.5));
					engine.ingp.pan[(size_t) v].store((float) num(banks, (size_t) (4 * v + 2), 0.5));
					engine.ingp.len[(size_t) v].store(clamp((int) num(banks, (size_t) (4 * v + 3), 64), 1, 64));
				}
		}
		if (json_t* p = json_object_get(root, "path")) {
			const char* s2 = json_string_value(p);
			if (s2 && *s2)
				loader.request(s2);
		}
	}

	// ---- audio ----------------------------------------------------------------------------

	pnd::Settings blockSettings() {
		pnd::Settings s = cfg;
		s.run = running;
		const float multCv = inputs[MULT_INPUT].getVoltage();
		s.mult = std::clamp((double) std::exp2(params[MULT_PARAM].getValue() + multCv * 0.4f), 0.25, 4.0);
		s.swing = params[SWING_PARAM].getValue() * 0.01;
		s.patternBars = kPatternBars[clamp((int) std::round(params[PATTERN_PARAM].getValue()), 0, kNumPatterns - 1)];
		s.scan = clamp((int) std::round(params[SCAN_PARAM].getValue()), 0, 3);
		s.stepSize = clamp((int) std::round(params[STEPSIZE_PARAM].getValue()), 0, 5);
		s.scanRate = std::exp2(params[RATE_PARAM].getValue());
		s.lvlIkdDb = params[IKD_PARAM].getValue();
		s.lvlIngpDb = params[INGP_PARAM].getValue();
		s.lvlJdatDb = params[JDAT_PARAM].getValue();
		s.masterDb = params[MASTER_PARAM].getValue();
		const float sm = params[SMOOTH_PARAM].getValue();
		s.ingpSmooth = sm <= 0.01f ? 0.f : std::exp2(sm);
		s.ingpTune = params[TUNE_PARAM].getValue();
		s.jdatClear = (int) std::round(params[CLEAR_PARAM].getValue());
		const int chance = (int) std::round(params[CHANCE_PARAM].getValue());
		s.ikdChance = chance;
		s.ikdAuto = chance > 0 ? autoEveryIdx : 0;
		s.readAs = readAs.load();
		return s;
	}

	void runRequests() {
		if (int f = factoryRequest.exchange(-1); f >= 0)
			applyFactory(f);
		if (int f = fnRequest.exchange(-1); f >= 0)
			engine.pinballFunction((pnd::Hybrid::Fn) f);
		if (int f = ikdFnRequest.exchange(-1); f >= 0) {
			if (f < 3)
				engine.ikdRandomise((pnd::Hybrid::IkdRandom) f);
			else
				engine.ikdMutate(-1);
		}
		if (int f = ingpFnRequest.exchange(-1); f >= 0) {
			if (f == 0)
				engine.ingpRandomRule();
			else if (f == 1)
				engine.ingpRandomBanks();
			else
				engine.ingpLoadOriginal(f - 2, cfg);
			if (f >= 2)
				params[SMOOTH_PARAM].setValue(std::log2(std::max(1.f, cfg.ingpSmooth)));
		}
		if (int f = jdatFnRequest.exchange(-1); f >= 0) {
			if (f == 0)
				engine.jdatRoll(0.6f, cfg);
			else if (f == 1)
				engine.requestJdatClear();
			else
				engine.requestRefeed();
			params[CLEAR_PARAM].setValue((float) cfg.jdatClear);
		}
		if (int64_t h = headRequest.exchange(-1); h >= 0)
			engine.requestHead((uint64_t) h);
	}

	void runBlock(float sampleRate, bool clocked) {
		slot.poll();
		runRequests();
		const Blob* blob = slot.active;

		// HEAD CV: 256 positions over the file
		if (inputs[HEAD_INPUT].isConnected() && blob && blob->nbits > 0) {
			const int q = clamp((int) (inputs[HEAD_INPUT].getVoltage() * 25.6f), 0, 255);
			if (q != lastHeadCv) {
				lastHeadCv = q;
				engine.requestHead((uint64_t) ((double) blob->nbits * q / 256.0) & ~(uint64_t) 63);
			}
		}
		else
			lastHeadCv = -1;

		pnd::Transport t;
		const int ppqn = kPpqn[clamp(ppqnIdx, 0, 4)];
		if (clocked) {
			t.playing = running && lastEdge >= 0 && (double) (sampleCount - lastEdge) < std::max(2.0 * sampleRate, 3.0 * clockPeriod);
			t.bpm = clockPeriod > 0.0 ? std::clamp(60.0 * sampleRate / (clockPeriod * ppqn), 20.0, 400.0) : 120.0;
			double ppq = 0.0;
			if (edges >= 1) {
				const double frac = clockPeriod > 0.0 ? std::min((double) (sampleCount - lastEdge) / clockPeriod, 0.999) : 0.0;
				ppq = ((double) (edges - 1) + frac) / ppqn;
			}
			t.ppq = ppq;
		}
		else {
			t.playing = running;
			t.bpm = kBpms[clamp(bpmIdx, 0, 7)];
			t.ppq = freePpq;
			if (running)
				freePpq += t.bpm / 60.0 / sampleRate * kBlock;
		}
		ppqNow = t.ppq;

		const pnd::Settings s = blockSettings();
		engine.process(blob, outBuf[0], outBuf[1], kBlock, s, t);

		// lanes -> GATE / PITCH / VEL (mono, the newest note wins; a new note retriggers)
		for (int l = 0; l < pnd::pinball::kLanes; l++)
			if (engine.noteSerial[(size_t) l] != seenSerial[l]) {
				seenSerial[l] = engine.noteSerial[(size_t) l];
				if (engine.noteOn[(size_t) l] >= 0) {
					retrig = gateLane >= 0 && engine.noteOn[(size_t) gateLane] >= 0;
					gateLane = l;
					pitchV = (engine.noteOn[(size_t) l] - 60) / 12.f;
					velV = engine.laneVel[(size_t) l] / 127.f * 10.f;
				}
			}
		if (engine.patternEvents != seenPatternEvents) {
			seenPatternEvents = engine.patternEvents;
			const float bars = (float) s.patternBars;
			eventVolts = semi::bus::turnToVolts(bars <= 4.f ? 4 : bars <= 8.f ? 8 : bars <= 16.f ? 16 : 32);
			eventPulse.trigger(semi::bus::kPulseSec);
		}

		// scenes: auto change on the bar, VIS CUT / VIS FLASH lanes
		const int bar = (int) std::floor(t.ppq / 4.0);
		if (t.playing && bar != lastBar) {
			lastBar = bar;
			static const int every[] = {0, 1, 2, 4, 8};
			if (sceneAuto >= 1 && sceneAuto <= 4 && bar % every[sceneAuto] == 0)
				scene = (scene + 1) % kScenes;
			else if (sceneAuto == 6)
				scene = (int) ((uint32_t) (bar * 2654435761u) >> 16) % kScenes;
		}
		if (engine.stepFires != seenFires) {
			seenFires = engine.stepFires;
			if (sceneAuto == 5)
				scene = (scene + 1) % kScenes;
		}
		if (engine.sceneCuts != seenCuts) {
			seenCuts = engine.sceneCuts;
			scene = (scene + 1) % kScenes;
		}
		if (engine.flashes != seenFlashes) {
			seenFlashes = engine.flashes;
			if (flashOn)
				flashLeft = (float) (7.5 / std::max(20.0, t.bpm));   // a 1/32 note
		}
		for (int l = 0; l < 8; l++)
			if (engine.laneHits[(size_t) l] != seenLaneHits[l]) {
				seenLaneHits[l] = engine.laneHits[(size_t) l];
				laneFlashAcc |= (uint8_t) (1u << l);
			}
		if (engine.pinball.currentStep.load() >= 0)
			stepFlashAcc |= (uint16_t) (1u << engine.pinball.currentStep.load());
	}

	void process(const ProcessArgs& args) override {
		if (args.sampleRate != preparedSr) {   // MetaModule may not send the event before the first block
			engine.prepare(args.sampleRate);
			preparedSr = args.sampleRate;
		}
		++sampleCount;

		if (runBtn.process(params[RUN_PARAM].getValue() > 0.f))
			running = !running;
		if (resetBtn.process(params[RESET_PARAM].getValue() > 0.f))
			engine.requestReset();
		if (haltBtn.process(params[HALT_PARAM].getValue() > 0.f))
			engine.requestHalt();
		if (sceneBtn.process(params[SCENE_PARAM].getValue() > 0.f))
			scene = (scene + 1) % kScenes;

		const bool clocked = inputs[CLK_INPUT].isConnected();
		if (clocked && clkTrig.process(inputs[CLK_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (lastEdge >= 0)
				clockPeriod = (double) (sampleCount - lastEdge);
			lastEdge = sampleCount;
			edges++;
		}
		if (rstTrig.process(inputs[RST_INPUT].getVoltage(), 0.1f, 1.f)) {
			edges = 0;
			freePpq = 0.0;
			lastBar = -1;
			engine.requestReset();
		}
		// FIRE: channel k fires lane k
		const int fireCh = std::min(inputs[FIRE_INPUT].getChannels(), 8);
		uint32_t fire = 0;
		for (int c = 0; c < fireCh; c++)
			if (fireTrig[c].process(inputs[FIRE_INPUT].getVoltage(c), 0.1f, 1.f))
				fire |= 1u << c;
		if (fire)
			engine.requestLanes(fire);

		outputs[L_OUTPUT].setVoltage(outBuf[0][pos] * kVoltsPerUnit);
		outputs[R_OUTPUT].setVoltage(outBuf[1][pos] * kVoltsPerUnit);
		scopeRing[scopePos] = 0.5f * (outBuf[0][pos] + outBuf[1][pos]);
		scopePos = (scopePos + 1) & 255;
		if (++pos >= kBlock) {
			pos = 0;
			runBlock(args.sampleRate, clocked);
		}

		const bool gate = gateLane >= 0 && engine.noteOn[(size_t) gateLane] >= 0;
		outputs[GATE_OUTPUT].setVoltage(gate && !(retrig && pos < 16) ? 10.f : 0.f);
		if (retrig && pos >= 16)
			retrig = false;
		outputs[PITCH_OUTPUT].setVoltage(pitchV);
		outputs[VEL_OUTPUT].setVoltage(velV);
		const int st = engine.pinball.currentStep.load();
		outputs[STEP_OUTPUT].setVoltage(st >= 0 ? st / 12.f : 0.f);

		if (outputs[BUS_OUTPUT].isConnected()) {
			outputs[BUS_OUTPUT].setChannels(semi::bus::kChannels);
			outputs[BUS_OUTPUT].setVoltage(semi::bus::posToVolts((float) (ppqNow / 4.0)), semi::bus::kPos);
			outputs[BUS_OUTPUT].setVoltage(clamp(engine.rmsOut * 30.f, 0.f, 10.f), semi::bus::kEnergy);
			outputs[BUS_OUTPUT].setVoltage(semi::bus::sceneToVolts(currentScene()), semi::bus::kScene);
			outputs[BUS_OUTPUT].setVoltage(eventPulse.process(args.sampleTime) ? eventVolts : 0.f, semi::bus::kEvent);
		}

		if (ui.tick(args.sampleTime)) {
			flashLeft = std::max(0.f, flashLeft - 1.f / semi::platform::kUiRateHz);
			lights[RUN_LIGHT].setBrightness(running ? 1.f : 0.f);
			lights[HALT_LIGHT].setBrightness(engine.pinball.isHalted() ? 1.f : 0.f);
			publish(clocked);
		}
	}

	int currentScene() {
		if (inputs[SCENE_INPUT].isConnected())
			return clamp((int) (inputs[SCENE_INPUT].getVoltage() * 0.1f * kScenes), 0, kScenes - 1);
		return scene;
	}

	void publish(bool clocked) {
		Snapshot& s = snap.writeSlot();
		s.scene = currentScene();
		s.flash = flashLeft > 0.f;
		s.running = running;
		s.clocked = clocked;
		s.playing = clocked ? (lastEdge >= 0 && running) : running;
		s.halted = engine.pinball.isHalted();
		s.bpm = clocked && clockPeriod > 0.0 ? (float) (60.0 * preparedSr / (clockPeriod * kPpqn[ppqnIdx])) : kBpms[bpmIdx];
		s.sixteenth = (int) std::floor(ppqNow * 4.0);

		s.step = engine.pinball.currentStep.load();
		s.lastFrom = engine.lastFrom;
		s.lastTo = engine.lastTo;
		s.pathN = engine.pathN;
		for (int k = 0; k < engine.pathN; k++)
			s.path[k] = engine.path[(size_t) k];
		for (int x = 0; x < 17; x++)
			for (int y = 0; y < 17; y++)
				s.matrix[x][y] = (uint8_t) clamp((int) engine.pinball.matrix[(size_t) x][(size_t) y].load(std::memory_order_relaxed), 0, 100);
		for (int k = 0; k < 16; k++) {
			s.trig[k] = (int8_t) engine.pinball.steps[(size_t) k].trig;
			s.vel[k] = (uint8_t) engine.pinball.steps[(size_t) k].velocity;
		}
		s.laneFlash = laneFlashAcc;
		s.stepFlash = stepFlashAcc;
		laneFlashAcc = 0;
		stepFlashAcc = 0;

		for (int v = 0; v < 9; v++) {
			s.ikdCells[v] = engine.ikdShared.voices[(size_t) v].cells.load(std::memory_order_relaxed);
			s.ikdEnv[v] = engine.ikdShared.voices[(size_t) v].envLevel.load(std::memory_order_relaxed);
		}
		s.ikdStep = engine.ikdShared.step.load(std::memory_order_relaxed);
		for (int r = 0; r < 32; r++) {
			uint64_t w = 0;
			for (int c = 0; c < 64; c++)
				if (engine.ingp.cell(r * 64 + c))
					w |= (uint64_t) 1 << c;
			s.field[r] = w;
			s.rowStep[r] = (int8_t) engine.ingp.stepShown[(size_t) r].load(std::memory_order_relaxed);
		}
		s.gates = engine.ingp.gates.load(std::memory_order_relaxed);
		const int ci = engine.ingp.cursorIdx.load(std::memory_order_relaxed);
		s.cursorX = ci >= 0 ? ci % 64 : -1;
		s.cursorY = ci >= 0 ? ci / 64 : -1;
		for (int b = 0; b < 3; b++) {
			for (int k = 0; k < 64; k++) {   // the strongest of every 8 samples: sparse writes stay visible
				int8_t best = 0;
				for (int j = 0; j < 8; j++) {
					const int8_t v = engine.jdat.bufView[(size_t) b][(size_t) (k * 8 + j)].load(std::memory_order_relaxed);
					if (std::abs((int) v) > std::abs((int) best))
						best = v;
				}
				s.jbuf[b][k] = best;
			}
			s.jgate[b] = engine.jdat.gateView[(size_t) b].load(std::memory_order_relaxed);
		}
		s.jphase = engine.jdat.phase25.load(std::memory_order_relaxed);
		s.rmsIk = engine.rmsIkd;
		s.rmsIg = engine.rmsIngp;
		s.rmsJd = engine.rmsJdat;
		s.rmsOut = engine.rmsOut;
		for (int k = 0; k < 64; k++)
			s.scope[k] = scopeRing[(scopePos + k * 4) & 255];

		const Blob* b = slot.active;
		s.haveData = b && b->nbits > 0;
		if (s.haveData) {
			std::snprintf(s.name, sizeof s.name, "%s", b->name.c_str());
			std::snprintf(s.type, sizeof s.type, "%s", b->typeName.c_str());
			std::snprintf(s.note, sizeof s.note, "%s", b->kindNote.c_str());
			s.head = engine.head;
			s.nbits = b->nbits;
			s.headByte = b->byteForBit(engine.head);
			s.fileSize = b->fileSize;
			s.entropy = (float) b->entropy;
			const size_t nb = b->blockEntropy.size();
			for (int k = 0; k < 64; k++)
				s.entMap[k] = nb ? (uint8_t) clamp((int) (b->blockEntropy[(size_t) k * nb / 64] * 255.f), 0, 255) : 0;
			for (int k = 0; k < 64; k++)
				s.hex[k] = b->byteAt(s.headByte + (uint64_t) k);
			for (int w = 0; w < 4; w++)
				s.bits[w] = b->word64(engine.head + (uint64_t) w * 64u);
			// the two printable strings at / after the head byte
			s.str[0][0] = s.str[1][0] = 0;
			if (!b->strings.empty()) {
				auto it = std::lower_bound(b->strings.begin(), b->strings.end(), s.headByte,
				                           [](const pnd::pdata::FoundString& f, uint64_t o) { return f.offset < o; });
				size_t i = (size_t) (it - b->strings.begin());
				for (int k = 0; k < 2; k++)
					std::snprintf(s.str[k], sizeof s.str[k], "%s", b->strings[(i + (size_t) k) % b->strings.size()].text.c_str());
			}
		}
		snap.publish();
	}
};

// ---------------------------------------------------------------- screens

namespace {

NVGcolor laneColor(int l) {
	return l % 2 ? semi::theme::amber() : semi::theme::cyan();
}

// horizontal runs of set bits as rects (a row of a field / automaton / barcode)
void bitRow(NVGcontext* vg, uint64_t w, int nbits, float x, float y, float cw, float h, NVGcolor c) {
	int k = 0;
	while (k < nbits) {
		if (!((w >> k) & 1u)) {
			k++;
			continue;
		}
		int e = k;
		while (e < nbits && ((w >> e) & 1u))
			e++;
		Screen::fillRect(vg, x + k * cw, y, (e - k) * cw - (cw > 2.5f ? 0.6f : 0.f), h, c);
		k = e;
	}
}

} // namespace

// The datamatic picture: one of eight scenes over a HUD (scene, head, step lights, lanes).
// Frames change only with the data (a step, a tick, a head move, a published level).
struct PindataVisualScreen : Screen {
	Pindata* module = nullptr;
	Snapshot demo;

	PindataVisualScreen() {
		demo.scene = 0;
		demo.step = 6;
		demo.lastFrom = 6;
		demo.lastTo = 7;
		for (int x = 0; x < 16; x++)
			demo.matrix[x][x + 1] = 100;
		demo.matrix[4][9] = 40;
		demo.matrix[8][1] = 30;
		demo.matrix[12][5] = 35;
		demo.matrix[16][1] = 60;
		demo.matrix[16][9] = 40;
		const int8_t tr[16] = {1, 3, 2, 1, 3, 5, 2, 1, 3, 4, 2, 6, 1, 9, 7, 8};
		for (int k = 0; k < 16; k++) {
			demo.trig[k] = tr[k];
			demo.vel[k] = (uint8_t) (70 + (k * 37) % 57);
		}
		const int8_t p[6] = {2, 3, 4, 5, 6};
		for (int k = 0; k < 5; k++)
			demo.path[k] = p[k];
		demo.pathN = 5;
		demo.laneFlash = 0x05;
		demo.haveData = true;
		std::snprintf(demo.name, sizeof demo.name, "example 1 - lattice chemistry");
		std::snprintf(demo.type, sizeof demo.type, "TEXT");
		demo.head = 0x0240;
		demo.nbits = 13000;
		demo.entropy = 4.5f;
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		if (s.flash)
			fillRect(vg, 0.f, 0.f, w, h, semi::theme::cyanDeep());

		// header: scene, head, transport
		char buf[48];
		std::snprintf(buf, sizeof buf, "%02d %s", s.scene + 1, kSceneNames[clamp(s.scene, 0, kScenes - 1)]);
		text(vg, 4.f, 2.f, buf, 10.f, semi::theme::cyan());
		std::snprintf(buf, sizeof buf, "HEAD %s", hex64(s.head).c_str());
		text(vg, w - 4.f, 2.f, buf, 9.f, semi::theme::text(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);

		const float x0 = 4.f, y0 = 15.f, x1 = w - 4.f, y1 = h - 13.f;
		switch (clamp(s.scene, 0, kScenes - 1)) {
			case 0: drawMatrix(vg, s, x0, y0, x1, y1); break;
			case 1: drawNetwork(vg, s, x0, y0, x1, y1); break;
			case 2: drawField(vg, s, x0, y0, x1, y1); break;
			case 3: drawAutomata(vg, s, x0, y0, x1, y1); break;
			case 4: drawBuffers(vg, s, x0, y0, x1, y1); break;
			case 5: drawBarcode(vg, s, x0, y0, x1, y1); break;
			case 6: drawNumbers(vg, s, x0, y0, x1, y1); break;
			default: drawSpectra(vg, s, x0, y0, x1, y1); break;
		}

		// footer HUD: 16 step lights, 8 lane lights
		const float fy = h - 9.f, cw = 7.f;
		for (int k = 0; k < 16; k++) {
			const bool cur = k == s.step;
			NVGcolor c = cur ? semi::theme::magenta() : (s.stepFlash >> k & 1) ? semi::theme::cyanDim() : semi::theme::grid();
			fillRect(vg, 4.f + k * (cw + 1.5f), fy, cw, 6.f, c);
		}
		for (int l = 0; l < 8; l++) {
			NVGcolor c = (s.laneFlash >> l & 1) ? laneColor(l) : semi::theme::grid();
			fillRect(vg, w - 4.f - (8 - l) * 8.5f, fy, 7.f, 6.f, c);
		}
		if (s.halted)
			text(vg, w * 0.5f + 20.f, fy - 1.f, "HALT", 9.f, semi::theme::magenta(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
	}

	// 17 x 17 matrix: square size = weight; the ball's row and the move just taken in magenta.
	// Right: the 16 steps with their trig and velocity.
	void drawMatrix(NVGcontext* vg, const Snapshot& s, float x0, float y0, float x1, float y1) {
		const float side = std::min(y1 - y0, (x1 - x0) * 0.55f);
		const float c = side / 17.f;
		for (int x = 0; x < 17; x++) {
			const bool row = x == s.lastTo;
			if (row)
				fillRect(vg, x0, y0 + x * c, side, c, semi::theme::magentaDeep());
			for (int y = 0; y < 17; y++) {
				const int v = s.matrix[x][y];
				if (!v)
					continue;
				const float q = std::max(1.2f, (c - 1.f) * std::sqrt(v / 100.f));
				const bool last = x == s.lastFrom && y == s.lastTo;
				fillRect(vg, x0 + y * c + (c - q) * 0.5f, y0 + x * c + (c - q) * 0.5f, q, q,
				         last ? semi::theme::magenta() : row ? semi::theme::text() : semi::theme::cyanDim());
			}
		}
		nvgBeginPath(vg);
		nvgRect(vg, x0, y0, side, side);
		nvgStrokeColor(vg, semi::theme::grid());
		nvgStrokeWidth(vg, semi::platform::kThinStroke);
		nvgStroke(vg);

		// steps: 4 x 4 cells T1..T8 / M, velocity bar
		const float sx = x0 + side + 8.f, sw = x1 - sx;
		const float gw = (sw - 3 * 3.f) / 4.f, gh = (y1 - y0 - 3 * 3.f) / 4.f;
		char buf[16];
		for (int k = 0; k < 16; k++) {
			const float cx = sx + (k % 4) * (gw + 3.f), cy = y0 + (k / 4) * (gh + 3.f);
			const bool cur = k == s.step;
			fillRect(vg, cx, cy, gw, gh, cur ? semi::theme::magentaDeep() : semi::theme::cell());
			const int t = s.trig[k];
			if (t == 0)
				std::snprintf(buf, sizeof buf, "%02d", k + 1);
			else if (t == 9)
				std::snprintf(buf, sizeof buf, "%02d M", k + 1);
			else
				std::snprintf(buf, sizeof buf, "%02d T%d", k + 1, t);
			text(vg, cx + 2.f, cy + 1.f, buf, 9.f, cur ? semi::theme::magenta() : t ? semi::theme::text() : semi::theme::dim());
			if (t > 0 && t < 9)
				fillRect(vg, cx + 2.f, cy + gh - 4.f, (gw - 4.f) * s.vel[k] / 127.f, 2.5f, laneColor(t - 1));
		}
	}

	// the Markov graph: 16 steps on a ring, edges = weights, the ball and its last move
	void drawNetwork(NVGcontext* vg, const Snapshot& s, float x0, float y0, float x1, float y1) {
		const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
		const float r = std::min(x1 - x0, y1 - y0) * 0.45f;
		float px[17], py[17];
		px[0] = cx;
		py[0] = cy;
		for (int k = 0; k < 16; k++) {
			const float a = -1.5707963f + k * 6.2831853f / 16.f;
			px[k + 1] = cx + r * std::cos(a);
			py[k + 1] = cy + r * std::sin(a);
		}
		int edges = 0;
		for (int x = 0; x < 17 && edges < semi::platform::kMaxPolyPoints; x++)
			for (int y = 1; y < 17 && edges < semi::platform::kMaxPolyPoints; y++) {
				const int v = s.matrix[x][y];
				if (!v || x == y)
					continue;
				line(vg, px[x], py[x], px[y], py[y], v > 60 ? semi::theme::cyanDim() : semi::theme::grid(),
				     v > 60 ? semi::platform::kStroke : semi::platform::kThinStroke);
				edges++;
			}
		if (s.lastFrom >= 0 && s.lastTo >= 0 && s.lastFrom != s.lastTo)
			line(vg, px[clamp(s.lastFrom, 0, 16)], py[clamp(s.lastFrom, 0, 16)], px[clamp(s.lastTo, 0, 16)], py[clamp(s.lastTo, 0, 16)],
			     semi::theme::magenta(), semi::platform::kStroke + 0.6f);
		char buf[4];
		for (int k = 0; k < 16; k++) {
			const bool cur = k == s.step;
			nvgBeginPath(vg);
			nvgCircle(vg, px[k + 1], py[k + 1], cur ? 4.2f : 2.6f);
			nvgFillColor(vg, cur ? semi::theme::magenta() : s.trig[k] ? semi::theme::text() : semi::theme::dim());
			nvgFill(vg);
			std::snprintf(buf, sizeof buf, "%d", k + 1);
			const float lx = cx + (px[k + 1] - cx) * 1.18f, ly = cy + (py[k + 1] - cy) * 1.18f;
			text(vg, lx, ly, buf, 9.f, cur ? semi::theme::magenta() : semi::theme::dim(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		}
		// path, newest last
		std::string p = "PATH";
		for (int k = std::max(0, s.pathN - 8); k < s.pathN; k++)
			p += string::f(" %02d", s.path[k] + 1);
		text(vg, x0, y1 - 1.f, p, 9.f, semi::theme::text(), NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
	}

	// tst_ingp: the 64 x 32 field, rows that sound in cyan, their read step in amber, the cursor
	void drawField(NVGcontext* vg, const Snapshot& s, float x0, float y0, float x1, float y1) {
		const float cw = (x1 - x0) / 64.f, ch = (y1 - y0) / 32.f;
		for (int r = 0; r < 32; r++) {
			const bool on = (s.gates >> r) & 1u;
			bitRow(vg, s.field[r], 64, x0, y0 + r * ch, cw, std::max(1.f, ch - 0.6f), on ? semi::theme::cyan() : semi::theme::cyanDim());
			const int st = clamp((int) s.rowStep[r], 0, 63);
			fillRect(vg, x0 + st * cw, y0 + r * ch, std::max(1.2f, cw), std::max(1.f, ch - 0.6f), semi::theme::amber());
		}
		if (s.cursorX >= 0 && s.cursorY >= 0 && s.cursorY < 32) {
			const float cx = x0 + (s.cursorX + 0.5f) * cw, cy = y0 + (s.cursorY + 0.5f) * ch;
			line(vg, cx - 5.f, cy, cx + 5.f, cy, semi::theme::magenta(), semi::platform::kStroke);
			line(vg, cx, cy - 5.f, cx, cy + 5.f, semi::theme::magenta(), semi::platform::kStroke);
		}
	}

	// IKEDAR: nine automata rows (pattern / wavetable / mask), the step column, envelopes
	void drawAutomata(NVGcontext* vg, const Snapshot& s, float x0, float y0, float x1, float y1) {
		static const char* const tags[3] = {"WAVE", "SPEC", "SINE"};
		const float lw = 36.f;
		const float cw = (x1 - x0 - lw) / 64.f, rh = (y1 - y0) / 9.f;
		if (s.ikdStep >= 0)
			fillRect(vg, x0 + lw + (s.ikdStep % 64) * cw, y0, std::max(1.5f, cw), y1 - y0, semi::theme::magentaDeep());
		for (int v = 0; v < 9; v++) {
			const float y = y0 + v * rh;
			const bool hit = s.ikdStep >= 0 && ((s.ikdCells[v] >> (s.ikdStep % 64)) & 1u);
			bitRow(vg, s.ikdCells[v], 64, x0 + lw, y + 1.f, cw, rh - 2.5f, hit ? semi::theme::text() : semi::theme::cyanDim());
			const float e = clamp(s.ikdEnv[v] * 2.f, 0.f, 1.f);
			fillRect(vg, x0, y + 1.f, 6.f, rh - 2.5f, semi::theme::grid());
			fillRect(vg, x0, y + 1.f + (rh - 2.5f) * (1.f - e), 6.f, (rh - 2.5f) * e, semi::theme::magenta());
			if (v % 3 == 0)
				text(vg, x0 + 9.f, y + 1.f, tags[v / 3], 9.f, semi::theme::dim());
		}
	}

	// JDAT: the three 512-sample buffers (64 columns each, +1 up, -1 down), gates, the 25 Hz reader
	void drawBuffers(NVGcontext* vg, const Snapshot& s, float x0, float y0, float x1, float y1) {
		const float lw = 12.f;
		const float bh = (y1 - y0) / 3.f, cw = (x1 - x0 - lw) / 64.f;
		for (int b = 0; b < 3; b++) {
			const float mid = y0 + b * bh + bh * 0.5f, amp = bh * 0.42f;
			line(vg, x0 + lw, mid, x1, mid, semi::theme::grid());
			for (int k = 0; k < 64; k++) {
				const float v = s.jbuf[b][k] / 127.f;
				if (std::fabs(v) < 0.02f)
					continue;
				const float hh = amp * std::fabs(v);
				fillRect(vg, x0 + lw + k * cw, v > 0 ? mid - hh : mid, std::max(1.f, cw - 0.8f), hh,
				         v > 0 ? semi::theme::text() : semi::theme::cyanDim());
			}
			fillRect(vg, x0, mid - amp, 6.f, 2.f * amp, semi::theme::grid());
			const float g = clamp(s.jgate[b], 0.f, 1.f);
			fillRect(vg, x0, mid + amp - 2.f * amp * g, 6.f, 2.f * amp * g, semi::theme::magenta());
		}
		const float rx = x0 + lw + (x1 - x0 - lw) * clamp(s.jphase, 0.f, 1.f);
		line(vg, rx, y0, rx, y1, semi::theme::amber(), semi::platform::kStroke);
	}

	// the bitstream under the read head: four rows of 64 bits; a row lights with its layer's level
	void drawBarcode(NVGcontext* vg, const Snapshot& s, float x0, float y0, float x1, float y1) {
		const float lw = 34.f;
		const float rh = (y1 - y0) / 4.f, cw = (x1 - x0 - lw) / 64.f;
		const float lv[4] = {s.rmsIk, s.rmsIg, s.rmsJd, s.rmsOut};
		static const char* const tags[4] = {"CELL", "TEXT", "DATA", "OUT"};
		char buf[16];
		for (int r = 0; r < 4; r++) {
			const bool loud = lv[r] > 0.05f;
			bitRow(vg, s.bits[r], 64, x0 + lw, y0 + r * rh + 1.f, cw, rh - 4.f, loud ? semi::theme::text() : semi::theme::cyanDim());
			std::snprintf(buf, sizeof buf, "%06llX", (unsigned long long) ((s.head + (uint64_t) r * 64u) / 8u) & 0xFFFFFFull);
			text(vg, x0, y0 + r * rh + 1.f, buf, 9.f, semi::theme::dim());
			text(vg, x0, y0 + r * rh + 10.f, tags[r], 9.f, loud ? semi::theme::amber() : semi::theme::dim());
		}
	}

	// hex dump at the head, the file's entropy map with the head on it
	void drawNumbers(NVGcontext* vg, const Snapshot& s, float x0, float y0, float x1, float y1) {
		const float mapH = 10.f;
		const float rows = 5.f, rh = (y1 - y0 - mapH - 4.f) / (rows + 1.f);
		char buf[64];
		for (int r = 0; r < (int) rows; r++) {
			const float y = y0 + r * rh;
			std::snprintf(buf, sizeof buf, "%06llX", (unsigned long long) (s.headByte + (uint64_t) r * 8u) & 0xFFFFFFull);
			text(vg, x0, y, buf, 9.f, semi::theme::dim());
			std::string hx, as;
			for (int k = 0; k < 8; k++) {
				const uint8_t b = s.hex[r * 8 + k];
				hx += string::f("%02X ", b);
				as += (b >= 32 && b < 127) ? (char) b : '.';
			}
			text(vg, x0 + 40.f, y, hx, 9.f, r == 0 ? semi::theme::cyan() : semi::theme::text());
			text(vg, x1, y, as, 9.f, semi::theme::amber(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		}
		// the file's printable strings at the head
		if (s.str[0][0]) {
			std::string t = std::string("\"") + s.str[0] + "\"";
			if (s.str[1][0])
				t += std::string("  \"") + s.str[1] + "\"";
			text(vg, x0, y0 + rows * rh, t, 9.f, semi::theme::magenta());
		}
		// entropy map (64 blocks), head marker
		const float my = y1 - mapH, cw = (x1 - x0) / 64.f;
		for (int k = 0; k < 64; k++) {
			const float e = s.entMap[k] / 255.f;
			fillRect(vg, x0 + k * cw, my + mapH * (1.f - e), std::max(1.f, cw - 0.6f), mapH * e, e > 0.85f ? semi::theme::amber() : semi::theme::cyanDim());
		}
		if (s.nbits > 0) {
			const float hx = x0 + (x1 - x0) * (float) ((double) s.head / (double) s.nbits);
			line(vg, hx, my - 2.f, hx, y1, semi::theme::magenta(), semi::platform::kStroke);
		}
	}

	// the output as a scope, the three layers and the output as meters
	void drawSpectra(NVGcontext* vg, const Snapshot& s, float x0, float y0, float x1, float y1) {
		const float mw = 46.f;
		const float sx1 = x1 - mw - 6.f, mid = (y0 + y1) * 0.5f, amp = (y1 - y0) * 0.48f;
		line(vg, x0, mid, sx1, mid, semi::theme::grid());
		float ys[64];
		for (int k = 0; k < 64; k++)
			ys[k] = mid - clamp(s.scope[k], -1.f, 1.f) * amp;
		polyline(vg, ys, 64, x0, sx1 - x0, semi::theme::cyan());
		const float lv[4] = {s.rmsIk, s.rmsIg, s.rmsJd, s.rmsOut};
		static const char* const tags[4] = {"IK", "IG", "JD", "OUT"};
		const float bw = (mw - 3 * 3.f) / 4.f;
		for (int k = 0; k < 4; k++) {
			const float bx = x1 - mw + k * (bw + 3.f);
			const float v = clamp(std::sqrt(lv[k]) * 1.6f, 0.f, 1.f);
			fillRect(vg, bx, y0, bw, y1 - y0 - 10.f, semi::theme::grid());
			fillRect(vg, bx, y0 + (y1 - y0 - 10.f) * (1.f - v), bw, (y1 - y0 - 10.f) * v, k == 3 ? semi::theme::amber() : semi::theme::cyanDim());
			text(vg, bx + bw * 0.5f, y1, tags[k], 9.f, semi::theme::dim(), NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
		}
	}
};

// File, read head and transport, two lines.
struct PindataInfoScreen : Screen {
	Pindata* module = nullptr;
	Snapshot demo;

	PindataInfoScreen() {
		demo.haveData = true;
		std::snprintf(demo.name, sizeof demo.name, "pinball_2.0.amxd");
		std::snprintf(demo.type, sizeof demo.type, "MAX FOR LIVE DEVICE");
		demo.entropy = 4.5f;
		demo.head = 0x100;
		demo.step = 3;
		demo.playing = true;
		demo.running = true;
		std::snprintf(demo.str[0], sizeof demo.str[0], "\"statusbarvisible\" : 2,");
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		char buf[64];
		if (s.haveData) {
			std::string n = s.name;
			if (n.size() > 22)
				n = n.substr(0, 21) + "~";
			text(vg, 3.f, 2.f, n, 9.f, semi::theme::cyan());
			text(vg, w - 3.f, 2.f, s.type, 9.f, semi::theme::magenta(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		}
		else
			text(vg, 3.f, 2.f, "NO DATA - MENU: LOAD ANY FILE", 9.f, semi::theme::dim());
		std::snprintf(buf, sizeof buf, "ENT %.2f  STEP %s  %s%.0f BPM", s.entropy, s.step >= 0 ? string::f("%02d", s.step + 1).c_str() : "--",
		              s.clocked ? "CLK " : "", s.bpm);
		text(vg, 3.f, h - 2.f, buf, 9.f, s.playing ? semi::theme::text() : semi::theme::dim(), NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
		if (s.nbits > 0) {
			std::snprintf(buf, sizeof buf, "@ %.1f%%", 100.0 * (double) s.head / (double) s.nbits);
			text(vg, w - 3.f, h - 2.f, buf, 9.f, semi::theme::amber(), NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
		}
	}
};

// ---------------------------------------------------------------- widget

namespace {

void addIdx(Menu* menu, const std::string& label, const std::vector<std::string>& labels, int* v) {
	menu->addChild(createIndexSubmenuItem(label, labels,
		[=]() { return (size_t) clamp(*v, 0, (int) labels.size() - 1); },
		[=](size_t i) { *v = (int) i; }));
}

template <typename T>
void addValues(Menu* menu, const std::string& label, const std::vector<T>& values, const char* fmt, T* v) {
	std::vector<std::string> labels;
	for (T x : values)
		labels.push_back(string::f(fmt, (double) x));
	menu->addChild(createIndexSubmenuItem(label, labels,
		[=]() {
			size_t best = 0;
			for (size_t i = 0; i < values.size(); i++)
				if (std::fabs((double) values[i] - (double) *v) < std::fabs((double) values[best] - (double) *v))
					best = i;
			return best;
		},
		[=](size_t i) { *v = values[i]; }));
}

} // namespace

struct PindataWidget : ModuleWidget {
	PindataWidget(Pindata* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Pindata.svg")));

		auto* vis = semi::createScreenMm<PindataVisualScreen>(Vec(L::VISUAL.x, L::VISUAL.y), Vec(L::VISUAL.w, L::VISUAL.h));
		vis->module = module;
		addChild(vis);
		auto* info = semi::createScreenMm<PindataInfoScreen>(Vec(L::INFO.x, L::INFO.y), Vec(L::INFO.w, L::INFO.h));
		info->module = module;
		addChild(info);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		addParam(createParamCentered<semi::Button>(at(L::RUN), module, Pindata::RUN_PARAM));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::RUN_LED), module, Pindata::RUN_LIGHT));
		addParam(createParamCentered<semi::Button>(at(L::RESET), module, Pindata::RESET_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::HALT), module, Pindata::HALT_PARAM));
		addChild(createLightCentered<semi::MagentaLedSmall>(at(L::HALT_LED), module, Pindata::HALT_LIGHT));
		addParam(createParamCentered<semi::Button>(at(L::SCENE), module, Pindata::SCENE_PARAM));

		auto snapKnob = [&](app::ParamWidget* k) {
			if (auto* kn = dynamic_cast<app::Knob*>(k))
				kn->snap = true;
			addParam(k);
		};
		addParam(createParamCentered<semi::KnobM>(at(L::MULT), module, Pindata::MULT_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::SWING), module, Pindata::SWING_PARAM));
		snapKnob(createParamCentered<semi::KnobM>(at(L::PATTERN), module, Pindata::PATTERN_PARAM));
		snapKnob(createParamCentered<semi::KnobM>(at(L::SCAN), module, Pindata::SCAN_PARAM));
		snapKnob(createParamCentered<semi::KnobM>(at(L::STEPSIZE), module, Pindata::STEPSIZE_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::RATE), module, Pindata::RATE_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::IKD), module, Pindata::IKD_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::INGP), module, Pindata::INGP_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::JDAT), module, Pindata::JDAT_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::MASTER), module, Pindata::MASTER_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::SMOOTH), module, Pindata::SMOOTH_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::TUNE), module, Pindata::TUNE_PARAM));
		addParam(createParamCentered<semi::KnobSHot>(at(L::CLEAR), module, Pindata::CLEAR_PARAM));
		addParam(createParamCentered<semi::KnobSHot>(at(L::CHANCE), module, Pindata::CHANCE_PARAM));

		addInput(createInputCentered<semi::Jack>(at(L::CLK), module, Pindata::CLK_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RST_IN), module, Pindata::RST_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::FIRE), module, Pindata::FIRE_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::HEAD), module, Pindata::HEAD_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::MULT_CV), module, Pindata::MULT_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::SCENE_CV), module, Pindata::SCENE_INPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_L), module, Pindata::L_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_R), module, Pindata::R_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::GATE), module, Pindata::GATE_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::PITCH), module, Pindata::PITCH_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::VEL), module, Pindata::VEL_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::STEP_OUT), module, Pindata::STEP_OUTPUT));
		addOutput(createOutputCentered<semi::BusJack>(at(L::BUS), module, Pindata::BUS_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Pindata*>(module);
		if (!m)
			return;
		pnd::Settings* s = &m->cfg;
		menu->addChild(new MenuSeparator);

		// ---- data
		const std::string cur = m->loader.lastPath.rfind("example:", 0) == 0 ? "example " + m->loader.lastPath.substr(8)
		                                                                       : semi::fileName(m->loader.lastPath);
		menu->addChild(createMenuLabel("Data: " + (cur.empty() ? std::string("none") : cur) + (m->loader.failed ? " (load failed)" : "")));
		menu->addChild(createMenuItem("Load any file...", "", [=]() {
			const std::string dir = m->loader.lastPath.rfind("example:", 0) == 0 ? "" : semi::dirName(m->loader.lastPath);
			semi::openFileDialog("Any file:*", dir, [=](const std::string& path) { m->loadPath(path); });
		}));
		std::vector<std::string> ex;
		for (int k = 1; k <= pnd::examples::kCount; k++)
			ex.push_back(string::f("%d  %s", k, pnd::examples::name(k)));
		menu->addChild(createIndexSubmenuItem("Example text", ex,
			[=]() { const auto& p = m->loader.lastPath; return p.rfind("example:", 0) == 0 ? (size_t) clamp(std::atoi(p.c_str() + 8) - 1, 0, 8) : (size_t) 99; },
			[=](size_t i) { m->loadPath(string::f("example:%d", (int) i + 1)); }));
		menu->addChild(createIndexSubmenuItem("Read as", {"Auto", "Bytes", "Text", "Audio (WAV)"},
			[=]() { return (size_t) clamp(m->readAs.load(), 0, 3); },
			[=](size_t i) { m->readAs.store((int) i); m->reload(); }));
		menu->addChild(createMenuItem("Read head: home", "", [=]() { m->headRequest.store(0); }));
		menu->addChild(createMenuItem("Read head: random", "", [=]() { m->headRequest.store((int64_t) (random::u32() & 0x7fffffff) * 64); }));

		// ---- presets / transport
		menu->addChild(new MenuSeparator);
		std::vector<std::string> fac;
		for (int k = 0; k < pnd::kNumFactory; k++)
			fac.push_back(pnd::factoryName(k));
		menu->addChild(createIndexSubmenuItem("Factory preset", fac,
			[=]() { return (size_t) m->factoryIndex; },
			[=](size_t i) { m->factoryRequest.store((int) i); }));
		addIdx(menu, "CLK input", names({"1/4 (1 PPQN)", "1/8 (2 PPQN)", "1/16 (4 PPQN)", "1/32 (8 PPQN)", "24 PPQN"}), &m->ppqnIdx);
		addIdx(menu, "Tempo without CLK", names({"60", "90", "110", "120", "128", "140", "160", "174"}), &m->bpmIdx);

		// ---- pinball
		menu->addChild(createSubmenuItem("Pinball", "", [=](Menu* sub) {
			std::vector<std::string> st;
			for (int k = 1; k <= 16; k++)
				st.push_back(string::f("%d", k));
			sub->addChild(createIndexSubmenuItem("Starting step", st,
				[=]() { return (size_t) clamp(s->startStep - 1, 0, 15); },
				[=](size_t i) { s->startStep = (int) i + 1; }));
			sub->addChild(createBoolPtrMenuItem("Lanes play notes (GATE / PITCH / VEL)", "", &s->laneOut));
			sub->addChild(new MenuSeparator);
			sub->addChild(createMenuLabel("Functions"));
			const char* fns[] = {"Randomize trigs", "Clear steps", "Clear matrix", "Clear all", "Matrix: chain 1 > 16",
			                     "Matrix: drunk walk", "Matrix: random", "Matrix: ping-pong", "Matrix: jumps", "Randomize step values"};
			for (int k = 0; k < 10; k++)
				sub->addChild(createMenuItem(fns[k], "", [=]() { m->fnRequest.store(k); }));
		}));

		// ---- lanes
		menu->addChild(createSubmenuItem("Lanes", "", [=](Menu* sub) {
			std::vector<std::string> dests, notes;
			for (int d = 0; d < pnd::dNumDests; d++)
				dests.push_back(pnd::destName(d));
			for (int n = 24; n <= 72; n++)
				notes.push_back(noteName(n));
			for (int l = 0; l < pnd::pinball::kLanes; l++)
				sub->addChild(createSubmenuItem(string::f("T%d  %s", l + 1, pnd::destName(s->laneDest[(size_t) l])), "", [=](Menu* lm) {
					addIdx(lm, "Destination", dests, &s->laneDest[(size_t) l]);
					lm->addChild(createIndexSubmenuItem("Note (PITCH)", notes,
						[=]() { return (size_t) clamp(s->laneNote[(size_t) l] - 24, 0, 48); },
						[=](size_t i) { s->laneNote[(size_t) l] = (int) i + 24; }));
					lm->addChild(createBoolMenuItem("Mute (choke)", "",
						[=]() { return (bool) s->laneChoke[(size_t) l]; },
						[=](bool b) { s->laneChoke[(size_t) l] = b; }));
				}));
		}));

		const auto clocks = names({"Own", "Grid", "Pinball"});
		const auto divs = names({"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"});
		const auto feeds = names({"Off", "On load", "On move"});
		menu->addChild(createSubmenuItem("CELLULA", "", [=](Menu* sub) {
			addIdx(sub, "Clock", clocks, &s->ikdClk);
			addIdx(sub, "Grid", divs, &s->ikdDiv);
			addIdx(sub, "Data feed", feeds, &s->ikdFeed);
			sub->addChild(createBoolPtrMenuItem("Data sets pitch / level / gate", "", &s->ikdFeedSnd));
			sub->addChild(createBoolPtrMenuItem("Hold automata", "", &s->ikdHold));
			addIdx(sub, "Auto mutate every (MUTATE knob = chance)", names({"-", "1", "2", "4", "8", "16", "32", "64"}), &m->autoEveryIdx);
			addValues<float>(sub, "Swing (own clock)", {0.f, 6.f, 12.f, 25.f, 50.f}, "%g %%", &s->ikdSwing);
			addValues<float>(sub, "Wave 1-3", {-60.f, -12.f, -6.f, -3.f, 0.f, 3.f}, "%g dB", &s->ikdWaveDb);
			addValues<float>(sub, "Spec 4-6", {-60.f, -12.f, -6.f, -3.f, 0.f, 3.f}, "%g dB", &s->ikdSpecDb);
			addValues<float>(sub, "Sine 7-9", {-60.f, -12.f, -6.f, -3.f, 0.f, 3.f}, "%g dB", &s->ikdSineDb);
			sub->addChild(new MenuSeparator);
			sub->addChild(createMenuItem("Random pattern", "", [=]() { m->ikdFnRequest.store(0); }));
			sub->addChild(createMenuItem("Random sound", "", [=]() { m->ikdFnRequest.store(1); }));
			sub->addChild(createMenuItem("Mutate all", "", [=]() { m->ikdFnRequest.store(3); }));
			sub->addChild(createMenuItem("Read data now", "", [=]() { m->jdatFnRequest.store(2); }));
		}));
		menu->addChild(createSubmenuItem("PALIMPSEST", "", [=](Menu* sub) {
			addIdx(sub, "Clock", clocks, &s->ingpClk);
			addValues<float>(sub, "BPM (own clock)", {70.f, 100.f, 120.f, 140.f, 175.f, 240.f}, "%g", &s->ingpBpm);
			addIdx(sub, "Grid", divs, &s->ingpDiv);
			sub->addChild(createBoolPtrMenuItem("Generationize (cursor walks)", "", &s->ingpGen));
			addIdx(sub, "Generations per tick", names({"0", "1", "2", "3", "4", "5", "6", "7", "8"}), &s->ingpGens);
			addIdx(sub, "Walk", names({"34 x 34", "Full field"}), &s->ingpWalk);
			addIdx(sub, "Data feed", feeds, &s->ingpFeed);
			sub->addChild(createBoolPtrMenuItem("Data sets the banks", "", &s->ingpFeedBanks));
			addValues<float>(sub, "Width", {0.f, 0.5f, 1.f, 1.5f, 2.f}, "%g", &s->ingpWidth);
			addValues<float>(sub, "Tilt", {-1.f, -0.5f, 0.f, 0.5f, 1.f}, "%g", &s->ingpTilt);
			addValues<float>(sub, "Swing", {0.f, 0.1f, 0.2f, 0.33f, 0.5f}, "%g", &s->ingpSwing);
			sub->addChild(new MenuSeparator);
			sub->addChild(createMenuItem("Random rule", "", [=]() { m->ingpFnRequest.store(0); }));
			sub->addChild(createMenuItem("Random banks", "", [=]() { m->ingpFnRequest.store(1); }));
			for (int k = 0; k < 4; k++)
				sub->addChild(createMenuItem(string::f("Patch preset 2008/%d", k + 1), "", [=]() { m->ingpFnRequest.store(2 + k); }));
		}));
		menu->addChild(createSubmenuItem("DATUM", "", [=](Menu* sub) {
			addIdx(sub, "Clock", clocks, &s->jdatClk);
			addValues<float>(sub, "Metro (own clock)", {60.f, 90.f, 120.f, 180.f, 250.f, 500.f}, "%g ms", &s->jdatRate);
			addIdx(sub, "Grid", divs, &s->jdatDiv);
			const auto srcs = names({"None", "Sine", "Noise", "Data"});
			addIdx(sub, "Data 1 source", srcs, &s->jdatSrc[0]);
			addIdx(sub, "Data 2 source", srcs, &s->jdatSrc[1]);
			addIdx(sub, "Data 3 source", srcs, &s->jdatSrc[2]);
			addIdx(sub, "Drone source", srcs, &s->jdatSrc[3]);
			sub->addChild(createBoolPtrMenuItem("Link 1 > 3", "", &s->jdatLink));
			addValues<int>(sub, "Noise jump", {0, 15, 30, 50, 75, 100}, "%g %%", &s->jdatJump);
			sub->addChild(createBoolPtrMenuItem("Hold buffers", "", &s->jdatHold));
			addIdx(sub, "Wave~ interpolation", names({"None", "Linear", "Cubic"}), &s->jdatInterp);
			addIdx(sub, "Write", names({"Random (patch)", "Data trit", "Data scatter", "Data raw"}), &s->jdatWrite);
			addIdx(sub, "Data feed", feeds, &s->jdatFeed);
			addIdx(sub, "Pattern lock", names({"Off", "16", "32", "64", "128", "256"}), &s->jdatLoop);
			for (int k = 0; k < 4; k++)
				addValues<float>(sub, k < 3 ? string::f("Data %d level", k + 1) : std::string("Drone level"),
				                 {-60.f, -12.f, -6.f, -3.f, 0.f, 3.f}, "%g dB", &s->jdatLvlDb[(size_t) k]);
			sub->addChild(new MenuSeparator);
			sub->addChild(createMenuItem("Roll", "", [=]() { m->jdatFnRequest.store(0); }));
			sub->addChild(createMenuItem("Clear now", "", [=]() { m->jdatFnRequest.store(1); }));
		}));
		menu->addChild(createSubmenuItem("Mix", "", [=](Menu* sub) {
			sub->addChild(createBoolPtrMenuItem("DAC clip", "", &s->clip));
			sub->addChild(createBoolPtrMenuItem("Limiter", "", &s->limit));
			sub->addChild(createBoolPtrMenuItem("DC block", "", &s->dcBlock));
			addValues<int>(sub, "Velocity to accent", {0, 25, 50, 75, 100}, "%g %%", &s->velDepth);
		}));
		menu->addChild(createSubmenuItem("Visual", "", [=](Menu* sub) {
			std::vector<std::string> sc;
			for (int k = 0; k < kScenes; k++)
				sc.push_back(string::f("%02d %s", k + 1, kSceneNames[k]));
			addIdx(sub, "Scene", sc, &m->scene);
			addIdx(sub, "Scene change", names({"Manual", "Every bar", "Every 2 bars", "Every 4 bars", "Every 8 bars", "On step", "Random"}), &m->sceneAuto);
			sub->addChild(createBoolPtrMenuItem("Flash (VIS FLASH lanes)", "", &m->flashOn));
		}));
	}
};

Model* modelPindata = createModel<Pindata, PindataWidget>("Pindata");
