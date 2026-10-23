// IKEDAT — Rack / MetaModule port of IKEDAT (ikedar × jdat × pinball 2: a datamatic instrument).
// Engine: engines/ikedat, vendored from semierendonmez-commits/ikedat Source/engine unchanged except
// for the namespace (ikt). The processor's parameter tree, presets and functions live here.
//
// Panel:   VIEW screen (transit, barcode, spatial, grid, stream; AUTO cuts every two bars), INFO
//          strip; RUN / LNCH (relaunch the ball) / HALT (the device's "Hault", latching) / VIEW.
//          M row: MULT (time multiplier), SWING, PATTERN (relaunch every N bars), SCAN (linear / ball /
//          random / hold), SPEED (bytes per 16th). S row: IKEDAR WAVE / SPEC / SINE groups, JDAT bus,
//          SEED (data as CA seed), CLEAR (JDAT clear chance), MASTER.
// Jacks:   CLK (1/16 by default: the master clock follows it, like the host transport), LNCH (relaunch
//          on the starting step = the plugin's notes 36-51), RUN (gate: the plugin's MIDI GATE), MUT
//          (mutate: mono = all voices, poly channel k = voice k = the other MIDI notes), JUMP (data
//          head to a random place), HEAD (0-10 V over the file).
//          Out: L / R, the lanes as GATE / PITCH (1 V/oct, C4 = 0 V) / VEL, STEP (the ball, 1/12 V per
//          step), BUS (FamilyBus CV: POS, ENERGY, SCENE = view, EVENT on relaunches).
// Menu:    data (any file, typed-text examples, data source, signs), factory presets, Pinball
//          functions and lanes, IKEDAR / JDAT pages, clock, view.
// Dropped: MIDI ports (CV/gate instead), the paper theme, REACT / FLASH, user preset files, data
//          embedded in the project (the patch keeps the path).

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/FamilyBusCV.hpp"
#include "../common/Sample.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/IkedatLayout.hpp"
#include "ikedat/Hybrid.h"
#include "ikedat/IkedarFactory.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace L = semi::layout::ikedat;
using semi::Screen;
namespace hy = ikt::hy;
namespace pb = ikt::pb;
namespace ryoz = ikt::ryoz;
namespace dat = ikt::dat;

namespace {

constexpr int kBlock = 32;
constexpr float kVoltsPerUnit = 5.f;
#ifdef METAMODULE
constexpr size_t kMaxFileBytes = 256u * 1024u;
#else
constexpr size_t kMaxFileBytes = 8u * 1024u * 1024u;
#endif

constexpr int kViews = 5;
const char* const kViewNames[kViews] = {"TRANSIT", "BARCODE", "SPATIAL", "GRID", "STREAM"};
const float kPatternBars[] = {0.f, 0.5f, 1.f, 2.f, 4.f, 8.f, 16.f, 32.f, 64.f};
constexpr int kNumPatterns = 9;
const int kPpqn[] = {1, 2, 4, 8, 24};
const float kBpms[] = {90.f, 110.f, 120.f, 125.f, 128.f, 140.f, 160.f, 174.f};
const int kDefTrig[pb::kSteps] = {1, 0, 4, 0, 2, 7, 0, 5, 1, 0, 3, 0, 6, 0, 8, 4};
const int kAutoSteps[] = {0, 1, 2, 4, 8, 16, 32, 64};
constexpr int kHist = 40;

const char* const kFactory[] = {"IKEDAT init", "pinball (device init)", "bouncing data", "datamatics", "drain + relaunch",
                                "the three patches", "spectral rain", "barcode drums"};
constexpr int kNumFactory = 8;

inline float dbToGain(float db) { return db <= -59.9f ? 0.f : std::pow(10.f, db / 20.f); }

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

int popcount64(uint64_t x) {
	int n = 0;
	while (x) {
		x &= x - 1;
		n++;
	}
	return n;
}

// Everything that is not a panel knob (the processor's parameters).
struct IkSettings {
	int startStep = 1;
	std::array<pb::StepParams, pb::kSteps> steps{};
	std::array<int, pb::kLanes> laneNote{}, laneTarget{};
	std::array<bool, pb::kLanes> laneOn{};
	int ikMode = hy::ikSixteenths, ikAuto = 4, ikChance = 50;
	bool ikHold = false;
	float ikSwing = 12.f, ikBusDb = -3.f;
	std::array<int, 4> src{{3, 3, 3, 3}};
	std::array<float, 4> lvlDb{{0.f, 0.f, 0.f, -6.f}};
	bool link = true, jHold = false;
	int jump = 30, interp = 1, jGates = 2, jWrite = 1;
	bool useData = true, dataSigns = true, clip = true;

	IkSettings() {
		for (int s = 0; s < pb::kSteps; s++) {
			steps[(size_t) s].trig = kDefTrig[s];
			steps[(size_t) s].duration = 1.f;
		}
		for (int l = 0; l < pb::kLanes; l++) {
			laneNote[(size_t) l] = 36 + l;
			laneTarget[(size_t) l] = hy::kDefaultTargets[l];
			laneOn[(size_t) l] = true;
		}
	}
};

struct Snapshot {
	int view = 0, viewShown = 0;
	bool running = true, playing = false, clocked = false, halted = false;
	float bpm = 125.f, peak = 0.f, rms = 0.f;
	int ball = 0, from = 0, to = 0;
	uint8_t laneFlash = 0;
	// data around the head
	bool haveData = false;
	char name[40]{}, kind[24]{};
	uint64_t head = 0, size = 0;
	float entropy = 0.f;
	uint8_t bytes[64]{};
	uint8_t sorted[64]{};
	uint8_t blockEnt[64]{};
	char str[2][30]{};
	// IKEDAR
	uint64_t cells[9]{};
	uint8_t pitchN[9]{}, gainN[9]{}, decayN[9]{};
	float env[9]{};
	int ikStep = -1;
	// JDAT
	int8_t jbuf[3][64]{};
	float jgate[3]{};
	// stream: one row per 16th, newest last: voices that sounded, lanes that fired
	uint16_t histVoices[kHist]{};
	uint8_t histLanes[kHist]{};
	int histN = 0;
	float angle = 0.f;   // SPATIAL: turns with the loudness (data-driven, still when silent)
};

using Data = dat::DataSet;

} // namespace

struct Ikedat : Module {
	enum ParamId { RUN_PARAM, LAUNCH_PARAM, HALT_PARAM, VIEW_PARAM,
	               MULT_PARAM, SWING_PARAM, PATTERN_PARAM, SCAN_PARAM, SPEED_PARAM,
	               WAVE_PARAM, SPEC_PARAM, SINE_PARAM, JDAT_PARAM, SEED_PARAM, CLEAR_PARAM, MASTER_PARAM, PARAMS_LEN };
	enum InputId { CLK_INPUT, RST_INPUT, GATE_INPUT, MUT_INPUT, JUMP_INPUT, HEAD_INPUT, INPUTS_LEN };
	enum OutputId { L_OUTPUT, R_OUTPUT, GATE_OUTPUT, PITCH_OUTPUT, VEL_OUTPUT, STEP_OUTPUT, BUS_OUTPUT, OUTPUTS_LEN };
	enum LightId { RUN_LIGHT, HALT_LIGHT, LIGHTS_LEN };

	hy::Engine engine;
	IkSettings cfg;
	pb::Matrix matrix = pb::chainMatrix();   // audio thread reads; UI edits go through requests

	semi::Slot<Data> slot;
	semi::Loader<Data> loader;

	// json state
	bool running = true, halted = false;
	int view = 0, ppqnIdx = 2, bpmIdx = 3, factoryIndex = 0;

	std::atomic<int> factoryRequest{-1}, fnRequest{-1};
	std::atomic<int64_t> headRequest{-1};

	float outBuf[2][kBlock]{};
	int pos = 0;
	double preparedSr = 0.0;
	float masterSm = 0.7f;

	dsp::SchmittTrigger clkTrig, rstTrig, mutTrig[9], jumpTrig;
	dsp::BooleanTrigger runBtn, launchBtn, haltBtn, viewBtn;
	int64_t sampleCount = 0, lastEdge = -1, edges = 0;
	double clockPeriod = 0.0, ppqNow = 0.0;
	int lastHeadCv = -1;

	// lanes -> GATE / PITCH / VEL
	struct Note { bool on = false; int note = 0; int64_t offAt = 0; };
	std::array<Note, pb::kLanes> notes{};
	int gateLane = -1;
	bool retrig = false;
	float pitchV = 0.f, velV = 0.f;
	uint8_t laneAcc = 0;
	dsp::PulseGenerator eventPulse;
	uint32_t launches = 0, seenLaunches = 0;

	// view state
	uint32_t seenSixteenth = 0, seenMoves = 0;
	uint16_t histVoices[kHist]{};
	uint8_t histLanes[kHist]{};
	int histN = 0;
	int lastFrom = 0, lastTo = 0;
	float angle = 0.f;
	int lastBar = -1, autoView = 0;

	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;

	Ikedat() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configButton(RUN_PARAM, "Run");
		configButton(LAUNCH_PARAM, "Relaunch the ball (starting step)");
		configButton(HALT_PARAM, "Hault (freeze the ball, latching)");
		configButton(VIEW_PARAM, "Next view (AUTO, TRANSIT, BARCODE, SPATIAL, GRID, STREAM)");
		configParam(MULT_PARAM, -2.f, 2.f, 0.f, "Time multiplier", "x", 2.f);
		configParam(SWING_PARAM, 0.f, 100.f, 0.f, "Swing", " %");
		std::vector<std::string> pat;
		for (float b : kPatternBars)
			pat.push_back(b <= 0.f ? "off" : b < 1.f ? "1/2 bar" : string::f("%g bar%s", b, b == 1.f ? "" : "s"));
		configSwitch(PATTERN_PARAM, 0.f, kNumPatterns - 1, 2.f, "Pattern length (relaunch)", pat);
		configSwitch(SCAN_PARAM, 0.f, 3.f, 0.f, "Scan", {"Linear", "Ball", "Random", "Hold"});
		configParam(SPEED_PARAM, 0.f, 12.f, 3.f, "Scan bytes / 16th", "", 2.f);
		configParam(WAVE_PARAM, -60.f, 6.f, 0.f, "Wave level (voices 1-3)", " dB");
		configParam(SPEC_PARAM, -60.f, 6.f, 0.f, "Spec level (voices 4-6)", " dB");
		configParam(SINE_PARAM, -60.f, 6.f, -6.f, "Sine level (voices 7-9)", " dB");
		configParam(JDAT_PARAM, -60.f, 6.f, -3.f, "DATUM bus", " dB");
		configParam(SEED_PARAM, 0.f, 100.f, 35.f, "Data seed (CA generations read from the data)", " %");
		configParam(CLEAR_PARAM, 0.f, 100.f, 40.f, "DATUM clear chance", " %");
		configParam(MASTER_PARAM, -60.f, 6.f, -3.f, "Master", " dB");
		paramQuantities[SPEED_PARAM]->snapEnabled = true;
		paramQuantities[PATTERN_PARAM]->description = "Relaunch the ball every N bars (off = never)";
		paramQuantities[SCAN_PARAM]->description = "The read head each 16th: forward by SPEED, inside the ball's region, anywhere, or still";
		configInput(CLK_INPUT, "Clock (1/16 by default; unpatched = own tempo)");
		configInput(RST_INPUT, "Relaunch the ball (and the transport)");
		configInput(GATE_INPUT, "Run gate (the clock runs while high)");
		configInput(MUT_INPUT, "Mutate (mono: all voices; poly: channel k = voice k)");
		configInput(JUMP_INPUT, "Data head to a random place");
		configInput(HEAD_INPUT, "Data head (0-10 V over the file)");
		configOutput(L_OUTPUT, "Left");
		configOutput(R_OUTPUT, "Right");
		configOutput(GATE_OUTPUT, "Lane gate");
		configOutput(PITCH_OUTPUT, "Lane note (1 V/oct, C4 = 0 V)");
		configOutput(VEL_OUTPUT, "Lane velocity (0-10 V)");
		configOutput(STEP_OUTPUT, "Ball step (1/12 V per step, step 1 = 0 V)");
		configOutput(BUS_OUTPUT, "FamilyBus (POS, ENERGY, SCENE = view, EVENT on relaunches)");

		loader.attach(this, &slot, [this](const std::string& path, Data& d) { return buildData(path, d); });
		engine.prepare(48000.0);
		preparedSr = 48000.0;
		engine.seed(random::u32());
		applyFactory(0);
		loader.run("boot:");
	}

	// ---- data -------------------------------------------------------------------------------
	static bool buildData(const std::string& path, Data& d) {
		if (path == "boot:" || path.rfind("text:", 0) == 0) {
			std::string t;
			if (path == "boot:") {
				t = "IKEDAT // cellula x datum x pinball // drop any file here: its bytes become sound and image.\n"
				    "cellula 9 voices x 64-cell automata // datum 3 data 2 drone // pinball 17x17 prob\n";
				for (int i = 0; i < 2048; ++i)
					t.push_back((char) ((i * 2654435761u) >> 13));
				t += "rule 13 14 15 17 18 22 27 28 30 54 // metro 120 // random(0,3)-1 // 2^random(0,15) // what~ prob makenote\n";
				auto ds = Data::fromBytes("ikedat.boot", std::vector<uint8_t>(t.begin(), t.end()));
				d = std::move(*ds);
				d.kind = "BOOT DATA";
				d.mime = "BOOT";
				return true;
			}
			auto ds = Data::fromText(path.substr(5));
			d = std::move(*ds);
			return true;
		}
		FILE* f = std::fopen(path.c_str(), "rb");
		if (!f)
			return false;
		std::fseek(f, 0, SEEK_END);
		const long total = std::ftell(f);
		std::fseek(f, 0, SEEK_SET);
		if (total <= 0) {
			std::fclose(f);
			return false;
		}
		std::vector<uint8_t> buf;
		bool sampled = false;
		if ((size_t) total <= kMaxFileBytes) {
			buf.resize((size_t) total);
			buf.resize(std::fread(buf.data(), 1, buf.size(), f));
		}
		else {
			// as the plugin: big files as evenly spaced chunks
			sampled = true;
			constexpr int chunks = 64;
			const size_t chunk = kMaxFileBytes / chunks;
			buf.reserve(kMaxFileBytes);
			std::vector<uint8_t> tmp(chunk);
			for (int c = 0; c < chunks; ++c) {
				const long at = (long) ((double) (total - (long) chunk) * c / (chunks - 1));
				std::fseek(f, at, SEEK_SET);
				const size_t got = std::fread(tmp.data(), 1, chunk, f);
				buf.insert(buf.end(), tmp.begin(), tmp.begin() + (long) got);
			}
		}
		std::fclose(f);
		if (buf.empty())
			return false;
		auto ds = Data::fromBytes(semi::fileName(path), std::move(buf), (uint64_t) total, sampled);
		d = std::move(*ds);
		return true;
	}

	// ---- presets / functions (audio thread or constructor) -------------------------------
	void setRows(int index) {
		auto& sh = engine.ikShared;
		for (int w = 0; w < ryoz::kMasterWords; ++w)
			sh.master[(size_t) w].store(ryoz::factory::kMaster[w]);
		const auto baseMs = (float) ryoz::Engine::stepMsForMasterSum(sh.masterSum());
		uint32_t r = (uint32_t) (index * 7919 + 13);
		auto next = [&r]() { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return r; };
		for (int v = 0; v < ryoz::kNumVoices; ++v) {
			auto& vs = sh.voices[(size_t) v];
			vs.pitch.store(ryoz::factory::kPitch[v]);
			vs.cells.store(ryoz::factory::kCells[v]);
			vs.gain.store(ryoz::factory::kGain[v]);
			vs.decay.store(ryoz::factory::kDecay[v]);
			vs.rule.store(ryoz::kVoiceSpecs[v].savedRule);
			vs.lastRule.store(ryoz::kVoiceSpecs[v].savedRule);
			vs.gateBaseMs.store(baseMs);
			vs.signs.store((uint64_t) next() << 32 | next());
		}
	}

	void applyFactory(int index) {
		factoryIndex = clamp(index, 0, kNumFactory - 1);
		cfg = IkSettings();
		setRows(factoryIndex);
		matrix = pb::chainMatrix();
		float mult = 1.f, swing = 0.f, plen = 1.f, wave = 0.f, spec = 0.f, sine = -6.f, seed = 35.f, clear = 40.f;
		int scan = 0, speed = 8;
		auto& c = cfg;
		switch (factoryIndex) {
			case 1:
				for (auto& s : c.steps) {
					s.trig = 0;
					s.duration = 5.f;
				}
				break;
			case 2:
				runFn(5);
				swing = 0.18f;
				plen = 4.f;
				for (int s = 0; s < pb::kSteps; s++)
					c.steps[(size_t) s].trigProb = 70 + (s * 37) % 31;
				break;
			case 3:
				c.jWrite = 2; scan = 1; speed = 64; seed = 80.f; c.ikMode = 2;
				spec = 3.f; wave = -9.f; sine = -3.f; clear = 10.f;
				break;
			case 4:
				for (int s = 9; s <= 16; ++s)
					matrix[(size_t) s][0] = 30;
				plen = 2.f;
				break;
			case 5:
				c.ikMode = 0; c.jGates = 0; c.jWrite = 0; c.useData = false; c.ikAuto = 0;
				for (auto& t : c.laneTarget)
					t = 0;
				break;
			case 6:
				c.ikMode = 2; wave = -60.f; sine = -60.f; spec = 6.f; mult = 0.5f; c.jGates = 1; c.src[1] = 2; seed = 100.f;
				for (int l = 0; l < pb::kLanes; l++)
					c.laneTarget[(size_t) l] = l % 2 == 0 ? hy::tgSpec : hy::tgSeed;
				break;
			case 7:
				c.jGates = 1; c.jWrite = 2; c.ikMode = 2; c.src[0] = 3; c.src[1] = 2; c.src[2] = 1;
				swing = 0.3f; scan = 2;
				for (auto& s : c.steps)
					s.duration = 0.5f;
				break;
			default: break;
		}
		params[MULT_PARAM].setValue(std::log2(mult));
		params[SWING_PARAM].setValue(swing * 100.f);
		int pi = 0;
		for (int i = 0; i < kNumPatterns; i++)
			if (std::fabs(kPatternBars[i] - plen) < std::fabs(kPatternBars[pi] - plen))
				pi = i;
		params[PATTERN_PARAM].setValue((float) pi);
		params[SCAN_PARAM].setValue((float) scan);
		params[SPEED_PARAM].setValue(std::round(std::log2((float) speed)));
		params[WAVE_PARAM].setValue(wave);
		params[SPEC_PARAM].setValue(spec);
		params[SINE_PARAM].setValue(sine);
		params[JDAT_PARAM].setValue(-3.f);
		params[SEED_PARAM].setValue(seed);
		params[CLEAR_PARAM].setValue(clear);
		params[MASTER_PARAM].setValue(-3.f);
		engine.ikShared.loadSerial.fetch_add(1, std::memory_order_release);
		engine.relaunch();
	}

	static uint64_t randomBits(int width, int n) {
		uint64_t w = 0;
		n = clamp(n, 0, width);
		while (popcount64(w) < n)
			w |= (uint64_t) 1 << (random::u32() % (uint32_t) width);
		return w;
	}
	static uint64_t randomDensity(int width, float density) {
		uint64_t w = 0;
		for (int i = 0; i < width; ++i)
			if (random::uniform() < density)
				w |= (uint64_t) 1 << i;
		return w;
	}

	// IkedatProcessor::run (audio thread)
	enum Fn { fnRandomTrigs, fnClearSteps, fnClearMatrix, fnClearAll, fnChain, fnRandomMatrix, fnDataMatrix, fnDataSteps,
	          fnIkRandomPattern, fnIkRandomSound, fnIkMutateAll, fnIkClear, kNumFns };
	void runFn(int fn) {
		const Data* d = slot.active;
		const uint64_t head = engine.headView.load();
		auto byte = [&](uint64_t i) -> int { return d && !d->empty() ? d->byteAt(head + i) : (int) (random::u32() & 255); };
		auto rnd = [](int n) { return (int) (random::u32() % (uint32_t) std::max(1, n)); };
		switch (fn) {
			case fnRandomTrigs:
				for (auto& s : cfg.steps)
					s.trig = rnd(10);
				break;
			case fnClearSteps:
				for (auto& s : cfg.steps)
					s = pb::StepParams{};
				break;
			case fnClearMatrix: matrix = pb::Matrix{}; break;
			case fnClearAll:
				runFn(fnClearSteps);
				matrix = pb::Matrix{};
				break;
			case fnChain: matrix = pb::chainMatrix(); break;
			case fnRandomMatrix: {
				pb::Matrix m{};
				for (int a = 1; a < pb::kStates; ++a) {
					const int n = 1 + rnd(3);
					for (int k = 0; k < n; ++k)
						m[(size_t) a][(size_t) (1 + rnd(16))] = (uint8_t) (20 + rnd(81));
					if (random::uniform() < 0.08f)
						m[(size_t) a][0] = (uint8_t) (5 + rnd(20));
				}
				matrix = m;
				break;
			}
			case fnDataMatrix: {
				pb::Matrix m{};
				for (int a = 1; a < pb::kStates; ++a) {
					bool any = false;
					for (int b = 0; b < pb::kStates; ++b) {
						const int v = byte((uint64_t) (a * pb::kStates + b));
						if (b == 0) {
							if (v < 6)
								m[(size_t) a][0] = (uint8_t) (v * 4);
							continue;
						}
						if (v % 5 == 0) {
							m[(size_t) a][(size_t) b] = (uint8_t) (10 + v * 90 / 255);
							any = true;
						}
					}
					if (!any)
						m[(size_t) a][(size_t) (a % pb::kSteps + 1)] = 100;
				}
				matrix = m;
				break;
			}
			case fnDataSteps:
				for (int s = 0; s < pb::kSteps; ++s) {
					const int b0 = byte((uint64_t) (s * 4)), b1 = byte((uint64_t) (s * 4 + 1)), b2 = byte((uint64_t) (s * 4 + 2)),
					          b3 = byte((uint64_t) (s * 4 + 3));
					auto& st = cfg.steps[(size_t) s];
					st.trig = b0 % 3 == 0 ? 0 : 1 + b0 % 8;
					st.trigProb = 35 + b1 % 66;
					st.delay = (b2 & 3) == 0 ? (float) ((b2 >> 2) % 4) * 0.5f : 0.f;
					st.velocity = 40 + b3 % 88;
					st.duration = 0.25f + (float) (b1 % 8) * 0.25f;
				}
				break;
			case fnIkRandomPattern:
			case fnIkRandomSound:
				for (auto& vs : engine.ikShared.voices) {
					if (fn == fnIkRandomPattern)
						vs.cells.store(randomDensity(ryoz::kCells, 0.15f + 0.45f * random::uniform()));
					else {
						vs.pitch.store(randomBits(ryoz::kPitchBits, 2 + rnd(37)));
						vs.gain.store(randomBits(ryoz::kGainBits, 3 + rnd(14)));
						vs.decay.store(randomBits(ryoz::kDecayBits, rnd(25)));
					}
				}
				break;
			case fnIkMutateAll: requestMutate(-1); break;
			case fnIkClear:
				for (auto& vs : engine.ikShared.voices)
					vs.cells.store(0);
				break;
			default: break;
		}
	}

	void requestMutate(int voice) {
		auto& sh = engine.ikShared;
		if (voice < 0)
			for (auto& v : sh.voices)
				v.mutateRequests.fetch_add(1);
		else if (voice < ryoz::kNumVoices)
			sh.voices[(size_t) voice].mutateRequests.fetch_add(1);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		engine.prepare(e.sampleRate);
		preparedSr = e.sampleRate;
		pos = 0;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		running = true;
		halted = false;
		view = 0;
		factoryRequest.store(0);
	}

	// ---- json -------------------------------------------------------------------------------
	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "path", json_string(loader.lastPath.c_str()));
		json_object_set_new(root, "running", json_boolean(running));
		json_object_set_new(root, "halted", json_boolean(halted));
		json_object_set_new(root, "view", json_integer(view));
		json_object_set_new(root, "ppqn", json_integer(ppqnIdx));
		json_object_set_new(root, "bpm", json_integer(bpmIdx));
		json_object_set_new(root, "factory", json_integer(factoryIndex));
		const IkSettings& s = cfg;
		json_t* c = json_object();
		auto i = [&](const char* k, int v) { json_object_set_new(c, k, json_integer(v)); };
		auto f = [&](const char* k, double v) { json_object_set_new(c, k, json_real(v)); };
		auto b = [&](const char* k, bool v) { json_object_set_new(c, k, json_boolean(v)); };
		i("start", s.startStep); i("ikMode", s.ikMode); i("ikAuto", s.ikAuto); i("ikChance", s.ikChance); b("ikHold", s.ikHold);
		f("ikSwing", s.ikSwing); f("ikBus", s.ikBusDb); b("link", s.link); b("jHold", s.jHold); i("jump", s.jump);
		i("interp", s.interp); i("jGates", s.jGates); i("jWrite", s.jWrite); b("useData", s.useData); b("signs", s.dataSigns);
		b("clip", s.clip);
		json_t* a = json_array();
		for (int k = 0; k < 4; k++) {
			json_array_append_new(a, json_integer(s.src[(size_t) k]));
			json_array_append_new(a, json_real(s.lvlDb[(size_t) k]));
		}
		json_object_set_new(c, "srcLvl", a);
		json_object_set_new(root, "cfg", c);
		json_t* steps = json_array();
		for (const auto& p : s.steps) {
			json_t* e = json_array();
			for (double v : {(double) p.trig, (double) p.trigProb, (double) p.delay, (double) p.delayProb, (double) p.velocity, (double) p.duration})
				json_array_append_new(e, json_real(v));
			json_array_append_new(steps, e);
		}
		json_object_set_new(root, "steps", steps);
		json_t* lanes = json_array();
		for (int l = 0; l < pb::kLanes; l++) {
			json_t* e = json_array();
			json_array_append_new(e, json_integer(s.laneNote[(size_t) l]));
			json_array_append_new(e, json_integer(s.laneTarget[(size_t) l]));
			json_array_append_new(e, json_boolean(s.laneOn[(size_t) l]));
			json_array_append_new(lanes, e);
		}
		json_object_set_new(root, "lanes", lanes);
		json_t* mx = json_array();
		for (auto& row : matrix)
			for (auto w : row)
				json_array_append_new(mx, json_integer(w));
		json_object_set_new(root, "matrix", mx);
		json_t* ik = json_array();
		for (auto& w : engine.ikShared.master)
			json_array_append_new(ik, json_string(hex64(w.load()).c_str()));
		for (auto& vs : engine.ikShared.voices) {
			for (uint64_t w : {vs.pitch.load(), vs.cells.load(), vs.gain.load(), vs.decay.load(), vs.signs.load()})
				json_array_append_new(ik, json_string(hex64(w).c_str()));
			json_array_append_new(ik, json_string(hex64((uint64_t) vs.rule.load()).c_str()));
		}
		json_object_set_new(root, "ikedar", ik);
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
		auto gf = [&](json_t* o, const char* k, float& v, double lo, double hi) {
			if (json_t* j = json_object_get(o, k))
				v = (float) std::clamp(json_number_value(j), lo, hi);
		};
		auto gb = [&](json_t* o, const char* k, bool& v) {
			if (json_t* j = json_object_get(o, k))
				v = json_is_true(j);
		};
		gb(root, "running", running);
		gb(root, "halted", halted);
		gi(root, "view", view, 0, kViews);
		gi(root, "ppqn", ppqnIdx, 0, 4);
		gi(root, "bpm", bpmIdx, 0, 7);
		gi(root, "factory", factoryIndex, 0, kNumFactory - 1);
		IkSettings& s = cfg;
		if (json_t* c = json_object_get(root, "cfg")) {
			gi(c, "start", s.startStep, 1, 16); gi(c, "ikMode", s.ikMode, 0, 2); gi(c, "ikAuto", s.ikAuto, 0, 7);
			gi(c, "ikChance", s.ikChance, 0, 100); gb(c, "ikHold", s.ikHold); gf(c, "ikSwing", s.ikSwing, 0.0, 50.0);
			gf(c, "ikBus", s.ikBusDb, -60.0, 6.0); gb(c, "link", s.link); gb(c, "jHold", s.jHold); gi(c, "jump", s.jump, 0, 100);
			gi(c, "interp", s.interp, 0, 2); gi(c, "jGates", s.jGates, 0, 2); gi(c, "jWrite", s.jWrite, 0, 2);
			gb(c, "useData", s.useData); gb(c, "signs", s.dataSigns); gb(c, "clip", s.clip);
			if (json_t* a = json_object_get(c, "srcLvl"))
				for (int k = 0; k < 4; k++) {
					s.src[(size_t) k] = clamp((int) num(a, 2 * k, 3), 0, 3);
					s.lvlDb[(size_t) k] = (float) std::clamp(num(a, 2 * k + 1, 0.0), -60.0, 6.0);
				}
		}
		if (json_t* steps = json_object_get(root, "steps"))
			for (int k = 0; k < pb::kSteps && k < (int) json_array_size(steps); k++) {
				json_t* e = json_array_get(steps, k);
				auto& p = s.steps[(size_t) k];
				p.trig = clamp((int) num(e, 0, 0), 0, 9);
				p.trigProb = clamp((int) num(e, 1, 100), 0, 100);
				p.delay = (float) std::clamp(num(e, 2, 0.0), 0.0, 64.0);
				p.delayProb = clamp((int) num(e, 3, 100), 0, 100);
				p.velocity = clamp((int) num(e, 4, 100), 1, 127);
				p.duration = (float) std::clamp(num(e, 5, 1.0), 0.0, 64.0);
			}
		if (json_t* lanes = json_object_get(root, "lanes"))
			for (int l = 0; l < pb::kLanes && l < (int) json_array_size(lanes); l++) {
				json_t* e = json_array_get(lanes, l);
				s.laneNote[(size_t) l] = clamp((int) num(e, 0, 36 + l), 0, 127);
				s.laneTarget[(size_t) l] = clamp((int) num(e, 1, hy::kDefaultTargets[l]), 0, hy::kNumTargets - 1);
				json_t* on = json_array_get(e, 2);
				s.laneOn[(size_t) l] = on ? json_is_true(on) : true;
			}
		if (json_t* mx = json_object_get(root, "matrix"))
			if (json_array_size(mx) == pb::kStates * pb::kStates)
				for (int a = 0; a < pb::kStates; a++)
					for (int b = 0; b < pb::kStates; b++)
						matrix[(size_t) a][(size_t) b] = (uint8_t) clamp((int) num(mx, (size_t) (a * pb::kStates + b), 0), 0, 100);
		if (json_t* ik = json_object_get(root, "ikedar"))
			if (json_array_size(ik) == (size_t) (ryoz::kMasterWords + 6 * ryoz::kNumVoices)) {
				for (int w = 0; w < ryoz::kMasterWords; w++)
					engine.ikShared.master[(size_t) w].store(hexAt(ik, (size_t) w));
				for (int v = 0; v < ryoz::kNumVoices; v++) {
					auto& vs = engine.ikShared.voices[(size_t) v];
					const size_t o = (size_t) (ryoz::kMasterWords + 6 * v);
					vs.pitch.store(hexAt(ik, o));
					vs.cells.store(hexAt(ik, o + 1));
					vs.gain.store(hexAt(ik, o + 2));
					vs.decay.store(hexAt(ik, o + 3));
					vs.signs.store(hexAt(ik, o + 4));
					vs.rule.store((int) hexAt(ik, o + 5));
				}
				engine.ikShared.loadSerial.fetch_add(1, std::memory_order_release);
			}
		if (json_t* p = json_object_get(root, "path")) {
			const char* str = json_string_value(p);
			if (str && *str)
				loader.request(str);
		}
	}

	// ---- audio ------------------------------------------------------------------------------
	hy::Controls controls(float sampleRate, bool clocked, hy::Host& host) {
		hy::Controls c;
		const bool gateOk = !inputs[GATE_INPUT].isConnected() || inputs[GATE_INPUT].getVoltage() > 1.f;
		c.run = running && gateOk;
		const int ppqn = kPpqn[clamp(ppqnIdx, 0, 4)];
		if (clocked) {
			c.hostSync = true;
			host.valid = true;
			host.playing = lastEdge >= 0 && (double) (sampleCount - lastEdge) < std::max(2.0 * sampleRate, 3.0 * clockPeriod);
			host.bpm = clockPeriod > 0.0 ? std::clamp(60.0 * sampleRate / (clockPeriod * ppqn), 20.0, 400.0) : 120.0;
			double ppq = 0.0;
			if (edges >= 1) {
				const double frac = clockPeriod > 0.0 ? std::min((double) (sampleCount - lastEdge) / clockPeriod, 0.999) : 0.0;
				ppq = ((double) (edges - 1) + frac) / ppqn;
			}
			host.ppq = ppq;
			ppqNow = ppq;
		}
		else {
			c.freeBpm = kBpms[clamp(bpmIdx, 0, 7)];
		}
		c.patternBars = kPatternBars[clamp((int) std::round(params[PATTERN_PARAM].getValue()), 0, kNumPatterns - 1)];
		c.startStep = cfg.startStep;
		c.pin.timeMult = std::clamp((double) std::exp2(params[MULT_PARAM].getValue()), 0.25, 4.0);
		c.pin.swing = params[SWING_PARAM].getValue() * 0.01;
		c.pin.halt = halted;
		c.pin.steps = cfg.steps;
		c.pin.matrix = &matrix;
		c.laneTarget = cfg.laneTarget;
		c.laneOn = cfg.laneOn;

		c.ikMode = cfg.ikMode;
		c.ik.run = c.run;
		c.ik.hold = cfg.ikHold;
		c.ik.autoEvery = kAutoSteps[clamp(cfg.ikAuto, 0, 7)];
		c.ik.autoChance = cfg.ikChance * 0.01f;
		c.ik.swing = cfg.ikSwing * 0.01;
		c.ik.clip = false;
		const float g[3] = {dbToGain(params[WAVE_PARAM].getValue()), dbToGain(params[SPEC_PARAM].getValue()), dbToGain(params[SINE_PARAM].getValue())};
		for (int v = 0; v < ryoz::kNumVoices; ++v)
			c.ik.voiceGain[(size_t) v] = g[v / 3];
		c.ikBus = dbToGain(cfg.ikBusDb);

		c.jd.run = true;
		for (int i = 0; i < 4; ++i) {
			c.jd.src[(size_t) i] = clamp(cfg.src[(size_t) i], 0, 3);
			c.jd.level[(size_t) i] = dbToGain(cfg.lvlDb[(size_t) i]);
		}
		c.jd.clearProb = (int) std::round(params[CLEAR_PARAM].getValue());
		c.jd.jumpProb = cfg.jump;
		c.jd.hold = cfg.jHold;
		c.jd.interp = clamp(cfg.interp, 0, 2);
		c.jd.master = 1.f;
		c.jd.clip = false;
		c.jdGates = cfg.jGates;
		c.jdWrite = cfg.jWrite;
		c.jdBus = dbToGain(params[JDAT_PARAM].getValue());

		c.useData = cfg.useData;
		c.scan = clamp((int) std::round(params[SCAN_PARAM].getValue()), 0, 3);
		c.scanBytes = clamp((int) std::round(std::exp2(params[SPEED_PARAM].getValue())), 1, 4096);
		c.seedChance = params[SEED_PARAM].getValue() * 0.01f;
		c.dataSigns = cfg.dataSigns;
		c.master = 1.f;
		c.clip = cfg.clip;
		return c;
	}

	void runBlock(float sampleRate, bool clocked) {
		slot.poll();
		if (int f = factoryRequest.exchange(-1); f >= 0)
			applyFactory(f);
		if (int f = fnRequest.exchange(-1); f >= 0)
			runFn(f);
		const Data* d = slot.active;
		engine.setData(d);
		if (int64_t h = headRequest.exchange(-1); h >= 0 && d && !d->empty())
			engine.jumpHead((uint64_t) h % d->size());
		if (inputs[HEAD_INPUT].isConnected() && d && !d->empty()) {
			const int q = clamp((int) (inputs[HEAD_INPUT].getVoltage() * 25.6f), 0, 255);
			if (q != lastHeadCv) {
				lastHeadCv = q;
				engine.jumpHead((uint64_t) ((double) d->size() * q / 256.0));
			}
		}
		else
			lastHeadCv = -1;

		hy::Host host;
		const hy::Controls c = controls(sampleRate, clocked, host);
		pb::FireSink fires;
		engine.process(outBuf[0], outBuf[1], kBlock, host, c, fires);
		if (!clocked)
			ppqNow += (c.run ? c.freeBpm / 60.0 / sampleRate * kBlock : 0.0);

		// master after the engine (as the processor), smoothed
		const float target = dbToGain(params[MASTER_PARAM].getValue());
		const float k = 1.f - std::exp(-1.f / (0.01f * sampleRate));
		for (int i = 0; i < kBlock; i++) {
			masterSm += (target - masterSm) * k;
			outBuf[0][i] *= masterSm;
			outBuf[1][i] *= masterSm;
		}

		// lanes -> notes ([makenote]); MUTE stops every note
		const int64_t t0 = sampleCount;
		for (int i = 0; i < fires.count; i++) {
			const auto& f = fires.items[(size_t) i];
			if (f.lane < 0) {
				for (auto& n : notes)
					n.on = false;
				continue;
			}
			if (!cfg.laneOn[(size_t) f.lane])
				continue;
			laneAcc |= (uint8_t) (1u << f.lane);
			auto& n = notes[(size_t) f.lane];
			retrig = gateLane >= 0 && notes[(size_t) gateLane].on;
			n.on = true;
			n.note = clamp(cfg.laneNote[(size_t) f.lane], 0, 127);
			n.offAt = t0 + f.offset + std::max<int64_t>(1, (int64_t) f.durSamples);
			gateLane = f.lane;
			pitchV = (n.note - 60) / 12.f;
			velV = clamp(f.velocity, 1, 127) / 127.f * 10.f;
		}
		for (auto& n : notes)
			if (n.on && n.offAt < t0 + kBlock)
				n.on = false;

		// stream history: one row per 16th
		const uint32_t sx = engine.sixteenthView.load(std::memory_order_relaxed);
		if (sx != seenSixteenth) {
			seenSixteenth = sx;
			const int st = engine.ik.currentStep();
			uint16_t vm = 0;
			if (st >= 0)
				for (int v = 0; v < 9; v++)
					if ((engine.ikShared.voices[(size_t) v].cells.load(std::memory_order_relaxed) >> (st % 64)) & 1u)
						vm |= (uint16_t) (1u << v);
			if (histN < kHist)
				histN++;
			for (int k2 = kHist - 1; k2 > 0; k2--) {
				histVoices[k2] = histVoices[k2 - 1];
				histLanes[k2] = histLanes[k2 - 1];
			}
			histVoices[0] = vm;
			histLanes[0] = 0;
		}
		histLanes[0] |= laneAcc;
		pb::Transition tr;
		while (engine.seq.transitions.pop(tr)) {
			lastFrom = tr.from;
			lastTo = tr.to;
			if (tr.from == 0)
				launches++;
		}
		if (launches != seenLaunches) {
			seenLaunches = launches;
			eventPulse.trigger(semi::bus::kPulseSec);
		}
		// AUTO view: hard cuts every two bars and on relaunches
		const int bar = (int) std::floor(ppqNow / 4.0);
		if (bar != lastBar) {
			lastBar = bar;
			if (bar % 2 == 0)
				autoView = (autoView + 1) % kViews;
		}
	}

	void process(const ProcessArgs& args) override {
		if (args.sampleRate != preparedSr) {
			engine.prepare(args.sampleRate);
			preparedSr = args.sampleRate;
		}
		++sampleCount;
		if (runBtn.process(params[RUN_PARAM].getValue() > 0.f))
			running = !running;
		if (launchBtn.process(params[LAUNCH_PARAM].getValue() > 0.f))
			engine.relaunch();
		if (haltBtn.process(params[HALT_PARAM].getValue() > 0.f))
			halted = !halted;
		if (viewBtn.process(params[VIEW_PARAM].getValue() > 0.f))
			view = (view + 1) % (kViews + 1);

		const bool clocked = inputs[CLK_INPUT].isConnected();
		if (clocked && clkTrig.process(inputs[CLK_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (lastEdge >= 0)
				clockPeriod = (double) (sampleCount - lastEdge);
			lastEdge = sampleCount;
			edges++;
		}
		if (rstTrig.process(inputs[RST_INPUT].getVoltage(), 0.1f, 1.f)) {
			edges = 0;
			engine.relaunch();
		}
		const int mutCh = std::min(inputs[MUT_INPUT].getChannels(), 9);
		for (int c = 0; c < mutCh; c++)
			if (mutTrig[c].process(inputs[MUT_INPUT].getVoltage(c), 0.1f, 1.f))
				requestMutate(mutCh == 1 ? -1 : c);
		if (jumpTrig.process(inputs[JUMP_INPUT].getVoltage(), 0.1f, 1.f))
			headRequest.store((int64_t) (random::u32() & 0x7fffffff));

		outputs[L_OUTPUT].setVoltage(outBuf[0][pos] * kVoltsPerUnit);
		outputs[R_OUTPUT].setVoltage(outBuf[1][pos] * kVoltsPerUnit);
		if (++pos >= kBlock) {
			pos = 0;
			runBlock(args.sampleRate, clocked);
		}
		const bool gate = gateLane >= 0 && notes[(size_t) gateLane].on;
		outputs[GATE_OUTPUT].setVoltage(gate && !(retrig && pos < 16) ? 10.f : 0.f);
		if (retrig && pos >= 16)
			retrig = false;
		outputs[PITCH_OUTPUT].setVoltage(pitchV);
		outputs[VEL_OUTPUT].setVoltage(velV);
		const int ball = engine.seq.ball();
		outputs[STEP_OUTPUT].setVoltage(ball >= 1 ? (ball - 1) / 12.f : 0.f);
		if (outputs[BUS_OUTPUT].isConnected()) {
			outputs[BUS_OUTPUT].setChannels(semi::bus::kChannels);
			outputs[BUS_OUTPUT].setVoltage(semi::bus::posToVolts((float) (ppqNow / 4.0)), semi::bus::kPos);
			outputs[BUS_OUTPUT].setVoltage(clamp(engine.rmsView.load(std::memory_order_relaxed) * 20.f, 0.f, 10.f), semi::bus::kEnergy);
			outputs[BUS_OUTPUT].setVoltage(semi::bus::sceneToVolts(shownView()), semi::bus::kScene);
			outputs[BUS_OUTPUT].setVoltage(eventPulse.process(args.sampleTime) ? semi::bus::turnToVolts(4) : 0.f, semi::bus::kEvent);
		}

		if (ui.tick(args.sampleTime)) {
			lights[RUN_LIGHT].setBrightness(running ? 1.f : 0.f);
			lights[HALT_LIGHT].setBrightness(halted ? 1.f : 0.f);
			publish(clocked);
		}
	}

	int shownView() const { return view == 0 ? autoView : view - 1; }

	void publish(bool clocked) {
		Snapshot& s = snap.writeSlot();
		s.view = view;
		s.viewShown = shownView();
		s.running = running;
		s.halted = halted;
		s.clocked = clocked;
		s.playing = engine.rollingView.load(std::memory_order_relaxed);
		s.bpm = engine.bpmView.load(std::memory_order_relaxed);
		s.peak = engine.peakView.load(std::memory_order_relaxed);
		s.rms = engine.rmsView.load(std::memory_order_relaxed);
		s.ball = engine.seq.ball();
		s.from = lastFrom;
		s.to = lastTo;
		s.laneFlash = laneAcc;
		laneAcc = 0;
		angle += s.rms * 0.6f;   // loudness turns the cube: still when silent
		if (angle > 6.2831853f)
			angle -= 6.2831853f;
		s.angle = angle;

		const Data* d = slot.active;
		s.haveData = d && !d->empty() && cfg.useData;
		if (d && !d->empty()) {
			std::snprintf(s.name, sizeof s.name, "%s", d->name.c_str());
			std::snprintf(s.kind, sizeof s.kind, "%s", d->kind.c_str());
			s.head = engine.headView.load(std::memory_order_relaxed);
			s.size = d->originalSize;
			s.entropy = (float) d->entropy;
			for (int k = 0; k < 64; k++)
				s.bytes[k] = d->byteAt(s.head + (uint64_t) k);
			std::memcpy(s.sorted, s.bytes, 64);
			std::sort(s.sorted, s.sorted + 64);
			for (int k = 0; k < 64; k++)
				s.blockEnt[k] = (uint8_t) clamp((int) (d->blockEntropy[(size_t) k * 4] / 8.f * 255.f), 0, 255);
			s.str[0][0] = s.str[1][0] = 0;
			const int si = d->stringNear(s.head);
			if (si >= 0)
				for (int k = 0; k < 2; k++)
					std::snprintf(s.str[k], sizeof s.str[k], "%s", d->strings[((size_t) si + (size_t) k) % d->strings.size()].text.c_str());
		}
		for (int v = 0; v < 9; v++) {
			const auto& vs = engine.ikShared.voices[(size_t) v];
			s.cells[v] = vs.cells.load(std::memory_order_relaxed);
			s.pitchN[v] = (uint8_t) popcount64(vs.pitch.load(std::memory_order_relaxed));
			s.gainN[v] = (uint8_t) popcount64(vs.gain.load(std::memory_order_relaxed));
			s.decayN[v] = (uint8_t) popcount64(vs.decay.load(std::memory_order_relaxed));
			s.env[v] = vs.envLevel.load(std::memory_order_relaxed);
		}
		s.ikStep = engine.ik.currentStep();
		for (int b = 0; b < 3; b++) {
			for (int k = 0; k < 64; k++) {
				int8_t best = 0;
				for (int j = 0; j < 8; j++) {
					const int8_t v = engine.jd.bufView[(size_t) b][(size_t) (k * 8 + j)].load(std::memory_order_relaxed);
					if (std::abs((int) v) > std::abs((int) best))
						best = v;
				}
				s.jbuf[b][k] = best;
			}
			s.jgate[b] = engine.jd.gateView[(size_t) b].load(std::memory_order_relaxed);
		}
		s.histN = histN;
		for (int k = 0; k < kHist; k++) {
			s.histVoices[k] = histVoices[k];
			s.histLanes[k] = histLanes[k];
		}
		snap.publish();
	}
};

// ---------------------------------------------------------------- screens

namespace {

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

// The five datamatic views over a thin HUD: view, file, head; the eight lanes on the right edge.
struct IkedatViewScreen : Screen {
	Ikedat* module = nullptr;
	Snapshot demo;

	IkedatViewScreen() {
		demo.view = 1;
		demo.viewShown = 0;
		demo.haveData = true;
		std::snprintf(demo.name, sizeof demo.name, "ikedat.boot");
		std::snprintf(demo.kind, sizeof demo.kind, "BOOT DATA");
		const char* t = "IKEDAT // cellula x datum x pinball // drop any file here: its bytes become sound";
		for (int k = 0; k < 64; k++)
			demo.bytes[k] = (uint8_t) t[k];
		std::memcpy(demo.sorted, demo.bytes, 64);
		std::sort(demo.sorted, demo.sorted + 64);
		demo.ball = 5;
		demo.from = 4;
		demo.to = 5;
		demo.laneFlash = 0x11;
		demo.rms = 0.2f;
		std::snprintf(demo.str[0], sizeof demo.str[0], "drop any file here");
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		char buf[48];
		const int v = clamp(s.viewShown, 0, kViews - 1);
		std::snprintf(buf, sizeof buf, "%s%s", s.view == 0 ? "AUTO / " : "", kViewNames[v]);
		text(vg, 4.f, 2.f, buf, 10.f, semi::theme::cyan());
		std::snprintf(buf, sizeof buf, "BALL %02d  HEAD %s", s.ball, hex64(s.head).c_str());
		text(vg, w - 4.f, 2.f, buf, 9.f, semi::theme::text(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);

		const float x0 = 4.f, y0 = 15.f, x1 = w - 14.f, y1 = h - 4.f;
		switch (v) {
			case 0: drawTransit(vg, s, x0, y0, x1, y1); break;
			case 1: drawBarcode(vg, s, x0, y0, x1, y1); break;
			case 2: drawSpatial(vg, s, x0, y0, x1, y1); break;
			case 3: drawGrid(vg, s, x0, y0, x1, y1); break;
			default: drawStream(vg, s, x0, y0, x1, y1); break;
		}
		// the eight trig lanes on the right edge
		const float lh = (h - 19.f) / 8.f;
		for (int l = 0; l < 8; l++)
			fillRect(vg, w - 9.f, 15.f + l * lh, 6.f, lh - 1.5f, (s.laneFlash >> l & 1) ? semi::theme::magenta() : semi::theme::grid());
	}

	// 64 bytes at the head on the upper rail, the same bytes sorted on the lower one, hairlines between
	void drawTransit(NVGcontext* vg, const Snapshot& s, float x0, float y0, float x1, float y1) {
		const float top = y0 + (y1 - y0) * 0.36f, bot = y0 + (y1 - y0) * 0.64f;
		const float cw = (x1 - x0) / 64.f;
		line(vg, x0, top, x1, top, semi::theme::dim());
		line(vg, x0, bot, x1, bot, semi::theme::dim());
		bool used[64] = {};
		for (int k = 0; k < 64; k++) {
			const uint8_t b = s.bytes[k];
			const float hh = (top - y0 - 2.f) * b / 255.f;
			fillRect(vg, x0 + k * cw, top - hh, std::max(1.f, cw - 0.8f), hh, k == 0 ? semi::theme::magenta() : semi::theme::cyanDim());
			const float hs = (y1 - bot - 2.f) * s.sorted[k] / 255.f;
			fillRect(vg, x0 + k * cw, bot, std::max(1.f, cw - 0.8f), hs, semi::theme::cyanDeep());
			// where this byte sorts to (the first unused slot of its value)
			int j = 0;
			for (; j < 64; j++)
				if (!used[j] && s.sorted[j] == b)
					break;
			if (j < 64) {
				used[j] = true;
				if (k % 2 == 0 || k == 0)   // 32 hairlines + the head
					line(vg, x0 + (k + 0.5f) * cw, top, x0 + (j + 0.5f) * cw, bot, k == 0 ? semi::theme::magenta() : semi::theme::text(),
					     k == 0 ? semi::platform::kStroke : semi::platform::kThinStroke);
			}
		}
		char buf[24];
		if (s.from >= 0 && s.to >= 0) {
			std::snprintf(buf, sizeof buf, "%02d > %02d", s.from, s.to);
			text(vg, x1, y1, buf, 9.f, semi::theme::magenta(), NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
		}
		if (s.str[0][0])
			text(vg, x0, y1, s.str[0], 9.f, semi::theme::amber(), NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
	}

	// eight rows of the bit stream (64 bytes), the byte values under each row; lane hits mark the rows
	void drawBarcode(NVGcontext* vg, const Snapshot& s, float x0, float y0, float x1, float y1) {
		const float rh = (y1 - y0) / 8.f, cw = (x1 - x0) / 64.f;
		for (int r = 0; r < 8; r++) {
			uint64_t wbits = 0;
			for (int k = 0; k < 8; k++)
				for (int b = 0; b < 8; b++)
					if ((s.bytes[r * 8 + k] >> (7 - b)) & 1)
						wbits |= (uint64_t) 1 << (k * 8 + b);
			const bool lit = (s.laneFlash >> r) & 1;
			bitRow(vg, wbits, 64, x0, y0 + r * rh, cw, rh - 2.f, lit ? semi::theme::text() : semi::theme::cyanDim());
		}
		if (s.str[0][0])
			text(vg, x0, y1, s.str[0], 9.f, semi::theme::amber(), NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
	}

	// byte triplets as points in a turning cube; IKEDAR voices as crosses (pitch, level, gate)
	void drawSpatial(NVGcontext* vg, const Snapshot& s, float x0, float y0, float x1, float y1) {
		const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, r = std::min(x1 - x0, y1 - y0) * 0.42f;
		const float ca = std::cos(s.angle), sa = std::sin(s.angle), ct = 0.86f, st = 0.5f;
		auto proj = [&](float x, float y, float z, float& px, float& py) {
			const float xr = x * ca - z * sa, zr = x * sa + z * ca;
			const float yr = y * ct - zr * st, zz = y * st + zr * ct;
			const float p = 2.6f / (2.6f + zz);
			px = cx + xr * r * p;
			py = cy + yr * r * p;
		};
		static const int e[12][2] = {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7}, {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
		float vx[8], vy[8];
		for (int i = 0; i < 8; i++)
			proj(i & 1 ? 1.f : -1.f, i & 2 ? 1.f : -1.f, i & 4 ? 1.f : -1.f, vx[i], vy[i]);
		for (auto& k : e)
			line(vg, vx[k[0]], vy[k[0]], vx[k[1]], vy[k[1]], semi::theme::dim());
		for (int k = 0; k + 2 < 64; k += 3) {
			float px, py;
			proj(s.bytes[k] / 127.5f - 1.f, s.bytes[k + 1] / 127.5f - 1.f, s.bytes[k + 2] / 127.5f - 1.f, px, py);
			fillRect(vg, px - 1.2f, py - 1.2f, 2.4f, 2.4f, semi::theme::text());
		}
		for (int v = 0; v < 9; v++) {
			float px, py;
			proj(s.pitchN[v] / 25.f - 1.f, s.gainN[v] / 25.f - 1.f, s.decayN[v] / 12.5f - 1.f, px, py);
			const NVGcolor c = s.env[v] > 0.05f ? semi::theme::magenta() : semi::theme::cyanDim();
			line(vg, px - 3.f, py, px + 3.f, py, c, semi::platform::kStroke);
			line(vg, px, py - 3.f, px, py + 3.f, c, semi::platform::kStroke);
		}
	}

	// the byte-pair network (b[i], b[i+1]) on a measured grid, the head byte crosshaired
	void drawGrid(NVGcontext* vg, const Snapshot& s, float x0, float y0, float x1, float y1) {
		for (int k = 0; k <= 8; k++) {
			const float gx = x0 + (x1 - x0) * k / 8.f, gy = y0 + (y1 - y0) * k / 8.f;
			line(vg, gx, y0, gx, y1, semi::theme::grid());
			line(vg, x0, gy, x1, gy, semi::theme::grid());
		}
		float xs[48], ys[48];
		for (int k = 0; k < 48; k++) {
			xs[k] = x0 + (x1 - x0) * s.bytes[k] / 255.f;
			ys[k] = y1 - (y1 - y0) * s.bytes[k + 1] / 255.f;
		}
		path(vg, xs, ys, 48, semi::theme::cyanDim(), semi::platform::kThinStroke);
		for (int k = 0; k < 48; k++)
			fillRect(vg, xs[k] - 1.2f, ys[k] - 1.2f, 2.4f, 2.4f, semi::theme::text());
		line(vg, xs[0], y0, xs[0], y1, semi::theme::magenta());
		line(vg, x0, ys[0], x1, ys[0], semi::theme::magenta());
		char buf[24];
		std::snprintf(buf, sizeof buf, "%02X %02X", s.bytes[0], s.bytes[1]);
		text(vg, std::min(xs[0] + 3.f, x1 - 30.f), std::max(ys[0] - 11.f, y0), buf, 9.f, semi::theme::magenta());
		if (s.str[0][0])
			text(vg, x0 + 2.f, y1 - 1.f, s.str[0], 9.f, semi::theme::amber(), NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
	}

	// the nine automata as a space-time diagram (one row per 16th, newest at the bottom), lane hits
	// marked; the three JDAT buffers as vertical barcodes on the right
	void drawStream(NVGcontext* vg, const Snapshot& s, float x0, float y0, float x1, float y1) {
		const float bw = (x1 - x0) * 0.3f;
		const float sx1 = x1 - bw - 6.f;
		const int rows = std::min(s.histN, kHist);
		const float rh = (y1 - y0) / kHist, cw = (sx1 - x0 - 10.f) / 9.f;
		for (int k = 0; k < rows; k++) {
			const float y = y1 - (k + 1) * rh;
			for (int v = 0; v < 9; v++)
				if ((s.histVoices[k] >> v) & 1)
					fillRect(vg, x0 + v * cw, y, cw - 1.5f, std::max(1.f, rh - 0.6f), v < 3 ? semi::theme::text() : v < 6 ? semi::theme::cyan() : semi::theme::cyanDim());
			if (s.histLanes[k])
				fillRect(vg, sx1 - 8.f, y, 6.f, std::max(1.f, rh - 0.6f), semi::theme::magenta());
		}
		const float cw2 = bw / 3.f, bh = (y1 - y0) / 64.f;
		for (int b = 0; b < 3; b++)
			for (int k = 0; k < 64; k++) {
				const int t = s.jbuf[b][k];
				if (t == 0)
					continue;
				fillRect(vg, x1 - bw + b * cw2, y0 + k * bh, cw2 - 2.f, std::max(1.f, bh - 0.4f), t > 0 ? semi::theme::text() : semi::theme::dim());
			}
	}
};

struct IkedatInfoScreen : Screen {
	Ikedat* module = nullptr;
	Snapshot demo;

	IkedatInfoScreen() {
		demo.haveData = true;
		std::snprintf(demo.name, sizeof demo.name, "ikedat.boot");
		std::snprintf(demo.kind, sizeof demo.kind, "BOOT DATA");
		demo.entropy = 5.9f;
		demo.size = 2300;
		demo.playing = true;
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		char buf[64];
		if (s.name[0]) {
			std::string n = s.name;
			if (n.size() > 18)
				n = n.substr(0, 17) + "~";
			text(vg, 3.f, 2.f, n, 9.f, s.haveData ? semi::theme::cyan() : semi::theme::dim());
			text(vg, w - 3.f, 2.f, s.haveData ? s.kind : "PATCH RANDOM", 9.f, semi::theme::magenta(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		}
		std::snprintf(buf, sizeof buf, "ENT %.2f  %s%.1f BPM%s", s.entropy, s.clocked ? "CLK " : "", s.bpm, s.halted ? "  HALT" : "");
		text(vg, 3.f, h - 2.f, buf, 9.f, s.playing ? semi::theme::text() : semi::theme::dim(), NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
		// the output peak as a red line under the text (the plugin's header)
		fillRect(vg, w - 3.f - 40.f, h - 6.f, 40.f, 2.5f, semi::theme::grid());
		fillRect(vg, w - 3.f - 40.f, h - 6.f, 40.f * clamp(s.peak, 0.f, 1.f), 2.5f, semi::theme::magenta());
	}
};

// ---------------------------------------------------------------- widget

namespace {

void addIdx(Menu* menu, const std::string& label, const std::vector<std::string>& labels, int* v) {
	menu->addChild(createIndexSubmenuItem(label, labels,
		[=]() { return (size_t) clamp(*v, 0, (int) labels.size() - 1); },
		[=](size_t i) { *v = (int) i; }));
}

void addValues(Menu* menu, const std::string& label, const std::vector<float>& values, const char* fmt, float* v) {
	std::vector<std::string> labels;
	for (float x : values)
		labels.push_back(string::f(fmt, x));
	menu->addChild(createIndexSubmenuItem(label, labels,
		[=]() {
			size_t best = 0;
			for (size_t i = 0; i < values.size(); i++)
				if (std::fabs(values[i] - *v) < std::fabs(values[best] - *v))
					best = i;
			return best;
		},
		[=](size_t i) { *v = values[i]; }));
}

const char* const kExamples[] = {
	"cellula x datum x pinball",
	"rule 13 14 15 17 18 22 27 28 30 54",
	"metro 120 // random(0,3)-1 // 2^random(0,15)",
	"the data is the score",
};

} // namespace

struct IkedatWidget : ModuleWidget {
	IkedatWidget(Ikedat* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Ikedat.svg")));
		auto* view = semi::createScreenMm<IkedatViewScreen>(Vec(L::VIEW.x, L::VIEW.y), Vec(L::VIEW.w, L::VIEW.h));
		view->module = module;
		addChild(view);
		auto* info = semi::createScreenMm<IkedatInfoScreen>(Vec(L::INFO.x, L::INFO.y), Vec(L::INFO.w, L::INFO.h));
		info->module = module;
		addChild(info);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		addParam(createParamCentered<semi::Button>(at(L::RUN), module, Ikedat::RUN_PARAM));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::RUN_LED), module, Ikedat::RUN_LIGHT));
		addParam(createParamCentered<semi::Button>(at(L::LAUNCH), module, Ikedat::LAUNCH_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::HALT), module, Ikedat::HALT_PARAM));
		addChild(createLightCentered<semi::MagentaLedSmall>(at(L::HALT_LED), module, Ikedat::HALT_LIGHT));
		addParam(createParamCentered<semi::Button>(at(L::VIEW_BTN), module, Ikedat::VIEW_PARAM));

		auto snapKnob = [&](app::ParamWidget* k) {
			if (auto* kn = dynamic_cast<app::Knob*>(k))
				kn->snap = true;
			addParam(k);
		};
		addParam(createParamCentered<semi::KnobM>(at(L::MULT), module, Ikedat::MULT_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::SWING), module, Ikedat::SWING_PARAM));
		snapKnob(createParamCentered<semi::KnobM>(at(L::PATTERN), module, Ikedat::PATTERN_PARAM));
		snapKnob(createParamCentered<semi::KnobM>(at(L::SCAN), module, Ikedat::SCAN_PARAM));
		snapKnob(createParamCentered<semi::KnobM>(at(L::SPEED), module, Ikedat::SPEED_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::WAVE), module, Ikedat::WAVE_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::SPEC), module, Ikedat::SPEC_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::SINE), module, Ikedat::SINE_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::JDAT), module, Ikedat::JDAT_PARAM));
		addParam(createParamCentered<semi::KnobSHot>(at(L::SEED), module, Ikedat::SEED_PARAM));
		addParam(createParamCentered<semi::KnobSHot>(at(L::CLEAR), module, Ikedat::CLEAR_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::MASTER), module, Ikedat::MASTER_PARAM));

		addInput(createInputCentered<semi::Jack>(at(L::CLK), module, Ikedat::CLK_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RST), module, Ikedat::RST_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::GATE_IN), module, Ikedat::GATE_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::MUT), module, Ikedat::MUT_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::JUMP), module, Ikedat::JUMP_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::HEAD), module, Ikedat::HEAD_INPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_L), module, Ikedat::L_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_R), module, Ikedat::R_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::GATE), module, Ikedat::GATE_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::PITCH), module, Ikedat::PITCH_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::VEL), module, Ikedat::VEL_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::STEP_OUT), module, Ikedat::STEP_OUTPUT));
		addOutput(createOutputCentered<semi::BusJack>(at(L::BUS), module, Ikedat::BUS_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Ikedat*>(module);
		if (!m)
			return;
		IkSettings* s = &m->cfg;
		menu->addChild(new MenuSeparator);
		const auto& lp = m->loader.lastPath;
		const std::string cur = lp == "boot:" ? std::string("boot data") : lp.rfind("text:", 0) == 0 ? "text" : semi::fileName(lp);
		menu->addChild(createMenuLabel("Data: " + (cur.empty() ? std::string("none") : cur) + (m->loader.failed ? " (load failed)" : "")));
		menu->addChild(createMenuItem("Load any file...", "", [=]() {
			const std::string dir = m->loader.lastPath.find(':') == std::string::npos || m->loader.lastPath.find('/') != std::string::npos
			                            ? semi::dirName(m->loader.lastPath) : "";
			semi::openFileDialog("Any file:*", dir, [=](const std::string& path) { m->loader.request(path); });
		}));
		std::vector<std::string> ex;
		ex.push_back("Boot data");
		for (auto* e : kExamples)
			ex.push_back(e);
		menu->addChild(createIndexSubmenuItem("Text as data", ex,
			[=]() { return (size_t) 99; },
			[=](size_t i) { m->loader.request(i == 0 ? std::string("boot:") : std::string("text:") + kExamples[i - 1]); }));
		menu->addChild(createIndexSubmenuItem("Data source", {"Patch random", "Data"},
			[=]() { return (size_t) (s->useData ? 1 : 0); },
			[=](size_t i) { s->useData = i == 1; }));
		menu->addChild(createBoolPtrMenuItem("Wavetable signs from the data", "", &s->dataSigns));
		menu->addChild(createMenuItem("Head: rewind", "", [=]() { m->headRequest.store(0); }));
		menu->addChild(createMenuItem("Head: random", "", [=]() { m->headRequest.store((int64_t) (random::u32() & 0x7fffffff)); }));

		menu->addChild(new MenuSeparator);
		std::vector<std::string> fac(kFactory, kFactory + kNumFactory);
		menu->addChild(createIndexSubmenuItem("Factory preset", fac,
			[=]() { return (size_t) m->factoryIndex; },
			[=](size_t i) { m->factoryRequest.store((int) i); }));
		addIdx(menu, "CLK input", names({"1/4 (1 PPQN)", "1/8 (2 PPQN)", "1/16 (4 PPQN)", "1/32 (8 PPQN)", "24 PPQN"}), &m->ppqnIdx);
		addIdx(menu, "Tempo without CLK", names({"90", "110", "120", "125 (the 120 ms metro)", "128", "140", "160", "174"}), &m->bpmIdx);
		addIdx(menu, "View", names({"Auto", "Transit", "Barcode", "Spatial", "Grid", "Stream"}), &m->view);

		menu->addChild(createSubmenuItem("Pinball", "", [=](Menu* sub) {
			std::vector<std::string> st;
			for (int k = 1; k <= 16; k++)
				st.push_back(string::f("%d", k));
			sub->addChild(createIndexSubmenuItem("Starting step", st,
				[=]() { return (size_t) clamp(s->startStep - 1, 0, 15); },
				[=](size_t i) { s->startStep = (int) i + 1; }));
			sub->addChild(new MenuSeparator);
			const char* fns[] = {"Randomize trigs", "Clear steps", "Clear matrix", "Clear all", "Chain 1 > 16", "Random matrix",
			                     "Data > matrix", "Data > steps"};
			for (int k = 0; k < 8; k++)
				sub->addChild(createMenuItem(fns[k], "", [=]() { m->fnRequest.store(k); }));
		}));
		menu->addChild(createSubmenuItem("Lanes", "", [=](Menu* sub) {
			std::vector<std::string> tg, notes;
			for (int t = 0; t < hy::kNumTargets; t++)
				tg.push_back(hy::targetName(t));
			for (int n = 24; n <= 72; n++)
				notes.push_back(noteName(n));
			for (int l = 0; l < pb::kLanes; l++)
				sub->addChild(createSubmenuItem(string::f("T%d  %s", l + 1, hy::targetName(s->laneTarget[(size_t) l])), "", [=](Menu* lm) {
					lm->addChild(createBoolMenuItem("On", "", [=]() { return (bool) s->laneOn[(size_t) l]; },
					                                [=](bool b) { s->laneOn[(size_t) l] = b; }));
					addIdx(lm, "Target", tg, &s->laneTarget[(size_t) l]);
					lm->addChild(createIndexSubmenuItem("Note (PITCH)", notes,
						[=]() { return (size_t) clamp(s->laneNote[(size_t) l] - 24, 0, 48); },
						[=](size_t i) { s->laneNote[(size_t) l] = (int) i + 24; }));
				}));
		}));
		menu->addChild(createSubmenuItem("CELLULA", "", [=](Menu* sub) {
			addIdx(sub, "Clock", names({"Patch clock", "16ths", "Trig only"}), &s->ikMode);
			sub->addChild(createBoolPtrMenuItem("Hold automata", "", &s->ikHold));
			addIdx(sub, "Auto mutate every", names({"Off", "1", "2", "4", "8", "16", "32", "64"}), &s->ikAuto);
			sub->addChild(createIndexSubmenuItem("Auto chance", {"0 %", "25 %", "50 %", "75 %", "100 %"},
				[=]() { return (size_t) clamp((s->ikChance + 12) / 25, 0, 4); },
				[=](size_t i) { s->ikChance = (int) i * 25; }));
			addValues(sub, "Patch swing", {0.f, 6.f, 12.f, 25.f, 50.f}, "%g %%", &s->ikSwing);
			addValues(sub, "CELLULA bus", {-60.f, -12.f, -6.f, -3.f, 0.f, 3.f}, "%g dB", &s->ikBusDb);
			sub->addChild(new MenuSeparator);
			sub->addChild(createMenuItem("Mutate all", "", [=]() { m->fnRequest.store(Ikedat::fnIkMutateAll); }));
			sub->addChild(createMenuItem("Random pattern", "", [=]() { m->fnRequest.store(Ikedat::fnIkRandomPattern); }));
			sub->addChild(createMenuItem("Random sound", "", [=]() { m->fnRequest.store(Ikedat::fnIkRandomSound); }));
			sub->addChild(createMenuItem("Clear", "", [=]() { m->fnRequest.store(Ikedat::fnIkClear); }));
		}));
		menu->addChild(createSubmenuItem("DATUM", "", [=](Menu* sub) {
			const auto srcs = names({"None", "Sine", "Noise", "Data"});
			sub->addChild(createIndexSubmenuItem("Data 1 source", srcs,
				[=]() { return (size_t) s->src[0]; },
				[=](size_t i) { s->src[0] = (int) i; if (s->link) s->src[2] = (int) i; }));
			addIdx(sub, "Data 2 source", srcs, &s->src[1]);
			addIdx(sub, "Data 3 source", srcs, &s->src[2]);
			addIdx(sub, "Drone source", srcs, &s->src[3]);
			sub->addChild(createBoolPtrMenuItem("Link 1 > 3", "", &s->link));
			sub->addChild(createIndexSubmenuItem("Noise jump", {"0 %", "15 %", "30 %", "50 %", "75 %", "100 %"},
				[=]() { static const int v[] = {0, 15, 30, 50, 75, 100}; size_t b = 0; for (size_t i = 0; i < 6; i++) if (std::abs(v[i] - s->jump) < std::abs(v[b] - s->jump)) b = i; return b; },
				[=](size_t i) { static const int v[] = {0, 15, 30, 50, 75, 100}; s->jump = v[i]; }));
			sub->addChild(createBoolPtrMenuItem("Hold buffers", "", &s->jHold));
			addIdx(sub, "Wave~ interpolation", names({"None", "Linear", "Cubic"}), &s->interp);
			addIdx(sub, "Gates", names({"Patch", "Trig", "Both"}), &s->jGates);
			addIdx(sub, "Write", names({"Patch random", "Data point", "Data block"}), &s->jWrite);
			for (int k = 0; k < 4; k++)
				addValues(sub, k < 3 ? string::f("Data %d level", k + 1) : std::string("Drone level"), {-60.f, -12.f, -6.f, -3.f, 0.f, 3.f},
				          "%g dB", &s->lvlDb[(size_t) k]);
		}));
		menu->addChild(createBoolPtrMenuItem("DAC clip", "", &s->clip));
	}
};

Model* modelIkedat = createModel<Ikedat, IkedatWidget>("Ikedat");
