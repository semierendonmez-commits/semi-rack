// Offline check of the vendored LOOPGENOME engine as the Rack module drives it: 32-sample blocks,
// ppq from the clock (the first block at 0, then one block ahead), the worker's bass line /
// joint walk / derive / articulation map, MUTATION 0 and 100, the skeleton, the bass notes that
// become V/OCT + GATE. Not part of the plugin build.
//   g++ -std=c++17 -O2 -Iengines -Itests tests/loopgenome_engine.cpp engines/breakdna/*.cpp engines/genotype/*.cpp
//       engines/loopgenome/*.cpp -o build/loopgenome_test && ./build/loopgenome_test
#include "dnafam_synth.hpp"
#include "loopgenome/Loop.h"

using namespace dnafam;
using namespace synth;

namespace {
constexpr int B = 32;
constexpr double SR = 48000.0, BPM = 174.0;

struct Res {
	Stats st;
	int drumOn = 0, bassOn = 0, bassOff = 0, moves = 0, jumps = 0, brokenSkel = 0;
	std::vector<int> steps;
};

std::shared_ptr<lg::Bundle> makeBundle(const lg::BassLine* line, int skeleton, float couple) {
	auto a = makeBreak(amenish(), BPM, 44100.0, 4.0, 11);
	dna::KitParams kp;
	kp.barsA = kp.barsB = 2;
	auto b = std::make_shared<lg::Bundle>();
	b->kit = std::shared_ptr<const dna::Kit>(dna::buildKit(a, a, kp).release());
	b->learned = geno::learn(*b->kit);
	const auto bl = line ? *line : lg::fromKicks(b->learned, 0.8f, 0.3f, 1);
	b->joint = lg::learnJoint(b->learned, bl, skeleton, couple);
	return b;
}

void deriveInto(lg::Brain& br, const lg::Bundle& b, float mutation, int skeleton) {
	static walk::Plain c, r, f;
	geno::DeriveParams d;
	d.mutation = mutation;
	lg::deriveJoint(b.joint.fromJoint, b.learned, b.joint, d, skeleton, c, r, f);
	br.genome.copyFrom(b.joint.fromJoint);
	br.call.copyFrom(c);
	br.resp.copyFrom(r);
	br.fill.copyFrom(f);
	const auto am = lg::artMap(b.joint, lg::MapHalves);
	for (int s = 0; s < walk::kMaxSteps; s++) br.bass[s].art.store(am[(size_t) s]);
}

Res run(const lg::Bundle& b, lg::Brain& br, lg::Params p, double seconds) {
	static lg::Engine e;   // big: keep it off the stack
	e.prepare(SR, B);
	Res res;
	float L[B], R[B];
	geno::MidiEvent mo[lg::Engine::kMaxOut];
	uint8_t ch[lg::Engine::kMaxOut];
	const long blocks = (long) (seconds * SR / B);
	for (long k = 0; k < blocks; k++) {
		p.playing = true;
		p.run = geno::RunHost;
		p.bpm = BPM;
		p.ppq = k == 0 ? 0.0 : ((k + 1) * B / SR) * BPM / 60.0;
		int no = 0;
		e.process(&b, br, p, nullptr, 0, L, R, B, mo, no, ch);
		res.st.add(L, B);
		res.st.add(R, B);
		for (int i = 0; i < no; i++) {
			if (ch[i] == p.bassChannel) (mo[i].vel ? res.bassOn : res.bassOff)++;
			else if (mo[i].vel) res.drumOn++;
		}
		geno::MoveEvent mv;
		while (e.popMove(mv))
			if (mv.to >= 0) {
				const int n = b.learned.steps;
				if (!res.steps.empty()) {
					const int expect = (res.steps.back() + 1) % n;
					if (mv.to != expect) res.jumps++;
					// (a reset, from < 0, and the fill are free)
					if (mv.from >= 0 && mv.role != geno::RoleFill && b.joint.skel[(size_t) mv.to] != b.joint.skel[(size_t) expect]) res.brokenSkel++;
				}
				res.moves++;
				res.steps.push_back(mv.to);
			}
	}
	return res;
}
} // namespace

int main() {
	auto bundle = makeBundle(nullptr, lg::SkelKickSnare, 0.7f);
	const auto& J = bundle->joint;
	CHECK(J.steps == 32, "joint over 32 steps (%d)", J.steps);
	CHECK(J.bass.notes() > 2, "FROM KICKS writes notes (%d)", J.bass.notes());
	CHECK(J.kicksWithOnset > 0.3f, "notes sit on the kicks (%.2f)", J.kicksWithOnset);

	lg::Brain br;
	lg::Params p;
	p.form = geno::FormCall;
	p.ending = geno::EndOff;

	deriveInto(br, *bundle, 0.f, lg::SkelKickSnare);
	Res r0 = run(*bundle, br, p, 6.0);
	CHECK(r0.st.finite && r0.st.peak <= 1.0, "finite, under 0 dBFS (peak %.3f)", r0.st.peak);
	CHECK(r0.st.rms() > 0.02, "drums + bass sound (rms %.4f)", r0.st.rms());
	CHECK(r0.jumps == 0, "MUTATION 0 plays the loop as written (%d jumps in %d moves)", r0.jumps, r0.moves);
	CHECK(r0.drumOn > 20, "drum notes (%d)", r0.drumOn);
	CHECK(r0.bassOn > 4, "bass notes on channel 2 (%d)", r0.bassOn);
	CHECK(std::abs(r0.bassOn - r0.bassOff) <= 1, "every bass note ends (%d on / %d off)", r0.bassOn, r0.bassOff);

	deriveInto(br, *bundle, 1.f, lg::SkelKickSnare);
	geno::Params gp;
	int broken = 0, jumps = 0, moves = 0;
	for (uint32_t seed = 1; seed <= 4; seed++) {
		lg::Params ps = p;
		ps.seed = seed;
		Res r1 = run(*bundle, br, ps, 8.0);
		broken += r1.brokenSkel;
		jumps += r1.jumps;
		moves += r1.moves;
		CHECK(r1.st.rms() > 0.02, "MUTATION 100 sounds (seed %u, rms %.4f)", seed, r1.st.rms());
	}
	CHECK(jumps > moves / 5, "MUTATION 100 jumps (%d of %d moves)", jumps, moves);
	CHECK(broken == 0, "the skeleton holds: kicks and snares where the loop has them (%d broken)", broken);

	// a STYLE line and COUPLE 0 still learn a walk
	auto styled = lg::style(2);
	auto b2 = makeBundle(&styled, lg::SkelKickSnare, 0.f);
	deriveInto(br, *b2, 0.6f, lg::SkelKickSnare);
	Res r2 = run(*b2, br, p, 4.0);
	CHECK(r2.st.rms() > 0.02 && r2.bassOn > 0, "STYLE line plays (rms %.4f, %d bass notes)", r2.st.rms(), r2.bassOn);

	// an audio bass loop (saws on the cells) becomes a line
	{
		const double rate = 44100.0, sps = 60.0 / BPM * rate / 4.0;
		std::vector<float> x((size_t) (32 * sps), 0.f);
		double ph = 0;
		for (size_t i = 0; i < x.size(); i++) {
			const int c = (int) (i / sps);
			const bool on = (c % 8) < 6;
			const double hz = 55.0 * ((c / 8) % 2 ? 1.5 : 1.0);
			ph += hz / rate;
			ph -= std::floor(ph);
			const double into = i - c * sps;
			x[i] = on && into > rate * 0.004 ? 0.5f * (float) (2 * ph - 1) : 0.f;
		}
		auto line = lg::fromAudio(x, rate, 32, "saws");
		CHECK(line.notes() >= 3, "the audio loop's notes are found (%d)", line.notes());
	}

	std::printf("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
