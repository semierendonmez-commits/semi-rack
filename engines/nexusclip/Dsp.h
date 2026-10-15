// NEXUSCLIP CTZ - DSP building blocks. No JUCE here so the engine can be
// tested on its own (tests/EngineTest.cpp).
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <deque>
#include <vector>

namespace nc
{
constexpr float kPi = 3.14159265358979f;

inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g, float floorDb = -120.0f) { return g > 0.0f ? std::max (floorDb, 20.0f * std::log10 (g)) : floorDb; }
inline float clampf (float x, float a, float b) { return x < a ? a : (x > b ? b : x); }

//==============================================================================
// In-place iterative radix-2 complex FFT. Sizes used here are 256..2048.
class Fft
{
public:
    void prepare (int n)
    {
        size = n;
        bits = 0;
        while ((1 << bits) < n) ++bits;
        rev.assign ((size_t) n, 0);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
        tw.resize ((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i) tw[(size_t) i] = std::polar (1.0f, -2.0f * kPi * (float) i / (float) n);
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
        for (int i = 0; i < size; ++i)
        {
            const int r = rev[(size_t) i];
            if (r > i) std::swap (d[i], d[r]);
        }
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
// Integer sample delay, per channel.
class Delay
{
public:
    void prepare (int channels, int maxDelay)
    {
        buf.assign ((size_t) channels, std::vector<float> ((size_t) std::max (1, maxDelay + 1), 0.0f));
        pos = 0;
        len = std::max (1, maxDelay + 1);
    }
    void setDelay (int d) { delay = std::min (std::max (0, d), len - 1); }
    int getDelay() const { return delay; }
    void reset() { for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f); pos = 0; }
    // process a block in place
    void process (float* const* ch, int nch, int n)
    {
        if (delay == 0) return;
        int p = pos;
        for (int c = 0; c < nch && c < (int) buf.size(); ++c)
        {
            auto& b = buf[(size_t) c];
            p = pos;
            for (int i = 0; i < n; ++i)
            {
                b[(size_t) p] = ch[c][i];
                int r = p - delay; if (r < 0) r += len;
                ch[c][i] = b[(size_t) r];
                if (++p == len) p = 0;
            }
        }
        pos = p;
    }
private:
    std::vector<std::vector<float>> buf;
    int pos = 0, len = 1, delay = 0;
};

//==============================================================================
// ITU-R BS.1770 K-weighting (pre-filter shelf + RLB high-pass), coefficients
// derived for any sample rate (the published 48 kHz values fall out exactly).
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;
    float process (float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
    void reset() { z1 = z2 = 0; }
};

inline void kWeighting (double sr, Biquad& shelf, Biquad& hp)
{
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (3.14159265358979 * f0 / sr);
        const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
        shelf.b1 = 2.0 * (K * K - Vh) / a0;
        shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
        shelf.a1 = 2.0 * (K * K - 1.0) / a0;
        shelf.a2 = (1.0 - K / Q + K * K) / a0;
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (3.14159265358979 * f0 / sr);
        const double a0 = 1.0 + K / Q + K * K;
        hp.b0 = 1.0; hp.b1 = -2.0; hp.b2 = 1.0;
        hp.a1 = 2.0 * (K * K - 1.0) / a0;
        hp.a2 = (1.0 - K / Q + K * K) / a0;
    }
}

//==============================================================================
// Sliding-window statistics over a stream of fixed-size cells (one cell =
// `cell` samples). Used for the 600 ms RMS / peak (crest factor) and the
// 400 ms momentary loudness.
class Window
{
public:
    void prepare (int cells) { sum.assign ((size_t) std::max (1, cells), 0.0); peak.assign ((size_t) std::max (1, cells), 0.0f); at = 0; total = 0; }
    void push (double sumSq, float pk)
    {
        total += sumSq - sum[(size_t) at];
        sum[(size_t) at] = sumSq;
        peak[(size_t) at] = pk;
        at = (at + 1) % (int) sum.size();
        if (total < 0) total = 0;
    }
    double meanSquare (double samplesPerCell) const { return total / (samplesPerCell * (double) sum.size()); }
    float maxPeak() const { float m = 0; for (auto p : peak) m = std::max (m, p); return m; }
    void reset() { std::fill (sum.begin(), sum.end(), 0.0); std::fill (peak.begin(), peak.end(), 0.0f); total = 0; }
private:
    std::vector<double> sum;
    std::vector<float> peak;
    int at = 0;
    double total = 0;
};
} // namespace nc
