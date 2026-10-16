// NMS — the instrument around the rack, JUCE-free (SEMI Family port).
//
// New file: the audio-thread half of the plugin's NmsProcessor (Source/PluginProcessor.cpp)
// without JUCE: factory patch build (loadFactory), default macro routes, dealt patterns
// (dealPatterns), recall / blend (recall, stepBlend), the transport (transport, warp), the
// 24 macros + AWARE + CHAOS (modulation), WANDER (wander), SCENES (scenes), the ENERGY bus,
// the LOOP PACK key mapping (midiEvent), the knob randomiser (randomize, KNOBS only) and
// EVOLVE. The logic follows the plugin line by line.
//
// Not ported (no place in a Rack module): undo, locks, user presets / XML state, the four
// global LFOs (no default routes use them), the recorder, the randomiser's PATCH target
// (re-cabling), adding / deleting patterns, MIDI out.
//
// Threads: build () allocates (call it off the audio thread); everything else is meant for
// the audio thread and does not allocate.
#pragma once
#include "Rack.h"
#include "Modules.h"
#include <map>

namespace nms
{
struct ModRoute { int kind = 1; int src = 0; int mod = 0; int param = 0; float amount = 0.0f; };   // kind 1 = macro
struct Pattern { std::map<int, std::vector<float>> values; float hue = 0.3f; };

// juce::Random's generator (48-bit LCG), so dealt patterns match the plugin
struct JRandom
{
    int64_t seed;
    explicit JRandom (int64_t s = 1) : seed (s) {}
    int nextInt() { seed = (int64_t) ((((uint64_t) seed * 0x5deece66dULL) + 11) & 0xffffffffffffULL); return (int) (seed >> 16); }
    int nextInt (int maxValue) { return (int) ((((uint64_t) (unsigned int) nextInt()) * (uint64_t) maxValue) >> 32); }
    bool nextBool() { return (nextInt() & 0x40000000) != 0; }
    float nextFloat() { const float r = (float) (uint32_t) nextInt() / 4294967296.0f; return r < 0.99999994f ? r : 0.99999994f; }
};

// External transport (the Rack module's CLK input): when `known`, it drives tempo,
// start / stop and the position (the plugin's host-locked mode).
struct ExtTransport { bool known = false, playing = false; double bpm = 120.0, pos16 = 0.0; };

// What the module's routing screen shows, computed once per build.
struct RouteView
{
    static constexpr int kPerLane = 5, kCables = 12, kTop = 8;
    struct Lane { int n = 0; char names[kPerLane][11] {}; bool fx[kPerLane] {}; };
    Lane lanes[kLanes];
    int numTop = 0; char top[kTop][11] {};
    struct Cable { int srcLane = -1, dstLane = -1; char label[24] {}; };
    int numCables = 0; Cable cables[kCables];
    int trigMod[kChannels]; int trigOut[kChannels];   // rack index / output of each channel's first instrument
};

class Host
{
public:
    Host() { blendItems.reserve (2048); }

    // ---- build (off the audio thread) -----------------------------------------------
    bool build (int patchIndex, double sampleRate);
    int patchIndex = 0;
    std::string patchName;
    RouteView view;

    // ---- audio thread -----------------------------------------------------------------
    void process (float* L, float* R, int n, const ExtTransport& ext);
    void restart() { restartPending = true; }
    void noteOn (int note, float vel);
    void noteOff (int note);
    void keyPitchCv (float octavesFromC3) { if (ctx.keyGate > 0.5f) ctx.keyPitch = octavesFromC3; }

    void recall (int index, double seconds, int type = -1, bool fromWander = false);
    void recallBlended (int index);                  // with PATTERNS' BLEND time
    void nextPattern (int dir);
    void randomPattern();
    void randomize (int amount, int type = 0);       // 0 nudge, 1 medium, 2 a lot; type 0 all, 1 gen, 2 sound

    float macro[kMacros];                            // 0..1, 0.5 = centre (the plugin's macro params)
    float chaosKnob = 0.0f;                          // CHAOS amount when the patch has no CHAOS module
    float outDb = 0.0f;
    bool softClip = true;
    int evolveBars = 0;                              // 0 = off, else 1/2/4/8/16
    int evolveAmount = 0;                            // 0 nudge, 1 medium, 2 a lot

    // read-outs
    Rack rack;
    Ctx ctx;
    std::vector<Pattern> patterns;
    int currentPattern = -1;
    float blendPos = 1.0f;
    float energy = 0.5f;
    int sceneIdx = -1;
    bool macroResetRequest = false;                  // LOOP PACK asked for the macros to centre
    Module* find (const char* type) const { return rack.findType (type); }
    float param (Module* m, const char* key) const { const int i = m ? m->spec.param (key) : -1; return i >= 0 ? m->getPlain (i) : 0.0f; }
    void setParam (Module* m, const char* key, float plain) { const int i = m ? m->spec.param (key) : -1; if (i >= 0) m->setPlain (i, plain); }

private:
    void addDefaultRoutes (const Module&);
    void dealPatterns (int count, uint32_t seed);
    Pattern snapshot() const;
    void transport (const ExtTransport&);
    void modulation (int n);
    void wander (int n);
    void scenes();
    void stepBlend (int n);
    void serviceRequests();
    double warp (double raw16) const;
    void buildView();

    std::vector<ModRoute> routes;
    struct BlendItem { Module* m; int p; float from, to, breath; };
    std::vector<BlendItem> blendItems;
    double blendT = 1.0, blendDur = 0.0;
    double sampleRate = 48000.0, rawPos = 0.0, timeSec = 0.0;
    double wanderPhase = 0.0;
    int wanderChanges = 0, patternChanges = 0;
    float swing = 0, wonk = 0; int swingUnit = 1; double wonkDiv = 4;
    bool wasRunning = false, restartPending = false;
    int sceneBarsLeft = 0, sceneBars = 1, sceneRestart = 0; long long sceneBar = -1;
    long long lastBar = -1;
    int previousPattern = -1;
    int heldPatternKeys = 0;
    int patternKeys[16] {}; int numPatternKeys = 0;
    int held[16] {}; int numHeld = 0;
    JRandom rnd { 0x51a7e };
};
} // namespace nms
