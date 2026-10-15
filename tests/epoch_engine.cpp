// Offline test of the vendored EPOCH engine (engines/epoch). Not part of the plugin build.
//   g++ -std=c++17 -O2 -Iengines/epoch tests/epoch_engine.cpp engines/epoch/Score.cpp
//     engines/epoch/Plan.cpp engines/epoch/Analyzer.cpp -o build/epoch_engine && build/epoch_engine
#include "Analyzer.h"
#include "Plan.h"
#include "Roles.h"
#include "Score.h"

#include <cmath>
#include <cstdio>
#include <vector>

static int checks = 0, failures = 0;
#define CHECK(cond, ...)                                                   \
	do {                                                                   \
		++checks;                                                          \
		if (!(cond)) {                                                     \
			++failures;                                                    \
			std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond);    \
			std::printf(__VA_ARGS__);                                      \
			std::printf("\n");                                             \
		}                                                                  \
	} while (0)

using namespace epoch;

int main() {
	static Plan plan;
	plan.allocate();
	const float* before = plan.out[0].data();

	// every factory plan renders into 0..1, no allocation, sane holes
	for (int t = 0; t < numTemplates(); ++t) {
		const Score s = makeTemplate(t);
		render(s, PlanParams::defaults(), plan);
		bool ok = true;
		for (int sl = 0; sl < kSlots; ++sl)
			for (int i = 0; i < plan.steps; ++i) {
				const float v = plan.out[(size_t) sl][(size_t) i];
				if (!std::isfinite(v) || v < 0.f || v > 1.f)
					ok = false;
			}
		CHECK(ok, "%s: outputs in 0..1", s.title.c_str());
		CHECK(plan.bars == s.bars && plan.steps == s.bars * kStepsPerBar, "%s: length", s.title.c_str());
		CHECK(std::abs(plan.plannedSpace - plan.targetSpace) < 0.06f, "%s: space cut %.3f vs drawn %.3f", s.title.c_str(),
		      plan.plannedSpace, plan.targetSpace);
		CHECK(plan.turnLevel[0] == 5, "%s: bar 1 is a section start", s.title.c_str());
	}
	CHECK(plan.out[0].data() == before, "render did not reallocate");

	// the CV slots the module uses carry the roles it labels them with
	const PlanParams d = PlanParams::defaults();
	CHECK(d.slots[0].role == epochlink::Energy, "slot 0 = ENERGY");
	CHECK(d.slots[1].role == epochlink::Density, "slot 1 = DENSITY");
	CHECK(d.slots[2].role == epochlink::Chaos, "slot 2 = CHAOS");
	CHECK(d.slots[4].role == epochlink::Duck, "slot 4 = DUCK");

	// DNB DJ TOOL: line starts and sections at the right bars
	{
		const Score s = makeTemplate(0);
		render(s, d, plan);
		CHECK(plan.turnLevel[4 * kStepsPerBar] == 1, "bar 5 starts a 4-bar line (%d)", plan.turnLevel[4 * kStepsPerBar]);
		CHECK(plan.turnLevel[16 * kStepsPerBar] == 3, "bar 17 starts a 16-bar line (%d)", plan.turnLevel[16 * kStepsPerBar]);
		CHECK(plan.turnLevel[32 * kStepsPerBar] == 5, "bar 33 is the DROP (%d)", plan.turnLevel[32 * kStepsPerBar]);
		CHECK(plan.barSection[40] == 1 && plan.barSection[100] == 2, "sections per bar");
		CHECK(plan.at(0, 40.0 * kStepsPerBar) > plan.at(0, 100.0 * kStepsPerBar), "the drop is louder than the break");
	}

	// same seed, same holes; another seed, other holes; SPACE x 0 = none
	{
		const Score s = makeTemplate(7);   // EBB 27: lots of space
		PlanParams p = d;
		render(s, p, plan);
		std::vector<float> a(plan.src[SrcHole].begin(), plan.src[SrcHole].begin() + plan.steps);
		render(s, p, plan);
		bool same = true;
		for (int i = 0; i < plan.steps; ++i)
			same = same && a[(size_t) i] == plan.src[SrcHole][(size_t) i];
		CHECK(same, "seeded holes are deterministic");
		p.seed = 77;
		render(s, p, plan);
		bool differ = false;
		for (int i = 0; i < plan.steps; ++i)
			differ = differ || a[(size_t) i] != plan.src[SrcHole][(size_t) i];
		CHECK(differ, "another seed cuts other holes");
		p.spaceScale = 0.f;
		render(s, p, plan);
		CHECK(plan.plannedSpace == 0.f, "SILENCE 0 = no holes (%.3f)", plan.plannedSpace);
	}

	// rescale (the LENGTH knob) keeps the shape
	{
		Score s = makeTemplate(0);
		s.rescale(120);
		render(s, d, plan);
		CHECK(plan.bars == 120, "rescaled to 120 bars");
		CHECK(std::abs(s.sections[1].bar - 16.0) < 1e-9, "DROP moved to bar 16 (%f)", s.sections[1].bar);
	}

	// MEASURE: 64 s of 2 s bursts with 2 s silences -> a score with space
	{
		const double sr = 44100.0;
		Analyzer an(sr);
		std::vector<float> buf(4096);
		long long n = 0;
		const long long total = (long long) (64.0 * sr);
		while (n < total) {
			for (auto& x : buf) {
				const double t = (double) n++ / sr;
				const bool on = std::fmod(t, 4.0) < 2.0;
				x = on ? 0.5f * (float) std::sin(2.0 * M_PI * 220.0 * t) * (std::fmod(t, 0.25) < 0.05 ? 1.f : 0.3f) : 0.f;
			}
			an.push(buf.data(), (int) buf.size());
		}
		const Measure m = an.finish(120.0);
		CHECK(m.bars == 32 || m.bars == 33, "64 s (+ the last block) at 120 = 32-33 bars (%d)", m.bars);
		CHECK(m.spaceShare > 0.35 && m.spaceShare < 0.6, "about half silent (%.3f)", m.spaceShare);
		const Score s = scoreFromMeasure(m, "TEST", 120.0);
		CHECK(!s.energy.pts.empty() && !s.space.pts.empty() && !s.sections.empty(), "measured score has curves and sections");
		render(s, d, plan);
		CHECK(plan.plannedSpace > 0.2f, "the measured plan cuts holes (%.3f)", plan.plannedSpace);
	}

	std::printf("%d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
