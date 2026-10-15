// Offline check of the vendored SEEDWALK engine as the Rack module drives it (32-sample blocks,
// internal tempo and the external clock). Not part of the plugin build; artifex compiles against
// the shim in tests/seedwalk_shim instead of the Rack SDK.
//   g++ -std=c++17 -O2 -DTEST -include cstdio -Itests/seedwalk_shim -Iengines -Iengines/thirdparty \
//       -Iengines/thirdparty/artifex tests/seedwalk_engine.cpp engines/seedwalk/Engine.cpp \
//       engines/thirdparty/beads/floater_beads.cc engines/thirdparty/beads/resources.cc \
//       engines/thirdparty/beads/dsp/*.cc engines/thirdparty/stmlib/dsp/*.cc engines/thirdparty/stmlib/utils/*.cc \
//       -o build/seedwalk_test && ./build/seedwalk_test
#include "seedwalk/Engine.h"
#include "seedwalk/Range.h"
#include <vector>
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

	// 5. RANGE: every shape keeps the ball inside FROM..TO (wrapping too), two nets ping-pong,
	//    one net sits, a ball sent outside comes straight back in
	{
		sw::Engine e;
		e.prepare(48000.0, B);
		makeNets(e, 5);
		std::array<sw::Net, sw::kSteps> nets;
		for (int s = 0; s < sw::kSteps; s++) nets[s] = e.nets[s].load();
		sw::walk::Survey sv, sub;
		sv.run(nets, 0.f, 0.f);
		const int ranges[][2] = {{0, 1}, {0, 7}, {4, 4}, {14, 1}, {9, 12}, {0, 15}, {5, 4}, {3, 4}};
		int bad = 0, pongFail = 0, sitFail = 0, backFail = 0, coverFail = 0, runs = 0;
		for (const auto& r : ranges) {
			int idx[sw::kSteps];
			const int n = sw::range::nets(r[0], r[1], idx);
			const uint16_t mask = sw::range::mask(idx, n);
			for (int shape = 0; shape < sw::range::kNumShapes; shape++) {
				if (shape == sw::walk::shClear) continue;
				const auto m = sw::range::shape(shape, sv, sub, idx, n, 0.4f, 77u);
				for (int x = 0; x < sw::walk::kStates; x++)
					for (int y = 0; y < sw::walk::kStates; y++)
						e.seq.matrix[x][y].store(m[x][y]);
				sw::Params p;
				p.timeMult = 0.25;   // 32 steps a second at 120 BPM
				p.bpm = 120.0;
				p.glide = 0.5f;
				e.requestReset();
				// the ball starts outside the range when it can: it must come straight back
				int outside = -1;
				for (int k = 0; k < sw::kSteps; k++) if (!(mask >> k & 1)) { outside = k; break; }
				float l[B], rr[B];
				uint32_t a0 = e.vis.arrivals.load();
				std::vector<int> seq;
				bool jumped = false;
				for (int b = 0; b < 48000 * 3 / B; b++) {
					if (b == 100 && outside >= 0) { e.requestJump(outside); jumped = true; }
					e.process(p, nullptr, nullptr, l, rr, B, nullptr);
					const uint32_t a = e.vis.arrivals.load();
					if (a != a0) { a0 = a; seq.push_back(e.vis.toStep.load()); }
				}
				runs++;
				uint16_t seen = 0;
				bool backIn = !jumped;
				for (size_t k = 0; k < seq.size(); k++) {
					const int t = seq[k];
					if (t == outside && jumped) {
						if (k + 1 < seq.size() && (mask >> seq[k + 1] & 1)) backIn = true;
						continue;
					}
					if (!(mask >> t & 1)) bad++;
					seen |= (uint16_t) (1u << t);
					if (n == 2 && k > 0 && seq[k - 1] != outside && seq[k - 1] == t) pongFail++;
					if (n == 1 && t != idx[0]) sitFail++;
				}
				if (!backIn) backFail++;
				if (seq.size() < 40) bad++;
				// shapes that must cover the whole range in 3 s (90 steps)
				const bool covering = shape == sw::walk::shChain || shape == sw::walk::shCoolDown || shape == sw::walk::shWarmUp
				                      || (shape == sw::range::kJump && n <= 8);   // JUMP: coupon collector, 16 nets may need more
				if (covering && seen != mask) { coverFail++; std::printf("  missed: range %d-%d shape %d seen %04x mask %04x (%zu steps)\n", r[0] + 1, r[1] + 1, shape, seen, mask, seq.size()); }
			}
		}
		CHECK(bad == 0, "range: %d arrivals outside the range / stalled runs", bad);
		CHECK(pongFail == 0, "range: two nets did not ping-pong %d times", pongFail);
		CHECK(sitFail == 0, "range: FROM == TO left its net %d times", sitFail);
		CHECK(backFail == 0, "range: %d runs did not come back in after a jump outside", backFail);
		CHECK(coverFail == 0, "range: %d covering shapes missed nets", coverFail);
		std::printf("range      %d runs (8 ranges x 11 shapes)\n", runs);

		// FROM 1 TO 16 is the plugin's walk: the same matrix as makeShape on all sixteen
		int idx[sw::kSteps];
		const int n = sw::range::nets(0, 15, idx);
		int diff = 0;
		for (int shape = 0; shape < sw::walk::kShapes; shape++) {
			const auto a = sw::range::shape(shape, sv, sub, idx, n, 0.4f, 9u);
			const auto b = sw::walk::makeShape(shape, sv, 16, 0.4f, 9u);
			for (int x = 0; x < sw::walk::kStates; x++)
				for (int y = 0; y < sw::walk::kStates; y++)
					diff += std::fabs(a[x][y] - b[x][y]) > 1e-4f;
		}
		CHECK(diff == 0, "full range differs from the plugin's shapes in %d cells", diff);
	}

	// 6. HOLD: mid-glide the ball stops and the glide freezes; release goes on from there
	{
		sw::Engine e;
		e.prepare(48000.0, B);
		makeNets(e, 6);
		e.requestReset();
		sw::Params p;
		p.timeMult = 4.0;
		p.bpm = 120.0;
		p.glide = 1.f;
		p.curve = sw::cLinear;
		float l[B], rr[B];
		// run until a glide is half way
		int guard = 0;
		do {
			e.process(p, nullptr, nullptr, l, rr, B, nullptr);
		} while (++guard < 48000 && !(e.vis.arrivals.load() >= 2 && e.vis.morph.load() > 0.4f && e.vis.morph.load() < 0.6f));
		const float m0 = e.vis.morph.load();
		const int to0 = e.vis.toStep.load();
		const uint32_t a0 = e.vis.arrivals.load();
		p.hold = true;
		e.requestJump((to0 + 5) % 16);   // waits for the release
		Run r = render(e, p, 48000.0, 3);
		CHECK(r.arrivals == 0, "hold: %u arrivals while held", r.arrivals);
		CHECK(std::fabs(e.vis.morph.load() - m0) < 1e-3f, "hold: glide moved %.3f -> %.3f", m0, e.vis.morph.load());
		CHECK(e.vis.toStep.load() == to0 && e.vis.holding.load(), "hold: target changed or not holding");
		CHECK(r.finite && r.peak <= 1.0001, "hold: output not bounded");
		// PIN while held: the frozen net goes into the target step
		const sw::Net pinned = e.currentNet();
		e.nets[to0].store(pinned);
		CHECK(std::fabs(e.nets[to0].load().scale - pinned.scale) < 1e-6f, "hold: pin failed");
		std::printf("hold       morph %.3f frozen, rms %.4f while held\n", m0, r.rms);
		// release: the queued jump lands, then the walk goes on
		p.hold = false;
		e.process(p, nullptr, nullptr, l, rr, B, nullptr);
		CHECK(e.vis.arrivals.load() == a0 + 1 && e.vis.toStep.load() == (to0 + 5) % 16, "release: the queued jump did not land");
		r = render(e, p, 48000.0, 4);
		CHECK(r.arrivals >= 6, "release: %u arrivals in 4 s", r.arrivals);
	}

	// 7. HOLD without a jump: release continues the frozen glide (no snap to the target)
	{
		sw::Engine e;
		e.prepare(48000.0, B);
		makeNets(e, 7);
		e.requestReset();
		sw::Params p;
		p.timeMult = 4.0;
		p.bpm = 120.0;
		p.glide = 1.f;
		p.curve = sw::cLinear;
		float l[B], rr[B];
		int guard = 0;
		do {
			e.process(p, nullptr, nullptr, l, rr, B, nullptr);
		} while (++guard < 48000 && !(e.vis.arrivals.load() >= 2 && e.vis.morph.load() > 0.3f && e.vis.morph.load() < 0.5f));
		const float m0 = e.vis.morph.load();
		p.hold = true;
		render(e, p, 48000.0, 1);
		p.hold = false;
		e.process(p, nullptr, nullptr, l, rr, B, nullptr);
		const float m1 = e.vis.morph.load();
		CHECK(m1 > m0 && m1 - m0 < 0.01f, "release: glide %.4f -> %.4f (should creep on)", m0, m1);
	}

	std::printf("%d / %d checks passed\n", checks - fails, checks);
	return fails ? 1 : 0;
}
