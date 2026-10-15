// NEXUSCLIP — Rack / MetaModule port of NEXUSCLIP CTZ (engine: engines/nexusclip,
// unchanged from semierendonmez-commits/nexusclipctz Source/engine).
//
// Chain (as in the plugin): DRIVE -> saturate from the bottom (SAT) ->
// engine (CLIP 7 curves / SPECTRAL / LIMIT) at THRESH -> OUT.
// Rack audio is +-5 V; 5 V = 0 dBFS for the engine.
//
// This file is the reference port for the series: copy its structure
// (block adapter, TripleBuffer snapshot, Screen subclass, layout header).

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/FamilyBusCV.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/NexusclipLayout.hpp"
#include "nexusclip/Clippers.h"

namespace L = semi::layout::nexusclip;
using semi::Screen;

namespace {

constexpr int kBlock = 32;          // engine block; adds 32 samples of latency
constexpr float kVoltsPerUnit = 5.f;

struct Snapshot {
	int mode = 0, shape = 0;
	float soft = 0.f, thrDb = 0.f, driveDb = 0.f;
	float peakIn = 0.f;   // input peak (after drive + sat), engine units
	float peakOut = 0.f;  // output peak before OUT gain
	float grDb = 0.f;     // max reduction over the last UI period
};

} // namespace

struct Nexusclip : Module {
	enum ParamId { MODE_PARAM, DRIVE_PARAM, THRESH_PARAM, SHAPE_PARAM, SOFT_PARAM, SAT_PARAM, OUT_PARAM, PARAMS_LEN };
	enum InputId { IN_L_INPUT, IN_R_INPUT, DRIVE_CV_INPUT, THRESH_CV_INPUT, INPUTS_LEN };
	enum OutputId { OUT_L_OUTPUT, OUT_R_OUTPUT, OUTPUTS_LEN };
	enum LightId { MODE_CLIP_LIGHT, MODE_SPEC_LIGHT, MODE_LIMIT_LIGHT, LIGHTS_LEN };

	nc::ClipEngine engine;
	nc::Settings settings;
	int mode = nc::modeHard;
	dsp::BooleanTrigger modeTrig;

	float inBuf[2][kBlock]{}, outBuf[2][kBlock]{};
	int pos = 0;
	float peakIn = 0.f, peakOut = 0.f, grMax = 0.f;

	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;

	Nexusclip() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configButton(MODE_PARAM, "Engine (CLIP / SPECTRAL / LIMIT)");
		configParam(DRIVE_PARAM, -24.f, 24.f, 0.f, "Drive", " dB");
		configParam(THRESH_PARAM, -48.f, 0.f, 0.f, "Threshold", " dB");
		configSwitch(SHAPE_PARAM, 0.f, nc::numShapes - 1, 0.f, "Clip shape",
		             {"HARD", "SMOOTH", "CRISP", "CLASSIC", "PRO", "TUBE", "TAPE"});
		configParam(SOFT_PARAM, 0.f, 100.f, 0.f, "Softness", " %");
		configParam(SAT_PARAM, 0.f, 100.f, 0.f, "Saturate from the bottom", " %");
		configParam(OUT_PARAM, -24.f, 24.f, 0.f, "Output", " dB");
		configInput(IN_L_INPUT, "Left");
		configInput(IN_R_INPUT, "Right (normalled to left)");
		configInput(DRIVE_CV_INPUT, "Drive CV (2.4 dB/V)");
		configInput(THRESH_CV_INPUT, "Threshold CV (4.8 dB/V)");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
		engine.prepare(44100.0, 2);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		engine.prepare(e.sampleRate, 2);
		pos = 0;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		mode = nc::modeHard;
		engine.reset();
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "mode", json_integer(mode));
		return root;
	}

	void dataFromJson(json_t* root) override {
		if (json_t* m = json_object_get(root, "mode"))
			mode = clamp((int) json_integer_value(m), 0, nc::numModes - 1);
	}

	void runBlock() {
		const float driveDb = clamp(params[DRIVE_PARAM].getValue() + inputs[DRIVE_CV_INPUT].getVoltage() * 2.4f, -24.f, 24.f);
		const float drive = nc::dbToGain(driveDb);
		const float sat = params[SAT_PARAM].getValue() * 0.01f;

		settings.mode = mode;
		settings.thresholdDb = clamp(params[THRESH_PARAM].getValue() + inputs[THRESH_CV_INPUT].getVoltage() * 4.8f, -48.f, 0.f);
		settings.shape = (int) std::round(params[SHAPE_PARAM].getValue());
		settings.softness = params[SOFT_PARAM].getValue() * 0.01f;

		float* ch[2] = {outBuf[0], outBuf[1]};
		for (int c = 0; c < 2; c++)
			for (int i = 0; i < kBlock; i++)
				ch[c][i] = inBuf[c][i] * drive;
		if (sat > 0.f)
			for (int c = 0; c < 2; c++)
				nc::Saturator::process(ch[c], kBlock, 1.f, sat, 0);

		float pin = 0.f;
		for (int c = 0; c < 2; c++)
			for (int i = 0; i < kBlock; i++)
				pin = std::max(pin, std::abs(ch[c][i]));

		engine.process(ch, 2, kBlock, settings);

		float pout = 0.f;
		for (int c = 0; c < 2; c++)
			for (int i = 0; i < kBlock; i++)
				pout = std::max(pout, std::abs(ch[c][i]));

		peakIn = std::max(peakIn, pin);
		peakOut = std::max(peakOut, pout);
		if (pin > 1e-6f && pout > 1e-6f)
			grMax = std::max(grMax, nc::gainToDb(pin) - nc::gainToDb(pout));

		const float out = nc::dbToGain(params[OUT_PARAM].getValue());
		for (int c = 0; c < 2; c++)
			for (int i = 0; i < kBlock; i++)
				ch[c][i] *= out;
	}

	void process(const ProcessArgs& args) override {
		if (modeTrig.process(params[MODE_PARAM].getValue() > 0.f))
			mode = (mode + 1) % nc::numModes;

		const float l = inputs[IN_L_INPUT].getVoltage();
		const float r = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() : l;
		inBuf[0][pos] = l / kVoltsPerUnit;
		inBuf[1][pos] = r / kVoltsPerUnit;
		outputs[OUT_L_OUTPUT].setVoltage(outBuf[0][pos] * kVoltsPerUnit);
		outputs[OUT_R_OUTPUT].setVoltage(outBuf[1][pos] * kVoltsPerUnit);
		if (++pos >= kBlock) {
			pos = 0;
			runBlock();
		}

		if (ui.tick(args.sampleTime)) {
			for (int i = 0; i < 3; i++)
				lights[MODE_CLIP_LIGHT + i].setBrightness(mode == i ? 1.f : 0.f);
			Snapshot& s = snap.writeSlot();
			s.mode = mode;
			s.shape = settings.shape;
			s.soft = settings.softness;
			s.thrDb = settings.thresholdDb;
			s.driveDb = params[DRIVE_PARAM].getValue();
			s.peakIn = peakIn;
			s.peakOut = peakOut;
			s.grDb = grMax;
			snap.publish();
			peakIn = peakOut = grMax = 0.f;
		}
	}
};

// Transfer curve of the active engine + live peak marker and GR readout.
// The curve only changes when a knob moves; the marker moves at most 20x/s
// on MetaModule, so most frames are pixel-identical.
struct TransferScreen : Screen {
	Nexusclip* module = nullptr;
	Snapshot demo;

	TransferScreen() {
		demo.shape = nc::shapeSmooth;
		demo.soft = 0.35f;
		demo.thrDb = -6.f;
		demo.peakIn = 0.8f;
		demo.grDb = 4.2f;
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		const float pad = 4.f;
		const float x0 = pad, x1 = w - pad, y0 = pad + 8.f, y1 = h - pad;
		// input axis 0..2 (engine units), output axis 0..1.1
		const float inMax = 2.f, outMax = 1.1f;
		auto X = [&](float u) { return x0 + (x1 - x0) * u / inMax; };
		auto Y = [&](float v) { return y1 - (y1 - y0) * v / outMax; };

		line(vg, x0, Y(0.f), x1, Y(0.f), semi::theme::grid());
		line(vg, X(1.f), y0, X(1.f), y1, semi::theme::grid());
		const float thr = nc::dbToGain(s.thrDb);
		dashed(vg, x0, Y(thr), x1, Y(thr), semi::theme::magenta());

		// the curve: CLIP shapes are exact; SPECTRAL/LIMIT are shown as a hard
		// ceiling (their transfer depends on the programme).
		nc::ShapeFn fn;
		fn.set(s.mode == nc::modeHard ? s.shape : nc::shapeHard, s.soft);
		constexpr int N = 64;
		float xs[N], ys[N];
		for (int i = 0; i < N; i++) {
			float u = inMax * i / (N - 1);
			xs[i] = X(u);
			ys[i] = Y(thr * fn(u / std::max(thr, 1e-4f)));
		}
		path(vg, xs, ys, N, semi::theme::cyan());

		// live peak marker on the curve
		if (s.peakIn > 1e-4f) {
			float u = std::min(s.peakIn, inMax);
			float v = thr * fn(u / std::max(thr, 1e-4f));
			nvgBeginPath(vg);
			nvgCircle(vg, X(u), Y(v), 2.6f);
			nvgFillColor(vg, semi::theme::amber());
			nvgFill(vg);
		}

		static const char* modes[] = {"CLIP", "SPECTRAL", "LIMIT"};
		std::string head = s.mode == nc::modeHard ? nc::shapeName(s.shape) : modes[s.mode];
		text(vg, pad, pad - 1.f, head, 10.f, semi::theme::text());
		char buf[32];
		std::snprintf(buf, sizeof buf, "GR %.1f", s.grDb);
		text(vg, w - pad, pad - 1.f, buf, 10.f, semi::theme::amber(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		std::snprintf(buf, sizeof buf, "THR %.1f", s.thrDb);
		text(vg, w - pad, h - pad, buf, 9.f, semi::theme::magenta(), NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
	}
};

struct NexusclipWidget : ModuleWidget {
	NexusclipWidget(Nexusclip* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Nexusclip.svg")));

		auto* scr = semi::createScreenMm<TransferScreen>(Vec(L::TRANSFER.x, L::TRANSFER.y), Vec(L::TRANSFER.w, L::TRANSFER.h));
		scr->module = module;
		addChild(scr);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::MODE_CLIP), module, Nexusclip::MODE_CLIP_LIGHT));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::MODE_SPEC), module, Nexusclip::MODE_SPEC_LIGHT));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::MODE_LIMIT), module, Nexusclip::MODE_LIMIT_LIGHT));
		addParam(createParamCentered<semi::Button>(at(L::MODE), module, Nexusclip::MODE_PARAM));

		addParam(createParamCentered<semi::KnobM>(at(L::DRIVE), module, Nexusclip::DRIVE_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::THRESH), module, Nexusclip::THRESH_PARAM));
		auto* shape = createParamCentered<semi::KnobM>(at(L::SHAPE), module, Nexusclip::SHAPE_PARAM);
		shape->snap = true;
		addParam(shape);
		addParam(createParamCentered<semi::KnobM>(at(L::SOFT), module, Nexusclip::SOFT_PARAM));
		addParam(createParamCentered<semi::KnobMHot>(at(L::SAT), module, Nexusclip::SAT_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::OUT), module, Nexusclip::OUT_PARAM));

		addInput(createInputCentered<semi::Jack>(at(L::IN_L), module, Nexusclip::IN_L_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::IN_R), module, Nexusclip::IN_R_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::DRIVE_CV), module, Nexusclip::DRIVE_CV_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::THRESH_CV), module, Nexusclip::THRESH_CV_INPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_L), module, Nexusclip::OUT_L_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_R), module, Nexusclip::OUT_R_OUTPUT));
	}
};

Model* modelNexusclip = createModel<Nexusclip, NexusclipWidget>("Nexusclip");
