// SEMI port: vendored from FLOATER Source/engine (v0.7), namespace fl -> flt; see engines/floater/sources.mk.
// The case: master clock -> rotating divider -> chance -> bursts -> modules -> effects -> reel -> bus.
#pragma once

#include "Types.h"
#include "Voices.h"
#include "Fx.h"
#include "Schwung.h"
#include "Reel.h"
#include "Drone.h"
#include <atomic>
#include <memory>

namespace flt
{

// What the editor reads (written on the audio thread, read on the message thread).
struct Display
{
    std::array<std::atomic<int>, kLanes> step {};      // current step of each lane
    std::array<std::atomic<int>, kLanes> div {};       // the division each lane gets after rotation
    std::array<std::atomic<int>, kLanes> fired {};     // counts hits (the editor flashes on change)
    std::array<std::atomic<int>, kLanes> bursts {};    // counts bursts
    std::array<std::atomic<float>, kLanes> energy {};  // how loud each module is now, 0..1
    std::array<std::atomic<uint32_t>, kLanes> live {}; // the pattern as LOCK has mutated it (bit s = step s)
    std::array<std::atomic<float>, kSources> source {};
    std::array<std::atomic<float>, kDests> dest {};
    std::atomic<int> rotation { 0 }, grains { 0 }, program { 0 }, degree { 0 }, breaks { 0 };
    std::atomic<int> turnTier { -1 }, turnIn { 0 };          // the next phrase line: tier (4/8/16/32 = 0..3), sixteenths to it
    std::atomic<bool> turning { false }, walled { false }, frozen { false };
    std::atomic<bool> running { false }, breaking { false }, host { false };
    std::atomic<double> ppq { 0.0 };
    std::atomic<float> bpm { 120.0f };
    // the CV sources over the last ~2.7 s, for the traces (written every 8th control step; read without a lock)
    static constexpr int kTrace = 512;
    std::array<std::array<float, kTrace>, kSources> trace {};
    std::atomic<int> traceWrite { 0 };
    // SMR: each band's level and frequency, and how far it has rotated
    std::array<std::atomic<float>, 6> smrLevel {}, smrHz {};
    std::atomic<int> smrRotation { 0 };
    // SEQ: the step it is on, and its X / Y
    std::atomic<int> seqIndex { 0 }, seqX { 0 }, seqY { 0 };
    // where the REEL voices in the module slots read, in the reel window (< 0 silent)
    std::array<std::atomic<float>, kVoices> slotReel {};
    std::atomic<float> feedback { 0.0f };              // the level coming back round the loop
    // the DRONE: where it is (A's frequency, its scale degree), the resonator's strings
    std::atomic<float> droneHz { 0.0f };
    std::atomic<int> droneDegree { 0 };
    std::array<std::atomic<float>, SympatheticStrings::kStrings> resHz {};
    // the output, for the scope (written at the end of every block; read without a lock)
    static constexpr int kScope = 4096;
    std::array<float, kScope> scopeL {}, scopeR {};
    std::atomic<int> scopeWrite { 0 };
};

class Engine
{
public:
    Engine();
    ~Engine();
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void setParams (const Params& p) { params = p; }
    const Params& getParams() const { return params; }

    // a MIDI note or the editor: strike lane (0..7) at sample offset
    void hit (int lane, float velocity, int offset);

    // renders n samples (stereo, overwritten); internally in chunks of at most kChunk
    void process (float* outL, float* outR, int n, const Transport& t);
    static constexpr int kChunk = 256;

    Display display;
    const Reel::Display& reelDisplay() const { return reel.display; }
    Drone& droneView() { return drone; }                 // the XY view reads its ring

    // the hits fired in the last process() call (for MIDI out): offset in the block, lane, velocity
    struct Fired { int offset; int lane; float vel; };
    const Fired* firedHits() const { return fired.data(); }
    int numFiredHits() const { return numFired; }

    // what the matrix sends to destination d right now (for tests and the editor)
    float destValue (int d) const { return dests[(size_t) d]; }
    float sourceValue (int s) const { return sources[(size_t) s]; }
    // the pattern of lane i as LOCK has mutated it (for tests)
    uint32_t liveMask (int i) const { return display.live[(size_t) i].load (std::memory_order_relaxed); }

private:
    struct Event { int64_t at = 0; int lane = 0; float vel = 0; bool burst = false; int step = -1; int sub = 0; };

    void processChunk (float* outL, float* outR, int n, const Transport& t);
    void tick (int64_t k, int64_t at, double bpm);
    bool laneGate (int i, int64_t k, int s, int steps, int div);
    void tickReel (int64_t k, int64_t at, double tickLen);
    void reelGene (int64_t k, int64_t at, double tickLen, uint32_t half);
    void seqStep (bool x, bool y);
    void schedule (const Event& e);
    void fire (const Event& e, int offset);
    void applyStepAndVary (const Event& e, Strike& s);
    void control (float dt);
    void updateDests();
    void render (int from, int to);
    void mixVoices (int n);
    const float* crossMod (int v, const float* x, int n);
    void feedbackReturn (int n);
    void runEffects (float* outL, float* outR, int n);
    void runResonator (int n);
    void controlDrone (float dt);
    void updateSustain();
    void sustainStrike (int v);
    float cvOf (int choice) const;
    void runReel (float* outL, float* outR, int n);
    void runBus (float* outL, float* outR, int n);
    void writeDisplay (int n);
    float rnd (uint32_t purpose, int lane = 0);
    void newProgram();

    Params params;
    double sr = 48000.0;
    int64_t now = 0;
    double internalPpq = 0.0;
    double lastBpm = 120.0;
    uint64_t freeCounter = 0;
    int64_t currentKey = 0;

    std::array<std::unique_ptr<VoiceBase>, kVoices> voices;
    StrikeVoice* strikeVoice = nullptr;
    ZigZag zig;
    Scatter scat;
    Space space;
    std::array<std::unique_ptr<PlaitsVoice>, kVoices> plaits;
    std::array<std::unique_ptr<BraidsVoice>, kVoices> braids;
    Reel reel;
    std::array<std::unique_ptr<ReelVoice>, kVoices> reelVoices;
    Smr smr;
    int smrRot = 0, traceTick = 0;
    float woggleV = 0.0f;
    std::unique_ptr<CloudsScatter> clouds;
    std::unique_ptr<ParasitesScatter> parasites;
    std::unique_ptr<BeadsScatter> beads;
    std::unique_ptr<CloudSeedSpace> seed;
    Drone drone;
    SympatheticStrings res;
    double arcPhase = 0.0, arcRate = 0.0;               // ARC: where in its phrase, how fast (per second)
    std::array<bool, kVoices> susOn {};
    std::array<int, kVoices> susModel {};
    float seedIn = 1.0f;                                // CloudSeed's input, closed while SPACE is INFINITE
    struct DrumBus;
    std::unique_ptr<DrumBus> bus;
    dsp::Rng rng;
    int64_t freezeUntil = 0;
    int chunkBase = 0;
    std::array<Fired, 256> fired {};
    int numFired = 0;
    float turnChance = 0.0f, turnBurst = 0.0f, turnFill = 0.0f;
    bool wallNow = false;

    std::array<Event, 512> queue {};
    int queued = 0;
    int64_t lastTick = -1;

    // ---- sequencing
    std::array<uint32_t, kLanes> flips {};             // LOCK: the steps each lane has flipped
    std::array<bool, kLanes> lastCond {};              // PRE: the last condition on the lane passed
    int lastFlipReset = 0;
    int seqXPos = 0, seqYPos = 0;
    float seqTarget = 0.0f;
    int organizeStep = 0;                              // REEL ORGANIZE moved on by a lane

    // ---- CV sources
    std::array<float, kSources> sources {};
    std::array<float, kDests> dests {};
    float smoothTarget = 0, smoothTime = 0, pongH = 1, pongV = 0, lfoPh = 0, forgeEnv = 0, forgePh = 0, chaosX = 0.4f;
    int controlLeft = 0;

    // ---- space program
    float progSize = 0, progDecay = 0, progDamp = 0, progDiff = 0.7f, progMod = 0.3f, progRate = 0.3f, progCross = 0.5f, progPre = 0.05f;
    int program = 0;

    // ---- per-hit drift (VARY): each slot's pan offset from its last hit
    std::array<float, kVoices> panOff {};

    // ---- X-MOD state per slot
    struct XMod { std::array<float, 512> line {}; int w = 0; float env = 0.0f; };
    std::array<XMod, kVoices> xmods {};
    float xAtk = 0.5f, xRel = 0.999f;

    // ---- feedback: the master's history, and the SMR's rings (the FDBK source of X-MOD)
    std::vector<float> histL, histR, smrRing;
    int64_t histCap = 1;
    static constexpr int kSmrRing = 2048;
    dsp::OnePole fbHpL, fbHpR, fbLpL, fbLpR;
    float fbLevel = 0.0f;

    // ---- scratch (kChunk long)
    std::array<std::vector<float>, kVoices> vraw;
    std::vector<float> vbuf, xbuf, fbTap, dryL, dryR, pdryL, pdryR, sendL, sendR, zl, zr, gl, gr, sl, sr2, inL, inR, ml, mr;
    std::vector<float> droneL, droneR, droneMono, resIn;
    std::array<float, kVoices> panL {}, panR {};
};

} // namespace flt
