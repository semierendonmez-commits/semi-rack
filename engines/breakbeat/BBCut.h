// BBCut cut procedures for BREAKBEAT (SEMI Family port).
//
// C++ port of four cut procedures of Nick Collins' BBCut2 (SuperCollider) as ported to TypeScript
// by bencodec/BBenCut (src/bbcut/BBCutProc.ts, BBCutProc11.ts, WarpCutProc1.ts, SQPusher1.ts,
// ChooseCutProc.ts). Same block / cut logic and the same defaults; changes for the audio thread:
// no allocation (fixed arrays), a seeded xorshift instead of Math.random, parameters read from a
// Params struct at every block instead of resolvable closures, and the listener replaced by the
// Block result (offset = phrase position, as BBenCut's chooseoffset).
//
// BBCut2 is GPL (Nick Collins); BBenCut carries no licence file. SEMI Family is GPL-3.0-or-later.
#pragma once

#include <cstdint>

namespace bbeat {
namespace bbcut {

constexpr int kMaxCuts = 128;

struct Rng {
	uint32_t s = 0x9e3779b9u;
	uint32_t next() noexcept {
		uint32_t x = s ? s : 0x9e3779b9u;
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		return s = x;
	}
	float u01() noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
	int rand(int n) noexcept { return n > 0 ? (int) (u01() * (float) n) % n : 0; }      // [0, n)
	int rrand(int lo, int hi) noexcept { return lo + rand(hi - lo + 1); }              // [lo, hi]
	bool coin(float p) noexcept { return u01() < p; }
};

// Everything the procedures read, set per block by the engine.
struct Params {
	float phraseBeats = 12.f;     // CutProc11: bars x 4; WarpCut / ChooseCut phrase length
	// BBCutProc11
	int sdiv = 8;                 // subdivisions per bar
	float repeatsChance = 0.2f;
	int repeatsSpeed = 2;
	float repeatsArea = 0.5f;
	// WarpCutProc1
	float straightChance = 0.5f, arithChance = 0.7f, accelChance = 0.6f, accel = 0.9f;
	// SQPusher1
	float activity = 0.1f;
	int fillFreq = 4;
	float fillScramble = 0.f;
	// ChooseCutProc
	float rollChance = 0.1f, rollAllowed = 2.f;
};

// One block: its cuts (beats, inter-onset), its length, the phrase position it starts at.
struct Block {
	float cut[kMaxCuts];
	int n = 0;
	double length = 0.0;
	double phrasePos = 0.0;      // offset into the phrase (BBenCut's chooseoffset)
	double phraseLength = 0.0;
	bool roll = false;
	bool fill = false;           // SQPusher1 fill phrase
};

enum Kind : int { CutProc11 = 0, WarpCut, SQPusher, ChooseCut, NumKinds };
const char* kindName(int k);     // "CUTPROC11" "WARPCUT" "SQPUSHER1" "CHOOSECUT"

// All four procedures in one object (no virtual dispatch, no allocation).
class Proc {
public:
	void reset(int kind, uint32_t seed) noexcept;
	int kind() const noexcept { return k; }
	// Produces the next block. p is read now.
	void chooseblock(const Params& p, Block& out) noexcept;
	Rng rng;

private:
	void newPhrase(double cpl) noexcept;
	void finish(Block& out, bool roll) noexcept;
	void cp11(const Params& p, Block& out) noexcept;
	void warp(const Params& p, Block& out) noexcept;
	void sqp(const Params& p, Block& out) noexcept;
	void choose(const Params& p, Block& out) noexcept;

	int k = CutProc11;
	int phrase = 0, block = 0;
	double phrasepos = 0.0, currphraselength = 0.0, beatspersubdiv = 0.5, blocklength = 0.0;
	// CutProc11
	int subdiv = 0;
	double unitsdone = 0.0, totalunits = 0.0;
	// SQPusher1: the current phrase's block sequence
	float seqCut[32][8];
	int seqLen[32];
	int seqN = 0;
	bool seqFill = false;
	float cuts[kMaxCuts];
	int ncuts = 0;
};

} // namespace bbcut
} // namespace bbeat
