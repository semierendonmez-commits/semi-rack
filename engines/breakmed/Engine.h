// BREAKMED - the real-time part: MIDI -> slice -> weighted chain -> voice -> Paula out.
// SEMI Family port: vendored from semierendonmez-commits/breakmed Source/engine. One change:
// `captureSeconds` (public, default 32 as before) sizes the capture buffer, so the Rack /
// MetaModule module (no DAW drag-out) can set it to 0 instead of holding 12 MB of floats.
#pragma once

#include "Dsp.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace bm
{

constexpr int kVoices = 16;
constexpr int kChains = 7;
enum Chain : int { Base = 0, PitchDn, PitchUp, Sub, Glitch, Reverse, Drill };
const char* chainName (int c);       // "BASE" "P.DN" "P.UP" "SUB" "GLCH" "REV" "DRIL"
const char* chainFx (int c, int semis); // tracker-style effect text for the hit log

// note <-> slice, chromatic or on white keys only (-1 = no slice)
int sliceForNote (int note, int root, bool white);
int noteForSlice (int slice, int root, bool white);

enum class Anchor : int { Off = 0, Downbeat, Kicks };
enum class Filter : int { Off = 0, A500, Led };

struct Params
{
    float volume = 48.0f;           // 0..64
    float pitch = 0.0f;             // semitones
    float fine = 0.0f;              // cents
    float attackMs = 0.0f, releaseMs = 4.0f;
    bool interp = false;
    Filter filter = Filter::Off;
    bool gate = true;               // note-off ends the hit (strict, for 1/16 and 1/32 drills)
    bool thru = false;              // T.MODE THRU: play on past the slice end
    bool choke = true;              // one tracker channel: a new hit cuts the last one
    bool velocity = true;
    int root = 36;                  // C1 = slice 1
    bool whiteKeys = false;         // slices on white keys only (C1 D1 E1 ...), as in the original Simpler setup

    std::array<float, kChains> weight { 55, 22, 12, 6, 4, 4, 3 };
    std::array<bool, 3> down { true, true, true };   // -1 -2 -3
    std::array<bool, 2> up { true, true };           // +1 +2
    bool pitchStretch = true;       // STRETCH (time kept) / REPITCH
    float cycleMs = 35.0f;          // Akai-style cycle length
    float grainMs = 80.0f;          // glitch: grains 20 ms .. grainMs
    float flux = 0.5f;              // glitch: position jitter + reversed grains
    float glitchSpeed = 0.5f;       // glitch: how fast the slice is read (time stretch)
    int drillRate = 1;              // 1/8 1/16 1/32
    float drillLoop = 0.5f;         // loop length, fraction of a 16th
    float drillPan = 1.0f;

    Anchor anchor = Anchor::Off;
    float memory = 0.4f;            // after a variation, chance the next one falls back to BASE
    bool lock = false;              // LOCK: the same choice at the same place every N bars
    int lockBars = 2;
    uint32_t seed = 0x5eed;

    bool play = false;              // the built-in player: the break in its own order
    bool syncHost = true;
    int captureBars = 8;
};

struct Transport
{
    bool valid = false;             // host gave a position
    bool playing = false;
    double ppq = 0.0;
    double bpm = 120.0;
};

struct Event
{
    int offset = 0;
    bool on = true;
    int note = 0;
    float velocity = 1.0f;
    int forceChain = -1;            // audition
    bool oneShot = false;
    double ppq = 0.0;               // musical position of the event, when known
    bool hasPpq = false;
};

struct Hit
{
    int slice = 0, chain = 0, semis = 0, r = 0, note = 0;
    float velocity = 1.0f;
    uint32_t serial = 0;
};

class Engine
{
public:
    Engine();

    double captureSeconds = 32.0;   // SEMI port: capture buffer length (0 = no capture)
    void prepareToPlay (double sampleRate, int maxBlock);
    // Audio thread. Hard-stops voices that read the old data.
    void setPrepared (const Prepared* p);
    const Prepared* prepared() const { return data; }

    // Events must be sorted by offset.
    void process (float* outL, float* outR, int n, const std::vector<Event>& events,
                  const Params&, const Transport&);
    void allNotesOff();

    // ---- what the editor reads -------------------------------------------------------
    static constexpr int kLog = 64;
    std::array<Hit, kLog> log {};
    std::atomic<uint32_t> logSerial { 0 };
    struct VoiceView { std::atomic<int> slice { -1 }, chain { 0 }; std::atomic<float> pos { 0 }, level { 0 }; };
    std::array<VoiceView, kVoices> view {};
    std::array<std::atomic<float>, kChains> chainFlash {};
    std::atomic<int> lastR { -1 };
    std::atomic<float> outPeak { 0.0f };
    std::atomic<double> playPpq { 0.0 };
    std::atomic<bool> playerRunning { false };

    // ---- capture (master resampling) -----------------------------------------------
    // Message thread: arm(); audio thread fills; message thread takes the result.
    void armCapture() { captureReq.store (true); }
    void cancelCapture() { captureCancel.store (true); }
    enum class CapState : int { Idle = 0, Waiting, Recording, Done };
    std::atomic<int> capState { 0 };
    std::atomic<float> capProgress { 0.0f };
    int capturedLength() const { return capLen; }
    const std::vector<float>& capturedL() const { return capL; }
    const std::vector<float>& capturedR() const { return capR; }
    double captureBpm() const { return capBpm; }
    void releaseCapture() { capState.store ((int) CapState::Idle); }

    // Choice made for a hit; exposed for the tests.
    struct Choice { int chain = 0, semis = 0, r = 0; };
    Choice choose (int slice, int note, double ppq, bool hasPpq, const Params&);

private:
    struct Grain { double src = 0, ph = 0, len = 1; bool rev = false; bool on = false; };
    struct Voice
    {
        bool active = false;
        int note = -1, slice = 0, chain = 0, semis = 0;
        int s0 = 0, s1 = 0;              // read window in pcm
        double pos = 0.0;                // read / timeline position (pcm samples)
        double rate = 1.0;               // read speed (pcm samples per output sample)
        double ratio = 1.0;              // pitch for STRETCH and GLITCH grains
        double cycle = 1.0, cyclePh = 0.0;
        double anchorA = 0.0, anchorB = 0.0;
        Grain g[2];
        double grainClock = 0.0;
        int dir = 1;                     // drill ping-pong
        double loopA = 0, loopB = 0;
        double lfoPh = 0, lfoInc = 0;
        float env = 0.0f, envTarget = 1.0f, envStep = 0.0f;
        int stage = 0;                   // 0 attack, 1 hold, 2 release
        bool held = false, oneShot = false;
        float gain = 1.0f, pan = 0.0f;
        int64_t age = 0;
        int64_t maxAge = 0;
        uint32_t rng = 1;
    };

    void noteOn (const Event&, const Params&, const Transport&);
    void noteOff (int note, const Params&);
    void startRelease (Voice&, float ms);
    float render (Voice&, const Params&);
    float read (const std::vector<float>& b, double p, int lo, int hi, bool interp) const;
    float readPcm (double p, int lo, int hi, bool interp) const { return read (data->pcm, p, lo, hi, interp); }
    void runPlayer (int n, const Params&, const Transport&, std::vector<Event>& out);
    void spawnGrain (Voice&, int k, const Params&);
    static uint32_t hash (uint32_t a, uint32_t b, uint32_t c);
    float rand01 (Voice&);

    const Prepared* data = nullptr;
    double sr = 48000.0;
    std::array<Voice, kVoices> voices {};
    std::vector<Event> merged;
    uint32_t rng = 0x12345u;
    int prevChain = 0;
    int64_t clock = 0;

    // player
    double freePpq = 0.0;
    bool wasRunning = false;
    int playerNote = -1;
    double lastPpqEnd = -1.0;

    // filters
    float aLp[2] = { 0, 0 }, ledA[2] = { 0, 0 }, ledB[2] = { 0, 0 };
    float hpX[2] = { 0, 0 }, hpY[2] = { 0, 0 };

    // capture
    std::atomic<bool> captureReq { false }, captureCancel { false };
    std::vector<float> capL, capR;
    int capLen = 0, capPos = 0, capTarget = 0;
    double capBpm = 120.0;
    float peakHold = 0.0f;
};

} // namespace bm
