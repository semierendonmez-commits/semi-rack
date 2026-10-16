// Offline check of the vendored GENOTYPE engine as the Rack module drives it: 32-sample blocks,
// ppq from the clock (one block ahead), the worker's mix / derive, MUTATION 0 and 100, halt and
// reset requests. Not part of the plugin build.
//   g++ -std=c++17 -O2 -Iengines -Itests tests/genotype_engine.cpp engines/breakdna/*.cpp engines/genotype/*.cpp -o build/genotype_test && ./build/genotype_test
#include "dnafam_synth.hpp"
#include "genotype/Genotype.h"

using namespace dnafam;
using namespace synth;

namespace {
constexpr int B = 32;
constexpr double SR = 48000.0, BPM = 170.0;

struct Res {
	Stats st;
	int ons = 0, offs = 0, jumps = 0, moves = 0;
	std::vector<int> steps;
};

void deriveInto(geno::Brain& br, const geno::Bundle& b, float mutation, float blend = 0.f) {
	static walk::Plain g, c, r, f;
	geno::mix(b.learned.fromA, b.learned.fromB, blend, b.learned.steps, g);
	geno::DeriveParams d;
	d.mutation = mutation;
	geno::derive(g, b.learned, d, c, r, f);
	br.genome.copyFrom(g);
	br.call.copyFrom(c);
	br.resp.copyFrom(r);
	br.fill.copyFrom(f);
}

Res run(const geno::Bundle& b, geno::Brain& br, geno::Params p, double seconds, bool clocked, int haltAtBlock = -1) {
	geno::Engine e;
	e.prepare(SR, B);
	Res res;
	float L[B], R[B];
	geno::MidiEvent mo[geno::Engine::kMaxOut];
	const long blocks = (long) (seconds * SR / B);
	for (long k = 0; k < blocks; k++) {
		if (k == haltAtBlock) e.haltRequest.store(1);
		if (clocked) {
			p.playing = true;
			p.run = geno::RunHost;
			p.bpm = BPM;
			p.ppq = k == 0 ? 0.0 : ((k + 1) * B / SR) * BPM / 60.0;   // the module starts the clock at 0
		}
		int no = 0;
		e.process(&b, br, p, nullptr, 0, L, R, B, mo, no);
		res.st.add(L, B);
		res.st.add(R, B);
		for (int i = 0; i < no; i++) (mo[i].vel ? res.ons : res.offs)++;
		geno::MoveEvent mv;
		while (e.popMove(mv))
			if (mv.to >= 0) {
				res.moves++;
				if (!res.steps.empty() && mv.to != (res.steps.back() + 1) % b.learned.steps) res.jumps++;
				res.steps.push_back(mv.to);
			}
	}
	return res;
}
} // namespace

int main() {
	auto a = makeBreak(amenish(), BPM, 44100.0, 4.0, 11);
	auto bb = makeBreak(straight(), 140.0, 44100.0, 2.0, 77, 1.7f);
	dna::KitParams kp;
	kp.barsA = kp.barsB = 2;
	auto bundle = std::make_shared<geno::Bundle>();
	bundle->kit = std::shared_ptr<const dna::Kit>(dna::buildKit(a, bb, kp).release());
	bundle->learned = geno::learn(*bundle->kit);
	const auto& Ln = bundle->learned;
	CHECK(Ln.steps == 32, "32 steps learned (%d)", Ln.steps);
	int rows = 0;
	for (int x = 1; x <= Ln.steps; x++) {
		float s = 0;
		for (int y = 0; y <= Ln.steps; y++) s += Ln.fromA[x][y];
		rows += s > 0.f;
	}
	CHECK(rows == Ln.steps, "every row of the learned walk can move (%d / %d)", rows, Ln.steps);

	geno::Brain br;
	geno::Params p;
	p.form = geno::FormCall;
	p.ending = geno::EndOff;

	// MUTATION 0: the break as it is (a plain run, every hit of A per loop)
	deriveInto(br, *bundle, 0.f);
	Res r0 = run(*bundle, br, p, 6.0, true);
	CHECK(r0.st.finite && r0.st.peak < 2.0, "finite, bounded (peak %.2f)", r0.st.peak);
	CHECK(r0.st.rms() > 0.01, "sounds (rms %.4f)", r0.st.rms());
	for (size_t i = 0; i < r0.steps.size() && r0.jumps; i++) std::printf("%d ", r0.steps[i]); if (r0.jumps) std::printf("\n");
	CHECK(r0.jumps == 0, "MUTATION 0 walks 1, 2, 3 ... (%d jumps in %d moves)", r0.jumps, r0.moves);
	const double loops = 6.0 * BPM / 60.0 / 8.0;
	CHECK(r0.ons >= (int) (Ln.hitLane.size() * (loops - 1.0)), "every hit plays (%d notes, %zu hits x %.1f loops)", r0.ons,
	      Ln.hitLane.size(), loops);
	CHECK(r0.ons == r0.offs || r0.ons - r0.offs <= 2, "notes end (%d on / %d off)", r0.ons, r0.offs);

	// MUTATION 100: a new walk, still sounding, still on the beat grid (metric places kept)
	deriveInto(br, *bundle, 1.f);
	Res r1 = run(*bundle, br, p, 12.0, true);
	CHECK(r1.st.rms() > 0.01, "MUTATION 100 sounds (rms %.4f)", r1.st.rms());
	CHECK(r1.jumps > r1.moves / 5, "MUTATION 100 jumps (%d of %d moves)", r1.jumps, r1.moves);
	int offGrid = 0;
	for (size_t i = 1; i < r1.steps.size(); i++)
		if (r1.steps[i] % 4 != (r1.steps[i - 1] + 1) % 4) offGrid++;
	CHECK(offGrid < (int) r1.steps.size() / 10, "the walk keeps the place in the beat (%d of %zu moves off)", offGrid, r1.steps.size());

	// BLEND 100: learned from B's rhythm, still a walk
	deriveInto(br, *bundle, 1.f, 1.f);
	Res rb = run(*bundle, br, p, 4.0, true);
	CHECK(rb.st.rms() > 0.005 && rb.moves > 0, "BLEND 100 walks (rms %.4f, %d moves)", rb.st.rms(), rb.moves);

	// free run (no CLK) and HALT
	deriveInto(br, *bundle, 0.4f);
	geno::Params pf = p;
	pf.run = geno::RunAlways;
	pf.bpm = bundle->kit->g.bpm;
	Res rf = run(*bundle, br, pf, 3.0, false);
	CHECK(rf.st.rms() > 0.01, "free run sounds (rms %.4f)", rf.st.rms());
	Res rh = run(*bundle, br, p, 4.0, true, (int) (1.0 * SR / B));
	CHECK(rh.moves < r0.moves * 0.4, "HALT stops the ball (%d moves, %d unhalted over 6 s)", rh.moves, r0.moves);

	// the call & response form switches matrices
	geno::Params pc = p;
	pc.form = geno::Form11;
	pc.ending = geno::EndBeat;
	Res rc = run(*bundle, br, pc, 6.0, true);
	CHECK(rc.st.rms() > 0.01, "1+1 form sounds (rms %.4f)", rc.st.rms());

	std::printf("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
