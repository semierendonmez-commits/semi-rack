// Offline check of the vendored BREAKSLICER engine as the Rack module drives it
// (32-sample blocks, clock -> ppq, a synthetic break, kits swapped while playing).
// Not part of the plugin build.
//   g++ -std=c++17 -O2 -Iengines tests/breakslicer_engine.cpp engines/breakslicer/Analysis.cpp engines/breakslicer/Kit.cpp engines/breakslicer/Engine.cpp -o build/breakslicer_test
//   ./build/breakslicer_test
#include "breakslicer/Engine.h"
#include <cmath>
#include <cstdio>
#include <random>

using namespace gtbs;

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static std::shared_ptr<Sample> makeBreak(double sr) {
	const int step = (int) (sr * 60.0 / 140.0 / 4.0);
	auto s = std::make_shared<Sample>();
	s->name = "SYNTH";
	s->rate = sr;
	s->l.assign((size_t) step * 32, 0.f);
	std::mt19937 rng(7);
	std::uniform_real_distribution<float> u(-1.f, 1.f);
	const int n = (int) s->l.size();
	static const int pat[32] = {1, 0, 3, 0, 2, 0, 3, 4, 3, 0, 1, 0, 2, 0, 3, 0, 1, 0, 3, 0, 2, 0, 3, 0, 3, 0, 1, 4, 2, 0, 3, 0};
	for (int k = 0; k < 32; k++) {
		const int at = k * step;
		const int len = (int) (sr * (pat[k] == 1 ? 0.18 : pat[k] == 3 ? 0.03 : 0.15));
		for (int i = 0; i < len && at + i < n && pat[k]; i++) {
			const double t = i / sr;
			float v = 0.f;
			if (pat[k] == 1) v = (float) (0.9 * std::sin(2 * M_PI * (50 + 90 * std::exp(-t * 30)) * t) * std::exp(-t * 14));
			else if (pat[k] == 3) v = 0.12f * u(rng) * (float) std::exp(-t * 120);
			else v = (pat[k] == 2 ? 0.8f : 0.15f) * (float) ((0.6 * u(rng) + 0.4 * std::sin(2 * M_PI * 190 * t)) * std::exp(-t * 22));
			s->l[(size_t) (at + i)] += v;
		}
	}
	s->frames = n;
	s->serial = 1;
	return s;
}

struct Run {
	float peak = 0.f;
	double energy = 0.0;
	bool finite = true;
	int hits = 0, natural = 0, cls[4] = {}, rows = 0;
};

static Run play(Engine& e, const GenParams& gp, const PlayParams& pp, double sr, double bpm, long& sample, int blocks,
                const Kit* swapTo = nullptr, int swapAt = -1) {
	Run r;
	constexpr int B = 32;
	float L[B], R[B];
	int lastRow = -1;
	for (int b = 0; b < blocks; b++) {
		if (b == swapAt && swapTo) e.setKit(swapTo);
		Transport t;
		t.valid = t.playing = true;
		t.bpm = bpm;
		t.ppq = sample * bpm / 60.0 / sr;
		e.process(L, R, B, t, gp, pp, nullptr, 0);
		sample += B;
		for (int i = 0; i < B; i++) {
			r.finite = r.finite && std::isfinite(L[i]) && std::isfinite(R[i]);
			r.peak = std::max(r.peak, std::max(std::abs(L[i]), std::abs(R[i])));
			r.energy += (double) L[i] * L[i];
		}
		for (int h = 0; h < e.numHits(); h++) {
			const HitEvent& x = e.hit(h);
			if (x.repeat) continue;
			r.hits++;
			r.natural += x.natural;
			r.cls[x.cls & 3]++;
		}
		const int row = e.viewRow.load();
		if (row != lastRow) r.rows++;
		lastRow = row;
	}
	return r;
}

int main() {
	const double SR = 48000.0;
	auto raw = makeBreak(44100.0);
	AnalysisParams ap;
	auto an = std::make_shared<Sample>(*raw);
	an->an = analyse(*raw, ap);
	CHECK(an->an.peakFrames.size() >= 12, "onsets %zu", an->an.peakFrames.size());
	std::shared_ptr<const Sample> s = an;

	MachineParams clean;
	clean.machine = Clean16;
	MachineParams s950;
	s950.machine = AkaiS950;
	s950.rateKHz = 22.05f;
	MachineParams paula;
	paula.machine = AmigaPaula;
	paula.rateKHz = 16.57f;
	auto k1 = buildKit(s, SliceOnsets, 0, clean, 0.0);
	auto k2 = buildKit(s, SliceOnsets, 0, s950, 0.0);
	auto k3 = buildKit(s, SliceGrid16, 2, paula, 0.0);
	CHECK(k1->bars == 2, "auto bars %d", k1->bars);
	CHECK(k1->numSlices() >= 12, "slices %d", k1->numSlices());
	CHECK(k2->mb.frames > 0 && k2->mb.frames < k1->mb.frames, "s950 buffer %d vs %d", k2->mb.frames, k1->mb.frames);
	CHECK(k3->numSlices() == 16, "grid16 slices %d", k3->numSlices());
	int kicks = 0, snares = 0;
	for (auto c : k1->slices.cls) { kicks += c == Kick; snares += c == Snare; }
	CHECK(kicks >= 2 && snares >= 2, "classes K%d S%d", kicks, snares);

	Engine e;
	e.prepare(SR, 32);
	e.setKit(k1.get());
	GenParams gp;
	PlayParams pp;
	long sample = 0;
	const int bars4 = (int) (SR * 60.0 / 170.0 * 16.0 / 32.0);

	// CHOP 0: the break's own hits only
	gp.complexity = 0.f;
	gp.retrig = gp.drill = 0.f;
	gp.fillEvery = 0;
	Run a = play(e, gp, pp, SR, 170.0, sample, bars4);
	CHECK(a.finite, "non-finite");
	CHECK(a.peak > 0.05f && a.peak <= 1.0f, "peak %f", a.peak);
	CHECK(a.hits > 8 && a.natural == a.hits, "chop 0: %d hits, %d natural", a.hits, a.natural);
	CHECK(a.rows >= 60, "rows %d", a.rows);   // 4 bars of 1/16 rows

	// CHOP 1 + rolls + drills + fills, swapping sampler kits while playing
	GenParams g2;
	g2.complexity = 1.f;
	g2.retrig = 0.4f;
	g2.drill = 0.3f;
	g2.fillEvery = 1;
	g2.fill = 1.f;
	g2.mutate = 0.4f;
	Run b = play(e, g2, pp, SR, 170.0, sample, bars4, k2.get(), bars4 / 3);
	CHECK(b.finite, "non-finite (chop 1)");
	CHECK(b.peak > 0.05f && b.peak <= 1.0f, "peak %f (soft limiter keeps it under 0 dBFS)", b.peak);
	CHECK(b.natural < b.hits, "chop 1: %d natural of %d", b.natural, b.hits);
	std::printf("chop 1: hits %d natural %d  K%d S%d G%d H%d  peak %.3f\n", b.hits, b.natural, b.cls[0], b.cls[1], b.cls[2], b.cls[3], b.peak);

	Run c = play(e, g2, pp, SR, 140.0, sample, bars4, k3.get(), 10);
	CHECK(c.finite && c.peak > 0.05f && c.energy > 1.0, "paula grid kit: peak %f", c.peak);

	// the pattern view is published
	PatternView pv;
	CHECK(e.patternOut.read(pv) && pv.rows == 32, "pattern view rows %d", pv.rows);

	// a stopped transport goes quiet
	{
		constexpr int B = 32;
		float L[B], R[B];
		Transport t;
		float tail = 0.f;
		for (int k = 0; k < 400; k++) {
			e.process(L, R, B, t, g2, pp, nullptr, 0);
			if (k > 300) for (int i = 0; i < B; i++) tail = std::max(tail, std::abs(L[i]));
		}
		CHECK(tail < 1e-3f, "stopped tail %f", tail);
	}

	std::printf("%d / %d checks passed\n", checks - fails, checks);
	return fails ? 1 : 0;
}
