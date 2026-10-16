#pragma once
// Shared by the three break slicers (BREAKMED, BREAKSLICER, BREAKBEAT): clock follower,
// break loading (WAV, capped at 30 s, through semi::readWav), the synthetic fallback break,
// and the waveform overview + its screen. Header-only, everything in namespace slc.

#include "../plugin.hpp"
#include "../common/Sample.hpp"
#include "../common/Screen.hpp"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace slc {

constexpr double kMaxBreakSeconds = 30.0;

// ------------------------------------------------------------------ clock

// CLK (1/16 by default) + RST -> song position in quarter notes and tempo.
// The first clock after RST (or after the clock starts) is beat 0. Stops when the clock
// stops for 2.5 periods; RST re-arms beat 0 for the next clock.
struct ClockFollower {
	enum Mode { k16th = 0, k8th, kQuarter, k24ppqn, kNumModes };
	int mode = k16th;
	dsp::SchmittTrigger clkTrig, rstTrig;
	int64_t pulse = -1;      // pulses since reset, -1 = waiting for the first one
	float since = 0.f, period = 0.f;
	bool seen = false;
	bool resetEdge = false;  // set for one call when RST fired

	static const std::vector<std::string>& modeNames() {
		static const std::vector<std::string> n = {"1/16", "1/8", "1/4 (beat)", "24 PPQN"};
		return n;
	}
	double beatsPerPulse() const {
		switch (mode) {
			case k8th: return 0.5;
			case kQuarter: return 1.0;
			case k24ppqn: return 1.0 / 24.0;
			default: return 0.25;
		}
	}
	// Returns true while the clock runs; ppq / bpm valid then.
	bool process(float dt, Input& clk, Input& rst, double& ppq, double& bpm) {
		resetEdge = false;
		since += dt;
		if (rst.isConnected() && rstTrig.process(rst.getVoltage(), 0.1f, 1.f)) {
			pulse = -1;
			resetEdge = true;
		}
		if (!clk.isConnected()) {
			seen = false;
			period = 0.f;
			pulse = -1;
			return false;
		}
		if (clkTrig.process(clk.getVoltage(), 0.1f, 1.f)) {
			if (seen)
				period = since;
			seen = true;
			pulse++;
			since = 0.f;
		}
		if (period > 0.f && since > 2.5f * period) {
			seen = false;
			period = 0.f;
		}
		if (pulse < 0 || period <= 0.f)
			return false;
		const double bpp = beatsPerPulse();
		bpm = 60.0 * bpp / period;
		ppq = ((double) pulse + std::min(since / period, 0.999f)) * bpp;
		return true;
	}
};

// ------------------------------------------------------------------ breaks

// A break as the slicers load it: mono or stereo, at most 30 s.
struct Break {
	std::vector<float> l, r;   // r empty = mono
	double rate = 44100.0;
	std::string name;          // shown on the screen
	int frames() const { return (int) l.size(); }
	std::vector<float> mono() const {
		if (r.empty())
			return l;
		std::vector<float> m(l.size());
		for (size_t i = 0; i < l.size(); i++)
			m[i] = 0.5f * (l[i] + r[i]);
		return m;
	}
};

inline std::string upperBase(const std::string& path, size_t maxLen = 24) {
	std::string n = semi::fileName(path);
	size_t dot = n.find_last_of('.');
	if (dot != std::string::npos && dot > 0)
		n = n.substr(0, dot);
	for (auto& c : n)
		c = (char) std::toupper((unsigned char) c);
	return n.size() > maxLen ? n.substr(0, maxLen) : n;
}

// WAV via the shared reader; keeps two channels at most, 30 s at most. false on any error.
inline bool loadBreak(const std::string& path, Break& out, bool keepStereo) {
	semi::Sample s;
	if (!semi::readWav(path, s, (size_t) (96000.0 * kMaxBreakSeconds)))
		return false;
	if (s.frames < 64 || s.rate <= 0.0)
		return false;
	const size_t frames = std::min(s.frames, (size_t) (s.rate * kMaxBreakSeconds));
	Break b;
	b.rate = s.rate;
	b.name = upperBase(path);
	b.l.resize(frames);
	const bool st = keepStereo && s.channels > 1;
	if (st)
		b.r.resize(frames);
	for (size_t i = 0; i < frames; i++) {
		if (st) {
			b.l[i] = s.at(i, 0);
			b.r[i] = s.at(i, 1);
		}
		else {
			b.l[i] = s.mono(i);
		}
	}
	out = std::move(b);
	return true;
}

// The fallback when no file is loaded: two bars of a synthetic jungle break at 140 BPM
// (kick / snare / hats / ghosts), the same generator BREAKMED's engine tests use.
inline void synthBreak(Break& out, double sr = 44100.0) {
	Break b;
	b.rate = sr;
	b.name = "SYNTH BREAK";
	const double bpm = 140.0;
	const int step = (int) (sr * 60.0 / bpm / 4.0);
	b.l.assign((size_t) (step * 32), 0.f);
	std::mt19937 rng(7);
	std::uniform_real_distribution<float> u(-1.f, 1.f);
	const int n = (int) b.l.size();
	auto kick = [&](int at) {
		for (int i = 0; i < (int) (sr * 0.18) && at + i < n; ++i) {
			const double t = i / sr;
			const double f = 50.0 + 90.0 * std::exp(-t * 30.0);
			b.l[(size_t) (at + i)] += (float) (0.9 * std::sin(2.0 * M_PI * f * t) * std::exp(-t * 14.0) * std::min(1.0, (0.18 - t) / 0.03));
		}
	};
	auto snare = [&](int at, float amp) {
		for (int i = 0; i < (int) (sr * 0.15) && at + i < n; ++i) {
			const double t = i / sr;
			b.l[(size_t) (at + i)] += amp * (float) ((0.6 * u(rng) + 0.4 * std::sin(2.0 * M_PI * 190.0 * t)) * std::exp(-t * 22.0) * std::min(1.0, (0.15 - t) / 0.03));
		}
	};
	auto hat = [&](int at) {
		float prev = 0;
		for (int i = 0; i < (int) (sr * 0.03) && at + i < n; ++i) {
			const float x = u(rng);
			b.l[(size_t) (at + i)] += 0.12f * (x - prev) * (float) std::exp(-i / sr * 120.0);
			prev = x;
		}
	};
	static const int pattern[32] = {1, 0, 3, 0, 2, 0, 3, 4, 3, 0, 1, 0, 2, 0, 3, 0,
	                                1, 0, 3, 0, 2, 0, 3, 0, 3, 0, 1, 4, 2, 0, 3, 0};
	for (int k = 0; k < 32; ++k) {
		const int at = k * step;
		switch (pattern[k]) {
			case 1: kick(at); break;
			case 2: snare(at, 0.8f); break;
			case 3: hat(at); break;
			case 4: snare(at, 0.15f); break;
			default: break;
		}
	}
	out = std::move(b);
}

// ------------------------------------------------------------------ waveform view

// Published by the builder (one writer) and read by the wave screen.
struct WaveView {
	static constexpr int kCols = 96;
	static constexpr int kMarks = 128;
	float lo[kCols]{}, hi[kCols]{};
	float mark[kMarks]{};     // slice starts, 0..1 of the break
	uint8_t tag[kMarks]{};    // class / role per slice (module-defined, 0..3)
	int marks = 0;
	int beats = 8;            // beat grid lines
	char name[28] = "NO BREAK";
	bool valid = false;

	void setWave(const float* x, int n) {
		valid = n > 0;
		for (int c = 0; c < kCols; c++) {
			const int a = (int) ((int64_t) n * c / kCols), b = std::max(a + 1, (int) ((int64_t) n * (c + 1) / kCols));
			float mn = 0.f, mx = 0.f;
			for (int i = a; i < b && i < n; i++) {
				mn = std::min(mn, x[i]);
				mx = std::max(mx, x[i]);
			}
			lo[c] = mn;
			hi[c] = mx;
		}
	}
	void setName(const std::string& s) {
		std::snprintf(name, sizeof name, "%s", s.c_str());
	}
};

// Playheads published by the audio thread with every snapshot.
struct Heads {
	static constexpr int kMax = 4;
	float pos[kMax]{};       // 0..1 of the break, < 0 = none
	uint8_t col[kMax]{};     // colour index (module-defined)
	int n = 0;
};

// Draws the waveform as min/max columns, slice markers with tags, beat grid and playheads.
// tagLetters: one char per tag value (' ' = none); tagColors / headColors: 8 colours each.
inline void drawWave(semi::Screen& scr, NVGcontext* vg, float w, float h, const WaveView& v, const Heads& heads,
                     const char* tagLetters, const NVGcolor* tagColors, const NVGcolor* headColors,
                     int highlightSlice, const char* rightText) {
	const float pad = 2.5f;
	const float th = std::max(9.f, semi::platform::kMinText);
	const float x0 = pad, x1 = w - pad, y0 = pad + th + 1.5f, y1 = h - pad;
	const float mid = 0.5f * (y0 + y1), amp = 0.5f * (y1 - y0) - 1.f;
	auto X = [&](float u) { return x0 + (x1 - x0) * u; };

	// beat grid
	if (v.beats > 0 && v.beats <= 64)
		for (int b = 1; b < v.beats; b++)
			semi::Screen::line(vg, X((float) b / v.beats), y0, X((float) b / v.beats), y1,
			                   b % 4 == 0 ? semi::theme::cyanDeep() : semi::theme::grid());

	// highlighted slice (the one playing)
	if (highlightSlice >= 0 && highlightSlice < v.marks) {
		const float a = v.mark[highlightSlice], b = highlightSlice + 1 < v.marks ? v.mark[highlightSlice + 1] : 1.f;
		semi::Screen::fillRect(vg, X(a), y0, std::max(1.f, X(b) - X(a)), y1 - y0, semi::theme::cyanDeep());
	}

	// waveform: one filled path (min/max envelope)
	if (v.valid) {
		nvgBeginPath(vg);
		const float cw = (x1 - x0) / WaveView::kCols;
		for (int c = 0; c < WaveView::kCols; c++) {
			const float top = mid - std::min(1.f, v.hi[c]) * amp, bot = mid - std::max(-1.f, v.lo[c]) * amp;
			nvgRect(vg, x0 + c * cw, top, std::max(1.f, cw - 0.4f), std::max(1.f, bot - top));
		}
		nvgFillColor(vg, semi::theme::cyanDim());
		nvgFill(vg);
	}

	// slice markers + tags
	for (int i = 0; i < v.marks; i++) {
		const float x = X(v.mark[i]);
		const int t = v.tag[i] & 7;
		semi::Screen::line(vg, x, y0, x, y1, i == highlightSlice ? semi::theme::cyan() : semi::theme::dim());
		if (tagLetters && v.marks <= 40) {
			char s[2] = {tagLetters[t], 0};
			if (s[0] != ' ')
				scr.text(vg, x + 1.f, y0, s, 9.f, tagColors[t], NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
		}
	}

	// playheads
	for (int k = 0; k < heads.n && k < Heads::kMax; k++) {
		if (heads.pos[k] < 0.f)
			continue;
		const float x = X(std::min(1.f, heads.pos[k]));
		semi::Screen::line(vg, x, y0, x, y1, headColors[heads.col[k] & 7], semi::platform::kStroke);
	}

	// header: name left, info right
	scr.text(vg, pad, pad - 0.5f, v.name, th, semi::theme::text());
	if (rightText)
		scr.text(vg, w - pad, pad - 0.5f, rightText, th, semi::theme::amber(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
}

} // namespace slc
