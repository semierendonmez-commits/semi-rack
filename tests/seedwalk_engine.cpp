// Offline check of the vendored SEEDWALK engine as the Rack module drives it (32-sample blocks,
// internal tempo and the external clock). Not part of the plugin build; artifex compiles against
// the shim in tests/seedwalk_shim instead of the Rack SDK.
//   g++ -std=c++17 -O2 -DTEST -include cstdio -Itests/seedwalk_shim -Iengines -Iengines/thirdparty \
//       -Iengines/thirdparty/artifex tests/seedwalk_engine.cpp engines/seedwalk/Engine.cpp \
//       engines/thirdparty/beads/floater_beads.cc engines/thirdparty/beads/resources.cc \
//       engines/thirdparty/beads/dsp/*.cc engines/thirdparty/stmlib/dsp/*.cc engines/thirdparty/stmlib/utils/*.cc \
//       -o build/seedwalk_test && ./build/seedwalk_test
#include "seedwalk/Engine.h"
#include <cmath>
#include <cstdio>

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

namespace {
constexpr int B = 32;

// the module's net recipe (Init Walk: heat 0.6..3.6, scrambled by 7)
void makeNets(sw::Engine& e, uint32_t seed) {
	sw::Rng r(seed * 2654435761u + 17u);
	for (int s = 0; s < sw::kSteps; s++) {
		sw::Net n;
		n.seed = r.u32() | 1u;
		for (auto& g : n.gain) g = 0.4f + 0.6f * r.uniform();
		n.leak = 0.1f + 0.6f * r.uniform();
		n.lvl = 0.6f;
		const float heat = 0.6f + 3.0f * (float) ((s * 7) % 16) / 15.f;
		e.nets[s].store(sw::walk::tuneHeat(n, heat));
	}
	for (int s = 0; s < sw::kSteps; s++) {
		sw::pinball::StepParams st;
		st.trig = (s % 8 == 0) ? 1 : 0;
		st.duration = 1.0;
		e.steps[s].store(st);
	}
}

struct Run {
	double peak = 0.0, rms = 0.0;
	bool finite = true;
	uint32_t arrivals = 0;
	int distinct = 0;
};

Run render(sw::Engine& e, sw::Params p, double sr, int seconds, int clockEvery = 0) {
	Run r;
	float l[B], rr[B];
	bool seen[sw::kSteps]{};
	const long blocks = (long) (sr * seconds) / B;
	const uint32_t a0 = e.vis.arrivals.load();
	long n = 0, sample = 0;
	for (long b = 0; b < blocks; b++) {
		if (clockEvery > 0)
			for (int i = 0; i < B; i++)
				if (++sample % clockEvery == 0) e.seq.clockEdge();
		e.process(p, nullptr, nullptr, l, rr, B, nullptr);
		for (int i = 0; i < B; i++) {
			if (!std::isfinite(l[i]) || !std::isfinite(rr[i])) r.finite = false;
			r.peak = std::max(r.peak, (double) std::max(std::fabs(l[i]), std::fabs(rr[i])));
			r.rms += l[i] * l[i] + rr[i] * rr[i];
			n += 2;
		}
		const int t = e.vis.toStep.load();
		if (t >= 0 && t < sw::kSteps) seen[t] = true;
	}
	r.rms = std::sqrt(r.rms / std::max(1L, n));
	r.arrivals = e.vis.arrivals.load() - a0;
	for (bool s : seen) r.distinct += s;
	return r;
}
} // namespace

int main() {
	// 1. internal tempo, chain walk: a beat a step at 120 BPM -> ~2 arrivals / s, all 16 nets in 8 s
	for (double sr : {44100.0, 48000.0, 96000.0}) {
		sw::Engine e;
		e.prepare(sr, B);
		makeNets(e, 1);
		e.requestReset();
		sw::Params p;
		p.run = sw::rAlways;
		p.timeMult = 4.0;
		p.bpm = 120.0;
		const Run r = render(e, p, sr, 9);
		CHECK(r.finite, "sr %.0f: non-finite output", sr);
		CHECK(r.peak <= 1.0001, "sr %.0f: peak %.3f above 0 dBFS", sr, r.peak);
		CHECK(r.rms > 1e-3, "sr %.0f: silent (rms %.6f)", sr, r.rms);
		CHECK(r.arrivals >= 16 && r.arrivals <= 20, "sr %.0f: %u arrivals in 9 s", sr, r.arrivals);
		CHECK(r.distinct == 16, "sr %.0f: visited %d nets", sr, r.distinct);
		std::printf("sr %6.0f  peak %.3f  rms %.4f  arrivals %u  nets %d\n", sr, r.peak, r.rms, r.arrivals, r.distinct);
	}

	// 2. the shapes from the survey: COOL DOWN goes to a cooler net every step but one in 16
	{
		std::array<sw::Net, sw::kSteps> nets;
		sw::Engine e;
		makeNets(e, 2);
		for (int s = 0; s < sw::kSteps; s++) nets[s] = e.nets[s].load();
		sw::walk::Survey sv;
		sv.run(nets, 0.f, 0.f);
		const auto m = sw::walk::makeShape(sw::walk::shCoolDown, sv, 16, 0.4f, 1u);
		int down = 0;
		for (int i = 0; i < sw::kSteps; i++)
			for (int j = 0; j < sw::kSteps; j++)
				if (m[i + 1][j + 1] > 0.f && sv.prof[j].heat < sv.prof[i].heat) down++;
		CHECK(down == 15, "cool down: %d of 16 moves go down", down);
	}

	// 3. external clock: the ball waits for clockEdge(); one edge a step
	{
		sw::Engine e;
		e.prepare(48000.0, B);
		makeNets(e, 3);
		e.requestReset();
		e.seq.extClock = true;
		sw::Params p;
		p.timeMult = 4.0;
		p.bpm = 120.0;
		Run r = render(e, p, 48000.0, 3, 0);           // no clock: stuck on the first net
		CHECK(r.arrivals <= 1, "ext clock, no edges: %u arrivals", r.arrivals);
		r = render(e, p, 48000.0, 4, 12000);           // 4 edges / s for 4 s
		CHECK(r.arrivals >= 15 && r.arrivals <= 17, "ext clock: %u arrivals for 16 edges", r.arrivals);
		CHECK(r.finite && r.rms > 1e-3, "ext clock: output rms %.5f", r.rms);
		std::printf("ext clock  arrivals %u  rms %.4f\n", r.arrivals, r.rms);
	}

	// 4. hot drive (CHAOS +50 %) and Beads on: still bounded and finite
	{
		sw::Engine e;
		e.prepare(48000.0, B);
		makeNets(e, 4);
		e.requestReset();
		sw::Params p;
		p.drive = 0.5f;
		p.bdOn = true;
		p.bdQuality = 3;
		p.axMode = 5;
		const Run r = render(e, p, 48000.0, 4);
		CHECK(r.finite && r.peak <= 1.0001 && r.rms > 1e-3, "chaos: finite %d peak %.3f rms %.5f", r.finite, r.peak, r.rms);
	}

	std::printf("%d / %d checks passed\n", checks - fails, checks);
	return fails ? 1 : 0;
}
