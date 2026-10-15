// Offline check of the vendored TST_INGP engine as the Rack module drives it (32-sample blocks,
// ticks at 1/16 of 140 BPM, the "Guitar Field" preset). Not part of the plugin build.
//   g++ -std=c++17 -O2 -Iengines tests/tstingp_engine.cpp engines/tstingp/IngpEngine.cpp -o build/tstingp_test
//   ./build/tstingp_test
#include "tstingp/Embedded.h"
#include "tstingp/IngpEngine.h"
#include "tstingp/OriginalPresets.h"
#include "tstingp/Randomizer.h"
#include <cmath>
#include <cstdio>

using namespace tstingp;

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static int fieldCount(const Engine& e) {
	int n = 0;
	for (int i = 0; i < N; i++) n += e.cell(i);
	return n;
}

int main() {
	// [p 10to2]: 'A' = 65 = 1000001, no padding; a zero byte produces nothing
	auto b = textToBits("A\nB");
	CHECK(b.size() == 14 && b[0] == 1 && b[6] == 1 && b[1] == 0 && b[7] == 1, "textToBits");

	const double SR = 48000.0;
	constexpr int B = 32;
	Engine e;
	e.prepare(SR);
	const auto& t = tstingp_data::kTexts[0];
	auto bits = textToBits(std::string((const char*) t.bytes, (size_t) t.len));
	CHECK(bits.size() == (size_t) N, "example 1 gives %zu bits", bits.size());
	e.loadBits(bits);
	const auto& o = tstingp_orig::all[1];
	for (int v = 0; v < V; v++) {
		e.freq[(size_t) v].store(o.freq[v]);
		e.amp[(size_t) v].store(o.amp[v]);
		e.pan[(size_t) v].store(o.pan[v]);
		e.len[(size_t) v].store(o.len[v]);
	}
	const int before = fieldCount(e);

	Engine::RenderParams rp;
	const double samplesPerTick = SR * 60.0 / 140.0 / 4.0;
	double next = 0.0;
	long n = 0;
	float L[B], R[B];
	double en = 0, peak = 0;
	bool finite = true;
	int ticks = 0, caChanged = 0;
	uint32_t gatesSeen = 0;
	for (int blk = 0; blk < (int) (SR * 4 / B); blk++) {
		std::fill(L, L + B, 0.f);
		std::fill(R, R + B, 0.f);
		int done = 0;
		for (int i = 0; i < B; i++, n++)
			if (n >= next) {
				if (i > done) { e.render(L + done, R + done, i - done, rp); done = i; }
				auto res = e.tick(1, WalkArea::original, (uint32_t) ticks + 1u);
				e.startGateRamps(res.gatesOn, 250.0);
				gatesSeen |= res.gatesOn;
				caChanged += res.caChanged;
				ticks++;
				next += samplesPerTick;
			}
		if (done < B) e.render(L + done, R + done, B - done, rp);
		for (int i = 0; i < B; i++) {
			finite &= std::isfinite(L[i]) && std::isfinite(R[i]);
			en += L[i] * L[i] + R[i] * R[i];
			peak = std::max(peak, (double) std::max(std::fabs(L[i]), std::fabs(R[i])));
		}
	}
	const double rms = std::sqrt(en / (SR * 4 * 2));
	CHECK(finite, "non-finite");
	CHECK(rms > 1e-3, "rms %f (silent?)", rms);
	CHECK(peak < 8.0, "peak %f", peak);
	CHECK(ticks >= 36 && ticks <= 38, "ticks %d", ticks);
	CHECK(caChanged > 0, "the automaton never flipped a cell");
	CHECK(fieldCount(e) != before || caChanged > 0, "field unchanged");
	CHECK(gatesSeen != 0, "no voice ever opened");
	std::printf("guitar field: rms %.4f peak %.3f ticks %d flips %d voices %08x\n", rms, peak, ticks, caChanged, gatesSeen);

	// sync resets the counters
	e.syncReset();
	auto r0 = e.tick(0, WalkArea::original, 1);
	(void) r0;
	CHECK(e.stepShown[0].load() == 0, "syncReset");

	// evolve (Randomizer, as on the audio thread) keeps everything in range
	Rng rng(3);
	Randomizer rz{e, rng, {}, {}};
	for (int k = 0; k < 50; k++) {
		rz.rule(Amount::alot);
		rz.field(Amount::alot);
		rz.freq(Amount::medium);
		rz.amp(Amount::medium);
		rz.pan(Amount::alot);
		rz.len(Amount::alot);
	}
	bool ok = true;
	for (int v = 0; v < V; v++) {
		ok &= e.freq[(size_t) v].load() >= 0.f && e.freq[(size_t) v].load() <= 1.f;
		ok &= e.len[(size_t) v].load() >= 1 && e.len[(size_t) v].load() <= 64;
	}
	CHECK(ok, "randomizer out of range");

	std::printf("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
