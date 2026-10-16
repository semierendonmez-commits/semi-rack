// Small DSP building blocks shared by the modules and the effects.
#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace flt::dsp
{

inline constexpr float pi = 3.14159265358979f;
inline constexpr float twoPi = 6.28318530717959f;

inline float clamp (float v, float lo, float hi) { return std::min (hi, std::max (lo, v)); }
inline float lerp (float a, float b, float t) { return a + (b - a) * t; }
inline float expMap (float t, float lo, float hi) { return lo * std::pow (hi / lo, clamp (t, 0.0f, 1.0f)); }
inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }

// soft clip, tanh-like, cheap and smooth
inline float soft (float x)
{
    x = clamp (x, -3.0f, 3.0f);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

// time constant -> per-sample multiplier
inline float decayCoef (float seconds, float sr) { return seconds <= 0.0f ? 0.0f : std::exp (-1.0f / (seconds * sr)); }

struct Rng
{
    uint32_t s = 0x1234567u;
    float next()    // [0, 1)
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float) (s >> 8) * (1.0f / 16777216.0f);
    }
    float bi() { return next() * 2.0f - 1.0f; }
};

// Topology-preserving state-variable filter (Zavalishin).
struct Svf
{
    float ic1 = 0, ic2 = 0, g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
    void set (float hz, float q, float sr)
    {
        g = std::tan (pi * clamp (hz, 10.0f, sr * 0.45f) / sr);
        k = 1.0f / std::max (0.3f, q);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0; }
    // returns low, writes band and high
    float tick (float v0, float& band, float& high)
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        band = v1;
        high = v0 - k * v1 - v2;
        return v2;
    }
    float lp (float x) { float b, h; return tick (x, b, h); }
    float bp (float x) { float b, h; tick (x, b, h); return b; }
    float hp (float x) { float b, h; tick (x, b, h); return h; }
};

struct OnePole
{
    float z = 0, a = 0;
    void set (float hz, float sr) { a = std::exp (-twoPi * clamp (hz, 1.0f, sr * 0.49f) / sr); }
    float lp (float x) { z = x + a * (z - x); return z; }
    float hp (float x) { return x - lp (x); }
};

// RBJ shelving biquad.
struct Shelf
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void set (bool high, float hz, float db, float sr)
    {
        const float A = std::pow (10.0f, db / 40.0f);
        const float w = twoPi * hz / sr, cw = std::cos (w), sw = std::sin (w);
        const float alpha = sw / 2.0f * std::sqrt (2.0f);   // S = 1
        const float sa = 2.0f * std::sqrt (A) * alpha;
        float B0, B1, B2, A0, A1, A2;
        if (! high)
        {
            B0 = A * ((A + 1) - (A - 1) * cw + sa); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - sa);
            A0 = (A + 1) + (A - 1) * cw + sa;       A1 = -2 * ((A - 1) + (A + 1) * cw);    A2 = (A + 1) + (A - 1) * cw - sa;
        }
        else
        {
            B0 = A * ((A + 1) + (A - 1) * cw + sa); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - sa);
            A0 = (A + 1) - (A - 1) * cw + sa;       A1 = 2 * ((A - 1) - (A + 1) * cw);      A2 = (A + 1) - (A - 1) * cw - sa;
        }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
    float tick (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// Linear smoother towards a target, per sample.
struct Smooth
{
    float v = 0, target = 0, coef = 0.999f;
    void setTime (float seconds, float sr) { coef = decayCoef (seconds, sr); }
    float tick() { v = target + coef * (v - target); return v; }
};

} // namespace flt::dsp
