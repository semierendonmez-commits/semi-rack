// Offline check of the vendored RETICULUM engine (not part of the plugin build).
//
//   sh tests/reticulum_engine.sh <Rack-SDK>
//
// artifex's headers need the Rack headers (inline helpers only); libRack is linked for the
// symbols plugin.hpp declares. Checks: silent with zero node gains and nothing wired;
// sounding after a gate on the init patch; finite and within +-1 everywhere (every artifex
// mode, every Beads quality, every FX order, three sample rates, modulators and free clock
// on, the output stage driven); the gate reseeds; a modulator moves LEAK.
#include "reticulum/Engine.h"
#include <cmath>
#include <cstdio>
#include <vector>

Plugin* pluginInstance = nullptr;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); ++fails; } } while (0)

struct Stats { float peak = 0.f, rms = 0.f; bool finite = true; };

static Stats run(rt::Engine& e, rt::Params& p, double sr, float seconds, bool pad) {
	const int n = 32;
	std::vector<float> l(n), r(n);
	Stats s;
	double acc = 0.0;
	long count = 0;
	const long blocks = (long) (seconds * sr / n);
	for (long b = 0; b < blocks; b++) {
		p.pad = pad && b < blocks / 4;   // hold the gate for the first quarter
		e.process(p, nullptr, nullptr, l.data(), r.data(), n);
		for (int i = 0; i < n; i++) {
			if (!std::isfinite(l[i]) || !std::isfinite(r[i])) s.finite = false;
			s.peak = std::max(s.peak, std::max(std::fabs(l[i]), std::fabs(r[i])));
			acc += l[i] * l[i] + r[i] * r[i];
			count += 2;
		}
	}
	s.rms = (float) std::sqrt(acc / std::max(1L, count));
	return s;
}

static rt::Params defaults() {
	rt::Params p;
	p.gain.fill(0.5f);
	for (int k = 0; k < 7; k++) p.wire[k] = 1u << (rt::kColA + k);
	p.wire[7] = 1u << rt::kColB;
	const int dest[rt::kDests] = {rt::gGate, rt::gGate, rt::gGate, rt::gGate, rt::gGate, rt::gOff, rt::gGate, rt::gOff};
	for (int d = 0; d < rt::kDests; d++) p.dest[d] = dest[d];
	p.axAmt = 0.3f; p.axFbk = 0.35f; p.axTime = 0.65f;
	return p;
}

int main() {
	// silence: no node gain, nothing wired, no gate
	{
		rt::Engine e;
		e.prepare(48000.0, 32);
		rt::Params p = defaults();
		p.gain.fill(0.f);
		p.wire.fill(0u);
		p.axOn = false;
		Stats s = run(e, p, 48000.0, 1.f, false);
		CHECK(s.finite && s.peak < 1e-3f, "zero gain should be silent (peak %g)", s.peak);
	}
	// the init patch: press the pad, it sounds and reseeds
	{
		rt::Engine e;
		e.prepare(48000.0, 32);
		rt::Params p = defaults();
		const uint32_t before = e.seed();
		Stats s = run(e, p, 48000.0, 3.f, true);
		std::printf("init net: peak %.3f rms %.3f seed %08x -> %08x\n", s.peak, s.rms, before, e.seed());
		CHECK(s.finite, "init net not finite");
		CHECK(s.rms > 0.01f, "init net silent (rms %g)", s.rms);
		CHECK(s.peak <= 1.0001f, "init net out of range (peak %g)", s.peak);
		CHECK(e.seed() != before, "gate did not reseed");
	}
	// every artifex mode / Beads quality / order at three rates, modulators on, free clock
	for (double sr : {44100.0, 48000.0, 96000.0})
		for (int order = 0; order < rt::kOrders; order++)
			for (int mode = 0; mode < 9; mode++) {
				rt::Engine e;
				e.prepare(sr, 32);
				rt::Params p = defaults();
				p.order = order;
				p.axMode = mode;
				p.axAmt = 0.7f; p.axFbk = 0.6f;
				p.bdOn = true; p.bdQuality = mode % 4; p.bdMix = 0.6f; p.bdVerb = 0.4f; p.bdFbk = 0.5f;
				p.clockMode = rt::cFree; p.clockHz = 3.f; p.flux = 0.7f;
				p.scale = 0.9f;
				p.level = 12.f;   // drive the output stage into its clip
				for (auto& m : p.mod) { m.mode = rt::mDrunk; m.rate = 4.f; m.depth = 1.f; }
				Stats s = run(e, p, sr, 1.5f, true);
				CHECK(s.finite && s.peak <= 1.0001f && s.rms > 1e-3f,
				      "sr %.0f order %d mode %d: finite %d peak %g rms %g", sr, order, mode, (int) s.finite, s.peak, s.rms);
			}
	// the modulators move what rete gets
	{
		rt::Engine e;
		e.prepare(48000.0, 32);
		rt::Params p = defaults();
		p.mod[8] = {rt::mWalk, rt::sFree, 8.f, 1.f};
		float lo = 1.f, hi = 0.f;
		std::vector<float> l(32), r(32);
		for (int b = 0; b < 48000 / 32; b++) {
			e.process(p, nullptr, nullptr, l.data(), r.data(), 32);
			const float v = e.vis.effective[8].load();
			lo = std::min(lo, v);
			hi = std::max(hi, v);
		}
		CHECK(hi - lo > 0.2f, "walk on LEAK did not move it (%g..%g)", lo, hi);
	}
	std::printf(fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails ? 1 : 0;
}
