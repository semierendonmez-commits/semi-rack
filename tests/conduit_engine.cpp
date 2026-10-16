// Offline test of the vendored CONDUIT pin matrix (engines/conduit) as the cable module drives it.
// Not part of the plugin build.
//   g++ -std=c++17 -O2 -Iengines/conduit tests/conduit_engine.cpp engines/conduit/Router.cpp -o build/conduit_engine && build/conduit_engine
#include "Router.h"

#include <cmath>
#include <cstdio>

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

using namespace conduit;

constexpr int kBlock = 32;
constexpr double kSr = 48000.0;
enum { R_BAR = 1, R_TURN = 2, R_ENERGY = 5, R_TRG1 = 7, R_CV1 = 11 };

static Patch basePatch() {
	Patch p;
	const char* rows[kRows] = {"CLK", "BAR", "TURN", "TURN+", "SCENE", "ENERGY", "PHASE", "TRG1", "TRG2", "TRG3", "TRG4", "CV1", "CV2", "CV3", "CV4"};
	for (int r = 0; r < kRows; r++) {
		const bool ev = r <= 4 || (r >= 7 && r <= 10);
		p.row[r] = Endpoint::make("IN", rows[r], ev ? SrcEvent : SrcValue);
	}
	const uint8_t types[kCols] = {DstValue, DstValue, DstValue, DstValue, DstTrigger, DstTrigger, DstGate, DstGate};
	for (int c = 0; c < kCols; c++) {
		char nm[8];
		std::snprintf(nm, sizeof nm, "OUT%d", c + 1);
		p.col[c] = Endpoint::make("OUT", nm, types[c]);
	}
	return p;
}

struct Rig {
	Router rt;
	uint64_t t = 0;
	Rig(const Patch& p) {
		rt.prepare(kSr);
		rt.setPatch(p);
	}
	// one block; hits at offsets; values set before end()
	void block(int hitRow = -1, int hitOff = 0, int valueRow = -1, float value = 0.f, int valueRow2 = -1, float value2 = 0.f) {
		Block b;
		b.numSamples = kBlock;
		b.sampleRate = kSr;
		b.bpm = 120.0;
		b.sampleTime = t;
		rt.begin(b);
		if (hitRow >= 0)
			rt.hit(hitRow, hitOff, 1.f);
		if (valueRow >= 0)
			rt.value(valueRow, value);
		if (valueRow2 >= 0)
			rt.value(valueRow2, value2);
		rt.end();
		t += kBlock;
	}
};

int main() {
	CHECK(kRows == 15 && kCols == 8, "rows %d cols %d", kRows, kCols);
	CHECK(sizeof(Router) < 64 * 1024, "router %zu bytes", sizeof(Router));
	const size_t histBytes = (size_t) kRows * Router::kHistory * 16;
	CHECK(histBytes <= 64 * 1024, "history %zu bytes", histBytes);

	const double sp16 = kSr * 60.0 / 120.0 / 4.0;   // 6000 samples

	// ---- BAR pumps OUT1: -60 %, EXP, 2/16: full depth at the hit, gone after LENGTH
	{
		Patch p = basePatch();
		p.connect(p.row[R_BAR], p.col[0], -0.6f, CurveExp, 0.f, 1.f, 2.f);
		Rig g(p);
		g.block(R_BAR, 0);
		CHECK(std::fabs(g.rt.out[0] + 0.6f) < 0.02f, "out at hit %f", g.rt.out[0]);
		float mid = 0.f;
		const int blocks16 = (int) (2.0 * sp16 / kBlock);
		for (int i = 1; i < blocks16 + 4; i++) {
			g.block();
			if (i == blocks16 / 2)
				mid = g.rt.out[0];
			CHECK(std::isfinite(g.rt.out[0]) && g.rt.out[0] <= 0.f && g.rt.out[0] >= -0.61f, "bounded %f", g.rt.out[0]);
		}
		CHECK(mid < -0.05f && mid > -0.2f, "EXP half way %f (0.6 * 0.5^3 = 0.075)", mid);
		CHECK(g.rt.out[0] == 0.f, "released after LENGTH %f", g.rt.out[0]);
	}

	// ---- a trigger pin with DELAY 1/2 sixteenth fires exactly 3000 samples after the hit
	{
		Patch p = basePatch();
		p.connect(p.row[R_TRG1], p.col[4], 1.f, CurveLin, 0.5f, 1.f, 1.f);
		Rig g(p);
		g.block(R_TRG1, 10);
		CHECK(g.rt.numTrigs == 0, "not yet %d", g.rt.numTrigs);
		uint64_t firedAt = 0;
		for (int i = 0; i < 200 && !firedAt; i++) {
			const uint64_t t0 = g.t;
			g.block();
			if (g.rt.numTrigs > 0)
				firedAt = t0 + (uint64_t) g.rt.trigs[0].offset;
		}
		CHECK(firedAt == 10 + 3000, "fired at %llu", (unsigned long long) firedAt);
	}

	// ---- PROB 50 % passes about half of 1000 hits
	{
		Patch p = basePatch();
		p.connect(p.row[R_TURN], p.col[5], 1.f, CurveLin, 0.f, 0.5f, 1.f);
		Rig g(p);
		int fired = 0;
		for (int i = 0; i < 1000; i++) {
			g.block(R_TURN, 0);
			fired += g.rt.numTrigs;
		}
		CHECK(fired > 420 && fired < 580, "fired %d of 1000", fired);
	}

	// ---- ENERGY (value) -> OUT2, and CV1 delayed 2/16 through the strided history
	{
		Patch p = basePatch();
		p.connect(p.row[R_ENERGY], p.col[1], 1.f, CurveLin);
		p.connect(p.row[R_CV1], p.col[2], 1.f, CurveLin, 2.f);
		Rig g(p);
		for (int i = 0; i < 2000; i++)
			g.block(-1, 0, R_ENERGY, 0.25f, R_CV1, 0.f);
		CHECK(std::fabs(g.rt.out[1] - 0.25f) < 1e-4f, "energy %f", g.rt.out[1]);
		const uint64_t stepAt = g.t;
		uint64_t seen = 0;
		for (int i = 0; i < 1000 && !seen; i++) {
			g.block(-1, 0, R_ENERGY, 0.25f, R_CV1, 1.f);
			if (g.rt.out[2] > 0.5f)
				seen = g.t - stepAt;
		}
		const double want = 2.0 * sp16;
		CHECK(seen > 0 && std::fabs((double) seen - want) < 600.0, "delayed by %llu samples (want %.0f)", (unsigned long long) seen, want);
	}

	// ---- a GATE column: CurveGate holds for LENGTH
	{
		Patch p = basePatch();
		p.connect(p.row[R_TRG1], p.col[6], 1.f, CurveGate, 0.f, 1.f, 4.f);
		Rig g(p);
		g.block(R_TRG1, 0);
		CHECK(g.rt.gate[6], "gate on");
		int on = 1;
		for (int i = 0; i < 2000; i++) {
			g.block();
			on += g.rt.gate[6] ? 1 : 0;
		}
		const int want = (int) (4.0 * sp16 / kBlock);
		CHECK(std::abs(on - want) <= 2, "gate blocks %d want %d", on, want);
	}

	std::printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
