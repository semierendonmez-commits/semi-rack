// IKEDAR — Rack / MetaModule port of ryoz_ikedar_ln (engine: engines/ikedar, the plugin's
// JUCE-free Engine with its namespace renamed and externalStep() added).
//
// The patch: nine voices, each driven by a 64-cell one-dimensional automaton row. The same
// 64 bits are the voice's rhythm, its wavetable and (voices 4-6) an FFT bin mask. Bar rows
// set pitch (popcount of 50), level (50), gate (25); the master row (167) switches the DAC
// (> 10 bars), the metro (> 25) and the tempo (bars x 2 BPM, 16ths).
//
// In Rack the bar rows are not drawn by hand; the panel plays them:
//   TEMPO   = the master row's bar count (default 66 = 132 BPM, the patch preset)
//   PITCH   = bars added to / taken from every voice's PITCH row (+ PITCH CV, 5 bars/V)
//   GATE    = bars taken from / added to every DECAY row (longer / shorter notes)
//   AUTO / CHANCE = the plugin's auto-mutate, MUTATE / RAND / HOLD its automaton actions.
// The offsets are applied to the preset's rows in a fixed, per-voice bit order, so returning
// a knob to 0 gives the preset's row back exactly.
// Clock: CLK (default 1/16 per pulse) or BUS POS replace the patch's metro; RUN is the
// plugin's MIDI GATE (runs while high, restarts on the rising edge).

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/FamilyBusCV.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/IkedarLayout.hpp"
#include "IkedarClock.hpp"
#include "ikedar/Engine.h"
#include "ikedar/FactoryPreset.h"

namespace L = semi::layout::ikedar;
namespace ik = ikedar_ln;
using semi::Screen;

namespace {

constexpr int kBlock = 32;
constexpr float kVoltsPerUnit = 5.f;

const std::vector<std::string> kPresetNames = {"original", "empty patterns", "sparse", "dense", "sine field", "spectral only"};
const std::vector<std::string> kDivNames = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/64"};
const double kDivBeats[] = {1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 0.0625};
const int kAutoSteps[] = {0, 1, 2, 4, 8, 16, 32, 64};

inline int popc(uint64_t x) {
	int n = 0;
	while (x) {
		x &= x - 1;
		++n;
	}
	return n;
}

struct XorShift {
	uint64_t s = 0x2545F4914F6CDD1Dull;
	uint32_t next() {
		s ^= s << 13;
		s ^= s >> 7;
		s ^= s << 17;
		return (uint32_t) (s >> 32);
	}
	float uni() { return (next() >> 8) * (1.f / 16777216.f); }
	int below(int n) { return n > 0 ? (int) (next() % (uint32_t) n) : 0; }
};

// Fixed bit orders (one per row) so offsets are deterministic and reversible.
struct Orders {
	uint8_t pitch[ik::kNumVoices][64];
	uint8_t decay[ik::kNumVoices][64];
	uint8_t master[ik::kMasterBits];
	Orders() {
		XorShift r;
		auto shuffle = [&](uint8_t* a, int n) {
			for (int i = 0; i < n; i++)
				a[i] = (uint8_t) i;
			for (int i = n - 1; i > 0; i--)
				std::swap(a[i], a[r.below(i + 1)]);
		};
		for (int v = 0; v < ik::kNumVoices; v++) {
			shuffle(pitch[v], ik::kPitchBits);
			shuffle(decay[v], ik::kDecayBits);
		}
		shuffle(master, ik::kMasterBits);
	}
};
const Orders& orders() {
	static const Orders o;
	return o;
}

inline bool getBit(const uint64_t* w, int i) { return (w[i >> 6] >> (i & 63)) & 1u; }
inline void setBit(uint64_t* w, int i, bool v) {
	if (v)
		w[i >> 6] |= 1ull << (i & 63);
	else
		w[i >> 6] &= ~(1ull << (i & 63));
}

// Row with exactly `target` bits: the base plus bits in `order`, or minus bits from its end.
void adjustRow(const uint64_t* base, uint64_t* out, int words, int nbits, int target, const uint8_t* order) {
	int c = 0;
	for (int w = 0; w < words; w++) {
		out[w] = base[w];
		c += popc(base[w]);
	}
	target = rack::math::clamp(target, 0, nbits);
	for (int i = 0; i < nbits && c < target; i++)
		if (!getBit(out, order[i])) {
			setBit(out, order[i], true);
			c++;
		}
	for (int i = nbits - 1; i >= 0 && c > target; i--)
		if (getBit(out, order[i])) {
			setBit(out, order[i], false);
			c--;
		}
}

struct Snapshot {
	uint64_t cells[ik::kNumVoices]{};
	uint64_t master[ik::kMasterWords]{};
	float env[ik::kNumVoices]{};
	uint8_t pitchBars[ik::kNumVoices]{}, gainBars[ik::kNumVoices]{}, decayBars[ik::kNumVoices]{};
	uint8_t rule[ik::kNumVoices]{};
	int step = -1;
	float bpm = 0.f;
	bool dac = false, running = false, hold = false;
	int source = 0; // 0 metro, 1 CLK, 2 BUS
	uint32_t gens = 0;
	float peak = 0.f;
	bool locked[ik::kNumVoices]{};
};

} // namespace

struct Ikedar : Module {
	enum ParamId {
		TEMPO_PARAM, SWING_PARAM, PITCH_PARAM, GATE_PARAM, AUTO_PARAM, CHANCE_PARAM,
		WAVE_PARAM, SPEC_PARAM, SINE_PARAM, MASTER_PARAM,
		MUTATE_PARAM, RAND_PARAM, HOLD_PARAM,
		PARAMS_LEN
	};
	enum InputId { CLK_INPUT, RST_INPUT, RUN_INPUT, MUT_INPUT, PITCH_INPUT, BUS_INPUT, INPUTS_LEN };
	enum OutputId { OUT_L_OUTPUT, OUT_R_OUTPUT, STEP_OUTPUT, OUTPUTS_LEN };
	enum LightId { HOLD_LIGHT, LIGHTS_LEN };

	ik::SharedState shared;
	ik::Engine engine{shared};

	// rows owned by the module (preset / randomise); the panel offsets are applied on top
	uint64_t basePitch[ik::kNumVoices]{}, baseDecay[ik::kNumVoices]{};
	uint64_t baseMaster[ik::kMasterWords]{};
	int appliedPitch = INT32_MIN, appliedGate = INT32_MIN, appliedTempo = INT32_MIN;
	bool rowsDirty = true;

	// settings (menu, saved in the patch)
	int preset = 0;
	bool hold = false;
	bool clip = true;
	int divIndex = 3;     // external step = 1/16
	int mutMode = 0;      // MUT input: 0 all unlocked voices, 1 one random unlocked voice
	bool locked[ik::kNumVoices]{};

	// requests from the UI thread, handled in process()
	std::atomic<int> reqPreset{-1};
	std::atomic<int> reqRandom{-1}; // 0 pattern, 1 sound, 2 all

	semi::ikclock::StepClock clock;
	dsp::BooleanTrigger mutBtn, randBtn, holdBtn;
	dsp::SchmittTrigger mutIn, runIn, busEv;
	dsp::PulseGenerator stepPulse;
	bool runWasHigh = false, restartNext = false;
	XorShift rng;

	struct Ev {
		int offset;
		bool restart;
	};
	Ev events[kBlock];
	int numEv = 0;
	float outBuf[2][kBlock]{};
	int pos = 0;
	int lastStepSeen = -1;
	double sr = 44100.0;
	float peak = 0.f;

	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;

	Ikedar() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(TEMPO_PARAM, 0.f, (float) ik::kMasterBits, 66.f, "Tempo (master bars: BPM = bars x 2, > 25 runs, > 10 DAC on)");
		getParamQuantity(TEMPO_PARAM)->snapEnabled = true;
		configParam(SWING_PARAM, 0.f, 50.f, 12.f, "Swing (odd steps late)", " %");
		configParam(PITCH_PARAM, -25.f, 25.f, 0.f, "Pitch (bars added to every PITCH row)");
		getParamQuantity(PITCH_PARAM)->snapEnabled = true;
		configParam(GATE_PARAM, -12.f, 12.f, 0.f, "Gate (bars taken from every DECAY row: + = longer notes)");
		getParamQuantity(GATE_PARAM)->snapEnabled = true;
		configSwitch(AUTO_PARAM, 0.f, 7.f, 0.f, "Auto-mutate every", {"off", "1 step", "2 steps", "4 steps", "8 steps", "16 steps", "32 steps", "64 steps"});
		configParam(CHANCE_PARAM, 0.f, 100.f, 50.f, "Auto-mutate chance", " %");
		configParam(WAVE_PARAM, -60.f, 6.f, 0.f, "Wave level (voices 1-3)", " dB");
		configParam(SPEC_PARAM, -60.f, 6.f, 0.f, "Spec level (voices 4-6)", " dB");
		configParam(SINE_PARAM, -60.f, 6.f, 0.f, "Sine level (voices 7-9)", " dB");
		configParam(MASTER_PARAM, -60.f, 6.f, 0.f, "Master", " dB");
		configButton(MUTATE_PARAM, "Mutate all (one generation per unlocked voice)");
		configButton(RAND_PARAM, "Randomise patterns (unlocked voices)");
		configButton(HOLD_PARAM, "Hold automata");
		configInput(CLK_INPUT, "Clock (1/16 per pulse by default; replaces the metro)");
		configInput(RST_INPUT, "Reset (next step is step 1)");
		configInput(RUN_INPUT, "Run gate (runs while high, restarts on the rising edge)");
		configInput(MUT_INPUT, "Mutate trigger");
		configInput(PITCH_INPUT, "Pitch CV (5 bars/V)");
		configInput(BUS_INPUT, "FamilyBus (POS clocks when CLK is unpatched, EVENT mutates, ENERGY scales chance)");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		configOutput(STEP_OUTPUT, "Step trigger");
		loadPreset(0);
		engine.prepare(sr);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		sr = e.sampleRate;
		engine.prepare(sr);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		hold = false;
		clip = true;
		divIndex = 3;
		mutMode = 0;
		for (bool& l : locked)
			l = false;
		reqPreset = 0;
	}

	// ---- rows ---------------------------------------------------------------

	static uint64_t randomBits(XorShift& r, int width, int n) {
		uint64_t w = 0;
		n = clamp(n, 0, width);
		while (popc(w) < n)
			w |= 1ull << r.below(width);
		return w;
	}
	static uint64_t randomDensity(XorShift& r, int width, float d) {
		uint64_t w = 0;
		for (int i = 0; i < width; i++)
			if (r.uni() < d)
				w |= 1ull << i;
		return w;
	}

	// RyozProcessor::loadFactoryPreset, with a fixed generator in place of juce::Random.
	void loadPreset(int index) {
		index = clamp(index, 0, (int) kPresetNames.size() - 1);
		preset = index;
		for (int w = 0; w < ik::kMasterWords; w++)
			baseMaster[w] = ik::factory::kMaster[w];
		const auto baseMs = (float) ik::Engine::stepMsForMasterSum(66);
		XorShift r;
		r.s ^= (uint64_t) (index * 7919 + 13) * 0x9E3779B97F4A7C15ull;
		for (int v = 0; v < ik::kNumVoices; v++) {
			auto& vs = shared.voices[(size_t) v];
			uint64_t cells = ik::factory::kCells[v], gain = ik::factory::kGain[v];
			switch (index) {
				case 1: cells = 0; break;
				case 2: cells = randomDensity(r, ik::kCells, 0.12f); break;
				case 3: cells = randomDensity(r, ik::kCells, 0.72f); break;
				case 4:
					if (ik::kVoiceSpecs[v].osc != ik::Osc::Sine)
						gain = 0;
					else if (gain == 0)
						gain = randomBits(r, ik::kGainBits, 8);
					break;
				case 5:
					if (ik::kVoiceSpecs[v].osc != ik::Osc::WavetableSpectral)
						gain = 0;
					else
						gain = randomBits(r, ik::kGainBits, 14 + r.below(10));
					break;
				default: break;
			}
			basePitch[v] = ik::factory::kPitch[v];
			baseDecay[v] = ik::factory::kDecay[v];
			vs.cells.store(cells);
			vs.gain.store(gain);
			vs.rule.store(ik::kVoiceSpecs[v].savedRule);
			vs.lastRule.store(ik::kVoiceSpecs[v].savedRule);
			vs.gateBaseMs.store(baseMs);
			vs.signs.store(((uint64_t) r.next() << 32) | r.next());
		}
		rowsDirty = true;
		applyRows();
		shared.loadSerial.fetch_add(1, std::memory_order_release);
	}

	// RyozProcessor::randomise: what 0 pattern, 1 sound, 2 all.
	void randomise(int what) {
		for (int v = 0; v < ik::kNumVoices; v++) {
			if (locked[v])
				continue;
			auto& vs = shared.voices[(size_t) v];
			if (what != 1)
				vs.cells.store(randomDensity(rng, ik::kCells, 0.15f + 0.45f * rng.uni()));
			if (what != 0) {
				basePitch[v] = randomBits(rng, ik::kPitchBits, 2 + rng.below(37));
				vs.gain.store(randomBits(rng, ik::kGainBits, 3 + rng.below(14)));
				baseDecay[v] = randomBits(rng, ik::kDecayBits, rng.below(25));
				rowsDirty = true;
			}
		}
	}

	void applyRows() {
		const int pOff = (int) std::lround(params[PITCH_PARAM].getValue() + clamp(inputs[PITCH_INPUT].getVoltage(), -5.f, 5.f) * 5.f);
		const int gOff = (int) std::lround(params[GATE_PARAM].getValue());
		const int tempo = (int) std::lround(params[TEMPO_PARAM].getValue());
		const Orders& o = orders();
		if (rowsDirty || pOff != appliedPitch) {
			for (int v = 0; v < ik::kNumVoices; v++) {
				uint64_t out;
				adjustRow(&basePitch[v], &out, 1, ik::kPitchBits, popc(basePitch[v]) + pOff, o.pitch[v]);
				shared.voices[(size_t) v].pitch.store(out, std::memory_order_relaxed);
			}
			appliedPitch = pOff;
		}
		if (rowsDirty || gOff != appliedGate) {
			for (int v = 0; v < ik::kNumVoices; v++) {
				uint64_t out;
				adjustRow(&baseDecay[v], &out, 1, ik::kDecayBits, popc(baseDecay[v]) - gOff, o.decay[v]);
				shared.voices[(size_t) v].decay.store(out, std::memory_order_relaxed);
			}
			appliedGate = gOff;
		}
		if (rowsDirty || tempo != appliedTempo) {
			uint64_t out[ik::kMasterWords];
			int c = 0;
			for (uint64_t w : baseMaster)
				c += popc(w);
			adjustRow(baseMaster, out, ik::kMasterWords, ik::kMasterBits, tempo, o.master);
			(void) c;
			for (int w = 0; w < ik::kMasterWords; w++)
				shared.master[(size_t) w].store(out[w], std::memory_order_relaxed);
			appliedTempo = tempo;
		}
		rowsDirty = false;
	}

	void requestMutate(int voice) {
		if (voice < 0) {
			for (int v = 0; v < ik::kNumVoices; v++)
				if (!locked[v])
					shared.voices[(size_t) v].mutateRequests.fetch_add(1, std::memory_order_relaxed);
		}
		else if (voice < ik::kNumVoices)
			shared.voices[(size_t) voice].mutateRequests.fetch_add(1, std::memory_order_relaxed);
	}

	void mutateRandomUnlocked() {
		int c[ik::kNumVoices], n = 0;
		for (int v = 0; v < ik::kNumVoices; v++)
			if (!locked[v])
				c[n++] = v;
		if (n)
			requestMutate(c[rng.below(n)]);
	}

	// ---- patch state ----------------------------------------------------------

	static std::string hex(uint64_t v) {
		char b[20];
		std::snprintf(b, sizeof b, "%016llx", (unsigned long long) v);
		return b;
	}
	static uint64_t unhex(json_t* j) {
		return j && json_is_string(j) ? (uint64_t) std::strtoull(json_string_value(j), nullptr, 16) : 0;
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "preset", json_integer(preset));
		json_object_set_new(root, "hold", json_boolean(hold));
		json_object_set_new(root, "clip", json_boolean(clip));
		json_object_set_new(root, "div", json_integer(divIndex));
		json_object_set_new(root, "ppqn", json_integer(clock.ppqnIndex));
		json_object_set_new(root, "mutMode", json_integer(mutMode));
		json_t* m = json_array();
		for (uint64_t w : baseMaster)
			json_array_append_new(m, json_string(hex(w).c_str()));
		json_object_set_new(root, "master", m);
		json_t* vs = json_array();
		for (int v = 0; v < ik::kNumVoices; v++) {
			auto& s = shared.voices[(size_t) v];
			json_t* o = json_object();
			json_object_set_new(o, "pitch", json_string(hex(basePitch[v]).c_str()));
			json_object_set_new(o, "decay", json_string(hex(baseDecay[v]).c_str()));
			json_object_set_new(o, "cells", json_string(hex(s.cells.load()).c_str()));
			json_object_set_new(o, "gain", json_string(hex(s.gain.load()).c_str()));
			json_object_set_new(o, "signs", json_string(hex(s.signs.load()).c_str()));
			json_object_set_new(o, "rule", json_integer(s.rule.load()));
			json_object_set_new(o, "gateMs", json_real(s.gateBaseMs.load()));
			json_object_set_new(o, "lock", json_boolean(locked[v]));
			json_array_append_new(vs, o);
		}
		json_object_set_new(root, "voices", vs);
		return root;
	}

	void dataFromJson(json_t* root) override {
		auto geti = [&](const char* k, int& dst, int lo, int hi) {
			if (json_t* j = json_object_get(root, k))
				dst = clamp((int) json_integer_value(j), lo, hi);
		};
		geti("preset", preset, 0, (int) kPresetNames.size() - 1);
		geti("div", divIndex, 0, 6);
		geti("ppqn", clock.ppqnIndex, 0, (int) semi::ikclock::kPpqnNames.size() - 1);
		geti("mutMode", mutMode, 0, 1);
		if (json_t* j = json_object_get(root, "hold"))
			hold = json_boolean_value(j);
		if (json_t* j = json_object_get(root, "clip"))
			clip = json_boolean_value(j);
		json_t* m = json_object_get(root, "master");
		json_t* vs = json_object_get(root, "voices");
		if (!m || !vs || json_array_size(vs) != (size_t) ik::kNumVoices)
			return;
		for (int w = 0; w < ik::kMasterWords; w++)
			baseMaster[w] = unhex(json_array_get(m, (size_t) w));
		for (int v = 0; v < ik::kNumVoices; v++) {
			json_t* o = json_array_get(vs, (size_t) v);
			auto& s = shared.voices[(size_t) v];
			basePitch[v] = unhex(json_object_get(o, "pitch"));
			baseDecay[v] = unhex(json_object_get(o, "decay"));
			s.cells.store(unhex(json_object_get(o, "cells")));
			s.gain.store(unhex(json_object_get(o, "gain")));
			s.signs.store(unhex(json_object_get(o, "signs")));
			if (json_t* j = json_object_get(o, "rule"))
				s.rule.store((int) json_integer_value(j));
			if (json_t* j = json_object_get(o, "gateMs"))
				s.gateBaseMs.store((float) json_number_value(j));
			if (json_t* j = json_object_get(o, "lock"))
				locked[v] = json_boolean_value(j);
		}
		rowsDirty = true;
		applyRows();
		shared.loadSerial.fetch_add(1, std::memory_order_release);
	}

	// ---- audio ----------------------------------------------------------------

	static float dbToGain(float db) { return db <= -59.9f ? 0.f : std::pow(10.f, db / 20.f); }

	void runBlock(bool external, bool runOk) {
		applyRows();

		ik::Engine::Controls c;
		c.run = !external && runOk;
		c.hostSync = false;
		c.swing = params[SWING_PARAM].getValue() * 0.01;
		c.clip = clip;
		c.hold = hold;
		c.autoEvery = kAutoSteps[clamp((int) params[AUTO_PARAM].getValue(), 0, 7)];
		float chance = params[CHANCE_PARAM].getValue() * 0.01f;
		if (inputs[BUS_INPUT].getChannels() > semi::bus::kEnergy)
			chance *= clamp(inputs[BUS_INPUT].getVoltage(semi::bus::kEnergy) / 10.f, 0.f, 1.f);
		c.autoChance = chance;
		const float g[3] = {dbToGain(params[WAVE_PARAM].getValue()), dbToGain(params[SPEC_PARAM].getValue()),
		                    dbToGain(params[SINE_PARAM].getValue())};
		for (int v = 0; v < ik::kNumVoices; v++) {
			c.voiceGain[(size_t) v] = g[v / 3];
			engine.setLocked(v, locked[v]);
		}
		ik::Engine::HostInfo host; // invalid: the engine runs its metro or waits for externalStep

		double stepMs = clock.stepSec * 1000.0;
		if (stepMs <= 0.0) {
			stepMs = ik::Engine::stepMsForMasterSum(shared.masterSum());
			if (stepMs <= 0.0)
				stepMs = 125.0;
		}

		int done = 0;
		for (int e = 0; e < numEv; e++) {
			const int off = events[e].offset;
			if (off > done) {
				engine.process(outBuf[0] + done, outBuf[1] + done, off - done, host, c);
				done = off;
			}
			engine.externalStep(stepMs, events[e].restart);
		}
		if (done < kBlock)
			engine.process(outBuf[0] + done, outBuf[1] + done, kBlock - done, host, c);
		numEv = 0;

		const float mg = dbToGain(params[MASTER_PARAM].getValue());
		for (int ch = 0; ch < 2; ch++)
			for (int i = 0; i < kBlock; i++) {
				outBuf[ch][i] *= mg;
				peak = std::max(peak, std::abs(outBuf[ch][i]));
			}

		const int st = engine.currentStep();
		if (st != lastStepSeen) {
			lastStepSeen = st;
			stepPulse.trigger(1e-3f);
		}
	}

	void process(const ProcessArgs& args) override {
		// requests from the menu
		const int rp = reqPreset.exchange(-1);
		if (rp >= 0)
			loadPreset(rp);
		const int rr = reqRandom.exchange(-1);
		if (rr >= 0)
			randomise(rr);

		if (holdBtn.process(params[HOLD_PARAM].getValue() > 0.f))
			hold = !hold;
		if (mutBtn.process(params[MUTATE_PARAM].getValue() > 0.f))
			requestMutate(-1);
		if (randBtn.process(params[RAND_PARAM].getValue() > 0.f))
			randomise(0);
		if (mutIn.process(inputs[MUT_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (mutMode == 0)
				requestMutate(-1);
			else
				mutateRandomUnlocked();
		}
		if (inputs[BUS_INPUT].getChannels() > semi::bus::kEvent
		    && busEv.process(inputs[BUS_INPUT].getVoltage(semi::bus::kEvent), 0.1f, 1.f))
			requestMutate(-1);

		// RUN gate (the plugin's MIDI GATE)
		bool runOk = true;
		if (inputs[RUN_INPUT].isConnected()) {
			const bool high = runIn.process(inputs[RUN_INPUT].getVoltage(), 0.1f, 1.f) || runIn.isHigh();
			if (high && !runWasHigh) {
				restartNext = true;
				engine.restartClock();
			}
			runWasHigh = high;
			runOk = high;
		}
		else
			runWasHigh = false;

		const double swing = params[SWING_PARAM].getValue() * 0.01;
		int64_t idx = 0;
		if (clock.process(args.sampleTime, inputs[CLK_INPUT], inputs[RST_INPUT], &inputs[BUS_INPUT],
		                  kDivBeats[divIndex], swing, idx)) {
			bool restart = clock.takeReset() || restartNext;
			restartNext = false;
			if (runOk && numEv < kBlock)
				events[numEv++] = {pos, restart};
		}
		else if (!clock.external() && clock.takeReset())
			engine.restartClock();

		outputs[OUT_L_OUTPUT].setVoltage(outBuf[0][pos] * kVoltsPerUnit);
		outputs[OUT_R_OUTPUT].setVoltage(outBuf[1][pos] * kVoltsPerUnit);
		outputs[STEP_OUTPUT].setVoltage(stepPulse.process(args.sampleTime) ? 10.f : 0.f);
		if (++pos >= kBlock) {
			pos = 0;
			runBlock(clock.external(), runOk);
		}

		if (ui.tick(args.sampleTime)) {
			lights[HOLD_LIGHT].setBrightness(hold ? 1.f : 0.f);
			Snapshot& s = snap.writeSlot();
			for (int v = 0; v < ik::kNumVoices; v++) {
				auto& vs = shared.voices[(size_t) v];
				s.cells[v] = vs.cells.load(std::memory_order_relaxed);
				s.env[v] = vs.envLevel.load(std::memory_order_relaxed);
				s.pitchBars[v] = (uint8_t) popc(vs.pitch.load(std::memory_order_relaxed));
				s.gainBars[v] = (uint8_t) popc(vs.gain.load(std::memory_order_relaxed));
				s.decayBars[v] = (uint8_t) popc(vs.decay.load(std::memory_order_relaxed));
				s.rule[v] = (uint8_t) vs.lastRule.load(std::memory_order_relaxed);
				s.locked[v] = locked[v];
			}
			s.gens = 0;
			for (int v = 0; v < ik::kNumVoices; v++)
				s.gens += shared.voices[(size_t) v].generation.load(std::memory_order_relaxed);
			for (int w = 0; w < ik::kMasterWords; w++)
				s.master[w] = shared.master[(size_t) w].load(std::memory_order_relaxed);
			s.step = shared.step.load(std::memory_order_relaxed);
			s.source = clock.source == semi::ikclock::StepClock::CLK ? 1 : clock.source == semi::ikclock::StepClock::BUS ? 2 : 0;
			s.bpm = s.source == 0 ? shared.bpm.load() : (clock.stepSec > 0.0 ? (float) (60.0 / (clock.stepSec / kDivBeats[divIndex])) : 0.f);
			s.dac = shared.dacOn.load(std::memory_order_relaxed);
			s.running = s.source == 0 ? shared.clockRunning.load() && runOk : clock.stepSec > 0.0 && runOk;
			s.hold = hold;
			s.peak = peak;
			snap.publish();
			peak = 0.f;
		}
	}
};

// ---- screens ------------------------------------------------------------------

namespace {

NVGcolor groupOn(int v) {
	return v < 3 ? semi::theme::cyan() : v < 6 ? semi::theme::magenta() : semi::theme::amber();
}
NVGcolor groupOff(int v) {
	return v < 3 ? semi::theme::cyanDeep() : v < 6 ? semi::theme::magentaDeep() : nvgRGB(0x2e, 0x24, 0x10);
}

Snapshot demoSnapshot() {
	Snapshot d;
	for (int v = 0; v < ik::kNumVoices; v++) {
		d.cells[v] = ik::factory::kCells[v];
		d.pitchBars[v] = (uint8_t) popc(ik::factory::kPitch[v]);
		d.gainBars[v] = (uint8_t) popc(ik::factory::kGain[v]);
		d.decayBars[v] = (uint8_t) popc(ik::factory::kDecay[v]);
		d.rule[v] = (uint8_t) ik::kVoiceSpecs[v].savedRule;
		d.env[v] = ((ik::factory::kCells[v] >> 21) & 1u) ? 0.3f + 0.07f * v : 0.f;
	}
	for (int w = 0; w < ik::kMasterWords; w++)
		d.master[w] = ik::factory::kMaster[w];
	d.step = 21;
	d.bpm = 132.f;
	d.dac = d.running = true;
	d.gens = 1408;
	return d;
}

} // namespace

// Nine automaton rows x 64 cells; the playhead column lights the cells that sound.
// Changes only on a step or a mutation (<= 20 Hz on MetaModule).
struct MatrixScreen : Screen {
	Ikedar* module = nullptr;
	Snapshot demo = demoSnapshot();

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		fillRect(vg, 0, 0, w, h, semi::theme::screen());
		const float padX = 3.f, padY = 3.f;
		const float meterW = 10.f;
		const float gx0 = padX, gx1 = w - padX - meterW - 3.f;
		const float groupGap = 3.f, rowGap = 1.2f;
		const float rowH = (h - 2 * padY - 2 * groupGap - 8 * rowGap) / 9.f;
		const float cw = (gx1 - gx0) / 64.f;

		// playhead column background
		if (s.step >= 0)
			fillRect(vg, gx0 + cw * s.step - 0.6f, 0, cw + 1.2f, h, semi::theme::grid());

		float y = padY;
		for (int v = 0; v < ik::kNumVoices; v++) {
			if (v == 3 || v == 6)
				y += groupGap;
			const NVGcolor on = groupOn(v), off = groupOff(v);
			// off cells as one strip, on cells as runs (few rects, cheap)
			fillRect(vg, gx0, y, gx1 - gx0, rowH, off);
			// one path per row; on MetaModule runs of set cells merge into one rect (fewer
			// primitives per frame), in Rack every cell keeps its gap
			const uint64_t c = s.cells[v];
			nvgBeginPath(vg);
			int i = 0;
			while (i < 64) {
				if (!((c >> i) & 1u)) {
					i++;
					continue;
				}
				int j = i;
				while (j < 64 && ((c >> j) & 1u))
					j++;
				if (semi::platform::kMetaModule)
					nvgRect(vg, gx0 + cw * i + 0.35f, y, cw * (j - i) - 0.7f, rowH);
				else
					for (int k = i; k < j; k++)
						nvgRect(vg, gx0 + cw * k + 0.35f, y, cw - 0.7f, rowH);
				i = j;
			}
			nvgFillColor(vg, on);
			nvgFill(vg);
			if (s.step >= 0 && ((c >> s.step) & 1u))
				fillRect(vg, gx0 + cw * s.step, y - 0.6f, cw, rowH + 1.2f, nvgRGB(0xf2, 0xf6, 0xf5));
			// envelope meter
			const float mx = w - padX - meterW;
			fillRect(vg, mx, y, meterW, rowH, semi::theme::cell());
			const float e = clamp(s.env[v] / 1.5f, 0.f, 1.f);
			if (e > 0.01f)
				fillRect(vg, mx, y, meterW * e, rowH, on);
			if (s.locked[v])
				fillRect(vg, mx - 2.2f, y, 1.4f, rowH, semi::theme::text());
			y += rowH + rowGap;
		}
		if (!s.dac)
			text(vg, w * 0.5f, h * 0.5f, "DAC OFF", 12.f, semi::theme::text(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
	}
};

// Master row as a barcode (167 bars) + the readouts: tempo, step, clock source, rules.
struct DataScreen : Screen {
	Ikedar* module = nullptr;
	Snapshot demo = demoSnapshot();

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		fillRect(vg, 0, 0, w, h, semi::theme::screen());
		const float pad = 3.f;
		const float fs = 9.f;
		char buf[64];
		static const char* src[] = {"METRO", "CLK", "BUS"};
		std::snprintf(buf, sizeof buf, "%3.0f BPM  %02d/64  %s%s", s.bpm, s.step < 0 ? 0 : s.step + 1, src[s.source],
		              s.running ? "" : " STOP");
		text(vg, pad, pad - 1.f, buf, fs, s.running ? semi::theme::cyan() : semi::theme::dim());
		std::snprintf(buf, sizeof buf, "%s%06u", s.hold ? "HOLD " : "GEN ", (unsigned) (s.gens % 1000000u));
		text(vg, w - pad, pad - 1.f, buf, fs, s.hold ? semi::theme::magenta() : semi::theme::amber(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);

		// barcode: one bar per master slider, set = full height
		const float by0 = pad + fs + 1.5f, by1 = h - pad - fs - 1.f;
		const float bw = (w - 2 * pad) / (float) ik::kMasterBits;
		nvgBeginPath(vg);
		for (int i = 0; i < ik::kMasterBits; i++)
			if (getBit(s.master, i))
				nvgRect(vg, pad + bw * i, by0, std::max(bw, 1.2f), by1 - by0);
		nvgFillColor(vg, nvgRGB(0xe6, 0xee, 0xec));
		nvgFill(vg);
		// thresholds: DAC (> 10), metro (> 25)
		int bars = 0;
		for (uint64_t x : s.master)
			bars += popc(x);
		fillRect(vg, pad, by1 + 0.6f, (w - 2 * pad) * bars / (float) ik::kMasterBits, 1.6f, semi::theme::cyan());

		// rules of the nine automata, grouped
		std::string r;
		for (int v = 0; v < ik::kNumVoices; v++) {
			char t[8];
			std::snprintf(t, sizeof t, "%s%02d", v == 0 ? "" : (v % 3 == 0 ? " | " : " "), s.rule[v]);
			r += t;
		}
		text(vg, pad, h - pad + 1.f, "R " + r, fs, semi::theme::text(), NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
	}
};

struct IkedarWidget : ModuleWidget {
	IkedarWidget(Ikedar* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Ikedar.svg")));

		auto* mx = semi::createScreenMm<MatrixScreen>(Vec(L::MATRIX.x, L::MATRIX.y), Vec(L::MATRIX.w, L::MATRIX.h));
		mx->module = module;
		addChild(mx);
		auto* ds = semi::createScreenMm<DataScreen>(Vec(L::DATA.x, L::DATA.y), Vec(L::DATA.w, L::DATA.h));
		ds->module = module;
		addChild(ds);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		addParam(createParamCentered<semi::KnobM>(at(L::TEMPO), module, Ikedar::TEMPO_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::SWING), module, Ikedar::SWING_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::PITCH), module, Ikedar::PITCH_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::GATE), module, Ikedar::GATE_PARAM));
		auto* au = createParamCentered<semi::KnobM>(at(L::AUTO), module, Ikedar::AUTO_PARAM);
		au->snap = true;
		addParam(au);
		addParam(createParamCentered<semi::KnobMHot>(at(L::CHANCE), module, Ikedar::CHANCE_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::WAVE), module, Ikedar::WAVE_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::SPEC), module, Ikedar::SPEC_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::SINE), module, Ikedar::SINE_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::MASTER), module, Ikedar::MASTER_PARAM));
		addParam(createParamCentered<semi::PadMagenta>(at(L::MUTATE), module, Ikedar::MUTATE_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::RAND), module, Ikedar::RAND_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::HOLD), module, Ikedar::HOLD_PARAM));
		addChild(createLightCentered<semi::MagentaLedSmall>(at(L::HOLD_LED), module, Ikedar::HOLD_LIGHT));

		addInput(createInputCentered<semi::Jack>(at(L::CLK), module, Ikedar::CLK_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RST), module, Ikedar::RST_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RUN), module, Ikedar::RUN_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::MUT), module, Ikedar::MUT_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::PITCH_CV), module, Ikedar::PITCH_INPUT));
		addInput(createInputCentered<semi::BusJack>(at(L::BUS), module, Ikedar::BUS_INPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_L), module, Ikedar::OUT_L_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_R), module, Ikedar::OUT_R_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::STEP), module, Ikedar::STEP_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Ikedar*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Preset", kPresetNames,
		                                      [=]() { return (size_t) m->preset; },
		                                      [=](size_t i) { m->reqPreset = (int) i; }));
		menu->addChild(createSubmenuItem("Randomise", "", [=](Menu* sub) {
			sub->addChild(createMenuItem("Patterns", "", [=]() { m->reqRandom = 0; }));
			sub->addChild(createMenuItem("Sound (pitch, level, gate rows)", "", [=]() { m->reqRandom = 1; }));
			sub->addChild(createMenuItem("All", "", [=]() { m->reqRandom = 2; }));
		}));
		menu->addChild(createSubmenuItem("Lock voices", "", [=](Menu* sub) {
			static const char* names[] = {"1 WAVE C", "2 WAVE L", "3 WAVE R", "4 SPEC C", "5 SPEC L", "6 SPEC R", "7 SINE C", "8 SINE L", "9 SINE R"};
			for (int v = 0; v < ik::kNumVoices; v++)
				sub->addChild(createBoolPtrMenuItem(names[v], "", &m->locked[v]));
		}));
		menu->addChild(createBoolPtrMenuItem("Hold automata", "", &m->hold));
		menu->addChild(createBoolPtrMenuItem("DAC clip", "", &m->clip));
		menu->addChild(createMenuLabel("Clock"));
		menu->addChild(createIndexSubmenuItem("CLK input", semi::ikclock::kPpqnNames,
		                                      [=]() { return (size_t) m->clock.ppqnIndex; },
		                                      [=](size_t i) { m->clock.ppqnIndex = (int) i; }));
		menu->addChild(createIndexSubmenuItem("Step (CLK / BUS)", kDivNames,
		                                      [=]() { return (size_t) m->divIndex; },
		                                      [=](size_t i) { m->divIndex = (int) i; }));
		menu->addChild(createIndexSubmenuItem("MUT input", {"Mutate all", "Mutate one random voice"},
		                                      [=]() { return (size_t) m->mutMode; },
		                                      [=](size_t i) { m->mutMode = (int) i; }));
	}
};

Model* modelIkedar = createModel<Ikedar, IkedarWidget>("Ikedar");
