// Vendored from semierendonmez-commits tst-ingp Source/engine/Randomizer.h (Rack / MetaModule port).
// Only change: namespaces ingp -> tstingp, origpresets -> tstingp_orig (one binary, PINDATA has a copy).
#pragma once
#include "IngpEngine.h"
#include "Sdif.h"
#include <functional>

namespace tstingp
{
/** Three tiers, as in the other plugins of the family:
      NUDGE   small moves around what is there
      MEDIUM  swap within the same family (a new harmonic series, a related rule ...)
      A LOT   anything goes                                                            */
enum class Amount { nudge = 0, medium = 1, alot = 2 };

enum Section : uint32_t
{
    secRule   = 1u << 0,
    secField  = 1u << 1,
    secFreq   = 1u << 2,
    secAmp    = 1u << 3,
    secPan    = 1u << 4,
    secLen    = 1u << 5,
    secLfo    = 1u << 6,
    secSound  = 1u << 7,
    secAll    = 0xffu
};

struct Rng
{
    uint64_t s = 0x853c49e6748fea9bull;
    explicit Rng (uint64_t seed = 1) { s ^= seed * 0x9E3779B97F4A7C15ull; next(); }
    uint64_t next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 2685821657736338717ull; }
    double uni() { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }
    double range (double a, double b) { return a + (b - a) * uni(); }
    int    integer (int n) { return n <= 0 ? 0 : (int) (next() % (uint64_t) n); }
    bool   chance (double p) { return uni() < p; }
};

/** Everything here is lock-free on the engine's atomics, so it can run on the audio
    thread (EVOLVE) or the message thread (RANDOMIZE). `sdifFrame` may supply a random
    analysed spectrum for the MEDIUM tier of FREQ/AMP; `exampleBits` a random example text. */
struct Randomizer
{
    Engine& e;
    Rng& r;
    std::function<bool (std::vector<double>&)> sdifFrame;          // message thread only
    std::function<bool (std::vector<uint8_t>&)> exampleBits;       // message thread only

    void rule (Amount a)
    {
        uint32_t v = e.rule.load();
        if (a == Amount::alot) v = (uint32_t) r.next();
        else
        {
            const int flips = a == Amount::nudge ? 1 + r.integer (2) : 3 + r.integer (6);
            for (int i = 0; i < flips; ++i) v ^= 1u << r.integer (32);
        }
        e.rule.store (v);
    }

    void field (Amount a)
    {
        if (a == Amount::nudge)
        {
            for (int i = 0; i < N / 100; ++i) { const int k = r.integer (N); e.setCell (k, ! e.cell (k)); }
            return;
        }
        if (a == Amount::medium)
        {
            for (int i = 0; i < 96; ++i) e.applyRuleAt (r.integer (N));
            for (int i = 0; i < N / 20; ++i) { const int k = r.integer (N); e.setCell (k, ! e.cell (k)); }
            return;
        }
        std::vector<uint8_t> bits;
        if (exampleBits && r.chance (0.5) && exampleBits (bits)) { e.loadBits (bits); return; }
        const double density = r.range (0.15, 0.6);
        for (int k = 0; k < N; ++k) e.setCell (k, r.chance (density));
    }

    void freq (Amount a)
    {
        if (a == Amount::nudge)
        {
            for (int v = 0; v < V; ++v)
            {
                auto& f = e.freq[(size_t) v];
                f.store ((float) std::clamp (f.load() + r.range (-0.012, 0.012), 0.0, 1.0));
            }
            return;
        }
        if (a == Amount::medium)
        {
            std::vector<double> vals;
            if (sdifFrame && r.chance (0.4) && sdifFrame (vals))
            {
                auto s = valuesToSliders (vals);
                for (int v = 0; v < V; ++v) e.freq[(size_t) v].store (v < (int) s.freq.size() ? s.freq[(size_t) v] : 0.0f);
                return;
            }
            // a harmonic (or slightly stretched / sub-harmonic) series on a new fundamental
            const double f0 = 40.0 * std::pow (2.0, r.range (0.0, 4.0));
            const double stretch = r.chance (0.5) ? 1.0 : r.range (0.97, 1.05);
            const int kind = r.integer (3);   // 0 harmonic, 1 odd only, 2 every partial from k0
            const int k0 = kind == 2 ? 1 + r.integer (6) : 1;
            int k = k0;
            for (int v = 0; v < V; ++v)
            {
                const double hz = f0 * std::pow ((double) k, stretch);
                e.freq[(size_t) v].store ((float) std::min (1.0, Engine::hzToSlider (hz)));
                k += kind == 1 ? 2 : 1;
            }
            return;
        }
        for (int v = 0; v < V; ++v) e.freq[(size_t) v].store ((float) r.range (0.18, 0.92));
    }

    void amp (Amount a)
    {
        if (a == Amount::nudge)
        {
            for (int v = 0; v < V; ++v)
            {
                auto& x = e.amp[(size_t) v];
                x.store ((float) std::clamp (x.load() + r.range (-0.04, 0.04), 0.0, 1.0));
            }
            return;
        }
        if (a == Amount::medium)
        {
            const double slope = r.range (0.4, 1.4), top = r.range (0.35, 0.7);
            for (int v = 0; v < V; ++v)
            {
                const double lin = top / std::pow ((double) v + 1.0, slope) * r.range (0.6, 1.2);
                e.amp[(size_t) v].store ((float) std::sqrt (std::clamp (lin, 0.0, 1.0)));
            }
            return;
        }
        for (int v = 0; v < V; ++v) e.amp[(size_t) v].store (r.chance (0.15) ? 0.0f : (float) r.range (0.05, 0.75));
    }

    void pan (Amount a)
    {
        if (a == Amount::nudge)
        {
            for (int v = 0; v < V; ++v)
            {
                auto& x = e.pan[(size_t) v];
                x.store ((float) std::clamp (x.load() + r.range (-0.06, 0.06), 0.0, 1.0));
            }
            return;
        }
        if (a == Amount::medium)
        {
            const int kind = r.integer (4); const double w = r.range (0.2, 0.5);
            for (int v = 0; v < V; ++v)
            {
                double p = 0.5;
                if (kind == 0) p = 0.5 + ((v & 1) ? w : -w);
                if (kind == 1) p = 0.5 - w + 2 * w * v / (V - 1.0);
                if (kind == 2) p = 0.5 + w * std::sin (v * 0.7);
                if (kind == 3) p = 0.5 + ((v / 4) & 1 ? w : -w);
                e.pan[(size_t) v].store ((float) std::clamp (p, 0.0, 1.0));
            }
            return;
        }
        for (int v = 0; v < V; ++v) e.pan[(size_t) v].store ((float) r.uni());
    }

    void len (Amount a)
    {
        if (a == Amount::nudge)
        {
            for (int i = 0; i < 4; ++i)
            {
                auto& x = e.len[(size_t) r.integer (V)];
                x.store (std::clamp (x.load() + (r.chance (0.5) ? 1 : -1), 1, 64));
            }
            return;
        }
        if (a == Amount::medium)
        {
            static const int sets[][4] = { { 64, 64, 64, 64 }, { 32, 32, 32, 32 }, { 16, 16, 16, 16 },
                                           { 16, 12, 20, 24 }, { 64, 48, 32, 16 }, { 13, 17, 19, 23 }, { 8, 12, 16, 6 } };
            const auto& s = sets[r.integer (7)];
            for (int v = 0; v < V; ++v) e.len[(size_t) v].store (s[r.integer (4)]);
            return;
        }
        for (int v = 0; v < V; ++v) e.len[(size_t) v].store (1 + r.integer (64));
    }
};
} // namespace ingp
