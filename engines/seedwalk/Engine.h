// Vendored unchanged from SEEDWALK Source/engine (JUCE-free already).
// SEEDWALK's engine: Pinball 2's ball walks sixteen pinned rete nets. When the ball fires a step,
// rete glides from the net it is in (wherever a glide left it) to that step's net: the 8 x 8
// matrix interpolated entry by entry (optionally keeping its size), the gains, leak, scale and lvl
// with it, and the integrators never reset. The eight lanes excite, kick, hold or dip the net and
// play artifex and Beads. Runs in chunks of 32 samples; the ball is sample-accurate inside them.
#pragma once

#include "Modules.h"
#include "Pinball.h"
#include "Rate.h"
#include "Walk.h"
#include <atomic>
#include <climits>
#include <memory>
#include <vector>

namespace floater_beads { class Processor; }
namespace artifex_fx { struct Core; }
namespace forsitan_mod { struct Modulation; }

namespace sw
{

// What the editor reads. Written by the audio thread, read by the UI; tearing is harmless here.
struct Visual
{
    static constexpr int kTrail = 4096;       // node states, one frame every 4 samples
    static constexpr int kScope = 4096;
    std::array<std::array<float, kNodes>, kTrail> trail {};
    std::atomic<int> trailWrite { 0 };
    std::array<float, kScope> scopeL {}, scopeR {};
    std::atomic<int> scopeWrite { 0 };

    std::array<std::atomic<float>, kNodes> node {};       // |activity|, smoothed
    Matrix matrixNow {};                                  // the matrix rete is using
    std::array<std::atomic<float>, kNodes> gainNow {};
    std::atomic<float> leakNow { 0.0f }, scaleNow { 0.0f }, lvlNow { 0.0f }, heatNow { 0.0f }, excite { 0.0f };
    std::atomic<int> fromStep { -1 }, toStep { -1 }, ballStep { -1 };
    std::atomic<float> morph { 1.0f };                    // where the glide is, curved, 0..1
    std::atomic<float> dipNow { 0.0f };
    std::atomic<bool> holding { false }, frozen { false }, halted { false }, idle { true };
    std::array<std::atomic<float>, kLanes> laneEnv {};    // what each lane is doing now 0..1
    std::array<std::atomic<uint32_t>, kLanes> laneHits {};
    std::atomic<uint32_t> fires { 0 }, arrivals { 0 }, resets { 0 };
    std::atomic<float> levelL { 0.0f }, levelR { 0.0f }, clipAmount { 0.0f };
    std::atomic<int> axMode { 0 };

    // the event log
    struct Event { uint32_t kind = 0; int a = 0, b = 0; float v = 0.0f; double t = 0.0; };
    enum Kind : uint32_t { eFire = 1, eTrig, eChoke, eReset, eHalt, eIdle, eArrive, eMidiIn };
    static constexpr int kEvents = 256;
    std::array<Event, kEvents> events {};
    std::atomic<uint32_t> eventWrite { 0 };
    void push (uint32_t kind, int a, int b, float v, double t)
    {
        const auto w = eventWrite.load (std::memory_order_relaxed);
        events[(size_t) (w % kEvents)] = { kind, a, b, v, t };
        eventWrite.store (w + 1, std::memory_order_release);
    }
};

// the nets, the steps and the lanes: state the editor writes and the audio thread reads
struct NetAtom
{
    std::atomic<uint32_t> seed { 1 };
    std::array<std::atomic<float>, kNodes> gain {};
    std::atomic<float> leak { 0.5f }, scale { 0.6f }, lvl { 0.5f };
    Net load() const
    {
        Net n;
        n.seed = seed.load (std::memory_order_relaxed);
        for (int k = 0; k < kNodes; ++k) n.gain[(size_t) k] = gain[(size_t) k].load (std::memory_order_relaxed);
        n.leak = leak.load (std::memory_order_relaxed);
        n.scale = scale.load (std::memory_order_relaxed);
        n.lvl = lvl.load (std::memory_order_relaxed);
        return n;
    }
    void store (const Net& n)
    {
        seed.store (n.seed ? n.seed : 1u, std::memory_order_relaxed);
        for (int k = 0; k < kNodes; ++k) gain[(size_t) k].store (n.gain[(size_t) k], std::memory_order_relaxed);
        leak.store (n.leak, std::memory_order_relaxed);
        scale.store (n.scale, std::memory_order_relaxed);
        lvl.store (n.lvl, std::memory_order_relaxed);
    }
};

struct StepAtom
{
    std::atomic<int> trig { 0 }, velocity { 100 }, trigProb { 100 }, delayProb { 100 };
    std::atomic<float> duration { 2.0f }, delay { 0.0f };
    pinball::StepParams load() const
    {
        pinball::StepParams s;
        s.trig = trig.load (std::memory_order_relaxed);
        s.velocity = velocity.load (std::memory_order_relaxed);
        s.trigProb = trigProb.load (std::memory_order_relaxed);
        s.delayProb = delayProb.load (std::memory_order_relaxed);
        s.duration = duration.load (std::memory_order_relaxed);
        s.delay = delay.load (std::memory_order_relaxed);
        return s;
    }
    void store (const pinball::StepParams& s)
    {
        trig.store (s.trig); velocity.store (s.velocity); trigProb.store (s.trigProb); delayProb.store (s.delayProb);
        duration.store ((float) s.duration); delay.store ((float) s.delay);
    }
};

struct LaneAtom
{
    std::atomic<int> note { 36 }, dest { lMidi };
    std::atomic<bool> choke { false };
    Lane load() const { return { note.load (std::memory_order_relaxed), dest.load (std::memory_order_relaxed), choke.load (std::memory_order_relaxed) }; }
    void store (const Lane& l) { note.store (l.note); dest.store (l.dest); choke.store (l.choke); }
};

struct MidiOut
{
    struct Ev { int offset = 0, note = 0, velocity = 0; bool on = false; };
    static constexpr int kMax = 256;
    std::array<Ev, kMax> ev {};
    int count = 0;
    void clear() { count = 0; }
    void add (int offset, int note, int vel, bool on) { if (count < kMax) ev[(size_t) count++] = { offset, note, vel, on }; }
};

class Engine
{
public:
    Engine();
    ~Engine();

    void prepare (double sampleRate, int maxBlock);
    void reset();

    void process (const Params& p, const float* inL, const float* inR, float* outL, float* outR, int n, MidiOut* midi = nullptr);

    // any thread
    void requestReset() { pendingReset.store (true, std::memory_order_relaxed); }
    void requestHalt() { pendingHalt.store (true, std::memory_order_relaxed); }
    void requestJump (int step) { pendingJump.store (step, std::memory_order_relaxed); }
    void midiExcite (float velocity) { pendingExcite.store (velocity, std::memory_order_relaxed); }
    void allNotesOff() { pendingFlush.store (true, std::memory_order_relaxed); }

    std::array<NetAtom, kSteps> nets;
    std::array<StepAtom, kSteps> steps;
    std::array<LaneAtom, kLanes> lanes;
    pinball::Sequencer seq;
    Visual vis;
    double seconds() const { return clockSeconds; }

    // the net the engine is in now (the editor's PIN NOW)
    Net currentNet() const;

private:
    struct Values
    {
        Matrix m {};
        std::array<float, kNodes> gain {};
        float leak = 0.5f, scale = 0.6f, lvl = 0.5f, norm = 1.0f;
    };
    void chunk (const Params& p, const float* inL, const float* inR, float* outL, float* outR, int n, MidiOut* midi, int base);
    void beadsChunk (const Params& p, float* l, float* r, int n, bool seed);
    void startGlide (int step, const Params& p);
    void handle (const pinball::Event& e, const Params& p, MidiOut* midi, int base);
    void evaluate (const Params& p);      // the values rete gets this sample

    double sr = 48000.0;
    float dt = 1.0f / 48000.0f;
    Rete rete;
    Rng rng { 0xa5a5a5a5u };

    // the glide
    Values from, to, now;
    float glideT = 1.0f, glideStep = 0.0f;
    int fromStep = -1, toStep = -1;
    bool hasNet = false;

    // the walk
    pinball::EventList<512> events;
    double internalPpq = 0.0;
    bool wasPlaying = false, started = false;
    int64_t patternIndex = INT64_MIN;
    double sixteenth = 6000.0, timeMult = 4.0;
    std::atomic<bool> pendingReset { false }, pendingHalt { false }, pendingFlush { false };
    std::atomic<int> pendingJump { -1 };
    std::atomic<float> pendingExcite { 0.0f };

    // lanes
    std::array<float, kLanes> exEnv {}, exCoef {};
    float midiEnv = 0.0f, midiCoef = 0.999f;
    std::array<int, kLanes> holdLeft {}, dipLeft {}, freezeLeft {}, noteLeft {}, noteNum {};
    std::array<bool, kLanes> noteOn {};
    float dipEnv = 0.0f;
    bool seedThisChunk = false, beadsSeedPending = false;
    double clockSeconds = 0.0;
    int heatDiv = 0;

    // fx
    std::unique_ptr<artifex_fx::Core> ax;
    std::unique_ptr<forsitan_mod::Modulation> axMod;
    std::unique_ptr<floater_beads::Processor> beads;
    std::array<fl::BlockBridge, 4> bridges;
    int beadsMedia = -1;
    float sinceAxTrig = 10.0f, axTrigPeriod = 0.0f;

    // work
    std::vector<float> wl, wr, al, ar, bl, br;
    std::vector<uint8_t> axTrig;
    float dcL = 0.0f, dcR = 0.0f, dcXL = 0.0f, dcXR = 0.0f;
    float envL = 0.0f, envR = 0.0f, clipEnv = 0.0f;
    std::array<float, kNodes> nodeEnv {};
    int trailDiv = 0;
};

} // namespace sw
