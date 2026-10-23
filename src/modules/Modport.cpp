// MODPORT — Rack / MetaModule port of MODPORT (engine: engines/modport, the plugin's
// .mod loader and ProTracker 2.3D replayer after pt2-clone; Driver.h is the Rack transport).
//
// Plays ProTracker-style .mod files with 1-8 channels (M.K., xCHN, OCTA, CD81, FA0x, FLT8,
// 15-sample SoundTracker ...).
//   * FREE: the song's own CIA / VBlank timing, TEMPO scales it (x0.25 .. x4).
//   * CLK patched: one clock edge = one row (the row's ticks are spread over the clock
//     period). RST: back to order 0 (with a clock, the next edge plays row 0). NEXT: jump to
//     the next order (speed / tempo kept). PLAY (pad or trigger) starts / stops.
//   * Per channel: GATE (high while the channel's sample sounds, 1 ms low on every new
//     trigger) and V/OCT (0 V = C-2 = period 428, follows slides and vibrato).
//   * OUT L / R: the Amiga mix with LRRL panning (STEREO = separation), A500 / A1200
//     filter and LED filter as in the plugin (context menu).
// Dropped from the plugin: exports (WAV / stems / MIDI + samples / .mod save), host sync,
// the sample browser and file-folder navigation, MIDI start.
//
// Files are read with semi::readFileBytes off the audio thread (semi::Loader); .mod files
// over 4 MB are refused (MetaModule RAM).

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/Sample.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/ModportLayout.hpp"
#include "modport/Driver.h"

namespace L = semi::layout::modport;
using semi::Screen;

namespace {

constexpr int kBlock = 32;                 // render block; adds 32 samples of latency
constexpr int kCh = ::modport::kMaxChannels;
constexpr size_t kMaxFileBytes = 4u << 20;
constexpr int kRowsShown = 9;
constexpr float kVoltsPerUnit = 5.f;

struct Loaded {
	::modport::Module mod;
	::modport::Player player;
	std::string name;
	double sr = 0.0;
};

struct Snapshot {
	bool loaded = false, playing = false, clock = false, failed = false, busy = false;
	char title[24] = {};
	char format[24] = {};
	char file[40] = {};
	int numCh = 4, order = 0, songLength = 1, pattern = 0, row = 0, speed = 6, bpm = 125;
	::modport::Cell cells[kRowsShown][kCh];
	float level[kCh] = {};
	bool muted[kCh] = {};
};

const std::vector<std::string> kInterpNames = {"Amiga (BLEP steps)", "Linear", "Raw"};
const std::vector<std::string> kModelNames = {"Amiga 500", "Amiga 1200", "Off"};
const std::vector<std::string> kLedNames = {"Follow the song (E0x)", "Always off", "Always on"};
const std::vector<std::string> kTimingNames = {"Auto (as detected)", "CIA", "VBlank"};

} // namespace

struct Modport : Module {
	enum ParamId { TEMPO_PARAM, STEREO_PARAM, LEVEL_PARAM, PLAY_PARAM, LOAD_PARAM, ENUMS(MUTE_PARAM, kCh), PARAMS_LEN };
	enum InputId { CLK_INPUT, RST_INPUT, PLAY_INPUT, NEXT_INPUT, INPUTS_LEN };
	enum OutputId { OUT_L_OUTPUT, OUT_R_OUTPUT, ENUMS(GATE_OUTPUT, kCh), ENUMS(VOCT_OUTPUT, kCh), OUTPUTS_LEN };
	enum LightId { PLAY_LIGHT, ENUMS(MUTE_LIGHT, kCh), LIGHTS_LEN };

	semi::Slot<Loaded> slot;
	semi::Loader<Loaded> loader;
	std::atomic<double> sampleRate{44100.0};
	char loadError[48] = {};
	std::atomic<bool> loadRequest{false};   // LOAD button -> the screen widget opens the dialog

	::modport::drv::Driver driver;
	Loaded* cur = nullptr;                 // audio-thread view of slot.active

	// options (menu, saved)
	int interp = 0, model = 0, led = 0, timing = 0;
	bool loopSong = true;
	bool muted[kCh] = {};
	bool playing = true;

	// block adapter
	enum EvKind { EV_CLK, EV_RST, EV_NEXT };
	struct Ev { int off; EvKind kind; };
	Ev evs[kBlock * 3];
	int numEvs = 0;
	float stemBuf[kCh][kBlock] = {};
	float outL[kBlock] = {}, outR[kBlock] = {};
	float gateBuf[kCh][kBlock] = {}, pitchBuf[kCh][kBlock] = {};
	int pos = 0;

	// clock
	dsp::SchmittTrigger clkTrig, rstTrig, playTrig, nextTrig;
	dsp::BooleanTrigger playBtn, loadBtn, muteBtn[kCh];
	int sinceClk = 0;
	double clkPeriod = 0.0;
	bool clkSeen = false;

	// per-channel gate state
	bool audible[kCh] = {};
	int gapLeft[kCh] = {};
	float pitch[kCh] = {};

	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;

	~Modport() override {
		// background jobs first (and wait for a run in flight): they write into this module
		loader.stopAndWait();
	}

	Modport() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(TEMPO_PARAM, -2.f, 2.f, 0.f, "Tempo (free running)", "x", 2.f);
		configParam(STEREO_PARAM, 0.f, 1.f, 0.5f, "Stereo separation (Amiga LRRL)", " %", 0.f, 100.f);
		configParam(LEVEL_PARAM, -24.f, 6.f, 0.f, "Level", " dB");
		configButton(PLAY_PARAM, "Play / stop");
		configButton(LOAD_PARAM, "Load .mod");
		for (int c = 0; c < kCh; c++) {
			configButton(MUTE_PARAM + c, string::f("Mute channel %d", c + 1));
			configLight(MUTE_LIGHT + c, string::f("Channel %d muted", c + 1));
			configOutput(GATE_OUTPUT + c, string::f("Channel %d gate", c + 1));
			configOutput(VOCT_OUTPUT + c, string::f("Channel %d V/OCT (0 V = C-2)", c + 1));
		}
		configInput(CLK_INPUT, "Clock (one row per edge)");
		configInput(RST_INPUT, "Reset (order 0)");
		configInput(PLAY_INPUT, "Play / stop trigger");
		configInput(NEXT_INPUT, "Next order trigger");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		configLight(PLAY_LIGHT, "Playing");
		loader.attach(this, &slot, [this](const std::string& path, Loaded& l) { return build(path, l); });
	}

	bool build(const std::string& path, Loaded& l) {
		std::vector<uint8_t> bytes;
		if (!semi::readFileBytes(path, bytes, kMaxFileBytes + 1)) {
			std::snprintf(loadError, sizeof loadError, "cannot read file");
			return false;
		}
		if (bytes.size() > kMaxFileBytes) {
			std::snprintf(loadError, sizeof loadError, "file over 4 MB");
			return false;
		}
		::modport::LoadResult r = ::modport::loadModule(bytes.data(), bytes.size());
		if (!r.ok) {
			std::snprintf(loadError, sizeof loadError, "%s", r.error.c_str());
			return false;
		}
		l.mod = std::move(r.module);
		if (l.mod.highFxx && l.mod.timing == ::modport::Timing::Cia)
			l.mod.timing = ::modport::Player::detectTiming(l.mod);
		l.sr = sampleRate.load();
		l.player.prepare(l.sr);
		l.player.setModule(&l.mod);
		l.name = semi::fileName(path);
		loadError[0] = 0;
		return true;
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		sampleRate = e.sampleRate;
		driver.sr = e.sampleRate;
		if (cur) {
			cur->sr = e.sampleRate;
			cur->player.prepare(e.sampleRate);
		}
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		interp = model = led = timing = 0;
		loopSong = true;
		playing = true;
		for (int c = 0; c < kCh; c++)
			muted[c] = false;
		driver.restart();
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "path", json_string(loader.path().c_str()));
		json_object_set_new(root, "interp", json_integer(interp));
		json_object_set_new(root, "model", json_integer(model));
		json_object_set_new(root, "led", json_integer(led));
		json_object_set_new(root, "timing", json_integer(timing));
		json_object_set_new(root, "loop", json_boolean(loopSong));
		json_object_set_new(root, "playing", json_boolean(playing));
		int m = 0;
		for (int c = 0; c < kCh; c++)
			m |= muted[c] ? 1 << c : 0;
		json_object_set_new(root, "muted", json_integer(m));
		return root;
	}

	void dataFromJson(json_t* root) override {
		auto geti = [&](const char* k, int& v, int lo, int hi) {
			if (json_t* j = json_object_get(root, k))
				v = clamp((int) json_integer_value(j), lo, hi);
		};
		geti("interp", interp, 0, 2);
		geti("model", model, 0, 2);
		geti("led", led, 0, 2);
		geti("timing", timing, 0, 2);
		if (json_t* j = json_object_get(root, "loop"))
			loopSong = json_boolean_value(j);
		if (json_t* j = json_object_get(root, "playing"))
			playing = json_boolean_value(j);
		int m = 0;
		geti("muted", m, 0, 255);
		for (int c = 0; c < kCh; c++)
			muted[c] = (m >> c) & 1;
		if (json_t* j = json_object_get(root, "path")) {
			std::string p = json_string_value(j) ? json_string_value(j) : "";
			if (!p.empty())
				loader.request(p);
		}
	}

	void load(const std::string& path) { loader.request(path); }

	// ---- audio ------------------------------------------------------------

	void applyOptions() {
		::modport::Player& p = cur->player;
		p.setInterp((::modport::Interp) interp);
		p.setModel((::modport::Model) model);
		p.setLed((::modport::Led) led);
		const ::modport::Timing t = timing == 0 ? cur->mod.timing : (timing == 1 ? ::modport::Timing::Cia : ::modport::Timing::VBlank);
		p.setTiming(t);
		for (int c = 0; c < kCh; c++)
			p.setMuted(c, muted[c]);
	}

	void jumpNext() {
		::modport::Player& p = cur->player;
		const int speed = p.getSpeed(), bpm = p.getTempo();
		int next = p.getOrder() + 1;
		if (next >= cur->mod.songLength)
			next = 0;
		p.restart(next, 0);
		p.setSpeedTempo(speed, bpm);
		driver.tickLeft = 0.0;
	}

	void renderBlock() {
		if (slot.poll()) {
			cur = slot.active;
			if (cur->sr != sampleRate.load()) {
				cur->sr = sampleRate.load();
				cur->player.prepare(cur->sr);
			}
			driver.sr = cur->sr;
			driver.attach(&cur->player);
			for (int c = 0; c < kCh; c++) {
				audible[c] = false;
				gapLeft[c] = 0;
			}
		}
		for (int c = 0; c < kCh; c++)
			std::fill(stemBuf[c], stemBuf[c] + kBlock, 0.f);
		if (!cur) {
			std::fill(outL, outL + kBlock, 0.f);
			std::fill(outR, outR + kBlock, 0.f);
			for (int c = 0; c < kCh; c++)
				std::fill(gateBuf[c], gateBuf[c] + kBlock, 0.f);
			numEvs = 0;
			return;
		}
		applyOptions();
		driver.clockMode = inputs[CLK_INPUT].isConnected();
		driver.clockPeriod = clkPeriod;
		driver.tempo = std::pow(2.f, params[TEMPO_PARAM].getValue());
		driver.stereo = params[STEREO_PARAM].getValue();
		for (int c = 0; c < kCh; c++)
			driver.trigOffset[c] = -1;

		float* stems[kCh];
		for (int c = 0; c < kCh; c++)
			stems[c] = stemBuf[c];
		::modport::Player& p = cur->player;
		int a = 0;
		for (int i = 0; i < numEvs; i++) {
			const Ev& ev = evs[i];
			driver.render(stems, a, ev.off, playing);
			a = ev.off;
			driver.curOffset = ev.off;
			if (ev.kind == EV_RST)
				driver.restart();
			else if (ev.kind == EV_NEXT)
				jumpNext();
			else if (ev.kind == EV_CLK && playing)
				driver.clockEdge();
		}
		numEvs = 0;
		driver.render(stems, a, kBlock, playing);

		if (playing && !loopSong && (p.hasEnded() || p.stoppedByF00())) {
			playing = false;
			driver.restart();
		}

		const int numCh = cur->mod.channels;
		driver.mix(stems, numCh, outL, outR, kBlock);
		const float g = std::pow(10.f, params[LEVEL_PARAM].getValue() / 20.f);
		for (int i = 0; i < kBlock; i++) {
			outL[i] *= g;
			outR[i] *= g;
		}

		// gates and pitches
		const int gap = std::max(1, (int) (0.001 * cur->sr));
		for (int c = 0; c < kCh; c++) {
			const ::modport::ChannelView v = p.channelView(c);
			const bool now = c < numCh && v.audible && v.volume > 0 && !muted[c];
			const float newPitch = v.period > 0 ? ::modport::drv::periodToVolts(v.period) : pitch[c];
			const int t = driver.trigOffset[c];
			for (int i = 0; i < kBlock; i++) {
				if (i == t)
					gapLeft[c] = gap;
				bool on = (t >= 0 && i >= t) ? now : audible[c];
				if (gapLeft[c] > 0) {
					gapLeft[c]--;
					on = false;
				}
				gateBuf[c][i] = on ? 10.f : 0.f;
				pitchBuf[c][i] = (t >= 0 && i < t) ? pitch[c] : newPitch;
			}
			audible[c] = now;
			pitch[c] = newPitch;
		}
	}

	void addEvent(EvKind k) {
		if (numEvs < (int) (sizeof evs / sizeof evs[0]))
			evs[numEvs++] = {pos, k};
	}

	void process(const ProcessArgs& args) override {
		// buttons
		if (playBtn.process(params[PLAY_PARAM].getValue() > 0.f))
			playing = !playing;
		if (loadBtn.process(params[LOAD_PARAM].getValue() > 0.f))
			loadRequest = true;
		for (int c = 0; c < kCh; c++)
			if (muteBtn[c].process(params[MUTE_PARAM + c].getValue() > 0.f))
				muted[c] = !muted[c];
		if (playTrig.process(inputs[PLAY_INPUT].getVoltage(), 0.1f, 1.f))
			playing = !playing;

		// clock / reset / next -> events at this sample of the collecting block
		sinceClk++;
		if (rstTrig.process(inputs[RST_INPUT].getVoltage(), 0.1f, 1.f))
			addEvent(EV_RST);
		if (nextTrig.process(inputs[NEXT_INPUT].getVoltage(), 0.1f, 1.f))
			addEvent(EV_NEXT);
		if (clkTrig.process(inputs[CLK_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (clkSeen && sinceClk < (int) (args.sampleRate * 4.f))
				clkPeriod = clkPeriod > 0.0 ? clkPeriod + 0.5 * (sinceClk - clkPeriod) : sinceClk;
			clkSeen = true;
			sinceClk = 0;
			addEvent(EV_CLK);
		}
		if (!inputs[CLK_INPUT].isConnected()) {
			clkSeen = false;
			clkPeriod = 0.0;
		}

		outputs[OUT_L_OUTPUT].setVoltage(outL[pos] * kVoltsPerUnit);
		outputs[OUT_R_OUTPUT].setVoltage(outR[pos] * kVoltsPerUnit);
		for (int c = 0; c < kCh; c++) {
			outputs[GATE_OUTPUT + c].setVoltage(gateBuf[c][pos]);
			outputs[VOCT_OUTPUT + c].setVoltage(pitchBuf[c][pos]);
		}
		if (++pos >= kBlock) {
			pos = 0;
			renderBlock();
		}

		if (ui.tick(args.sampleTime))
			publish();
	}

	void publish() {
		lights[PLAY_LIGHT].setBrightness(playing && cur ? 1.f : 0.f);
		for (int c = 0; c < kCh; c++)
			lights[MUTE_LIGHT + c].setBrightness(muted[c] ? 1.f : 0.f);
		Snapshot& s = snap.writeSlot();
		s.loaded = cur != nullptr;
		s.playing = playing;
		s.clock = inputs[CLK_INPUT].isConnected();
		s.failed = loader.failed.load();
		s.busy = loader.busy.load();
		if (cur) {
			const ::modport::Player& p = cur->player;
			std::snprintf(s.title, sizeof s.title, "%s", cur->mod.title().c_str());
			std::snprintf(s.format, sizeof s.format, "%s", cur->mod.formatName.c_str());
			std::snprintf(s.file, sizeof s.file, "%s", cur->name.c_str());
			s.numCh = cur->mod.channels;
			s.order = p.getOrder();
			s.songLength = cur->mod.songLength;
			s.pattern = p.getPattern();
			s.row = p.getRow();
			s.speed = p.getSpeed();
			s.bpm = p.getTempo();
			for (int r = 0; r < kRowsShown; r++) {
				const int row = s.row - kRowsShown / 2 + r;
				for (int c = 0; c < kCh; c++)
					s.cells[r][c] = (row >= 0 && row < ::modport::kRows && c < s.numCh) ? cur->mod.cell(s.pattern, row, c) : ::modport::Cell{};
			}
			for (int c = 0; c < kCh; c++)
				s.level[c] = c < s.numCh ? p.channelView(c).level : 0.f;
		}
		for (int c = 0; c < kCh; c++)
			s.muted[c] = muted[c];
		snap.publish();
	}
};

// The pattern view: header (title, order / pattern, speed / tempo), nine rows around
// the playing one (note, sample, effect as the channel width allows), and a strip of
// channel meters. Rows move at most once per published frame.
struct TrackerScreen : Screen {
	Modport* module = nullptr;
	Snapshot demo;

	TrackerScreen() {
		demo.loaded = true;
		demo.playing = true;
		std::snprintf(demo.title, sizeof demo.title, "space debris");
		std::snprintf(demo.format, sizeof demo.format, "ProTracker M.K.");
		demo.numCh = 4;
		demo.order = 3;
		demo.songLength = 42;
		demo.pattern = 7;
		demo.row = 20;
		static const uint16_t per[] = {428, 0, 339, 0, 285, 428, 0, 214, 0};
		for (int r = 0; r < kRowsShown; r++)
			for (int c = 0; c < 4; c++) {
				::modport::Cell& cl = demo.cells[r][c];
				if ((r + c) % 3 == 0) {
					cl.period = per[(r + c) % 9] ? per[(r + c) % 9] : 254;
					cl.sample = (uint8_t) (1 + c * 3);
				}
				if ((r * 7 + c) % 5 == 0) {
					cl.effect = 0xC;
					cl.param = 0x30;
				}
			}
		for (int c = 0; c < 4; c++)
			demo.level[c] = 0.25f + 0.18f * c;
	}

	void step() override {
		Screen::step();
		if (module && module->loadRequest.exchange(false)) {
			Modport* m = module;
			semi::openFileDialog("MOD:mod,MOD", semi::dirName(m->loader.path()), [m](const std::string& p) { m->load(p); });
		}
	}

	static void hex2(char* b, int v) {
		static const char* d = "0123456789ABCDEF";
		b[0] = d[(v >> 4) & 15];
		b[1] = d[v & 15];
	}

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		const float pad = 3.f;
		const float fs = std::max(9.f, semi::platform::kMinText + 1.f);
		const float cw = fs * 0.54f;  // ShareTechMono advance

		if (!s.loaded) {
			const char* msg = s.busy ? "LOADING..." : "LOAD A .MOD  (button or menu)";
			text(vg, w * 0.5f, h * 0.5f - fs, msg, fs + 1.f, semi::theme::dim(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			if (s.failed && module)
				text(vg, w * 0.5f, h * 0.5f + fs, module->loadError, fs, semi::theme::magenta(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			return;
		}

		// header
		const float hy = pad;
		text(vg, pad, hy, s.title[0] ? s.title : s.file, fs, semi::theme::cyan());
		char buf[48];
		std::snprintf(buf, sizeof buf, "ORD %02d/%02d PAT %02d", s.order, s.songLength, s.pattern);
		text(vg, w - pad, hy, buf, fs, semi::theme::text(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		const float sy = hy + fs + 1.5f;
		if (s.clock)
			std::snprintf(buf, sizeof buf, "%s  CLK  SPD %d", s.playing ? ">" : "||", s.speed);
		else
			std::snprintf(buf, sizeof buf, "%s  SPD %d  BPM %d", s.playing ? ">" : "||", s.speed, s.bpm);
		text(vg, pad, sy, buf, fs, s.playing ? semi::theme::amber() : semi::theme::dim());
		text(vg, w - pad, sy, s.format, fs, semi::theme::dim(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);

		// rows
		const float meterH = 7.f;
		const float y0 = sy + fs + 3.f, y1 = h - pad - meterH - 2.f;
		const float rowH = (y1 - y0) / kRowsShown;
		const float numW = cw * 2.f + 4.f;
		const float colW = (w - 2.f * pad - numW) / std::max(1, s.numCh);
		const int chars = (int) ((colW - 3.f) / cw);
		const int mode = chars >= 10 ? 2 : (chars >= 6 ? 1 : 0);
		const int cur = kRowsShown / 2;
		fillRect(vg, pad, y0 + cur * rowH, w - 2.f * pad, rowH, semi::theme::cyanDeep());
		for (int c = 1; c < s.numCh; c++) {
			const float x = pad + numW + c * colW - 1.5f;
			line(vg, x, y0, x, y1, semi::theme::grid());
		}
		for (int r = 0; r < kRowsShown; r++) {
			const int row = s.row - cur + r;
			if (row < 0 || row >= ::modport::kRows)
				continue;
			const float y = y0 + r * rowH + rowH * 0.5f;
			const int al = NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE;
			std::snprintf(buf, sizeof buf, "%02d", row);
			text(vg, pad + 1.f, y, buf, fs, r == cur ? semi::theme::amber() : semi::theme::dim(), al);
			for (int c = 0; c < s.numCh; c++) {
				const ::modport::Cell& cl = s.cells[r][c];
				float x = pad + numW + c * colW;
				const bool dimCol = s.muted[c];
				const std::string note = cl.period ? ::modport::noteName(cl.period) : "---";
				text(vg, x, y, note, fs, cl.period && !dimCol ? (r == cur ? semi::theme::cyan() : semi::theme::text()) : semi::theme::grid(), al);
				x += cw * 3.6f;
				if (mode >= 1) {
					char sm[3] = {'.', '.', 0};
					if (cl.sample)
						hex2(sm, cl.sample);
					text(vg, x, y, sm, fs, cl.sample && !dimCol ? semi::theme::amber() : semi::theme::grid(), al);
					x += cw * 2.6f;
				}
				if (mode >= 2) {
					char fx[4] = {'.', '.', '.', 0};
					if (cl.effect || cl.param) {
						fx[0] = "0123456789ABCDEF"[cl.effect & 15];
						hex2(fx + 1, cl.param);
					}
					text(vg, x, y, fx, fs, (cl.effect || cl.param) && !dimCol ? semi::theme::magenta() : semi::theme::grid(), al);
				}
			}
		}

		// channel meters
		const float my = h - pad - meterH;
		for (int c = 0; c < s.numCh; c++) {
			const float x = pad + numW + c * colW;
			const float mw = colW - 4.f;
			fillRect(vg, x, my, mw, meterH, semi::theme::cell());
			if (s.muted[c]) {
				fillRect(vg, x, my + meterH * 0.5f - 1.f, mw, 2.f, semi::theme::magenta());
				continue;
			}
			const float lv = clamp(s.level[c], 0.f, 1.f);
			if (lv > 0.01f)
				fillRect(vg, x, my, mw * lv, meterH, lv > 0.85f ? semi::theme::amber() : semi::theme::cyan());
		}
		text(vg, pad + 1.f, my + meterH * 0.5f, "VU", fs - 1.f, semi::theme::dim(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
	}
};

struct ModportWidget : ModuleWidget {
	ModportWidget(Modport* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Modport.svg")));

		auto* scr = semi::createScreenMm<TrackerScreen>(Vec(L::TRACKER.x, L::TRACKER.y), Vec(L::TRACKER.w, L::TRACKER.h));
		scr->module = module;
		addChild(scr);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		addInput(createInputCentered<semi::Jack>(at(L::CLK), module, Modport::CLK_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RST), module, Modport::RST_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::PLAY_IN), module, Modport::PLAY_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::NEXT), module, Modport::NEXT_INPUT));
		addParam(createParamCentered<semi::KnobM>(at(L::TEMPO), module, Modport::TEMPO_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::STEREO), module, Modport::STEREO_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::LEVEL), module, Modport::LEVEL_PARAM));
		addParam(createParamCentered<semi::PadCyan>(at(L::PLAY), module, Modport::PLAY_PARAM));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::PLAY_LED), module, Modport::PLAY_LIGHT));
		addParam(createParamCentered<semi::Button>(at(L::LOAD), module, Modport::LOAD_PARAM));

		const L::P mutes[kCh] = {L::MUTE1, L::MUTE2, L::MUTE3, L::MUTE4, L::MUTE5, L::MUTE6, L::MUTE7, L::MUTE8};
		const L::P leds[kCh] = {L::MUTE_LED1, L::MUTE_LED2, L::MUTE_LED3, L::MUTE_LED4, L::MUTE_LED5, L::MUTE_LED6, L::MUTE_LED7, L::MUTE_LED8};
		const L::P gates[kCh] = {L::GATE1, L::GATE2, L::GATE3, L::GATE4, L::GATE5, L::GATE6, L::GATE7, L::GATE8};
		const L::P vocts[kCh] = {L::VOCT1, L::VOCT2, L::VOCT3, L::VOCT4, L::VOCT5, L::VOCT6, L::VOCT7, L::VOCT8};
		for (int c = 0; c < kCh; c++) {
			addParam(createParamCentered<semi::Button>(at(mutes[c]), module, Modport::MUTE_PARAM + c));
			addChild(createLightCentered<semi::MagentaLedSmall>(at(leds[c]), module, Modport::MUTE_LIGHT + c));
			addOutput(createOutputCentered<semi::Jack>(at(gates[c]), module, Modport::GATE_OUTPUT + c));
			addOutput(createOutputCentered<semi::Jack>(at(vocts[c]), module, Modport::VOCT_OUTPUT + c));
		}
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_L), module, Modport::OUT_L_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_R), module, Modport::OUT_R_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Modport*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Load .mod…", "", [=]() { m->loadRequest = true; }));
		if (!m->loader.path().empty())
			menu->addChild(createMenuLabel(semi::fileName(m->loader.path())));
		menu->addChild(createBoolPtrMenuItem("Loop song", "", &m->loopSong));
		menu->addChild(createMenuLabel("Paula"));
		menu->addChild(createIndexSubmenuItem("Interpolation", kInterpNames,
		                                      [=]() { return (size_t) m->interp; }, [=](size_t i) { m->interp = (int) i; }));
		menu->addChild(createIndexSubmenuItem("Output filter", kModelNames,
		                                      [=]() { return (size_t) m->model; }, [=](size_t i) { m->model = (int) i; }));
		menu->addChild(createIndexSubmenuItem("LED filter", kLedNames,
		                                      [=]() { return (size_t) m->led; }, [=](size_t i) { m->led = (int) i; }));
		menu->addChild(createIndexSubmenuItem("Timing", kTimingNames,
		                                      [=]() { return (size_t) m->timing; }, [=](size_t i) { m->timing = (int) i; }));
		menu->addChild(createMenuItem("Unmute all", "", [=]() {
			for (int c = 0; c < kCh; c++)
				m->muted[c] = false;
		}));
	}
};

Model* modelModport = createModel<Modport, ModportWidget>("Modport");
