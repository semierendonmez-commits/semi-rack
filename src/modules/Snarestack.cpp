// SNARESTACK — Rack / MetaModule port of SNARESTACK (engine: engines/snarestack, vendored from
// semierendonmez-commits/snarestack Source/engine; namespace gtbs -> ss::gtbs, Player::uses() added).
//
// Four snare layers made into one hit: every attack lined up, each layer slid by a fraction of a
// millisecond and its polarity picked where the layers really have something in common, then
// split into CRACK / BODY / TAIL and re-weighted per hit from the balance point (BAL X / BAL Y).
//
// Layers: each slot plays a synthesised part of the plugin (body 185 ... brush; nothing to load,
// the default stack uses them) or a WAV file, as a one-shot or as a break whose loudest snare is
// cut out by the plugin's break analysis. Files are picked with semi::openFileDialog and read,
// analysed and stacked off the audio thread (Rack: a worker thread; MetaModule: an AsyncThread):
// the worker builds a whole new stack (ss::build: alignment, carve, split) whenever a layer or a
// stack setting changes and hands it over lock-free; the audio thread swaps it in and gives the
// old one back to the worker once no voice plays it any more. process() never allocates or reads.
//
// TRIG + VEL play the stack (MIDI notes in the plugin), V/OCT and PITCH tune it (continuous).
// The plugin's built-in break snares (FLAC in its binary) are not shipped: presets that used one
// keep whatever layer 1 holds (load your own break there).
//
// Structure follows NEXUSCLIP: block adapter, TripleBuffer snapshot via UiDivider, Screen
// subclasses with demo snapshots, layout header from tools/panels/snarestack.py.

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/Sample.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/SnarestackLayout.hpp"
#include "snarestack/Library.h"
#include "snarestack/Player.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#ifndef METAMODULE
#include <thread>
#endif

namespace L = semi::layout::snarestack;
using semi::Screen;

namespace {

constexpr int kBlock = 32;
constexpr float kVoltsPerUnit = 5.f;
constexpr int kLayers = ss::kLayers;
constexpr int kPts = 48;

#ifdef METAMODULE
constexpr int kDebounceMs = 600; // a build takes seconds on the A7: wait for the knob to settle
#else
constexpr int kDebounceMs = 150;
#endif
constexpr size_t kOneShotFrames = 48000 * 3;  // one-shots: 3 s is plenty (the stack keeps 2.5 s)
constexpr size_t kBreakFrames = 48000 * 12;   // breaks to cut a snare from: 12 s

int64_t nowMs() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

const std::vector<std::string> kAlignNames{"OFF", "ONSET", "ONSET+PHASE"};
const std::vector<std::string> kRefNames{"AUTO", "LAYER 1", "LAYER 2", "LAYER 3", "LAYER 4"};
const std::vector<std::string> kPolNames{"AUTO", "+", "-"};

NVGcolor layerColor(int i) {
	static const NVGcolor c[4] = {nvgRGB(0x4f, 0xe3, 0xd6), nvgRGB(0xe8, 0xb0, 0x4a), nvgRGB(0xff, 0x4f, 0x9a), nvgRGB(0x7f, 0xa8, 0xff)};
	return c[i & 3];
}

// Menu-only per-layer values (the panel has the gains), in the plugin's units.
struct LayerSet {
	bool on = true;
	float tune = 0.f, decay = 100.f, hp = 20.f, lp = 20000.f, pan = 0.f, nudge = 0.f;
	int pol = ss::PolAuto;
};

// What the screens show of a built stack (written by the worker only).
struct StackView {
	bool valid = false;
	char name[kLayers][24]{};
	bool active[kLayers]{};
	float shiftMs[kLayers]{}, corrRaw[kLayers]{}, corr[kLayers]{};
	bool inv[kLayers]{};
	float raw[kLayers][kPts]{}, al[kLayers][kPts]{};
	float cancelRaw[ss::kBands]{}, cancelAl[ss::kBands]{};
	float share[ss::NumComps]{};
	float normDb = 0.f;
	int ref = 0;
	char error[40]{};
};

// What the screens show of the playing side (written by the audio thread).
struct Snapshot {
	float bx = 0.5f, by = 2.f / 3.f;
	int align = ss::AlignPhase;
	float gainDb[ss::NumComps]{};
	float meter = 0.f;
	int hits = 0;
	bool building = false, ready = false;
};

struct PresetDef {
	const char* name;
	const char* src[kLayers]; // "synth:N", or nullptr = keep what the slot holds (the plugin's break snare)
	std::vector<std::pair<const char*, float>> values;
};
const std::vector<PresetDef>& presets() {
	static const std::vector<PresetDef> p{
	    {"808 Stack (default)", {"synth:7", "synth:0", "synth:2", "synth:6"}, {{"gain2", -4}, {"gain3", -6}, {"gain4", -10}}},
	    {"Amen Stack (L1: your break)", {nullptr, "synth:0", "synth:2", "synth:6"}, {{"gain2", -4}, {"gain3", -6}, {"gain4", -10}}},
	    {"Think Crack (L1: your break)", {nullptr, "synth:3", "synth:5", "synth:6"}, {{"gain2", -6}, {"gain3", -9}, {"on4", 0}, {"by", 0.3f}}},
	    {"Jungle Body (L1: your break)", {nullptr, "synth:1", "synth:8", "synth:4"},
	     {{"gain2", -3}, {"gain3", -8}, {"gain4", -9}, {"bx", 0.3f}, {"by", 0.85f}}},
	    {"Funky 808 (L1: your break)", {nullptr, "synth:7", "synth:2", "synth:6"}, {{"gain2", -5}, {"gain3", -8}, {"gain4", -12}, {"tune2", -2}}},
	    {"Sesame Brush (L1: your break)", {nullptr, "synth:9", "synth:0", "synth:6"},
	     {{"gain2", -4}, {"gain3", -8}, {"gain4", -6}, {"bx", 0.75f}, {"by", 0.85f}}},
	    {"Carved Clap", {"synth:4", "synth:0", "synth:2", "synth:6"}, {{"gain2", -2}, {"gain3", -5}, {"gain4", -9}, {"carve", 70}}},
	    {"Amen Stack, Not Aligned", {nullptr, "synth:0", "synth:2", "synth:6"}, {{"gain2", -4}, {"gain3", -6}, {"gain4", -10}, {"align", 0}}},
	    {"Body Pile-Up (L1: your break)", {nullptr, "synth:0", "synth:7", "synth:1"},
	     {{"gain2", -3}, {"gain3", -4}, {"gain4", -5}, {"tune4", 1}, {"bx", 0.35f}, {"by", 0.8f}}},
	    {"Tight Two-Step (L1: your break)", {nullptr, "synth:1", "synth:3", "synth:6"},
	     {{"gain2", -6}, {"gain3", -6}, {"on4", 0}, {"decay", 45}, {"vary", 30}}},
	};
	return p;
}

} // namespace

struct Snarestack : Module {
	enum ParamId {
		G1_PARAM, G2_PARAM, G3_PARAM, G4_PARAM, CARVE_PARAM, DEPTH_PARAM,
		PITCH_PARAM, DECAY_PARAM, BX_PARAM, BY_PARAM, VARY_PARAM, LEVEL_PARAM,
		ALIGN_PARAM, HIT_PARAM,
		PARAMS_LEN
	};
	enum InputId { TRIG_INPUT, VEL_INPUT, VOCT_INPUT, BX_INPUT, BY_INPUT, DECAY_INPUT, INPUTS_LEN };
	enum OutputId { OUT_L_OUTPUT, OUT_R_OUTPUT, OUTPUTS_LEN };
	enum LightId { AL_OFF_LIGHT, AL_ONSET_LIGHT, AL_PHASE_LIGHT, LIGHTS_LEN };

	// ---- settings (menus; the worker reads them, benign races on plain values as elsewhere)
	std::array<LayerSet, kLayers> lay;
	std::atomic<int> align{ss::AlignPhase};
	int ref = 0;
	float xfreq = 2500.f, crackMs = 12.f, bodyMs = 90.f;
	bool normalize = true;
	float velSens = 80.f, velCrack = 50.f;
	bool choke = false;
	std::atomic<int> presetIdx{0};

	// ---- layer sources: "synth:N", "file:<path>", "break:<path>" or "" (guarded by specLock)
	std::array<std::string, kLayers> spec{"synth:7", "synth:0", "synth:2", "synth:6"};
	std::atomic_flag specLock = ATOMIC_FLAG_INIT;
	std::atomic<int> specVersion{1};
	std::string lastDir;

	std::array<std::string, kLayers> specCopy() {
		while (specLock.test_and_set(std::memory_order_acquire)) {
		}
		auto c = spec;
		specLock.clear(std::memory_order_release);
		return c;
	}
	void setSpec(int i, const std::string& s) {
		while (specLock.test_and_set(std::memory_order_acquire)) {
		}
		spec[(size_t) i] = s;
		specLock.clear(std::memory_order_release);
		specVersion++;
	}

	// ---- worker side (Rack: std::thread; MetaModule: AsyncThread)
	std::array<std::string, kLayers> loadedSpec;
	std::array<std::shared_ptr<const ss::Source>, kLayers> loadedSrc;
	std::array<float, 64> settling{}, built{};
	bool builtOnce = false;
	int64_t settlingSince = 0;
	std::atomic<bool> building{false};
	semi::TripleBuffer<StackView> view;

	// ---- hand-over: worker -> audio (pending), audio -> worker (trash)
	std::atomic<ss::Stack*> pending{nullptr};
	ss::Stack* active = nullptr;                 // audio thread
	std::array<ss::Stack*, 4> held{};           // audio thread: replaced, maybe still playing
	std::array<std::atomic<ss::Stack*>, 4> trash{}; // the worker deletes these

	// ---- audio
	ss::Player player;
	dsp::SchmittTrigger trigIn;
	dsp::BooleanTrigger alignBtn, hitBtn;
	std::array<ss::Note, 8> notes{};
	int numNotes = 0;
	float outBuf[2][kBlock]{};
	int pos = 0;
	uint32_t seed = 1;
	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;

#ifdef METAMODULE
	MetaModule::AsyncThread worker{this, [this]() { service(); }};
#else
	std::thread worker;
	std::atomic<bool> quit{false};
#endif

	Snarestack() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int i = 0; i < kLayers; i++)
			configParam(G1_PARAM + i, -30.f, 12.f, 0.f, string::f("Layer %d gain", i + 1), " dB");
		configParam(CARVE_PARAM, 0.f, 100.f, 0.f, "Carve (the weaker layer steps back per band)", " %");
		configParam(DEPTH_PARAM, 0.f, 24.f, 12.f, "Balance depth", " dB");
		configParam(PITCH_PARAM, -12.f, 12.f, 0.f, "Pitch", " st");
		configParam(DECAY_PARAM, 0.f, 100.f, 100.f, "Decay", " %");
		configParam(BX_PARAM, 0.f, 1.f, 0.5f, "Balance X (body <-> tail)");
		configParam(BY_PARAM, 0.f, 1.f, 1.f / 3.f, "Balance Y (up: crack)");
		configParam(VARY_PARAM, 0.f, 100.f, 0.f, "Vary (per-hit drift)", " %");
		configParam(LEVEL_PARAM, -24.f, 6.f, 0.f, "Level", " dB");
		configButton(ALIGN_PARAM, "Align (OFF / ONSET / ONSET+PHASE)");
		configButton(HIT_PARAM, "Hit");
		configInput(TRIG_INPUT, "Trigger");
		configInput(VEL_INPUT, "Velocity (0-10 V)")->description = "Sampled at the trigger. Unpatched: 100";
		configInput(VOCT_INPUT, "V/OCT (pitch, 0 V = as it is)");
		configInput(BX_INPUT, "Balance X CV (0.1/V)");
		configInput(BY_INPUT, "Balance Y CV (0.1/V)");
		configInput(DECAY_INPUT, "Decay CV (10 %/V)");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		player.prepare(44100.0);
		applyPreset(0);
#ifdef METAMODULE
		worker.start();
#else
		worker = std::thread([this]() {
			while (!quit.load()) {
				service();
				std::this_thread::sleep_for(std::chrono::milliseconds(15));
			}
		});
#endif
	}

	~Snarestack() override {
#ifdef METAMODULE
		worker.stop();
#else
		quit = true;
		if (worker.joinable())
			worker.join();
#endif
		delete pending.exchange(nullptr);
		delete active;
		for (auto* h : held)
			delete h;
		for (auto& t : trash)
			delete t.exchange(nullptr);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		player.prepare(e.sampleRate);
		pos = 0;
		numNotes = 0;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		applyPreset(0);
	}

	// ------------------------------------------------------------------ presets
	void setValue(const std::string& id, float v) {
		auto layerOf = [&](const char* pfx) {
			const size_t n = std::strlen(pfx);
			if (id.size() == n + 1 && !id.compare(0, n, pfx) && id[n] >= '1' && id[n] <= '4')
				return id[n] - '1';
			return -1;
		};
		int i;
		if ((i = layerOf("gain")) >= 0) params[G1_PARAM + i].setValue(v);
		else if ((i = layerOf("on")) >= 0) lay[(size_t) i].on = v > 0.5f;
		else if ((i = layerOf("tune")) >= 0) lay[(size_t) i].tune = v;
		else if ((i = layerOf("ldecay")) >= 0) lay[(size_t) i].decay = v;
		else if ((i = layerOf("hp")) >= 0) lay[(size_t) i].hp = v;
		else if ((i = layerOf("lp")) >= 0) lay[(size_t) i].lp = v;
		else if ((i = layerOf("pan")) >= 0) lay[(size_t) i].pan = v;
		else if ((i = layerOf("pol")) >= 0) lay[(size_t) i].pol = clamp((int) v, 0, 2);
		else if ((i = layerOf("nudge")) >= 0) lay[(size_t) i].nudge = v;
		else if (id == "align") align = clamp((int) v, 0, 2);
		else if (id == "ref") ref = clamp((int) v, 0, 4);
		else if (id == "carve") params[CARVE_PARAM].setValue(v);
		else if (id == "xfreq") xfreq = v;
		else if (id == "crackms") crackMs = v;
		else if (id == "bodyms") bodyMs = v;
		else if (id == "norm") normalize = v > 0.5f;
		else if (id == "bx") params[BX_PARAM].setValue(v);
		else if (id == "by") params[BY_PARAM].setValue(1.f - v); // the pad's y runs down from the crack corner
		else if (id == "depth") params[DEPTH_PARAM].setValue(v);
		else if (id == "pitch") params[PITCH_PARAM].setValue(v);
		else if (id == "decay") params[DECAY_PARAM].setValue(v);
		else if (id == "vel") velSens = v;
		else if (id == "velcrack") velCrack = v;
		else if (id == "vary") params[VARY_PARAM].setValue(v);
		else if (id == "choke") choke = v > 0.5f;
		else if (id == "level") params[LEVEL_PARAM].setValue(v);
	}

	// The plugin's loadFactory: every value to its default, the preset's on top, its sources.
	void applyPreset(int index) {
		const auto& list = presets();
		if (index < 0 || index >= (int) list.size())
			return;
		const auto& p = list[(size_t) index];
		for (auto& l : lay)
			l = LayerSet();
		align = ss::AlignPhase;
		ref = 0;
		xfreq = 2500.f;
		crackMs = 12.f;
		bodyMs = 90.f;
		normalize = true;
		velSens = 80.f;
		velCrack = 50.f;
		choke = false;
		for (int i = 0; i < ALIGN_PARAM; i++)
			params[i].setValue(paramQuantities[i]->getDefaultValue());
		for (auto& v : p.values)
			setValue(v.first, v.second);
		for (int i = 0; i < kLayers; i++)
			if (p.src[i])
				setSpec(i, p.src[i]);
		presetIdx = index;
	}

	// ------------------------------------------------------------------ the worker
	ss::StackParams stackParams() {
		ss::StackParams p;
		for (int i = 0; i < kLayers; i++) {
			auto& l = p.layer[(size_t) i];
			const auto& s = lay[(size_t) i];
			l.on = s.on;
			l.gainDb = params[G1_PARAM + i].getValue();
			l.tune = s.tune;
			l.decay = s.decay * 0.01f;
			l.hp = s.hp;
			l.lp = s.lp;
			l.pan = s.pan * 0.01f;
			l.polarity = s.pol;
			l.nudgeMs = s.nudge;
		}
		p.align = align.load();
		p.ref = ref - 1;
		p.carve = params[CARVE_PARAM].getValue() * 0.01f;
		p.xfreq = xfreq;
		p.crackMs = crackMs;
		p.bodyMs = bodyMs;
		p.normalize = normalize;
		return p;
	}

	static std::array<float, 64> pack(const ss::StackParams& p, int ver) {
		std::array<float, 64> a{};
		int k = 0;
		for (const auto& l : p.layer)
			for (float v : {(float) l.on, l.gainDb, l.tune, l.decay, l.hp, l.lp, l.pan, (float) l.polarity, l.nudgeMs})
				a[(size_t) k++] = v;
		for (float v : {(float) p.align, (float) p.ref, p.carve, p.xfreq, p.crackMs, p.bodyMs, (float) p.normalize, (float) ver})
			a[(size_t) k++] = v;
		return a;
	}

	// Reads a layer's source (worker thread). Errors go to the view.
	std::shared_ptr<const ss::Source> loadSource(const std::string& s, std::string& err) {
		if (s.empty())
			return nullptr;
		if (!s.compare(0, 6, "synth:"))
			return ss::synth(std::atoi(s.c_str() + 6));
		const bool brk = !s.compare(0, 6, "break:");
		const std::string path = s.substr(s.find(':') + 1);
		semi::Sample w;
		if (!semi::readWav(path, w, brk ? kBreakFrames : kOneShotFrames) || w.frames < 64) {
			err = "can't read " + semi::fileName(path);
			return nullptr;
		}
		std::vector<float> l(w.frames), r;
		for (size_t n = 0; n < w.frames; n++)
			l[n] = w.at(n, 0);
		if (w.channels > 1) {
			r.resize(w.frames);
			for (size_t n = 0; n < w.frames; n++)
				r[n] = w.at(n, 1);
		}
		std::string name = semi::fileName(path);
		if (name.size() > 4 && (name.substr(name.size() - 4) == ".wav" || name.substr(name.size() - 4) == ".WAV"))
			name.resize(name.size() - 4);
		if (!brk) {
			auto src = std::make_shared<ss::Source>();
			src->name = name;
			src->rate = w.rate;
			src->l = std::move(l);
			src->r = std::move(r);
			return src;
		}
		ss::gtbs::Sample b;
		b.name = name;
		b.rate = w.rate;
		b.frames = (int) w.frames;
		b.l = std::move(l);
		b.r = std::move(r);
		b.an = ss::gtbs::analyse(b, ss::gtbs::AnalysisParams());
		return ss::snareFromBreak(b, name + " snare");
	}

	void service() {
		for (auto& t : trash)
			delete t.exchange(nullptr, std::memory_order_acq_rel);
		const ss::StackParams sp = stackParams();
		const auto req = pack(sp, specVersion.load());
		const int64_t t = nowMs();
		if (req != settling) {
			settling = req;
			settlingSince = t;
		}
		if (builtOnce && req == built)
			return;
		if (builtOnce && t - settlingSince < kDebounceMs)
			return;
		if (pending.load(std::memory_order_acquire))
			return; // the last one is not even playing yet
		building = true;
		std::string err;
		const auto specs = specCopy();
		for (int i = 0; i < kLayers; i++) {
			if (specs[(size_t) i] == loadedSpec[(size_t) i] && (loadedSrc[(size_t) i] || specs[(size_t) i].empty()))
				continue;
			loadedSrc[(size_t) i] = loadSource(specs[(size_t) i], err);
			loadedSpec[(size_t) i] = specs[(size_t) i];
		}
		std::unique_ptr<ss::Stack> st = ss::build(loadedSrc, sp);
		StackView& v = view.writeSlot();
		fillView(v, *st, err);
		view.publish();
		st->mixL = std::vector<float>(); // the player reads the three parts only
		st->mixR = std::vector<float>();
		delete pending.exchange(st.release(), std::memory_order_acq_rel);
		built = req;
		builtOnce = true;
		building = false;
	}

	void fillView(StackView& v, const ss::Stack& st, const std::string& err) {
		v = StackView();
		v.valid = true;
		for (int i = 0; i < kLayers; i++) {
			const auto& inf = st.info[(size_t) i];
			v.active[i] = inf.active;
			v.shiftMs[i] = inf.shiftMs;
			v.inv[i] = inf.inverted;
			v.corrRaw[i] = inf.corrRaw;
			v.corr[i] = inf.corr;
			const auto& src = loadedSrc[(size_t) i];
			std::snprintf(v.name[i], sizeof v.name[i], "%s", src ? src->name.c_str() : "empty");
			const auto& raw = st.viewRaw[(size_t) i];
			const auto& al = st.viewAligned[(size_t) i];
			if (raw.size() < (size_t) ss::kViewLen || al.size() < (size_t) ss::kViewLen)
				continue;
			float pk = 1e-6f;
			for (int n = 0; n < ss::kViewLen; n++)
				pk = std::max(pk, std::max(std::abs(raw[(size_t) n]), std::abs(al[(size_t) n])));
			const int per = ss::kViewLen / kPts;
			for (int b = 0; b < kPts; b++) {
				float r = 0.f, a = 0.f;
				for (int n = b * per; n < (b + 1) * per; n++) {
					if (std::abs(raw[(size_t) n]) > std::abs(r))
						r = raw[(size_t) n];
					if (std::abs(al[(size_t) n]) > std::abs(a))
						a = al[(size_t) n];
				}
				v.raw[i][b] = r / pk;
				v.al[i][b] = a / pk;
			}
		}
		for (int b = 0; b < ss::kBands; b++) {
			v.cancelRaw[b] = st.cancelRaw[(size_t) b];
			v.cancelAl[b] = st.cancelAligned[(size_t) b];
		}
		for (int k = 0; k < ss::NumComps; k++)
			v.share[k] = st.share[(size_t) k];
		v.normDb = st.normDb;
		v.ref = st.ref;
		std::snprintf(v.error, sizeof v.error, "%s", err.c_str());
	}

	// ------------------------------------------------------------------ patch state
	json_t* dataToJson() override {
		json_t* root = json_object();
		const auto specs = specCopy();
		json_t* src = json_array();
		for (auto& s : specs)
			json_array_append_new(src, json_string(s.c_str()));
		json_object_set_new(root, "sources", src);
		json_t* ls = json_array();
		for (const auto& l : lay) {
			json_t* o = json_object();
			json_object_set_new(o, "on", json_boolean(l.on));
			json_object_set_new(o, "tune", json_real(l.tune));
			json_object_set_new(o, "decay", json_real(l.decay));
			json_object_set_new(o, "hp", json_real(l.hp));
			json_object_set_new(o, "lp", json_real(l.lp));
			json_object_set_new(o, "pan", json_real(l.pan));
			json_object_set_new(o, "pol", json_integer(l.pol));
			json_object_set_new(o, "nudge", json_real(l.nudge));
			json_array_append_new(ls, o);
		}
		json_object_set_new(root, "layers", ls);
		json_object_set_new(root, "align", json_integer(align));
		json_object_set_new(root, "ref", json_integer(ref));
		json_object_set_new(root, "xfreq", json_real(xfreq));
		json_object_set_new(root, "crackms", json_real(crackMs));
		json_object_set_new(root, "bodyms", json_real(bodyMs));
		json_object_set_new(root, "norm", json_boolean(normalize));
		json_object_set_new(root, "vel", json_real(velSens));
		json_object_set_new(root, "velcrack", json_real(velCrack));
		json_object_set_new(root, "choke", json_boolean(choke));
		json_object_set_new(root, "preset", json_integer(presetIdx));
		json_object_set_new(root, "dir", json_string(lastDir.c_str()));
		return root;
	}

	void dataFromJson(json_t* root) override {
		if (json_t* src = json_object_get(root, "sources"))
			for (int i = 0; i < kLayers && i < (int) json_array_size(src); i++)
				if (const char* s = json_string_value(json_array_get(src, i)))
					setSpec(i, s);
		if (json_t* ls = json_object_get(root, "layers"))
			for (int i = 0; i < kLayers && i < (int) json_array_size(ls); i++) {
				json_t* o = json_array_get(ls, i);
				auto f = [&](const char* k, float def) { json_t* j = json_object_get(o, k); return j ? (float) json_number_value(j) : def; };
				auto& l = lay[(size_t) i];
				json_t* on = json_object_get(o, "on");
				l.on = on ? json_is_true(on) : true;
				l.tune = f("tune", 0.f);
				l.decay = f("decay", 100.f);
				l.hp = f("hp", 20.f);
				l.lp = f("lp", 20000.f);
				l.pan = f("pan", 0.f);
				json_t* pol = json_object_get(o, "pol");
				l.pol = pol ? clamp((int) json_integer_value(pol), 0, 2) : 0;
				l.nudge = f("nudge", 0.f);
			}
		auto f = [&](const char* k, float def) { json_t* j = json_object_get(root, k); return j ? (float) json_number_value(j) : def; };
		auto b = [&](const char* k, bool def) { json_t* j = json_object_get(root, k); return j ? json_is_true(j) : def; };
		align = clamp((int) f("align", ss::AlignPhase), 0, 2);
		ref = clamp((int) f("ref", 0), 0, 4);
		xfreq = f("xfreq", 2500.f);
		crackMs = f("crackms", 12.f);
		bodyMs = f("bodyms", 90.f);
		normalize = b("norm", true);
		velSens = f("vel", 80.f);
		velCrack = f("velcrack", 50.f);
		choke = b("choke", false);
		presetIdx = clamp((int) f("preset", 0), 0, (int) presets().size() - 1);
		if (json_t* d = json_object_get(root, "dir"))
			lastDir = json_string_value(d) ? json_string_value(d) : "";
	}

	// ------------------------------------------------------------------ audio
	void swapIn() {
		ss::Stack* np = pending.load(std::memory_order_acquire);
		if (np) {
			int free = -1;
			for (int i = 0; i < (int) held.size(); i++)
				if (!held[(size_t) i])
					free = i;
			if (!active || free >= 0) {
				np = pending.exchange(nullptr, std::memory_order_acq_rel);
				if (active)
					held[(size_t) free] = active;
				active = np;
			}
		}
		// replaced stacks go back to the worker once no voice plays them
		for (auto& h : held) {
			if (!h || player.uses(h))
				continue;
			for (auto& t : trash) {
				ss::Stack* expected = nullptr;
				if (t.compare_exchange_strong(expected, h, std::memory_order_acq_rel)) {
					h = nullptr;
					break;
				}
			}
		}
	}

	ss::PlayParams playParams() {
		ss::PlayParams p;
		p.bx = clamp(params[BX_PARAM].getValue() + inputs[BX_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		p.by = clamp(1.f - (params[BY_PARAM].getValue() + inputs[BY_INPUT].getVoltage() * 0.1f), 0.f, 1.f);
		ss::clampToTriangle(p.bx, p.by);
		p.depthDb = params[DEPTH_PARAM].getValue();
		p.pitch = clamp(params[PITCH_PARAM].getValue() + 12.f * inputs[VOCT_INPUT].getVoltage(), -36.f, 36.f);
		p.decay = clamp((params[DECAY_PARAM].getValue() + inputs[DECAY_INPUT].getVoltage() * 10.f) * 0.01f, 0.f, 1.f);
		p.velSens = velSens * 0.01f;
		p.velCrack = velCrack * 0.01f;
		p.vary = params[VARY_PARAM].getValue() * 0.01f;
		p.keytrack = false; // V/OCT goes to PITCH above, continuously
		p.choke = choke;
		p.gain = std::pow(10.f, params[LEVEL_PARAM].getValue() / 20.f);
		p.seed = ++seed;
		return p;
	}

	void process(const ProcessArgs& args) override {
		if (alignBtn.process(params[ALIGN_PARAM].getValue() > 0.f))
			align = (align + 1) % ss::NumAlign;
		const bool hit = hitBtn.process(params[HIT_PARAM].getValue() > 0.f);
		if ((trigIn.process(inputs[TRIG_INPUT].getVoltage(), 0.1f, 1.f) || hit) && numNotes < (int) notes.size()) {
			const int vel = inputs[VEL_INPUT].isConnected() ? clamp((int) std::round(inputs[VEL_INPUT].getVoltage() * 12.7f), 1, 127) : 100;
			notes[(size_t) numNotes++] = {pos, 60, (uint8_t) vel};
		}

		outputs[OUT_L_OUTPUT].setVoltage(outBuf[0][pos] * kVoltsPerUnit);
		outputs[OUT_R_OUTPUT].setVoltage(outBuf[1][pos] * kVoltsPerUnit);
		if (++pos >= kBlock) {
			pos = 0;
			swapIn();
			player.process(active, playParams(), notes.data(), numNotes, outBuf[0], outBuf[1], kBlock);
			numNotes = 0;
		}

		if (ui.tick(args.sampleTime)) {
			const int a = align.load();
			for (int i = 0; i < 3; i++)
				lights[AL_OFF_LIGHT + i].setBrightness(a == i ? 1.f : 0.f);
			Snapshot& s = snap.writeSlot();
			const ss::PlayParams p = playParams();
			s.bx = p.bx;
			s.by = p.by;
			s.align = a;
			for (int k = 0; k < ss::NumComps; k++)
				s.gainDb[k] = player.lastGainDb[(size_t) k].load(std::memory_order_relaxed);
			s.meter = player.meter.load(std::memory_order_relaxed);
			s.hits = player.hits.load(std::memory_order_relaxed);
			s.building = building.load(std::memory_order_relaxed) || pending.load(std::memory_order_relaxed) != nullptr;
			s.ready = active != nullptr;
			snap.publish();
		}
	}
};

// ---------------------------------------------------------------------------------------- screens

// The four layers around the attack (2 ms before, 30 ms after): as loaded (dim) and as stacked
// (bright), each with where the aligner moved it and whether it was flipped.
struct AlignScreen : Screen {
	Snarestack* module = nullptr;
	StackView demo;
	Snapshot demoSnap;

	AlignScreen() {
		const char* names[4] = {"808 snare", "body 185", "crack noise", "tail verb"};
		const float shifts[4] = {0.f, -0.42f, 0.11f, 0.03f};
		demo.valid = true;
		for (int i = 0; i < 4; i++) {
			std::snprintf(demo.name[i], sizeof demo.name[i], "%s", names[i]);
			demo.active[i] = true;
			demo.shiftMs[i] = shifts[i];
			demo.inv[i] = i == 1;
			for (int b = 0; b < kPts; b++) {
				const float t = (b - 3) / (float) kPts;
				const float env = t < 0.f ? 0.f : std::exp(-t * (2.f + 3.f * i));
				const float f = i == 0 ? 6.f : i == 1 ? 5.f : 17.f;
				demo.al[i][b] = env * std::sin(6.283f * f * t + 0.3f * i) * (i >= 2 ? (b % 2 ? 0.7f : -0.8f) : 1.f);
				demo.raw[i][b] = env * std::sin(6.283f * f * (t - 0.02f * i));
			}
		}
	}

	void drawScreen(NVGcontext* vg) override {
		const StackView& v = module ? module->view.read() : demo;
		const Snapshot& s = module ? module->snap.read() : demoSnap;
		const float w = box.size.x, h = box.size.y;
		const float pad = 3.f;
		text(vg, pad, pad - 1.f, kAlignNames[(size_t) clamp(s.align, 0, 2)], 9.f, semi::theme::text());
		if (s.building)
			text(vg, w - pad, pad - 1.f, "BUILDING", 9.f, semi::theme::amber(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		else if (v.valid && v.error[0])
			text(vg, w - pad, pad - 1.f, v.error, 9.f, semi::theme::magenta(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		else if (v.valid) {
			char buf[24];
			std::snprintf(buf, sizeof buf, "REF %d", v.ref + 1);
			text(vg, w - pad, pad - 1.f, buf, 9.f, semi::theme::dim(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		}
		if (!v.valid)
			return;
		const float top = pad + 11.f;
		const float laneH = (h - top - pad) / kLayers;
		const float attackX = pad + (w - 2.f * pad) * (2.f / 32.f); // views start 2 ms before the attack
		for (int i = 0; i < kLayers; i++) {
			const float y0 = top + i * laneH;
			const NVGcolor c = layerColor(i);
			char buf[32];
			std::snprintf(buf, sizeof buf, "%d %s", i + 1, v.name[i]);
			text(vg, pad, y0, buf, 9.f, v.active[i] ? c : semi::theme::dim());
			if (!v.active[i])
				continue;
			std::snprintf(buf, sizeof buf, "%+.2f%s", v.shiftMs[i], v.inv[i] ? " INV" : "");
			text(vg, w - pad, y0, buf, 9.f, v.inv[i] ? semi::theme::magenta() : semi::theme::text(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
			const float wy0 = y0 + 10.f, wy1 = y0 + laneH - 1.5f;
			const float mid = 0.5f * (wy0 + wy1), amp = 0.5f * (wy1 - wy0);
			line(vg, attackX, wy0, attackX, wy1, semi::theme::grid());
			float raw[kPts], al[kPts];
			for (int b = 0; b < kPts; b++) {
				raw[b] = mid - amp * clamp(v.raw[i][b], -1.f, 1.f);
				al[b] = mid - amp * clamp(v.al[i][b], -1.f, 1.f);
			}
			polyline(vg, raw, kPts, pad, w - 2.f * pad, semi::theme::cyanDeep());
			polyline(vg, al, kPts, pad, w - 2.f * pad, c);
		}
	}
};

// The balance triangle (crack top, body bottom-left, tail bottom-right) with the point and the
// last hit's gains, and the phase meter: per band, the sum against the sum of the energies
// (0 dB unrelated, up = they add, down = they cancel), stacked (bar) against as loaded (tick).
struct BalScreen : Screen {
	Snarestack* module = nullptr;
	StackView demo;
	Snapshot demoSnap;

	BalScreen() {
		demo.valid = true;
		const float raw[6]{-2.f, -4.5f, -3.f, 1.f, 0.5f, 0.2f}, al[6]{2.5f, 3.f, 3.5f, 2.f, 0.8f, 0.3f};
		for (int b = 0; b < 6; b++) {
			demo.cancelRaw[b] = raw[b];
			demo.cancelAl[b] = al[b];
		}
		demoSnap.bx = 0.45f;
		demoSnap.by = 0.45f;
	}

	void drawScreen(NVGcontext* vg) override {
		const StackView& v = module ? module->view.read() : demo;
		const Snapshot& s = module ? module->snap.read() : demoSnap;
		const float w = box.size.x, h = box.size.y;
		const float pad = 3.f;
		// triangle
		const float tw = w - 2.f * pad, th = tw * 0.866f;
		const float tx0 = pad, ty0 = pad + 9.f;
		const float cx = tx0 + 0.5f * tw, cy = ty0, bxp = tx0, byp = ty0 + th, txp = tx0 + tw, typ = ty0 + th;
		nvgBeginPath(vg);
		nvgMoveTo(vg, cx, cy);
		nvgLineTo(vg, bxp, byp);
		nvgLineTo(vg, txp, typ);
		nvgClosePath(vg);
		nvgFillColor(vg, semi::theme::cell());
		nvgFill(vg);
		nvgStrokeColor(vg, semi::theme::cyanDim());
		nvgStrokeWidth(vg, semi::platform::kThinStroke);
		nvgStroke(vg);
		text(vg, cx, pad - 1.f, "CRACK", 9.f, semi::theme::dim(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		text(vg, bxp, typ + 1.f, "B", 9.f, semi::theme::dim(), NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
		text(vg, txp, typ + 1.f, "T", 9.f, semi::theme::dim(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		// the point: x across, y down from the crack corner (the engine's pad coordinates)
		const float px = tx0 + s.bx * tw, py = ty0 + s.by * th;
		nvgBeginPath(vg);
		nvgCircle(vg, px, py, 3.f);
		nvgFillColor(vg, semi::theme::magenta());
		nvgFill(vg);

		// phase meter
		const float my0 = typ + 12.f, my1 = h - pad - 10.f;
		if (my1 - my0 < 10.f || !v.valid)
			return;
		const float mid = 0.5f * (my0 + my1), half = 0.5f * (my1 - my0);
		const float bw = tw / ss::kBands;
		line(vg, pad, mid, w - pad, mid, semi::theme::grid());
		for (int b = 0; b < ss::kBands; b++) {
			const float x = pad + b * bw;
			const float a = clamp(v.cancelAl[b] / 6.f, -1.f, 1.f), r = clamp(v.cancelRaw[b] / 6.f, -1.f, 1.f);
			const NVGcolor c = a >= 0.f ? semi::theme::cyan() : semi::theme::magenta();
			if (a >= 0.f)
				fillRect(vg, x + 1.f, mid - a * half, bw - 2.f, a * half, c);
			else
				fillRect(vg, x + 1.f, mid, bw - 2.f, -a * half, c);
			fillRect(vg, x + 0.5f, mid - r * half - 1.f, bw - 1.f, 2.f, semi::theme::text());
		}
		text(vg, w * 0.5f, h - pad, "PHASE dB", 9.f, semi::theme::dim(), NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
	}
};

// ---------------------------------------------------------------------------------------- widget
namespace {

void addSteps(Menu* menu, const std::string& label, const std::vector<float>& values, const char* fmt, float* v) {
	std::vector<std::string> labels;
	for (float x : values)
		labels.push_back(string::f(fmt, x));
	menu->addChild(createIndexSubmenuItem(label, labels,
	    [=]() {
		    size_t best = 0;
		    for (size_t i = 1; i < values.size(); i++)
			    if (std::abs(values[i] - *v) < std::abs(values[best] - *v))
				    best = i;
		    return best;
	    },
	    [=](size_t i) { *v = values[i]; }));
}

} // namespace

struct SnarestackWidget : ModuleWidget {
	SnarestackWidget(Snarestack* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Snarestack.svg")));

		auto* al = semi::createScreenMm<AlignScreen>(Vec(L::ALIGN.x, L::ALIGN.y), Vec(L::ALIGN.w, L::ALIGN.h));
		al->module = module;
		addChild(al);
		auto* bal = semi::createScreenMm<BalScreen>(Vec(L::BAL.x, L::BAL.y), Vec(L::BAL.w, L::BAL.h));
		bal->module = module;
		addChild(bal);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		addParam(createParamCentered<semi::Button>(at(L::ALIGN_BTN), module, Snarestack::ALIGN_PARAM));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::AL_OFF), module, Snarestack::AL_OFF_LIGHT));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::AL_ONSET), module, Snarestack::AL_ONSET_LIGHT));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::AL_PHASE), module, Snarestack::AL_PHASE_LIGHT));
		addParam(createParamCentered<semi::PadMagenta>(at(L::HIT), module, Snarestack::HIT_PARAM));

		addParam(createParamCentered<semi::KnobM>(at(L::G1), module, Snarestack::G1_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::G2), module, Snarestack::G2_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::G3), module, Snarestack::G3_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::G4), module, Snarestack::G4_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::CARVE), module, Snarestack::CARVE_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::DEPTH), module, Snarestack::DEPTH_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::PITCH), module, Snarestack::PITCH_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::DECAY), module, Snarestack::DECAY_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::BX), module, Snarestack::BX_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::BY), module, Snarestack::BY_PARAM));
		addParam(createParamCentered<semi::KnobMHot>(at(L::VARY), module, Snarestack::VARY_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::LEVEL), module, Snarestack::LEVEL_PARAM));

		addInput(createInputCentered<semi::Jack>(at(L::TRIG), module, Snarestack::TRIG_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::VEL), module, Snarestack::VEL_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::VOCT), module, Snarestack::VOCT_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::BX_CV), module, Snarestack::BX_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::BY_CV), module, Snarestack::BY_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::DECAY_CV), module, Snarestack::DECAY_INPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_L), module, Snarestack::OUT_L_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::OUT_R), module, Snarestack::OUT_R_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Snarestack*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		std::vector<std::string> names;
		for (auto& p : presets())
			names.push_back(p.name);
		menu->addChild(createIndexSubmenuItem("Preset", names, [=]() { return (size_t) m->presetIdx.load(); },
		                                      [=](size_t i) { m->applyPreset((int) i); }));

		const auto specs = m->specCopy();
		for (int i = 0; i < kLayers; i++) {
			const std::string& sp = specs[(size_t) i];
			std::string right = sp.empty() ? "empty" : !sp.compare(0, 6, "synth:") ? ss::synthNames()[(size_t) clamp(std::atoi(sp.c_str() + 6), 0, 9)]
			                                                                         : semi::fileName(sp.substr(sp.find(':') + 1));
			LayerSet* l = &m->lay[(size_t) i];
			menu->addChild(createSubmenuItem(string::f("Layer %d", i + 1), right, [=](Menu* sub) {
				std::vector<std::string> synths(ss::synthNames().begin(), ss::synthNames().end());
				sub->addChild(createIndexSubmenuItem("Built-in", synths,
				    [=]() {
					    const auto s = m->specCopy()[(size_t) i];
					    return (size_t) (!s.compare(0, 6, "synth:") ? clamp(std::atoi(s.c_str() + 6), 0, 9) : 99);
				    },
				    [=](size_t k) { m->setSpec(i, "synth:" + std::to_string(k)); }));
				sub->addChild(createMenuItem("Load WAV (one-shot)...", "", [=]() {
					semi::openFileDialog("WAV:wav,WAV", m->lastDir, [=](const std::string& path) {
						m->lastDir = semi::dirName(path);
						m->setSpec(i, "file:" + path);
					});
				}));
				sub->addChild(createMenuItem("Load break WAV (cut its snare)...", "", [=]() {
					semi::openFileDialog("WAV:wav,WAV", m->lastDir, [=](const std::string& path) {
						m->lastDir = semi::dirName(path);
						m->setSpec(i, "break:" + path);
					});
				}));
				sub->addChild(createMenuItem("Clear", "", [=]() { m->setSpec(i, ""); }));
				sub->addChild(new MenuSeparator);
				sub->addChild(createBoolPtrMenuItem("On", "", &l->on));
				addSteps(sub, "Tune", {-12, -7, -5, -3, -2, -1, 0, 1, 2, 3, 5, 7, 12}, "%+.0f st", &l->tune);
				addSteps(sub, "Decay", {10, 20, 30, 40, 50, 60, 70, 80, 90, 100}, "%.0f %%", &l->decay);
				addSteps(sub, "High cut (HP)", {20, 40, 80, 150, 300, 600, 1200, 2000}, "%.0f Hz", &l->hp);
				addSteps(sub, "Low cut (LP)", {1000, 2000, 4000, 6000, 8000, 12000, 20000}, "%.0f Hz", &l->lp);
				addSteps(sub, "Pan", {-100, -75, -50, -25, 0, 25, 50, 75, 100}, "%+.0f", &l->pan);
				sub->addChild(createIndexSubmenuItem("Polarity", kPolNames, [=]() { return (size_t) l->pol; }, [=](size_t k) { l->pol = (int) k; }));
				addSteps(sub, "Nudge", {-2, -1, -0.5f, -0.25f, 0, 0.25f, 0.5f, 1, 2}, "%+.2f ms", &l->nudge);
			}));
		}

		menu->addChild(createSubmenuItem("Stack", "", [=](Menu* sub) {
			sub->addChild(createIndexSubmenuItem("Align", kAlignNames, [=]() { return (size_t) m->align.load(); },
			                                     [=](size_t k) { m->align = (int) k; }));
			sub->addChild(createIndexSubmenuItem("Reference", kRefNames, [=]() { return (size_t) m->ref; }, [=](size_t k) { m->ref = (int) k; }));
			addSteps(sub, "Crack above", {800, 1200, 1800, 2500, 3500, 5000, 8000}, "%.0f Hz", &m->xfreq);
			addSteps(sub, "Crack time", {3, 6, 9, 12, 18, 25, 40}, "%.0f ms", &m->crackMs);
			addSteps(sub, "Body time", {30, 50, 70, 90, 120, 160, 220, 300}, "%.0f ms", &m->bodyMs);
			sub->addChild(createBoolPtrMenuItem("Normalize (peak -6 dBFS)", "", &m->normalize));
		}));
		menu->addChild(createSubmenuItem("Playing", "", [=](Menu* sub) {
			addSteps(sub, "Velocity", {0, 20, 40, 60, 80, 100}, "%.0f %%", &m->velSens);
			addSteps(sub, "Vel > crack", {0, 25, 50, 75, 100}, "%.0f %%", &m->velCrack);
			sub->addChild(createBoolPtrMenuItem("Choke (mono)", "", &m->choke));
		}));
	}
};

Model* modelSnarestack = createModel<Snarestack, SnarestackWidget>("Snarestack");
