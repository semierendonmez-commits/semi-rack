// semi-rack: vendored; namespaces wrapped in dnafam:: (see sources.mk for every change).
// NEUROSTACK: a mono drum & bass neuro bass whose articulations are switched by the
// "Pinball 2" Markov sequencer, phase-locked, with a pitch-tracked crossover and a
// clean sub under everything.
//
//   Articulations  eight neuro sounds (REESE WOBBLE GROWL FM STAB HOOVER TALK SCREECH
//                  SNARL), each a small patch with TONE / MOTION / GRIT. They all read
//                  one master phase, so the fundamental of every layer sits where the
//                  others' does: a switch from one to another never jumps in phase.
//   Pinball        16 steps, a 17 x 17 weight matrix, the device's step controls (trig
//                  = articulation, velocity, trig probability, duration, delay, delay
//                  probability) plus an octave per step. A step fires its articulation;
//                  CHOKE on an articulation (the device's per-trig mute) means it stops
//                  when another one fires, off means it keeps ringing and stacks.
//   Switch         every articulation has its own envelope; XFADE is the crossfade of a
//                  switch, RELEASE the end of a note.
//   Low end        (SUBSTRATE) the sum is split at XOVER x f0, f0 known exactly from the
//                  note (glide and step octaves included), with a Linkwitz-Riley pair (two
//                  Butterworth sections each side, the bands sum flat). Below the split: a sine
//                  on the master phase that follows the switch envelopes, plus BODY of the
//                  original low band, mono. Above it: the articulations, with WIDTH.
//
// Plain C++20, no JUCE, no allocation on the audio thread.
#pragma once

#include "../genotype/Walker.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace dnafam::neuro
{

// Eight slots; each plays one of twelve models.
enum Model { Reese = 0, Wobble, Growl, FmStab, Hoover, Talk, Screech, Snarl, Drop808, SolidFm, Hollow, SemiSines, NumModels };
constexpr int NumArts = 8;
inline const char* modelName (int m)  { static const char* n[] = { "REESE", "WOBBLE", "GROWL", "FM STAB", "HOOVER", "TALK", "SCREECH", "SNARL",
                                                                    "808 DROP", "SOLID FM", "HOLLOW", "SEMI SINES" }; return n[std::clamp (m, 0, NumModels - 1)]; }
inline const char* modelShort (int m) { static const char* n[] = { "RSE", "WOB", "GRL", "FMS", "HOV", "TLK", "SCR", "SNL", "808", "SFM", "HOL", "SMS" };
                                        return n[std::clamp (m, 0, NumModels - 1)]; }
// the slots' models by default (and the names the first versions used)
inline const char* artName (int a)  { return modelName (a & 7); }
inline const char* artShort (int a) { return modelShort (a & 7); }

// Scales for the rhythm's notes and the variation's exceptions
enum Scale { ScaleMinor = 0, ScalePhrygian, ScaleDorian, ScaleHarmMinor, ScaleMajor, ScaleChromatic, NumScales };
inline const std::array<int, 7>& scaleSteps (int s)
{
    static const std::array<std::array<int, 7>, 5> t {{ { 0, 2, 3, 5, 7, 8, 10 }, { 0, 1, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 9, 10 },
                                                        { 0, 2, 3, 5, 7, 8, 11 }, { 0, 2, 4, 5, 7, 9, 11 } }};
    return t[(size_t) std::clamp (s, 0, 4)];
}
/** the nearest note of the scale (chromatic: unchanged) */
inline int snapToScale (int semis, int scale)
{
    if (scale >= ScaleChromatic) return semis;
    const auto& st = scaleSteps (scale);
    const int oct = (int) std::floor ((double) semis / 12.0), pc = semis - 12 * oct;
    int best = 0, bd = 99;
    for (int d : st) for (int o : { -12, 0, 12 }) if (std::abs (pc - (d + o)) < bd) { bd = std::abs (pc - (d + o)); best = d + o; }
    return 12 * oct + best;
}

enum KickTemplate { KickOff = 0, KickDnb, KickTwoStep, KickFour, KickHalf, NumKicks };

constexpr int kSteps = 16;
constexpr int kTrigHold = 0, kTrigMute = 9;      // 1..8 = articulations

enum Source { SrcMidi = 0, SrcSelf, SrcLatch, NumSources };
enum Run { RunHost = 0, RunAlways };

struct ArtParams
{
    float level = 0.8f, tone = 0.5f, motion = 0.5f, grit = 0.3f;
    float attackMs = 0.0f;          // a swell in: the envelope rises in max(XFADE, ATTACK)
    int model = 0;
    bool mute = false, choke = true;
};

struct StepEdit
{
    std::atomic<int> trig { kTrigHold }, oct { 0 };
    std::atomic<float> vel { 100.0f }, prob { 100.0f }, dur { 1.0f }, delay { 0.0f }, dprob { 100.0f };
    std::atomic<float> mod { 0.0f };    // -1..1: TONE swept this far over the step's gate
};

// The bassline itself: where the bass plays, on 16ths, over two bars. The Pinball picks
// WHICH articulation (slowly, every few bars); the rhythm says WHEN it sounds.
constexpr int kRhythm = 32;
enum CellType { CellRest = 0, CellHit, CellTie };
struct RhythmCell
{
    std::atomic<int> type { CellRest }, semis { 0 }, slide { 0 };
    std::atomic<int> vel { 100 };   // 1..127: level of the layers and (VEL > TONE) their brightness
    std::atomic<int> bend { 0 };    // semitones the note starts away from its pitch, gliding in over BEND TIME
};

struct Brain
{
    walk::Matrix matrix;
    std::array<StepEdit, kSteps> steps;
    std::array<RhythmCell, kRhythm> rhythm;
    std::atomic<int> rhythmLen { kRhythm };     // 16 or 32
    Brain() { matrix.linear (kSteps); }
};

struct Params
{
    std::array<ArtParams, NumArts> art {};
    int source = SrcMidi;
    int root = 36;                  // SELF
    int octave = 0;
    float glideMs = 0.0f, bendRange = 2.0f;
    float xfadeMs = 4.0f, releaseMs = 60.0f;
    float xover = 2.5f;             // x f0
    float sub = 0.8f, subDrive = 0.15f, body = 0.0f;
    int subOct = 0;                 // 0 or -1
    float drive = 0.25f, width = 0.35f, gain = 1.0f;
    // the walk
    float mult = 1.0f, swing = 0.0f, resetBars = 1.0f;   // mult: 1/4 .. 64 (four bars); resetBars 0 = never
    bool rhythmOn = false;          // the rhythm gates the bass (SELF / LATCH, or MIDI with a key held)
    bool durInSteps = true;         // DURATION counts in steps (x TIME), else in 16ths as the device
    float drift = 0.0f;             // a slow LFO on TONE / MOTION of every articulation
    float driftBars = 4.0f;
    // the bassline
    bool rhythmHalf = false;        // a cell lasts an 1/8 (half-tempo, reggae / jungle)
    int scale = ScaleMinor;
    float variation = 0.0f;         // 0..1: how often the rules fire
    int budget = 4;                 // edits per 8 bars at most
    float gate = 1.0f;              // 0.4..1: notes shortened to this much of their length
    float velTone = 0.4f;           // how much a cell's velocity opens TONE
    float bendMs = 120.0f;
    // the phrase
    float arcAmt = 0.0f;            // TONE opens over the first half, GRIT and DRIVE rise over the second
    float arcBars = 16.0f;
    // the kick
    int kick = KickDnb;
    float duck = 0.0f, duckRelMs = 100.0f;
    bool duckSidechain = false;
    float snareGapMs = 0.0f;        // notes end this long before the snare (beats 2 and 4, half-time: 3)
    int startStep = 1, run = RunHost;
    uint32_t seed = 1;
    bool playing = false;
    double ppq = 0.0, bpm = 174.0;
    bool external = false;          // LOOPGENOME: driven by ExtEvents only
    Params() { for (int i = 0; i < NumArts; ++i) art[(size_t) i].model = i; }
};

struct MidiEvent { int offset = 0; uint8_t note = 0, vel = 0; bool bend = false; float bendValue = 0.0f; };

struct FireEvent { int8_t step = -1, art = -1, from = -1; uint8_t vel = 0; };

// LOOPGENOME: the bass driven from outside (the joint walk decides, this engine plays).
// With Params::external the engine's own walk, rhythm, kick template and snare gap are
// off; these events, at sample offsets in the block, take their place.
enum ExtKind { ExtCell = 0, ExtArt, ExtKick, ExtGap, ExtRelease };
struct ExtEvent
{
    int at = 0;
    uint8_t kind = ExtCell;
    int8_t type = CellRest, semis = 0, slide = 0, bend = 0, oct = 0;
    uint8_t vel = 100;
    int8_t art = -1, step = -1;
    int gateSamples = 0;            // ExtCell hit: note length for GATE (0 = until the next cell)
};

//==============================================================================
// DSP helpers
namespace dsp
{
constexpr double kTwoPi = 6.283185307179586;

inline float polyBlep (double t, double dt)
{
    if (dt <= 0.0) return 0.0f;
    if (t < dt) { t /= dt; return (float) (t + t - t * t - 1.0); }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
    return 0.0f;
}
inline float saw (double p, double dt) { return (float) (2.0 * p - 1.0) - polyBlep (p, dt); }
inline double wrap (double p) { return p - std::floor (p); }

// TPT state-variable filter (Zavalishin): click-free when the cutoff moves
struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f;
    float g = 0.1f, k = 1.4f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    void set (double fc, double sr, double q)
    {
        fc = std::clamp (fc, 10.0, sr * 0.45);
        g = (float) std::tan (3.141592653589793 * fc / sr);
        k = (float) (1.0 / std::max (0.3, q));
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0.0f; }
    // returns low, band, high
    void tick (float x, float& lp, float& bp, float& hp)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
    float low (float x) { float l, b, h; tick (x, l, b, h); return l; }
    float band (float x) { float l, b, h; tick (x, l, b, h); return b; }
    float high (float x) { float l, b, h; tick (x, l, b, h); return h; }
};

inline float drive (float x, float amt)
{
    const float d = 1.0f + amt * 7.0f;
    return std::tanh (x * d) / std::tanh (d);
}
} // namespace dsp

//==============================================================================
class Engine
{
public:
    static constexpr int kScope = 4096;

    void prepare (double sampleRate, int maxBlock);
    void reset();

    /** One block. outL / outR are overwritten. */
    void process (Brain& brain, const Params& p, const MidiEvent* in, int numIn, float* outL, float* outR, int n,
                  const float* sidechain = nullptr, const ExtEvent* ext = nullptr, int numExt = 0);

    // control (any thread)
    std::atomic<int> resetRequest { 0 }, haltRequest { 0 }, jumpRequest { -1 }, auditionRequest { -1 };

    // views
    std::atomic<int> viewStep { -1 }, viewPlaying { 0 }, viewHalted { 0 }, viewArt { -1 };
    std::atomic<float> viewF0 { 0.0f }, viewXover { 0.0f }, viewSub { 0.0f };
    std::array<std::atomic<float>, NumArts> viewEnv {}, viewPhase {}, viewCoh {};
    std::atomic<double> viewBpm { 174.0 };
    std::atomic<int64_t> viewTick { 0 };
    std::atomic<int> viewCell { -1 };
    // the variant of the rhythm that plays now: type | (semis + 64) << 2 | midOct << 9 | changed << 10
    std::array<std::atomic<int>, kRhythm> viewVariant {};
    std::atomic<float> viewDuck { 0.0f }, viewArc { 0.0f };
    std::atomic<int> viewEdits { 0 };
    /** for the tests: cells the variation changed in the last cycle it built */
    int lastChanged() const { return varChanged; }
    // phase-synchronous scope: (master phase, upper, low), written by the audio thread
    std::array<float, kScope> scopePhase {}, scopeUp {}, scopeLow {};
    std::atomic<int> scopeWrite { 0 };
    bool popFire (FireEvent& e);

    /** for the tests */
    double masterPhase() const { return phi; }
    float currentF0() const { return (float) f0; }
    int lastArt() const { return keyArt; }

private:
    struct Voice
    {
        // envelope
        float env = 0.0f, target = 0.0f, rate = 0.0f;
        int64_t gateLeft = 0;            // samples, -1 = until the key goes
        float vel = 1.0f;
        bool active = false;
        int64_t gateTotal = 1, elapsed = 0;
        float sweep = 0.0f;
        // per-articulation state
        double p1 = 0, p2 = 0, p3 = 0, pm = 0;
        float e = 0.0f, eDecay = 0.999f; // trigger envelope
        double lfo = 0.0;
        float fb = 0.0f, hold = 0.0f;
        int holdCount = 0;
        uint32_t noise = 0x1234567u;
        dsp::Svf f1, f2, f3, f4;
        float cohC = 0.0f, cohS = 0.0f, cohE = 0.0f;
    };

    struct Pending { int64_t at = 0; int step = 0; int art = 0; float vel = 1.0f; float durSamples = 0.0f; int oct = 0; bool mute = false; int from = -1; float mod = 0.0f; };

    float renderArt (int model, Voice& v, const ArtParams& ap, double lfoInc);
    void buildVariant (const Brain& b, const Params& p, int64_t cycle);
    void fire (const Pending& e, const Params& p);
    void startArt (int a, float vel, int64_t gate, const Params& p, bool chokeOthers, float sweep = 0.0f);
    void releaseAll (float ms);
    void emit (const FireEvent& e);

    double sr = 44100.0;
    int64_t clock = 0;
    // pitch
    double phi = 0.0, f0 = 55.0, f0Target = 55.0, bend = 0.0;
    int stepOct = 0;
    std::array<int, 16> held {};
    int numHeld = 0, latched = -1, keyArt = Reese;
    float keyEnv = 0.0f, keyTarget = 0.0f;
    bool keyDown = false, freeKey = false;
    float sumEnvSmooth = 0.0f;
    // voices
    std::array<Voice, NumArts> voices {};
    // low end
    dsp::Svf xa, xb, xc, xd;   // the split: Linkwitz-Riley low (xa, xb) and high (xc, xd)
    float subPrev = 0.0f;
    // width
    std::array<float, 2048> dly {};
    int dlyPos = 0;
    double chorus = 0.0, subHalf = 0.0, noteNow = -1000.0;
    int64_t auditionLeft = 0;
    // the rhythm
    int64_t nextCell = INT64_MIN;
    bool rhyGate = false, rhySlide = false, rhyOct = false;
    int rhySemis = 0;
    float rhyVel = 100.0f / 127.0f;
    int64_t gateOffAt = INT64_MAX;
    double bendNow = 0.0, bendStep = 0.0;
    // the variation: the cycle (2 bars of cells) that plays, as built
    struct VCell { int8_t type = 0, semis = 0, slide = 0, bend = 0, oct = 0, changed = 0; uint8_t vel = 100; };
    std::array<VCell, kRhythm> var {};
    int64_t varCycle = INT64_MIN, budgetBlock = INT64_MIN;
    int budgetLeft = 0, varChanged = 0;
    // the kick and the phrase
    float duckEnv = 0.0f, scEnv = 0.0f, duckNow = 0.0f;
    bool duckAtk = false, gateCut = false, lastHalf = false;
    float velSmooth = 1.0f;
    double arcPos = 0.0;
    double driftPos = 0.0;
    // the walk
    walk::Walker walker;
    uint32_t seedSeen = 0;
    double freePos = 0.0, lastPos = -1.0e9, lastMult = 1.0;
    int64_t nextTick = INT64_MIN;
    bool wasRunning = false, pendingReset = false;
    std::array<Pending, 64> queue {};
    int numQueued = 0;
    // views
    std::array<FireEvent, 128> fires {};
    std::atomic<int> fireWrite { 0 }, fireRead { 0 };
    int scopeDecim = 0;
};

} // namespace dnafam::neuro
