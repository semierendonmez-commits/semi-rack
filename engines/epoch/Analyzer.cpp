// Vendored unchanged from semierendonmez-commits/epoch Source/engine (Rack / MetaModule port).
#include "Analyzer.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace epoch
{

Analyzer::Analyzer (double sampleRate) : sr (sampleRate > 1000.0 ? sampleRate : 44100.0)
{
    hop = std::max (64, (int) std::round (sr * 0.0116));
    levelDb.reserve (1 << 16);
    diffDb.reserve (1 << 16);
}

void Analyzer::push (const float* x, int n)
{
    for (int i = 0; i < n; ++i)
    {
        const float v = x[i];
        const float d = v - last;
        last = v;
        accX += (double) v * v;
        accD += (double) d * d;
        if (++fill == hop)
        {
            levelDb.push_back ((float) (10.0 * std::log10 (accX / hop + 1.0e-12)));
            diffDb.push_back ((float) (10.0 * std::log10 (accD / hop + 1.0e-12)));
            accX = accD = 0.0;
            fill = 0;
        }
    }
    total += n;
}

static float percentile (std::vector<float> v, float p)
{
    if (v.empty()) return 0.0f;
    const size_t k = (size_t) std::clamp ((double) p * (double) (v.size() - 1), 0.0, (double) (v.size() - 1));
    std::nth_element (v.begin(), v.begin() + (long) k, v.end());
    return v[k];
}

Measure Analyzer::finish (double bpm, int maxBars)
{
    Measure m;
    m.seconds = seconds();
    const int H = (int) levelDb.size();
    if (H < 8) return m;
    const double hopSec = (double) hop / sr;

    // ---- silence: well under the loud part of the file, so a quiet recording still has its holes
    const float thr = std::max (percentile (levelDb, 0.95f) - 38.0f, -72.0f);
    std::vector<uint8_t> silent ((size_t) H, 0), space ((size_t) H, 0);
    for (int i = 0; i < H; ++i) silent[(size_t) i] = levelDb[(size_t) i] < thr ? 1 : 0;
    const int minSpace = (int) std::ceil (0.25 / hopSec), minGap = (int) std::ceil (1.5 / hopSec);
    long long spaceHops = 0, gapHops = 0;
    for (int i = 0; i < H;)
    {
        if (! silent[(size_t) i]) { ++i; continue; }
        int j = i;
        while (j < H && silent[(size_t) j]) ++j;
        const int len = j - i;
        if (len >= minSpace) { for (int k = i; k < j; ++k) space[(size_t) k] = 1; spaceHops += len; }
        if (len >= minGap) gapHops += len;
        i = j;
    }
    m.spaceShare = (double) spaceHops / H;
    m.gapShare = (double) gapHops / H;

    // ---- onsets: rises of the high-passed level that stand above their neighbourhood
    std::vector<float> odf ((size_t) H, 0.0f);
    for (int i = 1; i < H; ++i)
        if (! silent[(size_t) i]) odf[(size_t) i] = std::max (0.0f, diffDb[(size_t) i] - diffDb[(size_t) i - 1]);
    std::vector<double> onsets;
    const int W = 20, minDist = std::max (1, (int) std::round (0.05 / hopSec));
    std::vector<double> pre ((size_t) H + 1, 0.0);
    for (int i = 0; i < H; ++i) pre[(size_t) i + 1] = pre[(size_t) i] + odf[(size_t) i];
    int lastOn = -minDist;
    for (int i = 0; i < H; ++i)
    {
        const int lo = std::max (0, i - W), hi = std::min (H - 1, i + W);
        const float mean = (float) ((pre[(size_t) hi + 1] - pre[(size_t) lo]) / (double) (hi - lo + 1));
        const float v = odf[(size_t) i];
        if (v < 4.0f || v < mean + 2.5f) continue;
        if ((i > 0 && odf[(size_t) i - 1] > v) || (i + 1 < H && odf[(size_t) i + 1] >= v)) continue;
        if (i - lastOn < minDist) continue;
        onsets.push_back ((double) i * hopSec);
        lastOn = i;
    }
    m.onsetsPerSecond = m.seconds > 0.0 ? (double) onsets.size() / m.seconds : 0.0;
    if (onsets.size() > 2)
    {
        std::vector<double> iv;
        for (size_t k = 1; k < onsets.size(); ++k) iv.push_back (onsets[k] - onsets[k - 1]);
        const double mu = std::accumulate (iv.begin(), iv.end(), 0.0) / (double) iv.size();
        double var = 0.0;
        for (double x : iv) var += (x - mu) * (x - mu);
        m.intervalCv = mu > 0.0 ? std::sqrt (var / (double) iv.size()) / mu : 0.0;
    }

    // ---- per bar
    double barSec = 240.0 / std::max (20.0, bpm);
    int bars = (int) std::ceil (m.seconds / barSec);
    if (bars > maxBars) { bars = maxBars; barSec = m.seconds / maxBars; }   // squeezed onto the timeline
    bars = std::max (bars, 4);
    m.bars = bars;
    std::vector<float> loud ((size_t) bars, -120.0f), dens ((size_t) bars, 0.0f), spc ((size_t) bars, 0.0f);
    for (int b = 0; b < bars; ++b)
    {
        const int h0 = (int) ((double) b * barSec / hopSec), h1 = std::min (H, (int) ((double) (b + 1) * barSec / hopSec));
        if (h1 <= h0) continue;
        double p = 0.0, sp = 0.0;
        for (int i = h0; i < h1; ++i) { p += std::pow (10.0, levelDb[(size_t) i] / 10.0); sp += space[(size_t) i]; }
        loud[(size_t) b] = (float) (10.0 * std::log10 (p / (h1 - h0) + 1.0e-12));
        spc[(size_t) b] = (float) (sp / (h1 - h0));
    }
    for (double t : onsets)
    {
        const int b = (int) (t / barSec);
        if (b >= 0 && b < bars) dens[(size_t) b] += 1.0f;
    }
    for (auto& d : dens) d /= (float) barSec;
    const float l10 = percentile (loud, 0.10f), l98 = percentile (loud, 0.98f);
    const float d95 = std::max (0.5f, percentile (dens, 0.95f));
    m.energyPerBar.resize ((size_t) bars);
    m.spacePerBar = spc;
    double sum = 0.0;
    for (int b = 0; b < bars; ++b)
    {
        const float L = std::clamp ((loud[(size_t) b] - l10) / std::max (3.0f, l98 - l10), 0.0f, 1.0f);
        const float D = std::clamp (dens[(size_t) b] / d95, 0.0f, 1.0f);
        m.energyPerBar[(size_t) b] = 0.08f + 0.88f * (0.62f * L + 0.38f * D);
        sum += m.energyPerBar[(size_t) b];
    }
    // light smoothing: a bar is a short window for a slow curve
    auto smoothed = m.energyPerBar;
    for (int b = 0; b < bars; ++b)
    {
        float a = 0.0f, w = 0.0f;
        for (int k = -1; k <= 1; ++k)
            if (b + k >= 0 && b + k < bars) { const float wk = k == 0 ? 2.0f : 1.0f; a += wk * m.energyPerBar[(size_t) (b + k)]; w += wk; }
        smoothed[(size_t) b] = a / w;
    }
    m.energyPerBar = smoothed;
    m.meanEnergy = sum / bars;
    return m;
}

//==============================================================================
std::vector<int> simplify (const std::vector<float>& v, float eps, int maxPoints)
{
    const int n = (int) v.size();
    if (n <= 2) { std::vector<int> r; for (int i = 0; i < n; ++i) r.push_back (i); return r; }
    std::vector<uint8_t> keep ((size_t) n, 0);
    keep[0] = keep[(size_t) n - 1] = 1;
    // iterative: split the segment with the largest error until the error is under eps or the budget is spent
    int kept = 2;
    for (;;)
    {
        float worst = eps;
        int wi = -1;
        int a = 0;
        for (int b = 1; b < n; ++b)
        {
            if (! keep[(size_t) b]) continue;
            for (int i = a + 1; i < b; ++i)
            {
                const float t = (float) (i - a) / (float) (b - a);
                const float e = std::abs (v[(size_t) i] - (v[(size_t) a] + (v[(size_t) b] - v[(size_t) a]) * t));
                if (e > worst) { worst = e; wi = i; }
            }
            a = b;
        }
        if (wi < 0 || kept >= maxPoints) break;
        keep[(size_t) wi] = 1;
        ++kept;
    }
    std::vector<int> r;
    for (int i = 0; i < n; ++i) if (keep[(size_t) i]) r.push_back (i);
    return r;
}

Score scoreFromMeasure (const Measure& m, const std::string& title, double bpm)
{
    Score s;
    s.title = title;
    s.bpm = bpm;
    s.bars = std::clamp (m.bars, 4, kMaxBars);
    const int n = (int) m.energyPerBar.size();
    if (n == 0) { s.energy.pts = { { 0, 0.5f }, { (double) s.bars, 0.5f } }; s.space.pts = { { 0, 0.0f } }; return s; }

    for (int i : simplify (m.energyPerBar, 0.05f, 64))
        s.energy.pts.push_back ({ (double) i + 0.5, m.energyPerBar[(size_t) i], Shape::Linear });
    // space per bar is spiky: the curve follows its 4-bar average
    std::vector<float> sp ((size_t) n, 0.0f);
    for (int b = 0; b < n; ++b)
    {
        float a = 0.0f; int c = 0;
        for (int k = -2; k <= 1; ++k) if (b + k >= 0 && b + k < n) { a += m.spacePerBar[(size_t) (b + k)]; ++c; }
        sp[(size_t) b] = std::min (0.95f, a / (float) c);
    }
    for (int i : simplify (sp, 0.04f, 48))
        s.space.pts.push_back ({ (double) i + 0.5, sp[(size_t) i], Shape::Linear });

    // sections where the 8-bar energy moves a lot, on 8-bar lines, at least 16 bars apart
    auto mean8 = [&] (int from, int to)
    {
        from = std::max (0, from); to = std::min (n, to);
        float a = 0.0f; for (int b = from; b < to; ++b) a += m.energyPerBar[(size_t) b];
        return to > from ? a / (float) (to - from) : 0.0f;
    };
    std::vector<int> cuts { 0 };
    for (int b = 8; b + 8 <= n; b += 8)
        if (std::abs (mean8 (b, b + 8) - mean8 (b - 8, b)) > 0.16f && b - cuts.back() >= 16) cuts.push_back (b);
    for (size_t k = 0; k < cuts.size(); ++k)
    {
        const int a = cuts[k], b = k + 1 < cuts.size() ? cuts[k + 1] : n;
        const float e = mean8 (a, b);
        Role r = e > 0.68f ? Role::Drop : (e < 0.38f ? Role::Break : Role::Roll);
        if (k == 0 && e < 0.6f) r = Role::Intro;
        if (k + 1 == cuts.size() && k > 0 && e < 0.5f) r = Role::Outro;
        s.sections.push_back ({ (double) a, r, std::string ("M") + std::to_string (k + 1) + " " + roleName (r) });
    }
    return s;
}

} // namespace epoch
