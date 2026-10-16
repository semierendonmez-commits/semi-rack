// ILLUSORIUM — Rack / MetaModule port of ILLUSORIUM, the psychoacoustic illusion rack: 26 auditory
// illusions (Shepard, Risset, Deutsch, Huggins, Zwicker, Warren, Bregman, Haas, Franssen, ...).
// Engine: engines/illusorium (semierendonmez-commits/illusorium Source/engine; MetaModule-only
// lighter paths marked "SemiFamily" in the engine, see engines/illusorium/Module.h).
//
// One illusion at a time as a stereo effect (the plugin chains all 26; here ILLUSION selects one).
//   P1..P6 = the selected illusion's first four continuous parameters and its first two choices
//   (fewer when it has fewer); the PARAMS screen names them. Every illusion keeps its own settings
//   (saved in the patch), so switching back finds them where they were. The rest of its
//   parameters are in the context menu ("Illusion parameters").
//   MIX = the module's own wet / dry (the dry path is delayed by the illusion's latency).
//   GRAB = the illusion's capture / restart / reseed button (RISSET, SPEECH>SONG, PHANTOM WORDS,
//   CONST SPECTRUM, SCRAMBLE); MOD = CV onto one of P1..P6 (menu), 10 V = full range.
//   Rhythmic illusions follow CLK (1/16 notes by default) or, unpatched, the menu tempo.
//   Switching illusions fades the wet path out, swaps in a freshly prepared engine module (prepared
//   off the audio thread: Rack widget step / MetaModule AsyncThread) and fades back in.
//   Menu: test source (WHITE, PINK, SINE, SAW, CHORD, PULSES), added to or replacing IN.
//
// MetaModule (Cortex-A7): MASK (the 1024-point STFT codec model, ~4000 instructions / sample plus
// FFT bursts) is NOT available: selecting it passes the input dry and the screen says so.
// Lighter there: RISSET tape 10 s, CONST SPECTRUM <= 8 x 6 oscillators / 4 input copies,
// SHEPARD <= 4 voices.
//
// Rack audio is +-5 V; 5 V = 1.0 for the engine.

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/Sample.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/IllusoriumLayout.hpp"
#include "illusorium/Module.h"

#include <atomic>
#include <cstring>

namespace L = semi::layout::illusorium;
using semi::Screen;

namespace {

constexpr int kBlock = 32;          // engine block; adds 32 samples of latency
constexpr float kVoltsPerUnit = 5.f;
constexpr int kKnobs = 6;
constexpr int kMaxModules = 26;
constexpr int kMaxP = 16;
constexpr int kDryDelay = 2048;     // >= the largest module latency (MASK 1024) + a block

#ifdef METAMODULE
inline bool unavailable(int index) { return std::strcmp(il::moduleInfo(index).key, "msk") == 0; }
#else
inline bool unavailable(int) { return false; }
#endif

const std::vector<std::string> kPageNames = {"PITCH", "TIME", "SPACE", "PERCEPT"};
const std::vector<std::string> kPpqnNames = {"1 (quarter notes)", "2 (8ths)", "4 (16ths)", "8 (32nds)", "24 (DIN sync)"};
constexpr int kPpqn[] = {1, 2, 4, 8, 24};
constexpr float kTempos[] = {60, 70, 80, 90, 100, 110, 120, 128, 135, 140, 150, 160, 170, 174, 180};
const std::vector<std::string> kSourceNames = {"OFF", "WHITE", "PINK", "SINE 220 Hz", "SAW 110 Hz", "CHORD", "PULSES"};

// ---- parameter tables: knob position (0..1) <-> plain value, as the plugin's NormalisableRange
float toPlain(const il::PSpec& s, float n) {
	n = clamp(n, 0.f, 1.f);
	if (s.type != il::PType::Float)
		return std::round(s.lo + (s.hi - s.lo) * n);
	float v = s.lo + (s.hi - s.lo) * n;
	if (s.centre > s.lo && s.centre < s.hi) {
		const float skew = std::log(0.5f) / std::log((s.centre - s.lo) / (s.hi - s.lo));
		v = s.lo + (s.hi - s.lo) * std::pow(n, 1.f / skew);
	}
	return s.integer ? std::round(v) : v;
}
float toNorm(const il::PSpec& s, float v) {
	if (s.hi <= s.lo)
		return 0.f;
	const float x = clamp((v - s.lo) / (s.hi - s.lo), 0.f, 1.f);
	if (s.type == il::PType::Float && s.centre > s.lo && s.centre < s.hi) {
		const float skew = std::log(0.5f) / std::log((s.centre - s.lo) / (s.hi - s.lo));
		return std::pow(x, skew);
	}
	return x;
}
std::string choiceName(const il::PSpec& s, int i) {
	if (s.type == il::PType::Bool)
		return i ? "ON" : "OFF";
	const char* c = s.choices;
	for (int k = 0; k < i && *c; c++)
		if (*c == '|')
			k++;
	std::string out;
	for (; *c && *c != '|'; c++)
		out += *c;
	return out;
}
std::vector<std::string> choiceList(const il::PSpec& s) {
	std::vector<std::string> v;
	const int n = (int) std::round(s.hi - s.lo) + 1;
	for (int i = 0; i < n; i++)
		v.push_back(choiceName(s, i));
	return v;
}
std::string formatValue(const il::PSpec& s, float v) {
	if (s.type != il::PType::Float)
		return choiceName(s, (int) std::round(v));
	const float a = std::abs(v);
	std::string num = s.integer || a >= 100.f ? string::f("%.0f", v) : (a >= 10.f ? string::f("%.1f", v) : string::f("%.2f", v));
	if (std::strcmp(s.unit, "Hz") == 0 && a >= 1000.f)
		num = string::f("%.2fk", v / 1000.f);
	return num + (s.unit[0] ? std::string(" ") + s.unit : std::string());
}

// which PSpec each knob shows: the first four continuous ones, then the first two choices,
// topped up with whatever is left (in table order). -1 = unused.
struct KnobMap {
	int param[kKnobs];
	KnobMap() { std::fill(param, param + kKnobs, -1); }
	static KnobMap of(int index) {
		KnobMap m;
		const auto& ps = il::moduleInfo(index).params;
		const int n = std::min((int) ps.size(), kMaxP);
		int k = 0;
		bool used[kMaxP] = {};
		for (int i = 0; i < n && k < 4; i++)
			if (ps[i].type == il::PType::Float) {
				m.param[k++] = i;
				used[i] = true;
			}
		for (int i = 0; i < n && k < kKnobs; i++)
			if (ps[i].type != il::PType::Float && !used[i]) {
				m.param[k++] = i;
				used[i] = true;
			}
		for (int i = 0; i < n && k < kKnobs; i++)
			if (!used[i]) {
				m.param[k++] = i;
				used[i] = true;
			}
		return m;
	}
};

const KnobMap& knobMap(int index) {
	static KnobMap maps[kMaxModules];
	static bool built = false;
	if (!built) {
		for (int i = 0; i < std::min(kMaxModules, il::numModules()); i++)
			maps[i] = KnobMap::of(i);
		built = true;
	}
	return maps[clamp(index, 0, kMaxModules - 1)];
}

// A prepared engine module, handed from the worker to the audio thread.
struct Prepared {
	il::ModulePtr mod;
	int index = -1;
	float sr = 0.f;
};

struct Snapshot {
	int index = 4;
	bool ready = false, unavailable = false, playing = false;
	float viz[48] = {};
	float vals[kKnobs] = {};
	float mix = 1.f;
	float inLvl = 0.f, outLvl = 0.f, wet = 0.f;
	float t = 0.f;   // seconds, for the drawings that move with time
};

// the knobs show their parameter's name / value in Rack's tooltip
struct IllusionQuantity;

} // namespace

struct Illusorium : Module {
	enum ParamId { SELECT_PARAM, MIX_PARAM, ENUMS(P_PARAM, kKnobs), GRAB_PARAM, PARAMS_LEN };
	enum InputId { IN_L_INPUT, IN_R_INPUT, CLK_INPUT, RST_INPUT, GRAB_INPUT, MOD_INPUT, MIX_CV_INPUT, INPUTS_LEN };
	enum OutputId { OUT_L_OUTPUT, OUT_R_OUTPUT, OUTPUTS_LEN };
	enum LightId { GRAB_LIGHT, PHONES_LIGHT, ACTIVE_LIGHT, CLK_LIGHT, LIGHTS_LEN };

	// every illusion's settings (plain values, PSpec order); read by the audio thread
	float store[kMaxModules][kMaxP] = {};
	int cur = 4;                 // the illusion the knobs show (SHEPARD)
	int modTarget = 0;           // MOD -> P1..P6
	int source = 0, ppqnIdx = 2, tempoIdx = 6;
	bool sourceReplace = false;
	float sourceDb = -12.f;

	// ---- worker / hand-over
	semi::Slot<Prepared> slot;
	std::atomic_flag workerBusy = ATOMIC_FLAG_INIT;
	std::atomic<int> wanted{4};
	std::atomic<float> wantedSr{44100.f};
	int builtIndex = -1;
	float builtSr = 0.f;
#ifdef METAMODULE
	MetaModule::AsyncThread worker{this, [this]() { serviceWorker(); }};
#endif

	// ---- audio thread
	float switchGain = 0.f;      // fades the wet path around a swap
	dsp::BooleanTrigger grabTrig;
	dsp::SchmittTrigger grabInTrig, clkTrig, rstTrig;
	dsp::PulseGenerator grabFlash;
	int clkCount = -1;
	float sinceClk = 0.f, period = 0.f;
	bool clkSeen = false;
	double freePpq = 0.0;
	il::DelayLine dryL, dryR;
	float inBuf[2][kBlock]{}, outBuf[2][kBlock]{};
	int pos = 0;
	float inPeak = 0.f, outPeak = 0.f, wetLvl = 0.f;
	double clockSec = 0.0;
	il::Noise noise;
	double srcPhase[3] = {};
	float sampleRate = 44100.f;

	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;

	Illusorium();

	void initStore() {
		for (int m = 0; m < il::numModules() && m < kMaxModules; m++) {
			const auto& ps = il::moduleInfo(m).params;
			for (int k = 0; k < (int) ps.size() && k < kMaxP; k++)
				store[m][k] = ps[k].def;
		}
	}

	const il::PSpec* knobSpec(int knob, int index) const {
		const int p = knobMap(index).param[knob];
		return p >= 0 ? &il::moduleInfo(index).params[p] : nullptr;
	}

	// knobs <-> store
	void pullKnobs() {
		for (int k = 0; k < kKnobs; k++)
			if (const il::PSpec* s = knobSpec(k, cur))
				store[cur][knobMap(cur).param[k]] = toPlain(*s, params[P_PARAM + k].getValue());
	}
	void pushKnobs() {
		for (int k = 0; k < kKnobs; k++) {
			const il::PSpec* s = knobSpec(k, cur);
			params[P_PARAM + k].setValue(s ? toNorm(*s, store[cur][knobMap(cur).param[k]]) : 0.f);
		}
	}
	int selected() { return clamp((int) std::round(params[SELECT_PARAM].getValue()), 0, il::numModules() - 1); }

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		initStore();
		cur = selected();
		pushKnobs();
		modTarget = 0;
		source = 0;
		sourceReplace = false;
		sourceDb = -12.f;
		ppqnIdx = 2;
		tempoIdx = 6;
		clkCount = -1;
		freePpq = 0.0;
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		sampleRate = e.sampleRate;
		wantedSr = e.sampleRate;   // the worker prepares a new module at this rate
		pos = 0;
	}

	// ---- worker side (Rack: widget step; MetaModule: AsyncThread) -----------------------------------
	void serviceWorker() {
		if (workerBusy.test_and_set(std::memory_order_acquire))
			return;
		slot.collect();
		const int w = wanted.load(std::memory_order_relaxed);
		const float sr = wantedSr.load(std::memory_order_relaxed);
		if ((w != builtIndex || sr != builtSr) && !unavailable(w)) {
			auto* p = new Prepared;
			p->mod = il::createModule(w);
			p->mod->prepare(sr, kBlock);
			p->index = w;
			p->sr = sr;
			slot.offer(p);
			builtIndex = w;
			builtSr = sr;
		}
		else if (unavailable(w)) {
			builtIndex = w;
		}
		workerBusy.clear(std::memory_order_release);
	}

	// ---- JSON ---------------------------------------------------------------------------------------------
	json_t* dataToJson() override {
		pullKnobs();
		json_t* root = json_object();
		json_t* all = json_object();
		for (int m = 0; m < il::numModules() && m < kMaxModules; m++) {
			const il::MInfo& info = il::moduleInfo(m);
			json_t* o = json_object();
			for (int k = 0; k < (int) info.params.size() && k < kMaxP; k++)
				json_object_set_new(o, info.params[k].id, json_real(store[m][k]));
			json_object_set_new(all, info.key, o);
		}
		json_object_set_new(root, "illusions", all);
		json_object_set_new(root, "modTarget", json_integer(modTarget));
		json_object_set_new(root, "source", json_integer(source));
		json_object_set_new(root, "sourceReplace", json_boolean(sourceReplace));
		json_object_set_new(root, "sourceDb", json_real(sourceDb));
		json_object_set_new(root, "ppqn", json_integer(ppqnIdx));
		json_object_set_new(root, "tempo", json_integer(tempoIdx));
		return root;
	}

	void dataFromJson(json_t* root) override {
		if (json_t* all = json_object_get(root, "illusions"))
			for (int m = 0; m < il::numModules() && m < kMaxModules; m++) {
				const il::MInfo& info = il::moduleInfo(m);
				if (json_t* o = json_object_get(all, info.key))
					for (int k = 0; k < (int) info.params.size() && k < kMaxP; k++)
						if (json_t* v = json_object_get(o, info.params[k].id))
							store[m][k] = clamp((float) json_number_value(v), info.params[k].lo, info.params[k].hi);
			}
		auto geti = [&](const char* k, int lo, int hi, int def) {
			json_t* j = json_object_get(root, k);
			return j ? clamp((int) json_integer_value(j), lo, hi) : def;
		};
		modTarget = geti("modTarget", 0, kKnobs - 1, 0);
		source = geti("source", 0, (int) kSourceNames.size() - 1, 0);
		if (json_t* j = json_object_get(root, "sourceReplace"))
			sourceReplace = json_boolean_value(j);
		if (json_t* j = json_object_get(root, "sourceDb"))
			sourceDb = clamp((float) json_number_value(j), -48.f, 0.f);
		ppqnIdx = geti("ppqn", 0, 4, 2);
		tempoIdx = geti("tempo", 0, (int) (sizeof kTempos / sizeof kTempos[0]) - 1, 6);
		cur = selected();
		wanted = cur;
		pushKnobs();
	}

	// ---- clock ------------------------------------------------------------------------------------------
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
			clkSeen = false;
			period = 0.f;
		}
	}

	void blockCtx(il::Ctx& c) {
		c.sr = sampleRate;
		if (!inputs[CLK_INPUT].isConnected()) {
			c.bpm = kTempos[clamp(tempoIdx, 0, (int) (sizeof kTempos / sizeof kTempos[0]) - 1)];
			c.ppq = freePpq;
			c.playing = true;
			freePpq += kBlock * c.bpm / 60.0 / sampleRate;
			return;
		}
		const int ppqn = kPpqn[ppqnIdx];
		if (clkCount < 0 || period <= 0.f) {
			c.bpm = 120.0;
			c.ppq = std::max(0, clkCount) / (double) ppqn;
			c.playing = false;
			return;
		}
		c.bpm = std::clamp(60.0 / (period * ppqn), 20.0, 400.0);
		c.ppq = (clkCount + std::min(sinceClk / period, 0.999f)) / ppqn;
		c.playing = true;
	}

	// ---- test source --------------------------------------------------------------------------------------
	float testSample(double t) {
		const double sr = sampleRate;
		switch (source) {
			case 1: return noise.white() * 0.5f;
			case 2: return noise.pink() * 1.5f;
			case 3: srcPhase[0] = il::fracd(srcPhase[0] + 220.0 / sr); return 0.7f * std::sin(il::kTwoPi * (float) srcPhase[0]);
			case 4: srcPhase[0] = il::fracd(srcPhase[0] + 110.0 / sr); return 0.5f * (float) (2.0 * srcPhase[0] - 1.0);
			case 5: {
				static const double f[3] = {220.0, 277.18, 329.63};
				float x = 0.f;
				for (int k = 0; k < 3; k++) {
					srcPhase[k] = il::fracd(srcPhase[k] + f[k] / sr);
					x += 0.2f * (float) (2.0 * srcPhase[k] - 1.0);
				}
				return x;
			}
			case 6: {   // a click every 1/8 at the current tempo position
				const double ph = il::fracd(t * 4.0);
				return ph < 0.02 ? (float) std::exp(-ph * 400.0) * (noise.white() * 0.5f + 0.5f) : 0.f;
			}
			default: return 0.f;
		}
	}

	// ---- audio --------------------------------------------------------------------------------------------
	void runBlock() {
		il::Ctx ctx;
		blockCtx(ctx);

		// selection: fade out, swap when the new module is ready, fade in
		const int sel = selected();
		if (sel != cur) {
			pullKnobs();
			cur = sel;
			pushKnobs();
			wanted = cur;
		}
		Prepared* act = slot.active;
		const bool stale = !act || act->index != cur || act->sr != sampleRate;
		const float fade = (float) kBlock / (0.01f * sampleRate);   // 10 ms
		if (stale) {
			switchGain = std::max(0.f, switchGain - fade);
			if (switchGain <= 0.f && slot.poll())
				act = slot.active;
		}
		const bool live = act && act->index == cur && act->sr == sampleRate && !unavailable(cur);
		if (live && !(switchGain > 0.f && stale))
			switchGain = std::min(1.f, switchGain + fade);

		// parameters: knobs (+ MOD) into the store, the store into the module
		pullKnobs();
		if (inputs[MOD_INPUT].isConnected())
			if (const il::PSpec* s = knobSpec(modTarget, cur)) {
				const float n = params[P_PARAM + modTarget].getValue() + inputs[MOD_INPUT].getVoltage() / 10.f;
				store[cur][knobMap(cur).param[modTarget]] = toPlain(*s, n);
			}
		const float mix = clamp(params[MIX_PARAM].getValue() + inputs[MIX_CV_INPUT].getVoltage() / 10.f, 0.f, 1.f) * switchGain;

		// the active module keeps playing (with its own settings) while it fades out
		float wet[2][kBlock];
		std::memcpy(wet, inBuf, sizeof wet);
		int lat = 0;
		if (act && act->mod && switchGain > 0.f && !unavailable(act->index)) {
			il::Module& m = *act->mod;
			const int np = std::min((int) m.info().params.size(), kMaxP);
			for (int k = 0; k < np; k++)
				m.p[k] = store[act->index][k];
			m.process(wet[0], wet[1], kBlock, ctx);
			lat = m.latency();
		}
		float diff = 0.f;
		for (int i = 0; i < kBlock; i++) {
			dryL.push(inBuf[0][i]);
			dryR.push(inBuf[1][i]);
			const float dl = dryL.at(lat), dr = dryR.at(lat);
			float l = wet[0][i], r = wet[1][i];
			if (!std::isfinite(l) || !std::isfinite(r)) {
				l = dl;
				r = dr;
				if (act && act->mod)
					act->mod->reset();
			}
			outBuf[0][i] = dl + (l - dl) * mix;
			outBuf[1][i] = dr + (r - dr) * mix;
			diff = std::max(diff, std::abs(outBuf[0][i] - dl) + std::abs(outBuf[1][i] - dr));
			outPeak = std::max(outPeak, std::max(std::abs(outBuf[0][i]), std::abs(outBuf[1][i])));
		}
		wetLvl = std::max(wetLvl, diff);
		clockSec += kBlock / (double) sampleRate;
	}

	void process(const ProcessArgs& args) override {
		sampleRate = args.sampleRate;
		if (wantedSr.load(std::memory_order_relaxed) != sampleRate)
			wantedSr = sampleRate;
		processClockEdges(args.sampleTime);
		if (grabTrig.process(params[GRAB_PARAM].getValue() > 0.f) | grabInTrig.process(inputs[GRAB_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (slot.active && slot.active->mod && slot.active->index == cur) {
				slot.active->mod->trigger();
				grabFlash.trigger(0.15f);
			}
		}

		float l = inputs[IN_L_INPUT].getVoltage() / kVoltsPerUnit;
		float r = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() / kVoltsPerUnit : l;
		if (source > 0) {
			const float s = testSample(freePpq + pos * 2.0 / sampleRate) * std::pow(10.f, sourceDb / 20.f) * 2.f;
			l = sourceReplace ? s : l + s;
			r = sourceReplace ? s : r + s;
		}
		inPeak = std::max(inPeak, std::max(std::abs(l), std::abs(r)));
		inBuf[0][pos] = l;
		inBuf[1][pos] = r;
		outputs[OUT_L_OUTPUT].setVoltage(outBuf[0][pos] * kVoltsPerUnit);
		outputs[OUT_R_OUTPUT].setVoltage(outBuf[1][pos] * kVoltsPerUnit);
		if (++pos >= kBlock) {
			pos = 0;
			runBlock();
		}

		const bool flash = grabFlash.process(args.sampleTime);
		if (ui.tick(args.sampleTime)) {
			const il::MInfo& info = il::moduleInfo(cur);
			lights[GRAB_LIGHT].setBrightness(flash ? 1.f : (info.grab ? 0.25f : 0.f));
			lights[PHONES_LIGHT].setBrightness(info.phones ? 1.f : 0.f);
			lights[ACTIVE_LIGHT].setBrightness(clamp(wetLvl * 4.f, 0.f, 1.f));
			lights[CLK_LIGHT].setBrightness(inputs[CLK_INPUT].isConnected() && period > 0.f ? 1.f : 0.f);
			Snapshot& s = snap.writeSlot();
			s.index = cur;
			s.unavailable = unavailable(cur);
			Prepared* act = slot.active;
			s.ready = act && act->index == cur;
			if (s.ready)
				for (int i = 0; i < 48; i++)
					s.viz[i] = act->mod->viz[i].load(std::memory_order_relaxed);
			for (int k = 0; k < kKnobs; k++) {
				const int p = knobMap(cur).param[k];
				s.vals[k] = p >= 0 ? store[cur][p] : 0.f;
			}
			s.mix = params[MIX_PARAM].getValue();
			s.inLvl = inPeak;
			s.outLvl = outPeak;
			s.wet = wetLvl;
			s.t = (float) clockSec;
			snap.publish();
			inPeak = outPeak = wetLvl = 0.f;
		}
	}
};

namespace {

struct IllusionQuantity : ParamQuantity {
	int knob = 0;
	const il::PSpec* spec() {
		auto* m = dynamic_cast<Illusorium*>(module);
		return m ? m->knobSpec(knob, m->cur) : nullptr;
	}
	std::string getLabel() override {
		const il::PSpec* s = spec();
		return s ? string::f("P%d: %s", knob + 1, s->name) : string::f("P%d (unused)", knob + 1);
	}
	std::string getDisplayValueString() override {
		const il::PSpec* s = spec();
		return s ? formatValue(*s, toPlain(*s, getValue())) : std::string("-");
	}
};

} // namespace

Illusorium::Illusorium() {
	config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
	std::vector<std::string> names;
	for (int m = 0; m < il::numModules(); m++)
		names.push_back(std::string(il::moduleInfo(m).name) + (unavailable(m) ? " (not on MetaModule)" : ""));
	configSwitch(SELECT_PARAM, 0.f, (float) (il::numModules() - 1), 4.f, "Illusion", names);
	configParam(MIX_PARAM, 0.f, 1.f, 1.f, "Mix", " %", 0.f, 100.f);
	for (int k = 0; k < kKnobs; k++) {
		auto* q = configParam<IllusionQuantity>(P_PARAM + k, 0.f, 1.f, 0.5f, string::f("P%d", k + 1));
		q->knob = k;
	}
	configButton(GRAB_PARAM, "Grab / restart / reseed");
	configInput(IN_L_INPUT, "Left");
	configInput(IN_R_INPUT, "Right (normalled to left)");
	configInput(CLK_INPUT, "Clock (1/16 notes by default)");
	configInput(RST_INPUT, "Reset");
	configInput(GRAB_INPUT, "Grab trigger");
	configInput(MOD_INPUT, "Mod CV onto P1..P6 (menu), 10 V = full range");
	configInput(MIX_CV_INPUT, "Mix CV (10 V = 100 %)");
	configOutput(OUT_L_OUTPUT, "Left");
	configOutput(OUT_R_OUTPUT, "Right");
	configLight(PHONES_LIGHT, "Needs headphones");
	configLight(ACTIVE_LIGHT, "Wet signal");
	configLight(CLK_LIGHT, "Clock");
	configBypass(IN_L_INPUT, OUT_L_OUTPUT);
	configBypass(IN_R_INPUT, OUT_R_OUTPUT);
	dryL.prepare(kDryDelay);
	dryR.prepare(kDryDelay);
	initStore();
	pushKnobs();
	serviceWorker();   // the default illusion, prepared now
	slot.poll();
	switchGain = 1.f;
#ifdef METAMODULE
	worker.start();
#endif
}

// ---------------------------------------------------------------------------------------------- screens

namespace {

// word wrap for the monospaced screen font (ShareTechMono ~0.5 em per character)
std::vector<std::string> wrap(const char* s, int cols, int maxLines) {
	std::vector<std::string> out;
	std::string line, word;
	auto flushWord = [&]() {
		if (word.empty())
			return;
		if (!line.empty() && (int) (line.size() + 1 + word.size()) > cols) {
			out.push_back(line);
			line.clear();
		}
		line += (line.empty() ? "" : " ") + word;
		word.clear();
	};
	for (const char* c = s; *c; c++) {
		if (*c == ' ')
			flushWord();
		else
			word += *c;
	}
	flushWord();
	if (!line.empty())
		out.push_back(line);
	if ((int) out.size() > maxLines) {
		out.resize(maxLines);
		std::string& last = out.back();
		if ((int) last.size() > cols - 2)
			last.resize(std::max(0, cols - 2));
		last += "..";
	}
	return out;
}

NVGcolor pageColor(int page) {
	switch (page) {
		case il::pagePitch: return semi::theme::cyan();
		case il::pageTime: return semi::theme::amber();
		case il::pageSpace: return semi::theme::magenta();
		default: return semi::theme::text();
	}
}

} // namespace

// The illusion: number, name and page; who described it; a drawing of the percept from the
// module's own read-outs (SHEPARD barber pole, PHANTOM harmonics, TRITONE pitch-class clock;
// the others: the signal / percept pair as text over the in / out / wet meters); "you hear".
struct IllusionScreen : Screen {
	Illusorium* module = nullptr;
	Snapshot demo;

	IllusionScreen() {
		demo.ready = true;
		demo.viz[0] = 0.3f;
		demo.viz[1] = 5.f;
		demo.viz[19] = 0.f;
		demo.viz[21] = 12.f;
		demo.inLvl = 0.5f;
		demo.outLvl = 0.4f;
		demo.wet = 0.2f;
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const il::MInfo& info = il::moduleInfo(s.index);
		const float w = box.size.x, h = box.size.y, pad = 3.f;
		const float fs = 8.f, lh = std::max(fs, semi::platform::kMinText) + 1.5f;
		const int cols = (int) ((w - 2 * pad) / (std::max(fs, semi::platform::kMinText) * 0.52f));
		const NVGcolor pc = pageColor(info.page);

		text(vg, pad, pad - 1.f, string::f("%02d %s", s.index + 1, info.name), 10.f, pc);
		text(vg, w - pad, pad - 1.f, kPageNames[clamp(info.page, 0, 3)] + (info.phones ? " HP" : ""), fs,
		     info.phones ? semi::theme::magenta() : semi::theme::dim(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		const auto credit = wrap(info.illusion, cols, 1);
		if (!credit.empty())
			text(vg, pad, pad + 11.f, credit[0], fs - 1.f, semi::theme::dim());

		const float y0 = pad + 11.f + lh + 1.f;
		const auto hear = wrap(info.perceived, cols, 2);
		const float y1 = h - pad - lh * hear.size() - 1.f;
		for (size_t i = 0; i < hear.size(); i++)
			text(vg, pad, y1 + 1.f + lh * i, hear[i], fs, semi::theme::text());

		if (s.unavailable) {
			text(vg, w * 0.5f, (y0 + y1) * 0.5f, "NOT ON METAMODULE: DRY", 10.f, semi::theme::magenta(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			return;
		}
		const std::string key = info.key;
		if (s.ready && key == "shep")
			shepard(vg, s, pad, y0, w - pad, y1);
		else if (s.ready && key == "pha")
			phantom(vg, s, pad, y0, w - pad, y1);
		else if (s.ready && key == "tri")
			tritone(vg, s, pad, y0, w - pad, y1);
		else
			generic(vg, s, info, pad, y0, w - pad, y1, cols);
	}

	void meters(NVGcontext* vg, const Snapshot& s, float x0, float y0, float x1, float y1) {
		const float bw = (x1 - x0 - 4.f) / 3.f;
		const float lv[3] = {s.inLvl, s.outLvl, s.wet};
		const NVGcolor c[3] = {semi::theme::dim(), semi::theme::cyan(), semi::theme::amber()};
		for (int i = 0; i < 3; i++) {
			const float x = x0 + i * (bw + 2.f);
			fillRect(vg, x, y0, bw, y1 - y0, semi::theme::cell());
			const float v = clamp(lv[i], 0.f, 1.f);
			fillRect(vg, x, y1 - (y1 - y0) * v, bw, (y1 - y0) * v, c[i]);
		}
	}

	void generic(NVGcontext* vg, const Snapshot& s, const il::MInfo& info, float x0, float y0, float x1, float y1, int cols) {
		const float mw = 22.f;
		meters(vg, s, x1 - mw, y0, x1, y1);
		const float lh = std::max(8.f, semi::platform::kMinText) + 1.5f;
		const int c = std::max(10, cols - (int) (mw / 4.2f) - 1);
		const int lines = std::max(1, (int) ((y1 - y0) / lh));
		text(vg, x0, y0, "IN THE SIGNAL", 7.f, semi::theme::dim());
		const auto phys = wrap(info.physical, c, std::max(1, lines - 1));
		for (size_t i = 0; i < phys.size(); i++)
			text(vg, x0, y0 + lh * (i + 1), phys[i], 8.f, semi::theme::cyanDim());
	}

	// SHEPARD: each voice's position on the pitch axis under the fixed envelope (bar width = gain)
	void shepard(NVGcontext* vg, const Snapshot& s, float x0, float y0, float x1, float y1) {
		const int N = clamp((int) s.viz[1], 2, 8);
		const float pos = s.viz[0];
		const int dir = (int) s.viz[19];
		const float ex = x0 + 18.f;
		// envelope
		float xs[24], ys[24];
		for (int i = 0; i < 24; i++) {
			const float u = i / 23.f;
			xs[i] = x0 + 16.f * il::sqr(std::sin(il::kPi * u));
			ys[i] = y1 - (y1 - y0) * u;
		}
		path(vg, xs, ys, 24, semi::theme::magenta());
		// the voices' trails: 24 columns back in time
		const int colsN = 24;
		const float cw = (x1 - ex - 24.f) / colsN;
		const float sgn = dir == 0 ? 1.f : (dir == 1 ? -1.f : 0.f);
		for (int c = 0; c < colsN; c++) {
			const float back = (1.f - (float) c / (colsN - 1)) * 1.f;
			for (int k = 0; k < N; k++) {
				const float u = il::frac(((float) k + pos - sgn * back) / (float) N);
				const float g = il::sqr(std::sin(il::kPi * u));
				const NVGcolor col = semi::theme::cyan();
				const float kk = 0.2f + 0.8f * g;
				fillRect(vg, ex + c * cw, y1 - (y1 - y0) * u - 1.5f, cw, 3.f, nvgRGBf(col.r * kk, col.g * kk, col.b * kk));
			}
		}
		for (int k = 0; k < N; k++) {
			const float u = il::frac(((float) k + pos) / (float) N), g = il::sqr(std::sin(il::kPi * u));
			nvgBeginPath(vg);
			nvgCircle(vg, x1 - 10.f, y1 - (y1 - y0) * u, 1.5f + 3.f * g);
			nvgFillColor(vg, semi::theme::amber());
			nvgFill(vg);
		}
		text(vg, x1, y0, dir == 0 ? "UP FOREVER" : (dir == 1 ? "DOWN FOREVER" : "FROZEN"), 8.f, semi::theme::amber(),
		     NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
	}

	// PHANTOM: harmonics 2f..9f as bars, the missing fundamental as a dashed ghost column
	void phantom(NVGcontext* vg, const Snapshot& s, float x0, float y0, float x1, float y1) {
		float f0 = s.viz[0];
		if (f0 < 20.f || f0 > 400.f)
			f0 = s.viz[4] * 0.55f;
		const float harm = clamp(s.viz[2] * 10.f + 0.15f, 0.f, 1.f), remove = s.viz[3];
		const int H = 9;
		const float bw = (x1 - x0) / H;
		for (int k = 1; k <= H; k++) {
			const float x = x0 + (k - 1) * bw + bw * 0.2f, cw = bw * 0.6f;
			if (k == 1) {
				outline(vg, x, y0 + 2.f, cw, y1 - y0 - 2.f, semi::theme::magenta());
				const float real = (1.f - remove) * (y1 - y0 - 4.f) * 0.8f;
				if (real > 1.f)
					fillRect(vg, x, y1 - real, cw, real, semi::theme::magentaDeep());
				text(vg, x + cw * 0.5f, y0 + 4.f, string::f("%.0f", f0), 7.f, semi::theme::magenta(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
			}
			else {
				const float a = clamp(harm * 1.1f / (0.4f + 0.3f * k), 0.02f, 1.f);
				fillRect(vg, x, y1 - (y1 - y0) * a, cw, (y1 - y0) * a, semi::theme::cyan());
			}
		}
	}

	// TRITONE: the 12 pitch classes as a clock, the envelope's centre lit, the current step in amber
	void tritone(NVGcontext* vg, const Snapshot& s, float x0, float y0, float x1, float y1) {
		const float cur = s.viz[0], centre = std::max(20.f, s.viz[2]), iv = s.viz[3];
		const float r = (y1 - y0) * 0.42f;
		const float cx = x0 + r + 6.f, cy = (y0 + y1) * 0.5f;
		const float env = std::fmod(il::hzToMidi(centre), 12.f);
		for (int i = 0; i < 12; i++) {
			const float a = il::kTwoPi * i / 12.f - il::kPi * 0.5f;
			const float near = 1.f - std::min(1.f, std::abs(std::remainder((float) i - env, 12.f)) / 3.f);
			const NVGcolor c = semi::theme::cyan();
			const float k = 0.25f + 0.75f * near;
			nvgBeginPath(vg);
			nvgCircle(vg, cx + r * std::cos(a), cy + r * std::sin(a), 2.2f);
			nvgFillColor(vg, nvgRGBf(c.r * k, c.g * k, c.b * k));
			nvgFill(vg);
		}
		for (float st : {0.f, cur}) {
			const float a = il::kTwoPi * st / 12.f - il::kPi * 0.5f;
			nvgBeginPath(vg);
			nvgCircle(vg, cx + r * std::cos(a), cy + r * std::sin(a), st == 0.f ? 3.f : 4.f);
			nvgFillColor(vg, st == 0.f ? semi::theme::text() : semi::theme::amber());
			nvgFill(vg);
		}
		const float tx = cx + r + 12.f;
		text(vg, tx, cy - 9.f, string::f("UP +%.0f", iv), 9.f, semi::theme::cyan(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		text(vg, tx, cy + 3.f, string::f("OR DOWN -%.0f ?", 12.f - iv), 9.f, semi::theme::magenta(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		(void) x1;
	}

	static void outline(NVGcontext* vg, float x, float y, float w, float h, NVGcolor c) {
		nvgBeginPath(vg);
		nvgRect(vg, x, y, w, h);
		nvgStrokeColor(vg, c);
		nvgStrokeWidth(vg, semi::platform::kThinStroke);
		nvgStroke(vg);
	}
};

// P1..P6: name and value of each knob for the selected illusion (2 rows x 3).
struct ParamsScreen : Screen {
	Illusorium* module = nullptr;
	Snapshot demo;

	ParamsScreen() {
		demo.index = 4;
		const auto& ps = il::moduleInfo(4).params;
		for (int k = 0; k < kKnobs; k++) {
			const int p = knobMap(4).param[k];
			demo.vals[k] = p >= 0 ? ps[p].def : 0.f;
		}
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const il::MInfo& info = il::moduleInfo(s.index);
		const float w = box.size.x, h = box.size.y, pad = 2.f;
		const float cw = (w - 2 * pad) / 3.f, rh = (h - 2 * pad) / 2.f;
		const int chars = (int) (cw / (std::max(8.f, semi::platform::kMinText) * 0.52f)) - 1;
		for (int k = 0; k < kKnobs; k++) {
			const int p = knobMap(s.index).param[k];
			const float x = pad + (k % 3) * cw, y = pad + (k / 3) * rh + rh * 0.5f;
			text(vg, x, y, string::f("P%d", k + 1), 8.f, k == 5 ? semi::theme::magenta() : semi::theme::dim(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			if (p < 0) {
				text(vg, x + 12.f, y, "-", 8.f, semi::theme::dim(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
				continue;
			}
			const il::PSpec& sp = info.params[p];
			std::string line = std::string(sp.name) + " " + formatValue(sp, s.vals[k]);
			if ((int) line.size() > chars)
				line.resize(std::max(0, chars));
			text(vg, x + 12.f, y, line, 8.f, semi::theme::text(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		}
	}
};

// ---------------------------------------------------------------------------------------------- menu

namespace {

template <typename T>
void addIndex(Menu* menu, std::string label, const std::vector<std::string>& labels, T* v) {
	menu->addChild(createIndexSubmenuItem(label, labels,
		[=]() { return (size_t) *v; },
		[=](size_t i) { *v = (T) i; }));
}

// one PSpec of the current illusion as a fixed-step menu item (works on both platforms)
void addSpec(Menu* menu, Illusorium* m, int index, int p) {
	const il::PSpec& s = il::moduleInfo(index).params[p];
	float* v = &m->store[index][p];
	if (s.type == il::PType::Bool) {
		menu->addChild(createCheckMenuItem(s.name, "", [=]() { return *v > 0.5f; }, [=]() { *v = *v > 0.5f ? 0.f : 1.f; }));
		return;
	}
	std::vector<std::string> labels;
	std::vector<float> values;
	if (s.type == il::PType::Choice) {
		labels = choiceList(s);
		for (size_t i = 0; i < labels.size(); i++)
			values.push_back(s.lo + (float) i);
	}
	else {
		const int steps = s.integer ? std::min(17, (int) (s.hi - s.lo) + 1) : 11;
		for (int i = 0; i < steps; i++) {
			const float val = toPlain(s, i / (float) (steps - 1));
			values.push_back(val);
			labels.push_back(formatValue(s, val));
		}
	}
	menu->addChild(createIndexSubmenuItem(s.name, labels,
		[=]() {
			size_t best = 0;
			for (size_t i = 1; i < values.size(); i++)
				if (std::abs(values[i] - *v) < std::abs(values[best] - *v))
					best = i;
			return best;
		},
		[=](size_t i) { *v = values[i]; }));
}

} // namespace

struct IllusoriumWidget : ModuleWidget {
	IllusoriumWidget(Illusorium* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Illusorium.svg")));

		auto* ill = semi::createScreenMm<IllusionScreen>(Vec(L::ILLUSION.x, L::ILLUSION.y), Vec(L::ILLUSION.w, L::ILLUSION.h));
		ill->module = module;
		addChild(ill);
		auto* ps = semi::createScreenMm<ParamsScreen>(Vec(L::PARAMS.x, L::PARAMS.y), Vec(L::PARAMS.w, L::PARAMS.h));
		ps->module = module;
		addChild(ps);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		auto* sel = createParamCentered<semi::KnobL>(at(L::SELECT), module, Illusorium::SELECT_PARAM);
		sel->snap = true;
		addParam(sel);
		addParam(createParamCentered<semi::KnobM>(at(L::MIX), module, Illusorium::MIX_PARAM));
		const L::P pk[kKnobs] = {L::P1, L::P2, L::P3, L::P4, L::P5, L::P6};
		for (int k = 0; k < kKnobs; k++) {
			if (k == 5)
				addParam(createParamCentered<semi::KnobMHot>(at(pk[k]), module, Illusorium::P_PARAM + k));
			else
				addParam(createParamCentered<semi::KnobM>(at(pk[k]), module, Illusorium::P_PARAM + k));
		}
		addParam(createParamCentered<semi::Button>(at(L::GRAB), module, Illusorium::GRAB_PARAM));
		addChild(createLightCentered<semi::AmberLedSmall>(at(L::GRAB_LED), module, Illusorium::GRAB_LIGHT));
		addChild(createLightCentered<semi::MagentaLedSmall>(at(L::PHONES), module, Illusorium::PHONES_LIGHT));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::ACTIVE), module, Illusorium::ACTIVE_LIGHT));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::CLK_LED), module, Illusorium::CLK_LIGHT));

		const L::P ins[7] = {L::IN_L, L::IN_R, L::CLK, L::RST, L::GRAB_IN, L::MOD, L::MIX_CV};
		for (int i = 0; i < 7; i++)
			addInput(createInputCentered<semi::Jack>(at(ins[i]), module, Illusorium::IN_L_INPUT + i));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_L), module, Illusorium::OUT_L_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_R), module, Illusorium::OUT_R_OUTPUT));
	}

#ifndef METAMODULE
	void step() override {
		if (auto* m = dynamic_cast<Illusorium*>(module))
			m->serviceWorker();
		ModuleWidget::step();
	}
#endif

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Illusorium*>(module);
		if (!m)
			return;
		const int index = m->cur;
		const il::MInfo& info = il::moduleInfo(index);

		menu->addChild(new MenuSeparator);
		for (int pg = 0; pg < il::numPages; pg++) {
			menu->addChild(createSubmenuItem(kPageNames[pg], "", [=](Menu* sub) {
				for (int i = 0; i < il::numModules(); i++) {
					if (il::moduleInfo(i).page != pg)
						continue;
					std::string name = std::string(il::moduleInfo(i).name) + (unavailable(i) ? " (not on MetaModule)" : "");
					sub->addChild(createCheckMenuItem(name, "", [=]() { return m->cur == i; },
						[=]() { m->params[Illusorium::SELECT_PARAM].setValue((float) i); }));
				}
			}));
		}

		menu->addChild(createSubmenuItem(std::string("Illusion parameters: ") + info.name, "", [=](Menu* sub) {
			const KnobMap& km = knobMap(index);
			const int n = std::min((int) info.params.size(), kMaxP);
			bool any = false;
			for (int p = 0; p < n; p++) {
				bool onKnob = false;
				for (int k = 0; k < kKnobs; k++)
					onKnob |= km.param[k] == p;
				if (!onKnob) {
					addSpec(sub, m, index, p);
					any = true;
				}
			}
			if (!any)
				sub->addChild(createMenuLabel("(all on P1..P6)"));
			sub->addChild(new MenuSeparator);
			sub->addChild(createMenuItem("Reset this illusion", "", [=]() {
				for (int p = 0; p < n; p++)
					m->store[index][p] = info.params[p].def;
				m->pushKnobs();
			}));
		}));

		std::vector<std::string> targets;
		for (int k = 0; k < kKnobs; k++) {
			const il::PSpec* s = m->knobSpec(k, index);
			targets.push_back(string::f("P%d", k + 1) + (s ? std::string(" ") + s->name : std::string()));
		}
		addIndex(menu, "MOD input target", targets, &m->modTarget);

		menu->addChild(createSubmenuItem("Test source", "", [=](Menu* sub) {
			addIndex(sub, "Source", kSourceNames, &m->source);
			std::vector<std::string> lv;
			for (int d = -48; d <= 0; d += 6)
				lv.push_back(string::f("%d dB", d));
			sub->addChild(createIndexSubmenuItem("Level", lv,
				[=]() { return (size_t) clamp((int) std::round((m->sourceDb + 48.f) / 6.f), 0, 8); },
				[=](size_t i) { m->sourceDb = -48.f + 6.f * (float) i; }));
			sub->addChild(createBoolPtrMenuItem("Replace the input", "", &m->sourceReplace));
		}));

		menu->addChild(createSubmenuItem("Clock", "", [=](Menu* sub) {
			addIndex(sub, "CLK input PPQN", kPpqnNames, &m->ppqnIdx);
			std::vector<std::string> t;
			for (float b : kTempos)
				t.push_back(string::f("%.0f BPM", b));
			addIndex(sub, "Tempo (CLK unpatched)", t, &m->tempoIdx);
		}));
	}
};

Model* modelIllusorium = createModel<Illusorium, IllusoriumWidget>("Illusorium");
