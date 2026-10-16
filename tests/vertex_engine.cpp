// Offline check of the vendored VERTEX engine as the Rack module drives it (16-sample blocks,
// clock -> ppq, TRIG -> note events). Not part of the plugin build.
//   g++ -std=c++17 -O2 -Iengines tests/vertex_engine.cpp engines/vertex/Engine.cpp -o build/vertex_test
//   ./build/vertex_test
#include "vertex/Engine.h"
#include "vertex/Factory.h"
#include <chrono>
#include <cmath>
#include <cstdio>

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

struct Result { bool finite = true; float peak = 0.f; double rms = 0.0; unsigned trig = 0; };

// renders `seconds` of a preset on a running 16th clock at `bpm`
static Result render(const vx::Params& p, double bpm, double seconds, bool ext = false) {
	constexpr double SR = 48000.0;
	constexpr int B = 16;
	vx::Engine e;
	e.prepare(SR);
	Result r;
	float out[B];
	const long total = (long) (SR * seconds);
	double ppq = 0.0;
	const double pps = bpm / 60.0 / SR;
	const long trigEvery = (long) (SR * 60.0 / bpm / 4.0);   // 16ths for the EXT test
	double sum = 0.0;
	for (long n0 = 0; n0 + B <= total; n0 += B) {
		vx::Transport tr;
		tr.running = !ext;
		tr.hostPlaying = !ext;
		tr.ppq = ppq;
		tr.ppqPerSample = pps;
		vx::MidiEvt ev[2];
		int nev = 0;
		for (int k = 0; ext && k < B; k++)
			if ((n0 + k) % trigEvery == 0)
				ev[nev++] = {k, 48 + (int) (((n0 + k) / trigEvery) % 12), 0.9f};
		e.process(p, tr, ev, nev, nullptr, out, B);
		ppq += B * pps;
		for (int i = 0; i < B; i++) {
			if (!std::isfinite(out[i]))
				r.finite = false;
			r.peak = std::max(r.peak, std::abs(out[i]));
			sum += (double) out[i] * out[i];
		}
	}
	r.rms = std::sqrt(sum / (double) total);
	r.trig = e.triggers.load();
	return r;
}

int main() {
	// INIT (the device's own initial values): one trigger per 16th
	{
		const Result r = render(vx::initParams(), 120.0, 4.0);
		CHECK(r.finite, "INIT not finite");
		CHECK(r.rms > 1e-3, "INIT silent (rms %g)", r.rms);
		CHECK(r.peak < 4.f, "INIT peak %g", r.peak);
		CHECK(r.trig >= 31 && r.trig <= 33, "INIT triggers %u (want 32 16ths in 4 s @120)", r.trig);
		std::printf("INIT: peak %.3f rms %.4f triggers %u\n", r.peak, r.rms, r.trig);
	}
	// every factory preset
	for (int i = 0; i < vx::numFactory(); i++) {
		const auto& f = vx::factory(i);
		const bool ext = f.params.clock >= vx::kNumRates;
		const Result r = render(f.params, f.bpm, 4.0, ext);
		CHECK(r.finite, "%s not finite", f.name);
		CHECK(r.rms > 1e-4, "%s silent", f.name);
		CHECK(r.peak < 8.f, "%s peak %g", f.name, r.peak);
		CHECK(r.trig > 0, "%s no triggers", f.name);
		std::printf("%-20s peak %.3f rms %.4f triggers %u%s\n", f.name, r.peak, r.rms, r.trig, ext ? " (EXT)" : "");
	}
	// master clock 8n halves the triggers
	{
		vx::Params p = vx::initParams();
		p.clock = 2;
		const Result r = render(p, 120.0, 4.0);
		CHECK(r.trig >= 15 && r.trig <= 17, "8n triggers %u", r.trig);
	}
	// stopped transport: no triggers, silence
	{
		vx::Engine e;
		e.prepare(48000.0);
		vx::Transport tr;
		float out[16];
		float pk = 0.f;
		for (int b = 0; b < 3000; b++) {
			e.process(vx::initParams(), tr, nullptr, 0, nullptr, out, 16);
			for (float v : out)
				pk = std::max(pk, std::abs(v));
		}
		CHECK(e.triggers.load() == 0 && pk < 1e-4f, "stopped: %u triggers, peak %g", e.triggers.load(), pk);
	}
	// cost (desktop; the MetaModule's A7 is roughly 10-20x slower per core)
	{
		const auto t0 = std::chrono::steady_clock::now();
		vx::Params p = vx::factory(7).params;   // Industrial Loop: drive, 24 dB, FM
		render(p, 128.0, 10.0);
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		std::printf("cost: %.2f %% of one core (10 s rendered in %.0f ms)\n", ms / 100.0, ms);
	}
	std::printf("%d checks, %d failures\n", checks, fails);
	return fails ? 1 : 0;
}
