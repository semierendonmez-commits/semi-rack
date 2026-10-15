// Offline check of the vendored JDAT engine (header only) as the Rack module drives it:
// internal metro, FEED from a byte buffer (ternary and raw), external ticks. Not part of the build.
//   g++ -std=c++17 -O2 -Iengines tests/jdat_engine.cpp -o build/jdat_test && ./build/jdat_test
#include "jdat/JdatEngine.h"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace jdat_mod;

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

struct Run {
	double rms = 0, peak = 0;
	bool finite = true;
};

static Run render(Engine& e, const Params& p, double seconds, double sr) {
	Run r;
	float L[32], R[32];
	Transport t;
	const int blocks = (int) (seconds * sr / 32);
	double en = 0;
	for (int b = 0; b < blocks; b++) {
		e.process(L, R, 32, p, t);
		for (int i = 0; i < 32; i++) {
			r.finite &= std::isfinite(L[i]) && std::isfinite(R[i]);
			en += L[i] * L[i] + R[i] * R[i];
			r.peak = std::max(r.peak, (double) std::max(std::fabs(L[i]), std::fabs(R[i])));
		}
	}
	r.rms = std::sqrt(en / (blocks * 64.0));
	return r;
}

static int nonZero(Engine& e, int b) {
	int n = 0;
	for (int k = 0; k < kBufLen; k++) n += e.bufView[(size_t) b][(size_t) k].load() != 0;
	return n;
}

int main() {
	const double SR = 48000.0;
	Engine e;
	e.seed(1);
	e.prepare(SR);
	Params p; // the patch: 120 ms metro, every source = data

	Run r = render(e, p, 6.0, SR);
	const uint32_t ticks = e.tickCount.load();
	CHECK(r.finite, "non-finite");
	CHECK(r.peak <= 1.0001, "peak %f", r.peak);
	CHECK(r.rms > 1e-3, "rms %f (silent?)", r.rms);
	CHECK(ticks >= 49 && ticks <= 51, "ticks %u in 6 s at 120 ms", ticks);
	std::printf("patch: rms %.4f peak %.3f ticks %u\n", r.rms, r.peak, ticks);

	// FEED ternary, no clears, 64 writes per tick: buffers fill from the file
	std::vector<uint8_t> file(4096);
	for (size_t i = 0; i < file.size(); i++) file[i] = (uint8_t) (i * 37u + (i >> 3));
	e.setFeed(file.data(), file.size());
	p.feedMode = 1;
	p.feedAmount = 1.f;
	p.writes = 64;
	p.clearProb = 0;
	r = render(e, p, 2.0, SR);
	int filled = nonZero(e, 0) + nonZero(e, 1) + nonZero(e, 2);
	CHECK(filled > 600, "feed filled only %d cells", filled);
	CHECK(e.feedPosition() > 0, "feed cursor did not move");
	bool ternary = true;
	for (int b = 0; b < 3; b++)
		for (int k = 0; k < kBufLen; k++) {
			const int v = e.bufView[(size_t) b][(size_t) k].load();
			ternary &= v == 0 || v == 127 || v == -127;
		}
	CHECK(ternary, "ternary feed wrote other values");
	CHECK(r.finite && r.rms > 1e-3, "feed ternary: rms %f", r.rms);
	std::printf("feed ternary: rms %.4f filled %d pos %zu\n", r.rms, filled, e.feedPosition());

	// raw bytes
	p.feedMode = 2;
	r = render(e, p, 1.0, SR);
	int odd = 0;
	for (int b = 0; b < 3; b++)
		for (int k = 0; k < kBufLen; k++) {
			const int v = e.bufView[(size_t) b][(size_t) k].load();
			odd += v != 0 && v != 127 && v != -127;
		}
	CHECK(odd > 200, "raw feed: only %d non-ternary cells", odd);
	CHECK(r.finite && r.peak <= 1.0001, "raw feed bounds");
	std::printf("feed raw: rms %.4f non-ternary %d\n", r.rms, odd);

	// external ticks: run = false, the engine ticks only on externalTick
	p.run = false;
	const uint32_t t0 = e.tickCount.load();
	render(e, p, 1.0, SR);
	CHECK(e.tickCount.load() == t0, "ticks without a clock");
	GateSink sink;
	int gates = 0;
	for (int k = 0; k < 200; k++) {
		sink.count = 0;
		e.externalTick(p, k, &sink);
		gates += sink.count;
	}
	CHECK(e.tickCount.load() == t0 + 200, "external ticks %u", e.tickCount.load() - t0);
	// gate when 2^random(0,15) <= 255: 8/15 per column -> ~320 of 600 (+ clears = 0 here)
	CHECK(gates > 250 && gates < 400, "external gates %d", gates);

	// pattern lock repeats the same writes: two loops of 16 ticks give the same buffers
	Engine a;
	a.seed(7);
	a.prepare(SR);
	Params q;
	q.run = false;
	q.loopTicks = 16;
	std::vector<int8_t> s1, s2;
	for (int k = 0; k < 48; k++) {
		a.externalTick(q, k);
		if (k == 31 || k == 47) {
			auto& s = k == 31 ? s1 : s2;
			for (int b = 0; b < 3; b++)
				for (int i = 0; i < kBufLen; i++) s.push_back(a.bufView[(size_t) b][(size_t) i].load());
		}
	}
	CHECK(s1 == s2, "pattern lock does not repeat");

	std::printf("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
