// DUBPLATE — roles, genres, buses, section types and FX event bits.
// SEMI Family port of Source/Model/Roles.h (semierendonmez-commits/dubplate): JUCE types
// replaced (juce::String -> const char*, juce::Range -> TempoRange); the role table and
// the genre rules are unchanged. roleFromName () is not ported (no string state).
#pragma once
#include <array>
#include <cstdint>

namespace dubplate
{
enum class Genre { Jungle = 0, DnB, UKGarage, SpeedGarage, numGenres };

enum class Bus { Drums = 0, Bass, Music, FX, numBuses };

enum class Role
{
    Kick = 0, KickSub, Punch, Snare, Clap, HatClosed, HatOpen, ShuffleHat, Ride,
    Break, BreakChop, Perc, Sub, Bass, Bass2, Reese, OrganBass, Stab, VocalChop,
    Pad, Riser, Impact, numRoles
};

enum class FillKind { None, Break, Snare, Hat, Kick, Bass };

enum class SectionType { Intro = 0, Build, Drop, Breakdown, Outro, numTypes };

/** Phrase-locked FX events, stored as a bitmask per section. */
enum EventBit : uint32_t
{
    evSweep      = 1u << 0,
    evDropOut    = 1u << 1,
    evRewind     = 1u << 2,
    evTapeStop   = 1u << 3,
    evDelayThrow = 1u << 4,
    evVerbThrow  = 1u << 5,
    evSiren      = 1u << 6,
    evBeatRepeat = 1u << 7,
    evHalfTime   = 1u << 8,
    evVinylBrake = 1u << 9,
    evSnareRoll  = 1u << 10
};
constexpr int numEventTypes = 11;

struct RoleInfo
{
    const char* name;
    const char* shortName;
    Bus bus;
    bool tonal;          // key-aware
    bool duckedByKick;   // receives the kick sidechain
    bool kickSource;     // triggers the sidechain
    int priority;        // 1 = foundation, 5 = decoration (used by energy)
    bool prefersLoop;
    FillKind fill;
    float highPassHz;    // frequency slot: content below this is cut
};

struct TempoRange { double start, end; };

const RoleInfo& roleInfo (Role r);
int numRoles();
Role roleFromIndex (int i);
const char* roleName (Role r);

const char* genreName (Genre g);
const char* genreShortName (Genre g);
TempoRange genreTempoRange (Genre g);
double genreDefaultTempo (Genre g);
float genreDefaultSwing (Genre g);

const char* busName (Bus b);
const char* sectionTypeName (SectionType t);
const char* eventName (int bitIndex);
const char* eventShortName (int bitIndex);
} // namespace dubplate
