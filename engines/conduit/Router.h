// The pin matrix at work: hits and values come in per row, every pin shapes, delays and rolls
// its dice, the columns sum. Pure C++, no JUCE, no allocation after prepare(): the audio thread.
//
//   router.begin (block);                   // the block's place in the song
//   router.hit (row, offset, strength, ppq); // an event on a row (any number)
//   router.value (row, v);                  // a value row's value now
//   router.end();                           // out[], gate[], trigs[]
// SEMI Family Rack port: kHistory 4096 -> 256 entries per row, written at a tempo-scaled stride
// (Router.cpp) so the DELAY window still covers 16 sixteenths (RAM on MetaModule).
#pragma once

#include "Patch.h"
#include <vector>

namespace conduit
{
struct Block
{
    int numSamples = 0;
    double sampleRate = 48000.0;
    double bpm = 120.0;
    double ppq = 0.0;            // the song position at the block's start (quarter notes)
    bool playing = false;
    uint64_t sampleTime = 0;     // a running sample counter (never goes back)
};

// a deterministic die: same seed, row, column and moment, same answer
inline float dice (uint32_t seed, int row, int col, uint64_t moment)
{
    uint64_t h = (uint64_t) seed * 0x9E3779B97F4A7C15ull ^ ((uint64_t) (row + 1) << 40) ^ ((uint64_t) (col + 1) << 52) ^ moment;
    h ^= h >> 33; h *= 0xff51afd7ed558ccdull; h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ull; h ^= h >> 33;
    return (float) (h >> 40) / (float) (1ull << 24);
}

class Router
{
public:
    static constexpr int kMaxPending = 1024;
    static constexpr int kMaxTrigs = 128;
    static constexpr int kHistory = 256;    // value rows remember this many points (for DELAY), see histStride

    struct Trig { int col; int offset; float vel; };

    void prepare (double sampleRate);
    void reset();
    void setPatch (const Patch& p) { patch = p; }
    const Patch& getPatch() const { return patch; }
    void setMaster (float m) { master = m; }

    void begin (const Block& b);
    void hit (int row, int offset, float strength, double ppq = -1.0);
    void value (int row, float v);
    void end();

    // ---- results of the last block
    float out[kCols] {};            // the column's sum (value / gate columns), MASTER applied, -1..1
    bool gate[kCols] {};            // gate columns: on
    Trig trigs[kMaxTrigs] {};
    int numTrigs = 0;
    float pinLevel[kRows][kCols] {};   // what each pin gave this block (UI)
    float rowLevel[kRows] {};          // the row's input: the newest envelope or the value (UI)
    int rowHits[kRows] {};             // hits counted since reset (UI)
    int dropped = 0;                   // hits that lost their dice (UI)

    double samplesPer16() const;

private:
    void fire (int row, int col, int offset, float strength, double hitPpq);
    void apply (int row, int col, int offset, float strength);
    float historyAt (int row, uint64_t t) const;

    Patch patch;
    Block blk;
    double sr = 48000.0;
    float master = 1.0f;

    struct Env { uint64_t start = 0; float strength = 0.0f; bool on = false; };
    Env env[kRows][kCols];

    struct Pending { int row, col; uint64_t due; float strength; };
    Pending pending[kMaxPending];
    int numPending = 0;

    float rowValue[kRows] {};
    bool rowHasValue[kRows] {};
    float prevValueOut[kRows][kCols] {};      // value -> trigger columns: the last level (edges)

    struct Hist { uint64_t t; float v; };
    std::vector<Hist> hist;                    // kRows x kHistory
    int histHead[kRows] {}, histCount[kRows] {};
    uint64_t histLast[kRows] {};               // SEMI port: time of the last history point

    uint64_t hitCount[kRows][kCols] {};
};

} // namespace conduit
