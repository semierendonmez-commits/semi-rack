// The tail of the Reverb Drill chain: a small Schroeder / Moorer reverb (8 damped
// combs + 4 all-passes per side). Buffers are sized in prepare(), never on the audio thread.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace gtbs
{

class SendReverb
{
public:
    void prepare (double fs)
    {
        static const int combL[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static const int apL[4]   = { 556, 441, 341, 225 };
        const double k = fs / 44100.0;
        for (size_t s = 0; s < 2; ++s)
        {
            for (size_t i = 0; i < 8; ++i)
            {
                combs[s][i].buf.assign ((size_t) std::max (8, (int) ((combL[i] + (int) s * 23) * k)), 0.0f);
                combs[s][i].pos = 0; combs[s][i].store = 0.0f;
            }
            for (size_t i = 0; i < 4; ++i)
            {
                aps[s][i].buf.assign ((size_t) std::max (8, (int) ((apL[i] + (int) s * 23) * k)), 0.0f);
                aps[s][i].pos = 0;
            }
        }
    }

    void reset()
    {
        for (auto& side : combs) for (auto& c : side) { std::fill (c.buf.begin(), c.buf.end(), 0.0f); c.store = 0.0f; }
        for (auto& side : aps) for (auto& a : side) std::fill (a.buf.begin(), a.buf.end(), 0.0f);
    }

    void set (float size, float damp) noexcept
    {
        feedback = 0.70f + 0.22f * std::clamp (size, 0.0f, 1.0f);   // tops out at 0.92: long, never a runaway wash
        damping = 0.05f + 0.6f * std::clamp (damp, 0.0f, 1.0f);
    }

    inline void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        const float in = (inL + inR) * 0.015f;
        float acc[2] = { 0.0f, 0.0f };
        for (size_t s = 0; s < 2; ++s)
        {
            for (auto& c : combs[s])
            {
                float& slot = c.buf[(size_t) c.pos];
                const float y = slot;
                c.store = y * (1.0f - damping) + c.store * damping;
                slot = in + c.store * feedback;
                if (++c.pos >= (int) c.buf.size()) c.pos = 0;
                acc[s] += y;
            }
            for (auto& a : aps[s])
            {
                float& slot = a.buf[(size_t) a.pos];
                const float b = slot;
                slot = acc[s] + b * 0.5f;
                acc[s] = b - acc[s];
                if (++a.pos >= (int) a.buf.size()) a.pos = 0;
            }
        }
        outL = acc[0];
        outR = acc[1];
    }

private:
    struct Comb { std::vector<float> buf; int pos = 0; float store = 0.0f; };
    struct AllPass { std::vector<float> buf; int pos = 0; };
    std::array<std::array<Comb, 8>, 2> combs;
    std::array<std::array<AllPass, 4>, 2> aps;
    float feedback = 0.84f, damping = 0.3f;
};

} // namespace gtbs
