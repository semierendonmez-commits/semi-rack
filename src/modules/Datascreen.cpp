// DATASCREEN — Rack / MetaModule port of DATASCREEN, the family's visual engine hub: one big screen
// that renders the 26 family scenes (PINDATA 01-12, FLOATDAT films 13-20, case / reel / orbit /
// attractor / vortex / ghost 21-26) from the audio it sits on, the events of its siblings and a file.
// Model: engines/datascreen (namespace dsc): the plugin's Ingest (analysis, the made sections) and
// the scenes' Motion without JUCE; DataBlob vendored from Source/data. The scenes are redrawn here in
// NanoVG after Source/scenes/*.cpp, each as a pure function of one published Frame (stable frames).
//
// Panel:   SCREEN (the scenes with a HUD: scene, bar / BPM, steps, lanes, the file).
//          M row: SCENE, VARIANT (auto / 1-3), CHANGE (manual, every 1/2/4/8 bars, on step, random,
//          on onset, on BUS turn), DENSITY, REACT, SPEED. Buttons NEXT (next scene in the pool),
//          FLSH (flash), FRZ (freeze the picture).
// Jacks:   IN L / R (audio, +-5 V; R normalled to L), CLK (1/16 by default, PPQN in the menu), RST,
//          TRIG (poly: channel k = lane k hit, velocity = voltage / 10), STEP (a sibling's STEP CV:
//          1/12 V per step -> the Markov walk), HEAD (0-10 V = read head over the file),
//          SCENE (0-10 V over the 26 scenes), BUS (FamilyBus CV: POS clocks the picture, ENERGY
//          drives it, SCENE can be followed, EVENT flashes / cuts).
//          Out: L / R thru, CUT (trigger on every scene cut), BUS (thru, or made from the own
//          clock when nothing is patched; SCENE channel = the scene on screen).
// Menu:    data file (any file; WAV decoded for READ AS audio), READ AS, scan (hold / step / run /
//          follow) and rate, scene pool, palette, text, follow BUS scene, flash on events, clock.
// Dropped: the video output (window / full screen / resolution / FPS), the shared-memory bus and
//          the sibling snapshots (every section is made from the data and the audio, as the plugin
//          does when no sibling sends one), MIDI in (TRIG / STEP instead), the GPU renderer, the
//          themes beyond six palettes, camera drag, strobe, blackout.
#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/FamilyBusCV.hpp"
#include "../common/Sample.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/DatascreenLayout.hpp"
#include "datascreen/DataLoad.h"
#include "datascreen/Model.h"

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>

namespace L = semi::layout::datascreen;
using semi::Screen;
namespace pf = semi::platform;

namespace {

#ifdef METAMODULE
constexpr size_t kMaxFileBytes = 256u * 1024u;   // MetaModule RAM is shared by the whole patch
constexpr size_t kMaxAudioFrames = 48000u * 2u;
#else
constexpr size_t kMaxFileBytes = 8u * 1024u * 1024u;
constexpr size_t kMaxAudioFrames = 48000u * 60u;
#endif

const int kPpqn[] = {1, 2, 4, 8, 24};
const float kBpms[] = {60.f, 90.f, 110.f, 120.f, 128.f, 140.f, 160.f, 174.f};
const float kScanRates[] = {1.f, 2.f, 4.f, 8.f, 16.f, 32.f, 64.f};
constexpr int kNumPalettes = 6;
const char* const kPaletteNames[kNumPalettes] = {"Cyber", "Datamatics", "Mono", "Terminal", "Ice", "Negative"};

struct Snapshot {
	dsc::Frame f;
	int palette = 0, textMode = 0;
	bool flashOn = true, clocked = false, busIn = false, frozen = false;
};

std::vector<std::string> names(std::initializer_list<const char*> l) {
	return std::vector<std::string>(l.begin(), l.end());
}

std::string sceneLabel(int i) { return string::f("%02d %s", i + 1, dsc::sceneInfo(i).name); }

} // namespace

struct Datascreen : Module {
	enum ParamId { SCENE_PARAM, VARIANT_PARAM, CHANGE_PARAM, DENSITY_PARAM, REACT_PARAM, SPEED_PARAM,
	               NEXT_PARAM, FREEZE_PARAM, FLASH_PARAM, PARAMS_LEN };
	enum InputId { L_INPUT, R_INPUT, CLK_INPUT, RST_INPUT, TRIG_INPUT, STEP_INPUT, HEAD_INPUT, SCENE_INPUT, BUS_INPUT, INPUTS_LEN };
	enum OutputId { L_OUTPUT, R_OUTPUT, CUT_OUTPUT, BUS_OUTPUT, OUTPUTS_LEN };
	enum LightId { FREEZE_LIGHT, LIGHTS_LEN };

	dsc::Model ingest;   // not `model`: Module::model is the plugin Model*
	dsc::Director director;
	dsc::Settings cfg;

	semi::Slot<dsc::DataBlob> slot;
	semi::Loader<dsc::DataBlob> loader;
	std::atomic<int> readAs{0};
	std::atomic<bool> clearRequest{false};
	bool blobOn = false;

	// module state (json)
	bool frozen = false, followBus = false, flashOn = true;
	int pool = 0, palette = 0, textMode = 0, ppqnIdx = 2, bpmIdx = 3, scanRateIdx = 3;

	// clock
	dsp::SchmittTrigger clkTrig, rstTrig, trigs[dsc::kLanes], busEvTrig;
	dsp::BooleanTrigger nextBtn, freezeBtn, flashBtn;
	int64_t sampleCount = 0, lastEdge = -1, lastBusMove = -1000000;
	double period = 0.0, edgeBeats = 0.0, freeBeats = 0.0, busBeats = 0.0, lastPos = -1.0;
	double busBpm = 120.0, lastUiBusBeats = 0.0;
	int lastStepCv = -1;
	bool stepEvent = false, turnEvent = false;
	int lastSceneKnob = -1, lastSceneCv = -1, lastBusScene = -1;
	dsp::PulseGenerator cutPulse;

	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;

	~Datascreen() override {
		// background jobs first (and wait for a run in flight): they write into this module
		loader.stopAndWait();
	}

	Datascreen() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		std::vector<std::string> sc;
		for (int i = 0; i < dsc::kNumScenes; i++)
			sc.push_back(sceneLabel(i));
		configSwitch(SCENE_PARAM, 0.f, dsc::kNumScenes - 1, 0.f, "Scene", sc);
		configSwitch(VARIANT_PARAM, 0.f, 3.f, 0.f, "Variant", {"Auto (per cut)", "1", "2", "3"});
		std::vector<std::string> ch;
		for (int i = 0; i < dsc::kNumChanges; i++)
			ch.push_back(dsc::changeName(i));
		configSwitch(CHANGE_PARAM, 0.f, dsc::kNumChanges - 1, 3.f, "Change", ch);
		configParam(DENSITY_PARAM, 0.f, 1.f, 0.6f, "Density", " %", 0.f, 100.f);
		configParam(REACT_PARAM, 0.f, 2.f, 1.f, "React", " %", 0.f, 100.f);
		configParam(SPEED_PARAM, 0.f, 2.f, 1.f, "Speed", " %", 0.f, 100.f);
		configButton(NEXT_PARAM, "Next scene (in the pool)");
		configButton(FREEZE_PARAM, "Freeze the picture");
		configButton(FLASH_PARAM, "Flash");
		paramQuantities[DENSITY_PARAM]->description = "How much data a scene draws";
		paramQuantities[REACT_PARAM]->description = "How hard the audio drives the picture";
		paramQuantities[SPEED_PARAM]->description = "Free-running motion (attractor, read head drift)";
		configInput(L_INPUT, "Audio left");
		configInput(R_INPUT, "Audio right (normalled to left)");
		configInput(CLK_INPUT, "Clock (1/16 by default)");
		configInput(RST_INPUT, "Reset (bar 1, flash)");
		configInput(TRIG_INPUT, "Lane hits (poly: channel k = lane k, 0-10 V velocity)");
		configInput(STEP_INPUT, "Step (a sibling's STEP CV, 1/12 V per step)");
		configInput(HEAD_INPUT, "Read head (0-10 V over the file)");
		configInput(SCENE_INPUT, "Scene CV (0-10 V over the 26 scenes)");
		configInput(BUS_INPUT, "FamilyBus (POS, ENERGY, SCENE, EVENT)");
		configOutput(L_OUTPUT, "Audio left (thru)");
		configOutput(R_OUTPUT, "Audio right (thru)");
		configOutput(CUT_OUTPUT, "Scene cut trigger");
		configOutput(BUS_OUTPUT, "FamilyBus (thru; SCENE = this screen's scene)");
		configBypass(L_INPUT, L_OUTPUT);
		configBypass(R_INPUT, R_OUTPUT);

		loader.attach(this, &slot, [this](const std::string& path, dsc::DataBlob& b) { return buildBlob(path, b); });
		ingest.prepare(48000.0);
	}

	// ---- data (loader thread: Rack UI thread, MetaModule async thread) ------------------------
	bool buildBlob(const std::string& path, dsc::DataBlob& b) {
		const auto as = (dsc::pdata::ReadAs) clamp(readAs.load(), 0, 4);
		std::vector<uint8_t> bytes;
		if (!semi::readFileBytes(path, bytes, kMaxFileBytes))
			return false;
		dsc::pdata::setBytes(b, bytes.data(), bytes.size(), kMaxFileBytes, semi::fileName(path), path);
		if (FILE* f = std::fopen(path.c_str(), "rb")) {
			std::fseek(f, 0, SEEK_END);
			const long sz = std::ftell(f);
			std::fclose(f);
			if (sz > 0) {
				b.fileSize = (uint64_t) sz;
				b.truncated = b.fileSize > b.bytes.size();
			}
		}
		const bool isWav = bytes.size() > 12 && !std::memcmp(bytes.data(), "RIFF", 4) && !std::memcmp(bytes.data() + 8, "WAVE", 4);
		if (isWav && (as == dsc::pdata::ReadAs::Auto || as == dsc::pdata::ReadAs::Audio)) {
			semi::Sample s;
			if (semi::readWav(path, s, kMaxAudioFrames)) {
				std::vector<float> mono(s.frames);
				for (size_t i = 0; i < s.frames; i++)
					mono[i] = s.mono(i);
				dsc::pdata::build(b, as, &mono, s.rate, s.channels);
				return true;
			}
		}
		dsc::pdata::build(b, as);
		return true;
	}
	void loadPath(const std::string& path) { loader.request(path); }
	void reload() {
		if (!loader.path().empty())
			loader.request(loader.path());
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override { ingest.prepare(e.sampleRate); }

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		frozen = false;
		followBus = false;
		flashOn = true;
		pool = palette = textMode = 0;
		cfg = dsc::Settings();
		director.cutTo(0);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "path", json_string(loader.path().c_str()));
		json_object_set_new(root, "readAs", json_integer(readAs.load()));
		json_object_set_new(root, "frozen", json_boolean(frozen));
		json_object_set_new(root, "followBus", json_boolean(followBus));
		json_object_set_new(root, "flashOn", json_boolean(flashOn));
		json_object_set_new(root, "pool", json_integer(pool));
		json_object_set_new(root, "palette", json_integer(palette));
		json_object_set_new(root, "textMode", json_integer(textMode));
		json_object_set_new(root, "ppqn", json_integer(ppqnIdx));
		json_object_set_new(root, "bpm", json_integer(bpmIdx));
		json_object_set_new(root, "scan", json_integer(cfg.scan));
		json_object_set_new(root, "scanRate", json_integer(scanRateIdx));
		json_object_set_new(root, "scene", json_integer(director.scene));
		return root;
	}

	void dataFromJson(json_t* root) override {
		auto geti = [&](const char* k, int def, int lo, int hi) {
			json_t* j = json_object_get(root, k);
			return j ? clamp((int) json_integer_value(j), lo, hi) : def;
		};
		auto getb = [&](const char* k, bool def) {
			json_t* j = json_object_get(root, k);
			return j ? json_boolean_value(j) : def;
		};
		readAs.store(geti("readAs", 0, 0, 4));
		frozen = getb("frozen", false);
		followBus = getb("followBus", false);
		flashOn = getb("flashOn", true);
		pool = geti("pool", 0, 0, dsc::kNumPools - 1);
		palette = geti("palette", 0, 0, kNumPalettes - 1);
		textMode = geti("textMode", 0, 0, 2);
		ppqnIdx = geti("ppqn", 2, 0, 4);
		bpmIdx = geti("bpm", 3, 0, 7);
		cfg.scan = geti("scan", 3, 0, 3);
		scanRateIdx = geti("scanRate", 3, 0, 6);
		director.cutTo(geti("scene", 0, 0, dsc::kNumScenes - 1));
		lastSceneKnob = (int) params[SCENE_PARAM].getValue();
		if (json_t* p = json_object_get(root, "path")) {
			const std::string s = json_string_value(p) ? json_string_value(p) : "";
			if (!s.empty())
				loader.request(s);
		}
	}

	// ---- audio ----------------------------------------------------------------------------
	void process(const ProcessArgs& args) override {
		if (slot.poll()) {
			ingest.setBlob(slot.active);
			blobOn = true;
		}
		if (clearRequest.exchange(false)) {
			ingest.setBlob(nullptr);
			blobOn = false;
		}

		const float l = inputs[L_INPUT].getVoltage();
		const float r = inputs[R_INPUT].isConnected() ? inputs[R_INPUT].getVoltage() : l;
		outputs[L_OUTPUT].setVoltage(l);
		outputs[R_OUTPUT].setVoltage(r);
		ingest.pushAudio(clamp(l / 5.f, -2.f, 2.f), clamp(r / 5.f, -2.f, 2.f));

		sampleCount++;
		events(args);
		busOut();
		outputs[CUT_OUTPUT].setVoltage(cutPulse.process(args.sampleTime) ? 10.f : 0.f);

		if (ui.tick(args.sampleTime))
			frame(1.f / pf::kUiRateHz, args.sampleRate);
	}

	// triggers, clock edges, the BUS (every sample: edges must not be missed)
	void events(const ProcessArgs& args) {
		if (inputs[CLK_INPUT].isConnected() && clkTrig.process(inputs[CLK_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (lastEdge >= 0) {
				period = (double) (sampleCount - lastEdge);
				edgeBeats += 1.0 / kPpqn[ppqnIdx];
			}
			lastEdge = sampleCount;
		}
		if (rstTrig.process(inputs[RST_INPUT].getVoltage(), 0.1f, 1.f)) {
			edgeBeats = freeBeats = 0.0;
			lastEdge = -1;
			ingest.resetPulse();
		}
		const int nt = std::min(inputs[TRIG_INPUT].getChannels(), dsc::kLanes);
		for (int c = 0; c < nt; c++) {
			const float v = inputs[TRIG_INPUT].getVoltage(c);
			if (trigs[c].process(v, 0.1f, 1.f))
				ingest.laneHit(c, clamp(v / 10.f, 0.2f, 1.f));
		}
		if (inputs[STEP_INPUT].isConnected()) {
			const int s = clamp((int) std::lround(inputs[STEP_INPUT].getVoltage() * 12.f), 0, dsc::kSteps - 1);
			if (s != lastStepCv) {
				lastStepCv = s;
				ingest.stepEntered(s);
				stepEvent = true;
			}
		}
		if (inputs[BUS_INPUT].isConnected()) {
			auto& in = inputs[BUS_INPUT];
			const double pos = semi::bus::voltsToBars(in.getVoltage(semi::bus::kPos));
			if (lastPos >= 0.0) {
				double d = pos - lastPos;
				if (d < -semi::bus::kBarsPerCycle * 0.5)
					d += semi::bus::kBarsPerCycle;
				if (d > semi::bus::kBarsPerCycle * 0.5)
					d -= semi::bus::kBarsPerCycle;
				if (std::fabs(d) > 1e-7) {
					busBeats += d * 4.0;
					lastBusMove = sampleCount;
				}
			}
			else
				busBeats = pos * 4.0;
			lastPos = pos;
			const float ev = in.getChannels() > semi::bus::kEvent ? in.getVoltage(semi::bus::kEvent) : 0.f;
			if (busEvTrig.process(ev, 0.5f, 1.f)) {
				turnEvent = true;
				if (flashOn)
					ingest.flash(0.6f + 0.1f * (float) (semi::bus::voltsToTurn(ev) / 8));
			}
		}
		else
			lastPos = -1.0;
		freeBeats += args.sampleTime * kBpms[bpmIdx] / 60.f;
	}

	// BUS out: thru with this screen's scene, or made from the own clock
	void busOut() {
		auto& o = outputs[BUS_OUTPUT];
		if (!o.isConnected())
			return;
		o.setChannels(semi::bus::kChannels);
		if (inputs[BUS_INPUT].isConnected()) {
			auto& in = inputs[BUS_INPUT];
			for (int c = 0; c < semi::bus::kChannels; c++)
				o.setVoltage(c < in.getChannels() ? in.getVoltage(c) : 0.f, c);
		}
		else {
			o.setVoltage(semi::bus::posToVolts((float) (currentBeats() / 4.0)), semi::bus::kPos);
			o.setVoltage(0.f, semi::bus::kEnergy);
			o.setVoltage(0.f, semi::bus::kEvent);
		}
		o.setVoltage(semi::bus::sceneToVolts(director.scene % 20), semi::bus::kScene);
	}

	double currentBeats() {
		if (inputs[CLK_INPUT].isConnected()) {
			if (lastEdge < 0 || period <= 0.0)
				return edgeBeats;
			const double frac = std::min(1.0, (double) (sampleCount - lastEdge) / period);
			return edgeBeats + frac / kPpqn[ppqnIdx];
		}
		if (inputs[BUS_INPUT].isConnected())
			return busBeats;
		return freeBeats;
	}

	// ---- the display rate ------------------------------------------------------------------
	void frame(float dt, float sampleRate) {
		// buttons
		if (nextBtn.process(params[NEXT_PARAM].getValue() > 0.f)) {
			director.next(pool, false, (uint32_t) sampleCount);
			cutPulse.trigger(1e-3f);
		}
		if (freezeBtn.process(params[FREEZE_PARAM].getValue() > 0.f))
			frozen = !frozen;
		if (flashBtn.process(params[FLASH_PARAM].getValue() > 0.f))
			ingest.flash(1.f);
		lights[FREEZE_LIGHT].setBrightness(frozen ? 1.f : 0.f);

		// the clock
		bool playing = true;
		double bpm = kBpms[bpmIdx];
		const bool clocked = inputs[CLK_INPUT].isConnected();
		const bool busIn = inputs[BUS_INPUT].isConnected();
		if (clocked) {
			playing = lastEdge >= 0 && period > 0.0 && (double) (sampleCount - lastEdge) < std::max(2.5 * period, 0.25 * sampleRate);
			if (period > 0.0)
				bpm = 60.0 * sampleRate / (period * kPpqn[ppqnIdx]);
		}
		else if (busIn) {
			playing = (double) (sampleCount - lastBusMove) < 0.25 * sampleRate;
			const double rate = (busBeats - lastUiBusBeats) / dt * 60.0;
			if (playing && rate > 10.0 && rate < 600.0)
				busBpm += (rate - busBpm) * 0.2;
			bpm = busBpm;
			ingest.setEnergy(clamp(inputs[BUS_INPUT].getVoltage(semi::bus::kEnergy) / 10.f, 0.f, 1.f));
		}
		lastUiBusBeats = busBeats;
		if (!busIn)
			ingest.setEnergy(-1.f);
		ingest.setClock(currentBeats(), bpm, playing);
		ingest.setHeadNorm(inputs[HEAD_INPUT].isConnected() ? clamp(inputs[HEAD_INPUT].getVoltage() / 10.f, 0.f, 1.f) : -1.f);

		// the scene: knob, CV, BUS, then the automatic cuts
		const uint32_t cutsBefore = director.cuts;
		const int knob = (int) params[SCENE_PARAM].getValue();
		if (knob != lastSceneKnob) {
			if (lastSceneKnob >= 0)
				director.cutTo(knob);
			lastSceneKnob = knob;
		}
		if (inputs[SCENE_INPUT].isConnected()) {
			const int s = clamp((int) (inputs[SCENE_INPUT].getVoltage() / 10.f * dsc::kNumScenes), 0, dsc::kNumScenes - 1);
			if (s != lastSceneCv)
				director.cutTo(s);
			lastSceneCv = s;
		}
		else
			lastSceneCv = -1;
		if (followBus && busIn && inputs[BUS_INPUT].getChannels() > semi::bus::kScene) {
			const int s = semi::bus::voltsToScene(inputs[BUS_INPUT].getVoltage(semi::bus::kScene));
			if (s != lastBusScene)
				director.cutTo(s);
			lastBusScene = s;
		}

		cfg.react = params[REACT_PARAM].getValue();
		cfg.speed = params[SPEED_PARAM].getValue();
		cfg.scanRate = kScanRates[scanRateIdx];
		cfg.freeze = frozen;
		Snapshot& s = snap.writeSlot();
		ingest.update(s.f, dt, cfg);
		director.tick(s.f, (int) params[CHANGE_PARAM].getValue(), pool, stepEvent, ingest.onsetNow(), turnEvent);
		stepEvent = turnEvent = false;
		if (director.cuts != cutsBefore) {
			cutPulse.trigger(1e-3f);
			if (flashOn)
				ingest.flash(0.55f);
		}
		if (frozen)
			return;   // the picture holds: nothing new is published
		s.f.scene = director.scene;
		s.f.variant = director.variant((int) params[VARIANT_PARAM].getValue());
		s.f.cuts = director.cuts;
		s.f.density = params[DENSITY_PARAM].getValue();
		s.palette = palette;
		s.textMode = textMode;
		s.flashOn = flashOn;
		s.clocked = clocked;
		s.busIn = busIn;
		s.frozen = frozen;
		snap.publish();
	}
};

// ================================================================ drawing

namespace {

struct Pal {
	NVGcolor bg, fg, fg2, txt, dim, grid, hot, warm, paper, ink;
};

NVGcolor rgb(int r, int g, int b) { return nvgRGB((unsigned char) r, (unsigned char) g, (unsigned char) b); }
NVGcolor inv(NVGcolor c) { return nvgRGBf(1.f - c.r, 1.f - c.g, 1.f - c.b); }

Pal makePal(int p, bool invert) {
	namespace th = semi::theme;
	Pal c;
	switch (p) {
		case 1:   // datamatics: black and white, pure red and blue accents
			c = {rgb(0, 0, 0), rgb(255, 255, 255), rgb(130, 130, 130), rgb(220, 220, 220), rgb(110, 110, 110), rgb(38, 38, 38),
			     rgb(255, 42, 31), rgb(51, 89, 255), rgb(240, 240, 240), rgb(0, 0, 0)};
			break;
		case 2:   // mono
			c = {rgb(0, 0, 0), rgb(235, 235, 235), rgb(120, 120, 120), rgb(200, 200, 200), rgb(100, 100, 100), rgb(36, 36, 36),
			     rgb(255, 255, 255), rgb(170, 170, 170), rgb(225, 225, 225), rgb(0, 0, 0)};
			break;
		case 3:   // terminal amber
			c = {rgb(8, 5, 0), rgb(255, 176, 64), rgb(130, 82, 20), rgb(230, 160, 70), rgb(120, 80, 30), rgb(40, 26, 8),
			     rgb(255, 240, 210), rgb(255, 110, 40), rgb(255, 176, 64), rgb(8, 5, 0)};
			break;
		case 4:   // ice
			c = {rgb(2, 8, 16), rgb(190, 232, 255), rgb(64, 112, 144), rgb(170, 210, 235), rgb(80, 110, 140), rgb(18, 34, 52),
			     rgb(255, 255, 255), rgb(106, 168, 255), rgb(200, 236, 255), rgb(2, 8, 16)};
			break;
		case 5:   // negative: the cyber palette on white
			c = {rgb(236, 240, 240), rgb(10, 60, 66), rgb(80, 140, 140), rgb(30, 50, 56), rgb(110, 130, 140), rgb(200, 212, 214),
			     rgb(214, 20, 100), rgb(170, 110, 10), rgb(10, 20, 24), rgb(236, 240, 240)};
			break;
		default:
			c = {th::screen(), th::cyan(), th::cyanDim(), th::text(), th::dim(), th::grid(), th::magenta(), th::amber(), th::text(), th::screen()};
			break;
	}
	if (invert) {
		NVGcolor* all[] = {&c.bg, &c.fg, &c.fg2, &c.txt, &c.dim, &c.grid, &c.hot, &c.warm, &c.paper, &c.ink};
		for (auto* x : all)
			*x = inv(*x);
	}
	return c;
}

constexpr float kTau = 6.2831853f;

// One scene's drawing context: the box, the palette, the frame, budgets.
struct D {
	NVGcontext* vg;
	Screen* s;
	const dsc::Frame& f;
	Pal c;
	float x, y, w, h;
	int v;
	float dens;
	bool txt;    // scene text on
	bool mm = pf::kMetaModule;

	float r() const { return x + w; }
	float b() const { return y + h; }
	float cx() const { return x + w * 0.5f; }
	float cy() const { return y + h * 0.5f; }
	// a count for this platform: rack at full density, MetaModule capped
	int n(int rackMax, int mmMax, int min = 1) const {
		const int top = mm ? mmMax : rackMax;
		return std::max(min, (int) std::lround(top * (0.4f + 0.6f * dens)));
	}
	void rect(float x0, float y0, float ww, float hh, NVGcolor col) const { Screen::fillRect(vg, x0, y0, ww, hh, col); }
	void line(float x0, float y0, float x1, float y1, NVGcolor col, float wd = pf::kThinStroke) const {
		Screen::line(vg, x0, y0, x1, y1, col, wd);
	}
	void text(float tx, float ty, const std::string& str, float size, NVGcolor col, int align = NVG_ALIGN_LEFT | NVG_ALIGN_TOP) const {
		s->text(vg, tx, ty, str, size, col, align);
	}
	void path(const float* xs, const float* ys, int count, NVGcolor col, float wd = pf::kStroke) const {
		Screen::path(vg, xs, ys, count, col, wd);
	}
	void ring(float px, float py, float rad, NVGcolor col, float wd = pf::kThinStroke) const {
		nvgBeginPath(vg);
		nvgCircle(vg, px, py, std::max(0.5f, rad));
		nvgStrokeColor(vg, col);
		nvgStrokeWidth(vg, wd);
		nvgStroke(vg);
	}
	void disc(float px, float py, float rad, NVGcolor col) const {
		nvgBeginPath(vg);
		nvgCircle(vg, px, py, std::max(0.5f, rad));
		nvgFillColor(vg, col);
		nvgFill(vg);
	}
	void dot(float px, float py, NVGcolor col, float size = 0.f) const {
		const float sz = size > 0.f ? size : (mm ? 2.4f : 1.6f);
		rect(px - sz * 0.5f, py - sz * 0.5f, sz, sz, col);
	}
	void frameBox(float x0, float y0, float ww, float hh, NVGcolor col, float t = 1.f) const {
		rect(x0, y0, ww, t, col);
		rect(x0, y0 + hh - t, ww, t, col);
		rect(x0, y0, t, hh, col);
		rect(x0 + ww - t, y0, t, hh, col);
	}
	// data helpers
	uint8_t byte(int i) const { return f.bytes[i & 255]; }
	float u(int i) const { return f.bytes[i & 255] / 255.f; }
	// a byte as an even 0..1 over the window's distribution (SceneCtx::u)
	float ue(int i) const {
		const int v = byte(i);
		const float lo = f.cdf[v] / 256.f, hi = f.cdf[v + 1] / 256.f;
		const float j = std::fmod((float) (i % 1009) * 0.6180339887f, 1.f);
		return lo + (hi - lo) * j;
	}
	int bit(int k) const { return (int) ((f.bits[(k >> 6) & 7] >> (k & 63)) & 1u); }
	uint64_t word(int row) const { return f.bits[row & 7]; }
	float band(float x01) const { return f.bands[clamp((int) (x01 * dsc::kBands), 0, dsc::kBands - 1)] / 255.f; }
	int six() const { return (int) std::floor(f.beats * 4.0); }
	bool laneLit(int l) const { return ((f.laneFlash >> l) & 1) || f.laneLevel[l] > 200; }
};

// horizontal runs of set bits as rects
void bitRow(const D& d, uint64_t w, int nbits, float x, float y, float cw, float h, NVGcolor col) {
	int k = 0;
	while (k < nbits) {
		if (!((w >> k) & 1u)) {
			k++;
			continue;
		}
		int e = k;
		while (e < nbits && ((w >> e) & 1u))
			e++;
		d.rect(x + k * cw, y, (e - k) * cw - (cw > 2.5f ? 0.5f : 0.f), h, col);
		k = e;
	}
}

std::string hex2(int v) { return string::f("%02X", v & 0xff); }
std::string hex6(uint64_t v) { return string::f("%06llX", (unsigned long long) (v & 0xFFFFFFull)); }
char printable(uint8_t v) { return v >= 32 && v < 127 ? (char) v : '.'; }
float frac(double v) { return (float) (v - std::floor(v)); }
float sat(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

// ---------------------------------------------------------------- 01-12 (PINDATA)

void sBarcode(D& d) {
	const auto& f = d.f;
	const int sh = d.six() * 2;
	if (d.v == 0) {
		const int nb = d.n(128, 72, 32);
		const float cw = d.w / nb;
		for (int i = 0; i < nb; i++) {
			if (!d.bit(i + sh))
				continue;
			const bool loud = d.band((float) i / nb) > 0.55f;
			d.rect(d.x + i * cw, d.y, std::max(1.f, cw - (cw > 3.f ? 0.6f : 0.f)), d.h - (d.txt ? 10.f : 0.f), loud ? d.c.fg : d.c.fg2);
		}
		if (d.txt)
			for (int k = 0; k < 6; k++)
				d.text(d.x + k * d.w / 6.f, d.b() - 9.f, hex6(f.headByte + (uint64_t) (k * 8 + sh / 8)), 9.f, d.c.dim);
	}
	else if (d.v == 1) {
		const int nb = d.n(96, 64, 32);
		const float cw = d.w / nb, hh = d.h * 0.5f - 6.f;
		for (int i = 0; i < nb; i++) {
			if (d.bit(i + sh))
				d.rect(d.x + i * cw, d.y, std::max(1.f, cw - 0.5f), hh, d.c.fg);
			if (!d.bit(256 + i - sh))
				d.rect(d.x + i * cw, d.b() - hh, std::max(1.f, cw - 0.5f), hh, f.kick > 0.6f ? d.c.hot : d.c.fg2);
		}
		d.rect(d.x, d.cy() - 0.5f, d.w, 1.f, d.c.grid);
		if (d.txt)
			d.text(d.cx(), d.cy(), string::f("%s  %.2f BITS/BYTE", hex6(f.headByte).c_str(), f.entropy), 9.f, d.c.txt,
			       NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
	}
	else {
		const float lv[4] = {f.low, f.mid, f.high, f.level};
		const float rh = d.h / 4.f;
		for (int r = 0; r < 4; r++) {
			const int off = (d.six() * (r + 1)) & 63;
			const uint64_t w = (d.word(r) >> off) | (off ? d.word(r + 1) << (64 - off) : 0);
			const float th = rh * (0.35f + 0.6f * sat(lv[r] * 2.f));
			bitRow(d, w, 64, d.x, d.y + r * rh + (rh - th) * 0.5f, d.w / 64.f, th, lv[r] > 0.3f ? d.c.fg : d.c.fg2);
		}
	}
}

void sMatrix(D& d) {
	const auto& f = d.f;
	auto squares = [&](float x0, float y0, float side, int mode) {
		const float c = side / 17.f;
		for (int i = 0; i < 17; i++) {
			if (i == f.lastTo && mode == 0)
				d.rect(x0, y0 + i * c, side, c, d.c.grid);
			for (int j = 0; j < 17; j++) {
				int val = f.matrix[i][j];
				if (mode == 1 && val < 50)
					val = 0;
				if (mode == 2) {
					val = 0;
					for (int k = 1; k < f.pathN; k++)
						if (f.path[k - 1] + 1 == i && f.path[k] + 1 == j)
							val = 100;
				}
				if (!val)
					continue;
				const float q = std::max(1.2f, (c - 1.f) * std::sqrt(val / 100.f));
				const bool last = i == f.lastFrom && j == f.lastTo;
				d.rect(x0 + j * c + (c - q) * 0.5f, y0 + i * c + (c - q) * 0.5f, q, q, last ? d.c.hot : (i == f.lastTo ? d.c.txt : d.c.fg2));
			}
		}
		d.frameBox(x0, y0, side, side, d.c.grid);
	};
	if (d.v == 0) {
		// fan: states on two lines, every weight a line from row to column
		const float ty = d.y + 12.f, by = d.b() - 12.f, dx = d.w / 17.f;
		int lines = 0;
		const int maxL = d.n(160, 48);
		for (int i = 0; i < 17 && lines < maxL; i++)
			for (int j = 1; j < 17 && lines < maxL; j++) {
				const int val = f.matrix[i][j];
				if (val < 8)
					continue;
				d.line(d.x + (i + 0.5f) * dx, ty, d.x + (j + 0.5f) * dx, by, val > 60 ? d.c.fg2 : d.c.grid, val > 60 ? pf::kStroke : pf::kThinStroke);
				lines++;
			}
		if (f.lastFrom >= 0 && f.lastTo >= 0)
			d.line(d.x + (f.lastFrom + 0.5f) * dx, ty, d.x + (f.lastTo + 0.5f) * dx, by, d.c.hot, pf::kStroke + 0.8f);
		for (int i = 0; i < 17; i++) {
			const bool cur = i == f.lastTo;
			d.rect(d.x + i * dx + 1.f, ty - 4.f, dx - 2.f, 4.f, cur ? d.c.hot : d.c.fg2);
			d.rect(d.x + i * dx + 1.f, by, dx - 2.f, 4.f, cur ? d.c.hot : d.c.fg2);
			if (d.txt && (i % 2 == 0 || !d.mm))
				d.text(d.x + (i + 0.5f) * dx, d.y, string::f("%d", i), 9.f, cur ? d.c.hot : d.c.dim, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		}
		if (d.txt)
			d.text(d.r(), d.b(), f.learned ? "LEARNED FROM STEPS" : "READ FROM DATA", 9.f, d.c.dim, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
	}
	else if (d.v == 1) {
		const float side = std::min(d.h, d.w * 0.5f);
		squares(d.x, d.y, side, 0);
		const float sx = d.x + side + 6.f, sw = d.r() - sx;
		const int nb = d.mm ? 24 : 48;
		const float bw = sw / nb;
		for (int k = 0; k < nb; k++) {
			const float v = d.band((float) k / nb);
			d.rect(sx + k * bw, d.b() - d.h * v, std::max(1.f, bw - 1.f), d.h * v, v > 0.7f ? d.c.warm : d.c.fg2);
		}
	}
	else {
		const float side = std::min(d.h - 10.f, (d.w - 12.f) / 3.f);
		static const char* const lbl[3] = {"WEIGHTS", "STRONG", "PATH"};
		for (int m = 0; m < 3; m++) {
			const float x0 = d.x + m * (side + 6.f);
			squares(x0, d.y, side, m);
			if (d.txt)
				d.text(x0, d.y + side + 1.f, lbl[m], 9.f, d.c.dim);
		}
	}
}

void project(const D& d, float px, float py, float pz, float yaw, float pitch, float scale, float& sx, float& sy, float& depth) {
	const float stretch = std::min(1.8f, d.w / std::max(1.f, d.h));
	const float cy = std::cos(yaw), sy0 = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
	const float x1 = px * cy + pz * sy0, z1 = -px * sy0 + pz * cy;
	const float y2 = py * cp - z1 * sp, z2 = py * sp + z1 * cp;
	const float persp = 2.2f / (2.8f + z2);
	sx = d.cx() + x1 * scale * persp * stretch;
	sy = d.cy() + y2 * scale * persp;
	depth = z2;
}

void sSpace(D& d) {
	const auto& f = d.f;
	const float yaw = (float) (f.beats * 0.19634954), pitch = 0.45f + 0.15f * std::sin((float) f.beats * 0.2f);
	const float scale = std::min(d.w, d.h) * (0.85f + 0.2f * f.kick);
	const int np = d.n(320, 110, 24);
	for (int i = 0; i < np; i++) {
		float px, py, pz;
		if (d.v == 0) {
			px = d.ue(i * 3) - 0.5f;
			py = d.ue(i * 3 + 1) - 0.5f;
			pz = d.ue(i * 3 + 2) - 0.5f;
		}
		else if (d.v == 1) {
			const int rings = 12, per = std::max(4, np / rings);
			const int k = i / per, j = i % per;
			const float z = frac((double) k / rings - f.beats * 0.125);
			const float a = kTau * j / per + k * 0.3f;
			const float rr = 0.25f + 0.25f * d.u(k * 7 + j) + 0.15f * d.band((float) j / per);
			px = std::cos(a) * rr;
			py = std::sin(a) * rr;
			pz = z * 2.f - 1.f;
		}
		else {
			const float th = kTau * d.ue(i * 2), ph = std::acos(2.f * d.ue(i * 2 + 1) - 1.f);
			const float rr = 0.45f * (1.f + 0.35f * f.low);
			px = rr * std::sin(ph) * std::cos(th);
			py = rr * std::cos(ph);
			pz = rr * std::sin(ph) * std::sin(th);
		}
		float sx, sy, dep;
		project(d, px, py, pz, d.v == 1 ? 0.f : yaw, d.v == 1 ? 0.f : pitch, scale, sx, sy, dep);
		if (sx < d.x || sx > d.r() || sy < d.y || sy > d.b())
			continue;
		const bool near = dep < -0.1f;
		d.dot(sx, sy, (i % 29 == 0 && f.hit > 0.5f) ? d.c.hot : near ? d.c.fg : d.c.fg2, near ? (d.mm ? 3.f : 2.4f) : 0.f);
	}
	if (d.txt)
		d.text(d.x, d.b(), string::f("%d POINTS  YAW %03d", np, (int) (std::fmod(yaw, kTau) * 57.3f)), 9.f, d.c.dim,
		       NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
}

void sNetwork(D& d) {
	const auto& f = d.f;
	if (d.v == 0) {
		const int nn = d.mm ? 14 : 20;
		float px[20], py[20];
		for (int i = 0; i < nn; i++) {
			px[i] = d.x + 8.f + (d.w - 16.f) * d.ue(i * 2 + f.bar % 4 * 40);
			py[i] = d.y + 6.f + (d.h - 12.f) * d.ue(i * 2 + 1 + f.bar % 4 * 40);
		}
		const float lim = std::min(d.w, d.h) * (0.32f + 0.2f * d.dens);
		int lines = 0;
		for (int i = 0; i < nn; i++)
			for (int j = i + 1; j < nn && lines < 48; j++) {
				const float dx = px[i] - px[j], dy = py[i] - py[j];
				if (dx * dx + dy * dy < lim * lim) {
					d.line(px[i], py[i], px[j], py[j], d.c.fg2);
					lines++;
				}
			}
		for (int i = 0; i < nn; i++) {
			const bool cur = f.stepCur >= 0 && i == f.stepCur % nn;
			d.disc(px[i], py[i], 2.f + 4.f * d.band((float) i / nn), cur ? d.c.hot : d.c.fg);
			if (d.txt && i % 3 == 0)
				d.text(px[i] + 5.f, py[i] - 4.f, hex2(d.byte(i * 5)), 9.f, d.c.dim);
		}
	}
	else if (d.v == 1) {
		const int np = d.mm ? 24 : 40;
		float xs[41], ys[41];
		const float rot = (float) f.beats * 0.1f;
		for (int ringI = 0; ringI < 6; ringI++) {
			const float base = std::min(d.w, d.h) * 0.08f * (ringI + 1);
			for (int k = 0; k <= np; k++) {
				const float a = kTau * k / np + rot * (ringI % 2 ? 1.f : -1.f);
				const float rr = base * (1.f + 0.35f * (d.u(ringI * 41 + k % np) - 0.5f) + 0.25f * f.level);
				xs[k] = d.cx() + std::cos(a) * rr * 1.4f;
				ys[k] = d.cy() + std::sin(a) * rr;
			}
			d.path(xs, ys, np + 1, ringI == f.sixteenth % 6 ? d.c.fg : d.c.fg2, pf::kThinStroke);
		}
	}
	else {
		const float cx = d.cx(), cy = d.cy(), rr = std::min(d.w, d.h) * 0.44f;
		float px[17], py[17];
		px[0] = cx;
		py[0] = cy;
		for (int k = 0; k < 16; k++) {
			const float a = -1.5707963f + k * kTau / 16.f;
			px[k + 1] = cx + rr * 1.3f * std::cos(a);
			py[k + 1] = cy + rr * std::sin(a);
		}
		int edges = 0;
		for (int i = 0; i < 17 && edges < 48; i++)
			for (int j = 1; j < 17 && edges < 48; j++) {
				const int val = f.matrix[i][j];
				if (val < 10 || i == j)
					continue;
				d.line(px[i], py[i], px[j], py[j], val > 60 ? d.c.fg2 : d.c.grid, val > 60 ? pf::kStroke : pf::kThinStroke);
				edges++;
			}
		if (f.lastFrom >= 0 && f.lastTo >= 0 && f.lastFrom != f.lastTo)
			d.line(px[f.lastFrom], py[f.lastFrom], px[f.lastTo], py[f.lastTo], d.c.hot, pf::kStroke + 0.8f);
		for (int k = 0; k < 16; k++) {
			const bool cur = k == f.stepCur;
			d.disc(px[k + 1], py[k + 1], cur ? 4.5f : 2.6f, cur ? d.c.hot : (f.trig[k] > 0 ? d.c.txt : d.c.dim));
		}
	}
}

void sScatter(D& d) {
	const auto& f = d.f;
	if (d.v == 0) {
		const int np = d.n(255, 110);
		for (int i = 0; i < np; i++)
			d.dot(d.x + d.w * d.ue(i), d.b() - d.h * d.ue(i + 1), (i == f.sixteenth * 8) ? d.c.hot : d.c.fg);
		if (d.txt) {
			d.text(d.x, d.b(), "B[N]", 9.f, d.c.dim, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
			d.text(d.x, d.y, "B[N+1]", 9.f, d.c.dim);
		}
	}
	else if (d.v == 1) {
		float xs[64], ys[64];
		const int nPts = d.mm ? 48 : 64;
		const float sc = std::min(d.w, d.h) * 0.48f;
		float energy = 0.f, pk = 1.f;
		for (int i = 0; i < dsc::kScope; i++) {
			energy += std::fabs((float) f.wave[i]);
			pk = std::max(pk, std::fabs((float) f.wave[i]));
		}
		const bool audio = energy > 64.f;
		for (int i = 0; i < nPts; i++) {
			const float a = audio ? f.wave[i] / pk : d.u(i) * 2.f - 1.f;
			const float b2 = audio ? f.wave[(i + 6) % dsc::kScope] / pk : d.u(i + 3) * 2.f - 1.f;
			xs[i] = d.cx() + a * sc * 1.4f;
			ys[i] = d.cy() - b2 * sc;
		}
		d.line(d.x, d.cy(), d.r(), d.cy(), d.c.grid);
		d.line(d.cx(), d.y, d.cx(), d.b(), d.c.grid);
		d.path(xs, ys, nPts, d.c.fg);
		if (d.txt)
			d.text(d.x, d.y, audio ? "X[T] / X[T+D]" : "DATA DELAY MAP", 9.f, d.c.dim);
	}
	else {
		uint8_t cnt[16][16]{};
		for (int i = 0; i < 255; i++) {
			uint8_t& c = cnt[d.byte(i) >> 4][d.byte(i + 1) >> 4];
			if (c < 255)
				c++;
		}
		const float cw = d.w / 16.f, ch = d.h / 16.f;
		for (int a = 0; a < 16; a++)
			for (int b = 0; b < 16; b++) {
				const int c = cnt[a][b];
				if (!c)
					continue;
				d.rect(d.x + a * cw + 0.5f, d.b() - (b + 1) * ch + 0.5f, cw - 1.f, ch - 1.f, c >= 4 ? d.c.fg : c >= 2 ? d.c.fg2 : d.c.grid);
			}
	}
}

void sField(D& d) {
	const auto& f = d.f;
	if (d.v == 0 || d.v == 2) {
		const float fw = d.v == 0 ? d.w * 0.68f : d.w;
		const float cw = fw / 64.f, ch = d.h / 32.f;
		const int rs = d.mm ? 2 : 1;   // MetaModule: every other row, twice as tall
		for (int r = 0; r < 32; r += rs) {
			const bool on = (f.gates >> r) & 1u;
			bitRow(d, f.grid[r], 64, d.x, d.y + r * ch, cw, std::max(1.f, ch * rs - 0.6f), on ? d.c.fg : d.c.fg2);
			d.rect(d.x + f.gridCol[r] * cw, d.y + r * ch, std::max(1.2f, cw), std::max(1.f, ch * rs - 0.6f), d.c.warm);
		}
		if (d.v == 2 && f.cursor >= 0) {
			const float px = d.x + (f.cursor % 64 + 0.5f) * cw, py = d.y + (f.cursor / 64 % 32 + 0.5f) * ch;
			d.line(px - 6.f, py, px + 6.f, py, d.c.hot, pf::kStroke);
			d.line(px, py - 6.f, px, py + 6.f, d.c.hot, pf::kStroke);
		}
		if (d.v == 0) {
			// the gate roll: 32 rows in time, newest at the right
			const float rx = d.x + fw + 6.f, rw = d.r() - rx;
			const int cols = d.mm ? 32 : 64;
			const float cw2 = rw / cols;
			for (int r = 0; r < 32; r += rs) {
				uint64_t w = 0;
				for (int k = 0; k < cols; k++) {
					const int hi = (f.histHead - (cols - 1 - k) + dsc::kHist * 2) % dsc::kHist;
					if ((f.gateHist[hi] >> r) & 1u)
						w |= 1ull << k;
				}
				bitRow(d, w, cols, rx, d.y + r * ch, cw2, std::max(1.f, ch * rs - 0.6f), d.c.fg2);
			}
		}
	}
	else {
		const float lw = d.txt ? 30.f : 8.f, roll = d.w * 0.28f;
		const float cw = (d.w - lw - roll - 6.f) / 64.f, rh = d.h / 9.f;
		d.rect(d.x + lw + f.autoStep * cw, d.y, std::max(1.5f, cw), d.h, d.c.grid);
		static const char* const tags[3] = {"WAVE", "SPEC", "SINE"};
		for (int v = 0; v < 9; v++) {
			const float yy = d.y + v * rh;
			const bool hitNow = (f.autoRows[v] >> f.autoStep) & 1u;
			bitRow(d, f.autoRows[v], 64, d.x + lw, yy + 1.f, cw, rh - 2.5f, hitNow ? d.c.txt : d.c.fg2);
			const float e = f.autoEnv[v] / 255.f;
			d.rect(d.x, yy + 1.f, 5.f, rh - 2.5f, d.c.grid);
			d.rect(d.x, yy + 1.f + (rh - 2.5f) * (1.f - e), 5.f, (rh - 2.5f) * e, d.c.hot);
			if (d.txt && v % 3 == 0)
				d.text(d.x + 7.f, yy + 1.f, tags[v / 3], 9.f, d.c.dim);
		}
		const float rx = d.r() - roll, cw2 = roll / 32.f;
		for (int v = 0; v < 9; v++) {
			uint64_t w = 0;
			for (int k = 0; k < 32; k++) {
				const int hi = (f.histHead - (31 - k) + dsc::kHist * 2) % dsc::kHist;
				if ((f.rowHist[hi] >> v) & 1u)
					w |= 1ull << k;
			}
			bitRow(d, w, 32, rx, d.y + v * rh + 1.f, cw2, rh - 2.5f, d.c.fg);
		}
	}
}

void sScan(D& d) {
	const auto& f = d.f;
	if (d.v == 0) {
		const int cols = d.mm ? 32 : 64;
		const float cw = d.w / cols, rh = d.h / 16.f;
		const int cur = (f.sixteenth * cols) / 16;
		for (int c = 0; c < cols; c++) {
			const int height = (int) (16.f * (0.25f + 0.75f * d.u(c * 3)) * (0.5f + 0.5f * d.band((float) c / cols)));
			for (int k = 0; k < height; k++)
				if (d.bit(c * 8 + k) || k == height - 1)
					d.rect(d.x + c * cw + cw * 0.25f, d.b() - (k + 1) * rh + 1.f, std::max(1.2f, cw * 0.5f), std::max(1.2f, rh * 0.5f),
					       c == cur ? d.c.hot : k == height - 1 ? d.c.fg : d.c.fg2);
		}
	}
	else if (d.v == 1) {
		const float lw = 8.f, bh = d.h / 3.f, cw = (d.w - lw) / 64.f;
		for (int b = 0; b < 3; b++) {
			const float mid = d.y + b * bh + bh * 0.5f, amp = bh * 0.42f;
			d.line(d.x + lw, mid, d.r(), mid, d.c.grid);
			for (int k = 0; k < 64; k++) {
				const float val = f.buf[b][k] / 127.f;
				if (std::fabs(val) < 0.05f)
					continue;
				const float hh = amp * std::fabs(val);
				d.rect(d.x + lw + k * cw, val > 0 ? mid - hh : mid, std::max(1.f, cw - 0.8f), hh, val > 0 ? d.c.txt : d.c.fg2);
			}
			const float g = f.bufGate[b] / 255.f;
			d.rect(d.x, mid - amp, 4.f, 2.f * amp, d.c.grid);
			d.rect(d.x, mid + amp - 2.f * amp * g, 4.f, 2.f * amp * g, d.c.hot);
		}
		const float rx = d.x + lw + (d.w - lw) * f.bufPhase;
		d.line(rx, d.y, rx, d.b(), d.c.warm, pf::kStroke);
	}
	else {
		const int cols = d.mm ? 18 : 32;
		const float cw = d.w / cols, gh = d.mm ? 9.f : 8.f;
		for (int c = 0; c < cols; c++) {
			const float speed = 0.25f + 0.75f * d.u(c * 7 + 1);
			const float head = frac(dsc::hashf((uint32_t) c * 97u) + f.beats * 0.25 * speed);
			const int len = 3 + (int) (d.u(c * 7 + 2) * 5.f);
			const float hy = d.y + (d.h + len * gh) * head - len * gh;
			for (int k = 0; k < len; k++) {
				const float yy = hy + k * gh;
				if (yy < d.y || yy > d.b() - gh)
					continue;
				const NVGcolor col = k == len - 1 ? d.c.fg : d.c.fg2;
				if (d.txt && !d.mm)
					d.text(d.x + c * cw + cw * 0.5f, yy, hex2(d.byte(c * 8 + k + d.six())).substr(0, 1), 9.f, col, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
				else
					d.rect(d.x + c * cw + cw * 0.3f, yy, cw * 0.4f, gh - 2.f, col);
			}
		}
	}
}

void sNumbers(D& d) {
	const auto& f = d.f;
	if (d.v == 0) {
		d.text(d.x, d.y, string::f("%03d.%d.%02d", f.bar + 1, (int) (f.beats - std::floor(f.beats / 4.0) * 4.0) + 1, f.sixteenth + 1),
		       std::min(30.f, d.h * 0.3f), d.c.fg);
		const float ry = d.y + d.h * 0.4f, lh = (d.h * 0.6f) / 4.f;
		const char* lbl[4] = {"BPM", "HEAD", "ENTROPY", "LEVEL"};
		const std::string val[4] = {string::f("%.1f", f.bpm), hex6(f.headByte), string::f("%.3f", f.entropy),
		                            string::f("%.1f DB", 20.f * std::log10(std::max(1e-4f, f.level)))};
		for (int k = 0; k < 4; k++) {
			d.text(d.x, ry + k * lh, lbl[k], 9.f, d.c.dim);
			d.text(d.x + 70.f, ry + k * lh, val[k], std::min(14.f, lh * 0.9f), k == 0 && f.kick > 0.5f ? d.c.hot : d.c.txt);
		}
		// the bands as a small bar graph at the right
		const float bx = d.x + d.w * 0.62f, bw = d.r() - bx;
		for (int k = 0; k < 24; k++) {
			const float v = d.band(k / 24.f);
			d.rect(bx + k * bw / 24.f, d.b() - d.h * 0.55f * v, bw / 24.f - 1.f, d.h * 0.55f * v, d.c.fg2);
		}
	}
	else if (d.v == 1) {
		const float size = 9.f, lh = d.mm ? 11.f : 10.f;
		const int rows = std::max(1, (int) (d.h / lh));
		const int cols = d.mm ? 8 : 16;
		const int hot = (f.sixteenth * 7) % (rows * cols);
		for (int r = 0; r < rows; r++) {
			if (d.txt)
				d.text(d.x, d.y + r * lh, hex6(f.headByte + (uint64_t) (r * cols)), size, d.c.dim);
			std::string hx;
			for (int k = 0; k < cols; k++)
				hx += hex2(d.byte(r * cols + k)) + " ";
			d.text(d.x + 42.f, d.y + r * lh, hx, size, r == 0 ? d.c.fg : d.c.fg2);
			if (hot / cols == r) {
				const float cw = (d.w - 42.f) / cols;
				d.rect(d.x + 42.f + (hot % cols) * cw - 1.f, d.y + r * lh - 1.f, cw - 2.f, lh, d.c.hot);
				d.text(d.x + 42.f + (hot % cols) * cw, d.y + r * lh, hex2(d.byte(hot)), size, d.c.bg);
			}
		}
	}
	else {
		const float cx = d.cx(), cy = d.cy(), rr = std::min(d.w, d.h) * 0.45f;
		for (int k = 0; k < 16; k++) {
			const float a = -1.5707963f + kTau * k / 16.f;
			const float r0 = k % 4 == 0 ? rr * 0.78f : rr * 0.88f;
			d.line(cx + std::cos(a) * r0, cy + std::sin(a) * r0, cx + std::cos(a) * rr, cy + std::sin(a) * rr,
			       k == f.sixteenth ? d.c.hot : d.c.fg2, k % 4 == 0 ? pf::kStroke : pf::kThinStroke);
		}
		const float a = -1.5707963f + kTau * f.barPhase;
		d.line(cx, cy, cx + std::cos(a) * rr * 0.92f, cy + std::sin(a) * rr * 0.92f, d.c.fg, pf::kStroke);
		d.ring(cx, cy, rr, d.c.grid);
		d.text(cx, cy, string::f("%03d", f.bar + 1), std::min(26.f, rr * 0.5f), d.c.txt, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		if (d.txt) {
			d.text(d.x, d.y, string::f("%.1f BPM", f.bpm), 9.f, d.c.dim);
			d.text(d.r(), d.y, f.playing ? "PLAY" : "IDLE", 9.f, f.playing ? d.c.warm : d.c.dim, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		}
	}
}

void sTestPattern(D& d) {
	const auto& f = d.f;
	d.rect(d.x, d.y, d.w, d.h, d.c.ink);
	const int sh = d.six();
	if (d.v == 0 || d.v == 1) {
		const bool vert = d.v == 0;
		const int nb = d.mm ? 24 : 40;
		const float span = vert ? d.w : d.h, cw = span / nb;
		for (int i = 0; i < nb; i++)
			if (d.bit(i + sh)) {
				const NVGcolor col = (i == (sh % nb) && f.kick > 0.5f) ? d.c.hot : d.c.paper;
				if (vert)
					d.rect(d.x + i * cw, d.y, cw, d.h, col);
				else
					d.rect(d.x, d.y + i * cw, d.w, cw, col);
			}
		// the colour bars along the edge
		const NVGcolor bars[6] = {d.c.paper, d.c.fg, d.c.warm, d.c.hot, d.c.fg2, d.c.ink};
		for (int k = 0; k < 6; k++) {
			if (vert)
				d.rect(d.x + k * d.w / 6.f, d.b() - 8.f, d.w / 6.f, 8.f, bars[k]);
			else
				d.rect(d.r() - 8.f, d.y + k * d.h / 6.f, 8.f, d.h / 6.f, bars[k]);
		}
	}
	else {
		const int cols = 16, rows = 8;
		const float cw = d.w / cols, ch = d.h / rows;
		for (int r = 0; r < rows; r++)
			for (int c = 0; c < cols; c++)
				if (d.bit(r * cols + c + sh) ^ ((r + c) & 1))
					d.rect(d.x + c * cw, d.y + r * ch, cw, ch, (r * cols + c) % 37 == f.sixteenth ? d.c.hot : d.c.paper);
	}
}

void sSpectra(D& d) {
	const auto& f = d.f;
	if (d.v == 0) {
		const int rows = d.mm ? dsc::kSpecRows / 2 : dsc::kSpecRows, nb = dsc::kSpecBands;
		const float cw = d.w / nb, ch = d.h / rows;
		for (int r = 0; r < rows; r++) {
			const int idx = (f.specHead - r + rows * 2) % rows;
			for (int k = 0; k < nb; k++) {
				const int v = f.spec[idx][k];
				if (v < 40)
					continue;
				d.rect(d.x + k * cw, d.y + r * ch, cw - 0.5f, ch, v > 180 ? d.c.fg : v > 100 ? d.c.fg2 : d.c.grid);
			}
		}
	}
	else if (d.v == 1) {
		float xs[64], ys[64];
		const float sc = std::min(d.w, d.h) * 0.46f;
		float pk = 1.f;
		for (int i = 0; i < 64; i++)
			pk = std::max(pk, std::max(std::fabs((float) f.waveL[i]), std::fabs((float) f.waveR[i])));
		pk *= 1.42f;
		for (int i = 0; i < 64; i++) {
			const float l = f.waveL[i] / pk, r = f.waveR[i] / pk;
			xs[i] = d.cx() + (l - r) * 0.7071f * sc * 1.3f;
			ys[i] = d.cy() - (l + r) * 0.7071f * sc;
		}
		d.line(d.cx() - sc, d.cy(), d.cx() + sc, d.cy(), d.c.grid);
		d.line(d.cx(), d.y, d.cx(), d.b(), d.c.grid);
		d.path(xs, ys, 64, d.c.fg);
		if (d.txt)
			d.text(d.x, d.y, "L / R", 9.f, d.c.dim);
	}
	else {
		const float cx = d.cx(), cy = d.cy(), r0 = std::min(d.w, d.h) * 0.16f, rl = std::min(d.w, d.h) * 0.32f;
		const int nb = d.mm ? 32 : 48;
		for (int k = 0; k < nb; k++) {
			const float a = -1.5707963f + kTau * k / nb;
			const float v = d.band((float) k / nb);
			d.line(cx + std::cos(a) * r0, cy + std::sin(a) * r0, cx + std::cos(a) * (r0 + rl * v), cy + std::sin(a) * (r0 + rl * v),
			       v > 0.7f ? d.c.warm : d.c.fg, pf::kStroke);
		}
		d.ring(cx, cy, r0 * (0.6f + 0.4f * f.kick), f.kick > 0.5f ? d.c.hot : d.c.fg2);
	}
}

void sText(D& d) {
	const auto& f = d.f;
	if (d.v == 0) {
		const float size = d.mm ? 11.f : 12.f, lh = size * 1.5f;
		for (int k = 0; k < 4; k++) {
			std::string s = f.str[k][0] ? f.str[k] : hex6(f.headByte + (uint64_t) k * 16) + " " + hex2(d.byte(k)) + hex2(d.byte(k + 1));
			const int maxc = std::max(4, (int) (d.w / (size * 0.6f)) - 2);
			if ((int) s.size() > maxc)
				s = s.substr(0, (size_t) maxc);
			int shown = (int) s.size();
			if (k == f.bar % 4)
				shown = std::min(shown, (int) ((f.sixteenth + 1) / 16.f * s.size()) + 1);
			const std::string vis = s.substr(0, (size_t) shown);
			const float yy = d.y + 4.f + k * lh;
			d.text(d.x, yy, vis, size, k == f.bar % 4 ? d.c.fg : d.c.fg2);
			if (k == f.bar % 4)
				d.rect(d.x + vis.size() * size * 0.6f + 1.f, yy, size * 0.55f, size, d.c.hot);
		}
		if (d.txt)
			d.text(d.x, d.b(), string::f("%s  @%s", f.type, hex6(f.headByte).c_str()), 9.f, d.c.dim, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
	}
	else if (d.v == 1) {
		const std::string src = f.str[f.bar % 4][0] ? f.str[f.bar % 4] : hex6(f.headByte);
		std::string s, tok;
		for (size_t i = 0; i <= src.size() && s.empty(); i++) {
			const char ch = i < src.size() ? src[i] : ' ';
			if (std::isalnum((unsigned char) ch))
				tok += (char) std::toupper((unsigned char) ch);
			else {
				if (tok.size() >= 3)
					s = tok;
				tok.clear();
			}
		}
		if (s.empty())
			s = hex6(f.headByte);
		if (s.size() > 12)
			s = s.substr(0, 12);
		const float size = std::min(d.h * 0.45f, d.w / (std::max<size_t>(4, s.size()) * 0.62f));
		d.text(d.cx(), d.cy(), s, size, f.kick > 0.6f ? d.c.hot : d.c.fg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		if (d.txt) {
			d.text(d.x, d.y, string::f("STRING %d / OFFSET %s", f.bar % 4 + 1, hex6(f.headByte).c_str()), 9.f, d.c.dim);
			d.text(d.r(), d.b(), f.type, 9.f, d.c.dim, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
		}
	}
	else {
		const float size = 9.f, lh = d.mm ? 11.f : 10.f;
		const int rows = std::max(1, (int) (d.h / lh)), cols = std::max(8, (int) (d.w / (size * 0.6f)) - 1);
		for (int r = 0; r < rows; r++) {
			std::string line;
			for (int k = 0; k < cols; k++)
				line += printable(d.byte(r * cols + k));
			d.text(d.x, d.y + r * lh, line, size, r == f.sixteenth % rows ? d.c.fg : d.c.fg2);
		}
	}
}

void sSequencer(D& d) {
	const auto& f = d.f;
	if (d.v == 0) {
		const float lw = d.txt ? 22.f : 0.f;
		const float cw = (d.w - lw) / 16.f, rh = d.h / 8.f;
		for (int l = 0; l < 8; l++) {
			const float yy = d.y + l * rh;
			if (d.txt)
				d.text(d.x, yy + 1.f, string::f("L%d", l + 1), 9.f, d.laneLit(l) ? d.c.hot : d.c.dim);
			for (int s = 0; s < 16; s++) {
				const bool on = (f.lanePat[l] >> s) & 1u, cur = s == f.sixteenth;
				NVGcolor col = on ? (((f.laneFlip[l] >> s) & 1u) ? d.c.warm : (cur && d.laneLit(l) ? d.c.hot : d.c.fg)) : d.c.grid;
				d.rect(d.x + lw + s * cw + 0.8f, yy + 1.f, cw - 1.6f, rh - 2.f, col);
			}
		}
		d.frameBox(d.x + lw + f.sixteenth * cw, d.y, cw, d.h, d.c.txt);
	}
	else if (d.v == 1) {
		const float cx = d.cx(), cy = d.cy(), rmax = std::min(d.w * 0.6f, d.h) * 0.48f;
		for (int l = 0; l < 8; l++) {
			const float rr = rmax * (0.3f + 0.7f * l / 7.f);
			d.ring(cx, cy, rr, d.laneLit(l) ? d.c.hot : d.c.grid);
			for (int s = 0; s < 16; s++) {
				if (!((f.lanePat[l] >> s) & 1u))
					continue;
				const float a = -1.5707963f + kTau * s / 16.f;
				d.dot(cx + std::cos(a) * rr, cy + std::sin(a) * rr, s == f.sixteenth ? d.c.hot : d.c.fg, 3.2f);
			}
		}
		const float a = -1.5707963f + kTau * f.sixteenth / 16.f;
		d.line(cx, cy, cx + std::cos(a) * rmax, cy + std::sin(a) * rmax, d.c.txt);
	}
	else {
		const float gw = (d.w - 9.f) / 4.f, gh = (d.h - 9.f) / 4.f;
		for (int k = 0; k < 16; k++) {
			const float cx = d.x + (k % 4) * (gw + 3.f), cy = d.y + (k / 4) * (gh + 3.f);
			const bool cur = k == f.stepCur;
			d.rect(cx, cy, gw, gh, cur ? d.c.hot : d.c.grid);
			const int t = f.trig[k];
			if (d.txt)
				d.text(cx + 2.f, cy + 1.f, t > 0 ? string::f("%02d T%d", k + 1, t) : t < 0 ? string::f("%02d M", k + 1) : string::f("%02d", k + 1),
				       9.f, cur ? d.c.bg : t ? d.c.txt : d.c.dim);
			if (t > 0) {
				d.rect(cx + 2.f, cy + gh - 5.f, (gw - 4.f) * f.vel[k] / 127.f, 2.f, d.c.fg);
				d.rect(cx + 2.f, cy + gh - 2.5f, (gw - 4.f) * f.prob[k] / 100.f, 1.5f, d.c.warm);
			}
		}
	}
}

// ---------------------------------------------------------------- 13-20 (FLOATDAT films)

void sTunnel(D& d) {
	const auto& f = d.f;
	const bool rush = d.v == 1;
	const float vx = d.cx(), vy = d.cy();
	const int nf = d.n(16, 10, 6);
	const double rate = rush ? 0.5 : 0.25;
	for (int k = 0; k < nf; k++) {
		const float z = frac((double) k / nf + f.beats * rate);
		const float s = 0.04f + z * z * 1.1f;
		const int idx = k + (int) std::floor((double) k / nf + f.beats * rate) * nf;
		if (!d.bit(idx & 511) && z < 0.85f)
			continue;
		float ww = d.w * s, hh = d.h * s;
		if (rush)
			ww *= 0.8f + 0.5f * d.band((float) k / nf);
		const float t = std::max(1.f, 3.f * z);
		const NVGcolor col = z > 0.7f ? d.c.fg : d.c.fg2;
		d.frameBox(vx - ww * 0.5f, vy - hh * 0.5f, ww, hh, col, t);
	}
	// floor and ceiling points
	const int np = d.n(64, 24);
	for (int i = 0; i < np; i++) {
		const float z = frac(dsc::hashf((uint32_t) i * 31u) + f.beats * rate);
		const float px = vx + (dsc::hashf((uint32_t) i * 7u) - 0.5f) * d.w * z * 1.2f;
		const float py = vy + (i & 1 ? 1.f : -1.f) * d.h * 0.5f * z;
		if (px > d.x && px < d.r() && py > d.y && py < d.b())
			d.dot(px, py, d.c.txt);
	}
	d.line(d.x, d.b(), vx, vy, d.c.warm);
	d.line(d.r(), d.y, vx, vy, d.c.hot);
}

void sInterference(D& d) {
	const auto& f = d.f;
	const int cols = d.mm ? 36 : 64, rows = d.mm ? 18 : 30;
	const float cw = d.w / cols, ch = d.h / rows;
	const float freq = 0.6f + 1.4f * d.u(0) + 0.6f * d.dens;
	const float ph = (float) f.beats * 3.1415926f * 0.5f + f.low * 2.f;
	float sx0 = 0.3f, sy0 = 0.5f, sx1 = 0.7f, sy1 = 0.5f;
	if (d.v == 2) {
		sx0 = 0.2f + 0.3f * dsc::hashf((uint32_t) f.bar * 3u);
		sy0 = 0.2f + 0.6f * dsc::hashf((uint32_t) f.bar * 5u);
		sx1 = 0.5f + 0.3f * dsc::hashf((uint32_t) f.bar * 7u);
		sy1 = 0.2f + 0.6f * dsc::hashf((uint32_t) f.bar * 11u);
	}
	const float aspect = d.w / std::max(1.f, d.h);
	for (int r = 0; r < rows; r++)
		for (int c = 0; c < cols; c++) {
			const float px = (c + 0.5f) / cols, py = (r + 0.5f) / rows;
			float val;
			if (d.v == 0) {
				const float dx = (px - 0.5f) * aspect, dy = py - 0.5f;
				val = std::sin(std::sqrt(dx * dx + dy * dy) * 20.f * freq - ph);
			}
			else if (d.v == 1) {
				const float dx = std::fabs(px - 0.5f) * aspect, dy = std::fabs(py - 0.5f);
				val = std::sin((dy * dy - dx * dx * 0.25f) * 60.f * freq - ph);
			}
			else {
				const float d0 = std::hypot((px - sx0) * aspect, py - sy0), d1 = std::hypot((px - sx1) * aspect, py - sy1);
				val = 0.5f * (std::sin(d0 * 22.f * freq - ph) + std::sin(d1 * 22.f * freq - ph * 1.3f));
			}
			if (val > 0.35f - 0.3f * f.level)
				d.rect(d.x + c * cw + cw * 0.2f, d.y + r * ch + ch * 0.2f, cw * 0.6f, ch * 0.6f, val > 0.8f ? d.c.fg : d.c.fg2);
		}
}

void sCodex(D& d) {
	const auto& f = d.f;
	d.rect(d.x, d.y, d.w, d.h, d.c.paper);
	const bool colsMode = d.v == 1;
	const int lanes = d.mm ? 8 : 12;
	const float span = colsMode ? d.w : d.h, len = colsMode ? d.h : d.w;
	const float lh = span / lanes;
	for (int l = 0; l < lanes; l++) {
		const float speed = (float) ((l % 3) + 1) * 12.f * ((l & 1) ? 1.f : -1.f);
		float pos = std::fmod((float) f.beats * speed, len);
		if (pos < 0)
			pos += len;
		float at = -pos;
		int k = 0;
		while (at < len && k < 40) {
			const int bv = d.byte(l * 23 + k);
			const float bw = 4.f + (bv % 28);
			const float gap = 2.f + ((bv >> 5) * 2.f);
			const bool slab = bv > 230;
			const float a0 = std::max(0.f, at), a1 = std::min(len, at + bw);
			if (a1 > a0) {
				const NVGcolor col = slab ? d.c.ink : (bv & 1 ? d.c.dim : d.c.fg2);
				const float th = slab ? lh - 1.f : lh * 0.55f;
				if (colsMode)
					d.rect(d.x + l * lh + (lh - th) * 0.5f, d.y + a0, th, a1 - a0, col);
				else
					d.rect(d.x + a0, d.y + l * lh + (lh - th) * 0.5f, a1 - a0, th, col);
			}
			at += bw + gap;
			k++;
		}
	}
	if (f.kick > 0.6f)
		d.rect(colsMode ? d.x : d.x + d.w * (f.sixteenth / 16.f), colsMode ? d.y + d.h * (f.sixteenth / 16.f) : d.y,
		       colsMode ? d.w : 2.f, colsMode ? 2.f : d.h, d.c.hot);
}

void sSuperposition(D& d) {
	const auto& f = d.f;
	d.rect(d.x, d.y, d.w, d.h, d.c.paper);
	const float sw = d.w * 0.72f;
	const int rows = d.mm ? 24 : 40;
	const float rh = d.h / rows, mid = d.x + sw * 0.5f;
	for (int i = 0; i < rows; i++) {
		const float v = f.wave[(i * dsc::kScope) / rows] / 127.f;
		const float px = mid + v * sw * 0.48f;
		if (d.v == 0) {
			const float bw = std::max(4.f, sw * 0.06f);
			const float q = std::floor(px / bw) * bw;
			d.rect(q, d.y + i * rh, bw, rh + 0.5f, d.c.ink);
		}
		else {
			const float wd = std::max(2.f, std::fabs(v) * sw * 0.9f);
			d.rect(mid - wd * 0.5f, d.y + i * rh + 0.5f, wd, rh - 1.f, i % 4 == f.sixteenth % 4 ? d.c.hot : d.c.ink);
		}
	}
	// the grey strips made from the data
	const float gx = d.x + sw + 6.f, gw = d.r() - gx;
	const int strips = d.mm ? 14 : 24;
	for (int k = 0; k < strips; k++) {
		const float wd = gw * (0.15f + 0.85f * d.u(k + d.six()));
		d.rect(gx, d.y + k * d.h / strips + 1.f, wd, d.h / strips - 2.f, k & 1 ? d.c.dim : d.c.fg2);
	}
}

void sTiles(D& d) {
	const auto& f = d.f;
	const int cols = 4, rows = 3;
	const float gap = 4.f, tw = (d.w - gap * (cols - 1)) / cols, th = (d.h - gap * (rows - 1)) / rows;
	for (int t = 0; t < cols * rows; t++) {
		const float tx = d.x + (t % cols) * (tw + gap), ty = d.y + (t / cols) * (th + gap);
		const int base = t * 21;
		d.frameBox(tx, ty, tw, th, t == f.sixteenth % 12 ? d.c.hot : d.c.grid);
		if (d.v == 1) {
			int sum = 0, mn = 255, mx = 0;
			for (int k = 0; k < 16; k++) {
				const int b = d.byte(base + k);
				sum += b;
				mn = std::min(mn, b);
				mx = std::max(mx, b);
			}
			const float mean = sum / 16.f;
			d.text(tx + 3.f, ty + 2.f, string::f("%5.1f", mean), 9.f, d.c.fg);
			if (!d.mm || th > 30.f)
				d.text(tx + 3.f, ty + 12.f, string::f("%02X-%02X", mn, mx), 9.f, d.c.dim);
			d.rect(tx + 3.f, ty + th - 6.f, (tw - 6.f) * mean / 255.f, 3.f, d.c.warm);
			continue;
		}
		const int kind = (t + f.bar) % 5;
		const float ix = tx + 3.f, iy = ty + 3.f, iw = tw - 6.f, ih = th - 6.f;
		if (kind == 0) {   // image: 16 x 6 bits
			for (int r = 0; r < 6; r++)
				bitRow(d, (uint64_t) (d.byte(base + r * 2) | d.byte(base + r * 2 + 1) << 8), 16, ix, iy + r * ih / 6.f, iw / 16.f, ih / 6.f - 0.5f, d.c.fg2);
		}
		else if (kind == 1) {   // line
			float xs[16], ys[16];
			for (int k = 0; k < 16; k++) {
				xs[k] = ix + iw * k / 15.f;
				ys[k] = iy + ih * (1.f - d.u(base + k));
			}
			d.path(xs, ys, 16, d.c.fg, pf::kThinStroke);
		}
		else if (kind == 2) {   // histogram of the high nibbles
			int h[8]{};
			for (int k = 0; k < 24; k++)
				h[d.byte(base + k) >> 5]++;
			for (int k = 0; k < 8; k++)
				d.rect(ix + k * iw / 8.f, iy + ih - ih * std::min(1.f, h[k] / 8.f), iw / 8.f - 1.f, ih * std::min(1.f, h[k] / 8.f), d.c.fg2);
		}
		else if (kind == 3) {   // bits of two bytes, big
			bitRow(d, (uint64_t) (d.byte(base) | d.byte(base + 1) << 8), 16, ix, iy + ih * 0.3f, iw / 16.f, ih * 0.4f, d.c.fg);
		}
		else if (d.txt) {   // a string
			std::string s = f.str[t % 4][0] ? f.str[t % 4] : hex2(d.byte(base)) + hex2(d.byte(base + 1));
			const int maxc = std::max(2, (int) (iw / 5.4f));
			d.text(ix, iy + ih * 0.3f, s.substr(0, (size_t) maxc), 9.f, d.c.txt);
		}
	}
}

void sBitmap(D& d) {
	const auto& f = d.f;
	d.rect(d.x, d.y, d.w, d.h, d.c.paper);
	const int rows = d.mm ? 16 : 32, cols = 64;   // MetaModule: half the rows
	const float cw = d.w / cols, ch = d.h / rows;
	int best = 0;
	for (int i = 1; i < dsc::kBands; i++)
		if (f.bands[i] > f.bands[best])
			best = i;
	const float peak = f.bands[best] / 255.f;
	const float ax = d.x + d.w * (best + 0.5f) / dsc::kBands;
	for (int r = 0; r < rows; r++) {
		const float bv = d.band((float) r / rows);
		const uint64_t w = f.grid[r * (32 / rows)];
		if (d.v == 0) {
			// smear: each row dragged by its band
			const int shift = (int) (bv * bv * 18.f) * ((r * 7) & 1 ? 1 : -1);
			const int sh = ((shift % cols) + cols) % cols;
			const uint64_t rw = sh ? (w << sh) | (w >> (64 - sh)) : w;
			bitRow(d, rw, cols, d.x, d.y + r * ch, cw, ch + 0.4f, d.c.ink);
		}
		else {
			// pull: the blocks drawn toward the loudest band
			const float k = 0.85f * sat(peak * 1.4f), sigma = d.w * 0.3f;
			int c = 0;
			while (c < cols) {
				if (!((w >> c) & 1u)) {
					c++;
					continue;
				}
				int e = c;
				while (e < cols && ((w >> e) & 1u))
					e++;
				auto warp = [&](float px) { const float dd = ax - px; return px + dd * k * std::exp(-std::fabs(dd) / sigma); };
				const float x0 = warp(d.x + c * cw), x1 = warp(d.x + e * cw);
				d.rect(x0, d.y + r * ch, std::max(1.f, x1 - x0), ch + 0.4f, d.c.ink);
				c = e;
			}
		}
	}
	for (int l = 0; l < dsc::kLanes; l++)
		if ((f.laneFlash >> l) & 1)
			d.rect(d.x + d.w * (l + 0.5f) / dsc::kLanes - 1.f, d.y, 2.5f, d.h, d.c.hot);
}

void sTicker(D& d) {
	const auto& f = d.f;
	const float size = 9.f, adv = size * 0.6f;
	if (d.v == 0) {
		const int rows = std::max(2, (int) (d.h / (d.mm ? 12.f : 11.f)));
		const float rh = d.h / rows;
		const int nChars = (int) (d.w / adv) + 2;
		for (int r = 0; r < rows; r++) {
			const double rate = (r % 3 + 1) * 2.0 * (r & 1 ? 1.0 : -1.0);
			const double p = f.beats * rate;
			const int whole = (int) std::floor(p);
			const float fr = (float) (p - whole) * adv;
			std::string s;
			const bool hexRow = r % 2 == 0;
			for (int k = 0; k < nChars; k++) {
				const int idx = r * 37 + k + whole;
				s += hexRow ? hex2(d.byte(idx / 2)).substr((size_t) ((idx & 1)), 1) : std::string(1, printable(d.byte(idx)));
			}
			d.text(d.x - fr, d.y + r * rh, s, size, r == f.sixteenth % rows ? d.c.fg : d.c.fg2);
		}
	}
	else {
		const int rows = d.mm ? 5 : 7;
		const float rh = d.h / (rows + 1);
		if (d.txt)
			d.text(d.x, d.y, "OFFSET   STRING                    STATE", size, d.c.dim);
		for (int r = 0; r < rows; r++) {
			const bool flip = r == (int) std::floor(f.beats) % rows;
			const float yy = d.y + (r + 1) * rh;
			d.rect(d.x, yy - 1.f, d.w, rh - 2.f, flip ? d.c.grid : d.c.bg);
			std::string s = f.str[(r + f.bar) % 4][0] ? f.str[(r + f.bar) % 4] : "----";
			if (s.size() > 22)
				s = s.substr(0, 22);
			d.text(d.x + 2.f, yy, hex6(f.headByte + (uint64_t) (r * 64)), size, d.c.dim);
			d.text(d.x + 9 * adv, yy, s, size, flip ? d.c.fg : d.c.txt);
			d.text(d.r() - 2.f, yy, (f.lanePat[r % 8] >> f.sixteenth) & 1u ? "ON TIME" : "HOLD", size,
			       (f.lanePat[r % 8] >> f.sixteenth) & 1u ? d.c.warm : d.c.dim, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		}
	}
}

void sHorizon(D& d) {
	const auto& f = d.f;
	const bool ceiling = d.v == 1;
	const float hy = ceiling ? d.y + d.h * 0.7f : d.y + d.h * 0.3f;
	d.line(d.x, hy, d.r(), hy, d.c.fg2);
	const int rowsN = d.n(18, 10, 6);
	for (int k = 0; k < rowsN; k++) {
		const float z = frac((double) k / rowsN + f.beats * 0.25);
		const float depth = z * z;
		const float yy = ceiling ? hy - (hy - d.y) * depth : hy + (d.b() - hy) * depth;
		const float ww = d.w * (0.1f + 0.9f * z);
		const float th = std::max(1.f, 4.f * depth);
		const int idx = (k + (int) std::floor((double) k / rowsN + f.beats * 0.25) * rowsN) * 16;
		uint64_t w = 0;
		for (int b = 0; b < 16; b++)
			if (d.bit(idx + b))
				w |= 1ull << b;
		bitRow(d, w, 16, d.cx() - ww * 0.5f, yy - th * 0.5f, ww / 16.f, th, z > 0.6f ? d.c.fg : d.c.fg2);
	}
	// the ring of ticks around the sun, its sweep
	const float sx = d.cx(), sy = ceiling ? hy + (d.b() - hy) * 0.5f : d.y + (hy - d.y) * 0.5f;
	const float rr = std::min((ceiling ? d.b() - hy : hy - d.y) * 0.42f, d.w * 0.2f);
	const int ticks = d.mm ? 36 : 72;
	for (int t = 0; t < ticks; t++) {
		const float a = kTau * t / ticks;
		const float r0 = t % 6 == 0 ? rr * 0.7f : rr * 0.85f;
		d.line(sx + std::cos(a) * r0, sy + std::sin(a) * r0, sx + std::cos(a) * rr, sy + std::sin(a) * rr, d.c.fg2);
	}
	const float a = kTau * f.barPhase - 1.5707963f;
	d.line(sx, sy, sx + std::cos(a) * rr, sy + std::sin(a) * rr, d.c.hot, pf::kStroke);
	d.disc(sx, sy, 2.f + 3.f * f.kick, d.c.warm);
}

// ---------------------------------------------------------------- 21-26 (case, FLOATER, Vortessa)

// a cable: a sagging polyline, lit when its source is busy
void cable(const D& d, float x0, float y0, float x1, float y1, float lit) {
	float xs[9], ys[9];
	const float sag = 6.f + 0.15f * std::fabs(x1 - x0);
	for (int i = 0; i <= 8; i++) {
		const float t = i / 8.f;
		xs[i] = x0 + (x1 - x0) * t;
		ys[i] = y0 + (y1 - y0) * t + sag * 4.f * t * (1.f - t);
	}
	d.path(xs, ys, 9, lit > 0.4f ? d.c.hot : d.c.fg2, pf::kStroke);
	d.disc(x0, y0, 2.4f, d.c.txt);
	d.disc(x1, y1, 2.4f, d.c.txt);
}

constexpr int kRowsCase = 5;

void caseModule(const D& d, int which, float mx, float my, float mw, float mh, bool big) {
	const auto& f = d.f;
	static const char* const titles[5] = {"AUDIO IN", "CLOCK", "DATA", "MODEL", "SCREEN"};
	d.rect(mx, my, mw, mh, d.c.bg);
	d.frameBox(mx, my, mw, mh, d.c.fg2);
	d.rect(mx, my, mw, 2.f, d.c.fg);
	d.text(mx + 3.f, my + 3.f, titles[which], big ? 11.f : 9.f, d.c.txt);
	const float ix = mx + 3.f, iy = my + 15.f, iw = mw - 6.f, ih = mh - 18.f;
	if (ih < 4.f)
		return;
	switch (which) {
		case 0: {
			const int nb = big ? 24 : 8;
			for (int k = 0; k < nb; k++) {
				const float v = d.band((float) k / nb);
				d.rect(ix + k * iw / nb, iy + ih * (1.f - v), iw / nb - 1.f, ih * v, d.c.fg2);
			}
			d.rect(ix, iy + ih - 3.f, iw * sat(f.level * 1.5f), 3.f, d.c.warm);
			break;
		}
		case 1: {
			d.text(ix, iy, string::f("%.0f", f.bpm), big ? 22.f : 11.f, d.c.fg);
			for (int k = 0; k < 4; k++)
				d.rect(ix + k * iw / 4.f, iy + ih - 5.f, iw / 4.f - 2.f, 5.f, k == (int) (f.barPhase * 4.f) ? d.c.hot : d.c.grid);
			break;
		}
		case 2: {
			std::string n = f.name;
			const int maxc = std::max(3, (int) (iw / 5.4f));
			d.text(ix, iy, n.substr(0, (size_t) maxc), 9.f, d.c.fg);
			for (int k = 0; k < 32; k++) {
				const float e = f.entMap[k * 2] / 255.f;
				d.rect(ix + k * iw / 32.f, iy + ih - ih * 0.5f * e, iw / 32.f, ih * 0.5f * e, d.c.fg2);
			}
			const float hx = ix + iw * (f.nbits ? (float) ((double) f.head / (double) f.nbits) : 0.f);
			d.rect(hx, iy + ih * 0.4f, 1.5f, ih * 0.6f, d.c.hot);
			break;
		}
		case 3: {
			const float rh = ih * 0.5f / kRowsCase;
			for (int v = 0; v < kRowsCase; v++)
				bitRow(d, f.autoRows[v] & 0xffffffffu, 32, ix, iy + v * rh, iw / 32.f, rh - 0.5f, d.c.fg2);
			const float side = std::min(iw, ih * 0.5f) - 2.f, c = side / 17.f;
			for (int i = 0; i < 17; i += big ? 1 : 2)
				for (int j = 0; j < 17; j += big ? 1 : 2)
					if (f.matrix[i][j] > 30)
						d.rect(ix + j * c, iy + ih * 0.5f + 2.f + i * c, std::max(1.f, c - 0.5f), std::max(1.f, c - 0.5f),
						       i == f.lastTo ? d.c.hot : d.c.fg2);
			break;
		}
		default: {
			const int nb = big ? 48 : 16;
			for (int i = 0; i < nb; i++)
				if (d.bit(i + d.six()))
					d.rect(ix + i * iw / nb, iy, std::max(1.f, iw / nb - 0.5f), ih, d.c.fg);
			break;
		}
	}
}

void sCase(D& d) {
	const auto& f = d.f;
	if (d.v == 1) {
		const int which = (f.bar / 2) % 5;
		caseModule(d, which, d.x + d.w * 0.12f, d.y, d.w * 0.76f, d.h, true);
		return;
	}
	const float gap = 4.f, mw = (d.w - gap * 4.f) / 5.f, mh = d.h * 0.62f;
	float px[5];
	for (int k = 0; k < 5; k++) {
		px[k] = d.x + k * (mw + gap);
		caseModule(d, k, px[k], d.y, mw, mh, false);
	}
	const float jy = d.y + mh + 6.f;
	cable(d, px[0] + mw * 0.5f, jy, px[3] + mw * 0.3f, jy, f.level * 1.5f);
	cable(d, px[1] + mw * 0.5f, jy, px[3] + mw * 0.5f, jy, f.step + f.kick);
	cable(d, px[2] + mw * 0.5f, jy, px[3] + mw * 0.7f, jy, f.hit);
	cable(d, px[3] + mw * 0.5f, jy, px[4] + mw * 0.5f, jy, f.flash);
}

void sReel(D& d) {
	const auto& f = d.f;
	const bool genes = d.v == 1;
	const float th = genes ? d.h * 0.5f : d.h - (d.txt ? 10.f : 0.f);
	const float mid = d.y + th * 0.5f;
	const int nb = d.mm ? 64 : dsc::kReel;
	const float cw = d.w / nb;
	for (int i = 0; i < nb; i++) {
		const float v = f.reel[(i * dsc::kReel) / nb] / 255.f;
		const float hh = th * 0.48f * v;
		d.rect(d.x + i * cw, mid - hh, std::max(1.f, cw - 0.4f), std::max(1.f, 2.f * hh), v > 0.6f ? d.c.fg : d.c.fg2);
	}
	for (int s = 1; s < 8; s++)
		d.rect(d.x + d.w * s / 8.f, d.y, 1.f, th, d.c.txt);
	d.rect(d.r() - 2.5f, d.y, 2.5f, th, d.c.hot);   // the record head
	for (int g = 0; g < 4; g++) {
		const float gx = d.x + d.w * f.gene[g];
		if (!genes) {
			d.rect(gx, d.y, 1.5f, th, d.c.warm);
			if (d.txt)
				d.text(gx + 2.f, d.y, string::f("G%d", g + 1), 9.f, d.c.warm);
		}
		else {
			const float ty = d.y + th + 4.f + g * (d.h - th - 4.f) / 4.f, tt = (d.h - th - 4.f) / 4.f - 2.f;
			d.rect(d.x, ty + tt * 0.5f, d.w, 1.f, d.c.grid);
			d.rect(gx, ty, std::max(3.f, d.w / 32.f), tt, d.c.warm);
			if (d.txt)
				d.text(d.x, ty, string::f("G%d", g + 1), 9.f, d.c.dim);
		}
	}
	if (!genes && d.txt)
		d.text(d.x, d.b(), string::f("2 BARS  %s", f.playing ? "REC" : "IDLE"), 9.f, d.c.dim, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
}

void sOrbit(D& d) {
	const auto& f = d.f;
	const float cx = d.cx(), cy = d.cy(), rmax = std::min(d.w * 0.55f, d.h) * 0.48f;
	for (int l = 0; l < 8; l++) {
		const float rr = rmax * (0.28f + 0.72f * l / 7.f);
		const bool lit = d.laneLit(l);
		d.ring(cx, cy, rr, lit ? d.c.hot : d.c.grid, lit ? pf::kStroke : pf::kThinStroke);
		const uint16_t pat = d.v == 0 ? f.lanePat[l] : (uint16_t) (d.byte(l * 2) | d.byte(l * 2 + 1) << 8);
		const int n = 16;
		for (int s = 0; s < n; s++) {
			if (!((pat >> s) & 1u))
				continue;
			const float a = -1.5707963f + kTau * s / n + (d.v == 1 ? (float) f.beats * 0.05f * (l + 1) : 0.f);
			d.dot(cx + std::cos(a) * rr, cy + std::sin(a) * rr, s == f.sixteenth ? d.c.hot : d.c.fg, 3.f);
		}
	}
	// ripples: the latest lane hits as expanding rings
	for (int k = 0; k < 4; k++) {
		const int hi = (f.histHead - k + dsc::kHist) % dsc::kHist;
		if (f.laneHist[hi])
			d.ring(cx, cy, rmax * (0.1f + 0.25f * k), d.c.warm);
	}
	d.text(cx, cy, string::f("%02d", f.sixteenth + 1), 16.f, d.c.txt, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
	if (d.txt) {
		for (int l = 0; l < 8; l++) {
			std::string s;
			for (int k = 0; k < 16; k++)
				s += (f.lanePat[l] >> k) & 1u ? 'x' : '.';
			if (d.mm)
				s = s.substr(0, 8);
			d.text(d.x, d.y + l * 10.f, s, 9.f, d.laneLit(l) ? d.c.hot : d.c.dim);
		}
	}
}

void sAttractor(D& d) {
	const auto& f = d.f;
	if (d.v == 0) {
		float xs[dsc::kTrail], ys[dsc::kTrail];
		const float yaw = (float) f.beats * 0.08f, c = std::cos(yaw), s = std::sin(yaw);
		const float sc = std::min(d.w, d.h) * 0.42f;
		for (int i = 0; i < dsc::kTrail; i++) {
			const int k = (f.attrHead + 1 + i) % dsc::kTrail;
			const float px = f.attrX[k] * c - f.attrY[k] * s;
			xs[i] = d.cx() + px * sc * 1.4f;
			ys[i] = d.cy() - f.attrZ[k] * sc;
		}
		d.path(xs, ys, dsc::kTrail - 12, d.c.fg2, pf::kThinStroke);
		d.path(xs + dsc::kTrail - 13, ys + dsc::kTrail - 13, 13, d.c.fg, pf::kStroke);
		d.disc(xs[dsc::kTrail - 1], ys[dsc::kTrail - 1], 2.5f + 3.f * f.kick, d.c.hot);
		if (d.txt)
			d.text(d.x, d.b(), string::f("RHO %.1f", 24.f + f.entropy), 9.f, d.c.dim, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
	}
	else {
		static const char* const nm[8] = {"SMOOTH", "STEPPED", "PONG", "LFO", "FORGE", "CHAOS", "WOGGLE", "SEQ"};
		const float pw = (d.w - 12.f) / 4.f, ph = d.h - (d.txt ? 12.f : 0.f);
		for (int p = 0; p < 4; p++) {
			const float px = d.x + p * (pw + 4.f);
			d.frameBox(px, d.y, pw, ph, d.c.grid);
			float xs[dsc::kCvHist], ys[dsc::kCvHist];
			for (int i = 0; i < dsc::kCvHist; i++) {
				const int k = (f.cvHead + 1 + i) % dsc::kCvHist;
				xs[i] = px + pw * 0.5f + f.cvHist[p * 2][k] / 127.f * pw * 0.45f;
				ys[i] = d.y + ph * 0.5f - f.cvHist[p * 2 + 1][k] / 127.f * ph * 0.45f;
			}
			d.path(xs, ys, dsc::kCvHist, d.c.fg, pf::kThinStroke);
			d.disc(xs[dsc::kCvHist - 1], ys[dsc::kCvHist - 1], 2.5f, d.c.hot);
			if (d.txt)
				d.text(px, d.b(), d.mm ? nm[p * 2] : string::f("%s/%s", nm[p * 2], nm[p * 2 + 1]), 9.f, d.c.dim, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
		}
	}
}

void sVortex(D& d) {
	const auto& f = d.f;
	const float cx = d.cx(), cy = d.cy(), rmax = std::min(d.w * 0.5f, d.h * 0.62f);
	const float rot = (float) f.beats * 0.4f;
	for (int arm = 0; arm < 4; arm++) {
		float xs[24], ys[24];
		for (int i = 0; i < 24; i++) {
			const float t = i / 23.f;
			const float a = rot + arm * kTau / 4.f + t * 5.f;
			const float rr = rmax * std::exp(-2.2f * (1.f - t)) * (1.f - 0.05f);
			xs[i] = cx + std::cos(a) * rr * 1.25f;
			ys[i] = cy + std::sin(a) * rr * 0.8f;
		}
		d.path(xs, ys, 24, d.c.grid, pf::kThinStroke);
	}
	const int np = d.n(140, 50, 12);
	const int labels = d.v == 1 ? (d.mm ? 10 : 24) : 0;
	for (int i = 0; i < np; i++) {
		const float sp = 0.5f + dsc::hashf((uint32_t) i * 97u + 3u);
		const float t = frac(dsc::hashf((uint32_t) i * 13u) + f.beats * 0.125 * sp);
		float rr = rmax * (1.f - t) * (1.f - t) * (1.f + 0.4f * f.kick);
		const float a = dsc::hashf((uint32_t) i * 31u) * kTau + t * 4.f * 3.1415926f + rot;
		const float px = cx + std::cos(a) * rr * 1.25f, py = cy + std::sin(a) * rr * 0.8f;
		if (px < d.x || px > d.r() || py < d.y || py > d.b())
			continue;
		if (i < labels && d.txt)
			d.text(px, py, hex2(d.byte(i + d.six())), 9.f, t > 0.7f ? d.c.fg : d.c.fg2, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		else
			d.dot(px, py, t > 0.7f ? d.c.fg : d.c.fg2);
	}
	d.disc(cx, cy, 3.f + 7.f * f.kick, f.kick > 0.5f ? d.c.hot : d.c.warm);
}

void sGhost(D& d) {
	const auto& f = d.f;
	const int cols = d.mm ? 7 : 9, rows = d.mm ? 4 : 5;
	float px[9][5], py[9][5];
	const float gx = d.w / (cols - 1 + 0.6f), gy = d.h / (rows - 1 + 0.6f);
	for (int c = 0; c < cols; c++)
		for (int r = 0; r < rows; r++) {
			const float wave = std::sin((float) f.beats * 1.5707963f + c * 0.7f + r * 0.4f);
			const float amp = 3.f + 6.f * f.voices[3 + (c % 4)];
			px[c][r] = d.x + gx * (c + 0.3f) + (d.u(c * 5 + r) - 0.5f) * 6.f;
			py[c][r] = d.y + gy * (r + 0.3f) + wave * amp;
		}
	if (d.v == 0) {
		for (int c = 0; c < cols; c++)
			for (int r = 0; r < rows; r++) {
				if (c + 1 < cols)
					d.line(px[c][r], py[c][r], px[c + 1][r], py[c + 1][r], d.c.fg2);
				if (r + 1 < rows)
					d.line(px[c][r], py[c][r], px[c][r + 1], py[c][r + 1], d.c.fg2);
				if (!d.mm && c + 1 < cols && r + 1 < rows && (d.byte(c * 7 + r) & 3) == 0)
					d.line(px[c][r], py[c][r], px[c + 1][r + 1], py[c + 1][r + 1], d.c.grid);
			}
	}
	for (int c = 0; c < cols; c++)
		for (int r = 0; r < rows; r++) {
			const float sz = d.v == 1 ? 2.f + 6.f * d.u(c * 3 + r * 11) * (0.5f + f.level) : 2.5f;
			d.rect(px[c][r] - sz * 0.5f, py[c][r] - sz * 0.5f, sz, sz, d.c.fg);
		}
	// the four resonant nodes
	for (int k = 0; k < 4; k++) {
		const int c = d.byte(k * 9) % cols, r = d.byte(k * 9 + 1) % rows;
		const float v = f.voices[3 + k];
		d.ring(px[c][r], py[c][r], 4.f + 12.f * v, v > 0.4f ? d.c.hot : d.c.warm, pf::kStroke);
		if (d.txt)
			d.text(px[c][r] + 6.f, py[c][r] + 6.f, string::f("N%d", k + 1), 9.f, d.c.dim);
	}
}

using SceneFn = void (*)(D&);
const SceneFn kScenesFn[dsc::kNumScenes] = {sBarcode, sMatrix, sSpace, sNetwork, sScatter, sField, sScan, sNumbers, sTestPattern,
                                            sSpectra, sText, sSequencer, sTunnel, sInterference, sCodex, sSuperposition, sTiles,
                                            sBitmap, sTicker, sHorizon, sCase, sReel, sOrbit, sAttractor, sVortex, sGhost};

} // namespace

// The screen: the scene over a HUD (scene, variant, bar / BPM; step and lane lights, the file).
struct DatascreenScreen : Screen {
	Datascreen* module = nullptr;
	Snapshot demo;

	DatascreenScreen() {
		auto mp = std::make_unique<dsc::Model>();   // ~14 KB: not on the GUI stack
		dsc::Model& m = *mp;
		dsc::Settings st;
		// a short made-up run, so the browser preview shows a living scene
		for (int k = 0; k < 40; k++) {
			for (int i = 0; i < 1200; i++) {
				const float t = (k * 1200 + i) / 48000.f;
				const float v = 0.4f * std::sin(t * 2.f * 3.1415926f * 110.f) * std::exp(-std::fmod(t, 0.5f) * 8.f);
				m.pushAudio(v, v * 0.8f);
			}
			m.setClock(k * 0.1, 120.0, true);
			if (k % 4 == 0)
				m.stepEntered(k / 4 % 16);
			if (k % 3 == 0)
				m.laneHit(k % 8, 0.8f);
			m.update(demo.f, 0.05f, st);
		}
		demo.f.scene = 1;
		demo.f.variant = 0;
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const dsc::Frame& f = s.f;
		const float w = box.size.x, h = box.size.y;
		const bool invert = s.flashOn && f.flash > 0.5f;
		const Pal pal = makePal(s.palette, invert);
		if (s.palette != 0 || invert)
			fillRect(vg, 0.f, 0.f, w, h, pal.bg);
		const bool hud = s.textMode < 2;
		const float top = hud ? 13.f : 3.f, bottom = hud ? 11.f : 3.f;
		const int sc = clamp(f.scene, 0, dsc::kNumScenes - 1);
		const auto& info = dsc::sceneInfo(sc);
		D d{vg, this, f, pal, 4.f, top, w - 8.f, h - top - bottom, clamp(f.variant, 0, info.numVariants - 1), f.density, s.textMode == 0};

		nvgSave(vg);
		nvgScissor(vg, d.x, d.y, d.w, d.h);
		kScenesFn[sc](d);
		nvgRestore(vg);
		if (!hud)
			return;

		// header
		text(vg, 4.f, 2.f, string::f("%02d %s", sc + 1, info.name), 10.f, pal.fg);
		if (!pf::kMetaModule || w > 180.f)
			text(vg, 4.f + 7.f * (std::strlen(info.name) + 4), 2.f, info.variants[d.v], 9.f, pal.dim);
		text(vg, w - 4.f, 2.f,
		     string::f("%s%03d.%d  %.0f BPM", s.frozen ? "FRZ  " : "", f.bar + 1, (int) (f.beatPhase * 4.f) + 1, f.bpm), 9.f,
		     f.playing ? pal.txt : pal.dim, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		// footer: 16 steps, the file, 8 lanes
		const float fy = h - 8.f, cw = pf::kMetaModule ? 4.f : 5.f;
		for (int k = 0; k < 16; k++) {
			const bool cur = k == f.stepCur;
			fillRect(vg, 4.f + k * (cw + 1.f), fy, cw, 5.f, cur ? pal.hot : (f.stepFlash >> k & 1) ? pal.fg2 : pal.grid);
		}
		for (int l = 0; l < 8; l++)
			fillRect(vg, w - 4.f - (8 - l) * (cw + 1.f), fy, cw, 5.f, ((f.laneFlash >> l) & 1) || f.laneLevel[l] > 128 ? pal.warm : pal.grid);
		std::string n = f.haveData ? std::string(f.name) : std::string("NO FILE - MENU: LOAD ANY FILE");
		const float mid = w - 8.f - 24.f * (cw + 1.f);
		const size_t maxc = (size_t) std::max(4.f, mid / 5.6f);
		if (n.size() > maxc)
			n = n.substr(0, maxc - 1) + "~";
		text(vg, w * 0.5f, h - 1.f, n, 9.f, f.haveData ? pal.dim : pal.hot, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
	}
};

// ================================================================ widget

namespace {

void addIdx(Menu* menu, const std::string& label, const std::vector<std::string>& labels, int* v) {
	menu->addChild(createIndexSubmenuItem(label, labels,
		[=]() { return (size_t) clamp(*v, 0, (int) labels.size() - 1); },
		[=](size_t i) { *v = (int) i; }));
}

} // namespace

struct DatascreenWidget : ModuleWidget {
	DatascreenWidget(Datascreen* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Datascreen.svg")));

		auto* scr = semi::createScreenMm<DatascreenScreen>(Vec(L::SCREEN.x, L::SCREEN.y), Vec(L::SCREEN.w, L::SCREEN.h));
		scr->module = module;
		addChild(scr);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		auto snapKnob = [&](app::ParamWidget* k) {
			if (auto* kn = dynamic_cast<app::Knob*>(k))
				kn->snap = true;
			addParam(k);
		};
		snapKnob(createParamCentered<semi::KnobM>(at(L::SCENE), module, Datascreen::SCENE_PARAM));
		snapKnob(createParamCentered<semi::KnobM>(at(L::VARIANT), module, Datascreen::VARIANT_PARAM));
		snapKnob(createParamCentered<semi::KnobM>(at(L::CHANGE), module, Datascreen::CHANGE_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::DENSITY), module, Datascreen::DENSITY_PARAM));
		addParam(createParamCentered<semi::KnobMHot>(at(L::REACT), module, Datascreen::REACT_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::SPEED), module, Datascreen::SPEED_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::NEXT), module, Datascreen::NEXT_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::FREEZE), module, Datascreen::FREEZE_PARAM));
		addChild(createLightCentered<semi::MagentaLedSmall>(at(L::FREEZE_LED), module, Datascreen::FREEZE_LIGHT));
		addParam(createParamCentered<semi::Button>(at(L::FLASH), module, Datascreen::FLASH_PARAM));

		addInput(createInputCentered<semi::Jack>(at(L::IN_L), module, Datascreen::L_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::IN_R), module, Datascreen::R_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::CLK), module, Datascreen::CLK_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RST), module, Datascreen::RST_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::TRIG), module, Datascreen::TRIG_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::STEP), module, Datascreen::STEP_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::HEAD), module, Datascreen::HEAD_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::SCENE_CV), module, Datascreen::SCENE_INPUT));
		addInput(createInputCentered<semi::BusJack>(at(L::BUS_IN), module, Datascreen::BUS_INPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_L), module, Datascreen::L_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_R), module, Datascreen::R_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::CUT), module, Datascreen::CUT_OUTPUT));
		addOutput(createOutputCentered<semi::BusJack>(at(L::BUS_OUT), module, Datascreen::BUS_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Datascreen*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		const std::string cur = semi::fileName(m->loader.path());
		menu->addChild(createMenuLabel("Data: " + (cur.empty() ? std::string("none (hash)") : cur) + (m->loader.failed ? " (load failed)" : "")));
		menu->addChild(createMenuItem("Load any file...", "", [=]() {
			semi::openFileDialog("Any file:*", semi::dirName(m->loader.path()), [=](const std::string& path) { m->loadPath(path); });
		}));
		menu->addChild(createMenuItem("Clear file", "", [=]() {
			m->loader.clearPath();
			m->clearRequest.store(true);
		}));
		menu->addChild(createIndexSubmenuItem("Read as", {"Auto", "Bytes", "Text", "Audio (WAV)", "Image (as bytes)"},
			[=]() { return (size_t) m->readAs.load(); },
			[=](size_t i) { m->readAs.store((int) i); m->reload(); }));
		addIdx(menu, "Scan", names({"Hold", "Step (STEP input)", "Run", "Follow the clock"}), &m->cfg.scan);
		addIdx(menu, "Scan rate", names({"1 row/s", "2 rows/s", "4 rows/s", "8 rows/s", "16 rows/s", "32 rows/s", "64 rows/s"}), &m->scanRateIdx);

		menu->addChild(new MenuSeparator);
		menu->addChild(createSubmenuItem("Scene", sceneLabel(m->director.scene), [=](Menu* sub) {
			for (int i = 0; i < dsc::kNumScenes; i++)
				sub->addChild(createCheckMenuItem(sceneLabel(i), "", [=]() { return m->director.scene == i; },
				                                  [=]() { m->director.cutTo(i); }));
		}));
		std::vector<std::string> pools;
		for (int i = 0; i < dsc::kNumPools; i++)
			pools.push_back(dsc::poolName(i));
		addIdx(menu, "Scene pool", pools, &m->pool);
		addIdx(menu, "Palette", std::vector<std::string>(kPaletteNames, kPaletteNames + kNumPalettes), &m->palette);
		addIdx(menu, "Text", names({"Full", "HUD only", "None"}), &m->textMode);
		menu->addChild(createBoolPtrMenuItem("Follow BUS scene", "", &m->followBus));
		menu->addChild(createBoolPtrMenuItem("Flash on events / cuts", "", &m->flashOn));

		menu->addChild(new MenuSeparator);
		addIdx(menu, "CLK resolution", names({"1 PPQN (1/4)", "2 PPQN (1/8)", "4 PPQN (1/16)", "8 PPQN (1/32)", "24 PPQN"}), &m->ppqnIdx);
		addIdx(menu, "Free tempo (no CLK / BUS)", names({"60 BPM", "90 BPM", "110 BPM", "120 BPM", "128 BPM", "140 BPM", "160 BPM", "174 BPM"}),
		       &m->bpmIdx);
	}
};

Model* modelDatascreen = createModel<Datascreen, DatascreenWidget>("Datascreen");
