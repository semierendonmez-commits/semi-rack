// DRONE: two oscillators that never stop, patched into each other, after the complex oscillators
// of the Buchla 259 and the Make Noise DPO (not a model of either):
//   A  a sine core through a wavefolder; B  a sine that leans into a triangle and a square
//   B -> A and A -> B: through-zero phase modulation (both at once: the pair runs from a tone into chaos),
//   A x B: ring; A resets B: sync (soft to hard); then a vactrol low pass gate (BRIGHT)
// The FM indices run up to about 8 radians at full (the designed sounds carry 6 to 20 sidebands), on a curve
// so the low end stays fine. LPG CV opens and closes the gate from the matrix: the slow breathing (2 to 3.5 Hz)
// both long references have on their sustained layer.
// ORDER <-> CHAOS is one macro over the cross-modulation: at 0 two clean tones, up, the indices, the
// fold and the ring all open and the pair turns to noise.
//
// The lanes play it (ACT): DUCK it, strike its LPG, STEP it up the scale, jump it to a RANDom degree,
// PING its filter, hit its FOLD. It runs at twice the host rate (the cross-FM aliases otherwise).
//
// RESONATOR: four strings tuned to the drone's chord (root, and the scale's 3rd, 5th, 7th degrees);
// the drums excite them, so every hit rings in the drone's key (sympathetic strings, as on Rings).
#pragma once

#include "Dsp.h"
#include <array>
#include <atomic>
#include <vector>

namespace flt
{

// B against A. 4:3 and the high ratios came out of the reference recordings: drones a fourth apart, and
// FM A>B with B far above A: a formant of sidebands round B, spaced by A (c:m of 9 to 16 in the designed sounds)
inline constexpr int kDroneRatios = 14;
inline const std::array<const char*, kDroneRatios>& droneRatioNames()
{
    static const std::array<const char*, kDroneRatios> n { "1:4", "1:2", "2:3", "1:1", "4:3", "3:2", "2:1", "3:1", "4:1", "5:1", "7:1", "9:1", "11:1", "16:1" };
    return n;
}
inline float droneRatio (int i)
{
    static const float r[kDroneRatios] { 0.25f, 0.5f, 0.6667f, 1.0f, 1.3333f, 1.5f, 2.0f, 3.0f, 4.0f, 5.0f, 7.0f, 9.0f, 11.0f, 16.0f };
    return r[i < 0 ? 0 : (i >= kDroneRatios ? kDroneRatios - 1 : i)];
}

// what a lane does to the drone when it fires
enum DroneAct { actOff = 0, actDuck, actLpg, actStep, actRand, actPing, actFold };
inline const std::array<const char*, 7>& droneActNames()
{
    static const std::array<const char*, 7> n { "--", "DUCK", "LPG", "STEP", "RAND", "PING", "FOLD" };
    return n;
}

class Drone
{
public:
    struct Settings
    {
        float level = 0.0f;          // 0 = off
        float rootHz = 110.0f;       // the case's key with PITCH and the degree already in
        int ratio = 3;               // B against A
        float fine = 0.5f;           // B detuned up to half a semitone either way (0.5 = none)
        float fmAB = 0.0f, fmBA = 0.0f, ring = 0.0f, sync = 0.0f, fold = 0.0f, shape = 0.0f;
        float bright = 0.6f;         // the LPG's resting opening
        float chaos = 0.0f;
        float duck = 0.6f;           // how far a DUCK pulls it down
        float pitchCv = 0.0f, fmCv = 0.0f, foldCv = 0.0f, lpgCv = 0.0f;   // -1..1 from the matrix sources
        float arc = 0.0f;            // 0..1: how far the ARC opens it (the value comes in arcValue)
        float arcValue = 1.0f;       // 0..1
    };

    void prepare (double sampleRate);
    void reset();
    void set (const Settings& s) { settings = s; }
    // a lane fired: act on it (vel 0..1); glide is how STEP and RAND move the pitch
    void act (int action, float vel, int degreeJump);
    int degree() const { return deg; }
    void setDegree (int d) { deg = d; }
    // renders n samples, added into l / r and written (mono) into mono
    void render (float* l, float* r, float* mono, int n);
    float envelope() const { return follower; }        // its loudness, 0..1 (the DRONE source)

    // for the editor's XY view: A against B (read without a lock)
    static constexpr int kXY = 512;
    std::array<float, kXY> xyA {}, xyB {};
    std::atomic<int> xyWrite { 0 };

private:
    float tick (float fA, float fB, float fmAB, float fmBA, float fold, float ring, float sync, float shape);
    Settings settings;
    double sr = 48000.0;
    double phA = 0.0, phB = 0.0;
    float prevA = 0.0f, prevB = 0.0f, curA = 0.0f, curB = 0.0f;
    float pitchSemis = 0.0f, pitchTarget = 0.0f, glideK = 0.999f;
    int deg = 0;
    float duckEnv = 0.0f, duckK = 0.999f, lpgEnv = 0.0f, lpgK = 0.9999f, pingEnv = 0.0f, pingK = 0.999f, foldEnv = 0.0f, foldK = 0.999f;
    float follower = 0.0f, followK = 0.999f, openS = -1.0f, openK = 0.01f;
    dsp::OnePole lpgL, lpgR, dcL, dcR;
    dsp::Svf pingL, pingR, aaA, aaB;       // the ping; the 2x decimation filters
    dsp::Smooth levelS;
    dsp::Rng rng;
    int xyDecim = 0;
};

class SympatheticStrings
{
public:
    static constexpr int kStrings = 4;
    void prepare (double sampleRate);
    void reset();
    // the chord's frequencies; decay 0..1 (0.2 to 8 s), bright 0..1
    void set (const std::array<float, kStrings>& hz, float decay01, float bright01);
    // the drums in (mono), the rings out (stereo, added)
    void process (const float* in, float* l, float* r, int n);
    std::array<float, kStrings> hzNow {};
private:
    struct Str { std::vector<float> line; int w = 0; float delay = 100.0f, z = 0.0f, gain = 0.99f; };
    std::array<Str, kStrings> strings;
    double sr = 48000.0;
    float damp = 0.5f;
    dsp::OnePole inHp;
};

} // namespace flt
