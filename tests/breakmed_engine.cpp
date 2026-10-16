// Offline check of the vendored BREAKMED engine as the Rack module drives it
// (32-sample blocks, 1/16 clock -> ppq, the built-in player, a synthetic break).
// Not part of the plugin build.
//   g++ -std=c++17 -O2 -Iengines tests/breakmed_engine.cpp engines/breakmed/Dsp.cpp engines/breakmed/Engine.cpp -o build/breakmed_test
//   ./build/breakmed_test
#include "breakmed/Engine.h"
#include <cmath>
#include <cstdio>
#include <random>

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

// two bars at 140 BPM (the module's synthetic fallback, same generator)
static std::vector<float> makeBreak(double sr) {
	const int step = (int) (sr * 60.0 / 140.0 / 4.0);
	std::vector<float> x((size_t) step * 32, 0.f);
	std::mt19937 rng(7);
	std::uniform_real_distribution<float> u(-1.f, 1.f);
	const int n = (int) x.size();
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
			x[(size_t) (at + i)] += v;
		}
	}
	return x;
}

struct Run {
	float peak = 0.f;
	double energy = 0.0;
	bool finite = true;
	uint32_t hits = 0;
	int chains[bm::kChains] = {};
	std::vector<int> firstChains;
};

static Run play(bm::Engine& e, const bm::Params& p, double sr, double bpm, int blocks) {
	Run r;
	constexpr int B = 32;
	float L[B], R[B];
	std::vector<bm::Event> ev;
	const uint32_t s0 = e.logSerial.load();
	for (int b = 0; b < blocks; b++) {
		bm::Transport t;
		t.valid = t.playing = true;
		t.bpm = bpm;
		t.ppq = b * B * bpm / 60.0 / sr;
		e.process(L, R, B, ev, p, t);
		for (int i = 0; i < B; i++) {
			r.finite = r.finite && std::isfinite(L[i]) && std::isfinite(R[i]);
			r.peak = std::max(r.peak, std::max(std::abs(L[i]), std::abs(R[i])));
			r.energy += (double) L[i] * L[i];
		}
	}
	const uint32_t s1 = e.logSerial.load();
	r.hits = s1 - s0;
	for (uint32_t k = s1 - std::min<uint32_t>(r.hits, bm::Engine::kLog - 1) + 1; k <= s1; k++) {
		r.chains[e.log[k % bm::Engine::kLog].chain]++;
		r.firstChains.push_back(e.log[k % bm::Engine::kLog].chain);
	}
	return r;
}

int main() {
	const double SR = 48000.0, srcRate = 44100.0;
	auto x = makeBreak(srcRate);
	const int n = (int) x.size();
	float pk = 0.f;
	for (float v : x) pk = std::max(pk, std::abs(v));
	for (auto& v : x) v *= 0.98f / pk;

	auto on = bm::detectOnsets(x.data(), n, srcRate);
	CHECK(on.pos.size() >= 12, "onsets %zu", on.pos.size());
	const int bars = bm::autoBars((double) n / srcRate);
	CHECK(bars == 2, "auto bars %d", bars);
	auto starts = bm::transientSlices(on, n, srcRate, 12);
	CHECK(starts.size() >= 8 && starts.size() <= 32, "slices %zu", starts.size());
	auto roles = bm::classify(x.data(), n, srcRate, starts);
	int kicks = 0;
	for (int r : roles) kicks += r == bm::RoleKick;
	CHECK(kicks >= 2, "kicks %d", kicks);

	bm::PrepareSpec spec;
	spec.mono = x.data();
	spec.n = n;
	spec.srcRate = srcRate;
	spec.starts = starts;
	spec.roles = roles;
	spec.bars = bars;
	spec.rateIndex = bm::defaultRateIndex();
	spec.bits = 8;
	spec.space = 0.6f;
	auto prep = bm::prepare(spec);
	CHECK(prep->numSlices() == (int) starts.size(), "prepared slices %d", prep->numSlices());
	CHECK(std::abs(prep->rate - 22168.0) < 1.0, "paula rate %f", prep->rate);

	bm::Engine e;
	e.captureSeconds = 0.0;   // as the module
	e.prepareToPlay(SR, 32);
	e.setPrepared(prep.get());

	bm::Params p;
	p.play = true;
	p.syncHost = true;
	p.volume = 56.f;
	const int blocks = (int) (SR * 60.0 / 170.0 * 16.0 / 32.0);   // 4 bars at 170
	Run a = play(e, p, SR, 170.0, blocks);
	CHECK(a.finite, "non-finite output");
	CHECK(a.peak > 0.05f && a.peak < 4.f, "peak %f", a.peak);
	CHECK(a.energy > 1.0, "energy %f", a.energy);
	// the player hits every slice twice in 4 bars of a 2-bar loop
	CHECK(a.hits >= (uint32_t) (2 * prep->numSlices() - 2), "hits %u (slices %d)", a.hits, prep->numSlices());
	std::printf("free:  peak %.3f  hits %u  chains", a.peak, a.hits);
	for (int c = 0; c < bm::kChains; c++) std::printf(" %s=%d", bm::chainName(c), a.chains[c]);
	std::printf("\n");
	CHECK(a.chains[0] > 0 && a.chains[0] < (int) a.hits, "base %d of %u", a.chains[0], a.hits);

	// all weight on BASE: only BASE
	bm::Params pb = p;
	pb.weight = {100, 0, 0, 0, 0, 0, 0};
	Run b = play(e, pb, SR, 170.0, blocks / 2);
	CHECK(b.chains[0] == (int) std::min<uint32_t>(b.hits, bm::Engine::kLog - 1), "base-only gave %d of %u", b.chains[0], b.hits);

	// every chain alone renders finite, bounded audio
	for (int c = 1; c < bm::kChains; c++) {
		bm::Params pc = p;
		pc.weight = {0, 0, 0, 0, 0, 0, 0};
		pc.weight[(size_t) c] = 100;
		pc.memory = 0.f;
		Run r = play(e, pc, SR, 170.0, blocks / 2);
		CHECK(r.finite && r.peak > 0.01f && r.peak < 4.f, "chain %s peak %f", bm::chainName(c), r.peak);
	}

	// LOCK: the same seed at the same place gives the same chains on the next pass
	bm::Params pl = p;
	pl.lock = true;
	pl.lockBars = 2;
	pl.memory = 0.f;
	bm::Engine e2;
	e2.captureSeconds = 0.0;
	e2.prepareToPlay(SR, 32);
	e2.setPrepared(prep.get());
	const int twoBars = (int) (SR * 60.0 / 170.0 * 8.0 / 32.0);
	Run l1 = play(e2, pl, SR, 170.0, twoBars);
	bm::Engine e3;
	e3.captureSeconds = 0.0;
	e3.prepareToPlay(SR, 32);
	e3.setPrepared(prep.get());
	Run l2 = play(e3, pl, SR, 170.0, twoBars);
	CHECK(l1.firstChains == l2.firstChains, "lock not reproducible");

	std::printf("%d / %d checks passed\n", checks - fails, checks);
	return fails ? 1 : 0;
}
