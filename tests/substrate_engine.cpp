// Offline check of the vendored SUBSTRATE engine as the Rack module drives it (32-sample blocks,
// stereo, default settings). Not part of the plugin build.
//   g++ -std=c++17 -O2 -Iengines tests/substrate_engine.cpp -o build/substrate_test
//   ./build/substrate_test
#include "substrate/SubstrateCore.h"
#include <cstdio>
#include <random>
#include <vector>

namespace S = sbs::substrate;
static int fails = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

constexpr double SR = 48000.0;
constexpr int B = 32;

// naive-free saw (polyBLEP)
static float saw(double& ph, double inc) {
	ph += inc;
	if (ph >= 1.0)
		ph -= 1.0;
	double v = 2.0 * ph - 1.0;
	if (ph < inc) {
		const double t = ph / inc;
		v -= t + t - t * t - 1.0;
	}
	else if (ph > 1.0 - inc) {
		const double t = (ph - 1.0) / inc;
		v -= t * t + t + t + 1.0;
	}
	return (float) v;
}

// band-limited saw, 12 partials (the plugin's tests use additive bass voices too)
static float bl(double& ph, double inc) {
	ph += inc;
	if (ph >= 1.0)
		ph -= 1.0;
	float v = 0.f;
	for (int k = 1; k <= 12; k++)
		v += (float) (std::sin(2.0 * M_PI * k * ph) / k);
	return 0.6f * v;
}

struct Out {
	std::vector<float> inL, inR, L, R;
	std::vector<double> f0;
	uint64_t edits = 0;
	int latency = 0;
};

template <typename Gen>
static Out run(S::Params p, double seconds, Gen gen, double midiHz = 0.0) {
	S::Engine e;
	e.prepare(SR);
	e.setParams(p);
	e.setMidiF0(midiHz);
	Out o;
	o.latency = e.getLatency();
	const int n = (int) (seconds * SR) / B * B;
	o.inL.resize((size_t) n);
	o.inR.resize((size_t) n);
	for (int i = 0; i < n; i++)
		gen(i, o.inL[(size_t) i], o.inR[(size_t) i]);
	o.L = o.inL;
	o.R = o.inR;
	for (int i = 0; i < n; i += B) {
		float* io[2] = {o.L.data() + i, o.R.data() + i};
		e.process(io, 2, B);
		o.f0.push_back(e.getMeters().f0);
	}
	o.edits = e.getMeters().editsRepaired;
	return o;
}

int main() {
	// ---- 1. a held detuned saw pair (reese-ish, A1 55 Hz then D2 73.4 Hz): tracked, transparent
	{
		double p1 = 0, p2 = 0, p3 = 0, p4 = 0;
		auto gen = [&](int i, float& l, float& r) {
			const double f = i < (int) (2.0 * SR) ? 55.0 : 73.416;
			l = 0.3f * (bl(p1, f / SR) + bl(p2, f * 1.004 / SR));
			r = 0.3f * (bl(p3, f / SR) + bl(p4, f * 0.996 / SR));
		};
		const Out o = run(S::Params(), 4.0, gen);
		const double fA = o.f0[(size_t) (1.8 * SR / B)], fD = o.f0[(size_t) (3.8 * SR / B)];
		CHECK(std::abs(fA / 55.0 - 1.0) < 0.01, "A1 tracked at %.2f Hz", fA);
		CHECK(std::abs(fD / 73.416 - 1.0) < 0.01, "D2 tracked at %.2f Hz", fD);
		// output against the input delayed by the latency, over the second half of each note
		double e = 0, d = 0;
		bool finite = true;
		for (int i = (int) (1.0 * SR); i < (int) (1.9 * SR); i++) {
			// the mid: below the split the output is mono by design, so L alone does not null
			const float x = 0.5f * (o.inL[(size_t) i] + o.inR[(size_t) i]);
			const float y = 0.5f * (o.L[(size_t) (i + o.latency)] + o.R[(size_t) (i + o.latency)]);
			finite &= std::isfinite(y);
			e += (double) x * x;
			d += (double) (x - y) * (x - y);
		}
		const double nullDb = 10.0 * std::log10(d / e);
		CHECK(finite, "non-finite output");
		CHECK(nullDb < -15.0, "null against the input %.1f dB", nullDb);
		// below the split the output is mono: compare L-R energy of the output and the input low-passed
		std::printf("tracking: %.2f / %.2f Hz, latency %d samples (%.1f ms), null %.1f dB\n", fA, fD, o.latency, 1000.0 * o.latency / SR, nullDb);
	}

	// ---- 2. 1/16 neuro edits at 174 BPM: a saw whose phase jumps at every 16th -> cuts repaired
	{
		double ph = 0;
		std::mt19937 rng(3);
		std::uniform_real_distribution<double> u(0.0, 1.0);
		const int sixteenth = (int) (60.0 / 174.0 / 4.0 * SR);
		int cuts = 0;
		auto gen = [&](int i, float& l, float& r) {
			if (i > 0 && i % sixteenth == 0 && i > (int) (0.5 * SR)) {
				ph = u(rng);
				cuts++;
			}
			// a band-limited saw (12 partials, as the plugin's tests' bass voices)
			ph += 55.0 / SR;
			if (ph >= 1.0)
				ph -= 1.0;
			float v = 0.f;
			for (int k = 1; k <= 12; k++)
				v += (float) (std::sin(2.0 * M_PI * k * ph) / k);
			v *= 0.35f;
			l = r = v;
		};
		const Out o = run(S::Params(), 4.0, gen);
		const int cutsRun = cuts;
		CHECK(o.edits >= (uint64_t) (cuts * 6 / 10) && o.edits <= (uint64_t) (cuts * 3 / 2), "%llu of %d cuts repaired", (unsigned long long) o.edits, cuts);
		S::Params off;
		off.repairOn = false;
		ph = 0;
		const Out o2 = run(off, 1.0, gen);
		CHECK(o2.edits == 0, "repair off still repaired %llu", (unsigned long long) o2.edits);
		std::printf("edits: %llu repairs for %d cuts\n", (unsigned long long) o.edits, cutsRun);
	}

	// ---- 3. V/OCT (MIDI mode): f0 from the cable, sub at that pitch
	{
		double ph = 0;
		auto gen = [&](int, float& l, float& r) { l = r = 0.4f * saw(ph, 49.0 / SR); };
		S::Params p;
		p.midiMode = true;
		const Out o = run(p, 1.5, gen, 49.0);
		const double f = o.f0.back();
		CHECK(std::abs(f - 49.0) < 0.05, "midi f0 %.3f", f);
		float pk = 0.f;
		for (float x : o.L)
			pk = std::max(pk, std::abs(x));
		CHECK(pk > 0.1f && pk < 1.5f, "peak %.3f", pk);
		std::printf("v/oct: f0 %.2f Hz, peak %.3f\n", f, pk);
	}

	std::printf("%d / %d checks passed\n", checks - fails, checks);
	return fails ? 1 : 0;
}
