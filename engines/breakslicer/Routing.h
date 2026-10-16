// Chapter 4: the weighted probabilistic routing matrix and the Markov chain.
#pragma once

#include "Types.h"
#include <algorithm>
#include <array>
#include <random>

namespace gtbs
{

//==============================================================================
// Section 4.1. On every triggered slice a std::mt19937 draws a uniform R in [0, 1)
// that is evaluated against the weighted distribution of the five DSP chains.
//
// Real-time note: std::discrete_distribution allocates when its weights change,
// so the weights live in a fixed cumulative table instead; the draw is the same
// (one uniform R against normalised cumulative weights) and nothing allocates.
class ProbabilisticRoutingEngine
{
public:
    using DSPTargetChain = Chain;
    static constexpr size_t kCount = NumChains;

    ProbabilisticRoutingEngine() : randomEngine (std::random_device {}())
    {
        // default weighted probability distribution (section 4.1)
        updateDistribution ({ 0.55f, 0.25f, 0.08f, 0.06f, 0.06f });
    }

    /** seeded explicitly (no std::random_device): safe to build anywhere */
    explicit ProbabilisticRoutingEngine (uint32_t seedValue) : randomEngine (seedValue)
    {
        updateDistribution ({ 0.55f, 0.25f, 0.08f, 0.06f, 0.06f });
    }

    void seed (uint32_t s) noexcept { randomEngine.seed (s); }

    void updateDistribution (const std::array<float, kCount>& weights) noexcept
    {
        float sum = 0.0f;
        for (auto w : weights) sum += std::max (0.0f, w);
        float acc = 0.0f;
        for (size_t i = 0; i < kCount; ++i)
        {
            acc += sum > 0.0f ? std::max (0.0f, weights[i]) / sum : (i == 0 ? 1.0f : 0.0f);
            cumulative[i] = acc;
        }
        cumulative[kCount - 1] = 1.0f;
    }

    /** one draw against raw weights, no engine needed (the pattern generator) */
    [[nodiscard]] static DSPTargetChain evaluate (const std::array<float, kCount>& weights, float R) noexcept
    {
        float sum = 0.0f;
        for (auto w : weights) sum += std::max (0.0f, w);
        if (sum <= 0.0f) return (DSPTargetChain) 0;
        float acc = 0.0f;
        for (size_t i = 0; i < kCount; ++i)
        {
            acc += std::max (0.0f, weights[i]) / sum;
            if (R < acc) return (DSPTargetChain) i;
        }
        return (DSPTargetChain) (kCount - 1);
    }

    /** probability of one chain under the current weights */
    float probability (size_t i) const noexcept { return cumulative[i] - (i > 0 ? cumulative[i - 1] : 0.0f); }

    [[nodiscard]] DSPTargetChain evaluateNextSliceTarget() noexcept
    {
        // real-time lock-free evaluation on the audio thread
        return evaluate (uniform (randomEngine));
    }

    /** the same evaluation for an R drawn elsewhere (the generator's own seeded stream) */
    [[nodiscard]] DSPTargetChain evaluate (float R) const noexcept
    {
        for (size_t i = 0; i < kCount; ++i)
            if (R < cumulative[i]) return (DSPTargetChain) i;
        return (DSPTargetChain) (kCount - 1);
    }

private:
    std::mt19937 randomEngine;
    std::uniform_real_distribution<float> uniform { 0.0f, 1.0f };
    std::array<float, kCount> cumulative {};
};

//==============================================================================
// Section 4.2: first-order Markov chain over {Kick, Snare, Ghost, Hat}.
// Rows are normalised on use so sum_j P_ij = 1 whatever the controls say.
struct MarkovChain
{
    std::array<std::array<float, NumCls>, NumCls> p {{
        //  ->K    ->S    ->G    ->H
        {{ 0.15f, 0.40f, 0.20f, 0.25f }},   // from Kick
        {{ 0.40f, 0.10f, 0.30f, 0.20f }},   // from Snare
        {{ 0.30f, 0.30f, 0.20f, 0.20f }},   // from Ghost
        {{ 0.35f, 0.35f, 0.15f, 0.15f }},   // from Hat
    }};

    float prob (int from, int to) const noexcept
    {
        float sum = 0.0f;
        for (auto v : p[(size_t) from]) sum += std::max (0.0f, v);
        return sum > 0.0f ? std::max (0.0f, p[(size_t) from][(size_t) to]) / sum : 0.25f;
    }
};

} // namespace gtbs
