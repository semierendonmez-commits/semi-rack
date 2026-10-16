#pragma once
// Shared by BREAKDNA, GENOTYPE and LOOPGENOME (one engine lineage, engines/breakdna +
// engines/genotype + engines/loopgenome, all in namespace dnafam).
//
//   * loadBreak(): WAV -> dna::Sample (capped) -> onset analysis. Runs inside a semi::Loader
//     builder, i.e. off the audio thread on both platforms.
//   * WaveView / GenomeView: small, fixed-size pictures of a break for the screens, made
//     once per load (never touch the kit from the GUI).
//   * ClockPos: CLK (1/16 by default) + RST -> ppq / bpm, as the plugins get from the host.
//   * TrigOut: MIDI-like note events at sample offsets -> trigger / gate outputs.
//   * Paths: the file paths the loader thread reads (spinlock; never the audio thread).

#include "../plugin.hpp"
#include "../common/Sample.hpp"
#include "../common/Screen.hpp"
#include "breakdna/Dna.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>

namespace dnamod {

using namespace dnafam;

#ifdef METAMODULE
constexpr double kMaxBreakSec = 12.0;   // the kit holds ~12 floats per frame: keep MetaModule RAM sane (~300 MB for all plugins)
#else
constexpr double kMaxBreakSec = 30.0;
#endif

inline int64_t nowMs() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// WAV -> analysed break. false: unreadable / too short.
inline bool loadBreak(const std::string& path, std::shared_ptr<const dna::Sample>& out) {
	semi::Sample s;
	if (!semi::readWav(path, s, (size_t) (kMaxBreakSec * 48000.0)))
		return false;
	const size_t cap = (size_t) (kMaxBreakSec * s.rate);
	const size_t frames = std::min(s.frames, cap);
	if (frames < 2048 || s.rate < 8000.0)
		return false;
	auto d = std::make_shared<dna::Sample>();
	d->name = semi::fileName(path);
	d->rate = s.rate;
	d->frames = (int) frames;
	d->l.resize(frames);
	if (s.channels > 1)
		d->r.resize(frames);
	for (size_t i = 0; i < frames; i++) {
		d->l[i] = s.at(i, 0);
		if (s.channels > 1)
			d->r[i] = s.at(i, 1);
	}
	s.data.clear();
	s.data.shrink_to_fit();
	d->an = gtbs::analyse(*d, gtbs::AnalysisParams{});
	out = d;
	return true;
}

// Mono WAV (LOOPGENOME's bass loop).
inline bool loadMono(const std::string& path, std::vector<float>& mono, double& rate) {
	semi::Sample s;
	if (!semi::readWav(path, s, (size_t) (16.0 * 48000.0)))
		return false;
	if (s.frames < 1024)
		return false;
	mono.resize(s.frames);
	for (size_t i = 0; i < s.frames; i++)
		mono[i] = s.mono(i);
	rate = s.rate;
	return true;
}

// ------------------------------------------------------------------ views

constexpr int kWaveCols = 64;
constexpr int kMaxMarks = 96;

struct WaveView {
	bool valid = false;
	uint8_t peak[kWaveCols]{};       // |x| per column, 0..255
	int nMarks = 0;
	uint16_t mark[kMaxMarks]{};      // slice starts, 0..65535 of the break
	uint8_t cls[kMaxMarks]{};        // class (K S G H) or lane
	uint8_t lit[kMaxMarks]{};        // 1: drawn bright (B: answers something in A)
	int bars = 0;
	float bpm = 0.f;
	char name[28]{};
};

inline void makeWave(const dna::Sample& s, const std::vector<int>& starts, const std::vector<uint8_t>& cls, WaveView& w) {
	w = WaveView{};
	w.valid = s.frames > 0;
	if (!w.valid)
		return;
	float peak[kWaveCols]{};
	float top = 1e-6f;
	for (int i = 0; i < s.frames; i++) {
		const int c = std::min(kWaveCols - 1, (int) ((int64_t) i * kWaveCols / s.frames));
		float a = std::fabs(s.l[(size_t) i]);
		if (!s.r.empty())
			a = std::max(a, std::fabs(s.r[(size_t) i]));
		peak[c] = std::max(peak[c], a);
		top = std::max(top, a);
	}
	for (int c = 0; c < kWaveCols; c++)
		w.peak[c] = (uint8_t) clamp((int) std::lround(255.f * std::sqrt(peak[c] / top)), 0, 255);
	w.nMarks = std::min((int) starts.size(), kMaxMarks);
	for (int i = 0; i < w.nMarks; i++) {
		w.mark[i] = (uint16_t) clamp((int) ((int64_t) starts[(size_t) i] * 65535 / s.frames), 0, 65535);
		w.cls[i] = i < (int) cls.size() ? cls[(size_t) i] : 0;
		w.lit[i] = 1;
	}
	std::snprintf(w.name, sizeof w.name, "%s", s.name.c_str());
}

// A's genome: per 16th, the hits by class (level) and the main hit's micro-timing.
struct GenomeView {
	int steps = 0;
	uint8_t level[4][dna::kMaxSteps]{};   // 0 none, else 1..255 (K S G H)
	int8_t offset[dna::kMaxSteps]{};      // main hit's offset, 1/100 of a step (+ late)
	float swing = 0.f;
};

inline void makeGenome(const dna::Genome& g, GenomeView& v) {
	v = GenomeView{};
	v.steps = std::min(g.steps, dna::kMaxSteps);
	for (const auto& h : g.hits) {
		if (h.step < 0 || h.step >= v.steps)
			continue;
		const uint8_t l = (uint8_t) clamp((int) std::lround(1 + 254 * h.level), 1, 255);
		uint8_t& cell = v.level[h.cls & 3][h.step];
		cell = std::max(cell, l);
	}
	for (int s = 0; s < v.steps; s++) {
		const int m = g.main[(size_t) s];
		if (m >= 0 && m < (int) g.hits.size())
			v.offset[s] = (int8_t) clamp((int) std::lround(g.hits[(size_t) m].offset * 100.f), -100, 100);
	}
	v.swing = g.swing;
}

// ------------------------------------------------------------------ colours

inline NVGcolor classColor(int c) {
	switch (c & 3) {
		case 0: return semi::theme::magenta();   // KICK
		case 1: return semi::theme::cyan();      // SNARE
		case 2: return semi::theme::cyanDim();   // GHOST
		default: return semi::theme::amber();    // HAT
	}
}

// Waveform columns + slice markers (coloured by class), the lit slice as a magenta frame.
inline void drawWave(semi::Screen& sc, NVGcontext* vg, const WaveView& w, float x, float y, float wd, float ht,
                     int litSlice, bool dim, NVGcolor (*colorOf)(int) = classColor) {
	if (!w.valid) {
		sc.text(vg, x + wd * 0.5f, y + ht * 0.5f, "NO BREAK", 9.f, semi::theme::dim(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		return;
	}
	const float cw = wd / kWaveCols;
	const float mid = y + ht * 0.5f;
	for (int c = 0; c < kWaveCols; c++) {
		const float a = w.peak[c] / 255.f * ht * 0.48f;
		if (a < 0.4f)
			continue;
		semi::Screen::fillRect(vg, x + c * cw, mid - a, std::max(1.f, cw - 0.6f), 2.f * a,
		                       dim ? semi::theme::cyanDeep() : semi::theme::cyanDim());
	}
	for (int i = 0; i < w.nMarks; i++) {
		const float mx = x + wd * w.mark[i] / 65535.f;
		const NVGcolor c = w.lit[i] ? colorOf(w.cls[i]) : semi::theme::grid();
		semi::Screen::fillRect(vg, mx, y, std::max(1.f, semi::platform::kThinStroke * 0.8f), ht * 0.22f, c);
	}
	if (litSlice >= 0 && litSlice < w.nMarks) {
		const float x0 = x + wd * w.mark[litSlice] / 65535.f;
		const float x1 = litSlice + 1 < w.nMarks ? x + wd * w.mark[litSlice + 1] / 65535.f : x + wd;
		nvgBeginPath(vg);
		nvgRect(vg, x0, y + 0.5f, std::max(2.f, x1 - x0), ht - 1.f);
		nvgStrokeColor(vg, semi::theme::magenta());
		nvgStrokeWidth(vg, semi::platform::kThinStroke);
		nvgStroke(vg);
	}
}

// ------------------------------------------------------------------ clock

// CLK pulses (PPQN from the menu, 1/16 = 4 by default) + RST -> ppq and bpm.
// The first pulse after RST (or after the clock starts) is beat 1. Stops after 2.5 periods
// without a pulse.
constexpr int kNumPpqn = 4;
inline int ppqnOf(int idx) {
	static const int v[kNumPpqn] = {4, 2, 1, 24};
	return v[clamp(idx, 0, kNumPpqn - 1)];
}
inline std::vector<std::string> ppqnNames() { return {"1/16 (4 PPQN)", "1/8 (2 PPQN)", "1/4 (1 PPQN)", "24 PPQN"}; }

struct ClockPos {
	dsp::SchmittTrigger clk, rst;
	int64_t pulses = -1;
	float since = 0.f, period = 0.f;
	bool seen = false;
	bool resetFired = false;

	// Call once per sample. Returns true while the clock runs (ppq / bpm valid).
	bool process(float dt, bool connected, float vClk, float vRst, int ppqn, double& ppq, double& bpm) {
		resetFired = false;
		since += dt;
		if (rst.process(vRst, 0.1f, 1.f)) {
			pulses = -1;
			resetFired = true;
		}
		if (!connected) {
			seen = false;
			period = 0.f;
			return false;
		}
		if (clk.process(vClk, 0.1f, 1.f)) {
			if (seen)
				period = since;
			seen = true;
			pulses++;
			since = 0.f;
		}
		if (period > 0.f && since > 2.5f * period) {
			seen = false;
			period = 0.f;
		}
		if (pulses < 0 || period <= 0.f)
			return false;
		bpm = 60.0 / ((double) ppqn * period);
		ppq = ((double) pulses + std::min(since / period, 0.999f)) / ppqn;
		return true;
	}
};

constexpr int kNumTempos = 6;
inline std::vector<std::string> tempoNames() { return {"The break's own", "120", "140", "160", "170", "174"}; }
inline double tempoOf(int idx, double own) {
	static const double v[kNumTempos] = {0.0, 120.0, 140.0, 160.0, 170.0, 174.0};
	const double t = v[clamp(idx, 0, kNumTempos - 1)];
	return t > 0.0 ? t : (own > 40.0 ? own : 120.0);
}

// ------------------------------------------------------------------ outputs

// Note events of one engine block -> trigger (5 ms) / gate outputs, played back one block
// later in step with the audio.
template <int kMax>
struct OutEvents {
	struct E { int at; int8_t out; int8_t on; float v; };
	E ev[kMax];
	int n = 0;
	void clear() { n = 0; }
	void add(int at, int out, bool on, float v) {
		if (n < kMax)
			ev[n++] = {at, (int8_t) out, (int8_t) (on ? 1 : 0), v};
	}
};

// ------------------------------------------------------------------ misc

struct Spin {
	std::atomic_flag f = ATOMIC_FLAG_INIT;
	void lock() {
		while (f.test_and_set(std::memory_order_acquire)) {
		}
	}
	void unlock() { f.clear(std::memory_order_release); }
};

// Paths the loader thread syncs to (the UI / dataFromJson write them).
template <int N>
struct Paths {
	Spin spin;
	std::string want[N];
	void set(int i, const std::string& p) {
		spin.lock();
		want[i] = p;
		spin.unlock();
	}
	std::string get(int i) {
		spin.lock();
		std::string p = want[i];
		spin.unlock();
		return p;
	}
};

inline std::vector<std::string> stepLabels(float lo, float hi, int n, float mult, const char* unit) {
	std::vector<std::string> v;
	for (int i = 0; i < n; i++)
		v.push_back(string::f("%.3g%s", (lo + (hi - lo) * i / (n - 1)) * mult, unit));
	return v;
}

// A float setting as a fixed-step submenu (works on MetaModule: no sliders, no text entry).
inline MenuItem* floatMenu(const std::string& label, float* v, float lo, float hi, int n, float mult, const char* unit,
                           std::function<void()> changed = nullptr) {
	return createIndexSubmenuItem(label, stepLabels(lo, hi, n, mult, unit),
		[=]() { return (size_t) clamp((int) std::round((*v - lo) / (hi - lo) * (n - 1)), 0, n - 1); },
		[=](size_t i) {
			*v = lo + (hi - lo) * i / (n - 1);
			if (changed)
				changed();
		});
}

inline MenuItem* intMenu(const std::string& label, const std::vector<std::string>& names, int* v,
                         std::function<void()> changed = nullptr) {
	return createIndexSubmenuItem(label, names, [=]() { return (size_t) *v; },
		[=](size_t i) {
			*v = (int) i;
			if (changed)
				changed();
		});
}

inline void jsonInt(json_t* root, const char* key, int& v, int lo, int hi) {
	if (json_t* j = json_object_get(root, key))
		v = clamp((int) json_integer_value(j), lo, hi);
}
inline void jsonFloat(json_t* root, const char* key, float& v, float lo, float hi) {
	if (json_t* j = json_object_get(root, key))
		v = clamp((float) json_number_value(j), lo, hi);
}
inline void jsonBool(json_t* root, const char* key, bool& v) {
	if (json_t* j = json_object_get(root, key))
		v = json_is_true(j);
}
inline std::string jsonStr(json_t* root, const char* key) {
	json_t* j = json_object_get(root, key);
	return j && json_is_string(j) ? std::string(json_string_value(j)) : std::string();
}

} // namespace dnamod
