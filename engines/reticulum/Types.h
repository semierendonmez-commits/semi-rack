// RETICULUM: the engine's view of the patch. Plain C++20, no JUCE.
//
// The VCV patch it comes from:
//   MANUAL GATE (Count Modula)  --gate-->  rete RND trig, RANDOM VALUES trig (x2), MG-16 trig
//   RANDOM VALUES A + B (VCV Fundamental, 7 outs each)  -->  rete node gain CVs
//   MG-16 (TinyTricks)  -->  rete EXCITE (and anything else on the wire matrix)
//   rete (forsitan modulare)  --L/R-->  artifex (forsitan) and/or Beads (Mutable Instruments)  -->  out
#pragma once

#include <array>
#include <cstdint>

namespace rt
{

constexpr int kNodes = 8;
constexpr int kRndOuts = 7;
constexpr int kMg = 16;
constexpr int kTargets = 11;     // N1..N8, LEAK, SCALE, LVL: what the modulators and RANDOMIZE reach
constexpr int kWireRows = kNodes + 1;   // N1..N8, EXCITE

// wire matrix columns (bit index in a row)
constexpr int kColA = 0;                 // A1..A7
constexpr int kColB = kColA + kRndOuts;  // B1..B7
constexpr int kColMg = kColB + kRndOuts; // MG1..MG16
constexpr int kColExt = kColMg + kMg;    // the plug-in's audio input
constexpr int kCols = kColExt + 1;       // 31

// where the gate goes; each destination takes one of Manual Gate's seven outputs
enum Dest { dRete = 0, dRndA, dRndB, dMg, dMod, dArtifex, dSeed, dFreeze, kDests };
// Manual Gate's outputs, in the order of its panel
enum GateOut { gOff = 0, gGate, gInvGate, gExt, gInvExt, gTrig, gLatch, gInvLatch, kGateOuts };

enum ModMode { mOff = 0, mRandom, mWalk, mDrunk, kModModes };
// modulator clock: free (rate knob), the gate, or tempo divisions
enum ModSync { sFree = 0, sGate, s4b, s2b, s1b, s2, s4, s8, s16, s32, s4t, s8t, s16t, kModSyncs };
inline double syncBeats (int s)
{
    switch (s)
    {
        case s4b: return 16.0; case s2b: return 8.0; case s1b: return 4.0; case s2: return 2.0;
        case s4: return 1.0; case s8: return 0.5; case s16: return 0.25; case s32: return 0.125;
        case s4t: return 2.0 / 3.0; case s8t: return 1.0 / 3.0; case s16t: return 1.0 / 6.0;
        default: return 0.0;
    }
}

enum ClockMode { cOff = 0, cFree, cSync, kClockModes };
enum Order { oAxBd = 0, oBdAx, oParallel, kOrders };

struct ModSettings
{
    int mode = mOff;
    int sync = sFree;
    float rate = 1.0f;      // Hz, free
    float depth = 0.5f;     // 0..1: +-half the knob's travel at 1
};

struct Params
{
    // ---- rete (knob values 0..1, as on the panel)
    std::array<float, kNodes> gain {};
    float leak = 0.5f, scale = 0.67f, lvl = 0.0f;
    std::array<ModSettings, kTargets> mod {};
    float modAmount = 1.0f;          // master depth over every modulator

    // ---- the random sources
    int rangeA = 0, rangeB = 0;      // RandomValues range (randomRange())
    float cvDepth = 1.0f;            // node CV attenuator; 1 = a cable (0.2 per volt)
    std::array<uint32_t, kWireRows> wire {};
    float mgRange = 1.0f, mgBias = 0.0f; // MG-16: frequency variance, bias
    bool mgSH = true, mgUni = false;

    // ---- manual gate
    bool pad = false;                // the big button (or anything automating it)
    float length = 0.0f;             // seconds, extended outputs
    int clockMode = cOff;
    float clockHz = 1.0f;
    int clockSync = s4;              // a ModSync division
    float chance = 1.0f;
    float flux = 0.0f;               // free clock: random intervals
    float width = 0.5f;              // the clock's gate width, share of its period
    std::array<int, kDests> dest {};

    // ---- artifex
    bool axOn = true;
    int axMode = 0;
    float axTime = 0.5f, axFbk = 0.0f, axAmt = 0.5f, axFilter = 0.0f, axStereo = 0.0f, axGain = 1.0f, axMix = 1.0f;
    // ---- beads
    bool bdOn = false;
    int bdQuality = 0;
    float bdTime = 0.05f, bdSize = 0.5f, bdShape = 0.5f, bdDensity = 0.5f, bdPitch = 0.0f,
          bdFbk = 0.0f, bdVerb = 0.0f, bdRand = 0.0f, bdMix = 0.5f, bdGain = 1.0f;
    bool bdFreeze = false;

    // ---- out
    int order = oAxBd;
    float level = 0.0f;              // dB
    float width2 = 1.0f;             // stereo width (M/S), 0..2
    int clip = 1;                    // 0 off, 1 soft, 2 hard

    // ---- host
    double bpm = 120.0, ppq = 0.0;
    bool playing = false;
};

// RandomValues' range menu (offset, gain): v = uniform * gain + offset
inline void randomRange (int r, float& gain, float& offset)
{
    static const float g[6] { 10.0f, 10.0f, 5.0f, 20.0f, 2.0f, 1.0f };
    static const float o[6] { 0.0f, -5.0f, 0.0f, -10.0f, -1.0f, 0.0f };
    const int i = r < 0 ? 0 : (r > 5 ? 5 : r);
    gain = g[i];
    offset = o[i];
}

} // namespace rt
