// Vendored unchanged from SEEDWALK Source/engine (JUCE-free already).
// SEEDWALK: how far apart two nets are, and the matrices that walk between them.
//
// HEAT    log10 of the loop gain: how hard a net drives itself round the loop. rho(|M|) (the Perron
//         root of the matrix of absolute weights: the largest gain a signal can find going round)
//         x the drive SCALE gives (10^(3k)) x the mean node gain x what the leaky integrator gives
//         at the DC blocker's corner (|1 / (1 - leak e^-jw)|, w = 2 pi 38 Hz / 48 kHz: about 10
//         with LEAK left, 200 right). Below 0 the net dies away unless excited; around 0 it
//         rings at the edge; far above it is clipped chaos.
// DIST    |heat difference| / 1.5 + mean |gain difference| + |leak difference|
//         + half the relative distance between the two matrices (Frobenius).
// SHAPES  Pinball matrices drawn from those: chains in heat order, slides down or up the heat,
//         nearest or farthest neighbours. Row 0 is S (before step 1), column 0 is "rest".
// MAP     x = heat, y = the nets' first principal coordinate of what is left of the distance once
//         the heat is taken out (classical MDS, one dimension): character.
#pragma once

#include "Modules.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace sw::walk
{
constexpr int kStates = kSteps + 1;
using Pin = std::array<std::array<float, kStates>, kStates>;   // 0..100, [from][to]

inline float meanGain (const Net& n, float open)
{
    float s = 0.0f;
    for (auto g : n.gain) s += std::clamp (g + open, 0.0f, 1.0f);
    return s / (float) kNodes;
}

// Perron root of |M| by power iteration (|M| is positive: it converges)
inline float rhoAbs (const Matrix& m)
{
    std::array<float, kNodes> v, w;
    v.fill (1.0f);
    float lambda = 0.0f;
    for (int it = 0; it < 60; ++it)
    {
        float norm = 0.0f;
        for (int i = 0; i < kNodes; ++i)
        {
            float s = 0.0f;
            for (int j = 0; j < kNodes; ++j) s += std::fabs (m[(size_t) i][(size_t) j]) * v[(size_t) j];
            w[(size_t) i] = s;
            norm = std::max (norm, s);
        }
        if (norm <= 1.0e-12f) return 0.0f;
        lambda = norm;
        for (int i = 0; i < kNodes; ++i) v[(size_t) i] = w[(size_t) i] / norm;
    }
    return lambda;
}

// the leaky integrator's gain at the DC blocker's corner, at the module's 48 kHz
inline float integratorGain (float leakK)
{
    const double leak = 1.0 - std::pow (10.0, -1.0 - 4.0 * std::clamp ((double) leakK, 0.0, 1.0));
    const double w = 2.0 * 3.14159265358979 * 38.0 / 48000.0;
    const double re = 1.0 - leak * std::cos (w), im = leak * std::sin (w);
    return (float) (1.0 / std::sqrt (re * re + im * im));
}

inline float heatOf (const Matrix& m, float scaleK, float meanG, float leakK)
{
    const float drive = std::pow (10.0f, 3.0f * std::clamp (scaleK, 0.0f, 1.0f));
    return std::log10 (std::max (1.0e-6f, rhoAbs (m) * drive * std::max (meanG, 1.0e-4f) * integratorGain (leakK)));
}

// a net at a given heat: SCALE solved for it; when SCALE would have to go below 0, the gains
// come down instead (and when above 1, it stays at 1: the net is as hot as it gets)
inline Net tuneHeat (Net n, float target, float open = 0.0f, float drive = 0.0f)
{
    Matrix m;
    Rete::matrixFor (n.seed, m);
    const float g = meanGain (n, open);
    const float base = std::log10 (std::max (1.0e-6f, rhoAbs (m) * std::max (g, 1.0e-4f) * integratorGain (n.leak)));
    float k = (target - base) / 3.0f - drive;
    if (k < 0.0f)
    {
        const float f = std::pow (10.0f, 3.0f * k);   // what the gains must lose
        for (auto& x : n.gain) x = std::clamp (x * f, 0.0f, 1.0f);
        k = 0.0f;
    }
    n.scale = std::clamp (k, 0.0f, 1.0f);
    return n;
}

struct Profile
{
    Matrix m {};
    float heat = 0.0f, norm = 0.0f;
};

inline Profile profile (const Net& n, float open, float drive)
{
    Profile p;
    Rete::matrixFor (n.seed, p.m);
    p.heat = heatOf (p.m, n.scale + drive, meanGain (n, open), n.leak);
    float s = 0.0f;
    for (auto& row : p.m) for (auto x : row) s += x * x;
    p.norm = std::sqrt (s);
    return p;
}

// the part of the distance that is not heat
inline float character (const Net& a, const Profile& pa, const Net& b, const Profile& pb)
{
    float g = 0.0f;
    for (int k = 0; k < kNodes; ++k) g += std::fabs (a.gain[(size_t) k] - b.gain[(size_t) k]);
    float d2 = 0.0f;
    for (int i = 0; i < kNodes; ++i)
        for (int j = 0; j < kNodes; ++j)
        {
            const float d = pa.m[(size_t) i][(size_t) j] - pb.m[(size_t) i][(size_t) j];
            d2 += d * d;
        }
    const float rel = std::sqrt (d2) / std::max (1.0e-3f, pa.norm + pb.norm);
    return g / (float) kNodes + std::fabs (a.leak - b.leak) + 0.5f * rel;
}

inline float distance (const Net& a, const Profile& pa, const Net& b, const Profile& pb)
{
    return std::fabs (pa.heat - pb.heat) / 1.5f + character (a, pa, b, pb);
}

// everything the shapes and the map need, computed once for the nets
struct Survey
{
    std::array<Profile, kSteps> prof {};
    std::array<std::array<float, kSteps>, kSteps> dist {}, chr {};
    std::array<float, kSteps> mapX {}, mapY {};      // 0..1
    float heatLo = 0.0f, heatHi = 1.0f;

    void run (const std::array<Net, kSteps>& nets, float open, float drive)
    {
        for (int i = 0; i < kSteps; ++i) prof[(size_t) i] = profile (nets[(size_t) i], open, drive);
        for (int i = 0; i < kSteps; ++i)
            for (int j = 0; j < kSteps; ++j)
            {
                chr[(size_t) i][(size_t) j] = i == j ? 0.0f : character (nets[(size_t) i], prof[(size_t) i], nets[(size_t) j], prof[(size_t) j]);
                dist[(size_t) i][(size_t) j] = i == j ? 0.0f : std::fabs (prof[(size_t) i].heat - prof[(size_t) j].heat) / 1.5f + chr[(size_t) i][(size_t) j];
            }
        heatLo = 1.0e9f; heatHi = -1.0e9f;
        for (auto& p : prof) { heatLo = std::min (heatLo, p.heat); heatHi = std::max (heatHi, p.heat); }
        if (heatHi - heatLo < 0.5f) { const float c = 0.5f * (heatLo + heatHi); heatLo = c - 0.25f; heatHi = c + 0.25f; }
        for (int i = 0; i < kSteps; ++i) mapX[(size_t) i] = (prof[(size_t) i].heat - heatLo) / (heatHi - heatLo);
        mds1();
    }

    // classical MDS of the character distance, first coordinate: double-centre D^2, power iteration
    void mds1()
    {
        constexpr int n = kSteps;
        std::array<std::array<double, n>, n> b {};
        std::array<double, n> rowMean {};
        double all = 0.0;
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
            {
                const double d = chr[(size_t) i][(size_t) j];
                b[(size_t) i][(size_t) j] = d * d;
                rowMean[(size_t) i] += d * d / n;
                all += d * d / (n * n);
            }
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                b[(size_t) i][(size_t) j] = -0.5 * (b[(size_t) i][(size_t) j] - rowMean[(size_t) i] - rowMean[(size_t) j] + all);
        std::array<double, n> v {};
        for (int i = 0; i < n; ++i) v[(size_t) i] = 1.0 + 0.1 * i;   // not orthogonal to the top vector
        for (int it = 0; it < 100; ++it)
        {
            std::array<double, n> w {};
            double norm = 0.0;
            for (int i = 0; i < n; ++i)
            {
                for (int j = 0; j < n; ++j) w[(size_t) i] += b[(size_t) i][(size_t) j] * v[(size_t) j];
                norm += w[(size_t) i] * w[(size_t) i];
            }
            norm = std::sqrt (norm);
            if (norm < 1.0e-12) break;
            for (int i = 0; i < n; ++i) v[(size_t) i] = w[(size_t) i] / norm;
        }
        double lo = 1.0e9, hi = -1.0e9;
        for (auto x : v) { lo = std::min (lo, x); hi = std::max (hi, x); }
        for (int i = 0; i < n; ++i) mapY[(size_t) i] = hi - lo > 1.0e-9 ? (float) ((v[(size_t) i] - lo) / (hi - lo)) : 0.5f;
    }

    // the first `count` steps sorted by heat, hottest first
    std::array<int, kSteps> byHeat (int count) const
    {
        std::array<int, kSteps> o {};
        std::iota (o.begin(), o.end(), 0);
        std::stable_sort (o.begin(), o.begin() + count, [this] (int a, int b) { return prof[(size_t) a].heat > prof[(size_t) b].heat; });
        return o;
    }
};

enum Shape
{
    shChain = 0,     // 1 > 2 > .. > N > 1: an ordinary sequencer
    shCoolDown,      // hottest > .. > coolest > hottest
    shWarmUp,        // coolest > .. > hottest > coolest
    shHeatDrunk,     // one step hotter or cooler, half and half; the ends bounce
    shDescent,       // only to cooler nets, the nearer the likelier; the coolest jumps to the hottest
    shAscent,        // only to hotter nets; the hottest drops to the coolest
    shNearest,       // anywhere, the nearer the likelier
    shFarthest,      // anywhere, the farther the likelier
    shDrunk,         // one step left or right on the steps
    shRandom,
    shClear,
    kShapes
};

inline const char* shapeName (int s)
{
    static const char* n[kShapes] { "chain", "cool down", "warm up", "heat drunk", "descent", "ascent",
                                    "nearest", "farthest", "drunk", "random", "clear" };
    return n[std::clamp (s, 0, kShapes - 1)];
}

// count: how many steps the shape uses (the first N); spread: how sharply NEAREST / DESCENT
// prefer the near ones (smaller = sharper)
inline Pin makeShape (int shape, const Survey& sv, int count, float spread, uint32_t seed)
{
    Pin m {};
    const int n = std::clamp (count, 2, kSteps);
    Rng rng (seed);
    auto set = [&] (int from, int to, float w) { m[(size_t) from][(size_t) to + 1] = std::clamp (w, 0.0f, 100.0f); };   // from: state, to: step 0..15
    const float tau = std::max (0.02f, spread);

    if (shape == shClear) return m;
    if (shape == shChain)
    {
        m[0][1] = 100.0f;
        for (int s = 0; s < n; ++s) set (s + 1, (s + 1) % n, 100.0f);
        return m;
    }
    if (shape == shCoolDown || shape == shWarmUp || shape == shHeatDrunk)
    {
        auto o = sv.byHeat (n);
        if (shape == shWarmUp) std::reverse (o.begin(), o.begin() + n);
        m[0][(size_t) o[0] + 1] = 100.0f;
        for (int k = 0; k < n; ++k)
        {
            if (shape == shHeatDrunk)
            {
                if (k > 0) set (o[(size_t) k] + 1, o[(size_t) k - 1], 50.0f);
                if (k < n - 1) set (o[(size_t) k] + 1, o[(size_t) k + 1], 50.0f);
            }
            else set (o[(size_t) k] + 1, o[(size_t) ((k + 1) % n)], 100.0f);
        }
        return m;
    }
    if (shape == shDescent || shape == shAscent)
    {
        auto o = sv.byHeat (n);
        const int top = shape == shDescent ? o[0] : o[(size_t) n - 1];
        const int bottom = shape == shDescent ? o[(size_t) n - 1] : o[0];
        m[0][(size_t) top + 1] = 100.0f;
        for (int i = 0; i < n; ++i)
        {
            if (i == bottom) { set (i + 1, top, 100.0f); continue; }
            float best = 0.0f;
            std::array<float, kSteps> w {};
            for (int j = 0; j < n; ++j)
            {
                if (j == i) continue;
                const float dh = sv.prof[(size_t) j].heat - sv.prof[(size_t) i].heat;
                if (shape == shDescent ? dh >= 0.0f : dh <= 0.0f) continue;
                w[(size_t) j] = std::exp (-sv.dist[(size_t) i][(size_t) j] / tau);
                best = std::max (best, w[(size_t) j]);
            }
            if (best <= 0.0f) { set (i + 1, top, 100.0f); continue; }
            for (int j = 0; j < n; ++j) if (w[(size_t) j] > 0.0f) set (i + 1, j, std::max (1.0f, 100.0f * w[(size_t) j] / best));
        }
        return m;
    }
    if (shape == shNearest || shape == shFarthest)
    {
        m[0][1] = 100.0f;
        float dmax = 1.0e-3f;
        for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) dmax = std::max (dmax, sv.dist[(size_t) i][(size_t) j]);
        for (int i = 0; i < n; ++i)
        {
            std::array<float, kSteps> w {};
            float best = 0.0f;
            for (int j = 0; j < n; ++j)
            {
                if (j == i) continue;
                const float d = sv.dist[(size_t) i][(size_t) j];
                w[(size_t) j] = shape == shNearest ? std::exp (-d / tau) : std::exp (-(dmax - d) / tau);
                best = std::max (best, w[(size_t) j]);
            }
            for (int j = 0; j < n; ++j)
                if (j != i)
                {
                    const float v = 100.0f * w[(size_t) j] / best;
                    if (v >= 1.0f) set (i + 1, j, v);
                }
        }
        return m;
    }
    if (shape == shDrunk)
    {
        m[0][1] = 100.0f;
        for (int s = 0; s < n; ++s)
        {
            set (s + 1, (s + 1) % n, 50.0f);
            set (s + 1, (s + n - 1) % n, 50.0f);
        }
        return m;
    }
    // random
    m[0][1] = 100.0f;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            if (j != i && rng.uniform() < 0.3f) set (i + 1, j, std::round (10.0f + 90.0f * rng.uniform()));
    for (int i = 0; i < n; ++i)
    {
        bool any = false;
        for (int j = 1; j < kStates; ++j) any |= m[(size_t) i + 1][(size_t) j] > 0.0f;
        if (! any) set (i + 1, (i + 1) % n, 100.0f);
    }
    return m;
}

// glide curves, t 0..1
inline float curve (int c, float t)
{
    t = std::clamp (t, 0.0f, 1.0f);
    switch (c)
    {
        case cLinear: return t;
        case cLate: return t * t * t;
        case cEarly: { const float u = 1.0f - t; return 1.0f - u * u * u; }
        default: return t * t * (3.0f - 2.0f * t);
    }
}

} // namespace sw::walk
