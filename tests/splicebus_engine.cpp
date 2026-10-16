// Offline test of the vendored SPLICEBUS engine + the Patch port (engines/splicebus). Not part of the plugin build.
//   g++ -std=c++17 -O2 -Iengines tests/splicebus_engine.cpp engines/splicebus/Engine.cpp engines/splicebus/Reel.cpp
//       engines/splicebus/Patch.cpp -o build/splicebus_engine && build/splicebus_engine
// On a synthetic 174 BPM drum and bass loop, in the module's 64-sample blocks on a clock: CHAOS 0 + SPLIT OFF
// is the input (delayed by the latency) sample for sample; at full chaos with FIXED the bass under 60 Hz is not
// broken while the upper band is; every factory preset is finite and bounded; MUTATE keeps locks; deterministic;
// FREE RUN plays on its own clock and a stopped transport is a wire.
#include "splicebus/Patch.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
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

using namespace spb;
static const double kSr = 48000.0, kBpm = 174.0, kSpb = kSr * 60.0 / kBpm;

struct Stereo { std::vector<float> l, r; };

static Stereo material (double bars)
{
    const int n = (int) (bars * 4 * kSpb);
    Stereo s { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) };
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    double ph = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double beat = i / kSpb;
        static const double notes[4] { 55.0, 55.0, 41.2, 49.0 };
        ph += 2.0 * M_PI * notes[((int) (beat / 4.0)) % 4] / kSr;
        float x = 0.35f * (float) (std::sin (ph) + 0.25 * std::sin (2.0 * ph) + 0.1 * std::sin (3.0 * ph));
        const double inBar = std::fmod (beat, 4.0);
        for (double kb : { 0.0, 2.5 })
        {
            const double t = (inBar - kb) * 60.0 / kBpm;
            if (t >= 0.0 && t < 0.3) x += 0.6f * (float) (std::exp (-t * 14.0) * std::sin (2.0 * M_PI * (50.0 * t + 120.0 * (1.0 - std::exp (-t * 30.0)) / 30.0)));
        }
        const double t8 = std::fmod (beat, 0.5) * 60.0 / kBpm;
        if (t8 < 0.05) x += 0.25f * (float) std::exp (-t8 * 80.0) * u (rng);
        const double ts = (std::fmod (beat, 2.0) - 1.0) * 60.0 / kBpm;
        if (ts >= 0.0 && ts < 0.2) x += 0.4f * (float) std::exp (-ts * 20.0) * u (rng);
        s.l[(size_t) i] = x;
        s.r[(size_t) i] = x * 0.9f + 0.02f * u (rng);
    }
    return s;
}

// in 64-sample blocks; clocked = the module's CLK path (a position every block), else the engine's own
static Stereo render (const Params& p, const Stereo& in, bool clocked, Engine* keep = nullptr)
{
    std::unique_ptr<Engine> own;
    Engine* e = keep;
    if (! e) { own = std::make_unique<Engine>(); e = own.get(); }
    e->prepare (kSr, 64);
    e->setParams (p);
    Stereo out { std::vector<float> (in.l.size()), std::vector<float> (in.l.size()) };
    double ppq = 0.0;
    for (size_t at = 0; at + 64 <= in.l.size(); at += 64)
    {
        Transport t;
        if (clocked) { t.playing = true; t.hostHasPosition = true; t.bpm = kBpm; t.ppq = ppq; }
        e->process (&in.l[at], &in.r[at], nullptr, nullptr, &out.l[at], &out.r[at], 64, t);
        ppq += 64.0 * kBpm / 60.0 / kSr;
    }
    return out;
}

static double rms (const std::vector<float>& v, size_t a, size_t b)
{
    double s = 0.0;
    b = std::min (b, v.size());
    for (size_t i = a; i < b; ++i) s += (double) v[i] * v[i];
    return b > a ? std::sqrt (s / (double) (b - a)) : 0.0;
}

static std::vector<float> lowpass (const std::vector<float>& x, float fc)
{
    std::vector<float> y = x;
    for (int s = 0; s < 4; ++s)
    {
        dsp::Svf f;
        f.set (fc, 0.7071f, (float) kSr);
        for (auto& v : y) v = f.lp (v);
    }
    return y;
}

static bool finiteAndBounded (const Stereo& s, float lim = 4.0f)
{
    for (size_t i = 0; i < s.l.size(); ++i)
        if (! std::isfinite (s.l[i]) || ! std::isfinite (s.r[i]) || std::abs (s.l[i]) > lim || std::abs (s.r[i]) > lim) return false;
    return true;
}

static float urand (void* ctx) { return std::uniform_real_distribution<float> (0.0f, 1.0f) (*(std::mt19937*) ctx); }

int main()
{
    const auto in = material (8);
    auto vals = defaults();

    // ---- the wire
    {
        auto v = vals;
        v[(size_t) indexOf (ids::split)] = 0;
        v[(size_t) indexOf (ids::chaos)] = 0;
        v[(size_t) indexOf (ids::turns)] = 0;
        Engine e;
        const auto out = render (toParams (v), in, true, &e);
        const int L = e.latency();
        double d = 0.0;
        for (size_t i = (size_t) L; i < in.l.size() - 64; ++i) d = std::max (d, (double) std::abs (out.l[i] - in.l[i - (size_t) L]));
        CHECK (d < 1e-6, "CHAOS 0, SPLIT OFF: a wire (%g)", d);
        std::printf ("wire: latency %d samples, max diff %g\n", L, d);
    }

    // ---- the bass under the split is never broken; above it the reel breaks
    {
        auto v = vals;
        v[(size_t) indexOf (ids::split)] = 1;
        v[(size_t) indexOf (ids::splithz)] = 150.0f;
        v[(size_t) indexOf (ids::chaos)] = 1.0f;
        v[(size_t) indexOf (ids::turns)] = 1.0f;
        v[(size_t) indexOf (ids::wall)] = 2;
        v[(size_t) indexOf (ids::morph)] = 0.9f;
        v[(size_t) indexOf (ids::vari)] = 0.3f;
        v[(size_t) indexOf (ids::budget)] = 0;
        v[(size_t) indexOf (ids::protby)] = 0;   // as the plug-in's test: nothing protected
        const auto out = render (toParams (v), in, true);
        auto q = v;
        q[(size_t) indexOf (ids::chaos)] = 0.0f;
        q[(size_t) indexOf (ids::turns)] = 0.0f;
        q[(size_t) indexOf (ids::wall)] = 0;
        const auto ref = render (toParams (q), in, true);
        std::vector<float> diff (out.l.size());
        for (size_t i = 0; i < diff.size(); ++i) diff[i] = out.l[i] - ref.l[i];
        const auto dLow = lowpass (diff, 60.0f), xLow = lowpass (ref.l, 60.0f);
        const size_t from = (size_t) (2 * 4 * kSpb);
        const double lowDb = 20.0 * std::log10 (rms (dLow, from, dLow.size()) / std::max (1e-12, rms (xLow, from, xLow.size())));
        const double upDb = 20.0 * std::log10 (rms (diff, from, diff.size()) / std::max (1e-12, rms (ref.l, from, ref.l.size())));
        std::printf ("full chaos FIXED 150 Hz: change under 60 Hz %.1f dB, overall %.1f dB\n", lowDb, upDb);
        CHECK (lowDb < -30.0, "bass broken: %.1f dB", lowDb);
        CHECK (upDb > -12.0, "nothing broke above the split: %.1f dB", upDb);
        CHECK (finiteAndBounded (out), "full chaos not finite / bounded");
    }

    // ---- every factory preset
    for (const auto& pr : factoryPresets())
    {
        std::vector<float> v;
        apply (pr, v);
        const auto out = render (toParams (v), in, true);
        CHECK (finiteAndBounded (out) && rms (out.l, 0, out.l.size()) > 0.01, "preset %s", pr.name);
    }
    std::printf ("%d presets played\n", (int) factoryPresets().size());

    // ---- MUTATE keeps locks, stays finite
    {
        std::mt19937 rng (3);
        std::vector<float> v;
        apply (factoryPresets()[0], v);
        std::vector<bool> locked (v.size(), false);
        const int c = indexOf (ids::chaos);
        locked[(size_t) c] = true;
        const float before = v[(size_t) c];
        for (int k = 0; k < 10; ++k)
        {
            mutate (v, 0.5f, locked, urand, &rng);
            const auto& d = defs();
            for (size_t i = 0; i < d.size(); ++i) CHECK (v[i] >= d[i].min - 1e-4f && v[i] <= d[i].max + 1e-4f, "%s out of range %.3f", d[i].id, v[i]);
        }
        CHECK (v[(size_t) c] == before, "locked chaos moved");
        CHECK (finiteAndBounded (render (toParams (v), in, true)), "mutated not finite");
    }

    // ---- deterministic; FREE RUN; stopped = wire
    {
        std::vector<float> v;
        apply (factoryPresets()[2], v);
        const auto a = render (toParams (v), in, true), b = render (toParams (v), in, true);
        double d = 0.0;
        for (size_t i = 0; i < a.l.size(); ++i) d = std::max (d, (double) std::abs (a.l[i] - b.l[i]));
        CHECK (d == 0.0, "two bounces differ (%g)", d);

        v[(size_t) indexOf (ids::run)] = 1;
        v[(size_t) indexOf (ids::bpm)] = (float) kBpm;
        v[(size_t) indexOf (ids::chaos)] = 1.0f;
        Engine e;
        const auto fr = render (toParams (v), in, false, &e);
        CHECK (e.display.running.load(), "FREE RUN is not running");
        v[(size_t) indexOf (ids::run)] = 0;
        v[(size_t) indexOf (ids::split)] = 0;
        Engine e2;
        const auto st = render (toParams (v), in, false, &e2);
        const int L = e2.latency();
        double ds = 0.0;
        for (size_t i = (size_t) L; i < in.l.size() - 64; ++i) ds = std::max (ds, (double) std::abs (st.l[i] - in.l[i - (size_t) L]));
        CHECK (ds < 1e-6, "stopped is not a wire (%g)", ds);
        (void) fr;
    }

    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
