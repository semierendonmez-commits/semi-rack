// Vendored unchanged from SEEDWALK Source/engine (JUCE-free already).
// rete - forsitan modulare (GPL-3.0), src/rete.cpp, as RETICULUM runs it, with one change for
// SEEDWALK: the matrix is not only what a seed makes, it can be any matrix (the one between two
// seeds while the net glides). The seeding law, the block order and the laws of the knobs are
// the module's.
#pragma once

#include "Types.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sw
{

constexpr float kPi = 3.14159265358979f;

// xorshift32 / uniform, one per user so nothing shares a stream
struct Rng
{
    uint32_t s = 0x9e3779b9u;
    explicit Rng (uint32_t seed = 0x9e3779b9u) : s (seed ? seed : 1u) {}
    uint32_t u32() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uniform() { return (float) (u32() >> 8) * (1.0f / 16777216.0f); }   // [0, 1)
    float bipolar() { return uniform() * 2.0f - 1.0f; }
};

using Matrix = std::array<std::array<float, kNodes>, kNodes>;

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
        matrixFor (0x5eedf00du, matrix);
        kick (0x5eedf00du, 1.0f);
    }

    static uint32_t nextRand (uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    static float bipolar (uint32_t& s) { return (float) (nextRand (s) >> 8) * (2.0f / 16777216.0f) - 1.0f; }

    // the matrix a seed makes: exactly the module's (and RETICULUM's) draw order
    static void matrixFor (uint32_t seed, Matrix& m)
    {
        uint32_t r = seed ? seed : 1u;
        for (int i = 0; i < kNodes; ++i)
            for (int j = 0; j < kNodes; ++j)
                m[(size_t) i][(size_t) j] = bipolar (r);
    }

    // what the module's RND does to the state after the matrix: small random integrators,
    // filters and feedback cleared. amount 1 = the module's; SEEDWALK's KICK lane scales it.
    void kick (uint32_t s, float amount)
    {
        uint32_t r = s ? s : 1u;
        for (int k = 0; k < kNodes * kNodes; ++k) nextRand (r);   // the integrators come after the matrix in the module's stream
        for (int i = 0; i < kNodes; ++i)
        {
            integ[i] += 0.01f * amount * bipolar (r) * 10.0f;
            if (amount >= 1.0f) { integ[i] = 0.01f * bipolar (r); hpX[i] = hpY[i] = 0.0f; fb[i] = 0.0f; }
        }
    }

    void setSampleRate (float sr) { srRatio = 48000.0f / sr; leakCache = -1.0f; }

    // knobs 0..1 (gain with its CV already summed, clamped here as on the module);
    // excite in volts; returns L/R in volts (about +-5)
    void process (const float* gainIn, float leakK, float scaleK, float lvl, float exciteVolts, float& outL, float& outR)
    {
        leakK = std::clamp (leakK, 0.0f, 1.0f);
        scaleK = std::clamp (scaleK, 0.0f, 1.0f);
        if (leakK != leakCache)
        {
            const float leak48 = 1.0f - std::pow (10.0f, -1.0f - 4.0f * leakK);
            leak = std::pow (leak48, srRatio);
            dcR = std::pow (0.995f, srRatio);
            leakCache = leakK;
        }
        if (scaleK != scaleCache) { scale = std::pow (10.0f, 3.0f * scaleK); scaleCache = scaleK; }

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
            const auto& row = matrix[(size_t) i];
            float m = 0.0f;
            for (int j = 0; j < kNodes; ++j) m += row[(size_t) j] * gi[j];
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

    Matrix matrix {};
    float fb[kNodes] {};

private:
    float integ[kNodes] {}, hpX[kNodes] {}, hpY[kNodes] {};
    float panL[kNodes] {}, panR[kNodes] {};
    uint32_t noiseState = 0x9d2c5680u;
    float srRatio = 1.0f;
    float leakCache = -1.0f, scaleCache = -1.0f, leak = 0.99f, dcR = 0.995f, scale = 1.0f;
};

} // namespace sw
