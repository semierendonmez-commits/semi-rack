// Vendored unchanged from semierendonmez-commits/epoch Source/engine (Rack / MetaModule port).
// The plan: the score rendered at every 16th into the signals EPOCH derives (holes, tension,
// turns, arcs, drift) and the eight macros it hands to the siblings. Rendered on the message
// thread, read by the audio thread and published whole through EpochLink.
#pragma once

#include "Score.h"
#include <array>
#include <cstdint>
#include <vector>

namespace epoch
{

// what a macro slot follows
enum Source : int
{
    SrcEnergy = 0,   // the drawn ENERGY curve
    SrcSpace,        // the drawn SPACE curve: the share of time that should be silent
    SrcHole,         // the hole gate: 1 while a hole is open (the SPACE curve made real, seeded)
    SrcDensity,      // energy, with the holes cut out
    SrcTension,      // rises into 4 / 8 / 16 / 32-bar lines and section ends
    SrcHeat,         // energy and tension together (the default CHAOS)
    SrcArc,          // rise and fall inside each section
    SrcTurn,         // 1 on the last beat before a line of TURN level or above
    SrcCalm,         // 1 - energy
    SrcDrift,        // a slow seeded wander, 8 bars per cycle at RATE 1
    SrcBudget,       // edits allowed per 8 bars, from energy and tension (TURNAROUND / VARIATION)
    kNumSources
};
const char* sourceName (int s);

struct SlotConfig
{
    bool on = true;
    int role = 0;            // epochlink::Role
    int source = SrcEnergy;
    float lo = 0.0f, hi = 1.0f;   // the output range
    float curve = 0.0f;      // -1..1: bends the response (negative: early, positive: late)
    float smoothBeats = 0.0f;// lag, in beats (gates stay sharp with 0)
    bool invert = false;
    int cc = 20;             // MIDI CC number
};

struct PlanParams
{
    uint32_t seed = 1;
    int grainSteps = 16;     // how long one hole decision lasts (4 = a beat ... 64 = four bars)
    float cluster = 0.6f;    // 0: holes scattered, 0.95: holes come in long runs
    float spaceScale = 1.0f; // multiplies the SPACE curve (0..2)
    bool anchor = true;      // the first beat of every 4-bar line and each DROP's first bar stay full
    float tensionDepth = 1.0f;
    int turnLevel = 1;       // 0: every 4 bars, 1: 8, 2: 16, 3: 32
    float driftRate = 1.0f;
    float budgetMax = 4.0f;  // edits per 8 bars at full
    std::array<SlotConfig, kSlots> slots;

    static PlanParams defaults();
};

struct Plan
{
    int bars = 0;
    int steps = 0;
    std::array<std::vector<float>, kNumSources> src;   // each steps long
    std::array<std::vector<float>, kSlots> out;        // each steps long, 0..1
    std::vector<uint8_t> turnLevel;                    // per step: 0 none, 1..4 = 4/8/16/32-bar line starts here, 5 = a section
    std::vector<int16_t> barSection;                   // per bar: index of the section it is in (-1 before the first)
    std::array<uint8_t, kSlots> roles {};
    float targetSpace = 0.0f, plannedSpace = 0.0f;     // shares of the timeline
    float meanEnergy = 0.0f;
    int holes = 0;                                     // number of separate holes
    float meanHoleBars = 0.0f;

    void allocate();                                   // kMaxSteps everywhere, once
    float at (int slot, double step) const;            // linear between 16ths, clamped to the plan
};

// renders score + params into plan (plan must be allocated); no allocation
void render (const Score&, const PlanParams&, Plan&);

// the shaping of one slot, exposed for tests and the editor's preview
float shapeValue (float x, const SlotConfig&);

// counter-based hash: the same seed and index give the same number on every platform
inline uint32_t hash32 (uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
    h ^= h >> 16; h *= 0x85EBCA6Bu; h ^= h >> 13; h *= 0xC2B2AE35u; h ^= h >> 16;
    return h;
}
inline float hash01 (uint32_t a, uint32_t b) { return (float) (hash32 (a, b) >> 8) * (1.0f / 16777216.0f); }

} // namespace epoch
