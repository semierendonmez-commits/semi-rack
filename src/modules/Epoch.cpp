// EPOCH — Rack / MetaModule port of EPOCH, the long clock (engine: engines/epoch, vendored from
// semierendonmez-commits/epoch Source/engine; only the EpochLink include is swapped for Roles.h).
//
// A plan (score: ENERGY + SPACE curves and sections -> holes, tension, turns, macros at every 16th)
// is played from CLK / RUN / RST (or the plan's own tempo when CLK is not patched) and sent out as
//   BUS  the FamilyBus CV cable (src/common/FamilyBusCV.hpp): POS, ENERGY, SCENE, EVENT
//   NRG / DENS / CHS / DUCK  the ENERGY, DENSITY, CHAOS and DUCK macros, 0-10 V
//
// Threads: the plan is rendered off the audio thread (Rack: the widget's step() on the UI thread;
// MetaModule: an AsyncThread) into the free one of two plan slots; process() swaps to it at the next
// sample. process() never allocates and never renders.
//
// Structure follows NEXUSCLIP (TripleBuffer snapshot via UiDivider, Screen subclasses with a demo).

#include "../plugin.hpp"
#include "../common/Components.hpp"
#include "../common/FamilyBusCV.hpp"
#include "../common/Screen.hpp"
#include "../common/TripleBuffer.hpp"
#include "../layout/EpochLayout.hpp"
#include "epoch/Analyzer.h"
#include "epoch/Plan.h"
#include "epoch/Score.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>

#ifdef METAMODULE
#include "threads/async_thread.hh"
#else
#include <osdialog.h>
#endif

namespace L = semi::layout::epoch;
using semi::Screen;

namespace {

constexpr int kCols = 96;       // timeline columns (curves are decimated again for MetaModule)
constexpr int kMaxSec = 32;     // sections shown in the strip
constexpr int kRefPlan = -1;    // planIndex of the measured reference (after the templates)

// Macro slots of PlanParams::defaults() that the CV outs carry.
constexpr int kSlotEnergy = 0, kSlotDensity = 1, kSlotChaos = 2, kSlotDuck = 4;

const int kLengths[] = {0, 32, 64, 96, 128, 160, 192, 224, 256, 320, 384, 448, 512};
constexpr int kNumLengths = sizeof(kLengths) / sizeof(kLengths[0]);
const int kGrainSteps[] = {4, 8, 16, 32, 64};
const float kClusters[] = {0.f, 0.3f, 0.6f, 0.8f, 0.95f};
const float kTensions[] = {0.f, 0.5f, 1.f, 1.5f};
const int kLineBars[] = {4, 8, 16, 32};
const float kClockSteps[] = {1.f, 2.f, 4.f, 1.f / 6.f};   // 16ths per CLK pulse: 1/16, 1/8, 1/4, 24 PPQN
const float kClockPerBeat[] = {4.f, 2.f, 1.f, 24.f};

int numPlans() { return epoch::numTemplates() + 1; }   // the templates + REF

// The plan as the timeline draws it: plain data, copied into every snapshot.
struct SecView {
	float bar = 0.f;
	uint8_t role = 0;
	char name[12]{};
};

struct View {
	char title[24]{};
	int bars = 0;
	float bpm = 0.f;
	float energy[kCols]{}, density[kCols]{};
	uint8_t hole[kCols]{};
	int nSec = 0;
	SecView sec[kMaxSec];
};

struct Snapshot {
	View view;
	float bar = 0.f;          // playhead, bars from the start of the plan
	float energy = 0.f, density = 0.f;
	int section = -1;
	float bpm = 0.f;
	bool running = false, external = false;
};

void copyName(char* dst, size_t n, const std::string& s) {
	std::snprintf(dst, n, "%s", s.c_str());
}

void buildView(const epoch::Score& s, const epoch::Plan& p, View& v) {
	copyName(v.title, sizeof v.title, s.title);
	v.bars = p.bars;
	v.bpm = (float) s.bpm;
	const auto& E = p.src[epoch::SrcEnergy];
	const auto& D = p.src[epoch::SrcDensity];
	const auto& H = p.src[epoch::SrcHole];
	for (int c = 0; c < kCols; c++) {
		const int a = c * p.steps / kCols, b = std::max(a + 1, (c + 1) * p.steps / kCols);
		float e = 0.f, d = 0.f, h = 0.f;
		for (int i = a; i < b; i++) {
			e += E[(size_t) i];
			d += D[(size_t) i];
			h += H[(size_t) i];
		}
		const float n = (float) (b - a);
		v.energy[c] = e / n;
		v.density[c] = d / n;
		v.hole[c] = h / n >= 0.5f ? 1 : 0;
	}
	v.nSec = std::min((int) s.sections.size(), kMaxSec);
	for (int i = 0; i < v.nSec; i++) {
		v.sec[i].bar = (float) s.sections[(size_t) i].bar;
		v.sec[i].role = (uint8_t) s.sections[(size_t) i].role;
		copyName(v.sec[i].name, sizeof v.sec[i].name, s.sections[(size_t) i].name);
	}
}

// Everything the plan depends on. The worker renders when this changes (and has settled).
struct Request {
	int plan = 0, length = 0, seed = 1, grain = 2, cluster = 2, tension = 2, turn = 1, refVersion = 0;
	float scale = 1.f, smooth = 0.f, silence = 1.f;
	bool operator==(const Request& o) const {
		return plan == o.plan && length == o.length && seed == o.seed && grain == o.grain && cluster == o.cluster &&
		       tension == o.tension && turn == o.turn && refVersion == o.refVersion && scale == o.scale &&
		       smooth == o.smooth && silence == o.silence;
	}
	bool operator!=(const Request& o) const { return !(*this == o); }
};

#ifdef METAMODULE
constexpr int kDebounceMs = 150;   // a render is ~10 ms on the A7: don't re-render on every knob tick
#else
constexpr int kDebounceMs = 0;     // ~1-2 ms on the UI thread: follow the knob
#endif

int64_t nowMs() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

#ifndef METAMODULE
// Minimal streaming WAV reader (PCM 8/16/24/32, float 32; WAVE_FORMAT_EXTENSIBLE) into the Analyzer.
// Rack only: the MetaModule has no file dialog path here and keeps to the built-in plans.
bool measureWav(const char* path, epoch::Analyzer*& out, double& rate) {
	FILE* f = std::fopen(path, "rb");
	if (!f)
		return false;
	auto u32 = [](const uint8_t* b) { return (uint32_t) b[0] | (uint32_t) b[1] << 8 | (uint32_t) b[2] << 16 | (uint32_t) b[3] << 24; };
	auto u16 = [](const uint8_t* b) { return (uint32_t) b[0] | (uint32_t) b[1] << 8; };
	uint8_t hdr[12];
	if (std::fread(hdr, 1, 12, f) != 12 || std::memcmp(hdr, "RIFF", 4) || std::memcmp(hdr + 8, "WAVE", 4)) {
		std::fclose(f);
		return false;
	}
	int fmt = 0, ch = 0, bits = 0;
	uint32_t sr = 0, dataLen = 0;
	bool haveData = false;
	uint8_t ck[8];
	while (std::fread(ck, 1, 8, f) == 8) {
		const uint32_t len = u32(ck + 4);
		if (!std::memcmp(ck, "fmt ", 4)) {
			uint8_t b[40]{};
			const size_t n = std::min<uint32_t>(len, sizeof b);
			if (std::fread(b, 1, n, f) != n)
				break;
			fmt = (int) u16(b);
			ch = (int) u16(b + 2);
			sr = u32(b + 4);
			bits = (int) u16(b + 14);
			if (fmt == 0xFFFE && n >= 26)
				fmt = (int) u16(b + 24);   // sub-format GUID starts with the format tag
			if (len > n)
				std::fseek(f, (long) (len - n), SEEK_CUR);
		}
		else if (!std::memcmp(ck, "data", 4)) {
			dataLen = len;
			haveData = true;
			break;
		}
		else
			std::fseek(f, (long) (len + (len & 1)), SEEK_CUR);
	}
	const int bytes = bits / 8;
	if (!haveData || ch < 1 || sr < 1000 || !(fmt == 1 || fmt == 3) || bytes < 1 || bytes > 4 || (fmt == 3 && bytes != 4)) {
		std::fclose(f);
		return false;
	}
	rate = sr;
	out = new epoch::Analyzer(sr);
	constexpr int kFrames = 4096;
	const int frameBytes = bytes * ch;
	std::vector<uint8_t> raw((size_t) kFrames * frameBytes);
	std::vector<float> mono(kFrames);
	uint64_t left = dataLen / (uint32_t) frameBytes;
	while (left > 0) {
		const int want = (int) std::min<uint64_t>(left, kFrames);
		const int got = (int) (std::fread(raw.data(), (size_t) frameBytes, (size_t) want, f));
		if (got <= 0)
			break;
		for (int i = 0; i < got; i++) {
			float acc = 0.f;
			for (int c = 0; c < ch; c++) {
				const uint8_t* s = raw.data() + (size_t) i * frameBytes + (size_t) c * bytes;
				float x;
				if (fmt == 3) {
					uint32_t u = u32(s);
					std::memcpy(&x, &u, 4);
				}
				else if (bytes == 1)
					x = ((int) s[0] - 128) / 128.f;
				else if (bytes == 2)
					x = (int16_t) u16(s) / 32768.f;
				else if (bytes == 3)
					x = (float) ((int32_t) (((uint32_t) s[0] << 8) | ((uint32_t) s[1] << 16) | ((uint32_t) s[2] << 24)) >> 8) / 8388608.f;
				else
					x = (float) (int32_t) u32(s) / 2147483648.f;
				acc += x;
			}
			mono[(size_t) i] = acc / (float) ch;
		}
		out->push(mono.data(), got);
		left -= (uint64_t) got;
	}
	std::fclose(f);
	return true;
}
#endif

} // namespace

struct Epoch : Module {
	enum ParamId { PLAN_PARAM, LENGTH_PARAM, SCALE_PARAM, SMOOTH_PARAM, SILENCE_PARAM, RUN_PARAM, PARAMS_LEN };
	enum InputId { CLK_INPUT, RUN_INPUT, RST_INPUT, INPUTS_LEN };
	enum OutputId { BUS_OUTPUT, NRG_OUTPUT, DENS_OUTPUT, CHS_OUTPUT, DUCK_OUTPUT, OUTPUTS_LEN };
	enum LightId { SRC_DNB_LIGHT, SRC_UKG_LIGHT, SRC_MSR_LIGHT, SRC_FORM_LIGHT, SRC_REF_LIGHT, RUN_LIGHT, LIGHTS_LEN };

	// ---- plan slots (rendered off the audio thread, swapped in process())
	struct Slot {
		epoch::Plan plan;
		View view;
	};
	Slot slots[2];
	std::atomic<int> active{0};
	std::atomic<bool> pendingSwap{false};
	std::atomic_flag workerBusy = ATOMIC_FLAG_INIT;
	Request rendered, settling;
	int64_t settlingSince = 0;

	// ---- state outside the params (menu + JSON); read by the worker and process()
	std::atomic<int> planIndex{0};
	std::atomic<int> seed{1}, grain{2}, cluster{2}, tension{2}, turn{1};
	std::atomic<int> endLoop{1}, clockMode{0};
	std::atomic<bool> running{true};

	// the measured reference (REF); guarded by workerBusy
	epoch::Score refScore;
	std::atomic<bool> hasRef{false};
	std::atomic<int> refVersion{0};
	std::string refInfo;

	// ---- transport (audio thread)
	dsp::SchmittTrigger clkTrig, runTrig, rstTrig;
	dsp::BooleanTrigger planBtn, runBtn;
	dsp::PulseGenerator eventPulse;
	float eventVolts = 0.f;
	double base = 0.0;          // position in 16ths at the last clock pulse (or now, internal)
	double pos = 0.0;
	float sinceClk = 0.f, clkPeriod = 0.f;
	bool armed = false;         // after a reset the next clock pulse plays step 0
	int lastBar = -1;
	std::atomic<float> liveBpm{172.f};

	semi::TripleBuffer<Snapshot> snap;
	semi::UiDivider ui;

#ifdef METAMODULE
	MetaModule::AsyncThread worker{this, [this]() { serviceRender(false); }};
#endif

	Epoch() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configButton(PLAN_PARAM, "Next plan");
		std::vector<std::string> lens{"Plan's own"};
		for (int i = 1; i < kNumLengths; i++)
			lens.push_back(std::to_string(kLengths[i]) + " bars");
		configSwitch(LENGTH_PARAM, 0.f, kNumLengths - 1, 0.f, "Length", lens)->description =
		    "Stretch the plan to this many bars (the shape is kept)";
		configParam(SCALE_PARAM, 0.5f, 1.5f, 1.f, "Energy scale", " %", 0.f, 100.f)->description =
		    "Scales the ENERGY curve without redrawing it";
		configParam(SMOOTH_PARAM, 0.f, 8.f, 0.f, "Smooth", " beats")->description =
		    "Lag added to NRG / DENS / CHS / DUCK (on top of each macro's own)";
		configParam(SILENCE_PARAM, 0.f, 2.f, 1.f, "Silence", " %", 0.f, 100.f)->description =
		    "SPACE x: scales the share of time the plan cuts into holes";
		configButton(RUN_PARAM, "Run / stop");
		configInput(CLK_INPUT, "Clock")->description = "1/16 pulses by default (menu: 1/8, 1/4, 24 PPQN). Unpatched: the plan's own tempo";
		configInput(RUN_INPUT, "Run trigger")->description = "Trigger: toggles run / stop";
		configInput(RST_INPUT, "Reset")->description = "Back to bar 1; with CLK patched the next pulse plays bar 1";
		configOutput(BUS_OUTPUT, "Bus")->description = "FamilyBus, 4 channels: POS, ENERGY, SCENE, EVENT (phrase turns and sections)";
		configOutput(NRG_OUTPUT, "Energy")->description = "ENERGY macro, 0-10 V";
		configOutput(DENS_OUTPUT, "Density")->description = "DENSITY macro (energy with the holes cut out), 0-10 V";
		configOutput(CHS_OUTPUT, "Chaos")->description = "CHAOS macro (energy + tension), 0-10 V";
		configOutput(DUCK_OUTPUT, "Duck")->description = "DUCK macro, 0-10 V";
		configLight(RUN_LIGHT, "Running");

		for (auto& s : slots)
			s.plan.allocate();
		// first plan, synchronously (not the audio thread)
		rendered = settling = makeRequest();
		renderInto(0, rendered);
#ifdef METAMODULE
		worker.start();
#endif
	}

	// ---- the worker side ----------------------------------------------------------------------

	Request makeRequest() {
		Request r;
		r.plan = planIndex.load(std::memory_order_relaxed);
		r.length = clamp((int) std::round(params[LENGTH_PARAM].getValue()), 0, kNumLengths - 1);
		r.scale = params[SCALE_PARAM].getValue();
		r.smooth = params[SMOOTH_PARAM].getValue();
		r.silence = params[SILENCE_PARAM].getValue();
		r.seed = seed.load(std::memory_order_relaxed);
		r.grain = grain.load(std::memory_order_relaxed);
		r.cluster = cluster.load(std::memory_order_relaxed);
		r.tension = tension.load(std::memory_order_relaxed);
		r.turn = turn.load(std::memory_order_relaxed);
		r.refVersion = refVersion.load(std::memory_order_relaxed);
		return r;
	}

	bool isRef(int plan) const { return plan == epoch::numTemplates(); }

	// Caller holds workerBusy (or is the constructor).
	void renderInto(int target, const Request& r) {
		epoch::Score s = (isRef(r.plan) && hasRef.load()) ? refScore : epoch::makeTemplate(std::min(r.plan, epoch::numTemplates() - 1));
		if (r.length > 0)
			s.rescale(kLengths[r.length]);
		if (std::abs(r.scale - 1.f) > 1e-4f)
			for (auto& p : s.energy.pts)
				p.value = clamp(p.value * r.scale, 0.f, 1.f);

		epoch::PlanParams pp = epoch::PlanParams::defaults();
		pp.seed = (uint32_t) std::max(1, r.seed);
		pp.grainSteps = kGrainSteps[clamp(r.grain, 0, 4)];
		pp.cluster = kClusters[clamp(r.cluster, 0, 4)];
		pp.spaceScale = r.silence;
		pp.tensionDepth = kTensions[clamp(r.tension, 0, 3)];
		pp.turnLevel = clamp(r.turn, 0, 3);
		for (int sl : {kSlotEnergy, kSlotDensity, kSlotChaos, kSlotDuck})
			pp.slots[(size_t) sl].smoothBeats += r.smooth;

		epoch::render(s, pp, slots[target].plan);
		buildView(s, slots[target].plan, slots[target].view);
	}

	// Rack: called from the widget's step() (UI thread). MetaModule: from the AsyncThread.
	void serviceRender(bool force) {
		if (workerBusy.test_and_set(std::memory_order_acquire))
			return;
		const Request r = makeRequest();
		const int64_t t = nowMs();
		if (r != settling) {
			settling = r;
			settlingSince = t;
		}
		const bool due = force || (r != rendered && t - settlingSince >= kDebounceMs);
		if (due && !pendingSwap.load(std::memory_order_acquire)) {
			const int target = 1 - active.load(std::memory_order_acquire);
			renderInto(target, r);
			rendered = r;
			pendingSwap.store(true, std::memory_order_release);
		}
		workerBusy.clear(std::memory_order_release);
	}

	void lockRef() {
		while (workerBusy.test_and_set(std::memory_order_acquire)) {
		}
	}
	void unlockRef() { workerBusy.clear(std::memory_order_release); }

#ifndef METAMODULE
	void loadReference(const char* path) {
		epoch::Analyzer* a = nullptr;
		double rate = 0.0;
		if (!measureWav(path, a, rate)) {
			delete a;
			refInfo = "could not read the file";
			return;
		}
		const double bpm = liveBpm.load() > 20.f ? liveBpm.load() : 172.0;
		const epoch::Measure m = a->finish(bpm);
		delete a;
		std::string title = system::getStem(path);
		for (auto& c : title)
			c = (char) std::toupper((unsigned char) c);
		if (title.size() > 23)
			title.resize(23);
		epoch::Score s = epoch::scoreFromMeasure(m, title, bpm);
		char line[128];
		std::snprintf(line, sizeof line, "%.1f min, %d bars @ %.0f, gaps %.1f%%, space %.1f%%", m.seconds / 60.0, s.bars, bpm,
		              m.gapShare * 100.0, m.spaceShare * 100.0);
		lockRef();
		refScore = s;
		refInfo = line;
		hasRef = true;
		refVersion++;
		planIndex = epoch::numTemplates();
		unlockRef();
	}
#endif

	// ---- state ------------------------------------------------------------------------------

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		planIndex = 0;
		seed = 1;
		grain = 2;
		cluster = 2;
		tension = 2;
		turn = 1;
		endLoop = 1;
		clockMode = 0;
		running = true;
		base = pos = 0.0;
		lastBar = -1;
		armed = false;
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "plan", json_integer(planIndex));
		json_object_set_new(root, "running", json_boolean(running));
		json_object_set_new(root, "seed", json_integer(seed));
		json_object_set_new(root, "grain", json_integer(grain));
		json_object_set_new(root, "cluster", json_integer(cluster));
		json_object_set_new(root, "tension", json_integer(tension));
		json_object_set_new(root, "turn", json_integer(turn));
		json_object_set_new(root, "loop", json_boolean(endLoop));
		json_object_set_new(root, "clock", json_integer(clockMode));
		if (hasRef) {
			lockRef();
			json_t* r = json_object();
			json_object_set_new(r, "title", json_string(refScore.title.c_str()));
			json_object_set_new(r, "info", json_string(refInfo.c_str()));
			json_object_set_new(r, "bars", json_integer(refScore.bars));
			json_object_set_new(r, "bpm", json_real(refScore.bpm));
			auto curve = [&](const epoch::Curve& c) {
				json_t* a = json_array();
				for (const auto& p : c.pts) {
					json_t* q = json_array();
					json_array_append_new(q, json_real(p.bar));
					json_array_append_new(q, json_real(p.value));
					json_array_append_new(q, json_integer((int) p.shape));
					json_array_append_new(a, q);
				}
				return a;
			};
			json_object_set_new(r, "energy", curve(refScore.energy));
			json_object_set_new(r, "space", curve(refScore.space));
			json_t* secs = json_array();
			for (const auto& s : refScore.sections) {
				json_t* q = json_array();
				json_array_append_new(q, json_real(s.bar));
				json_array_append_new(q, json_integer((int) s.role));
				json_array_append_new(q, json_string(s.name.c_str()));
				json_array_append_new(secs, q);
			}
			json_object_set_new(r, "sections", secs);
			unlockRef();
			json_object_set_new(root, "ref", r);
		}
		return root;
	}

	void dataFromJson(json_t* root) override {
		auto geti = [&](const char* k, std::atomic<int>& v, int lo, int hi) {
			if (json_t* j = json_object_get(root, k))
				v = clamp((int) json_integer_value(j), lo, hi);
		};
		geti("seed", seed, 1, 9999);
		geti("grain", grain, 0, 4);
		geti("cluster", cluster, 0, 4);
		geti("tension", tension, 0, 3);
		geti("turn", turn, 0, 3);
		geti("clock", clockMode, 0, 3);
		if (json_t* j = json_object_get(root, "running"))
			running = json_is_true(j);
		if (json_t* j = json_object_get(root, "loop"))
			endLoop = json_is_true(j) ? 1 : 0;

		if (json_t* r = json_object_get(root, "ref")) {
			epoch::Score s;
			if (json_t* j = json_object_get(r, "title"))
				s.title = json_string_value(j);
			if (json_t* j = json_object_get(r, "bars"))
				s.bars = clamp((int) json_integer_value(j), 4, epoch::kMaxBars);
			if (json_t* j = json_object_get(r, "bpm"))
				s.bpm = json_number_value(j);
			auto curve = [&](const char* k, epoch::Curve& c) {
				json_t* a = json_object_get(r, k);
				size_t i;
				json_t* q;
				json_array_foreach(a, i, q) {
					epoch::Point p;
					p.bar = json_number_value(json_array_get(q, 0));
					p.value = clamp((float) json_number_value(json_array_get(q, 1)), 0.f, 1.f);
					p.shape = (epoch::Shape) clamp((int) json_integer_value(json_array_get(q, 2)), 0, (int) epoch::Shape::kNum - 1);
					c.pts.push_back(p);
				}
				if (c.pts.empty())
					c.pts.push_back({0.0, 0.5f, epoch::Shape::Linear});
				c.sort();
			};
			curve("energy", s.energy);
			curve("space", s.space);
			size_t i;
			json_t* q;
			json_array_foreach(json_object_get(r, "sections"), i, q) {
				epoch::Section sec;
				sec.bar = json_number_value(json_array_get(q, 0));
				sec.role = (epoch::Role) clamp((int) json_integer_value(json_array_get(q, 1)), 0, (int) epoch::Role::kNum - 1);
				if (const char* n = json_string_value(json_array_get(q, 2)))
					sec.name = n;
				s.sections.push_back(sec);
			}
			s.sortSections();
			lockRef();
			refScore = s;
			if (json_t* j = json_object_get(r, "info"))
				refInfo = json_string_value(j) ? json_string_value(j) : "";
			hasRef = true;
			refVersion++;
			unlockRef();
		}
		if (json_t* j = json_object_get(root, "plan")) {
			int p = clamp((int) json_integer_value(j), 0, numPlans() - 1);
			if (isRef(p) && !hasRef)
				p = 0;
			planIndex = p;
		}
		serviceRender(true);   // not the audio thread; if a swap is still pending the worker follows
	}

	// ---- audio ------------------------------------------------------------------------------

	void process(const ProcessArgs& args) override {
		if (pendingSwap.load(std::memory_order_acquire)) {
			active.store(1 - active.load(std::memory_order_relaxed), std::memory_order_release);
			pendingSwap.store(false, std::memory_order_release);
		}
		const Slot& slot = slots[active.load(std::memory_order_relaxed)];
		const epoch::Plan& P = slot.plan;
		const int steps = std::max(1, P.steps);

		// plan button: next plan (REF only once one is loaded)
		if (planBtn.process(params[PLAN_PARAM].getValue() > 0.f)) {
			int p = planIndex + 1;
			if (p >= numPlans() || (isRef(p) && !hasRef))
				p = 0;
			planIndex = p;
		}

		// transport
		bool run = running;
		if (runBtn.process(params[RUN_PARAM].getValue() > 0.f))
			run = !run;
		if (runTrig.process(inputs[RUN_INPUT].getVoltage(), 0.1f, 1.f))
			run = !run;
		running = run;
		if (rstTrig.process(inputs[RST_INPUT].getVoltage(), 0.1f, 1.f)) {
			base = pos = 0.0;
			armed = true;
			lastBar = -1;
		}

		const bool loop = endLoop;
		const bool external = inputs[CLK_INPUT].isConnected();
		float bpm = slot.view.bpm > 0.f ? slot.view.bpm : 172.f;
		if (external) {
			const int cm = clamp((int) clockMode, 0, 3);
			sinceClk += args.sampleTime;
			if (clkTrig.process(inputs[CLK_INPUT].getVoltage(), 0.1f, 1.f)) {
				clkPeriod = sinceClk;
				sinceClk = 0.f;
				if (run) {
					if (armed)
						armed = false;
					else
						base += kClockSteps[cm];
				}
			}
			if (clkPeriod > 0.f)
				bpm = 60.f / (clkPeriod * kClockPerBeat[cm]);
			const float frac = (run && clkPeriod > 0.f && !armed) ? std::min(sinceClk / clkPeriod, 0.999f) : 0.f;
			pos = base + frac * kClockSteps[cm];
		}
		else {
			armed = false;
			if (run)
				base += args.sampleTime * bpm / 60.f * 4.f;
			pos = base;
		}
		if (pos >= steps) {
			if (loop) {
				const double wrapped = std::fmod(pos, (double) steps);
				base -= pos - wrapped;
				pos = wrapped;
			}
			else {
				pos = steps - 1e-3;
				base = std::min(base, pos);
			}
		}
		liveBpm.store(bpm, std::memory_order_relaxed);

		// the macros now
		const float vE = P.at(kSlotEnergy, pos), vD = P.at(kSlotDensity, pos);
		const float vC = P.at(kSlotChaos, pos), vK = P.at(kSlotDuck, pos);
		outputs[NRG_OUTPUT].setVoltage(10.f * vE);
		outputs[DENS_OUTPUT].setVoltage(10.f * vD);
		outputs[CHS_OUTPUT].setVoltage(10.f * vC);
		outputs[DUCK_OUTPUT].setVoltage(10.f * vK);

		// bar starts: phrase turns and sections -> EVENT
		const int bar = clamp((int) (pos / epoch::kStepsPerBar), 0, std::max(0, P.bars - 1));
		if (run && bar != lastBar) {
			lastBar = bar;
			const int lv = P.turnLevel[(size_t) bar * epoch::kStepsPerBar];
			const int minLine = kLineBars[clamp((int) turn, 0, 3)];
			float v = 0.f;
			if (lv == 5)
				v = semi::bus::turnToVolts(32);
			else if (lv >= 1 && kLineBars[lv - 1] >= minLine)
				v = semi::bus::turnToVolts(kLineBars[lv - 1]);
			if (v > 0.f) {
				eventVolts = v;
				eventPulse.trigger(semi::bus::kPulseSec);
			}
		}
		const bool ev = eventPulse.process(args.sampleTime);
		const int section = P.barSection[(size_t) bar];

		outputs[BUS_OUTPUT].setChannels(semi::bus::kChannels);
		outputs[BUS_OUTPUT].setVoltage(semi::bus::posToVolts((float) (pos / epoch::kStepsPerBar)), semi::bus::kPos);
		outputs[BUS_OUTPUT].setVoltage(10.f * vE, semi::bus::kEnergy);
		outputs[BUS_OUTPUT].setVoltage(semi::bus::sceneToVolts(clamp(section, 0, 19)), semi::bus::kScene);
		outputs[BUS_OUTPUT].setVoltage(ev ? eventVolts : 0.f, semi::bus::kEvent);

		if (ui.tick(args.sampleTime)) {
			const int p = planIndex;
			int group = 4;   // REF
			if (!isRef(p)) {
				const char* g = epoch::templateInfo(p).group;
				group = !std::strcmp(g, "DNB") ? 0 : !std::strcmp(g, "GARAGE") ? 1 : !std::strcmp(g, "MEASURED") ? 2 : 3;
			}
			for (int i = 0; i < 5; i++)
				lights[SRC_DNB_LIGHT + i].setBrightness(group == i ? 1.f : 0.f);
			lights[RUN_LIGHT].setBrightness(run ? 1.f : 0.f);

			Snapshot& s = snap.writeSlot();
			s.view = slot.view;
			s.bar = (float) (pos / epoch::kStepsPerBar);
			s.energy = vE;
			s.density = vD;
			s.section = section;
			s.bpm = bpm;
			s.running = run;
			s.external = external;
			snap.publish();
		}
	}
};

// ---- screens --------------------------------------------------------------------------------

namespace {

NVGcolor roleColor(int role) {
	switch ((epoch::Role) role) {
		case epoch::Role::Drop:
		case epoch::Role::Wall: return nvgRGB(0x5a, 0x1c, 0x3a);
		case epoch::Role::Build: return nvgRGB(0x4a, 0x3a, 0x16);
		case epoch::Role::Roll: return nvgRGB(0x16, 0x4a, 0x48);
		case epoch::Role::Break:
		case epoch::Role::Drift: return nvgRGB(0x1c, 0x2a, 0x36);
		default: return semi::theme::cyanDeep();   // INTRO, OUTRO
	}
}

// A demo snapshot for the module browser: the first factory plan, rendered once.
const Snapshot& demoSnapshot() {
	static const Snapshot demo = [] {
		Snapshot s;
		epoch::Plan* p = new epoch::Plan;
		p->allocate();
		const epoch::Score sc = epoch::makeTemplate(0);
		epoch::render(sc, epoch::PlanParams::defaults(), *p);
		buildView(sc, *p, s.view);
		s.bar = 96.f;
		const double st = 96.0 * epoch::kStepsPerBar;
		s.energy = p->at(kSlotEnergy, st);
		s.density = p->at(kSlotDensity, st);
		s.section = p->barSection[96];
		s.bpm = (float) sc.bpm;
		s.running = true;
		delete p;
		return s;
	}();
	return demo;
}

} // namespace

// The macro timeline: holes shaded, energy (cyan), density (amber, dashed), the section strip and
// the playhead. Everything but the playhead changes only when the plan changes.
struct TimelineScreen : Screen {
	Epoch* module = nullptr;

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demoSnapshot();
		const View& v = s.view;
		const float w = box.size.x, h = box.size.y;
		const float pad = 3.f;
		const float stripH = 11.f;
		const float x0 = pad, x1 = w - pad, gy0 = 13.f, gy1 = h - stripH - 2.f;
		const float gw = x1 - x0;
		const int bars = std::max(1, v.bars);
		auto X = [&](float bar) { return x0 + gw * bar / bars; };
		auto Y = [&](float val) { return gy1 - (gy1 - gy0) * val; };

		// header: plan title, clock
		text(vg, pad, 1.5f, v.title, 9.f, semi::theme::text());
		char buf[32];
		std::snprintf(buf, sizeof buf, "%s %.0f", s.external ? "EXT" : "INT", s.bpm);
		text(vg, w - pad, 1.5f, buf, 9.f, semi::theme::dim(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);

		// silence: runs of hole columns
		const float cw = gw / kCols;
		for (int c = 0; c < kCols;) {
			if (!v.hole[c]) {
				c++;
				continue;
			}
			int e = c;
			while (e < kCols && v.hole[e])
				e++;
			fillRect(vg, x0 + c * cw, gy0, (e - c) * cw, gy1 - gy0, semi::theme::magentaDeep());
			c = e;
		}
		// 32-bar grid
		for (int b = 32; b < bars; b += 32)
			line(vg, X((float) b), gy0, X((float) b), gy1, semi::theme::grid());

		// density, dashed: every other pair of segments of the decimated polyline
		float ys[kCols];
		for (int c = 0; c < kCols; c++)
			ys[c] = Y(v.density[c]);
		{
			const int step = std::max(1, (kCols + semi::platform::kMaxPolyPoints - 1) / semi::platform::kMaxPolyPoints);
			nvgBeginPath(vg);
			int k = 0;
			for (int c = 0; c + step < kCols; c += step, k++) {
				if (k % 2)
					continue;
				const float xa = x0 + gw * (c + 0.5f) / kCols, xb = x0 + gw * (c + step + 0.5f) / kCols;
				nvgMoveTo(vg, xa, ys[c]);
				nvgLineTo(vg, xb, ys[c + step]);
			}
			nvgStrokeColor(vg, semi::theme::amber());
			nvgStrokeWidth(vg, semi::platform::kThinStroke);
			nvgStroke(vg);
		}
		// energy
		for (int c = 0; c < kCols; c++)
			ys[c] = Y(v.energy[c]);
		polyline(vg, ys, kCols, x0 + 0.5f * cw, gw - cw, semi::theme::cyan());

		// section strip
		const float sy = h - stripH - 0.5f;
		for (int i = 0; i < v.nSec; i++) {
			const float a = X(v.sec[i].bar);
			const float b = i + 1 < v.nSec ? X(v.sec[i + 1].bar) : x1;
			if (b - a < 1.f)
				continue;
			fillRect(vg, a, sy, b - a - 0.8f, stripH, roleColor(v.sec[i].role));
			const float tw = std::strlen(v.sec[i].name) * 0.52f * std::max(8.f, semi::platform::kMinText);
			if (tw + 3.f < b - a)
				text(vg, a + 1.5f, sy + stripH * 0.5f, v.sec[i].name, 8.f, semi::theme::text(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		}

		// playhead
		const float px = X(clamp(s.bar, 0.f, (float) bars));
		line(vg, px, gy0 - 2.f, px, h - 0.5f, semi::theme::magenta(), semi::platform::kStroke);
		nvgBeginPath(vg);
		nvgMoveTo(vg, px - 3.f, gy0 - 4.f);
		nvgLineTo(vg, px + 3.f, gy0 - 4.f);
		nvgLineTo(vg, px, gy0);
		nvgClosePath(vg);
		nvgFillColor(vg, semi::theme::magenta());
		nvgFill(vg);
	}
};

// BAR / ENERGY / DENSITY / SCENE in a 2x2 grid. Values are rounded so frames stay identical.
struct ReadoutScreen : Screen {
	Epoch* module = nullptr;

	void drawScreen(NVGcontext* vg) override {
		const Snapshot& s = module ? module->snap.read() : demoSnapshot();
		const float w = box.size.x, h = box.size.y;
		const float cw = w / 2.f, ch = h / 2.f;
		line(vg, cw, 2.f, cw, h - 2.f, semi::theme::grid());
		line(vg, 2.f, ch, w - 2.f, ch, semi::theme::grid());

		char bar[16], nrg[8], dns[8], scn[12];
		std::snprintf(bar, sizeof bar, "%03d/%03d", std::min((int) s.bar + 1, std::max(1, s.view.bars)), s.view.bars);
		std::snprintf(nrg, sizeof nrg, "%d%%", (int) std::round(s.energy * 100.f));
		std::snprintf(dns, sizeof dns, "%d%%", (int) std::round(s.density * 100.f));
		if (s.section >= 0 && s.section < s.view.nSec)
			std::snprintf(scn, sizeof scn, "%.7s", s.view.sec[s.section].name);
		else
			std::snprintf(scn, sizeof scn, "--");

		auto cell = [&](float x, float y, const char* label, const char* val, NVGcolor c) {
			const float my = y + ch * 0.5f;
			text(vg, x + 3.f, my, label, 7.5f, semi::theme::dim(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			text(vg, x + cw - 3.f, my, val, 10.f, c, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
		};
		cell(0.f, 0.f, "BAR", bar, s.running ? semi::theme::cyan() : semi::theme::text());
		cell(cw, 0.f, "ENERGY", nrg, semi::theme::cyan());
		cell(0.f, ch, "DENSITY", dns, semi::theme::amber());
		cell(cw, ch, "SCENE", scn, semi::theme::magenta());
	}
};

struct EpochWidget : ModuleWidget {
	EpochWidget(Epoch* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Epoch.svg")));

		auto* tl = semi::createScreenMm<TimelineScreen>(Vec(L::TIMELINE.x, L::TIMELINE.y), Vec(L::TIMELINE.w, L::TIMELINE.h));
		tl->module = module;
		addChild(tl);
		auto* ro = semi::createScreenMm<ReadoutScreen>(Vec(L::READOUT.x, L::READOUT.y), Vec(L::READOUT.w, L::READOUT.h));
		ro->module = module;
		addChild(ro);

		auto at = [](L::P p) { return mm2px(Vec(p.x, p.y)); };
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::SRC_DNB), module, Epoch::SRC_DNB_LIGHT));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::SRC_UKG), module, Epoch::SRC_UKG_LIGHT));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::SRC_MSR), module, Epoch::SRC_MSR_LIGHT));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::SRC_FORM), module, Epoch::SRC_FORM_LIGHT));
		addChild(createLightCentered<semi::MagentaLedSmall>(at(L::SRC_REF), module, Epoch::SRC_REF_LIGHT));
		addParam(createParamCentered<semi::Button>(at(L::PLAN), module, Epoch::PLAN_PARAM));

		auto* len = createParamCentered<semi::KnobM>(at(L::LENGTH), module, Epoch::LENGTH_PARAM);
		len->snap = true;
		addParam(len);
		addParam(createParamCentered<semi::KnobM>(at(L::SCALE), module, Epoch::SCALE_PARAM));
		addParam(createParamCentered<semi::KnobM>(at(L::SMOOTH), module, Epoch::SMOOTH_PARAM));
		addParam(createParamCentered<semi::KnobMHot>(at(L::SILENCE), module, Epoch::SILENCE_PARAM));

		addInput(createInputCentered<semi::Jack>(at(L::CLK), module, Epoch::CLK_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RUN_IN), module, Epoch::RUN_INPUT));
		addInput(createInputCentered<semi::Jack>(at(L::RST), module, Epoch::RST_INPUT));
		addParam(createParamCentered<semi::PadCyan>(at(L::RUN), module, Epoch::RUN_PARAM));
		addChild(createLightCentered<semi::CyanLedSmall>(at(L::RUN_LED), module, Epoch::RUN_LIGHT));

		addOutput(createOutputCentered<semi::BusJack>(at(L::BUS), module, Epoch::BUS_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::NRG), module, Epoch::NRG_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::DENS), module, Epoch::DENS_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::CHS), module, Epoch::CHS_OUTPUT));
		addOutput(createOutputCentered<semi::Jack>(at(L::DUCK), module, Epoch::DUCK_OUTPUT));
	}

#ifndef METAMODULE
	// Rack: the plan is rendered here, on the UI thread (MetaModule: Epoch::worker).
	void step() override {
		if (auto* m = dynamic_cast<Epoch*>(module))
			m->serviceRender(false);
		ModuleWidget::step();
	}
#endif

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Epoch*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);

		std::vector<std::string> plans;
		for (int i = 0; i < epoch::numTemplates(); i++)
			plans.push_back(std::string(epoch::templateInfo(i).group) + "  " + epoch::templateInfo(i).name);
		plans.push_back(m->hasRef ? "REF  " + m->refScore.title : "REF  (load a WAV)");
		menu->addChild(createIndexSubmenuItem("Plan", plans,
		    [=]() { return (size_t) m->planIndex.load(); },
		    [=](size_t i) {
			    if (!m->isRef((int) i) || m->hasRef)
				    m->planIndex = (int) i;
		    }));
#ifndef METAMODULE
		menu->addChild(createMenuItem("Measure a reference (WAV)...", m->hasRef ? m->refScore.title : "", [=]() {
			osdialog_filters* filters = osdialog_filters_parse("WAV:wav,WAV");
			char* path = osdialog_file(OSDIALOG_OPEN, nullptr, nullptr, filters);
			osdialog_filters_free(filters);
			if (path) {
				m->loadReference(path);
				std::free(path);
			}
		}));
		if (!m->refInfo.empty())
			menu->addChild(createMenuLabel("REF: " + m->refInfo));
#endif

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Clock"));
		menu->addChild(createIndexSubmenuItem("CLK input", {"1/16", "1/8", "1/4 (beat)", "24 PPQN"},
		    [=]() { return (size_t) m->clockMode.load(); }, [=](size_t i) { m->clockMode = (int) i; }));
		menu->addChild(createIndexSubmenuItem("After the end", {"HOLD", "LOOP"},
		    [=]() { return (size_t) m->endLoop.load(); }, [=](size_t i) { m->endLoop = (int) i; }));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Shape"));
		menu->addChild(createMenuItem("New seed", "seed " + std::to_string(m->seed.load()), [=]() {
			m->seed = (int) (epoch::hash32((uint32_t) m->seed.load(), 0x5EEDu) % 9999u) + 1;
		}));
		menu->addChild(createIndexSubmenuItem("Hole grain", {"1 BEAT", "2 BEATS", "1 BAR", "2 BARS", "4 BARS"},
		    [=]() { return (size_t) m->grain.load(); }, [=](size_t i) { m->grain = (int) i; }));
		menu->addChild(createIndexSubmenuItem("Cluster", {"0% (scattered)", "30%", "60%", "80%", "95% (long silences)"},
		    [=]() { return (size_t) m->cluster.load(); }, [=](size_t i) { m->cluster = (int) i; }));
		menu->addChild(createIndexSubmenuItem("Tension", {"0%", "50%", "100%", "150%"},
		    [=]() { return (size_t) m->tension.load(); }, [=](size_t i) { m->tension = (int) i; }));
		menu->addChild(createIndexSubmenuItem("Turn line (EVENT)", {"4 bars", "8 bars", "16 bars", "32 bars"},
		    [=]() { return (size_t) m->turn.load(); }, [=](size_t i) { m->turn = (int) i; }));
	}
};

Model* modelEpoch = createModel<Epoch, EpochWidget>("Epoch");
