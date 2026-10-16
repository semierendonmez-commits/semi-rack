// LOOPGENOME — Rack / MetaModule port of LOOPGENOME (engine: engines/loopgenome on top of
// engines/genotype + engines/breakdna, vendored from semierendonmez-commits/loopgenome
// Source/engine; namespaces wrapped in dnafam::, the Pinball copy is dnafam::walk shared with
// GENOTYPE, see the sources.mk files).
//
// Drums and bass learned as ONE loop. Every 16th of the break is a joint state: what the drums
// do there (kick, snare, hit, rest) and what the bass does (rest, held, root, note). One Pinball
// ball walks the loop: the step it lands on plays the break's hits AND that step's bass cell
// AND its articulation (NEUROSTACK's phase-locked neuro bass over one sub), so a jump is a drum
// switch-up and a bass switch-up at once. The SKELETON (kick + snare) is a hard rule of the
// walk: even at MUTATION 100 the kicks and snares land where the loop has them.
//
// Panel:   MUTATION, TEMP, COUPLE (how much the bass decides the jumps), DRUMS, BASS (levels);
//          FEEL, GHOSTS, FORM, ANSWER, SWING, DRIVE (the bass); RESET (the ball goes home).
// Jacks:   CLK (1/16 by default) + RST, HALT (trigger), MUT CV (10 %/V), TEMP CV (0.4 oct/V),
//          ROOT (V/oct: the bass's key, folded to the sub's octave); out L / R (+-5 V, drums +
//          bass), K / S triggers, V/OCT + GATE (the bass line, MIDI channel 2 of the plugin),
//          STEP (the ball's step, 0-10 V over the loop).
// Menu:    load the break, load a bass loop (WAV), the bass line (source FROM KICKS / STYLE /
//          AUDIO LOOP, style, density, pickups, loop bars), skeleton, articulation map, the
//          eight articulation slots (model, level, tone, motion, grit), the bass sound (key,
//          octave, sub, crossover, duck, snare gap, glide, switch, release, width, gate), call &
//          response (ending, answer temperature, shift, roll, answer articulations, fill bass,
//          fill articulation), the walk (time, start, reset, seed), drums (dynamics, decay,
//          tune, mono, lanes), clock PPQN, free tempo.
// Dropped: the MIDI FILE bass source (no MIDI files here: patch a sequencer into ROOT), drawing
// on the matrix / step rows, presets, MIDI drag.

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/LoopgenomeLayout.hpp"
#include "DnaWalk.hpp"
#include "loopgenome/Loop.h"

#ifdef METAMODULE
#include "threads/async_thread.hh"
#endif

namespace L = semi::layout::loopgenome;
using semi::Screen;
using namespace dnamod;

namespace {

constexpr int kBlock = 32;
constexpr float kVoltsPerUnit = 5.f;
constexpr int kArts = neuro::NumArts;

const char* const kBarNames[] = {"Auto", "1 bar", "2 bars", "4 bars"};
const int kBarValues[] = {0, 1, 2, 4};

// built by the loader thread
struct Heavy {
	std::shared_ptr<const dna::Kit> kit;
	geno::Learned learned;
	std::shared_ptr<const std::vector<float>> bassMono;
	double bassRate = 44100.0;
	std::string bassName;
	WaveView wa;
	int serial = 0;
};
struct Live {
	std::shared_ptr<const lg::Bundle> b;
};

struct StaticView {
	WaveView wa;
	int steps = 0;
	int8_t mainLane[walk::kMaxSteps]{};
	int8_t bassType[walk::kMaxSteps]{};
	int8_t bassSemis[walk::kMaxSteps]{};
	int8_t art[walk::kMaxSteps]{};
	MatView mat;
	float mutation = 0.f, couple = 0.7f;
	char line[24]{};
};

struct Snapshot {
	int ball = -1, role = 0, lit = -1, art = -1;
	Move moves[kMoves];
	int nMoves = 0;
	double pos16 = 0.0;
	int form = geno::Form22, answer = geno::AnsEcho;
	float bpm = 174.f;
	bool running = false, halted = false, busy = false, errA = false, errB = false, gate = false;
};

struct ArtSet {
	int model = 0;
	float level = 0.5f, tone = 0.5f, motion = 0.4f, grit = 0.3f;
};

struct JointKey {
	int source = lg::SrcKicks, style = 0, bbars = 0, seed = 1, skeleton = lg::SkelKickSnare, heavy = 0;
	float density = 0.8f, pickup = 0.3f, couple = 0.7f;
	bool same(const JointKey& o) const {
		return source == o.source && style == o.style && bbars == o.bbars && seed == o.seed && skeleton == o.skeleton &&
		       heavy == o.heavy && std::fabs(density - o.density) < 1e-4f && std::fabs(pickup - o.pickup) < 1e-4f &&
		       std::fabs(couple - o.couple) < 0.005f;
	}
	bool sameLine(const JointKey& o) const {
		return source == o.source && style == o.style && bbars == o.bbars && seed == o.seed && heavy == o.heavy &&
		       std::fabs(density - o.density) < 1e-4f && std::fabs(pickup - o.pickup) < 1e-4f;
	}
};

int foldRoot(int note) {
	const int pc = ((note % 12) + 12) % 12;
	int r = 24 + pc;
	if (r < 28)
		r += 12;   // E0 .. D#1: the sub's octave
	return r;
}

} // namespace

struct Loopgenome : Module {
	enum ParamId { MUTATION_PARAM, TEMP_PARAM, COUPLE_PARAM, DRUMS_PARAM, BASS_PARAM, FEEL_PARAM, GHOSTS_PARAM, FORM_PARAM,
		           ANSWER_PARAM, SWING_PARAM, DRIVE_PARAM, RESET_PARAM, PARAMS_LEN };
	enum InputId { CLK_INPUT, RST_INPUT, HALT_INPUT, MUT_INPUT, TEMP_INPUT, ROOT_INPUT, INPUTS_LEN };
	enum OutputId { L_OUTPUT, R_OUTPUT, K_OUTPUT, S_OUTPUT, PITCH_OUTPUT, GATE_OUTPUT, STEP_OUTPUT, OUTPUTS_LEN };
	enum LightId { HALT_LIGHT, LIGHTS_LEN };

	lg::Engine engine;
	lg::Brain brain;

	// hidden settings (json)
	int barsA = 0;
	int source = lg::SrcKicks, style = 0, bbars = 0, skeleton = lg::SkelKickSnare, artMapIdx = lg::MapHalves;
	float density = 0.8f, pickup = 0.3f;
	int ending = geno::EndBeat, shift = 8, startStep = 1, resetIdx = 0, seed = 1, timeIdx = 3;
	float atemp = 1.6f, roll = 0.6f;
	int answerArts = 4, fillBass = lg::FillPickup, fillArt = 0;
	float dyn = 1.f, decay = 1.f, tune = 0.f;
	bool mono = true;
	int key = 0, boct = 0;   // key 0 = AUTO, 1..12 = C..B
	float sub = 0.7f, xover = 2.5f, duck = 0.5f, gapMs = 25.f, glideMs = 0.f, xfadeMs = 4.f, releaseMs = 60.f, width = 0.35f,
	      gate = 1.f;
	ArtSet arts[kArts];
	int ppqnIdx = 0, tempoIdx = 0;

	// files (the loader thread syncs to these): 0 the break, 1 the bass loop
	Paths<2> paths;
	std::shared_ptr<const dna::Sample> cachedBreak;
	std::shared_ptr<const dna::Kit> cachedKit;
	int cachedBars = -1;
	std::shared_ptr<const std::vector<float>> cachedBass;
	double cachedBassRate = 44100.0;
	std::string cachedPath[2];
	std::atomic<bool> err[2]{};
	std::atomic<bool> busy{false};
	int heavySerial = 0;

	// worker side
	const Heavy* hv = nullptr;
	std::shared_ptr<const lg::Bundle> cur;
	JointKey builtKey, pendingKey, lineKey;
	bool haveBuilt = false, haveLine = false;
	int64_t pendingSince = 0, lastDeriveMs = 0;
	lg::BassLine line;
	walk::Plain genomeP{}, callP{}, respP{}, fillP{};
	geno::DeriveParams lastD;
	int appliedMap = -1;
	bool haveD = false;
	StaticView sv;
	semi::TripleBuffer<StaticView> staticSnap;
	std::atomic<int> rootSeen{31};   // the line's root (AUDIO), folded, for AUTO

	// audio block
	float outBuf[2][kBlock]{};
	int pos = 0;
	double preparedSr = 0.0;
	geno::MidiEvent midiOut[lg::Engine::kMaxOut];
	uint8_t midiCh[lg::Engine::kMaxOut]{};
	OutEvents<128> outEv, playEv;
	float trigLeft[2]{};
	float pitchV = 0.f, stepVolts = 0.f;
	int bassHeld = -1;
	bool gateOut = false;
	dsp::BooleanTrigger resetBtn;
	dsp::SchmittTrigger haltTrig;
	ClockPos clock;
	double blockPpq = 0.0, blockBpm = 174.0;
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

	Loopgenome() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(MUTATION_PARAM, 0.f, 100.f, 40.f, "Mutation", " %")->description =
		    "0: the loop as it is, 100: the joint walk (the skeleton stays where it is)";
		configParam(TEMP_PARAM, -2.f, 2.f, 0.f, "Temperature", "", 2.f)->description = "x1/4 sharper .. x4 flatter";
		configParam(COUPLE_PARAM, 0.f, 100.f, 70.f, "Couple", " %")->description =
		    "How much the bass decides the jumps (0: the drums choose, the bass follows)";
		configParam(DRUMS_PARAM, -24.f, 6.f, 0.f, "Drums level", " dB");
		configParam(BASS_PARAM, -24.f, 6.f, 0.f, "Bass level", " dB");
		configParam(FEEL_PARAM, 0.f, 150.f, 100.f, "Feel", " %");
		configParam(GHOSTS_PARAM, 0.f, 200.f, 100.f, "Ghosts", " %");
		configSwitch(FORM_PARAM, 0.f, geno::NumForms - 1, geno::Form22, "Form", {"Call only", "1 + 1", "2 + 2", "3 + 1", "4 + 4", "1 + 3"});
		configSwitch(ANSWER_PARAM, 0.f, geno::NumAnswers - 1, geno::AnsEcho, "Answer", {"Echo", "Shift", "Invert", "Mirror"});
		configParam(SWING_PARAM, 0.f, 100.f, 0.f, "Swing", " %");
		configParam(DRIVE_PARAM, 0.f, 100.f, 25.f, "Bass drive", " %");
		configButton(RESET_PARAM, "Reset the ball (to the start step)");
		configInput(CLK_INPUT, "Clock")->description = "1/16 pulses by default (menu: 1/8, 1/4, 24 PPQN). Unpatched: runs free";
		configInput(RST_INPUT, "Reset")->description = "The next clock pulse is the phrase's first 16th; the ball goes home";
		configInput(HALT_INPUT, "Halt (trigger: the ball stops until the next reset)");
		configInput(MUT_INPUT, "Mutation CV (10 %/V)");
		configInput(TEMP_INPUT, "Temperature CV (0.4 octave/V)");
		configInput(ROOT_INPUT, "Root (V/oct: the bass's key, folded to the sub's octave)");
		configOutput(L_OUTPUT, "Left (drums + bass)");
		configOutput(R_OUTPUT, "Right (drums + bass)");
		configOutput(K_OUTPUT, "Kick trigger");
		configOutput(S_OUTPUT, "Snare / ghost / perc trigger");
		configOutput(PITCH_OUTPUT, "Bass pitch (V/oct, 0 V = C4)");
		configOutput(GATE_OUTPUT, "Bass gate");
		configOutput(STEP_OUTPUT, "Step (the ball's step, 0-10 V over the loop)");
		configLight(HALT_LIGHT, "Halted");
		resetArts();
		engine.prepare(48000.0, kBlock);
		preparedSr = 48000.0;
		staticSnap.publish(sv);
		loader.attach(this, &heavySlot, [this](const std::string&, Heavy& h) { return build(h); });
#ifdef METAMODULE
		worker.start();
#endif
	}

	void resetArts() {
		static const float tones[] = {45, 55, 50, 55, 50, 55, 45, 50};
		static const float motions[] = {40, 45, 45, 35, 35, 35, 50, 40};
		static const float grits[] = {25, 30, 35, 20, 20, 25, 35, 30};
		for (int a = 0; a < kArts; a++)
			arts[a] = {a, 0.5f, tones[a] * 0.01f, motions[a] * 0.01f, grits[a] * 0.01f};
	}

	// ---- the loader thread: the break -> kit -> learned walk; the bass loop -------------

	bool build(Heavy& out) {
		busy = true;
		bool changed = false;
		const std::string want0 = paths.get(0), want1 = paths.get(1);
		if (want0 != cachedPath[0] || err[0]) {
			cachedPath[0] = want0;
			cachedBreak.reset();
			cachedKit.reset();
			err[0] = false;
			if (!want0.empty() && !loadBreak(want0, cachedBreak))
				err[0] = true;
			changed = true;
		}
		if (want1 != cachedPath[1] || err[1]) {
			cachedPath[1] = want1;
			cachedBass.reset();
			err[1] = false;
			if (!want1.empty()) {
				auto v = std::make_shared<std::vector<float>>();
				if (loadMono(want1, *v, cachedBassRate))
					cachedBass = v;
				else
					err[1] = true;
			}
			changed = true;
		}
		const int bars = kBarValues[clamp(barsA, 0, 3)];
		if (!cachedBreak) {
			busy = false;
			return false;
		}
		if (!cachedKit || bars != cachedBars) {
			dna::KitParams kp;
			kp.barsA = kp.barsB = bars;
			cachedKit = std::shared_ptr<const dna::Kit>(dna::buildKit(cachedBreak, cachedBreak, kp).release());
			cachedBars = bars;
			changed = true;
		}
		(void) changed;
		out.kit = cachedKit;
		out.learned = geno::learn(*cachedKit);
		out.bassMono = cachedBass;
		out.bassRate = cachedBassRate;
		out.bassName = want1.empty() ? std::string() : semi::fileName(want1);
		makeWave(*cachedKit->a, cachedKit->sa.starts, cachedKit->sa.cls, out.wa);
		out.wa.bars = cachedKit->g.bars;
		out.wa.bpm = (float) cachedKit->g.bpm;
		out.serial = ++heavySerial;
		busy = false;
		return true;
	}

	void requestBuild() { loader.request("sync"); }
	void setPath(int i, const std::string& p) {
		paths.set(i, p);
		requestBuild();
	}

	// ---- the worker: bass line + joint walk (debounced), derive, articulation map --------

	JointKey jointKey() {
		JointKey k;
		k.source = source;
		k.style = style;
		k.bbars = bbars;
		k.seed = seed;
		k.skeleton = skeleton;
		k.heavy = hv ? hv->serial : 0;
		k.density = density;
		k.pickup = pickup;
		k.couple = params[COUPLE_PARAM].getValue() * 0.01f;
		return k;
	}

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

	void makeLine(const JointKey& k) {
		const auto& Ln = hv->learned;
		if (k.source == lg::SrcAudio && hv->bassMono && !hv->bassMono->empty()) {
			int bars = kBarValues[clamp(k.bbars, 0, 3)];
			if (bars == 0) {
				const double b = (double) hv->bassMono->size() / hv->bassRate * std::max(60.0, hv->kit->g.bpm) / 240.0;
				bars = b < 1.5 ? 1 : b < 3.0 ? 2 : 4;
			}
			line = lg::fromAudio(*hv->bassMono, hv->bassRate, bars * 16, hv->bassName);
		}
		else if (k.source == lg::SrcStyle)
			line = lg::style(k.style);
		else
			line = lg::fromKicks(Ln, k.density, k.pickup, (uint32_t) k.seed);
		lineKey = k;
		haveLine = true;
	}

	void applyArtMap() {
		if (!cur)
			return;
		const auto a = lg::artMap(cur->joint, artMapIdx);
		for (int s = 0; s < walk::kMaxSteps; s++) {
			brain.bass[s].art.store(a[(size_t) s]);
			sv.art[s] = (int8_t) a[(size_t) s];
		}
		appliedMap = artMapIdx;
	}

	void rebuildJoint(const JointKey& k) {
		if (!haveLine || !lineKey.sameLine(k))
			makeLine(k);
		auto b = std::make_shared<lg::Bundle>();
		b->kit = hv->kit;
		b->learned = hv->learned;
		b->joint = lg::learnJoint(b->learned, line, k.skeleton, k.couple);
		b->key = (uint64_t) hv->serial;
		cur = b;
		genomeP = cur->joint.fromJoint;
		brain.genome.copyFrom(genomeP);
		builtKey = k;
		haveBuilt = true;
		haveD = false;
		applyArtMap();
		const auto& J = cur->joint;
		int r = J.bass.root > 0 ? J.bass.root : 31;
		rootSeen.store(foldRoot(r));
		sv.steps = cur->learned.steps;
		for (int s = 0; s < walk::kMaxSteps; s++) {
			const bool in = s < J.steps;
			sv.mainLane[s] = in ? cur->learned.a[(size_t) s].main : -1;
			sv.bassType[s] = in ? J.bass.cells[(size_t) s].type : 0;
			sv.bassSemis[s] = in ? J.bass.cells[(size_t) s].semis : 0;
		}
		std::snprintf(sv.line, sizeof sv.line, "%s", J.bass.name.empty() ? "LINE" : J.bass.name.c_str());
	}

	void service() {
		liveSlot.collect();
		bool fresh = false;
		if (heavySlot.poll()) {
			hv = heavySlot.active;
			sv.wa = hv->wa;
			fresh = true;
		}
		if (!hv)
			return;
#ifdef METAMODULE
		if (!fresh && nowMs() - lastDeriveMs < 30)
			return;
#endif
		const JointKey k = jointKey();
		bool rebuilt = false;
		if (fresh || !haveBuilt || !builtKey.same(k)) {
			if (!pendingKey.same(k)) {
				pendingKey = k;
				pendingSince = nowMs();
			}
			if (fresh || !haveBuilt || nowMs() - pendingSince > 150) {   // a knob that moves: wait until it rests
				rebuildJoint(k);
				rebuilt = true;
			}
		}
		if (!cur)
			return;
		if (artMapIdx != appliedMap) {
			applyArtMap();
			staticSnap.publish(sv);
		}
		const geno::DeriveParams d = deriveParams();
		if (rebuilt || !haveD || !sameD(d, lastD)) {
			lg::deriveJoint(genomeP, cur->learned, cur->joint, d, builtKey.skeleton, callP, respP, fillP);
			brain.call.copyFrom(callP);
			brain.resp.copyFrom(respP);
			brain.fill.copyFrom(fillP);
			lastD = d;
			haveD = true;
			lastDeriveMs = nowMs();
			makeMatView(callP, respP, fillP, cur->learned.steps, sv.mat);
			sv.mutation = d.mutation;
			sv.couple = builtKey.couple;
			staticSnap.publish(sv);
		}
		if (rebuilt)
			liveSlot.offer(new Live{cur});
	}

	// ---- json -------------------------------------------------------------------------------

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "path", json_string(paths.get(0).c_str()));
		json_object_set_new(root, "bassPath", json_string(paths.get(1).c_str()));
		auto seti = [&](const char* k, int v) { json_object_set_new(root, k, json_integer(v)); };
		auto setf = [&](const char* k, float v) { json_object_set_new(root, k, json_real(v)); };
		seti("bars", barsA);
		seti("source", source);
		seti("style", style);
		seti("bbars", bbars);
		seti("skeleton", skeleton);
		seti("artmap", artMapIdx);
		setf("density", density);
		setf("pickup", pickup);
		seti("ending", ending);
		seti("shift", shift);
		seti("start", startStep);
		seti("reset", resetIdx);
		seti("seed", seed);
		seti("time", timeIdx);
		setf("atemp", atemp);
		setf("roll", roll);
		seti("ansArts", answerArts);
		seti("fillBass", fillBass);
		seti("fillArt", fillArt);
		setf("dyn", dyn);
		setf("decay", decay);
		setf("tune", tune);
		json_object_set_new(root, "mono", json_boolean(mono));
		seti("key", key);
		seti("boct", boct);
		setf("sub", sub);
		setf("xover", xover);
		setf("duck", duck);
		setf("gap", gapMs);
		setf("glide", glideMs);
		setf("xfade", xfadeMs);
		setf("release", releaseMs);
		setf("width", width);
		setf("gate", gate);
		seti("ppqn", ppqnIdx);
		seti("tempo", tempoIdx);
		json_t* a = json_array();
		for (const auto& s : arts) {
			json_t* o = json_array();
			json_array_append_new(o, json_integer(s.model));
			for (float v : {s.level, s.tone, s.motion, s.grit})
				json_array_append_new(o, json_real(v));
			json_array_append_new(a, o);
		}
		json_object_set_new(root, "arts", a);
		json_t* lanes = json_array();
		for (auto& l : brain.lanes)
			json_array_append_new(lanes, json_integer(l.mute.load() | (l.choke.load() << 1)));
		json_object_set_new(root, "lanes", lanes);
		return root;
	}

	void dataFromJson(json_t* root) override {
		jsonInt(root, "bars", barsA, 0, 3);
		jsonInt(root, "source", source, 0, lg::SrcAudio);
		jsonInt(root, "style", style, 0, lg::numStyles() - 1);
		jsonInt(root, "bbars", bbars, 0, 3);
		jsonInt(root, "skeleton", skeleton, 0, lg::NumSkeletons - 1);
		jsonInt(root, "artmap", artMapIdx, 0, lg::NumArtMaps - 1);
		jsonFloat(root, "density", density, 0.f, 1.f);
		jsonFloat(root, "pickup", pickup, 0.f, 1.f);
		jsonInt(root, "ending", ending, 0, geno::NumEndings - 1);
		jsonInt(root, "shift", shift, 0, 31);
		jsonInt(root, "start", startStep, 1, 64);
		jsonInt(root, "reset", resetIdx, 0, 8);
		jsonInt(root, "seed", seed, 1, 9999);
		jsonInt(root, "time", timeIdx, 0, 7);
		jsonFloat(root, "atemp", atemp, 0.25f, 4.f);
		jsonFloat(root, "roll", roll, 0.f, 1.f);
		jsonInt(root, "ansArts", answerArts, 0, 7);
		jsonInt(root, "fillBass", fillBass, 0, lg::NumFillBass - 1);
		jsonInt(root, "fillArt", fillArt, 0, 8);
		jsonFloat(root, "dyn", dyn, 0.f, 1.5f);
		jsonFloat(root, "decay", decay, 0.f, 1.f);
		jsonFloat(root, "tune", tune, -12.f, 12.f);
		jsonBool(root, "mono", mono);
		jsonInt(root, "key", key, 0, 12);
		jsonInt(root, "boct", boct, -2, 2);
		jsonFloat(root, "sub", sub, 0.f, 1.f);
		jsonFloat(root, "xover", xover, 1.5f, 6.f);
		jsonFloat(root, "duck", duck, 0.f, 1.f);
		jsonFloat(root, "gap", gapMs, 0.f, 60.f);
		jsonFloat(root, "glide", glideMs, 0.f, 500.f);
		jsonFloat(root, "xfade", xfadeMs, 0.5f, 40.f);
		jsonFloat(root, "release", releaseMs, 5.f, 800.f);
		jsonFloat(root, "width", width, 0.f, 1.f);
		jsonFloat(root, "gate", gate, 0.05f, 1.f);
		jsonInt(root, "ppqn", ppqnIdx, 0, kNumPpqn - 1);
		jsonInt(root, "tempo", tempoIdx, 0, kNumTempos - 1);
		if (json_t* a = json_object_get(root, "arts"))
			for (int i = 0; i < kArts; i++)
				if (json_t* o = json_array_get(a, i)) {
					auto num = [&](size_t j, float def) { json_t* v = json_array_get(o, j); return v ? (float) json_number_value(v) : def; };
					arts[i].model = clamp((int) num(0, (float) i), 0, neuro::NumModels - 1);
					arts[i].level = clamp(num(1, 0.5f), 0.f, 1.f);
					arts[i].tone = clamp(num(2, 0.5f), 0.f, 1.f);
					arts[i].motion = clamp(num(3, 0.4f), 0.f, 1.f);
					arts[i].grit = clamp(num(4, 0.3f), 0.f, 1.f);
				}
		if (json_t* a = json_object_get(root, "lanes"))
			for (int l = 0; l < geno::NumLanes; l++)
				if (json_t* v = json_array_get(a, l)) {
					const int f = (int) json_integer_value(v);
					brain.lanes[l].mute.store(f & 1);
					brain.lanes[l].choke.store((f >> 1) & 1);
				}
		paths.set(0, jsonStr(root, "path"));
		paths.set(1, jsonStr(root, "bassPath"));
		requestBuild();
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		barsA = 0;
		source = lg::SrcKicks;
		style = bbars = 0;
		skeleton = lg::SkelKickSnare;
		artMapIdx = lg::MapHalves;
		density = 0.8f;
		pickup = 0.3f;
		ending = geno::EndBeat;
		shift = 8;
		startStep = 1;
		resetIdx = 0;
		seed = 1;
		timeIdx = 3;
		atemp = 1.6f;
		roll = 0.6f;
		answerArts = 4;
		fillBass = lg::FillPickup;
		fillArt = 0;
		dyn = decay = 1.f;
		tune = 0.f;
		mono = true;
		key = boct = 0;
		sub = 0.7f;
		xover = 2.5f;
		duck = 0.5f;
		gapMs = 25.f;
		glideMs = 0.f;
		xfadeMs = 4.f;
		releaseMs = 60.f;
		width = 0.35f;
		gate = 1.f;
		resetArts();
		for (int l = 0; l < geno::NumLanes; l++) {
			brain.lanes[l].mute.store(0);
			brain.lanes[l].choke.store(l == geno::LHat || l == geno::LOpen);
		}
		requestBuild();
	}

	// ---- audio ------------------------------------------------------------------------------

	int bassRoot() {
		if (inputs[ROOT_INPUT].isConnected())
			return foldRoot(60 + (int) std::lround(inputs[ROOT_INPUT].getVoltage() * 12.f));
		if (key > 0)
			return foldRoot(key - 1);
		return source == lg::SrcAudio ? rootSeen.load() : 31;
	}

	void runBlock(float sampleRate) {
		liveSlot.poll();
		const lg::Bundle* b = liveSlot.active ? liveSlot.active->b.get() : nullptr;

		lg::Params p;
		p.feel = params[FEEL_PARAM].getValue() * 0.01f;
		p.dyn = dyn;
		p.ghosts = params[GHOSTS_PARAM].getValue() * 0.01f;
		p.decay = decay;
		p.tune = tune;
		p.gain = std::pow(10.f, params[DRUMS_PARAM].getValue() / 20.f);
		p.mono = mono;
		p.form = clamp((int) std::round(params[FORM_PARAM].getValue()), 0, geno::NumForms - 1);
		p.ending = ending;
		p.mult = kTimeValues[clamp(timeIdx, 0, 7)];
		p.swing = params[SWING_PARAM].getValue() * 0.01f;
		p.startStep = startStep;
		p.resetBars = kResetValues[clamp(resetIdx, 0, 8)];
		p.midiOut = true;
		p.seed = (uint32_t) seed;
		p.answerArts = answerArts;
		p.fillBass = fillBass;
		p.fillArt = fillArt;
		p.snareGapMs = gapMs;
		p.duck = duck;
		p.bassLevel = 0.42f * std::pow(10.f, params[BASS_PARAM].getValue() / 20.f);
		p.bassRoot = bassRoot();
		auto& bp = p.bassP;
		for (int a = 0; a < kArts; a++) {
			auto& ap = bp.art[(size_t) a];
			ap.model = arts[a].model;
			ap.level = arts[a].level;
			ap.tone = arts[a].tone;
			ap.motion = arts[a].motion;
			ap.grit = arts[a].grit;
			ap.choke = true;
		}
		bp.octave = boct;
		bp.glideMs = glideMs;
		bp.xfadeMs = xfadeMs;
		bp.releaseMs = releaseMs;
		bp.xover = xover;
		bp.sub = sub;
		bp.drive = params[DRIVE_PARAM].getValue() * 0.01f;
		bp.width = width;
		bp.gate = std::max(0.05f, gate);
		bp.duckRelMs = 100.f;

		if (blockClocked) {
			p.run = geno::RunHost;
			p.playing = true;
			p.bpm = blockBpm;
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
		engine.process(b, brain, p, nullptr, 0, outBuf[0], outBuf[1], kBlock, midiOut, numOut, midiCh);
		outEv.clear();
		for (int i = 0; i < numOut; i++) {
			const auto& m = midiOut[i];
			if (midiCh[i] == p.bassChannel)
				outEv.add(m.offset, 2, m.vel > 0, (float) m.note);
			else if (m.vel > 0) {
				const int o = m.note == 36 ? 0 : (m.note == 38 || m.note == 37 || m.note == 39) ? 1 : -1;
				if (o >= 0)
					outEv.add(m.offset, o, true, m.vel / 127.f);
			}
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
		double ppq = 0.0, bpm = 174.0;
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
		for (int i = 0; i < playEv.n; i++) {
			const auto& e = playEv.ev[i];
			if (e.at != pos)
				continue;
			if (e.out < 2)
				trigLeft[e.out] = 0.005f;
			else if (e.on) {
				bassHeld = (int) e.v;
				pitchV = (e.v - 60.f) / 12.f;
			}
			else if ((int) e.v == bassHeld)
				bassHeld = -1;
		}
		for (int o = 0; o < 2; o++) {
			outputs[K_OUTPUT + o].setVoltage(trigLeft[o] > 0.f ? 10.f : 0.f);
			trigLeft[o] -= args.sampleTime;
		}
		gateOut = bassHeld >= 0;
		outputs[PITCH_OUTPUT].setVoltage(pitchV);
		outputs[GATE_OUTPUT].setVoltage(gateOut ? 10.f : 0.f);
		outputs[STEP_OUTPUT].setVoltage(stepVolts);

		if (++pos >= kBlock) {
			pos = 0;
			runBlock(args.sampleRate);
			playEv = outEv;
		}
		if (ui.tick(args.sampleTime))
			publish();
	}

	void publish() {
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
		lg::BassViewEvent be;
		while (engine.popBass(be)) {
		}
		const bool halted = engine.viewHalted.load(std::memory_order_relaxed) != 0;
		lights[HALT_LIGHT].setBrightness(halted ? 1.f : 0.f);

		Snapshot& s = snap.writeSlot();
		s.ball = engine.viewStep.load(std::memory_order_relaxed);
		s.role = engine.viewRole.load(std::memory_order_relaxed);
		s.art = engine.viewArt.load(std::memory_order_relaxed);
		s.lit = lit;
		for (int i = 0; i < nMoves; i++)
			s.moves[i] = moves[i];
		s.nMoves = nMoves;
		s.pos16 = (double) engine.viewTick.load(std::memory_order_relaxed) * kTimeValues[clamp(timeIdx, 0, 7)];
		s.form = clamp((int) std::round(params[FORM_PARAM].getValue()), 0, geno::NumForms - 1);
		s.answer = clamp((int) std::round(params[ANSWER_PARAM].getValue()), 0, geno::NumAnswers - 1);
		s.bpm = (float) engine.viewBpm.load(std::memory_order_relaxed);
		s.running = engine.viewPlaying.load(std::memory_order_relaxed) != 0;
		s.halted = halted;
		s.busy = busy.load();
		s.errA = err[0].load();
		s.errB = err[1].load();
		s.gate = gateOut;
		snap.publish();
	}
};

// ---------------------------------------------------------------------------------- screens

namespace {

StaticView& demoView() {
	static StaticView v;
	static bool made = false;
	if (made)
		return v;
	made = true;
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
	v.wa.bpm = 174.f;
	std::snprintf(v.wa.name, sizeof v.wa.name, "amen.wav");
	std::snprintf(v.line, sizeof v.line, "FROM KICKS");
	v.steps = 32;
	static const int8_t lanes[16] = {0, -1, 0, -1, 1, -1, 3, 2, -1, 2, 0, -1, 1, -1, 3, -1};
	static const int8_t bt[16] = {1, 2, 1, 2, 0, 0, 0, 0, 0, 0, 1, 2, 0, 0, 1, 2};
	static const int8_t bs[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 7, 0, 0, 12, 12};
	for (int s = 0; s < 32; s++) {
		v.mainLane[s] = lanes[s % 16];
		v.bassType[s] = bt[s % 16];
		v.bassSemis[s] = bs[s % 16];
		v.art[s] = (int8_t) (s < 16 ? 1 : 2);
	}
	static walk::Plain c{}, r{}, f{};   // static: kept off the GUI thread's stack
	for (int x = 1; x <= 32; x++) {
		c[x][x % 32 + 1] = 100.f;
		c[x][(x + 15) % 32 + 1] = (x % 4 == 1) ? 50.f : 0.f;
		r[x][(x + 8) % 32 + 1] = 100.f;
		f[x][x] = (x % 8 == 5) ? 100.f : 0.f;
		f[x][x % 32 + 1] = 40.f;
	}
	makeMatView(c, r, f, 32, v.mat);
	v.mutation = 0.4f;
	v.couple = 0.7f;
	return v;
}

Snapshot demoSnap() {
	Snapshot s;
	s.ball = 10;
	s.role = geno::RoleCall;
	s.lit = 5;
	s.art = 0;
	const Move m[3] = {{8, 8, 0}, {9, 25, 0}, {26, 10, 0}};
	for (int i = 0; i < 3; i++)
		s.moves[i] = m[i];
	s.nMoves = 3;
	s.pos16 = 20.0;
	s.running = true;
	return s;
}

} // namespace

// The break (slices by class, the one that plays framed) over the bass line as a piano roll
// (notes as bars at their pitch, held cells joined), the ball framed on both.
struct LoopScreen : Screen {
	Loopgenome* module = nullptr;
	Snapshot demo = demoSnap();

	void drawScreen(NVGcontext* vg) override {
		const StaticView& v = module ? module->staticSnap.read() : demoView();
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y, pad = 3.f;
		char buf[48];
		std::snprintf(buf, sizeof buf, "%s", s.errA ? "LOAD FAILED" : v.wa.valid ? v.wa.name : "load a break (menu)");
		text(vg, pad, 2.f, buf, 9.f, s.errA ? semi::theme::magenta() : semi::theme::cyan());
		if (s.busy)
			text(vg, w - pad, 2.f, "LEARNING", 9.f, semi::theme::amber(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		else
			text(vg, w - pad, 2.f, s.errB ? "BASS LOAD FAILED" : v.line, 9.f, s.errB ? semi::theme::magenta() : semi::theme::text(),
			     NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		const float wy = 13.f, wh = (h - wy - pad) * 0.5f;
		drawWave(*this, vg, v.wa, pad, wy, w - 2 * pad, wh - 1.f, s.lit, false);

		// the bass roll
		const float ry = wy + wh + 1.f, rh = h - ry - pad;
		const int n = v.steps;
		if (n <= 0)
			return;
		int lo = 99, hi = -99;
		for (int st = 0; st < n; st++)
			if (v.bassType[st] != lg::BRest) {
				lo = std::min(lo, (int) v.bassSemis[st]);
				hi = std::max(hi, (int) v.bassSemis[st]);
			}
		if (lo > hi)
			lo = hi = 0;
		const int span = std::max(12, hi - lo);
		const float cw = (w - 2 * pad) / n, nh = std::max(2.5f, rh / 8.f);
		fillRect(vg, pad, ry, w - 2 * pad, rh, semi::theme::cell());
		for (int st = 0; st < n; st++) {
			const float x = pad + st * cw;
			if (st % 4 == 0)
				fillRect(vg, x, ry, 1.f, rh, semi::theme::grid());
			if (v.bassType[st] == lg::BRest)
				continue;
			const float y = ry + rh - nh - (rh - nh) * (v.bassSemis[st] - lo) / span;
			const bool tie = v.bassType[st] == lg::BTie;
			fillRect(vg, tie ? x - 0.6f : x + 0.6f, y, tie ? cw + 0.6f : std::max(1.f, cw - 1.2f), nh,
			         st == s.ball && s.running ? semi::theme::magenta() : semi::theme::cyan());
		}
		if (s.running && s.ball >= 0 && s.ball < n) {
			nvgBeginPath(vg);
			nvgRect(vg, pad + s.ball * cw - 0.5f, ry, std::max(2.f, cw + 1.f), rh);
			nvgStrokeColor(vg, roleColor(s.role));
			nvgStrokeWidth(vg, semi::platform::kStroke);
			nvgStroke(vg);
		}
	}
};

// The joint walk's matrix (CALL / ANSWER / FILL by the place in the form), the ball's row and
// its last moves; on the right the role, MUTATION, COUPLE, the articulation that sounds and the
// articulation row (one cell per step), the form bar by bar.
struct MatrixScreen : Screen {
	Loopgenome* module = nullptr;
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
		std::snprintf(buf, sizeof buf, "CPL %d%%", (int) std::lround(v.couple * 100.f));
		text(vg, tx, pad + 24.f, buf, 9.f, semi::theme::text());
		const int model = module && s.art >= 0 && s.art < kArts ? module->arts[s.art].model : s.art;
		std::snprintf(buf, sizeof buf, "ART %s", model >= 0 ? neuro::modelShort(model) : "--");
		text(vg, tx, pad + 34.f, buf, 9.f, s.gate ? semi::theme::cyan() : semi::theme::dim());

		// the articulation row: 1..8 as shades, '.' (hold) blank
		const int n = v.steps;
		const float ay = h - pad - 19.f, ah = 6.f;
		if (n > 0) {
			const float cw = tw / n;
			for (int st = 0; st < n; st++) {
				const int a = v.art[st];
				if (a <= 0)
					continue;
				fillRect(vg, tx + st * cw, ay, std::max(0.8f, cw - 0.4f), ah,
				         a == s.art + 1 ? semi::theme::cyan() : (a % 2) ? semi::theme::cyanDim() : semi::theme::cyanDeep());
			}
		}
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

struct LoopgenomeWidget : ModuleWidget {
	LoopgenomeWidget(Loopgenome* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Loopgenome.svg")));

		auto* loop = semi::createScreenMm<LoopScreen>(Vec(L::LOOP.x, L::LOOP.y), Vec(L::LOOP.w, L::LOOP.h));
		loop->module = module;
		addChild(loop);
		auto* mat = semi::createScreenMm<MatrixScreen>(Vec(L::MATRIX.x, L::MATRIX.y), Vec(L::MATRIX.w, L::MATRIX.h));
		mat->module = module;
		addChild(mat);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		auto snapKnob = [&](app::ParamWidget* k) {
			if (auto* kn = dynamic_cast<app::Knob*>(k))
				kn->snap = true;
			addParam(k);
		};
		addParam(createParamCentered<semi::Button>(at(L::RESET), module, Loopgenome::RESET_PARAM));
		addChild(createLightCentered<semi::AmberLedSmall>(at(L::HALT_LED), module, Loopgenome::HALT_LIGHT));
		addParam(createParamCentered<semi::KnobMHot>(at(L::MUTATION), module, Loopgenome::MUTATION_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::TEMP), module, Loopgenome::TEMP_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::COUPLE), module, Loopgenome::COUPLE_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::DRUMS), module, Loopgenome::DRUMS_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::BASS), module, Loopgenome::BASS_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::FEEL), module, Loopgenome::FEEL_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::GHOSTS), module, Loopgenome::GHOSTS_PARAM));
		snapKnob(createParamCentered<semi::KnobS>(at(L::FORM), module, Loopgenome::FORM_PARAM));
		snapKnob(createParamCentered<semi::KnobS>(at(L::ANSWER), module, Loopgenome::ANSWER_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::SWING), module, Loopgenome::SWING_PARAM));
		addParam(createParamCentered<semi::KnobSHot>(at(L::DRIVE), module, Loopgenome::DRIVE_PARAM));

		addInput(createInputCentered<semi::Jack>(at(L::CLK), module, Loopgenome::CLK_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RST), module, Loopgenome::RST_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::HALT), module, Loopgenome::HALT_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::MUT_CV), module, Loopgenome::MUT_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::TEMP_CV), module, Loopgenome::TEMP_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::ROOT), module, Loopgenome::ROOT_INPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_L), module, Loopgenome::L_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_R), module, Loopgenome::R_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::K_OUT), module, Loopgenome::K_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::S_OUT), module, Loopgenome::S_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::PITCH_OUT), module, Loopgenome::PITCH_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::GATE_OUT), module, Loopgenome::GATE_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::STEP_OUT), module, Loopgenome::STEP_OUTPUT));
	}

#ifndef METAMODULE
	void step() override {
		if (auto* m = dynamic_cast<Loopgenome*>(module))
			m->service();
		ModuleWidget::step();
	}
#endif

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Loopgenome*>(module);
		if (!m)
			return;
		auto rebuild = [=]() { m->requestBuild(); };
		menu->addChild(new MenuSeparator);
		const std::string a = m->paths.get(0), b = m->paths.get(1);
		menu->addChild(createMenuItem("Load break...", a.empty() ? "" : semi::fileName(a), [=]() {
			semi::openFileDialog("WAV:wav,WAV", semi::dirName(m->paths.get(0)), [=](const std::string& p) { m->setPath(0, p); });
		}));
		menu->addChild(createMenuItem("Load bass loop (WAV)...", b.empty() ? "" : semi::fileName(b), [=]() {
			const std::string cur = m->paths.get(1).empty() ? m->paths.get(0) : m->paths.get(1);
			semi::openFileDialog("WAV:wav,WAV", semi::dirName(cur), [=](const std::string& p) {
				m->setPath(1, p);
				m->source = lg::SrcAudio;
			});
		}));
		menu->addChild(intMenu("Bars", std::vector<std::string>(kBarNames, kBarNames + 4), &m->barsA, rebuild));

		menu->addChild(createSubmenuItem("Bass line", "", [=](Menu* sub) {
			sub->addChild(intMenu("Source", {"Style", "From the kicks", "Audio loop"}, &m->source));
			std::vector<std::string> styles;
			for (int i = 0; i < lg::numStyles(); i++)
				styles.push_back(lg::styleName(i));
			sub->addChild(intMenu("Style", styles, &m->style));
			sub->addChild(floatMenu("Kick density", &m->density, 0.f, 1.f, 11, 100.f, "%"));
			sub->addChild(floatMenu("Pickups", &m->pickup, 0.f, 1.f, 11, 100.f, "%"));
			sub->addChild(intMenu("Bass loop bars", std::vector<std::string>(kBarNames, kBarNames + 4), &m->bbars));
		}));
		menu->addChild(createSubmenuItem("Joint walk", "", [=](Menu* sub) {
			sub->addChild(intMenu("Skeleton", {"Kick + snare", "Kick", "Off"}, &m->skeleton));
			sub->addChild(intMenu("Articulation map", {"Halves", "Bars", "Notes", "Drums", "One"}, &m->artMapIdx));
			sub->addChild(intMenu("Time (the ball's tick)", std::vector<std::string>(kTimeNames, kTimeNames + 8), &m->timeIdx));
			std::vector<std::string> st;
			for (int i = 1; i <= 64; i++)
				st.push_back(string::f("%d", i));
			sub->addChild(createIndexSubmenuItem("Start step", st, [=]() { return (size_t) (m->startStep - 1); },
			                                     [=](size_t i) { m->startStep = (int) i + 1; }));
			sub->addChild(intMenu("Reset every", std::vector<std::string>(kResetNames, kResetNames + 9), &m->resetIdx));
			sub->addChild(createMenuItem("New seed (another walk and line)", string::f("%d", m->seed),
			                             [=]() { m->seed = 1 + (int) (random::u32() % 9999u); }));
		}));
		menu->addChild(createSubmenuItem("Call & response", "", [=](Menu* sub) {
			sub->addChild(intMenu("Ending (the fill)", std::vector<std::string>(kEndingNames, kEndingNames + 4), &m->ending));
			sub->addChild(floatMenu("Answer temperature", &m->atemp, 0.25f, 4.f, 16, 1.f, ""));
			std::vector<std::string> sh;
			for (int i = 0; i < 32; i++)
				sh.push_back(string::f("+%d", i));
			sub->addChild(intMenu("Answer shift (steps)", sh, &m->shift));
			sub->addChild(floatMenu("Roll (the fill)", &m->roll, 0.f, 1.f, 11, 100.f, "%"));
			sub->addChild(intMenu("Answer articulations", {"Same", "+1", "+2", "+3", "+4", "+5", "+6", "+7"}, &m->answerArts));
			sub->addChild(intMenu("Fill bass", {"Follow", "Pickup", "Drop"}, &m->fillBass));
			sub->addChild(intMenu("Fill articulation", {"As the step", "1", "2", "3", "4", "5", "6", "7", "8"}, &m->fillArt));
		}));
		menu->addChild(createSubmenuItem("Articulations", "", [=](Menu* sub) {
			std::vector<std::string> models;
			for (int i = 0; i < neuro::NumModels; i++)
				models.push_back(neuro::modelName(i));
			for (int i = 0; i < kArts; i++) {
				sub->addChild(createSubmenuItem(string::f("Slot %d", i + 1), neuro::modelName(m->arts[i].model), [=](Menu* am) {
					am->addChild(intMenu("Model", models, &m->arts[i].model));
					am->addChild(floatMenu("Level", &m->arts[i].level, 0.f, 1.f, 11, 100.f, "%"));
					am->addChild(floatMenu("Tone", &m->arts[i].tone, 0.f, 1.f, 11, 100.f, "%"));
					am->addChild(floatMenu("Motion", &m->arts[i].motion, 0.f, 1.f, 11, 100.f, "%"));
					am->addChild(floatMenu("Grit", &m->arts[i].grit, 0.f, 1.f, 11, 100.f, "%"));
				}));
			}
		}));
		menu->addChild(createSubmenuItem("Bass sound", "", [=](Menu* sub) {
			sub->addChild(intMenu("Key (ROOT unpatched)", {"Auto (G, or the bass loop's)", "C", "C#", "D", "D#", "E", "F", "F#", "G",
			                                               "G#", "A", "A#", "B"},
			                      &m->key));
			sub->addChild(createIndexSubmenuItem("Octave", {"-2", "-1", "0", "+1", "+2"}, [=]() { return (size_t) (m->boct + 2); },
			                                     [=](size_t i) { m->boct = (int) i - 2; }));
			sub->addChild(floatMenu("Sub", &m->sub, 0.f, 1.f, 11, 100.f, "%"));
			sub->addChild(floatMenu("Crossover (x f0)", &m->xover, 1.5f, 6.f, 10, 1.f, ""));
			sub->addChild(floatMenu("Kick duck", &m->duck, 0.f, 1.f, 11, 100.f, "%"));
			sub->addChild(floatMenu("Snare gap", &m->gapMs, 0.f, 60.f, 13, 1.f, " ms"));
			sub->addChild(floatMenu("Glide", &m->glideMs, 0.f, 500.f, 11, 1.f, " ms"));
			sub->addChild(floatMenu("Switch (crossfade)", &m->xfadeMs, 0.5f, 40.f, 9, 1.f, " ms"));
			sub->addChild(floatMenu("Release", &m->releaseMs, 5.f, 800.f, 11, 1.f, " ms"));
			sub->addChild(floatMenu("Width", &m->width, 0.f, 1.f, 11, 100.f, "%"));
			sub->addChild(floatMenu("Gate (note length)", &m->gate, 0.1f, 1.f, 10, 100.f, "%"));
		}));
		menu->addChild(createSubmenuItem("Drums", "", [=](Menu* sub) {
			sub->addChild(floatMenu("Dynamics", &m->dyn, 0.f, 1.5f, 16, 100.f, "%"));
			sub->addChild(floatMenu("Decay", &m->decay, 0.f, 1.f, 11, 100.f, "%"));
			sub->addChild(floatMenu("Tune", &m->tune, -12.f, 12.f, 25, 1.f, " st"));
			sub->addChild(createBoolPtrMenuItem("Mono (every hit cuts the last)", "", &m->mono));
			for (int l = 0; l < geno::NumLanes; l++)
				sub->addChild(createSubmenuItem(string::f("Lane %s", geno::laneName(l)), "", [=](Menu* lm) {
					lm->addChild(createBoolMenuItem("Mute", "", [=]() { return m->brain.lanes[l].mute.load() != 0; },
					                                [=](bool v) { m->brain.lanes[l].mute.store(v ? 1 : 0); }));
					lm->addChild(createBoolMenuItem("Choke group", "", [=]() { return m->brain.lanes[l].choke.load() != 0; },
					                                [=](bool v) { m->brain.lanes[l].choke.store(v ? 1 : 0); }));
				}));
		}));
		menu->addChild(intMenu("Clock input", ppqnNames(), &m->ppqnIdx));
		menu->addChild(intMenu("Tempo without CLK", tempoNames(), &m->tempoIdx));
	}
};

Model* modelLoopgenome = createModel<Loopgenome, LoopgenomeWidget>("Loopgenome");
