// The rack: every module in processing order, each with its own ON, MIX and
// SOLO, crossfaded (20 ms slew) so switching never clicks. The focused module
// is tapped before and after for the illusion screen.
#pragma once
#include "Module.h"

namespace il
{
struct TapRing
{
    static constexpr int kLen = 4096;
    std::array<float, kLen> pre {}, postL {}, postR {};
    std::atomic<int> write { 0 };
    void push (float a, float l, float r)
    {
        int w = write.load (std::memory_order_relaxed);
        pre[(size_t) w] = a; postL[(size_t) w] = l; postR[(size_t) w] = r;
        write.store ((w + 1) & (kLen - 1), std::memory_order_release);
    }
};

class Chain
{
public:
    Chain()
    {
        for (int i = 0; i < numModules(); ++i) mods.push_back (createModule (i));
        slots.resize (mods.size());
        activity = std::vector<std::atomic<float>> (mods.size());
    }
    int size() const { return (int) mods.size(); }
    Module& mod (int i) { return *mods[(size_t) i]; }
    const Module& mod (int i) const { return *mods[(size_t) i]; }

    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        slew = (float) (1.0 / (0.02 * sr));
        for (size_t i = 0; i < mods.size(); ++i)
        {
            mods[i]->prepare (sr, maxBlock);
            auto& s = slots[i];
            s.dryL.assign ((size_t) maxBlock, 0.0f); s.dryR.assign ((size_t) maxBlock, 0.0f);
            const int lat = mods[i]->latency();
            if (lat > 0) { s.dl.prepare (lat + 8); s.dr.prepare (lat + 8); }
            s.mixCur = 0; s.running = false;
        }
    }
    void reset() { for (size_t i = 0; i < mods.size(); ++i) { mods[i]->reset(); slots[i].mixCur = 0; slots[i].running = false; slots[i].dl.reset(); slots[i].dr.reset(); } }

    // per-block controls (host thread writes them before process)
    struct Control { bool on = false; float mix = 1.0f; };
    std::vector<Control> ctl = std::vector<Control> ((size_t) numModules());
    int solo = -1;
    std::atomic<int> focus { 0 };
    TapRing tap;
    std::vector<std::atomic<float>> activity;

    bool isRunning (int i) const { return slots[(size_t) i].running; }

    int latency() const
    {
        int l = 0;
        for (size_t i = 0; i < mods.size(); ++i)
            if (ctl[i].on && (solo < 0 || solo == (int) i)) l += mods[i]->latency();
        return l;
    }

    void process (float* L, float* R, int n, const Ctx& ctx)
    {
        const int f = focus.load (std::memory_order_relaxed);
        for (size_t i = 0; i < mods.size(); ++i)
        {
            auto& s = slots[i];
            auto& m = *mods[i];
            const bool want = ctl[i].on && (solo < 0 || solo == (int) i);
            const float target = want ? clampf (ctl[i].mix, 0.0f, 1.0f) : 0.0f;
            const bool focused = (int) i == f;
            if (! want && s.mixCur <= 0.0f)
            {
                if (s.running) { m.reset(); s.running = false; s.dl.reset(); s.dr.reset(); }
                activity[i].store (activity[i].load (std::memory_order_relaxed) * 0.8f, std::memory_order_relaxed);
                if (focused) for (int k = 0; k < n; ++k) tap.push (0.5f * (L[k] + R[k]), L[k], R[k]);
                continue;
            }
            if (! s.running) { m.reset(); s.running = true; }
            std::copy (L, L + n, s.dryL.begin());
            std::copy (R, R + n, s.dryR.begin());
            m.process (L, R, n, ctx);
            const int lat = m.latency();
            if (lat > 0)
                for (int k = 0; k < n; ++k) { s.dl.push (s.dryL[(size_t) k]); s.dr.push (s.dryR[(size_t) k]); s.dryL[(size_t) k] = s.dl.at (lat); s.dryR[(size_t) k] = s.dr.at (lat); }
            double diff = 0;
            for (int k = 0; k < n; ++k)
            {
                if (s.mixCur < target) s.mixCur = std::min (target, s.mixCur + slew);
                else if (s.mixCur > target) s.mixCur = std::max (target, s.mixCur - slew);
                const float dl = s.dryL[(size_t) k], dr = s.dryR[(size_t) k];
                float ol = dl + (L[k] - dl) * s.mixCur, orr = dr + (R[k] - dr) * s.mixCur;
                if (! std::isfinite (ol) || ! std::isfinite (orr)) { ol = dl; orr = dr; s.bad = true; }
                L[k] = ol; R[k] = orr;
                diff += (double) sqr (ol - dl) + (double) sqr (orr - dr);
                if (focused) tap.push (0.5f * (dl + dr), ol, orr);
            }
            if (s.bad) { m.reset(); s.bad = false; }
            const float a = clampf ((float) std::sqrt (diff / (2.0 * std::max (1, n))) * 7.0f, 0.0f, 1.0f);
            const float prev = activity[i].load (std::memory_order_relaxed);
            activity[i].store (std::max (a, prev * 0.85f), std::memory_order_relaxed);
        }
    }

private:
    struct Slot { std::vector<float> dryL, dryR; DelayLine dl, dr; float mixCur = 0; bool running = false, bad = false; };
    std::vector<ModulePtr> mods;
    std::vector<Slot> slots;
    double sr = 48000.0;
    float slew = 0.001f;
};

//==============================================================================
// Song-locked LFOs (shared with the sibling plug-ins' LFO tiles)
struct Lfo
{
    static const char* shapes() { return "SINE|TRI|SAW UP|SAW DOWN|SQUARE|S&H|SMOOTH"; }
    static const char* rates() { return "1/16|1/8|1/4|1/2|1 BAR|2 BARS|4 BARS|8 BARS|16 BARS|0.1 Hz|0.25 Hz|0.5 Hz|1 Hz|2 Hz|5 Hz"; }
    static bool synced (int r) { return r < 9; }
    static double beats (int r) { static const double b[] = { 0.25, 0.5, 1, 2, 4, 8, 16, 32, 64 }; return b[std::min (8, std::max (0, r))]; }
    static double hz (int r) { static const double h[] = { 0.1, 0.25, 0.5, 1, 2, 5 }; return h[std::min (5, std::max (0, r - 9))]; }

    double phase = 0, freePhase = 0;
    long long cycle = -1;
    float a = 0, b = 0, value = 0;
    uint32_t seed = 1;

    // advance to the block, returns -1..1
    float tick (int shape, int rate, const Ctx& ctx, int n)
    {
        double cyc;
        if (synced (rate)) { const double x = ctx.ppq / beats (rate); cyc = std::floor (x); phase = x - cyc; }
        else { freePhase += hz (rate) * n / ctx.sr; cyc = std::floor (freePhase); phase = freePhase - cyc; }
        const long long c = (long long) cyc;
        if (c != cycle)
        {
            cycle = c;
            a = b;
            b = (float) (hash (seed * 7919u + (uint32_t) c) >> 8) / 8388608.0f - 1.0f;
        }
        const float p = (float) phase;
        switch (shape)
        {
            case 0: value = std::sin (kTwoPi * p); break;
            case 1: value = 1.0f - 4.0f * std::abs (p - 0.5f); break;
            case 2: value = 2.0f * p - 1.0f; break;
            case 3: value = 1.0f - 2.0f * p; break;
            case 4: value = p < 0.5f ? 1.0f : -1.0f; break;
            case 5: value = b; break;
            default: { const float t = 0.5f - 0.5f * std::cos (kPi * p); value = a + (b - a) * t; }
        }
        return value;
    }
    static uint32_t hash (uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
};
} // namespace il
