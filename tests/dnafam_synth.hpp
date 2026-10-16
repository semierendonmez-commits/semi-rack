// Synthetic breaks for the BREAKDNA / GENOTYPE / LOOPGENOME engine tests (from the plugins'
// own tests/EngineTests.cpp: drum sounds at known steps and micro-timing offsets).
#pragma once
#include "breakdna/Dna.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <vector>

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

namespace synth {
using namespace dnafam;
constexpr double kPi = 3.14159265358979323846;

struct Placed { int step; int cls; float offsetMs; float level; };

inline void addDrum(std::vector<float>& x, double rate, int at, int cls, float level, uint32_t seed, float pitch = 1.f) {
	std::mt19937 rng(seed);
	std::uniform_real_distribution<float> u(-1.0f, 1.0f);
	const int len = (int) (rate * (cls == gtbs::Hat ? 0.05 : 0.16));
	float lp = 0.0f, hp1 = 0.0f, hpz = 0.0f;
	double ph = 0.0;
	for (int i = 0; i < len && at + i < (int) x.size(); ++i) {
		const double t = i / rate;
		float v = 0.0f;
		if (cls == gtbs::Kick) {
			const double f = (50.0 + 90.0 * std::exp(-t * 30.0)) * pitch;
			ph += 2.0 * kPi * f / rate;
			v = (float) (std::sin(ph) * std::exp(-t * 14.0));
		}
		else if (cls == gtbs::Snare || cls == gtbs::Ghost) {
			const float n = u(rng);
			lp += 0.35f * (n - lp);
			v = (float) (0.55 * std::sin(2.0 * kPi * 190.0 * pitch * t) * std::exp(-t * 25.0) + 0.8 * lp * std::exp(-t * 18.0));
		}
		else {
			const float n = u(rng);
			const float h = n - hp1;
			hp1 = n;
			hpz = 0.6f * hpz + 0.4f * h;
			v = (float) (0.7 * (h - 0.3f * hpz) * std::exp(-t * 60.0));
		}
		const float tail = i > len - (int) (rate * 0.01) ? (float) (len - i) / (float) (rate * 0.01) : 1.0f;
		x[(size_t) (at + i)] += level * v * tail;
	}
}

inline std::shared_ptr<dna::Sample> makeBreak(const std::vector<Placed>& hits, double bpm, double rate, double leadMs,
                                              uint32_t seed, float pitch = 1.f) {
	auto s = std::make_shared<dna::Sample>();
	s->rate = rate;
	const double sps = 60.0 / bpm * rate / 4.0;
	s->frames = (int) std::lround(32 * sps);
	s->l.assign((size_t) s->frames, 0.0f);
	for (size_t i = 0; i < hits.size(); ++i) {
		const auto& h = hits[i];
		const int at = (int) std::lround(h.step * sps + (h.offsetMs + leadMs) * 0.001 * rate);
		addDrum(s->l, rate, std::clamp(at, 0, s->frames - 1), h.cls, h.level, seed + (uint32_t) i * 13u, pitch);
	}
	s->name = "synthetic";
	s->an = gtbs::analyse(*s, gtbs::AnalysisParams{});
	return s;
}

inline std::vector<Placed> amenish() {
	using namespace gtbs;
	return {
		{0, Kick, 0.0f, 1.0f}, {2, Kick, 3.0f, 0.9f}, {4, Snare, 6.0f, 1.0f}, {7, Ghost, 9.0f, 0.3f},
		{9, Ghost, -4.0f, 0.28f}, {10, Kick, 2.0f, 0.95f}, {12, Snare, 5.0f, 1.0f}, {14, Hat, -3.0f, 0.5f},
		{16, Kick, 0.0f, 1.0f}, {18, Kick, 4.0f, 0.9f}, {20, Snare, 7.0f, 1.0f}, {23, Ghost, 10.0f, 0.3f},
		{26, Kick, 1.0f, 0.95f}, {28, Snare, 4.0f, 1.0f}, {30, Hat, -2.0f, 0.5f}, {31, Ghost, 8.0f, 0.25f},
	};
}

// a straighter second break (the skin), other sounds
inline std::vector<Placed> straight() {
	using namespace gtbs;
	std::vector<Placed> v;
	for (int s = 0; s < 32; s += 2)
		v.push_back({s, s % 8 == 0 ? Kick : s % 8 == 4 ? Snare : Hat, 0.f, s % 8 == 0 || s % 8 == 4 ? 1.f : 0.5f});
	return v;
}

struct Stats {
	double peak = 0, sumSq = 0;
	long n = 0;
	bool finite = true;
	void add(const float* x, int len) {
		for (int i = 0; i < len; i++) {
			if (!std::isfinite(x[i]))
				finite = false;
			peak = std::max(peak, (double) std::fabs(x[i]));
			sumSq += (double) x[i] * x[i];
		}
		n += len;
	}
	double rms() const { return n ? std::sqrt(sumSq / n) : 0.0; }
};

} // namespace synth
