// SEMI Family port: vendored from semierendonmez-commits/snarestack Source/engine (0fa7677). Only change:
// namespace gtbs -> ss::gtbs (the break-slicer engines in the binary use gtbs too).
#include "Library.h"
#include "Dsp.h"

#include <algorithm>
#include <random>

namespace ss
{

using gtbs::kPi;

const std::vector<std::string>& synthNames()
{
    static const std::vector<std::string> n { "body 185", "body 230 tight", "crack noise", "crack bright", "clap",
                                              "rim", "tail verb", "808 snare", "sub thump", "brush" };
    return n;
}

namespace
{
struct Noise
{
    std::mt19937 rng;
    std::uniform_real_distribution<float> u { -1.0f, 1.0f };
    explicit Noise (uint32_t s) : rng (s) {}
    float operator()() { return u (rng); }
};

float attack (double t, double a) { return a <= 0.0 ? 1.0f : (float) std::min (1.0, t / a); }
}

std::shared_ptr<const Source> synth (int index)
{
    auto s = std::make_shared<Source>();
    const double rate = kRate;
    s->rate = rate;
    const auto& names = synthNames();
    index = std::clamp (index, 0, (int) names.size() - 1);
    s->name = names[(size_t) index];
    double len = 0.4;
    switch (index)
    {
        case 0: len = 0.6; break;  case 1: len = 0.3; break;  case 2: len = 0.3; break;  case 3: len = 0.15; break;
        case 4: len = 0.5; break;  case 5: len = 0.12; break; case 6: len = 1.6; break;  case 7: len = 0.5; break;
        case 8: len = 0.35; break; default: len = 0.4; break;
    }
    const int n = (int) (len * rate);
    s->l.assign ((size_t) n, 0.0f);
    s->r.assign ((size_t) n, 0.0f);
    Noise nz (1234u + (uint32_t) index), nz2 (999u + (uint32_t) index);
    gtbs::Biquad h1, h2, l1, l2, hr1, hr2, lr1, lr2;
    double ph = 0.0, ph2 = 0.0;
    switch (index)
    {
        case 2: h1.highpass (rate, 1800, 0.5412); h2.highpass (rate, 1800, 1.3066); break;
        case 3: h1.highpass (rate, 3500, 0.7071); h2.highpass (rate, 3500, 0.7071); l1.lowpass (rate, 9000, 0.7071); break;
        case 4: h1.highpass (rate, 1000, 0.7071); l1.lowpass (rate, 2600, 0.7071); break;
        case 5: h1.highpass (rate, 2500, 0.7071); break;
        case 6: h1.highpass (rate, 300, 0.7071); l1.lowpass (rate, 6000, 0.7071); hr1.highpass (rate, 300, 0.7071); lr1.lowpass (rate, 6000, 0.7071); break;
        case 7: h1.highpass (rate, 1200, 0.7071); h2.highpass (rate, 1200, 0.7071); break;
        case 9: l1.lowpass (rate, 4000, 0.7071); h1.highpass (rate, 400, 0.7071); break;
        default: break;
    }
    for (int i = 0; i < n; ++i)
    {
        const double t = i / rate;
        float v = 0.0f, vr = 0.0f;
        bool stereo = false;
        switch (index)
        {
            case 0:
            {
                const double f = 185.0 + 70.0 * std::exp (-t / 0.008);
                ph += 2.0 * kPi * f / rate;
                ph2 += 2.0 * kPi * 330.0 / rate;
                const double x = std::sin (ph) * std::exp (-t / 0.09) + 0.25 * std::sin (ph2) * std::exp (-t / 0.05);
                v = (float) std::tanh (1.5 * x) * attack (t, 0.0003) * 0.8f;
                break;
            }
            case 1:
            {
                const double f = 230.0 + 60.0 * std::exp (-t / 0.005);
                ph += 2.0 * kPi * f / rate;
                v = (float) (std::sin (ph) * std::exp (-t / 0.05)) * attack (t, 0.0003) * 0.85f;
                break;
            }
            case 2: v = h2.process (h1.process (nz())) * (float) std::exp (-t / 0.035) * 0.9f; break;
            case 3:
            {
                float x = l1.process (h2.process (h1.process (nz())));
                if (i < 3) x += 0.8f;
                v = x * (float) std::exp (-t / 0.02) * 0.9f;
                break;
            }
            case 4:
            {
                const float x = l1.process (h1.process (nz()));
                double e = 0.0;
                for (double b : { 0.0, 0.0085, 0.017 }) if (t >= b) e += std::exp (-(t - b) / 0.004);
                if (t >= 0.021) e += 0.5 * std::exp (-(t - 0.021) / 0.12);
                v = x * (float) e * 0.9f;
                break;
            }
            case 5:
            {
                ph += 2.0 * kPi * 1700.0 / rate;
                ph2 += 2.0 * kPi * 2600.0 / rate;
                v = (float) ((0.6 * std::sin (ph) + 0.4 * std::sin (ph2)) * std::exp (-t / 0.012) + 0.3 * h1.process (nz()) * std::exp (-t / 0.006)) * 0.8f;
                break;
            }
            case 6:
            {
                stereo = true;
                const float e = attack (t, 0.004) * (float) std::exp (-t / 0.35) * 0.45f;
                v = l1.process (h1.process (nz())) * e;
                vr = lr1.process (hr1.process (nz2())) * e;
                break;
            }
            case 7:
            {
                ph += 2.0 * kPi * 180.0 / rate;
                ph2 += 2.0 * kPi * 330.0 / rate;
                v = (float) (0.5 * std::sin (ph) * std::exp (-t / 0.07) + 0.3 * std::sin (ph2) * std::exp (-t / 0.04)
                             + 0.5 * h2.process (h1.process (nz())) * std::exp (-t / 0.12)) * attack (t, 0.0002);
                break;
            }
            case 8:
            {
                const double f = 90.0 + 25.0 * std::exp (-t / 0.01);
                ph += 2.0 * kPi * f / rate;
                v = (float) (std::sin (ph) * std::exp (-t / 0.06)) * attack (t, 0.0005) * 0.9f;
                break;
            }
            default:
                v = h1.process (l1.process (nz())) * attack (t, 0.006) * (float) std::exp (-t / 0.15) * 0.7f;
                break;
        }
        // the last 10 ms fade out
        const double tail = len - t;
        const float f = tail < 0.01 ? (float) (tail / 0.01) : 1.0f;
        s->l[(size_t) i] = v * f;
        s->r[(size_t) i] = (stereo ? vr : v) * f;
    }
    return s;
}

std::shared_ptr<const Source> snareFromBreak (const gtbs::Sample& b, const std::string& name)
{
    auto m = gtbs::buildSlices (b, gtbs::SliceOnsets);
    int best = -1;
    float bl = -1.0f;
    for (int i = 0; i < m.count(); ++i)
        if (m.cls[(size_t) i] == gtbs::Snare && m.level[(size_t) i] > bl) { bl = m.level[(size_t) i]; best = i; }
    if (best < 0)
        for (int i = 0; i < m.count(); ++i) if (m.level[(size_t) i] > bl) { bl = m.level[(size_t) i]; best = i; }
    auto s = std::make_shared<Source>();
    s->name = name;
    s->rate = b.rate;
    if (best < 0) return s;
    // the slicer cuts where a slice may start; the snare is heard to start where its peak
    // envelope rises through 20 % above the quiet before it (looking up to 15 ms back)
    const int s0 = m.starts[(size_t) best];
    const int nx = best + 1 < m.count() ? m.starts[(size_t) best + 1] : b.frames;
    const int lo = std::max (best > 0 ? m.starts[(size_t) best - 1] + (int) (b.rate * 0.006) : 0, s0 - (int) (b.rate * 0.015));
    const int hi = std::min (nx, s0 + (int) (b.rate * 0.03));
    int attack = s0;
    if (hi - lo > 64)
    {
        const int w = std::max (4, (int) (b.rate * 0.001));
        std::vector<float> e ((size_t) (hi - lo));
        for (int n = lo; n < hi; ++n)
        {
            float pk = 0.0f;
            for (int k = std::max (0, n - w + 1); k <= n; ++k)
                pk = std::max (pk, std::abs (0.5f * (b.l[(size_t) k] + (b.r.empty() ? b.l[(size_t) k] : b.r[(size_t) k]))));
            e[(size_t) (n - lo)] = pk;
        }
        int pkAt = 0;
        for (int n = 0; n < hi - lo; ++n) if (e[(size_t) n] > e[(size_t) pkAt]) pkAt = n;
        float floorV = e[(size_t) pkAt];
        for (int n = 0; n <= pkAt; ++n) floorV = std::min (floorV, e[(size_t) n]);
        const float thr = floorV + 0.2f * (e[(size_t) pkAt] - floorV);
        int a = pkAt;
        while (a > 0 && e[(size_t) a - 1] >= thr) --a;
        attack = lo + a;
    }
    const int pre = (int) (b.rate * 0.0015);
    const int st = std::max (0, attack - pre);
    const int len = std::min (nx - st, (int) (b.rate * 0.45));
    const int fade = std::min (len / 4, (int) (b.rate * 0.008));
    s->l.resize ((size_t) len);
    s->r.resize ((size_t) len);
    for (int i = 0; i < len && st + i < b.frames; ++i)
    {
        const float g = i >= len - fade ? (float) (len - i) / (float) fade : 1.0f;
        s->l[(size_t) i] = b.l[(size_t) (st + i)] * g;
        s->r[(size_t) i] = (b.r.empty() ? b.l[(size_t) (st + i)] : b.r[(size_t) (st + i)]) * g;
    }
    return s;
}

} // namespace ss
