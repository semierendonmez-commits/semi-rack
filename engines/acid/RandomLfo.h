// Random modulation source for one 303 control.
//   trig — a new random value on every note the sequencer plays
//   s&h  — a new random value every `rate` (tempo-synced), held
// Both can be smoothed: the output glides to each new value with a
// time constant of `smoothMs` (0 = hard steps).
// Output is bipolar, -1..+1; the engine scales it by depth around the knob.
// SEMI Family: vendored from semierendonmez-commits/acid Source/engine; only change: wrapped in namespace acid
// (one plugin binary links ~35 engines).
#pragma once
#include <cmath>
#include <cstdint>

namespace acid {

struct LfoSettings
{
    float depth    = 0.0f;   // 0..1
    int   mode     = 1;      // 0 trig, 1 s&h
    int   rate     = 3;      // index into RandomLfo::rateBeats
    float smoothMs = 0.0f;   // 0..2000
};

class RandomLfo
{
public:
    static constexpr int numRates = 8;
    static constexpr double rateBeats[numRates] = { 0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };

    explicit RandomLfo (uint32_t seed = 1) : rng (seed ? seed : 1) {}

    void trigger() { trigValue = next(); }

    // dtSeconds: time since the previous call (for the smoothing).
    float value (double ppq, const LfoSettings& s, double dtSeconds)
    {
        float target;
        if (s.mode == 0)
            target = trigValue;
        else
        {
            const double beats = rateBeats[s.rate < 0 ? 0 : (s.rate >= numRates ? numRates - 1 : s.rate)];
            const long idx = (long) std::floor (ppq / beats);
            if (idx != segment) { shValue = next(); segment = idx; }
            target = shValue;
        }

        if (s.smoothMs < 0.5f || dtSeconds <= 0.0)
            out = target;
        else
        {
            const double coef = 1.0 - std::exp (-dtSeconds / (s.smoothMs * 0.001));
            out += (float) ((target - out) * coef);
        }
        return out;
    }

private:
    float next()
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (float) ((double) rng / 4294967295.0 * 2.0 - 1.0);
    }

    uint32_t rng;
    long  segment = -1000000;
    float trigValue = 0.0f, shValue = 0.0f, out = 0.0f;
};

} // namespace acid
