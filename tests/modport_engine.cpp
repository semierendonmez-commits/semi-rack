// Offline check of the vendored MODPORT engine as the Rack module drives it
// (32-sample blocks through modport::drv::Driver: free tempo, one row per clock,
// NEXT jumps, per-channel triggers). Not part of the plugin build.
//   g++ -std=c++17 -O2 -Iengines tests/modport_engine.cpp engines/modport/Module.cpp engines/modport/Player.cpp -o build/modport_test
//   ./build/modport_test
#include "modport/Driver.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace modport;

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

// A hand-written module (as the plugin's EngineTests): `channels` channels, `numPatterns`
// patterns, a looped square and a one-shot click; a note every 4 rows on every channel.
static std::vector<uint8_t> makeMod(int channels, const char* tag, int numPatterns, int songLength) {
	std::vector<uint8_t> d(1084, 0);
	std::memcpy(d.data(), "rack test", 9);
	auto setSample = [&](int s, const char* name, int words, int vol, int ls, int ll) {
		uint8_t* p = d.data() + 20 + s * 30;
		std::memcpy(p, name, std::strlen(name));
		p[22] = (uint8_t) (words >> 8); p[23] = (uint8_t) words;
		p[25] = (uint8_t) vol;
		p[26] = (uint8_t) (ls >> 8); p[27] = (uint8_t) ls;
		p[28] = (uint8_t) (ll >> 8); p[29] = (uint8_t) ll;
	};
	setSample(0, "square", 32, 64, 0, 32);
	setSample(1, "click", 64, 48, 0, 1);
	d[950] = (uint8_t) songLength;
	d[951] = 0x7F;
	for (int i = 0; i < songLength; ++i) d[952 + i] = (uint8_t) (i % numPatterns);
	std::memcpy(d.data() + 1080, tag, 4);
	static const uint16_t per[] = {428, 339, 285, 214};
	for (int p = 0; p < numPatterns; ++p)
		for (int r = 0; r < 64; ++r)
			for (int c = 0; c < channels; ++c) {
				Cell cl;
				if (r % 4 == 0) { cl.period = per[(c + r / 4) % 4]; cl.sample = (uint8_t) (1 + (c & 1)); }
				d.push_back((uint8_t) ((cl.sample & 0xF0) | (cl.period >> 8)));
				d.push_back((uint8_t) cl.period);
				d.push_back((uint8_t) (((cl.sample & 15) << 4) | cl.effect));
				d.push_back(cl.param);
			}
	for (int i = 0; i < 64; ++i) d.push_back((uint8_t) (i < 32 ? 0x60 : 0xA0));
	for (int i = 0; i < 128; ++i) d.push_back((uint8_t) (std::sin(i * 0.4) * 100.0));
	return d;
}

int main() {
	constexpr double SR = 48000.0;
	constexpr int B = 32;
	const auto bytes = makeMod(8, "8CHN", 2, 3);
	LoadResult r = loadModule(bytes.data(), bytes.size());
	CHECK(r.ok, "8CHN loads (%s)", r.error.c_str());
	CHECK(r.module.channels == 8, "8 channels (got %d)", r.module.channels);

	Player p;
	p.prepare(SR);
	p.setModule(&r.module);
	drv::Driver d;
	d.sr = SR;
	d.attach(&p);

	float st[kMaxChannels][B];
	float* stems[kMaxChannels];
	for (int c = 0; c < kMaxChannels; ++c) stems[c] = st[c];
	float L[B], R[B];

	// ---- free mode: 2 s at the song's tempo (speed 6, 125 BPM -> 50 Hz ticks, 8.33 rows / s)
	int trigs[kMaxChannels] = {};
	double peak = 0.0;
	bool finite = true;
	for (int b = 0; b < (int) (2.0 * SR / B); ++b) {
		for (int c = 0; c < kMaxChannels; ++c) { std::fill(st[c], st[c] + B, 0.f); d.trigOffset[c] = -1; }
		d.render(stems, 0, B, true);
		d.mix(stems, 8, L, R, B);
		for (int i = 0; i < B; ++i) {
			finite = finite && std::isfinite(L[i]) && std::isfinite(R[i]);
			peak = std::max(peak, (double) std::max(std::fabs(L[i]), std::fabs(R[i])));
		}
		for (int c = 0; c < kMaxChannels; ++c) trigs[c] += d.trigOffset[c] >= 0;
	}
	CHECK(finite, "free mode output finite");
	CHECK(peak > 0.05 && peak <= 1.5, "free mode audible and bounded (peak %.3f)", peak);
	const int rowsFree = p.getRow();
	CHECK(rowsFree >= 14 && rowsFree <= 18, "2 s at speed 6 / 125 BPM ~ 16 rows (row %d)", rowsFree);
	for (int c = 0; c < 8; ++c) CHECK(trigs[c] >= 3, "channel %d triggered in free mode (%d)", c + 1, trigs[c]);
	CHECK(std::fabs(drv::periodToVolts(428)) < 1e-6 && std::fabs(drv::periodToVolts(214) - 1.f) < 1e-3, "428 = 0 V, 214 = +1 V");

	// ---- clock mode: 20 clock edges (one every 1200 samples) -> 20 rows from the start
	d.restart();
	d.clockMode = true;
	d.clockPeriod = 1200.0;
	int edges = 0, firstRowSeen = -1;
	for (int n = 0; n < 1200 * 20; n += B) {
		for (int c = 0; c < kMaxChannels; ++c) std::fill(st[c], st[c] + B, 0.f);
		int a = 0;
		const int e = (n + 1199) / 1200 * 1200;   // next edge at or after n
		if (e < n + B) {
			const int off = e - n;
			d.render(stems, a, off, true);
			d.curOffset = off;
			d.clockEdge();
			if (firstRowSeen < 0) firstRowSeen = p.getRow();
			++edges;
			a = off;
		}
		d.render(stems, a, B, true);
	}
	CHECK(firstRowSeen == 0, "first clock after reset plays row 0 (row %d)", firstRowSeen);
	CHECK(p.getRow() == edges - 1 && p.getOrder() == 0, "row follows the clock: %d edges -> row %d (order %d)", edges, p.getRow(), p.getOrder());
	// no clock for a long time: the row must not advance
	const int held = p.getRow();
	for (int k = 0; k < 400; ++k) {
		for (int c = 0; c < kMaxChannels; ++c) std::fill(st[c], st[c] + B, 0.f);
		d.render(stems, 0, B, true);
	}
	CHECK(p.getRow() == held, "rows wait for the clock (row %d -> %d)", held, p.getRow());
	// fast clock: edges every 100 samples with speed 6 -> each edge still exactly one row
	d.clockPeriod = 100.0;
	for (int k = 0; k < 10; ++k) {
		for (int c = 0; c < kMaxChannels; ++c) std::fill(st[c], st[c] + B, 0.f);
		d.clockEdge();
		d.render(stems, 0, B, true);
	}
	CHECK(p.getRow() == held + 10, "fast clock: 10 edges = 10 rows (row %d)", p.getRow());

	// ---- NEXT: jump to the next order keeping speed / tempo
	const int sp = p.getSpeed(), bp = p.getTempo();
	p.restart(1, 0);
	p.setSpeedTempo(sp, bp);
	d.clockEdge();
	CHECK(p.getOrder() == 1 && p.getRow() == 0, "NEXT -> order 1 row 0 (order %d row %d)", p.getOrder(), p.getRow());

	// ---- > 8 channels refused
	const auto big = makeMod(10, "10CH", 1, 1);
	LoadResult r2 = loadModule(big.data(), big.size());
	CHECK(!r2.ok, "10CH refused");

	std::printf("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
