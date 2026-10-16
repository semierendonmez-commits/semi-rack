// SPLICEBUS — Rack / MetaModule port of SPLICEBUS 0.1 (FLOATER's REEL as a bus effect over SUBSTRATE's
// split, with TURNAROUND's form; engine: engines/splicebus, vendored from semierendonmez-commits/splicebus
// Source/engine; the plug-in's parameter layout, 14 presets and MUTATE in engines/splicebus/Patch).
//
// Stereo effect: IN L/R (R normalled to L) -> SPLIT (OFF / FIXED / TRACK) -> the REEL above it -> OUT L/R.
// SC = the sidechain (mono, for PROTECT BY SC HITS / SC LEVEL).
// Position (no host transport): CLK = 1/16 notes, RST = bar 1 on the next clock; or BUS (FamilyBus CV:
// POS gives the bar, ENERGY scales CHAOS); without either, FREE RUN at BPM (menu), else a wire as the
// plug-in with the host stopped. BRK = 10 V while a gene breaks; TURN = 10 V inside a turn window.
// Panel: the reel and form macros, SPLIT mode, HOLD. Menu: LENGTH, GENE, BUDGET, WALL, ANCHOR, LOOP, SEED,
// RATIO, F0 MAX, PROTECT BY / RELEASE / SENS, FREE RUN + BPM, presets, MUTATE, NEW SEED.
// Dropped: user preset files, UNDO, themes, the SPLIT screen (its readout is on the TAPE screen).
// Latency: the plug-in's (3 ms in OFF / FIXED, ~60 ms in TRACK) + 64 samples of block.

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/FamilyBusCV.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/SplicebusLayout.hpp"
#include "splicebus/Patch.h"

#include <cstring>
#include <random>

namespace L = semi::layout::splicebus;
using semi::Screen;
namespace bus = semi::bus;

namespace {

constexpr int kBlock = 64;
constexpr float kVolts = 5.f;
constexpr int kCols = 96;

struct Snapshot {
	float peaks[kCols]{};
	float genePos[spb::Reel::kGenes]{};
	int splices = 0, genes = 0, broken = 0;
	bool valid = false, held = false, breaking = false, walled = false, running = false;
	int split = 1;
	float splitHz = 0.f, f0 = 0.f, guard = 0.f, bpm = 174.f;
	int64_t sixteenth = 0;
	// FORM: 32 bars (bit flags) and this line / the next, slot codes
	uint8_t bars[32]{};            // bit0 turn bar, bits1-2 tier, bit3 wall, bit4 anchor
	uint8_t cur[128]{}, nxt[128]{};  // 0 dot, 1 break, 2 turn, 3 roll, 4 anchor, 5 wall, 6 stop
	int used = 0, budget = 0, nextUsed = 0, loopBars = 0;
	bool halves = false;
	int geneSixteenths = 1;
	char preset[32] = "";
};

uint8_t code(const spb::Decision& d) {
	if (d.wall) return d.stop ? 6 : 5;
	if (d.anchored) return 4;
	if (!d.broken) return 0;
	if (d.roll) return 3;
	return d.turn ? 2 : 1;
}

enum Knob { K_CHAOS, K_VARI, K_MORPH, K_ORG, K_SLIDE, K_TURNS, K_ROLL, K_FREQ, K_PROTECT, K_MIX, K_OUT, K_COUNT };
const char* kKnobId[K_COUNT] = {spb::ids::chaos, spb::ids::vari, spb::ids::morph, spb::ids::organize, spb::ids::slide,
                                spb::ids::turns, spb::ids::roll, spb::ids::splithz, spb::ids::protect, spb::ids::mix, spb::ids::out};

} // namespace

struct Splicebus : Module {
	enum ParamId { CHAOS_PARAM, VARI_PARAM, MORPH_PARAM, ORG_PARAM, SLIDE_PARAM, TURNS_PARAM, ROLL_PARAM, FREQ_PARAM,
	               PROTECT_PARAM, MIX_PARAM, OUT_PARAM, SPLIT_PARAM, HOLD_PARAM, PARAMS_LEN };
	enum InputId { IN_L_INPUT, IN_R_INPUT, SC_INPUT, CLK_INPUT, RST_INPUT, BUS_INPUT, CHAOS_INPUT, INPUTS_LEN };
	enum OutputId { OUT_L_OUTPUT, OUT_R_OUTPUT, BRK_OUTPUT, TURN_OUTPUT, OUTPUTS_LEN };
	enum LightId { SPLIT_OFF_LIGHT, SPLIT_FIXED_LIGHT, SPLIT_TRACK_LIGHT, HOLD_LIGHT, LIGHTS_LEN };

	spb::Engine engine;
	std::vector<std::atomic<float>> vals;
	std::vector<bool> locked;
	int knobIdx[K_COUNT]{};
	std::atomic<int> reqPreset{-1};
	std::atomic<bool> reqMutate{false}, reqSeed{false}, pullKnobs{true};
	int presetIndex = 0;
	bool modified = false;
	std::mt19937 rng{(uint32_t) random::u32()};

	dsp::BooleanTrigger splitBtn, holdBtn;
	dsp::SchmittTrigger clkTrig, rstTrig;

	// transport
	int64_t step = -1;
	float sinceClk = 0.f, period = 0.f;
	bool clkSeen = false;
	bool busInit = false;
	float prevBusBars = 0.f, busWinTime = 0.f, busBpm = 0.f, busIdle = 1.f;
	int64_t busCycle = 0;
	double busWinPpq = 0.0;

	float inBuf[3][kBlock]{}, outBuf[2][kBlock]{};
	int pos = 0;
	float sampleRate = 44100.f;

	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;
	int64_t planLineIdx = INT64_MIN;
	spb::Params planParams{};
	spb::Engine::Line lineCur, lineNxt;

	Splicebus() : vals(spb::defs().size()), locked(spb::defs().size(), false) {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(CHAOS_PARAM, 0.f, 1.f, 0.2f, "Order <> chaos", " %", 0.f, 100.f);
		configParam(VARI_PARAM, 0.f, 1.f, 0.75f, "Varispeed (75% = as recorded, 50% stopped)", " %", 0.f, 100.f);
		configParam(MORPH_PARAM, 0.f, 1.f, 0.45f, "Morph (gaps / seamless / layers)", " %", 0.f, 100.f);
		configParam(ORG_PARAM, 0.f, 1.f, 0.f, "Organize (which splice)", " %", 0.f, 100.f);
		configParam(SLIDE_PARAM, 0.f, 1.f, 0.f, "Slide (where in the splice)", " %", 0.f, 100.f);
		configParam(TURNS_PARAM, 0.f, 1.f, 0.6f, "Turns (4/8/16/32)", " %", 0.f, 100.f);
		configParam(ROLL_PARAM, 0.f, 1.f, 0.5f, "Roll", " %", 0.f, 100.f);
		configParam(FREQ_PARAM, 30.f, 1000.f, 120.f, "Split frequency (FIXED)", " Hz");
		configParam(PROTECT_PARAM, 0.f, 1.f, 0.8f, "Protect", " %", 0.f, 100.f);
		configParam(MIX_PARAM, 0.f, 1.f, 1.f, "Mix", " %", 0.f, 100.f);
		configParam(OUT_PARAM, -24.f, 12.f, 0.f, "Output", " dB");
		configButton(SPLIT_PARAM, "Split (OFF / FIXED / TRACK)");
		configButton(HOLD_PARAM, "Hold (stop recording)");
		configInput(IN_L_INPUT, "Left");
		configInput(IN_R_INPUT, "Right (normalled to left)");
		configInput(SC_INPUT, "Sidechain (PROTECT BY SC HITS / SC LEVEL)");
		configInput(CLK_INPUT, "Clock (1/16 notes)");
		configInput(RST_INPUT, "Reset (next clock = bar 1)");
		configInput(BUS_INPUT, "FamilyBus (POS = the bar, ENERGY scales CHAOS)");
		configInput(CHAOS_INPUT, "Chaos CV (+10% per V)");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		configOutput(BRK_OUTPUT, "Break gate (a gene breaks)");
		configOutput(TURN_OUTPUT, "Turn gate (inside a turn window)");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
		for (int k = 0; k < K_COUNT; k++)
			knobIdx[k] = spb::indexOf(kKnobId[k]);
		setValues(spb::defaults());
		loadPreset(0);
		engine.prepare(44100.0, kBlock);
	}

	float get(const char* id) const { return vals[(size_t) spb::indexOf(id)].load(std::memory_order_relaxed); }
	void set(const char* id, float v) { setIdx(spb::indexOf(id), v); }
	void setIdx(int i, float v) {
		if (i < 0)
			return;
		const auto& d = spb::defs()[(size_t) i];
		v = clamp(v, d.min, d.max);
		if (d.kind != spb::PDef::real)
			v = std::round(v);
		vals[(size_t) i].store(v, std::memory_order_relaxed);
	}
	std::vector<float> values() const {
		std::vector<float> v(vals.size());
		for (size_t i = 0; i < v.size(); i++)
			v[i] = vals[i].load(std::memory_order_relaxed);
		return v;
	}
	void setValues(const std::vector<float>& v) {
		for (size_t i = 0; i < v.size() && i < vals.size(); i++)
			setIdx((int) i, v[i]);
		pullKnobs = true;
	}
	void loadPreset(int i) {
		const auto& f = spb::factoryPresets();
		i = clamp(i, 0, (int) f.size() - 1);
		std::vector<float> v;
		spb::apply(f[(size_t) i], v);
		// the clock settings are the patch's, not the preset's
		v[(size_t) spb::indexOf(spb::ids::run)] = get(spb::ids::run);
		v[(size_t) spb::indexOf(spb::ids::bpm)] = get(spb::ids::bpm);
		setValues(v);
		presetIndex = i;
		modified = false;
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		sampleRate = e.sampleRate;
		engine.prepare(e.sampleRate, kBlock);
		pos = 0;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		std::fill(locked.begin(), locked.end(), false);
		reqPreset = 0;
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_t* v = json_object();
		json_t* lk = json_array();
		const auto& d = spb::defs();
		for (size_t i = 0; i < d.size(); i++) {
			json_object_set_new(v, d[i].id, json_real(vals[i].load()));
			if (locked[i])
				json_array_append_new(lk, json_string(d[i].id));
		}
		json_object_set_new(root, "values", v);
		json_object_set_new(root, "locks", lk);
		json_object_set_new(root, "preset", json_integer(presetIndex));
		json_object_set_new(root, "modified", json_boolean(modified));
		return root;
	}

	void dataFromJson(json_t* root) override {
		if (json_t* v = json_object_get(root, "values")) {
			const auto& d = spb::defs();
			for (size_t i = 0; i < d.size(); i++)
				if (json_t* x = json_object_get(v, d[i].id))
					setIdx((int) i, (float) json_number_value(x));
		}
		std::fill(locked.begin(), locked.end(), false);
		if (json_t* lk = json_object_get(root, "locks")) {
			size_t i;
			json_t* x;
			json_array_foreach(lk, i, x) {
				const int k = spb::indexOf(json_string_value(x));
				if (k >= 0)
					locked[(size_t) k] = true;
			}
		}
		if (json_t* p = json_object_get(root, "preset"))
			presetIndex = (int) json_integer_value(p);
		if (json_t* p = json_object_get(root, "modified"))
			modified = json_boolean_value(p);
		reqPreset = -1;
		for (int k = 0; k < K_COUNT; k++)
			params[CHAOS_PARAM + k].setValue(vals[(size_t) knobIdx[k]].load());
		pullKnobs = false;
	}

	// ---- position (as TURNAROUND) -----------------------------------------------------------

	bool clockPosition(float dt, double& ppq, double& bpm) {
		sinceClk += dt;
		if (rstTrig.process(inputs[RST_INPUT].getVoltage(), 0.1f, 1.f))
			step = -1;
		if (clkTrig.process(inputs[CLK_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (clkSeen)
				period = sinceClk;
			clkSeen = true;
			step++;
			sinceClk = 0.f;
		}
		if (!inputs[CLK_INPUT].isConnected()) {
			clkSeen = false;
			period = 0.f;
			return false;
		}
		if (period > 0.f && sinceClk > 2.5f * period) {
			clkSeen = false;
			period = 0.f;
		}
		if (step < 0 || period <= 0.f)
			return false;
		bpm = 60.0 / (4.0 * period);
		ppq = (step + std::min(sinceClk / period, 0.999f)) * 0.25;
		return true;
	}

	bool busPosition(float dt, double& ppq, double& bpm) {
		const float bars = bus::voltsToBars(clamp(inputs[BUS_INPUT].getVoltage(bus::kPos), 0.f, 10.f));
		if (!busInit) {
			busInit = true;
			prevBusBars = bars;
			busWinPpq = bars * 4.0;
		}
		if (bars < prevBusBars - 16.f)
			busCycle++;
		else if (bars > prevBusBars + 16.f)
			busCycle--;
		prevBusBars = bars;
		ppq = ((double) busCycle * bus::kBarsPerCycle + bars) * 4.0;
		busWinTime += dt;
		if (busWinTime >= 0.1f) {
			const double d = ppq - busWinPpq;
			if (d > 1e-6 && d < 8.0) {
				const float inst = (float) (d / busWinTime * 60.0);
				busBpm = busBpm <= 0.f ? inst : busBpm + 0.3f * (inst - busBpm);
				busIdle = 0.f;
			}
			else
				busIdle += busWinTime;
			busWinPpq = ppq;
			busWinTime = 0.f;
		}
		bpm = busBpm;
		return busIdle < 0.5f && bpm > 1.0;
	}

	// ---- the block -------------------------------------------------------------------------

	void runBlock(const ProcessArgs& args) {
		const int pr = reqPreset.exchange(-1);
		if (pr >= 0)
			loadPreset(pr);
		if (reqMutate.exchange(false)) {
			auto v = values();
			spb::mutate(v, 0.25f, locked, [](void* c) { return std::uniform_real_distribution<float>(0.f, 1.f)(*(std::mt19937*) c); }, &rng);
			setValues(v);
			modified = true;
		}
		if (reqSeed.exchange(false) && !locked[(size_t) spb::indexOf(spb::ids::seed)])
			set(spb::ids::seed, (float) (rng() % 10000u));
		if (pullKnobs.exchange(false)) {
			for (int k = 0; k < K_COUNT; k++)
				params[CHAOS_PARAM + k].setValue(vals[(size_t) knobIdx[k]].load());
		}
		for (int k = 0; k < K_COUNT; k++)
			setIdx(knobIdx[k], params[CHAOS_PARAM + k].getValue());

		spb::Params p = spb::toParams(values());
		p.chaos = clamp(p.chaos + inputs[CHAOS_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		const float dt = args.sampleTime * kBlock;
		double ppq = 0.0, bpm = 120.0;
		spb::Transport t;
		bool ok = clockPosition(dt, ppq, bpm);
		if (inputs[BUS_INPUT].isConnected()) {
			double bppq, bbpm;
			const double clkBpm = bpm;
			if (busPosition(dt, bppq, bbpm)) {
				ppq = bppq;
				bpm = ok ? clkBpm : bbpm;
				ok = true;
			}
			if (inputs[BUS_INPUT].getChannels() > bus::kEnergy)
				p.chaos *= clamp(inputs[BUS_INPUT].getVoltage(bus::kEnergy) / 10.f, 0.f, 1.f);
		}
		else
			busInit = false;
		if (ok) {
			t.playing = true;
			t.hostHasPosition = true;
			t.bpm = std::min(400.0, std::max(20.0, bpm));
			t.ppq = ppq;
		}
		engine.setParams(p);
		const bool sc = inputs[SC_INPUT].isConnected();
		engine.process(inBuf[0], inBuf[1], sc ? inBuf[2] : nullptr, sc ? inBuf[2] : nullptr, outBuf[0], outBuf[1], kBlock, t);
	}

	void process(const ProcessArgs& args) override {
		if (splitBtn.process(params[SPLIT_PARAM].getValue() > 0.f))
			set(spb::ids::split, (float) (((int) get(spb::ids::split) + 1) % 3));
		if (holdBtn.process(params[HOLD_PARAM].getValue() > 0.f))
			set(spb::ids::hold, get(spb::ids::hold) > 0.5f ? 0.f : 1.f);

		const float l = inputs[IN_L_INPUT].getVoltage();
		const float r = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() : l;
		inBuf[0][pos] = l / kVolts;
		inBuf[1][pos] = r / kVolts;
		inBuf[2][pos] = inputs[SC_INPUT].getVoltage() / kVolts;
		outputs[OUT_L_OUTPUT].setVoltage(outBuf[0][pos] * kVolts);
		outputs[OUT_R_OUTPUT].setVoltage(outBuf[1][pos] * kVolts);
		if (++pos >= kBlock) {
			pos = 0;
			runBlock(args);
		}
		outputs[BRK_OUTPUT].setVoltage(engine.reel.display.breaking.load(std::memory_order_relaxed) ? 10.f : 0.f);
		const bool running = engine.display.running.load(std::memory_order_relaxed);
		outputs[TURN_OUTPUT].setVoltage(running && spb::Engine::turnIntensity(engine.getParams(), engine.display.sixteenth.load(std::memory_order_relaxed)) > 0.f ? 10.f : 0.f);

		if (ui.tick(args.sampleTime))
			publish();
	}

	void publish() {
		const auto& D = engine.display;
		const auto& R = engine.reel.display;
		const spb::Params& P = engine.getParams();
		Snapshot& s = snap.writeSlot();
		const int split = P.split;
		for (int i = 0; i < 3; i++)
			lights[SPLIT_OFF_LIGHT + i].setBrightness(split == i ? 1.f : 0.f);
		lights[HOLD_LIGHT].setBrightness(P.hold ? 1.f : 0.f);
		s.split = split;
		s.running = D.running.load(std::memory_order_relaxed);
		s.bpm = D.bpm.load(std::memory_order_relaxed);
		s.splitHz = D.splitHz.load(std::memory_order_relaxed);
		s.f0 = D.f0.load(std::memory_order_relaxed);
		s.guard = D.guard.load(std::memory_order_relaxed);
		s.sixteenth = D.sixteenth.load(std::memory_order_relaxed);
		s.valid = R.valid.load(std::memory_order_relaxed);
		s.held = R.held.load(std::memory_order_relaxed);
		s.breaking = R.breaking.load(std::memory_order_relaxed);
		s.walled = R.walled.load(std::memory_order_relaxed);
		s.splices = R.splices.load(std::memory_order_relaxed);
		s.genes = R.genes.load(std::memory_order_relaxed);
		s.broken = R.broken.load(std::memory_order_relaxed);
		const int cap = std::max(1, R.peakCap.load(std::memory_order_relaxed));
		int64_t c0, c1;
		if (s.valid) {
			c0 = R.winStart.load(std::memory_order_relaxed) / spb::Reel::kPeak;
			c1 = c0 + (int64_t) s.splices * R.spliceLen.load(std::memory_order_relaxed) / spb::Reel::kPeak;
		}
		else {
			c1 = R.rec.load(std::memory_order_relaxed) / spb::Reel::kPeak;
			c0 = c1 - std::min<int64_t>(cap - 1, (int64_t) (4.0 * sampleRate) / spb::Reel::kPeak);
		}
		const int64_t span = std::max<int64_t>(1, c1 - c0);
		for (int k = 0; k < kCols; k++) {
			float pk = 0.f;
			const int64_t a = c0 + span * k / kCols, b = std::max(a + 1, c0 + span * (k + 1) / kCols);
			for (int64_t c = a; c < b; c++)
				if (c >= 0)
					pk = std::max(pk, R.peaks[(size_t) (c % cap)]);
			s.peaks[k] = pk;
		}
		for (int g = 0; g < spb::Reel::kGenes; g++)
			s.genePos[g] = R.voicePos[(size_t) g].load(std::memory_order_relaxed);

		// FORM: replanned when the line or the parameters change (planLine is a pure function)
		const int64_t k = s.running ? std::max<int64_t>(0, s.sixteenth) : 0;
		const int64_t line = k / 64;
		if (line != planLineIdx || std::memcmp(&planParams, &P, sizeof P) != 0) {
			planLineIdx = line;
			std::memcpy(&planParams, &P, sizeof P);
			spb::Engine::planLine(P, line, lineCur);
			spb::Engine::planLine(P, line + 1, lineNxt);
		}
		for (int i = 0; i < 128; i++) {
			s.cur[i] = code(lineCur.slot[(size_t) i]);
			s.nxt[i] = code(lineNxt.slot[(size_t) i]);
		}
		s.used = lineCur.used;
		s.nextUsed = lineNxt.used;
		s.budget = spb::budgetCount(P.budget);
		s.halves = spb::geneSixteenths(P.gene) < 1.0;
		s.geneSixteenths = std::max(1, (int) spb::geneSixteenths(P.gene));
		s.loopBars = spb::loopBars(P.loop);
		const int64_t base32 = (k / 16 / 32) * 32;
		const int ab = spb::anchorBars(P.anchor);
		for (int b = 0; b < 32; b++) {
			const int64_t absBar = base32 + b;
			const int64_t next = (absBar / 4) * 4 + 4;
			const int tier = next % 32 == 0 ? 3 : (next % 16 == 0 ? 2 : (next % 8 == 0 ? 1 : 0));
			uint8_t f = 0;
			if (absBar % 4 == 3 && P.turns > 0.f)
				f |= 1 | (uint8_t) (tier << 1);
			if (P.wall > 0 && absBar % 4 == 3 && tier >= 2)
				f |= 8;
			if (ab > 0 && absBar % ab == 0)
				f |= 16;
			s.bars[b] = f;
		}
		std::snprintf(s.preset, sizeof s.preset, "%s%s", presetIndex >= 0 ? spb::factoryPresets()[(size_t) presetIndex].name : "", modified ? "*" : "");
		snap.publish();
	}
};

// ---------------------------------------------------------------------------- screens

struct SplicebusTapeScreen : Screen {
	Splicebus* module = nullptr;
	Snapshot demo;
	SplicebusTapeScreen() {
		for (int k = 0; k < kCols; k++)
			demo.peaks[k] = 0.2f + 0.6f * std::fabs(std::sin(k * 0.41f)) * ((k % 12) < 3 ? 1.f : 0.5f);
		demo.valid = true;
		demo.splices = 2;
		for (int g = 0; g < spb::Reel::kGenes; g++)
			demo.genePos[g] = g == 0 ? 0.62f : -1.f;
		demo.running = true;
		demo.splitHz = 120.f;
		std::snprintf(demo.preset, sizeof demo.preset, "Mix Bus Order");
	}
	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		char buf[64];
		static const char* sn[] = {"OFF", "FIXED", "TRACK"};
		if (s.split == 2 && s.f0 > 0.f)
			std::snprintf(buf, sizeof buf, "%s %d HZ  F0 %d", sn[clamp(s.split, 0, 2)], (int) s.splitHz, (int) s.f0);
		else
			std::snprintf(buf, sizeof buf, "%s %d HZ", sn[clamp(s.split, 0, 2)], s.split ? (int) s.splitHz : 0);
		text(vg, 3.f, 2.f, buf, 9.f, semi::theme::text());
		text(vg, w - 3.f, 2.f, s.preset, 9.f, semi::theme::cyan(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		const float y0 = 13.f, y1 = h - 11.f, mid = 0.5f * (y0 + y1), half = 0.5f * (y1 - y0);
		const float cw = (w - 6.f) / kCols;
		for (int k = 0; k < kCols; k++) {
			const float a = std::min(1.f, s.peaks[k]) * half;
			if (a > 0.3f)
				fillRect(vg, 3.f + cw * k, mid - a, std::max(1.f, cw - 0.4f), 2.f * a, s.breaking ? semi::theme::magenta() : semi::theme::cyanDim());
		}
		if (s.valid && s.splices > 0) {
			for (int k = 1; k < s.splices; k++) {
				const float x = 3.f + (w - 6.f) * k / s.splices;
				line(vg, x, y0, x, y1, semi::theme::text());
			}
			for (int g = 0; g < spb::Reel::kGenes; g++)
				if (s.genePos[g] >= 0.f) {
					const float x = 3.f + (w - 6.f) * clamp(s.genePos[g], 0.f, 1.f);
					line(vg, x, y0, x, y1, semi::theme::amber(), semi::platform::kStroke);
				}
		}
		std::snprintf(buf, sizeof buf, "%s%s%d SPL  %d/%d BROKE", s.held ? "HELD " : "", s.walled ? "WALL " : "", s.valid ? s.splices : 0, s.broken, s.genes);
		text(vg, 3.f, h - 2.f, buf, 8.f, s.breaking ? semi::theme::magenta() : semi::theme::dim(), NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
		std::snprintf(buf, sizeof buf, "%s %.0f", s.running ? "RUN" : "WIRE", s.bpm);
		text(vg, w - 3.f, h - 2.f, buf, 8.f, s.running ? semi::theme::text() : semi::theme::dim(), NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
	}
};

// The form: 32 bars (turn windows brighter before bigger lines, walls, anchors, the bar playing), this
// 4-bar line slot by slot with the playhead, and the next one; the budget.
struct FormScreen : Screen {
	Splicebus* module = nullptr;
	Snapshot demo;
	FormScreen() {
		demo.running = true;
		demo.sixteenth = 37;
		for (int b = 0; b < 32; b++)
			demo.bars[b] = (b % 4 == 3 ? 1 | (((b + 1) % 32 == 0 ? 3 : ((b + 1) % 16 == 0 ? 2 : ((b + 1) % 8 == 0 ? 1 : 0))) << 1) : 0) | (b % 4 == 0 ? 16 : 0);
		for (int i = 0; i < 128; i += 2)
			demo.cur[i] = demo.nxt[i] = (i % 22 == 6) ? 1 : ((i > 110) ? 2 : 0);
		demo.cur[0] = demo.nxt[0] = 4;
		demo.used = 4;
		demo.budget = 4;
		demo.geneSixteenths = 1;
	}
	void drawLine(NVGcontext* vg, const uint8_t* slots, const Snapshot& s, float x0, float y, float w, float h, int at, bool big) {
		const float sw = w / 64.f;
		for (int st = 0; st < 64; st++) {
			const float x = x0 + sw * st;
			if (st % 16 == 0)
				fillRect(vg, x, y, 1.f, h, semi::theme::grid());
			for (int half = 0; half < (s.halves ? 2 : 1); half++) {
				if (!s.halves && st % s.geneSixteenths != 0)
					continue;
				const uint8_t c = slots[st * 2 + half];
				const float cw = s.halves ? sw * 0.5f : sw * s.geneSixteenths;
				const float cx = x + (s.halves ? sw * 0.5f * half : 0.f) + 0.5f;
				const float ww = std::max(1.f, cw - 1.f);
				switch (c) {
					case 0: fillRect(vg, cx + ww * 0.5f - 1.f, y + h * 0.5f - 1.f, 2.f, 2.f, semi::theme::dim()); break;
					case 1: fillRect(vg, cx, y + 1.f, ww, h - 2.f, big ? semi::theme::text() : semi::theme::dim()); break;
					case 2: fillRect(vg, cx, y + 1.f, ww, h - 2.f, big ? semi::theme::cyan() : semi::theme::cyanDim()); break;
					case 3:
						fillRect(vg, cx, y + 1.f, ww, h - 2.f, big ? semi::theme::cyan() : semi::theme::cyanDim());
						fillRect(vg, cx, y + h * 0.5f - 0.5f, ww, 1.5f, semi::theme::screen());
						break;
					case 4:
						fillRect(vg, cx, y + 1.f, 1.5f, h - 2.f, semi::theme::text());
						break;
					case 5: fillRect(vg, cx, y + 1.f, ww, h - 2.f, semi::theme::magentaDeep()); break;
					case 6: fillRect(vg, cx, y + 1.f, ww, h - 2.f, semi::theme::magenta()); break;
				}
			}
		}
		if (at >= 0)
			fillRect(vg, x0 + sw * (at + 0.5f) - 1.f, y - 1.f, 2.f, h + 2.f, semi::theme::amber());
	}
	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		const float x0 = 3.f, gw = w - 6.f;
		const int64_t k = s.running ? std::max<int64_t>(0, s.sixteenth) : 0;
		const int64_t bar = k / 16;
		// the 32-bar map
		const float bw = gw / 32.f, my = 2.f, mh = 9.f;
		for (int b = 0; b < 32; b++) {
			const float x = x0 + bw * b;
			fillRect(vg, x + 0.5f, my, bw - 1.f, mh, semi::theme::cell());
			const uint8_t f = s.bars[b];
			if (f & 1) {
				static const float frac[4] = {0.25f, 0.5f, 1.f, 1.f};
				const int tier = (f >> 1) & 3;
				const float ww = (bw - 1.f) * frac[tier];
				fillRect(vg, x + bw - 0.5f - ww, my, ww, mh, tier >= 2 ? semi::theme::cyan() : semi::theme::cyanDim());
			}
			if (f & 8)
				fillRect(vg, x + bw * 0.75f, my + 2.f, bw * 0.25f - 0.5f, mh - 4.f, semi::theme::magenta());
			if (f & 16)
				fillRect(vg, x + 0.5f, my, 1.5f, mh, semi::theme::text());
			if (s.running && (bar % 32) == b) {
				fillRect(vg, x + 0.5f, my + mh + 0.5f, bw - 1.f, 1.5f, semi::theme::amber());
			}
		}
		if (s.loopBars > 0)
			line(vg, x0, my + mh + 2.5f, x0 + bw * s.loopBars, my + mh + 2.5f, semi::theme::text());
		// this line and the next
		const float ly = my + mh + 5.f, lh = (h - ly - 13.f) * 0.62f;
		drawLine(vg, s.cur, s, x0, ly, gw, lh, s.running ? (int) (k % 64) : -1, true);
		const float ny = ly + lh + 2.f, nh = (h - ly - 13.f) - lh - 2.f;
		drawLine(vg, s.nxt, s, x0, ny, gw, nh, -1, false);
		char buf[64];
		const int64_t line4 = k / 64;
		if (s.budget > 0)
			std::snprintf(buf, sizeof buf, "BARS %d-%d  BREAKS %d/%d", (int) (line4 * 4 + 1), (int) (line4 * 4 + 4), s.used, s.budget);
		else
			std::snprintf(buf, sizeof buf, "BARS %d-%d  BREAKS %d", (int) (line4 * 4 + 1), (int) (line4 * 4 + 4), s.used);
		text(vg, 3.f, h - 2.f, buf, 8.f, semi::theme::text(), NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
		std::snprintf(buf, sizeof buf, "NEXT %d", s.nextUsed);
		text(vg, w - 3.f, h - 2.f, buf, 8.f, semi::theme::dim(), NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
	}
};

// ---------------------------------------------------------------------------- menu

namespace {
void valueItem(Menu* menu, Splicebus* m, const char* id, const char* label) {
	const int i = spb::indexOf(id);
	const auto& d = spb::defs()[(size_t) i];
	std::vector<std::string> labels;
	std::vector<float> steps;
	if (d.kind == spb::PDef::choice) {
		labels = d.choices;
		for (size_t k = 0; k < labels.size(); k++)
			steps.push_back((float) k);
	}
	else if (d.kind == spb::PDef::boolean) {
		menu->addChild(createBoolMenuItem(label, "", [=]() { return m->vals[(size_t) i].load() > 0.5f; },
			[=](bool b) { m->setIdx(i, b ? 1.f : 0.f); }));
		return;
	}
	else {
		// fixed steps that work on both platforms
		const int n = 11;
		for (int k = 0; k < n; k++) {
			float v = d.min + (d.max - d.min) * k / (n - 1);
			if (d.skewCentre > 0.f) {
				const float skew = std::log(0.5f) / std::log((d.skewCentre - d.min) / (d.max - d.min));
				v = d.min + (d.max - d.min) * std::pow((float) k / (n - 1), 1.f / skew);
			}
			steps.push_back(v);
			labels.push_back(string::f("%.3g", v));
		}
	}
	menu->addChild(createIndexSubmenuItem(label, labels,
		[=]() {
			const float v = m->vals[(size_t) i].load();
			size_t best = 0;
			for (size_t k = 1; k < steps.size(); k++)
				if (std::fabs(steps[k] - v) < std::fabs(steps[best] - v))
					best = k;
			return best;
		},
		[=](size_t k) { m->setIdx(i, steps[k]); }));
}
} // namespace

struct SplicebusWidget : ModuleWidget {
	SplicebusWidget(Splicebus* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Splicebus.svg")));
		auto* tape = semi::createScreenMm<SplicebusTapeScreen>(Vec(L::TAPE.x, L::TAPE.y), Vec(L::TAPE.w, L::TAPE.h));
		tape->module = module;
		addChild(tape);
		auto* form = semi::createScreenMm<FormScreen>(Vec(L::FORM.x, L::FORM.y), Vec(L::FORM.w, L::FORM.h));
		form->module = module;
		addChild(form);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::SPLIT_OFF), module, Splicebus::SPLIT_OFF_LIGHT));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::SPLIT_FIXED), module, Splicebus::SPLIT_FIXED_LIGHT));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::SPLIT_TRACK), module, Splicebus::SPLIT_TRACK_LIGHT));
		addParam(createParamCentered<semi::Button>(at(L::SPLIT), module, Splicebus::SPLIT_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::HOLD), module, Splicebus::HOLD_PARAM));
		addChild(createLightCentered<semi::AmberLedSmall>(at(L::HOLD_LED), module, Splicebus::HOLD_LIGHT));

		addParam(createParamCentered<semi::KnobLHot>(at(L::CHAOS), module, Splicebus::CHAOS_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::VARI), module, Splicebus::VARI_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::MORPH), module, Splicebus::MORPH_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::ORGANIZE), module, Splicebus::ORG_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::SLIDE), module, Splicebus::SLIDE_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::TURNS), module, Splicebus::TURNS_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::ROLL), module, Splicebus::ROLL_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::FREQ), module, Splicebus::FREQ_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::PROTECT), module, Splicebus::PROTECT_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::MIX), module, Splicebus::MIX_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::OUT), module, Splicebus::OUT_PARAM));

		addInput(createInputCentered<semi::Jack>(at(L::IN_L), module, Splicebus::IN_L_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::IN_R), module, Splicebus::IN_R_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::SC), module, Splicebus::SC_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::CLK), module, Splicebus::CLK_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RST), module, Splicebus::RST_INPUT));
		addInput(createInputCentered<semi::BusJack>(at(L::BUS), module, Splicebus::BUS_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::CHAOS_CV), module, Splicebus::CHAOS_INPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_L), module, Splicebus::OUT_L_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_R), module, Splicebus::OUT_R_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::BRK), module, Splicebus::BRK_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::TURN), module, Splicebus::TURN_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Splicebus*>(module);
		if (!m)
			return;
		using namespace spb;
		menu->addChild(new MenuSeparator);
		menu->addChild(createSubmenuItem("Factory presets", "", [=](Menu* sub) {
			const auto& f = factoryPresets();
			for (int i = 0; i < (int) f.size(); i++)
				sub->addChild(createMenuItem(std::string(f[(size_t) i].category) + " · " + f[(size_t) i].name, CHECKMARK(m->presetIndex == i),
					[=]() { m->reqPreset = i; }));
		}));
		menu->addChild(createMenuItem("MUTATE", "", [=]() { m->reqMutate = true; }));
		menu->addChild(createMenuItem(string::f("NEW SEED (now %d)", (int) m->get(ids::seed)), "", [=]() { m->reqSeed = true; }));
		menu->addChild(createSubmenuItem("Reel", "", [=](Menu* sub) {
			valueItem(sub, m, ids::length, "Length");
			valueItem(sub, m, ids::gene, "Gene");
		}));
		menu->addChild(createSubmenuItem("Form", "", [=](Menu* sub) {
			valueItem(sub, m, ids::budget, "Budget (breaks per 4 bars)");
			valueItem(sub, m, ids::wall, "Wall");
			valueItem(sub, m, ids::anchor, "Anchor");
			valueItem(sub, m, ids::loop, "Loop");
		}));
		menu->addChild(createSubmenuItem("Split", "", [=](Menu* sub) {
			valueItem(sub, m, ids::split, "Mode");
			valueItem(sub, m, ids::ratio, "Ratio (TRACK, x F0)");
			valueItem(sub, m, ids::f0max, "F0 max (Hz)");
		}));
		menu->addChild(createSubmenuItem("Protect", "", [=](Menu* sub) {
			valueItem(sub, m, ids::protby, "By");
			valueItem(sub, m, ids::prel, "Release (ms)");
			valueItem(sub, m, ids::sens, "Sensitivity");
		}));
		menu->addChild(createSubmenuItem("Clock", "", [=](Menu* sub) {
			valueItem(sub, m, ids::run, "Free run without CLK / BUS");
			valueItem(sub, m, ids::bpm, "Free-run BPM");
		}));
		menu->addChild(createSubmenuItem("Lock from MUTATE", "", [=](Menu* sub) {
			const auto& d = defs();
			for (size_t i = 0; i < d.size(); i++)
				sub->addChild(createBoolMenuItem(d[i].name, "", [=]() { return (bool) m->locked[i]; }, [=](bool b) { m->locked[i] = b; }));
		}));
	}
};

Model* modelSplicebus = createModel<Splicebus, SplicebusWidget>("Splicebus");
