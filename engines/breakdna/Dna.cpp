// semi-rack: vendored; namespaces wrapped in dnafam:: (see sources.mk for every change).
#include "Dna.h"
#include "Dsp.h"
#include "Fft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>

namespace dnafam::dna
{

using gtbs::kPi;

//==============================================================================
// Genome
int Genome::hitOf (int step, int c) const
{
    if (step < 0 || step >= steps) return -1;
    int best = -1;
    for (int i = 0; i < (int) hits.size(); ++i)
        if (hits[(size_t) i].step == step && hits[(size_t) i].cls == c
            && (best < 0 || hits[(size_t) i].level > hits[(size_t) best].level))
            best = i;
    return best;
}

float Genome::offsetFor (int step, int c) const
{
    step = ((step % steps) + steps) % steps;
    int h = hitOf (step, c);
    if (h < 0 && c == Ghost) h = hitOf (step, Snare);
    if (h < 0 && c == Snare) h = hitOf (step, Ghost);
    if (h < 0) h = main[(size_t) step];
    if (h >= 0) return hits[(size_t) h].offset;
    const int p = step & 3;
    if (profCount[c][p] > 0) return profOffset[c][p];
    float acc = 0.0f; int n = 0;
    for (int k = 0; k < NumCls; ++k) { acc += profOffset[k][p] * (float) profCount[k][p]; n += profCount[k][p]; }
    return n > 0 ? acc / (float) n : 0.0f;
}

float Genome::levelFor (int step, int c) const
{
    step = ((step % steps) + steps) % steps;
    int h = hitOf (step, c);
    if (h < 0 && c == Ghost) h = hitOf (step, Snare);
    if (h < 0 && c == Snare) h = hitOf (step, Ghost);
    if (h >= 0) return hits[(size_t) h].level;
    const int p = step & 3;
    if (profCount[c][p] > 0) return profLevel[c][p];
    return meanLevel[c] > 0.0f ? meanLevel[c] : 0.7f;
}

namespace
{
float median (std::vector<float> v)
{
    if (v.empty()) return 0.0f;
    std::sort (v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5f * (v[n / 2 - 1] + v[n / 2]);
}
} // namespace

std::vector<int> attacks (const Sample& s, const SliceMap& m)
{
    std::vector<int> out ((size_t) m.count());
    if (s.frames == 0) return out;
    std::vector<float> x ((size_t) s.frames);
    for (int i = 0; i < s.frames; ++i)
        x[(size_t) i] = std::abs (0.5f * (s.l[(size_t) i] + (s.r.empty() ? s.l[(size_t) i] : s.r[(size_t) i])));
    const int w = std::max (4, (int) (s.rate * 0.001));          // 1 ms trailing peak hold
    for (int i = 0; i < m.count(); ++i)
    {
        const int st = m.starts[(size_t) i];
        const int nx = i + 1 < m.count() ? m.starts[(size_t) i + 1] : s.frames;
        const int lo = std::max (i > 0 ? m.starts[(size_t) i - 1] + (int) (s.rate * 0.006) : 0, st - (int) (s.rate * 0.015));
        const int hi = std::min (nx, st + (int) (s.rate * 0.03));
        if (hi - lo < 2 * w) { out[(size_t) i] = st; continue; }
        // trailing peak envelope over [lo, hi)
        std::vector<float> e ((size_t) (hi - lo));
        for (int n = lo; n < hi; ++n)
        {
            float pk = 0.0f;
            for (int k = std::max (0, n - w + 1); k <= n; ++k) pk = std::max (pk, x[(size_t) k]);
            e[(size_t) (n - lo)] = pk;
        }
        int pkAt = 0;
        for (int n = 0; n < hi - lo; ++n) if (e[(size_t) n] > e[(size_t) pkAt]) pkAt = n;
        float floorV = e[(size_t) pkAt];
        for (int n = 0; n <= pkAt; ++n) floorV = std::min (floorV, e[(size_t) n]);
        const float thr = floorV + 0.2f * (e[(size_t) pkAt] - floorV);
        int a = pkAt;
        while (a > 0 && e[(size_t) a - 1] >= thr) --a;
        out[(size_t) i] = std::clamp (lo + a, 0, std::max (0, nx - 1));
    }
    return out;
}

Genome extract (const Sample& s, const SliceMap& m, int bars, const std::vector<int>& attack)
{
    Genome g;
    g.bars = std::clamp (bars, 1, kMaxSteps / 16);
    g.steps = g.bars * 16;
    g.rate = s.rate;
    g.framesPerStep = std::max (1.0, (double) s.frames / (double) g.steps);
    g.bpm = (double) g.bars * 240.0 / std::max (1.0e-3, (double) s.frames / s.rate);
    g.main.fill (-1);
    g.mask.fill (0);

    struct Raw { double pos; int slice; };
    std::vector<Raw> raw;
    for (int i = 0; i < m.count(); ++i)
        raw.push_back ({ (double) attack[(size_t) i] / g.framesPerStep, i });

    // the grid sits where the loud beats are: their median deviation is the file's own lag
    // (the kicks on the beat first: the drummer's "one"; else every loud hit on a beat)
    std::vector<float> kicks, beats, loud;
    for (auto& r : raw)
    {
        const int step = (int) std::lround (r.pos);
        const float off = (float) (r.pos - step);
        const auto c = m.cls[(size_t) r.slice];
        const float lv = m.level[(size_t) r.slice];
        if (lv > 0.45f) loud.push_back (off);
        if (lv > 0.45f && (step & 3) == 0 && (c == Kick || c == Snare)) beats.push_back (off);
        if (lv > 0.45f && (step & 3) == 0 && c == Kick) kicks.push_back (off);
    }
    if (kicks.size() >= 2) g.bias = median (kicks);
    else if (beats.size() >= 2) g.bias = median (beats);
    else if (loud.size() > 2) g.bias = median (loud);
    g.bias = std::clamp (g.bias, -0.35f, 0.35f);

    for (auto& r : raw)
    {
        const double p = r.pos - g.bias;
        int step = (int) std::lround (p);
        float off = (float) (p - step);
        off = std::clamp (off, -0.5f, 0.4999f);
        step = ((step % g.steps) + g.steps) % g.steps;
        Hit h;
        h.slice = r.slice;
        h.frame = attack[(size_t) r.slice];
        h.step = step;
        h.offset = off;
        h.cls = m.cls[(size_t) r.slice];
        h.level = m.level[(size_t) r.slice];
        g.hits.push_back (h);
    }
    std::sort (g.hits.begin(), g.hits.end(), [] (const Hit& a, const Hit& b) { return a.step + a.offset < b.step + b.offset; });

    float sum[NumCls] {}; int cnt[NumCls] {};
    double onSum = 0, offSum = 0; int onN = 0, offN = 0;
    for (int i = 0; i < (int) g.hits.size(); ++i)
    {
        const auto& h = g.hits[(size_t) i];
        g.mask[(size_t) h.step] |= (uint8_t) (1u << h.cls);
        auto& mn = g.main[(size_t) h.step];
        if (mn < 0 || h.level > g.hits[(size_t) mn].level) mn = i;
        const int p = h.step & 3;
        g.profOffset[h.cls][p] += h.offset;
        g.profLevel[h.cls][p] += h.level;
        g.profCount[h.cls][p]++;
        sum[h.cls] += h.level; cnt[h.cls]++;
        if (h.level > 0.15f)
        {
            if (p == 1 || p == 3) { offSum += h.offset; ++offN; }
            else                  { onSum += h.offset; ++onN; }
        }
    }
    for (int c = 0; c < NumCls; ++c)
    {
        for (int p = 0; p < 4; ++p)
            if (g.profCount[c][p] > 0)
            {
                g.profOffset[c][p] /= (float) g.profCount[c][p];
                g.profLevel[c][p] /= (float) g.profCount[c][p];
            }
        g.meanLevel[c] = cnt[c] > 0 ? sum[c] / (float) cnt[c] : 0.0f;
    }
    g.swing = (float) ((offN > 0 ? offSum / offN : 0.0) - (onN > 0 ? onSum / onN : 0.0));
    return g;
}

//==============================================================================
// Features and matching
namespace
{
std::vector<float> mono (const Sample& s)
{
    std::vector<float> x ((size_t) s.frames);
    for (int i = 0; i < s.frames; ++i)
        x[(size_t) i] = 0.5f * (s.l[(size_t) i] + (s.r.empty() ? s.l[(size_t) i] : s.r[(size_t) i]));
    return x;
}
} // namespace

std::vector<Features> features (const Sample& s, const SliceMap& m)
{
    std::vector<Features> out ((size_t) m.count());
    if (m.count() == 0 || s.frames == 0) return out;
    const auto x = mono (s);
    constexpr int log2N = 11, N = 1 << log2N;
    gtbs::Fft fft (log2N);
    std::vector<std::complex<float>> buf ((size_t) N);
    for (int i = 0; i < m.count(); ++i)
    {
        const int st = m.starts[(size_t) i];
        const int len = std::max (32, std::min (m.lengthOf (i, s.frames), (int) (s.rate * 0.09)));
        Features f;
        float pk = 0.0f; int pkAt = 0;
        const int full = m.lengthOf (i, s.frames);
        for (int k = 0; k < full && st + k < s.frames; ++k)
            if (std::abs (x[(size_t) (st + k)]) > pk) { pk = std::abs (x[(size_t) (st + k)]); pkAt = k; }
        f.level = pk;
        // decay: first time after the peak the 5 ms RMS stays under -20 dB of the peak
        {
            const int w = std::max (8, (int) (s.rate * 0.005));
            f.decay = (float) full / (float) s.rate;
            for (int k = pkAt; k + w < full && st + k + w < s.frames; k += w / 2)
            {
                double e = 0.0;
                for (int q = 0; q < w; ++q) e += (double) x[(size_t) (st + k + q)] * x[(size_t) (st + k + q)];
                if (std::sqrt (e / w) < pk * 0.1f) { f.decay = (float) (k - pkAt) / (float) s.rate; break; }
            }
        }
        for (int k = 0; k < N; ++k)
        {
            float v = 0.0f;
            if (k < len && st + k < s.frames)
                v = x[(size_t) (st + k)] * (0.5f + 0.5f * std::cos ((float) kPi * (float) k / (float) len));
            buf[(size_t) k] = { v, 0.0f };
        }
        fft.forward (buf);
        double tot = 0.0, cen = 0.0;
        for (int k = 1; k < N / 2; ++k)
        {
            const double hz = (double) k * s.rate / N;
            const float e = std::norm (buf[(size_t) k]);
            if (hz < 30.0) continue;
            if (hz < 160.0) f.low += e;
            else if (hz < 1000.0) f.body += e;
            else if (hz < 5000.0) f.crack += e;
            else if (hz < 16000.0) f.air += e;
            tot += e;
            cen += e * std::log2 (hz);
        }
        const float t = std::max (1.0e-12f, f.low + f.body + f.crack + f.air);
        f.low /= t; f.body /= t; f.crack /= t; f.air /= t;
        f.centroid = tot > 0.0 ? (float) (cen / tot) : 10.0f;
        out[(size_t) i] = f;
    }
    float mx = 1.0e-9f;
    for (auto& f : out) mx = std::max (mx, f.level);
    for (auto& f : out) f.level /= mx;
    return out;
}

std::vector<int> matchSlices (const SliceMap& a, const std::vector<Features>& fa, int framesA,
                              const SliceMap& b, const std::vector<Features>& fb, int framesB,
                              int mode, uint32_t seed)
{
    std::vector<int> out ((size_t) a.count(), 0);
    if (b.count() == 0) return out;

    auto relPos = [] (const SliceMap& m, int i, int frames) { return (double) m.starts[(size_t) i] / std::max (1, frames); };
    auto classNear = [] (int ca, int cb)
    {
        if (ca == cb) return 0.0f;
        if ((ca == Snare && cb == Ghost) || (ca == Ghost && cb == Snare)) return 0.6f;
        if ((ca == Ghost && cb == Hat) || (ca == Hat && cb == Ghost)) return 0.9f;
        return 2.0f;
    };

    for (int i = 0; i < a.count(); ++i)
    {
        const int ca = a.cls[(size_t) i];
        const auto& f = fa[(size_t) i];
        const double pa = relPos (a, i, framesA);
        if (mode == MatchPosition)
        {
            int best = 0; double bd = 1e9;
            for (int j = 0; j < b.count(); ++j)
            {
                double d = std::abs (relPos (b, j, framesB) - pa);
                d = std::min (d, 1.0 - d);
                if (d < bd) { bd = d; best = j; }
            }
            out[(size_t) i] = best;
            continue;
        }
        if (mode == MatchRandom)
        {
            std::vector<int> same;
            for (int j = 0; j < b.count(); ++j) if (b.cls[(size_t) j] == ca) same.push_back (j);
            if (same.empty()) for (int j = 0; j < b.count(); ++j) same.push_back (j);
            out[(size_t) i] = same[gtbs::hash32 (seed, (uint32_t) i, 77u) % same.size()];
            continue;
        }
        // by class: nearest in sound, the class first, then where in the bar it sits
        int best = 0; double bd = 1e9;
        for (int j = 0; j < b.count(); ++j)
        {
            const auto& g = fb[(size_t) j];
            double d = classNear (ca, b.cls[(size_t) j]);
            d += std::abs (f.low - g.low) + std::abs (f.body - g.body) + std::abs (f.crack - g.crack) + std::abs (f.air - g.air);
            d += 0.15 * std::abs (f.centroid - g.centroid);
            d += 0.5 * std::abs (f.level - g.level);
            double dp = std::abs (relPos (b, j, framesB) - pa);
            dp = std::min (dp, 1.0 - dp);
            d += 0.8 * dp;
            // a little seeded jitter so equal candidates do not always resolve the same way
            d += 0.02 * (double) (gtbs::hash32 (seed, (uint32_t) i, (uint32_t) j) & 1023) / 1023.0;
            if (d < bd) { bd = d; best = j; }
        }
        out[(size_t) i] = best;
    }
    return out;
}

//==============================================================================
// Spectral blend
namespace
{
void inverse (const gtbs::Fft& fft, std::vector<std::complex<float>>& x)
{
    for (auto& v : x) v = std::conj (v);
    fft.forward (x);
    const float inv = 1.0f / (float) fft.size();
    for (auto& v : x) v = std::conj (v) * inv;
}

// RMS envelope over a centred window
std::vector<float> envelope (const std::vector<float>& x, int win)
{
    const int n = (int) x.size();
    std::vector<double> c ((size_t) n + 1, 0.0);
    for (int i = 0; i < n; ++i) c[(size_t) i + 1] = c[(size_t) i] + (double) x[(size_t) i] * x[(size_t) i];
    std::vector<float> e ((size_t) n);
    const int h = win / 2;
    for (int i = 0; i < n; ++i)
    {
        const int a = std::max (0, i - h), b = std::min (n, i + h + 1);
        e[(size_t) i] = (float) std::sqrt ((c[(size_t) b] - c[(size_t) a]) / std::max (1, b - a));
    }
    return e;
}

// all morph layers of one channel in one pass: the analysis of a and b is shared
std::vector<std::vector<float>> blendMany (const std::vector<float>& a, const std::vector<float>& b,
                                           const std::vector<float>& ms, float env, double rate)
{
    const int n = (int) a.size();
    std::vector<std::vector<float>> out (ms.size(), std::vector<float> ((size_t) n, 0.0f));
    if (n == 0) return out;

    // 512 points at 44.1 kHz (11.6 ms): short enough to keep the hits sharp
    const int log2N = rate > 70000.0 ? 10 : 9;
    const int N = 1 << log2N, H = N / 4;
    gtbs::Fft fft (log2N);
    std::vector<float> win ((size_t) N);
    for (int k = 0; k < N; ++k) win[(size_t) k] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * k / N));
    const float olaNorm = 1.0f / 1.5f;      // sum of Hann^2 at a quarter-window hop

    const int padded = n + 2 * N;
    std::vector<std::vector<float>> acc (ms.size(), std::vector<float> ((size_t) padded, 0.0f));
    std::vector<std::complex<float>> A ((size_t) N), B ((size_t) N), Y ((size_t) N);

    for (int start = -N; start < n + N; start += H)
    {
        bool any = false;
        for (int k = 0; k < N; ++k)
        {
            const int i = start + k;
            const float va = (i >= 0 && i < n) ? a[(size_t) i] : 0.0f;
            const float vb = (i >= 0 && i < n) ? b[(size_t) i] : 0.0f;
            any = any || std::abs (va) + std::abs (vb) > 0.0f;
            A[(size_t) k] = { va * win[(size_t) k], 0.0f };
            B[(size_t) k] = { vb * win[(size_t) k], 0.0f };
        }
        if (! any) continue;
        fft.forward (A);
        fft.forward (B);
        float peak = 1.0e-9f;
        for (int k = 0; k <= N / 2; ++k) peak = std::max ({ peak, std::abs (A[(size_t) k]), std::abs (B[(size_t) k]) });
        const float eps = peak * 1.0e-3f;

        for (size_t li = 0; li < ms.size(); ++li)
        {
            const float m = ms[li];
            for (int k = 0; k <= N / 2; ++k)
            {
                const float ma = std::abs (A[(size_t) k]), mb = std::abs (B[(size_t) k]);
                // geometric blend of the magnitudes: the timbre of both at once, not a crossfade
                const float mag = std::exp ((1.0f - m) * std::log (ma + eps) + m * std::log (mb + eps)) - eps;
                const auto& ph = (1.0f - m) * ma >= m * mb ? A[(size_t) k] : B[(size_t) k];
                const float pm = std::abs (ph);
                Y[(size_t) k] = pm > 1.0e-12f ? ph * (std::max (0.0f, mag) / pm) : std::complex<float> (0.0f, 0.0f);
                if (k > 0 && k < N / 2) Y[(size_t) (N - k)] = std::conj (Y[(size_t) k]);
            }
            inverse (fft, Y);
            auto& o = acc[li];
            for (int k = 0; k < N; ++k) o[(size_t) (start + k + N)] += Y[(size_t) k].real() * win[(size_t) k] * olaNorm;
        }
    }
    for (size_t li = 0; li < ms.size(); ++li)
        for (int i = 0; i < n; ++i) out[li][(size_t) i] = acc[li][(size_t) (i + N)];

    // envelope transfer: the blend keeps the attack shape of the interpolated envelopes
    if (env > 0.0f)
    {
        const int w = std::max (8, (int) (rate * 0.0015));
        const auto ea = envelope (a, w), eb = envelope (b, w);
        float floorV = 1.0e-5f;
        for (int i = 0; i < n; ++i) floorV = std::max (floorV, std::max (ea[(size_t) i], eb[(size_t) i]) * 1.0e-3f);
        for (size_t li = 0; li < ms.size(); ++li)
        {
            const float m = ms[li];
            const auto ey = envelope (out[li], w);
            std::vector<float> gain ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const float target = std::exp ((1.0f - m) * std::log (ea[(size_t) i] + floorV) + m * std::log (eb[(size_t) i] + floorV)) - floorV;
                const float gg = std::clamp ((target + floorV) / (ey[(size_t) i] + floorV), 0.25f, 4.0f);
                gain[(size_t) i] = 1.0f + env * (gg - 1.0f);
            }
            // smooth the gain both ways (0.7 ms) so it shapes, never buzzes
            const float c = (float) std::exp (-1.0 / (rate * 0.0007));
            float z = gain[0];
            for (int i = 0; i < n; ++i) { z = gain[(size_t) i] + c * (z - gain[(size_t) i]); gain[(size_t) i] = z; }
            z = gain[(size_t) n - 1];
            for (int i = n - 1; i >= 0; --i) { z = gain[(size_t) i] + c * (z - gain[(size_t) i]); gain[(size_t) i] = z; }
            for (int i = 0; i < n; ++i) out[li][(size_t) i] *= gain[(size_t) i];
        }
    }
    return out;
}
} // namespace

std::vector<float> blend (const std::vector<float>& a, const std::vector<float>& b, float m, float env, double rate)
{
    if (m <= 0.0f) return a;
    if (m >= 1.0f && env <= 0.0f) return b;
    return blendMany (a, b, { m }, env, rate)[0];
}

std::array<float, kSpecBins> spectrum (const float* x, int n, double rate)
{
    std::array<float, kSpecBins> out {};
    constexpr int log2N = 11, N = 1 << log2N;
    gtbs::Fft fft (log2N);
    std::vector<std::complex<float>> buf ((size_t) N);
    const int len = std::min (n, N);
    for (int k = 0; k < N; ++k)
    {
        const float w = k < len ? 0.5f + 0.5f * std::cos ((float) kPi * (float) k / (float) std::max (1, len)) : 0.0f;
        buf[(size_t) k] = { k < len ? x[k] * w : 0.0f, 0.0f };
    }
    fft.forward (buf);
    for (int b = 0; b < kSpecBins; ++b)
    {
        const double lo = specBinHz (b) / std::pow (400.0, 0.5 / (kSpecBins - 1));
        const double hi = specBinHz (b) * std::pow (400.0, 0.5 / (kSpecBins - 1));
        const int k0 = std::max (1, (int) std::floor (lo * N / rate)), k1 = std::max (k0, (int) std::ceil (hi * N / rate));
        double e = 0.0;
        for (int k = k0; k <= k1 && k < N / 2; ++k) e = std::max (e, (double) std::norm (buf[(size_t) k]));
        out[(size_t) b] = (float) (10.0 * std::log10 (e + 1.0e-12));
    }
    return out;
}

//==============================================================================
// Kit
int Kit::sliceFor (int step, int c) const
{
    const int s = ((step % g.steps) + g.steps) % g.steps;
    int h = g.hitOf (s, c);
    if (h < 0 && c == Ghost) h = g.hitOf (s, Snare);
    if (h < 0 && c == Snare) h = g.hitOf (s, Ghost);
    if (h >= 0) return g.hits[(size_t) h].slice;
    // nearest hit of the class around the loop
    int best = -1, bd = 1 << 30;
    for (const auto& hit : g.hits)
    {
        const bool ok = hit.cls == c || (c == Ghost && hit.cls == Snare) || (c == Snare && hit.cls == Ghost);
        if (! ok) continue;
        int d = std::abs (hit.step - s);
        d = std::min (d, g.steps - d);
        // a ghost should be a quiet one, a snare a loud one
        if (c == Snare && hit.cls == Ghost) d += 4;
        if (c == Ghost && hit.cls == Snare) d += 4;
        if (d < bd) { bd = d; best = hit.slice; }
    }
    if (best >= 0) return best;
    return g.main[(size_t) s] >= 0 ? g.hits[(size_t) g.main[(size_t) s]].slice : 0;
}

namespace
{
// Continues b past `avail` (its own slice ended) up to `len`: noise with the spectrum of
// b's last frame, at b's last level, following a's envelope from there on, crossfaded in.
void extendTail (std::vector<float>& b, const std::vector<float>& a, int avail, int len, double rate, uint32_t seed)
{
    if (avail >= len) return;
    int log2N = 10;
    while (log2N > 7 && (1 << log2N) > std::max (avail, 1)) --log2N;
    const int N = 1 << log2N, H = N / 4;
    if (avail < N / 2)
    {
        // too little of B to continue: a short fade is all it gets
        const int f = std::min (avail, (int) (rate * 0.003));
        for (int n = 0; n < f; ++n) b[(size_t) (avail - 1 - n)] *= (float) n / (float) std::max (1, f);
        return;
    }
    gtbs::Fft fft (log2N);
    std::vector<float> win ((size_t) N);
    for (int k = 0; k < N; ++k) win[(size_t) k] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * k / N));
    // the colour: the magnitude spectrum of B's last frame
    std::vector<std::complex<float>> X ((size_t) N);
    const int from = std::max (0, avail - N);
    for (int k = 0; k < N; ++k)
    {
        const int i = from + k;
        X[(size_t) k] = { i < avail ? b[(size_t) i] * win[(size_t) k] : 0.0f, 0.0f };
    }
    fft.forward (X);
    std::vector<float> mag ((size_t) N / 2 + 1);
    for (int k = 0; k <= N / 2; ++k) mag[(size_t) k] = std::abs (X[(size_t) k]);
    double eb = 0.0;
    const int tailFrom = std::max (0, avail - N / 2);
    for (int i = tailFrom; i < avail; ++i) eb += (double) b[(size_t) i] * b[(size_t) i];
    const float rmsB = (float) std::sqrt (eb / std::max (1, avail - tailFrom));

    // the continuation: random-phase frames overlap-added, then normalised
    const int xf = std::min ({ avail / 2, (int) (rate * 0.012), N });
    const int s0 = avail - xf, count = len - s0;
    std::vector<float> s ((size_t) count + (size_t) N, 0.0f);
    uint32_t r = seed * 2654435761u + 7u;
    for (int at = -N; at < count; at += H)
    {
        for (int k = 0; k <= N / 2; ++k)
        {
            r ^= r << 13; r ^= r >> 17; r ^= r << 5;
            const float ph = (float) (r & 0xFFFF) / 65535.0f * (float) (2.0 * kPi);
            X[(size_t) k] = std::polar (mag[(size_t) k], (k == 0 || k == N / 2) ? 0.0f : ph);
            if (k > 0 && k < N / 2) X[(size_t) (N - k)] = std::conj (X[(size_t) k]);
        }
        inverse (fft, X);
        for (int k = 0; k < N; ++k)
        {
            const int i = at + k;
            if (i >= 0 && i < count) s[(size_t) i] += X[(size_t) k].real() * win[(size_t) k];
        }
    }
    double es = 0.0;
    for (int i = 0; i < count; ++i) es += (double) s[(size_t) i] * s[(size_t) i];
    const float norm = rmsB / std::max (1.0e-9f, (float) std::sqrt (es / std::max (1, count)));

    // A's envelope from B's end on, relative to where B stopped
    const int w = std::max (8, (int) (rate * 0.005));
    const auto ea = envelope (a, w);
    const float ref = std::max (1.0e-6f, ea[(size_t) std::min (avail, (int) ea.size() - 1)]);
    float g = 1.0f;
    const float c = (float) std::exp (-1.0 / (rate * 0.004));
    for (int i = 0; i < count; ++i)
    {
        const int n = s0 + i;
        const float target = std::min (1.5f, ea[(size_t) std::min (n, (int) ea.size() - 1)] / ref);
        g = target + c * (g - target);
        const float v = s[(size_t) i] * norm * g;
        if (n < avail)
        {
            const float wv = (float) (i + 1) / (float) (xf + 1);
            b[(size_t) n] = b[(size_t) n] * (1.0f - wv) + v * wv;
        }
        else b[(size_t) n] = v;
    }
}
} // namespace

std::unique_ptr<Kit> buildKit (std::shared_ptr<const Sample> a, std::shared_ptr<const Sample> b, const KitParams& p)
{
    auto k = std::make_unique<Kit>();
    k->a = a;
    k->b = b;
    k->rate = a->rate;
    k->frames = a->frames;
    k->sa = gtbs::buildSlices (*a, gtbs::SliceOnsets);
    k->sb = gtbs::buildSlices (*b, gtbs::SliceOnsets);
    k->attack = attacks (*a, k->sa);
    // a slice the onset detector cut late would lose its attack: cut it just before the attack
    for (int i = 1; i < k->sa.count(); ++i)
    {
        const int want = k->attack[(size_t) i] - (int) (a->rate * 0.0005);
        k->sa.starts[(size_t) i] = std::clamp (want, k->sa.starts[(size_t) i - 1] + 1, k->sa.starts[(size_t) i]);
    }
    k->g = extract (*a, k->sa, gtbs::resolveBars (*a, p.barsA, 0.0), k->attack);
    k->gb = extract (*b, k->sb, gtbs::resolveBars (*b, p.barsB, 0.0));
    k->fa = features (*a, k->sa);
    k->fb = features (*b, k->sb);
    k->match = matchSlices (k->sa, k->fa, a->frames, k->sb, k->fb, b->frames, p.match, p.seed);

    // B at A's clock, pitched
    const double pf = std::pow (2.0, (double) p.bPitch / 12.0);
    const double inRate = b->rate * pf;
    const double scale = a->rate / inRate;                  // B frames -> blend frames
    std::vector<float> bL, bR;
    if (std::abs (scale - 1.0) < 1.0e-9) { bL = b->l; bR = b->r.empty() ? b->l : b->r; }
    else
    {
        bL = gtbs::resampleSinc (b->l, inRate, a->rate);
        bR = b->r.empty() ? bL : gtbs::resampleSinc (b->r, inRate, a->rate);
    }
    const int bFrames = (int) bL.size();
    const auto& aL = a->l;
    const auto& aR = a->r.empty() ? a->l : a->r;

    for (int l = 0; l < kLayers; ++l)
    {
        k->L[(size_t) l].assign ((size_t) a->frames, 0.0f);
        k->R[(size_t) l].assign ((size_t) a->frames, 0.0f);
    }
    std::copy (aL.begin(), aL.end(), k->L[0].begin());
    std::copy (aR.begin(), aR.end(), k->R[0].begin());

    const int fadeEnd = std::max (8, (int) (a->rate * 0.0015));
    const int maxLen = (int) (a->rate * 2.5);
    const std::vector<float> ms { 0.25f, 0.5f, 0.75f, 1.0f };
    k->specA.resize ((size_t) k->numSlices());
    k->specB.resize ((size_t) k->numSlices());

    for (int i = 0; i < k->numSlices(); ++i)
    {
        const int st = k->sa.starts[(size_t) i];
        const int len = std::min (k->sliceLength (i), maxLen);
        if (len <= 0) continue;
        const int j = k->match[(size_t) i];
        const int bs = std::clamp ((int) std::lround (k->sb.starts[(size_t) j] * scale), 0, std::max (0, bFrames - 1));
        const int bl = std::max (1, (int) std::lround (k->sb.lengthOf (j, b->frames) * scale));
        const int avail = std::min ({ bl, len, bFrames - bs });

        std::vector<float> sa ((size_t) len), sar ((size_t) len), sbv ((size_t) len, 0.0f), sbr ((size_t) len, 0.0f);
        for (int n = 0; n < len; ++n) { sa[(size_t) n] = aL[(size_t) (st + n)]; sar[(size_t) n] = aR[(size_t) (st + n)]; }
        for (int n = 0; n < avail; ++n) { sbv[(size_t) n] = bL[(size_t) (bs + n)]; sbr[(size_t) n] = bR[(size_t) (bs + n)]; }
        // B's slice ends sooner than A's: B's colour carries on, shaped like A's decay
        if (avail < len)
        {
            extendTail (sbv, sa, avail, len, a->rate, (uint32_t) i * 2u + 1u);
            extendTail (sbr, sar, avail, len, a->rate, (uint32_t) i * 2u + 2u);
        }
        // B plays at A's loudness: the dynamics are A's business
        {
            const int w = std::min (len, (int) (a->rate * 0.05));
            float pa = 1.0e-6f, pb = 1.0e-6f;
            for (int n = 0; n < w; ++n)
            {
                pa = std::max (pa, std::max (std::abs (sa[(size_t) n]), std::abs (sar[(size_t) n])));
                pb = std::max (pb, std::max (std::abs (sbv[(size_t) n]), std::abs (sbr[(size_t) n])));
            }
            const float gb = std::clamp (pa / pb, 0.05f, 20.0f);
            for (int n = 0; n < len; ++n) { sbv[(size_t) n] *= gb; sbr[(size_t) n] *= gb; }
        }

        auto yl = blendMany (sa, sbv, ms, p.env, a->rate);
        auto yr = blendMany (sar, sbr, ms, p.env, a->rate);
        for (int l = 1; l < kLayers; ++l)
        {
            auto& ol = k->L[(size_t) l];
            auto& orr = k->R[(size_t) l];
            auto& srcL = yl[(size_t) l - 1];
            auto& srcR = yr[(size_t) l - 1];
            for (int n = 0; n < len; ++n)
            {
                float g = 1.0f;
                if (n >= len - fadeEnd) g = (float) (len - n) / (float) fadeEnd;
                ol[(size_t) (st + n)] = srcL[(size_t) n] * g;
                orr[(size_t) (st + n)] = srcR[(size_t) n] * g;
            }
        }

        std::vector<float> ma ((size_t) len), mb ((size_t) len);
        for (int n = 0; n < len; ++n) { ma[(size_t) n] = 0.5f * (sa[(size_t) n] + sar[(size_t) n]); mb[(size_t) n] = 0.5f * (sbv[(size_t) n] + sbr[(size_t) n]); }
        k->specA[(size_t) i] = spectrum (ma.data(), len, a->rate);
        k->specB[(size_t) i] = spectrum (mb.data(), len, a->rate);
    }
    return k;
}

} // namespace dnafam::dna
