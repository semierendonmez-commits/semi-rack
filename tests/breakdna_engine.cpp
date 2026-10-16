// Offline check of the vendored BREAKDNA engine as the Rack module drives it (32-sample blocks,
// ppq from the 1/16 clock, ahead by one block + the engine's lookahead in BREAK A mode).
// Not part of the plugin build.
//   g++ -std=c++17 -O2 -Iengines -Itests tests/breakdna_engine.cpp engines/breakdna/*.cpp -o build/breakdna_test && ./build/breakdna_test
#include "dnafam_synth.hpp"
#include "breakdna/Engine.h"
#include <array>
#include <atomic>

using namespace dnafam;
using namespace synth;

namespace {
constexpr int B = 32;
constexpr double SR = 48000.0, BPM = 170.0;

struct Run {
	Stats st;
	std::vector<std::pair<long, dna::MidiEvent>> notes;   // absolute sample, event
	std::vector<float> out;
};

Run render(const dna::Kit* kit, dna::Params p, double seconds, bool clocked, const std::vector<std::pair<long, dna::MidiEvent>>& in = {}) {
	dna::Engine e;
	e.prepare(SR, B);
	std::array<std::atomic<float>, dna::kMaxSteps> lane{};
	for (auto& l : lane) l.store(0.f);
	Run r;
	float L[B], R[B];
	dna::MidiEvent mo[dna::Engine::kMaxOut];
	const long blocks = (long) (seconds * SR / B);
	size_t ii = 0;
	for (long b = 0; b < blocks; b++) {
		const long t0 = b * B;
		dna::MidiEvent mi[64];
		int ni = 0;
		while (ii < in.size() && in[ii].first < t0 + B) {
			mi[ni] = in[ii].second;
			mi[ni].offset = (int) (in[ii].first - t0);
			ni++;
			ii++;
		}
		if (clocked) {
			p.playing = true;
			p.run = dna::RunHost;
			p.bpm = BPM;
			double ahead = B / SR + (p.source == dna::SourceBreak ? e.latency() / SR : 0.0);
			p.ppq = (b == 0 ? (ahead - B / SR) : (t0 / SR + ahead)) * BPM / 60.0;   // as the module: the first block starts the loop
		}
		int no = 0;
		e.process(kit, p, lane, mi, ni, L, R, B, mo, no);
		r.st.add(L, B);
		r.st.add(R, B);
		for (int i = 0; i < B; i++) r.out.push_back(L[i]);
		for (int i = 0; i < no; i++) r.notes.push_back({t0 + mo[i].offset + B, mo[i]});   // +B: the module plays a block later
	}
	return r;
}

double corr(const std::vector<float>& a, const std::vector<float>& b) {
	double ab = 0, aa = 0, bb = 0;
	for (size_t i = 0; i < std::min(a.size(), b.size()); i++) { ab += a[i] * b[i]; aa += a[i] * a[i]; bb += b[i] * b[i]; }
	return ab / std::sqrt(aa * bb + 1e-12);
}
} // namespace

int main() {
	auto a = makeBreak(amenish(), BPM, 44100.0, 4.0, 11);
	auto b = makeBreak(straight(), 140.0, 44100.0, 2.0, 77, 1.7f);
	dna::KitParams kp;
	kp.barsA = 2;
	kp.barsB = 2;
	auto kit = dna::buildKit(a, b, kp);
	CHECK(kit->g.steps == 32, "A's genome has 32 steps (%d)", kit->g.steps);
	CHECK(kit->numSlices() >= 12, "A sliced (%d slices)", kit->numSlices());
	CHECK(std::fabs(kit->g.bpm - BPM) < 3.0, "A's tempo found (%.1f)", kit->g.bpm);
	int kicks = 0;
	for (const auto& h : kit->g.hits) kicks += h.cls == gtbs::Kick;
	CHECK(kicks >= 5, "kicks classified (%d)", kicks);

	// BREAK A on the clock: sound, bounded, the loop's notes on time
	dna::Params p;
	p.morph = 0.f;
	Run r = render(kit.get(), p, 6.0, true);
	CHECK(r.st.finite, "finite");
	CHECK(r.st.peak < 2.0, "bounded (peak %.2f)", r.st.peak);
	CHECK(r.st.rms() > 0.01, "not silent (rms %.4f)", r.st.rms());
	const double loop = 8.0 * 60.0 / BPM * SR;           // 2 bars in samples
	int ons = 0, kOnLoop = 0;
	for (auto& n : r.notes) {
		if (n.second.vel == 0) continue;
		ons++;
		// the second loop's first kick: A plays it on the beat (its lag is taken out)
		if (n.second.note == 36 && std::fabs(n.first - loop) < 0.004 * SR) kOnLoop++;
	}
	const double loops = 6.0 * SR / loop;
	CHECK(ons > (int) (kit->g.hits.size() * (loops - 1)), "every hit of A sends a note (%d notes, %zu hits x %.1f loops)", ons,
	      kit->g.hits.size(), loops);
	CHECK(kOnLoop == 1, "the kick on the loop's downbeat lands within 4 ms of the clock (%d)", kOnLoop);

	// FEEL 0 vs 100: the late snare (step 4, +6 ms) moves
	auto snareAt = [&](const Run& rr) {
		for (auto& n : rr.notes)
			if (n.second.vel > 0 && n.second.note == 38 && n.first > loop + 3.5 * loop / 32 && n.first < loop + 4.5 * loop / 32)
				return (double) n.first - (loop + 4 * loop / 32);
		return 1e9;
	};
	dna::Params p0 = p;
	p0.feel = 0.f;
	const double late1 = snareAt(r), late0 = snareAt(render(kit.get(), p0, 4.0, true));
	CHECK(late1 < 1e8 && late0 < 1e8, "snare found (%.0f / %.0f)", late1, late0);
	CHECK(late1 - late0 > 0.002 * SR, "FEEL 100 plays A's late snare later than FEEL 0 (%.2f ms vs %.2f ms)", late1 / SR * 1000,
	      late0 / SR * 1000);

	// MORPH: B's skin changes the sound, same timing
	dna::Params pm = p;
	pm.morph = 1.f;
	Run rm = render(kit.get(), pm, 6.0, true);
	const double c = corr(r.out, rm.out);
	CHECK(rm.st.rms() > 0.01 && c < 0.9, "MORPH 100 sounds different (corr %.2f, rms %.4f)", c, rm.st.rms());
	CHECK(rm.notes.size() == r.notes.size(), "MORPH keeps the timing (%zu / %zu notes)", rm.notes.size(), r.notes.size());

	// free run (no CLK): RUN ALWAYS at the break's tempo
	dna::Params pf = p;
	pf.run = dna::RunAlways;
	pf.bpm = kit->g.bpm;
	Run rf = render(kit.get(), pf, 3.0, false);
	CHECK(rf.st.rms() > 0.01, "free run sounds (rms %.4f)", rf.st.rms());

	// stopped clock: silence
	dna::Params ps = p;
	ps.run = dna::RunHost;
	ps.playing = false;
	Run rs = render(kit.get(), ps, 1.0, false);
	CHECK(rs.st.peak < 1e-6, "stopped: silent (%.6f)", rs.st.peak);

	// GATES: your kicks / snares get A's feel, A's ghosts fill the empty steps
	dna::Params pg = p;
	pg.source = dna::SourceMidi;
	std::vector<std::pair<long, dna::MidiEvent>> in;
	const double sps = 60.0 / BPM * SR / 4.0;
	for (int bar = 0; bar < 4; bar++)
		for (int s : {0, 4, 10, 12}) {
			const long t = (long) ((bar * 16 + s) * sps);
			const uint8_t note = (s == 4 || s == 12) ? 38 : 36;
			in.push_back({t, {0, note, 110}});
			in.push_back({t + 2000, {0, note, 0}});
		}
	std::sort(in.begin(), in.end(), [](auto& x, auto& y) { return x.first < y.first; });
	Run rg = render(kit.get(), pg, 5.0, true, in);
	int gOn = 0, gOff = 0;
	for (auto& n : rg.notes) (n.second.vel ? gOn : gOff)++;
	CHECK(rg.st.rms() > 0.005, "GATES: your notes play the kit (rms %.4f)", rg.st.rms());
	CHECK(gOn >= 16, "GATES: your notes (and A's ghosts) come out (%d on)", gOn);
	CHECK(gOn == gOff, "GATES: every note on has its off (%d / %d)", gOn, gOff);

	std::printf("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
