// DUBPLATE — genre templates. SEMI Family port of Source/Model/GenreTemplates.cpp:
// juce::jlimit / isPositiveAndBelow / StringArray replaced by std; rules unchanged.
#include "GenreTemplates.h"
#include <algorithm>
#include "PatternGenerator.h"

namespace dubplate::GenreTemplates
{
namespace
{
std::vector<Role> rolesFor (Genre g)
{
    switch (g)
    {
        case Genre::Jungle:
            return { Role::Kick, Role::Snare, Role::Break, Role::BreakChop, Role::HatClosed, Role::Sub,
                     Role::Reese, Role::Pad, Role::VocalChop, Role::Riser, Role::Impact };
        case Genre::DnB:
            return { Role::Kick, Role::Snare, Role::HatClosed, Role::Break, Role::Sub, Role::Reese,
                     Role::Bass2, Role::Pad, Role::Riser, Role::Impact };
        case Genre::UKGarage:
            return { Role::Kick, Role::Clap, Role::ShuffleHat, Role::HatOpen, Role::Perc, Role::Sub,
                     Role::Bass, Role::Stab, Role::VocalChop, Role::Pad, Role::Riser };
        case Genre::SpeedGarage:
            return { Role::Kick, Role::Clap, Role::HatOpen, Role::HatClosed, Role::OrganBass,
                     Role::Reese, Role::Stab, Role::VocalChop, Role::Riser, Role::Impact };
        default:
            return {};
    }
}

const char* templateName (Genre g)
{
    switch (g)
    {
        case Genre::Jungle:      return "Jungle Roller 01";
        case Genre::DnB:         return "DnB Two-Step 174";
        case Genre::UKGarage:    return "UKG 2-Step Skippy";
        case Genre::SpeedGarage: return "Speed Garage Dong 132";
        default:                 return "Custom";
    }
}

int fillProfileFor (Genre g)
{
    switch (g)
    {
        case Genre::Jungle:      return 0;
        case Genre::DnB:         return 1;
        case Genre::UKGarage:    return 2;
        case Genre::SpeedGarage: return 3;
        default:                 return 0;
    }
}

std::vector<Section> sectionsFor (Genre g)
{
    if (g == Genre::UKGarage)
        return { { "Intro", SectionType::Intro, 16, 0.25f, 0u },
                 { "Groove", SectionType::Drop, 16, 0.7f, 0u },
                 { "Breakdown", SectionType::Breakdown, 8, 0.35f, 0u },
                 { "Build", SectionType::Build, 8, 0.6f, 0u },
                 { "Drop", SectionType::Drop, 32, 0.95f, 0u },
                 { "Outro", SectionType::Outro, 16, 0.25f, 0u } };

    return { { "Intro", SectionType::Intro, 16, 0.2f, 0u },
             { "Build", SectionType::Build, 8, 0.55f, 0u },
             { "Drop", SectionType::Drop, 32, 0.95f, 0u },
             { "Breakdown", SectionType::Breakdown, 16, 0.35f, 0u },
             { "Build 2", SectionType::Build, 8, 0.65f, 0u },
             { "Drop 2", SectionType::Drop, 32, 1.0f, 0u },
             { "Outro", SectionType::Outro, 16, 0.25f, 0u } };
}
} // namespace

float cellLevelFor (Role role, Genre genre, SectionType type, float energy)
{
    const auto& info = roleInfo (role);

    // FX roles are placed by section type, not energy.
    if (role == Role::Riser)  return type == SectionType::Build ? 0.9f : 0.0f;
    if (role == Role::Impact) return type == SectionType::Drop ? 0.9f : 0.0f;

    // DJ-friendly intros and outros: drums only.
    if (type == SectionType::Intro || type == SectionType::Outro)
    {
        if (info.bus != Bus::Drums) return 0.0f;
        if (role == Role::Break && genre == Genre::Jungle) return 0.7f;
        return info.priority <= 2 ? 0.9f : 0.0f;
    }

    if (type == SectionType::Breakdown)
    {
        if (role == Role::Pad)       return 1.0f;
        if (role == Role::VocalChop) return 0.9f;
        if (role == Role::Stab)      return 0.8f;
        if (role == Role::HatClosed || role == Role::ShuffleHat || role == Role::Ride) return energy > 0.3f ? 0.6f : 0.0f;
        return 0.0f;
    }

    if (type == SectionType::Build)
    {
        if (info.bus == Bus::Bass) return energy > 0.6f && info.priority == 1 ? 0.6f : 0.0f;
        if (role == Role::Kick || role == Role::Punch) return energy > 0.5f ? 0.8f : 0.0f;
        if (info.bus == Bus::Drums) return info.priority <= 2 ? 0.85f : 0.0f;
        if (role == Role::Pad) return 0.7f;
        return 0.0f;
    }

    // Drops: everything gated by energy and priority.
    static const float thresholds[] = { 0.0f, 0.0f, 0.3f, 0.55f, 0.8f, 0.92f };
    auto p = std::max (1, std::min (5, info.priority));
    if (energy < thresholds[p])
        return 0.0f;
    if (role == Role::Pad && energy > 0.9f) return 0.6f;
    return 1.0f;
}

void applyEnergy (Arrangement& arr, int si, bool includeLocked)
{
    if (si < 0 || si >= (int) arr.sections.size())
        return;

    arr.resizeCells();
    const auto& sec = arr.sections[(size_t) si];
    for (auto& l : arr.lanes)
    {
        if (l.locked && ! includeLocked)
            continue;
        auto lvl = cellLevelFor (l.role, arr.genre, sec.type, sec.energy);
        auto& c = l.cells[(size_t) si];
        c.on = lvl > 0.0f;
        c.level = c.on ? lvl : 1.0f;
    }
}

void applyDefaultEvents (Arrangement& arr)
{
    const auto n = (int) arr.sections.size();
    int dropCount = 0;
    for (int i = 0; i < n; ++i)
    {
        auto& s = arr.sections[(size_t) i];
        s.events = 0;
        auto nextType = i + 1 < n ? arr.sections[(size_t) i + 1].type : SectionType::numTypes;
        bool beforeDrop = nextType == SectionType::Drop;

        switch (s.type)
        {
            case SectionType::Build:
                s.events |= evSweep | evSnareRoll;
                break;
            case SectionType::Drop:
                ++dropCount;
                if (nextType == SectionType::Breakdown) s.events |= evDelayThrow | evBeatRepeat;
                break;
            case SectionType::Breakdown:
                s.events |= evVerbThrow;
                if (arr.genre == Genre::Jungle) s.events |= evSiren;
                break;
            case SectionType::Outro:
                s.events |= (arr.genre == Genre::UKGarage || arr.genre == Genre::SpeedGarage) ? evVinylBrake : evTapeStop;
                break;
            default: break;
        }

        if (beforeDrop)
        {
            // The pre-drop moment: rewind into the second drop, drop-out into the first.
            if (dropCount >= 1 && arr.genre != Genre::DnB) s.events |= evRewind;
            else s.events |= evDropOut;
        }
    }
}

Arrangement makeBlank (Genre genre)
{
    Arrangement a;
    a.genre = genre;
    a.name = "Blank";
    a.bpm = genreDefaultTempo (genre);
    a.swing = genreDefaultSwing (genre);
    a.sections = { { "Section A", SectionType::Drop, 16, 0.8f, 0u } };
    a.fill.profile = fillProfileFor (genre);
    a.fill.grid = PatternGenerator::defaultFillGrid (a.fill.profile);
    a.ghostPattern = PatternGenerator::defaultGhostKick (genre);
    return a;
}

Arrangement makeTemplate (Genre genre)
{
    Arrangement a;
    a.genre = genre;
    a.name = templateName (genre);
    a.bpm = genreDefaultTempo (genre);
    a.swing = genreDefaultSwing (genre);
    a.sections = sectionsFor (genre);
    a.fill.profile = fillProfileFor (genre);
    a.fill.probability = 0.35f;
    a.fill.every = 8;
    a.fill.grid = PatternGenerator::defaultFillGrid (a.fill.profile);
    a.ghostKick = genre == Genre::UKGarage;
    a.ghostPattern = PatternGenerator::defaultGhostKick (genre);
    a.duckDepthDb = (genre == Genre::SpeedGarage || genre == Genre::UKGarage) ? -10.0f : -8.0f;

    int breakUid = -1, reeseUid = -1;
    for (auto role : rolesFor (genre))
    {
        auto& lane = a.addLane (role);
        auto pat = PatternGenerator::defaultPattern (role, genre, a.seed, 0.5f);
        lane.pattern = pat.velocity;
        lane.stepPitch = pat.pitch;
        lane.comp = role == Role::Break || role == Role::BreakChop ? 0.5f : 0.3f;
        lane.clipDb = (role == Role::Kick || role == Role::Snare || role == Role::Break) ? 2.0f : 0.0f;
        lane.send = (role == Role::VocalChop || role == Role::Stab || role == Role::Pad) ? 0.3f : 0.0f;

        if (role == Role::Break) breakUid = lane.uid;
        if (role == Role::Reese) reeseUid = lane.uid;
        if (role == Role::BreakChop && breakUid >= 0) { lane.variationOf = breakUid; lane.alt = AltRule::Every8; }
        if (role == Role::Bass2 && reeseUid >= 0)     { lane.variationOf = reeseUid; lane.alt = AltRule::Drop2Only; }
    }

    applyDefaultEvents (a);
    for (int i = 0; i < (int) a.sections.size(); ++i)
        applyEnergy (a, i, true);
    return a;
}

std::vector<std::string> builtInTemplateNames()
{
    std::vector<std::string> s;
    for (int g = 0; g < (int) Genre::numGenres; ++g)
        s.push_back (templateName ((Genre) g));
    return s;
}
} // namespace dubplate::GenreTemplates
