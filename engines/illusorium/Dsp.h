// ILLUSORIUM - DSP building blocks. Plain C++17, no JUCE, so the engine can be
// tested on its own (tests/EngineTest.cpp).
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace il
{
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g, float floorDb = -120.0f) { return g > 0.0f ? std::max (floorDb, 20.0f * std::log10 (g)) : floorDb; }
inline float clampf (float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
inline float lerpf (float a, float b, float t) { return a + (b - a) * t; }
inline float midiToHz (float m) { return 440.0f * std::pow (2.0f, (m - 69.0f) / 12.0f); }
inline float hzToMidi (float f) { return 69.0f + 12.0f * std::log2 (std::max (1.0f, f) / 440.0f); }
inline float frac (float x) { return x - std::floor (x); }
inline double fracd (double x) { return x - std::floor (x); }
inline float sqr (float x) { return x * x; }

// equal-power pan, p in -1..1
inline void panGains (float p, float& l, float& r)
{
    const float a = (clampf (p, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
    l = std::cos (a); r = std::sin (a);
}

//==============================================================================
// splitmix64: cheap, deterministic, one per module
struct Rng
{
    uint64_t s = 0x9e3779b97f4a7c15ull;
    void seed (uint64_t v) { s = v * 0x9e3779b97f4a7c15ull + 0x632be59bd9b4e019ull; }
    uint64_t next()
    {
        uint64_t z = (s += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }
    float uni() { return (float) (next() >> 40) / (float) (1ull << 24); }   // [0, 1)
    float bi() { return uni() * 2.0f - 1.0f; }                              // [-1, 1)
    int below (int n) { return n <= 1 ? 0 : (int) (uni() * (float) n) % n; }
};

// white / pink noise source (Paul Kellet's refined pink filter)
struct Noise
{
    Rng rng;
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    float white() { return rng.bi(); }
    float pink()
    {
        const float w = rng.bi();
        b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f;
        b2 = 0.96900f * b2 + w * 0.1538520f; b3 = 0.86650f * b3 + w * 0.3104856f;
        b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
        const float p = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
        b6 = w * 0.115926f;
        return p * 0.11f;
    }
    float get (bool pinkNoise) { return pinkNoise ? pink() : white(); }
};

//==============================================================================
// one-pole smoother / low-pass
struct OnePole
{
    float a = 0.0f, z = 0.0f;
    void setMs (float ms, double sr) { a = ms <= 0.0f ? 0.0f : (float) std::exp (-1.0 / (0.001 * ms * sr)); }
    void setHz (float hz, double sr) { a = (float) std::exp (-kTwoPi * std::max (1.0f, hz) / sr); }
    float process (float x) { z = x + a * (z - x); return z; }
    void reset (float v = 0.0f) { z = v; }
};

// attack / release envelope follower on |x|
struct Env
{
    float att = 0, rel = 0, z = 0;
    void set (float attMs, float relMs, double sr)
    {
        att = (float) std::exp (-1.0 / (0.001 * std::max (0.01f, attMs) * sr));
        rel = (float) std::exp (-1.0 / (0.001 * std::max (0.01f, relMs) * sr));
    }
    float process (float x)
    {
        const float a = std::abs (x);
        z = a + (a > z ? att : rel) * (z - a);
        return z;
    }
    void reset() { z = 0; }
};

struct DcBlock
{
    float x1 = 0, y1 = 0, r = 0.995f;
    float process (float x) { const float y = x - x1 + r * y1; x1 = x; y1 = y; return y; }
    void reset() { x1 = y1 = 0; }
};

//==============================================================================
// RBJ biquad, transposed direct form II
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }

    enum Type { LP, HP, BP, Notch, AP, Peak, LowShelf, HighShelf };
    void set (Type t, double f, double q, double db, double sr)
    {
        f = std::min (std::max (f, 5.0), sr * 0.49);
        q = std::max (q, 0.05);
        const double w = 2.0 * 3.14159265358979 * f / sr, cw = std::cos (w), sw = std::sin (w);
        const double A = std::pow (10.0, db / 40.0), al = sw / (2.0 * q);
        double B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;
        switch (t)
        {
            case LP: B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2; A0 = 1 + al; A1 = -2 * cw; A2 = 1 - al; break;
            case HP: B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2; A0 = 1 + al; A1 = -2 * cw; A2 = 1 - al; break;
            case BP: B0 = al; B1 = 0; B2 = -al; A0 = 1 + al; A1 = -2 * cw; A2 = 1 - al; break;
            case Notch: B0 = 1; B1 = -2 * cw; B2 = 1; A0 = 1 + al; A1 = -2 * cw; A2 = 1 - al; break;
            case AP: B0 = 1 - al; B1 = -2 * cw; B2 = 1 + al; A0 = 1 + al; A1 = -2 * cw; A2 = 1 - al; break;
            case Peak: B0 = 1 + al * A; B1 = -2 * cw; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * cw; A2 = 1 - al / A; break;
            case LowShelf:
            {
                const double s = 2 * std::sqrt (A) * al;
                B0 = A * ((A + 1) - (A - 1) * cw + s); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - s);
                A0 = (A + 1) + (A - 1) * cw + s; A1 = -2 * ((A - 1) + (A + 1) * cw); A2 = (A + 1) + (A - 1) * cw - s;
                break;
            }
            case HighShelf:
            {
                const double s = 2 * std::sqrt (A) * al;
                B0 = A * ((A + 1) + (A - 1) * cw + s); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - s);
                A0 = (A + 1) - (A - 1) * cw + s; A1 = 2 * ((A - 1) - (A + 1) * cw); A2 = (A + 1) - (A - 1) * cw - s;
                break;
            }
        }
        b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0); a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
    }
};

// band-limit pair: high-pass + low-pass (12 dB/oct each), a broad "window"
struct Band
{
    Biquad hp, lp;
    void set (float lo, float hi, double sr) { hp.set (Biquad::HP, lo, 0.707, 0, sr); lp.set (Biquad::LP, std::max (lo * 1.05f, hi), 0.707, 0, sr); }
    float process (float x) { return lp.process (hp.process (x)); }
    void reset() { hp.reset(); lp.reset(); }
};

//==============================================================================
// power-of-two delay line with linear and cubic reads
class DelayLine
{
public:
    void prepare (int maxSamples)
    {
        int n = 16;
        while (n < maxSamples + 4) n <<= 1;
        buf.assign ((size_t) n, 0.0f);
        mask = n - 1;
        w = 0;
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); }
    int size() const { return mask + 1; }
    void push (float x) { w = (w + 1) & mask; buf[(size_t) w] = x; }
    float at (int d) const { return buf[(size_t) ((w - d) & mask)]; }    // d = 0: the newest sample
    float read (float d) const
    {
        d = clampf (d, 0.0f, (float) (mask - 3));
        const int i = (int) d;
        const float f = d - (float) i;
        return at (i) + f * (at (i + 1) - at (i));
    }
    float cubic (float d) const
    {
        d = clampf (d, 1.0f, (float) (mask - 3));
        const int i = (int) d;
        const float f = d - (float) i;
        const float ym1 = at (i - 1), y0 = at (i), y1 = at (i + 1), y2 = at (i + 2);
        const float c1 = 0.5f * (y1 - ym1), c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2, c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * f + c2) * f + c1) * f + y0;
    }
private:
    std::vector<float> buf { 0.0f };
    int mask = 0, w = 0;
};

//==============================================================================
// Granular delay-line pitch shifter: two taps sweep through a window and
// crossfade with sin^2 / cos^2 (they always sum to 1). Ratio 2 = an octave up.
// An anti-alias low-pass sits in front of upward shifts.
class Shifter
{
public:
    void prepare (double sampleRate, float maxWinMs = 160.0f)
    {
        sr = sampleRate;
        line.prepare ((int) (sr * maxWinMs * 0.001) + 16);
        setWindow (60.0f);
        reset();
    }
    void reset() { line.reset(); phase = 0.0f; aa.reset(); aa2.reset(); }
    void setWindow (float ms) { win = clampf ((float) (ms * 0.001 * sr), 64.0f, (float) line.size() - 8.0f); }
    void setRatio (float r)
    {
        r = clampf (r, 0.0625f, 16.0f);
        if (std::abs (r - ratio) > 1.0e-4f || aaOff)
        {
            ratio = r;
            aaOff = false;
            const float fc = (float) (sr * 0.45) / std::max (1.0f, ratio);
            if (ratio > 1.001f) { aa.set (Biquad::LP, fc, 0.7, 0, sr); aa2.set (Biquad::LP, fc, 0.7, 0, sr); useAa = true; }
            else useAa = false;
        }
    }
    float getRatio() const { return ratio; }
    float process (float x)
    {
        if (useAa) x = aa2.process (aa.process (x));
        line.push (x);
        phase += (1.0f - ratio) / win;
        phase -= std::floor (phase);
        float p2 = phase + 0.5f;
        if (p2 >= 1.0f) p2 -= 1.0f;
        const float ga = sqr (std::sin (kPi * phase));
        return line.read (phase * win + 2.0f) * ga + line.read (p2 * win + 2.0f) * (1.0f - ga);
    }
private:
    DelayLine line;
    Biquad aa, aa2;
    double sr = 48000.0;
    float win = 2000.0f, phase = 0.0f, ratio = 1.0f;
    bool useAa = false, aaOff = true;
};

//==============================================================================
// 90-degree phase-difference network (Olli Niemitalo's 2 x 4 allpass pair).
// i and q are in quadrature from ~20 Hz to ~20 kHz at 44.1 / 48 kHz.
class Hilbert
{
public:
    void reset() { for (auto& s : st) s = {}; d1 = 0; }
    void process (float x, float& i, float& q)
    {
        static const float ca[4] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
        static const float cb[4] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
        float a = x, b = x;
        for (int k = 0; k < 4; ++k) a = stage (st[(size_t) k], ca[k] * ca[k], a);
        for (int k = 0; k < 4; ++k) b = stage (st[(size_t) k + 4], cb[k] * cb[k], b);
        i = d1; d1 = a;      // one-sample delay on the first path
        q = b;
    }
private:
    struct S { float x1 = 0, x2 = 0, y1 = 0, y2 = 0; };
    static float stage (S& s, float c, float x)
    {
        const float y = c * (x + s.y2) - s.x2;
        s.x2 = s.x1; s.x1 = x; s.y2 = s.y1; s.y1 = y;
        return y;
    }
    std::array<S, 8> st {};
    float d1 = 0;
};

// single-sideband frequency shifter (shift in Hz, + = up)
class FreqShifter
{
public:
    void prepare (double sampleRate) { sr = sampleRate; reset(); }
    void reset() { h.reset(); ph = 0.0; }
    float process (float x, float shiftHz)
    {
        float i, q;
        h.process (x, i, q);
        ph += shiftHz / sr;
        ph -= std::floor (ph);
        const float a = (float) (ph * 6.283185307179586);
        return i * std::cos (a) + q * std::sin (a);
    }
private:
    Hilbert h;
    double sr = 48000.0, ph = 0.0;
};

//==============================================================================
// In-place iterative radix-2 complex FFT (from the sibling NEXUSCLIP engine).
class Fft
{
public:
    void prepare (int n)
    {
        size = n; bits = 0;
        while ((1 << bits) < n) ++bits;
        rev.assign ((size_t) n, 0);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
        tw.resize ((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i) tw[(size_t) i] = std::polar (1.0f, -kTwoPi * (float) i / (float) n);
    }
    int getSize() const { return size; }
    void forward (std::complex<float>* d) const { run (d, false); }
    void inverse (std::complex<float>* d) const
    {
        run (d, true);
        const float s = 1.0f / (float) size;
        for (int i = 0; i < size; ++i) d[i] *= s;
    }
private:
    void run (std::complex<float>* d, bool inv) const
    {
        for (int i = 0; i < size; ++i) { const int r = rev[(size_t) i]; if (r > i) std::swap (d[i], d[r]); }
        for (int len = 2; len <= size; len <<= 1)
        {
            const int half = len / 2, step = size / len;
            for (int i = 0; i < size; i += len)
                for (int j = 0; j < half; ++j)
                {
                    auto w = tw[(size_t) (j * step)];
                    if (inv) w = std::conj (w);
                    const auto u = d[i + j], v = d[i + j + half] * w;
                    d[i + j] = u + v;
                    d[i + j + half] = u - v;
                }
        }
    }
    int size = 0, bits = 0;
    std::vector<int> rev;
    std::vector<std::complex<float>> tw;
};

//==============================================================================
// YIN pitch tracker on a 4x decimated mono stream (50 Hz .. ~1.5 kHz).
class Yin
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate / kDec;
        buf.assign (kLen, 0.0f);
        d.assign (kTau + 1, 0.0f);
        pos = 0; acc = 0; cnt = 0; sinceHop = 0;
        lp1.setHz (2000.0f, sampleRate); lp2.setHz (2000.0f, sampleRate);
        hz = 0; conf = 0;
    }
    // returns true when a new estimate is ready
    bool push (float x)
    {
        x = lp2.process (lp1.process (x));
        acc += x;
        if (++cnt < kDec) return false;
        const float v = acc / (float) kDec;
        acc = 0; cnt = 0;
        for (int i = 0; i < kLen - 1; ++i) buf[(size_t) i] = buf[(size_t) i + 1];   // short buffer: a shift is fine
        buf[(size_t) kLen - 1] = v;
        if (++sinceHop < kHop) return false;
        sinceHop = 0;
        estimate();
        return true;
    }
    float hz = 0, conf = 0;
private:
    void estimate()
    {
        const int W = kLen - kTau;
        d[0] = 1.0f;
        float running = 0;
        int best = -1;
        for (int tau = 1; tau <= kTau; ++tau)
        {
            float s = 0;
            for (int j = 0; j < W; ++j) { const float e = buf[(size_t) j] - buf[(size_t) (j + tau)]; s += e * e; }
            running += s;
            d[(size_t) tau] = running > 0 ? s * (float) tau / running : 1.0f;
        }
        for (int tau = 3; tau < kTau; ++tau)
            if (d[(size_t) tau] < 0.15f)
            {
                while (tau + 1 < kTau && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau;
                best = tau; break;
            }
        if (best < 0) { conf = 0; return; }
        const float a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
        const float den = a + c - 2 * b;
        const float t = (float) best + (std::abs (den) > 1e-9f ? 0.5f * (a - c) / den : 0.0f);
        hz = (float) (sr / t);
        conf = 1.0f - b;
    }
    static constexpr int kDec = 4, kTau = 240, kLen = 240 + 384, kHop = 96;
    double sr = 12000.0;
    std::vector<float> buf, d;
    OnePole lp1, lp2;
    float acc = 0;
    int pos = 0, cnt = 0, sinceHop = 0;
};

// sync divisions shared by every rhythmic module
inline const char* divChoices() { return "1/32|1/16T|1/16|1/8T|1/8|1/4T|1/4|1/2|1 BAR|2 BARS|4 BARS"; }
inline double divBeats (int i)
{
    static const double b[] = { 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0, 2.0, 4.0, 8.0, 16.0 };
    return b[std::min (std::max (i, 0), 10)];
}
} // namespace il
