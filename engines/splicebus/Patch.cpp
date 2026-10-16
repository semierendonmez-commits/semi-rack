// SEMI port: see Patch.h. The presets are Source/Presets.cpp's, value for value.
#include "Patch.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace spb
{

template <size_t N>
static std::vector<std::string> list (const std::array<const char*, N>& a) { return std::vector<std::string> (a.begin(), a.end()); }

const std::vector<PDef>& defs()
{
    static const std::vector<PDef> d {
        { ids::chaos, "Chaos", 0.0f, 1.0f, 0.2f, PDef::real, {} },
        { ids::length, "Reel Length", 0.0f, 3.0f, 1.0f, PDef::choice, list (reelLengthNames()) },
        { ids::gene, "Gene", 0.0f, 5.0f, 1.0f, PDef::choice, list (geneNames()) },
        { ids::vari, "Varispeed", 0.0f, 1.0f, 0.75f, PDef::real, {} },
        { ids::morph, "Morph", 0.0f, 1.0f, 0.45f, PDef::real, {} },
        { ids::organize, "Organize", 0.0f, 1.0f, 0.0f, PDef::real, {} },
        { ids::slide, "Slide", 0.0f, 1.0f, 0.0f, PDef::real, {} },
        { ids::hold, "Hold", 0.0f, 1.0f, 0.0f, PDef::boolean, {} },
        { ids::turns, "Turns", 0.0f, 1.0f, 0.6f, PDef::real, {} },
        { ids::roll, "Roll", 0.0f, 1.0f, 0.5f, PDef::real, {} },
        { ids::budget, "Budget", 0.0f, 10.0f, 4.0f, PDef::choice, list (budgetNames()) },
        { ids::wall, "Wall", 0.0f, 2.0f, 0.0f, PDef::choice, list (wallNames()) },
        { ids::anchor, "Anchor", 0.0f, 4.0f, 3.0f, PDef::choice, list (anchorNames()) },
        { ids::loop, "Loop", 0.0f, 5.0f, 0.0f, PDef::choice, list (loopNames()) },
        { ids::seed, "Seed", 0.0f, 9999.0f, 1977.0f, PDef::integer, {} },
        { ids::split, "Split", 0.0f, 2.0f, 1.0f, PDef::choice, list (splitNames()) },
        { ids::splithz, "Split Freq", 30.0f, 1000.0f, 120.0f, PDef::real, {}, 150.0f },
        { ids::ratio, "Split Ratio", 0.5f, 4.0f, 1.5f, PDef::real, {} },
        { ids::f0max, "F0 Max", 60.0f, 500.0f, 220.0f, PDef::real, {} },
        { ids::protby, "Protect By", 0.0f, 3.0f, 1.0f, PDef::choice, list (protectNames()) },
        { ids::protect, "Protect", 0.0f, 1.0f, 0.8f, PDef::real, {} },
        { ids::prel, "Protect Release", 5.0f, 1000.0f, 80.0f, PDef::real, {}, 120.0f },
        { ids::sens, "Sensitivity", 0.0f, 1.0f, 0.5f, PDef::real, {} },
        { ids::mix, "Mix", 0.0f, 1.0f, 1.0f, PDef::real, {} },
        { ids::out, "Output", -24.0f, 12.0f, 0.0f, PDef::real, {} },
        { ids::run, "Free Run", 0.0f, 1.0f, 0.0f, PDef::boolean, {} },
        { ids::bpm, "BPM", 40.0f, 300.0f, 174.0f, PDef::real, {} },
    };
    return d;
}

int indexOf (const char* id)
{
    const auto& d = defs();
    for (int i = 0; i < (int) d.size(); ++i)
        if (std::strcmp (d[(size_t) i].id, id) == 0) return i;
    return -1;
}

std::vector<float> defaults()
{
    std::vector<float> v;
    for (const auto& p : defs()) v.push_back (p.def);
    return v;
}

Params toParams (const std::vector<float>& v)
{
    auto f = [&] (const char* id) { return v[(size_t) indexOf (id)]; };
    auto i = [&] (const char* id) { return (int) std::lround (f (id)); };
    Params p;
    p.chaos = f (ids::chaos);
    p.length = i (ids::length);
    p.gene = i (ids::gene);
    p.varispeed = f (ids::vari);
    p.morph = f (ids::morph);
    p.organize = f (ids::organize);
    p.slide = f (ids::slide);
    p.hold = f (ids::hold) > 0.5f;
    p.turns = f (ids::turns);
    p.roll = f (ids::roll);
    p.budget = i (ids::budget);
    p.wall = i (ids::wall);
    p.anchor = i (ids::anchor);
    p.loop = i (ids::loop);
    p.seed = (uint32_t) i (ids::seed) * 2654435761u + 17u;
    p.split = i (ids::split);
    p.splitHz = f (ids::splithz);
    p.ratio = f (ids::ratio);
    p.f0Max = f (ids::f0max);
    p.protectBy = i (ids::protby);
    p.protect = f (ids::protect);
    p.protectRel = f (ids::prel) * 0.001f;
    p.sens = f (ids::sens);
    p.mix = f (ids::mix);
    p.outDb = f (ids::out);
    p.run = f (ids::run) > 0.5f;
    p.bpm = f (ids::bpm);
    return p;
}

namespace
{
using V = std::map<std::string, float>;

V base()
{
    return { { ids::chaos, 0.2f }, { ids::length, 1 }, { ids::gene, 1 }, { ids::vari, 0.75f }, { ids::morph, 0.45f },
             { ids::organize, 0.0f }, { ids::slide, 0.0f }, { ids::hold, 0 },
             { ids::turns, 0.6f }, { ids::roll, 0.5f }, { ids::budget, 4 }, { ids::wall, 0 }, { ids::anchor, 3 }, { ids::loop, 0 }, { ids::seed, 1977 },
             { ids::split, 1 }, { ids::splithz, 120.0f }, { ids::ratio, 1.5f }, { ids::f0max, 220.0f },
             { ids::protby, 1 }, { ids::protect, 0.8f }, { ids::prel, 80.0f }, { ids::sens, 0.5f },
             { ids::mix, 1.0f }, { ids::out, 0.0f }, { ids::run, 0 }, { ids::bpm, 174.0f } };
}

V with (V v, std::initializer_list<std::pair<const std::string, float>> changes)
{
    for (const auto& c : changes) v[c.first] = c.second;
    return v;
}
} // namespace

const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> v {
        { "Mix Bus Order", "BUS",
          "On the master: a few breaks a 4-bar line, most of them before the phrase lines, the bass and the hits as played.",
          base() },
        { "Wire", "BUS",
          "CHAOS 0, no turns: what the split alone does (nothing you can hear). Start here and bring CHAOS up.",
          with (base(), { { ids::chaos, 0.0f }, { ids::turns, 0.0f } }) },
        { "Roller Keeps Rolling", "DRUMS",
          "A drum bus at 174: sixteenth genes, turns before every 8, a gap before 16 and 32, the first beat of every line clean.",
          with (base(), { { ids::chaos, 0.3f }, { ids::turns, 0.75f }, { ids::budget, 5 }, { ids::wall, 1 }, { ids::splithz, 150.0f }, { ids::seed, 404 } }) },
        { "Turn Every Eight", "FORM",
          "Nothing breaks inside the phrase; every turn rolls what just went by, faster towards the line, and the tape stops before 16.",
          with (base(), { { ids::chaos, 0.0f }, { ids::turns, 1.0f }, { ids::roll, 0.85f }, { ids::budget, 6 }, { ids::wall, 2 }, { ids::seed, 88 } }) },
        { "Sixteen Bar Wall", "FORM",
          "Quiet for fifteen bars, then the turn and a gap: TURNAROUND's hierarchy on a bus.",
          with (base(), { { ids::chaos, 0.1f }, { ids::turns, 0.9f }, { ids::budget, 3 }, { ids::wall, 1 }, { ids::anchor, 4 }, { ids::seed, 1616 } }) },
        { "Reese Upstairs", "BASS",
          "On a bass channel: TRACK splits at 2 x the note, so the fundamental never moves while the reese's top is cut, reversed and layered.",
          with (base(), { { ids::split, 2 }, { ids::ratio, 2.0f }, { ids::chaos, 0.5f }, { ids::gene, 2 }, { ids::morph, 0.55f },
                          { ids::protby, 0 }, { ids::budget, 0 }, { ids::seed, 3030 } }) },
        { "Sub Stays Put", "BASS",
          "A bass channel into the reel above 1.5 x the note: chaos everywhere upstairs, the note's fundamental as it was played.",
          with (base(), { { ids::split, 2 }, { ids::ratio, 1.5f }, { ids::chaos, 0.85f }, { ids::gene, 1 }, { ids::vari, 0.62f },
                          { ids::protby, 0 }, { ids::budget, 0 }, { ids::turns, 0.3f }, { ids::seed, 50 } }) },
        { "Garage Skip", "DRUMS",
          "UK garage: a two-bar loop of the dice, sixteenth genes, a little reverse, the kick kept clean.",
          with (base(), { { ids::chaos, 0.3f }, { ids::loop, 2 }, { ids::turns, 0.4f }, { ids::splithz, 100.0f }, { ids::vari, 0.7f },
                          { ids::budget, 0 }, { ids::anchor, 1 }, { ids::bpm, 132.0f }, { ids::seed, 2002 } }) },
        { "Loop The Dice", "FORM",
          "LOOP 1 BAR: every bar breaks in the same places. Change SEED for another bar.",
          with (base(), { { ids::chaos, 0.5f }, { ids::loop, 1 }, { ids::turns, 0.0f }, { ids::budget, 0 }, { ids::anchor, 0 }, { ids::seed, 7 } }) },
        { "Morph Layers", "TEXTURE",
          "Quarter genes from four bars of tape, overlapping into octave and fifth layers above 200 Hz.",
          with (base(), { { ids::chaos, 0.6f }, { ids::gene, 3 }, { ids::length, 2 }, { ids::morph, 0.85f }, { ids::vari, 0.68f },
                          { ids::splithz, 200.0f }, { ids::budget, 0 }, { ids::protby, 0 }, { ids::seed, 4242 } }) },
        { "Backwards Tape", "TEXTURE",
          "Eighth genes read backwards at the speed they were recorded.",
          with (base(), { { ids::chaos, 0.45f }, { ids::gene, 2 }, { ids::vari, 0.25f }, { ids::budget, 0 }, { ids::seed, 1312 } }) },
        { "Held Reel", "TEXTURE",
          "HOLD: the reel keeps the bars it had; the genes keep cutting them into whatever comes next.",
          with (base(), { { ids::hold, 1 }, { ids::chaos, 0.6f }, { ids::gene, 2 }, { ids::organize, 0.5f }, { ids::budget, 0 }, { ids::seed, 999 } }) },
        { "Sidechain Kick Clean", "DRUMS",
          "Route the kick to the sidechain: wherever it hits, the bus comes through as played; in between, chaos.",
          with (base(), { { ids::protby, 2 }, { ids::protect, 1.0f }, { ids::prel, 120.0f }, { ids::chaos, 0.7f }, { ids::budget, 0 }, { ids::seed, 36 } }) },
        { "Thirty-Two Bar Arc", "FORM",
          "Half-bar genes, a budget of two a line, turns that only really open before 32.",
          with (base(), { { ids::gene, 4 }, { ids::chaos, 0.05f }, { ids::turns, 1.0f }, { ids::roll, 0.3f }, { ids::budget, 2 },
                          { ids::wall, 2 }, { ids::anchor, 4 }, { ids::length, 3 }, { ids::seed, 3232 } }) },
    };
    return v;
}

void apply (const Preset& p, std::vector<float>& v)
{
    const auto& d = defs();
    v.resize (d.size());
    for (size_t i = 0; i < d.size(); ++i)
    {
        auto it = p.values.find (d[i].id);
        v[i] = it != p.values.end() ? it->second : d[i].def;
    }
}

// JUCE's NormalisableRange with setSkewForCentre (the two skewed ranges) or linear
static float to01 (const PDef& d, float x)
{
    const float n = (x - d.min) / (d.max - d.min);
    if (d.skewCentre <= 0.0f) return std::max (0.0f, std::min (1.0f, n));
    const float skew = std::log (0.5f) / std::log ((d.skewCentre - d.min) / (d.max - d.min));
    return std::pow (std::max (0.0f, std::min (1.0f, n)), skew);
}
static float from01 (const PDef& d, float n)
{
    n = std::max (0.0f, std::min (1.0f, n));
    if (d.skewCentre > 0.0f)
    {
        const float skew = std::log (0.5f) / std::log ((d.skewCentre - d.min) / (d.max - d.min));
        n = std::pow (n, 1.0f / skew);
    }
    return d.min + (d.max - d.min) * n;
}

void mutate (std::vector<float>& v, float amount, const std::vector<bool>& locked, float (*rnd) (void*), void* ctx)
{
    const auto& d = defs();
    auto mutable_ = [] (const char* id)
    {
        return std::strcmp (id, ids::mix) && std::strcmp (id, ids::out) && std::strcmp (id, ids::run) && std::strcmp (id, ids::bpm)
            && std::strcmp (id, ids::split) && std::strcmp (id, ids::protby);
    };
    const int seedIdx = indexOf (ids::seed);
    for (size_t i = 0; i < d.size(); ++i)
    {
        const auto& p = d[i];
        if (! mutable_ (p.id) || (i < locked.size() && locked[i]) || (int) i == seedIdx) continue;
        if (p.kind == PDef::choice || p.kind == PDef::boolean)
        {
            if (rnd (ctx) < amount * 0.5f)
            {
                const int steps = (int) (p.max - p.min);
                const int cur = (int) std::lround (v[i] - p.min);
                v[i] = p.min + (float) std::max (0, std::min (steps, cur + (rnd (ctx) < 0.5f ? 1 : -1)));
            }
        }
        else
            v[i] = from01 (p, to01 (p, v[i]) + (rnd (ctx) + rnd (ctx) + rnd (ctx) - 1.5f) * amount * 0.3f);
    }
    if (! (seedIdx < (int) locked.size() && locked[(size_t) seedIdx]) && rnd (ctx) < amount)
        v[(size_t) seedIdx] = (float) std::min (9999, (int) (rnd (ctx) * 10000.0f));
}

} // namespace spb
