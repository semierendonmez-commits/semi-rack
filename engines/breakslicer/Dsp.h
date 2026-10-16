// Small DSP building blocks: biquads, Butterworth cascades, the offline
// windowed-sinc resampler, the QMF half-band pair, and the sampler quantisers.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include "Types.h"

namespace gtbs
{

constexpr double kPi = 3.14159265358979323846;

//==============================================================================
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    inline float process (float x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
    void reset() noexcept { z1 = z2 = 0; }

    // RBJ cookbook low-pass
    void lowpass (double fs, double fc, double q) noexcept
    {
        fc = std::clamp (fc, 10.0, fs * 0.49);
        const double w = 2.0 * kPi * fc / fs, c = std::cos (w), s = std::sin (w), al = s / (2.0 * q);
        const double a0 = 1.0 + al;
        b0 = (1.0 - c) * 0.5 / a0; b1 = (1.0 - c) / a0; b2 = b0;
        a1 = -2.0 * c / a0; a2 = (1.0 - al) / a0;
    }
    void highpass (double fs, double fc, double q) noexcept
    {
        fc = std::clamp (fc, 1.0, fs * 0.49);
        const double w = 2.0 * kPi * fc / fs, c = std::cos (w), s = std::sin (w), al = s / (2.0 * q);
        const double a0 = 1.0 + al;
        b0 = (1.0 + c) * 0.5 / a0; b1 = -(1.0 + c) / a0; b2 = b0;
        a1 = -2.0 * c / a0; a2 = (1.0 - al) / a0;
    }
};

// One-pole low-pass (the A500's RC filter)
struct OnePole
{
    float a = 1.0f, z = 0.0f;
    void lowpass (double fs, double fc) noexcept { a = (float) (1.0 - std::exp (-2.0 * kPi * fc / fs)); }
    inline float process (float x) noexcept { z += a * (x - z); return z; }
    void reset() noexcept { z = 0.0f; }
};

// Butterworth low-pass of even order as a cascade of biquads (order 2, 4, 6)
template <int Order>
struct Butterworth
{
    static_assert (Order % 2 == 0 && Order >= 2 && Order <= 8);
    std::array<Biquad, (size_t) (Order / 2)> s;
    double fs = 0, fc = 0;

    void lowpass (double sampleRate, double cutoff) noexcept
    {
        if (same (sampleRate, fs) && std::abs (cutoff - fc) < 1.0e-3) return;
        fs = sampleRate; fc = cutoff;
        for (size_t k = 0; k < (size_t) (Order / 2); ++k)
        {
            // pole angles of the analogue prototype
            const double theta = kPi * (2.0 * (double) k + 1.0) / (2.0 * Order);
            const double q = 1.0 / (2.0 * std::sin (theta));
            s[k].lowpass (sampleRate, cutoff, q);
        }
    }
    inline float process (float x) noexcept
    {
        for (auto& b : s) x = b.process (x);
        return x;
    }
    void reset() noexcept { for (auto& b : s) b.reset(); }
};

//==============================================================================
// Offline band-limited resampler (Blackman-windowed sinc). Used when the
// sampler's clock is set: never on the audio thread.
inline std::vector<float> resampleSinc (const std::vector<float>& in, double inRate, double outRate, int halfTaps = 16)
{
    if (in.empty()) return {};
    const double ratio = outRate / inRate;
    const int outLen = std::max (1, (int) std::floor ((double) in.size() * ratio));
    std::vector<float> out ((size_t) outLen);
    const double c = std::min (1.0, ratio) * 0.94;          // cut-off, fraction of the input Nyquist
    const double half = (double) halfTaps / c;              // wider kernel when decimating
    const int M = (int) std::ceil (half);                   // taps each side
    const int taps = 2 * M;
    constexpr int P = 512;                                  // fractional phases (polyphase table)

    // kernel for every phase, normalised: the sin / cos happen here once, not per sample
    std::vector<float> table ((size_t) ((P + 1) * taps));
    for (int p = 0; p <= P; ++p)
    {
        const double frac = (double) p / P;
        double norm = 0.0;
        for (int k = 0; k < taps; ++k)
        {
            const double d = frac - (double) (k - M + 1);   // t - j with j = floor (t) + (k - M + 1)
            double v = 0.0;
            if (std::abs (d) < half)
            {
                const double u = c * d;
                const double sinc = std::abs (u) < 1.0e-9 ? 1.0 : std::sin (kPi * u) / (kPi * u);
                const double wp = (d / half + 1.0) * 0.5;
                const double win = 0.42 - 0.5 * std::cos (2.0 * kPi * wp) + 0.08 * std::cos (4.0 * kPi * wp);
                v = c * sinc * win;
            }
            table[(size_t) (p * taps + k)] = (float) v;
            norm += v;
        }
        if (norm > 0.0) for (int k = 0; k < taps; ++k) table[(size_t) (p * taps + k)] = (float) (table[(size_t) (p * taps + k)] / norm);
    }

    const int n = (int) in.size();
    for (int i = 0; i < outLen; ++i)
    {
        const double t = (double) i / ratio;
        const int base = (int) std::floor (t);
        const int p = (int) std::lround ((t - base) * P);
        const float* kern = &table[(size_t) (p * taps)];
        const int j0 = base - M + 1;
        double acc = 0.0;
        if (j0 >= 0 && j0 + taps <= n)
            for (int k = 0; k < taps; ++k) acc += (double) kern[k] * in[(size_t) (j0 + k)];
        else
            for (int k = 0; k < taps; ++k)
            {
                const int j = j0 + k;
                if (j >= 0 && j < n) acc += (double) kern[k] * in[(size_t) j];
            }
        out[(size_t) i] = (float) acc;
    }
    return out;
}

//==============================================================================
// Quadrature mirror filter pair: a linear-phase half-band low-pass H0 and its
// mirror H1(z) = H0(-z). One analysis stage splits a signal into two bands at
// half the rate.
struct QmfStage
{
    std::vector<double> h0;

    QmfStage()
    {
        constexpr int N = 32;                               // even length, Johnston-style
        h0.resize (N);
        double sum = 0.0;
        for (int i = 0; i < N; ++i)
        {
            const double m = (double) i - (N - 1) * 0.5;
            const double u = 0.5 * m;
            const double sinc = std::sin (kPi * u) / (kPi * u);
            const double win = 0.54 - 0.46 * std::cos (2.0 * kPi * (double) i / (N - 1));
            h0[(size_t) i] = 0.5 * sinc * win;
            sum += h0[(size_t) i];
        }
        for (auto& v : h0) v /= sum;
    }

    void split (const std::vector<float>& x, std::vector<float>& lo, std::vector<float>& hi) const
    {
        const int n = (int) x.size(), N = (int) h0.size();
        lo.assign ((size_t) (n / 2), 0.0f);
        hi.assign ((size_t) (n / 2), 0.0f);
        for (int i = 0; i < n / 2; ++i)
        {
            const int c = 2 * i;
            double a = 0.0, b = 0.0;
            for (int k = 0; k < N; ++k)
            {
                const int j = c - k + N / 2;
                if (j < 0 || j >= n) continue;
                const double v = x[(size_t) j];
                a += h0[(size_t) k] * v;
                b += ((k & 1) ? -h0[(size_t) k] : h0[(size_t) k]) * v;   // H1(z) = H0(-z)
            }
            lo[(size_t) i] = (float) a;
            hi[(size_t) i] = (float) b;
        }
    }
};

//==============================================================================
// Section 5.1's VintageSamplerEmulation, extended with the S1000 and with a
// drive stage. processSample latches a new sample whenever the read phase
// crosses an integer (zero-order hold) in Paula mode, exactly as the template.
class VintageSamplerEmulation
{
public:
    enum class EmulationMode { Clean16Bit, AkaiS950_12Bit, AkaiS1000_16Bit, AmigaPaula8Bit };

    void prepare (double sampleRate) noexcept { outputSampleRate = sampleRate; lastPaulaSample = 0.0f; }
    void setMode (EmulationMode newMode) noexcept { currentMode = newMode; }

    [[nodiscard]] float processSample (float inputSample, double phase, double phaseInc) noexcept
    {
        if (currentMode == EmulationMode::AmigaPaula8Bit)
        {
            // 8-bit PCM quantization & zero-order hold aliasing
            const float sample = quantize8 (inputSample);
            if ((long long) std::floor (phase) != (long long) std::floor (phase - phaseInc))
                lastPaulaSample = sample;
            return lastPaulaSample;
        }
        if (currentMode == EmulationMode::AkaiS950_12Bit)
            return quantize12 (inputSample);
        if (currentMode == EmulationMode::AkaiS1000_16Bit)
            return quantize16 (inputSample);
        return inputSample;
    }

    // 8-bit signed PCM, as the template: floor (x * 127) / 127
    static float quantize8 (float x) noexcept { return std::floor (std::clamp (x, -1.0f, 1.0f) * 127.0f) / 127.0f; }

    // 12-bit non-linear: a gentle compander around the 2048-level truncation of
    // the template, so quiet tails keep more resolution than loud hits
    static float quantize12 (float x) noexcept
    {
        constexpr float levels = 2048.0f;
        x = std::clamp (x, -1.0f, 1.0f);
        const float s = x < 0.0f ? -1.0f : 1.0f;
        const float c = std::pow (std::abs (x), 0.8f);          // compress
        const float q = std::trunc (c * levels) / levels;       // template: trunc (x * 2048) / 2048
        return s * std::pow (q, 1.25f);                         // expand
    }

    static float quantize16 (float x) noexcept { return std::round (std::clamp (x, -1.0f, 1.0f) * 32767.0f) / 32767.0f; }

private:
    EmulationMode currentMode = EmulationMode::Clean16Bit;
    double outputSampleRate = 44100.0;
    float lastPaulaSample = 0.0f;
};

//==============================================================================
// Equal-power crossfade curves (eq. 3.1)
inline float gOut (float theta) noexcept { return std::cos (0.5f * (float) kPi * theta); }
inline float gIn  (float theta) noexcept { return std::sin (0.5f * (float) kPi * theta); }

inline uint32_t hash32 (uint32_t a, uint32_t b = 0, uint32_t c = 0) noexcept
{
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}

} // namespace gtbs
