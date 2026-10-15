// Phrase arithmetic and the edit budget. Plain C++20, no JUCE: the tests run it alone.
//
// The grid: phrases of 4, 8, 16 and 32 bars, nested. Every 4th bar line is a
// phrase boundary, and its tier is the largest phrase that ends there:
//
//     bar line   4   8  12  16  20  24  28  32
//     tier       4   8   4  16   4   8   4  32
//
// A 32-bar block holds eight boundaries. The block has a budget of "edit beats"
// (beats of the break that may be rewritten). Each boundary asks for
// intensity x window beats; the plan decides how much each one gets.
//   hierarchy 0 : everyone is scaled down by the same factor
//   hierarchy 1 : the biggest turn is served first, small ones get what is left
// so the busiest edit is kept for the point where the arrangement turns.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace tna
{

constexpr int kTiers = 4;                 // 0 = 4 bars, 1 = 8, 2 = 16, 3 = 32
constexpr int kEvents = 8;                // boundaries in one 32-bar block
constexpr int kBlockBars = 32;
constexpr double kBeatsPerBar = 4.0;

inline int tierBars (int t) { return 4 << t; }

enum class Action : int { Off, Ghost, Roll, Retrig, Reverse, TapeStop, Cut };
constexpr int kNumActions = 7;
inline const char* actionName (Action a)
{
    static const char* n[] = { "OFF", "GHOST", "ROLL", "RETRIG", "REVERSE", "TAPE STOP", "CUT" };
    return n[(int) a];
}

enum class RollSource : int { Snare, Kick, Loudest, Vary };
enum class RollShape : int { Stepped, Smooth };

struct TierCfg
{
    Action action = Action::Off;
    double windowBeats = 2.0;   // how long before the bar line the action may start
    float intensity = 0.8f;     // how much of that window it wants
};

struct Settings
{
    std::array<TierCfg, kTiers> tier {{ { Action::Ghost,   2.0, 0.6f },
                                        { Action::Roll,    2.0, 0.8f },
                                        { Action::Retrig,  4.0, 0.8f },
                                        { Action::Roll,    4.0, 1.0f } }};
    float budget = 0.5f;        // 0..1  ->  0..24 edit beats per 32 bars
    float hierarchy = 0.7f;     // 0 = share evenly, 1 = biggest turn first
    float variation = 0.25f;    // per-event jitter, retrig / ghost choices
    std::uint32_t seed = 1;

    double gapBeats = 1.0;      // the wall: silence right before the bar line
    int gapFromTier = 2;        // ... at boundaries of this tier and up

    RollSource source = RollSource::Snare;
    RollShape shape = RollShape::Stepped;
    double rollStart = 0.5;     // first repeat length, beats (1/8 note)
    double rollEnd = 0.125;     // last repeat length, beats (1/32 note)
    float rollCurve = 1.5f;     // smooth shape: >1 holds back, <1 rushes
    float rollPitch = 5.0f;     // semitones climbed over the roll
    float rollVelStart = 0.45f; // level of the first repeat
    float layer2 = 0.0f;        // second snare, offset by half a repeat

    float ghostLevel = 0.45f;
    float mix = 1.0f;
    int phraseOffset = 0;       // bars: where bar 1 of the phrase grid sits
};

//==============================================================================
inline std::int64_t floorDiv (std::int64_t a, std::int64_t b)
{
    std::int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}
inline std::int64_t floorMod (std::int64_t a, std::int64_t b) { return a - floorDiv (a, b) * b; }

// tier of the boundary that sits on (relative) bar line `relBar` (a multiple of 4)
inline int tierOfBoundary (std::int64_t relBar)
{
    if (floorMod (relBar, 32) == 0) return 3;
    if (floorMod (relBar, 16) == 0) return 2;
    if (floorMod (relBar, 8) == 0)  return 1;
    return 0;
}

inline int tierOfEvent (int j)   // j = 0..7 inside a block
{
    static const int t[kEvents] { 0, 1, 0, 2, 0, 1, 0, 3 };
    return t[j];
}

// where a position sits on the phrase grid
struct GridPos
{
    std::int64_t relBar = 0;          // bar, counted from the phrase offset
    std::int64_t nextBoundary = 4;    // relative bar line of the next boundary
    std::int64_t block = 0;           // 32-bar block the next boundary closes an event of
    int event = 0;                    // 0..7
    int tier = 0;
    double boundaryPpq = 16.0;        // absolute ppq of that bar line
    double beatsToBoundary = 16.0;
};

inline GridPos locate (double ppq, int phraseOffset)
{
    GridPos g;
    const auto absBar = (std::int64_t) std::floor (ppq / kBeatsPerBar);
    g.relBar = absBar - phraseOffset;
    g.nextBoundary = (floorDiv (g.relBar, 4) + 1) * 4;
    g.block = floorDiv (g.nextBoundary - 1, kBlockBars);
    g.event = (int) (floorMod (g.nextBoundary - 1, kBlockBars) / 4);
    g.tier = tierOfBoundary (g.nextBoundary);
    g.boundaryPpq = (double) (g.nextBoundary + phraseOffset) * kBeatsPerBar;
    g.beatsToBoundary = g.boundaryPpq - ppq;
    return g;
}

//==============================================================================
// small deterministic RNG (same plan for the same seed and block, every time)
struct Rng
{
    std::uint64_t s;
    explicit Rng (std::uint64_t seed) : s (seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) { next(); next(); }
    std::uint64_t next()
    {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return s * 0x2545F4914F6CDD1Dull;
    }
    float uniform() { return (float) ((next() >> 40) * (1.0 / 16777216.0)); }
    int below (int n) { return n <= 1 ? 0 : (int) (uniform() * (float) n) % n; }
};

inline std::uint64_t eventSeed (std::uint32_t seed, std::int64_t boundaryBar)
{
    return ((std::uint64_t) seed << 32) ^ (std::uint64_t) (boundaryBar * 2654435761ll);
}

//==============================================================================
struct Plan
{
    std::int64_t block = 0;
    float budgetBeats = 0;
    std::array<float, kEvents> desire {};   // edit beats asked for
    std::array<float, kEvents> alloc {};    // edit beats granted
    std::array<float, kEvents> amount {};   // fraction of the tier window used, 0..1
    float total() const { float s = 0; for (auto a : alloc) s += a; return s; }
};

constexpr float kMaxBudgetBeats = 24.0f;

inline float costWeight (Action a)
{
    switch (a)
    {
        case Action::Off:   return 0.0f;
        case Action::Ghost: return 0.35f;   // adds hits, keeps the break: cheap
        case Action::Cut:   return 0.6f;    // silence is loud, but simple
        default:            return 1.0f;
    }
}

inline Plan makePlan (const Settings& s, std::int64_t block)
{
    Plan p;
    p.block = block;
    p.budgetBeats = s.budget * kMaxBudgetBeats;

    std::array<float, kEvents> cost {};
    float sumDesire = 0.0f;
    for (int j = 0; j < kEvents; ++j)
    {
        const auto& tc = s.tier[(size_t) tierOfEvent (j)];
        cost[(size_t) j] = costWeight (tc.action);
        p.desire[(size_t) j] = tc.intensity * (float) tc.windowBeats * cost[(size_t) j];
        sumDesire += p.desire[(size_t) j];
    }

    // proportional share
    const float scale = sumDesire > p.budgetBeats && sumDesire > 0.0f ? p.budgetBeats / sumDesire : 1.0f;
    // greedy: biggest tier first, the later of two equal tiers first (closer to the turn)
    std::array<int, kEvents> order { 7, 3, 5, 1, 6, 4, 2, 0 };
    std::array<float, kEvents> greedy {};
    float left = p.budgetBeats;
    for (int j : order)
    {
        const float g = std::min (p.desire[(size_t) j], std::max (0.0f, left));
        greedy[(size_t) j] = g;
        left -= g;
    }

    const float h = std::clamp (s.hierarchy, 0.0f, 1.0f);
    Rng rng (eventSeed (s.seed, block * 977 + 13));
    for (int j = 0; j < kEvents; ++j)
    {
        float a = (1.0f - h) * p.desire[(size_t) j] * scale + h * greedy[(size_t) j];
        if (s.variation > 0.0f && a > 0.0f)   // jitter never adds beats, it only holds some back
            a *= 1.0f - s.variation * 0.5f * rng.uniform();
        p.alloc[(size_t) j] = a;
        const auto& tc = s.tier[(size_t) tierOfEvent (j)];
        const float w = (float) tc.windowBeats * cost[(size_t) j];
        p.amount[(size_t) j] = w > 0.0f ? std::clamp (a / w, 0.0f, 1.0f) : 0.0f;
    }
    return p;
}

} // namespace tna
