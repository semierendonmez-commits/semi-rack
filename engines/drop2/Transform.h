// From drop 1 to drop 2. Five moves, applied in this order:
//
//   HALF-TIME   the later bars switch to half time (snare on 3, kicks and bass stretched x2)
//   KICK SHIFT  kicks slide by 1/16 steps off their places
//   BASS LOCK   bass notes snap onto the kicks (and snares): the bass rides the drums
//   FLIP        whole bars of bass jump an octave, alternate, or turn upside down
//   CALL / RESP the bass splits in two voices: every other unit answers, transposed
//
// IDENTITY is the one knob over all of them: 1 = drop 1 again, 0 = everything the
// moves allow. Above 0.25 the anchors stay: the kick on the one, the snares on 2
// and 4, the first bass note of each bar. That is what keeps drop 2 recognisable.
// After generating, identity is also measured: onsets on a 1/16 grid, per role.
#pragma once

#include "Clip.h"

namespace dr2
{

struct Settings
{
    float identity = 0.6f;

    bool halftime = false;
    bool halftimeHats = true;        // keep the hats at full speed under the half-time

    bool kickShift = true;
    float kickAmount = 0.7f;
    int kickMax = 2;                 // furthest shift, in 1/16

    bool bassLock = true;
    float lockAmount = 0.8f;
    int lockTo = 0;                  // 0 = kicks, 1 = kicks + snares

    bool flip = false;
    int flipMode = 2;                // 0 = up an octave, 1 = down an octave, 2 = alternate bars, 3 = invert
    float flipAmount = 0.6f;

    bool split = false;
    int splitUnit = 1;               // 0 = 1/2 bar, 1 = 1 bar, 2 = 2 bars
    int respInterval = 0;            // semitones
};

inline const int kIntervals[] { 0, 3, 5, 7, 12, -5, -12 };
inline constexpr double kUnits[] { 2.0, 4.0, 8.0 };

struct Result
{
    Clip drums, bassA, bassB;        // bassB = the response voice (empty without the split)
    float identity = 1.0f;           // measured
    float idDrums = 1.0f, idBass = 1.0f;
};

Result generate (const Clip& drums, const Clip& bass, const Settings& s, std::uint32_t seed);

// onset similarity, 0..1 (Jaccard on a 1/16 grid, kick / snare / hats / bass weighted)
float similarity (const Clip& drumsA, const Clip& bassA, const Clip& drumsB, const Clip& bassB, const DrumRoles& roles);

// small deterministic RNG
struct Rng
{
    std::uint64_t s;
    explicit Rng (std::uint64_t seed) : s (seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) { next(); next(); }
    std::uint64_t next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 0x2545F4914F6CDD1Dull; }
    float uniform() { return (float) ((next() >> 40) * (1.0 / 16777216.0)); }
    int below (int n) { return n <= 1 ? 0 : (int) (uniform() * (float) n) % n; }
};

} // namespace dr2
