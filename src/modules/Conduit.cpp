// CONDUIT — Rack / MetaModule port of CONDUIT, the family's patch bay (engine: engines/conduit, the
// pin matrix vendored from semierendonmez-commits/conduit Source/engine; see sources.mk for changes).
//
// In the DAW CONDUIT reads the shared-memory FamilyBus and writes into the siblings' bus inputs. Here the
// family talks over cables, so the rows are what arrives on cables and the columns are output jacks:
//
//   rows (15)    CLK, BAR (POS crosses a bar), TURN (any EVENT pulse), TURN+ (a long line), SCENE (the
//                SCENE channel changed), ENERGY, PHASE (bar phase), TRG 1-4 (events), CV 1-4 (values)
//   pins         AMOUNT, CURVE, DELAY, PROB, LENGTH: CONDUIT's own router, unchanged
//   columns (8)  OUT 1-8 = MOD (CV), TRIG or GATE, each with a DEPTH attenuverter; optionally merged
//                onto BUS OUT (ENERGY += value, EVENT <- triggers). BUS OUT = BUS IN passed through.
//
// The router runs in blocks of kBlock samples (one block of latency); MOD outputs ramp across the block.
// The pin under the ROW / COL cursor is edited with the five pin knobs (the knobs jump to the pin when the
// cursor moves); clicking the matrix in Rack moves the cursor, double-click patches / pulls.

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/FamilyBusCV.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/ConduitLayout.hpp"
#include "conduit/Router.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace L = semi::layout::conduit;
using semi::Screen;

namespace {

constexpr int kRows = conduit::kRows;   // 15
constexpr int kCols = conduit::kCols;   // 8
constexpr int kBlock = 32;
constexpr int kMaxBlockEvents = 64;

enum RowId {
	R_CLK, R_BAR, R_TURN, R_TURNX, R_SCENE, R_ENERGY, R_PHASE,
	R_TRG1, R_TRG2, R_TRG3, R_TRG4,
	R_CV1, R_CV2, R_CV3, R_CV4,
};
const char* const kRowNames[kRows] = {"CLK", "BAR", "TURN", "TURN+", "SCENE", "ENERGY", "PHASE",
                                      "TRG1", "TRG2", "TRG3", "TRG4", "CV1", "CV2", "CV3", "CV4"};
bool rowIsEvent(int r) { return r <= R_SCENE || (r >= R_TRG1 && r <= R_TRG4); }

enum ColType { COL_MOD, COL_TRIG, COL_GATE };
const char* const kColTypeShort[3] = {"M", "T", "G"};
enum Merge { MERGE_NONE, MERGE_ENERGY, MERGE_EVENT };

const float kRests[] = {0.f, 0.25f, 0.5f, 0.75f, 1.f};
const int kTurnLines[] = {4, 8, 16, 32};
const float kClockSteps[] = {1.f, 2.f, 4.f, 1.f / 6.f};   // 16ths per CLK pulse
const float kTempos[] = {90.f, 120.f, 140.f, 160.f, 172.f, 174.f};
constexpr int kNumTempos = 6;

// LENGTH knob 0..1 -> 1/4 .. 64 sixteenths (exponential)
float lenFromKnob(float k) { return 0.25f * std::pow(2.f, 8.f * clamp(k, 0.f, 1.f)); }
float knobFromLen(float l) { return clamp(std::log2(std::max(l, 0.25f) / 0.25f) / 8.f, 0.f, 1.f); }

// Column settings kept beside the Patch (the router only knows the endpoint type).
struct ColCfg {
	int type = COL_MOD;
	int bipolar = 0;
	int rest = 0;      // index into kRests
	int merge = MERGE_NONE;
};

uint8_t dstType(int t) { return t == COL_TRIG ? conduit::DstTrigger : t == COL_GATE ? conduit::DstGate : conduit::DstValue; }

void initEndpoints(conduit::Patch& p, const ColCfg* cfg) {
	for (int r = 0; r < kRows; r++)
		p.row[r] = conduit::Endpoint::make(r <= R_PHASE ? "BUS" : "IN", kRowNames[r], rowIsEvent(r) ? conduit::SrcEvent : conduit::SrcValue);
	for (int c = 0; c < kCols; c++) {
		char nm[8];
		std::snprintf(nm, sizeof nm, "OUT%d", c + 1);
		p.col[c] = conduit::Endpoint::make("OUT", nm, dstType(cfg[c].type));
		p.base[c] = kRests[clamp(cfg[c].rest, 0, 4)];
	}
}

// pin defaults when it is patched (the original: MOD gets EXP + 2/16, GATE gets GATE)
void defaultPin(conduit::Pin& p, int colType, float amount) {
	p.on = 1;
	p.amount = std::fabs(amount) > 0.01f ? amount : 1.f;
	p.curve = colType == COL_GATE ? conduit::CurveGate : colType == COL_MOD ? conduit::CurveExp : conduit::CurveLin;
	p.delay = 0.f;
	p.prob = 1.f;
	p.len = colType == COL_GATE ? 4.f : 2.f;
}

// ---- factory patches (cable versions of CONDUIT's FAMILY / TURNS EVERYWHERE / HOST GRID)
const char* const kFactory[] = {"FAMILY", "TURNS EVERYWHERE", "GRID", "EMPTY"};
constexpr int kNumFactory = 4;

void makeFactory(int i, conduit::Patch& p, ColCfg* cfg) {
	p = conduit::Patch{};
	p.seed = 1;
	const int types[kCols] = {COL_MOD, COL_MOD, COL_MOD, COL_MOD, COL_TRIG, COL_TRIG, COL_GATE, COL_GATE};
	for (int c = 0; c < kCols; c++) {
		cfg[c] = ColCfg{};
		cfg[c].type = types[c];
	}
	auto pin = [&](int r, int c, float amt, int curve, float delay, float prob, float len) {
		conduit::Pin& q = p.pin[r][c];
		q.on = 1;
		q.amount = amt;
		q.curve = (uint8_t) curve;
		q.delay = delay;
		q.prob = prob;
		q.len = len;
	};
	switch (i) {
		case 0:   // FAMILY: the bar pumps OUT1 (rests at 100 %), ENERGY drives OUT2, triggers make envelopes
			cfg[0].rest = 4;
			pin(R_BAR, 0, -0.6f, conduit::CurveExp, 0.f, 1.f, 2.f);
			pin(R_ENERGY, 1, 1.f, conduit::CurveLin, 0.f, 1.f, 1.f);
			pin(R_TRG1, 2, 1.f, conduit::CurveExp, 0.f, 1.f, 4.f);
			pin(R_CV1, 3, 1.f, conduit::CurveS, 0.f, 1.f, 1.f);
			pin(R_TURN, 4, 1.f, conduit::CurveLin, 0.f, 1.f, 1.f);
			pin(R_TURNX, 5, 1.f, conduit::CurveLin, 0.f, 1.f, 1.f);
			pin(R_SCENE, 6, 1.f, conduit::CurveGate, 0.f, 1.f, 16.f);
			pin(R_ENERGY, 7, 1.f, conduit::CurveGate, 0.f, 1.f, 1.f);
			break;
		case 1:   // TURNS EVERYWHERE: turns relaunch, cut and mutate, some of the time, a 16th late
			pin(R_TURN, 4, 1.f, conduit::CurveLin, 0.f, 0.75f, 1.f);
			pin(R_TURN, 5, 1.f, conduit::CurveLin, 1.f, 0.5f, 1.f);
			pin(R_TURNX, 0, 1.f, conduit::CurveExp, 0.f, 1.f, 16.f);
			pin(R_SCENE, 6, 1.f, conduit::CurveGate, 0.f, 1.f, 8.f);
			pin(R_TURN, 1, 0.8f, conduit::CurveLog, 0.f, 1.f, 8.f);
			pin(R_ENERGY, 2, 1.f, conduit::CurveExp, 0.f, 1.f, 1.f);
			break;
		case 2:   // GRID: the clock and the bar line as triggers and a beat pulse
			pin(R_CLK, 4, 1.f, conduit::CurveLin, 0.f, 1.f, 1.f);
			pin(R_BAR, 5, 1.f, conduit::CurveLin, 0.f, 1.f, 1.f);
			pin(R_CLK, 0, 1.f, conduit::CurveExp, 0.f, 1.f, 1.f);
			pin(R_PHASE, 1, 1.f, conduit::CurveLin, 0.f, 1.f, 1.f);
			pin(R_CLK, 6, 1.f, conduit::CurveGate, 0.f, 0.5f, 0.5f);
			break;
		default:
			break;
	}
	p.setName(kFactory[clamp(i, 0, kNumFactory - 1)]);
	initEndpoints(p, cfg);
}

struct Snapshot {
	conduit::Pin pin[kRows][kCols];
	float level[kRows][kCols]{};   // |pin output| peak over the UI period
	float rowLevel[kRows]{};       // events: 1 on a hit (held for the period); values: the value
	bool rowLive[kRows]{};         // its cable is patched
	float colOut[kCols]{};         // -1..1 after DEPTH
	float colVolts[kCols]{};
	bool colFire[kCols]{};
	uint8_t colType[kCols]{};
	uint8_t colMerge[kCols]{};
	int selR = 0, selC = 0;
	float bpm = 120.f;
	int tempoSrc = 0;               // 0 fallback, 1 CLK, 2 BUS
	int pins = 0;
	char name[32]{};
};

} // namespace

struct Conduit : Module {
	enum ParamId {
		ROW_PARAM, COL_PARAM, AMOUNT_PARAM, CURVE_PARAM, DELAY_PARAM, PROB_PARAM, LEN_PARAM, PIN_PARAM, MASTER_PARAM,
		ENUMS(DEPTH_PARAM, kCols),
		PARAMS_LEN
	};
	enum InputId { BUS_INPUT, CLK_INPUT, ENUMS(TRG_INPUT, 4), ENUMS(CV_INPUT, 4), INPUTS_LEN };
	enum OutputId { ENUMS(OUT_OUTPUT, kCols), BUS_OUTPUT, OUTPUTS_LEN };
	enum LightId { PIN_LIGHT, LIGHTS_LEN };

	// ---- the patch (edited by the knobs on the audio thread, by the menu / JSON on the UI thread;
	// the router gets a copy when `dirty` is set)
	conduit::Patch patch;
	ColCfg cfg[kCols];
	std::atomic<bool> dirty{true};
	std::atomic<bool> reloadKnobs{true};
	std::atomic<int> toggleReq{0};   // double-click on the matrix

	// settings (menu + JSON)
	int clockMode = 0;
	int tempoIndex = 1;
	int turnPlus = 2;       // TURN+ = lines >= kTurnLines[turnPlus]
	int mergeLine = 1;      // merged EVENT pulse height = this line
	int cvBipolar = 0;

	conduit::Router rt;

	// ---- audio thread
	dsp::SchmittTrigger clkTrig, trgTrig[4];
	dsp::BooleanTrigger pinBtn;
	struct Ev {
		int row, off;
	};
	Ev evs[kMaxBlockEvents];
	int numEvs = 0;
	int blockPos = 0;
	uint64_t sampleCount = 0, blockStart = 0;

	float eventPrev = 0.f;
	int sceneLast = -1;
	int barLast = -1;
	bool busWas = false;

	// tempo
	float posPrevBars = 0.f;
	double posUnwrapped = 0.0, posCycles = 0.0;
	float posMeasureT = 0.f, posMeasureBars = 0.f, posIdle = 10.f;
	float busBpm = 0.f;
	float clkSince = 10.f, clkPeriod = 0.f;
	float bpm = 120.f;
	int tempoSrc = 0;

	// outputs for the current block (computed at the end of the previous one)
	float outFrom[kCols]{}, outTo[kCols]{};
	bool gateOn[kCols]{};
	int trigAt[kCols];
	dsp::PulseGenerator trigPulse[kCols], mergePulse;
	float mergeEnergyV = 0.f;

	// pin editor
	int shownR = -1, shownC = -1;
	float shadow[5]{};

	// UI
	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;
	float peak[kRows][kCols]{};
	float rowFlash[kRows]{};
	bool colFire[kCols]{};

	Conduit() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configSwitch(ROW_PARAM, 0.f, kRows - 1, 0.f, "Row (source)",
		             std::vector<std::string>(kRowNames, kRowNames + kRows));
		configSwitch(COL_PARAM, 0.f, kCols - 1, 0.f, "Column (output)", {"OUT1", "OUT2", "OUT3", "OUT4", "OUT5", "OUT6", "OUT7", "OUT8"});
		configParam(AMOUNT_PARAM, -1.f, 1.f, 0.f, "Pin amount", " %", 0.f, 100.f)->description =
		    "The pin under the cursor; turning it on an empty crossing patches it";
		configSwitch(CURVE_PARAM, 0.f, conduit::kNumCurves - 1, 1.f, "Pin curve", {"LIN", "EXP", "LOG", "S", "GATE", "INV"});
		configParam(DELAY_PARAM, 0.f, 16.f, 0.f, "Pin delay", " 16ths")->snapEnabled = true;
		configParam(PROB_PARAM, 0.f, 1.f, 1.f, "Pin probability", " %", 0.f, 100.f);
		configParam(LEN_PARAM, 0.f, 1.f, knobFromLen(2.f), "Pin length", " 16ths", 256.f, 0.25f);
		configButton(PIN_PARAM, "Patch / pull the pin");
		configParam(MASTER_PARAM, 0.f, 1.f, 1.f, "Master", " %", 0.f, 100.f);
		for (int c = 0; c < kCols; c++)
			configParam(DEPTH_PARAM + c, -1.f, 1.f, 1.f, string::f("OUT%d depth", c + 1), " %", 0.f, 100.f);
		configInput(BUS_INPUT, "Bus")->description = "FamilyBus, 4 channels: POS, ENERGY, SCENE, EVENT";
		configInput(CLK_INPUT, "Clock")->description = "1/16 pulses (menu: 1/8, 1/4, 24 PPQN): the CLK row and the tempo when the BUS is not moving";
		for (int i = 0; i < 4; i++)
			configInput(TRG_INPUT + i, string::f("Trigger %d", i + 1));
		for (int i = 0; i < 4; i++)
			configInput(CV_INPUT + i, string::f("CV %d", i + 1))->description = "0-10 V (menu: ±5 V)";
		for (int c = 0; c < kCols; c++)
			configOutput(OUT_OUTPUT + c, string::f("OUT%d", c + 1));
		configOutput(BUS_OUTPUT, "Bus")->description = "BUS IN passed through, with the merged columns";
		configLight(PIN_LIGHT, "Pin patched");
		for (int c = 0; c < kCols; c++)
			trigAt[c] = -1;

		makeFactory(0, patch, cfg);
		rt.prepare(48000.0);
		rt.setPatch(patch);
	}

	// ---- helpers ----------------------------------------------------------------------------

	void applyCfg() {
		for (int c = 0; c < kCols; c++) {
			patch.col[c].type = dstType(cfg[c].type);
			patch.base[c] = kRests[clamp(cfg[c].rest, 0, 4)];
		}
		dirty = true;
	}

	void loadFactory(int i) {
		makeFactory(i, patch, cfg);
		dirty = true;
		reloadKnobs = true;
	}

	float busV(int ch) {
		Input& in = inputs[BUS_INPUT];
		return ch < in.getChannels() ? in.getVoltage(ch) : 0.f;
	}

	void addEvent(int row) {
		if (numEvs < kMaxBlockEvents)
			evs[numEvs++] = {row, blockPos};
		rowFlash[row] = 1.f;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		clockMode = 0;
		tempoIndex = 1;
		turnPlus = 2;
		mergeLine = 1;
		cvBipolar = 0;
		loadFactory(0);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		Module::onSampleRateChange(e);
		rt.reset();
	}

	// ---- the pin editor (audio thread) -------------------------------------------------------

	void loadKnobs(int r, int c) {
		const conduit::Pin& p = patch.pin[r][c];
		params[AMOUNT_PARAM].setValue(p.on ? p.amount : 0.f);
		const int defCurve = cfg[c].type == COL_GATE ? (int) conduit::CurveGate : cfg[c].type == COL_MOD ? (int) conduit::CurveExp : (int) conduit::CurveLin;
		params[CURVE_PARAM].setValue((float) (p.on ? (int) p.curve : defCurve));
		params[DELAY_PARAM].setValue(p.on ? p.delay : 0.f);
		params[PROB_PARAM].setValue(p.on ? p.prob : 1.f);
		params[LEN_PARAM].setValue(knobFromLen(p.on ? p.len : 2.f));
		for (int k = 0; k < 5; k++)
			shadow[k] = params[AMOUNT_PARAM + k].getValue();
		shownR = r;
		shownC = c;
	}

	void editPin() {
		const int r = clamp((int) std::round(params[ROW_PARAM].getValue()), 0, kRows - 1);
		const int c = clamp((int) std::round(params[COL_PARAM].getValue()), 0, kCols - 1);
		if (r != shownR || c != shownC || reloadKnobs.exchange(false)) {
			loadKnobs(r, c);
			return;
		}
		conduit::Pin& p = patch.pin[r][c];
		bool changed = false;
		float v[5];
		for (int k = 0; k < 5; k++) {
			v[k] = params[AMOUNT_PARAM + k].getValue();
			if (v[k] != shadow[k])
				changed = true;
		}
		if (changed) {
			if (!p.on && v[0] != shadow[0] && std::fabs(v[0]) > 0.01f)
				p.on = 1;
			if (p.on) {
				p.amount = v[0];
				p.curve = (uint8_t) clamp((int) std::round(v[1]), 0, conduit::kNumCurves - 1);
				p.delay = v[2];
				p.prob = v[3];
				p.len = lenFromKnob(v[4]);
				dirty = true;
			}
			for (int k = 0; k < 5; k++)
				shadow[k] = v[k];
		}
		int toggles = toggleReq.exchange(0);
		if (pinBtn.process(params[PIN_PARAM].getValue() > 0.f))
			toggles++;
		if (toggles % 2) {
			if (p.on)
				p = conduit::Pin{};
			else
				defaultPin(p, cfg[c].type, params[AMOUNT_PARAM].getValue());
			dirty = true;
			loadKnobs(r, c);
		}
	}

	// ---- audio ------------------------------------------------------------------------------

	void runBlock(const ProcessArgs& args) {
		if (dirty.exchange(false))
			rt.setPatch(patch);
		rt.setMaster(params[MASTER_PARAM].getValue());

		conduit::Block b;
		b.numSamples = kBlock;
		b.sampleRate = args.sampleRate;
		b.bpm = bpm;
		b.playing = tempoSrc == 2;
		b.ppq = b.playing ? (posCycles * semi::bus::kBarsPerCycle + posPrevBars) * 4.0 : 0.0;
		b.sampleTime = blockStart;
		rt.begin(b);
		const double ppqNow = b.playing ? b.ppq : -1.0;
		for (int i = 0; i < numEvs; i++)
			rt.hit(evs[i].row, evs[i].off, 1.f, ppqNow);
		numEvs = 0;

		// value rows
		const bool bus = inputs[BUS_INPUT].isConnected();
		rt.value(R_ENERGY, bus ? clamp(busV(semi::bus::kEnergy) / 10.f, 0.f, 1.f) : 0.f);
		const float bars = semi::bus::voltsToBars(busV(semi::bus::kPos));
		rt.value(R_PHASE, bus ? bars - std::floor(bars) : 0.f);
		for (int i = 0; i < 4; i++) {
			const float v = inputs[CV_INPUT + i].getVoltage();
			rt.value(R_CV1 + i, clamp(cvBipolar ? (v + 5.f) / 10.f : v / 10.f, 0.f, 1.f));
		}
		rt.end();

		// the columns -> next block's outputs
		mergeEnergyV = 0.f;
		for (int c = 0; c < kCols; c++) {
			const float depth = params[DEPTH_PARAM + c].getValue();
			const float o = clamp(rt.out[c] * depth, -1.f, 1.f);
			outFrom[c] = outTo[c];
			trigAt[c] = -1;
			switch (cfg[c].type) {
				case COL_MOD:
					outTo[c] = cfg[c].bipolar ? 5.f * o : 10.f * clamp(kRests[clamp(cfg[c].rest, 0, 4)] + o, 0.f, 1.f);
					if (cfg[c].merge == MERGE_ENERGY)
						mergeEnergyV += cfg[c].bipolar ? outTo[c] : outTo[c] - 10.f * kRests[clamp(cfg[c].rest, 0, 4)];
					break;
				case COL_GATE:
					gateOn[c] = o > 0.5f;
					outTo[c] = outFrom[c] = gateOn[c] ? 10.f : 0.f;
					if (cfg[c].merge == MERGE_ENERGY)
						mergeEnergyV += gateOn[c] ? 10.f : 0.f;
					break;
				default:
					outTo[c] = outFrom[c] = 0.f;
					break;
			}
		}
		for (int i = 0; i < rt.numTrigs; i++) {
			const int c = rt.trigs[i].col;
			if (c < 0 || c >= kCols || cfg[c].type != COL_TRIG || std::fabs(params[DEPTH_PARAM + c].getValue()) < 0.02f)
				continue;
			if (trigAt[c] < 0 || rt.trigs[i].offset < trigAt[c])
				trigAt[c] = rt.trigs[i].offset;
		}

		// UI peaks
		for (int r = 0; r < kRows; r++)
			for (int c = 0; c < kCols; c++)
				peak[r][c] = std::max(peak[r][c], std::fabs(rt.pinLevel[r][c]));
		blockStart += kBlock;
	}

	void tempo(const ProcessArgs& args, bool bus) {
		// BUS POS: unwrap, measure bars per second over ~0.25 s
		const float bars = semi::bus::voltsToBars(busV(semi::bus::kPos));
		if (bus) {
			float d = bars - posPrevBars;
			if (d < -semi::bus::kBarsPerCycle * 0.5f) {
				d += semi::bus::kBarsPerCycle;
				posCycles += 1.0;
			}
			else if (d > semi::bus::kBarsPerCycle * 0.5f) {
				d -= semi::bus::kBarsPerCycle;
				posCycles = std::max(0.0, posCycles - 1.0);
			}
			if (std::fabs(d) > 1e-6f && d < 2.f && d > 0.f)
				posIdle = 0.f;
			else
				posIdle += args.sampleTime;
			if (d > 0.f && d < 2.f)
				posMeasureBars += d;
			posMeasureT += args.sampleTime;
			if (posMeasureT >= 0.25f) {
				if (posMeasureBars > 0.f) {
					const float b = posMeasureBars / posMeasureT * 4.f * 60.f;
					busBpm = busBpm > 0.f ? busBpm + 0.5f * (b - busBpm) : b;
				}
				posMeasureT = posMeasureBars = 0.f;
			}
		}
		else
			posIdle += args.sampleTime;
		posPrevBars = bars;

		clkSince += args.sampleTime;
		if (posIdle < 0.5f && busBpm > 20.f) {
			bpm = clamp(busBpm, 20.f, 400.f);
			tempoSrc = 2;
		}
		else if (inputs[CLK_INPUT].isConnected() && clkPeriod > 0.f && clkSince < 2.f) {
			bpm = clamp(60.f / (clkPeriod * 4.f / kClockSteps[clamp(clockMode, 0, 3)]), 20.f, 400.f);
			tempoSrc = 1;
		}
		else {
			bpm = kTempos[clamp(tempoIndex, 0, kNumTempos - 1)];
			tempoSrc = 0;
		}
	}

	void process(const ProcessArgs& args) override {
		if (blockPos == 0)
			editPin();
		const bool bus = inputs[BUS_INPUT].isConnected();

		// ---- events on this sample
		if (clkTrig.process(inputs[CLK_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (clkSince < 2.f)
				clkPeriod = clkSince;
			clkSince = 0.f;
			addEvent(R_CLK);
		}
		for (int i = 0; i < 4; i++)
			if (trgTrig[i].process(inputs[TRG_INPUT + i].getVoltage(), 0.1f, 1.f))
				addEvent(R_TRG1 + i);
		tempo(args, bus);
		if (bus) {
			const float ev = busV(semi::bus::kEvent);
			if (ev > 1.f && eventPrev <= 1.f) {
				addEvent(R_TURN);
				if (semi::bus::voltsToTurn(ev) >= kTurnLines[clamp(turnPlus, 0, 3)])
					addEvent(R_TURNX);
			}
			eventPrev = ev;
			const int scene = semi::bus::voltsToScene(busV(semi::bus::kScene));
			if (busWas && scene != sceneLast)
				addEvent(R_SCENE);
			sceneLast = scene;
			const int bar = (int) std::floor(semi::bus::voltsToBars(busV(semi::bus::kPos)));
			if (busWas && bar != barLast && posIdle < 0.5f)
				addEvent(R_BAR);
			barLast = bar;
		}
		busWas = bus;

		// ---- outputs (this block)
		const float t = (blockPos + 1) / (float) kBlock;
		bool mergeFire = false;
		for (int c = 0; c < kCols; c++) {
			float v;
			if (cfg[c].type == COL_TRIG) {
				if (trigAt[c] == blockPos) {
					trigPulse[c].trigger(1e-3f);
					colFire[c] = true;
					if (cfg[c].merge == MERGE_EVENT)
						mergeFire = true;
				}
				v = trigPulse[c].process(args.sampleTime) ? 10.f : 0.f;
			}
			else
				v = outFrom[c] + (outTo[c] - outFrom[c]) * t;
			outputs[OUT_OUTPUT + c].setVoltage(v);
		}
		if (mergeFire)
			mergePulse.trigger(semi::bus::kPulseSec);
		const bool mp = mergePulse.process(args.sampleTime);

		Output& bo = outputs[BUS_OUTPUT];
		bo.setChannels(semi::bus::kChannels);
		bo.setVoltage(busV(semi::bus::kPos), semi::bus::kPos);
		bo.setVoltage(clamp(busV(semi::bus::kEnergy) + mergeEnergyV, 0.f, 10.f), semi::bus::kEnergy);
		bo.setVoltage(busV(semi::bus::kScene), semi::bus::kScene);
		const float evIn = busV(semi::bus::kEvent);
		bo.setVoltage(mp ? std::max(evIn, semi::bus::turnToVolts(kTurnLines[clamp(mergeLine, 0, 3)])) : evIn, semi::bus::kEvent);

		if (++blockPos >= kBlock) {
			blockPos = 0;
			runBlock(args);
		}
		sampleCount++;

		if (ui.tick(args.sampleTime))
			publish();
	}

	void publish() {
		const int r = shownR < 0 ? 0 : shownR, c = shownC < 0 ? 0 : shownC;
		lights[PIN_LIGHT].setBrightness(patch.pin[r][c].on ? 1.f : 0.f);
		Snapshot& s = snap.writeSlot();
		std::memcpy((void*) s.pin, (const void*) patch.pin, sizeof s.pin);
		const bool bus = inputs[BUS_INPUT].isConnected();
		for (int i = 0; i < kRows; i++) {
			for (int j = 0; j < kCols; j++) {
				s.level[i][j] = peak[i][j];
				peak[i][j] = 0.f;
			}
			if (rowIsEvent(i)) {
				s.rowLevel[i] = rowFlash[i];
				rowFlash[i] = 0.f;
			}
			else
				s.rowLevel[i] = rt.rowLevel[i];
			s.rowLive[i] = i == R_CLK ? inputs[CLK_INPUT].isConnected()
			             : i <= R_PHASE ? bus
			             : i <= R_TRG4 ? inputs[TRG_INPUT + i - R_TRG1].isConnected()
			                           : inputs[CV_INPUT + i - R_CV1].isConnected();
		}
		for (int j = 0; j < kCols; j++) {
			s.colOut[j] = clamp(rt.out[j] * params[DEPTH_PARAM + j].getValue(), -1.f, 1.f);
			s.colVolts[j] = outputs[OUT_OUTPUT + j].getVoltage();
			s.colFire[j] = colFire[j];
			colFire[j] = false;
			s.colType[j] = (uint8_t) cfg[j].type;
			s.colMerge[j] = (uint8_t) cfg[j].merge;
		}
		s.selR = r;
		s.selC = c;
		s.bpm = bpm;
		s.tempoSrc = tempoSrc;
		s.pins = patch.numPins();
		std::memcpy(s.name, patch.name, sizeof s.name);
		snap.publish();
	}

	// ---- state ------------------------------------------------------------------------------

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "name", json_string(patch.name));
		json_object_set_new(root, "seed", json_integer(patch.seed));
		json_t* pins = json_array();
		for (int r = 0; r < kRows; r++)
			for (int c = 0; c < kCols; c++) {
				const conduit::Pin& p = patch.pin[r][c];
				if (!p.on)
					continue;
				json_t* q = json_array();
				json_array_append_new(q, json_integer(r));
				json_array_append_new(q, json_integer(c));
				json_array_append_new(q, json_integer(p.curve));
				json_array_append_new(q, json_real(p.amount));
				json_array_append_new(q, json_real(p.delay));
				json_array_append_new(q, json_real(p.prob));
				json_array_append_new(q, json_real(p.len));
				json_array_append_new(pins, q);
			}
		json_object_set_new(root, "pins", pins);
		json_t* cols = json_array();
		for (int c = 0; c < kCols; c++) {
			json_t* q = json_array();
			json_array_append_new(q, json_integer(cfg[c].type));
			json_array_append_new(q, json_integer(cfg[c].bipolar));
			json_array_append_new(q, json_integer(cfg[c].rest));
			json_array_append_new(q, json_integer(cfg[c].merge));
			json_array_append_new(cols, q);
		}
		json_object_set_new(root, "cols", cols);
		json_object_set_new(root, "clock", json_integer(clockMode));
		json_object_set_new(root, "tempo", json_integer(tempoIndex));
		json_object_set_new(root, "turnPlus", json_integer(turnPlus));
		json_object_set_new(root, "mergeLine", json_integer(mergeLine));
		json_object_set_new(root, "cvBipolar", json_integer(cvBipolar));
		return root;
	}

	void dataFromJson(json_t* root) override {
		auto geti = [&](const char* k, int& v, int lo, int hi) {
			if (json_t* j = json_object_get(root, k))
				v = clamp((int) json_integer_value(j), lo, hi);
		};
		geti("clock", clockMode, 0, 3);
		geti("tempo", tempoIndex, 0, kNumTempos - 1);
		geti("turnPlus", turnPlus, 0, 3);
		geti("mergeLine", mergeLine, 0, 3);
		geti("cvBipolar", cvBipolar, 0, 1);
		conduit::Patch p;
		if (json_t* j = json_object_get(root, "name"))
			p.setName(json_string_value(j) ? json_string_value(j) : "");
		if (json_t* j = json_object_get(root, "seed"))
			p.seed = (uint32_t) std::max<json_int_t>(1, json_integer_value(j));
		size_t i;
		json_t* q;
		json_array_foreach(json_object_get(root, "cols"), i, q) {
			if (i >= (size_t) kCols)
				break;
			cfg[i].type = clamp((int) json_integer_value(json_array_get(q, 0)), 0, 2);
			cfg[i].bipolar = clamp((int) json_integer_value(json_array_get(q, 1)), 0, 1);
			cfg[i].rest = clamp((int) json_integer_value(json_array_get(q, 2)), 0, 4);
			cfg[i].merge = clamp((int) json_integer_value(json_array_get(q, 3)), 0, 2);
		}
		json_array_foreach(json_object_get(root, "pins"), i, q) {
			const int r = (int) json_integer_value(json_array_get(q, 0));
			const int c = (int) json_integer_value(json_array_get(q, 1));
			if (r < 0 || r >= kRows || c < 0 || c >= kCols)
				continue;
			conduit::Pin& pn = p.pin[r][c];
			pn.on = 1;
			pn.curve = (uint8_t) clamp((int) json_integer_value(json_array_get(q, 2)), 0, conduit::kNumCurves - 1);
			pn.amount = clamp((float) json_number_value(json_array_get(q, 3)), -1.f, 1.f);
			pn.delay = clamp((float) json_number_value(json_array_get(q, 4)), 0.f, 16.f);
			pn.prob = clamp((float) json_number_value(json_array_get(q, 5)), 0.f, 1.f);
			pn.len = clamp((float) json_number_value(json_array_get(q, 6)), 0.25f, 64.f);
		}
		initEndpoints(p, cfg);
		patch = p;
		dirty = true;
		reloadKnobs = true;
	}
};

// ---- screens --------------------------------------------------------------------------------

namespace {

const Snapshot& demoSnapshot() {
	static const Snapshot demo = [] {
		Snapshot s;
		conduit::Patch p;
		ColCfg cfg[kCols];
		makeFactory(0, p, cfg);
		std::memcpy((void*) s.pin, (const void*) p.pin, sizeof s.pin);
		for (int r = 0; r < kRows; r++)
			s.rowLive[r] = r <= R_PHASE || r == R_TRG1 || r == R_CV1;
		s.rowLevel[R_ENERGY] = 0.72f;
		s.rowLevel[R_PHASE] = 0.4f;
		s.rowLevel[R_BAR] = 1.f;
		s.level[R_BAR][0] = 0.6f;
		s.level[R_ENERGY][1] = 0.72f;
		for (int c = 0; c < kCols; c++) {
			s.colType[c] = (uint8_t) cfg[c].type;
			s.colOut[c] = c == 0 ? -0.45f : c == 1 ? 0.72f : c == 7 ? 1.f : 0.f;
		}
		s.colFire[4] = true;
		s.selR = R_BAR;
		s.selC = 0;
		s.bpm = 172.f;
		s.tempoSrc = 2;
		s.pins = p.numPins();
		std::snprintf(s.name, sizeof s.name, "%s", p.name);
		return s;
	}();
	return demo;
}

NVGcolor colTypeColor(int t) {
	return t == COL_TRIG ? semi::theme::magenta() : t == COL_GATE ? semi::theme::amber() : semi::theme::cyan();
}

// Geometry shared by drawing and clicking.
struct MatrixGeo {
	float w, h, labelW, headH, cw, rh;
	MatrixGeo(float w_, float h_) : w(w_), h(h_) {
		labelW = 36.f;
		headH = 12.f;
		cw = (w - labelW - 2.f) / kCols;
		rh = (h - headH - 1.f) / kRows;
	}
	float cx(int c) const { return labelW + cw * (c + 0.5f); }
	float cy(int r) const { return headH + rh * (r + 0.5f); }
};

} // namespace

// The pin matrix: rows = sources (LED: patched / hit / value), columns = outputs (type letter, meter),
// a square per pin sized by AMOUNT (hollow = PROB < 100 % or delayed, amber = negative, magenta while it
// works), the cursor in magenta.
struct MatrixScreen : Screen {
	Conduit* module = nullptr;

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demoSnapshot();
		const MatrixGeo g(box.size.x, box.size.y);

		// cursor row / column bands
		fillRect(vg, 0.f, g.headH + g.rh * s.selR, g.w, g.rh, semi::theme::grid());
		fillRect(vg, g.labelW + g.cw * s.selC, 0.f, g.cw, g.h, semi::theme::grid());

		// column heads: number + type letter, a meter under it
		for (int c = 0; c < kCols; c++) {
			const float x = g.cx(c);
			const NVGcolor tc = colTypeColor(s.colType[c]);
			char nm[4];
			std::snprintf(nm, sizeof nm, "%d", c + 1);
			text(vg, x - 1.f, 1.f, nm, 9.f, c == s.selC ? semi::theme::magenta() : semi::theme::text(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
			text(vg, x + 0.5f, 1.f, kColTypeShort[s.colType[c]], 9.f, s.colFire[c] ? semi::theme::magenta() : tc, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
			const float mw = g.cw - 4.f;
			fillRect(vg, x - mw / 2.f, g.headH - 2.5f, mw, 2.f, semi::theme::cell());
			const float o = s.colOut[c];
			if (o > 0.004f)
				fillRect(vg, x - mw / 2.f, g.headH - 2.5f, mw * std::min(1.f, o), 2.f, tc);
			else if (o < -0.004f)
				fillRect(vg, x - mw / 2.f, g.headH - 2.5f, mw * std::min(1.f, -o), 2.f, semi::theme::amber());
		}

		for (int r = 0; r < kRows; r++) {
			const float y = g.cy(r);
			// LED: dim = nothing patched, cyan = patched, magenta = a hit / the value
			const float lv = s.rowLevel[r];
			NVGcolor led = !s.rowLive[r] ? semi::theme::cell()
			             : rowIsEvent(r) ? (lv > 0.5f ? semi::theme::magenta() : semi::theme::cyanDim())
			                             : (lv > 0.5f ? semi::theme::cyan() : lv > 0.1f ? semi::theme::cyanDim() : semi::theme::cyanDeep());
			fillRect(vg, 2.f, y - 1.6f, 3.2f, 3.2f, led);
			text(vg, 7.f, y, kRowNames[r], 8.f, r == s.selR ? semi::theme::magenta() : (s.rowLive[r] ? semi::theme::text() : semi::theme::dim()),
			     NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			for (int c = 0; c < kCols; c++) {
				const float x = g.cx(c);
				const conduit::Pin& p = s.pin[r][c];
				if (!p.on) {
					fillRect(vg, x - 0.8f, y - 0.8f, 1.6f, 1.6f, semi::theme::cyanDeep());
					continue;
				}
				const float maxSide = std::min(g.cw, g.rh) - 1.5f;
				const float side = std::max(2.6f, maxSide * std::min(1.f, std::fabs(p.amount)));
				const bool working = s.level[r][c] > 0.05f;
				NVGcolor col = working ? semi::theme::magenta() : (p.amount < 0.f ? semi::theme::amber() : semi::theme::cyan());
				const bool hollow = p.prob < 0.999f || p.delay > 0.f;
				if (hollow) {
					nvgBeginPath(vg);
					nvgRect(vg, x - side / 2.f, y - side / 2.f, side, side);
					nvgStrokeColor(vg, col);
					nvgStrokeWidth(vg, semi::platform::kThinStroke);
					nvgStroke(vg);
					const float fh = side * clamp(p.prob, 0.f, 1.f);
					fillRect(vg, x - side / 2.f, y + side / 2.f - fh, side, fh, col);
				}
				else
					fillRect(vg, x - side / 2.f, y - side / 2.f, side, side, col);
			}
		}

		// cursor
		nvgBeginPath(vg);
		nvgRect(vg, g.labelW + g.cw * s.selC + 0.5f, g.headH + g.rh * s.selR + 0.2f, g.cw - 1.f, g.rh - 0.4f);
		nvgStrokeColor(vg, semi::theme::magenta());
		nvgStrokeWidth(vg, semi::platform::kThinStroke);
		nvgStroke(vg);
	}

	// Rack: click = move the cursor, double-click = patch / pull
	bool hit(Vec p, int& r, int& c) {
		const MatrixGeo g(box.size.x, box.size.y);
		if (p.x < g.labelW || p.y < g.headH)
			return false;
		c = clamp((int) ((p.x - g.labelW) / g.cw), 0, kCols - 1);
		r = clamp((int) ((p.y - g.headH) / g.rh), 0, kRows - 1);
		return true;
	}
	void onButton(const ButtonEvent& e) override {
		int r, c;
		if (module && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && hit(e.pos, r, c)) {
			module->params[Conduit::ROW_PARAM].setValue((float) r);
			module->params[Conduit::COL_PARAM].setValue((float) c);
			e.consume(this);
			return;
		}
		Screen::onButton(e);
	}
	void onDoubleClick(const DoubleClickEvent& e) override {
		if (module) {
			module->toggleReq++;
			e.consume(this);
		}
	}
};

// The pin under the cursor: source -> output, its response (an envelope over LENGTH for an event row, a
// transfer curve for a value row), the five values, the output and the tempo.
struct PinScreen : Screen {
	Conduit* module = nullptr;

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demoSnapshot();
		const float w = box.size.x, h = box.size.y;
		const conduit::Pin& p = s.pin[s.selR][s.selC];
		const bool ev = rowIsEvent(s.selR);
		const float pad = 3.f;

		text(vg, pad, 2.f, kRowNames[s.selR], 9.f, semi::theme::text());
		char buf[32];
		std::snprintf(buf, sizeof buf, "> OUT%d %s", s.selC + 1, kColTypeShort[s.colType[s.selC]]);
		text(vg, pad, 12.f, buf, 9.f, colTypeColor(s.colType[s.selC]));

		// response graph
		const float gx = pad, gy = 24.f, gw = w - 2.f * pad, gh = 40.f;
		fillRect(vg, gx, gy, gw, gh, semi::theme::cell());
		const float zy = gy + gh * 0.5f;
		semi::Screen::dashed(vg, gx, zy, gx + gw, zy, semi::theme::grid(), 3.f);
		if (p.on) {
			constexpr int n = 32;
			float ys[n];
			for (int i = 0; i < n; i++) {
				const float x = i / (float) (n - 1);
				const float v = ev ? conduit::shape(p.curve, x < 0.999f ? 1.f - x : 0.f, true) : conduit::shape(p.curve, x, false);
				ys[i] = zy - (gh * 0.5f - 2.f) * clamp(p.amount * v, -1.f, 1.f);
			}
			// delay as a dead stretch on the left (event rows: in 16ths against the envelope length)
			float d0 = 0.f;
			if (p.delay > 0.f)
				d0 = gw * clamp(p.delay / (p.delay + (ev ? p.len : 4.f)), 0.f, 0.8f);
			if (d0 > 0.f)
				line(vg, gx, zy, gx + d0, zy, semi::theme::dim(), semi::platform::kStroke);
			polyline(vg, ys, n, gx + d0, gw - d0, p.amount < 0.f ? semi::theme::amber() : semi::theme::cyan());
		}
		else
			text(vg, gx + gw / 2.f, zy, "NO PIN", 9.f, semi::theme::dim(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

		// values
		float y = gy + gh + 4.f;
		auto row = [&](const char* label, const char* val, NVGcolor c) {
			text(vg, pad, y, label, 8.f, semi::theme::dim());
			text(vg, w - pad, y, val, 9.f, c, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
			y += 10.5f;
		};
		const NVGcolor vc = p.on ? semi::theme::text() : semi::theme::dim();
		std::snprintf(buf, sizeof buf, "%+d%%", (int) std::round(p.amount * 100.f));
		row("AMT", p.on ? buf : "--", p.on ? (p.amount < 0.f ? semi::theme::amber() : semi::theme::cyan()) : semi::theme::dim());
		row("CRV", p.on ? conduit::curveName(p.curve) : "--", vc);
		std::snprintf(buf, sizeof buf, "%g/16", std::round(p.delay * 4.f) / 4.f);
		row("DLY", p.on ? buf : "--", vc);
		std::snprintf(buf, sizeof buf, "%d%%", (int) std::round(p.prob * 100.f));
		row("PRB", p.on ? buf : "--", vc);
		if (p.len < 1.f)
			std::snprintf(buf, sizeof buf, "%.2f/16", p.len);
		else
			std::snprintf(buf, sizeof buf, "%d/16", (int) std::round(p.len));
		row("LEN", p.on && ev ? buf : "--", vc);

		// footer: OUT n volts, tempo
		const float fy = h - 21.f;
		line(vg, pad, fy - 2.f, w - pad, fy - 2.f, semi::theme::grid());
		std::snprintf(buf, sizeof buf, "%+.2fV", s.colVolts[s.selC]);
		text(vg, pad, fy, "OUT", 8.f, semi::theme::dim());
		text(vg, w - pad, fy, buf, 9.f, colTypeColor(s.colType[s.selC]), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		static const char* src[] = {"INT", "CLK", "BUS"};
		std::snprintf(buf, sizeof buf, "%.0f", s.bpm);
		text(vg, pad, fy + 10.f, src[clamp(s.tempoSrc, 0, 2)], 8.f, s.tempoSrc == 2 ? semi::theme::magenta() : semi::theme::dim());
		text(vg, w - pad, fy + 10.f, buf, 9.f, semi::theme::text(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
	}
};

struct ConduitWidget : ModuleWidget {
	ConduitWidget(Conduit* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Conduit.svg")));

		auto* mx = semi::createScreenMm<MatrixScreen>(Vec(L::MATRIX.x, L::MATRIX.y), Vec(L::MATRIX.w, L::MATRIX.h));
		mx->module = module;
		addChild(mx);
		auto* ps = semi::createScreenMm<PinScreen>(Vec(L::PIN.x, L::PIN.y), Vec(L::PIN.w, L::PIN.h));
		ps->module = module;
		addChild(ps);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		auto snapKnob = [&](L::P p, int id) {
			auto* k = createParamCentered<semi::KnobS>(at(p), module, id);
			k->snap = true;
			addParam(k);
		};
		snapKnob(L::ROW, Conduit::ROW_PARAM);
		snapKnob(L::COL, Conduit::COL_PARAM);
		addParam(createParamCentered<semi::KnobS>(at(L::AMOUNT), module, Conduit::AMOUNT_PARAM));
		snapKnob(L::CURVE, Conduit::CURVE_PARAM);
		addParam(createParamCentered<semi::KnobS>(at(L::DELAY), module, Conduit::DELAY_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::PROB), module, Conduit::PROB_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::LEN), module, Conduit::LEN_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::PINBTN), module, Conduit::PIN_PARAM));
		addChild(createLightCentered<semi::MagentaLedSmall>(at(L::PIN_LED), module, Conduit::PIN_LIGHT));
		addParam(createParamCentered<semi::KnobM>(at(L::MASTER), module, Conduit::MASTER_PARAM));

		addInput(createInputCentered<semi::BusJack>(at(L::BUS_IN), module, Conduit::BUS_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::CLK), module, Conduit::CLK_INPUT));
		const L::P trg[4] = {L::TRG1, L::TRG2, L::TRG3, L::TRG4};
		const L::P cv[4] = {L::CV1, L::CV2, L::CV3, L::CV4};
		for (int i = 0; i < 4; i++) {
			addInput(createInputCentered<semi::Jack>(at(trg[i]), module, Conduit::TRG_INPUT + i));
			addInput(createInputCentered<semi::Jack>(at(cv[i]), module, Conduit::CV_INPUT + i));
		}
		const L::P depth[kCols] = {L::DEPTH1, L::DEPTH2, L::DEPTH3, L::DEPTH4, L::DEPTH5, L::DEPTH6, L::DEPTH7, L::DEPTH8};
		const L::P out[kCols] = {L::OUT1, L::OUT2, L::OUT3, L::OUT4, L::OUT5, L::OUT6, L::OUT7, L::OUT8};
		for (int c = 0; c < kCols; c++) {
			addParam(createParamCentered<semi::KnobS>(at(depth[c]), module, Conduit::DEPTH_PARAM + c));
			addOutput(createOutputCentered<semi::Jack>(at(out[c]), module, Conduit::OUT_OUTPUT + c));
		}
		addOutput(createOutputCentered<semi::BusJack>(at(L::BUS_OUT), module, Conduit::BUS_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Conduit*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel(string::f("Patch: %s (%d pins)", m->patch.name, m->patch.numPins())));
		menu->addChild(createSubmenuItem("Load factory patch", "", [=](Menu* sub) {
			for (int i = 0; i < kNumFactory; i++)
				sub->addChild(createMenuItem(kFactory[i], "", [=]() { m->loadFactory(i); }));
		}));
		menu->addChild(createMenuItem("Pull every pin in this row", "", [=]() {
			const int r = clamp((int) std::round(m->params[Conduit::ROW_PARAM].getValue()), 0, kRows - 1);
			for (auto& p : m->patch.pin[r])
				p = conduit::Pin{};
			m->dirty = true;
			m->reloadKnobs = true;
		}));
		menu->addChild(createMenuItem("Pull every pin in this column", "", [=]() {
			const int c = clamp((int) std::round(m->params[Conduit::COL_PARAM].getValue()), 0, kCols - 1);
			for (auto& rr : m->patch.pin)
				rr[c] = conduit::Pin{};
			m->dirty = true;
			m->reloadKnobs = true;
		}));
		menu->addChild(createMenuItem("Dice (new PROB seed)", string::f("seed %u", (unsigned) m->patch.seed), [=]() {
			m->patch.seed = m->patch.seed * 1664525u + 1013904223u;
			if (m->patch.seed == 0)
				m->patch.seed = 1;
			m->dirty = true;
		}));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Outputs"));
		for (int c = 0; c < kCols; c++) {
			menu->addChild(createSubmenuItem(string::f("OUT%d", c + 1), kColTypeShort[m->cfg[c].type], [=](Menu* sub) {
				sub->addChild(createIndexSubmenuItem("Type", {"MOD (CV)", "TRIG", "GATE"},
				    [=]() { return (size_t) m->cfg[c].type; }, [=](size_t i) { m->cfg[c].type = (int) i; m->applyCfg(); m->reloadKnobs = true; }));
				sub->addChild(createIndexSubmenuItem("MOD range", {"0..10 V", "±5 V"},
				    [=]() { return (size_t) m->cfg[c].bipolar; }, [=](size_t i) { m->cfg[c].bipolar = (int) i; }));
				sub->addChild(createIndexSubmenuItem("MOD rest (0..10 V)", {"0 %", "25 %", "50 %", "75 %", "100 %"},
				    [=]() { return (size_t) m->cfg[c].rest; }, [=](size_t i) { m->cfg[c].rest = (int) i; m->applyCfg(); }));
				sub->addChild(createIndexSubmenuItem("Merge onto BUS OUT", {"Off", "ENERGY (adds)", "EVENT (triggers)"},
				    [=]() { return (size_t) m->cfg[c].merge; }, [=](size_t i) { m->cfg[c].merge = (int) i; }));
			}));
		}

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Inputs"));
		menu->addChild(createIndexSubmenuItem("TURN+ row: lines of", {"4+ bars", "8+ bars", "16+ bars", "32 bars"},
		    [=]() { return (size_t) m->turnPlus; }, [=](size_t i) { m->turnPlus = (int) i; }));
		menu->addChild(createIndexSubmenuItem("Merged EVENT pulse", {"4-bar line", "8-bar line", "16-bar line", "32-bar line"},
		    [=]() { return (size_t) m->mergeLine; }, [=](size_t i) { m->mergeLine = (int) i; }));
		menu->addChild(createIndexSubmenuItem("CV inputs", {"0..10 V", "±5 V"},
		    [=]() { return (size_t) m->cvBipolar; }, [=](size_t i) { m->cvBipolar = (int) i; }));
		menu->addChild(createIndexSubmenuItem("CLK input", {"1/16", "1/8", "1/4 (beat)", "24 PPQN"},
		    [=]() { return (size_t) m->clockMode; }, [=](size_t i) { m->clockMode = (int) i; }));
		menu->addChild(createIndexSubmenuItem("Tempo without BUS / CLK", {"90", "120", "140", "160", "172", "174"},
		    [=]() { return (size_t) m->tempoIndex; }, [=](size_t i) { m->tempoIndex = (int) i; }));
	}
};

Model* modelConduit = createModel<Conduit, ConduitWidget>("Conduit");
