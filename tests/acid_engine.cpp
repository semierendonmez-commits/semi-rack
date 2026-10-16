// ACID engine test — the plugin's tests/EngineTest.cpp, ported to the JUCE-free API of the vendored
// engine (mono float buffer + MidiIn array instead of juce::AudioBuffer / MidiBuffer). Same checks.
// Not part of the plugin build:
//   g++ -std=c++17 -O2 -w -Iengines -Iengines/thirdparty tests/acid_engine.cpp engines/acid/*.cpp \
//       engines/thirdparty/open303/*.cpp -o build/acid_test && ./build/acid_test
#include "acid/AcidEngine.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace acid;

static double magnitude (const float* b, int n) { double m = 0; for (int i = 0; i < n; ++i) m = std::max (m, (double) std::abs (b[i])); return m; }
static bool finiteAll (const float* b, int n) { for (int i = 0; i < n; ++i) if (! std::isfinite (b[i])) return false; return true; }

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf ("FAIL: " __VA_ARGS__); std::printf ("\n"); ++fails; } } while (0)

static int gates (const Tb3poCore& c) { int n = 0; for (int i = 0; i < 32; ++i) n += c.step (i).gate; return n; }

int main()
{
    // --- density -----------------------------------------------------------
    Tb3poCore core;
    Tb3poCore::GenParams g;
    std::array<Tb3poCore::Step, 32> at3;
    g.density = 3; core.generate (g);
    for (int i = 0; i < 32; ++i) at3[(size_t) i] = core.step (i);
    g.density = 6; core.generate (g);
    g.density = 3; core.generate (g);
    bool same = true;
    for (int i = 0; i < 32; ++i)
        same &= at3[(size_t) i].gate == core.step (i).gate && at3[(size_t) i].degree == core.step (i).degree;
    CHECK (same, "density 3 -> 6 -> 3 did not return the same line");

    std::printf ("gates by density:");
    int prevPos = -1, prevNeg = -1;
    bool mono = true;
    for (int d = 0; d <= 7; ++d)
    {
        g.density = d;  core.generate (g); const int gp = gates (core);
        g.density = -d; core.generate (g); const int gn = gates (core);
        std::printf (" %+d:%d/%d", d, gp, gn);
        mono &= gp >= prevPos && gn >= prevNeg;
        prevPos = gp; prevNeg = gn;
    }
    std::printf ("\n");
    CHECK (mono, "gate count not monotonic in |density|");

    g.density = -7; core.generate (g);
    bool rootOnly = true;
    for (int i = 0; i < 32; ++i) rootOnly &= core.step (i).degree == 0;
    CHECK (rootOnly, "-7 is not root-only");
    CHECK (gates (core) >= 24, "-7 is not dense (%d)", gates (core));
    g.density = 7; core.generate (g);
    int maxDeg = 0;
    for (int i = 0; i < 32; ++i) maxDeg = std::max (maxDeg, (int) core.step (i).degree);
    CHECK (maxDeg >= 4, "+7 does not use the scale (max degree %d)", maxDeg);

    // --- engine --------------------------------------------------------------
    const double sr = 48000.0;
    const int block = 256;
    AcidEngine e;
    e.prepare (sr);
    EngineSettings s;
    for (int i = 0; i < Voice303::numFloat; ++i) s.base[(size_t) i] = 0.5f;
    s.base[5] = 0.8f; s.base[14] = 1.0f; s.base[13] = 0.0f;
    s.gen.density = 5;
    s.lfo[0] = { 0.4f, 2, 2 };          // cutoff: smooth random, 1/8
    e.refresh (s);

    std::vector<float> buf ((size_t) block);
    bool finite = true;
    HostTime ht; ht.valid = true; ht.playing = true; ht.bpm = 132.0;
    const double pps = ht.bpm / 60.0 / sr;
    int ons = 0, off = 0;
    double peak = 0.0;
    float cutLo = 1.0f, cutHi = 0.0f;
    const int blocks = (int) (32.0 / pps / block);
    for (int b = 0; b < blocks; ++b)
    {
        e.process (buf.data(), block, nullptr, 0, ht, s);
        finite = finite && finiteAll (buf.data(), block);
        for (int q = 0; q < e.eventCount(); ++q)
            if ((e.event (q).b[0] & 0xF0) == 0x90 && e.event (q).b[2] > 0)
            {
                ++ons;
                const double six = (ht.ppq + e.event (q).time * pps) * 4.0;
                if (std::abs (six - std::round (six)) / (pps * 4.0) > 1.5) ++off;
            }
        peak = std::max (peak, magnitude (buf.data(), block));
        cutLo = std::min (cutLo, e.modValue (0)); cutHi = std::max (cutHi, e.modValue (0));
        ht.ppq += block * pps;
    }
    std::printf ("8 bars: %d notes, %d off-grid, peak %.3f, cutoff lfo %.2f..%.2f\n", ons, off, peak, cutLo, cutHi);
    CHECK (ons > 30 && off == 0, "playback");
    CHECK (peak > 0.01 && peak < 4.0, "level");
    CHECK (cutHi - cutLo > 0.2f, "lfo not moving");
    CHECK (finite, "non-finite audio");

    // locate: step follows ppq
    ht.ppq = 10.25; e.process (buf.data(), block, nullptr, 0, ht, s);
    std::printf ("locate ppq 10.25 -> step %d\n", e.position());
    CHECK (e.position() == 41 % 16, "locate");

    // auto mutate every bar
    s.autoMode = 1; s.autoBars = 1;
    auto before = e.core().pattern;
    for (int b = 0; b < (int) (8.0 / pps / block); ++b) { e.setEntropy ((uint32_t) b * 7919u); e.process (buf.data(), block, nullptr, 0, ht, s); ht.ppq += block * pps; }
    CHECK (std::memcmp (&before.salt, &e.core().pattern.salt, sizeof before.salt) != 0, "auto mutate did nothing");
    CHECK (before.seed == e.core().pattern.seed, "mutate changed the seed");
    s.autoMode = 2;
    before = e.core().pattern;
    for (int b = 0; b < (int) (8.0 / pps / block); ++b) { e.setEntropy ((uint32_t) b * 7919u); e.process (buf.data(), block, nullptr, 0, ht, s); ht.ppq += block * pps; }
    CHECK (before.seed != e.core().pattern.seed, "auto new kept the seed");

    // --- play modes -----------------------------------------------------------
    {
        AcidEngine g2;
        g2.prepare (sr);
        EngineSettings p = s;
        p.autoMode = 0;
        p.gen.density = 7;                       // every step gates: easy to count
        p.gen.slide = 0.0f;
        p.lfo[0].depth = 0.0f;
        p.playMode = 1; p.retrig = true; p.trigChannel = 0;
        g2.refresh (p);
        HostTime h; h.valid = true; h.playing = true; h.bpm = 120.0; h.ppq = 0.0;
        const double pp = h.bpm / 60.0 / sr;
        const int blk = 256;
        const double sPer16 = 0.25 / pp;
        std::vector<float> b ((size_t) blk);

        auto run = [&] (double fromPpq, double toPpq, double onPpq, double offPpq, int& onsOut, std::vector<int>& steps)
        {
            h.ppq = fromPpq;
            while (h.ppq < toPpq)
            {
                MidiIn m[2]; int nm = 0;
                const double end = h.ppq + blk * pp;
                if (onPpq >= h.ppq && onPpq < end)   { m[nm].time = (int) ((onPpq - h.ppq) / pp); m[nm].data[0] = 0x9F; m[nm].data[1] = 60; m[nm].data[2] = 100; ++nm; }
                if (offPpq >= h.ppq && offPpq < end) { m[nm].time = (int) ((offPpq - h.ppq) / pp); m[nm].data[0] = 0x8F; m[nm].data[1] = 60; m[nm].data[2] = 0; ++nm; }
                g2.process (b.data(), blk, m, nm, h, p);
                for (int q = 0; q < g2.eventCount(); ++q) if ((g2.event (q).b[0] & 0xF0) == 0x90 && g2.event (q).b[2] > 0) { ++onsOut; steps.push_back (g2.position()); }
                h.ppq = end;
            }
        };

        int ons = 0; std::vector<int> steps;
        run (0.0, 8.0, 1e9, 1e9, ons, steps);
        std::printf ("gate, no notes: %d notes out\n", ons);
        CHECK (ons == 0, "gate mode played without a gate");

        ons = 0; steps.clear();
        run (8.0, 16.0, 10.0, 11.0, ons, steps);          // held for one beat, on the grid
        std::printf ("gate held 1 beat: %d notes, first step %d\n", ons, steps.empty() ? -1 : steps[0]);
        CHECK (ons == 4, "gate 1 beat should give 4 sixteenths (got %d)", ons);
        CHECK (! steps.empty() && steps[0] == 0, "retrig did not start at step 1");

        p.playMode = 2; p.trigSteps = 8;
        ons = 0; steps.clear();
        run (16.0, 24.0, 18.0, 18.05, ons, steps);         // short note, 1/2-bar window
        std::printf ("trigger 1/2 bar: %d notes\n", ons);
        CHECK (ons == 8, "trigger window should give 8 sixteenths (got %d)", ons);

        ons = 0; steps.clear();
        run (24.0, 32.0, 26.0 + 0.01 * 0.25 / 0.25 * 0.02, 26.1, ons, steps);   // a touch late
        std::printf ("trigger late by ~%.1f ms: %d notes, first step %d\n", 0.005 / pp / sr * 1000.0, ons, steps.empty() ? -1 : steps[0]);
        CHECK (ons == 8 && ! steps.empty() && steps[0] == 0, "late trigger not caught");

        // host stopped: the trigger starts the line itself
        h.playing = false;
        ons = 0; steps.clear();
        run (0.0, 4.0, 1.3, 1.31, ons, steps);
        std::printf ("stopped transport, trigger: %d notes\n", ons);
        CHECK (ons == 8, "trigger with stopped transport (got %d)", ons);
        (void) sPer16;
    }

    std::printf (fails ? "%d FAILED\n" : "all good\n", fails);
    return fails ? 1 : 0;
}
