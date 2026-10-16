// SEMI port: SPLICEBUS's patch layer without JUCE. Port of Source/Params.h, the parameter layout and
// currentParams() of Source/PluginProcessor.cpp, MUTATE / NEW SEED and the 14 factory presets of
// Source/Presets.cpp (semierendonmez-commits/splicebus 9cefada). Ids, ranges, defaults are the plug-in's.
#pragma once

#include "Engine.h"
#include <map>
#include <string>
#include <vector>

namespace spb
{

namespace ids
{
    // the reel
    inline constexpr const char* chaos = "chaos", *length = "length", *gene = "gene", *vari = "vari", *morph = "morph",
                                 *organize = "organize", *slide = "slide", *hold = "hold";
    // the form
    inline constexpr const char* turns = "turns", *roll = "roll", *budget = "budget", *wall = "wall", *anchor = "anchor",
                                 *loop = "loop", *seed = "seed";
    // the split
    inline constexpr const char* split = "split", *splithz = "splithz", *ratio = "ratio", *f0max = "f0max";
    // protect
    inline constexpr const char* protby = "protby", *protect = "protect", *prel = "prel", *sens = "sens";
    // out
    inline constexpr const char* mix = "mix", *out = "out", *run = "run", *bpm = "bpm";
} // namespace ids

struct PDef
{
    enum Kind { real, choice, integer, boolean };
    const char* id;
    const char* name;
    float min, max, def;
    Kind kind;
    std::vector<std::string> choices;
    float skewCentre = -1.0f;   // the plug-in's skewed ranges (for the menus' steps)
};

// in the plug-in's order (ids::all())
const std::vector<PDef>& defs();
int indexOf (const char* id);

// the values -> the engine's parameters (the plug-in's currentParams())
Params toParams (const std::vector<float>& v);
std::vector<float> defaults();

struct Preset { const char* name; const char* category; const char* note; std::map<std::string, float> values; };
const std::vector<Preset>& factoryPresets();
// what applyValues does: anything missing goes back to its default
void apply (const Preset& p, std::vector<float>& v);

// MUTATE (never the mix, the level, the clock, the split mode or PROTECT BY; SEED by chance) and NEW SEED.
// rnd() returns 0..1. Locked ids are left alone.
void mutate (std::vector<float>& v, float amount, const std::vector<bool>& locked, float (*rnd) (void*), void* ctx);

} // namespace spb
