// shorthand for parameter tables
#pragma once
#include "Module.h"

namespace il
{
inline PSpec F (const char* id, const char* name, float lo, float hi, float def, float centre, const char* unit, const char* tip)
{ return { id, name, PType::Float, lo, hi, def, centre, unit, "", tip, false }; }
inline PSpec FI (const char* id, const char* name, float lo, float hi, float def, const char* unit, const char* tip)
{ return { id, name, PType::Float, lo, hi, def, 0.0f, unit, "", tip, true }; }
inline PSpec C (const char* id, const char* name, const char* choices, int def, const char* tip)
{
    int n = 1;
    for (const char* c = choices; *c; ++c) if (*c == '|') ++n;
    return { id, name, PType::Choice, 0.0f, (float) (n - 1), (float) def, 0.0f, "", choices, tip, true };
}
inline PSpec B (const char* id, const char* name, bool def, const char* tip)
{ return { id, name, PType::Bool, 0.0f, 1.0f, def ? 1.0f : 0.0f, 0.0f, "", "", tip, true }; }

// beat-grid step counter shared by the rhythmic modules
struct StepClock
{
    long long last = -1;
    // true when sample i of the block starts a new step
    bool tick (const Ctx& c, int i, double divBeatsLen, long long& index)
    {
        const double beat = c.ppq + (double) i * c.beatsPerSample();
        index = (long long) std::floor (beat / divBeatsLen + 1.0e-9);
        if (index != last) { last = index; return true; }
        return false;
    }
    double phase (const Ctx& c, int i, double divBeatsLen) const
    {
        const double beat = c.ppq + (double) i * c.beatsPerSample();
        return fracd (beat / divBeatsLen);
    }
    void reset() { last = -1; }
};

inline uint32_t hash32 (uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
inline float hashUni (uint32_t a, uint32_t b) { return (float) (hash32 (a * 0x9E3779B1U ^ hash32 (b + 0x68E31DA4U)) >> 8) / 16777216.0f; }
} // namespace il
