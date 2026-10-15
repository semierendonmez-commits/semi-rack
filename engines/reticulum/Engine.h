// RETICULUM's engine: the gate, the two random banks, MG-16, eleven modulators, rete, then
// artifex and Beads. Runs in chunks of 32 samples so Beads' seed lands within a chunk of its gate.
#pragma once

#include "Modules.h"
#include "Rate.h"
#include <atomic>
#include <climits>
#include <memory>
#include <vector>

namespace floater_beads { class Processor; }
namespace artifex_fx { struct Core; }
namespace forsitan_mod { struct Modulation; }

namespace rt
{

// What the editor reads. Written by the audio thread, read by the UI; tearing is harmless here.
struct Visual
{
    static constexpr int kTrail = 4096;       // node states, one frame every 4 samples
    static constexpr int kScope = 4096;       // output
    std::array<std::array<float, kNodes>, kTrail> trail {};
    std::atomic<int> trailWrite { 0 };
    std::array<float, kScope> scopeL {}, scopeR {};
    std::atomic<int> scopeWrite { 0 };

    std::array<std::atomic<float>, kNodes> node {};         // |activity|, smoothed
    std::array<std::atomic<float>, kNodes> nodeGain {};     // the gain rete is using (knob + mod + CV)
    std::array<std::atomic<float>, kTargets> effective {};  // knob + modulator, 0..1 (what the dials show)
    std::array<std::atomic<float>, kTargets> modOut {};     // the modulators, -1..1
    std::array<std::atomic<float>, kRndOuts * 2> rnd {};    // volts
    std::array<std::atomic<float>, kMg> mg {};              // volts
    std::array<std::atomic<bool>, kMg> mgHolding {};
    std::array<std::atomic<float>, kMg> mgFreq {}, mgWave {};   // LFO pitch (Hz) and wave blend 0..3 (sin tri saw sqr)
    std::atomic<float> excite { 0.0f };
    std::atomic<uint32_t> seed { 1 };
    std::atomic<uint32_t> reseeds { 0 }, rollsA { 0 }, rollsB { 0 }, mgRolls { 0 }, gateEdges { 0 };
    std::atomic<uint8_t> gateBits { 0 };
    std::atomic<bool> gateIn { false }, frozen { false };
    std::atomic<float> levelL { 0.0f }, levelR { 0.0f }, clipAmount { 0.0f };
    std::atomic<int> axMode { 0 };

    // the event log: what happened, for the screen that writes it out
    struct Event { uint32_t kind = 0, a = 0; float v = 0.0f; double t = 0.0; };
    enum Kind : uint32_t { eReseed = 1, eRollA, eRollB, eMg, eGateOn, eGateOff, eSeed, eFreeze };
    static constexpr int kEvents = 128;
    std::array<Event, kEvents> events {};
    std::atomic<uint32_t> eventWrite { 0 };
    void push (uint32_t kind, uint32_t a, float v, double t)
    {
        const auto w = eventWrite.load (std::memory_order_relaxed);
        events[(size_t) (w % kEvents)] = { kind, a, v, t };
        eventWrite.store (w + 1, std::memory_order_release);
    }
};

class Engine
{
public:
    Engine();
    ~Engine();

    void prepare (double sampleRate, int maxBlock);
    void reset();

    // in: the audio input (may be null). MIDI gate: notes held, set by the processor between calls.
    void process (const Params& p, const float* inL, const float* inR, float* outL, float* outR, int n);
    void setMidiGate (bool held) { midiHeld = held; }
    void setUiPad (bool down) { uiPad.store (down, std::memory_order_relaxed); }
    void reseedNow (uint32_t s) { pendingSeed.store (s ? s : 1u, std::memory_order_relaxed); }   // any thread
    void fire() { pendingFire.fetch_add (1, std::memory_order_relaxed); }                        // a tap of the gate, any thread
    // the PUSH buttons of the two banks and MG-16 (bit 0 A, 1 B, 2 MG), any thread
    void push (int which) { pendingPush.fetch_or (1 << which, std::memory_order_relaxed); }

    uint32_t seed() const { return rete.seed; }
    double seconds() const { return clockSeconds; }
    Visual vis;

private:
    void chunk (const Params& p, const float* inL, const float* inR, float* outL, float* outR, int n);
    void beadsChunk (const Params& p, float* l, float* r, int n, bool seed);

    double sr = 48000.0;
    float dt = 1.0f / 48000.0f;
    Rete rete;
    RandomValues rndA { 0x1234abcdu }, rndB { 0x7e57c0deu };
    int lastRangeA = 0, lastRangeB = 0;
    Mg16 mg { 0x3c6ef372u };
    ManualGate gate;
    std::array<Modulator, kTargets> mods;
    std::array<bool, kDests> prevDest {};
    std::array<float, kTargets> knobPrev {};
    bool knobsPrimed = false;
    Rng rng { 0xa5a5a5a5u };

    // clocks
    double clockSeconds = 0.0, internalPpq = 0.0, lastPpq = 0.0;
    float clkPhase = 0.0f, clkPeriodScale = 1.0f, clkHighLeft = 0.0f;
    static constexpr int64_t kNoIndex = INT64_MIN;
    int64_t clkLastIndex = kNoIndex;
    std::array<int64_t, kModSyncs> syncIndex {};
    float gatePeriod = 0.5f, sinceModGate = 10.0f;
    float sinceAxTrig = 10.0f, axTrigPeriod = 0.0f;
    bool midiHeld = false;
    std::atomic<bool> uiPad { false };
    std::atomic<uint32_t> pendingSeed { 0 };
    std::atomic<int> pendingFire { 0 }, pendingPush { 0 };
    float fireLeft = 0.0f;

    // fx
    std::unique_ptr<artifex_fx::Core> ax;
    std::unique_ptr<forsitan_mod::Modulation> axMod;
    std::unique_ptr<floater_beads::Processor> beads;
    std::array<fl::BlockBridge, 4> bridges;
    int beadsMedia = -1;
    bool beadsSeedPending = false;

    // work
    std::vector<float> wl, wr, al, ar, bl, br;
    std::vector<uint8_t> axTrig;
    float dcL = 0.0f, dcR = 0.0f, dcXL = 0.0f, dcXR = 0.0f;
    float envL = 0.0f, envR = 0.0f, clipEnv = 0.0f;
    std::array<float, kNodes> nodeEnv {};
    int trailDiv = 0;
};

} // namespace rt
