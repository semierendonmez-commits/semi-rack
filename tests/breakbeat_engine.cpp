// Offline check of the BREAKBEAT engine as the Rack module drives it (32-sample blocks, clock ->
// ppq, a synthetic loop) in every cut mode, plus the BBCut procedures on their own.
// Not part of the plugin build.
//   g++ -std=c++17 -O2 -Iengines tests/breakbeat_engine.cpp engines/breakbeat/SliceSelect.cpp engines/breakbeat/BBCut.cpp engines/breakbeat/BreakbeatEngine.cpp -o build/breakbeat_test
//   ./build/breakbeat_test
#include "breakbeat/BreakbeatEngine.h"
#include <cmath>
#include <cstdio>

using namespace bbeat;

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

// a 2-bar loop whose level encodes the slice (so we can tell which slice plays)
static std::unique_ptr<SampleData> makeLoop(int frames) {
	auto d = std::make_unique<SampleData>();
	d->totalFrames = frames;
	d->numChannels = 1;
	d->l.resize((size_t) frames);
	for (int i = 0; i < frames; i++)
		d->l[(size_t) i] = 0.05f + 0.05f * (float) (i * 16 / frames);   // slice k of 16 -> 0.05 (k+1)
	return d;
}

struct Run { bool finite = true; float peak = 0.f; double energy = 0.0; int hits = 0, rolls = 0; };

static Run play(BreakbeatEngine& e, const EngineParams& p, double sr, double bpm, double& ppq, int blocks, std::vector<int>* slices = nullptr) {
	Run r;
	constexpr int B = 32;
	float L[B], R[B];
	for (int b = 0; b < blocks; b++) {
		TransportInfo t;
		t.hostPlaying = t.hasPpq = true;
		t.ppq = ppq;
		t.bpm = bpm;
		e.process(L, R, B, p, t, nullptr, 0);
		ppq += B * bpm / 60.0 / sr;
		for (int i = 0; i < B; i++) {
			r.finite = r.finite && std::isfinite(L[i]) && std::isfinite(R[i]);
			r.peak = std::max(r.peak, std::abs(L[i]));
			r.energy += (double) L[i] * L[i];
		}
		for (int h = 0; h < e.numHits(); h++) {
			r.hits++;
			r.rolls += e.hit(h).vel != 100;
			if (slices) slices->push_back(e.hit(h).slice);
		}
	}
	return r;
}

static float rnd(void* ctx) {
	uint32_t& s = *static_cast<uint32_t*>(ctx);
	s ^= s << 13; s ^= s >> 17; s ^= s << 5;
	return (float) (s & 0xffffff) / 16777216.f;
}

int main() {
	const double SR = 48000.0, BPM = 170.0;

	// slice_select_n with 8 slices over 1 bar makes the original's decisions
	{
		int same = 0;
		for (int k = 0; k < 2000; k++) {
			uint32_t a = 1234 + k, b = 1234 + k;
			slice_inputs_t o{};
			o.current_slice = k % 8; o.beat_position = (k * 3) % 8; o.complexity = 0.6f; o.anchor = 0.4f; o.roll = 0.3f;
			slice_n_inputs_t n{};
			n.num_slices = 8; n.current_slice = o.current_slice; n.beat_position = o.beat_position; n.loop_bars = 1.f;
			n.complexity = o.complexity; n.anchor = o.anchor; n.roll = o.roll;
			same += slice_select_next(&o, rnd, &a) == slice_n_next(&n, rnd, &b);
		}
		CHECK(same == 2000, "slice_n_next differs from upstream %d / 2000", same);
	}

	// BBCut procedures: blocks fill their phrases exactly, cuts are positive
	for (int k = 0; k < bbcut::NumKinds; k++) {
		bbcut::Proc pr;
		pr.reset(k, 77);
		bbcut::Params p;
		p.repeatsChance = 0.6f;
		p.straightChance = 0.3f;
		p.activity = 0.7f;
		p.rollChance = 0.5f;
		bbcut::Block b;
		double sumPhrase = 0.0, lastLen = 0.0;
		int phrases = 0, rolls = 0;
		bool ok = true;
		for (int i = 0; i < 400; i++) {
			pr.chooseblock(p, b);
			if (b.phrasePos < 1e-9 && i > 0) {
				ok = ok && std::abs(sumPhrase - lastLen) < 1e-3;
				phrases++;
				sumPhrase = 0.0;
			}
			lastLen = b.phraseLength;
			double s = 0.0;
			for (int c = 0; c < b.n; c++) {
				ok = ok && b.cut[c] > 0.f;
				s += b.cut[c];
			}
			ok = ok && std::abs(s - b.length) < 1e-3;
			sumPhrase += b.length;
			rolls += b.roll;
		}
		CHECK(ok && phrases > 5, "%s: blocks do not tile phrases (%d phrases)", bbcut::kindName(k), phrases);
		std::printf("%-10s phrases %3d  roll blocks %3d\n", bbcut::kindName(k), phrases, rolls);
	}

	// the engine in every cut mode
	const int frames = (int) (SR * 60.0 / BPM * 8.0);   // 2 bars at 170
	for (int m = 0; m < NumCutModes; m++) {
		BreakbeatEngine e;
		e.prepare(SR, 32);
		e.setFixedSeed(99);
		e.submitSample(0, makeLoop(frames), false);
		e.setSlotGrid(0, 2.f, 16);
		EngineParams p;
		p.cutMode = m;
		p.complexity = 0.6f;
		p.roll = 0.3f;
		p.retrig = {0.3f, 0.f, 0.3f, 0.f};
		p.seqSteps = 4;
		Step fill;
		fill.fill = 80;
		e.setStep(3, fill);
		double ppq = 0.0;
		const int blocks = (int) (SR * 60.0 / BPM * 16.0 / 32.0);   // 4 bars
		Run r = play(e, p, SR, BPM, ppq, blocks);
		CHECK(r.finite, "%s non-finite", cutModeName(m));
		CHECK(r.peak > 0.04f && r.peak < 1.f, "%s peak %f", cutModeName(m), r.peak);
		CHECK(r.hits >= 8, "%s hits %d", cutModeName(m), r.hits);
		CHECK(e.cutWrite >= 8, "%s cut log %d", cutModeName(m), e.cutWrite);
		std::printf("%-10s hits %3d  rolls %3d  cuts %3d  peak %.3f\n", cutModeName(m), r.hits, r.rolls, e.cutWrite, r.peak);
		e.collectGarbage();
	}

	// MOVE with complexity 0 plays the slices in order
	{
		BreakbeatEngine e;
		e.prepare(SR, 32);
		e.submitSample(0, makeLoop(frames), false);
		e.setSlotGrid(0, 2.f, 16);
		EngineParams p;
		p.complexity = 0.f;
		p.seqSteps = 1;
		double ppq = 0.0;
		std::vector<int> sl;
		play(e, p, SR, BPM, ppq, (int) (SR * 60.0 / BPM * 8.0 / 32.0), &sl);
		bool inOrder = sl.size() >= 15;
		for (size_t i = 0; i < sl.size(); i++)
			inOrder = inOrder && sl[i] == (int) (i % 16);
		CHECK(inOrder, "complexity 0 not in order (%zu hits)", sl.size());
	}

	// switching modes while running keeps it sounding
	{
		BreakbeatEngine e;
		e.prepare(SR, 32);
		e.submitSample(0, makeLoop(frames), false);
		e.setSlotGrid(0, 2.f, 16);
		EngineParams p;
		double ppq = 0.0;
		bool ok = true;
		for (int m = 0; m < 2 * NumCutModes; m++) {
			p.cutMode = m % NumCutModes;
			Run r = play(e, p, SR, BPM, ppq, 300);
			ok = ok && r.finite && r.energy > 0.1;
		}
		CHECK(ok, "mode switching went silent / non-finite");
	}

	std::printf("%d / %d checks passed\n", checks - fails, checks);
	return fails ? 1 : 0;
}
