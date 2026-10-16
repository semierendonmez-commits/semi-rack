// SEMI Family port: vendored from semierendonmez-commits/breakslicer Source/engine (namespace gtbs).
// One change here: padRouting is seeded explicitly instead of from std::random_device.
//
// The real-time core: tracker pattern, row generator, 09xx / Rxy / drill, routing
// into the five DSP chains, voices with equal-power crossfades, the sampler's
// playback path and output filters, the reverb send.
//
// Real-time rules (section 3.1): process() never allocates, never locks, and only
// reads what prepare() sized. Everything it plays comes from an immutable Kit.
#pragma once

#include "Dsp.h"
#include "Kit.h"
#include "Reverb.h"
#include "Routing.h"
#include "TripleBuffer.h"

#include <array>
#include <atomic>
#include <random>

namespace gtbs
{

struct GenParams
{
    float complexity = 0.35f;   // chance a row is recut from the Markov chain
    float anchor = 0.6f;        // how strongly the beats keep the break's own hit
    float density = 0.5f;       // 0.5 = the break's own hits; less removes, more adds
    float shuffle = 0.15f;      // 0: recuts take the nearest slice of the class, 1: any
    float barFeel = 0.25f;      // how much the bar's grammar (kick on 1, snare on 2 / 4) bends the Markov choice
    float retrig = 0.12f;       // chance of an Rxy retrigger
    float drill = 0.05f;        // chance of a drill (sub-tick retrigger, eq. 2.3)
    float fill = 0.5f;          // fill strength
    int fillEvery = 4;          // bars, 0 = off
    float mutate = 0.0f;        // chance per row and pass to play a variation (the pattern itself stays)
    int retrigVol = 0;          // 0 any, RvDecay, RvBoost, RvExp
    int retrigAmt = 4;          // x of Rxy, 1..8
    int drillY = 6;             // y of the drill, sub-ticks 1..15
    float sweep = 0.0f;         // semitones over a retrigger's repeats (eq. 2.4)
    float human = 0.2f;         // volume variation
    std::array<float, NumChains> weights { 0.55f, 0.25f, 0.08f, 0.06f, 0.06f };
    MarkovChain markov;
    int pitchSemis = 7;
    bool pitchBoth = true;      // PitchShift goes up or down
    int rows = 32;              // pattern length
    int speed = 6;              // ticks per row: 3 = 1/32, 6 = 1/16, 12 = 1/8
    bool lock = false;          // the pattern stops following the controls
    uint32_t seed = 1;
    bool snap = true;           // 09xx offsets snap to the nearest onset
    uint32_t userKey = 0;       // hash of the controls as set (without LFOs); 0 = use the values above
};

struct PlayParams
{
    float swing = 0.0f;
    float gate = 1.0f;          // 1 = slices play on (tracker), less = each hit stops after part of its slice
    float decayMs = 40.0f;
    float xfadeMs = 2.0f;       // equal-power crossfade at every slice change (eq. 3.1)
    float level = 1.0f;
    float revSize = 0.6f, revDamp = 0.35f, revReturn = 0.6f;
    int fit = 0;                // 0 chop (slices at their own speed), 1 tape (follows the tempo)
    float mfilter = 0.8f;       // S950 output filter, fraction of the clock's Nyquist
    int amigaModel = 0;         // 0 A500 (4.4 kHz RC), 1 A1200 (none)
    bool led = false;           // Amiga LED filter (2-pole, 3.1 kHz)
    bool run = false;           // internal clock when the host is stopped
    double internalBpm = 170.0;
};

struct Transport
{
    bool valid = false, playing = false;
    double ppq = 0.0, bpm = 120.0;
};

struct HitEvent
{
    int offset = 0, row = -1, slice = 0;
    uint8_t cls = 0, chain = 0;
    float vol = 1.0f;
    bool repeat = false, pad = false, natural = false;
    uint8_t variant = 0;
};

struct PadEvent { int offset = 0, slice = 0; float velocity = 1.0f; bool on = true; };

struct Edit
{
    enum Type : uint8_t { SetCell, Pin, Unpin, Regenerate, Clear, RegenerateAll, UnpinAll };
    Type type = SetCell;
    int row = 0;
    Cell cell {};
};

class Engine
{
public:
    Engine();

    void prepare (double sampleRate, int maxBlock);
    void reset();

    /** audio thread, block start: the kit to play from (may change between blocks) */
    void setKit (const Kit* k) noexcept;
    const Kit* currentKit() const noexcept { return kit; }

    /** audio thread: a pattern edit from the editor (arrives through the SPSC FIFO) */
    void applyEdit (const Edit& e) noexcept;

    /** audio thread. pads are sorted by offset. */
    void process (float* outL, float* outR, int n, const Transport& t, const GenParams& gp, const PlayParams& pp,
                  const PadEvent* pads, int numPads) noexcept;

    // ---- results of the last block (audio thread reads them right after process)
    int numHits() const noexcept { return hitCount; }
    const HitEvent& hit (int i) const noexcept { return hits[(size_t) i]; }

    // ---- editor views ---------------------------------------------------------
    TripleBuffer<PatternView> patternOut;
    std::atomic<int> viewRow { -1 }, viewPass { 0 }, viewPlaying { 0 }, viewVariant { 0 };   // 0 base, 1 variation, 2 fill
    static constexpr int kViews = 4;
    std::array<std::atomic<float>, kViews> viewHead {};    // playheads, source frames (-1 none)
    std::array<std::atomic<float>, kViews> viewGain {};
    std::atomic<int> hitCounter { 0 }, lastHitSlice { -1 }, lastHitChain { 0 };
    std::atomic<float> peakL { 0 }, peakR { 0 };

    /** which row (pattern index) a global row index lands on, and the fill bars */
    static bool isFillBar (int64_t g, int speed, int fillEvery) noexcept;

    /** generator exposed for tests: fills one cell for global row g */
    void generateCell (Cell& out, int64_t g, int r, const GenParams& gp, uint32_t salt, int prevCls, bool withFill = false) const noexcept;
    int naturalSlice (int64_t g, const GenParams& gp, int* currentOut = nullptr) const noexcept;
    double rowStartPpq (int64_t g, double rowLen, float swing) const noexcept;

private:
    struct Voice
    {
        bool active = false;
        const MachineBuffer* src = nullptr;   // kept alive by the processor until voices are done with it
        double pos = 0.0, inc = 1.0;
        double lo = 0.0, hi = 0.0;      // readable range (machine frames)
        float vol = 1.0f, send = 0.0f;
        float fadeIn = 1.0f, fadeInStep = 0.0f;
        bool out = false; float fadeOut = 0.0f, fadeOutStep = 0.0f;
        int64_t holdLeft = -1; float env = 1.0f, envStep = 0.0f;
        int view = 0; float srcScale = 1.0f; // machine -> source frames for the display
    };

    void regenerateAll (const GenParams& gp, bool force) noexcept;
    void publishPattern() noexcept;
    void handleRow (int64_t g, double startPpq, double nextStartPpq, int offset, const GenParams& gp, const PlayParams& pp) noexcept;
    void handleRepeat (int offset, const PlayParams& pp) noexcept;
    void startVoice (int slice, int off09, uint8_t chain, float vol, double pitchRatio, bool snap,
                     const PlayParams& pp, int offset, double minFadeSamples) noexcept;
    void releaseAll (double fadeSamples) noexcept;
    void render (float* L, float* R, int from, int to, const PlayParams& pp) noexcept;
    inline void readVoice (Voice& v, float& l, float& r) const noexcept;
    void emitHit (int offset, int row, int slice, uint8_t cls, uint8_t chain, float vol, bool repeat, bool pad, bool natural = false, uint8_t variant = 0) noexcept;
    uint32_t keyFor (const GenParams& gp) const noexcept;
    void updateOutputFilters (const PlayParams& pp) noexcept;

    double fs = 44100.0;
    const Kit* kit = nullptr;
    uint32_t sliceSig = 0;

    // pattern
    std::array<Cell, kMaxRows> cells {};
    std::array<uint8_t, kMaxRows> pinned {}, generated {};
    std::array<uint32_t, kMaxRows> salt {}, rowKey {};
    std::array<int64_t, kMaxRows> passSeen {};
    int rows = 32, speed = 6;
    uint32_t key = 0, modKey = 0;
    bool locked = false;
    int patternSerial = 0;
    bool patternDirty = true;

    // timing
    bool playing = false;
    double expectedPpq = 0.0, internalPpq = 0.0, ppqPerSample = 0.0, blockPpq = 0.0, bpm = 120.0;
    int64_t nextRow = 0;
    double nextRowPpq = 0.0;
    int64_t curRowG = -1;
    double curRowStart = 0.0, curRowEnd = 0.0;
    int prevCls = Kick;

    // retrigger state (eq. 2.3 - 2.5)
    bool repActive = false;
    double repNextPpq = 0.0, repIntervalPpq = 0.0;
    int repK = 0, repKMax = 0;
    Cell repCell {};
    float repV0 = 1.0f, repDp = 0.0f, repGamma = 1.0f, repDv = 0.0f;
    int64_t repIntervalSamples = 0;

    // voices + output
    static constexpr int kVoices = 24;
    std::array<Voice, kVoices> voices {};
    int voiceRound = 0;
    SendReverb reverb;
    Butterworth<6> s950L, s950R;
    OnePole a500L, a500R;
    Biquad ledL, ledR;
    int filterMachine = -1; double filterRate = 0.0; float filterAmt = -1.0f; int filterModel = -1;
    float levelSm = 1.0f, returnSm = 0.6f;
    std::array<float, 8 * 64> sincTable {};   // S1000 8-point interpolation, 64 phases

    ProbabilisticRoutingEngine padRouting { 0x5eedu };   // SEMI port: seeded (no std::random_device on MetaModule)
    std::mt19937 padRng { 0x5eed };

    std::array<HitEvent, 512> hits {};
    int hitCount = 0;
};

} // namespace gtbs
