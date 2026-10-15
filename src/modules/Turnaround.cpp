// TURNAROUND — Rack / MetaModule port of TURNAROUND (engine: engines/turnaround,
// unchanged from semierendonmez-commits/turnaround Source/engine).
//
// A phrase-end modulator for breaks: every 4th bar line is a turn (4 / 8 / 16 /
// 32-bar tier), and before it the engine rewrites the last beats of the break
// that plays through IN, using the break's own slices (ghost kicks, rolls,
// retriggers, reverse, tape stop, cut), paid for out of an edit budget per
// 32 bars, then the wall: silence right before the bar line.
//
// Position (no host transport in Rack):
//   * CLK = 1/16 notes (4 PPQN). The first clock after RST (or after start) is
//     the downbeat of bar 1 of the 32-bar block. Tempo = measured clock period.
//     The transport stops (audio passes dry) when the clock stops for 2.5
//     periods.
//   * BUS patched: the bar comes from POS (FamilyBus CV ch 1, EPOCH's 32-bar
//     cycle) and ENERGY (ch 2) scales the edit depth. CLK, if also patched,
//     only refines the tempo; otherwise the tempo is read from POS's slope.
//
// Rack audio is +-5 V; 5 V = 1.0 for the engine. Mono IN -> both engine
// channels; OUT is the left channel.

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/FamilyBusCV.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/TurnaroundLayout.hpp"
#include "turnaround/Engine.h"

namespace L = semi::layout::turnaround;
namespace bus = semi::bus;
using semi::Screen;

namespace {

constexpr int kBlock = 32;          // engine block; adds 32 samples of latency
constexpr float kVoltsPerUnit = 5.f;
constexpr int kLeds = 8;

// Choice tables from the plugin (Source/Params.h).
constexpr double kWindowBeats[] = {0.5, 1.0, 2.0, 3.0, 4.0, 6.0, 8.0, 16.0};
constexpr double kGapBeats[] = {0.5, 1.0, 2.0, 4.0};       // GAP knob (the plugin's OFF is WALL = OFF)
constexpr double kStartBeats[] = {1.0, 0.5, 0.25, 0.125};
constexpr double kEndBeats[] = {0.5, 0.25, 0.125, 0.0625};
constexpr float kHierarchy[] = {0.f, 0.25f, 0.5f, 0.7f, 0.85f, 1.f};
constexpr float kLayer2[] = {0.f, 0.5f, 1.f};

const std::vector<std::string> kActionNames = {"OFF", "GHOST", "ROLL", "RETRIG", "REVERSE", "TAPE STOP", "CUT"};
const std::vector<std::string> kWindowNames = {"1/2 beat", "1 beat", "2 beats", "3 beats", "1 bar", "6 beats", "2 bars", "4 bars"};
const std::vector<std::string> kPresetNames = {"TURNAROUND", "ROLLER TURNS", "JUNGLE '94", "TAPE + WALL", "MINIMAL", "SPEND IT ALL"};

// Everything a factory preset sets (the plugin's values; knobs included).
struct Preset {
	int act[4] = {1, 2, 3, 2};
	int win[4] = {2, 2, 4, 4};
	float inten[4] = {0.6f, 0.75f, 0.8f, 1.f};
	float budget = 0.5f, vary = 0.25f, pitch = 5.f;
	int hier = 3;           // index into kHierarchy (0.7)
	int wall = 3, gap = 1;  // WALL 16 UP, GAP 1 beat
	int source = 0, start = 1, end = 2, layer2 = 0;
};

Preset makePreset(int i) {
	Preset p;
	switch (i) {
		case 1:  // ROLLER TURNS
			p.act[0] = 0;
			p.act[1] = 2; p.win[1] = 1; p.inten[1] = 0.6f;
			p.act[2] = 2; p.win[2] = 2; p.inten[2] = 0.9f;
			p.act[3] = 2; p.win[3] = 4; p.inten[3] = 1.f;
			p.budget = 0.55f; p.hier = 4; p.wall = 3; p.gap = 1; p.pitch = 7.f;
			break;
		case 2:  // JUNGLE '94
			p.act[0] = 1; p.win[0] = 4; p.inten[0] = 0.7f;
			p.act[1] = 3; p.win[1] = 2; p.inten[1] = 0.7f;
			p.act[2] = 3; p.win[2] = 4; p.inten[2] = 0.9f;
			p.act[3] = 2; p.win[3] = 4;
			p.source = 3; p.vary = 0.6f; p.gap = 0; p.wall = 4; p.layer2 = 1; p.budget = 0.7f;
			break;
		case 3:  // TAPE + WALL
			p.act[0] = 0;
			p.act[1] = 1; p.inten[1] = 0.5f;
			p.act[2] = 4; p.win[2] = 2; p.inten[2] = 1.f;
			p.act[3] = 5; p.win[3] = 4; p.inten[3] = 1.f;
			p.gap = 1; p.wall = 3; p.budget = 0.6f;
			break;
		case 4:  // MINIMAL
			p.act[0] = 0;
			p.act[1] = 1; p.inten[1] = 0.5f;
			p.act[2] = 2; p.win[2] = 1; p.inten[2] = 0.7f;
			p.act[3] = 6; p.win[3] = 2;
			p.budget = 0.3f; p.wall = 0;
			break;
		case 5:  // SPEND IT ALL
			p.act[0] = 3; p.win[0] = 2; p.inten[0] = 1.f;
			p.act[1] = 2; p.win[1] = 4; p.inten[1] = 1.f;
			p.act[2] = 3; p.win[2] = 5; p.inten[2] = 1.f;
			p.act[3] = 2; p.win[3] = 5;
			p.budget = 1.f; p.hier = 0; p.vary = 0.8f;
			break;
		default: break;
	}
	return p;
}

struct Snapshot {
	int mode = 0;           // 0 stopped, 1 clock, 2 bus
	int bar = -1;           // 0..31 inside the block, -1 = stopped
	int nextTurn = 32;      // bars of the next turn's line
	bool granted[tna::kEvents] = {};
	int action = 0;         // running tna::Action (0 = none)
	bool wall = false;
};

} // namespace

struct Turnaround : Module {
	enum ParamId { BUDGET_PARAM, ROLL_PARAM, WALL_PARAM, GAP_PARAM, DEPTH_PARAM, VARY_PARAM, PARAMS_LEN };
	enum InputId { CLK_INPUT, RST_INPUT, BUS_INPUT, IN_INPUT, INPUTS_LEN };
	enum OutputId { OUT_OUTPUT, TURN_OUTPUT, ROLL_OUTPUT, BUS_OUTPUT, OUTPUTS_LEN };
	enum LightId { ENUMS(BUDGET_LIGHT, kLeds), LIGHTS_LEN };

	tna::Engine engine;
	tna::Settings settings;

	// menu state (saved in the patch)
	Preset cfg;             // tier actions/windows/intensities + roll settings
	int preset = 0;
	int seed = 1;

	// block adapter
	float inBuf[2][kBlock]{}, outBuf[2][kBlock]{};
	int pos = 0;
	tna::Transport blockTp;

	// clock
	dsp::SchmittTrigger clkTrig, rstTrig;
	int step = -1;          // 1/16 count since reset, -1 = waiting for the first clock
	float sinceClk = 0.f, period = 0.f;
	bool clkSeen = false;   // a clock edge has been seen, so sinceClk is a real period

	// bus
	float prevBusBars = 0.f;
	int busCycle = 0;
	bool busInit = false;
	double busWinPpq = 0.0;
	float busWinTime = 0.f, busIdle = 1.f, busBpm = 0.f;

	// outputs
	std::int64_t lastRelBar = INT64_MIN;
	dsp::PulseGenerator turnGate, eventPulse;
	int eventLine = 32;
	bool rollOn = false;

	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;
	int mode = 0;

	Turnaround() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(BUDGET_PARAM, 0.f, 1.f, 0.5f, "Edit budget", " beats / 32 bars", 0.f, tna::kMaxBudgetBeats);
		configParam(ROLL_PARAM, -12.f, 24.f, 5.f, "Roll pitch", " st");
		configSwitch(WALL_PARAM, 0.f, 4.f, 3.f, "Wall at",
		             {"OFF", "4-bar turns up", "8-bar turns up", "16-bar turns up", "32-bar turn only"});
		configSwitch(GAP_PARAM, 0.f, 3.f, 1.f, "Wall length", {"1/2 beat", "1 beat", "2 beats", "1 bar"});
		configParam(DEPTH_PARAM, 0.f, 1.f, 1.f, "Edit depth", " %", 0.f, 100.f);
		configParam(VARY_PARAM, 0.f, 1.f, 0.25f, "Variation", " %", 0.f, 100.f);
		configInput(CLK_INPUT, "Clock (1/16 notes)");
		configInput(RST_INPUT, "Reset (next clock = bar 1)");
		configInput(BUS_INPUT, "FamilyBus (POS = bar, ENERGY scales depth)");
		configInput(IN_INPUT, "Break audio");
		configOutput(OUT_OUTPUT, "Audio");
		configOutput(TURN_OUTPUT, "Turn gate (one 1/16)");
		configOutput(ROLL_OUTPUT, "Roll gate");
		configOutput(BUS_OUTPUT, "FamilyBus (input + this module's EVENT)");
		for (int i = 0; i < kLeds; i++)
			configLight(BUDGET_LIGHT + i, "Edit budget spent");
		configBypass(IN_INPUT, OUT_OUTPUT);
		engine.prepare(44100.0, kBlock);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		engine.prepare(e.sampleRate, kBlock);
		pos = 0;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		cfg = Preset();
		preset = 0;
		seed = 1;
		step = -1;
		lastRelBar = INT64_MIN;
		engine.reset();
	}

	void applyPreset(int i) {
		preset = clamp(i, 0, (int) kPresetNames.size() - 1);
		cfg = makePreset(preset);
		params[BUDGET_PARAM].setValue(cfg.budget);
		params[ROLL_PARAM].setValue(cfg.pitch);
		params[WALL_PARAM].setValue((float) cfg.wall);
		params[GAP_PARAM].setValue((float) cfg.gap);
		params[VARY_PARAM].setValue(cfg.vary);
		params[DEPTH_PARAM].setValue(1.f);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "preset", json_integer(preset));
		json_object_set_new(root, "seed", json_integer(seed));
		char key[8];
		for (int t = 0; t < tna::kTiers; t++) {
			std::snprintf(key, sizeof key, "a%d", t);
			json_object_set_new(root, key, json_integer(cfg.act[t]));
			std::snprintf(key, sizeof key, "w%d", t);
			json_object_set_new(root, key, json_integer(cfg.win[t]));
			std::snprintf(key, sizeof key, "i%d", t);
			json_object_set_new(root, key, json_real(cfg.inten[t]));
		}
		json_object_set_new(root, "hier", json_integer(cfg.hier));
		json_object_set_new(root, "source", json_integer(cfg.source));
		json_object_set_new(root, "start", json_integer(cfg.start));
		json_object_set_new(root, "end", json_integer(cfg.end));
		json_object_set_new(root, "layer2", json_integer(cfg.layer2));
		return root;
	}

	void dataFromJson(json_t* root) override {
		auto geti = [&](const char* k, int& v, int lo, int hi) {
			if (json_t* j = json_object_get(root, k))
				v = clamp((int) json_integer_value(j), lo, hi);
		};
		geti("preset", preset, 0, (int) kPresetNames.size() - 1);
		geti("seed", seed, 1, 9999);
		char key[8];
		for (int t = 0; t < tna::kTiers; t++) {
			std::snprintf(key, sizeof key, "a%d", t);
			geti(key, cfg.act[t], 0, tna::kNumActions - 1);
			std::snprintf(key, sizeof key, "w%d", t);
			geti(key, cfg.win[t], 0, 7);
			std::snprintf(key, sizeof key, "i%d", t);
			if (json_t* j = json_object_get(root, key))
				cfg.inten[t] = clamp((float) json_number_value(j), 0.f, 1.f);
		}
		geti("hier", cfg.hier, 0, 5);
		geti("source", cfg.source, 0, 3);
		geti("start", cfg.start, 0, 3);
		geti("end", cfg.end, 0, 3);
		geti("layer2", cfg.layer2, 0, 2);
	}

	// ---- position -----------------------------------------------------------

	// Clock: 1/16 notes. Returns false while stopped.
	bool clockPosition(float dt, double& ppq, double& bpm) {
		sinceClk += dt;
		if (inputs[RST_INPUT].isConnected() && rstTrig.process(inputs[RST_INPUT].getVoltage(), 0.1f, 1.f)) {
			step = -1;
			lastRelBar = INT64_MIN;
		}
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
			// the clock stopped: wait for the next edge to measure a fresh period
			clkSeen = false;
			period = 0.f;
		}
		if (step < 0 || period <= 0.f)
			return false;
		bpm = 60.0 / (4.0 * period);
		ppq = (step + std::min(sinceClk / period, 0.999f)) * 0.25;
		return true;
	}

	// Bus: POS gives the bar inside EPOCH's 32-bar cycle, unwrapped across cycles.
	bool busPosition(float dt, double& ppq, double& bpm, bool clockOk, double clockBpm) {
		const float bars = bus::voltsToBars(clamp(inputs[BUS_INPUT].getVoltage(bus::kPos), 0.f, 10.f));
		if (!busInit) {
			busInit = true;
			prevBusBars = bars;
			busWinPpq = bars * tna::kBeatsPerBar;
		}
		if (bars < prevBusBars - 16.f)
			busCycle++;
		else if (bars > prevBusBars + 16.f)
			busCycle--;
		prevBusBars = bars;
		ppq = ((double) busCycle * bus::kBarsPerCycle + bars) * tna::kBeatsPerBar;

		// tempo from POS's slope, measured over 100 ms windows
		busWinTime += dt;
		if (busWinTime >= 0.1f) {
			const double d = ppq - busWinPpq;
			if (d > 1e-6 && d < 8.0) {
				const float inst = (float) (d / busWinTime * 60.0);
				busBpm = busBpm <= 0.f ? inst : busBpm + 0.3f * (inst - busBpm);
				busIdle = 0.f;
			}
			else {
				busIdle += busWinTime;
			}
			busWinPpq = ppq;
			busWinTime = 0.f;
		}
		bpm = clockOk ? clockBpm : busBpm;
		return busIdle < 0.5f && bpm > 1.0;
	}

	// ---- engine ---------------------------------------------------------------

	void updateSettings() {
		float depth = params[DEPTH_PARAM].getValue();
		if (mode == 2 && inputs[BUS_INPUT].getChannels() > bus::kEnergy)
			depth *= clamp(inputs[BUS_INPUT].getVoltage(bus::kEnergy) / 10.f, 0.f, 1.f);
		for (int t = 0; t < tna::kTiers; t++) {
			settings.tier[t].action = (tna::Action) cfg.act[t];
			settings.tier[t].windowBeats = kWindowBeats[cfg.win[t]];
			settings.tier[t].intensity = cfg.inten[t] * depth;
		}
		settings.budget = params[BUDGET_PARAM].getValue();
		settings.hierarchy = kHierarchy[cfg.hier];
		settings.variation = params[VARY_PARAM].getValue();
		settings.seed = (std::uint32_t) seed;
		const int wall = (int) std::round(params[WALL_PARAM].getValue());
		settings.gapBeats = wall > 0 ? kGapBeats[clamp((int) std::round(params[GAP_PARAM].getValue()), 0, 3)] : 0.0;
		settings.gapFromTier = std::max(0, wall - 1);
		settings.source = (tna::RollSource) cfg.source;
		settings.rollStart = kStartBeats[cfg.start];
		settings.rollEnd = std::min(settings.rollStart, kEndBeats[cfg.end]);
		settings.rollPitch = params[ROLL_PARAM].getValue();
		settings.layer2 = kLayer2[cfg.layer2];
		settings.phraseOffset = 0;
	}

	void runBlock() {
		updateSettings();
		engine.setSettings(settings);
		for (int i = 0; i < kBlock; i++) {
			outBuf[0][i] = inBuf[0][i];
			outBuf[1][i] = inBuf[1][i];
		}
		engine.process(outBuf[0], outBuf[1], kBlock, blockTp, nullptr);

		// TURN: the bar line of a phrase (every 4th bar) was crossed going forward
		if (blockTp.playing) {
			const std::int64_t relBar = (std::int64_t) std::floor(blockTp.ppq / tna::kBeatsPerBar);
			if (lastRelBar != INT64_MIN && relBar == lastRelBar + 1 && tna::floorMod(relBar, 4) == 0) {
				const float len = (float) (0.25 * 60.0 / std::max(40.0, blockTp.bpm));
				turnGate.trigger(std::max(0.005f, len));
				eventLine = tna::tierBars(tna::tierOfBoundary(relBar));
				eventPulse.trigger(bus::kPulseSec);
			}
			lastRelBar = relBar;
		}
		else {
			lastRelBar = INT64_MIN;
		}
		rollOn = blockTp.playing && engine.tele.action.load(std::memory_order_relaxed) == (int) tna::Action::Roll
		         && engine.tele.progress.load(std::memory_order_relaxed) >= 0.f;
	}

	void process(const ProcessArgs& args) override {
		double clkPpq = 0.0, clkBpm = 0.0, busPpq = 0.0, busBpmNow = 0.0;
		const bool clockOk = clockPosition(args.sampleTime, clkPpq, clkBpm);
		const bool busConn = inputs[BUS_INPUT].isConnected();
		bool busOk = false;
		if (busConn)
			busOk = busPosition(args.sampleTime, busPpq, busBpmNow, clockOk, clkBpm);
		else
			busInit = false;

		if (pos == 0) {
			if (busConn) {
				blockTp.playing = busOk;
				blockTp.ppq = busPpq;
				blockTp.bpm = busBpmNow;
				mode = busOk ? 2 : 0;
			}
			else {
				blockTp.playing = clockOk;
				blockTp.ppq = clkPpq;
				blockTp.bpm = clkBpm;
				mode = clockOk ? 1 : 0;
			}
		}

		const float in = inputs[IN_INPUT].getVoltage() / kVoltsPerUnit;
		inBuf[0][pos] = in;
		inBuf[1][pos] = in;
		outputs[OUT_OUTPUT].setVoltage(outBuf[0][pos] * kVoltsPerUnit);
		if (++pos >= kBlock) {
			pos = 0;
			runBlock();
		}

		outputs[TURN_OUTPUT].setVoltage(turnGate.process(args.sampleTime) ? 10.f : 0.f);
		outputs[ROLL_OUTPUT].setVoltage(rollOn ? 10.f : 0.f);

		// BUS out: the incoming bus (or our own POS) + our EVENT
		const bool ev = eventPulse.process(args.sampleTime);
		outputs[BUS_OUTPUT].setChannels(bus::kChannels);
		if (busConn) {
			for (int c = 0; c < bus::kEvent; c++)
				outputs[BUS_OUTPUT].setVoltage(inputs[BUS_INPUT].getVoltage(c), c);
		}
		else {
			const double bars = clockOk ? clkPpq / tna::kBeatsPerBar : 0.0;
			outputs[BUS_OUTPUT].setVoltage(bus::posToVolts((float) std::fmod(bars, (double) bus::kBarsPerCycle)), bus::kPos);
			outputs[BUS_OUTPUT].setVoltage(10.f, bus::kEnergy);  // full energy: never mutes a receiver
			outputs[BUS_OUTPUT].setVoltage(0.f, bus::kScene);
		}
		const float inEv = busConn ? inputs[BUS_INPUT].getVoltage(bus::kEvent) : 0.f;
		outputs[BUS_OUTPUT].setVoltage(std::max(inEv, ev ? bus::turnToVolts(eventLine) : 0.f), bus::kEvent);

		if (ui.tick(args.sampleTime)) {
			const tna::Plan& plan = engine.currentPlan();
			const float budget = plan.budgetBeats;
			const float spent = engine.tele.spent.load(std::memory_order_relaxed);
			const int lit = budget > 0.f && mode != 0 ? (int) std::round(kLeds * clamp(spent / budget, 0.f, 1.f)) : 0;
			for (int i = 0; i < kLeds; i++)
				lights[BUDGET_LIGHT + i].setBrightness(i < lit ? 1.f : 0.f);

			Snapshot& s = snap.writeSlot();
			s.mode = mode;
			if (mode != 0) {
				const float bib = engine.tele.barInBlock.load(std::memory_order_relaxed);
				s.bar = clamp((int) std::floor(bib), 0, tna::kBlockBars - 1);
				s.nextTurn = tna::tierBars(tna::tierOfEvent(clamp(engine.tele.event.load(std::memory_order_relaxed), 0, tna::kEvents - 1)));
				const bool running = engine.tele.progress.load(std::memory_order_relaxed) >= 0.f;
				s.action = running ? engine.tele.action.load(std::memory_order_relaxed) : 0;
				s.wall = engine.tele.inGap.load(std::memory_order_relaxed);
			}
			else {
				s.bar = -1;
				s.action = 0;
				s.wall = false;
			}
			for (int j = 0; j < tna::kEvents; j++)
				s.granted[j] = plan.alloc[j] > 0.01f;
			snap.publish();
		}
	}
};

// The 32-bar block as four phrase lines (4 / 8 / 16 / 32 bars), 16 cells per
// line, 2 bars per cell. A filled cell at a line end = that turn got budget;
// an outline = it asked but got none. The frame only changes when the cell,
// the bar number or the running action changes.
struct BarMapScreen : Screen {
	Turnaround* module = nullptr;
	Snapshot demo;

	BarMapScreen() {
		demo.mode = 1;
		demo.bar = 28;
		demo.nextTurn = 32;
		for (int j = 0; j < tna::kEvents; j++)
			demo.granted[j] = j != 0 && j != 4;
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		const float pad = 3.f, labelW = 14.f;
		const float textH = std::max(10.f, semi::platform::kMinText + 1.f);
		const float gx0 = pad + labelW, gx1 = w - pad;
		const float gy0 = pad, gy1 = h - pad - textH - 2.f;
		const float rowH = (gy1 - gy0) / tna::kTiers;
		const float cellW = (gx1 - gx0) / 16.f;
		const float gapX = 1.6f, gapY = 2.4f;
		const int cur = s.bar >= 0 ? s.bar / 2 : -1;
		static const char* labels[] = {"4", "8", "16", "32"};

		for (int r = 0; r < tna::kTiers; r++) {
			const float y = gy0 + r * rowH;
			const float ch = rowH - gapY;
			text(vg, gx0 - 3.f, y + ch * 0.5f, labels[r], 9.f, r == 3 ? semi::theme::magenta() : semi::theme::dim(),
			     NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
			const int line = tna::tierBars(r);
			for (int c = 0; c < 16; c++) {
				const float x = gx0 + c * cellW;
				const float cw = cellW - gapX;
				const int endBar = (c + 1) * 2;
				const bool lineEnd = endBar % line == 0;
				const bool owner = lineEnd && tna::tierOfBoundary(endBar) == r;
				const bool granted = lineEnd && s.granted[endBar / 4 - 1];
				if (c == cur) {
					fillRect(vg, x, y, cw, ch, semi::theme::amber());
				}
				else if (owner) {
					const NVGcolor col = r == 3 ? semi::theme::magenta() : semi::theme::cyan();
					if (granted)
						fillRect(vg, x, y, cw, ch, col);
					else
						outline(vg, x, y, cw, ch, col);
				}
				else if (lineEnd) {
					outline(vg, x, y, cw, ch, semi::theme::cyanDim());
				}
				else {
					fillRect(vg, x, y, cw, ch, c < cur ? semi::theme::cyanDim() : semi::theme::cell());
				}
			}
		}

		// "BAR 29 · TURN @32"   + status on the right
		const float ty = h - pad;
		const float fs = std::max(9.f, semi::platform::kMinText + 1.f);
		const float charW = fs * 0.54f;  // ShareTechMono is monospaced
		const int align = NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM;
		if (s.mode == 0 || s.bar < 0) {
			text(vg, pad, ty, module && module->inputs[Turnaround::BUS_INPUT].isConnected() ? "BUS IDLE" : "NO CLOCK", fs,
			     semi::theme::dim(), align);
			return;
		}
		char buf[16];
		std::snprintf(buf, sizeof buf, "BAR %d", s.bar + 1);
		text(vg, pad, ty, buf, fs, semi::theme::text(), align);
		float x = pad + charW * std::strlen(buf) + charW * 0.6f;
		fillRect(vg, x, ty - fs * 0.45f, 2.2f, 2.2f, semi::theme::dim());
		x += charW * 1.0f;
		std::snprintf(buf, sizeof buf, "TURN @%d", s.nextTurn);
		text(vg, x, ty, buf, fs, s.nextTurn == 32 ? semi::theme::magenta() : semi::theme::cyan(), align);

		const char* status = s.mode == 2 ? "BUS" : "CLK";
		NVGcolor sc = semi::theme::dim();
		if (s.wall) {
			status = "WALL";
			sc = semi::theme::magenta();
		}
		else if (s.action > 0 && s.action < tna::kNumActions) {
			static const char* shortNames[] = {"", "GHOST", "ROLL", "RETRG", "REV", "STOP", "CUT"};
			status = shortNames[s.action];
			sc = semi::theme::amber();
		}
		text(vg, w - pad, ty, status, fs, sc, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
	}

	static void outline(NVGcontext* vg, float x, float y, float w, float h, NVGcolor c) {
		const float sw = semi::platform::kThinStroke;
		nvgBeginPath(vg);
		nvgRect(vg, x + sw * 0.5f, y + sw * 0.5f, w - sw, h - sw);
		nvgStrokeColor(vg, c);
		nvgStrokeWidth(vg, sw);
		nvgStroke(vg);
	}
};

struct TurnaroundWidget : ModuleWidget {
	TurnaroundWidget(Turnaround* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Turnaround.svg")));

		auto* scr = semi::createScreenMm<BarMapScreen>(Vec(L::MAP.x, L::MAP.y), Vec(L::MAP.w, L::MAP.h));
		scr->module = module;
		addChild(scr);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		const L::P leds[kLeds] = {L::BUDGET_LED0, L::BUDGET_LED1, L::BUDGET_LED2, L::BUDGET_LED3,
		                          L::BUDGET_LED4, L::BUDGET_LED5, L::BUDGET_LED6, L::BUDGET_LED7};
		for (int i = 0; i < kLeds; i++)
			addChild(createLightCentered<semi::AmberLedSmall>(at(leds[i]), module, Turnaround::BUDGET_LIGHT + i));

		addParam(createParamCentered<semi::KnobM>(at(L::BUDGET), module, Turnaround::BUDGET_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::ROLL), module, Turnaround::ROLL_PARAM));
		auto* wall = createParamCentered<semi::KnobMHot>(at(L::WALL), module, Turnaround::WALL_PARAM);
		wall->snap = true;
		addParam(wall);
		auto* gap = createParamCentered<semi::KnobM>(at(L::GAP), module, Turnaround::GAP_PARAM);
		gap->snap = true;
		addParam(gap);
		addParam(createParamCentered<semi::KnobM>(at(L::DEPTH), module, Turnaround::DEPTH_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::VARY), module, Turnaround::VARY_PARAM));

		addInput(createInputCentered<semi::Jack>(at(L::CLK), module, Turnaround::CLK_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RST), module, Turnaround::RST_INPUT));
		addInput(createInputCentered<semi::BusJack>(at(L::BUS_IN), module, Turnaround::BUS_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::IN), module, Turnaround::IN_INPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT), module, Turnaround::OUT_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::TURN), module, Turnaround::TURN_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::ROLL_GATE), module, Turnaround::ROLL_OUTPUT));
		addOutput(createOutputCentered<semi::BusJack>(at(L::BUS_OUT), module, Turnaround::BUS_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Turnaround*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Preset", kPresetNames,
		                                      [=]() { return (size_t) m->preset; },
		                                      [=](size_t i) { m->applyPreset((int) i); }));
		menu->addChild(createMenuItem("New seed", "", [=]() { m->seed = (int) (random::u32() % 9999u) + 1; }));

		menu->addChild(createMenuLabel("Turn ladder"));
		static const char* tierLabels[] = {"4-bar turn", "8-bar turn", "16-bar turn", "32-bar turn"};
		for (int t = tna::kTiers - 1; t >= 0; t--) {
			menu->addChild(createIndexSubmenuItem(std::string(tierLabels[t]) + " action", kActionNames,
			                                      [=]() { return (size_t) m->cfg.act[t]; },
			                                      [=](size_t i) { m->cfg.act[t] = (int) i; }));
			menu->addChild(createIndexSubmenuItem(std::string(tierLabels[t]) + " window", kWindowNames,
			                                      [=]() { return (size_t) m->cfg.win[t]; },
			                                      [=](size_t i) { m->cfg.win[t] = (int) i; }));
		}
		menu->addChild(createIndexSubmenuItem("Hierarchy", {"0 (share evenly)", "0.25", "0.5", "0.7", "0.85", "1 (biggest turn first)"},
		                                      [=]() { return (size_t) m->cfg.hier; },
		                                      [=](size_t i) { m->cfg.hier = (int) i; }));

		menu->addChild(createMenuLabel("Rollcall"));
		menu->addChild(createIndexSubmenuItem("Roll source", {"Snare", "Kick", "Loudest", "Vary"},
		                                      [=]() { return (size_t) m->cfg.source; },
		                                      [=](size_t i) { m->cfg.source = (int) i; }));
		menu->addChild(createIndexSubmenuItem("Roll from", {"1/4", "1/8", "1/16", "1/32"},
		                                      [=]() { return (size_t) m->cfg.start; },
		                                      [=](size_t i) { m->cfg.start = (int) i; }));
		menu->addChild(createIndexSubmenuItem("Roll to", {"1/8", "1/16", "1/32", "1/64"},
		                                      [=]() { return (size_t) m->cfg.end; },
		                                      [=](size_t i) { m->cfg.end = (int) i; }));
		menu->addChild(createIndexSubmenuItem("Layer 2", {"Off", "50%", "100%"},
		                                      [=]() { return (size_t) m->cfg.layer2; },
		                                      [=](size_t i) { m->cfg.layer2 = (int) i; }));
	}
};

Model* modelTurnaround = createModel<Turnaround, TurnaroundWidget>("Turnaround");
