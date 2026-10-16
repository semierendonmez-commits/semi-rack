// DUBPLATE — the arrangement model: sections, role lanes (16-step patterns, a cell per
// section), fill settings. SEMI Family port of Source/Model/Arrangement.h:
//   * JUCE types replaced by std (juce::String -> std::string);
//   * samples, analysis, key detection and the JSON (juce::var) state are dropped: the
//     Rack module has no sample playback and saves the model itself (Rack JSON);
//   * a lane without a sample counts as "present" (it drives a gate output instead).
// Field names, defaults and the lane / variation / cell logic are unchanged.
#pragma once
#include "Roles.h"
#include <array>
#include <string>
#include <vector>

namespace dubplate
{
enum class PlayMode { Auto = 0, Loop, OneShot };
enum class LoopSync { Slice = 0, Varispeed };
enum class AltRule { Every8 = 0, Drop2Only, Random, Manual };

struct Cell
{
    bool on = false;
    float level = 1.0f;   // 0..1
};

struct Lane
{
    int uid = 0;
    Role role = Role::Perc;
    Bus bus = Bus::Drums;
    PlayMode mode = PlayMode::Auto;
    LoopSync sync = LoopSync::Slice;
    int loopBars = 0;                    // 0 = 1 bar (no analysis here)
    std::array<uint8_t, 16> pattern {};  // one-shot velocities per 16th (0 = off)
    std::array<int8_t, 16> stepPitch {}; // one-shot pitch offset per step (semitones)
    int variationOf = -1;                // uid of the parent lane, -1 = none
    AltRule alt = AltRule::Every8;
    std::vector<Cell> cells;             // one per section
    float gainDb = 0.0f;
    float pan = 0.0f;
    float comp = 0.3f;
    float clipDb = 0.0f;
    float duck = 0.0f;
    float send = 0.0f;
    float pitch = 0.0f;                  // semitones
    bool mute = false;
    bool solo = false;
    bool locked = false;                 // cells untouched by energy / form generation

    bool isLoop() const;                 // mode + the role's preference (no analysis)
    int resolvedLoopBars() const { return loopBars > 0 ? loopBars : 1; }
};

struct Section
{
    std::string name;
    SectionType type = SectionType::Drop;
    int bars = 16;
    float energy = 0.5f;   // 0..1
    uint32_t events = 0;   // EventBit mask
};

struct FillSettings
{
    int profile = 0;        // 0 amen edit, 1 snare roll, 2 hat skip, 3 bass/organ variation
    float probability = 0.35f;
    int every = 8;          // bars
    float density = 0.6f;
    std::array<std::array<bool, 16>, 4> grid {}; // rows: roll, stutter, reverse, pitch
};

struct Arrangement
{
    std::string name = "Untitled";
    Genre genre = Genre::Jungle;
    double bpm = 165.0;
    float swing = 50.0f;
    int phraseBars = 16;
    uint32_t seed = 2417;
    std::vector<Section> sections;
    std::vector<Lane> lanes;
    FillSettings fill;
    float duckDepthDb = -8.0f;
    float duckReleaseMs = 90.0f;
    bool ghostKick = false;
    std::array<bool, 16> ghostPattern {};
    bool autoEnergy = true;
    float eventAmount = 0.7f;   // FX event intensity
    float eventLength = 1.0f;   // beats
    float eventWet = 0.45f;
    float ceilingDb = -0.3f;
    float subMonoHz = 120.0f;
    int nextUid = 1;

    int totalBars() const;
    int sectionIndexAtBar (int bar, int* sectionStartBar = nullptr) const;
    int sectionStartBar (int index) const;
    Lane* findLane (int uid);
    const Lane* findLane (int uid) const;
    int laneIndex (int uid) const;
    void resizeCells();
    Lane& addLane (Role role);
    void removeLane (int uid);
    int addVariation (int parentUid);
};
} // namespace dubplate
