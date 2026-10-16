// Offline check of the vendored IKEDAT engine (engines/ikedat, namespace ikt) as the Rack module drives
// it: 32-sample blocks, free tempo and a host-style transport (the module's CLK), data from bytes.
// Not part of the plugin build:
//   g++ -std=c++17 -O2 -Iengines tests/ikedat_engine.cpp engines/ikedat/Ikedar.cpp -o build/ikedat_test
//   ./build/ikedat_test
#include "ikedat/Hybrid.h"
#include "ikedat/IkedarFactory.h"
#include <cmath>
#include <cstdio>
#include <memory>

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

using namespace ikt;

namespace {
constexpr int B = 32;
constexpr double SR = 48000.0;
const int kDefTrig[pb::kSteps] = {1, 0, 4, 0, 2, 7, 0, 5, 1, 0, 3, 0, 6, 0, 8, 4};

struct Run {
	double rms = 0, peak = 0;
	bool finite = true;
	int fires = 0, mutes = 0;
	uint32_t sixteenths = 0;
};

void factoryRows(hy::Engine& e) {
	auto& sh = e.ikShared;
	for (int w = 0; w < ryoz::kMasterWords; ++w) sh.master[(size_t) w].store(ryoz::factory::kMaster[w]);
	const auto baseMs = (float) ryoz::Engine::stepMsForMasterSum(sh.masterSum());
	for (int v = 0; v < ryoz::kNumVoices; ++v) {
		auto& vs = sh.voices[(size_t) v];
		vs.pitch.store(ryoz::factory::kPitch[v]);
		vs.cells.store(ryoz::factory::kCells[v]);
		vs.gain.store(ryoz::factory::kGain[v]);
		vs.decay.store(ryoz::factory::kDecay[v]);
		vs.rule.store(ryoz::kVoiceSpecs[v].savedRule);
		vs.gateBaseMs.store(baseMs);
		vs.signs.store(0x9e3779b97f4a7c15ull * (uint64_t) (v + 1));
	}
	sh.loadSerial.fetch_add(1);
}

Run play(hy::Engine& e, hy::Controls c, double seconds, bool host, double bpm = 125.0) {
	Run r;
	float L[B], R[B];
	double sum = 0.0, ppq = 0.0;
	const uint32_t s0 = e.sixteenthView.load();
	const int blocks = (int) (seconds * SR / B);
	for (int b = 0; b < blocks; ++b) {
		hy::Host h;
		h.valid = host;
		h.playing = host;
		h.bpm = bpm;
		h.ppq = ppq;
		c.hostSync = host;
		pb::FireSink fs;
		e.process(L, R, B, h, c, fs);
		ppq += bpm / 60.0 / SR * B;
		for (int k = 0; k < fs.count; ++k)
			(fs.items[(size_t) k].lane >= 0 ? r.fires : r.mutes)++;
		for (int i = 0; i < B; ++i) {
			if (!std::isfinite(L[i]) || !std::isfinite(R[i])) r.finite = false;
			sum += L[i] * L[i] + R[i] * R[i];
			r.peak = std::max(r.peak, (double) std::max(std::fabs(L[i]), std::fabs(R[i])));
		}
	}
	r.rms = std::sqrt(sum / (2.0 * blocks * B));
	r.sixteenths = e.sixteenthView.load() - s0;
	return r;
}
} // namespace

int main() {
	std::vector<uint8_t> bytes(32768);
	uint32_t x = 7;
	for (auto& v : bytes) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; v = (uint8_t) x; }
	auto data = dat::DataSet::fromBytes("noise.bin", bytes);
	CHECK(data && data->size() == bytes.size() && data->entropy > 7.9, "dataset");
	auto text = dat::DataSet::fromText("IKEDAT // ikedar x jdat x pinball // any file becomes sound and image.");
	CHECK(text->kind == "TYPED TEXT", "text kind %s", text->kind.c_str());

	static pb::Matrix chain = pb::chainMatrix();
	hy::Controls c;
	c.pin.matrix = &chain;
	for (int s = 0; s < pb::kSteps; ++s) c.pin.steps[(size_t) s].trig = kDefTrig[s];
	for (auto& st : c.pin.steps) st.duration = 1.f;

	// free tempo, 125 BPM: 16ths at 8.33 Hz
	{
		auto e = std::make_unique<hy::Engine>();
		e->prepare(SR);
		factoryRows(*e);
		e->setData(data.get());
		const Run r = play(*e, c, 8.0, false);
		std::printf("free   rms %.3f peak %.3f fires %d mutes %d 16ths %u\n", r.rms, r.peak, r.fires, r.mutes, r.sixteenths);
		CHECK(r.finite && r.peak <= 1.0 && r.rms > 1e-3, "free: finite / bounded / sound");
		CHECK(r.sixteenths >= 64 && r.sixteenths <= 68, "free: %u sixteenths in 8 s at 125 BPM", r.sixteenths);
		CHECK(r.fires > 30, "free: lanes fired %d", r.fires);
		CHECK(e->headView.load() > 0, "the head moves (linear scan)");
	}
	// host transport (the module's CLK): 140 BPM, ball scan, data block writes
	{
		auto e = std::make_unique<hy::Engine>();
		e->prepare(SR);
		factoryRows(*e);
		e->setData(data.get());
		hy::Controls d = c;
		d.scan = hy::scanBall;
		d.jdWrite = 2;
		d.ikMode = hy::ikTrigOnly;
		const Run r = play(*e, d, 8.0, true, 140.0);
		std::printf("host   rms %.3f peak %.3f fires %d mutes %d 16ths %u\n", r.rms, r.peak, r.fires, r.mutes, r.sixteenths);
		CHECK(r.finite && r.peak <= 1.0 && r.rms > 1e-3, "host: finite / bounded / sound");
		CHECK(r.sixteenths >= 73 && r.sixteenths <= 76, "host: %u sixteenths in 8 s at 140 BPM", r.sixteenths);
		// halt freezes the ball
		hy::Controls h = d;
		h.pin.halt = true;
		const Run q = play(*e, h, 1.0, true, 140.0);
		CHECK(q.fires <= 1, "halt: fired %d", q.fires);   // one already-scheduled trig may land
		// an empty matrix drains the ball after the launch
		static pb::Matrix empty{};
		hy::Controls m = d;
		m.pin.matrix = &empty;
		e->relaunch();
		const Run z = play(*e, m, 1.0, true, 140.0);
		CHECK(z.fires <= 1, "empty matrix fired %d", z.fires);
	}
	// no data: the patches' own random numbers; clip off stays finite
	{
		auto e = std::make_unique<hy::Engine>();
		e->prepare(SR);
		factoryRows(*e);
		hy::Controls d = c;
		d.useData = false;
		d.clip = false;
		const Run r = play(*e, d, 4.0, false);
		CHECK(r.finite && r.rms > 1e-3, "no data: rms %.4f", r.rms);
	}
	std::printf("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
