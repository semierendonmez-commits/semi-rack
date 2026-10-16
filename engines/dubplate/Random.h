// DUBPLATE (SEMI Family port): a JUCE-free stand-in for juce::Random with the same
// generator (48-bit LCG) and the same derived calls, so seeded templates, patterns and
// forms come out exactly as in the plugin.
#pragma once
#include <cstdint>
#include <limits>

namespace dubplate
{
class Random
{
public:
    explicit Random (int64_t seedValue) : seed (seedValue) {}

    int nextInt()
    {
        seed = (int64_t) ((((uint64_t) seed * 0x5deece66dULL) + 11) & 0xffffffffffffULL);
        return (int) (seed >> 16);
    }
    int nextInt (int maxValue) { return (int) ((((uint64_t) (unsigned int) nextInt()) * (uint64_t) maxValue) >> 32); }
    int nextInt (int start, int end) { return start + nextInt (end - start); }   // juce::Range<int> (start, end)
    bool nextBool() { return (nextInt() & 0x40000000) != 0; }
    float nextFloat()
    {
        const float r = (float) (uint32_t) nextInt() / ((float) std::numeric_limits<uint32_t>::max() + 1.0f);
        const float top = 1.0f - std::numeric_limits<float>::epsilon();
        return r < top ? r : top;
    }

private:
    int64_t seed;
};
} // namespace dubplate
