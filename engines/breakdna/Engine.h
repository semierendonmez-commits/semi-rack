// semi-rack: vendored; namespaces wrapped in dnafam:: (see sources.mk for every change).
// The real-time part of BREAKDNA. Allocation free once prepared.
//
//   BREAK A : A's own hits play from the genome, on the host's timeline, with the
//             feel (micro-timing), dynamics and ghosts scaled; every hit sounds at
//             its step's morph between A and B.
//   MIDI IN : your drum notes get A's feel: each note is moved by the offset A has
//             on that step (its own hit, or A's feel for that class and position),
//             its velocity takes A's accent, A's ghost notes are added where you
//             left the step empty. The notes play the kit and go to MIDI out.
//
// The engine runs a fixed lookahead L (25 ms) and reports it as latency: early
// notes need it. In BREAK A mode the timeline is rendered L behind the host, so
// after the host's delay compensation everything lines up.
#pragma once

#include "Dna.h"
#include <atomic>

namespace dnafam::dna
{

enum Source { SourceBreak = 0, SourceMidi, NumSources };
enum Run { RunHost = 0, RunAlways, NumRuns };

struct Params
{
    int source = SourceBreak;
    float feel = 1.0f;          // 0 grid .. 1 A's timing .. 1.5 exaggerated
    float dyn = 1.0f;           // 0 flat .. 1 A's accents .. 1.5
    float ghosts = 1.0f;        // 0 none .. 1 A's .. 2 louder (MIDI: how many are added, then louder)
    float morph = 0.0f;         // 0 A .. 1 B
    float laneAmt = 1.0f;       // how much the morph lane adds
    int rotate = 0;             // steps
    float decay = 1.0f;         // 0 short .. 1 the whole slice
    float tune = 0.0f;          // semitones
    float gain = 1.0f;
    bool choke = true;
    int noteK = 36, noteS = 38, noteH = 42;
    bool midiOut = true;
    int run = RunHost;
    uint32_t seed = 1;
    // transport
    bool playing = false;
    double ppq = 0.0;           // at the block start
    double bpm = 120.0;
};

struct MidiEvent
{
    int offset = 0;             // sample in block
    uint8_t note = 0, vel = 0;  // vel 0 = note off
};

// What the displays get (audio -> editor)
struct HitEvent
{
    int16_t slice = -1, bslice = -1;
    int8_t step = -1;
    uint8_t cls = 0;
    uint8_t fromMidi = 0;
    uint8_t ghostAdded = 0;
    float amp = 0.0f, morph = 0.0f, offsetMs = 0.0f;
};

class Engine
{
public:
    static constexpr int kVoices = 24;
    static constexpr int kQueue = 512;
    static constexpr int kMaxOut = 256;

    void prepare (double sampleRate, int maxBlock);
    void reset();
    int latency() const { return lookahead; }

    /** Plays one block. kit may be null (silence). lane: per-step morph offsets, -1..1.
        Fills outL/outR (overwrites) and midiOut (count returned in numOut). */
    void process (const Kit* kit, const Params& p, const std::array<std::atomic<float>, kMaxSteps>& lane,
                  const MidiEvent* in, int numIn, float* outL, float* outR, int n,
                  MidiEvent* midiOutBuf, int& numOut);

    // views
    std::atomic<int> viewStep { -1 };
    std::atomic<int> viewPlaying { 0 };
    std::atomic<double> viewBpm { 120.0 };
    /** hits for the displays; drained by the editor */
    bool popHit (HitEvent& h);

    // for the tests: what the scheduler holds
    int queued() const { return numQueued; }

private:
    struct Voice
    {
        bool active = false;
        int slice = 0, start = 0, len = 0;
        double pos = 0.0, inc = 1.0;
        float amp = 1.0f, morph = 0.0f;
        float env = 1.0f, decayMul = 1.0f;
        int fade = -1, fadeLen = 1;
        int rampPos = 0, rampLen = 22;     // the half millisecond before the attack fades in
        uint32_t age = 0;
    };
    struct Pending
    {
        int64_t at = 0;             // absolute sample
        uint8_t note = 0, vel = 0;  // vel 0: off
        uint8_t cls = 0;
        int8_t step = -1;
        uint8_t ghost = 0;          // added by the DNA: dropped if a note of yours got the step
        int64_t absStep = 0;
        float offsetMs = 0.0f;
    };

    void trigger (const Kit& k, const Params& p, const std::array<std::atomic<float>, kMaxSteps>& lane,
                  int slice, int step, float amp, bool fromMidi, bool ghost, float offsetMs);
    void render (const Kit& k, float* L, float* R, int from, int to, const Params& p);
    void push (const Pending& e);
    int classOf (const Params& p, int note, int vel) const;
    void emitHit (const HitEvent& h);

    double sr = 44100.0;
    int lookahead = 1103;
    int64_t clock = 0;                      // samples since prepare
    double freePos = 0.0;                   // 16ths, when the host is stopped and RUN = ALWAYS
    bool wasPlaying = false;
    double lastPos = -1.0e9;                // last rendered timeline position (16ths) of BREAK A
    double lastMidiPos = -1.0e9;            // last scanned position for added ghosts
    const Kit* lastKit = nullptr;

    std::array<Voice, kVoices> voices {};
    uint32_t ageCounter = 0;
    std::array<Pending, kQueue> queue {};
    int numQueued = 0;
    std::array<int64_t, 128> noteShift {};  // delay given to each held note, for its note off
    std::array<int64_t, 64> stepTaken {};   // absolute step that a note of yours took (snare / ghost)
    int64_t lastTaken = -1000000;

    std::array<HitEvent, 256> hits {};
    std::atomic<int> hitWrite { 0 }, hitRead { 0 };
};

} // namespace dnafam::dna
