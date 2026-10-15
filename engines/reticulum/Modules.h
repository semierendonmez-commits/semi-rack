// The five modules of the patch, ported from their VCV sources with the Rack layer taken off.
// Each keeps its own sources' behaviour; what changed is noted where it changed.
#pragma once

#include "Types.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rt
{

constexpr float kPi = 3.14159265358979f;

// xorshift32 / uniform / normal, one per module so nothing shares a stream
struct Rng
{
    uint32_t s = 0x9e3779b9u;
    explicit Rng (uint32_t seed = 0x9e3779b9u) : s (seed ? seed : 1u) {}
    uint32_t u32() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uniform() { return (float) (u32() >> 8) * (1.0f / 16777216.0f); }   // [0, 1)
    float bipolar() { return uniform() * 2.0f - 1.0f; }
    float normal()
    {
        const float u1 = std::max (1.0e-7f, uniform()), u2 = uniform();
        return std::sqrt (-2.0f * std::log (u1)) * std::cos (2.0f * kPi * u2);
    }
};

//==============================================================================
// rete - forsitan modulare (GPL-3.0), src/rete.cpp. A feedback integrator network after
// Nathan Ho: eight leaky integrators into a fixed random 8x8 matrix, DC-blocking highpass,
// hard clip, fed back with one sample of delay. Same block order, same laws, same seeding.
class Rete
{
public:
    Rete()
    {
        for (int i = 0; i < kNodes; ++i)
        {
            const float p = (float) i / (kNodes - 1);
            panR[i] = std::sqrt (p);
            panL[i] = std::sqrt (1.0f - p);
        }
        reseed (0x5eedf00du);
    }

    static uint32_t nextRand (uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    static float bipolar (uint32_t& s) { return (float) (nextRand (s) >> 8) * (2.0f / 16777216.0f) - 1.0f; }

    // the matrix a seed makes (the editor draws it from the seed alone)
    static void matrixFor (uint32_t seed, float m[kNodes][kNodes])
    {
        uint32_t r = seed ? seed : 1u;
        for (int i = 0; i < kNodes; ++i)
            for (int j = 0; j < kNodes; ++j)
                m[i][j] = bipolar (r);
    }

    void reseed (uint32_t s)
    {
        seed = s ? s : 0x1u;
        uint32_t r = seed;
        for (int i = 0; i < kNodes; ++i)
            for (int j = 0; j < kNodes; ++j)
                matrix[i][j] = bipolar (r);
        for (int i = 0; i < kNodes; ++i)
        {
            integ[i] = 0.01f * bipolar (r);
            hpX[i] = hpY[i] = 0.0f;
            fb[i] = 0.0f;
        }
    }

    void setSampleRate (float sr) { srRatio = 48000.0f / sr; }

    // knobs 0..1 with CV already summed (gain = knob + 0.2 * volts, clamped here as on the module)
    // excite in volts; returns L/R in volts (about +-5)
    void process (const float* gainIn, float leakK, float scaleK, float lvl, float exciteVolts, float& outL, float& outR)
    {
        const float leak48 = 1.0f - std::pow (10.0f, -1.0f - 4.0f * std::clamp (leakK, 0.0f, 1.0f));
        const float leak = std::pow (leak48, srRatio);
        const float dcR = std::pow (0.995f, srRatio);
        const float scale = std::pow (10.0f, 3.0f * std::clamp (scaleK, 0.0f, 1.0f));

        float gi[kNodes];
        const float excite = exciteVolts * 0.1f * std::clamp (lvl, 0.0f, 1.0f);
        const float life = 1.0e-5f * bipolar (noiseState);
        for (int i = 0; i < kNodes; ++i)
        {
            const float in = fb[i] + ((i & 1) ? -excite : excite) + life;
            integ[i] = leak * integ[i] + in;
            gi[i] = std::clamp (gainIn[i], 0.0f, 1.0f) * scale * integ[i];
        }

        float l = 0.0f, r = 0.0f;
        for (int i = 0; i < kNodes; ++i)
        {
            const float* row = matrix[i];
            float m = 0.0f;
            for (int j = 0; j < kNodes; ++j) m += row[j] * gi[j];
            float y = m - hpX[i] + dcR * hpY[i];
            hpX[i] = m;
            hpY[i] = y;
            if (! std::isfinite (y)) { y = 0.0f; hpX[i] = hpY[i] = 0.0f; integ[i] = 0.0f; }
            const float c = std::clamp (y, -1.0f, 1.0f);
            fb[i] = c;
            l += c * panL[i];
            r += c * panR[i];
        }
        const float comp = 5.0f / (kNodes / 2.0f);
        outL = l * comp;
        outR = r * comp;
    }

    uint32_t seed = 1;
    float matrix[kNodes][kNodes] {};
    float fb[kNodes] {};

private:
    float integ[kNodes] {}, hpX[kNodes] {}, hpY[kNodes] {};
    float panL[kNodes] {}, panR[kNodes] {};
    uint32_t noiseState = 0x9d2c5680u;
    float srRatio = 1.0f;
};

//==============================================================================
// RANDOM VALUES - VCV Fundamental (GPL-3.0), src/RandomValues.cpp: seven new values on every
// trigger (or the PUSH button), uniform * gain + offset, gain/offset from its range menu.
struct RandomValues
{
    explicit RandomValues (uint32_t seed) : rng (seed) { roll (0); }
    void roll (int range)
    {
        float g, o;
        randomRange (range, g, o);
        for (auto& v : values) v = rng.uniform() * g + o;
        ++count;
    }
    // a range change re-reads the same draws in the new range, so switching it is not a trigger
    void rerange (int from, int to)
    {
        float g0, o0, g1, o1;
        randomRange (from, g0, o0);
        randomRange (to, g1, o1);
        for (auto& v : values) v = (v - o0) / g0 * g1 + o1;
    }
    std::array<float, kRndOuts> values {};
    uint32_t count = 0;
    Rng rng;
};

//==============================================================================
// MODULATION GENERATOR (MG-16) - TinyTricks (GPL-3.0), src/modulation-generator.cpp and
// src/oscillators/lfo.cpp. On a trigger each of sixteen outputs becomes, at random, either a
// held value or an LFO of random wave (sin/tri/saw/sqr crossfade) and random pitch
// (2^-8 .. 2^8 Hz around the BIAS, spread by RANGE). Bipolar +-5 V, +5 V offset when UNIPOLAR.
struct MgSource
{
    float phase = 0.0f, freq = 1.0f, lfoWave = 0.0f, holdValue = 0.0f, value = 0.0f;
    bool holding = false;

    void regenerate (float range, float bias, bool shOn, Rng& rng)
    {
        holding = shOn && rng.uniform() >= 0.5f;
        if (holding)
        {
            const float spread = 5.0f * range;
            const float b = bias * 5.0f;
            const float lo = std::max (-5.0f, b - spread), hi = std::min (5.0f, b + spread);
            holdValue = lo + rng.uniform() * (hi - lo);
            value = holdValue;
        }
        else
        {
            lfoWave = rng.uniform() * 3.0f;
            const float spread = 8.0f * range;
            const float b = bias * 8.0f;
            const float lo = std::max (-8.0f, b - spread), hi = std::min (8.0f, b + spread);
            const float pitch = std::min (8.0f, lo + rng.uniform() * (hi - lo));
            freq = std::pow (2.0f, pitch);
            // the source sets a random normal phase and then resets it on the same call (its reset
            // trigger is a Schmitt fed 1.0): every regenerated LFO starts at phase 0
            phase = 0.0f;
        }
    }

    float tick (float dt)
    {
        if (holding) { value = holdValue; return value; }
        phase += std::min (freq * dt, 0.5f);
        if (phase >= 1.0f) phase -= 1.0f;
        const float s = std::sin (2.0f * kPi * phase);
        const float tr = -1.0f + 4.0f * std::fabs ((phase - 0.75f) - std::round (phase - 0.75f));
        const float sw = 2.0f * (phase - std::round (phase));
        const float sq = phase < 0.5f ? 1.0f : -1.0f;
        float v = 0.0f;
        v += s * std::max (0.0f, 1.0f - std::fabs (lfoWave - 0.0f));
        v += tr * std::max (0.0f, 1.0f - std::fabs (lfoWave - 1.0f));
        v += sw * std::max (0.0f, 1.0f - std::fabs (lfoWave - 2.0f));
        v += sq * std::max (0.0f, 1.0f - std::fabs (lfoWave - 3.0f));
        value = 5.0f * v;
        return value;
    }
};

struct Mg16
{
    explicit Mg16 (uint32_t seed) : rng (seed) { regenerate (1.0f, 0.0f, true); }
    void regenerate (float range, float bias, bool sh)
    {
        for (auto& s : src) s.regenerate (range, bias, sh, rng);
        ++count;
    }
    void tick (float dt, bool uni)
    {
        for (int i = 0; i < kMg; ++i) out[(size_t) i] = src[(size_t) i].tick (dt) + (uni ? 5.0f : 0.0f);
    }
    std::array<MgSource, kMg> src {};
    std::array<float, kMg> out {};
    uint32_t count = 0;
    Rng rng;
};

//==============================================================================
// MANUAL GATE - Count Modula (GPL-3.0), src/modules/ManualGate.cpp: one button, seven outputs.
// GATE / INV GATE follow the button; EXTENDED holds on for LENGTH seconds after release (the
// timer restarts while the button is held); TRIGGER is 1 ms on press; LATCH toggles on press.
struct ManualGate
{
    bool high = false, latch = false;
    float trigLeft = 0.0f, extLeft = 0.0f;

    // in: the button (or whatever presses it); returns the seven outputs as bits (GateOut - 1)
    uint8_t process (bool in, float lengthSeconds, float dt)
    {
        if (in && ! high)
        {
            latch = ! latch;
            trigLeft = 1.0e-3f;
        }
        else if (trigLeft > 0.0f)
            trigLeft -= dt;
        high = in;
        if (high) extLeft = lengthSeconds;            // pmGate.restart() while held
        else if (extLeft > 0.0f) extLeft -= dt;
        const bool ext = high || extLeft > 0.0f;
        const bool trig = trigLeft > 0.0f;
        uint8_t b = 0;
        if (high) b |= 1 << (gGate - 1);
        if (! high) b |= 1 << (gInvGate - 1);
        if (ext) b |= 1 << (gExt - 1);
        if (! ext) b |= 1 << (gInvExt - 1);
        if (trig) b |= 1 << (gTrig - 1);
        if (latch) b |= 1 << (gLatch - 1);
        if (! latch) b |= 1 << (gInvLatch - 1);
        return b;
    }
    static bool output (uint8_t bits, int which) { return which > 0 && (bits & (1 << (which - 1))) != 0; }
};

//==============================================================================
// A modulator for one rete control: RANDOM (sample & hold), WALK (smooth random: a cosine glide
// to each new value) or DRUNK (a random walk: small steps, reflected at the ends, glided).
// Bipolar -1..1. Ticks come from its own rate, the tempo, or the gate.
struct Modulator
{
    float value = 0.0f, from = 0.0f, to = 0.0f, glide = 1.0f, glideStep = 0.0f, phase = 0.0f;
    double lastBeatIndex = -1.0;
    int lastMode = -1;
    Rng rng;
    explicit Modulator (uint32_t seed = 1) : rng (seed) {}

    void tickNew (int mode, float periodSeconds, float dt)
    {
        from = value;
        if (mode == mRandom) { to = rng.bipolar(); value = to; glide = 1.0f; return; }
        if (mode == mWalk) to = rng.bipolar();
        else
        {
            float t = to + rng.bipolar() * 0.35f;
            if (t > 1.0f) t = 2.0f - t;
            if (t < -1.0f) t = -2.0f - t;
            to = t;
        }
        glide = 0.0f;
        glideStep = dt / std::max (periodSeconds, 1.0e-3f);
    }

    // tick: this sample is a clock edge for it (gate / tempo); free mode keeps its own phase
    float process (const ModSettings& m, bool tick, float periodSeconds, float dt)
    {
        if (m.mode != lastMode)
        {
            if (m.mode == mOff) value = from = to = 0.0f;
            lastMode = m.mode;
        }
        if (m.mode == mOff) return 0.0f;
        if (m.sync == sFree)
        {
            phase += m.rate * dt;
            if (phase >= 1.0f) { phase -= std::floor (phase); tickNew (m.mode, 1.0f / std::max (0.001f, m.rate), dt); }
        }
        else if (tick)
            tickNew (m.mode, periodSeconds, dt);

        if (glide < 1.0f)
        {
            glide = std::min (1.0f, glide + glideStep);
            const float k = m.mode == mWalk ? 0.5f - 0.5f * std::cos (kPi * glide) : glide;
            value = from + (to - from) * k;
        }
        return value;
    }
};

} // namespace rt
