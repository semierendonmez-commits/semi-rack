// SEMI port: vendored from FLOATER Source/engine (v0.7), namespace fl -> flt; see engines/floater/sources.mk.
// FLOATER engine types: plain C++20, no JUCE.
//
// One floater case, after the way Richard Devine documents his Eurorack patches:
// everything clocked from one master clock, the clock through a rotating clock
// divider into eight lanes, each lane through a probability gate and a burst
// generator, each lane triggering one module; eight CV sources patched by pins to
// fourteen destinations; the modules into a spectral resonator, a clock-broken delay,
// a granular scatter, a reverb that changes program on the clock, a tape that breaks the
// master on the grid (REEL), and a bus compressor.
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace flt
{

inline constexpr int kLanes = 8;     // = the eight outputs of the rotating divider
inline constexpr int kVoices = 7;    // lanes 1..7 play a module, lane 8 triggers SCATTER
inline constexpr int kSources = 10;
inline constexpr int kDests = 14;
inline constexpr int kMaxSteps = 32;
inline constexpr int kControl = 32;  // samples per control-rate step

enum Lane { kick = 0, mesh, fm, opto, lantern, zap, strike, scatter };

inline const std::array<const char*, kLanes>& laneNames()
{
    static const std::array<const char*, kLanes> n { "KICK", "MESH", "FM", "OPTO", "LANTERN", "ZAP", "STRIKE", "SCATTER" };
    return n;
}

// new sources go last so the pins of the older ones keep their ids
enum Source { smooth = 0, stepped, pong, lfo, forge, chaos, woggle, seq, droneSrc, arc };

inline const std::array<const char*, kSources>& sourceNames()
{
    static const std::array<const char*, kSources> n { "SMOOTH", "STEPPED", "PONG", "LFO", "FORGE", "CHAOS", "WOGGLE", "SEQ", "DRONE", "ARC" };
    return n;
}

enum Dest { kPitch = 0, kDecay, meshTone, fmIndex, fmRatio, optoColor, hatDecay, zapSweep, strNote, strBright, dlyTime, scatSize, rotate, smrFreq };

inline const std::array<const char*, kDests>& destNames()
{
    static const std::array<const char*, kDests> n { "K.PITCH", "K.DECAY", "M.TONE", "FM.INDEX", "FM.RATIO", "OPTO.COL", "HAT.DEC",
                                                    "ZAP.SWP", "STR.NOTE", "STR.BRT", "DLY.TIME", "SCAT.SIZE", "ROTATE", "SMR.FREQ" };
    return n;
}

struct LaneParams
{
    bool on = true;
    int steps = 16, hits = 4, offset = 0, div = 1;   // div 1..8
    float chance = 1.0f;                              // 0..1
    float burst = 0.0f;                               // 0..1: chance a hit becomes a burst
    bool draw = false;                                // false: Euclidean; true: the drawn steps (a trigger sequencer)
    uint32_t pattern = 0;                             // the drawn steps, bit s = step s
    // LOCK, after the Music Thing Turing Machine: 1 = the pattern loops as it is; lower, each pass
    // over a step may flip it (add or take away a hit), so the pattern walks around what is set
    float lock = 1.0f;
    float vary = 0.0f;                                // per-hit drift of tune / decay / colour / level / pan / timing
    int logic = 0;                                    // 0 off, else 1 + op * 8 + other lane (op: AND, OR, XOR, NOT)
    std::array<uint8_t, kMaxSteps> cond {};           // per step: a condition (condNames())
    // per step: offsets locked onto TUNE, COLOR, DECAY of the lane's module (-1..1, 0 = no lock)
    std::array<std::array<float, kMaxSteps>, 3> plock {};
    int droneAct = 0;                                 // what a hit does to the DRONE (droneActNames())
};

// ---- conditions on a step (Elektron-style trig conditions)
inline constexpr int kConds = 16;
inline const std::array<const char*, kConds>& condNames()
{
    static const std::array<const char*, kConds> n { "--", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4",
                                                     "1:8", "8:8", "FILL", "!FILL", "PRE", "!PRE" };
    return n;
}
// A:B conditions: the step plays on the A-th of every B passes of its lane's pattern
inline bool condRatio (int c, int& a, int& b)
{
    static const int ab[12][2] { { 0, 0 }, { 1, 2 }, { 2, 2 }, { 1, 3 }, { 2, 3 }, { 3, 3 }, { 1, 4 }, { 2, 4 }, { 3, 4 }, { 4, 4 }, { 1, 8 }, { 8, 8 } };
    if (c < 1 || c > 11) return false;
    a = ab[c][0]; b = ab[c][1];
    return true;
}
inline constexpr int kCondFill = 12, kCondNotFill = 13, kCondPre = 14, kCondNotPre = 15;

// ---- logic between lanes (after Devine's logic modules): the lane's gate against another lane's
enum LogicOp { logicAnd = 0, logicOr, logicXor, logicNot };
inline const std::array<const char*, 4>& logicOpNames()
{
    static const std::array<const char*, 4> n { "AND", "OR", "XOR", "NOT" };
    return n;
}
inline bool logicApply (int op, bool own, bool other)
{
    switch (op)
    {
        case logicAnd: return own && other;
        case logicOr:  return own || other;
        case logicXor: return own != other;
        default:       return own && ! other;
    }
}

// does lane L hit on step s (0..steps-1)?
inline bool laneHits (const LaneParams& L, int steps, int s)
{
    if (L.draw) return s >= 0 && s < 32 && ((L.pattern >> s) & 1u) != 0;
    if (steps <= 0 || L.hits <= 0) return false;
    if (L.hits >= steps) return true;
    const int k = ((s - L.offset) % steps + steps) % steps;
    return (k * L.hits) % steps < L.hits;
}

// the Euclidean pattern as drawn steps (what a lane shows when it is switched to DRAW)
inline uint32_t euclidMask (int steps, int hits, int offset)
{
    uint32_t m = 0;
    LaneParams l; l.hits = hits; l.offset = offset;
    for (int s = 0; s < steps && s < 32; ++s) if (laneHits (l, steps, s)) m |= 1u << s;
    return m;
}

struct VoiceParams
{
    int model = 0;                                    // 0 = the slot's own voice, 1..24 Plaits, 25.. Braids (see Schwung.h)
    float tune = 0.5f, decay = 0.5f, color = 0.5f;    // 0..1, meaning per module
    float harm = 0.5f, morph = 0.5f;                  // 0..1, meaning per module / Plaits engine
    float level = 0.8f, pan = 0.0f, send = 0.2f;      // level 0..1, pan -1..1, send 0..1
    // X-MOD: another lane's audio (or the feedback loop) moving this one at audio rate
    int xmod = 0;                                     // xmodNames()
    int xsrc = 0;                                     // 0..6 a lane's module, 7 FDBK (the SMR's rings of the master), 8 DRONE
    float xamt = 0.5f;
    bool sus = false;                                 // SUSTAIN: the slot holds its note (a drone); hits re-strike it
};

inline const std::array<const char*, 5>& xmodNames()
{
    static const std::array<const char*, 5> n { "OFF", "RING", "PM", "FOLLOW", "DUCK" };
    return n;
}
inline constexpr int kXsrcFeedback = 7, kXsrcDrone = 8;

struct Params
{
    // ---- clock
    float bpm = 150.0f;            // internal tempo (when the host gives none)
    bool run = true;               // free-run when the host is stopped
    float swing = 0.0f;            // 0..0.5 of a 16th
    float loose = 0.0f;            // GRID <> FREE: each hit lands up to a whole step late, bursts at ~100 ms
    float ebb = 0.0f;              // EBB: every lane's chance rides one slow seeded wave (silences and dense stretches)
    int rotate = 0;                // 0..7
    int loopBars = 0;              // 0 = free, else 1/2/4/8: the random repeats
    int seed = 1;
    int burstMax = 4;              // 2..8 ratchets
    int burstShape = 0;            // 0 bounce (accelerating), 1 even, 2 decelerating
    std::array<LaneParams, kLanes> lanes {};

    // ---- modules
    std::array<VoiceParams, kVoices> voices {};
    int scale = 0;                 // STRIKE quantizer scale

    // ---- scatter (lane 8): model 0 = FLOATER grains, 1..4 = Clouds granular / stretch / looper / spectral,
    // 5 / 6 = Parasites OLIVERB / RESONESTOR
    int scatModel = 0;
    float scatSize = 0.35f, scatDensity = 0.5f, scatPitch = 0.3f, scatSpread = 0.6f, scatFeed = 0.0f, scatMix = 0.5f;
    float scatPosition = 0.5f, scatTexture = 0.5f;
    int scatCloudsPitch = 0;       // Clouds' PITCH, semitones -24..24 (Beads' too)
    float scatReverb = 0.0f;       // Beads' own REVERB

    // ---- modulation
    std::array<float, kSources> rate {};      // 0..1
    std::array<float, kSources> depth {};     // 0..1
    std::array<std::array<int, kDests>, kSources> pins {};   // -1, 0, +1
    // SEQ: sixteen stepped values (after Make Noise Rene), moved on by a lane's hits; with a Y lane it is
    // a 4 x 4 grid, X and Y moved on by their own lanes (Cartesian); RATE sets the clock when X is the clock
    std::array<float, 16> seqValues {};
    int seqLen = 16;               // 1..16, linear mode
    int seqX = 0;                  // 0 = the clock, 1..8 a lane
    int seqY = 0;                  // 0 = linear, 1..8 a lane (Cartesian)
    float seqGlide = 0.0f;

    // ---- fx / bus
    int dlyDiv = 3;                // index into delayDivisions()
    float dlyFeedback = 0.45f, dlyBreak = 0.3f, dlyMix = 0.3f;
    int dlyStyle = 0;              // what a break does: 0 zigzag, 1 roll, 2 reverse, 3 tape stop
    int dlyMachine = 0;            // 0 clean, 1 S950 12-bit, 2 S1000, 3 Paula 8-bit (from BREAKSLICER)
    float dlyRate = 0.5f;          // the machine's sample clock

    // ---- SMR: six resonant bands on the sends, rotated up the scale by a lane (4ms SMR)
    float smrMix = 0.0f, smrFreq = 0.4f, smrQ = 0.6f, smrSpread = 0.3f, smrGlide = 0.3f;
    int smrScale = 0, smrRotateBy = 0;   // 0 off, 1..8 a lane
    int verbModel = 0;             // 0 FDN, 1 CloudSeed
    float verbSize = 0.6f, verbDecay = 0.5f, verbDamp = 0.4f, verbMix = 0.25f;
    int programChange = 0;         // 0 off, 1 every bar, 2 every 2 bars, 3 every 4, 4 on lane 8
    // the bus: Drum Buss (schwung-busdriver)
    bool busComp = false;
    int busChar = 1;               // 0 soft (a folder), 1 medium, 2 hard
    float drive = 0.15f, crunch = 0.05f, trans = 0.0f, boom = 0.15f;   // trans -1..1
    float level = 0.0f;            // dB

    // ---- phrase turns (from TURNAROUND): before every 4 / 8 / 16 / 32-bar line the lanes fill
    float turns = 0.0f;            // 0..1
    bool wall = false;             // a beat of silence before the 16 and 32-bar lines

    // ---- REEL: the master tape (see Reel.h)
    float reelChaos = 0.0f;        // ORDER <-> CHAOS
    int reelLength = 1;            // 1 / 2 / 4 / 8 bars
    int reelGene = 2;              // geneNames()
    float reelSlide = 0.0f, reelVarispeed = 0.75f, reelMorph = 0.45f, reelOrganize = 0.0f;
    int reelOrgBy = 0;             // 0 off, 1..8: each hit of that lane moves ORGANIZE on one splice
    float reelSplit = 0.0f;        // 0..1: off, then 40..800 Hz
    float reelDuck = 0.5f;
    int reelDuckBy = 1;            // 0 off, 1..8
    bool reelHold = false, reelAnchor = true;
    int reelProtect = 1 | 256;     // bit i: lane i bypasses the reel (the kick by default); bit 8: the DRONE and RESONATOR

    // ---- DRONE (see Drone.h): two cross-modulated oscillators that never stop, played by the lanes
    float dLevel = 0.0f, dFine = 0.5f, dFmAB = 0.2f, dFmBA = 0.0f, dRing = 0.0f, dSync = 0.0f, dFold = 0.1f, dShape = 0.0f;
    float dBright = 0.55f, dChaos = 0.0f, dSend = 0.3f, dDuck = 0.6f;
    int dPitch = -12;              // semitones from the case's key (STRIKE's root)
    int dRatio = 6;                // droneRatioNames(): 2:1
    int dPitchCv = 0, dFmCv = 0, dFoldCv = 0, dLpgCv = 0;   // 0 none, else 1 + a CV source
    float dArc = 0.0f;             // how far the ARC opens it
    // RESONATOR: four strings in the drone's chord, excited by the drums
    float resMix = 0.0f, resDecay = 0.5f, resBright = 0.5f;
    // ARC: a function over the phrase (after Maths): rises over ARC LEN bars, falls into the line
    int arcLen = 2;                // 4 / 8 / 16 / 32 bars
    float arcShape = 0.8f;         // how much of the phrase it rises for
    // the beds: SCATTER held frozen, SPACE held infinite
    bool scatFreeze = false, verbInfinite = false;

    // ---- FEEDBACK: the master, a clock division later, back into the SMR and from there to the modules
    float fdbk = 0.0f;
    int fdbkTime = 1;              // fdbkTimeNames()

    // ---- LOCK: bump to forget every mutation the unlocked lanes have made
    int flipReset = 0;

    // ---- stems: when >= 0 only this lane is heard (0..7), effects as in the mix, no bus, no reel
    int solo = -1;

    Params()
    {
        rate  = { 0.35f, 0.5f, 0.4f, 0.3f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
        depth = { 0.5f, 0.5f, 0.5f, 0.5f, 0.6f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
        for (auto& r : pins) r.fill (0);
    }
};

// the last two are not divisions: the delay is tuned, as a Mungo d0 in its Karplus-Strong setting,
// to the root of STRIKE's chord (and an octave up)
inline constexpr int kDelayKarplus = 7;
inline const std::array<const char*, 9>& delayDivisionNames()
{
    static const std::array<const char*, 9> n { "1/32", "1/16", "1/16.", "1/8", "1/8.", "1/4", "1/2", "KARPLUS", "KARPLUS 8VA" };
    return n;
}
inline double delayDivisionBeats (int i)
{
    static const double v[7] { 0.125, 0.25, 0.375, 0.5, 0.75, 1.0, 2.0 };
    return v[i < 0 ? 0 : (i > 6 ? 6 : i)];
}
inline const std::array<const char*, 4>& breakStyleNames()
{
    static const std::array<const char*, 4> n { "ZIGZAG", "ROLL", "REVERSE", "TAPE STOP" };
    return n;
}
inline const std::array<const char*, 4>& machineNames()
{
    static const std::array<const char*, 4> n { "CLEAN", "S950", "S1000", "PAULA" };
    return n;
}

inline const std::array<const char*, 4>& fdbkTimeNames()
{
    static const std::array<const char*, 4> n { "1/16", "1/8", "1/4", "1/2" };
    return n;
}
inline double fdbkTimeBeats (int i) { static const double v[4] { 0.25, 0.5, 1.0, 2.0 }; return v[i < 0 ? 0 : (i > 3 ? 3 : i)]; }
inline const std::array<const char*, 4>& arcLenNames()
{
    static const std::array<const char*, 4> n { "4 BARS", "8 BARS", "16 BARS", "32 BARS" };
    return n;
}
inline float reelSplitHz (float v01) { return v01 <= 0.0f ? 0.0f : 40.0f * std::pow (20.0f, v01 > 1.0f ? 1.0f : v01); }

inline const std::array<const char*, 6>& scaleNames()
{
    static const std::array<const char*, 6> n { "MINOR PENTA", "MAJOR PENTA", "DORIAN", "WHOLE TONE", "HIRAJOSHI", "CHROMATIC" };
    return n;
}

// Transport for one block, from the host (or the internal clock).
struct Transport
{
    bool playing = false;
    double bpm = 120.0;
    double ppq = 0.0;              // position at the block's first sample (quarter notes)
    bool hostHasPosition = false;
};

// Small deterministic hash -> [0, 1).
inline float hash01 (uint32_t a, uint32_t b, uint32_t c = 0, uint32_t d = 0)
{
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du ^ (d + 0x27D4EB2Fu) * 0x9E3779B9u;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return (float) (h >> 8) * (1.0f / 16777216.0f);
}

// Euclidean pattern with offset: does step s (0..steps-1) hit?
inline bool euclid (int steps, int hits, int offset, int s)
{
    if (steps <= 0 || hits <= 0) return false;
    if (hits >= steps) return true;
    const int k = ((s - offset) % steps + steps) % steps;
    return (k * hits) % steps < hits;
}

} // namespace flt
