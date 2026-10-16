// SEMI Family port: vendored from semierendonmez-commits/snarestack Source/engine (0fa7677). Only change:
// namespace gtbs -> ss::gtbs (the break-slicer engines in the binary use gtbs too).
// SNARESTACK: four snare layers made into one hit.
//
//   1. each layer: tuned (resampled), high / low cut, carved, its decay shaped
//   2. aligned: every attack to one point (ONSET), then each layer slid by up to
//      +-1.5 ms, sub-sample, and its polarity chosen, for the most correlation
//      with the rest in the body band (PHASE): where the cancelling happens
//   3. mixed, measured (how much the layers cancel or add, per band, before and
//      after the alignment; which layer owns which frequencies)
//   4. split into CRACK (the highs of the first milliseconds), BODY (the lows of
//      the first ~100 ms) and TAIL (the rest). The three parts add up to the
//      stack exactly, so the balance pad only ever re-weights the real thing.
//
// Plain C++20, no JUCE, built off the audio thread and published whole.
#pragma once

#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace ss
{

constexpr int kLayers = 4;
constexpr int kBands = 6;          // phase meter
constexpr int kSpecBins = 64;      // spectra, 30 Hz .. 18 kHz, log spaced
constexpr double kRate = 48000.0;  // the stack's own clock
constexpr int kViewLen = 1536;     // alignment view: 32 ms at 48 kHz

inline float specHz (int k) { return 30.0f * std::pow (600.0f, (float) k / (float) (kSpecBins - 1)); }
inline const char* bandName (int b) { static const char* n[] = { "SUB", "LOW", "BODY", "MID", "CRACK", "AIR" }; return n[b % kBands]; }
inline float bandLo (int b) { static const float f[] = { 20, 100, 200, 500, 1500, 5000 }; return f[b % kBands]; }
inline float bandHi (int b) { static const float f[] = { 100, 200, 500, 1500, 5000, 20000 }; return f[b % kBands]; }

enum Align { AlignOff = 0, AlignOnset, AlignPhase, NumAlign };
enum Polarity { PolAuto = 0, PolNormal, PolInvert, NumPol };
enum Comp { Crack = 0, Body, Tail, NumComps };

//==============================================================================
// A one-shot as loaded (any rate, mono or stereo)
struct Source
{
    std::string name;
    double rate = kRate;
    std::vector<float> l, r;
    int frames() const { return (int) l.size(); }
};

struct LayerParams
{
    bool on = true;
    float gainDb = 0.0f;
    float tune = 0.0f;         // semitones
    float decay = 1.0f;        // 0 short .. 1 untouched
    float hp = 20.0f;          // Hz
    float lp = 20000.0f;       // Hz
    float pan = 0.0f;          // -1 .. 1
    int polarity = PolAuto;
    float nudgeMs = 0.0f;      // after the alignment, by hand
};

struct StackParams
{
    std::array<LayerParams, kLayers> layer {};
    int align = AlignPhase;
    int ref = -1;              // the layer the others align to; -1: the one with the most body
    float carve = 0.0f;        // 0..1
    float xfreq = 2500.0f;     // crack / body split, Hz
    float crackMs = 12.0f;
    float bodyMs = 90.0f;
    bool normalize = true;     // the stack's peak at -6 dBFS, whatever the layers add up to
};

struct LayerInfo
{
    bool active = false;
    int onset = 0;                       // attack in the layer's own (resampled) frames
    float shiftMs = 0.0f;                // where the aligner moved it, against the raw stack
    float fineMs = 0.0f;                 // of which the phase step (sub-sample)
    bool inverted = false;
    float corrRaw = 0.0f, corr = 0.0f;   // body-band correlation with the rest, before / after
    float peak = 0.0f;
    float bodyShare = 0.0f;
    std::array<float, kSpecBins> spec {};    // dB, the first 150 ms
    std::array<float, kSpecBins> carveDb {}; // what CARVE took out (<= 0)
};

struct Stack
{
    double rate = kRate;
    int frames = 0;
    int onset = 0;                                   // where the hit starts
    std::vector<float> mixL, mixR;                   // the whole stack
    std::array<std::vector<float>, NumComps> cL, cR; // crack, body, tail (they sum to mix)
    std::array<LayerInfo, kLayers> info {};
    std::array<std::vector<float>, kLayers> viewRaw, viewAligned;   // mono, kViewLen, from 2 ms before the attack
    std::array<float, kBands> cancelRaw {}, cancelAligned {};       // dB: energy of the sum against the sum of energies
    std::array<int, kSpecBins> owner {};             // the loudest layer per bin, -1 none
    std::array<float, NumComps> share {};            // energy of crack / body / tail
    float normDb = 0.0f;                             // what NORMALIZE applied
    int ref = 0;
    int serial = 0;
};

std::unique_ptr<Stack> build (const std::array<std::shared_ptr<const Source>, kLayers>& src, const StackParams& p);

/** gain (dB) of crack / body / tail for a point of the balance triangle and a depth */
std::array<float, NumComps> balanceGains (float x, float y, float depthDb);
/** barycentric weights of a pad point (x, y in 0..1; crack top centre, body bottom left, tail bottom right) */
std::array<float, NumComps> barycentric (float x, float y);
/** the point clamped into the triangle */
void clampToTriangle (float& x, float& y);

/** where a hit is heard to start (20 % of the way from the quiet before it to its peak) */
int attackOf (const std::vector<float>& mono, double rate);

/** the stack as one stereo hit with a balance, for export (no pitch, no velocity) */
void renderHit (const Stack& s, const std::array<float, NumComps>& gainsDb, std::vector<float>& L, std::vector<float>& R);

} // namespace ss
