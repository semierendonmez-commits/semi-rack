// Vendored unchanged from SEEDWALK Source/engine (JUCE-free already).
// SEEDWALK: the engine's view of the instrument. Plain C++20, no JUCE.
//
//   Pinball 2 (nouserid) -- 16 steps, a 17 x 17 Markov matrix, 8 trig lanes
//        |  the ball fires a step
//        v
//   16 pinned nets (a rete seed = its 8 x 8 matrix, eight node gains, leak, scale, lvl)
//        |  the net glides from where it is to the step's net: the matrix interpolated
//        v  entry by entry, the integrators never reset
//   rete (forsitan modulare), excited by the lanes  --L/R-->  artifex and/or Beads  -->  out
#pragma once

#include <array>
#include <cstdint>

namespace sw
{

constexpr int kNodes = 8;
constexpr int kSteps = 16;
constexpr int kLanes = 8;

// one pinned net: what a step holds
struct Net
{
    uint32_t seed = 1;
    std::array<float, kNodes> gain {};   // knob values 0..1
    float leak = 0.5f, scale = 0.6f, lvl = 0.5f;
};

// what a lane does inside when the ball fires it (besides its MIDI note)
enum LaneDest
{
    lMidi = 0,     // the note only
    lExcite,       // a burst into rete's excite: velocity, decaying over the step's DURATION
    lKick,         // a jolt to the integrators (what rete's RND does to them, without a new matrix)
    lHold,         // the glide stops where it is for DURATION
    lDip,          // SCALE falls for DURATION: the net goes quiet and comes back
    lSnap,         // this glide is instant: the net is there at once
    lBeadsSeed,    // Beads: a seed
    lBeadsFreeze,  // Beads: freeze for DURATION
    lArtifex,      // artifex: its trig
    kLaneDests
};

enum Curve { cLinear = 0, cSmooth, cLate, cEarly, kCurves };
enum Run { rHost = 0, rAlways, kRuns };
enum Order { oAxBd = 0, oBdAx, oParallel, kOrders };

struct Lane
{
    int note = 36;
    int dest = lMidi;
    bool choke = false;     // Pinball's per-lane Mute: another lane / a mute step cuts this one
};

struct Params
{
    // ---- the walk
    int run = rAlways;
    double timeMult = 4.0;           // one step = 1/16 x this
    double patternBars = 0.0;        // relaunch every N bars, 0 = never
    int startStep = 1;
    // ---- the glide
    float glide = 0.75f;             // share of the step's time the net takes to arrive
    int curve = cSmooth;
    float dip = 0.0f;                // SCALE falls by this much in the middle of a glide (knob units)
    bool keepNorm = true;            // keep the matrix's size through the glide (a lerp of two random matrices shrinks in the middle)
    // ---- the net, everywhere
    float open = 0.0f;               // added to every node gain (RETICULUM's banks opened the nodes)
    float drive = 0.0f;              // added to every net's SCALE (knob units, -0.5..0.5)
    float excite = 0.5f;             // lane bursts into excite
    float ext = 0.0f;                // the audio input into excite
    // ---- artifex
    bool axOn = true;
    int axMode = 0;
    float axTime = 0.5f, axFbk = 0.0f, axAmt = 0.5f, axFilter = 0.0f, axStereo = 0.0f, axGain = 1.0f, axMix = 0.35f;
    // ---- beads
    bool bdOn = false;
    int bdQuality = 0;
    float bdTime = 0.05f, bdSize = 0.5f, bdShape = 0.5f, bdDensity = 0.5f, bdPitch = 0.0f,
          bdFbk = 0.0f, bdVerb = 0.0f, bdRand = 0.0f, bdMix = 0.5f, bdGain = 1.0f;
    bool bdFreeze = false;
    // ---- out
    int order = oAxBd;
    float level = 0.0f;              // dB
    float width2 = 1.0f;
    int clip = 1;                    // 0 off, 1 soft, 2 hard
    // ---- host
    double bpm = 120.0, ppq = 0.0;
    bool playing = false;
};

} // namespace sw
