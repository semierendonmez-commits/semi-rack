#include "BBCut.h"

#include <algorithm>
#include <cmath>

namespace bbeat {
namespace bbcut {

const char* kindName(int k) {
	static const char* n[] = {"CUTPROC11", "WARPCUT", "SQPUSHER1", "CHOOSECUT"};
	return k >= 0 && k < NumKinds ? n[k] : "?";
}

void Proc::reset(int kind, uint32_t seed) noexcept {
	k = std::clamp(kind, 0, NumKinds - 1);
	rng.s = seed ? seed : 0x9e3779b9u;
	phrase = block = 0;
	phrasepos = currphraselength = blocklength = 0.0;
	beatspersubdiv = 0.5;
	subdiv = 0;
	unitsdone = totalunits = 0.0;
	seqN = 0;
	seqFill = false;
	ncuts = 0;
}

// BBCutProc.newPhraseAccounting
void Proc::newPhrase(double cpl) noexcept {
	currphraselength = cpl;
	phrasepos = 0.0;
	phrase += 1;
	block = 0;
}

// chooseoffset + updateblock + endBlockAccounting
void Proc::finish(Block& out, bool roll) noexcept {
	out.n = std::min(ncuts, kMaxCuts);
	for (int i = 0; i < out.n; i++)
		out.cut[i] = cuts[i];
	out.length = blocklength;
	out.phrasePos = phrasepos;
	out.phraseLength = currphraselength;
	out.roll = roll;
	out.fill = k == SQPusher && seqFill;
	phrasepos += blocklength;
	block += 1;
}

void Proc::chooseblock(const Params& p, Block& out) noexcept {
	switch (k) {
		case WarpCut: warp(p, out); break;
		case SQPusher: sqp(p, out); break;
		case ChooseCut: choose(p, out); break;
		default: cp11(p, out); break;
	}
	// guard: a block always moves time forward
	if (!(out.length > 1e-4)) {
		out.length = std::max(1e-4, currphraselength - out.phrasePos);
		out.n = 1;
		out.cut[0] = (float) out.length;
	}
}

// ---- BBCutProc11 (the classic BreakBeatx) ---------------------------------------------------------
void Proc::cp11(const Params& p, Block& out) noexcept {
	int unitblock;
	int repeats;
	double unit;
	bool repeatson = false;
	if (unitsdone >= totalunits - 0.000001) {
		subdiv = std::max(1, p.sdiv);
		const double sdivbeats = 4.0;                                   // barlength
		const int numbarsnow = std::max(1, (int) std::lround(p.phraseBeats / 4.0));
		totalunits = (double) numbarsnow * subdiv;
		newPhrase(sdivbeats * numbarsnow);
		beatspersubdiv = sdivbeats / subdiv;
		unitsdone = 0.0;
	}
	const double unitsleft = totalunits - unitsdone;
	if (rng.coin(p.repeatsChance) && unitsleft < p.repeatsArea * subdiv) {
		// repeats: rapid-fire repeats of a tiny slice filling the remainder
		const int stutsp = std::max(1, p.repeatsSpeed);
		repeats = (int) std::lround(unitsleft * stutsp);
		unit = 1.0 / stutsp;
		blocklength = currphraselength - phrasepos;
		unitsdone = totalunits;
		repeatson = true;
	}
	else {
		int oddmax = subdiv / 2;
		oddmax = oddmax % 2 == 0 ? (oddmax - 2) / 2 : (oddmax - 1) / 2;
		oddmax = std::max(0, oddmax);
		unitblock = 2 * rng.rrand(0, oddmax) + 1;
		while (unitblock > unitsleft && unitblock > 1)
			unitblock -= 2;
		repeats = rng.rand(2) + 1;   // numrepeats default: rand(2) + 1
		double unitproj = repeats * unitblock + unitsdone;
		while (unitproj > totalunits) {
			repeats -= 1;
			if (repeats <= 1) {
				repeats = 1;
				unitblock = (int) std::lround(unitsleft);
			}
			unitproj = repeats * unitblock + unitsdone;
		}
		blocklength = repeats * unitblock * beatspersubdiv;
		if (std::abs(unitproj - totalunits) < 1e-9)
			blocklength = currphraselength - phrasepos;
		unitsdone += repeats * unitblock;
		unit = unitblock;
	}
	const float cutLength = (float) (unit * beatspersubdiv);
	ncuts = std::clamp(repeats, 1, kMaxCuts);
	for (int i = 0; i < ncuts; i++)
		cuts[i] = cutLength;
	finish(out, repeatson);
}

// ---- WarpCutProc1 ---------------------------------------------------------------------------------
void Proc::warp(const Params& p, Block& out) noexcept {
	if (phrasepos >= currphraselength - 0.0001)
		newPhrase(std::max(0.25, (double) p.phraseBeats));
	const double beatsleft = currphraselength - phrasepos;
	// blocksizefunc default: wchoose([0.5, 1, 2], [0.5, 0.4, 0.1])
	const float r = rng.u01();
	blocklength = r < 0.5f ? 0.5 : r < 0.9f ? 1.0 : 2.0;
	if (blocklength > beatsleft)
		blocklength = beatsleft;
	bool rollon = false;
	if (rng.coin(p.straightChance)) {
		ncuts = 1;
		cuts[0] = (float) blocklength;
	}
	else {
		rollon = true;
		// rollfunc default: size < 1 -> choose([4, 8, 16]) else choose([8, 16, 32])
		static const int small[3] = {4, 8, 16}, large[3] = {8, 16, 32};
		const int repeats = blocklength < 1.0 ? small[rng.rand(3)] : large[rng.rand(3)];
		ncuts = std::min(repeats, kMaxCuts);
		if (rng.coin(p.arithChance)) {
			const float cs = (float) (blocklength / repeats);
			for (int i = 0; i < ncuts; i++)
				cuts[i] = cs;
		}
		else {
			const double a = std::clamp((double) p.accel, 0.05, 0.995);
			const double temp = blocklength * (1.0 - a) / (1.0 - std::pow(a, repeats));
			for (int i = 0; i < ncuts; i++)
				cuts[i] = (float) (temp * std::pow(a, i));
			if (rng.coin(p.accelChance))
				std::reverse(cuts, cuts + ncuts);
		}
	}
	finish(out, rollon);
}

// ---- SQPusher1 ------------------------------------------------------------------------------------
namespace {
// Pre-computed fill patterns (BBenCut's SQPusher1.FILLS, after BBCut2)
struct Fill {
	int groups;
	int len[6];
	float cut[6][6];
};
const Fill kFills[13] = {
	{2, {4, 1}, {{0.75f, 0.75f, 0.75f, 0.75f}, {1.0f}}},
	{3, {2, 1, 2}, {{0.5f, 1.0f}, {1.0f}, {1.0f, 0.5f}}},
	{3, {1, 3, 1}, {{0.5f}, {1.0f, 1.0f, 1.0f}, {0.5f}}},
	{5, {1, 2, 2, 1, 2}, {{0.571429f}, {0.571429f, 0.571429f}, {0.571429f, 0.571429f}, {0.571429f}, {0.285714f, 0.285716f}}},
	{3, {2, 2, 2}, {{1.0f, 0.5f}, {1.0f, 0.5f}, {0.5f, 0.5f}}},
	{3, {2, 3, 1}, {{0.5f, 0.5f}, {0.66f, 0.67f, 0.67f}, {1.01f}}},
	{4, {1, 2, 2, 1}, {{0.34f}, {0.33f, 0.33f}, {0.34f, 0.33f}, {2.33f}}},
	{4, {1, 2, 2, 1}, {{1.4f}, {0.4f, 0.4f}, {0.6f, 0.2f}, {1.0f}}},
	{3, {3, 3, 1}, {{0.167f, 0.167f, 0.166f}, {1.0f, 1.0f, 1.0f}, {0.5f}}},
	{2, {3, 4}, {{1.5f, 0.5f, 1.0f}, {0.25f, 0.25f, 0.25f, 0.25f}}},
	{4, {2, 2, 2, 1}, {{0.2f, 0.2f}, {0.4f, 0.4f}, {0.4f, 0.4f}, {2.0f}}},
	{2, {3, 6}, {{0.75f, 0.75f, 1.0f}, {0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f}}},
	{5, {2, 1, 4, 1, 3}, {{0.5f, 1.0f}, {0.5f}, {0.125f, 0.125f, 0.125f, 0.125f}, {1.0f}, {0.167f, 0.167f, 0.166f}}},
};
const float kSqWeights[8] = {0.0f, 0.3f, 0.0f, 0.5f, 0.7f, 0.8f, 0.9f, 0.6f};
} // namespace

void Proc::sqp(const Params& p, Block& out) noexcept {
	if (phrasepos >= currphraselength - 0.001) {
		newPhrase(4.0);   // always one bar
		seqN = 0;
		const int ff = std::max(1, p.fillFreq);
		if (phrase % ff == 0) {
			seqFill = true;
			const Fill& f = kFills[rng.rand(13)];
			int order[6] = {0, 1, 2, 3, 4, 5};
			if (rng.coin(p.fillScramble))
				for (int i = f.groups - 1; i > 0; i--)
					std::swap(order[i], order[rng.rand(i + 1)]);
			for (int g = 0; g < f.groups; g++) {
				const int src = order[g];
				seqLen[seqN] = f.len[src];
				for (int c = 0; c < f.len[src]; c++)
					seqCut[seqN][c] = f.cut[src][c];
				seqN++;
			}
		}
		else {
			seqFill = false;
			double done = 0.0;
			while (done < 4.0 - 1e-9 && seqN < 32) {
				const double beatpos = std::fmod(std::round(done / 0.25) * 0.25, 1.0);
				const int quaver = ((int) std::lround(done / 0.5) * 2) % 8;
				const float sqchance = kSqWeights[quaver] * p.activity;
				const int groupLen = std::abs(beatpos - 0.5) < 1e-9 ? 1 : rng.rand(2) + 1;
				const bool six = rng.coin(sqchance);
				const int n = six ? groupLen * 2 : groupLen;
				seqLen[seqN] = n;
				for (int c = 0; c < n; c++)
					seqCut[seqN][c] = six ? 0.25f : 0.5f;
				done += n * (six ? 0.25 : 0.5);
				seqN++;
			}
		}
	}
	const double beatsleft = currphraselength - phrasepos;
	if (block < seqN) {
		ncuts = seqLen[block];
		for (int c = 0; c < ncuts; c++)
			cuts[c] = seqCut[block][c];
	}
	else {
		ncuts = 1;
		cuts[0] = (float) beatsleft;
	}
	blocklength = 0.0;
	for (int c = 0; c < ncuts; c++)
		blocklength += cuts[c];
	if (blocklength > beatsleft) {
		// trim at the phrase end
		double acc = 0.0;
		int cutoff = 0;
		for (int i = 0; i < ncuts; i++) {
			acc += cuts[i];
			if (acc > beatsleft - 0.001) {
				cutoff = i;
				break;
			}
		}
		ncuts = cutoff + 1;
		cuts[cutoff] = (float) (cuts[cutoff] - (acc - beatsleft));
		if (ncuts < 1 || cuts[cutoff] <= 0.f) {
			ncuts = 1;
			cuts[0] = (float) beatsleft;
		}
		blocklength = beatsleft;
	}
	finish(out, false);
}

// ---- ChooseCutProc --------------------------------------------------------------------------------
void Proc::choose(const Params& p, Block& out) noexcept {
	if (phrasepos >= currphraselength - 1e-9)
		newPhrase(std::max(0.25, (double) p.phraseBeats));
	const double beatsleft = currphraselength - phrasepos;
	bool rollon = false;
	if (rng.coin(p.rollChance) && beatsleft < p.rollAllowed) {
		// rollfunc default: round(blocksize * 4) equal cuts
		rollon = true;
		blocklength = beatsleft;
		const int reps = std::clamp((int) std::lround(beatsleft * 4.0), 1, kMaxCuts);
		ncuts = reps;
		for (int i = 0; i < reps; i++)
			cuts[i] = (float) (beatsleft / reps);
	}
	else {
		double cutsize = rng.coin(0.5f) ? 1.0 : 1.5;   // cutsizefunc default: choose([1.0, 1.5])
		if (cutsize > beatsleft)
			cutsize = beatsleft;
		int repeats = rng.rand(2) + 1;                 // repeatfunc default: choose([1, 2])
		double proj = repeats * cutsize + phrasepos;
		while (proj > currphraselength + 1e-9) {
			repeats -= 1;
			if (repeats <= 1) {
				repeats = 1;
				cutsize = beatsleft;
			}
			proj = repeats * cutsize + phrasepos;
		}
		ncuts = repeats;
		for (int i = 0; i < repeats; i++)
			cuts[i] = (float) cutsize;
		blocklength = repeats * cutsize;
	}
	finish(out, rollon);
}

} // namespace bbcut
} // namespace bbeat
