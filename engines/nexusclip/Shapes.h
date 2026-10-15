// NEXUSCLIP CTZ - clip transfer curves and the 4-band crossover.
//
// Every curve works on u = x / threshold and is bounded: |f(u)| <= 1, f(u) ~= u
// for small u (except where noted), so the clip level is exact in every shape.
//   HARD     0 % softness: a straight cut (drums: punch and 'spit')
//   SMOOTH   variable soft knee (SOFTNESS 0..100 %): quadratic knee that
//            starts at (1 - s) and lands on the threshold at (1 + s)
//   CRISP    fixed short knee from -2.5 dB that meets the threshold still
//            rising, then a hard cut: soft colour, hard ceiling
//   CLASSIC  tanh over the whole range (soft clip 'classic': everything bends)
//   PRO      knee at SOFTNESS -> 100 / 75 / 50 / 25 % of the threshold, then
//            the dynamic range above it halved (2:1 in dB) up to the threshold
//   TUBE     asymmetric (even harmonics), DC-blocked
//   TAPE     arctan: softer shoulder than tanh, never quite reaches the top
#pragma once
#include "Dsp.h"

namespace nc
{
enum Shape { shapeHard = 0, shapeSmooth, shapeCrisp, shapeClassic, shapePro, shapeTube, shapeTape, numShapes };
inline const char* shapeName (int s)
{
    static const char* n[] = { "HARD", "SMOOTH", "CRISP", "CLASSIC", "PRO", "TUBE", "TAPE" };
    return n[std::min (std::max (s, 0), numShapes - 1)];
}

struct ShapeFn
{
    int shape = shapeHard;
    float soft = 0.0f;          // 0..1
    // derived
    float k = 1.0f, proK = 1.0f;

    void set (int s, float softness)
    {
        shape = s;
        soft = clampf (softness, 0.0f, 1.0f);
        k = 1.0f - soft;
        proK = 1.0f - 0.75f * soft;    // 100 % .. 25 % of the threshold
    }

    float operator() (float u) const
    {
        const float a = std::abs (u), sg = u < 0.0f ? -1.0f : 1.0f;
        switch (shape)
        {
            case shapeSmooth:
            {
                if (a <= k) return u;
                if (a >= 2.0f - k) return sg;
                const float d = a - k;
                return sg * (a - d * d / (4.0f * (1.0f - k)));
            }
            case shapeCrisp:
            {
                static const float kk = 0.75f;   // -2.5 dB
                if (a <= kk) return u;
                if (a >= 1.0f) return sg;
                // cubic Hermite: (kk, kk, slope 1) -> (1, 1, slope 0.5)
                const float h = 1.0f - kk, t = (a - kk) / h, t2 = t * t, t3 = t2 * t;
                const float y = (2 * t3 - 3 * t2 + 1) * kk + (t3 - 2 * t2 + t) * h + (-2 * t3 + 3 * t2) * 1.0f + (t3 - t2) * 0.5f * h;
                return sg * std::min (1.0f, y);
            }
            case shapeClassic: return std::tanh (u);
            case shapePro:
            {
                if (a <= proK) return u;
                if (proK >= 0.999f) return sg;
                const float y = proK * std::sqrt (a / proK);   // 2:1 above the knee (in dB)
                return sg * std::min (1.0f, y);
            }
            case shapeTube:
            {
                // tanh(c u + b) - tanh(b), scaled so the negative side reaches -1 and slope(0) == 1
                static const float b = 0.3f, tb = std::tanh (b), sc = 1.0f / (1.0f + tb), c = 1.0f / ((1.0f - tb * tb) * sc);
                const float y = (std::tanh (c * u + b) - tb) * sc;
                return clampf (y, -1.0f, 1.0f);
            }
            case shapeTape: return 0.63661977f * std::atan (1.5707963f * u);
            default: return clampf (u, -1.0f, 1.0f);
        }
    }
};

//==============================================================================
// Linkwitz-Riley 4th order from two Butterworth TPT state-variable sections.
struct Svf
{
    float g = 0, k = 1.41421356f, a1 = 0, a2 = 0, a3 = 0;
    float ic1 = 0, ic2 = 0;
    void set (double sr, float f)
    {
        g = (float) std::tan (3.14159265358979 * std::min ((double) f, sr * 0.45) / sr);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0; }
    void tick (float x, float& lp, float& hp)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        lp = v2; hp = x - k * v1 - v2;
    }
};

struct LR4
{
    Svf lp1, lp2, hp1, hp2;
    void set (double sr, float f) { for (auto* s : { &lp1, &lp2, &hp1, &hp2 }) s->set (sr, f); }
    void reset() { for (auto* s : { &lp1, &lp2, &hp1, &hp2 }) s->reset(); }
    void split (float x, float& lo, float& hi)
    {
        float l, h, d;
        lp1.tick (x, l, d); lp2.tick (l, lo, d);
        hp1.tick (x, d, h); hp2.tick (h, d, hi);
    }
};

// 4 bands with allpass compensation: the unprocessed sum is an allpass of the
// input (flat magnitude), tested in EngineTest.
struct Crossover4
{
    LR4 mid, low, high, apLow, apHigh;   // apLow aligns the low pair to f3, apHigh the high pair to f1
    void set (double sr, float f1, float f2, float f3)
    {
        low.set (sr, f1); mid.set (sr, f2); high.set (sr, f3);
        apLow.set (sr, f3); apHigh.set (sr, f1);
    }
    void reset() { mid.reset(); low.reset(); high.reset(); apLow.reset(); apHigh.reset(); }
    void process (float x, float* b)
    {
        float L, H, a, c;
        mid.split (x, L, H);
        apLow.split (L, a, c); L = a + c;
        apHigh.split (H, a, c); H = a + c;
        low.split (L, b[0], b[1]);
        high.split (H, b[2], b[3]);
    }
};
} // namespace nc
