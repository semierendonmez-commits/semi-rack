// SEMI port: vendored from FLOATDAT Source/engine (7e30328); namespaces renamed (ryoz/vtx/pinball/jdat/pdata -> fdat_*), see sources.mk.
// VTX — four voices from Vortessa v3.2's gen~ code, ported line by line (JUCE-free):
//
//   CAVE KICK   ftrve_bd.gendsp     sine body with a pitch sweep, sub, punch, click, soft
//                                   saturation, into "the nuclear cave": predelay, three
//                                   allpasses (2073 / 3137 / 4987 samples), a 3 kHz low pass
//                                   and a 15007-sample feedback tail
//   FDBK TONE   ftrve_fdbd.gendsp   granular resonant feedback: noise grains ring three
//                                   two-pole resonators (f, f x 1.618, f / 2) and two combs,
//                                   bit / rate crush, the same cave with a 137 ms predelay
//   GHOST NET   ghostnetwork        Vink resonant network v4: four tuned feedback delays
//                                   (ratios 1, 1.414, 1.933, 2.771), a Hadamard feedback matrix,
//                                   self excitation, tanh, fed by the other layers' sound
//   DRIFT       driftnoise          a beating cluster on the left, breath / ring-mod noise /
//                                   fourth harmonic on the right, five slow LFOs
//
// In Vortessa the kick and the tone run on their own clocks (an 8 Hz phasor through a
// probability, a grain phasor); here they can still do that (OWN) or be struck by the
// Pinball lanes (VTX KICK / VTX TONE), or both. The tone, when struck, opens its grain clock
// for the trig's duration.
#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <cmath>
#include <algorithm>

namespace fdat_vtx
{
inline constexpr double twoPi = 6.283185307179586;

// gen's noise(): white, -1..1 (a small LCG, deterministic per seed)
struct Noise
{
    uint32_t s = 0x9E3779B9u;
    float operator()() noexcept { s = s * 1664525u + 1013904223u; return (float) ((int32_t) s) * (1.0f / 2147483648.0f); }
};

// gen's dcblock: y = x - x1 + 0.9997 * y1
struct DcBlock
{
    float x1 = 0, y1 = 0;
    float operator() (float x) noexcept { const float y = x - x1 + 0.9997f * y1; x1 = x; y1 = y; return y; }
    void reset() { x1 = y1 = 0; }
};

// a circular buffer with gen's peek / poke at integer positions (and a fractional read)
struct Ring
{
    std::vector<float> b;
    int w = 0;
    void setSize (int n) { b.assign ((size_t) std::max (8, n), 0.0f); w = 0; }
    void clear() { std::fill (b.begin(), b.end(), 0.0f); w = 0; }
    int size() const { return (int) b.size(); }
    float read (double delay) const noexcept
    {
        const int n = size();
        double r = (double) w - delay;
        while (r < 0.0) r += n;
        const int i0 = (int) r;
        const float f = (float) (r - i0);
        const float a = b[(size_t) (i0 % n)], c = b[(size_t) ((i0 + 1) % n)];
        return a + (c - a) * f;
    }
    float tap (int delay) const noexcept { int r = w - delay; if (r < 0) r += size(); return b[(size_t) r]; }
    void write (float x) noexcept { b[(size_t) w] = x; if (++w >= size()) w = 0; }
};

// The cave: Vortessa's shared reverb topology (buffer sizes are the patch's, at 44.1 kHz;
// lengths scale with the sample rate)
struct Cave
{
    Ring pd, ap1, ap2, ap3, fb;
    float ap1Out = 0, ap2Out = 0, ap3Out = 0, lp = 0, fbSig = 0;
    double scale = 1.0;
    void prepare (double sr);
    void clear();
    // g1..g3: allpass gains; tailDelay / tailFb: the feedback cave; lpHz: in-loop low pass
    float process (float in, double predelaySamples, float g1, float g2, float g3, double tailDelay, float tailFb, float lpCoeff) noexcept;
};

struct Params
{
    // levels (linear)
    float kickLevel = 0.8f, toneLevel = 0.6f, ghostLevel = 0.5f, driftLevel = 0.0f;
    // CAVE KICK
    bool kickOwn = false; float kickRate = 8.0f, kickProb = 0.7f;
    float kickPitch = 60.0f, kickDecay = 1.0f, kickDrive = 0.3f, kickRevmix = 0.5f, kickPredelay = 80.0f;
    // FDBK TONE
    bool toneOwn = false; float toneProb = 0.6f;
    float toneFreq = 800.0f, toneFeedback = 0.7f, toneCollapse = 0.3f, toneMutation = 0.4f, toneCrush = 0.3f;
    // GHOST NET (the patch's eight knobs, already in their ranges)
    float gSens = 0.2f, gFeedback = 0.5f, gPitch = 600.0f, gDetune = 0.3f, gRate = 0.3f, gBright = 0.5f, gSelf = 0.2f, gSat = 0.9f;
    float gInput = 0.5f;          // how much of the other layers excites it
    // DRIFT
    float driftFreq = 55.0f, driftDetune = 0.35f;
};

struct Hit { int offset; int voice; float vel; float gateMs; };   // voice 0 kick, 1 tone, 2 ghost (a strike)

class Engine
{
public:
    void prepare (double sampleRate);
    void reset();
    void seed (uint32_t s) { noise.s = s * 2654435761u + 1u; }
    // excite: the other layers' sound (mono), may be null. Hits sorted by offset.
    void process (float* outL, float* outR, int n, const float* excite, const Params& p, const Hit* hits, int numHits);

    // telemetry
    float kickEnv = 0, toneEnv = 0, ghostEnergy = 0, driftLevel = 0;
    uint32_t kicks = 0, grains = 0;
    std::array<float, 4> ghostNode {};     // the four nodes, for the visuals
    float toneFreqNow = 0;

private:
    double sr = 44100.0;
    Noise noise;

    // ---- kick
    double kPrevPhase = 0, kClk = 0, kEnvPh = 1, kPunchPh = 1, kClickPh = 1, kOsc = 0, kSub = 0;
    float kHp = 0, kVel = 1;
    Cave kCave;

    // ---- tone
    double tPrev = 0, tClk = 0, tGrainPh = 1, tNoisePh = 0;
    float tScatter = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0, r5 = 0, r6 = 0, tSmooth = 0, tVel = 1;
    int64_t toneGateLeft = 0;
    Ring comb1, comb2;
    Cave tCave;

    // ---- ghost
    std::array<float, 8> gk {};
    std::array<double, 4> gLfo {}, gPh {};
    std::array<float, 4> gFb {}, gLpA {}, gLpB {};
    float gOutL = 0, gOutR = 0, gEnv = 0, gPrevIn = 0;
    std::array<Ring, 4> gDel;
    DcBlock gInDc, gOutLDc, gOutRDc;
    std::array<DcBlock, 4> gSelfDc, gTapDc, gResDc, gLoopDc, gNodeDc, gSatDc, gWrDc;
    float gKick = 0;

    // ---- drift
    std::array<double, 5> dLfo {};
    std::array<double, 5> dOsc {};
    double dBeat1 = 0, dBeat2 = 0, dRm = 0, dH4 = 0;
};
} // namespace fdat_vtx
