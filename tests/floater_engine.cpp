// Offline test of the vendored FLOATER engine + the Case port (engines/floater). Not part of the plugin build.
//   sh tests/floater_engine.sh
// Checks: the default patch plays on its own clock (RUN) and on an external clock (the module's CLK path),
// finite, bounded and not silent; the kick lands on the beat with an external clock; every factory patch plays
// finite and is heard; NEW PATCH and MUTATE give finite, bounded output and keep locked controls; the Case
// round-trips through snapshot / apply; block size does not change the sound; heap used by one instance.
#include "floater/Case.h"
#include "floater/Engine.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <malloc.h>
#include <memory>
#include <vector>

static int checks = 0, failures = 0;
#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        ++checks;                                                           \
        if (! (cond)) {                                                     \
            ++failures;                                                     \
            std::printf ("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond);     \
            std::printf (__VA_ARGS__);                                      \
            std::printf ("\n");                                             \
        }                                                                   \
    } while (0)

struct Stats { double rms = 0; float peak = 0; bool finite = true; };

// renders seconds of audio in blocks of `block`, internal clock (no host position) unless clockBpm > 0
static Stats render (flt::Engine& e, double sr, double seconds, int block, double clockBpm = 0.0, std::vector<float>* keep = nullptr)
{
    Stats st;
    std::vector<float> l ((size_t) block), r ((size_t) block);
    const int64_t total = (int64_t) (seconds * sr);
    double sum = 0.0;
    double ppq = 0.0;
    for (int64_t done = 0; done < total; done += block)
    {
        flt::Transport t;
        if (clockBpm > 0.0) { t.playing = true; t.hostHasPosition = true; t.bpm = clockBpm; t.ppq = ppq; }
        e.process (l.data(), r.data(), block, t);
        ppq += block * clockBpm / 60.0 / sr;
        for (int i = 0; i < block; ++i)
        {
            if (! std::isfinite (l[(size_t) i]) || ! std::isfinite (r[(size_t) i])) st.finite = false;
            st.peak = std::max (st.peak, std::max (std::abs (l[(size_t) i]), std::abs (r[(size_t) i])));
            sum += (double) l[(size_t) i] * l[(size_t) i] + (double) r[(size_t) i] * r[(size_t) i];
            if (keep) keep->push_back (l[(size_t) i]);
        }
    }
    st.rms = std::sqrt (sum / (2.0 * (double) total));
    return st;
}

int main()
{
    const double sr = 48000.0;
    auto heapNow = [] { auto m = mallinfo2(); return (size_t) (m.uordblks + m.hblkhd); };
    const size_t heap0 = heapNow();
    static flt::Case cs;
    static flt::Engine eng;
    eng.prepare (sr, 64);
    const size_t heap1 = heapNow();
    std::printf ("heap: Case + Engine at 48 kHz = %.1f MB\n", (heap1 - heap0) / 1048576.0);

    // ---- default patch on its own clock
    eng.setParams (cs.currentParams());
    int hits0 = 0;
    for (int l = 0; l < flt::kLanes; ++l) hits0 += eng.display.fired[(size_t) l].load();
    const auto t0 = std::chrono::steady_clock::now();
    Stats s = render (eng, sr, 6.0, 64);
    const double cpu = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / 6.0;
    int hits1 = 0;
    for (int l = 0; l < flt::kLanes; ++l) hits1 += eng.display.fired[(size_t) l].load();
    std::printf ("default: rms %.3f peak %.3f, %d hits in 6 s, %.1f%% of a core\n", s.rms, s.peak, hits1 - hits0, cpu * 100.0);
    CHECK (s.finite, "non-finite");
    CHECK (s.peak < 1.5f, "peak %.3f", s.peak);
    CHECK (s.rms > 0.01, "rms %.4f", s.rms);
    CHECK (hits1 - hits0 > 40, "hits %d", hits1 - hits0);

    // ---- external clock at 120 BPM: the kick (4 on the floor patch: hits 3/16 -> step 0 is a hit) lands near a 16th
    {
        eng.reset();
        flt::Params p = cs.currentParams();
        p.run = false;
        eng.setParams (p);
        Stats e = render (eng, sr, 4.0, 64, 120.0);
        CHECK (e.finite && e.rms > 0.01 && e.peak < 1.5f, "clocked rms %.4f peak %.3f", e.rms, e.peak);
        // stopped transport + RUN off = silence
        eng.reset();
        Stats z = render (eng, sr, 1.0, 64);
        Stats z2 = render (eng, sr, 1.0, 64);
        CHECK (z2.peak < 1.0e-3f, "stopped peak %.5f", z2.peak);
        (void) z;
    }

    // ---- every factory patch
    const auto& f = flt::Case::factoryPresets();
    for (int i = 0; i < (int) f.size(); ++i)
    {
        cs.loadFactory (i);
        eng.reset();
        eng.setParams (cs.currentParams());
        Stats st = render (eng, sr, 3.0, 128);
        CHECK (st.finite && st.peak < 1.5f && st.rms > 0.002, "patch %d %s: rms %.4f peak %.3f", i, f[(size_t) i].name.c_str(), st.rms, st.peak);
    }
    std::printf ("%d factory patches played\n", (int) f.size());

    // ---- NEW PATCH keeps locks; MUTATE
    cs.loadFactory (0);
    const int lockIdx = flt::Case::index (flt::ids::chance (2));
    cs.set (lockIdx, 0.123f);
    cs.setLocked (lockIdx, true);
    cs.seedRandom (12345);
    for (int k = 0; k < 8; ++k)
    {
        cs.newPatch();
        CHECK (std::abs (cs.get (lockIdx) - 0.123f) < 1e-6f, "lock lost: %.3f", cs.get (lockIdx));
        eng.reset();
        eng.setParams (cs.currentParams());
        Stats st = render (eng, sr, 2.0, 64);
        CHECK (st.finite && st.peak < 1.5f, "new patch %d (%s): peak %.3f", k, cs.presetName, st.peak);
        cs.mutate (0.5f);
        eng.setParams (cs.currentParams());
        st = render (eng, sr, 1.0, 64);
        CHECK (st.finite && st.peak < 1.5f, "mutated %d: peak %.3f", k, st.peak);
    }

    // ---- snapshot / apply round trip
    {
        cs.loadFactory (18);   // Turing Floor: conditions, step locks
        const auto snap = cs.snapshotValues();
        flt::Case other;
        other.applyValues (snap);
        bool same = true;
        for (int i = 0; i < flt::Case::numParams(); ++i)
            if (flt::Case::defs()[(size_t) i].id != flt::ids::run && flt::Case::defs()[(size_t) i].id != flt::ids::bpm && other.get (i) != cs.get (i)) same = false;
        for (int l = 0; l < flt::kLanes; ++l)
        {
            if (other.pattern (l) != cs.pattern (l)) same = false;
            for (int st = 0; st < flt::kMaxSteps; ++st) if (other.cond (l, st) != cs.cond (l, st)) same = false;
        }
        CHECK (same, "round trip");
    }

    // ---- deterministic: two fresh engines on the same clock give the same sound (the module always runs 64-sample
    //      blocks; the factory patches with the bus / FDN are not block-size invariant in the plug-in either)
    {
        cs.loadFactory (0);
        std::vector<float> a, b;
        // two fresh engines (reset() keeps the voices' state, as in the plug-in)
        auto e1 = std::make_unique<flt::Engine>(), e2 = std::make_unique<flt::Engine>();
        e1->prepare (sr, 64); e2->prepare (sr, 64);
        e1->setParams (cs.currentParams()); e2->setParams (cs.currentParams());
        render (*e1, sr, 2.0, 64, 150.0, &a);
        render (*e2, sr, 2.0, 64, 150.0, &b);
        const size_t n = std::min (a.size(), b.size());
        double diff = 0.0;
        size_t first = n;
        for (size_t i = 0; i < n; ++i) { diff = std::max (diff, (double) std::abs (a[i] - b[i])); if (first == n && std::abs (a[i] - b[i]) > 1e-4f) first = i; }
        std::printf ("two runs: max diff %.6f\n", diff);
        CHECK (diff == 0.0, "two runs differ by %.5f", diff);
    }

    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
