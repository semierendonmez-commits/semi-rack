// Offline check of the vendored TURNAROUND engine as the Rack module drives it
// (32-sample blocks, 1/16 clock -> ppq). Not part of the plugin build.
//   g++ -std=c++17 -O2 -Iengines tests/turnaround_engine.cpp engines/turnaround/Engine.cpp -o build/turnaround_test
//   ./build/turnaround_test
#include "turnaround/Engine.h"
#include <cmath>
#include <cstdio>

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

int main() {
	constexpr double SR = 48000.0, BPM = 170.0;
	constexpr int B = 32;
	const double spb = SR * 60.0 / BPM, slot = spb / 4.0;

	tna::Engine e;
	e.prepare(SR, B);
	tna::Settings s;  // plugin defaults: ghost / roll / retrig / roll, 1-beat wall at 16 and 32
	e.setSettings(s);

	// synthetic break: kick on 1 and 3+, snare on 2 and 4, quiet hats elsewhere
	auto brk = [&](long n) {
		const long inBar = n % (long) (spb * 4);
		const int sl = (int) (inBar / slot);
		const double t = (inBar - sl * slot) / SR;
		const float env = (float) std::exp(-t * 30.0);
		if (sl == 0 || sl == 10) return 0.8f * env * (float) std::sin(2 * M_PI * 60 * t);
		if (sl == 4 || sl == 12) return 0.7f * env * (float) std::sin(2 * M_PI * 900 * t * (1 + 3 * t));
		return 0.08f * env * (float) std::sin(2 * M_PI * 7000 * t);
	};

	const long total = (long) (spb * 4 * 33);  // 33 bars
	float L[B], R[B];
	long silentInWall = 0, wallSamples = 0, changed = 0;
	bool finite = true;
	for (long n0 = 0; n0 + B <= total; n0 += B) {
		tna::Transport tp;
		tp.playing = true;
		tp.bpm = BPM;
		tp.ppq = n0 / spb;
		for (int i = 0; i < B; i++)
			L[i] = R[i] = brk(n0 + i);
		e.process(L, R, B, tp, nullptr);
		for (int i = 0; i < B; i++) {
			const double ppq = (n0 + i) / spb;
			const tna::GridPos g = tna::locate(ppq, 0);
			const float dry = brk(n0 + i);
			finite = finite && std::isfinite(L[i]);
			if (std::fabs(L[i] - dry) > 1e-4f)
				changed++;
			// the wall: last beat before bar 16 and 32 (skip the 2.5 ms fade edges)
			if (g.tier >= 2 && g.beatsToBoundary <= 0.95 && g.beatsToBoundary > 0.01 && g.relBar < 32) {
				wallSamples++;
				if (std::fabs(L[i]) < 1e-3f)
					silentInWall++;
			}
		}
	}
	CHECK(finite, "non-finite output");
	CHECK(changed > 0, "engine never touched the break");
	CHECK(wallSamples > 0 && silentInWall == wallSamples, "wall not silent: %ld / %ld", silentInWall, wallSamples);
	CHECK(e.tele.barsSeen.load() >= 30, "capture saw %d bars", e.tele.barsSeen.load());
	CHECK(e.tele.hitCount.load() > 0, "no roll hits");

	// stereo: both channels get the same decisions on the same samples. The engine is linear in
	// the audio once the plan is made, so with R = g x L in, OUT R must be g x OUT L sample for
	// sample (rolls, retrigs, reverse, tape stop, wall, gap). Two gains, one of them inverted.
	for (const float g : {0.5f, -0.7f}) {
		tna::Engine st;
		st.prepare(SR, B);
		tna::Settings ss;
		ss.tier[0].action = tna::Action::Retrig;
		ss.tier[1].action = tna::Action::Reverse;
		ss.tier[2].action = tna::Action::TapeStop;
		ss.tier[3].action = tna::Action::Roll;
		ss.budget = 1.f;
		st.setSettings(ss);
		double maxErr = 0.0;
		long edited = 0, wallL = 0, wallR = 0, wallN = 0;
		for (long n0 = 0; n0 + B <= total; n0 += B) {
			tna::Transport tp;
			tp.playing = true;
			tp.bpm = BPM;
			tp.ppq = n0 / spb;
			for (int i = 0; i < B; i++) {
				L[i] = brk(n0 + i);
				R[i] = g * L[i];
			}
			st.process(L, R, B, tp, nullptr);
			for (int i = 0; i < B; i++) {
				maxErr = std::max(maxErr, (double) std::fabs(R[i] - g * L[i]));
				if (std::fabs(L[i] - brk(n0 + i)) > 1e-4f)
					edited++;
				const tna::GridPos gp = tna::locate((n0 + i) / spb, 0);
				if (gp.tier >= 2 && gp.beatsToBoundary <= 0.95 && gp.beatsToBoundary > 0.01 && gp.relBar < 32) {
					wallN++;
					wallL += std::fabs(L[i]) < 1e-3f;
					wallR += std::fabs(R[i]) < 1e-3f;
				}
			}
		}
		CHECK(edited > 0, "stereo g=%.1f: nothing edited", g);
		CHECK(maxErr < 1e-5, "stereo g=%.1f: L/R diverge by %g", g, maxErr);
		CHECK(wallN > 0 && wallL == wallN && wallR == wallN, "stereo g=%.1f: wall L %ld R %ld of %ld", g, wallL, wallR, wallN);
		std::printf("stereo g=%+.1f  edited %ld  max |R - gL| %.2g  wall %ld/%ld\n", g, edited, maxErr, wallR, wallN);
	}

	// budget never overspent across random settings
	for (int k = 0; k < 200; k++) {
		tna::Settings r;
		tna::Rng rng(k + 1);
		for (auto& t : r.tier) {
			t.action = (tna::Action) rng.below(tna::kNumActions);
			t.windowBeats = 0.5 + rng.below(32) * 0.5;
			t.intensity = rng.uniform();
		}
		r.budget = rng.uniform();
		r.hierarchy = rng.uniform();
		r.variation = rng.uniform();
		const tna::Plan p = tna::makePlan(r, k);
		CHECK(p.total() <= p.budgetBeats + 1e-3f, "overspent %f > %f", p.total(), p.budgetBeats);
	}

	std::printf("%d checks, %d failures\n", checks, fails);
	return fails ? 1 : 0;
}
