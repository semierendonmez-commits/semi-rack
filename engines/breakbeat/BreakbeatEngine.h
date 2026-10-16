// BREAKBEAT engine (SEMI Family port).
//
// Source: semierendonmez-commits/breakbeat Source/BreakbeatEngine.{h,cpp} — the plugin's real-time
// port of mestela/schwung-breakbeat's breakbeat.c (the slice decisions themselves are upstream's
// slice_select.c / the plugin's slice_select_n.c, compiled unchanged in SliceSelect.cpp).
// Changes for the Rack / MetaModule module (everything else is the plugin's code):
//   * JUCE removed: SampleData holds std::vectors, juce::jmax/jlimit -> std::max/clamp, the
//     retire FIFO is a small lock-free ring, the reseed on transport start uses a counter instead
//     of juce::Time; namespace bb -> bbeat.
//   * MIDI in -> `Pad` events (jump to a slice); MIDI out -> a per-block `Hit` list (the module
//     turns it into TRIG / slice CV / loop gates).
//   * New: cut modes. Besides the original slice decisions (MOVE), the trigger grid can be
//     replaced by one of four BBCut procedures (BBCut.h): CUTPROC11, WARPCUT, SQPUSHER1, CHOOSECUT.
//     Each cut plays the loop from its block's phrase position (or, by SCRAMBLE, from a random
//     eighth), so repeats inside a block stutter as in BBCut. The phrase sequencer (fills, loop B,
//     speed) runs on top in every mode.
//   * The phrase-sequencer sample library slots (2..63) are kept but the module only uses loop A
//     (slot 0) and loop B (slot 1).
#pragma once

#include "BBCut.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bbeat {
#include "upstream/slice_select_n.h"

inline constexpr int maxSteps = 32;
inline constexpr int numSlots = 64;       // 0 = loop A, 1 = loop B, 2.. = sample library
inline constexpr int firstLibrarySlot = 2;
inline constexpr int maxLibrary = numSlots - firstLibrarySlot;   // 62
inline constexpr int sampleRandom = 63;   // step sample value meaning "any library sample"

/** An immutable loop. Created off the audio thread, handed over by pointer. */
struct SampleData
{
    std::vector<float> l, r;   // r empty = mono
    int totalFrames = 0;
    int numChannels = 0;
    std::string name;
};

/** One phrase-sequencer step, packed so the audio thread reads it atomically. */
struct Step
{
    int fill = 0;      // 0..100  fill amount (0 = no fill)
    int bChance = 0;   // 0..100  chance this step plays loop B
    int prob = 100;    // 0..100  chance the step's events happen at all
    int speed = 0;     // 0 = global, 1 = x1/2, 2 = x1, 3 = x2
    int fx = 0;        // bitmask: FX lanes active on this step (unused in the module)
    int sampleA = 0;   // 0 = loop A's sample, 1..62 = library entry, 63 = random
    int sampleB = 0;   // 0 = loop B's sample, 1..62 = library entry, 63 = random

    uint64_t pack() const noexcept
    {
        return (uint64_t) (fill & 127) | ((uint64_t) (bChance & 127) << 7) | ((uint64_t) (prob & 127) << 14)
             | ((uint64_t) (speed & 3) << 21) | ((uint64_t) (fx & 15) << 23)
             | ((uint64_t) (sampleA & 63) << 27) | ((uint64_t) (sampleB & 63) << 33) | (1ull << 40);
    }
    static Step unpack (uint64_t v) noexcept
    {
        Step s;
        if ((v >> 40) == 0) return s;   // never written: defaults
        s.fill = (int) (v & 127); s.bChance = (int) ((v >> 7) & 127); s.prob = (int) ((v >> 14) & 127);
        s.speed = (int) ((v >> 21) & 3); s.fx = (int) ((v >> 23) & 15);
        s.sampleA = (int) ((v >> 27) & 63); s.sampleB = (int) ((v >> 33) & 63);
        return s;
    }
};

inline float speedFactor (int index) noexcept  // 1 = x1/2, 2 = x1, 3 = x2
{
    return index == 1 ? 0.5f : (index == 3 ? 2.0f : 1.0f);
}

enum CutMode : int { CutMove = 0, CutProc11, CutWarp, CutSQPusher, CutChoose, NumCutModes };
const char* cutModeName (int m);   // "MOVE" "CUTPROC11" "WARPCUT" "SQPUSHER1" "CHOOSECUT"

struct EngineParams
{
    float complexity = 0.5f, anchor = 0.0f, roll = 0.0f;
    float intensity = 1.0f;     // scales every step's fill (0..2)
    float chaos = 0.0f;         // per-pass variation of the pattern (0..1)
    float gate = 1.0f;          // fraction of each hit that plays at full level
    float decay = 0.0f;         // fade after the gate, as a fraction of a hit
    int   lockBars = 0;         // 0 = off, else loop the last N bars of slice decisions
    int   seqSteps = 4;         // 1..32
    float stepBars = 1.0f;      // 1, 0.5, 0.25
    int   globalSpeed = 2;      // 1 = x1/2, 2 = x1, 3 = x2
    bool  tapeSpeed = true;     // speed also shifts pitch (tape) vs keeps it (chop)
    std::array<float, 4> retrig { 0, 0, 0, 0 };
    bool  declick = true;
    bool  internalRun = false;
    float internalBpm = 170.0f;
    // SEMI port: cut modes
    int   cutMode = CutMove;
    float scramble = 0.5f;      // BBCut modes: chance a block plays from a random eighth instead of its place
    bbcut::Params bbcut;
};

struct TransportInfo
{
    bool   hostPlaying = false;
    bool   hasPpq = false;
    double ppq = 0.0;
    double bpm = 0.0;
};

/** A pad: jump to a slice at this sample (the plugin's MIDI notes 36..). */
struct Pad { int sample = 0; int slice = 0; };

/** Every hit of the last block (the plugin's MIDI out). */
struct Hit { int sample = 0; int slice = 0; int loop = 0; int vel = 100; };

/** A cut as the screen draws it: start (song beats), length, where in the loop it reads. */
struct CutRecord { double ppq = 0.0; float beats = 0.f; float offset = 0.f; uint8_t roll = 0, loop = 0, fill = 0; };

class BreakbeatEngine
{
public:
    BreakbeatEngine();
    ~BreakbeatEngine();

    void prepare (double sampleRate, int maxBlock);
    void submitSample (int slot, std::unique_ptr<SampleData> data, bool previewIfStopped);
    void clearSlot (int slot);
    /** Musical length and slice count belong to each sample (slot). Any thread. */
    void setSlotGrid (int slot, float bars, int slices) noexcept
    {
        if (slot < 0 || slot >= numSlots) return;
        slotBars[(size_t) slot].store (bars);
        slotSlices[(size_t) slot].store (slices >= 32 ? 32 : (slices >= 16 ? 16 : 8));
    }
    /** Any non-audio thread: frees samples the audio thread has swapped out. */
    void collectGarbage();
    void setStep (int index, const Step& s) noexcept { steps[(size_t) index].store (s.pack()); }
    Step getStep (int index) const noexcept { return Step::unpack (steps[(size_t) index].load()); }
    void requestPreview() noexcept { previewRequest.store (true); }
    void setFixedSeed (uint32_t seed) noexcept { fixedSeed = seed; }

    /** Audio thread. pads sorted by sample. */
    void process (float* outL, float* outR, int numSamples,
                  const EngineParams& p, const TransportInfo& t,
                  const Pad* pads, int numPads);

    // ---- results of the last process() call (audio thread)
    static constexpr int maxHits = 64;
    int numHits() const noexcept { return hitCount; }
    const Hit& hit (int i) const noexcept { return hits[(size_t) i]; }
    // the recent cuts (ring), newest at cutWrite - 1
    static constexpr int cutHistory = 96;
    std::array<CutRecord, cutHistory> cutLog {};
    int cutWrite = 0;

    struct Status
    {
        std::atomic<int>   slice { 0 }, numSlices { 8 }, loop { 0 }, slot { 0 };
        std::atomic<int64_t> phraseCount { 0 };
        std::atomic<int>   retrigDiv { 1 }, step { 0 }, numSteps { 4 }, speed { 2 };
        std::atomic<float> fill { 0.0f };
        std::atomic<bool>  running { false }, previewing { false }, internalClock { false }, locked { false };
        std::atomic<float> playhead { 0.0f }, bpm { 120.0f }, loopBars { 1.0f };
        std::atomic<double> ppq { 0.0 };
    };
    Status status;

private:
    std::array<std::atomic<uint64_t>, maxSteps> steps {};
    std::array<std::atomic<float>, numSlots> slotBars {};
    std::array<std::atomic<int>, numSlots> slotSlices {};

    std::atomic<SampleData*> incoming[numSlots] {};
    std::atomic<bool> incomingPreview[numSlots] {};
    std::atomic<bool> clearRequest[numSlots] {};
    SampleData* active[numSlots] {};
    // retire ring: audio thread writes, collectGarbage() frees
    static constexpr int retireCapacity = 64;
    std::array<std::atomic<SampleData*>, retireCapacity> retireBuf {};
    std::atomic<int> retireHead { 0 };   // audio thread
    int retireTail = 0;                  // collector
    void retire (SampleData* s) noexcept;

    EngineParams P;
    double sampleRate = 44100.0;
    uint32_t rngState = 0x9e3779b9u;
    uint32_t fixedSeed = 0;
    uint32_t reseedCount = 0;
    float stableBpm = 120.0f;

    int    currentLoop = 0;
    int    curSlot = 0;
    int    segSpeed = 2;
    double segStartPpq = 0.0;
    int64_t nextTrig = 1;
    double trigPpq = 0.5;
    int    segSlices = 8;
    float  segLength = 1.0f;

    int64_t curStepAbs = 0;
    bool   stepIsFill = false;
    float  stepFill = 0.0f;
    unsigned stepFx = 0;
    static constexpr int lockRes = 192;
    static constexpr int lockSpan = 64 * lockRes;
    struct Decision { int8_t slice = -1; int8_t div = 0; };
    std::array<Decision, lockSpan> decisions {};
    int lockBarsActive = 0;
    double lockStartPpq = 0.0;
    static int lockKey (double ppq) noexcept
    {
        auto k = (long long) std::llround (ppq * (double) lockRes) % lockSpan;
        return (int) (k < 0 ? k + lockSpan : k);
    }
    double envPos = 0.0, hitLen = 1.0;

    int    currentSlice = 0;
    bool   subSliceActive = false;
    int    subSliceCounter = 0, retriggerDivisions = 2;
    double playPos = 0.0;
    int    previewFrames = 0;

    bool   wasRunning = false;
    double expectedPpq = 0.0;
    bool   internalActive = false;
    double internalPpq = 0.0;
    float  lastBars = -1.0f;
    int    lastSlices = -1;
    std::atomic<bool> previewRequest { false };

    const SampleData* tailSample = nullptr;
    double tailPos = 0.0, tailRate = 1.0;
    int tailRemaining = 0, fadeInRemaining = 0, fadeLength = 96;

    // hits of this block
    std::array<Hit, maxHits> hits {};
    int hitCount = 0;
    int eventSample = 0;

    // SEMI port: BBCut schedule
    bbcut::Proc proc;
    bbcut::Block blk;
    int    cutMode = CutMove;
    int    cutIdx = 0;              // next cut inside blk
    double nextCutPpq = 0.0;        // song position of the next cut
    double curCutPpq = 0.0;         // start of the cut playing
    double curCutSrcBeat = 0.0;     // where in the loop it reads (beats)
    double blockSrcBeat = 0.0;      // the block's source position

    uint32_t randomU32() noexcept;
    static float randCallback (void* ctx);
    const SampleData* current() const noexcept;
    float  barsOf (int slot) const noexcept { const float b = slotBars[(size_t) slot].load(); return b > 0.0f ? b : 1.0f; }
    int    slicesOf (int slot) const noexcept { const int n = slotSlices[(size_t) slot].load(); return n > 0 ? n : 8; }
    int    resolveSample (int loop, int value) noexcept;
    double stepPpq() const noexcept { return 4.0 * (double) P.stepBars; }
    int    effectiveSpeed (const Step& s) const noexcept { return s.speed == 0 ? P.globalSpeed : s.speed; }
    double samplesPerTrigger (bool withSpeed) const noexcept;
    double playbackRate() const noexcept;

    void  beginSegment (double startPpq) noexcept;
    void  jumpTo (double newPos) noexcept;
    void  swapIncoming() noexcept;
    void  applyGridChanges (double ppqNow) noexcept;
    void  resetTransport() noexcept;
    void  resync (double ppq) noexcept;
    bool  handleStep (int64_t stepAbs, double atPpq, bool consumeRandom) noexcept;
    void  fireTrigger (double atPpq) noexcept;
    void  applyRetrig (int div) noexcept;
    void  handleNote (int slice) noexcept;
    void  startPreview() noexcept;
    void  renderSpan (float* L, float* R, int from, int to) noexcept;
    void  emitNote (int vel) noexcept;
    void  restartEnvelope (double lengthSamples) noexcept { envPos = 0.0; hitLen = std::max (1.0, lengthSamples); }
    void  publishStatus (bool running) noexcept;
    void  logCut (double ppq, double beats, double srcBeat, bool roll) noexcept;
    // SEMI port: cut modes
    void  cutResync (double ppq) noexcept;
    void  nextBlock() noexcept;
    void  fireCut (double atPpq) noexcept;
    void  resumeCut (double atPpq) noexcept;
};

} // namespace bbeat
