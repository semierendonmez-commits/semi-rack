// semi-rack: vendored; namespaces wrapped in dnafam:: (see sources.mk for every change).
// BREAKDNA: what a break "is" apart from its sound.
//
//   Genome   : per 16th step of the loop, which hits land there, how early or late
//              (micro-timing, in fractions of a step), how loud, which are ghosts;
//              plus a feel profile per class and 16th position, used where a step
//              has no hit of its own (MIDI notes the break never played).
//   Kit      : break A (the DNA) and break B (the timbre), their slices, which B
//              slice answers which A slice, and A's timeline rendered in five
//              morph layers: A, three spectral blends, B.
//
// Plain C++20, no JUCE: builds and tests on its own.
#pragma once

#include "Analysis.h"
#include <array>
#include <cmath>
#include <memory>

namespace dnafam::dna
{

using gtbs::Sample;
using gtbs::SliceMap;
using gtbs::Kick;
using gtbs::Snare;
using gtbs::Ghost;
using gtbs::Hat;
using gtbs::NumCls;

constexpr int kLayers   = 5;        // morph 0, .25, .5, .75, 1
constexpr int kMaxSteps = 64;       // up to four bars of 16ths
constexpr int kSpecBins = 48;       // display spectra, 40 Hz .. 16 kHz, log spaced

//==============================================================================
struct Hit
{
    int slice = 0;          // slice of A
    int frame = 0;          // its attack in A (where it is heard to start, not the slice start)
    int step = 0;           // nearest 16th, 0 .. steps-1
    float offset = 0.0f;    // micro-timing: fraction of a step, + late, - early
    uint8_t cls = Kick;
    float level = 1.0f;     // 0..1 against the loudest hit
};

struct Genome
{
    int bars = 2, steps = 32;
    double framesPerStep = 1.0;
    double rate = 44100.0;
    double bpm = 120.0;                         // the break's own tempo
    std::vector<Hit> hits;                      // in time order
    std::array<int, kMaxSteps> main {};         // loudest hit on each step, -1 none
    std::array<uint8_t, kMaxSteps> mask {};     // classes present on each step (bits)

    // feel profile: by class and position in the beat (0 = on the beat .. 3 = the "a")
    float profOffset[NumCls][4] {};
    float profLevel[NumCls][4] {};
    int profCount[NumCls][4] {};
    float meanLevel[NumCls] {};
    float swing = 0.0f;                         // late-ness of the off 16ths against the on ones, steps
    float bias = 0.0f;                          // removed from every offset so the beats sit on the grid

    /** hit of class c on a step (-1 none) */
    int hitOf (int step, int c) const;
    /** offset a note of class c on that step should get: the break's own hit if it has one, else its feel */
    float offsetFor (int step, int c) const;
    /** level (0..1) of that hit, else the class' level at that position */
    float levelFor (int step, int c) const;
    double ms (float offsetSteps) const { return offsetSteps * framesPerStep / rate * 1000.0; }
};

/** Where each slice's hit is heard to start: the point its peak envelope crosses 20 %
    of the way from the quiet before it to its peak. Within a fraction of a millisecond,
    which is what micro-timing needs (slice starts are where to cut, not when it lands). */
std::vector<int> attacks (const Sample& s, const SliceMap& m);

/** Builds A's genome from its slices. bars: the loop's length (resolved, > 0). */
Genome extract (const Sample& s, const SliceMap& m, int bars, const std::vector<int>& attack);
inline Genome extract (const Sample& s, const SliceMap& m, int bars) { return extract (s, m, bars, attacks (s, m)); }

//==============================================================================
// What a slice sounds like, for matching A slices with B slices.
struct Features
{
    float low = 0, body = 0, crack = 0, air = 0;   // band shares, sum 1
    float centroid = 0;                            // log2 Hz
    float level = 0;
    float decay = 0;                               // seconds to -20 dB
};

std::vector<Features> features (const Sample& s, const SliceMap& m);

enum MatchMode { MatchClass = 0, MatchPosition, MatchRandom, NumMatch };
inline const char* matchName (int m) { static const char* n[] = { "CLASS", "POSITION", "RANDOM" }; return n[m % NumMatch]; }

/** For each slice of A, the slice of B that answers it. */
std::vector<int> matchSlices (const SliceMap& a, const std::vector<Features>& fa, int framesA,
                              const SliceMap& b, const std::vector<Features>& fb, int framesB,
                              int mode, uint32_t seed);

//==============================================================================
struct KitParams
{
    int match = MatchClass;
    float bPitch = 0.0f;      // semitones, B resampled before the blend
    float env = 0.5f;         // 0..1: how much of A's attack envelope the blends keep
    uint32_t seed = 1;
    int barsA = 0, barsB = 0; // 0 = auto
    bool operator== (const KitParams& o) const
    {
        return match == o.match && gtbs::same (bPitch, o.bPitch) && gtbs::same (env, o.env) && seed == o.seed
            && barsA == o.barsA && barsB == o.barsB;
    }
};

struct Kit
{
    std::shared_ptr<const Sample> a, b;
    SliceMap sa, sb;                   // sb in B's own frames
    std::vector<int> attack;           // per A slice: where the hit is heard (voices start here)
    std::vector<Features> fa, fb;
    Genome g, gb;                      // A's genome (the DNA) and B's (shown only)
    std::vector<int> match;            // A slice -> B slice
    double rate = 44100.0;             // A's rate: every layer plays at it
    int frames = 0;                    // A's length
    std::array<std::vector<float>, kLayers> L, R;
    std::vector<std::array<float, kSpecBins>> specA, specB;   // dB, per A slice and its B answer
    int serial = 0;

    int numSlices() const { return sa.count(); }
    int sliceLength (int i) const { return sa.lengthOf (i, frames); }
    /** slice of class c to play for a note on step s (the break's own if there is one) */
    int sliceFor (int step, int c) const;
};

std::unique_ptr<Kit> buildKit (std::shared_ptr<const Sample> a, std::shared_ptr<const Sample> b, const KitParams& p);

/** Spectral blend of two equal-length segments at morph m (0 = a, 1 = b), with
    envelope transfer (env 0..1). Exposed for the tests. */
std::vector<float> blend (const std::vector<float>& a, const std::vector<float>& b, float m, float env, double rate);

/** dB spectrum of a segment (first 2048 frames) in kSpecBins log bins. */
std::array<float, kSpecBins> spectrum (const float* x, int n, double rate);
inline float specBinHz (int k) { return 40.0f * std::pow (400.0f, (float) k / (float) (kSpecBins - 1)); }

} // namespace dnafam::dna
