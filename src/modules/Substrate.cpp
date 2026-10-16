// SUBSTRATE — Rack / MetaModule port of SUBSTRATE (engine: engines/substrate/SubstrateCore.h,
// vendored from semierendonmez-commits/substrate Source/engine; namespace moved to sbs::substrate).
//
// Automatic DnB low end on a stereo bass: a YIN tracker finds f0, everything below SPLIT x f0 is
// folded to mono and the sub is rebuilt as one clean phase-continuous sine that follows the bass's
// own fundamental; cuts between resampled edits are found and crossfaded from a Burg LPC
// extrapolation before they are heard. Fixed latency ~60 ms (+32 samples of block adapter).
//
// The plugin's MIDI pitch source becomes V/OCT (patched: f0 from the cable, e.g. NEUROSTACK's F0
// output); SUB and F0 outputs carry the rebuilt sub and the tracked pitch. PHASE MODE / MONITOR /
// REPAIR are buttons (shown on the scope); MAX PITCH and FADE are in the context menu.
//
// Structure follows NEXUSCLIP: block adapter, TripleBuffer snapshot via UiDivider, Screen
// subclasses with demo snapshots, layout header from tools/panels/substrate.py.

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/SubstrateLayout.hpp"
#include "substrate/SubstrateCore.h"

#include <cstdio>

namespace L = semi::layout::substrate;
namespace S = sbs::substrate;
using semi::Screen;

namespace {

constexpr int kBlock = 32;
constexpr float kVoltsPerUnit = 5.f;
constexpr int kHist = 64;   // low.end map columns
constexpr float kHistHz = 15.f;
constexpr int kRingDec = 4;  // scope ring: every 4th sample (the low band is < ~800 Hz)
constexpr int kRing = 2048;
constexpr int kPts = 48;

const std::vector<std::string> kModeNames{"FREE", "FOLLOW", "AUTO"};
const std::vector<std::string> kMonNames{"MIX", "LOW", "UPPER", "DELTA"};
const std::vector<float> kMaxPitch{80, 110, 150, 220, 300, 400};
const std::vector<float> kFades{0.25f, 0.5f, 1, 2, 4, 8};

struct Snapshot {
	// low.end map, oldest first
	float f0[kHist]{}, split[kHist]{}, conf[kHist]{};
	uint8_t edits[kHist]{};
	// phase.scope
	float in[kPts]{}, sub[kPts]{};
	float corr = 0.f, subInDb = 0.f, f0Now = 0.f, confNow = 0.f;
	int mode = 2, monitor = 0;
	bool repair = true, midi = false;
	uint32_t editsTotal = 0;
	int latencyMs = 0;
};

} // namespace

struct Substrate : Module {
	enum ParamId {
		SPLIT_PARAM, SUB_PARAM, PHASE_PARAM, FOLLOW_PARAM, STAB_PARAM,
		RESID_PARAM, UPPER_PARAM, WIDTH_PARAM, SENS_PARAM, OUT_PARAM,
		REPAIR_PARAM, MODE_PARAM, MON_PARAM,
		PARAMS_LEN
	};
	enum InputId { IN_L_INPUT, IN_R_INPUT, VOCT_INPUT, SPLIT_CV_INPUT, SUB_CV_INPUT, INPUTS_LEN };
	enum OutputId { OUT_L_OUTPUT, OUT_R_OUTPUT, SUB_OUTPUT, F0_OUTPUT, OUTPUTS_LEN };
	enum LightId { REPAIR_LIGHT, MON_LIGHT, LIGHTS_LEN };

	S::Engine engine;
	S::Params prm;
	bool repair = true;
	int mode = 2, monitor = 0;
	float f0Max = 220.f, fadeMs = 2.f;
	dsp::BooleanTrigger repairBtn, modeBtn, monBtn;

	float inBuf[2][kBlock]{}, outBuf[2][kBlock]{}, tapIn[kBlock]{}, tapSub[kBlock]{}, tapLow[kBlock]{}, tapUp[kBlock]{};
	float subOut[kBlock]{};
	int pos = 0;
	float sr = 44100.f;

	// display
	float ringIn[kRing]{}, ringSub[kRing]{};
	int ringW = 0, ringDec = 0;
	float hF0[kHist]{}, hSplit[kHist]{}, hConf[kHist]{};
	uint8_t hEdits[kHist]{};
	int hW = 0;
	float histAcc = 0.f;
	uint64_t editsSeen = 0;
	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;

	Substrate() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(SPLIT_PARAM, 0.5f, 2.f, 1.5f, "Mono split", " x f0");
		configParam(SUB_PARAM, -36.f, 12.f, 0.f, "Sub level", " dB");
		configParam(PHASE_PARAM, -180.f, 180.f, 0.f, "Sub phase", " deg");
		configParam(FOLLOW_PARAM, 0.f, 1.f, std::log(20.f) / std::log(80.f), "Phase follow", " Hz", 80.f, 0.5f);
		configParam(STAB_PARAM, 0.f, 1.f, 0.f, "Stabilize", " %", 0.f, 100.f);
		configParam(RESID_PARAM, -60.f, 6.f, 0.f, "Low residual (-60 = off)", " dB");
		configParam(UPPER_PARAM, -36.f, 12.f, 0.f, "Upper level", " dB");
		configParam(WIDTH_PARAM, 0.f, 2.f, 1.f, "Upper width", " %", 0.f, 100.f);
		configParam(SENS_PARAM, 0.f, 1.f, 0.5f, "Repair sensitivity", " %", 0.f, 100.f);
		configParam(OUT_PARAM, -24.f, 12.f, 0.f, "Output", " dB");
		configButton(REPAIR_PARAM, "Edit repair on / off");
		configButton(MODE_PARAM, "Sub phase mode (FREE / FOLLOW / AUTO)");
		configButton(MON_PARAM, "Monitor (MIX / LOW / UPPER / DELTA)");
		configInput(IN_L_INPUT, "Left");
		configInput(IN_R_INPUT, "Right (normalled to left)");
		configInput(VOCT_INPUT, "V/OCT pitch")->description = "Patched: f0 from the cable (0 V = C4) instead of the tracker";
		configInput(SPLIT_CV_INPUT, "Split CV (0.15 x/V)");
		configInput(SUB_CV_INPUT, "Sub level CV (4.8 dB/V)");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		configOutput(SUB_OUTPUT, "Rebuilt sub (mono, with SUB level)");
		configOutput(F0_OUTPUT, "Tracked f0 (V/OCT, 0 V = C4)");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
		configLight(REPAIR_LIGHT, "Edit repair");
		configLight(MON_LIGHT, "Monitoring a band");
		engine.prepare(44100.0);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		sr = e.sampleRate;
		engine.prepare(e.sampleRate);
		pos = 0;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		repair = true;
		mode = 2;
		monitor = 0;
		f0Max = 220.f;
		fadeMs = 2.f;
		engine.reset();
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "repair", json_boolean(repair));
		json_object_set_new(root, "mode", json_integer(mode));
		json_object_set_new(root, "monitor", json_integer(monitor));
		json_object_set_new(root, "f0max", json_real(f0Max));
		json_object_set_new(root, "fade", json_real(fadeMs));
		return root;
	}

	void dataFromJson(json_t* root) override {
		if (json_t* j = json_object_get(root, "repair"))
			repair = json_is_true(j);
		if (json_t* j = json_object_get(root, "mode"))
			mode = clamp((int) json_integer_value(j), 0, 2);
		if (json_t* j = json_object_get(root, "monitor"))
			monitor = clamp((int) json_integer_value(j), 0, 3);
		if (json_t* j = json_object_get(root, "f0max"))
			f0Max = clamp((float) json_number_value(j), 60.f, 400.f);
		if (json_t* j = json_object_get(root, "fade"))
			fadeMs = clamp((float) json_number_value(j), 0.25f, 8.f);
	}

	void runBlock() {
		const bool midi = inputs[VOCT_INPUT].isConnected();
		prm.midiMode = midi;
		prm.f0Max = f0Max;
		prm.splitRatio = clamp(params[SPLIT_PARAM].getValue() + inputs[SPLIT_CV_INPUT].getVoltage() * 0.15f, 0.5f, 2.f);
		prm.residualDb = params[RESID_PARAM].getValue();
		prm.subGainDb = clamp(params[SUB_PARAM].getValue() + inputs[SUB_CV_INPUT].getVoltage() * 4.8f, -36.f, 12.f);
		prm.subPhaseDeg = params[PHASE_PARAM].getValue();
		prm.phaseMode = mode;
		prm.pllHz = 0.5 * std::pow(80.0, (double) params[FOLLOW_PARAM].getValue());
		prm.stabilize = params[STAB_PARAM].getValue();
		prm.highGainDb = params[UPPER_PARAM].getValue();
		prm.highWidth = params[WIDTH_PARAM].getValue();
		prm.repairOn = repair;
		prm.sensitivity = params[SENS_PARAM].getValue();
		prm.fadeMs = fadeMs;
		prm.monitor = monitor;
		prm.outGainDb = params[OUT_PARAM].getValue();
		engine.setParams(prm);
		engine.setMidiF0(midi ? 261.6256 * std::pow(2.0, (double) inputs[VOCT_INPUT].getVoltage()) : 0.0);

		for (int c = 0; c < 2; c++)
			std::copy(inBuf[c], inBuf[c] + kBlock, outBuf[c]);
		float* io[2] = {outBuf[0], outBuf[1]};
		engine.setTaps({tapIn, tapSub, tapLow, tapUp});
		engine.process(io, 2, kBlock);
		for (int i = 0; i < kBlock; i++) {
			subOut[i] = tapSub[i];
			if (++ringDec >= kRingDec) {
				ringDec = 0;
				ringIn[ringW] = tapIn[i];
				ringSub[ringW] = tapSub[i];
				ringW = (ringW + 1) % kRing;
			}
		}
	}

	void process(const ProcessArgs& args) override {
		sr = args.sampleRate;
		if (repairBtn.process(params[REPAIR_PARAM].getValue() > 0.f))
			repair = !repair;
		if (modeBtn.process(params[MODE_PARAM].getValue() > 0.f))
			mode = (mode + 1) % 3;
		if (monBtn.process(params[MON_PARAM].getValue() > 0.f))
			monitor = (monitor + 1) % 4;

		const float l = inputs[IN_L_INPUT].getVoltage();
		const float r = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() : l;
		inBuf[0][pos] = l / kVoltsPerUnit;
		inBuf[1][pos] = r / kVoltsPerUnit;
		outputs[OUT_L_OUTPUT].setVoltage(outBuf[0][pos] * kVoltsPerUnit);
		outputs[OUT_R_OUTPUT].setVoltage(outBuf[1][pos] * kVoltsPerUnit);
		outputs[SUB_OUTPUT].setVoltage(subOut[pos] * kVoltsPerUnit);
		if (++pos >= kBlock) {
			pos = 0;
			runBlock();
		}
		const S::Meters& m = engine.getMeters();
		outputs[F0_OUTPUT].setVoltage(m.f0 > 1.0 ? (float) std::log2(m.f0 / 261.6256) : 0.f);

		// low.end map history at a fixed rate (independent of the display rate)
		histAcc += args.sampleTime;
		if (histAcc >= 1.f / kHistHz) {
			histAcc -= 1.f / kHistHz;
			hF0[hW] = (float) m.f0;
			hSplit[hW] = (float) (m.f0 * prm.splitRatio);
			hConf[hW] = (float) m.confidence;
			hEdits[hW] = (uint8_t) std::min<uint64_t>(255, m.editsRepaired - editsSeen);
			editsSeen = m.editsRepaired;
			hW = (hW + 1) % kHist;
		}

		if (ui.tick(args.sampleTime)) {
			lights[REPAIR_LIGHT].setBrightness(repair ? 1.f : 0.f);
			lights[MON_LIGHT].setBrightness(monitor != 0 ? 1.f : 0.f);
			publish(m);
		}
	}

	void publish(const S::Meters& m) {
		Snapshot& s = snap.writeSlot();
		for (int k = 0; k < kHist; k++) {
			const int i = (hW + k) % kHist;
			s.f0[k] = hF0[i];
			s.split[k] = hSplit[i];
			s.conf[k] = hConf[i];
			s.edits[k] = hEdits[i];
		}
		// phase.scope: three periods from a rising zero crossing of the sub
		const float f0 = (float) std::max(20.0, m.f0);
		const float per = sr / (kRingDec * f0); // ring samples per period
		const int span = std::min(kRing / 2, (int) (3.f * per));
		int start = (ringW - span - 2 + kRing) % kRing;
		for (int k = 0; k < (int) per + 2 && k < kRing / 2; k++) {
			const int i = (ringW - span - 2 - k + 2 * kRing) % kRing;
			const int j = (i + 1) % kRing;
			if (ringSub[i] < 0.f && ringSub[j] >= 0.f) {
				start = j;
				break;
			}
		}
		double sx = 0, sy = 0, sxy = 0, sxx = 0, syy = 0;
		float pk = 1e-4f;
		for (int k = 0; k < span; k++) {
			const int i = (start + k) % kRing;
			const double a = ringIn[i], b = ringSub[i];
			sx += a;
			sy += b;
			sxy += a * b;
			sxx += a * a;
			syy += b * b;
			pk = std::max(pk, std::max(std::abs(ringIn[i]), std::abs(ringSub[i])));
		}
		const double n = std::max(1, span);
		const double cov = sxy / n - sx / n * sy / n, vx = sxx / n - sx / n * sx / n, vy = syy / n - sy / n * sy / n;
		s.corr = vx > 1e-12 && vy > 1e-12 ? (float) (cov / std::sqrt(vx * vy)) : 0.f;
		s.subInDb = sxx > 1e-12 && syy > 1e-12 ? (float) (10.0 * std::log10(syy / sxx)) : 0.f;
		for (int b = 0; b < kPts; b++) {
			const int i = (start + (int) ((float) b / (kPts - 1) * (span - 1))) % kRing;
			s.in[b] = ringIn[i] / pk;
			s.sub[b] = ringSub[i] / pk;
		}
		s.f0Now = (float) m.f0;
		s.confNow = (float) m.confidence;
		s.mode = mode;
		s.monitor = monitor;
		s.repair = repair;
		s.midi = prm.midiMode;
		s.editsTotal = (uint32_t) m.editsRepaired;
		s.latencyMs = (int) std::round(1000.0 * (engine.getLatency() + kBlock) / sr);
		snap.publish();
	}
};

// ---------------------------------------------------------------------------------------- screens

// low.end map: the tracked f0 (bright) over the last ~4 s on a log axis 25 Hz .. 1 kHz, the mono
// zone under SPLIT x f0 (filled), a magenta tick for every repaired edit.
struct MapScreen : Screen {
	Substrate* module = nullptr;
	Snapshot demo;

	MapScreen() {
		const float notes[8]{55.f, 55.f, 73.4f, 49.f, 55.f, 82.4f, 65.4f, 55.f};
		for (int k = 0; k < kHist; k++) {
			demo.f0[k] = notes[k / 8];
			demo.split[k] = 1.5f * demo.f0[k];
			demo.conf[k] = 0.9f;
			demo.edits[k] = (k % 8 == 0 && k < 32) ? 1 : 0;
		}
		demo.f0Now = 55.f;
		demo.confNow = 0.93f;
		demo.editsTotal = 4;
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		const float pad = 3.f;
		char buf[48];
		std::snprintf(buf, sizeof buf, "%s %.1f Hz", s.midi ? "V/OCT" : "F0", s.f0Now);
		text(vg, pad, pad - 1.f, buf, 9.f, semi::theme::cyan());
		std::snprintf(buf, sizeof buf, "EDITS %u", (unsigned) s.editsTotal);
		text(vg, w - pad, pad - 1.f, buf, 9.f, s.repair ? semi::theme::magenta() : semi::theme::dim(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		const float y0 = pad + 11.f, y1 = h - pad - 4.f;
		const float lo = std::log(25.f), hi = std::log(1000.f);
		auto Y = [&](float hz) { return y1 - (y1 - y0) * clamp((std::log(std::max(hz, 1.f)) - lo) / (hi - lo), 0.f, 1.f); };
		for (float g : {50.f, 100.f, 200.f, 400.f})
			line(vg, pad, Y(g), w - pad, Y(g), semi::theme::grid());
		const float cw = (w - 2.f * pad) / kHist;
		float ys[kHist];
		for (int k = 0; k < kHist; k++) {
			const float x = pad + k * cw;
			if (s.f0[k] > 1.f)
				fillRect(vg, x, Y(s.split[k]), cw + 0.5f, y1 - Y(s.split[k]), semi::theme::cyanDeep());
			if (s.edits[k])
				fillRect(vg, x, y1, std::max(cw, 1.5f), 3.5f, semi::theme::magenta());
			ys[k] = Y(s.f0[k]);
		}
		polyline(vg, ys, kHist, pad + 0.5f * cw, w - 2.f * pad - cw, semi::theme::cyan());
	}
};

// phase.scope: three periods of the input's low band (dim) and the rebuilt sub (bright), from a
// rising zero crossing of the sub. Lined up = in phase. CORR, SUB/IN, phase mode, monitor.
struct ScopeScreen : Screen {
	Substrate* module = nullptr;
	Snapshot demo;

	ScopeScreen() {
		for (int b = 0; b < kPts; b++) {
			const float ph = 3.f * 6.2832f * b / (kPts - 1);
			demo.sub[b] = 0.8f * std::sin(ph);
			demo.in[b] = 0.7f * std::sin(ph + 0.15f) + 0.15f * std::sin(2.f * ph);
		}
		demo.corr = 0.97f;
		demo.subInDb = 0.6f;
		demo.latencyMs = 60;
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		const float pad = 3.f;
		char buf[48];
		std::snprintf(buf, sizeof buf, "CORR %+.2f", s.corr);
		text(vg, pad, pad - 1.f, buf, 9.f, s.corr > 0.7f ? semi::theme::cyan() : semi::theme::amber());
		std::snprintf(buf, sizeof buf, "SUB/IN %+.1f", s.subInDb);
		text(vg, w - pad, pad - 1.f, buf, 9.f, semi::theme::text(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		const float y0 = pad + 10.f, y1 = h - pad - 10.f;
		const float mid = 0.5f * (y0 + y1), amp = 0.5f * (y1 - y0);
		line(vg, pad, mid, w - pad, mid, semi::theme::grid());
		float a[kPts], b[kPts];
		for (int i = 0; i < kPts; i++) {
			a[i] = mid - amp * clamp(s.in[i], -1.f, 1.f);
			b[i] = mid - amp * clamp(s.sub[i], -1.f, 1.f);
		}
		polyline(vg, a, kPts, pad, w - 2.f * pad, semi::theme::cyanDim(), semi::platform::kStroke * 1.5f);
		polyline(vg, b, kPts, pad, w - 2.f * pad, semi::theme::cyan());
		std::snprintf(buf, sizeof buf, "PHASE %s", kModeNames[(size_t) clamp(s.mode, 0, 2)].c_str());
		text(vg, pad, h - pad, buf, 9.f, semi::theme::dim(), NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
		std::snprintf(buf, sizeof buf, "%s  %d ms", kMonNames[(size_t) clamp(s.monitor, 0, 3)].c_str(), s.latencyMs);
		text(vg, w - pad, h - pad, buf, 9.f, s.monitor ? semi::theme::amber() : semi::theme::dim(), NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
	}
};

struct SubstrateWidget : ModuleWidget {
	SubstrateWidget(Substrate* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Substrate.svg")));

		auto* map = semi::createScreenMm<MapScreen>(Vec(L::MAP.x, L::MAP.y), Vec(L::MAP.w, L::MAP.h));
		map->module = module;
		addChild(map);
		auto* scope = semi::createScreenMm<ScopeScreen>(Vec(L::SCOPE.x, L::SCOPE.y), Vec(L::SCOPE.w, L::SCOPE.h));
		scope->module = module;
		addChild(scope);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		addParam(createParamCentered<semi::Button>(at(L::REPAIR), module, Substrate::REPAIR_PARAM));
		addChild(createLightCentered<semi::MagentaLedSmall>(at(L::REPAIR_LED), module, Substrate::REPAIR_LIGHT));
		addParam(createParamCentered<semi::Button>(at(L::MODE), module, Substrate::MODE_PARAM));
		addParam(createParamCentered<semi::Button>(at(L::MON), module, Substrate::MON_PARAM));
		addChild(createLightCentered<semi::AmberLedSmall>(at(L::MON_LED), module, Substrate::MON_LIGHT));

		addParam(createParamCentered<semi::KnobM>(at(L::SPLIT), module, Substrate::SPLIT_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::SUB), module, Substrate::SUB_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::PHASE), module, Substrate::PHASE_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::FOLLOW), module, Substrate::FOLLOW_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::STAB), module, Substrate::STAB_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::RESID), module, Substrate::RESID_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::UPPER), module, Substrate::UPPER_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::WIDTH), module, Substrate::WIDTH_PARAM));
		addParam(createParamCentered<semi::KnobMHot>(at(L::SENS), module, Substrate::SENS_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::OUT), module, Substrate::OUT_PARAM));

		addInput(createInputCentered<semi::Jack>(at(L::IN_L), module, Substrate::IN_L_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::IN_R), module, Substrate::IN_R_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::VOCT), module, Substrate::VOCT_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::SPLIT_CV), module, Substrate::SPLIT_CV_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::SUB_CV), module, Substrate::SUB_CV_INPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_L), module, Substrate::OUT_L_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_R), module, Substrate::OUT_R_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::SUB_OUT), module, Substrate::SUB_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::F0), module, Substrate::F0_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Substrate*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Sub phase mode", kModeNames, [=]() { return (size_t) m->mode; }, [=](size_t i) { m->mode = (int) i; }));
		menu->addChild(createIndexSubmenuItem("Monitor", kMonNames, [=]() { return (size_t) m->monitor; }, [=](size_t i) { m->monitor = (int) i; }));
		menu->addChild(createBoolPtrMenuItem("Edit repair", "", &m->repair));
		std::vector<std::string> pitches, fades;
		for (float v : kMaxPitch)
			pitches.push_back(string::f("%.0f Hz", v));
		for (float v : kFades)
			fades.push_back(string::f("%g ms", v));
		menu->addChild(createIndexSubmenuItem("Max pitch", pitches,
		    [=]() {
			    size_t b = 0;
			    for (size_t i = 1; i < kMaxPitch.size(); i++)
				    if (std::abs(kMaxPitch[i] - m->f0Max) < std::abs(kMaxPitch[b] - m->f0Max))
					    b = i;
			    return b;
		    },
		    [=](size_t i) { m->f0Max = kMaxPitch[i]; }));
		menu->addChild(createIndexSubmenuItem("Repair fade", fades,
		    [=]() {
			    size_t b = 0;
			    for (size_t i = 1; i < kFades.size(); i++)
				    if (std::abs(kFades[i] - m->fadeMs) < std::abs(kFades[b] - m->fadeMs))
					    b = i;
			    return b;
		    },
		    [=](size_t i) { m->fadeMs = kFades[i]; }));
	}
};

Model* modelSubstrate = createModel<Substrate, SubstrateWidget>("Substrate");
