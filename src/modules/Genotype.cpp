// GENOTYPE — Rack / MetaModule port of GENOTYPE (engine: engines/genotype on top of
// engines/breakdna, vendored from semierendonmez-commits/genotype Source/engine; namespaces
// wrapped in dnafam::, the Pinball copy is dnafam::walk, see engines/genotype/sources.mk).
//
// "The walk a break wants, learned from its DNA." Break A's genome (every hit's 16th, its
// micro-timing, accent, ghost) teaches the Pinball 2 matrix how the break moves from one 16th
// to the next. The ball walks those steps: at MUTATION 0 the break plays as it is, at 100 it is
// a new walk that still lands every hit on the right place in the beat. A call & response form
// switches between the learned walk (CALL), a transformed one (ANSWER) and a roll (FILL).
// Break B (optional) is the skin (MORPH, BREAKDNA's spectral layers) and, with BLEND, its
// rhythm teaches the walk too.
//
// Panel:   MUTATION, TEMP (x1/4..x4), BLEND A>B, MORPH, SWING; FEEL, GHOSTS, FORM, ANSWER,
//          TIME (the ball's tick: x1/4..x4 of a 16th), LEVEL; RESET (the ball goes home).
// Jacks:   CLK (1/16 by default) + RST, HALT (trigger: the ball stops until the next reset),
//          MUT CV (10 %/V), TEMP CV (0.4 oct/V), MORPH CV (10 %/V); out L / R (+-5 V),
//          K / S / H triggers (S: snare, ghost, perc; H: hat, open, ride, fx), ACC (velocity,
//          0-10 V), STEP (the ball's step, 0-10 V over the loop).
// Menu:    load A / B, swap, kit (bars, match, B pitch, envelope), call & response (ending,
//          answer temperature, shift, roll), the walk (start step, reset, seed), dynamics,
//          decay, tune, mono, lanes (mute / choke), clock PPQN, free tempo.
// Work split: the loader thread reads + analyses the breaks and learns the matrix; a worker
// (Rack: the UI thread, MetaModule: an AsyncThread) mixes / derives the matrices when the
// knobs move; the audio thread only plays. No CLK: the walk runs free at the break's tempo.

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/GenotypeLayout.hpp"
#include "DnaWalk.hpp"

#ifdef METAMODULE
#include "threads/async_thread.hh"
#endif

namespace L = semi::layout::genotype;
using semi::Screen;
using namespace dnamod;

namespace {

constexpr int kBlock = 32;
constexpr float kVoltsPerUnit = 5.f;

const char* const kBarNames[] = {"Auto", "1 bar", "2 bars", "4 bars"};
const int kBarValues[] = {0, 1, 2, 4};

// built by the loader thread
struct Heavy {
	std::shared_ptr<const geno::Bundle> b;
	WaveView wa, wb;
	bool hasB = false;
};
// handed to the audio thread
struct Live {
	std::shared_ptr<const geno::Bundle> b;
};

// written by the worker (pictures of the breaks and the matrices)
struct StaticView {
	WaveView wa, wb;
	bool hasB = false;
	int steps = 0;
	int8_t mainLane[walk::kMaxSteps]{};
	MatView mat;
	float mutation = 0.f, temp = 1.f;
};

// written by the audio thread
struct Snapshot {
	int ball = -1, role = 0, lit = -1;
	Move moves[kMoves];
	int nMoves = 0;
	double pos16 = 0.0;
	int form = geno::Form22, answer = geno::AnsShift;
	float bpm = 120.f;
	bool running = false, clocked = false, halted = false, busy = false, errA = false, errB = false;
};

int outOfNote(int note) {
	switch (note) {
		case 36: return 0;
		case 38: case 37: case 39: return 1;
		case 42: case 46: case 51: case 49: return 2;
		default: return -1;
	}
}

} // namespace

struct Genotype : Module {
	enum ParamId { MUTATION_PARAM, TEMP_PARAM, BLEND_PARAM, MORPH_PARAM, SWING_PARAM, FEEL_PARAM, GHOSTS_PARAM, FORM_PARAM,
		           ANSWER_PARAM, TIME_PARAM, LEVEL_PARAM, RESET_PARAM, PARAMS_LEN };
	enum InputId { CLK_INPUT, RST_INPUT, HALT_INPUT, MUT_INPUT, TEMP_INPUT, MORPH_INPUT, INPUTS_LEN };
	enum OutputId { L_OUTPUT, R_OUTPUT, K_OUTPUT, S_OUTPUT, H_OUTPUT, ACC_OUTPUT, STEP_OUTPUT, OUTPUTS_LEN };
	enum LightId { HALT_LIGHT, LIGHTS_LEN };

	geno::Engine engine;
	geno::Brain brain;

	// hidden settings (json)
	int barsA = 0, barsB = 0, match = dna::MatchClass, bpitch = 0;
	float env = 0.5f;
	int ending = geno::EndBeat, shift = 8, startStep = 1, resetIdx = 0, seed = 1;
	float atemp = 1.6f, roll = 0.6f, dyn = 1.f, decay = 1.f, tune = 0.f;
	bool mono = true;
	int ppqnIdx = 0, tempoIdx = 0;

	// files (the loader thread syncs to these)
	Paths<2> paths;
	std::shared_ptr<const dna::Sample> cached[2];
	std::string cachedPath[2];
	std::atomic<bool> err[2]{};
	std::atomic<bool> busy{false};

	// worker side
	std::shared_ptr<const geno::Bundle> cur;
	walk::Plain genomeP{}, callP{}, respP{}, fillP{};
	float lastBlend = -1.f;
	geno::DeriveParams lastD;
	bool haveD = false;
	int64_t lastDeriveMs = 0;
	StaticView sv;
	semi::TripleBuffer<StaticView> staticSnap;

	// audio block
	float outBuf[2][kBlock]{};
	int pos = 0;
	double preparedSr = 0.0;
	geno::MidiEvent midiOut[geno::Engine::kMaxOut];
	OutEvents<96> outEv, playEv;
	float trigLeft[3]{};
	float acc = 0.f, stepVolts = 0.f;
	dsp::BooleanTrigger resetBtn;
	dsp::SchmittTrigger haltTrig;
	ClockPos clock;
	double blockPpq = 0.0, blockBpm = 120.0;
	bool blockClocked = false, wasClocked = false, resetFree = false;

	// display
	Move moves[kMoves];
	int nMoves = 0, lit = -1;
	float litLeft = 0.f;
	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;

	semi::Slot<Live> liveSlot;
	semi::Slot<Heavy> heavySlot;
	semi::Loader<Heavy> loader;
#ifdef METAMODULE
	MetaModule::AsyncThread worker{this, [this]() { service(); }};
#endif

	Genotype() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(MUTATION_PARAM, 0.f, 100.f, 40.f, "Mutation", " %")->description =
		    "0: the break as it is, 100: the walk learned from its DNA (every hit still on its place in the beat)";
		configParam(TEMP_PARAM, -2.f, 2.f, 0.f, "Temperature", "", 2.f)->description = "x1/4 sharper .. x4 flatter";
		configParam(BLEND_PARAM, 0.f, 100.f, 0.f, "Blend A > B", " %")->description = "The walk learned from A's rhythm .. from B's";
		configParam(MORPH_PARAM, 0.f, 100.f, 0.f, "Morph A > B", " %")->description = "A's slices into the B slices that answer them";
		configParam(SWING_PARAM, 0.f, 100.f, 0.f, "Swing", " %");
		configParam(FEEL_PARAM, 0.f, 150.f, 100.f, "Feel", " %")->description = "A's micro-timing: 0 grid, 100 A's, 150 more";
		configParam(GHOSTS_PARAM, 0.f, 200.f, 100.f, "Ghosts", " %");
		configSwitch(FORM_PARAM, 0.f, geno::NumForms - 1, geno::Form22, "Form", {"Call only", "1 + 1", "2 + 2", "3 + 1", "4 + 4", "1 + 3"});
		configSwitch(ANSWER_PARAM, 0.f, geno::NumAnswers - 1, geno::AnsShift, "Answer", {"Echo", "Shift", "Invert", "Mirror"});
		configSwitch(TIME_PARAM, 0.f, 7.f, 3.f, "Time (the ball's tick)", {"x1/4", "x1/2", "x3/4", "x1", "x3/2", "x2", "x3", "x4"});
		configParam(LEVEL_PARAM, -24.f, 6.f, 0.f, "Level", " dB");
		configButton(RESET_PARAM, "Reset the ball (to the start step)");
		configInput(CLK_INPUT, "Clock")->description = "1/16 pulses by default (menu: 1/8, 1/4, 24 PPQN). Unpatched: runs free";
		configInput(RST_INPUT, "Reset")->description = "The next clock pulse is the phrase's first 16th; the ball goes home";
		configInput(HALT_INPUT, "Halt (trigger: the ball stops until the next reset)");
		configInput(MUT_INPUT, "Mutation CV (10 %/V)");
		configInput(TEMP_INPUT, "Temperature CV (0.4 octave/V)");
		configInput(MORPH_INPUT, "Morph CV (10 %/V)");
		configOutput(L_OUTPUT, "Left");
		configOutput(R_OUTPUT, "Right");
		configOutput(K_OUTPUT, "Kick trigger");
		configOutput(S_OUTPUT, "Snare / ghost / perc trigger");
		configOutput(H_OUTPUT, "Hat / open / ride / fx trigger");
		configOutput(ACC_OUTPUT, "Accent (the last hit's velocity, 0-10 V)");
		configOutput(STEP_OUTPUT, "Step (the ball's step, 0-10 V over the loop)");
		configLight(HALT_LIGHT, "Halted");
		engine.prepare(48000.0, kBlock);
		preparedSr = 48000.0;
		staticSnap.publish(sv);
		loader.attach(this, &heavySlot, [this](const std::string&, Heavy& h) { return build(h); });
#ifdef METAMODULE
		worker.start();
#endif
	}

	// ---- the loader thread: breaks -> kit -> learned walk ---------------------------------

	bool build(Heavy& out) {
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
		kp.barsA = kBarValues[clamp(barsA, 0, 3)];
		kp.barsB = kBarValues[clamp(barsB, 0, 3)];
		auto b = std::make_shared<geno::Bundle>();
		b->kit = std::shared_ptr<const dna::Kit>(dna::buildKit(cached[0], cached[1] ? cached[1] : cached[0], kp).release());
		b->learned = geno::learn(*b->kit);
		const dna::Kit& k = *b->kit;
		makeWave(*k.a, k.sa.starts, k.sa.cls, out.wa);
		out.wa.bars = k.g.bars;
		out.wa.bpm = (float) k.g.bpm;
		makeWave(*k.b, k.sb.starts, k.sb.cls, out.wb);
		out.hasB = cached[1] != nullptr;
		out.b = b;
		busy = false;
		return true;
	}

	void requestBuild() { loader.request("sync"); }
	void setPath(int i, const std::string& p) {
		paths.set(i, p);
		requestBuild();
	}

	// ---- the worker: genome (mix by BLEND) -> call / answer / fill ------------------------

	geno::DeriveParams deriveParams() {
		geno::DeriveParams d;
		d.mutation = clamp((params[MUTATION_PARAM].getValue() + inputs[MUT_INPUT].getVoltage() * 10.f) * 0.01f, 0.f, 1.f);
		d.temp = std::pow(2.f, clamp(params[TEMP_PARAM].getValue() + inputs[TEMP_INPUT].getVoltage() * 0.4f, -2.f, 2.f));
		d.answer = clamp((int) std::round(params[ANSWER_PARAM].getValue()), 0, geno::NumAnswers - 1);
		d.answerTemp = atemp;
		d.shift = shift;
		d.roll = roll;
		return d;
	}

	static bool sameD(const geno::DeriveParams& a, const geno::DeriveParams& b) {
		return std::fabs(a.mutation - b.mutation) < 0.004f && std::fabs(a.temp - b.temp) < 0.004f * a.temp && a.answer == b.answer &&
		       std::fabs(a.answerTemp - b.answerTemp) < 1e-4f && a.shift == b.shift && std::fabs(a.roll - b.roll) < 1e-4f;
	}

	void service() {
		liveSlot.collect();
		bool fresh = false;
		if (heavySlot.poll()) {
			const Heavy* h = heavySlot.active;
			cur = h->b;
			sv.wa = h->wa;
			sv.wb = h->wb;
			sv.hasB = h->hasB;
			const auto& Ln = cur->learned;
			sv.steps = Ln.steps;
			for (int s = 0; s < walk::kMaxSteps; s++)
				sv.mainLane[s] = s < Ln.steps ? Ln.a[(size_t) s].main : -1;
			fresh = true;
		}
		if (!cur)
			return;
#ifdef METAMODULE
		if (!fresh && nowMs() - lastDeriveMs < 30)
			return;
#endif
		const auto& Ln = cur->learned;
		const float blend = params[BLEND_PARAM].getValue() * 0.01f;
		const bool regen = fresh || std::fabs(blend - lastBlend) > 1e-3f;
		if (regen) {
			geno::mix(Ln.fromA, Ln.fromB, blend, Ln.steps, genomeP);
			brain.genome.copyFrom(genomeP);
			lastBlend = blend;
		}
		const geno::DeriveParams d = deriveParams();
		if (regen || !haveD || !sameD(d, lastD)) {
			geno::derive(genomeP, Ln, d, callP, respP, fillP);
			brain.call.copyFrom(callP);
			brain.resp.copyFrom(respP);
			brain.fill.copyFrom(fillP);
			lastD = d;
			haveD = true;
			lastDeriveMs = nowMs();
			makeMatView(callP, respP, fillP, Ln.steps, sv.mat);
			sv.mutation = d.mutation;
			sv.temp = d.temp;
			staticSnap.publish(sv);
		}
		if (fresh)
			liveSlot.offer(new Live{cur});
	}

	// ---- json -------------------------------------------------------------------------------

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "pathA", json_string(paths.get(0).c_str()));
		json_object_set_new(root, "pathB", json_string(paths.get(1).c_str()));
		json_object_set_new(root, "barsA", json_integer(barsA));
		json_object_set_new(root, "barsB", json_integer(barsB));
		json_object_set_new(root, "match", json_integer(match));
		json_object_set_new(root, "bpitch", json_integer(bpitch));
		json_object_set_new(root, "env", json_real(env));
		json_object_set_new(root, "ending", json_integer(ending));
		json_object_set_new(root, "shift", json_integer(shift));
		json_object_set_new(root, "start", json_integer(startStep));
		json_object_set_new(root, "reset", json_integer(resetIdx));
		json_object_set_new(root, "seed", json_integer(seed));
		json_object_set_new(root, "atemp", json_real(atemp));
		json_object_set_new(root, "roll", json_real(roll));
		json_object_set_new(root, "dyn", json_real(dyn));
		json_object_set_new(root, "decay", json_real(decay));
		json_object_set_new(root, "tune", json_real(tune));
		json_object_set_new(root, "mono", json_boolean(mono));
		json_object_set_new(root, "ppqn", json_integer(ppqnIdx));
		json_object_set_new(root, "tempo", json_integer(tempoIdx));
		json_t* lanes = json_array();
		for (auto& l : brain.lanes)
			json_array_append_new(lanes, json_integer(l.mute.load() | (l.choke.load() << 1)));
		json_object_set_new(root, "lanes", lanes);
		return root;
	}

	void dataFromJson(json_t* root) override {
		jsonInt(root, "barsA", barsA, 0, 3);
		jsonInt(root, "barsB", barsB, 0, 3);
		jsonInt(root, "match", match, 0, dna::NumMatch - 1);
		jsonInt(root, "bpitch", bpitch, -12, 12);
		jsonFloat(root, "env", env, 0.f, 1.f);
		jsonInt(root, "ending", ending, 0, geno::NumEndings - 1);
		jsonInt(root, "shift", shift, 0, 31);
		jsonInt(root, "start", startStep, 1, 64);
		jsonInt(root, "reset", resetIdx, 0, 8);
		jsonInt(root, "seed", seed, 1, 9999);
		jsonFloat(root, "atemp", atemp, 0.25f, 4.f);
		jsonFloat(root, "roll", roll, 0.f, 1.f);
		jsonFloat(root, "dyn", dyn, 0.f, 1.5f);
		jsonFloat(root, "decay", decay, 0.f, 1.f);
		jsonFloat(root, "tune", tune, -12.f, 12.f);
		jsonBool(root, "mono", mono);
		jsonInt(root, "ppqn", ppqnIdx, 0, kNumPpqn - 1);
		jsonInt(root, "tempo", tempoIdx, 0, kNumTempos - 1);
		if (json_t* a = json_object_get(root, "lanes"))
			for (int l = 0; l < geno::NumLanes; l++)
				if (json_t* v = json_array_get(a, l)) {
					const int f = (int) json_integer_value(v);
					brain.lanes[l].mute.store(f & 1);
					brain.lanes[l].choke.store((f >> 1) & 1);
				}
		haveD = false;
		paths.set(0, jsonStr(root, "pathA"));
		paths.set(1, jsonStr(root, "pathB"));
		requestBuild();
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		barsA = barsB = 0;
		match = dna::MatchClass;
		bpitch = 0;
		env = 0.5f;
		ending = geno::EndBeat;
		shift = 8;
		startStep = 1;
		resetIdx = 0;
		seed = 1;
		atemp = 1.6f;
		roll = 0.6f;
		dyn = decay = 1.f;
		tune = 0.f;
		mono = true;
		for (int l = 0; l < geno::NumLanes; l++) {
			brain.lanes[l].mute.store(0);
			brain.lanes[l].choke.store(l == geno::LHat || l == geno::LOpen);
		}
		haveD = false;
		requestBuild();
	}

	// ---- audio ------------------------------------------------------------------------------

	void runBlock(float sampleRate) {
		liveSlot.poll();
		const geno::Bundle* b = liveSlot.active ? liveSlot.active->b.get() : nullptr;

		geno::Params p;
		p.feel = params[FEEL_PARAM].getValue() * 0.01f;
		p.dyn = dyn;
		p.ghosts = params[GHOSTS_PARAM].getValue() * 0.01f;
		p.morph = clamp((params[MORPH_PARAM].getValue() + inputs[MORPH_INPUT].getVoltage() * 10.f) * 0.01f, 0.f, 1.f);
		p.decay = decay;
		p.tune = tune;
		p.gain = std::pow(10.f, params[LEVEL_PARAM].getValue() / 20.f);
		p.mono = mono;
		p.form = clamp((int) std::round(params[FORM_PARAM].getValue()), 0, geno::NumForms - 1);
		p.ending = ending;
		p.mult = kTimeValues[clamp((int) std::round(params[TIME_PARAM].getValue()), 0, 7)];
		p.swing = params[SWING_PARAM].getValue() * 0.01f;
		p.startStep = startStep;
		p.resetBars = kResetValues[clamp(resetIdx, 0, 8)];
		p.midiOut = true;
		p.seed = (uint32_t) seed;
		if (blockClocked) {
			p.run = geno::RunHost;
			p.playing = true;
			p.bpm = blockBpm;
			// the audio leaves a block later; the first block after the clock starts is the loop's start
			p.ppq = wasClocked ? blockPpq + kBlock / (double) sampleRate * blockBpm / 60.0 : 0.0;
		}
		else {
			p.playing = false;
			p.run = inputs[CLK_INPUT].isConnected() ? geno::RunHost : geno::RunAlways;
			p.bpm = tempoOf(tempoIdx, b && b->kit ? b->kit->g.bpm : 0.0);
			if (resetFree) {
				engine.reset();
				resetFree = false;
			}
		}

		wasClocked = blockClocked;
		int numOut = 0;
		engine.process(b, brain, p, nullptr, 0, outBuf[0], outBuf[1], kBlock, midiOut, numOut);
		outEv.clear();
		for (int i = 0; i < numOut; i++) {
			const int o = outOfNote(midiOut[i].note);
			if (o >= 0)
				outEv.add(midiOut[i].offset, o, midiOut[i].vel > 0, midiOut[i].vel / 127.f);
		}
		const int steps = b ? b->learned.steps : 0;
		const int ball = engine.viewStep.load(std::memory_order_relaxed);
		if (steps > 0 && ball >= 0)
			stepVolts = 10.f * ball / steps;
	}

	void process(const ProcessArgs& args) override {
		if (args.sampleRate != preparedSr) {
			engine.prepare(args.sampleRate, kBlock);
			preparedSr = args.sampleRate;
			pos = 0;
		}
		double ppq = 0.0, bpm = 120.0;
		const bool clocked = clock.process(args.sampleTime, inputs[CLK_INPUT].isConnected(), inputs[CLK_INPUT].getVoltage(),
		                                   inputs[RST_INPUT].getVoltage(), ppqnOf(ppqnIdx), ppq, bpm);
		if (clock.resetFired) {
			if (inputs[CLK_INPUT].isConnected())
				engine.resetRequest.store(1);
			else
				resetFree = true;
		}
		if (resetBtn.process(params[RESET_PARAM].getValue() > 0.f))
			engine.resetRequest.store(1);
		if (haltTrig.process(inputs[HALT_INPUT].getVoltage(), 0.1f, 1.f))
			engine.haltRequest.store(1);
		if (pos == 0) {
			blockPpq = ppq;
			blockBpm = bpm;
			blockClocked = clocked;
		}

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
		outputs[STEP_OUTPUT].setVoltage(stepVolts);

		if (++pos >= kBlock) {
			pos = 0;
			runBlock(args.sampleRate);
			playEv = outEv;
		}
		if (ui.tick(args.sampleTime))
			publish(clocked);
	}

	void publish(bool clocked) {
		geno::HitEvent h;
		while (engine.popHit(h)) {
			lit = h.slice;
			litLeft = 0.15f;
		}
		litLeft -= 1.f / semi::platform::kUiRateHz;
		if (litLeft <= 0.f)
			lit = -1;
		geno::MoveEvent mv;
		while (engine.popMove(mv)) {
			if (mv.to < 0)
				continue;
			if (nMoves == kMoves) {
				for (int i = 1; i < kMoves; i++)
					moves[i - 1] = moves[i];
				nMoves--;
			}
			moves[nMoves++] = {mv.from, mv.to, mv.role};
		}
		const bool halted = engine.viewHalted.load(std::memory_order_relaxed) != 0;
		lights[HALT_LIGHT].setBrightness(halted ? 1.f : 0.f);

		Snapshot& s = snap.writeSlot();
		s.ball = engine.viewStep.load(std::memory_order_relaxed);
		s.role = engine.viewRole.load(std::memory_order_relaxed);
		s.lit = lit;
		for (int i = 0; i < nMoves; i++)
			s.moves[i] = moves[i];
		s.nMoves = nMoves;
		s.pos16 = (double) engine.viewTick.load(std::memory_order_relaxed) *
		          kTimeValues[clamp((int) std::round(params[TIME_PARAM].getValue()), 0, 7)];
		s.form = clamp((int) std::round(params[FORM_PARAM].getValue()), 0, geno::NumForms - 1);
		s.answer = clamp((int) std::round(params[ANSWER_PARAM].getValue()), 0, geno::NumAnswers - 1);
		s.bpm = (float) engine.viewBpm.load(std::memory_order_relaxed);
		s.running = engine.viewPlaying.load(std::memory_order_relaxed) != 0;
		s.clocked = clocked;
		s.halted = halted;
		s.busy = busy.load();
		s.errA = err[0].load();
		s.errB = err[1].load();
		snap.publish();
	}
};

// ---------------------------------------------------------------------------------- screens

namespace {

void demoStatic(StaticView& v) {
	v.wa.valid = true;
	for (int c = 0; c < kWaveCols; c++)
		v.wa.peak[c] = (uint8_t) (50 + 200 * std::exp(-(float) (c % 8) * 0.45f));
	v.wa.nMarks = 16;
	static const uint8_t cls[] = {0, 3, 1, 3, 2, 0, 1, 3, 0, 3, 1, 2, 0, 3, 1, 3};
	for (int i = 0; i < 16; i++) {
		v.wa.mark[i] = (uint16_t) (i * 65535 / 16);
		v.wa.cls[i] = cls[i];
		v.wa.lit[i] = 1;
	}
	v.wa.bars = 2;
	v.wa.bpm = 170.f;
	std::snprintf(v.wa.name, sizeof v.wa.name, "amen.wav");
	v.steps = 32;
	static const int8_t lanes[16] = {0, -1, 0, -1, 1, -1, 3, 2, -1, 2, 0, -1, 1, -1, 3, -1};
	for (int s = 0; s < 32; s++)
		v.mainLane[s] = lanes[s % 16];
	static walk::Plain c{}, r{}, f{};   // static: 50 KB, kept off the GUI thread's stack
	for (int x = 1; x <= 32; x++) {
		c[x][x % 32 + 1] = 100.f;
		c[x][(x + 7) % 32 + 1] = (x % 4 == 1) ? 60.f : 0.f;
		c[x][(x + 15) % 32 + 1] = 30.f;
		r[x][(x + 8) % 32 + 1] = 100.f;
		f[x][x] = (x % 8 == 5) ? 100.f : 0.f;
		f[x][x % 32 + 1] = 40.f;
	}
	makeMatView(c, r, f, 32, v.mat);
	v.mutation = 0.4f;
	v.temp = 1.f;
}

StaticView& demoView() {
	static StaticView v;
	static bool made = false;
	if (!made) {
		demoStatic(v);
		made = true;
	}
	return v;
}

Snapshot demoSnap() {
	Snapshot s;
	s.ball = 10;
	s.role = geno::RoleCall;
	s.lit = 5;
	const Move m[4] = {{7, 7, 0}, {8, 8, 0}, {9, 16, 0}, {17, 10, 0}};
	for (int i = 0; i < 4; i++)
		s.moves[i] = m[i];
	s.nMoves = 4;
	s.pos16 = 26.0;
	s.bpm = 170.f;
	s.running = true;
	return s;
}

} // namespace

// Break A (slices by class, the one that plays framed) over the steps strip: each 16th's
// loudest lane and the ball, framed in its role's colour.
struct GenotypeWaveScreen : Screen {
	Genotype* module = nullptr;
	Snapshot demo = demoSnap();

	void drawScreen(NVGcontext* vg) override {
		const StaticView& v = module ? module->staticSnap.read() : demoView();
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y, pad = 3.f;
		char buf[48];
		std::snprintf(buf, sizeof buf, "A %s", s.errA ? "LOAD FAILED" : v.wa.valid ? v.wa.name : "load a break (menu)");
		text(vg, pad, 2.f, buf, 9.f, s.errA ? semi::theme::magenta() : semi::theme::cyan());
		if (s.busy)
			text(vg, w - pad, 2.f, "LEARNING", 9.f, semi::theme::amber(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		else if (v.wa.valid) {
			std::snprintf(buf, sizeof buf, "%s%.0f BPM", v.hasB ? "+B " : "", s.bpm);
			text(vg, w - pad, 2.f, buf, 9.f, semi::theme::text(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		}
		const float sh = 9.f, wy = 13.f, wh = h - wy - sh - 2 * pad;
		drawWave(*this, vg, v.wa, pad, wy, w - 2 * pad, wh, s.lit, false);
		drawSteps(vg, v.mainLane, v.steps, pad, h - pad - sh, w - 2 * pad, sh, s.running ? s.ball : -1, s.role);
	}
};

// The matrix the walk uses now (CALL / ANSWER / FILL by the place in the form), the ball's row
// and its last moves; on the right the role, the readouts and the form bar by bar.
struct GenotypeMatrixScreen : Screen {
	Genotype* module = nullptr;
	Snapshot demo = demoSnap();

	void drawScreen(NVGcontext* vg) override {
		const StaticView& v = module ? module->staticSnap.read() : demoView();
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y, pad = 3.f;
		const float size = h - 2 * pad;
		const int role = clamp(s.role, 0, 2);
		drawMatrix(*this, vg, v.mat, role, pad, pad, size, s.running ? s.ball : -1, s.moves, s.nMoves);

		const float tx = pad + size + 5.f, tw = w - tx - pad;
		text(vg, tx, pad, s.halted ? "HALT" : s.running ? roleName(role) : "STOP", 11.f,
		     s.halted ? semi::theme::amber() : s.running ? roleColor(role) : semi::theme::dim());
		char buf[32];
		std::snprintf(buf, sizeof buf, "MUT %d%%", (int) std::lround(v.mutation * 100.f));
		text(vg, tx, pad + 14.f, buf, 9.f, semi::theme::magenta());
		std::snprintf(buf, sizeof buf, "TEMP x%.2f", v.temp);
		text(vg, tx, pad + 24.f, buf, 9.f, semi::theme::text());
		std::snprintf(buf, sizeof buf, "%s %s", kFormNames[clamp(s.form, 0, 5)], kAnswerNames[clamp(s.answer, 0, 3)]);
		text(vg, tx, pad + 34.f, buf, 9.f, semi::theme::text());

		// the form, bar by bar: call cyan, answer magenta, the bar that plays bright
		const int callBars = geno::formCallBars(s.form), bars = geno::formBars(s.form);
		const int bar = (int) std::floor(std::fmod(std::max(0.0, s.pos16), 16.0 * bars) / 16.0);
		const float by = h - pad - 8.f, bw = tw / bars;
		for (int i = 0; i < bars; i++) {
			const bool call = i < callBars;
			const bool now = s.running && i == bar;
			fillRect(vg, tx + i * bw + 0.5f, by, bw - 1.f, 8.f,
			         now ? (call ? semi::theme::cyan() : semi::theme::magenta()) : (call ? semi::theme::cyanDeep() : semi::theme::magentaDeep()));
		}
	}
};

struct GenotypeWidget : ModuleWidget {
	GenotypeWidget(Genotype* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Genotype.svg")));

		auto* wave = semi::createScreenMm<GenotypeWaveScreen>(Vec(L::WAVE.x, L::WAVE.y), Vec(L::WAVE.w, L::WAVE.h));
		wave->module = module;
		addChild(wave);
		auto* mat = semi::createScreenMm<GenotypeMatrixScreen>(Vec(L::MATRIX.x, L::MATRIX.y), Vec(L::MATRIX.w, L::MATRIX.h));
		mat->module = module;
		addChild(mat);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		auto snapKnob = [&](app::ParamWidget* k) {
			if (auto* kn = dynamic_cast<app::Knob*>(k))
				kn->snap = true;
			addParam(k);
		};
		addParam(createParamCentered<semi::Button>(at(L::RESET), module, Genotype::RESET_PARAM));
		addChild(createLightCentered<semi::AmberLedSmall>(at(L::HALT_LED), module, Genotype::HALT_LIGHT));
		addParam(createParamCentered<semi::KnobMHot>(at(L::MUTATION), module, Genotype::MUTATION_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::TEMP), module, Genotype::TEMP_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::BLEND), module, Genotype::BLEND_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::MORPH), module, Genotype::MORPH_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::SWING), module, Genotype::SWING_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::FEEL), module, Genotype::FEEL_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::GHOSTS), module, Genotype::GHOSTS_PARAM));
		snapKnob(createParamCentered<semi::KnobS>(at(L::FORM), module, Genotype::FORM_PARAM));
		snapKnob(createParamCentered<semi::KnobS>(at(L::ANSWER), module, Genotype::ANSWER_PARAM));
		snapKnob(createParamCentered<semi::KnobS>(at(L::TIME), module, Genotype::TIME_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::LEVEL), module, Genotype::LEVEL_PARAM));

		addInput(createInputCentered<semi::Jack>(at(L::CLK), module, Genotype::CLK_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RST), module, Genotype::RST_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::HALT), module, Genotype::HALT_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::MUT_CV), module, Genotype::MUT_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::TEMP_CV), module, Genotype::TEMP_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::MORPH_CV), module, Genotype::MORPH_INPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_L), module, Genotype::L_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_R), module, Genotype::R_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::K_OUT), module, Genotype::K_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::S_OUT), module, Genotype::S_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::H_OUT), module, Genotype::H_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::ACC_OUT), module, Genotype::ACC_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::STEP_OUT), module, Genotype::STEP_OUTPUT));
	}

#ifndef METAMODULE
	void step() override {
		if (auto* m = dynamic_cast<Genotype*>(module))
			m->service();
		ModuleWidget::step();
	}
#endif

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Genotype*>(module);
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

		menu->addChild(createSubmenuItem("Kit (relearned off the audio thread)", "", [=](Menu* sub) {
			std::vector<std::string> bars(kBarNames, kBarNames + 4);
			sub->addChild(intMenu("Bars A", bars, &m->barsA, rebuild));
			sub->addChild(intMenu("Bars B", bars, &m->barsB, rebuild));
			sub->addChild(intMenu("Match", {"Class (nearest sound)", "Position", "Random"}, &m->match, rebuild));
			sub->addChild(createIndexSubmenuItem("B pitch", stepLabels(-12.f, 12.f, 25, 1.f, " st"),
				[=]() { return (size_t) (m->bpitch + 12); }, [=](size_t i) { m->bpitch = (int) i - 12; m->requestBuild(); }));
			sub->addChild(floatMenu("Envelope (A's attack in the blends)", &m->env, 0.f, 1.f, 11, 100.f, "%", rebuild));
		}));
		menu->addChild(createSubmenuItem("Call & response", "", [=](Menu* sub) {
			sub->addChild(intMenu("Ending (the fill)", std::vector<std::string>(kEndingNames, kEndingNames + 4), &m->ending));
			sub->addChild(floatMenu("Answer temperature", &m->atemp, 0.25f, 4.f, 16, 1.f, ""));
			std::vector<std::string> sh;
			for (int i = 0; i < 32; i++)
				sh.push_back(string::f("+%d", i));
			sub->addChild(intMenu("Answer shift (steps)", sh, &m->shift));
			sub->addChild(floatMenu("Roll (the fill)", &m->roll, 0.f, 1.f, 11, 100.f, "%"));
		}));
		menu->addChild(createSubmenuItem("Walk", "", [=](Menu* sub) {
			std::vector<std::string> st;
			for (int i = 1; i <= 64; i++)
				st.push_back(string::f("%d", i));
			sub->addChild(createIndexSubmenuItem("Start step", st, [=]() { return (size_t) (m->startStep - 1); },
			                                     [=](size_t i) { m->startStep = (int) i + 1; }));
			sub->addChild(intMenu("Reset every", std::vector<std::string>(kResetNames, kResetNames + 9), &m->resetIdx));
			sub->addChild(createMenuItem("New seed (another walk)", string::f("%d", m->seed),
			                             [=]() { m->seed = 1 + (int) (random::u32() % 9999u); }));
		}));
		menu->addChild(createSubmenuItem("Sound", "", [=](Menu* sub) {
			sub->addChild(floatMenu("Dynamics", &m->dyn, 0.f, 1.5f, 16, 100.f, "%"));
			sub->addChild(floatMenu("Decay", &m->decay, 0.f, 1.f, 11, 100.f, "%"));
			sub->addChild(floatMenu("Tune", &m->tune, -12.f, 12.f, 25, 1.f, " st"));
			sub->addChild(createBoolPtrMenuItem("Mono (every hit cuts the last)", "", &m->mono));
		}));
		menu->addChild(createSubmenuItem("Lanes", "", [=](Menu* sub) {
			for (int l = 0; l < geno::NumLanes; l++) {
				sub->addChild(createSubmenuItem(geno::laneName(l), "", [=](Menu* lm) {
					lm->addChild(createBoolMenuItem("Mute", "", [=]() { return m->brain.lanes[l].mute.load() != 0; },
					                                [=](bool v) { m->brain.lanes[l].mute.store(v ? 1 : 0); }));
					lm->addChild(createBoolMenuItem("Choke group", "", [=]() { return m->brain.lanes[l].choke.load() != 0; },
					                                [=](bool v) { m->brain.lanes[l].choke.store(v ? 1 : 0); }));
				}));
			}
		}));
		menu->addChild(intMenu("Clock input", ppqnNames(), &m->ppqnIdx));
		menu->addChild(intMenu("Tempo without CLK", tempoNames(), &m->tempoIdx));
	}
};

Model* modelGenotype = createModel<Genotype, GenotypeWidget>("Genotype");
