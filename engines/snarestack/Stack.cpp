// SEMI Family port: vendored from semierendonmez-commits/snarestack Source/engine (0fa7677). Only change:
// namespace gtbs -> ss::gtbs (the break-slicer engines in the binary use gtbs too).
#include "Stack.h"
#include "Dsp.h"
#include "Fft.h"

#include <algorithm>
#include <complex>
#include <numeric>

namespace ss
{

using gtbs::kPi;
using cvec = std::vector<std::complex<float>>;

namespace
{
int log2Ceil (int n) { int l = 0; while ((1 << l) < n) ++l; return l; }

void inverse (const gtbs::Fft& fft, cvec& x)
{
    for (auto& v : x) v = std::conj (v);
    fft.forward (x);
    const float inv = 1.0f / (float) fft.size();
    for (auto& v : x) v = std::conj (v) * inv;
}

// applies a real, zero-phase gain curve g (Hz -> linear) to a whole buffer
template <typename F>
void zeroPhase (std::vector<float>& x, double rate, F&& gain)
{
    if (x.empty()) return;
    const int l2 = log2Ceil ((int) x.size() + 256);
    gtbs::Fft fft (l2);
    const int N = fft.size();
    cvec X ((size_t) N);
    for (int i = 0; i < N; ++i) X[(size_t) i] = { i < (int) x.size() ? x[(size_t) i] : 0.0f, 0.0f };
    fft.forward (X);
    for (int k = 0; k <= N / 2; ++k)
    {
        const float g = gain ((double) k * rate / N);
        X[(size_t) k] *= g;
        if (k > 0 && k < N / 2) X[(size_t) (N - k)] *= g;
    }
    inverse (fft, X);
    for (size_t i = 0; i < x.size(); ++i) x[i] = X[i].real();
}

// moves a buffer later by a fraction of a sample (-1..1), as a pure phase shift
void fracShift (std::vector<float>& x, double frac)
{
    if (std::abs (frac) < 1.0e-6 || x.empty()) return;
    const int l2 = log2Ceil ((int) x.size() + 64);
    gtbs::Fft fft (l2);
    const int N = fft.size();
    cvec X ((size_t) N);
    for (int i = 0; i < N; ++i) X[(size_t) i] = { i < (int) x.size() ? x[(size_t) i] : 0.0f, 0.0f };
    fft.forward (X);
    for (int k = 0; k <= N / 2; ++k)
    {
        const double w = -2.0 * kPi * (double) k * frac / N;
        const std::complex<float> r ((float) std::cos (w), (float) std::sin (w));
        if (k == N / 2) { X[(size_t) k] *= (float) std::cos (kPi * frac); continue; }
        X[(size_t) k] *= r;
        if (k > 0) X[(size_t) (N - k)] *= std::conj (r);
    }
    inverse (fft, X);
    for (size_t i = 0; i < x.size(); ++i) x[i] = X[i].real();
}

// 4th-order Butterworth as two biquads, run forwards (the phase it adds is part of the sound)
void filter (std::vector<float>& x, double rate, float hp, float lp)
{
    if (hp > 21.0f)
        for (double q : { 0.5412, 1.3066 })
        {
            gtbs::Biquad b; b.highpass (rate, hp, q);
            for (auto& v : x) v = b.process (v);
        }
    if (lp < 19900.0f)
        for (double q : { 0.5412, 1.3066 })
        {
            gtbs::Biquad b; b.lowpass (rate, lp, q);
            for (auto& v : x) v = b.process (v);
        }
}

std::array<float, kSpecBins> spectrumOf (const std::vector<float>& x, int from, double rate)
{
    std::array<float, kSpecBins> out {};
    constexpr int l2 = 13, N = 1 << l2;
    const int len = std::min ((int) (rate * 0.15), N);
    gtbs::Fft fft (l2);
    cvec X ((size_t) N);
    for (int i = 0; i < N; ++i)
    {
        const int j = from + i;
        const float w = i < len ? 0.5f + 0.5f * std::cos ((float) kPi * (float) i / (float) len) : 0.0f;
        X[(size_t) i] = { (j >= 0 && j < (int) x.size() && i < len) ? x[(size_t) j] * w : 0.0f, 0.0f };
    }
    fft.forward (X);
    for (int b = 0; b < kSpecBins; ++b)
    {
        const double half = std::pow (600.0, 0.5 / (kSpecBins - 1));
        const int k0 = std::max (1, (int) std::floor (specHz (b) / half * N / rate));
        const int k1 = std::max (k0, (int) std::ceil (specHz (b) * half * N / rate));
        double e = 0.0; int n = 0;
        for (int k = k0; k <= k1 && k < N / 2; ++k) { e += std::norm (X[(size_t) k]); ++n; }
        out[(size_t) b] = (float) (10.0 * std::log10 (e / std::max (1, n) + 1.0e-14));
    }
    return out;
}

// the band-limited body (80 Hz .. 1.2 kHz) of a window, zero phase: what the aligner listens to
std::vector<float> bodyBand (const std::vector<float>& x, int from, int len, double rate)
{
    std::vector<float> w ((size_t) len, 0.0f);
    for (int i = 0; i < len; ++i)
    {
        const int j = from + i;
        if (j >= 0 && j < (int) x.size()) w[(size_t) i] = x[(size_t) j];
    }
    zeroPhase (w, rate, [] (double hz)
    {
        if (hz < 50.0 || hz > 2000.0) return 0.0f;
        const double lo = hz < 80.0 ? (hz - 50.0) / 30.0 : 1.0;
        const double hi = hz > 1200.0 ? 1.0 - (hz - 1200.0) / 800.0 : 1.0;
        return (float) (lo * hi);
    });
    return w;
}

struct Layer
{
    bool active = false;
    std::vector<float> l, r, mono;
    int onset = 0;
    float bodyE = 0.0f;
};
} // namespace

//==============================================================================
int attackOf (const std::vector<float>& x, double rate)
{
    const int n = (int) x.size();
    if (n < 16) return 0;
    const int w = std::max (4, (int) (rate * 0.001));
    const int search = std::min (n, (int) (rate * 0.25));
    std::vector<float> e ((size_t) search);
    // trailing 1 ms peak hold
    for (int i = 0; i < search; ++i)
    {
        float pk = 0.0f;
        for (int k = std::max (0, i - w + 1); k <= i; ++k) pk = std::max (pk, std::abs (x[(size_t) k]));
        e[(size_t) i] = pk;
    }
    int pkAt = 0;
    for (int i = 0; i < search; ++i) if (e[(size_t) i] > e[(size_t) pkAt]) pkAt = i;
    // the first time it gets near its peak decides which hit this is (a soft lead-in does not count)
    for (int i = 0; i < pkAt; ++i) if (e[(size_t) i] > 0.7f * e[(size_t) pkAt]) { pkAt = i; break; }
    float floorV = e[(size_t) pkAt];
    for (int i = 0; i <= pkAt; ++i) floorV = std::min (floorV, e[(size_t) i]);
    const float thr = floorV + 0.2f * (e[(size_t) pkAt] - floorV);
    int a = pkAt;
    while (a > 0 && e[(size_t) a - 1] >= thr) --a;
    return a;
}

std::array<float, NumComps> barycentric (float x, float y)
{
    // crack (0.5, 0), body (0, 1), tail (1, 1)
    const float x1 = 0.5f, y1 = 0.0f, x2 = 0.0f, y2 = 1.0f, x3 = 1.0f, y3 = 1.0f;
    const float det = (y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3);
    float a = ((y2 - y3) * (x - x3) + (x3 - x2) * (y - y3)) / det;
    float b = ((y3 - y1) * (x - x3) + (x1 - x3) * (y - y3)) / det;
    float c = 1.0f - a - b;
    a = std::max (0.0f, a); b = std::max (0.0f, b); c = std::max (0.0f, c);
    const float s = std::max (1.0e-6f, a + b + c);
    return { a / s, b / s, c / s };
}

void clampToTriangle (float& x, float& y)
{
    const auto w = barycentric (x, y);
    x = w[0] * 0.5f + w[1] * 0.0f + w[2] * 1.0f;
    y = w[0] * 0.0f + w[1] * 1.0f + w[2] * 1.0f;
}

std::array<float, NumComps> balanceGains (float x, float y, float depthDb)
{
    const auto w = barycentric (x, y);
    std::array<float, NumComps> g {};
    for (int k = 0; k < NumComps; ++k) g[(size_t) k] = depthDb * (3.0f * w[(size_t) k] - 1.0f) * 0.5f;
    return g;
}

void renderHit (const Stack& s, const std::array<float, NumComps>& gainsDb, std::vector<float>& L, std::vector<float>& R)
{
    L.assign ((size_t) s.frames, 0.0f);
    R.assign ((size_t) s.frames, 0.0f);
    for (int k = 0; k < NumComps; ++k)
    {
        const float g = std::pow (10.0f, gainsDb[(size_t) k] / 20.0f);
        for (int i = 0; i < s.frames; ++i)
        {
            L[(size_t) i] += g * s.cL[(size_t) k][(size_t) i];
            R[(size_t) i] += g * s.cR[(size_t) k][(size_t) i];
        }
    }
}

//==============================================================================
std::unique_ptr<Stack> build (const std::array<std::shared_ptr<const Source>, kLayers>& src, const StackParams& p)
{
    auto st = std::make_unique<Stack>();
    const double rate = kRate;
    std::array<Layer, kLayers> L;
    const int maxLen = (int) (rate * 2.5);

    // ---- 1. each layer on its own
    for (int i = 0; i < kLayers; ++i)
    {
        const auto& lp = p.layer[(size_t) i];
        auto& ly = L[(size_t) i];
        if (src[(size_t) i] == nullptr || ! lp.on || src[(size_t) i]->frames() < 16) continue;
        const auto& s = *src[(size_t) i];
        const double inRate = s.rate * std::pow (2.0, (double) lp.tune / 12.0);
        ly.l = std::abs (inRate - rate) < 1.0e-6 ? s.l : gtbs::resampleSinc (s.l, inRate, rate);
        ly.r = s.r.empty() ? ly.l : (std::abs (inRate - rate) < 1.0e-6 ? s.r : gtbs::resampleSinc (s.r, inRate, rate));
        if ((int) ly.l.size() > maxLen) { ly.l.resize ((size_t) maxLen); ly.r.resize ((size_t) maxLen); }
        if (ly.r.size() != ly.l.size()) ly.r.resize (ly.l.size(), 0.0f);
        filter (ly.l, rate, lp.hp, lp.lp);
        filter (ly.r, rate, lp.hp, lp.lp);
        ly.mono.resize (ly.l.size());
        for (size_t n = 0; n < ly.l.size(); ++n) ly.mono[n] = 0.5f * (ly.l[n] + ly.r[n]);
        ly.onset = attackOf (ly.mono, rate);

        // decay, from the attack on
        if (lp.decay < 0.999f)
        {
            const double tau = 0.015 * std::pow (60.0, (double) lp.decay) * rate;
            for (size_t n = (size_t) ly.onset; n < ly.l.size(); ++n)
            {
                const float g = (float) std::exp (-(double) (n - (size_t) ly.onset) / tau);
                ly.l[n] *= g; ly.r[n] *= g; ly.mono[n] *= g;
            }
        }
        // gain, equal-power pan (centre = unity)
        const float gain = std::pow (10.0f, lp.gainDb / 20.0f);
        const double th = ((double) std::clamp (lp.pan, -1.0f, 1.0f) + 1.0) * kPi * 0.25;
        const float gl = gain * std::min (1.0f, (float) (std::cos (th) / std::cos (kPi * 0.25)));
        const float gr = gain * std::min (1.0f, (float) (std::sin (th) / std::sin (kPi * 0.25)));
        for (size_t n = 0; n < ly.l.size(); ++n) { ly.l[n] *= gl; ly.r[n] *= gr; ly.mono[n] *= gain; }
        ly.active = true;

        auto& inf = st->info[(size_t) i];
        inf.active = true;
        inf.onset = ly.onset;
        inf.spec = spectrumOf (ly.mono, ly.onset - (int) (rate * 0.001), rate);
        float pk = 0.0f; for (float v : ly.mono) pk = std::max (pk, std::abs (v));
        inf.peak = pk;
        // body energy: what the reference is chosen by
        double eb = 0.0, et = 1.0e-12;
        for (int b = 0; b < kSpecBins; ++b)
        {
            const double e = std::pow (10.0, inf.spec[(size_t) b] / 10.0);
            et += e;
            if (specHz (b) > 120.0f && specHz (b) < 900.0f) eb += e;
        }
        inf.bodyShare = (float) (eb / et);
        ly.bodyE = (float) eb;
    }

    int nActive = 0;
    for (auto& ly : L) if (ly.active) ++nActive;

    // ---- 2. carve: in a band two layers fight over, the weaker one steps back
    if (p.carve > 0.001f && nActive > 1)
    {
        for (int b = 0; b < kSpecBins; ++b)
        {
            int own = -1; float best = -1.0e9f;
            for (int i = 0; i < kLayers; ++i)
                if (L[(size_t) i].active && st->info[(size_t) i].spec[(size_t) b] > best) { best = st->info[(size_t) i].spec[(size_t) b]; own = i; }
            st->owner[(size_t) b] = own;
        }
        for (int i = 0; i < kLayers; ++i)
        {
            if (! L[(size_t) i].active) continue;
            auto& inf = st->info[(size_t) i];
            std::array<float, kSpecBins> att {};
            for (int b = 0; b < kSpecBins; ++b)
            {
                const int o = st->owner[(size_t) b];
                if (o < 0 || o == i) continue;
                const float d = st->info[(size_t) o].spec[(size_t) b] - inf.spec[(size_t) b];
                if (d < 12.0f) att[(size_t) b] = -p.carve * 9.0f * (1.0f - d / 12.0f);
            }
            // smooth across neighbours: a carve is an EQ curve, not a comb
            for (int b = 0; b < kSpecBins; ++b)
            {
                float s = 0.0f, n = 0.0f;
                for (int k = -2; k <= 2; ++k)
                    if (b + k >= 0 && b + k < kSpecBins) { const float w = 1.0f - 0.3f * (float) std::abs (k); s += w * att[(size_t) (b + k)]; n += w; }
                inf.carveDb[(size_t) b] = s / n;
            }
            auto curve = [&inf] (double hz)
            {
                if (hz <= 0.0) return 1.0f;
                const double pos = std::log (hz / 30.0) / std::log (600.0) * (kSpecBins - 1);
                const int k = std::clamp ((int) std::floor (pos), 0, kSpecBins - 1);
                const int k2 = std::min (kSpecBins - 1, k + 1);
                const float f = (float) std::clamp (pos - k, 0.0, 1.0);
                const float db = inf.carveDb[(size_t) k] * (1.0f - f) + inf.carveDb[(size_t) k2] * f;
                return std::pow (10.0f, db / 20.0f);
            };
            auto& ly = L[(size_t) i];
            zeroPhase (ly.l, rate, curve);
            zeroPhase (ly.r, rate, curve);
            for (size_t n = 0; n < ly.l.size(); ++n) ly.mono[n] = 0.5f * (ly.l[n] + ly.r[n]);
        }
    }
    else
    {
        for (int b = 0; b < kSpecBins; ++b)
        {
            int own = -1; float best = -1.0e9f;
            for (int i = 0; i < kLayers; ++i)
                if (L[(size_t) i].active && st->info[(size_t) i].spec[(size_t) b] > best) { best = st->info[(size_t) i].spec[(size_t) b]; own = i; }
            st->owner[(size_t) b] = own;
        }
    }

    // ---- 3. alignment
    int ref = -1;
    if (p.ref >= 0 && p.ref < kLayers && L[(size_t) p.ref].active) ref = p.ref;
    else
    {
        float best = -1.0f;
        for (int i = 0; i < kLayers; ++i) if (L[(size_t) i].active && L[(size_t) i].bodyE > best) { best = L[(size_t) i].bodyE; ref = i; }
    }
    st->ref = std::max (0, ref);
    int maxOnset = 0;
    for (auto& ly : L) if (ly.active) maxOnset = std::max (maxOnset, ly.onset);
    const int T = maxOnset + (int) (rate * 0.005);           // every attack lands here
    const int rawStart = T - maxOnset;                       // the naive stack: all files start together

    std::array<double, kLayers> start {};                    // aligned placement (fractional)
    std::array<bool, kLayers> inv {};
    const int win = (int) (rate * 0.04), pre = (int) (rate * 0.001);
    const int maxLag = (int) (rate * 0.0015);
    std::array<std::vector<float>, kLayers> band;            // body band of each layer, attack at index pre
    for (int i = 0; i < kLayers; ++i)
        if (L[(size_t) i].active) band[(size_t) i] = bodyBand (L[(size_t) i].mono, L[(size_t) i].onset - pre - maxLag, win + 2 * maxLag, rate);

    for (int i = 0; i < kLayers; ++i)
    {
        if (! L[(size_t) i].active) continue;
        start[(size_t) i] = p.align == AlignOff ? (double) rawStart : (double) (T - L[(size_t) i].onset);
        const int pol = p.layer[(size_t) i].polarity;
        inv[(size_t) i] = pol == PolInvert;
    }

    if (p.align != AlignOff && nActive > 1)
    {
        // what the layers are aligned against: the reference first, then each layer as it is placed
        std::vector<float> acc ((size_t) win, 0.0f);
        for (int n = 0; n < win; ++n) acc[(size_t) n] = band[(size_t) ref][(size_t) (n + maxLag)] * (inv[(size_t) ref] ? -1.0f : 1.0f);
        std::vector<int> order;
        for (int i = 0; i < kLayers; ++i) if (L[(size_t) i].active && i != ref) order.push_back (i);
        std::sort (order.begin(), order.end(), [&] (int a, int b) { return L[(size_t) a].bodyE > L[(size_t) b].bodyE; });
        for (int j : order)
        {
            const int pol = p.layer[(size_t) j].polarity;
            const auto& bj = band[(size_t) j];
            auto corrAt = [&] (int d)
            {
                double c = 0.0;
                for (int n = 0; n < win; ++n) c += (double) acc[(size_t) n] * bj[(size_t) (n + maxLag + d)];
                return c;
            };
            double fine = 0.0;
            bool flip = pol == PolInvert;
            // normalised correlation at a lag: only a real likeness moves or flips a layer
            double eAcc = 1.0e-20;
            for (int n = 0; n < win; ++n) eAcc += (double) acc[(size_t) n] * acc[(size_t) n];
            auto rho = [&] (int d)
            {
                double e = 1.0e-20;
                for (int n = 0; n < win; ++n) e += (double) bj[(size_t) (n + maxLag + d)] * bj[(size_t) (n + maxLag + d)];
                return corrAt (d) / std::sqrt (eAcc * e);
            };
            constexpr double minLike = 0.2;      // below this the layers have nothing in common to line up
            const double r0 = rho (0);
            if (p.align == AlignPhase)
            {
                int bd = 0; double bv = -1.0e30;
                std::vector<double> cs ((size_t) (2 * maxLag + 1));
                for (int d = -maxLag; d <= maxLag; ++d)
                {
                    const double c = corrAt (d);
                    cs[(size_t) (d + maxLag)] = c;
                    const double v = pol == PolAuto ? std::abs (c) : (pol == PolInvert ? -c : c);
                    if (v > bv) { bv = v; bd = d; }
                }
                const double cBest = cs[(size_t) (bd + maxLag)];
                const double rBest = rho (bd);
                const double like = pol == PolAuto ? std::abs (rBest) : (pol == PolInvert ? -rBest : rBest);
                const double atZero = pol == PolAuto ? std::abs (r0) : (pol == PolInvert ? -r0 : r0);
                if (like >= minLike && like >= atZero)
                {
                    if (pol == PolAuto) flip = cBest < 0.0;
                    // parabolic peak between the samples
                    double frac = 0.0;
                    if (bd > -maxLag && bd < maxLag)
                    {
                        const double sgn = flip ? -1.0 : 1.0;
                        const double y0 = sgn * cs[(size_t) (bd + maxLag - 1)], y1 = sgn * cs[(size_t) (bd + maxLag)], y2 = sgn * cs[(size_t) (bd + maxLag + 1)];
                        const double den = y0 - 2.0 * y1 + y2;
                        if (std::abs (den) > 1.0e-20) frac = std::clamp (0.5 * (y0 - y2) / den, -0.5, 0.5);
                    }
                    fine = -((double) bd + frac);     // matching at +d means the layer moves d earlier
                }
                else
                {
                    // little in common: no flip, and only a nudge small enough not to be heard as
                    // timing (0.3 ms), toward the most energy in the sum (the least cancelling)
                    if (pol == PolAuto) flip = r0 < -minLike;
                    const double sgn = flip ? -1.0 : 1.0;
                    const int narrow = std::min (maxLag, (int) (rate * 0.0003));
                    int nd = 0; double nv = sgn * corrAt (0);
                    for (int d = -narrow; d <= narrow; ++d) { const double v = sgn * corrAt (d); if (v > nv) { nv = v; nd = d; } }
                    fine = -(double) nd;
                }
            }
            else if (pol == PolAuto) flip = r0 < -minLike;
            inv[(size_t) j] = flip;
            start[(size_t) j] += fine;
            st->info[(size_t) j].fineMs = (float) (fine / rate * 1000.0);
            // add it to what the next layer aligns against (nearest whole sample is plenty here)
            const int di = (int) std::lround (-fine);
            for (int n = 0; n < win; ++n)
            {
                const int k = n + maxLag + di;
                if (k >= 0 && k < (int) bj.size()) acc[(size_t) n] += (flip ? -1.0f : 1.0f) * bj[(size_t) k];
            }
        }
    }
    for (int i = 0; i < kLayers; ++i)
    {
        if (! L[(size_t) i].active) continue;
        start[(size_t) i] += (double) p.layer[(size_t) i].nudgeMs * 0.001 * rate;
        start[(size_t) i] = std::max (0.0, start[(size_t) i]);
        st->info[(size_t) i].shiftMs = (float) ((start[(size_t) i] - rawStart) / rate * 1000.0);
        st->info[(size_t) i].inverted = inv[(size_t) i];
    }

    // ---- 4. the mix
    int frames = 0;
    for (int i = 0; i < kLayers; ++i)
        if (L[(size_t) i].active) frames = std::max (frames, (int) std::ceil (start[(size_t) i]) + (int) L[(size_t) i].l.size() + 2);
    frames = std::max (frames, T + (int) (rate * 0.05));
    st->frames = frames;
    // the hit starts at the earliest placed attack (= T once aligned; sooner when OFF)
    int first = T;
    for (int i = 0; i < kLayers; ++i)
        if (L[(size_t) i].active) first = std::min (first, (int) std::floor (start[(size_t) i]) + L[(size_t) i].onset);
    st->onset = std::max (0, first);
    st->rate = rate;
    st->mixL.assign ((size_t) frames, 0.0f);
    st->mixR.assign ((size_t) frames, 0.0f);
    std::array<std::vector<float>, kLayers> placedMono;
    for (int i = 0; i < kLayers; ++i)
    {
        auto& ly = L[(size_t) i];
        if (! ly.active) continue;
        const int whole = (int) std::floor (start[(size_t) i]);
        const double frac = start[(size_t) i] - whole;
        auto l = ly.l, r = ly.r;
        fracShift (l, frac);
        fracShift (r, frac);
        const float sg = inv[(size_t) i] ? -1.0f : 1.0f;
        placedMono[(size_t) i].assign ((size_t) frames, 0.0f);
        for (size_t n = 0; n < l.size(); ++n)
        {
            const size_t k = (size_t) whole + n;
            if (k >= (size_t) frames) break;
            st->mixL[k] += sg * l[n];
            st->mixR[k] += sg * r[n];
            placedMono[(size_t) i][k] = sg * 0.5f * (l[n] + r[n]);
        }
    }

    // ---- 5. what the alignment did: cancellation per band, correlation per layer, the views
    {
        const int from = T - pre, len = (int) (rate * 0.06);
        constexpr int l2 = 12;
        gtbs::Fft fft (l2);
        const int N = fft.size();
        auto spectrumAt = [&] (const std::vector<float>& x, int at, bool flip)
        {
            cvec X ((size_t) N);
            for (int n = 0; n < N; ++n)
            {
                const int j = at + n;
                const float w = n < len ? (n < 48 ? (float) n / 48.0f : 1.0f) * (n > len - 96 ? (float) (len - n) / 96.0f : 1.0f) : 0.0f;
                X[(size_t) n] = { (n < len && j >= 0 && j < (int) x.size()) ? x[(size_t) j] * w * (flip ? -1.0f : 1.0f) : 0.0f, 0.0f };
            }
            fft.forward (X);
            return X;
        };
        std::array<cvec, kLayers> rawX, alX;
        for (int i = 0; i < kLayers; ++i)
        {
            if (! L[(size_t) i].active) continue;
            // raw: the file as loaded, starting with the others, no polarity change
            rawX[(size_t) i] = spectrumAt (L[(size_t) i].mono, from - rawStart, false);
            alX[(size_t) i] = spectrumAt (placedMono[(size_t) i], from, false);
        }
        auto cancel = [&] (const std::array<cvec, kLayers>& X, std::array<float, kBands>& out)
        {
            for (int b = 0; b < kBands; ++b)
            {
                const int k0 = std::max (1, (int) (bandLo (b) * N / rate)), k1 = std::min (N / 2, (int) (bandHi (b) * N / rate));
                double es = 0.0, ei = 1.0e-20;
                for (int k = k0; k <= k1; ++k)
                {
                    std::complex<float> s (0.0f, 0.0f);
                    for (int i = 0; i < kLayers; ++i)
                        if (L[(size_t) i].active) { s += X[(size_t) i][(size_t) k]; ei += std::norm (X[(size_t) i][(size_t) k]); }
                    es += std::norm (s);
                }
                out[(size_t) b] = nActive > 1 ? (float) (10.0 * std::log10 ((es + 1.0e-20) / ei)) : 0.0f;
            }
        };
        cancel (rawX, st->cancelRaw);
        cancel (alX, st->cancelAligned);
        auto corr = [&] (const std::array<cvec, kLayers>& X, int i)
        {
            const int k0 = (int) (80.0 * N / rate), k1 = (int) (1200.0 * N / rate);
            double num = 0.0, ex = 1.0e-20, ey = 1.0e-20;
            for (int k = k0; k <= k1; ++k)
            {
                std::complex<float> y (0.0f, 0.0f);
                for (int j = 0; j < kLayers; ++j) if (j != i && L[(size_t) j].active) y += X[(size_t) j][(size_t) k];
                num += (double) (X[(size_t) i][(size_t) k] * std::conj (y)).real();
                ex += std::norm (X[(size_t) i][(size_t) k]);
                ey += std::norm (y);
            }
            return (float) (num / std::sqrt (ex * ey));
        };
        for (int i = 0; i < kLayers; ++i)
        {
            if (! L[(size_t) i].active || nActive < 2) continue;
            st->info[(size_t) i].corrRaw = corr (rawX, i);
            st->info[(size_t) i].corr = corr (alX, i);
        }
        // views: 2 ms before the attack, 30 ms after
        int minOn = 1 << 30;
        for (auto& ly : L) if (ly.active) minOn = std::min (minOn, ly.onset);
        for (int i = 0; i < kLayers; ++i)
        {
            st->viewRaw[(size_t) i].assign ((size_t) kViewLen, 0.0f);
            st->viewAligned[(size_t) i].assign ((size_t) kViewLen, 0.0f);
            if (! L[(size_t) i].active) continue;
            const int r0 = minOn - (int) (rate * 0.002);
            const int a0 = T - (int) (rate * 0.002);
            for (int n = 0; n < kViewLen; ++n)
            {
                const int jr = r0 + n;
                if (jr >= 0 && jr < (int) L[(size_t) i].mono.size()) st->viewRaw[(size_t) i][(size_t) n] = L[(size_t) i].mono[(size_t) jr];
                const int ja = a0 + n;
                if (ja >= 0 && ja < frames) st->viewAligned[(size_t) i][(size_t) n] = placedMono[(size_t) i][(size_t) ja];
            }
        }
    }

    // ---- 6. one loudness, whatever the layers add up to
    if (p.normalize)
    {
        float pk = 1.0e-9f;
        for (int n = 0; n < frames; ++n) pk = std::max ({ pk, std::abs (st->mixL[(size_t) n]), std::abs (st->mixR[(size_t) n]) });
        const float g = 0.5f / pk;
        for (int n = 0; n < frames; ++n) { st->mixL[(size_t) n] *= g; st->mixR[(size_t) n] *= g; }
        st->normDb = 20.0f * std::log10 (g);
    }

    // ---- 7. crack / body / tail: complementary in frequency and in time, so they sum back exactly
    {
        auto lowOf = [&] (const std::vector<float>& x)
        {
            auto lo = x;
            const double fc = std::clamp ((double) p.xfreq, 200.0, 16000.0);
            zeroPhase (lo, rate, [fc] (double hz) { return (float) (1.0 / std::sqrt (1.0 + std::pow (hz / fc, 8.0))); });
            return lo;
        };
        const double cMs = std::max (1.0, (double) p.crackMs), bMs = std::max (5.0, (double) p.bodyMs);
        auto window = [&] (int n, double holdMs, double fadeMs)
        {
            const double t = (double) (n - st->onset) / rate * 1000.0;
            if (t <= holdMs) return 1.0f;
            if (t >= holdMs + fadeMs) return 0.0f;
            return (float) (0.5 + 0.5 * std::cos (kPi * (t - holdMs) / fadeMs));
        };
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto& mix = ch == 0 ? st->mixL : st->mixR;
            auto& C = ch == 0 ? st->cL : st->cR;
            const auto lo = lowOf (mix);
            for (auto& v : C) v.assign ((size_t) frames, 0.0f);
            for (int n = 0; n < frames; ++n)
            {
                const float hi = mix[(size_t) n] - lo[(size_t) n];
                const float c = hi * window (n, cMs, cMs);
                const float b = lo[(size_t) n] * window (n, bMs, bMs * 0.6);
                C[Crack][(size_t) n] = c;
                C[Body][(size_t) n] = b;
                C[Tail][(size_t) n] = mix[(size_t) n] - c - b;
            }
        }
        double e[NumComps] {}, tot = 1.0e-20;
        for (int k = 0; k < NumComps; ++k)
            for (int n = 0; n < frames; ++n) e[k] += (double) st->cL[(size_t) k][(size_t) n] * st->cL[(size_t) k][(size_t) n] + (double) st->cR[(size_t) k][(size_t) n] * st->cR[(size_t) k][(size_t) n];
        for (double v : e) tot += v;
        for (int k = 0; k < NumComps; ++k) st->share[(size_t) k] = (float) (e[k] / tot);
    }
    return st;
}

} // namespace ss
