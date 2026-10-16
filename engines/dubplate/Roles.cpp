// DUBPLATE — role table and genre rules. SEMI Family port of Source/Model/Roles.cpp:
// juce helpers replaced by std (jlimit / isPositiveAndBelow), roleFromName and
// foldTempoIntoGenre (sample analysis) dropped; tables unchanged.
#include "Roles.h"
#include <algorithm>

namespace dubplate
{
namespace
{
const std::array<RoleInfo, (size_t) Role::numRoles> kRoles {{
    // name          short   bus          tonal  duck   src    prio loop   fill              HP
    { "Kick",        "KCK",  Bus::Drums,  false, false, true,  1,  false, FillKind::Kick,   20.0f },
    { "Kick Sub",    "KSB",  Bus::Bass,   true,  false, true,  1,  false, FillKind::None,   20.0f },
    { "Punch",       "PNC",  Bus::Drums,  false, false, false, 2,  false, FillKind::Kick,   80.0f },
    { "Snare",       "SNR",  Bus::Drums,  false, false, false, 1,  false, FillKind::Snare, 120.0f },
    { "Clap",        "CLP",  Bus::Drums,  false, false, false, 2,  false, FillKind::Snare, 150.0f },
    { "Hat Closed",  "HHC",  Bus::Drums,  false, false, false, 2,  false, FillKind::Hat,   300.0f },
    { "Hat Open",    "HHO",  Bus::Drums,  false, false, false, 3,  false, FillKind::Hat,   300.0f },
    { "Shuffle Hat", "SHF",  Bus::Drums,  false, false, false, 2,  true,  FillKind::Hat,   300.0f },
    { "Ride",        "RDE",  Bus::Drums,  false, false, false, 3,  false, FillKind::Hat,   300.0f },
    { "Break",       "BRK",  Bus::Drums,  false, false, false, 2,  true,  FillKind::Break,  60.0f },
    { "Break Chop",  "CHP",  Bus::Drums,  false, false, false, 3,  true,  FillKind::Break,  60.0f },
    { "Perc",        "PRC",  Bus::Drums,  false, false, false, 3,  true,  FillKind::None,  150.0f },
    { "Sub",         "SUB",  Bus::Bass,   true,  true,  false, 1,  false, FillKind::Bass,   20.0f },
    { "Bass",        "BAS",  Bus::Bass,   true,  true,  false, 1,  true,  FillKind::Bass,   30.0f },
    { "Bass 2",      "BS2",  Bus::Bass,   true,  true,  false, 2,  true,  FillKind::Bass,   30.0f },
    { "Reese",       "RSE",  Bus::Bass,   true,  true,  false, 2,  true,  FillKind::Bass,   60.0f },
    { "Organ Bass",  "ORG",  Bus::Bass,   true,  true,  false, 1,  false, FillKind::Bass,   30.0f },
    { "Stab",        "STB",  Bus::Music,  true,  true,  false, 3,  false, FillKind::None,  150.0f },
    { "Vocal Chop",  "VOX",  Bus::Music,  true,  false, false, 4,  false, FillKind::None,  150.0f },
    { "Pad",         "PAD",  Bus::Music,  true,  true,  false, 3,  true,  FillKind::None,  150.0f },
    { "Riser",       "RSR",  Bus::FX,     false, false, false, 4,  false, FillKind::None,  200.0f },
    { "Impact",      "IMP",  Bus::FX,     false, false, false, 4,  false, FillKind::None,   30.0f },
}};
} // namespace

const RoleInfo& roleInfo (Role r)
{
    auto i = std::max (0, std::min ((int) Role::numRoles - 1, (int) r));
    return kRoles[(size_t) i];
}

int numRoles() { return (int) Role::numRoles; }

Role roleFromIndex (int i) { return (Role) std::max (0, std::min ((int) Role::numRoles - 1, i)); }

const char* roleName (Role r) { return roleInfo (r).name; }

const char* genreName (Genre g)
{
    switch (g)
    {
        case Genre::Jungle:      return "Jungle";
        case Genre::DnB:         return "Drum & Bass";
        case Genre::UKGarage:    return "UK Garage";
        case Genre::SpeedGarage: return "Speed Garage";
        default:                 return "Custom";
    }
}

const char* genreShortName (Genre g)
{
    switch (g)
    {
        case Genre::Jungle:      return "JUNGLE";
        case Genre::DnB:         return "DNB";
        case Genre::UKGarage:    return "UKG";
        case Genre::SpeedGarage: return "SPEED G";
        default:                 return "CUSTOM";
    }
}

TempoRange genreTempoRange (Genre g)
{
    switch (g)
    {
        case Genre::Jungle:      return { 160.0, 175.0 };
        case Genre::DnB:         return { 170.0, 178.0 };
        case Genre::UKGarage:    return { 128.0, 138.0 };
        case Genre::SpeedGarage: return { 128.0, 135.0 };
        default:                 return { 60.0, 200.0 };
    }
}

double genreDefaultTempo (Genre g)
{
    switch (g)
    {
        case Genre::Jungle:      return 165.0;
        case Genre::DnB:         return 174.0;
        case Genre::UKGarage:    return 132.0;
        case Genre::SpeedGarage: return 132.0;
        default:                 return 140.0;
    }
}

float genreDefaultSwing (Genre g)
{
    switch (g)
    {
        case Genre::UKGarage:    return 58.0f;
        case Genre::SpeedGarage: return 54.0f;
        case Genre::Jungle:      return 51.0f;
        default:                 return 50.0f;
    }
}

const char* busName (Bus b)
{
    switch (b)
    {
        case Bus::Drums: return "Drums";
        case Bus::Bass:  return "Bass";
        case Bus::Music: return "Music";
        case Bus::FX:    return "FX";
        default:         return "?";
    }
}

const char* sectionTypeName (SectionType t)
{
    switch (t)
    {
        case SectionType::Intro:     return "Intro";
        case SectionType::Build:     return "Build";
        case SectionType::Drop:      return "Drop";
        case SectionType::Breakdown: return "Breakdown";
        case SectionType::Outro:     return "Outro";
        default:                     return "Section";
    }
}

const char* eventName (int bitIndex)
{
    static const char* names[] = { "Filter sweep", "Bass drop-out", "Rewind", "Tape stop", "Delay throw",
                                   "Reverb throw", "Dub siren", "Beat repeat", "Half-time", "Vinyl brake", "Snare roll" };
    return (bitIndex >= 0 && bitIndex < numEventTypes) ? names[bitIndex] : "?";
}

const char* eventShortName (int bitIndex)
{
    static const char* names[] = { "SWEEP", "DROP-OUT", "REWIND", "TAPE STOP", "DLY THROW",
                                   "VRB THROW", "SIREN", "REPEAT", "HALF-TIME", "BRAKE", "SNR ROLL" };
    return (bitIndex >= 0 && bitIndex < numEventTypes) ? names[bitIndex] : "?";
}

} // namespace dubplate
