// Vendored from SEEDWALK Source/engine; only change: wrapped in namespace sw.
// Sample-rate bridges for the Schwung modules, which run at their own fixed rates
// (Plaits at 48 kHz, the Move port of Clouds at 44.1 kHz, CloudSeed at 48 kHz) inside a
// host running at anything. Four-point Hermite interpolation, streaming, no allocation.
#pragma once

#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <vector>

// Semi-rack port: wrapped in sw:: (was ::fl) so it cannot collide at link time with the
// same-named bridges of RETICULUM / FLOATER, which also link into SemiFamily.
namespace sw
{
namespace fl
{

inline float hermite (float xm1, float x0, float x1, float x2, float t)
{
    const float c = (x1 - xm1) * 0.5f;
    const float v = x0 - x1;
    const float w = c + v;
    const float a = w + v + (x2 - x0) * 0.5f;
    const float b = w + a;
    return ((a * t - b) * t + c) * t + x0;
}

// C channels in at one rate, out at another. push() for a stream that arrives
// (an effect's input), pull() for a stream made on demand (a voice).
template <size_t C>
class Resampler
{
public:
    using Frame = std::array<float, C>;

    void setRates (double inRate, double outRate) { step = inRate / outRate; }
    void reset()
    {
        for (auto& f : ring) f.fill (0.0f);
        n = 0;
        pos = 1.0;
    }

    // feed one input frame; emit(const Frame&) is called for every output it completes
    template <class Emit>
    void push (const Frame& x, Emit&& emit)
    {
        feed (x);
        while (std::floor (pos) + 2.0 <= (double) (n - 1))
            emit (next());
    }

    // one output frame; gen() is asked for input frames as they are needed
    template <class Gen>
    Frame pull (Gen&& gen)
    {
        while (std::floor (pos) + 2.0 > (double) (n - 1))
            feed (gen());
        return next();
    }

private:
    void feed (const Frame& x)
    {
        ring[(size_t) (n & 7)] = x;
        ++n;
        if (n > (int64_t) 1 << 40) { n -= (int64_t) 1 << 39; pos -= (double) ((int64_t) 1 << 39); }
    }
    Frame next()
    {
        const auto i = (int64_t) std::floor (pos);
        const float t = (float) (pos - (double) i);
        Frame out {};
        const auto& a = ring[(size_t) ((i - 1) & 7)];
        const auto& b = ring[(size_t) (i & 7)];
        const auto& c = ring[(size_t) ((i + 1) & 7)];
        const auto& d = ring[(size_t) ((i + 2) & 7)];
        for (size_t ch = 0; ch < C; ++ch) out[ch] = hermite (a[ch], b[ch], c[ch], d[ch], t);
        pos += step;
        return out;
    }

    std::array<Frame, 8> ring {};
    int64_t n = 0;
    double pos = 1.0, step = 1.0;
};

// A stereo effect that works in fixed blocks at its own rate: the host's audio is
// resampled in, processed a block at a time, resampled out. Constant latency of one
// block plus a little (filled with silence at the start, so it never runs dry).
class BlockBridge
{
public:
    // process(inL, inR, outL, outR, n) at the module's rate, n = block
    void prepare (double hostRate, double moduleRate, int blockSize)
    {
        block = blockSize;
        down.setRates (hostRate, moduleRate);
        up.setRates (moduleRate, hostRate);
        down.reset();
        up.reset();
        inL.assign ((size_t) block, 0.0f); inR.assign ((size_t) block, 0.0f);
        outL.assign ((size_t) block, 0.0f); outR.assign ((size_t) block, 0.0f);
        fifo.assign (kFifo, { 0.0f, 0.0f });
        inCount = 0;
        rd = 0;
        wr = block * 2;   // pre-roll
    }

    template <class Process>
    void run (const float* hostInL, const float* hostInR, float* hostOutL, float* hostOutR, int n, Process&& process)
    {
        for (int i = 0; i < n; ++i)
        {
            down.push ({ hostInL[i], hostInR[i] }, [&] (const Resampler<2>::Frame& f)
            {
                inL[(size_t) inCount] = f[0];
                inR[(size_t) inCount] = f[1];
                if (++inCount == block)
                {
                    process (inL.data(), inR.data(), outL.data(), outR.data(), block);
                    for (int k = 0; k < block; ++k) fifo[(size_t) ((wr + k) % kFifo)] = { outL[(size_t) k], outR[(size_t) k] };
                    wr += block;
                    inCount = 0;
                }
            });
        }
        for (int i = 0; i < n; ++i)
        {
            const auto f = up.pull ([&]
            {
                Resampler<2>::Frame x { 0.0f, 0.0f };
                if (rd < wr) { const auto& s = fifo[(size_t) (rd % kFifo)]; x = { s[0], s[1] }; ++rd; }
                return x;
            });
            hostOutL[i] = f[0];
            hostOutR[i] = f[1];
        }
        if (rd > kFifo * 64) { rd -= kFifo * 32; wr -= kFifo * 32; }
    }

private:
    static constexpr int kFifo = 1 << 15;
    Resampler<2> down, up;
    std::vector<float> inL, inR, outL, outR;
    std::vector<std::array<float, 2>> fifo;
    int block = 32, inCount = 0;
    int64_t rd = 0, wr = 0;
};

} // namespace fl
} // namespace sw
