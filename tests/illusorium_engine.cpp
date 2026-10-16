// Offline check of the vendored ILLUSORIUM engine as the Rack module drives it: one illusion at a
// time, 32-sample blocks, a running 120 BPM transport, a test input (saw chord + pink noise +
// clicks). Every module: finite, bounded, non-silent; its cost and its heap after prepare() are
// printed (the numbers behind the MetaModule list in src/modules/Illusorium.cpp).
// Not part of the plugin build.
//   g++ -std=c++17 -O2 -Iengines tests/illusorium_engine.cpp engines/illusorium/*.cpp -o build/illusorium_test
//   ./build/illusorium_test        (add -DMETAMODULE for the MetaModule variants)
#include "illusorium/Module.h"
#include <chrono>
#include <string>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <malloc.h>

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

int main(int argc, char** argv) {
	const int only = argc > 1 ? std::atoi(argv[1]) : -1;   // one module (for callgrind)
	constexpr double SR = 48000.0;
	constexpr int B = 32;
	const double seconds = argc > 2 ? std::atof(argv[2]) : 6.0;
	il::Noise nz;
	double ph[3] = {0, 0, 0};
	const double f[3] = {220.0, 277.18, 329.63};
	for (int m = 0; m < il::numModules(); m++) {
		if (only >= 0 && m != only)
			continue;
		const il::MInfo& info = il::moduleInfo(m);
		const size_t heap0 = mallinfo2().uordblks + mallinfo2().hblkhd;
		il::ModulePtr mod = il::createModule(m);
		mod->prepare(SR, B);
		const size_t heap = mallinfo2().uordblks + mallinfo2().hblkhd - heap0;
		for (size_t k = 0; k < info.params.size(); k++)
			mod->p[k] = info.params[k].def;
		if (std::string(info.key) == "yan")
			mod->p[0] = 80.f;   // MORPH: the default 0 is neutral
		mod->reset();
		il::Ctx ctx;
		ctx.sr = SR;
		ctx.bpm = 120.0;
		ctx.playing = true;
		float L[B], R[B];
		bool finite = true;
		float peak = 0.f;
		double diff = 0.0, energy = 0.0;
		double ms = 0.0;
		const long total = (long) (SR * seconds);
		for (long n = 0; n + B <= total; n += B) {
			for (int i = 0; i < B; i++) {
				float x = 0.f;
				for (int k = 0; k < 3; k++) {
					ph[k] += f[k] / SR;
					ph[k] -= std::floor(ph[k]);
					x += 0.12f * (float) (2.0 * ph[k] - 1.0);
				}
				x += 0.15f * nz.pink();
				if ((n + i) % 12000 < 200)
					x += 0.5f * (float) std::exp(-(double) ((n + i) % 12000) / 40.0);
				L[i] = R[i] = x;
			}
			float inL[B];
			std::copy(L, L + B, inL);
			ctx.ppq = n * ctx.beatsPerSample();
			const auto t0 = std::chrono::steady_clock::now();
			mod->process(L, R, B, ctx);
			ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
			for (int i = 0; i < B; i++) {
				if (!std::isfinite(L[i]) || !std::isfinite(R[i]))
					finite = false;
				peak = std::max(peak, std::max(std::abs(L[i]), std::abs(R[i])));
				diff += (double) (L[i] - inL[i]) * (L[i] - inL[i]) + (double) (R[i] - inL[i]) * (R[i] - inL[i]);
				energy += (double) L[i] * L[i] + (double) R[i] * R[i];
			}
		}
		CHECK(finite, "%s not finite", info.name);
		CHECK(peak < 8.f, "%s peak %g", info.name, peak);
		CHECK(energy > 1.0, "%s silent", info.name);
		CHECK(diff > 1e-3 || m == il::numModules() - 1, "%s does nothing (wet == dry)", info.name);
		mod->trigger();
		std::printf("%2d %-15s heap %7.0f KB  cost %5.2f %%  peak %.2f  wet-dry %.3g\n", m, info.name, heap / 1024.0, ms / (seconds * 10.0),
		            peak, std::sqrt(diff / total));
	}
	std::printf("%d checks, %d failures\n", checks, fails);
	return fails ? 1 : 0;
}
