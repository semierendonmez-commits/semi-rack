// The book: sixteen scenes of the whole family, the cues that place them on EPOCH's timeline,
// and the clocks that fire them. Plain C++20, no JUCE, no bus: the processor feeds it what the
// wire brings and sends what it decides.
#pragma once

#include "../../link/SetbookMorph.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sb
{

using setbook::Desc;
using setbook::Rule;

constexpr int kScenes = 16;
constexpr int kMaxCues = 64;
constexpr int kStepsPerBar = 16;

// one sibling inside a scene
struct Part
{
    std::string kind, name;              // the key: KIND/name
    bool on = true;                      // off: this scene leaves that sibling alone
    std::vector<Desc> desc;
    std::vector<float> values;           // normalised 0..1, one per desc
    std::vector<uint8_t> over;           // a rule chosen in SETBOOK for this parameter (RuleAuto: the sibling's)

    std::string key() const { return name.empty() ? kind : kind + "/" + name; }
    int size() const { return (int) values.size(); }
    int find (uint32_t id) const
    {
        for (int i = 0; i < size(); ++i) if (desc[(size_t) i].id == id) return i;
        return -1;
    }
};

struct Scene
{
    std::string name;
    std::vector<Part> parts;
    uint32_t stamp = 0;                  // which capture made it (0: empty)

    bool empty() const { return parts.empty(); }
    int params() const { int n = 0; for (const auto& p : parts) n += p.on ? p.size() : 0; return n; }
    int siblings() const { int n = 0; for (const auto& p : parts) n += p.on ? 1 : 0; return n; }
    Part* part (const std::string& key) { for (auto& p : parts) if (p.key() == key) return &p; return nullptr; }
    const Part* part (const std::string& key) const { for (const auto& p : parts) if (p.key() == key) return &p; return nullptr; }
};

// how far apart two scenes are: the mean difference over the parameters both hold, 0..1
// (-1: nothing in common)
float distance (const Scene& a, const Scene& b);

//==============================================================================
// A cue: at `bar` (counted from EPOCH's start bar when FOLLOW is on), recall `scene`, morphing
// over `beats` (0: a cut) with `curve`.
struct Cue
{
    double bar = 0.0;
    int scene = 0;
    float beats = 4.0f;
    uint8_t curve = setbook::CurveSmooth;
};

void sortCues (std::vector<Cue>&);

// Where the cue lane sits on the song. origin: the song position (quarter notes) of bar 0 of the
// lane; loopQ > 0: the lane repeats every loopQ quarter notes (EPOCH set to LOOP).
struct Placement
{
    double originPpq = 0.0;
    double loopQ = 0.0;
    double rel (double ppq) const;      // song position -> lane position (quarter notes)
};

// What the transport did between two blocks. fired: the cues crossed, in order. chase: after a
// start or a locate, the cue the lane is "inside" now (the last one at or before the playhead).
struct Crossing
{
    int fired[8] {};
    int numFired = 0;
    int chase = -1;
    bool located = false;
};

// realtime safe (no allocation): cues must be sorted
Crossing cross (const Cue* cues, int n, const Placement&, double prevPpq, double ppq, bool wasPlaying, bool playing);
int cueAt (const Cue* cues, int n, double relQ);      // the last cue at or before relQ, -1 none

//==============================================================================
// A capture in progress: every sibling that is there gets asked; whoever answers within the
// window is in the scene.
class Capture
{
public:
    void begin (int scene, uint32_t request, int expected, double nowSec)
    {
        target = scene; req = request; want = expected; started = nowSec;
        building = Scene {};
        got = 0;
    }
    bool active() const { return target >= 0; }
    int scene() const { return target; }
    uint32_t request() const { return req; }

    // one sibling's answer; true when everyone answered
    bool add (Part p, uint32_t request)
    {
        if (! active() || request != req) return false;
        if (auto* old = building.part (p.key())) *old = std::move (p);
        else building.parts.push_back (std::move (p));
        ++got;
        return got >= want;
    }
    bool overdue (double nowSec, double windowSec = 0.6) const { return active() && nowSec - started > windowSec; }
    int answered() const { return got; }
    int expected() const { return want; }

    // the finished scene (the old scene's name and per-sibling switches carry over)
    Scene finish (const Scene& old, uint32_t stamp)
    {
        Scene s = std::move (building);
        s.name = old.name;
        s.stamp = stamp;
        for (auto& p : s.parts)
            if (const auto* o = old.part (p.key()))
            {
                p.on = o->on;
                if (p.over.size() != p.values.size()) p.over.assign (p.values.size(), 0);
                for (int i = 0; i < p.size(); ++i)
                {
                    const int j = o->find (p.desc[(size_t) i].id);
                    if (j >= 0 && j < (int) o->over.size()) p.over[(size_t) i] = o->over[(size_t) j];
                }
            }
        for (auto& p : s.parts) if (p.over.size() != p.values.size()) p.over.assign (p.values.size(), 0);
        target = -1;
        return s;
    }
    void cancel() { target = -1; }

private:
    int target = -1, want = 0, got = 0;
    uint32_t req = 0;
    double started = 0.0;
    Scene building;
};

//==============================================================================
// The morph in flight, on SETBOOK's beat clock (beats counted from the audio thread: they run
// with the host's tempo whether the transport plays or not).
struct Morph
{
    uint32_t session = 0;
    int scene = -1, from = -1;
    double startBeat = 0.0;
    float beats = 0.0f;
    bool active = false;
    int tail = 0;                       // frames the final position is repeated after arriving

    float x (double beatNow) const
    {
        if (! active) return 1.0f;
        if (beats <= 0.0f) return 1.0f;
        const double t = (beatNow - startBeat) / (double) beats;
        return (float) (t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t));
    }
};

// When a recall launched now should start: QUANT 0 = at once, else the next multiple of
// quantQ quarter notes on the song (only while playing).
double launchPpq (double ppq, double quantQ, bool playing);

//==============================================================================
// MIDI: notes BASE..BASE+15 recall scenes 1-16, BASE+16..BASE+31 capture into them (NOTE CAPTURE
// on), program changes 0-15 recall (PC on). Channel 0 = any.
struct MidiMap
{
    int base = 36, channel = 0;
    bool pc = true, noteCapture = true;

    enum What { Nothing, Recall, Store };
    struct Hit { What what = Nothing; int scene = -1; int velocity = 0; };

    Hit note (int ch, int note, int velocity) const
    {
        Hit h;
        if (velocity <= 0 || (channel != 0 && ch != channel)) return h;
        const int k = note - base;
        if (k >= 0 && k < kScenes) { h.what = Recall; h.scene = k; h.velocity = velocity; }
        else if (noteCapture && k >= kScenes && k < 2 * kScenes) { h.what = Store; h.scene = k - kScenes; h.velocity = velocity; }
        return h;
    }
    Hit program (int ch, int program) const
    {
        Hit h;
        if (! pc || (channel != 0 && ch != channel)) return h;
        h.what = Recall;
        h.scene = program % kScenes;
        h.velocity = 127;
        return h;
    }
};

// factory names for empty scenes
const char* defaultSceneName (int i);

} // namespace sb
