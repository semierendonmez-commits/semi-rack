// BREAKDNA — Rack / MetaModule port of BREAKDNA (engine: engines/breakdna, vendored from
// semierendonmez-commits/breakdna Source/engine; namespaces wrapped in dnafam::, see
// engines/breakdna/sources.mk).
//
// Two breaks. A is the DNA: where every hit lands inside its 16th, how loud, which are
// ghosts. B is the skin: its slices answer A's hits and MORPH blends their spectra into A's
// (five pre-rendered layers: A, 25 / 50 / 75 % spectral blends, B). The result keeps A's feel.
//
// Panel:   FEEL, DYN, GHOSTS, MORPH, LANE (how much the per-step morph lane adds);
//          ROTATE, DECAY, TUNE, LEVEL; SOURCE (BREAK A: A's own hits on the clock / GATES:
//          your K / S / H gates get A's micro-timing, accents and ghost notes).
// Jacks:   CLK (1/16 by default, the menu has others) + RST, K / S / H gate inputs (S: the
//          gate's height is the velocity, below ~3 V it is a ghost), MORPH CV (10 %/V), FEEL CV
//          (15 %/V); out L / R (+-5 V), K / S / H triggers (5 ms) and ACC (the last hit's
//          velocity, 0-10 V).
// Menu:    load A / B (WAV, read and analysed off the audio thread), swap, bars A / B, match,
//          B pitch, envelope, seed, the morph lane's shape, choke, clock PPQN, free tempo.
// No CLK patched: the break runs free at its own tempo (menu). In GATES mode the notes are
// played 25 ms late (the engine's lookahead: A's early hits need it; Rack has no delay
// compensation). Rack audio is +-5 V; the engine works in +-1.

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/BreakdnaLayout.hpp"
#include "DnaFamily.hpp"
#include "breakdna/Engine.h"

namespace L = semi::layout::breakdna;
using semi::Screen;
using namespace dnamod;

namespace {

constexpr int kBlock = 32;
constexpr float kVoltsPerUnit = 5.f;
constexpr int kNoteK = 36, kNoteS = 38, kNoteH = 42;

// What the loader thread builds: the kit (five morph layers) and its pictures.
struct Built {
	std::unique_ptr<dna::Kit> kit;
	WaveView wa, wb;
	GenomeView gv;
	bool hasB = false;
};

struct Snapshot {
	WaveView wa, wb;
	GenomeView gv;
	bool hasB = false;
	int8_t lane[dna::kMaxSteps]{};
	int step = -1, litA = -1, litB = -1;
	float morph = 0.f, bpm = 120.f;
	int source = 0, rotate = 0;
	bool clocked = false, busy = false, errA = false, errB = false;
};

const char* const kBarNames[] = {"Auto", "1 bar", "2 bars", "4 bars"};
const int kBarValues[] = {0, 1, 2, 4};

} // namespace

struct Breakdna : Module {
	enum ParamId { FEEL_PARAM, DYN_PARAM, GHOSTS_PARAM, MORPH_PARAM, LANE_PARAM, ROTATE_PARAM, DECAY_PARAM, TUNE_PARAM,
		           LEVEL_PARAM, SOURCE_PARAM, PARAMS_LEN };
	enum InputId { CLK_INPUT, RST_INPUT, K_INPUT, S_INPUT, H_INPUT, MORPH_INPUT, FEEL_INPUT, INPUTS_LEN };
	enum OutputId { L_OUTPUT, R_OUTPUT, K_OUTPUT, S_OUTPUT, H_OUTPUT, ACC_OUTPUT, OUTPUTS_LEN };
	enum LightId { SRC_BREAK_LIGHT, SRC_GATES_LIGHT, LIGHTS_LEN };

	dna::Engine engine;
	std::array<std::atomic<float>, dna::kMaxSteps> lane{};

	// hidden settings (json)
	int source = dna::SourceBreak;
	int barsA = 0, barsB = 0, match = dna::MatchClass, bpitch = 0, seed = 1;
	float env = 0.5f;
	bool choke = true;
	int ppqnIdx = 0, tempoIdx = 0;

	// files: the loader thread syncs to these
	Paths<2> paths;
	std::shared_ptr<const dna::Sample> cached[2];   // loader thread only
	std::string cachedPath[2];
	std::atomic<bool> err[2]{};
	std::atomic<bool> busy{false};

	// audio block
	float outBuf[2][kBlock]{};
	int pos = 0;
	double preparedSr = 0.0;
	dna::MidiEvent midiIn[64];
	int numIn = 0;
	dna::MidiEvent midiOut[dna::Engine::kMaxOut];
	OutEvents<96> outEv, playEv;
	dsp::SchmittTrigger gateTrig[3];
	bool gateHigh[3]{};
	float trigLeft[3]{};
	float acc = 0.f;
	dsp::BooleanTrigger sourceTrig;

	// transport
	ClockPos clock;
	double blockPpq = 0.0, blockBpm = 120.0;
	bool blockClocked = false, wasClocked = false, resetFree = false;

	// display
	int litA = -1, litB = -1;
	float litLeft = 0.f;
	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;

	semi::Slot<Built> kitSlot;
	semi::Loader<Built> loader;   // after kitSlot: destroyed (and its thread stopped) first

	~Breakdna() override {
		// background jobs first (and wait for a run in flight): they write into this module
		loader.stopAndWait();
	}

	Breakdna() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(FEEL_PARAM, 0.f, 150.f, 100.f, "Feel", " %")->description = "0: on the grid, 100: A's micro-timing, 150: more";
		configParam(DYN_PARAM, 0.f, 150.f, 100.f, "Dynamics", " %")->description = "0: flat, 100: A's accents";
		configParam(GHOSTS_PARAM, 0.f, 200.f, 100.f, "Ghosts", " %")->description = "A's ghost notes: none .. louder (GATES: how many are added)";
		configParam(MORPH_PARAM, 0.f, 100.f, 35.f, "Morph A > B", " %")->description = "Spectral blend from A's slice to the B slice that answers it";
		configParam(LANE_PARAM, 0.f, 100.f, 100.f, "Lane", " %")->description = "How much the per-step morph lane (menu) adds to MORPH";
		configParam(ROTATE_PARAM, 0.f, 31.f, 0.f, "Rotate", " steps");
		paramQuantities[ROTATE_PARAM]->snapEnabled = true;
		configParam(DECAY_PARAM, 0.f, 100.f, 100.f, "Decay", " %");
		configParam(TUNE_PARAM, -12.f, 12.f, 0.f, "Tune", " st");
		configParam(LEVEL_PARAM, -24.f, 6.f, 0.f, "Level", " dB");
		configButton(SOURCE_PARAM, "Source (BREAK A / GATES)");
		configInput(CLK_INPUT, "Clock")->description = "1/16 pulses by default (menu: 1/8, 1/4, 24 PPQN). Unpatched: runs free";
		configInput(RST_INPUT, "Reset")->description = "The next clock pulse is the loop's first 16th";
		configInput(K_INPUT, "Kick gate (GATES mode)");
		configInput(S_INPUT, "Snare gate (GATES mode; height = velocity, below ~3 V a ghost)");
		configInput(H_INPUT, "Hat gate (GATES mode)");
		configInput(MORPH_INPUT, "Morph CV (10 %/V)");
		configInput(FEEL_INPUT, "Feel CV (15 %/V)");
		configOutput(L_OUTPUT, "Left");
		configOutput(R_OUTPUT, "Right");
		configOutput(K_OUTPUT, "Kick trigger");
		configOutput(S_OUTPUT, "Snare / ghost trigger");
		configOutput(H_OUTPUT, "Hat trigger");
		configOutput(ACC_OUTPUT, "Accent (the last hit's velocity, 0-10 V)");
		configLight(SRC_BREAK_LIGHT, "Source: break A");
		configLight(SRC_GATES_LIGHT, "Source: gates");
		for (auto& l : lane)
			l.store(0.f);
		engine.prepare(48000.0, kBlock);
		preparedSr = 48000.0;
		loader.attach(this, &kitSlot, [this](const std::string&, Built& b) { return build(b); });
	}

	// ---- the loader thread ---------------------------------------------------------------

	bool build(Built& out) {
		busy = true;
		for (int i = 0; i < 2; i++) {
			const std::string want = paths.get(i);
			if (want != cachedPath[i] || err[i]) {
				cachedPath[i] = want;
				cached[i].reset();
				err[i] = false;
				if (!want.empty() && !loadBreak(want, cached[i]))
					err[i] = true;
			}
		}
		if (!cached[0]) {
			busy = false;
			return false;
		}
		dna::KitParams kp;
		kp.match = match;
		kp.bPitch = (float) bpitch;
		kp.env = env;
		kp.seed = (uint32_t) seed;
		kp.barsA = kBarValues[clamp(barsA, 0, 3)];
		kp.barsB = kBarValues[clamp(barsB, 0, 3)];
		const auto& b = cached[1] ? cached[1] : cached[0];
		out.kit = dna::buildKit(cached[0], b, kp);
		out.hasB = cached[1] != nullptr;
		const dna::Kit& k = *out.kit;
		makeWave(*k.a, k.sa.starts, k.sa.cls, out.wa);
		out.wa.bars = k.g.bars;
		out.wa.bpm = (float) k.g.bpm;
		makeWave(*k.b, k.sb.starts, k.sb.cls, out.wb);
		for (int i = 0; i < out.wb.nMarks; i++)
			out.wb.lit[i] = 0;
		for (int j : k.match)
			if (j >= 0 && j < out.wb.nMarks)
				out.wb.lit[j] = 1;
		makeGenome(k.g, out.gv);
		busy = false;
		return true;
	}

	void requestBuild() { loader.request("sync"); }

	void setPath(int i, const std::string& p) {
		paths.set(i, p);
		requestBuild();
	}

	// ---- json -------------------------------------------------------------------------------

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "pathA", json_string(paths.get(0).c_str()));
		json_object_set_new(root, "pathB", json_string(paths.get(1).c_str()));
		json_object_set_new(root, "source", json_integer(source));
		json_object_set_new(root, "barsA", json_integer(barsA));
		json_object_set_new(root, "barsB", json_integer(barsB));
		json_object_set_new(root, "match", json_integer(match));
		json_object_set_new(root, "bpitch", json_integer(bpitch));
		json_object_set_new(root, "seed", json_integer(seed));
		json_object_set_new(root, "env", json_real(env));
		json_object_set_new(root, "choke", json_boolean(choke));
		json_object_set_new(root, "ppqn", json_integer(ppqnIdx));
		json_object_set_new(root, "tempo", json_integer(tempoIdx));
		json_t* a = json_array();
		for (auto& l : lane)
			json_array_append_new(a, json_real(l.load()));
		json_object_set_new(root, "lane", a);
		return root;
	}

	void dataFromJson(json_t* root) override {
		jsonInt(root, "source", source, 0, 1);
		jsonInt(root, "barsA", barsA, 0, 3);
		jsonInt(root, "barsB", barsB, 0, 3);
		jsonInt(root, "match", match, 0, dna::NumMatch - 1);
		jsonInt(root, "bpitch", bpitch, -12, 12);
		jsonInt(root, "seed", seed, 1, 9999);
		jsonFloat(root, "env", env, 0.f, 1.f);
		jsonBool(root, "choke", choke);
		jsonInt(root, "ppqn", ppqnIdx, 0, kNumPpqn - 1);
		jsonInt(root, "tempo", tempoIdx, 0, kNumTempos - 1);
		if (json_t* a = json_object_get(root, "lane"))
			for (int s = 0; s < dna::kMaxSteps; s++) {
				json_t* v = json_array_get(a, s);
				lane[s].store(v ? clamp((float) json_number_value(v), -1.f, 1.f) : 0.f);
			}
		paths.set(0, jsonStr(root, "pathA"));
		paths.set(1, jsonStr(root, "pathB"));
		requestBuild();
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		source = dna::SourceBreak;
		barsA = barsB = 0;
		match = dna::MatchClass;
		bpitch = 0;
		seed = 1;
		env = 0.5f;
		choke = true;
		for (auto& l : lane)
			l.store(0.f);
		requestBuild();
	}

	// the plugin's lane shapes (PluginProcessor::shapeLane)
	void shapeLane(int shape) {
		for (int s = 0; s < dna::kMaxSteps; s++) {
			float v = 0.f;
			const int p = s % 16;
			switch (shape) {
				case 0: v = 0.f; break;
				case 1: v = -0.5f + (float) p / 15.f; break;
				case 2: v = (p % 4 == 0) ? 0.6f : -0.2f; break;
				case 3: v = (p % 4 == 2) ? 0.6f : ((p % 2) ? 0.25f : -0.3f); break;
				default: v = random::uniform() * 1.2f - 0.6f; break;
			}
			lane[s].store(v);
		}
	}

	// ---- audio ------------------------------------------------------------------------------

	void runBlock(float sampleRate) {
		kitSlot.poll();
		const Built* built = kitSlot.active;
		const dna::Kit* kit = built ? built->kit.get() : nullptr;

		dna::Params p;
		p.source = source;
		p.feel = clamp((params[FEEL_PARAM].getValue() + inputs[FEEL_INPUT].getVoltage() * 15.f) * 0.01f, 0.f, 1.5f);
		p.dyn = params[DYN_PARAM].getValue() * 0.01f;
		p.ghosts = params[GHOSTS_PARAM].getValue() * 0.01f;
		p.morph = clamp((params[MORPH_PARAM].getValue() + inputs[MORPH_INPUT].getVoltage() * 10.f) * 0.01f, 0.f, 1.f);
		p.laneAmt = params[LANE_PARAM].getValue() * 0.01f;
		p.rotate = (int) std::round(params[ROTATE_PARAM].getValue());
		p.decay = params[DECAY_PARAM].getValue() * 0.01f;
		p.tune = params[TUNE_PARAM].getValue();
		p.gain = std::pow(10.f, params[LEVEL_PARAM].getValue() / 20.f);
		p.choke = choke;
		p.noteK = kNoteK;
		p.noteS = kNoteS;
		p.noteH = kNoteH;
		p.midiOut = true;
		p.seed = (uint32_t) seed;
		if (blockClocked) {
			p.run = dna::RunHost;
			p.playing = true;
			p.bpm = blockBpm;
			// the audio leaves one block later; BREAK A renders the engine's lookahead behind
			double ahead = kBlock / (double) sampleRate;
			if (source == dna::SourceBreak)
				ahead += engine.latency() / (double) sampleRate;
			// the first block after the clock starts is the loop's start (nothing of it is skipped)
			p.ppq = (wasClocked ? blockPpq + ahead : ahead - kBlock / (double) sampleRate) * blockBpm / 60.0;
		}
		else {
			p.playing = false;
			p.run = inputs[CLK_INPUT].isConnected() ? dna::RunHost : dna::RunAlways;
			p.bpm = tempoOf(tempoIdx, kit ? kit->g.bpm : 0.0);
			if (resetFree) {
				engine.reset();
				resetFree = false;
			}
		}

		wasClocked = blockClocked;
		int numOut = 0;
		engine.process(kit, p, lane, midiIn, numIn, outBuf[0], outBuf[1], kBlock, midiOut, numOut);
		numIn = 0;

		outEv.clear();
		for (int i = 0; i < numOut; i++) {
			const auto& m = midiOut[i];
			const int o = m.note == kNoteK ? 0 : m.note == kNoteH ? 2 : 1;
			outEv.add(m.offset, o, m.vel > 0, m.vel / 127.f);
		}
	}

	void process(const ProcessArgs& args) override {
		if (args.sampleRate != preparedSr) {
			engine.prepare(args.sampleRate, kBlock);
			preparedSr = args.sampleRate;
			pos = 0;
		}
		if (sourceTrig.process(params[SOURCE_PARAM].getValue() > 0.f))
			source = 1 - source;

		double ppq = 0.0, bpm = 120.0;
		const bool clocked = clock.process(args.sampleTime, inputs[CLK_INPUT].isConnected(), inputs[CLK_INPUT].getVoltage(),
		                                   inputs[RST_INPUT].getVoltage(), ppqnOf(ppqnIdx), ppq, bpm);
		if (clock.resetFired && !inputs[CLK_INPUT].isConnected())
			resetFree = true;
		if (pos == 0) {
			blockPpq = ppq;
			blockBpm = bpm;
			blockClocked = clocked;
		}

		// gates in -> note events of this block (GATES mode)
		static const uint8_t notes[3] = {kNoteK, kNoteS, kNoteH};
		for (int g = 0; g < 3; g++) {
			Input& in = inputs[K_INPUT + g];
			const float v = in.getVoltage();
			if (gateTrig[g].process(v, 0.1f, 1.f)) {
				gateHigh[g] = true;
				if (numIn < 64)
					midiIn[numIn++] = {pos, notes[g], (uint8_t) clamp((int) std::lround(v / 10.f * 127.f), 1, 127)};
			}
			else if (gateHigh[g] && !gateTrig[g].isHigh()) {
				gateHigh[g] = false;
				if (numIn < 64)
					midiIn[numIn++] = {pos, notes[g], 0};
			}
		}

		// the previous block's audio and triggers
		outputs[L_OUTPUT].setVoltage(outBuf[0][pos] * kVoltsPerUnit);
		outputs[R_OUTPUT].setVoltage(outBuf[1][pos] * kVoltsPerUnit);
		for (int i = 0; i < playEv.n; i++)
			if (playEv.ev[i].at == pos && playEv.ev[i].on) {
				trigLeft[playEv.ev[i].out] = 0.005f;
				acc = playEv.ev[i].v * 10.f;
			}
		for (int o = 0; o < 3; o++) {
			outputs[K_OUTPUT + o].setVoltage(trigLeft[o] > 0.f ? 10.f : 0.f);
			trigLeft[o] -= args.sampleTime;
		}
		outputs[ACC_OUTPUT].setVoltage(acc);

		if (++pos >= kBlock) {
			pos = 0;
			runBlock(args.sampleRate);
			playEv = outEv;
		}

		if (ui.tick(args.sampleTime))
			publish(clocked);
	}

	void publish(bool clocked) {
		lights[SRC_BREAK_LIGHT].setBrightness(source == dna::SourceBreak ? 1.f : 0.f);
		lights[SRC_GATES_LIGHT].setBrightness(source == dna::SourceMidi ? 1.f : 0.f);
		dna::HitEvent h;
		while (engine.popHit(h)) {
			litA = h.slice;
			litB = h.bslice;
			litLeft = 0.15f;
		}
		litLeft -= 1.f / semi::platform::kUiRateHz;
		if (litLeft <= 0.f)
			litA = litB = -1;

		Snapshot& s = snap.writeSlot();
		const Built* b = kitSlot.active;
		if (b) {
			s.wa = b->wa;
			s.wb = b->wb;
			s.gv = b->gv;
			s.hasB = b->hasB;
		}
		else {
			s.wa = WaveView{};
			s.wb = WaveView{};
			s.gv = GenomeView{};
			s.hasB = false;
		}
		for (int i = 0; i < dna::kMaxSteps; i++)
			s.lane[i] = (int8_t) clamp((int) std::lround(lane[i].load(std::memory_order_relaxed) * 100.f), -100, 100);
		s.step = engine.viewStep.load(std::memory_order_relaxed);
		s.litA = litA;
		s.litB = litB;
		s.morph = clamp((params[MORPH_PARAM].getValue() + inputs[MORPH_INPUT].getVoltage() * 10.f) * 0.01f, 0.f, 1.f);
		s.bpm = (float) engine.viewBpm.load(std::memory_order_relaxed);
		s.source = source;
		s.rotate = (int) std::round(params[ROTATE_PARAM].getValue());
		s.clocked = clocked;
		s.busy = busy.load();
		s.errA = err[0].load();
		s.errB = err[1].load();
		snap.publish();
	}
};

// ---------------------------------------------------------------------------------- screens

namespace {

void demoWave(WaveView& w, int marks, uint32_t seed) {
	w = WaveView{};
	w.valid = true;
	uint32_t r = seed;
	for (int c = 0; c < kWaveCols; c++) {
		r = r * 1664525u + 1013904223u;
		const float env = std::exp(-(float) (c % 8) * 0.45f);
		w.peak[c] = (uint8_t) (40 + 200 * env * (0.6f + 0.4f * ((r >> 24) / 255.f)));
	}
	w.nMarks = marks;
	static const uint8_t cls[] = {0, 3, 1, 3, 2, 0, 1, 3, 0, 3, 1, 2, 0, 3, 1, 3};
	for (int i = 0; i < marks; i++) {
		w.mark[i] = (uint16_t) (i * 65535 / marks);
		w.cls[i] = cls[i % 16];
		w.lit[i] = 1;
	}
	w.bars = 2;
	w.bpm = 168.f;
}

} // namespace

// dna.a over skin.b: A's waveform, slices by class (K magenta, S cyan, G dim, H amber), the
// slice that plays framed; B under it, the slices that answer A bright. Changes only on hits.
struct DnaScreen : Screen {
	Breakdna* module = nullptr;
	Snapshot demo;

	DnaScreen() {
		demoWave(demo.wa, 16, 7);
		std::snprintf(demo.wa.name, sizeof demo.wa.name, "amen.wav");
		demoWave(demo.wb, 12, 99);
		std::snprintf(demo.wb.name, sizeof demo.wb.name, "think.wav");
		demo.hasB = true;
		demo.litA = 5;
		demo.litB = 3;
		demo.morph = 0.35f;
		demo.bpm = 168.f;
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y, pad = 3.f;
		char buf[48];
		std::snprintf(buf, sizeof buf, "A %s", s.errA ? "LOAD FAILED" : s.wa.valid ? s.wa.name : "load a break (menu)");
		text(vg, pad, 2.f, buf, 9.f, s.errA ? semi::theme::magenta() : semi::theme::cyan());
		if (s.busy)
			text(vg, w - pad, 2.f, "ANALYSING", 9.f, semi::theme::amber(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		else if (s.wa.valid) {
			std::snprintf(buf, sizeof buf, "%.0f BPM %dB", s.wa.bpm, s.wa.bars);
			text(vg, w - pad, 2.f, buf, 9.f, semi::theme::text(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		}
		const float ay = 13.f, ah = h * 0.48f;
		drawWave(*this, vg, s.wa, pad, ay, w - 2 * pad, ah, s.litA, false);

		const float by = ay + ah + 3.f, bh = h - by - pad;
		line(vg, pad, by - 1.5f, w - pad, by - 1.5f, semi::theme::grid());
		if (s.hasB || !module) {
			drawWave(*this, vg, s.wb, pad + 14.f, by, w - 2 * pad - 14.f, bh, s.litB, true);
			text(vg, pad, by + bh * 0.5f, "B", 9.f, semi::theme::magenta(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		}
		else
			text(vg, w * 0.5f, by + bh * 0.5f, s.errB ? "B: LOAD FAILED" : "B = A  (load a skin: menu)", 9.f,
			     s.errB ? semi::theme::magenta() : semi::theme::dim(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		std::snprintf(buf, sizeof buf, "%d%%", (int) std::lround(s.morph * 100.f));
		text(vg, w - pad, by + 1.f, buf, 9.f, semi::theme::magenta(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
	}
};

// The genome: one column per 16th. K S G H blocks heated by level, TIME (the main hit's
// micro-timing, late up / early down), LANE (the morph lane), the playhead framed.
struct GenomeScreen : Screen {
	Breakdna* module = nullptr;
	Snapshot demo;

	GenomeScreen() {
		demo.gv.steps = 32;
		static const char* pat[4] = {"x.........x.....x.........x.....", "....x.......x.......x.......x...",
		                             "..x....x.x..x.x...x..x...x...x.x", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x."};
		for (int r = 0; r < 4; r++)
			for (int st = 0; st < 32; st++)
				if (pat[r][st] == 'x')
					demo.gv.level[r][st] = (uint8_t) (r == 2 ? 70 : 140 + (st * 37) % 110);
		for (int st = 0; st < 32; st++) {
			demo.gv.offset[st] = (int8_t) ((st % 2) ? 18 : -4);
			demo.lane[st] = (int8_t) ((st % 4 == 0) ? 60 : -20);
		}
		demo.step = 9;
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y, pad = 3.f, lab = 9.f;
		const int n = s.gv.steps;
		static const char* rows[] = {"K", "S", "G", "H"};
		const float rowH = (h - 2 * pad) / 7.f;
		for (int r = 0; r < 4; r++)
			text(vg, pad, pad + rowH * (r + 0.5f), rows[r], 8.f, classColor(r), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		text(vg, pad, pad + rowH * 4.9f, "T", 8.f, semi::theme::dim(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		text(vg, pad, pad + rowH * 6.3f, "L", 8.f, semi::theme::magenta(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		if (n <= 0) {
			text(vg, w * 0.5f, h * 0.5f, "NO GENOME", 9.f, semi::theme::dim(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			return;
		}
		const float x0 = pad + lab, cw = (w - x0 - pad) / n;
		const int play = s.step >= 0 ? ((s.step + s.rotate) % n + n) % n : -1;
		for (int st = 0; st < n; st++) {
			const float x = x0 + st * cw;
			if (st % 4 == 0)
				fillRect(vg, x, pad, 1.f, h - 2 * pad, semi::theme::grid());
			for (int r = 0; r < 4; r++) {
				const int l = s.gv.level[r][st];
				if (!l)
					continue;
				const NVGcolor c = l > 150 ? classColor(r) : l > 70 ? (r == 0 ? semi::theme::magentaDeep() : semi::theme::cyanDim())
				                                                    : semi::theme::cyanDeep();
				fillRect(vg, x + 0.6f, pad + rowH * r + 0.8f, std::max(1.f, cw - 1.2f), rowH - 1.6f, c);
			}
			// TIME: offset of the main hit, +-1 step = +-rowH
			const float ty = pad + rowH * 4.9f;
			const float o = s.gv.offset[st] / 100.f * rowH * 0.9f;
			if (std::fabs(o) > 0.3f)
				fillRect(vg, x + cw * 0.3f, o > 0 ? ty - o : ty, std::max(1.f, cw * 0.4f), std::fabs(o),
				         o > 0 ? semi::theme::amber() : semi::theme::cyan());
			// LANE
			const float ly = pad + rowH * 6.3f;
			const float lv = s.lane[st] / 100.f * rowH * 0.7f;
			if (std::fabs(lv) > 0.3f)
				fillRect(vg, x + cw * 0.2f, lv > 0 ? ly - lv : ly, std::max(1.f, cw * 0.6f), std::fabs(lv), semi::theme::magenta());
		}
		line(vg, x0, pad + rowH * 4.9f, w - pad, pad + rowH * 4.9f, semi::theme::grid());
		if (play >= 0) {
			nvgBeginPath(vg);
			nvgRect(vg, x0 + play * cw, pad, std::max(2.f, cw), rowH * 4.f);
			nvgStrokeColor(vg, semi::theme::text());
			nvgStrokeWidth(vg, semi::platform::kThinStroke);
			nvgStroke(vg);
		}
	}
};

struct BreakdnaWidget : ModuleWidget {
	BreakdnaWidget(Breakdna* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Breakdna.svg")));

		auto* dnaScr = semi::createScreenMm<DnaScreen>(Vec(L::DNA.x, L::DNA.y), Vec(L::DNA.w, L::DNA.h));
		dnaScr->module = module;
		addChild(dnaScr);
		auto* gen = semi::createScreenMm<GenomeScreen>(Vec(L::GENOME.x, L::GENOME.y), Vec(L::GENOME.w, L::GENOME.h));
		gen->module = module;
		addChild(gen);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		addParam(createParamCentered<semi::KnobM>(at(L::FEEL), module, Breakdna::FEEL_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::DYN), module, Breakdna::DYN_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::GHOSTS), module, Breakdna::GHOSTS_PARAM));
		addParam(createParamCentered<semi::KnobMHot>(at(L::MORPH), module, Breakdna::MORPH_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::LANE), module, Breakdna::LANE_PARAM));
		auto* rot = createParamCentered<semi::KnobS>(at(L::ROTATE), module, Breakdna::ROTATE_PARAM);
		rot->snap = true;
		addParam(rot);
		addParam(createParamCentered<semi::KnobS>(at(L::DECAY), module, Breakdna::DECAY_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::TUNE), module, Breakdna::TUNE_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::LEVEL), module, Breakdna::LEVEL_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::SOURCE), module, Breakdna::SOURCE_PARAM));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::SRC_BREAK), module, Breakdna::SRC_BREAK_LIGHT));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::SRC_GATES), module, Breakdna::SRC_GATES_LIGHT));

		addInput(createInputCentered<semi::Jack>(at(L::CLK), module, Breakdna::CLK_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RST), module, Breakdna::RST_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::K_IN), module, Breakdna::K_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::S_IN), module, Breakdna::S_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::H_IN), module, Breakdna::H_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::MORPH_CV), module, Breakdna::MORPH_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::FEEL_CV), module, Breakdna::FEEL_INPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_L), module, Breakdna::L_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_R), module, Breakdna::R_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::K_OUT), module, Breakdna::K_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::S_OUT), module, Breakdna::S_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::H_OUT), module, Breakdna::H_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::ACC_OUT), module, Breakdna::ACC_OUTPUT));
	}

#ifndef METAMODULE
	void step() override {
		if (auto* m = dynamic_cast<Breakdna*>(module))
			m->kitSlot.collect();
		ModuleWidget::step();
	}
#endif

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Breakdna*>(module);
		if (!m)
			return;
		auto rebuild = [=]() { m->requestBuild(); };
		menu->addChild(new MenuSeparator);
		auto load = [=](int slot) {
			const std::string cur = m->paths.get(slot).empty() ? m->paths.get(0) : m->paths.get(slot);
			semi::openFileDialog("WAV:wav,WAV", semi::dirName(cur), [=](const std::string& p) { m->setPath(slot, p); });
		};
		const std::string a = m->paths.get(0), b = m->paths.get(1);
		menu->addChild(createMenuItem("Load break A (the DNA)...", a.empty() ? "" : semi::fileName(a), [=]() { load(0); }));
		menu->addChild(createMenuItem("Load break B (the skin)...", b.empty() ? "= A" : semi::fileName(b), [=]() { load(1); }));
		menu->addChild(createMenuItem("Swap A <> B", "", [=]() {
			const std::string pa = m->paths.get(0), pb = m->paths.get(1);
			if (pb.empty())
				return;
			m->paths.set(0, pb);
			m->paths.set(1, pa);
			m->requestBuild();
		}));
		menu->addChild(createMenuItem("B = A (clear B)", "", [=]() { m->setPath(1, ""); }));

		menu->addChild(createMenuLabel("Kit (rebuilt off the audio thread)"));
		std::vector<std::string> bars(kBarNames, kBarNames + 4);
		menu->addChild(intMenu("Bars A", bars, &m->barsA, rebuild));
		menu->addChild(intMenu("Bars B", bars, &m->barsB, rebuild));
		menu->addChild(intMenu("Match", {"Class (nearest sound)", "Position", "Random (seed)"}, &m->match, rebuild));
		menu->addChild(createIndexSubmenuItem("B pitch", stepLabels(-12.f, 12.f, 25, 1.f, " st"),
			[=]() { return (size_t) (m->bpitch + 12); }, [=](size_t i) { m->bpitch = (int) i - 12; m->requestBuild(); }));
		menu->addChild(floatMenu("Envelope (A's attack in the blends)", &m->env, 0.f, 1.f, 11, 100.f, "%", rebuild));
		menu->addChild(createMenuItem("New seed", string::f("%d", m->seed), [=]() {
			m->seed = 1 + (int) (random::u32() % 9999u);
			m->requestBuild();
		}));

		menu->addChild(createSubmenuItem("Morph lane", "", [=](Menu* sub) {
			static const char* names[] = {"Clear", "Ramp over each bar", "The beats go B", "The offbeats go B", "Random"};
			for (int i = 0; i < 5; i++)
				sub->addChild(createMenuItem(names[i], "", [=]() { m->shapeLane(i); }));
		}));
		menu->addChild(createBoolPtrMenuItem("Choke (a hit cuts the last)", "", &m->choke));
		menu->addChild(intMenu("Clock input", ppqnNames(), &m->ppqnIdx));
		menu->addChild(intMenu("Tempo without CLK", tempoNames(), &m->tempoIdx));
	}
};

Model* modelBreakdna = createModel<Breakdna, BreakdnaWidget>("Breakdna");
