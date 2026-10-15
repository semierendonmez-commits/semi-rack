// Copied from SEEDWALK ThirdParty/artifex/rack.hpp: only for tests/seedwalk_engine.cpp (not the plugin build).
// The handful of VCV Rack SDK names the artifex core (forsitan modulare, GPL-3.0) uses,
// so its headers compile unchanged outside Rack. Same semantics as Rack's own
// (include/math.hpp, include/dsp/digital.hpp).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace rack
{
inline int clamp (int x, int a, int b) { return std::max (std::min (x, b), a); }
inline float clamp (float x, float a = 0.f, float b = 1.f) { return std::fmax (std::fmin (x, b), a); }
inline float crossfade (float a, float b, float p) { return a + (b - a) * p; }
inline float rescale (float x, float xMin, float xMax, float yMin, float yMax)
{
    return yMin + (x - xMin) / (xMax - xMin) * (yMax - yMin);
}

namespace dsp
{
struct SchmittTrigger
{
    bool state = true;
    void reset() { state = true; }
    bool process (float in, float lowThreshold = 0.f, float highThreshold = 1.f)
    {
        if (state) { if (in <= lowThreshold) state = false; }
        else if (in >= highThreshold) { state = true; return true; }
        return false;
    }
    bool isHigh() const { return state; }
};
} // namespace dsp
} // namespace rack
