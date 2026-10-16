// semi-rack: vendored; namespaces wrapped in dnafam:: (see sources.mk for every change).
#include "Analysis.h"
#include "Dsp.h"
#include "Fft.h"

#include <algorithm>
#include <complex>
#include <numeric>

namespace dnafam::gtbs
{

namespace
{
std::vector<float> monoOf (const Sample& s)
{
    std::vector<float> m ((size_t) s.frames);
    for (int i = 0; i < s.frames; ++i)
        m[(size_t) i] = 0.5f * (s.l[(size_t) i] + (s.r.empty() ? s.l[(size_t) i] : s.r[(size_t) i]));
    return m;
}

inline float princarg (float a)
{
    // mod (theta + pi, 2 pi) - pi
    const float twoPi = (float) (2.0 * kPi);
    a = std::fmod (a + (float) kPi, twoPi);
    if (a < 0.0f) a += twoPi;
    return a - (float) kPi;
}

void normalise (std::vector<float>& v)
{
    if (v.empty()) return;
    std::vector<float> s (v);
    std::sort (s.begin(), s.end());
    // robust peak: the 99.5th percentile, so one freak frame does not flatten the rest
    const float p = std::max (1.0e-9f, s[(size_t) std::min ((int) s.size() - 1, (int) ((float) s.size() * 0.995f))]);
    for (auto& x : v) x = std::min (1.0f, x / p);
}

int nextPow2Log (int n) { int l = 0; while ((1 << l) < n) ++l; return l; }
} // namespace

//==============================================================================
OdfSet detectionFunctions (const Sample& s, float lambda)
{
    OdfSet out;
    const auto x = monoOf (s);
    const int hop = std::max (1, (int) std::lround (s.rate * 0.01));          // 10 ms frame step
    const int log2N = nextPow2Log ((int) std::lround (2048.0 * s.rate / 44100.0));
    const int N = 1 << log2N;                                                // 2048 at 44.1 kHz
    const int K = N / 2;
    const int frames = s.frames / hop + 1;
    out.hop = hop;
    for (auto& f : out.f) f.assign ((size_t) frames, 0.0f);

    Fft fft (log2N);
    std::vector<float> win ((size_t) N);
    for (int m = 0; m < N; ++m) win[(size_t) m] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * m / N));   // Hann

    std::vector<std::complex<float>> buf ((size_t) N);
    std::vector<float> mag ((size_t) K), mag1 ((size_t) K, 0.0f), ph ((size_t) K), ph1 ((size_t) K, 0.0f), ph2 ((size_t) K, 0.0f);

    for (int n = 0; n < frames; ++n)
    {
        // eq. 1.1, frame centred on n*h so the detection function lines up with the attack
        const int c = n * hop - N / 2;
        for (int m = 0; m < N; ++m)
        {
            const int i = c + m;
            buf[(size_t) m] = { i >= 0 && i < s.frames ? x[(size_t) i] * win[(size_t) m] : 0.0f, 0.0f };
        }
        fft.forward (buf);

        double sf = 0.0, wpd = 0.0, rcd = 0.0;
        for (int k = 0; k < K; ++k)
        {
            const float raw = std::abs (buf[(size_t) k]);
            mag[(size_t) k] = std::log (1.0f + lambda * raw);                     // eq. 1.9
            ph[(size_t) k] = std::arg (buf[(size_t) k]);

            // eq. 1.2: half-wave rectified magnitude rise
            const float d = mag[(size_t) k] - mag1[(size_t) k];
            sf += (d + std::abs (d)) * 0.5f;

            // eq. 1.3-1.4: second phase difference weighted by magnitude
            const float d2 = princarg (ph[(size_t) k] - 2.0f * ph1[(size_t) k] + ph2[(size_t) k]);
            wpd += mag[(size_t) k] * std::abs (d2);

            // eq. 1.5-1.8: distance to the predicted complex value, rising bins only
            if (mag[(size_t) k] >= mag1[(size_t) k])
            {
                const float predPhase = ph1[(size_t) k] + (ph1[(size_t) k] - ph2[(size_t) k]);
                const std::complex<float> X = std::polar (mag[(size_t) k], ph[(size_t) k]);
                const std::complex<float> Xh = std::polar (mag1[(size_t) k], predPhase);
                rcd += std::abs (X - Xh);
            }
        }
        out.f[OdfSF][(size_t) n]  = (float) sf;
        out.f[OdfWPD][(size_t) n] = (float) (wpd / (double) K);
        out.f[OdfRCD][(size_t) n] = (float) rcd;
        std::swap (ph2, ph1);
        ph1 = ph;
        mag1 = mag;
    }

    // ---- sub-band QMF tree: three stages, four octave bands -----------------
    {
        QmfStage q;
        std::vector<float> lo1, hi1, lo2, hi2, lo3, hi3;
        q.split (x, lo1, hi1);
        q.split (lo1, lo2, hi2);
        q.split (lo2, lo3, hi3);
        const std::vector<float>* bands[4] = { &lo3, &hi3, &hi2, &hi1 };
        const int decim[4] = { 8, 8, 4, 2 };
        std::vector<float> sum ((size_t) frames, 0.0f);
        for (int b = 0; b < 4; ++b)
        {
            const auto& v = *bands[b];
            const int bh = std::max (1, hop / decim[b]);
            std::vector<float> e ((size_t) frames, 0.0f), flux ((size_t) frames, 0.0f);
            for (int n = 0; n < frames; ++n)
            {
                const int c = n * hop / decim[b];
                double acc = 0.0; int cnt = 0;
                for (int i = c - bh; i < c + bh; ++i)
                    if (i >= 0 && i < (int) v.size()) { acc += (double) v[(size_t) i] * v[(size_t) i]; ++cnt; }
                const float rms = cnt > 0 ? (float) std::sqrt (acc / cnt) : 0.0f;
                e[(size_t) n] = std::log (1.0f + lambda * 100.0f * rms);
                if (n > 0)
                {
                    const float d = e[(size_t) n] - e[(size_t) n - 1];
                    flux[(size_t) n] = (d + std::abs (d)) * 0.5f;
                }
            }
            normalise (flux);
            const float weight = b == 0 ? 1.2f : 1.0f;                       // low band: kicks
            for (int n = 0; n < frames; ++n) sum[(size_t) n] += weight * flux[(size_t) n];
        }
        out.f[OdfQMF] = sum;
    }

    for (int i = 0; i < OdfHybrid; ++i) normalise (out.f[i]);
    out.f[OdfHybrid].resize ((size_t) frames);
    for (int n = 0; n < frames; ++n)
        out.f[OdfHybrid][(size_t) n] = 0.5f * (out.f[OdfRCD][(size_t) n] + out.f[OdfQMF][(size_t) n]);
    normalise (out.f[OdfHybrid]);
    return out;
}

//==============================================================================
std::vector<int> pickPeaks (const std::vector<float>& odf, std::vector<float>& thr, float alpha, float delta, int minGap)
{
    constexpr int M1 = 7, M2 = 3;                     // 70 ms look-back, 30 ms look-ahead
    const int n = (int) odf.size();
    thr.assign ((size_t) n, 0.0f);
    std::vector<float> w;
    w.reserve (M1 + M2 + 1);
    for (int i = 0; i < n; ++i)
    {
        w.clear();
        double mean = 0.0;
        for (int m = -M1; m <= M2; ++m)
        {
            const int j = std::clamp (i + m, 0, n - 1);
            w.push_back (odf[(size_t) j]);
            mean += odf[(size_t) j];
        }
        mean /= (double) (M1 + M2 + 1);
        std::nth_element (w.begin(), w.begin() + (long) w.size() / 2, w.end());
        thr[(size_t) i] = w[w.size() / 2] + alpha * (float) mean + delta;        // eq. 1.10
    }

    std::vector<int> peaks;
    for (int i = 1; i < n - 1; ++i)
    {
        if (odf[(size_t) i] <= thr[(size_t) i]) continue;
        bool isMax = true;
        for (int m = -3; m <= 3 && isMax; ++m)
        {
            const int j = i + m;
            if (j < 0 || j >= n || j == i) continue;
            if (odf[(size_t) j] > odf[(size_t) i] || (m < 0 && same (odf[(size_t) j], odf[(size_t) i]))) isMax = false;
        }
        if (! isMax) continue;
        if (! peaks.empty() && i - peaks.back() < minGap)
        {
            if (odf[(size_t) i] > odf[(size_t) peaks.back()]) peaks.back() = i;
            continue;
        }
        peaks.push_back (i);
    }
    return peaks;
}

//==============================================================================
namespace
{
// Moves a detected onset back to where the attack really starts: the energy
// minimum just before the steepest rise, then the zero crossing before that.
int backtrackOnset (const std::vector<float>& x, double rate, int centre, int floorFrame)
{
    const int n = (int) x.size();
    const int win = std::max (16, (int) (rate * 0.001));            // 1 ms energy window
    const int lo = std::max ({ win, floorFrame, centre - (int) (rate * 0.045) });
    const int hi = std::min (n - 1, centre + (int) (rate * 0.025));
    if (hi <= lo + win) return std::clamp (centre, 0, n - 1);

    // e[i]: energy of the window that ends at i, so it starts rising exactly at the attack
    std::vector<double> e ((size_t) (hi - lo + 1), 0.0);
    double acc = 0.0;
    for (int i = lo - win; i < lo; ++i) acc += (double) x[(size_t) i] * x[(size_t) i];
    for (int i = lo; i <= hi; ++i)
    {
        e[(size_t) (i - lo)] = acc;
        acc += (double) x[(size_t) i] * x[(size_t) i] - (double) x[(size_t) (i - win)] * x[(size_t) (i - win)];
    }
    // steepest rise over one window
    int best = lo; double bestRise = -1.0;
    for (int i = lo; i + win <= hi; ++i)
    {
        const double r = e[(size_t) (i + win - lo)] - e[(size_t) (i - lo)];
        if (r > bestRise) { bestRise = r; best = i; }
    }
    // from there, back to where the energy stops falling (the quiet just before the hit)
    int t = best + win / 2;
    const double floorE = e[(size_t) (best - lo)];
    while (t - 1 >= lo && e[(size_t) (t - 1 - lo)] > floorE * 1.02 + 1.0e-12) --t;
    while (t - 1 >= lo && e[(size_t) (t - 1 - lo)] < e[(size_t) (t - lo)] - 1.0e-12) --t;
    t = std::max (lo, t - std::max (2, win / 8));                    // a hair of pre-roll
    // the zero crossing just before, within half a millisecond
    for (int i = t; i > std::max (lo, t - win / 2); --i)
        if ((x[(size_t) i] >= 0.0f) != (x[(size_t) i - 1] >= 0.0f)) { t = i; break; }
    return t;
}

double autocorrAt (const std::vector<float>& f, double lag)
{
    const int L0 = (int) std::floor (lag);
    const double fr = lag - L0;
    double num = 0.0, den = 0.0;
    const int n = (int) f.size();
    for (int i = 0; i < n; ++i)
    {
        den += (double) f[(size_t) i] * f[(size_t) i];
        if (i + L0 + 1 < n)
            num += (double) f[(size_t) i] * ((1.0 - fr) * f[(size_t) (i + L0)] + fr * f[(size_t) (i + L0 + 1)]);
    }
    return den > 0.0 ? num / den : 0.0;
}
} // namespace

Analysis analyse (const Sample& s, const AnalysisParams& p)
{
    Analysis a;
    if (s.frames < 64) { a.starts = { 0 }; a.cls = { Kick }; a.level = { 1.0f }; return a; }

    const auto set = detectionFunctions (s, p.lambda);
    a.hop = set.hop;
    a.odf = set.f[std::clamp (p.odf, 0, NumOdf - 1)];
    const int minGap = std::max (1, (int) std::lround (p.minGapMs / 10.0));
    auto peaks = pickPeaks (a.odf, a.threshold, p.alpha, deltaFor (p.sensitivity), minGap);

    const auto x = monoOf (s);
    for (int pk : peaks) a.peakFrames.push_back (pk * a.hop);

    std::vector<int> st;
    const int gapFrames = (int) (s.rate * p.minGapMs * 0.001);
    for (int pk : a.peakFrames)
    {
        const int floorFrame = st.empty() ? 0 : st.back() + gapFrames / 2;
        int t = p.backtrack ? backtrackOnset (x, s.rate, pk, floorFrame) : std::max (floorFrame, pk - a.hop / 2);
        t = std::clamp (t, 0, s.frames - 1);
        if (! st.empty() && t <= st.back()) continue;
        st.push_back (t);
    }
    // the break's first frame always starts a slice
    if (st.empty() || st.front() > (int) (s.rate * 0.03)) st.insert (st.begin(), 0);
    else st.front() = 0;
    if ((int) st.size() > kMaxSlices) st.resize ((size_t) kMaxSlices);
    a.starts = st;

    // ---- tempo: which bar count makes the beat line up with the ODF's period
    const double dur = (double) s.frames / s.rate;
    double bestScore = -1.0e9;
    for (int bars : { 1, 2, 4, 8 })
    {
        const double bpm = bars * 240.0 / dur;
        if (bpm < 50.0 || bpm > 260.0) continue;
        const double beatLag = 60.0 / bpm / 0.01;                 // ODF frames
        const double ac = autocorrAt (a.odf, beatLag) + 0.6 * autocorrAt (a.odf, beatLag * 0.5)
                        + 0.3 * autocorrAt (a.odf, beatLag * 2.0);
        const double prior = -0.45 * std::pow (std::log2 (bpm / 118.0), 2.0);
        const double score = ac + prior;
        if (score > bestScore) { bestScore = score; a.tempo = bpm; }
    }
    if (a.tempo <= 0.0) a.tempo = 4.0 * 240.0 / std::max (0.1, dur);
    return a;
}

//==============================================================================
void classify (const Sample& s, SliceMap& m)
{
    const int n = m.count();
    m.cls.assign ((size_t) n, Snare);
    m.level.assign ((size_t) n, 0.0f);
    if (n == 0) return;

    const auto x = monoOf (s);
    constexpr int log2N = 11;
    const int N = 1 << log2N;
    Fft fft (log2N);
    std::vector<std::complex<float>> buf ((size_t) N);
    struct F { float low, body, crack, air, total; };
    std::vector<F> feats ((size_t) n);

    for (int i = 0; i < n; ++i)
    {
        const int st = m.starts[(size_t) i];
        const int len = std::max (32, std::min (m.lengthOf (i, s.frames), (int) (s.rate * 0.09)));
        float pk = 0.0f;
        for (int k = 0; k < std::min (len, (int) (s.rate * 0.05)); ++k)
            if (st + k < s.frames) pk = std::max (pk, std::abs (x[(size_t) (st + k)]));
        m.level[(size_t) i] = pk;

        for (int k = 0; k < N; ++k)
        {
            float v = 0.0f;
            if (k < len && st + k < s.frames)
            {
                // fast attack, Hann-shaped tail over the analysed part
                const float w = 0.5f + 0.5f * std::cos ((float) kPi * (float) k / (float) len);
                v = x[(size_t) (st + k)] * (k < 32 ? (float) k / 32.0f : w);
            }
            buf[(size_t) k] = { v, 0.0f };
        }
        fft.forward (buf);
        F f {};
        for (int k = 1; k < N / 2; ++k)
        {
            const double hz = (double) k * s.rate / N;
            const float e = std::norm (buf[(size_t) k]);
            if (hz < 30.0) continue;
            if (hz < 160.0) f.low += e;
            else if (hz < 1000.0) f.body += e;
            else if (hz < 5000.0) f.crack += e;
            else if (hz < 16000.0) f.air += e;
        }
        f.total = std::max (1.0e-12f, f.low + f.body + f.crack + f.air);
        feats[(size_t) i] = f;
    }

    const float maxLevel = std::max (1.0e-6f, *std::max_element (m.level.begin(), m.level.end()));
    float maxLow = 0.0f;
    for (int i = 0; i < n; ++i)
        if (m.level[(size_t) i] > 0.25f * maxLevel)
            maxLow = std::max (maxLow, feats[(size_t) i].low / feats[(size_t) i].total);

    // what a loud snare looks like in this break: its share of body (150 Hz - 1 kHz)
    float snareBody = 0.0f; int snares = 0;
    for (int i = 0; i < n; ++i)
    {
        const auto& f = feats[(size_t) i];
        const float fl = f.low / f.total, fa = f.air / f.total, fm = (f.body + f.crack) / f.total;
        const float lv = m.level[(size_t) i] / maxLevel;
        const bool kick = fl >= 0.6f * maxLow && fl > 0.14f && fl * 1.4f >= fa && lv > 0.2f;
        if (! kick && lv >= 0.42f && fm >= fa * 0.55f) { snareBody += f.body / f.total; ++snares; }
    }
    snareBody = snares > 0 ? snareBody / (float) snares : 0.15f;

    for (int i = 0; i < n; ++i)
    {
        const auto& f = feats[(size_t) i];
        const float fl = f.low / f.total, fa = f.air / f.total, fm = (f.body + f.crack) / f.total, fb = f.body / f.total;
        const float lv = m.level[(size_t) i] / maxLevel;
        uint8_t c;
        if (fl >= 0.6f * maxLow && fl > 0.14f && fl * 1.4f >= fa && lv > 0.2f)  c = Kick;
        else if (lv < 0.42f)                                                     c = fb >= 0.4f * snareBody ? Ghost : Hat;
        else                                                                     c = fm >= fa * 0.55f ? Snare : Hat;
        m.cls[(size_t) i] = c;
    }
    // normalise levels for the generator (0..1 against the loudest hit)
    for (auto& l : m.level) l /= maxLevel;
}

SliceMap buildSlices (const Sample& s, int mode)
{
    SliceMap m;
    if (mode == SliceOnsets && ! s.an.starts.empty())
        m.starts = s.an.starts;
    else
    {
        const int n = mode == SliceGrid8 ? 8 : mode == SliceGrid32 ? 32 : 16;
        for (int i = 0; i < n; ++i) m.starts.push_back ((int) ((long long) s.frames * i / n));
    }
    classify (s, m);
    return m;
}

int resolveBars (const Sample& s, int setting, double)
{
    if (setting > 0) return setting;
    const double dur = (double) s.frames / std::max (1.0, s.rate);
    const double t = s.an.tempo > 0.0 ? s.an.tempo : 120.0;
    int best = 1; double bestErr = 1.0e9;
    for (int b : { 1, 2, 4, 8 })
    {
        const double e = std::abs (std::log2 ((b * 240.0 / dur) / t));
        if (e < bestErr) { bestErr = e; best = b; }
    }
    return best;
}

} // namespace dnafam::gtbs
