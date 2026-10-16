// Offline check of the vendored NEUROSTACK engine as the Rack module drives it (32-sample blocks,
// ppq advanced from a 1/16 clock at 174 BPM, SELF source). Not part of the plugin build.
//   g++ -std=c++17 -O2 -Iengines tests/neurostack_engine.cpp engines/neurostack/Neuro.cpp -o build/neurostack_test
//   ./build/neurostack_test
#include "neurostack/Neuro.h"
#include <cmath>
#include <complex>
#include <cstdio>
#include <set>
#include <vector>

namespace N = nst::neuro;
static int fails = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

constexpr double SR = 48000.0, BPM = 174.0;
constexpr int B = 32;

struct Run {
	std::vector<float> L, R;
	std::vector<int> arts;
};

static Run render(N::Engine& e, N::Brain& b, N::Params p, double seconds, bool clocked) {
	Run r;
	const int n = (int) (seconds * SR);
	r.L.resize((size_t) n);
	r.R.resize((size_t) n);
	p.bpm = BPM;
	p.gain = std::pow(10.f, -9.f / 20.f); // the plugin's / module's default LEVEL
	p.playing = clocked;
	p.run = clocked ? N::RunHost : N::RunAlways;
	const double sps = 60.0 / BPM * SR / 4.0;
	for (int i = 0; i + B <= n; i += B) {
		p.ppq = (double) i / sps / 4.0;
		e.process(b, p, nullptr, 0, r.L.data() + i, r.R.data() + i, B);
		N::FireEvent fe;
		while (e.popFire(fe))
			if (fe.art >= 0)
				r.arts.push_back(fe.art);
	}
	return r;
}

static double rms(const std::vector<float>& x, size_t a, size_t b) {
	double s = 0.0;
	for (size_t i = a; i < b && i < x.size(); i++)
		s += (double) x[i] * x[i];
	return std::sqrt(s / std::max<size_t>(1, b - a));
}

int main() {
	const double sps = 60.0 / BPM * SR / 4.0; // samples per 16th

	// ---- 1. a switch every 16th, every step a different slot, no rhythm: the sub holds the fundamental
	{
		N::Engine e;
		e.prepare(SR, B);
		N::Brain b;
		for (int s = 0; s < N::kSteps; s++)
			b.steps[(size_t) s].trig.store(1 + s % 8);
		N::Params p;
		p.source = N::SrcSelf;
		p.root = 33; // A1, 55 Hz
		p.mult = 1.f;
		p.rhythmOn = false;
		p.drift = 0.f;
		const Run r = render(e, b, p, 4.0, true);
		bool finite = true;
		float peak = 0.f;
		for (size_t i = 0; i < r.L.size(); i++) {
			finite &= std::isfinite(r.L[i]) && std::isfinite(r.R[i]);
			peak = std::max(peak, std::max(std::abs(r.L[i]), std::abs(r.R[i])));
		}
		CHECK(finite, "non-finite output");
		CHECK(peak < 1.0f, "peak %.3f", peak);
		CHECK(rms(r.L, 0, r.L.size()) > 0.02, "silent: rms %.4f", rms(r.L, 0, r.L.size()));
		std::set<int> distinct(r.arts.begin(), r.arts.end());
		CHECK(r.arts.size() >= 45, "only %zu switches", r.arts.size());
		CHECK(distinct.size() == 8, "only %zu different slots fired", distinct.size());
		CHECK(std::abs(e.currentF0() - 55.f) < 0.01f, "f0 %.3f", e.currentF0());
		// fundamental (55 Hz) magnitude over 4-period windows after the first second
		const int win = (int) std::round(4.0 * SR / 55.0);
		double lo = 1e9, hi = 0.0;
		for (int a = (int) SR; a + win < (int) r.L.size(); a += win) {
			std::complex<double> acc = 0.0;
			for (int i = 0; i < win; i++)
				acc += (double) (r.L[(size_t) (a + i)] + r.R[(size_t) (a + i)]) * std::polar(1.0, -2.0 * M_PI * 55.0 * i / SR);
			const double m = std::abs(acc);
			lo = std::min(lo, m);
			hi = std::max(hi, m);
		}
		const double swingDb = 20.0 * std::log10(hi / std::max(lo, 1e-12));
		CHECK(swingDb < 1.5, "fundamental swings %.2f dB across the switches", swingDb);
		std::printf("switch every 16th: %zu switches, %zu slots, peak %.3f, fundamental swing %.2f dB\n", r.arts.size(), distinct.size(), peak, swingDb);
	}

	// ---- 2. the ROLLER rhythm: its spaces are silent
	{
		N::Engine e;
		e.prepare(SR, B);
		N::Brain b;
		for (int s = 0; s < N::kSteps; s++)
			b.steps[(size_t) s].trig.store(1 + (s / 4) % 8);
		const char* cells = "x--.......x--...x--.......x-x-..";
		for (int i = 0; i < N::kRhythm; i++)
			b.rhythm[(size_t) i].type.store(cells[i] == 'x' ? N::CellHit : cells[i] == '-' ? N::CellTie : N::CellRest);
		N::Params p;
		p.source = N::SrcSelf;
		p.root = 31;
		p.mult = 32.f; // 2 bars
		p.rhythmOn = true;
		p.variation = 0.f;
		const Run r = render(e, b, p, 8.0 * 4 * 60.0 / BPM, true); // 8 bars
		double note = 0.0, space = 0.0;
		for (int bar2 = 1; bar2 < 4; bar2++) {
			const size_t c0 = (size_t) (bar2 * 32 * sps);
			note += rms(r.L, c0, c0 + (size_t) (2 * sps));                                // cells 0-1: the note
			space += rms(r.L, c0 + (size_t) (6 * sps), c0 + (size_t) (10 * sps));        // cells 6-9: space
		}
		CHECK(note > 0.02, "roller notes silent (%.4f)", note / 3);
		CHECK(space < 0.03 * note, "spaces not silent: %.4f vs notes %.4f", space / 3, note / 3);
		std::printf("roller: notes rms %.4f, spaces %.5f (%.2f %%)\n", note / 3, space / 3, 100.0 * space / note);
	}

	// ---- 3. CLK unpatched: free-running (RUN ALWAYS) still plays and switches
	{
		N::Engine e;
		e.prepare(SR, B);
		N::Brain b;
		for (int s = 0; s < N::kSteps; s++)
			b.steps[(size_t) s].trig.store(1 + s % 3);
		N::Params p;
		p.source = N::SrcSelf;
		p.mult = 4.f;
		p.rhythmOn = false;
		const Run r = render(e, b, p, 2.0, false);
		CHECK(rms(r.L, 0, r.L.size()) > 0.02, "free run silent");
		CHECK(r.arts.size() >= 5, "free run: %zu switches", r.arts.size());
		std::printf("free run: rms %.4f, %zu switches\n", rms(r.L, 0, r.L.size()), r.arts.size());
	}

	// ---- 4. every model renders sane on its own
	for (int m = 0; m < N::NumModels; m++) {
		N::Engine e;
		e.prepare(SR, B);
		N::Brain b;
		for (int s = 0; s < N::kSteps; s++)
			b.steps[(size_t) s].trig.store(1);
		N::Params p;
		p.source = N::SrcSelf;
		p.rhythmOn = false;
		p.art[0].model = m;
		p.art[0].grit = 0.9f;
		p.art[0].tone = 0.9f;
		p.sub = 0.f;
		const Run r = render(e, b, p, 1.0, true);
		bool finite = true;
		float peak = 0.f;
		for (float x : r.L) {
			finite &= std::isfinite(x);
			peak = std::max(peak, std::abs(x));
		}
		CHECK(finite && peak < 1.0f && rms(r.L, 0, r.L.size()) > 1e-3, "model %s: finite %d peak %.3f rms %.5f", N::modelName(m), finite,
		      peak, rms(r.L, 0, r.L.size()));
	}

	std::printf("%d / %d checks passed\n", checks - fails, checks);
	return fails ? 1 : 0;
}
