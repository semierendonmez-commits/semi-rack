// DUBPLATE — host time -> arrangement. SEMI Family port of Source/Audio/Timeline.h
// (juce::jmin / jmax / jlimit -> std; otherwise unchanged).
#pragma once
#include "Arrangement.h"
#include <algorithm>
#include <cmath>

namespace dubplate
{
/** Maps host time (ppq / absolute 16th steps) onto the arrangement, per block.
    Cheap to rebuild; no allocation. */
struct Timeline
{
    static constexpr int maxSections = 64;

    const Arrangement* arr = nullptr;
    int totalBars = 1;
    int barOffset = 0;
    bool loop = true;
    int numSections = 0;
    std::array<int, maxSections + 1> startBar {};
    std::array<int, maxSections> dropOrdinal {}; // 0 = first drop, 1 = second ...

    void build (const Arrangement& a, int offsetBars, bool loopArrangement)
    {
        arr = &a;
        barOffset = offsetBars;
        loop = loopArrangement;
        numSections = std::min (maxSections, (int) a.sections.size());
        int bar = 0, drops = 0;
        for (int i = 0; i < numSections; ++i)
        {
            startBar[(size_t) i] = bar;
            bar += a.sections[(size_t) i].bars;
            dropOrdinal[(size_t) i] = a.sections[(size_t) i].type == SectionType::Drop ? drops++ : -1;
        }
        startBar[(size_t) numSections] = bar;
        totalBars = std::max (1, bar);
    }

    /** Absolute 16th step -> arrangement step, or -1 when outside a non-looping arrangement. */
    long long arrStep (long long absStep) const
    {
        long long s = absStep + (long long) barOffset * 16;
        const long long total = (long long) totalBars * 16;
        if (loop)
        {
            s %= total;
            if (s < 0) s += total;
            return s;
        }
        return (s >= 0 && s < total) ? s : -1;
    }

    double arrPpq (double ppq) const
    {
        double p = ppq + barOffset * 4.0;
        const double total = totalBars * 4.0;
        if (loop)
        {
            p = std::fmod (p, total);
            if (p < 0) p += total;
            return p;
        }
        return (p >= 0.0 && p < total) ? p : -1.0;
    }

    int sectionOfBar (int bar) const
    {
        for (int i = 0; i < numSections; ++i)
            if (bar < startBar[(size_t) i + 1])
                return i;
        return -1;
    }

    int sectionStart (int si) const { return startBar[(size_t) std::max (0, std::min (numSections, si))]; }
    int sectionEnd (int si) const   { return startBar[(size_t) std::max (0, std::min (numSections, si + 1))]; }
};

/** Deterministic hash for seeded per-bar / per-step decisions (identical in playback and export). */
inline float hash01 (uint32_t a, uint32_t b, uint32_t c = 0)
{
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA6Bu ^ (c + 0x165667B1u) * 0xC2B2AE35u;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return (float) (h & 0xFFFFFF) / (float) 0x1000000;
}
} // namespace dubplate
