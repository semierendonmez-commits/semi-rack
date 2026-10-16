// semi-rack: vendored; namespaces wrapped in dnafam:: (see sources.mk for every change).
// The real-time part of GENOTYPE. Allocation free once prepared.
//
// The ball (walk::Walker) moves on the host's grid, one move per 1/16 x TIME. Each
// move draws the next step from one of three matrices, chosen by where the tick sits
// in the phrase: CALL, RESPONSE, or FILL (the ending). The step it lands on plays A's
// own hits of that step, each with its DNA: its micro-timing (FEEL), its accent
// (DYNAMICS), its ghosts (GHOSTS), its lane (kit piece). The sound is A's slice at
// MORPH between A and B (BREAKDNA's spectral layers). Every hit also goes to MIDI
// out on its lane's note.
//
// Per step (as in the Pinball device): trig probability, velocity, delay and delay
// probability, mute. Per lane: note, mute, choke (a lane with choke on is cut when
// another lane fires: open / closed hat). MONO: every hit cuts the last (a break).
//
// The walk is decided half a tick before the tick, so hits A plays early land early
// without any reported latency.
#pragma once

#include "Learn.h"
#include <atomic>
#include <memory>

namespace dnafam::geno
{

/** Everything the audio thread needs of the breaks. Immutable once published. */
struct Bundle
{
    std::shared_ptr<const dna::Kit> kit;
    Learned learned;
    int serial = 0;
    uint64_t key = 0;               // which breaks (and grids) it was learned from
};

struct StepEdit
{
    std::atomic<float> prob { 100.0f }, vel { 100.0f }, delay { 0.0f }, delayProb { 100.0f };
    std::atomic<int> mute { 0 };
};

struct LaneEdit
{
    std::atomic<int> note { 36 }, mute { 0 }, choke { 0 };
};

struct Brain
{
    walk::Matrix genome, call, resp, fill;     // genome: learned (and edited); the rest derived
    std::array<StepEdit, kMaxSteps> steps;
    std::array<LaneEdit, NumLanes> lanes;
    Brain()
    {
        for (int l = 0; l < NumLanes; ++l) lanes[(size_t) l].note.store (laneDefaultNote[l]);
        lanes[LHat].choke.store (1);
        lanes[LOpen].choke.store (1);
    }
};

enum Run { RunHost = 0, RunAlways };

struct Params
{
    float feel = 1.0f, dyn = 1.0f, ghosts = 1.0f;
    float morph = 0.0f;
    float decay = 1.0f, tune = 0.0f, gain = 1.0f;
    bool mono = true;
    int form = Form22, ending = EndBeat;
    float mult = 1.0f;              // TIME: the ball's tick in 16ths
    float swing = 0.0f;             // 0..1: odd ticks late by swing x half a tick
    int startStep = 1;
    float resetBars = 0.0f;         // 0 = AUTO (the phrase)
    bool midiOut = true;
    int run = RunHost;
    uint32_t seed = 1;
    bool playing = false;
    double ppq = 0.0, bpm = 120.0;
};

struct MidiEvent { int offset = 0; uint8_t note = 0, vel = 0; };

struct HitEvent
{
    int16_t slice = -1, bslice = -1;
    int8_t step = -1, lane = 0, role = 0;
    float amp = 0.0f, offsetMs = 0.0f;
};

struct MoveEvent { int8_t from = -1, to = -1, role = 0; uint8_t fired = 0; };

/** One move of the ball: what the tick plays. Shared by the engine and the offline walk. */
struct Note
{
    double pos = 0.0;               // 16ths, absolute
    int hit = -1;                   // index into A's genome
    int lane = 0, step = 0, role = 0;
    float amp = 0.0f;               // 0..~1.5
};

/** Plays tick k: moves the ball, returns the notes it fires (written to out, count returned). */
int playTick (const Bundle& b, Brain& brain, const Params& p, walk::Walker& w, int64_t k, bool resetHere,
              Note* out, int maxOut, int& stepOut, int& roleOut);

/** The walk offline: bars of it from the start (resets as the engine does). For the MIDI drag and the tests. */
std::vector<Note> walkOffline (const Bundle& b, Brain& brain, const Params& p, int bars, uint32_t seed);

class Engine
{
public:
    static constexpr int kVoices = 24;
    static constexpr int kQueue = 512;
    static constexpr int kMaxOut = 256;

    void prepare (double sampleRate, int maxBlock);
    void reset();
    int latency() const { return 0; }

    void process (const Bundle* b, Brain& brain, const Params& p, const MidiEvent* in, int numIn,
                  float* outL, float* outR, int n, MidiEvent* midiOutBuf, int& numOut);

    // control (any thread)
    std::atomic<int> resetRequest { 0 }, haltRequest { 0 }, jumpRequest { -1 };

    // views
    std::atomic<int> viewStep { -1 }, viewRole { 0 }, viewPlaying { 0 }, viewHalted { 0 };
    std::atomic<double> viewBpm { 120.0 };
    std::atomic<int64_t> viewTick { 0 };
    bool popHit (HitEvent& h);
    bool popMove (MoveEvent& m);

    int queued() const { return numQueued; }

private:
    struct Voice
    {
        bool active = false;
        int slice = 0, start = 0, len = 0, lane = 0;
        double pos = 0.0, inc = 1.0;
        float amp = 1.0f, morph = 0.0f, env = 1.0f, decayMul = 1.0f;
        int fade = -1, fadeLen = 1, rampPos = 0, rampLen = 22;
        uint32_t age = 0;
    };
    struct Pending
    {
        double pos = 0.0;           // 16ths (timeline) ; < 0 unused
        int64_t at = 0;             // or an absolute sample (MIDI offs, pads)
        bool timed = false;         // true: `at` is used
        int hit = -1, lane = 0, step = 0, role = 0;
        uint8_t note = 0, vel = 0;  // vel 0: off
        float amp = 0.0f;
        bool midiOnly = false;
    };

    void trigger (const dna::Kit& k, const Params& p, int slice, int lane, float amp, int step, int role, float offMs);
    void render (const dna::Kit& k, float* L, float* R, int from, int to, const Params& p);
    void push (const Pending& e) { if (numQueued < kQueue) queue[(size_t) numQueued++] = e; }
    template <typename T, size_t N> static void emit (std::array<T, N>& ring, std::atomic<int>& wr, std::atomic<int>& rd, const T& v);
    template <typename T, size_t N> static bool pop (std::array<T, N>& ring, std::atomic<int>& wr, std::atomic<int>& rd, T& v);

    double sr = 44100.0;
    int64_t clock = 0;
    double freePos = 0.0, lastPos = -1.0e9;
    int64_t nextTick = INT64_MIN;   // next tick to decide
    bool wasRunning = false;
    const Bundle* lastBundle = nullptr;
    walk::Walker walker;
    uint32_t seedSeen = 0;
    double lastMult = 1.0;
    bool pendingReset = false;
    std::array<bool, NumLanes> lastChoke {};

    std::array<Voice, kVoices> voices {};
    uint32_t ageCounter = 0;
    std::array<Pending, kQueue> queue {};
    int numQueued = 0;

    std::array<HitEvent, 256> hits {};
    std::atomic<int> hitWrite { 0 }, hitRead { 0 };
    std::array<MoveEvent, 256> moves {};
    std::atomic<int> moveWrite { 0 }, moveRead { 0 };
};

} // namespace dnafam::geno
