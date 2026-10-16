// Offline check of the vendored SNARESTACK engine as the Rack module drives it: the module's default
// stack (four synthesised layers), alignment of a late + inverted layer, the crack / body / tail
// split, a snare cut out of a synthetic break, the player in 32-sample blocks with the stack
// swapped while voices ring (Player::uses). Not part of the plugin build.
//   g++ -std=c++17 -O2 -Iengines tests/snarestack_engine.cpp engines/snarestack/*.cpp -o build/snarestack_test
//   ./build/snarestack_test
#include "snarestack/Library.h"
#include "snarestack/Player.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

using namespace ss;
constexpr double SR = 48000.0;
constexpr int B = 32;

static std::shared_ptr<Source> bodyLayer(double lead, bool invert, uint32_t seed) {
	auto s = std::make_shared<Source>();
	s->rate = kRate;
	const int n = (int) (kRate * 0.4);
	s->l.assign((size_t) n, 0.0f);
	std::mt19937 rng(seed);
	std::uniform_real_distribution<float> u(-1.0f, 1.0f);
	float lp = 0.0f;
	for (int i = 0; i < n; ++i) {
		const double t = (i - lead) / kRate;
		if (t < 0)
			continue;
		lp += 0.3f * (u(rng) - lp);
		const double v = std::sin(2.0 * M_PI * 190.0 * t) * std::exp(-t / 0.08) + 0.4 * std::sin(2.0 * M_PI * 437.0 * t) * std::exp(-t / 0.03) +
		                 0.3 * lp * std::exp(-t / 0.05);
		s->l[(size_t) i] = (float) v * (invert ? -1.0f : 1.0f) * 0.6f;
	}
	s->r = s->l;
	s->name = "test";
	return s;
}

int main() {
	// ---- 1. the module's default stack: 808 snare, body 185, crack noise, tail verb (0 / -4 / -6 / -10 dB)
	std::array<std::shared_ptr<const Source>, kLayers> src{synth(7), synth(0), synth(2), synth(6)};
	StackParams p;
	p.layer[1].gainDb = -4.f;
	p.layer[2].gainDb = -6.f;
	p.layer[3].gainDb = -10.f;
	const auto t0 = std::chrono::steady_clock::now();
	auto st = build(src, p);
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	CHECK(st->frames > 48000, "stack frames %d", st->frames);
	int active = 0;
	for (auto& i : st->info)
		active += i.active;
	CHECK(active == 4, "%d active layers", active);
	double worst = 0.0;
	for (int i = 0; i < st->frames; ++i)
		worst = std::max(worst, std::abs((double) st->cL[0][(size_t) i] + st->cL[1][(size_t) i] + st->cL[2][(size_t) i] - st->mixL[(size_t) i]));
	CHECK(worst < 1e-5, "crack + body + tail != stack (%.2e)", worst);
	std::printf("default stack: %d frames, built in %.0f ms, shifts %.2f %.2f %.2f %.2f ms, parts sum to %.1e\n", st->frames, ms,
	            st->info[0].shiftMs, st->info[1].shiftMs, st->info[2].shiftMs, st->info[3].shiftMs, worst);

	// ---- 2. a body layer 37.4 samples late and inverted is found, flipped and moved back
	{
		std::array<std::shared_ptr<const Source>, kLayers> s2{bodyLayer(0.0, false, 7), bodyLayer(37.4, true, 7), synth(2), nullptr};
		StackParams q;
		q.ref = 0;
		auto a = build(s2, q);
		const double rel = (a->info[1].shiftMs - a->info[0].shiftMs) * kRate / 1000.0;
		CHECK(a->info[1].inverted, "late layer not flipped");
		CHECK(std::abs(rel + 37.4) < 1.5, "relative shift %.2f samples (want -37.4)", rel);
		CHECK(a->cancelAligned[2] > a->cancelRaw[2] + 3.f, "body band %.1f -> %.1f dB", a->cancelRaw[2], a->cancelAligned[2]);
		std::printf("alignment: shift %.2f samples, inverted %d, body band %.1f -> %.1f dB\n", rel, (int) a->info[1].inverted, a->cancelRaw[2],
		            a->cancelAligned[2]);
	}

	// ---- 3. a snare cut out of a synthetic break (kick on 1 / 3, snare on 2 / 4)
	{
		gtbs::Sample b;
		b.rate = 44100.0;
		b.frames = (int) (b.rate * 60.0 / 170.0 * 4.0);
		b.l.assign((size_t) b.frames, 0.f);
		const int beat = (int) (b.rate * 60.0 / 170.0);
		std::mt19937 rng(5);
		std::uniform_real_distribution<float> u(-1.f, 1.f);
		for (int k = 0; k < 4; k++)
			for (int i = 0; i < beat; i++) {
				const double t = i / b.rate;
				const float v = (k % 2 == 0) ? (float) (0.9 * std::sin(2 * M_PI * 55 * t * (1 + 2 * std::exp(-t / 0.02))) * std::exp(-t / 0.12))
				                             : (float) (0.7 * (0.6 * std::sin(2 * M_PI * 200 * t) + 0.6 * u(rng)) * std::exp(-t / 0.07));
				b.l[(size_t) (k * beat + i)] = v;
			}
		b.an = gtbs::analyse(b, gtbs::AnalysisParams());
		auto sn = snareFromBreak(b, "test snare");
		CHECK(sn && sn->frames() > 1000, "no snare cut (%d frames)", sn ? sn->frames() : 0);
		// the cut starts near a snare (beat 2 or 4): its first 5 ms are noisy-bright, not a 55 Hz sine
		if (sn && sn->frames() > 1000) {
			int zc = 0;
			for (int i = 1; i < (int) (b.rate * 0.02); i++)
				zc += (sn->l[(size_t) i] > 0.f) != (sn->l[(size_t) i - 1] > 0.f);
			CHECK(zc > 20, "cut is not the snare (%d zero crossings in 20 ms)", zc);
			std::printf("break: %zu slices, snare cut %d frames, %d zero crossings / 20 ms\n", b.an.starts.size(), sn->frames(), zc);
		}
	}

	// ---- 4. the player as the module runs it: hits in 32-sample blocks, a new stack swapped in
	//          while the first still rings, the old one released only when no voice uses it
	{
		Player pl;
		pl.prepare(SR);
		std::vector<float> L(B), R(B);
		PlayParams pp;
		Note hit{0, 60, 110};
		double e = 0.0;
		float peak = 0.f;
		bool finite = true;
		const Stack* cur = st.get();
		auto st2 = build(src, StackParams());
		bool swapped = false, released = false;
		int releasedAt = -1;
		for (int blk = 0; blk < (int) (SR * 3.0 / B); blk++) {
			const bool trig = blk == 0 || blk == 600;
			if (blk == 100) { // a knob moved: the new stack arrives while hit 1 still rings
				cur = st2.get();
				swapped = true;
			}
			pl.process(cur, pp, trig ? &hit : nullptr, trig ? 1 : 0, L.data(), R.data(), B);
			if (swapped && !released && !pl.uses(st.get())) {
				released = true;
				releasedAt = blk;
			}
			if (swapped && blk == 101)
				CHECK(pl.uses(st.get()), "the ringing voice should still use the old stack");
			for (int i = 0; i < B; i++) {
				finite &= std::isfinite(L[(size_t) i]) && std::isfinite(R[(size_t) i]);
				peak = std::max(peak, std::max(std::abs(L[(size_t) i]), std::abs(R[(size_t) i])));
				e += (double) L[(size_t) i] * L[(size_t) i];
			}
		}
		CHECK(finite, "non-finite output");
		CHECK(peak > 0.1f && peak < 1.2f, "peak %.3f", peak);
		CHECK(pl.hits.load() == 2, "%d hits", pl.hits.load());
		CHECK(released && releasedAt > 101, "old stack released at block %d", releasedAt);
		std::printf("player: peak %.3f, energy %.1f, old stack free after %.2f s\n", peak, e, releasedAt * B / SR);
	}

	std::printf("%d / %d checks passed\n", checks - fails, checks);
	return fails ? 1 : 0;
}
