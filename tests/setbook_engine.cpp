// Offline test of the vendored SETBOOK engine (engines/setbook: SetbookMorph.h, Book.*) as the cable
// module uses it. Not part of the plugin build.
//   g++ -std=c++17 -O2 -Iengines/setbook/Source/engine tests/setbook_engine.cpp engines/setbook/Source/engine/Book.cpp -o build/setbook_engine && build/setbook_engine
#include "Book.h"

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

using namespace setbook;

int main() {
	Policy pol;
	const float corner[4] = {0.2f, 0.8f, 0.f, 1.f};

	// a CV channel glides from base to the scene along every curve, ends exactly there
	Desc cv;
	cv.kind = Continuous;
	for (int c = 0; c < kNumCurves; c++) {
		pol.curve = (uint8_t) c;
		float prev = -1.f;
		bool mono = true;
		for (int i = 0; i <= 100; i++) {
			const float v = blend(cv, resolve(cv, RuleAuto, pol), 0.2f, corner, 3, i / 100.f, 0.f, pol);
			mono = mono && v >= prev - 1e-6f && std::isfinite(v);
			prev = v;
		}
		CHECK(mono && std::fabs(prev - 0.8f) < 1e-5f, "curve %d monotone %d end %f", c, (int) mono, prev);
	}

	// a gate channel (switch) jumps at the threshold on the raw position, whatever the curve
	Desc gate;
	gate.kind = Switch;
	gate.steps = 2;
	const float gc[4] = {0.f, 1.f, 0.f, 0.f};
	pol.curve = CurveLate;
	pol.threshold = 0.3f;
	CHECK(blend(gate, resolve(gate, RuleAuto, pol), 0.f, gc, 3, 0.29f, 0.f, pol) == 0.f, "before threshold");
	CHECK(blend(gate, resolve(gate, RuleAuto, pol), 0.f, gc, 3, 0.31f, 0.f, pol) == 1.f, "after threshold");

	// semitone channel (121 steps over 10 V): WALK snaps to 1/12 V, JUMP holds then jumps
	Desc semi;
	semi.kind = Stepped;
	semi.steps = 121;
	const float sc[4] = {0.f, 0.5f, 0.f, 0.f};   // 0 V -> 5 V
	pol.curve = CurveLinear;
	pol.threshold = 0.5f;
	pol.steppedWalk = true;
	const float w = blend(semi, resolve(semi, RuleAuto, pol), 0.f, sc, 3, 0.37f, 0.f, pol) * 10.f;
	CHECK(std::fabs(w * 12.f - std::round(w * 12.f)) < 1e-3f && w > 1.5f && w < 2.2f, "walk %f V", w);
	pol.steppedWalk = false;
	CHECK(blend(semi, resolve(semi, RuleAuto, pol), 0.f, sc, 3, 0.37f, 0.f, pol) == 0.f, "jump holds");

	// the pad: bilinear between four corners, a missing corner takes the base
	pol.curve = CurveLinear;
	const float pc[4] = {0.f, 1.f, 0.f, 1.f};
	CHECK(std::fabs(blend(cv, RuleGlide, 0.f, pc, 15, 0.5f, 0.5f, pol) - 0.5f) < 1e-5f, "pad centre");
	CHECK(std::fabs(blend(cv, RuleGlide, 0.4f, pc, 1 | 4, 1.f, 0.f, pol) - 0.4f) < 1e-5f, "missing corner = base");

	// cues on the BUS POS cycle (32 bars = 128 quarters, looping): fire in order, also across the wrap
	std::vector<sb::Cue> cues;
	for (double bar : {24.0, 0.0, 8.0, 31.0}) {
		sb::Cue c;
		c.bar = bar;
		c.scene = (int) bar / 8;
		cues.push_back(c);
	}
	sb::sortCues(cues);
	CHECK(cues[0].bar == 0.0 && cues[3].bar == 31.0, "sorted");
	sb::Placement pl;
	pl.loopQ = 128.0;
	std::vector<int> fired;
	double ppq = 4.0;
	bool was = true;
	sb::Crossing first = sb::cross(cues.data(), (int) cues.size(), pl, 0.0, ppq, false, true);
	CHECK(first.located && first.chase == 0, "start chases cue %d", first.chase);
	for (int i = 0; i < 2400; i++) {
		const double next = ppq + 0.1;   // 60 bars
		const sb::Crossing c = sb::cross(cues.data(), (int) cues.size(), pl, ppq, next, was, true);
		for (int k = 0; k < c.numFired; k++)
			fired.push_back(c.fired[k]);
		ppq = next;
	}
	// from bar 1 over 60 bars: 8, 24, 31, 0 (wrap), 8, 24
	const int want[] = {1, 2, 3, 0, 1, 2};
	CHECK(fired.size() >= 6, "fired %zu", fired.size());
	for (int i = 0; i < 6 && i < (int) fired.size(); i++)
		CHECK(fired[(size_t) i] == want[i], "cue %d fired %d want %d", i, fired[(size_t) i], want[i]);

	// launch quantise
	CHECK(sb::launchPpq(13.2, 4.0, true) == 16.0, "next bar");
	CHECK(sb::launchPpq(13.2, 4.0, false) == 13.2, "stopped: now");

	std::printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
