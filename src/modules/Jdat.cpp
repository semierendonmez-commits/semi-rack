// JDAT — Rack / MetaModule port of kdr_jdat.mxt (engine: engines/jdat, the plugin's JUCE-free
// JdatEngine with a FEED added; see the notes at the top of that file).
//
// The patch: a 120 ms metro writes one random -1 / 0 / +1 into each of three 512-sample
// buffers per tick (and clears them with 40 % chance). Three data voices read them through
// wave~ at 25 Hz (or play a sine / FM-noise instead), gated by the "mlts" lottery; two drones
// read buffers 1 and 3 at 100 Hz. The LCD fills with random digit rows.
//
// Rack additions:
//   FEED  any file (menu: Load file...) supplies the writes: value + index from the next three
//         bytes (ternary like the patch, or raw bytes as audio). The knob sets the share.
//   DENS  writes per buffer per tick (patch: 1).
//   CLK / RST / BUS POS replace the metro (RATE stays the fallback), RUN = the plugin's MIDI
//   GATE, CLR forces a clear, G1-G3 / CLR outs = the plugin's MIDI notes as gates.

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/FamilyBusCV.hpp"
#include "../common/Sample.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/JdatLayout.hpp"
#include "IkedarClock.hpp"
#include "jdat/JdatEngine.h"

namespace L = semi::layout::jdat;
namespace jd = jdat_mod;
using semi::Screen;

namespace {

constexpr int kBlock = 32;
constexpr float kVoltsPerUnit = 5.f;
constexpr size_t kMaxFileBytes = 4u << 20; // 4 MB

const std::vector<std::string> kSourceNames = {"none", "sine", "noise", "data"};
const std::vector<std::string> kDivNames = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/64"};
const double kDivBeats[] = {1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 0.0625};
const int kLoopTicks[] = {0, 16, 32, 64, 128, 256};
const double kGateMs[3] = {40.1, 70.2, 40.2}; // attack + hold of each data voice's line~

struct FileData {
	std::vector<uint8_t> bytes;
	std::string name;
};

bool buildFile(const std::string& path, FileData& f) {
	if (!semi::readFileBytes(path, f.bytes, kMaxFileBytes) || f.bytes.size() < 3)
		return false;
	f.name = semi::fileName(path);
	return true;
}

struct Snapshot {
	int8_t buf[3][jd::kBufLen]{};
	uint32_t lcdSeed = 0, ticks = 0;
	uint8_t rows = 18;
	bool clear = false;
	uint8_t grey[3]{};
	float gate[3]{};
	float phase = 0.f;
	int loopPos = -1, loopLen = 0;
	float rateMs = 120.f;
	int src[4]{3, 3, 3, 3};
	bool hold = false, running = true;
	int source = 0;
	bool file = false, feeding = false;
	float filePos = 0.f;
	char fileName[28]{};
	uint32_t fileKb = 0;
};

} // namespace

struct Jdat : Module {
	enum ParamId {
		RATE_PARAM, CLEAR_PARAM, JUMP_PARAM, DENS_PARAM, FEED_PARAM,
		LVL1_PARAM, LVL2_PARAM, LVL3_PARAM, DRONE_PARAM, MASTER_PARAM, LOOP_PARAM,
		SRC_PARAM, HOLD_PARAM,
		PARAMS_LEN
	};
	enum InputId { CLK_INPUT, RST_INPUT, RUN_INPUT, CLR_INPUT, RATE_INPUT, BUS_INPUT, INPUTS_LEN };
	enum OutputId { OUT_L_OUTPUT, OUT_R_OUTPUT, G1_OUTPUT, G2_OUTPUT, G3_OUTPUT, CLR_OUTPUT, OUTPUTS_LEN };
	enum LightId { SRC_DATA_LIGHT, SRC_SINE_LIGHT, SRC_NOISE_LIGHT, HOLD_LIGHT, LIGHTS_LEN };

	jd::Engine engine;

	// settings (menu, saved)
	int src[4] = {jd::srcData, jd::srcData, jd::srcData, jd::srcData};
	bool link = true;
	bool hold = false;
	bool clip = true;
	int interp = jd::interpLinear;
	int divIndex = 3;
	int feedMode = 1; // 1 ternary, 2 raw bytes

	// file
	semi::Slot<FileData> fileSlot;
	semi::Loader<FileData> loader;
	std::atomic<bool> reqUnload{false};
	bool fileActive = false;

	semi::ikclock::StepClock clock;
	dsp::BooleanTrigger srcBtn, holdBtn;
	dsp::SchmittTrigger runIn, clrIn, busEv;
	bool runWasHigh = false, restartNext = false, forceClear = false;
	int64_t extIndex = 0;

	struct Ev {
		int offset;
		bool restart;
	};
	Ev events[kBlock];
	int numEv = 0;
	float outBuf[2][kBlock]{};
	int gateAt[4] = {-1, -1, -1, -1}; // offsets in the output block
	int gateLeft[4]{};
	int pos = 0;
	double sr = 44100.0;

	jd::TickEvent lastEv;
	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;

	~Jdat() override {
		// background jobs first (and wait for a run in flight): they write into this module
		loader.stopAndWait();
	}

	Jdat() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		// 10 * 100^v ms: 10 ms .. 1 s, the patch's 120 ms at the default
		configParam(RATE_PARAM, 0.f, 1.f, std::log(12.f) / std::log(100.f), "Metro", " ms", 100.f, 10.f);
		configParam(CLEAR_PARAM, 0.f, 100.f, 40.f, "Clear chance", " %");
		configParam(JUMP_PARAM, 0.f, 100.f, 30.f, "Noise jump chance", " %");
		configParam(DENS_PARAM, 1.f, 64.f, 1.f, "Writes per buffer per tick");
		getParamQuantity(DENS_PARAM)->snapEnabled = true;
		configParam(FEED_PARAM, 0.f, 100.f, 100.f, "Feed (share of the writes taken from the loaded file)", " %");
		configParam(LVL1_PARAM, -60.f, 6.f, 0.f, "Data 1 level", " dB");
		configParam(LVL2_PARAM, -60.f, 6.f, 0.f, "Data 2 level", " dB");
		configParam(LVL3_PARAM, -60.f, 6.f, 0.f, "Data 3 level", " dB");
		configParam(DRONE_PARAM, -60.f, 6.f, 0.f, "Drone level", " dB");
		configParam(MASTER_PARAM, -60.f, 6.f, 0.f, "Master", " dB");
		configSwitch(LOOP_PARAM, 0.f, 5.f, 0.f, "Pattern lock", {"off", "16 ticks", "32 ticks", "64 ticks", "128 ticks", "256 ticks"});
		configButton(SRC_PARAM, "Data sources (DATA / SINE / NOISE / none)");
		configButton(HOLD_PARAM, "Hold data");
		configInput(CLK_INPUT, "Clock (1/16 per pulse by default; replaces the metro)");
		configInput(RST_INPUT, "Reset (restarts the metro, rewinds the file)");
		configInput(RUN_INPUT, "Run gate (runs while high, restarts on the rising edge)");
		configInput(CLR_INPUT, "Force clear (on the next tick)");
		configInput(RATE_INPUT, "Rate CV (1 V/oct faster)");
		configInput(BUS_INPUT, "FamilyBus (POS clocks when CLK is unpatched, EVENT clears)");
		configOutput(OUT_L_OUTPUT, "Left (data 1 + 2 + drone 1)");
		configOutput(OUT_R_OUTPUT, "Right (data 2 + 3 + drone 2)");
		configOutput(G1_OUTPUT, "Data 1 gate");
		configOutput(G2_OUTPUT, "Data 2 gate");
		configOutput(G3_OUTPUT, "Data 3 gate");
		configOutput(CLR_OUTPUT, "Clear trigger");
		loader.attach(this, &fileSlot, buildFile);
		engine.seed(random::u32());
		engine.prepare(sr);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		sr = e.sampleRate;
		engine.prepare(sr);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		for (int& s : src)
			s = jd::srcData;
		link = true;
		hold = false;
		clip = true;
		interp = jd::interpLinear;
		divIndex = 3;
		feedMode = 1;
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_t* s = json_array();
		for (int x : src)
			json_array_append_new(s, json_integer(x));
		json_object_set_new(root, "src", s);
		json_object_set_new(root, "link", json_boolean(link));
		json_object_set_new(root, "hold", json_boolean(hold));
		json_object_set_new(root, "clip", json_boolean(clip));
		json_object_set_new(root, "interp", json_integer(interp));
		json_object_set_new(root, "div", json_integer(divIndex));
		json_object_set_new(root, "ppqn", json_integer(clock.ppqnIndex));
		json_object_set_new(root, "feedMode", json_integer(feedMode));
		if (!loader.path().empty() && fileActive)
			json_object_set_new(root, "file", json_string(loader.path().c_str()));
		return root;
	}

	void dataFromJson(json_t* root) override {
		auto geti = [&](const char* k, int& dst, int lo, int hi) {
			if (json_t* j = json_object_get(root, k))
				dst = clamp((int) json_integer_value(j), lo, hi);
		};
		if (json_t* s = json_object_get(root, "src"))
			for (int i = 0; i < 4 && i < (int) json_array_size(s); i++)
				src[i] = clamp((int) json_integer_value(json_array_get(s, (size_t) i)), 0, 3);
		if (json_t* j = json_object_get(root, "link"))
			link = json_boolean_value(j);
		if (json_t* j = json_object_get(root, "hold"))
			hold = json_boolean_value(j);
		if (json_t* j = json_object_get(root, "clip"))
			clip = json_boolean_value(j);
		geti("interp", interp, 0, 2);
		geti("div", divIndex, 0, 6);
		geti("ppqn", clock.ppqnIndex, 0, (int) semi::ikclock::kPpqnNames.size() - 1);
		geti("feedMode", feedMode, 1, 2);
		if (json_t* f = json_object_get(root, "file"))
			if (json_is_string(f))
				loader.request(json_string_value(f));
	}

	void loadFile(const std::string& path) {
		reqUnload = false;
		loader.request(path);
	}

	static float dbToGain(float db) { return db <= -59.9f ? 0.f : std::pow(10.f, db / 20.f); }

	jd::Params readParams(bool runInternal) {
		jd::Params p;
		p.run = runInternal;
		p.sync = false;
		const float v = params[RATE_PARAM].getValue();
		p.rateMs = 10.0 * std::pow(100.0, (double) v) / std::pow(2.0, (double) clamp(inputs[RATE_INPUT].getVoltage(), -5.f, 5.f));
		p.rateMs = std::max(5.0, p.rateMs);
		for (int i = 0; i < 4; i++)
			p.src[(size_t) i] = src[i];
		if (link)
			p.src[2] = src[0];
		p.level[0] = dbToGain(params[LVL1_PARAM].getValue());
		p.level[1] = dbToGain(params[LVL2_PARAM].getValue());
		p.level[2] = dbToGain(params[LVL3_PARAM].getValue());
		p.level[3] = dbToGain(params[DRONE_PARAM].getValue());
		p.master = dbToGain(params[MASTER_PARAM].getValue());
		p.clip = clip;
		p.clearProb = (int) std::lround(params[CLEAR_PARAM].getValue());
		p.jumpProb = (int) std::lround(params[JUMP_PARAM].getValue());
		p.hold = hold;
		p.interp = interp;
		p.loopTicks = kLoopTicks[clamp((int) params[LOOP_PARAM].getValue(), 0, 5)];
		p.writes = clamp((int) params[DENS_PARAM].getValue(), 1, 64);
		const float feed = params[FEED_PARAM].getValue() * 0.01f;
		p.feedMode = (fileActive && feed > 0.f) ? feedMode : 0;
		p.feedAmount = feed;
		return p;
	}

	void runBlock(bool external, bool runOk) {
		jd::Params p = readParams(!external && runOk);
		// a forced clear (CLR in, BUS EVENT) rides on the next metro tick
		const bool forceInternal = forceClear && !external;
		if (forceInternal)
			p.clearProb = 101;
		const uint32_t ticksBefore = engine.tickCount.load(std::memory_order_relaxed);
		jd::Transport t;
		for (int c = 0; c < 4; c++)
			gateAt[c] = -1;

		jd::GateSink sink;
		int done = 0;
		auto render = [&](int end) {
			if (end <= done)
				return;
			sink.count = 0;
			engine.process(outBuf[0] + done, outBuf[1] + done, end - done, p, t, &sink);
			for (int k = 0; k < sink.count; k++)
				gateAt[sink.items[(size_t) k].column] = done + sink.items[(size_t) k].offset;
			done = end;
		};
		for (int e = 0; e < numEv; e++) {
			render(events[e].offset);
			if (events[e].restart) {
				extIndex = 0;
				engine.rewindFeed();
			}
			jd::Params q = p;
			if (forceClear) {
				q.clearProb = 101;
				forceClear = false;
			}
			sink.count = 0;
			engine.externalTick(q, extIndex++, &sink);
			for (int k = 0; k < sink.count; k++)
				gateAt[sink.items[(size_t) k].column] = done;
		}
		render(kBlock);
		numEv = 0;
		if (forceInternal && engine.tickCount.load(std::memory_order_relaxed) != ticksBefore)
			forceClear = false;

		jd::TickEvent ev;
		while (engine.events.pop(ev))
			lastEv = ev;
	}

	void process(const ProcessArgs& args) override {
		if (fileSlot.poll()) {
			engine.setFeed(fileSlot.active->bytes.data(), fileSlot.active->bytes.size());
			fileActive = true;
		}
		if (reqUnload.exchange(false)) {
			engine.setFeed(nullptr, 0);
			fileActive = false;
		}

		if (srcBtn.process(params[SRC_PARAM].getValue() > 0.f)) {
			// DATA -> SINE -> NOISE -> none -> DATA, for the three data voices
			static const int next[4] = {jd::srcData, jd::srcNoise, jd::srcNone, jd::srcSine};
			const int n = next[clamp(src[0], 0, 3)];
			src[0] = src[1] = src[2] = n;
		}
		if (holdBtn.process(params[HOLD_PARAM].getValue() > 0.f))
			hold = !hold;
		if (clrIn.process(inputs[CLR_INPUT].getVoltage(), 0.1f, 1.f))
			forceClear = true;
		if (inputs[BUS_INPUT].getChannels() > semi::bus::kEvent
		    && busEv.process(inputs[BUS_INPUT].getVoltage(semi::bus::kEvent), 0.1f, 1.f))
			forceClear = true;

		bool runOk = true;
		if (inputs[RUN_INPUT].isConnected()) {
			const bool high = runIn.process(inputs[RUN_INPUT].getVoltage(), 0.1f, 1.f) || runIn.isHigh();
			if (high && !runWasHigh) {
				restartNext = true;
				engine.restartClock();
			}
			runWasHigh = high;
			runOk = high;
		}
		else
			runWasHigh = false;

		int64_t idx = 0;
		if (clock.process(args.sampleTime, inputs[CLK_INPUT], inputs[RST_INPUT], &inputs[BUS_INPUT],
		                  kDivBeats[divIndex], 0.0, idx)) {
			const bool restart = clock.takeReset() || restartNext;
			restartNext = false;
			if (runOk && numEv < kBlock)
				events[numEv++] = {pos, restart};
		}
		else if (!clock.external() && clock.takeReset()) {
			engine.restartClock();
			engine.rewindFeed();
		}
		const bool external = clock.external();

		outputs[OUT_L_OUTPUT].setVoltage(outBuf[0][pos] * kVoltsPerUnit);
		outputs[OUT_R_OUTPUT].setVoltage(outBuf[1][pos] * kVoltsPerUnit);
		for (int c = 0; c < 4; c++) {
			if (gateAt[c] == pos) {
				gateLeft[c] = (int) (sr * (c < 3 ? kGateMs[c] : 1.0) * 0.001);
				gateAt[c] = -1;
			}
			const bool on = gateLeft[c] > 0;
			if (on)
				gateLeft[c]--;
			outputs[G1_OUTPUT + c].setVoltage(on ? 10.f : 0.f);
		}
		if (++pos >= kBlock) {
			pos = 0;
			runBlock(external, runOk);
		}

		if (ui.tick(args.sampleTime)) {
			int s0 = src[0];
			const bool same = src[0] == src[1] && (link || src[2] == src[0]);
			lights[SRC_DATA_LIGHT].setBrightness(same && s0 == jd::srcData ? 1.f : (s0 == jd::srcData ? 0.3f : 0.f));
			lights[SRC_SINE_LIGHT].setBrightness(same && s0 == jd::srcSine ? 1.f : (s0 == jd::srcSine ? 0.3f : 0.f));
			lights[SRC_NOISE_LIGHT].setBrightness(same && s0 == jd::srcNoise ? 1.f : (s0 == jd::srcNoise ? 0.3f : 0.f));
			lights[HOLD_LIGHT].setBrightness(hold ? 1.f : 0.f);

			Snapshot& s = snap.writeSlot();
			for (int b = 0; b < 3; b++)
				for (int k = 0; k < jd::kBufLen; k++)
					s.buf[b][k] = engine.bufView[(size_t) b][(size_t) k].load(std::memory_order_relaxed);
			s.lcdSeed = lastEv.lcdSeed;
			s.ticks = lastEv.index;
			s.rows = lastEv.rows;
			s.clear = lastEv.clear;
			for (int c = 0; c < 3; c++) {
				s.grey[c] = lastEv.grey[(size_t) c];
				s.gate[c] = engine.gateView[(size_t) c].load(std::memory_order_relaxed);
			}
			s.phase = engine.phase25.load(std::memory_order_relaxed);
			const int loopLen = kLoopTicks[clamp((int) params[LOOP_PARAM].getValue(), 0, 5)];
			s.loopLen = loopLen;
			s.loopPos = loopLen > 0 ? lastEv.loopPos : -1;
			const jd::Params p = readParams(true);
			s.rateMs = external ? (float) (clock.stepSec * 1000.0) : (float) p.rateMs;
			for (int i = 0; i < 4; i++)
				s.src[i] = p.src[(size_t) i];
			s.hold = hold;
			s.running = external ? clock.stepSec > 0.0 && runOk : runOk;
			s.source = clock.source == semi::ikclock::StepClock::CLK ? 1 : clock.source == semi::ikclock::StepClock::BUS ? 2 : 0;
			s.file = fileActive;
			s.feeding = p.feedMode != 0;
			if (fileActive && fileSlot.active) {
				const size_t n = fileSlot.active->bytes.size();
				s.filePos = n ? (float) engine.feedPosition() / (float) n : 0.f;
				std::snprintf(s.fileName, sizeof s.fileName, "%s", fileSlot.active->name.c_str());
				s.fileKb = (uint32_t) ((n + 1023) / 1024);
			}
			else
				s.fileName[0] = 0;
			snap.publish();
		}
	}
};

// ---- screens ------------------------------------------------------------------

namespace {

Snapshot demoSnapshot() {
	Snapshot d;
	uint32_t x = 0x6a646174u;
	for (int b = 0; b < 3; b++)
		for (int k = 0; k < jd::kBufLen; k++) {
			x ^= x << 13;
			x ^= x >> 17;
			x ^= x << 5;
			d.buf[b][k] = (x % 7u) == 0 ? ((x >> 8) & 1u ? 127 : -127) : 0;
		}
	d.lcdSeed = 0x1234567u;
	d.ticks = 4217;
	d.rows = 19;
	d.grey[0] = 64;
	d.grey[1] = 255;
	d.grey[2] = 8;
	d.gate[0] = 1.f;
	d.gate[2] = 0.6f;
	d.phase = 0.37f;
	std::snprintf(d.fileName, sizeof d.fileName, "%s", "");
	return d;
}

inline uint32_t lcdRand(uint32_t& s) {
	s ^= s << 13;
	s ^= s >> 17;
	s ^= s << 5;
	return s;
}

} // namespace

// The patch's LCD: rows of random digits redrawn on every metro tick (stable in between), a
// cleared screen starts dim; the mlts lottery as three heat columns on the right.
struct LcdScreen : Screen {
	Jdat* module = nullptr;
	Snapshot demo = demoSnapshot();

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		fillRect(vg, 0, 0, w, h, semi::theme::screen());
		const float pad = 3.f;
		const float heatW = 7.f, heatGap = 2.f;
		const float fx1 = w - pad - 3 * heatW - 2 * heatGap - 3.f;

		if (!ensureFont())
			return;
		const float fs = semi::platform::kMetaModule ? 9.f : 8.f;
		nvgFontFaceId(vg, font->handle);
		nvgFontSize(vg, fs);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
		float bounds[4];
		nvgTextBounds(vg, 0, 0, "0", nullptr, bounds);
		const float cw = std::max(3.f, bounds[2] - bounds[0]);
		const int cols = std::max(1, (int) ((fx1 - pad) / cw));
		const float lh = fs * 1.02f;
		const int maxRows = std::max(1, (int) ((h - 2 * pad) / lh));
		// random(18,21) rows of the patch's 20 -> the share of the screen that fills
		const int rows = std::min(maxRows, (int) std::lround(maxRows * (s.rows + 1) / 21.f));
		uint32_t r = s.lcdSeed ? s.lcdSeed : 1u;
		char line[96];
		for (int row = 0; row < rows; row++) {
			const int n = std::min(cols, (int) sizeof line - 1);
			for (int i = 0; i < n; i++)
				line[i] = (char) ('0' + lcdRand(r) % 10u);
			line[n] = 0;
			// alternate rows bright / dim like the patch's overdraw
			const bool bright = (lcdRand(r) & 3u) == 0;
			nvgFillColor(vg, s.clear ? semi::theme::dim() : (bright ? nvgRGB(0xe6, 0xee, 0xec) : semi::theme::cyan()));
			nvgText(vg, pad, pad + row * lh, line, nullptr);
		}

		// mlts heat: grey = min(2^r, 255); gate when <= 255
		for (int c = 0; c < 3; c++) {
			const float x = fx1 + 3.f + c * (heatW + heatGap);
			fillRect(vg, x, pad, heatW, h - 2 * pad, semi::theme::cell());
			const float g = s.grey[c] / 255.f;
			const float hh = (h - 2 * pad) * g;
			const NVGcolor col = s.gate[c] > 0.01f ? semi::theme::magenta() : semi::theme::cyanDim();
			fillRect(vg, x, h - pad - hh, heatW, hh, col);
		}
	}
};

// The three 512-sample buffers as barcodes (one bar per 4 samples: + up cyan, - down magenta),
// the 25 Hz read position, and a status line (clock, tick, lock, file).
struct BufScreen : Screen {
	Jdat* module = nullptr;
	Snapshot demo = demoSnapshot();

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demo;
		const float w = box.size.x, h = box.size.y;
		fillRect(vg, 0, 0, w, h, semi::theme::screen());
		const float pad = 3.f, fs = 9.f;
		char buf[64];
		static const char* srcName[] = {"METRO", "CLK", "BUS"};
		if (s.loopLen > 0)
			std::snprintf(buf, sizeof buf, "%s %3.0fms  L%02d/%d", srcName[s.source], s.rateMs, std::max(0, s.loopPos) + 1, s.loopLen);
		else
			std::snprintf(buf, sizeof buf, "%s %3.0fms  T%06u", srcName[s.source], s.rateMs, (unsigned) (s.ticks % 1000000u));
		text(vg, pad, pad - 1.f, buf, fs, s.running ? semi::theme::cyan() : semi::theme::dim());
		if (s.file) {
			std::snprintf(buf, sizeof buf, "%.12s %02d%%", s.fileName, (int) (s.filePos * 100.f));
			text(vg, w - pad, pad - 1.f, buf, fs, s.feeding ? semi::theme::magenta() : semi::theme::dim(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		}
		else if (s.hold)
			text(vg, w - pad, pad - 1.f, "HOLD", fs, semi::theme::magenta(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);

		const float y0 = pad + fs + 1.f, y1 = h - pad;
		const float laneH = (y1 - y0) / 3.f;
		constexpr int kBars = 128;
		const float bw = (w - 2 * pad) / kBars;
		for (int b = 0; b < 3; b++) {
			const float mid = y0 + laneH * (b + 0.5f);
			const float amp = laneH * 0.45f;
			nvgBeginPath(vg);
			for (int i = 0; i < kBars; i++) {
				int sum = 0;
				for (int k = 0; k < 4; k++)
					sum += s.buf[b][i * 4 + k];
				if (sum > 0)
					nvgRect(vg, pad + bw * i, mid - amp * std::min(1.f, sum / 254.f), std::max(bw - 0.4f, 1.f), amp * std::min(1.f, sum / 254.f));
			}
			nvgFillColor(vg, semi::theme::cyan());
			nvgFill(vg);
			nvgBeginPath(vg);
			for (int i = 0; i < kBars; i++) {
				int sum = 0;
				for (int k = 0; k < 4; k++)
					sum += s.buf[b][i * 4 + k];
				if (sum < 0)
					nvgRect(vg, pad + bw * i, mid, std::max(bw - 0.4f, 1.f), amp * std::min(1.f, -sum / 254.f));
			}
			nvgFillColor(vg, semi::theme::magenta());
			nvgFill(vg);
			fillRect(vg, pad, mid - 0.5f, w - 2 * pad, 1.f, semi::theme::grid());
		}
		// wave~ read position (phasor~ 25 Hz), quantised to the bar grid
		const int bar = clamp((int) (s.phase * kBars), 0, kBars - 1);
		fillRect(vg, pad + bw * bar, y0, std::max(bw, 1.5f), y1 - y0, semi::theme::amber());
	}
};

struct JdatWidget : ModuleWidget {
	JdatWidget(Jdat* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Jdat.svg")));

		auto* lcd = semi::createScreenMm<LcdScreen>(Vec(L::LCD.x, L::LCD.y), Vec(L::LCD.w, L::LCD.h));
		lcd->module = module;
		addChild(lcd);
		auto* bs = semi::createScreenMm<BufScreen>(Vec(L::BUF.x, L::BUF.y), Vec(L::BUF.w, L::BUF.h));
		bs->module = module;
		addChild(bs);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::SRC_DATA), module, Jdat::SRC_DATA_LIGHT));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::SRC_SINE), module, Jdat::SRC_SINE_LIGHT));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::SRC_NOISE), module, Jdat::SRC_NOISE_LIGHT));
		addParam(createParamCentered<semi::Button>(at(L::SRC), module, Jdat::SRC_PARAM));
		addChild(createLightCentered<semi::MagentaLedSmall>(at(L::HOLD_LED), module, Jdat::HOLD_LIGHT));
		addParam(createParamCentered<semi::Button>(at(L::HOLD), module, Jdat::HOLD_PARAM));

		addParam(createParamCentered<semi::KnobM>(at(L::RATE), module, Jdat::RATE_PARAM));
		addParam(createParamCentered<semi::KnobMHot>(at(L::CLEAR), module, Jdat::CLEAR_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::JUMP), module, Jdat::JUMP_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::DENS), module, Jdat::DENS_PARAM));
		addParam(createParamCentered<semi::KnobMHot>(at(L::FEED), module, Jdat::FEED_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::LVL1), module, Jdat::LVL1_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::LVL2), module, Jdat::LVL2_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::LVL3), module, Jdat::LVL3_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::DRONE), module, Jdat::DRONE_PARAM));
		addParam(createParamCentered<semi::KnobS>(at(L::MASTER), module, Jdat::MASTER_PARAM));
		auto* loop = createParamCentered<semi::KnobS>(at(L::LOOP), module, Jdat::LOOP_PARAM);
		loop->snap = true;
		addParam(loop);

		addInput(createInputCentered<semi::Jack>(at(L::CLK), module, Jdat::CLK_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RST), module, Jdat::RST_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RUN), module, Jdat::RUN_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::CLR_IN), module, Jdat::CLR_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RATE_CV), module, Jdat::RATE_INPUT));
		addInput(createInputCentered<semi::BusJack>(at(L::BUS), module, Jdat::BUS_INPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_L), module, Jdat::OUT_L_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_R), module, Jdat::OUT_R_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::G1), module, Jdat::G1_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::G2), module, Jdat::G2_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::G3), module, Jdat::G3_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::CLR_OUT), module, Jdat::CLR_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Jdat*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Data file (FEED)"));
		menu->addChild(createMenuItem("Load file...", "", [=]() {
			semi::openFileDialog("Any file:*", semi::dirName(m->loader.path()), [=](const std::string& path) { m->loadFile(path); });
		}));
		if (m->fileActive)
			menu->addChild(createMenuItem("Unload " + semi::fileName(m->loader.path()), "", [=]() { m->reqUnload = true; }));
		if (m->loader.failed)
			menu->addChild(createMenuLabel("(last file could not be read)"));
		menu->addChild(createIndexSubmenuItem("File values", {"Ternary -1 / 0 / +1 (patch)", "Raw bytes"},
		                                      [=]() { return (size_t) (m->feedMode - 1); },
		                                      [=](size_t i) { m->feedMode = (int) i + 1; }));

		menu->addChild(createMenuLabel("patcher window"));
		static const char* names[] = {"Data 1 source", "Data 2 source", "Data 3 source", "Drone source"};
		for (int i = 0; i < 4; i++)
			menu->addChild(createIndexSubmenuItem(names[i], kSourceNames,
			                                      [=]() { return (size_t) m->src[i]; },
			                                      [=](size_t k) { m->src[i] = (int) k; }));
		menu->addChild(createBoolPtrMenuItem("Link 1 > 3", "", &m->link));
		menu->addChild(createBoolPtrMenuItem("Hold data", "", &m->hold));
		menu->addChild(createIndexSubmenuItem("wave~ interpolation", {"none", "linear", "cubic"},
		                                      [=]() { return (size_t) m->interp; },
		                                      [=](size_t i) { m->interp = (int) i; }));
		menu->addChild(createBoolPtrMenuItem("DAC clip", "", &m->clip));

		menu->addChild(createMenuLabel("Clock"));
		menu->addChild(createIndexSubmenuItem("CLK input", semi::ikclock::kPpqnNames,
		                                      [=]() { return (size_t) m->clock.ppqnIndex; },
		                                      [=](size_t i) { m->clock.ppqnIndex = (int) i; }));
		menu->addChild(createIndexSubmenuItem("Tick (CLK / BUS)", kDivNames,
		                                      [=]() { return (size_t) m->divIndex; },
		                                      [=](size_t i) { m->divIndex = (int) i; }));
	}
};

Model* modelJdat = createModel<Jdat, JdatWidget>("Jdat");
