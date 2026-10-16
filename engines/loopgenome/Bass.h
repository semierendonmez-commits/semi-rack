// semi-rack: vendored; namespaces wrapped in dnafam:: (see sources.mk for every change).
// LOOPGENOME: the bass line of the loop, on the same 16ths as the break's genome.
//
// Four places it can come from:
//   STYLE      a line from NEUROSTACK's library (its rhythms and the ones taken from the
//              119 analysed bass loops), cell for cell.
//   FROM KICKS a line written on the break's own kicks with what the analysed loops do:
//              a note on (almost) every kick, long, held until the next kick or the snare,
//              no note on the snare, a pickup in the last beat now and then, the root most
//              of the time, then the octave, the flat-7, the flat-3.
//   AUDIO      a recorded bass loop: YIN pitch every 5 ms, re-attacks from the level, one
//              cell per 16th (rest / note / held), semitones from the loop's root.
//   MIDI       the notes of a MIDI file (the lowest line), one cell per 16th.
//
// Plain C++20, no JUCE.
#pragma once

#include "../genotype/Learn.h"
#include <array>
#include <string>
#include <vector>

namespace dnafam::lg
{
using walk::kMaxSteps;

enum BassCellType { BRest = 0, BHit, BTie };

struct BassCell
{
    int8_t type = BRest;
    int8_t semis = 0;       // from the line's root
    int8_t slide = 0;
    int8_t bend = 0;        // semitones the note starts away
    uint8_t vel = 100;      // 1..127
};

struct BassLine
{
    std::string name;
    int steps = 32;                         // as written (scaled onto the break's steps by fit())
    std::array<BassCell, kMaxSteps> cells {};
    int root = 36;                          // MIDI note of semitone 0 (AUDIO / MIDI: as found)
    bool fromAudio = false;

    int notes() const { int c = 0; for (int i = 0; i < steps; ++i) c += cells[(size_t) i].type == BHit; return c; }
};

enum BassSource { SrcStyle = 0, SrcKicks, SrcAudio, SrcMidiFile, NumBassSources };

/** The built-in styles (NEUROSTACK's library). */
int numStyles();
const char* styleName (int i);
BassLine style (int i);

/** A line on the break's kicks (L: the break's steps, learned). density 0..1: how many of
    the kicks get a note, pickup 0..1: how often the last beat of a bar has one. */
BassLine fromKicks (const geno::Learned& L, float density, float pickup, uint32_t seed);

/** A recorded bass loop: mono samples, its rate, how many 16ths it lasts (bars x 16). */
BassLine fromAudio (const std::vector<float>& mono, double rate, int steps16, const std::string& name);

struct MidiNoteIn { double start16 = 0.0, len16 = 1.0; int note = 36, vel = 100; };
/** Notes (in 16ths from the start), the loop's length in 16ths. The lowest line is kept. */
BassLine fromNotes (std::vector<MidiNoteIn> notes, int steps16, const std::string& name);

/** The line on n steps: repeated or cut, a held cell after a cut becomes a note. */
BassLine fit (const BassLine& b, int n);

/** YIN on a frame: Hz, 0 = unvoiced. Exposed for the tests. */
double yin (const float* x, int n, double rate, double fmin, double fmax, float threshold, float* clarity = nullptr);

} // namespace dnafam::lg
