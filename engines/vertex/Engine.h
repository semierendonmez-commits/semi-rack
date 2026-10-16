// Vendored into SemiFamily (Rack / MetaModule) unchanged from semierendonmez-commits/vertex
// Source/engine @14ca007 except this comment. Attribution: Vertex 1.3 by aorist (aorist.io);
// the device carries no licence text (ask the author before a public release).
// VERTEX - sound engine (no JUCE).
// Rebuilt from "Vertex 1.3 by aorist" (Max for Live instrument, a Drummer
// From Another Mother style percussion synth). Object-by-object reading in
// docs/ORIGINAL.md. Signal flow, as in the patch:
//
//   master clock (2n .. 128n, or EXT = MIDI notes) --> trigger
//   4 sequencers x 8 steps (PITCH, VEL, MOD 1, MOD 2), each with its own
//     length and a rate relative to the master clock, stepped from the
//     transport position (seek safe)
//   OSC 1, OSC 2: tri / square / sine / saw; freq knob ^4.5 + pitch seq;
//     MIDI pitch, scale quantiser, glide, pitch envelope in Hz, OSC1 -> OSC2
//     linear FM (Hz) and "sync" (OSC1 triangle into the square's sync input
//     and the triangle's peak position)
//   noise (white / pink) + osc 1 + osc 2 + external input
//     x amp envelope -> Sallen & Key filter (LP/BP/HP, 12/24 dB, saturator
//     pre/post) -> volume
//   three envelopes (amp, pitch, filter): a shared linear attack times a
//     curved decay; a trigger restarts an envelope only when the new level
//     is higher than where it currently is (the patch's >~ test)
//   every modulated knob has a source: none, MOD 1, MOD 2, VEL, PITCH (the
//     sequencers) or MIDI velocity; knob x source
#pragma once
#include "SallenKey.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace vx
{
enum Src { srcNone = 0, srcMod1, srcMod2, srcVel, srcPitch, srcMidi, numSrc };
enum Seq { seqPitch = 0, seqVel, seqMod1, seqMod2, numSeq };
enum Shape { shTri = 0, shSqu, shSin, shSaw };

// note values of the clock menus and their length in 16ths (the patch's coll "rates")
constexpr int kNumRates = 7;
inline double rateMult (int i) { static const double m[] = { 8, 4, 2, 1, 0.5, 0.25, 0.125 }; return m[i < 0 ? 0 : (i > 6 ? 6 : i)]; }

struct OscParams
{
    double freq = 130.8;        // Hz at the knob (5 .. 6200, ^4.5)
    int shape = shSqu;
    double level = 1.0;  int levelSrc = srcNone;
    double env = 0.0;    int envSrc = srcNone;   // pitch envelope depth, Hz (-600 .. 600)
    bool intPitch = true, extPitch = false, quantize = false, glide = true;
    double fine = 0.0;          // extra: semitones (-12 .. 12)
};

struct Params
{
    // clock
    int clock = 3;              // 0..6 = 2n..128n, 7 = EXT (MIDI)
    double swing = 0.0;         // extra: 0 .. 0.5 (16th pairs)
    // sequencers
    std::array<std::array<double, 8>, numSeq> step {};
    std::array<int, numSeq> length { 8, 8, 8, 8 };
    std::array<int, numSeq> rate { 3, 3, 3, 3 };
    double pitchRange = 0.5;    // tweak: pitch seq spans +- range of the freq knob
    // oscillators
    OscParams osc[2];
    double fm = 0.0; int fmSrc = srcNone; int fmFrom = 0;   // 0 = osc 1, 1 = external input
    double glide = 0.0; int glideSrc = srcNone;             // seconds (knob ^4, 0..1)
    bool sync = false;
    int syncMode = 0;           // extra: 0 = as the patch (square sync + tri peak), 1 = hard sync of every shape
    int root = 0, scale = 0;    // quantiser (Live's scale in the patch)
    // filter
    int filterType = 0;         // 0 LP, 1 BP, 2 HP
    double cutoff = 140.0; int cutoffSrc = srcNone;        // MIDI pitch 0..140
    double res = 0.3; int resSrc = srcNone;
    double fDecay = 150.0; int fDecaySrc = srcNone;        // ms
    double fEnv = 0.0; int fEnvSrc = srcVel;               // semitones -150..150
    double fMod = 0.0; int fModSrc = srcNone; int fModFrom = 0;   // 0 noise, 1 osc1, 2 osc2, 3 input
    int slope = 0, order = 0;
    double drive = 0.0;         // 0..1
    // amp / envelopes
    double aDecay = 150.0; int aDecaySrc = srcNone;
    double pDecay = 150.0; int pDecaySrc = srcNone;
    double attack = 0.0; int attackSrc = srcNone;          // ms, shared by the three envelopes
    double curve = -0.606;      // decay curve -1..0
    double volume = 1.0; int volumeSrc = srcVel;
    double noise = 0.0; int noiseSrc = srcNone; int noiseColour = 0;
    double extLevel = 0.0;      // 0..1
};

struct Transport
{
    bool running = false;       // the clock runs (host playing, or free run)
    double ppq = 0.0;           // quarter notes at the first sample of the block
    double ppqPerSample = 0.0;
    bool hostPlaying = false;   // for EXT: steps from the position while playing
};

struct MidiEvt { int pos; int note; float vel; };   // vel 0 = note off

class Engine
{
public:
    void prepare (double sampleRate);
    void reset();
    // in: optional external input (mono), may be nullptr
    void process (const Params&, const Transport&, const MidiEvt* events, int numEvents, const float* in, float* out, int n);

    // ---- read-outs for the UI (relaxed atomics) ----
    std::array<std::atomic<int>, numSeq> curStep {};
    std::atomic<uint32_t> triggers { 0 };
    std::atomic<float> envAmp { 0 }, envPitch { 0 }, envFilt { 0 }, cutoffNow { 140 }, freqNow[2] {}, srcNow[numSrc] {};
    static constexpr int kScope = 512;
    std::array<std::atomic<float>, kScope> scope {};
    std::atomic<int> scopeAt { 0 };

    // for tests
    int stepOf (int s) const { return step[s]; }
    double ampEnvValue() const { return env[0].value(); }
    static double scaleQuantize (double hz, int root, int scale);
    static const char* scaleName (int i);
    static int numScales();

private:
    struct Env
    {
        double a = 1.0, aInc = 1.0;      // attack ramp 0 -> 1
        double level = 0.0, t = 1.0, tInc = 0.0, k = 0.0;   // decay from level -> 0 over t 0..1
        double slideOut = 0.0;
        double value() const { return a * cur(); }
        double cur() const
        {
            if (t >= 1.0) return 0.0;
            if (k < 1.0e-4) return level * (1.0 - t);
            const double e = std::exp (-k);
            return level * (std::exp (-k * t) - e) / (1.0 - e);
        }
        void trigger (double lvl, double attackSamples, double decaySamples, double curve)
        {
            level = lvl; t = 0.0; tInc = 1.0 / std::max (1.0, decaySamples); k = -curve * 10.0;
            if (attackSamples < 1.0) { a = 1.0; aInc = 0.0; } else { a = 0.0; aInc = 1.0 / attackSamples; }
        }
        inline double tick()
        {
            const double v = value();
            if (a < 1.0) a = std::min (1.0, a + aInc);
            if (t < 1.0) t += tInc;
            return v;
        }
    };
    struct Osc
    {
        double ph = 0.0, freqSm = 0.0, glideTarget = -1.0, glideInc = 0.0;
        int glideLeft = 0;
        double triOut = 0.0, prevSync = 0.0;
    };

    double srcValue (int src) const;
    void trigger (const Params&);
    double oscFreq (int o, const Params&, double pitchOffset);

    double sr = 48000.0;
    double cur[numSeq] {};
    std::array<int, numSeq> step { 0, 0, 0, 0 };
    long long lastMaster = -1;
    bool wasRunning = false;
    long long extCount = 0;
    double extVel = 0.0;
    int lastNote = 60;
    Env env[3];                 // 0 amp, 1 pitch, 2 filter
    double pitchSlide = 0.0, ampSlide = 0.0, volSm = 1.0, extSm = 0.0;
    Osc osc[2];
    SallenKey filter;
    // noise
    uint32_t rng = 0x12345678u;
    double pk[7] {};
    double white = 0.0, pink = 0.0;
};
} // namespace vx
