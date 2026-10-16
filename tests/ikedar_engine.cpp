// Offline check of the vendored IKEDAR engine as the Rack module drives it
// (32-sample blocks, internal metro, then externalStep() from a 1/16 clock). Not part of the build.
//   g++ -std=c++17 -O2 -Iengines tests/ikedar_engine.cpp engines/ikedar/Engine.cpp -o build/ikedar_test
//   ./build/ikedar_test
#include "ikedar/Engine.h"
#include "ikedar/FactoryPreset.h"
#include <cmath>
#include <cstdio>

using namespace ikedar_ln;

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static void loadOriginal(SharedState& s) {
	for (int w = 0; w < kMasterWords; w++) s.master[(size_t) w].store(factory::kMaster[w]);
	for (int v = 0; v < kNumVoices; v++) {
		auto& vs = s.voices[(size_t) v];
		vs.pitch.store(factory::kPitch[v]);
		vs.cells.store(factory::kCells[v]);
		vs.gain.store(factory::kGain[v]);
		vs.decay.store(factory::kDecay[v]);
		vs.rule.store(kVoiceSpecs[v].savedRule);
		vs.gateBaseMs.store((float) Engine::stepMsForMasterSum(66));
		vs.signs.store(0x5555aaaa3333ccccull * (v + 1));
	}
	s.loadSerial.fetch_add(1);
}

int main() {
	constexpr double SR = 48000.0;
	constexpr int B = 32;
	SharedState sh;
	loadOriginal(sh);
	Engine e(sh);
	e.prepare(SR);
	Engine::Controls c;
	Engine::HostInfo host;
	float L[B], R[B];

	CHECK(sh.masterSum() == 66, "master sum %d", sh.masterSum());

	// 4 s internal metro
	double energy = 0, peak = 0;
	bool finite = true;
	int steps = 0, lastStep = -1;
	for (int b = 0; b < (int) (SR * 4 / B); b++) {
		e.process(L, R, B, host, c);
		for (int i = 0; i < B; i++) {
			finite &= std::isfinite(L[i]) && std::isfinite(R[i]);
			energy += L[i] * L[i] + R[i] * R[i];
			peak = std::max(peak, (double) std::max(std::fabs(L[i]), std::fabs(R[i])));
		}
		if (sh.step.load() != lastStep) { lastStep = sh.step.load(); steps++; }
	}
	const double rms = std::sqrt(energy / (SR * 4 * 2));
	CHECK(finite, "non-finite output");
	CHECK(peak <= 1.0001, "peak %f above the DAC clip", peak);
	CHECK(rms > 0.01, "rms %f (silent?)", rms);
	CHECK(sh.dacOn.load() && sh.clockRunning.load(), "dac/clock off");
	// 132 BPM 16ths = 8.8 steps/s -> ~35 in 4 s
	CHECK(steps > 28 && steps < 42, "internal steps %d", steps);
	std::printf("internal: rms %.3f peak %.3f steps %d bpm %.0f\n", rms, peak, steps, sh.bpm.load());

	// mutation changes the row
	const uint64_t before = sh.voices[0].cells.load();
	sh.voices[0].mutateRequests.fetch_add(1);
	e.process(L, R, B, host, c);
	CHECK(sh.voices[0].cells.load() != before, "mutation did not change the row");
	CHECK(sh.voices[0].generation.load() == 1, "generation count");

	// external clock: run = false, externalStep every 1/16 at 100 BPM
	c.run = false;
	const int stepSamples = (int) (SR * 60.0 / 100.0 / 4.0);
	int extSteps = 0, since = 0;
	energy = 0;
	bool restartOk = true;
	for (int b = 0; b < (int) (SR * 2 / B); b++) {
		int done = 0;
		for (int i = 0; i < B; i++)
			if (++since >= stepSamples) {
				since = 0;
				if (i > done) { e.process(L + done, R + done, i - done, host, c); done = i; }
				e.externalStep(150.0, extSteps == 0);
				if (extSteps == 0) restartOk = e.currentStep() == 0;
				extSteps++;
			}
		if (done < B) e.process(L + done, R + done, B - done, host, c);
		for (int i = 0; i < B; i++) energy += L[i] * L[i] + R[i] * R[i];
	}
	CHECK(restartOk, "restart step is not step 1");
	CHECK(e.currentStep() == (extSteps - 1) % 64, "external step %d vs %d", e.currentStep(), extSteps);
	CHECK(std::sqrt(energy / (SR * 2 * 2)) > 0.01, "external clock silent");
	std::printf("external: %d steps, step %d\n", extSteps, e.currentStep());

	// no metro and no external steps -> envelopes decay to silence
	for (int b = 0; b < (int) (SR / B); b++) e.process(L, R, B, host, c);
	double tail = 0;
	for (int i = 0; i < B; i++) tail += std::fabs(L[i]) + std::fabs(R[i]);
	CHECK(tail < 1e-3, "stopped clock still sounds (%f)", tail);

	std::printf("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
