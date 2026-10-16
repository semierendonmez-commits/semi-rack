// NMS - DSP building blocks (no JUCE). Shared with CLOUDLAB 200t / kreLL.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>
#include <string>
#include <memory>

namespace nms
{
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float clamp01 (float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
inline float clampf (float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
inline float lerp (float a, float b, float t) { return a + (b - a) * t; }
inline float fastTanh (float x) { x = clampf (x, -4.5f, 4.5f); const float x2 = x * x; return x * (27.0f + x2) / (27.0f + 9.0f * x2); }
inline double midiHz (double note) { return 440.0 * std::exp2 ((note - 69.0) / 12.0); }

// deterministic hashes: the pattern engines are pure functions of (seed, position),
// so a pattern loops exactly and a stored pattern always plays back the same
inline uint32_t hash32 (uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
inline float hashf (uint32_t seed, int32_t pos, uint32_t salt = 0)   // 0..1
{
    return (float) (hash32 (seed * 0x9e3779b1u ^ hash32 ((uint32_t) pos + 0x632be5abu * (salt + 1))) >> 8) * (1.0f / 16777216.0f);
}
inline uint32_t seedOf (float norm, uint32_t salt) { return (uint32_t) std::lround (norm * 1000.0f) * 2654435761u + salt * 40503u + 17u; }

struct Edge
{
    bool hi = false;
    inline bool rise (float x)
    {
        if (! hi && x > 0.55f) { hi = true; return true; }
        if (hi && x < 0.45f) hi = false;
        return false;
    }
};

struct Pulse
{
    int left = 0;
    void fire (int samples) { left = samples; }
    inline float tick() { if (left > 0) { --left; return 1.0f; } return 0.0f; }
};

struct Rng
{
    uint32_t s = 0x9e3779b9u;
    explicit Rng (uint32_t seed = 0x9e3779b9u) : s (seed ? seed : 1u) {}
    inline uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    inline float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    inline float bi() { return uni() * 2.0f - 1.0f; }
};

inline float polyBlep (float t, float dt)
{
    if (dt <= 0.0f) return 0.0f;
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

// TPT state-variable filter (Zavalishin)
struct Svf
{
    float ic1 = 0, ic2 = 0, g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
    void set (float hz, float q, float sr)
    {
        hz = clampf (hz, 5.0f, sr * 0.45f);
        g = std::tan (kPi * hz / sr);
        k = 1.0f / std::max (0.05f, q);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    inline void tick (float x, float& lp, float& bp, float& hp)
    {
        const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
        if (! std::isfinite (ic1) || ! std::isfinite (ic2)) { ic1 = ic2 = 0; lp = bp = hp = 0; }
    }
    inline float lp (float x) { float l, b, h; tick (x, l, b, h); return l; }
    inline float bp (float x) { float l, b, h; tick (x, l, b, h); return b; }
    inline float hp (float x) { float l, b, h; tick (x, l, b, h); return h; }
    void clear() { ic1 = ic2 = 0; }
};

struct OnePole
{
    float s = 0, g = 0;
    void set (double hz, double sr) { const double w = std::tan (3.141592653589793 * std::min (hz, sr * 0.49) / sr); g = (float) (w / (1.0 + w)); }
    inline float lp (float x) { const float v = (x - s) * g; const float y = v + s; s = y + v; return y; }
    inline float hp (float x) { return x - lp (x); }
};

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline float tick (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void shelf (double f, double db, double sr, bool high)
    {
        const double A = std::pow (10.0, db / 40.0), w = 6.283185307179586 * std::min (f, sr * 0.45) / sr, c = std::cos (w), s = std::sin (w);
        const double al = s / 2.0 * std::sqrt (2.0), sa = 2.0 * std::sqrt (A) * al;
        double B0, B1, B2, A0, A1, A2;
        if (! high) { B0 = A * ((A + 1) - (A - 1) * c + sa); B1 = 2 * A * ((A - 1) - (A + 1) * c); B2 = A * ((A + 1) - (A - 1) * c - sa);
                      A0 = (A + 1) + (A - 1) * c + sa; A1 = -2 * ((A - 1) + (A + 1) * c); A2 = (A + 1) + (A - 1) * c - sa; }
        else        { B0 = A * ((A + 1) + (A - 1) * c + sa); B1 = -2 * A * ((A - 1) + (A + 1) * c); B2 = A * ((A + 1) + (A - 1) * c - sa);
                      A0 = (A + 1) - (A - 1) * c + sa; A1 = 2 * ((A - 1) - (A + 1) * c); A2 = (A + 1) - (A - 1) * c - sa; }
        b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0); a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
    }
    void clear() { z1 = z2 = 0; }
};

struct DelayLine
{
    std::vector<float> buf; int mask = 0, w = 0;
    void init (int maxSamples) { int n = 1; while (n < maxSamples + 4) n <<= 1; buf.assign ((size_t) n, 0.0f); mask = n - 1; w = 0; }
    inline void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    inline float read (float d) const
    {
        float rp = (float) w - d; while (rp < 0) rp += (float) (mask + 1);
        const int i0 = (int) rp; const float f = rp - (float) i0;
        return buf[(size_t) (i0 & mask)] * (1.0f - f) + buf[(size_t) ((i0 + 1) & mask)] * f;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
};

// attack / decay envelope, exponential decay, linear attack
struct AD
{
    float v = 0, atk = 1, dec = 0.999f; int st = 0;
    void trigger (float attackS, float decayS, float sr)
    {
        atk = 1.0f / std::max (1.0f, attackS * sr);
        dec = std::exp (-6.9f / std::max (1.0f, decayS * sr));
        st = 1;
    }
    inline float tick()
    {
        if (st == 1) { v += atk; if (v >= 1.0f) { v = 1.0f; st = 2; } }
        else if (st == 2) { v *= dec; if (v < 1.0e-5f) { v = 0; st = 0; } }
        return v;
    }
};

// The reVerb* (Martijn Zwartjes / lAZyfISh) as rebuilt in kreLL beatBox / CLOUDLAB.
struct RevParams { float size = 10, decay = 70, spin = 1, hiCut = 3, loCut = 0, diffusion = -1; };
class Reverb
{
public:
    void prepare (double s)
    {
        sr = s;
        const int maxD = (int) (sr * 2.5);
        for (auto& t : tank) { for (auto& d : t.dif) d.init (maxD); t.dly.init (maxD); }
        for (int i = 0; i < 4; ++i) rnd[i].s = 1234u + 977u * (uint32_t) i;
        reset();
    }
    void reset()
    {
        for (auto& t : tank) { for (auto& d : t.dif) d.clear(); t.dly.clear(); t.lp.s = t.hp.s = 0; t.feed = 0; t.gain = 0; }
        ctlCount = 0;
    }
    inline void tick (float inL, float inR, float& outL, float& outR, const RevParams& p)
    {
        if (--ctlCount <= 0)
        {
            ctlCount = (float) (sr / 1000.0);
            const double S = 30.0 - p.size;
            static const double offs[2][4] = { { 8, 4, 0, -7 }, { 10, 6, 2, -5 } };
            const double rtMs = 1000.0 / midiHz (-p.decay);
            dff = p.diffusion >= 0 ? clampf (p.diffusion, 0.0f, 0.82f) : (float) (p.size / 240.0 + 0.5);
            const double F = 0.5 * p.spin, wander = F * F;
            lfoPh += F / 1000.0; lfoPh -= std::floor (lfoPh);
            for (int k = 0; k < 4; ++k)
            {
                const double m = std::sin (6.283185307179586 * (lfoPh - 0.125 * k)) * p.spin + rndTick (k, F > 0 ? F : 0.0001) * wander;
                for (int c = 0; c < 2; ++c) dlyTgt[c][k] = (float) std::max (0.05, 1000.0 / midiHz (S + offs[c][k]) + (c == 0 ? m : -m));
            }
            for (int c = 0; c < 2; ++c)
            {
                gTgt[c] = (float) std::pow (10.0, (-60.0 * dlyTgt[c][3] / rtMs) / 20.0);
                tank[c].lp.set (midiHz ((c ? 112.0 : 96.0) - 12.0 * p.hiCut), sr);
                tank[c].hp.set (midiHz (60.0 + 12.0 * (p.loCut - 6.0)), sr);
            }
        }
        const float in[2] = { inL, inR }, fb[2] = { tank[1].feed, tank[0].feed };
        float wet[2];
        for (int c = 0; c < 2; ++c)
        {
            auto& t = tank[c];
            for (int k = 0; k < 4; ++k) dlyMs[c][k] += (dlyTgt[c][k] - dlyMs[c][k]) * 0.002f;
            t.gain += (gTgt[c] - t.gain) * 0.002f;
            float x = in[c] + t.hp.hp (t.lp.lp (fb[c]));
            for (int k = 0; k < 3; ++k)
            {
                auto& d = t.dif[k];
                const float del = d.read (std::max (1.0f, dlyMs[c][k] * 0.001f * (float) sr));
                const float v = x + dff * del;
                d.push (v);
                x = del - dff * v;
            }
            wet[c] = x;
            t.dly.push (x);
            t.feed = t.dly.read (std::max (1.0f, dlyMs[c][3] * 0.001f * (float) sr)) * t.gain;
            if (std::abs (t.feed) < 1.0e-20f || ! std::isfinite (t.feed)) t.feed = 0.0f;
        }
        outL = wet[0]; outR = wet[1];
    }
private:
    struct Tank { DelayLine dif[3]; DelayLine dly; OnePole lp, hp; float feed = 0, gain = 0; } tank[2];
    struct SlowRnd { float cur = 0, tgt = 0, step = 0; double ph = 1.0; uint32_t s = 1; };
    SlowRnd rnd[4];
    float rndTick (int k, double hz)
    {
        auto& r = rnd[k];
        r.ph += hz / 1000.0;
        if (r.ph >= 1.0)
        {
            r.ph -= std::floor (r.ph);
            r.s = r.s * 1664525u + 1013904223u;
            r.tgt = (float) ((r.s >> 8) & 0xffff) / 32767.5f - 1.0f;
            r.step = (float) ((r.tgt - r.cur) * hz / 1000.0);
        }
        r.cur += r.step;
        if ((r.step > 0 && r.cur > r.tgt) || (r.step < 0 && r.cur < r.tgt)) r.cur = r.tgt;
        return r.cur;
    }
    double sr = 48000.0, lfoPh = 0.0;
    float ctlCount = 0, dlyMs[2][4] {}, dlyTgt[2][4] {}, dff = 0.5f, gTgt[2] {};
};

// a mono sample for SCHAEFER (shared, immutable once published)
struct SampleData
{
    std::vector<float> l, r;
    double rate = 48000.0;
    std::string name;
    size_t size() const { return l.size(); }
    inline float at (const std::vector<float>& ch, double pos) const
    {
        const size_t n = ch.size();
        if (n < 2) return 0.0f;
        while (pos < 0) pos += (double) n;
        const size_t i = (size_t) pos % n; const float f = (float) (pos - std::floor (pos));
        return ch[i] + (ch[(i + 1) % n] - ch[i]) * f;
    }
};
} // namespace nms

namespace nms
{
// iterative radix-2 FFT, in place; n must be a power of two
inline void fft (float* re, float* im, int n, bool inverse)
{
    for (int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
    }
    for (int len = 2; len <= n; len <<= 1)
    {
        const float ang = (inverse ? 2.0f : -2.0f) * kPi / (float) len;
        const float wr = std::cos (ang), wi = std::sin (ang);
        for (int i = 0; i < n; i += len)
        {
            float cr = 1.0f, ci = 0.0f;
            for (int j = 0; j < len / 2; ++j)
            {
                const int a = i + j, b = a + len / 2;
                const float tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
                re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
                const float nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
            }
        }
    }
    if (inverse) for (int i = 0; i < n; ++i) { re[i] /= (float) n; im[i] /= (float) n; }
}
} // namespace nms
