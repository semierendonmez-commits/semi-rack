#include "Dsp.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace bm
{

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kPeriods[36] = {
    856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
    428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
    214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113 };
constexpr const char* kNoteNames[12] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };

float median (std::vector<float> v)
{
    if (v.empty()) return 0.0f;
    auto mid = v.begin() + (long) v.size() / 2;
    std::nth_element (v.begin(), mid, v.end());
    return *mid;
}
} // namespace

const char* roleLetter (int r)
{
    static const char* l[] = { "K", "S", "H", "G" };
    return (r >= 0 && r < kNumRoles) ? l[r] : "?";
}

const char* sliceModeName (int m)
{
    static const char* n[] = { "TRANS", "EQUAL", "GRID", "MANUAL" };
    return (m >= 0 && m < 4) ? n[m] : "?";
}

// ---- onsets ----------------------------------------------------------------------------
Onsets detectOnsets (const float* x, int n, double sr)
{
    Onsets out;
    if (n < 64) return out;
    const int hop = std::max (16, (int) (sr * 0.005));
    const int win = hop * 4;          // 20 ms: shorter windows let a 50 Hz kick tail flutter
    const int frames = std::max (0, (n - win) / hop + 1);
    std::vector<float> d ((size_t) n, 0.0f);
    for (int i = 1; i < n; ++i) d[(size_t) i] = x[i] - x[i - 1];   // first difference: tilts toward the attack
    // prefix sums of energy for fast windows
    std::vector<double> cx ((size_t) n + 1, 0.0), cd ((size_t) n + 1, 0.0);
    for (int i = 0; i < n; ++i)
    {
        cx[(size_t) i + 1] = cx[(size_t) i] + (double) x[i] * x[i];
        cd[(size_t) i + 1] = cd[(size_t) i] + (double) d[(size_t) i] * d[(size_t) i];
    }
    std::vector<float> eFull ((size_t) frames), eHigh ((size_t) frames);
    for (int f = 0; f < frames; ++f)
    {
        const int a = f * hop, b = a + win;
        eFull[(size_t) f] = (float) std::log10 (1e-9 + (cx[(size_t) b] - cx[(size_t) a]) / win);
        eHigh[(size_t) f] = (float) std::log10 (1e-9 + (cd[(size_t) b] - cd[(size_t) a]) / win);
    }
    // a frame's onset value: how far its new 5 ms (the hop entering the window) rises over the window before
    std::vector<float> odf ((size_t) frames, 0.0f);
    for (int f = 4; f < frames; ++f)
    {
        const float rf = eFull[(size_t) f] - eFull[(size_t) f - 4];
        const float rh = eHigh[(size_t) f] - eHigh[(size_t) f - 4];
        const float gate = eFull[(size_t) f] > -4.5f ? 1.0f : 0.0f;
        odf[(size_t) f] = gate * (std::max (0.0f, rf) + 0.7f * std::max (0.0f, rh));
    }
    const float med = median (odf);
    std::vector<float> dev (odf.size());
    for (size_t i = 0; i < odf.size(); ++i) dev[i] = std::abs (odf[i] - med);
    const float mad = std::max (1e-3f, median (dev));
    const float thr = med + 3.0f * mad + 0.25f;

    const int minGap = std::max (1, (int) (0.045 * sr / hop));
    float maxS = 0.0f;
    int last = -minGap * 2;
    for (int f = 4; f < frames - 1; ++f)
    {
        const float v = odf[(size_t) f];
        if (v < thr || v < odf[(size_t) f - 1] || v < odf[(size_t) f + 1]) continue;
        if (f - last < minGap)
        {
            if (! out.strength.empty() && v > out.strength.back()) out.strength.back() = v;
            continue;
        }
        // the attack lies in the window's newest part: find where the short envelope
        // first climbs 30 % of the way from what was ringing before to the peak
        const int a = std::max (0, f * hop - hop), b = std::min (n, f * hop + win + hop);
        auto env = [&] (int i) { float m = 0; for (int j = i; j < std::min (n, i + 24); ++j) m = std::max (m, std::abs (x[j])); return m; };
        float base = 0.0f, peak = 0.0f;
        for (int i = std::max (0, a - (int) (sr * 0.012)); i < a; ++i) base = std::max (base, std::abs (x[i]));
        for (int i = a; i < b; i += 8) peak = std::max (peak, env (i));
        int s = a;
        for (int i = a; i < b; i += 2)
            if (env (i) > base + 0.3f * (peak - base)) { s = i; break; }
        s = std::max (0, s - (int) (sr * 0.0006));
        if (! out.pos.empty() && s - out.pos.back() < (int) (0.03 * sr)) continue;
        out.pos.push_back (s);
        out.strength.push_back (v);
        maxS = std::max (maxS, v);
        last = f;
    }
    if (maxS > 0)
        for (auto& v : out.strength) v /= maxS;
    // the loop starts with a hit even when the very first frame cannot rise
    if (out.pos.empty() || out.pos.front() > (int) (sr * 0.03))
    {
        out.pos.insert (out.pos.begin(), 0);
        out.strength.insert (out.strength.begin(), 1.0f);
    }
    else
    {
        out.pos.front() = 0;
        out.strength.front() = 1.0f;
    }
    return out;
}

std::vector<int> transientSlices (const Onsets& o, int n, double sr, int thres)
{
    std::vector<int> s;
    thres = std::clamp (thres, 2, 64);
    // 2 keeps everything, 64 keeps only the very strongest
    const float keep = std::pow ((float) (thres - 2) / 62.0f, 0.7f) * 0.85f;
    const int minLen = (int) (sr * 0.03);
    for (size_t i = 0; i < o.pos.size(); ++i)
    {
        if (i > 0 && o.strength[i] < keep) continue;
        if (! s.empty() && o.pos[i] - s.back() < minLen) continue;
        if (o.pos[i] >= n - minLen) continue;
        s.push_back (o.pos[i]);
        if ((int) s.size() >= kMaxSlices) break;
    }
    if (s.empty()) s.push_back (0);
    return s;
}

std::vector<int> equalSlices (int n, int count)
{
    count = std::clamp (count, 1, kMaxSlices);
    std::vector<int> s;
    for (int i = 0; i < count; ++i) s.push_back ((int) ((int64_t) n * i / count));
    return s;
}

std::vector<int> gridSlices (const Onsets& o, int n, int bars)
{
    const int steps = std::clamp (bars * 16, 1, kMaxSlices);
    const double step = (double) n / steps;
    std::vector<int> s;
    for (int k = 0; k < steps; ++k)
    {
        const double g = k * step;
        int best = (int) g;
        double bestD = step * 0.4;
        for (int p : o.pos)
        {
            const double d = std::abs (p - g);
            if (d < bestD) { bestD = d; best = p; }
        }
        if (! s.empty() && best <= s.back()) best = std::max (s.back() + 1, (int) g);
        if (best < n - 8) s.push_back (best);
    }
    if (s.empty()) s.push_back (0);
    s.front() = 0;
    return s;
}

int snapToZero (const float* x, int n, int pos, int maxBack)
{
    pos = std::clamp (pos, 0, std::max (0, n - 1));
    for (int d = 0; d <= maxBack; ++d)
    {
        const int i = pos - d;
        if (i <= 0) return 0;
        if ((x[i - 1] <= 0.0f && x[i] > 0.0f) || (x[i - 1] >= 0.0f && x[i] < 0.0f) || x[i] == 0.0f) return i;
    }
    return pos;
}

std::vector<int> classify (const float* x, int n, double sr, const std::vector<int>& starts)
{
    std::vector<int> roles (starts.size(), RoleSnare);
    if (starts.empty() || n <= 0) return roles;
    const int win = std::max (32, (int) (sr * 0.03));
    const float aLo = (float) (1.0 - std::exp (-2.0 * kPi * 150.0 / sr));
    const float aHi = (float) (1.0 - std::exp (-2.0 * kPi * 4000.0 / sr));
    std::vector<float> e (starts.size()), lo (starts.size()), hi (starts.size());
    float maxE = 1e-12f;
    for (size_t k = 0; k < starts.size(); ++k)
    {
        const int s0 = starts[k];
        const int s1 = std::min (n, s0 + win);
        float l = 0, h = 0;
        double el = 0, eh = 0, et = 0;
        // warm the filters on what came just before, so a ringing kick reads as its own band
        for (int i = std::max (0, s0 - 64); i < s1; ++i)
        {
            l += aLo * (x[i] - l);
            h += aHi * (x[i] - h);
            if (i < s0) continue;
            const float hp = x[i] - h;
            el += (double) l * l;
            eh += (double) hp * hp;
            et += (double) x[i] * x[i];
        }
        e[k] = (float) et;
        lo[k] = (float) (el / (et + 1e-12));
        hi[k] = (float) (eh / (et + 1e-12));
        maxE = std::max (maxE, e[k]);
    }
    for (size_t k = 0; k < starts.size(); ++k)
    {
        const float rel = e[k] / maxE;
        if (lo[k] > 0.42f && rel > 0.12f)       roles[k] = RoleKick;
        else if (hi[k] > 0.30f && rel < 0.30f)  roles[k] = RoleHat;
        else if (rel < 0.16f)                   roles[k] = RoleGhost;
        else                                    roles[k] = RoleSnare;
    }
    return roles;
}

int autoBars (double seconds)
{
    int best = 2;
    double bestD = 1e9;
    for (int b : { 1, 2, 4, 8 })
    {
        const double bpm = loopBpm (seconds, b);
        if (bpm < 70.0 || bpm > 190.0) continue;
        const double d = std::abs (bpm - 140.0);
        if (d < bestD) { bestD = d; best = b; }
    }
    return best;
}

// ---- Paula ---------------------------------------------------------------------------------
double paulaRate (int index, double fullRate)
{
    if (index < 0 || index >= 36) return fullRate;
    return std::floor (3546895.0 / kPeriods[index]);
}

std::string paulaRateName (int index)
{
    if (index < 0 || index >= 36) return "FULL";
    return std::string (kNoteNames[index % 12]) + std::to_string (1 + index / 12);
}

int defaultRateIndex() { return 29; }   // F-3, period 160, 22168 Hz

std::vector<float> resample (const float* x, int n, double srIn, double srOut)
{
    std::vector<float> y;
    if (n <= 0 || srIn <= 0 || srOut <= 0) return y;
    if (std::abs (srIn - srOut) < 0.5) return std::vector<float> (x, x + n);
    const int outN = (int) std::floor ((double) n * srOut / srIn);
    y.resize ((size_t) std::max (0, outN));
    const double ratio = srIn / srOut;
    const double fc = std::min (1.0, srOut / srIn) * 0.92;   // cutoff relative to the input Nyquist
    constexpr int taps = 24;
    const int half = (int) std::ceil (taps / fc);
    for (int j = 0; j < outN; ++j)
    {
        const double t = j * ratio;
        const int c = (int) std::floor (t);
        double acc = 0, wsum = 0;
        for (int k = c - half + 1; k <= c + half; ++k)
        {
            const double d = (t - k) * fc;
            if (std::abs (d) >= taps) continue;
            const double sinc = d == 0.0 ? 1.0 : std::sin (kPi * d) / (kPi * d);
            const double w = 0.42 + 0.5 * std::cos (kPi * d / taps) + 0.08 * std::cos (2.0 * kPi * d / taps);
            const double h = sinc * w;
            wsum += h;
            if (k >= 0 && k < n) acc += x[k] * h;
        }
        y[(size_t) j] = (float) (wsum != 0.0 ? acc / wsum : 0.0);
    }
    return y;
}

void quantise (std::vector<float>& x, int bits)
{
    if (bits >= 16 || bits <= 0) return;
    const float q = (float) (1 << (bits - 1));
    for (auto& v : x)
    {
        float s = std::floor (v * q + 0.5f);
        s = std::clamp (s, -q, q - 1.0f);
        v = s / q;
    }
}

// ---- reverb --------------------------------------------------------------------------------
void MonoVerb::setup (double sr, float size, float damp)
{
    static const int ct[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    static const int at[4] = { 556, 441, 341, 225 };
    const double sc = sr / 44100.0 * (0.55 + 0.6 * size);
    for (int i = 0; i < 8; ++i) { comb[i].buf.assign ((size_t) std::max (8, (int) (ct[i] * sc)), 0.0f); comb[i].idx = 0; comb[i].store = 0; }
    for (int i = 0; i < 4; ++i) { ap[i].buf.assign ((size_t) std::max (8, (int) (at[i] * sr / 44100.0)), 0.0f); ap[i].idx = 0; }
    feedback = 0.70f + 0.27f * std::clamp (size, 0.0f, 1.0f);
    damp1 = std::clamp (damp, 0.0f, 0.95f);
    damp2 = 1.0f - damp1;
}

float MonoVerb::process (float in)
{
    float out = 0.0f;
    const float inp = in * 0.12f;
    for (auto& c : comb)
    {
        float y = c.buf[(size_t) c.idx];
        c.store = y * damp2 + c.store * damp1;
        c.buf[(size_t) c.idx] = inp + c.store * feedback;
        if (++c.idx >= (int) c.buf.size()) c.idx = 0;
        out += y;
    }
    for (auto& a : ap)
    {
        const float b = a.buf[(size_t) a.idx];
        a.buf[(size_t) a.idx] = out + b * 0.5f;
        out = b - out;
        if (++a.idx >= (int) a.buf.size()) a.idx = 0;
    }
    return out;
}

// ---- prepare -------------------------------------------------------------------------------
std::shared_ptr<Prepared> prepare (const PrepareSpec& sp)
{
    auto p = std::make_shared<Prepared>();
    p->srcRate = sp.srcRate;
    p->srcLen = sp.n;
    p->bars = std::max (1, sp.bars);
    p->rate = paulaRate (sp.rateIndex, sp.srcRate);
    if (sp.mono == nullptr || sp.n <= 0) return p;

    p->pcm = resample (sp.mono, sp.n, sp.srcRate, p->rate);
    quantise (p->pcm, sp.bits);
    const int len = (int) p->pcm.size();
    const double k = p->rate / sp.srcRate;

    std::vector<int> st = sp.starts;
    if (st.empty()) st.push_back (0);
    std::sort (st.begin(), st.end());
    st.erase (std::unique (st.begin(), st.end()), st.end());
    if ((int) st.size() > kMaxSlices) st.resize (kMaxSlices);
    for (int s : st)
    {
        const int ps = std::clamp ((int) std::lround (s * k), 0, std::max (0, len - 1));
        if (! p->start.empty() && ps <= p->start.back()) continue;
        p->start.push_back (ps);
    }
    for (size_t i = 0; i < p->start.size(); ++i)
    {
        const int r = i < sp.roles.size() ? sp.roles[i] : RoleSnare;
        p->role.push_back (r);
        p->beat.push_back ((float) ((double) p->start[i] / std::max (1, len) * p->bars * 4.0));
    }
    p->start.push_back (len);

    // drill tails: the slice through a mono reverb, 8-bit like the rest
    const float space = std::clamp (sp.space, 0.0f, 1.0f);
    const int tail = (int) (p->rate * (0.25 + 1.0 * space));
    MonoVerb verb;
    for (int i = 0; i < p->numSlices(); ++i)
    {
        const int s0 = p->start[(size_t) i], s1 = p->start[(size_t) i + 1];
        const int sl = std::max (1, s1 - s0);
        verb.setup (p->rate, 0.35f + 0.65f * space, 0.35f);
        std::vector<float> w ((size_t) (sl + tail));
        const float wetG = 0.55f + 1.1f * space;
        float peak = 0.0f;
        for (int j = 0; j < (int) w.size(); ++j)
        {
            const float dry = j < sl ? p->pcm[(size_t) (s0 + j)] : 0.0f;
            float v = dry * 0.75f + verb.process (dry) * wetG;
            w[(size_t) j] = v;
            peak = std::max (peak, std::abs (v));
        }
        if (peak > 0.99f) for (auto& v : w) v *= 0.99f / peak;
        // the tail fades out over its last third so a held drill never hits a wall
        const int fadeLen = tail / 3;
        for (int j = 0; j < fadeLen; ++j)
            w[w.size() - 1 - (size_t) j] *= (float) j / (float) fadeLen;
        quantise (w, sp.bits);
        p->wet.push_back (std::move (w));
    }
    return p;
}

} // namespace bm
