// SetbookMorph: how a parameter travels from one scene to another. Shared by SETBOOK and every
// sibling (the sibling does the morph itself, at its own rate, so SETBOOK sends one position per
// frame instead of every value of every plug-in). One header, no JUCE, no allocation.
//
// A morph has up to four corners (SEEDWALK's matrix interpolation, lifted to the family):
//
//      corner 2 (0,1) ─────── corner 3 (1,1)          a timed recall uses the bottom edge only:
//          │                       │                    corner 0 = where the sibling is now,
//          │      (x, y)           │                    corner 1 = the scene, y = 0, x: 0 -> 1
//          │                       │                  the XY pad uses all four: four scenes
//      corner 0 (0,0) ─────── corner 1 (1,0)
//
// A corner the sibling was not given takes the value the parameter had when the morph was armed
// (the "base"), so a scene that leaves a parameter out leaves it where it is.
//
// Continuous parameters glide: bilinear in curve(x), curve(y).
// Discrete parameters (choices, ints, switches) cannot glide, so they follow a rule:
//   JUMP    switch to the nearer corner when x (and y) cross the THRESHOLD (default 0.5)
//   WALK    glide, then snap to the parameter's steps (a choice walks through the ones between)
//   START   switch as soon as the morph leaves the corner (x > 0)
//   END     switch only when the morph arrives (x = 1)
//   FIXED   never moved by SETBOOK (a file path, a link channel, a theme)
// The threshold is measured on the raw position (time), not the curved one, so JUMP at 0.5 lands
// in the middle of the morph's duration whatever the curve.
#pragma once

#include <cstdint>
#include <cmath>

namespace setbook
{

enum Kind : uint8_t { Continuous = 0, Stepped = 1, Switch = 2 };

enum Rule : uint8_t
{
    RuleAuto = 0,     // Continuous: GLIDE, Stepped: the global stepped mode, Switch: JUMP
    RuleGlide,        // Continuous glide (Stepped: WALK)
    RuleJump,
    RuleStart,
    RuleEnd,
    RuleFixed,
    kNumRules
};

enum Curve : uint8_t { CurveLinear = 0, CurveSmooth, CurveEarly, CurveLate, kNumCurves };

inline const char* ruleName (int r)
{
    static const char* n[] = { "AUTO", "GLIDE", "JUMP", "START", "END", "FIXED" };
    return r >= 0 && r < kNumRules ? n[r] : "?";
}
inline const char* curveName (int c)
{
    static const char* n[] = { "LINEAR", "SMOOTH", "EARLY", "LATE" };
    return c >= 0 && c < kNumCurves ? n[c] : "?";
}

// the same four shapes as SEEDWALK's glide
inline float curve (int c, float t)
{
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    switch (c)
    {
        case CurveLinear: return t;
        case CurveLate:   return t * t * t;
        case CurveEarly:  { const float u = 1.0f - t; return 1.0f - u * u * u; }
        default:          return t * t * (3.0f - 2.0f * t);
    }
}

// What a sibling says about one parameter. Values are always normalised 0..1.
struct Desc
{
    uint32_t id = 0;        // a hash of the parameter's id (idHash), the same in every session
    Kind kind = Continuous;
    uint16_t steps = 0;     // Stepped / Switch: how many positions (a bool has 2), 0 for Continuous
    Rule rule = RuleAuto;   // what the sibling wants; SETBOOK can override it per scene
};

// FNV-1a: the parameter's string id -> the id SETBOOK stores
inline uint32_t idHash (const char* s)
{
    uint32_t h = 2166136261u;
    for (; s != nullptr && *s != 0; ++s) { h ^= (uint8_t) *s; h *= 16777619u; }
    return h == 0 ? 1u : h;
}

// The morph's settings, armed once per recall.
struct Policy
{
    uint8_t curve = CurveSmooth;
    bool steppedWalk = false;  // Stepped parameters under AUTO: WALK (true) or JUMP (false)
    float threshold = 0.5f;    // JUMP: where in the morph the switch happens
};

inline Rule resolve (const Desc& d, Rule override_, const Policy& p)
{
    const Rule r = override_ != RuleAuto ? override_ : d.rule;
    if (r != RuleAuto) return r;
    switch (d.kind)
    {
        case Continuous: return RuleGlide;
        case Stepped:    return p.steppedWalk ? RuleGlide : RuleJump;
        default:         return RuleJump;
    }
}

inline float snap (float v, uint16_t steps)
{
    if (steps < 2) return v;
    const float n = (float) (steps - 1);
    return std::round (v * n) / n;
}

// One parameter at position (x, y). corner[i] is used when (mask & (1 << i)), base otherwise.
inline float blend (const Desc& d, Rule rule, float base, const float corner[4], uint8_t mask, float x, float y, const Policy& p)
{
    auto c = [&] (int i) { return (mask >> i) & 1u ? corner[i] : base; };
    x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
    y = y < 0.0f ? 0.0f : (y > 1.0f ? 1.0f : y);
    const bool discrete = d.kind != Continuous;
    switch (rule)
    {
        case RuleFixed: return base;
        case RuleGlide:
        {
            const float cx = curve (p.curve, x), cy = curve (p.curve, y);
            const float v = (c (0) * (1.0f - cx) + c (1) * cx) * (1.0f - cy) + (c (2) * (1.0f - cx) + c (3) * cx) * cy;
            return discrete ? snap (v, d.steps) : v;
        }
        default:
        {
            bool xi, yi;
            if (rule == RuleStart)    { xi = x > 0.0f;  yi = y > 0.0f; }
            else if (rule == RuleEnd) { xi = x >= 1.0f; yi = y >= 1.0f; }
            else                      { xi = x >= p.threshold; yi = y >= p.threshold; }
            const float v = c ((xi ? 1 : 0) + (yi ? 2 : 0));
            return discrete ? snap (v, d.steps) : v;
        }
    }
}

// How far apart two value sets are (shared ids only), 0..1: the mean absolute difference.
// SETBOOK draws it on the scene cards (like SEEDWALK's distance between nets).
template <typename GetA, typename GetB>
inline float distance (int n, GetA&& a, GetB&& b)
{
    if (n <= 0) return 0.0f;
    double s = 0.0;
    for (int i = 0; i < n; ++i) s += std::fabs ((double) a (i) - (double) b (i));
    return (float) (s / (double) n);
}

} // namespace setbook
