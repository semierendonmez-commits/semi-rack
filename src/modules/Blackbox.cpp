// BLACKBOX — Rack / MetaModule port of BLACKBOX, the family's flight recorder (engine: engines/blackbox,
// a cable tape written for this port; its MIDI map and SMF writer follow the plug-in's Export.cpp).
//
// The plug-in records what happens on the shared-memory buses. Here it records what travels on cables:
// the BUS (POS, ENERGY, SCENE, EVENT) and four CV + four gate lanes, into a fixed ring buffer
// (8-byte records, value lanes with a deadband, gates as edges). It is transparent: the outputs pass the
// inputs through until you
//   PLAY    replay the tape from the cursor on every output (recording pauses meanwhile)
//   RECALL  hold the state at the cursor on the outputs (press again to go back to THRU)
//   MARK    put a mark on the tape (a conductor marker in the MIDI file)
// and EXPORT (menu) the whole tape or the window on the screen as a multi-track MIDI file:
// Rack: file dialog; MetaModule: the firmware's save browser. The file is built and written in the GUI
// context, never on the audio thread.

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/FamilyBusCV.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/BlackboxLayout.hpp"
#include "blackbox/CableTape.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>

#ifdef METAMODULE
#include "filesystem/async_filebrowser.hh"
#else
#include <osdialog.h>
#endif

namespace L = semi::layout::blackbox;
using semi::Screen;

namespace {

#ifdef METAMODULE
constexpr size_t kCapacity = 65536;    // 512 KB: ~10 min of busy lanes, hours of slow ones
#else
constexpr size_t kCapacity = 524288;   // 4 MB
#endif
constexpr int kKeyEvery = 1024;

enum Mode { MODE_THRU, MODE_PLAY, MODE_HOLD };

const float kDeadbands[] = {0.002f, 0.005f, 0.02f};
const float kRates[] = {1.f, 4.f, 10.f};

struct Snapshot {
	uint32_t t0 = 0, t1 = 0;          // the window on the screen (tape ms)
	uint32_t begin = 0, end = 0;      // what the ring holds
	uint32_t cursor = 0, play = 0;
	uint64_t records = 0;
	float used = 0.f;
	int marks = 0;
	int mode = MODE_THRU;
	bool armed = false, recording = false, full = false;
};

std::string tapeClock(uint32_t ms) {
	const unsigned s = (unsigned) (ms / 1000), tenth = (unsigned) ((ms / 100) % 10);
	char b[24];
	if (s >= 3600)
		std::snprintf(b, sizeof b, "%u:%02u:%02u", s / 3600, (s / 60) % 60, s % 60);
	else
		std::snprintf(b, sizeof b, "%02u:%02u.%u", s / 60, s % 60, tenth);
	return b;
}

} // namespace

struct Blackbox : Module {
	enum ParamId { REC_PARAM, PLAY_PARAM, RECALL_PARAM, MARK_PARAM, CURSOR_PARAM, ZOOM_PARAM, PARAMS_LEN };
	enum InputId { REC_INPUT, PLAY_INPUT, MARK_INPUT, BUS_INPUT, ENUMS(CV_INPUT, 4), ENUMS(GATE_INPUT, 4), INPUTS_LEN };
	enum OutputId { BUS_OUTPUT, ENUMS(CV_OUTPUT, 4), ENUMS(GATE_OUTPUT, 4), OUTPUTS_LEN };
	enum LightId { REC_LIGHT, PLAY_LIGHT, RECALL_LIGHT, LIGHTS_LEN };

	bbxcv::Tape tape{kCapacity};
	bbxcv::Recorder rec{tape};
	bbxcv::Player player{tape};

	// settings (menu + JSON)
	std::atomic<bool> armed{true};
	int whenFull = 0;          // 0 roll, 1 stop
	int atEnd = 0;             // 0 stop, 1 loop from the cursor
	int deadband = 1, rate = 1;
	bool pitched[4] = {false, false, false, false};
	bool cvBipolar = false;
	std::atomic<bool> clearReq{false};

	// audio thread
	int mode = MODE_THRU;
	uint32_t playFrom = 0;
	bool full = false;
	dsp::BooleanTrigger recBtn, playBtn, recallBtn, markBtn;
	dsp::SchmittTrigger recTrig, playTrig, markTrig;
	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;
	float recBlink = 0.f;

	// the window (audio thread computes it, the screen and the export read it from the snapshot)
	std::atomic<uint32_t> winT0{0}, winT1{0};
	std::string lastExport;

	Blackbox() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configButton(REC_PARAM, "Record on / off");
		configButton(PLAY_PARAM, "Replay from the cursor / stop");
		configButton(RECALL_PARAM, "Recall: hold the state at the cursor / back to THRU");
		configButton(MARK_PARAM, "Mark");
		configParam(CURSOR_PARAM, 0.f, 1.f, 1.f, "Cursor", " %", 0.f, 100.f)->description =
		    "Where on the tape: 0 = the oldest record, 100 % = now";
		configParam(ZOOM_PARAM, 0.f, 1.f, 1.f, "Zoom", " %", 0.f, 100.f)->description =
		    "The screen's window: 2 s .. the whole tape";
		configInput(REC_INPUT, "Record trigger")->description = "Toggles recording";
		configInput(PLAY_INPUT, "Play trigger")->description = "Toggles replay from the cursor";
		configInput(MARK_INPUT, "Mark trigger");
		configInput(BUS_INPUT, "Bus")->description = "FamilyBus, 4 channels: POS, ENERGY, SCENE, EVENT";
		for (int i = 0; i < 4; i++)
			configInput(CV_INPUT + i, string::f("CV %d", i + 1));
		for (int i = 0; i < 4; i++)
			configInput(GATE_INPUT + i, string::f("Gate %d", i + 1));
		configOutput(BUS_OUTPUT, "Bus")->description = "Thru, or the replayed / recalled BUS";
		for (int i = 0; i < 4; i++)
			configOutput(CV_OUTPUT + i, string::f("CV %d", i + 1));
		for (int i = 0; i < 4; i++)
			configOutput(GATE_OUTPUT + i, string::f("Gate %d", i + 1));
		configLight(REC_LIGHT, "Recording");
		configLight(PLAY_LIGHT, "Replaying");
		configLight(RECALL_LIGHT, "Holding the recalled state");
		applySettings();
	}

	void applySettings() {
		rec.settings.deadband = kDeadbands[clamp(deadband, 0, 2)];
		rec.settings.rateMs = kRates[clamp(rate, 0, 2)];
		rec.settings.keyEvery = kKeyEvery;
	}

	float busV(int ch) {
		Input& in = inputs[BUS_INPUT];
		return ch < in.getChannels() ? in.getVoltage(ch) : 0.f;
	}

	// tape times: oldest record, now
	uint32_t tapeBegin() const {
		const uint64_t b = tape.begin(), e = tape.end();
		return e > b ? tape.at(b).t : 0;
	}

	uint32_t cursorMs() {
		const uint32_t b = tapeBegin(), e = std::max(b, rec.now());
		return b + (uint32_t) std::llround((double) (e - b) * clamp(params[CURSOR_PARAM].getValue(), 0.f, 1.f));
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		whenFull = 0;
		atEnd = 0;
		deadband = rate = 1;
		for (bool& p : pitched)
			p = false;
		cvBipolar = false;
		armed = true;
		applySettings();
	}

	void process(const ProcessArgs& args) override {
		if (clearReq.exchange(false)) {
			player.stop();
			tape.clear();
			rec.reset();
			mode = MODE_THRU;
			full = false;
		}

		// ---- transport
		if (recBtn.process(params[REC_PARAM].getValue() > 0.f) | recTrig.process(inputs[REC_INPUT].getVoltage(), 0.1f, 1.f)) {
			armed = !armed;
			if (armed)
				full = false;
		}
		if (playBtn.process(params[PLAY_PARAM].getValue() > 0.f) | playTrig.process(inputs[PLAY_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (mode == MODE_PLAY) {
				player.stop();
				mode = MODE_THRU;
			}
			else if (tape.end() > 0) {
				playFrom = cursorMs();
				player.start(playFrom, kKeyEvery);
				mode = MODE_PLAY;
			}
		}
		if (recallBtn.process(params[RECALL_PARAM].getValue() > 0.f)) {
			if (mode == MODE_HOLD) {
				player.stop();
				mode = MODE_THRU;
			}
			else if (tape.end() > 0) {
				player.start(cursorMs(), kKeyEvery);
				mode = MODE_HOLD;
			}
		}
		if (markBtn.process(params[MARK_PARAM].getValue() > 0.f) | markTrig.process(inputs[MARK_INPUT].getVoltage(), 0.1f, 1.f))
			rec.mark();

		// ---- record (the inputs, not what we replay)
		float v[bbxcv::kLanes];
		for (int c = 0; c < 4; c++)
			v[bbxcv::LPos + c] = busV(c);
		for (int i = 0; i < 4; i++) {
			v[bbxcv::LCv1 + i] = inputs[CV_INPUT + i].getVoltage();
			v[bbxcv::LGate1 + i] = inputs[GATE_INPUT + i].getVoltage();
		}
		if (whenFull == 1 && tape.end() + 64 >= tape.capacity()) {
			full = true;
			armed = false;
		}
		const bool recording = armed && mode != MODE_PLAY;
		rec.sample(v, recording, args.sampleTime);

		// ---- outputs
		float o[bbxcv::kLanes];
		if (mode == MODE_PLAY) {
			if (!player.sample(args.sampleTime, rec.now())) {
				if (atEnd == 1 && tape.end() > 0)
					player.start(playFrom, kKeyEvery);
				else
					mode = MODE_THRU;
			}
		}
		if (mode == MODE_THRU)
			std::memcpy(o, v, sizeof o);
		else
			for (int l = 0; l < bbxcv::kLanes; l++)
				o[l] = mode == MODE_HOLD && l == bbxcv::LEvent ? 0.f : player.out(l);
		Output& bo = outputs[BUS_OUTPUT];
		bo.setChannels(semi::bus::kChannels);
		for (int c = 0; c < 4; c++)
			bo.setVoltage(o[bbxcv::LPos + c], c);
		for (int i = 0; i < 4; i++) {
			outputs[CV_OUTPUT + i].setVoltage(o[bbxcv::LCv1 + i]);
			outputs[GATE_OUTPUT + i].setVoltage(o[bbxcv::LGate1 + i]);
		}

		if (ui.tick(args.sampleTime))
			publish(recording);
	}

	void publish(bool recording) {
		Snapshot& s = snap.writeSlot();
		s.begin = tapeBegin();
		s.end = std::max(s.begin, rec.now());
		s.cursor = cursorMs();
		s.play = mode == MODE_PLAY ? (uint32_t) player.position() : s.cursor;
		s.records = tape.end() - tape.begin();
		s.used = (float) s.records / (float) tape.capacity();
		s.marks = rec.marks();
		s.mode = mode;
		s.armed = armed;
		s.recording = recording;
		s.full = full;

		// the window: 2 s .. the whole tape, centred on the cursor (the playhead while replaying)
		const double span = std::max(1.0, (double) (s.end - s.begin));
		const double z = clamp(params[ZOOM_PARAM].getValue(), 0.f, 1.f);
		const double len = std::min(span, 2000.0 * std::pow(std::max(1.0, span / 2000.0), z));
		const double centre = mode == MODE_PLAY ? s.play : s.cursor;
		double a = centre - len * 0.5;
		a = std::max((double) s.begin, std::min(a, (double) s.end - len));
		s.t0 = (uint32_t) std::max(0.0, a);
		s.t1 = (uint32_t) std::max((double) s.t0 + 1.0, a + len);
		winT0 = s.t0;
		winT1 = s.t1;
		snap.publish();

		recBlink = recording ? 1.f : (armed ? 0.25f : 0.f);
		lights[REC_LIGHT].setBrightness(recBlink);
		lights[PLAY_LIGHT].setBrightness(mode == MODE_PLAY ? 1.f : 0.f);
		lights[RECALL_LIGHT].setBrightness(mode == MODE_HOLD ? 1.f : 0.f);
	}

	// ---- export (GUI context only) ------------------------------------------------------------

	void exportTo(const std::string& pathIn, uint32_t t0, uint32_t t1) {
		std::string path = pathIn;
		if (path.size() < 4 || (path.compare(path.size() - 4, 4, ".mid") != 0 && path.compare(path.size() - 4, 4, ".MID") != 0))
			path += ".mid";
		std::vector<bbxcv::Rec> copy;
		if (!tape.copyOut(copy)) {
			lastExport = "nothing on the tape";
			return;
		}
		bbxcv::MidiOptions opt;
		opt.t0 = t0;
		opt.t1 = t1;
		for (int k = 0; k < 4; k++)
			opt.pitched[k] = pitched[k];
		opt.cvBipolar = cvBipolar;
		const bbxcv::MidiResult r = bbxcv::exportMidi(copy, opt);
		FILE* f = std::fopen(path.c_str(), "wb");
		if (!f) {
			lastExport = "could not write " + path;
			return;
		}
		const size_t n = std::fwrite(r.bytes.data(), 1, r.bytes.size(), f);
		std::fclose(f);
		char b[160];
		std::snprintf(b, sizeof b, "%s: %d tracks, %d notes, %d CCs @ %.0f BPM%s", n == r.bytes.size() ? "saved" : "short write",
		              r.tracks, r.notes, r.ccs, r.bpm, r.tempoFromBus ? " (BUS)" : "");
		lastExport = b;
	}

	void requestExport(bool window) {
		const uint32_t t0 = window ? winT0.load() : 0u;
		const uint32_t t1 = window ? winT1.load() : 0xffffffffu;
#ifdef METAMODULE
		async_save_file("", "blackbox", ".mid", [this, t0, t1](char* path) {
			if (path) {
				exportTo(path, t0, t1);
				std::free(path);
			}
		});
#else
		osdialog_filters* filters = osdialog_filters_parse("MIDI file:mid");
		char* path = osdialog_file(OSDIALOG_SAVE, nullptr, "blackbox.mid", filters);
		osdialog_filters_free(filters);
		if (path) {
			exportTo(path, t0, t1);
			std::free(path);
		}
#endif
	}

	// ---- state (the tape itself is not stored in the patch) ---------------------------------------

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "armed", json_boolean(armed));
		json_object_set_new(root, "whenFull", json_integer(whenFull));
		json_object_set_new(root, "atEnd", json_integer(atEnd));
		json_object_set_new(root, "deadband", json_integer(deadband));
		json_object_set_new(root, "rate", json_integer(rate));
		json_object_set_new(root, "cvBipolar", json_boolean(cvBipolar));
		json_t* p = json_array();
		for (bool b : pitched)
			json_array_append_new(p, json_boolean(b));
		json_object_set_new(root, "pitched", p);
		return root;
	}

	void dataFromJson(json_t* root) override {
		auto geti = [&](const char* k, int& v, int lo, int hi) {
			if (json_t* j = json_object_get(root, k))
				v = clamp((int) json_integer_value(j), lo, hi);
		};
		if (json_t* j = json_object_get(root, "armed"))
			armed = json_is_true(j);
		geti("whenFull", whenFull, 0, 1);
		geti("atEnd", atEnd, 0, 1);
		geti("deadband", deadband, 0, 2);
		geti("rate", rate, 0, 2);
		if (json_t* j = json_object_get(root, "cvBipolar"))
			cvBipolar = json_is_true(j);
		size_t i;
		json_t* q;
		json_array_foreach(json_object_get(root, "pitched"), i, q) {
			if (i < 4)
				pitched[i] = json_is_true(q);
		}
		applySettings();
	}
};

// ---- screens --------------------------------------------------------------------------------

namespace {

NVGcolor cvColor(int k) {
	switch (k) {
		case 0: return semi::theme::cyan();
		case 1: return semi::theme::amber();
		case 2: return semi::theme::magenta();
		default: return semi::theme::text();
	}
}

// A procedural tape for the module browser.
const bbxcv::View& demoView() {
	static const bbxcv::View v = [] {
		bbxcv::View d;
		d.t0 = 0;
		d.t1 = 64000;
		d.any = true;
		for (int c = 0; c < bbxcv::kViewCols; c++) {
			const float x = c / (float) bbxcv::kViewCols;
			d.energy[c] = 10.f * clamp(0.25f + 0.6f * x * x + (c > 60 && c < 70 ? -0.4f : 0.f), 0.f, 1.f);
			d.scene[c] = (int8_t) (c / 24);
			d.event[c] = c % 24 == 23 ? 4 : (c % 12 == 11 ? 2 : 0);
			d.bar[c] = c % 3 == 0 ? (c % 24 == 0 ? 2 : 1) : 0;
			d.mark[c] = c == 40 ? 1 : 0;
			d.cv[0][c] = 5.f + 4.f * std::sin(x * 12.f);
			d.cv[1][c] = (c / 6) % 2 ? 7.f : 2.f;
			d.cv[2][c] = 10.f * x;
			d.cv[3][c] = 5.f + 3.f * std::sin(x * 31.f) * std::cos(x * 3.f);
			d.gates[c] = (uint8_t) ((c % 4 == 0 ? 1 : 0) | (c % 6 < 3 ? 2 : 0) | (c % 8 == 0 ? 4 : 0) | (c > 48 && c % 2 ? 8 : 0));
		}
		return d;
	}();
	return v;
}

Snapshot demoSnapshot() {
	Snapshot s;
	s.t0 = 0;
	s.t1 = 64000;
	s.begin = 0;
	s.end = 64000;
	s.cursor = 40000;
	s.play = 40000;
	s.records = 18432;
	s.used = 0.28f;
	s.marks = 1;
	s.armed = s.recording = true;
	return s;
}

} // namespace

// The tape: ENERGY (filled), SCENE strip, EVENT turns (height = line), CV 1-4, GATE 1-4, bar ticks on
// the ruler, marks (amber), the cursor (magenta) and the playhead (cyan).
struct TapeScreen : Screen {
	Blackbox* module = nullptr;
	bbxcv::View view;
	uint32_t vT0 = 1, vT1 = 0;
	uint64_t vEnd = ~0ull;
	uint32_t vGen = ~0u;
	int sinceBuild = 0;

	void drawScreen(NVGcontext* vg) override {
		Snapshot s = module ? module->snap.read() : demoSnapshot();
		const bbxcv::View* v = &demoView();
		if (module) {
			const uint64_t end = module->tape.end();
			const uint32_t gen = module->tape.generation();
			sinceBuild++;
			const bool windowMoved = s.t0 != vT0 || s.t1 != vT1 || gen != vGen;
			if ((windowMoved && sinceBuild >= 2) || (end != vEnd && sinceBuild >= 6)) {
				bbxcv::buildView(module->tape, s.t0, s.t1, kKeyEvery, view);
				vT0 = s.t0;
				vT1 = s.t1;
				vEnd = end;
				vGen = gen;
				sinceBuild = 0;
			}
			v = &view;
		}

		const float w = box.size.x, h = box.size.y;
		const float lx = 20.f, gw = w - lx - 2.f;
		const float cw = gw / bbxcv::kViewCols;
		auto X = [&](uint32_t t) {
			const double span = std::max(1.0, (double) v->t1 - (double) v->t0);
			return lx + gw * (float) std::max(0.0, std::min(1.0, ((double) t - (double) v->t0) / span));
		};

		// ruler: times + bar ticks
		const float rulerH = 10.f;
		text(vg, lx, 1.f, tapeClock(v->t0), 8.f, semi::theme::dim());
		text(vg, w - 2.f, 1.f, tapeClock(v->t1), 8.f, semi::theme::dim(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		for (int c = 0; c < bbxcv::kViewCols; c++)
			if (v->bar[c])
				line(vg, lx + cw * (c + 1), v->bar[c] == 2 ? rulerH - 5.f : rulerH - 2.5f, lx + cw * (c + 1), rulerH,
				     v->bar[c] == 2 ? semi::theme::text() : semi::theme::cyanDim());

		float y = rulerH + 1.f;
		auto label = [&](const char* s, float yy, float hh, NVGcolor c) {
			text(vg, 1.5f, yy + hh * 0.5f, s, 8.f, c, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		};

		// ENERGY
		const float eh = 20.f;
		label("NRG", y, eh, semi::theme::dim());
		fillRect(vg, lx, y, gw, eh, semi::theme::cell());
		for (int c = 0; c < bbxcv::kViewCols; c++) {
			const float e = clamp(v->energy[c] / 10.f, 0.f, 1.f);
			if (e > 0.01f)
				fillRect(vg, lx + cw * c, y + eh * (1.f - e), cw + 0.3f, eh * e, semi::theme::cyanDim());
		}
		y += eh + 1.5f;

		// SCENE strip
		const float sh = 9.f;
		label("SCN", y, sh, semi::theme::dim());
		for (int c = 0; c < bbxcv::kViewCols;) {
			int e = c;
			while (e < bbxcv::kViewCols && v->scene[e] == v->scene[c])
				e++;
			if (v->scene[c] >= 0) {
				const NVGcolor col = v->scene[c] % 2 ? semi::theme::cyanDeep() : semi::theme::grid();
				fillRect(vg, lx + cw * c, y, cw * (e - c) - 0.6f, sh, col);
				if (cw * (e - c) > 10.f) {
					char b[8];
					std::snprintf(b, sizeof b, "%d", v->scene[c]);
					text(vg, lx + cw * c + 1.5f, y + sh * 0.5f, b, 8.f, semi::theme::text(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
				}
			}
			c = e;
		}
		y += sh + 1.5f;

		// EVENT
		const float evh = 9.f;
		label("EVT", y, evh, semi::theme::dim());
		for (int c = 0; c < bbxcv::kViewCols; c++)
			if (v->event[c]) {
				const float hh = evh * v->event[c] / 4.f;
				line(vg, lx + cw * (c + 0.5f), y + evh, lx + cw * (c + 0.5f), y + evh - hh, semi::theme::magenta(), semi::platform::kStroke);
			}
		y += evh + 1.5f;

		// CV 1-4
		const float gateBlock = 4 * 6.f;
		const float cvh = (h - y - gateBlock - 2.f) / 4.f;
		for (int k = 0; k < 4; k++) {
			char nm[4] = {'C', 'V', (char) ('1' + k), 0};
			label(nm, y, cvh, semi::theme::dim());
			line(vg, lx, y + cvh - 0.5f, lx + gw, y + cvh - 0.5f, semi::theme::grid());
			float lo = v->cv[k][0], hi = lo;
			for (int c = 1; c < bbxcv::kViewCols; c++) {
				lo = std::min(lo, v->cv[k][c]);
				hi = std::max(hi, v->cv[k][c]);
			}
			// fixed 0..10 V scale unless the lane goes negative
			const float a = lo < -0.05f ? -10.f : 0.f, b = 10.f;
			float ys[bbxcv::kViewCols];
			for (int c = 0; c < bbxcv::kViewCols; c++)
				ys[c] = y + cvh - 1.5f - (cvh - 3.f) * clamp((v->cv[k][c] - a) / (b - a), 0.f, 1.f);
			if (v->any)
				polyline(vg, ys, bbxcv::kViewCols, lx + cw * 0.5f, gw - cw, cvColor(k));
			y += cvh;
		}

		// GATES
		for (int k = 0; k < 4; k++) {
			char nm[3] = {'G', (char) ('1' + k), 0};
			label(nm, y, 6.f, semi::theme::dim());
			for (int c = 0; c < bbxcv::kViewCols;) {
				if (!(v->gates[c] & (1 << k))) {
					c++;
					continue;
				}
				int e = c;
				while (e < bbxcv::kViewCols && (v->gates[e] & (1 << k)))
					e++;
				fillRect(vg, lx + cw * c, y + 1.f, std::max(1.2f, cw * (e - c) - 0.6f), 4.f, cvColor(k));
				c = e;
			}
			y += 6.f;
		}

		// marks, cursor, playhead
		for (int c = 0; c < bbxcv::kViewCols; c++)
			if (v->mark[c])
				line(vg, lx + cw * (c + 0.5f), rulerH, lx + cw * (c + 0.5f), h, semi::theme::amber());
		if (s.cursor >= v->t0 && s.cursor <= v->t1) {
			const float cx = X(s.cursor);
			line(vg, cx, 0.f, cx, h, semi::theme::magenta(), semi::platform::kStroke);
		}
		if (s.mode == MODE_PLAY && s.play >= v->t0 && s.play <= v->t1) {
			const float px = X(s.play);
			line(vg, px, 0.f, px, h, semi::theme::cyan(), semi::platform::kStroke);
		}
	}
};

// REC / PLAY / HOLD state, tape length, how full the ring is, cursor, marks.
struct ReadoutScreen : Screen {
	Blackbox* module = nullptr;

	void drawScreen(NVGcontext* vg) override {
		const Snapshot s = module ? module->snap.read() : demoSnapshot();
		const float w = box.size.x, h = box.size.y;
		const float my = h * 0.5f;
		const char* st = s.mode == MODE_PLAY ? "PLAY" : s.mode == MODE_HOLD ? "HOLD" : s.recording ? "REC" : s.full ? "FULL" : "STOP";
		const NVGcolor sc = s.mode == MODE_PLAY ? semi::theme::cyan() : s.mode == MODE_HOLD ? semi::theme::amber()
		                  : s.recording ? semi::theme::magenta() : semi::theme::dim();
		text(vg, 3.f, my, st, 10.f, sc, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		text(vg, 30.f, my, tapeClock(s.end - s.begin), 10.f, semi::theme::text(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

		// ring use
		const float bx = 92.f, bw = 46.f, bh = 6.f;
		fillRect(vg, bx, my - bh / 2.f, bw, bh, semi::theme::cell());
		fillRect(vg, bx, my - bh / 2.f, bw * clamp(s.used, 0.f, 1.f), bh, s.used > 0.9f ? semi::theme::magenta() : semi::theme::cyanDim());
		char b[48];
		std::snprintf(b, sizeof b, "%d%%", (int) std::round(s.used * 100.f));
		text(vg, bx + bw + 3.f, my, b, 9.f, semi::theme::dim(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

		const uint32_t at = s.mode == MODE_PLAY ? s.play : s.cursor;
		std::snprintf(b, sizeof b, "%s %s", s.mode == MODE_PLAY ? ">" : "@", tapeClock(at - std::min(at, s.begin)).c_str());
		text(vg, 168.f, my, b, 10.f, s.mode == MODE_PLAY ? semi::theme::cyan() : semi::theme::magenta(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		std::snprintf(b, sizeof b, "M%d", s.marks);
		text(vg, w - 3.f, my, b, 10.f, semi::theme::amber(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
	}
};

struct BlackboxWidget : ModuleWidget {
	BlackboxWidget(Blackbox* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Blackbox.svg")));

		auto* tp = semi::createScreenMm<TapeScreen>(Vec(L::TAPE.x, L::TAPE.y), Vec(L::TAPE.w, L::TAPE.h));
		tp->module = module;
		addChild(tp);
		auto* ro = semi::createScreenMm<ReadoutScreen>(Vec(L::READOUT.x, L::READOUT.y), Vec(L::READOUT.w, L::READOUT.h));
		ro->module = module;
		addChild(ro);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		addParam(createParamCentered<semi::Button>(at(L::REC), module, Blackbox::REC_PARAM));
		addChild(createLightCentered<semi::MagentaLedSmall>(at(L::REC_LED), module, Blackbox::REC_LIGHT));
		addInput(createInputCentered<semi::Jack>(at(L::REC_IN), module, Blackbox::REC_INPUT));
		addParam(createParamCentered<semi::Button>(at(L::PLAY), module, Blackbox::PLAY_PARAM));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::PLAY_LED), module, Blackbox::PLAY_LIGHT));
		addInput(createInputCentered<semi::Jack>(at(L::PLAY_IN), module, Blackbox::PLAY_INPUT));
		addParam(createParamCentered<semi::Button>(at(L::RECALL), module, Blackbox::RECALL_PARAM));
		addChild(createLightCentered<semi::AmberLedSmall>(at(L::RECALL_LED), module, Blackbox::RECALL_LIGHT));
		addParam(createParamCentered<semi::Button>(at(L::MARK), module, Blackbox::MARK_PARAM));
		addInput(createInputCentered<semi::Jack>(at(L::MARK_IN), module, Blackbox::MARK_INPUT));
		addParam(createParamCentered<semi::KnobM>(at(L::CURSOR), module, Blackbox::CURSOR_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::ZOOM), module, Blackbox::ZOOM_PARAM));

		addInput(createInputCentered<semi::BusJack>(at(L::BUS_IN), module, Blackbox::BUS_INPUT));
		addOutput(createOutputCentered<semi::BusJack>(at(L::BUS_OUT), module, Blackbox::BUS_OUTPUT));
		const L::P cvIn[4] = {L::CV1_IN, L::CV2_IN, L::CV3_IN, L::CV4_IN};
		const L::P gIn[4] = {L::G1_IN, L::G2_IN, L::G3_IN, L::G4_IN};
		const L::P cvOut[4] = {L::CV1_OUT, L::CV2_OUT, L::CV3_OUT, L::CV4_OUT};
		const L::P gOut[4] = {L::G1_OUT, L::G2_OUT, L::G3_OUT, L::G4_OUT};
		for (int i = 0; i < 4; i++) {
			addInput(createInputCentered<semi::Jack>(at(cvIn[i]), module, Blackbox::CV_INPUT + i));
			addInput(createInputCentered<semi::Jack>(at(gIn[i]), module, Blackbox::GATE_INPUT + i));
			addOutput(createOutputCentered<semi::Jack>(at(cvOut[i]), module, Blackbox::CV_OUTPUT + i));
			addOutput(createOutputCentered<semi::Jack>(at(gOut[i]), module, Blackbox::GATE_OUTPUT + i));
		}
	}

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Blackbox*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Export MIDI"));
		menu->addChild(createMenuItem("Export the whole tape...", "", [=]() { m->requestExport(false); }));
		menu->addChild(createMenuItem("Export the window on the screen...", "", [=]() { m->requestExport(true); }));
		if (!m->lastExport.empty())
			menu->addChild(createMenuLabel(m->lastExport));
		menu->addChild(createSubmenuItem("Lanes as V/OCT notes", "", [=](Menu* sub) {
			for (int k = 0; k < 4; k++)
				sub->addChild(createBoolPtrMenuItem(string::f("GATE %d plays CV %d as pitch", k + 1, k + 1), "", &m->pitched[k]));
		}));
		menu->addChild(createBoolPtrMenuItem("CV CCs from ±5 V (else 0..10 V)", "", &m->cvBipolar));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Tape"));
		menu->addChild(createIndexSubmenuItem("When full", {"ROLL (drop the oldest)", "STOP recording"},
		    [=]() { return (size_t) m->whenFull; }, [=](size_t i) { m->whenFull = (int) i; }));
		menu->addChild(createIndexSubmenuItem("Replay at the end", {"STOP (back to THRU)", "LOOP from the cursor"},
		    [=]() { return (size_t) m->atEnd; }, [=](size_t i) { m->atEnd = (int) i; }));
		menu->addChild(createIndexSubmenuItem("CV resolution", {"2 mV (more records)", "5 mV", "20 mV (longer tape)"},
		    [=]() { return (size_t) m->deadband; }, [=](size_t i) { m->deadband = (int) i; m->applySettings(); }));
		menu->addChild(createIndexSubmenuItem("CV rate", {"1 kHz", "250 Hz", "100 Hz"},
		    [=]() { return (size_t) m->rate; }, [=](size_t i) { m->rate = (int) i; m->applySettings(); }));
		menu->addChild(createMenuItem("Clear the tape", "", [=]() { m->clearReq = true; }));
		menu->addChild(createMenuLabel(string::f("%u records max (%u KB)", (unsigned) kCapacity, (unsigned) (kCapacity * 8 / 1024))));
	}
};

Model* modelBlackbox = createModel<Blackbox, BlackboxWidget>("Blackbox");
