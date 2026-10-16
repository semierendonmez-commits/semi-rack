// BLACKBOX, cable edition: the flight recorder for the BUS cable and CV / gate lanes.
//
// The DAW plug-in (semierendonmez-commits/blackbox) records what happens on the shared-memory buses as
// 32-byte records in a growing log and exports Standard MIDI Files with a fixed note / CC map
// (docs/FORMATS.md). In Rack / MetaModule the family talks over cables, so this tape records the four
// BUS channels and four CV + four gate lanes instead, into a FIXED ring buffer (bounded memory, the
// oldest records roll off), and keeps BLACKBOX's MIDI map where it applies:
//
//   lane          what is recorded                        MIDI export
//   POS           value (deadband)                        tempo of the region (bars per second)
//   ENERGY        value                                   CC 20 (EPOCH slot 0 = ENERGY)
//   SCENE         value (steps)                           program change + conductor marker
//   EVENT         pulse with its height (turn line)       note 48 + line (C2.. like FIRE), a 16th
//   CV 1-4        value                                   CC 70 + n (or the pitch of GATE n)
//   GATE 1-4      on (velocity = volts) / off             note 36 + n (C1 + lane, like LANE) or V/OCT pitch
//   MARK          the MARK button                         conductor marker
//
// Audio thread: Recorder::sample() and Player. Any other thread: copyOut(), buildView(), exportMidi().
// No exceptions, no iostreams, no allocation after construction on the audio side.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bbxcv
{

enum Lane : uint8_t
{
    LPos = 0, LEnergy, LScene, LEvent,
    LCv1, LCv2, LCv3, LCv4,
    LGate1, LGate2, LGate3, LGate4,
    kLanes,                         // 12 recorded lanes
    LMark = kLanes                  // marks (not a signal)
};

inline bool isValueLane (int l) { return l == LPos || l == LEnergy || l == LScene || (l >= LCv1 && l <= LCv4); }
inline bool isGateLane (int l) { return l >= LGate1 && l <= LGate4; }

enum Kind : uint8_t { KNone = 0, KValue, KOn, KOff, KPulse, KMark, KKey };

// 8 bytes. t: tape time in ms. v: volts * kScale (or a mark number).
struct Rec
{
    uint32_t t = 0;
    uint8_t lane = 0;
    uint8_t kind = KNone;
    int16_t v = 0;
};
static_assert (sizeof (Rec) == 8, "Rec layout");

constexpr float kScale = 3200.0f;                 // +-10.24 V
inline int16_t enc (float volts)
{
    float x = volts * kScale;
    x = x > 32767.0f ? 32767.0f : (x < -32767.0f ? -32767.0f : x);
    return (int16_t) (x >= 0.0f ? x + 0.5f : x - 0.5f);
}
inline float dec (int16_t v) { return (float) v / kScale; }

//==============================================================================
// The ring. One writer (the audio thread). `written` counts every record ever pushed; the valid
// logical range is [written - capacity, written).
class Tape
{
public:
    explicit Tape (size_t capacity);

    void push (const Rec& r)
    {
        const uint64_t w = written.load (std::memory_order_relaxed);
        buf[(size_t) (w % cap)] = r;
        written.store (w + 1, std::memory_order_release);
    }
    void clear() { written.store (0, std::memory_order_release); cleared.fetch_add (1, std::memory_order_acq_rel); }

    size_t capacity() const { return cap; }
    uint64_t end() const { return written.load (std::memory_order_acquire); }
    uint64_t begin() const { const uint64_t w = end(); return w > cap ? w - cap : 0; }
    const Rec& at (uint64_t logical) const { return buf[(size_t) (logical % cap)]; }
    uint32_t generation() const { return cleared.load (std::memory_order_acquire); }

    // first logical index in [lo, hi) with t >= time (records are in time order)
    uint64_t lowerBound (uint64_t lo, uint64_t hi, uint32_t time) const;

    // Any thread: a consistent copy of the newest records (at most `maxRecords`). Records the writer
    // may be overwriting during the copy are left out. Returns false if nothing could be copied.
    bool copyOut (std::vector<Rec>& out, size_t maxRecords = (size_t) -1) const;

private:
    std::vector<Rec> buf;
    size_t cap;
    std::atomic<uint64_t> written { 0 };
    std::atomic<uint32_t> cleared { 0 };
};

//==============================================================================
// The lanes' state at a moment (what a recall puts on the outputs).
struct LaneState
{
    float value[kLanes] {};         // value lanes: volts; gate lanes: the gate's volts while on
    bool on[kLanes] {};             // gate lanes
    bool known[kLanes] {};
};

// The state at logical index `idx` (i.e. after every record before it): scans back to the last key group.
void stateBefore (const Tape& tape, uint64_t idx, LaneState& st, int keyEvery);

//==============================================================================
struct RecorderSettings
{
    float deadband = 0.005f;        // volts: a value lane records when it moved this much
    float rateMs = 4.0f;            // and at most this often (jumps > 0.5 V always go at once)
    int keyEvery = 1024;            // a key group (every lane's state) after this many records
};

class Recorder
{
public:
    explicit Recorder (Tape& t) : tape (t) {}

    RecorderSettings settings;

    // Audio thread, once per sample. volts: the 12 lanes. armed: false pauses (tape time stops).
    void sample (const float* volts, bool armed, float sampleTime);
    void mark();                    // audio thread
    void reset();                   // audio thread (after Tape::clear)

    uint32_t now() const { return tapeMs; }
    int marks() const { return markCount; }

private:
    void put (uint32_t t, int lane, int kind, int16_t v);
    void keyGroup (uint32_t t);

    Tape& tape;
    double msAcc = 0.0;
    uint32_t tapeMs = 0;
    bool started = false;
    bool wasArmed = false;
    float lastVal[kLanes] {};
    uint32_t lastT[kLanes] {};
    bool haveVal[kLanes] {};
    float tickVal[kLanes] {};
    bool gate[kLanes] {};
    float gateV[kLanes] {};
    bool pulseHigh = false;
    int sinceKey = 0;
    int markCount = 0;
    bool markReq = false;
};

//==============================================================================
// Replays the tape from a time. Audio thread.
class Player
{
public:
    explicit Player (const Tape& t) : tape (t) {}

    void start (uint32_t fromMs, int keyEvery);
    void stop() { playing = false; }
    bool isPlaying() const { return playing; }

    // Advance one sample; false when the tape ran out (or reached `untilMs`).
    bool sample (float sampleTime, uint32_t untilMs);

    float out (int lane) const;     // the lane's voltage now (EVENT: the pulse)
    double position() const { return nowMs; }

    LaneState state;

private:
    const Tape& tape;
    bool playing = false;
    double nowMs = 0.0;
    uint64_t idx = 0;
    uint32_t gen = 0;
    float pulseV = 0.0f, pulseLeft = 0.0f;
    float gateMin[kLanes] {};       // seconds a gate must still stay high (>= 1 ms)
    bool offPending[kLanes] {};
};

//==============================================================================
// A decimated picture of a window of the tape, for the timeline screen.
constexpr int kViewCols = 96;

struct View
{
    uint32_t t0 = 0, t1 = 0;
    float energy[kViewCols] {};     // volts
    int8_t scene[kViewCols] {};     // -1 unknown
    uint8_t event[kViewCols] {};    // 0, or the turn line's index + 1 (1..4)
    uint8_t bar[kViewCols] {};      // 1 a bar line, 2 an 8-bar line
    uint8_t mark[kViewCols] {};
    float cv[4][kViewCols] {};
    uint8_t gates[kViewCols] {};    // bit n: GATE n was on in this column
    bool any = false;
};

void buildView (const Tape& tape, uint32_t t0, uint32_t t1, int keyEvery, View& v);

//==============================================================================
struct MidiOptions
{
    uint32_t t0 = 0, t1 = 0xffffffffu;  // tape time
    bool pitched[4] { false, false, false, false };  // GATE n plays CV n as V/OCT (0 V = C4)
    bool cvBipolar = false;             // CC map: +-5 V instead of 0..10 V
    int division = 960;
};

struct MidiResult
{
    std::vector<uint8_t> bytes;
    double bpm = 120.0;
    bool tempoFromBus = false;
    int tracks = 0, notes = 0, ccs = 0;
};

// recs: a copy of the tape in time order (Tape::copyOut).
MidiResult exportMidi (const std::vector<Rec>& recs, const MidiOptions& opt);

} // namespace bbxcv
