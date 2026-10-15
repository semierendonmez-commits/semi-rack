// The audio engine. It sits on a drum bus, follows the host's bar position and
// rewrites the last beats before each phrase boundary with material taken from
// the break that is playing through it.
//
//   capture : every bar is recorded; each 1/16 slot is measured (low / mid / high
//             energy) and named K(ick) S(nare) G(host) H(at)
//   plan    : the edit budget of the current 32-bar block (Phrase.h)
//   render  : GHOST / ROLL / RETRIG / REVERSE / TAPE STOP / CUT, then the wall
//
// Real time: everything is allocated in prepare(); process() does no allocation,
// takes no locks. Telemetry for the editor goes out through atomics.
#pragma once

#include "Phrase.h"
#include <atomic>
#include <vector>

namespace tna
{

constexpr int kSlots = 16;                // 1/16 slots of a bar
constexpr int kPeaks = 256;               // waveform bins the editor draws

enum SlotClass : int { Empty = -1, Kick = 0, Snare = 1, Ghost = 2, Hat = 3 };

struct Transport
{
    bool playing = false;
    double ppq = 0.0;
    double bpm = 170.0;
};

// MIDI the engine wants to send (CC lanes, boundary notes); the processor turns
// these into a MidiBuffer.
struct MidiEvent { int sampleOffset; int cc; int value; bool isNote; };

struct Telemetry
{
    std::atomic<int> relBar { 0 }, block { 0 }, event { 0 }, tier { 0 }, action { 0 };
    std::atomic<float> barInBlock { 0.0f };     // 0..32, fractional
    std::atomic<float> progress { 0.0f };       // 0..1 inside the running action, -1 = none
    std::atomic<bool> inGap { false }, playing { false };
    std::array<std::atomic<float>, kEvents> alloc {}, desire {}, amount {};
    std::atomic<float> budgetBeats { 0.0f }, spent { 0.0f };
    // the bar the edits draw from
    std::array<std::atomic<float>, kPeaks> peaks {};
    std::array<std::atomic<int>, kSlots> cls {};
    std::array<std::atomic<float>, kSlots> level {};
    std::atomic<int> rollSlot { -1 }, rollSlot2 { -1 }, hitCount { 0 }, barsSeen { 0 };
};

class Engine
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void setSettings (const Settings& s) { set = s; }
    const Settings& settings() const { return set; }

    // in place, stereo. `midi` may be null.
    void process (float* L, float* R, int n, const Transport& tp, std::vector<MidiEvent>* midi = nullptr);

    Telemetry tele;

    // ---- exposed for the tests
    const Plan& currentPlan() const { return plan; }
    int lastBarClass (int slot) const { return lastCls[(size_t) slot]; }

private:
    struct Bar
    {
        std::vector<float> l, r;
        int length = 0;              // samples written
        double spb = 0.0;            // samples per beat when it was recorded
        std::array<double, kSlots> low {}, mid {}, high {}, tot {}, pre {};
        std::array<int, kSlots> count {}, preCount {};
        void clearStats() { low.fill (0); mid.fill (0); high.fill (0); tot.fill (0); pre.fill (0); count.fill (0); preCount.fill (0); length = 0; }
    };

    void startBar (std::int64_t absBar, double spb);
    void finishBar();
    void beginEvent (const GridPos& g);
    int pickSlot (SlotClass wanted, int exclude) const;
    float readSnap (const std::vector<float>& buf, double pos) const;
    float readHist (const std::vector<float>& buf, double pos) const;
    void publishPlan();

    Settings set;
    double sr = 48000.0;

    // capture
    Bar bars[2];
    int cur = 0;                      // bars[cur] is being written, bars[1-cur] is the last full one
    std::int64_t curAbsBar = INT64_MIN;
    std::array<int, kSlots> lastCls {};
    std::array<float, kSlots> lastLevel {}, lastScoreK {}, lastScoreS {};
    float lpLow = 0, lpMid1 = 0, lpMid2 = 0, lpHigh = 0;

    // continuous history for TAPE STOP / REVERSE
    std::vector<float> histL, histR;
    std::int64_t histWrite = 0;

    // the bar a running event draws from (copied once when the event starts)
    std::vector<float> snapL, snapR;
    int snapLen = 0;
    double snapSpb = 0.0;
    std::array<int, kSlots> snapCls {};
    std::array<float, kSlots> snapLevel {};

    // event state
    Plan plan;
    std::int64_t planBlock = INT64_MIN;
    std::int64_t evBoundary = INT64_MIN;
    bool evStarted = false;
    int rollSlot = -1, rollSlot2 = -1, kickSlot = -1;
    double rollPhase = 0.0, rollDiv = 0.5, hitAge = 0.0, hit2Age = -1.0;
    bool layerFired = false, actOn = false;
    std::int64_t tapeStart = 0;
    double tapeRead = 0.0;
    int retrigStep = -1, retrigSlot = -1;
    double retrigAge = 0.0;
    int ghostStep = -1;
    bool ghostOn = false;
    double ghostAge = 0.0;
    std::uint64_t evSeed = 0;
    float wet = 0.0f;                 // smoothed: 0 = the dry break, 1 = replaced
    std::array<int, kTiers + 1> lastCC { -1, -1, -1, -1, -1 };
    float spentBeats = 0.0f;
};

} // namespace tna
