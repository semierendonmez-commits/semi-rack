// Offline check of the vendored NMS engine + Host as the Rack module drives it (32-sample
// blocks): every factory patch builds, plays (finite, bounded, audible), recalls patterns,
// follows an external clock; prints the CPU cost per second of audio. Not part of the build.
//   g++ -std=c++17 -O2 -Iengines tests/nms_engine.cpp engines/nms/*.cpp -o build/nms_test
//   ./build/nms_test
#include "nms/Host.h"
#include <chrono>
#include <cmath>
#include <cstdio>

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

int main() {
	constexpr double SR = 48000.0;
	constexpr int B = 32;
	const auto& patches = nms::factoryPatches();
	for (int p = 0; p < (int) patches.size(); p++) {
		nms::Host h;
		CHECK(h.build(p, SR), "patch %d builds", p);
		float L[B], R[B];
		double peak = 0.0, sum = 0.0;
		bool finite = true;
		nms::ExtTransport ext;
		const auto t0 = std::chrono::steady_clock::now();
		const int blocks = (int) (6.0 * SR / B);
		for (int b = 0; b < blocks; b++) {
			if (p == 6 && b % 1500 == 10) h.noteOn(48 + (b / 1500) % 12, 0.8f);   // Keys > Sawbird: play it
			if (p == 6 && b % 1500 == 700) h.noteOff(48 + (b / 1500) % 12);
			h.process(L, R, B, ext);
			for (int i = 0; i < B; i++) {
				finite = finite && std::isfinite(L[i]) && std::isfinite(R[i]);
				peak = std::max(peak, (double) std::max(std::fabs(L[i]), std::fabs(R[i])));
				sum += L[i] * L[i];
			}
		}
		const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		const double rms = std::sqrt(sum / (blocks * B));
		const bool blank = std::string(patches[(size_t) p].name) == "Blank Slate";
		CHECK(finite, "%s finite", patches[(size_t) p].name);
		CHECK(peak <= 1.0, "%s bounded (soft clip) peak %.3f", patches[(size_t) p].name, peak);
		CHECK(blank || rms > 0.003, "%s audible (rms %.4f)", patches[(size_t) p].name, rms);
		std::printf("  %-26s mods %2zu cables %zu patterns %2zu  rms %.3f peak %.3f  cpu %.1f%% of real time\n", patches[(size_t) p].name,
		            h.rack.mods.size(), h.rack.cables.size(), h.patterns.size(), rms, peak, 100.0 * sec / 6.0);

		// pattern recall changes controls (when there are patterns)
		if (h.patterns.size() >= 2) {
			h.recall(0, 0.0);
			int diff = 0;
			const auto& v1 = h.patterns[1].values;
			const auto& v0 = h.patterns[0].values;
			for (auto& [id, vals] : v1) {
				auto it = v0.find(id);
				if (it == v0.end()) continue;
				for (size_t i = 0; i < vals.size(); i++)
					if (!std::isnan(vals[i]) && std::fabs(vals[i] - it->second[i]) > 1e-4f) diff++;
			}
			CHECK(diff > 0 && h.currentPattern == 0, "%s dealt patterns differ (%d controls)", patches[(size_t) p].name, diff);
		}
	}

	// external clock: 16ths at 120 BPM drive the position; stopping the clock stops the transport
	{
		nms::Host h;
		h.build(0, SR);
		float L[B], R[B];
		nms::ExtTransport ext;
		ext.known = true;
		ext.playing = true;
		ext.bpm = 120.0;
		for (int b = 0; b < 3000; b++) {
			ext.pos16 = b * B / (SR * 60.0 / 120.0 / 4.0);
			h.process(L, R, B, ext);
		}
		const double expect = 3000.0 * B / (SR * 60.0 / 120.0 / 4.0);
		CHECK(std::fabs(h.ctx.pos16 - expect) < 1.0, "pos16 follows the clock (%.2f vs %.2f)", h.ctx.pos16, expect);
		ext.playing = false;
		h.process(L, R, B, ext);
		CHECK(!h.ctx.running, "clock stopped -> transport stopped");
	}

	// macros move controls: SPACE (23) to the top raises BAIKAL's mod offsets somewhere
	{
		nms::Host h;
		h.build(0, SR);
		float L[B], R[B];
		nms::ExtTransport ext;
		h.macro[23] = 1.0f;
		h.process(L, R, B, ext);
		float maxMod = 0.f;
		for (auto& m : h.rack.mods)
			for (size_t i = 0; i < m->n; i++) maxMod = std::max(maxMod, std::fabs(m->modOffset((int) i)));
		CHECK(maxMod > 0.05f, "a macro moves controls (max offset %.3f)", maxMod);
	}

	std::printf("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
