// Offline check of the PINDATA engine as the Rack module drives it (engines/pindata/Hybrid.h:
// 32-sample blocks, transport from a tempo, the factory presets, data from text / bytes).
// Not part of the plugin build:
//   g++ -std=c++17 -O2 -Iengines tests/pindata_engine.cpp engines/pindata/IkdEngine.cpp
//       engines/pindata/IngpEngine.cpp -o build/pindata_test && ./build/pindata_test
#include "pindata/Hybrid.h"
#include "pindata/ExampleTexts.h"
#include <cmath>
#include <cstdio>
#include <memory>

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

namespace {
constexpr int B = 32;
constexpr double SR = 48000.0;
double gPpq = 0.0;   // the transport, kept across play() calls on one engine

struct Run {
	double rms = 0, peak = 0;
	bool finite = true;
	uint32_t fires = 0, lanes = 0, ikd = 0, ingp = 0, jdat = 0, resets = 0;
};

Run play(pnd::Hybrid& h, const pnd::DataBlob* blob, pnd::Settings& s, double seconds, double bpm = 120.0) {
	Run r;
	float L[B], R[B];
	double& ppq = gPpq;
	double sum = 0.0;
	const int blocks = (int) (seconds * SR / B);
	const uint32_t f0 = h.stepFires, l0 = h.laneHitsTotal, i0 = h.ikdSteps, g0 = h.ingpTicks, j0 = h.jdatTicks, r0 = h.resets;
	for (int b = 0; b < blocks; ++b) {
		pnd::Transport t;
		t.playing = true;
		t.bpm = bpm;
		t.ppq = ppq;
		h.process(blob, L, R, B, s, t);
		ppq += bpm / 60.0 / SR * B;
		for (int i = 0; i < B; ++i) {
			if (!std::isfinite(L[i]) || !std::isfinite(R[i]))
				r.finite = false;
			sum += L[i] * L[i] + R[i] * R[i];
			r.peak = std::max(r.peak, (double) std::max(std::fabs(L[i]), std::fabs(R[i])));
		}
	}
	r.rms = std::sqrt(sum / (2.0 * blocks * B));
	r.fires = h.stepFires - f0;
	r.lanes = h.laneHitsTotal - l0;
	r.ikd = h.ikdSteps - i0;
	r.ingp = h.ingpTicks - g0;
	r.jdat = h.jdatTicks - j0;
	r.resets = h.resets - r0;
	return r;
}
} // namespace

int main() {
	// data: tst_ingp's first example text, as the module boots
	auto text = std::make_unique<pnd::DataBlob>();
	pnd::pdata::fromText(*text, pnd::examples::text(1), "example 1", pnd::pdata::ReadAs::Auto);
	CHECK(text->used == pnd::pdata::ReadAs::Text && text->nbits > 1000, "text blob: %llu bits", (unsigned long long) text->nbits);

	// any file as bytes
	std::vector<uint8_t> bytes(65536);
	uint32_t x = 1;
	for (auto& v : bytes) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; v = (uint8_t) x; }
	auto bin = std::make_unique<pnd::DataBlob>();
	pnd::pdata::setBytes(*bin, bytes.data(), bytes.size(), 1u << 20, "noise.bin", "/tmp/noise.bin");
	pnd::pdata::build(*bin, pnd::pdata::ReadAs::Auto);
	CHECK(bin->used == pnd::pdata::ReadAs::Bytes && bin->nbits == 65536u * 8u, "bytes blob");
	CHECK(bin->entropy > 7.9, "entropy of noise %.2f", bin->entropy);

	// a PNG header falls back to bytes (no image decoder in Rack)
	std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 13, 10, 26, 10, 1, 2, 3, 4};
	auto img = std::make_unique<pnd::DataBlob>();
	pnd::pdata::setBytes(*img, png.data(), png.size(), 1u << 20, "x.png", "");
	pnd::pdata::build(*img, pnd::pdata::ReadAs::Auto);
	CHECK(img->typeName == "PNG IMAGE" && img->used == pnd::pdata::ReadAs::Bytes, "png as bytes");

	// audio decoded by the caller
	std::vector<float> sine(4800);
	for (size_t i = 0; i < sine.size(); ++i) sine[i] = 0.3f * std::sin(i * 0.05f);
	auto wav = std::make_unique<pnd::DataBlob>();
	std::vector<uint8_t> riff = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E'};
	pnd::pdata::setBytes(*wav, riff.data(), riff.size(), 1u << 20, "x.wav", "");
	pnd::pdata::build(*wav, pnd::pdata::ReadAs::Auto, &sine, 48000.0, 1);
	CHECK(wav->used == pnd::pdata::ReadAs::Audio && wav->nbits == 4800 && std::fabs(wav->symbols[31] - 1.f) < 0.05f, "audio blob");

	for (int f = 0; f < pnd::kNumFactory; ++f) {
		auto h = std::make_unique<pnd::Hybrid>();
		h->prepare(SR);
		gPpq = 0.0;
		pnd::Settings s;
		h->loadFactory(f, s);
		const Run r = play(*h, f % 2 ? bin.get() : text.get(), s, 8.0);
		std::printf("factory %d %-16s rms %.3f peak %.3f fires %u lanes %u ikd %u ingp %u jdat %u resets %u\n", f,
		            pnd::factoryName(f), r.rms, r.peak, r.fires, r.lanes, r.ikd, r.ingp, r.jdat, r.resets);
		CHECK(r.finite, "factory %d not finite", f);
		CHECK(r.peak <= 1.0, "factory %d peak %.3f", f, r.peak);
		CHECK(r.rms > 1e-3, "factory %d silent (rms %.5f)", f, r.rms);
		CHECK(r.fires >= 16, "factory %d: the ball fired %u steps in 8 s", f, r.fires);
		CHECK(r.resets >= 1, "factory %d: no reset", f);
	}

	// default preset at 120 bpm: chain + factory weights, lanes reach every layer
	{
		auto h = std::make_unique<pnd::Hybrid>();
		h->prepare(SR);
		gPpq = 0.0;
		pnd::Settings s;
		h->loadFactory(0, s);
		const Run r = play(*h, text.get(), s, 10.0);
		CHECK(r.ikd > 10 && r.ingp > 10 && r.jdat > 5, "layers clocked by the lanes: ikd %u ingp %u jdat %u", r.ikd, r.ingp, r.jdat);
		CHECK(h->pathN > 0, "path");
		// pattern 4 bars at 120 bpm = 8 s -> one pattern reset plus the start in 10 s
		CHECK(r.resets == 2, "resets %u", r.resets);

		// halt stops the ball; reset relaunches it
		h->requestHalt();
		Run q = play(*h, text.get(), s, 1.0);
		CHECK(q.fires == 0, "halted but fired %u", q.fires);
		h->requestReset();
		q = play(*h, text.get(), s, 1.0);
		CHECK(q.fires >= 6, "reset relaunch fired %u", q.fires);

		// FIRE: a lane request plays its note and its destination
		const uint32_t before = h->laneHits[1];
		h->requestLanes(1u << 1);
		play(*h, text.get(), s, 0.01);
		CHECK(h->laneHits[1] == before + 1, "lane request");

		// the head moves with a lane's data step and with scan RUN
		s.scan = 2;
		s.scanRate = 16.f;
		const uint64_t h0 = h->head;
		play(*h, text.get(), s, 1.0);
		CHECK(h->head != h0, "scan run moves the head");

		// note state: lanes with Output on have notes; off kills them
		s.laneOut = false;
		play(*h, text.get(), s, 0.5);
		bool any = false;
		for (int l = 0; l < 8; ++l) any |= h->noteOn[(size_t) l] >= 0;
		CHECK(!any, "notes off without output");
	}

	// matrix functions keep rows in range; an empty matrix stops the ball
	{
		auto h = std::make_unique<pnd::Hybrid>();
		h->prepare(SR);
		gPpq = 0.0;
		pnd::Settings s;
		h->loadFactory(0, s);
		h->pinballFunction(pnd::Hybrid::Fn::ClearMatrix);
		Run r = play(*h, text.get(), s, 2.0);
		CHECK(r.fires <= 1, "empty matrix fired %u", r.fires);
		for (auto f : {pnd::Hybrid::Fn::MatrixDrunk, pnd::Hybrid::Fn::MatrixPingPong, pnd::Hybrid::Fn::MatrixJumps, pnd::Hybrid::Fn::MatrixRandom}) {
			h->pinballFunction(f);
			h->pinballFunction(pnd::Hybrid::Fn::RandomizeSteps);
			h->requestReset();
			r = play(*h, bin.get(), s, 2.0);
			CHECK(r.finite && r.fires > 4, "matrix fn %d fired %u", (int) f, r.fires);
		}
	}

	std::printf("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
