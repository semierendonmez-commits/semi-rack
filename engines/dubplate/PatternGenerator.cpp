// DUBPLATE — generators. SEMI Family port of Source/Model/PatternGenerator.cpp:
// juce::Random -> dubplate::Random (same generator, Random.h), juce::jlimit / jmin -> std;
// the musical rules and seeds are unchanged, so results match the plugin.
#include "PatternGenerator.h"
#include "Random.h"
#include <algorithm>
#include "GenreTemplates.h"

namespace dubplate::PatternGenerator
{
namespace
{
void set (StepPattern& p, std::initializer_list<int> steps, int vel)
{
    for (auto s : steps)
        p.velocity[(size_t) (s & 15)] = (uint8_t) std::max (0, std::min (127, vel));
}

void maybe (StepPattern& p, Random& rng, std::initializer_list<int> steps, int vel, float chance)
{
    for (auto s : steps)
        if (p.velocity[(size_t) (s & 15)] == 0 && rng.nextFloat() < chance)
            p.velocity[(size_t) (s & 15)] = (uint8_t) std::max (1, std::min (127, vel + rng.nextInt (-12, 12)));
}

bool isBreakbeatGenre (Genre g) { return g == Genre::Jungle || g == Genre::DnB; }

StepPattern kickPattern (Genre g, Random& rng, float cx)
{
    StepPattern p;
    switch (g)
    {
        case Genre::Jungle:
            set (p, { 0, 10 }, 120);
            maybe (p, rng, { 7, 14 }, 90, cx * 0.5f);
            break;
        case Genre::DnB:
            set (p, { 0, 10 }, 124);
            maybe (p, rng, { 6, 15 }, 85, cx * 0.35f);
            break;
        case Genre::UKGarage:
            set (p, { 0, 10 }, 118);       // skippy 2-step
            maybe (p, rng, { 7, 13 }, 92, cx * 0.6f);
            break;
        case Genre::SpeedGarage:
            set (p, { 0, 4, 8, 12 }, 122); // four to the floor
            maybe (p, rng, { 14 }, 80, cx * 0.3f);
            break;
        default: break;
    }
    return p;
}

StepPattern snarePattern (Genre g, Random& rng, float cx, bool clap)
{
    StepPattern p;
    set (p, { 4, 12 }, clap ? 112 : 122);
    if (isBreakbeatGenre (g) && ! clap)
        maybe (p, rng, { 7, 9, 15 }, 48, cx * 0.7f);
    if (g == Genre::UKGarage)
        maybe (p, rng, { 15 }, 70, cx * 0.4f);
    return p;
}

StepPattern hatPattern (Genre g, Random& rng, float cx, bool open)
{
    StepPattern p;
    if (open)
    {
        if (g == Genre::SpeedGarage || g == Genre::UKGarage)
            set (p, { 2, 6, 10, 14 }, 108);
        else
            set (p, { 6, 14 }, 90);
        return p;
    }

    switch (g)
    {
        case Genre::UKGarage:
            set (p, { 2, 6, 10, 14 }, 100);
            maybe (p, rng, { 3, 7, 11, 15, 1, 9 }, 62, 0.35f + cx * 0.4f);
            break;
        case Genre::SpeedGarage:
            for (int i = 0; i < 16; ++i) p.velocity[(size_t) i] = (uint8_t) ((i % 2) ? 58 : 92);
            break;
        default:
            set (p, { 2, 6, 10, 14 }, 96);
            maybe (p, rng, { 0, 4, 8, 12 }, 60, 0.3f + cx * 0.3f);
            maybe (p, rng, { 1, 3, 5, 7, 9, 11, 13, 15 }, 44, cx * 0.4f);
            break;
    }
    return p;
}

StepPattern bassPattern (Role role, Genre g, Random& rng, float cx)
{
    StepPattern p;
    if (role == Role::OrganBass || g == Genre::SpeedGarage)
    {
        // The speed garage "dong": off-beat organ bass with octave jumps.
        set (p, { 2, 6, 10, 14 }, 112);
        p.pitch[6] = 12;
        if (rng.nextFloat() < 0.5f + cx * 0.3f) { set (p, { 7 }, 84); p.pitch[7] = 12; }
        if (rng.nextFloat() < cx) { set (p, { 15 }, 90); p.pitch[15] = 7; }
        return p;
    }

    if (role == Role::KickSub)
        return kickPattern (g, rng, 0.0f);

    if (g == Genre::UKGarage)
    {
        set (p, { 0, 3, 10 }, 110);
        p.pitch[3] = 3;
        maybe (p, rng, { 6, 13 }, 96, cx * 0.6f);
        p.pitch[13] = -2;
        return p;
    }

    // Jungle / DnB sub line: long notes on the kick with a pickup.
    set (p, { 0 }, 120);
    set (p, { 10 }, 108);
    if (rng.nextFloat() < 0.4f + cx * 0.4f) { set (p, { 6 }, 96); p.pitch[6] = rng.nextBool() ? 3 : -2; }
    if (rng.nextFloat() < cx * 0.6f) { set (p, { 14 }, 90); p.pitch[14] = 5; }
    return p;
}
} // namespace

StepPattern defaultPattern (Role role, Genre genre, uint32_t seed, float complexity)
{
    Random rng ((int64_t) seed * 7919 + (int) role * 104729);
    switch (role)
    {
        case Role::Kick:
        case Role::Punch:      return kickPattern (genre, rng, complexity);
        case Role::Snare:      return snarePattern (genre, rng, complexity, false);
        case Role::Clap:       return snarePattern (genre, rng, complexity, true);
        case Role::HatClosed:
        case Role::ShuffleHat: return hatPattern (genre, rng, complexity, false);
        case Role::HatOpen:    return hatPattern (genre, rng, complexity, true);
        case Role::Ride:       { StepPattern p; set (p, { 0, 4, 8, 12 }, 90); return p; }
        case Role::Perc:       { StepPattern p; set (p, { 3, 11 }, 90); maybe (p, rng, { 6, 14, 9 }, 70, complexity * 0.6f); return p; }
        case Role::Sub:
        case Role::Bass:
        case Role::Bass2:
        case Role::Reese:
        case Role::KickSub:
        case Role::OrganBass:  return bassPattern (role, genre, rng, complexity);
        case Role::Stab:       { StepPattern p; set (p, { 6 }, 104); maybe (p, rng, { 3, 11, 14 }, 90, complexity * 0.5f); return p; }
        case Role::VocalChop:  { StepPattern p; set (p, { 0 }, 110); maybe (p, rng, { 8, 11 }, 90, complexity * 0.5f); return p; }
        case Role::Break:
        case Role::BreakChop:  { StepPattern p; set (p, { 0, 4, 8, 12 }, 110); return p; }
        case Role::Pad:        { StepPattern p; set (p, { 0 }, 100); return p; }
        case Role::Riser:
        case Role::Impact:     { StepPattern p; set (p, { 0 }, 120); return p; }
        default:               return {};
    }
}

void generateDrums (Arrangement& arr, float complexity, uint32_t seed)
{
    for (auto& l : arr.lanes)
    {
        if (l.locked || roleInfo (l.role).bus != Bus::Drums)
            continue;
        auto p = defaultPattern (l.role, arr.genre, seed + (uint32_t) l.uid, complexity);
        l.pattern = p.velocity;
        l.stepPitch = p.pitch;
    }
}

void generateBassline (Arrangement& arr, float complexity, uint32_t seed)
{
    for (auto& l : arr.lanes)
    {
        if (l.locked || roleInfo (l.role).bus != Bus::Bass)
            continue;
        auto p = defaultPattern (l.role, arr.genre, seed + (uint32_t) l.uid * 31u, complexity);
        l.pattern = p.velocity;
        l.stepPitch = p.pitch;
    }
}

void generateForm (Arrangement& arr, uint32_t seed, bool vip)
{
    Random rng ((int64_t) seed * 48271 + (vip ? 99991 : 0));
    auto pick = [&rng] (std::initializer_list<int> opts)
    {
        std::vector<int> v (opts);
        return v[(size_t) rng.nextInt ((int) v.size())];
    };

    std::vector<Section> s;
    auto add = [&s] (const char* name, SectionType t, int bars, float e) { s.push_back ({ name, t, bars, e, 0u }); };

    const bool garage = arr.genre == Genre::UKGarage || arr.genre == Genre::SpeedGarage;
    add ("Intro", SectionType::Intro, pick ({ 16, 32 }), 0.2f);
    if (vip)
    {
        add ("Drop", SectionType::Drop, 32, 0.9f);
        add ("Switch", SectionType::Breakdown, pick ({ 8, 16 }), 0.45f);
        add ("Build", SectionType::Build, 8, 0.65f);
        add ("Drop 2", SectionType::Drop, pick ({ 32, 48 }), 1.0f);
        add ("Outro", SectionType::Outro, pick ({ 16, 32 }), 0.25f);
    }
    else
    {
        add ("Build", SectionType::Build, pick ({ 8, 16 }), 0.55f);
        add ("Drop", SectionType::Drop, garage ? pick ({ 16, 32 }) : 32, 0.95f);
        add ("Breakdown", SectionType::Breakdown, pick ({ 16, 32 }), 0.35f);
        add ("Build 2", SectionType::Build, 8, 0.65f);
        add ("Drop 2", SectionType::Drop, 32, 1.0f);
        add ("Outro", SectionType::Outro, pick ({ 16, 32 }), 0.25f);
    }

    arr.sections = s;
    GenreTemplates::applyDefaultEvents (arr);
    arr.resizeCells();
    for (int i = 0; i < (int) arr.sections.size(); ++i)
        GenreTemplates::applyEnergy (arr, i, true);

    if (vip)
    {
        for (auto& l : arr.lanes)
            if (l.variationOf >= 0)
                l.alt = AltRule::Drop2Only;
        arr.fill.probability = std::min (1.0f, arr.fill.probability + 0.2f);
    }
}

void mutate (Arrangement& arr, float amount, uint32_t seed)
{
    Random rng ((int64_t) seed * 16807 + 12345);
    amount = std::max (0.0f, std::min (1.0f, amount));

    for (auto& l : arr.lanes)
    {
        if (l.locked)
            continue;

        if (! l.isLoop())
        {
            for (int i = 0; i < 16; ++i)
            {
                // Keep the backbone (downbeat and the backbeat) intact.
                if (i == 0 || i == 4 || i == 12)
                    continue;
                if (rng.nextFloat() < amount * 0.12f)
                    l.pattern[(size_t) i] = l.pattern[(size_t) i] > 0 ? 0 : (uint8_t) (50 + rng.nextInt (50));
            }
        }

        if (roleInfo (l.role).priority >= 3)
            for (auto& c : l.cells)
                if (rng.nextFloat() < amount * 0.08f)
                    c.on = ! c.on;
    }

    for (auto& s : arr.sections)
        s.energy = std::max (0.05f, std::min (1.0f, s.energy + (rng.nextFloat() - 0.5f) * 0.2f * amount));

    for (auto& row : arr.fill.grid)
        for (int i = 8; i < 16; ++i)
            if (rng.nextFloat() < amount * 0.1f)
                row[(size_t) i] = ! row[(size_t) i];
}

std::array<bool, 16> defaultGhostKick (Genre genre)
{
    std::array<bool, 16> a {};
    if (genre == Genre::UKGarage || genre == Genre::SpeedGarage)
        a[0] = a[4] = a[8] = a[12] = true;
    else
        a[0] = a[10] = true;
    return a;
}

std::array<std::array<bool, 16>, 4> defaultFillGrid (int profile)
{
    std::array<std::array<bool, 16>, 4> g {};
    switch (profile)
    {
        case 0: // amen edit: stutter + reverse + rolls in the last beat
            g[1][8] = g[1][9] = true;
            g[2][10] = g[2][11] = true;
            g[0][12] = g[0][13] = g[0][14] = g[0][15] = true;
            g[3][14] = g[3][15] = true;
            break;
        case 1: // snare roll
            for (int i = 8; i < 16; ++i) g[0][(size_t) i] = true;
            g[3][14] = g[3][15] = true;
            break;
        case 2: // hat skip / kick shuffle
            g[1][11] = g[1][13] = true;
            g[2][15] = true;
            break;
        case 3: // bass / organ variation
            g[3][12] = g[3][14] = true;
            g[2][15] = true;
            break;
        default: break;
    }
    return g;
}
} // namespace dubplate::PatternGenerator
