// SETBOOK — Rack / MetaModule port of SETBOOK, the family's scene memory (engine: engines/setbook, the
// morph rules SetbookMorph.h and the cue clock Book.* vendored unchanged from semierendonmez-commits/setbook).
//
// In the DAW SETBOOK asks every sibling for its state over a shared-memory wire. Here (both platforms) it
// is a CV scene memory for the cable family:
//   * 8 scenes x 8 CV channels. A channel's source is its knob + its input; the outputs follow the sources
//     (LIVE) until a scene is recalled, then hold it. Moving a knob gives that channel back to LIVE
//     (the plug-in's "a knob the user grabs is let go").
//   * STORE: the plug-in's CAPTURE (the selected scene if empty, else the next empty one, else the selected
//     one); hold STORE and press a scene button to store straight into it.
//   * recall = a timed morph: TIME in beats of the BUS tempo (0 = cut), CURVE (LINEAR / SMOOTH / EARLY /
//     LATE), THRESH for stepped (semitone) and gate channels, LAUNCH quantised to the BUS bar (menu).
//   * scene select: the buttons, SCN CV (1 V per scene; with RCL patched only on a RCL trigger), NXT,
//     the BUS SCENE channel (EPOCH's sections), EPOCH turns (menu), and cues on the BUS POS 32-bar cycle.
//   * PAD: X / Y morph between four corner scenes (SetbookMorph's bilinear corners).
// Rack only (#ifndef METAMODULE): FAMILY — the same scenes also capture / recall / morph the params of every
// SEMI module in the patch through the engine (like stoermelder 8FACE): the member list is gathered on the
// UI thread, params are read and written on the engine thread with getModule_NoLock().

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/FamilyBusCV.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/SetbookLayout.hpp"
#include "setbook/Source/engine/Book.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

namespace L = semi::layout::setbook;
using semi::Screen;

namespace {

constexpr int kScenes = 8;
constexpr int kCh = 8;
constexpr int kMaxCues = 16;
constexpr double kCycleQ = 128.0;   // the BUS POS cycle: 32 bars of 4 quarter notes

enum ChKind { CH_CV, CH_SEMI, CH_GATE };
const char* const kKindNames[3] = {"CV (glide)", "Semitones (stepped)", "Gate (switch)"};
const float kLaunchQ[] = {0.f, 1.f, 4.f, 8.f, 16.f};
const float kTempos[] = {90.f, 120.f, 140.f, 160.f, 172.f, 174.f};
constexpr int kNumTempos = 6;
const int kTurnLines[] = {0, 4, 8, 16, 32};

float beatsFromKnob(float k) { return 64.f * k * k; }

struct CardView {
	bool used = false;
	bool fam = false;
	char name[12]{};
	float norm[kCh]{};
	float dist = -1.f;
};

struct Snapshot {
	CardView card[kScenes];
	int selected = 0, target = -1, from = -1;
	float x = 1.f;
	bool morphing = false, waiting = false, padOn = false;
	float padX = 0.f, padY = 0.f;
	int corner[4]{0, 1, 2, 3};
	float norm[kCh]{};
	bool live[kCh]{};
	float bpm = 120.f;
	int tempoSrc = 0;
	bool busMoving = false;
	float bar = 0.f;
	int nCues = 0;
	float cueBar[kMaxCues]{};
	int cueScene[kMaxCues]{};
	float timeBeats = 4.f;
	int curve = 1;
	float thresh = 0.5f;
	int members = -1;   // Rack: SEMI modules in the patch (-1: MetaModule)
	int memberParams = 0;
};

#ifndef METAMODULE
// ---- FAMILY (Rack only) -------------------------------------------------------------------------
constexpr int kMaxMembers = 24;
constexpr int kMaxParams = 48;

struct FamEntry {
	int64_t id = -1;
	int np = 0;
	float v[kMaxParams]{};            // normalised 0..1
	setbook::Desc desc[kMaxParams];
};
struct FamScene {
	int n = 0;
	FamEntry e[kMaxMembers];
	const FamEntry* find(int64_t id) const {
		for (int i = 0; i < n; i++)
			if (e[i].id == id)
				return &e[i];
		return nullptr;
	}
};
struct FamArmed {
	int64_t id = -1;
	int np = 0;
	setbook::Desc desc[kMaxParams];
	float base[kMaxParams]{};
	float corner[4][kMaxParams]{};
	uint8_t mask = 0;
	bool released[kMaxParams]{};
	float last[kMaxParams]{};
};
#endif

} // namespace

struct Setbook : Module {
	enum ParamId {
		ENUMS(SCENE_PARAM, kScenes), ENUMS(CH_PARAM, kCh),
		STORE_PARAM, LIVE_PARAM, PAD_PARAM, TIME_PARAM, CURVE_PARAM, THRESH_PARAM,
		PARAMS_LEN
	};
	enum InputId { ENUMS(CH_INPUT, kCh), BUS_INPUT, SCN_INPUT, RCL_INPUT, NXT_INPUT, X_INPUT, Y_INPUT, INPUTS_LEN };
	enum OutputId { ENUMS(CH_OUTPUT, kCh), BUS_OUTPUT, END_OUTPUT, MORPH_OUTPUT, OUTPUTS_LEN };
	enum LightId { ENUMS(SCENE_LIGHT, kScenes), PAD_LIGHT, LIGHTS_LEN };

	// ---- the book (UI thread writes on menu / JSON; the audio thread on STORE)
	struct CableScene {
		bool used = false;
		float v[kCh]{};
		char name[12]{};
	};
	CableScene scenes[kScenes];
	sb::Cue cues[kMaxCues];
	std::atomic<int> nCues{0};

	// settings (menu + JSON)
	int kind[kCh]{};
	int bipolar[kCh]{};
	int launch = 0;          // index into kLaunchQ
	int tempoIndex = 1;
	bool steppedWalk = false;
	bool busSceneRecalls = true;
	int turnNext = 0;        // index into kTurnLines (0 off)
	bool cuesOn = true;
	bool chase = true;
	int busSceneOut = 1;     // 0 thru, 1 SETBOOK's scene
	int padCorner[4] = {0, 1, 2, 3};
	bool padOn = false;
#ifndef METAMODULE
	bool famOn = true;
	bool famSystem = false;  // include CONDUIT / BLACKBOX
#endif

	// ---- audio thread
	float cur[kCh]{};        // volts on the outputs
	bool live[kCh];
	float knobLast[kCh];
	int selected = 0;
	struct Morph {
		bool active = false;
		int target = -1, from = -1;
		float x = 1.f, beats = 0.f;
		uint8_t curve = setbook::CurveSmooth;
		float base[kCh]{};   // normalised, where each channel was at the recall
	} morph;
	int pendingScene = -1;
	double pendingPpq = 0.0;
	float pendingBeats = 0.f;
	int pendingCurve = -1;
	dsp::BooleanTrigger sceneBtn[kScenes], liveBtn, padBtn;
	bool storeHeld = false, storeUsed = false;
	dsp::SchmittTrigger rclTrig, nxtTrig;
	int scnLast = -1;
	int busSceneLast = -1;
	float eventPrev = 0.f;
	dsp::PulseGenerator endPulse;
	bool xyWas = false;
	float padBase[kCh]{};

	// tempo / position from the BUS
	float posPrevBars = 0.f;
	double posCycles = 0.0;
	float posIdle = 10.f, posMeasureT = 0.f, posMeasureBars = 0.f, busBpm = 0.f;
	double ppq = 0.0, prevPpq = 0.0;
	bool playing = false, wasPlaying = false;
	float bpm = 120.f;
	int tempoSrc = 0;

	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;

#ifndef METAMODULE
	// FAMILY: member list (UI thread -> audio thread, seqlock), scenes, the armed morph
	engine::Engine* eng = nullptr;
	std::atomic<uint32_t> memSeq{0};
	int64_t memIds[kMaxMembers];
	int memCount = 0;
	int64_t audioIds[kMaxMembers];
	int audioCount = 0;
	uint32_t audioSeq = ~0u;
	std::vector<FamScene> fam = std::vector<FamScene>(kScenes);
	std::vector<FamArmed> armed = std::vector<FamArmed>(kMaxMembers);
	int nArmed = 0;
	bool famPad = false;
	int famDiv = 0;
	std::atomic<int> famParams{0};
#endif

	Setbook() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int i = 0; i < kScenes; i++)
			configButton(SCENE_PARAM + i, string::f("Scene %d (hold STORE: store into it)", i + 1));
		for (int c = 0; c < kCh; c++)
			configParam(CH_PARAM + c, 0.f, 1.f, 0.f, string::f("Channel %d", c + 1), " V", 0.f, 10.f)->description =
			    "The channel's source (+ its input). Moving it gives the channel back to LIVE";
		configButton(STORE_PARAM, "Store (capture the outputs into a scene)");
		configButton(LIVE_PARAM, "Live: every output follows its knob + input");
		configButton(PAD_PARAM, "Pad on / off (X / Y morph of the four corner scenes)");
		configParam(TIME_PARAM, 0.f, 1.f, 0.25f, "Morph time", " beats", 0.f, 1.f)->description = "0 = a cut";
		configSwitch(CURVE_PARAM, 0.f, 3.f, 1.f, "Morph curve", {"LINEAR", "SMOOTH", "EARLY", "LATE"});
		configParam(THRESH_PARAM, 0.f, 1.f, 0.5f, "Threshold", " %", 0.f, 100.f)->description =
		    "Where in the morph gate and stepped channels jump";
		for (int c = 0; c < kCh; c++)
			configInput(CH_INPUT + c, string::f("Channel %d", c + 1))->description = "Added to the knob";
		configInput(BUS_INPUT, "Bus")->description = "FamilyBus, 4 channels: POS (tempo, launch, cues), ENERGY, SCENE (recalls), EVENT";
		configInput(SCN_INPUT, "Scene CV")->description = "1 V per scene (0 V = scene 1). Recalls on change, or on RCL when RCL is patched";
		configInput(RCL_INPUT, "Recall trigger");
		configInput(NXT_INPUT, "Next scene trigger");
		configInput(X_INPUT, "Pad X")->description = "0-10 V across the pad (corners A-B)";
		configInput(Y_INPUT, "Pad Y")->description = "0-10 V across the pad (corners A-C)";
		for (int c = 0; c < kCh; c++)
			configOutput(CH_OUTPUT + c, string::f("Channel %d", c + 1));
		configOutput(BUS_OUTPUT, "Bus")->description = "BUS IN passed through; SCENE = SETBOOK's scene (menu)";
		configOutput(END_OUTPUT, "End of morph")->description = "Trigger when a morph arrives";
		configOutput(MORPH_OUTPUT, "Morph position")->description = "0-10 V while a morph travels";
		for (int i = 0; i < kScenes; i++)
			configLight(SCENE_LIGHT + i, string::f("Scene %d", i + 1));
		configLight(PAD_LIGHT, "Pad");
		for (int i = 0; i < kScenes; i++)
			std::snprintf(scenes[i].name, sizeof scenes[i].name, "%s", sb::defaultSceneName(i));
		for (int c = 0; c < kCh; c++) {
			live[c] = true;
			knobLast[c] = -1.f;
		}
	}

	// ---- channels ----------------------------------------------------------------------------

	setbook::Desc desc(int c) const {
		setbook::Desc d;
		d.id = (uint32_t) c + 1;
		d.kind = kind[c] == CH_GATE ? setbook::Switch : kind[c] == CH_SEMI ? setbook::Stepped : setbook::Continuous;
		d.steps = kind[c] == CH_GATE ? 2 : kind[c] == CH_SEMI ? 121 : 0;
		return d;
	}
	float toNorm(int c, float v) const {
		if (kind[c] == CH_GATE)
			return v > 1.f ? 1.f : 0.f;
		return clamp(bipolar[c] ? (v + 5.f) / 10.f : v / 10.f, 0.f, 1.f);
	}
	float toVolts(int c, float n) const {
		if (kind[c] == CH_GATE)
			return n >= 0.5f ? 10.f : 0.f;
		return bipolar[c] ? n * 10.f - 5.f : n * 10.f;
	}
	float source(int c) {
		const float k = params[CH_PARAM + c].getValue();
		float v = bipolar[c] ? k * 10.f - 5.f : k * 10.f;
		if (inputs[CH_INPUT + c].isConnected())
			v += inputs[CH_INPUT + c].getVoltage();
		return bipolar[c] ? clamp(v, -5.f, 5.f) : clamp(v, 0.f, 10.f);
	}
	setbook::Policy policy(int curve) {
		setbook::Policy p;
		p.curve = (uint8_t) clamp(curve, 0, 3);
		p.steppedWalk = steppedWalk;
		p.threshold = params[THRESH_PARAM].getValue();
		return p;
	}

	// ---- store / recall (audio thread) ---------------------------------------------------------

	int captureTarget() const {
		if (!scenes[selected].used)
			return selected;
		for (int k = 1; k < kScenes; k++) {
			const int s = (selected + k) % kScenes;
			if (!scenes[s].used)
				return s;
		}
		return selected;
	}

	void store(int s) {
		for (int c = 0; c < kCh; c++)
			scenes[s].v[c] = cur[c];
		scenes[s].used = true;
		selected = s;
#ifndef METAMODULE
		famCapture(s);
#endif
	}

	void startRecall(int s, float beats, int curve) {
		morph.active = true;
		morph.from = morph.target >= 0 ? morph.target : -1;
		morph.target = s;
		morph.x = 0.f;
		morph.beats = beats;
		morph.curve = (uint8_t) clamp(curve, 0, 3);
		for (int c = 0; c < kCh; c++) {
			morph.base[c] = toNorm(c, cur[c]);
			live[c] = false;
			knobLast[c] = params[CH_PARAM + c].getValue();
		}
		selected = s;
		padOn = false;
#ifndef METAMODULE
		famArm(s, -1);
#endif
		if (beats <= 0.f)
			stepMorph(0.f);
	}

	// beats < 0: the TIME knob; curve < 0: the CURVE knob
	void recall(int s, float beats = -1.f, int curve = -1) {
		if (s < 0 || s >= kScenes || !scenes[s].used)
			return;
		if (beats < 0.f)
			beats = beatsFromKnob(params[TIME_PARAM].getValue());
		if (curve < 0)
			curve = (int) std::round(params[CURVE_PARAM].getValue());
		const float q = kLaunchQ[clamp(launch, 0, 4)];
		if (q > 0.f && playing) {
			pendingScene = s;
			pendingPpq = sb::launchPpq(ppq, q, true);
			pendingBeats = beats;
			pendingCurve = curve;
			selected = s;
			return;
		}
		pendingScene = -1;
		startRecall(s, beats, curve);
	}

	void stepMorph(float sampleTime) {
		if (!morph.active)
			return;
		if (morph.beats > 0.f)
			morph.x += sampleTime * bpm / 60.f / morph.beats;
		else
			morph.x = 1.f;
		const bool arrived = morph.x >= 1.f;
		morph.x = std::min(morph.x, 1.f);
		const CableScene& sc = scenes[morph.target];
		const setbook::Policy pol = policy(morph.curve);
		for (int c = 0; c < kCh; c++) {
			if (live[c])
				continue;
			const float corner[4] = {morph.base[c], toNorm(c, sc.v[c]), 0.f, 0.f};
			const setbook::Desc d = desc(c);
			const float n = setbook::blend(d, setbook::resolve(d, setbook::RuleAuto, pol), morph.base[c], corner, 3, morph.x, 0.f, pol);
			cur[c] = toVolts(c, n);
		}
		if (arrived) {
			morph.active = false;
			endPulse.trigger(1e-3f);
		}
	}

	void stepPad() {
		const float x = clamp(inputs[X_INPUT].getVoltage() / 10.f, 0.f, 1.f);
		const float y = clamp(inputs[Y_INPUT].getVoltage() / 10.f, 0.f, 1.f);
		const setbook::Policy pol = policy((int) std::round(params[CURVE_PARAM].getValue()));
		uint8_t mask = 0;
		for (int k = 0; k < 4; k++)
			if (scenes[clamp(padCorner[k], 0, kScenes - 1)].used)
				mask |= (uint8_t) (1u << k);
		for (int c = 0; c < kCh; c++) {
			if (live[c])
				continue;
			float corner[4];
			for (int k = 0; k < 4; k++)
				corner[k] = toNorm(c, scenes[clamp(padCorner[k], 0, kScenes - 1)].v[c]);
			const setbook::Desc d = desc(c);
			cur[c] = toVolts(c, setbook::blend(d, setbook::resolve(d, setbook::RuleAuto, pol), padBase[c], corner, mask, x, y, pol));
		}
	}

	void armPad() {
		padOn = true;
		morph.active = false;
		pendingScene = -1;
		for (int c = 0; c < kCh; c++) {
			padBase[c] = toNorm(c, cur[c]);
			live[c] = false;
			knobLast[c] = params[CH_PARAM + c].getValue();
		}
#ifndef METAMODULE
		famArm(-1, -1);
		famPad = true;
#endif
	}

	void goLive() {
		morph.active = false;
		pendingScene = -1;
		padOn = false;
		for (int c = 0; c < kCh; c++)
			live[c] = true;
#ifndef METAMODULE
		nArmed = 0;
#endif
	}

	int nextUsed(int from) const {
		for (int k = 1; k <= kScenes; k++) {
			const int s = (from + k) % kScenes;
			if (scenes[s].used)
				return s;
		}
		return -1;
	}

	// ---- tempo / position from the BUS (as CONDUIT) --------------------------------------------

	float busV(int ch) {
		Input& in = inputs[BUS_INPUT];
		return ch < in.getChannels() ? in.getVoltage(ch) : 0.f;
	}

	void transport(float dt) {
		const bool bus = inputs[BUS_INPUT].isConnected();
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
			if (d > 0.f && d < 2.f) {
				posIdle = 0.f;
				posMeasureBars += d;
			}
			else
				posIdle += dt;
			posMeasureT += dt;
			if (posMeasureT >= 0.25f) {
				if (posMeasureBars > 0.f) {
					const float b = posMeasureBars / posMeasureT * 4.f * 60.f;
					busBpm = busBpm > 0.f ? busBpm + 0.5f * (b - busBpm) : b;
				}
				posMeasureT = posMeasureBars = 0.f;
			}
		}
		else
			posIdle += dt;
		posPrevBars = bars;
		wasPlaying = playing;
		prevPpq = ppq;
		playing = bus && posIdle < 0.5f;
		ppq = (posCycles * semi::bus::kBarsPerCycle + bars) * 4.0;
		if (playing && busBpm > 20.f) {
			bpm = clamp(busBpm, 20.f, 400.f);
			tempoSrc = 1;
		}
		else {
			bpm = kTempos[clamp(tempoIndex, 0, kNumTempos - 1)];
			tempoSrc = 0;
		}
	}

	// ---- audio ------------------------------------------------------------------------------

	void process(const ProcessArgs& args) override {
		transport(args.sampleTime);

		// STORE: press + scene button = store into it; release alone = CAPTURE
		const bool storeNow = params[STORE_PARAM].getValue() > 0.f;
		if (storeNow && !storeHeld)
			storeUsed = false;
		for (int i = 0; i < kScenes; i++)
			if (sceneBtn[i].process(params[SCENE_PARAM + i].getValue() > 0.f)) {
				if (storeNow) {
					store(i);
					storeUsed = true;
				}
				else
					recall(i);
			}
		if (!storeNow && storeHeld && !storeUsed)
			store(captureTarget());
		storeHeld = storeNow;

		if (liveBtn.process(params[LIVE_PARAM].getValue() > 0.f))
			goLive();
		if (padBtn.process(params[PAD_PARAM].getValue() > 0.f)) {
			if (padOn)
				padOn = false;
			else
				armPad();
		}
		const bool xy = inputs[X_INPUT].isConnected() || inputs[Y_INPUT].isConnected();
		if (xy && !xyWas)
			armPad();
		xyWas = xy;

		// scene CV / RCL / NXT
		if (inputs[SCN_INPUT].isConnected()) {
			const int s = clamp((int) std::floor(inputs[SCN_INPUT].getVoltage() + 0.5f), 0, kScenes - 1);
			if (inputs[RCL_INPUT].isConnected()) {
				if (rclTrig.process(inputs[RCL_INPUT].getVoltage(), 0.1f, 1.f))
					recall(s);
			}
			else if (s != scnLast)
				recall(s);
			scnLast = s;
		}
		else if (rclTrig.process(inputs[RCL_INPUT].getVoltage(), 0.1f, 1.f))
			recall(selected);
		if (nxtTrig.process(inputs[NXT_INPUT].getVoltage(), 0.1f, 1.f))
			recall(nextUsed(selected));

		// the BUS: SCENE changes, turns, cues
		if (inputs[BUS_INPUT].isConnected()) {
			const int bs = semi::bus::voltsToScene(busV(semi::bus::kScene));
			if (busSceneRecalls && busSceneLast >= 0 && bs != busSceneLast)
				recall(bs % kScenes);
			busSceneLast = bs;
			const float ev = busV(semi::bus::kEvent);
			if (turnNext > 0 && ev > 1.f && eventPrev <= 1.f && semi::bus::voltsToTurn(ev) >= kTurnLines[clamp(turnNext, 0, 4)])
				recall(nextUsed(selected));
			eventPrev = ev;
			const int n = nCues.load(std::memory_order_acquire);
			if (cuesOn && n > 0) {
				sb::Placement pl;
				pl.originPpq = 0.0;
				pl.loopQ = kCycleQ;
				const sb::Crossing x = sb::cross(cues, n, pl, prevPpq, ppq, wasPlaying, playing);
				if (x.located && chase && x.chase >= 0)
					startRecall(clamp(cues[x.chase].scene, 0, kScenes - 1), 0.f, setbook::CurveLinear);
				for (int i = 0; i < x.numFired; i++) {
					const sb::Cue& c = cues[x.fired[i]];
					if (scenes[clamp(c.scene, 0, kScenes - 1)].used)
						startRecall(clamp(c.scene, 0, kScenes - 1), c.beats, c.curve);
				}
			}
		}
		else
			busSceneLast = -1;

		// a launch waiting for its beat / bar
		if (pendingScene >= 0 && (!playing || ppq >= pendingPpq - 1e-6)) {
			const int s = pendingScene;
			pendingScene = -1;
			startRecall(s, pendingBeats, pendingCurve);
		}

		// knobs: a moved knob gives its channel back to LIVE
		for (int c = 0; c < kCh; c++) {
			const float k = params[CH_PARAM + c].getValue();
			if (knobLast[c] >= 0.f && std::fabs(k - knobLast[c]) > 1e-4f)
				live[c] = true;
			knobLast[c] = k;
		}

		if (padOn)
			stepPad();
		else
			stepMorph(args.sampleTime);
		for (int c = 0; c < kCh; c++) {
			if (live[c])
				cur[c] = kind[c] == CH_GATE ? (source(c) > 1.f ? 10.f : 0.f) : source(c);
			outputs[CH_OUTPUT + c].setVoltage(cur[c]);
		}
		outputs[END_OUTPUT].setVoltage(endPulse.process(args.sampleTime) ? 10.f : 0.f);
		outputs[MORPH_OUTPUT].setVoltage(morph.active ? 10.f * morph.x : 0.f);

		Output& bo = outputs[BUS_OUTPUT];
		bo.setChannels(semi::bus::kChannels);
		for (int ch = 0; ch < 4; ch++)
			bo.setVoltage(busV(ch), ch);
		if (busSceneOut == 1)
			bo.setVoltage(semi::bus::sceneToVolts(selected), semi::bus::kScene);

#ifndef METAMODULE
		if (++famDiv >= 32) {
			famDiv = 0;
			famStep();
		}
#endif

		if (ui.tick(args.sampleTime))
			publish();
	}

	// ---- FAMILY (Rack only) ------------------------------------------------------------------------
#ifndef METAMODULE
	bool memberModel(Module* m) const {
		if (!m || m == this || !m->model || m->model->plugin != pluginInstance)
			return false;
		const std::string& s = m->model->slug;
		if (s == "Setbook")
			return false;
		if (!famSystem && (s == "Conduit" || s == "Blackbox"))
			return false;
		return true;
	}

	// UI thread: who is in the patch
	void refreshMembers() {
		eng = APP->engine;
		int64_t ids[kMaxMembers];
		int n = 0;
		int nParams = 0;
		for (int64_t id : eng->getModuleIds()) {
			Module* m = eng->getModule(id);
			if (!memberModel(m) || n >= kMaxMembers)
				continue;
			ids[n++] = id;
			nParams += std::min((int) m->params.size(), kMaxParams);
		}
		memSeq.fetch_add(1, std::memory_order_acq_rel);
		std::memcpy(memIds, ids, sizeof(int64_t) * (size_t) n);
		memCount = n;
		memSeq.fetch_add(1, std::memory_order_release);
		famParams = nParams;
	}

	void pullMembers() {
		const uint32_t s0 = memSeq.load(std::memory_order_acquire);
		if (s0 == audioSeq || (s0 & 1))
			return;
		int64_t ids[kMaxMembers];
		const int n = clamp(memCount, 0, kMaxMembers);
		std::memcpy(ids, memIds, sizeof(int64_t) * (size_t) n);
		if (memSeq.load(std::memory_order_acquire) != s0)
			return;
		std::memcpy(audioIds, ids, sizeof(int64_t) * (size_t) n);
		audioCount = n;
		audioSeq = s0;
	}

	Module* member(int64_t id) {
		if (!eng)
			return nullptr;
		Module* m = eng->getModule_NoLock(id);   // the engine thread already holds the engine's lock
		return memberModel(m) ? m : nullptr;
	}

	static bool paramUsable(Module* m, int i) {
		ParamQuantity* pq = i < (int) m->paramQuantities.size() ? m->paramQuantities[i] : nullptr;
		return pq && pq->randomizeEnabled && pq->maxValue > pq->minValue;
	}

	void famCapture(int s) {
		if (!famOn)
			return;
		pullMembers();
		FamScene& fs = fam[(size_t) s];
		fs.n = 0;
		for (int k = 0; k < audioCount; k++) {
			Module* m = member(audioIds[k]);
			if (!m)
				continue;
			FamEntry& e = fs.e[fs.n++];
			e.id = audioIds[k];
			e.np = std::min((int) m->params.size(), kMaxParams);
			for (int i = 0; i < e.np; i++) {
				setbook::Desc d;
				d.id = (uint32_t) i + 1;
				if (!paramUsable(m, i)) {
					d.rule = setbook::RuleFixed;
					e.v[i] = 0.f;
				}
				else {
					ParamQuantity* pq = m->paramQuantities[i];
					const float range = pq->maxValue - pq->minValue;
					e.v[i] = clamp((m->params[i].getValue() - pq->minValue) / range, 0.f, 1.f);
					if (pq->snapEnabled) {
						const int steps = clamp((int) std::round(range) + 1, 2, 65535);
						d.kind = steps == 2 ? setbook::Switch : setbook::Stepped;
						d.steps = (uint16_t) steps;
					}
				}
				e.desc[i] = d;
			}
		}
	}

	// scene >= 0: a timed recall to it; scene < 0: the pad's four corners
	void famArm(int scene, int) {
		nArmed = 0;
		famPad = false;
		if (!famOn)
			return;
		const FamScene* corners[4] = {nullptr, nullptr, nullptr, nullptr};
		if (scene >= 0)
			corners[1] = &fam[(size_t) scene];
		else
			for (int k = 0; k < 4; k++)
				corners[k] = &fam[(size_t) clamp(padCorner[k], 0, kScenes - 1)];
		// every member any corner holds
		for (int k = 0; k < 4; k++) {
			if (!corners[k])
				continue;
			for (int j = 0; j < corners[k]->n; j++) {
				const FamEntry& e = corners[k]->e[j];
				bool seen = false;
				for (int a = 0; a < nArmed; a++)
					seen = seen || armed[(size_t) a].id == e.id;
				if (seen || nArmed >= kMaxMembers)
					continue;
				Module* m = member(e.id);
				if (!m)
					continue;
				FamArmed& A = armed[(size_t) nArmed++];
				A.id = e.id;
				A.np = std::min(e.np, std::min((int) m->params.size(), kMaxParams));
				A.mask = 0;
				for (int i = 0; i < A.np; i++) {
					A.desc[i] = e.desc[i];
					A.released[i] = !paramUsable(m, i) || e.desc[i].rule == setbook::RuleFixed;
					ParamQuantity* pq = m->paramQuantities[i];
					A.base[i] = A.released[i] ? 0.f : clamp((m->params[i].getValue() - pq->minValue) / (pq->maxValue - pq->minValue), 0.f, 1.f);
					A.last[i] = m->params[i].getValue();
				}
				for (int c = 0; c < 4; c++) {
					const FamEntry* ce = corners[c] ? corners[c]->find(e.id) : nullptr;
					if (!ce)
						continue;
					A.mask |= (uint8_t) (1u << c);
					for (int i = 0; i < A.np && i < ce->np; i++)
						A.corner[c][i] = ce->v[i];
				}
			}
		}
	}

	// engine thread, every 32 samples: write the armed members' params
	void famStep() {
		if (!famOn || nArmed == 0)
			return;
		const bool pad = padOn && famPad;
		if (!pad && !morph.active && morph.x < 1.f)
			return;
		const float x = pad ? clamp(inputs[X_INPUT].getVoltage() / 10.f, 0.f, 1.f) : morph.x;
		const float y = pad ? clamp(inputs[Y_INPUT].getVoltage() / 10.f, 0.f, 1.f) : 0.f;
		const setbook::Policy pol = policy(pad ? (int) std::round(params[CURVE_PARAM].getValue()) : morph.curve);
		for (int a = 0; a < nArmed; a++) {
			FamArmed& A = armed[(size_t) a];
			Module* m = member(A.id);
			if (!m)
				continue;
			const uint8_t mask = pad ? A.mask : (uint8_t) (A.mask & 2u ? 3u : 0u);
			if (!mask)
				continue;
			for (int i = 0; i < A.np && i < (int) m->params.size(); i++) {
				if (A.released[i])
					continue;
				// a knob the user grabbed is let go (until the next recall)
				if (std::fabs(m->params[i].getValue() - A.last[i]) > 1e-5f) {
					A.released[i] = true;
					continue;
				}
				ParamQuantity* pq = m->paramQuantities[i];
				const float corner[4] = {pad ? A.corner[0][i] : A.base[i], A.corner[1][i], A.corner[2][i], A.corner[3][i]};
				const float n = setbook::blend(A.desc[i], setbook::resolve(A.desc[i], setbook::RuleAuto, pol), A.base[i], corner, mask, x, y, pol);
				const float v = pq->minValue + n * (pq->maxValue - pq->minValue);
				m->params[i].setValue(v);
				A.last[i] = v;
			}
		}
		if (!pad && !morph.active)
			nArmed = 0;   // arrived: the last write is done
	}
#endif

	// ---- UI ---------------------------------------------------------------------------------

	void publish() {
		Snapshot& s = snap.writeSlot();
		float nowNorm[kCh];
		for (int c = 0; c < kCh; c++)
			nowNorm[c] = toNorm(c, cur[c]);
		for (int i = 0; i < kScenes; i++) {
			CardView& cv = s.card[i];
			cv.used = scenes[i].used;
			std::memcpy(cv.name, scenes[i].name, sizeof cv.name);
			cv.name[sizeof cv.name - 1] = 0;
			float d = 0.f;
			for (int c = 0; c < kCh; c++) {
				cv.norm[c] = toNorm(c, scenes[i].v[c]);
				d += std::fabs(cv.norm[c] - nowNorm[c]);
			}
			cv.dist = cv.used ? d / kCh : -1.f;
#ifndef METAMODULE
			cv.fam = fam[(size_t) i].n > 0;
#endif
		}
		s.selected = selected;
		s.target = morph.target;
		s.from = morph.from;
		s.x = morph.x;
		s.morphing = morph.active;
		s.waiting = pendingScene >= 0;
		s.padOn = padOn;
		s.padX = clamp(inputs[X_INPUT].getVoltage() / 10.f, 0.f, 1.f);
		s.padY = clamp(inputs[Y_INPUT].getVoltage() / 10.f, 0.f, 1.f);
		for (int k = 0; k < 4; k++)
			s.corner[k] = padCorner[k];
		for (int c = 0; c < kCh; c++) {
			s.norm[c] = nowNorm[c];
			s.live[c] = live[c];
		}
		s.bpm = bpm;
		s.tempoSrc = tempoSrc;
		s.busMoving = playing;
		s.bar = (float) std::fmod(ppq / 4.0, (double) semi::bus::kBarsPerCycle);
		s.nCues = nCues.load();
		for (int i = 0; i < s.nCues; i++) {
			s.cueBar[i] = (float) cues[i].bar;
			s.cueScene[i] = cues[i].scene;
		}
		s.timeBeats = beatsFromKnob(params[TIME_PARAM].getValue());
		s.curve = (int) std::round(params[CURVE_PARAM].getValue());
		s.thresh = params[THRESH_PARAM].getValue();
#ifndef METAMODULE
		s.members = audioCount;
		s.memberParams = famParams.load();
#endif
		snap.publish();

		for (int i = 0; i < kScenes; i++) {
			float b = scenes[i].used ? 0.15f : 0.f;
			if (i == selected)
				b = morph.active ? 0.3f + 0.7f * morph.x : (pendingScene == i ? 0.5f : 1.f);
			lights[SCENE_LIGHT + i].setBrightness(b);
		}
		lights[PAD_LIGHT].setBrightness(padOn ? 1.f : 0.f);
	}

	// ---- cues (UI thread) --------------------------------------------------------------------------

	void addCue(int scene, double bar) {
		const int n = nCues.load();
		if (n >= kMaxCues)
			return;
		std::vector<sb::Cue> v(cues, cues + n);
		sb::Cue c;
		c.bar = bar;
		c.scene = scene;
		c.beats = beatsFromKnob(params[TIME_PARAM].getValue());
		c.curve = (uint8_t) clamp((int) std::round(params[CURVE_PARAM].getValue()), 0, 3);
		v.push_back(c);
		sb::sortCues(v);
		nCues.store(0, std::memory_order_release);
		for (size_t i = 0; i < v.size() && i < (size_t) kMaxCues; i++)
			cues[i] = v[i];
		nCues.store((int) std::min(v.size(), (size_t) kMaxCues), std::memory_order_release);
	}

	// ---- state ------------------------------------------------------------------------------

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		for (int i = 0; i < kScenes; i++) {
			scenes[i] = CableScene{};
			std::snprintf(scenes[i].name, sizeof scenes[i].name, "%s", sb::defaultSceneName(i));
#ifndef METAMODULE
			fam[(size_t) i].n = 0;
#endif
		}
		nCues = 0;
		for (int c = 0; c < kCh; c++) {
			kind[c] = CH_CV;
			bipolar[c] = 0;
		}
		launch = 0;
		tempoIndex = 1;
		steppedWalk = false;
		busSceneRecalls = true;
		turnNext = 0;
		cuesOn = chase = true;
		busSceneOut = 1;
		for (int k = 0; k < 4; k++)
			padCorner[k] = k;
		selected = 0;
		goLive();
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_t* sc = json_array();
		for (int i = 0; i < kScenes; i++) {
			json_t* o = json_object();
			json_object_set_new(o, "name", json_string(scenes[i].name));
			json_object_set_new(o, "used", json_boolean(scenes[i].used));
			json_t* v = json_array();
			for (int c = 0; c < kCh; c++)
				json_array_append_new(v, json_real(scenes[i].v[c]));
			json_object_set_new(o, "v", v);
#ifndef METAMODULE
			const FamScene& fs = fam[(size_t) i];
			if (fs.n > 0) {
				json_t* fa = json_array();
				for (int k = 0; k < fs.n; k++) {
					const FamEntry& e = fs.e[k];
					json_t* fe = json_object();
					json_object_set_new(fe, "id", json_integer(e.id));
					json_t* pv = json_array();
					json_t* pk = json_array();
					for (int p = 0; p < e.np; p++) {
						json_array_append_new(pv, json_real(e.v[p]));
						json_array_append_new(pk, json_integer((int) e.desc[p].kind | ((int) e.desc[p].steps << 2) | ((int) e.desc[p].rule << 18)));
					}
					json_object_set_new(fe, "v", pv);
					json_object_set_new(fe, "k", pk);
					json_array_append_new(fa, fe);
				}
				json_object_set_new(o, "family", fa);
			}
#endif
			json_array_append_new(sc, o);
		}
		json_object_set_new(root, "scenes", sc);
		json_t* cu = json_array();
		for (int i = 0; i < nCues.load(); i++) {
			json_t* q = json_array();
			json_array_append_new(q, json_real(cues[i].bar));
			json_array_append_new(q, json_integer(cues[i].scene));
			json_array_append_new(q, json_real(cues[i].beats));
			json_array_append_new(q, json_integer(cues[i].curve));
			json_array_append_new(cu, q);
		}
		json_object_set_new(root, "cues", cu);
		json_t* ch = json_array();
		for (int c = 0; c < kCh; c++) {
			json_t* q = json_array();
			json_array_append_new(q, json_integer(kind[c]));
			json_array_append_new(q, json_integer(bipolar[c]));
			json_array_append_new(q, json_real(cur[c]));
			json_array_append_new(q, json_boolean(live[c]));
			json_array_append_new(ch, q);
		}
		json_object_set_new(root, "channels", ch);
		json_t* pc = json_array();
		for (int k = 0; k < 4; k++)
			json_array_append_new(pc, json_integer(padCorner[k]));
		json_object_set_new(root, "padCorners", pc);
		json_object_set_new(root, "selected", json_integer(selected));
		json_object_set_new(root, "launch", json_integer(launch));
		json_object_set_new(root, "tempo", json_integer(tempoIndex));
		json_object_set_new(root, "steppedWalk", json_boolean(steppedWalk));
		json_object_set_new(root, "busSceneRecalls", json_boolean(busSceneRecalls));
		json_object_set_new(root, "turnNext", json_integer(turnNext));
		json_object_set_new(root, "cuesOn", json_boolean(cuesOn));
		json_object_set_new(root, "chase", json_boolean(chase));
		json_object_set_new(root, "busSceneOut", json_integer(busSceneOut));
#ifndef METAMODULE
		json_object_set_new(root, "family", json_boolean(famOn));
		json_object_set_new(root, "familySystem", json_boolean(famSystem));
#endif
		return root;
	}

	void dataFromJson(json_t* root) override {
		size_t i;
		json_t* q;
		json_array_foreach(json_object_get(root, "scenes"), i, q) {
			if (i >= (size_t) kScenes)
				break;
			CableScene& s = scenes[i];
			if (const char* n = json_string_value(json_object_get(q, "name")))
				std::snprintf(s.name, sizeof s.name, "%s", n);
			s.used = json_is_true(json_object_get(q, "used"));
			size_t c;
			json_t* v;
			json_array_foreach(json_object_get(q, "v"), c, v) {
				if (c < (size_t) kCh)
					s.v[c] = clamp((float) json_number_value(v), -10.f, 10.f);
			}
#ifndef METAMODULE
			FamScene& fs = fam[i];
			fs.n = 0;
			size_t k;
			json_t* fe;
			json_array_foreach(json_object_get(q, "family"), k, fe) {
				if (fs.n >= kMaxMembers)
					break;
				FamEntry& e = fs.e[fs.n++];
				e.id = (int64_t) json_integer_value(json_object_get(fe, "id"));
				e.np = 0;
				json_t* pv = json_object_get(fe, "v");
				json_t* pk = json_object_get(fe, "k");
				size_t p;
				json_t* x;
				json_array_foreach(pv, p, x) {
					if (p >= (size_t) kMaxParams)
						break;
					e.v[p] = clamp((float) json_number_value(x), 0.f, 1.f);
					const int code = (int) json_integer_value(json_array_get(pk, p));
					setbook::Desc d;
					d.id = (uint32_t) p + 1;
					d.kind = (setbook::Kind) clamp(code & 3, 0, 2);
					d.steps = (uint16_t) ((code >> 2) & 0xffff);
					d.rule = (setbook::Rule) clamp((code >> 18) & 7, 0, (int) setbook::kNumRules - 1);
					e.desc[p] = d;
					e.np = (int) p + 1;
				}
			}
#endif
		}
		int n = 0;
		json_array_foreach(json_object_get(root, "cues"), i, q) {
			if (n >= kMaxCues)
				break;
			sb::Cue& c = cues[n++];
			c.bar = clamp((float) json_number_value(json_array_get(q, 0)), 0.f, 32.f);
			c.scene = clamp((int) json_integer_value(json_array_get(q, 1)), 0, kScenes - 1);
			c.beats = clamp((float) json_number_value(json_array_get(q, 2)), 0.f, 64.f);
			c.curve = (uint8_t) clamp((int) json_integer_value(json_array_get(q, 3)), 0, 3);
		}
		nCues = n;
		json_array_foreach(json_object_get(root, "channels"), i, q) {
			if (i >= (size_t) kCh)
				break;
			kind[i] = clamp((int) json_integer_value(json_array_get(q, 0)), 0, 2);
			bipolar[i] = clamp((int) json_integer_value(json_array_get(q, 1)), 0, 1);
			cur[i] = clamp((float) json_number_value(json_array_get(q, 2)), -10.f, 10.f);
			live[i] = json_is_true(json_array_get(q, 3));
			knobLast[i] = -1.f;
		}
		json_array_foreach(json_object_get(root, "padCorners"), i, q) {
			if (i < 4)
				padCorner[i] = clamp((int) json_integer_value(q), 0, kScenes - 1);
		}
		auto geti = [&](const char* k, int& v, int lo, int hi) {
			if (json_t* j = json_object_get(root, k))
				v = clamp((int) json_integer_value(j), lo, hi);
		};
		auto getb = [&](const char* k, bool& v) {
			if (json_t* j = json_object_get(root, k))
				v = json_is_true(j);
		};
		geti("selected", selected, 0, kScenes - 1);
		geti("launch", launch, 0, 4);
		geti("tempo", tempoIndex, 0, kNumTempos - 1);
		getb("steppedWalk", steppedWalk);
		getb("busSceneRecalls", busSceneRecalls);
		geti("turnNext", turnNext, 0, 4);
		getb("cuesOn", cuesOn);
		getb("chase", chase);
		geti("busSceneOut", busSceneOut, 0, 1);
#ifndef METAMODULE
		getb("family", famOn);
		getb("familySystem", famSystem);
#endif
		morph.target = selected;
	}
};

// ---- screens --------------------------------------------------------------------------------

namespace {

const Snapshot& demoSnapshot() {
	static const Snapshot demo = [] {
		Snapshot s;
		for (int i = 0; i < kScenes; i++) {
			CardView& c = s.card[i];
			std::snprintf(c.name, sizeof c.name, "%s", sb::defaultSceneName(i));
			c.used = i < 6;
			for (int k = 0; k < kCh; k++)
				c.norm[k] = 0.5f + 0.45f * std::sin(1.3f * i + 0.9f * k);
			c.dist = c.used ? 0.05f + 0.07f * i : -1.f;
			c.fam = i < 4;
		}
		s.selected = 2;
		s.target = 2;
		s.from = 1;
		s.x = 0.62f;
		s.morphing = true;
		for (int k = 0; k < kCh; k++)
			s.norm[k] = 0.5f + 0.4f * std::sin(2.4f + 0.9f * k);
		s.bpm = 172.f;
		s.tempoSrc = 1;
		s.busMoving = true;
		s.bar = 13.4f;
		s.nCues = 4;
		const float bars[4] = {0.f, 8.f, 16.f, 24.f};
		for (int i = 0; i < 4; i++) {
			s.cueBar[i] = bars[i];
			s.cueScene[i] = i;
		}
		s.timeBeats = 8.f;
		s.curve = 1;
		s.members = 5;
		s.memberParams = 112;
		return s;
	}();
	return demo;
}

} // namespace

// Eight scene cards: name, the eight channels as bars, D = how far the outputs are from it. Selected
// in cyan, the morph's target in magenta (with its progress along the bottom), empty cards dashed.
struct CardsScreen : Screen {
	Setbook* module = nullptr;

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demoSnapshot();
		const float w = box.size.x, h = box.size.y;
		const float gap = 2.f;
		const float cw = (w - gap * 5.f) / 4.f, ch = (h - gap * 3.f) / 2.f;
		for (int i = 0; i < kScenes; i++) {
			const CardView& c = s.card[i];
			const float x = gap + (i % 4) * (cw + gap), y = gap + (i / 4) * (ch + gap);
			const bool target = s.morphing && s.target == i;
			const bool sel = s.selected == i;
			if (c.used)
				fillRect(vg, x, y, cw, ch, semi::theme::cell());
			NVGcolor edge = target || (s.waiting && sel) ? semi::theme::magenta() : sel ? semi::theme::cyan() : semi::theme::grid();
			if (c.used || sel) {
				nvgBeginPath(vg);
				nvgRect(vg, x + 0.5f, y + 0.5f, cw - 1.f, ch - 1.f);
				nvgStrokeColor(vg, edge);
				nvgStrokeWidth(vg, sel || target ? semi::platform::kStroke : semi::platform::kThinStroke);
				nvgStroke(vg);
			}
			else {
				dashed(vg, x, y, x + cw, y, semi::theme::grid(), 2.5f);
				dashed(vg, x, y + ch, x + cw, y + ch, semi::theme::grid(), 2.5f);
				dashed(vg, x, y, x, y + ch, semi::theme::grid(), 2.5f);
				dashed(vg, x + cw, y, x + cw, y + ch, semi::theme::grid(), 2.5f);
			}
			char num[12];
			std::snprintf(num, sizeof num, "%d", i + 1);
			text(vg, x + 2.f, y + 1.5f, num, 8.f, sel ? semi::theme::cyan() : semi::theme::dim());
			text(vg, x + 9.f, y + 1.5f, c.used ? c.name : "EMPTY", 8.f, c.used ? semi::theme::text() : semi::theme::dim());
			if (!c.used)
				continue;
			// the eight channels
			const float by0 = y + 12.f, bh = ch - 24.f, bw = (cw - 6.f) / kCh;
			for (int k = 0; k < kCh; k++) {
				const float v = clamp(c.norm[k], 0.f, 1.f);
				fillRect(vg, x + 3.f + bw * k + 0.4f, by0 + bh * (1.f - v), bw - 0.8f, std::max(1.f, bh * v),
				         target ? semi::theme::magenta() : sel ? semi::theme::cyan() : semi::theme::cyanDim());
			}
			char d[12];
			if (c.dist >= 0.f)
				std::snprintf(d, sizeof d, "D%d", (int) std::round(c.dist * 100.f));
			else
				d[0] = 0;
			text(vg, x + 2.f, y + ch - 10.5f, d, 8.f, semi::theme::dim());
			if (c.fam)
				text(vg, x + cw - 2.f, y + ch - 10.5f, "F", 8.f, semi::theme::amber(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
			if (target)
				fillRect(vg, x + 1.f, y + ch - 2.5f, (cw - 2.f) * clamp(s.x, 0.f, 1.f), 1.5f, semi::theme::magenta());
		}
	}
};

// The cue lane: the BUS POS cycle (32 bars), a tick per cue with its scene, the playhead.
struct LaneScreen : Screen {
	Setbook* module = nullptr;

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demoSnapshot();
		const float w = box.size.x, h = box.size.y;
		const float x0 = 3.f, x1 = w - 3.f, gw = x1 - x0;
		auto X = [&](float bar) { return x0 + gw * bar / semi::bus::kBarsPerCycle; };
		for (int b = 0; b <= 32; b += 4)
			line(vg, X((float) b), b % 8 == 0 ? 2.f : h - 6.f, X((float) b), h - 2.f, b % 8 == 0 ? semi::theme::grid() : semi::theme::cell());
		line(vg, x0, h - 2.f, x1, h - 2.f, semi::theme::grid());
		for (int i = 0; i < s.nCues; i++) {
			const float x = X(s.cueBar[i]);
			line(vg, x, 3.f, x, h - 2.f, semi::theme::amber(), semi::platform::kStroke);
			char b[12];
			std::snprintf(b, sizeof b, "%d", s.cueScene[i] + 1);
			text(vg, x + 2.f, 2.f, b, 8.f, semi::theme::amber());
		}
		if (s.busMoving) {
			const float px = X(clamp(s.bar, 0.f, 32.f));
			line(vg, px, 0.f, px, h, semi::theme::magenta(), semi::platform::kStroke);
		}
		else if (s.nCues == 0)
			text(vg, w * 0.5f, h * 0.5f, module && module->inputs[Setbook::BUS_INPUT].isConnected() ? "BUS STOPPED" : "CUES: PATCH THE BUS",
			     8.f, semi::theme::dim(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
	}
};

// The morph: state, target, progress, the curve with the position on it (or the pad with its dot),
// TIME / THRESH, tempo, FAMILY.
struct InfoScreen : Screen {
	Setbook* module = nullptr;

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demoSnapshot();
		const float w = box.size.x, h = box.size.y;
		const float pad = 3.f;
		const char* st = s.padOn ? "PAD" : s.waiting ? "WAIT" : s.morphing ? "MORPH" : "HOLD";
		bool allLive = true;
		for (int c = 0; c < kCh; c++)
			allLive = allLive && s.live[c];
		if (allLive && !s.padOn && !s.morphing && !s.waiting)
			st = "LIVE";
		text(vg, pad, 2.f, st, 10.f, s.morphing || s.waiting ? semi::theme::magenta() : s.padOn ? semi::theme::amber() : semi::theme::cyan());
		const int t = s.morphing ? s.target : s.selected;
		if (t >= 0 && t < kScenes)
			text(vg, w - pad, 2.f, s.card[t].name, 9.f, semi::theme::text(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);

		// graph area
		const float gx = pad, gy = 15.f, gw = w - 2.f * pad, gh = 50.f;
		fillRect(vg, gx, gy, gw, gh, semi::theme::cell());
		if (s.padOn) {
			for (int k = 0; k < 4; k++) {
				const float cx = gx + ((k & 1) ? gw - 2.f : 2.f), cy = gy + ((k & 2) ? 2.f : gh - 2.f);
				char b[12];
				std::snprintf(b, sizeof b, "%d", s.corner[k] + 1);
				text(vg, cx, cy, b, 8.f, s.card[s.corner[k]].used ? semi::theme::text() : semi::theme::dim(),
				     ((k & 1) ? NVG_ALIGN_RIGHT : NVG_ALIGN_LEFT) | ((k & 2) ? NVG_ALIGN_TOP : NVG_ALIGN_BOTTOM));
			}
			const float px = gx + gw * s.padX, py = gy + gh * (1.f - s.padY);
			fillRect(vg, px - 2.5f, py - 2.5f, 5.f, 5.f, semi::theme::amber());
		}
		else {
			constexpr int n = 32;
			float ys[n];
			for (int i = 0; i < n; i++)
				ys[i] = gy + gh - 2.f - (gh - 4.f) * setbook::curve(s.curve, i / (float) (n - 1));
			polyline(vg, ys, n, gx, gw, semi::theme::cyanDim());
			const float tx = gx + gw * s.thresh;
			dashed(vg, tx, gy, tx, gy + gh, semi::theme::grid(), 2.5f);
			if (s.morphing) {
				const float mx = gx + gw * s.x;
				line(vg, mx, gy, mx, gy + gh, semi::theme::magenta(), semi::platform::kStroke);
			}
		}
		// the outputs as eight bars under the graph (hollow = LIVE)
		const float by = gy + gh + 3.f, bh = 14.f, bw = gw / kCh;
		for (int c = 0; c < kCh; c++) {
			const float v = clamp(s.norm[c], 0.f, 1.f);
			fillRect(vg, gx + bw * c + 0.5f, by + bh * (1.f - v), bw - 1.f, std::max(1.f, bh * v), s.live[c] ? semi::theme::dim() : semi::theme::cyan());
		}

		char b[32];
		float y = by + bh + 4.f;
		if (s.timeBeats < 0.05f)
			std::snprintf(b, sizeof b, "CUT");
		else
			std::snprintf(b, sizeof b, "%.1f BT %.1fs", s.timeBeats, s.timeBeats * 60.f / std::max(1.f, s.bpm));
		text(vg, pad, y, "TIME", 8.f, semi::theme::dim());
		text(vg, w - pad, y, b, 9.f, semi::theme::text(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		y += 10.5f;
		std::snprintf(b, sizeof b, "%.0f", s.bpm);
		text(vg, pad, y, s.tempoSrc ? "BUS" : "INT", 8.f, s.tempoSrc ? semi::theme::magenta() : semi::theme::dim());
		text(vg, w - pad, y, b, 9.f, semi::theme::text(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		y += 10.5f;
		if (s.members >= 0 && y + 9.f < h) {
			std::snprintf(b, sizeof b, "%d / %dP", s.members, s.memberParams);
			text(vg, pad, y, "FAM", 8.f, semi::theme::amber());
			text(vg, w - pad, y, b, 9.f, semi::theme::text(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		}
	}
};

struct SetbookWidget : ModuleWidget {
	int refresh = 0;

	SetbookWidget(Setbook* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Setbook.svg")));

		auto* cs = semi::createScreenMm<CardsScreen>(Vec(L::CARDS.x, L::CARDS.y), Vec(L::CARDS.w, L::CARDS.h));
		cs->module = module;
		addChild(cs);
		auto* ls = semi::createScreenMm<LaneScreen>(Vec(L::LANE.x, L::LANE.y), Vec(L::LANE.w, L::LANE.h));
		ls->module = module;
		addChild(ls);
		auto* is = semi::createScreenMm<InfoScreen>(Vec(L::INFO.x, L::INFO.y), Vec(L::INFO.w, L::INFO.h));
		is->module = module;
		addChild(is);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		const L::P led[kScenes] = {L::SCENE1_LED, L::SCENE2_LED, L::SCENE3_LED, L::SCENE4_LED, L::SCENE5_LED, L::SCENE6_LED, L::SCENE7_LED, L::SCENE8_LED};
		const L::P btn[kScenes] = {L::SCENE1, L::SCENE2, L::SCENE3, L::SCENE4, L::SCENE5, L::SCENE6, L::SCENE7, L::SCENE8};
		const L::P knob[kCh] = {L::CH1, L::CH2, L::CH3, L::CH4, L::CH5, L::CH6, L::CH7, L::CH8};
		const L::P in[kCh] = {L::IN1, L::IN2, L::IN3, L::IN4, L::IN5, L::IN6, L::IN7, L::IN8};
		const L::P out[kCh] = {L::OUT1, L::OUT2, L::OUT3, L::OUT4, L::OUT5, L::OUT6, L::OUT7, L::OUT8};
		for (int i = 0; i < kScenes; i++) {
			addChild(createLightCentered<semi::CyanLedSmall>(at(led[i]), module, Setbook::SCENE_LIGHT + i));
			addParam(createParamCentered<semi::Button>(at(btn[i]), module, Setbook::SCENE_PARAM + i));
		}
		for (int c = 0; c < kCh; c++) {
			addParam(createParamCentered<semi::KnobS>(at(knob[c]), module, Setbook::CH_PARAM + c));
			addInput(createInputCentered<semi::Jack>(at(in[c]), module, Setbook::CH_INPUT + c));
			addOutput(createOutputCentered<semi::Jack>(at(out[c]), module, Setbook::CH_OUTPUT + c));
		}
		addParam(createParamCentered<semi::PadMagenta>(at(L::STORE), module, Setbook::STORE_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::LIVE), module, Setbook::LIVE_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::PAD), module, Setbook::PAD_PARAM));
		addChild(createLightCentered<semi::AmberLedSmall>(at(L::PAD_LED), module, Setbook::PAD_LIGHT));
		addParam(createParamCentered<semi::KnobM>(at(L::TIME), module, Setbook::TIME_PARAM));
		auto* cv = createParamCentered<semi::KnobS>(at(L::CURVE), module, Setbook::CURVE_PARAM);
		cv->snap = true;
		addParam(cv);
		addParam(createParamCentered<semi::KnobS>(at(L::THRESH), module, Setbook::THRESH_PARAM));
		addInput(createInputCentered<semi::BusJack>(at(L::BUS_IN), module, Setbook::BUS_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::SCN), module, Setbook::SCN_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RCL), module, Setbook::RCL_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::NXT), module, Setbook::NXT_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::X), module, Setbook::X_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::Y), module, Setbook::Y_INPUT));
		addOutput(createOutputCentered<semi::BusJack>(at(L::BUS_OUT), module, Setbook::BUS_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::END), module, Setbook::END_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::MORPH), module, Setbook::MORPH_OUTPUT));
	}

#ifndef METAMODULE
	// FAMILY: gather the SEMI modules in the patch about once a second (UI thread)
	void step() override {
		if (auto* m = dynamic_cast<Setbook*>(module))
			if (refresh-- <= 0) {
				refresh = 60;
				m->refreshMembers();
			}
		ModuleWidget::step();
	}
#endif

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Setbook*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Scenes"));
		for (int i = 0; i < kScenes; i++) {
			menu->addChild(createSubmenuItem(string::f("%d  %s", i + 1, m->scenes[i].name), m->scenes[i].used ? "" : "empty", [=](Menu* sub) {
				std::vector<std::string> names;
				for (int k = 0; k < 16; k++)
					names.push_back(sb::defaultSceneName(k));
				sub->addChild(createIndexSubmenuItem("Name", names,
				    [=]() {
					    for (int k = 0; k < 16; k++)
						    if (!std::strcmp(m->scenes[i].name, sb::defaultSceneName(k)))
							    return (size_t) k;
					    return (size_t) 0;
				    },
				    [=](size_t k) { std::snprintf(m->scenes[i].name, sizeof m->scenes[i].name, "%s", sb::defaultSceneName((int) k)); }));
				sub->addChild(createMenuItem("Add a cue here at the current bar", "", [=]() { m->addCue(i, std::floor(m->snap.read().bar)); }));
				sub->addChild(createMenuItem("Clear", "", [=]() {
					m->scenes[i].used = false;
#ifndef METAMODULE
					m->fam[(size_t) i].n = 0;
#endif
				}));
			}));
		}

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Morph"));
		menu->addChild(createIndexSubmenuItem("Launch", {"NOW", "next beat", "next bar", "next 2 bars", "next 4 bars"},
		    [=]() { return (size_t) m->launch; }, [=](size_t i) { m->launch = (int) i; }));
		menu->addChild(createIndexSubmenuItem("Stepped channels", {"JUMP at the threshold", "WALK through the steps"},
		    [=]() { return (size_t) (m->steppedWalk ? 1 : 0); }, [=](size_t i) { m->steppedWalk = i == 1; }));
		menu->addChild(createIndexSubmenuItem("Tempo without BUS", {"90", "120", "140", "160", "172", "174"},
		    [=]() { return (size_t) m->tempoIndex; }, [=](size_t i) { m->tempoIndex = (int) i; }));
		menu->addChild(createSubmenuItem("Channels", "", [=](Menu* sub) {
			for (int c = 0; c < kCh; c++) {
				sub->addChild(createSubmenuItem(string::f("Channel %d", c + 1), kKindNames[m->kind[c]], [=](Menu* s2) {
					s2->addChild(createIndexSubmenuItem("Kind", {kKindNames[0], kKindNames[1], kKindNames[2]},
					    [=]() { return (size_t) m->kind[c]; }, [=](size_t i) { m->kind[c] = (int) i; }));
					s2->addChild(createIndexSubmenuItem("Range", {"0..10 V", "±5 V"},
					    [=]() { return (size_t) m->bipolar[c]; }, [=](size_t i) { m->bipolar[c] = (int) i; }));
				}));
			}
		}));
		menu->addChild(createSubmenuItem("Pad corners", "", [=](Menu* sub) {
			const char* names[4] = {"A (X 0, Y 0)", "B (X 10, Y 0)", "C (X 0, Y 10)", "D (X 10, Y 10)"};
			for (int k = 0; k < 4; k++)
				sub->addChild(createIndexSubmenuItem(names[k], {"1", "2", "3", "4", "5", "6", "7", "8"},
				    [=]() { return (size_t) m->padCorner[k]; }, [=](size_t i) { m->padCorner[k] = (int) i; }));
		}));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("BUS and cues"));
		menu->addChild(createBoolPtrMenuItem("BUS SCENE recalls scene (n mod 8)", "", &m->busSceneRecalls));
		menu->addChild(createIndexSubmenuItem("Next scene on EPOCH turns", {"off", "4+ bars", "8+ bars", "16+ bars", "32 bars"},
		    [=]() { return (size_t) m->turnNext; }, [=](size_t i) { m->turnNext = (int) i; }));
		menu->addChild(createIndexSubmenuItem("BUS OUT SCENE", {"thru", "SETBOOK's scene"},
		    [=]() { return (size_t) m->busSceneOut; }, [=](size_t i) { m->busSceneOut = (int) i; }));
		menu->addChild(createBoolPtrMenuItem("Cues on", "", &m->cuesOn));
		menu->addChild(createBoolPtrMenuItem("Chase (cut to the cue's scene on start / locate)", "", &m->chase));
		menu->addChild(createMenuItem("Add a cue: selected scene at the current bar", "", [=]() {
			m->addCue(m->selected, std::floor(m->snap.read().bar));
		}));
		menu->addChild(createMenuItem("Remove the last cue", "", [=]() {
			const int n = m->nCues.load();
			if (n > 0)
				m->nCues = n - 1;
		}));
		menu->addChild(createMenuItem("Clear cues", "", [=]() { m->nCues = 0; }));
#ifndef METAMODULE
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel(string::f("Family: %d SEMI modules", m->audioCount)));
		menu->addChild(createBoolPtrMenuItem("Scenes capture / morph the family's knobs", "", &m->famOn));
		menu->addChild(createBoolPtrMenuItem("Include CONDUIT and BLACKBOX", "", &m->famSystem));
#endif
	}
};

Model* modelSetbook = createModel<Setbook, SetbookWidget>("Setbook");
