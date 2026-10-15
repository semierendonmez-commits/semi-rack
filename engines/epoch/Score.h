// Vendored unchanged from semierendonmez-commits/epoch Source/engine (Rack / MetaModule port).
// The score: what the user draws. Two curves over the whole set (ENERGY, SPACE) and the sections.
// Plain C++, no JUCE.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace epoch
{

constexpr int kStepsPerBar = 16;
constexpr int kMaxBars = 512;
constexpr int kMaxSteps = kMaxBars * kStepsPerBar;
constexpr int kSlots = 8;

// how a curve travels from a point to the next one
enum class Shape : uint8_t { Linear = 0, Smooth, Hold, Exp, Log, kNum };
const char* shapeName (Shape);

struct Point
{
    double bar = 0.0;     // position in bars from the start of the timeline
    float value = 0.0f;   // 0..1
    Shape shape = Shape::Smooth;
};

struct Curve
{
    std::vector<Point> pts;
    float eval (double bar) const;       // flat before the first point and after the last
    void sort();
    int nearest (double bar, float value, double barsPerUnit, double maxDist) const;   // -1: none
};

// what a section is for: it sets the anchors and the label colour
enum class Role : uint8_t { Intro = 0, Build, Drop, Break, Roll, Outro, Drift, Wall, kNum };
const char* roleName (Role);

struct Section
{
    double bar = 0.0;
    Role role = Role::Intro;
    std::string name;
};

struct Score
{
    std::string title = "FLAT";
    int bars = 256;
    double bpm = 172.0;          // the tempo the template was written for (the host tempo plays it)
    Curve energy, space;
    std::vector<Section> sections;

    void sortSections();
    int sectionAt (double bar) const;           // -1 before the first one
    double sectionEnd (int index) const;        // the next section's start, or the end
    void clampTo (int newBars);                 // drop what falls past the end
    void rescale (int newBars);                 // stretch everything to a new length
};

// ---- the factory scores
struct Template
{
    const char* name;
    const char* group;           // DNB / GARAGE / MEASURED / FORM
    const char* note;
};
int numTemplates();
const Template& templateInfo (int i);
Score makeTemplate (int i);

} // namespace epoch
