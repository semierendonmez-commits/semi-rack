// semi-rack: vendored; namespaces wrapped in dnafam:: (see sources.mk for every change).
// The real-time part of LOOPGENOME. Allocation free once prepared.
//
// One ball (walk::Walker) moves on the host's grid, one move per 1/16 x TIME, drawing the
// next step from CALL, ANSWER or FILL by the place in the phrase (GENOTYPE). The step it
// lands on plays:
//   * the break's hits of that step (GENOTYPE's voices: feel, dynamics, ghosts, lanes),
//   * the bass cell of that step (rest / note / held, semitones, slide, bend, velocity),
//   * that step's articulation: when it differs from the one sounding, the bass switches
//     (NEUROSTACK's phase-locked layers over one sub). A jump of the ball is therefore a
//     drum switch-up and a bass switch-up at once.
// The kick hits that play duck the bass's sub (DUCK), the snare hits that play close its
// note GAP ms before (SNARE GAP). Everything is decided half a tick early.
#pragma once

#include "../genotype/Genotype.h"
#include "Joint.h"
#include "Neuro.h"

namespace dnafam::lg
{

/** Everything the audio thread needs. Immutable once published. */
struct Bundle : geno::Bundle
{
    Joint joint;
};

struct StepBass
{
    std::atomic<int> art { 1 };       // 1..8, 0 = hold (no switch)
    std::atomic<int> oct { 0 };       // -1, 0, +1: the layers (and the note) an octave away
    std::atomic<int> mute { 0 };      // the bass rests on this step
};

struct Brain : geno::Brain
{
    std::array<StepBass, kMaxSteps> bass;
    neuro::Brain dummyNeuro;          // the bass synth's own walk, unused (external mode)
};

struct Params : geno::Params
{
    neuro::Params bassP;              // the bass synth (external mode is set by the engine)
    int bassRoot = 36;                // MIDI note of the line's semitone 0
    float bassLevel = 0.55f;
    int answerArts = 4;               // ANSWER plays articulation + this (mod 8)
    int fillArt = 0;                  // 0 = as the step, 1..8
    int fillBass = FillFollow;
    float pickupSemis = 12.0f;
    float snareGapMs = 25.0f;
    float duck = 0.5f;
    bool bassOn = true, drumsOn = true;
    int bassChannel = 2;
};

struct BassMove
{
    double pos = 0.0;                 // 16ths
    int step = -1, role = 0;
    BassCell cell;
    int art = -1;                     // 0..7: switch to it, -1: no switch
    int oct = 0;
    bool jump = false;                // the ball did not go to the step that comes next
};

struct BassViewEvent { int8_t step = -1, art = -1, role = 0, type = 0; uint8_t jump = 0; };

/** The bass part of one tick (after geno::playTick decided the step). */
BassMove bassFor (const Bundle& b, const Brain& brain, const Params& p, int64_t k, int step, int role, int prevStep, int& curArt);

/** The whole loop offline: drum notes and bass moves of `bars` bars (for the MIDI drag and the tests). */
struct OfflineWalk { std::vector<geno::Note> drums; std::vector<BassMove> bass; std::vector<int> steps; };
OfflineWalk walkOffline (const Bundle& b, Brain& brain, const Params& p, int bars, uint32_t seed);

class Engine
{
public:
    static constexpr int kVoices = 24;
    static constexpr int kQueue = 768;
    static constexpr int kMaxOut = 256;

    void prepare (double sampleRate, int maxBlock);
    void reset();

    void process (const Bundle* b, Brain& brain, const Params& p, const geno::MidiEvent* in, int numIn,
                  float* outL, float* outR, int n, geno::MidiEvent* midiOutBuf, int& numOut, uint8_t* outChannel);

    // control (any thread)
    std::atomic<int> resetRequest { 0 }, haltRequest { 0 }, jumpRequest { -1 }, auditionRequest { -1 };

    // views
    std::atomic<int> viewStep { -1 }, viewRole { 0 }, viewPlaying { 0 }, viewHalted { 0 }, viewArt { -1 }, viewJumps { 0 };
    std::atomic<double> viewBpm { 174.0 };
    std::atomic<int64_t> viewTick { 0 };
    bool popHit (geno::HitEvent& h);
    bool popMove (geno::MoveEvent& m);
    bool popBass (BassViewEvent& e);
    neuro::Engine bass;

private:
    struct Voice
    {
        bool active = false;
        int slice = 0, start = 0, len = 0, lane = 0;
        double pos = 0.0, inc = 1.0;
        float amp = 1.0f, env = 1.0f, decayMul = 1.0f;
        int fade = -1, fadeLen = 1, rampPos = 0, rampLen = 22;
        uint32_t age = 0;
    };
    enum Kind { KHit = 0, KMove, KBass, KGap, KMidiOff, KPad };
    struct Pending
    {
        double pos = 0.0;
        int64_t at = 0;
        bool timed = false;
        int kind = KHit;
        int hit = -1, lane = 0, step = 0, role = 0;
        uint8_t note = 0, vel = 0, ch = 1;
        float amp = 0.0f;
        BassMove bm;
        bool consumed = false;
    };

    void trigger (const dna::Kit& k, const Params& p, int slice, int lane, float amp, int step, int role, float offMs);
    void render (const dna::Kit& k, float* L, float* R, int from, int to, const Params& p);
    void push (const Pending& e) { if (numQueued < kQueue) queue[(size_t) numQueued++] = e; }
    template <typename T, size_t N> static void emit (std::array<T, N>& ring, std::atomic<int>& wr, std::atomic<int>& rd, const T& v);
    template <typename T, size_t N> static bool pop (std::array<T, N>& ring, std::atomic<int>& wr, std::atomic<int>& rd, T& v);

    double sr = 44100.0;
    int64_t clock = 0;
    double freePos = 0.0, lastPos = -1.0e9;
    int64_t nextTick = INT64_MIN;
    bool wasRunning = false;
    const Bundle* lastBundle = nullptr;
    walk::Walker walker;
    uint32_t seedSeen = 0;
    double lastMult = 1.0;
    bool pendingReset = false;
    int prevStep = -1, curArt = -1;
    int bassNote = -1;                // MIDI bass note sounding (out)
    std::array<bool, geno::NumLanes> lastChoke {};

    std::array<Voice, kVoices> voices {};
    uint32_t ageCounter = 0;
    std::array<Pending, kQueue> queue {};
    int numQueued = 0;
    std::array<neuro::ExtEvent, 256> ext {};
    std::vector<float> bassL, bassR;

    std::array<geno::HitEvent, 256> hits {};
    std::atomic<int> hitWrite { 0 }, hitRead { 0 };
    std::array<geno::MoveEvent, 256> moves {};
    std::atomic<int> moveWrite { 0 }, moveRead { 0 };
    std::array<BassViewEvent, 256> bassEv {};
    std::atomic<int> bassWrite { 0 }, bassRead { 0 };
};

} // namespace dnafam::lg
