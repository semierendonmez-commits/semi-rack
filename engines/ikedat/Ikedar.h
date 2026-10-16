// Vendored from semierendonmez-commits/ikedat Source/engine/Ikedar.h (JUCE-free). Semi-rack change: wrapped in namespace ikt (one binary, ~35 engines).
// IKEDAT — the IKEDAR half: C++ port of the Max/MSP 4.6 patch "ryoz_ikedar_ln.mxt"
// (unchanged from the IKEDAR plug-in), plus a per-sample API so the hybrid
// engine can clock it from the Pinball / master clock and seed it with data.
//
// The engine has no JUCE dependency so it can be tested offline.
// Every behaviour below is traced back to an object chain in the original
// patch; see docs/PORTING_NOTES.md for the full mapping.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ikt {

namespace ryoz
{
constexpr int kNumVoices   = 9;
constexpr int kCells       = 64;   // [p alvotonN] inlet 1  : 64-slider multiSlider (CA row / pattern / table / mask)
constexpr int kPitchBits   = 50;   // inlet 0  : 50 sliders, popcount -> frequency
constexpr int kGainBits    = 50;   // inlet 2  : 50 sliders, popcount -> amplitude
constexpr int kDecayBits   = 25;   // inlet 3  : 25 sliders, popcount -> gate length
constexpr int kMutateBits  = 25;   // inlet 4  : 25 sliders, any change -> one CA generation
constexpr int kMasterBits  = 167;  // [p mstrclk] : 167 sliders, popcount -> DAC on / clock on / BPM
constexpr int kMasterWords = 3;

// [ubumenu] contents inside every voice: the rule pool for random(0,10).
constexpr int kRulePool[10] = { 13, 14, 15, 17, 18, 22, 27, 28, 30, 54 };

enum class Osc { Wavetable, WavetableSpectral, Sine };
enum class AmpCurve { Squared, Linear3 };   // pow(n/25,2)  or  n/50*3

struct VoiceSpec
{
    Osc osc;
    AmpCurve curve;
    float outGain;   // the [*~ 0.5] / [*~ 1] after the envelope
    bool toLeft, toRight;
    int savedRule;   // rule left in [p 10to2in8bit]'s message box when the patch was saved
    const char* tag;
};

extern const VoiceSpec kVoiceSpecs[kNumVoices];

//==============================================================================
// State shared between UI, host state and audio thread. Everything is atomic,
// bit edits use fetch_or / fetch_and so no lock is ever taken.
struct VoiceState
{
    std::atomic<std::uint64_t> pitch { 0 }, cells { 0 }, gain { 0 }, decay { 0 };
    std::atomic<std::uint32_t> mutateRequests { 0 };

    // written by the engine, persisted by the processor
    std::atomic<std::uint64_t> signs { 0 };        // random +/- per cell of the wavetable
    std::atomic<int> rule { 0 };                   // rule that the NEXT generation will use
    std::atomic<float> gateBaseMs { 0.0f };        // "bpmms" captured when DECAY was last edited

    // telemetry for the editor
    std::atomic<int> lastRule { 0 };
    std::atomic<std::uint32_t> generation { 0 };
    std::atomic<float> envLevel { 0.0f };
};

struct SharedState
{
    std::array<VoiceState, kNumVoices> voices;
    std::array<std::atomic<std::uint64_t>, kMasterWords> master {};

    std::atomic<std::uint32_t> loadSerial { 0 };  // bumped after a full state load

    // telemetry
    std::atomic<int> step { -1 };
    std::atomic<float> bpm { 0.0f };
    std::atomic<bool> clockRunning { false };
    std::atomic<bool> dacOn { false };
    std::atomic<float> peak { 0.0f };

    static void setBit (std::atomic<std::uint64_t>& w, int i, bool v) noexcept
    {
        const auto m = std::uint64_t (1) << i;
        if (v) w.fetch_or (m, std::memory_order_relaxed);
        else   w.fetch_and (~m, std::memory_order_relaxed);
    }
    static bool getBit (const std::atomic<std::uint64_t>& w, int i) noexcept
    {
        return ((w.load (std::memory_order_relaxed) >> i) & 1u) != 0;
    }
    bool getMasterBit (int i) const noexcept { return getBit (master[(std::size_t) (i / 64)], i % 64); }
    void setMasterBit (int i, bool v) noexcept { setBit (master[(std::size_t) (i / 64)], i % 64, v); }
    int masterSum() const noexcept;
};

//==============================================================================
// line~ fed with "$1 0.1 $1 $2 0 1"
struct LineEnvelope
{
    void start (double amp, double gateMs, double sampleRate) noexcept;
    inline float tick() noexcept
    {
        if (seg < numSegs)
        {
            value += inc;
            if (--remaining <= 0.0) { value = segs[seg].target; ++seg; beginSegment(); }
        }
        return (float) value;
    }
    void reset() noexcept { value = 0.0; seg = numSegs = 0; }
    double value = 0.0;

private:
    struct Seg { double target, samples; };
    Seg segs[3] {};
    int numSegs = 0, seg = 0;
    double remaining = 0.0, inc = 0.0;
    void beginSegment() noexcept;
};

// pfft~ fltrN 128 2  :  fftin~ 1 -> (real,imag) * index~ fltrN[bin] -> fftout~ 1
class SpectralMask
{
public:
    static constexpr int N = 128, Hop = 64;
    SpectralMask();
    void reset() noexcept;
    void setMask (std::uint64_t bits) noexcept { mask = bits; }
    float process (float x) noexcept;

private:
    void frame() noexcept;
    std::uint64_t mask = 0;
    float window[N] {};
    float inHist[N] {}, inNew[Hop] {}, outCur[Hop] {}, ola[N] {};
    double re[N] {}, im[N] {};
    double cosT[N / 2] {}, sinT[N / 2] {};
    int bitrev[N] {};
    int pos = 0;
    void fft (bool inverse) noexcept;
};

//==============================================================================
class Engine
{
public:
    struct HostInfo
    {
        bool valid = false, playing = false;
        double bpm = 120.0, ppq = 0.0;
    };

    // Controls added around the patch. Defaults reproduce the patch exactly.
    struct Controls
    {
        bool run = true;            // metro allowed to run
        bool hostSync = false;      // steps from the host transport
        double divBeats = 0.25;     // host step length in quarter notes (1/16)
        double swing = 0.12;        // [expr $f1*12*0.01]: odd steps late by this fraction
        bool clip = true;           // dac~ clips at full scale
        bool hold = false;          // freeze the automata (mutations are ignored)
        int autoEvery = 0;          // auto-mutate one voice every N steps (0 = off)
        float autoChance = 0.5f;    // chance that an auto-mutate actually fires
        std::array<float, kNumVoices> voiceGain {};   // per-voice gain (group levels)
        Controls() { voiceGain.fill (1.0f); }
    };

    // Data hook (hybrid): when a voice mutates, with `seedChance` its new row is
    // read from the data instead of the automaton; with `signs` the wavetable
    // signs come from the data too. what: 0 = row, 1 = signs.
    struct DataHook
    {
        void* ctx = nullptr;
        std::uint64_t (*bits) (void* ctx, int voice, int what) = nullptr;
        float seedChance = 0.0f;
        bool signs = false;
    };
    void setDataHook (const DataHook& h) noexcept { hook = h; }

    explicit Engine (SharedState& s);
    void prepare (double sampleRate);
    void process (float* left, float* right, int numSamples, const HostInfo& host, const Controls& c) noexcept;
    void restartClock() noexcept { restartPending = true; }   // next step is step 1, immediately
    void setLocked (int v, bool l) noexcept { if (v >= 0 && v < kNumVoices) locked[(std::size_t) v] = l; }

    // Pure CA step exactly as [p cellam] does it, exposed for tests.
    struct CarryState { int c1 = 0, c2 = 0; };   // [bucket 2] contents: b[-1], b[-2]
    static std::uint64_t caGeneration (std::uint64_t row, int rule, CarryState& carry) noexcept;

    // ---- per-sample API used by the hybrid engine -----------------------
    void beginBlock (const Controls& c) noexcept;          // controls + poll the shared rows
    bool internalClockSample() noexcept;                   // the patch's own metro, one sample
    void hybridStep (double gateMs) noexcept;              // advance the pattern on an outside clock
    void fireVoices (int first, int count, float vel, double gateMs) noexcept;  // trig from Pinball
    void renderSample (float& l, float& r, const Controls& c) noexcept;
    void endBlock() noexcept;
    void mutateVoice (int v) noexcept { if (v >= 0 && v < kNumVoices && ! hold) mutate (v); }
    void autoMutateOne() noexcept;                         // one unlocked voice
    int currentStep() const noexcept { return step; }
    bool patchDacOn() const noexcept { return dacOn; }
    double patchStepMs() const noexcept { return bpmms; }

    static double pitchToHz (int n) noexcept;     // pow(n/50,4)*30000+20
    static double ampScale (int voice, int n) noexcept;
    static double stepMsForMasterSum (int sum) noexcept;  // 60000/(sum*2)*0.25

private:
    struct Voice
    {
        std::uint64_t lastPitch = ~0ull, lastCells = ~0ull, lastGain = ~0ull, lastDecay = ~0ull;
        std::uint32_t seenMutations = 0;
        double hz = 20.0, amp = 0.0, gateFrac = 1.0;
        double phase = 0.0;
        float table[kCells] {};
        LineEnvelope env;
        SpectralMask spectral;
        CarryState carry;
    };

    SharedState& shared;
    std::array<Voice, kNumVoices> voices;
    double sr = 44100.0;
    std::uint64_t rng = 0x9E3779B97F4A7C15ull;
    std::uint32_t seenLoadSerial = ~0u;

    // [p mstrclk]
    std::uint64_t lastMaster[kMasterWords] { ~0ull, ~0ull, ~0ull };
    bool dacOn = false, metroOn = false;
    double bpmms = 0.0;               // [s bpmms]
    double metroPeriod = 0.0, metroCountdown = 0.0;
    int swingToggle = 0;              // [counter 0 0 1]
    bool swingPending = false;        // [del]
    double swingCountdown = 0.0, swingDelay = 0.0;
    int step = -1;                    // [counter 0 0 63] -> [s alvclock]

    double lastHostQ = -1.0e9;
    bool restartPending = false;
    bool hold = false;
    int autoEvery = 0, autoCounter = 0;
    float autoChance = 0.5f;
    double swingFrac = 0.12;
    std::array<bool, kNumVoices> locked {};
    DataHook hook;
    float blockPeak = 0.0f;

    std::uint32_t nextRandom() noexcept;
    void pollState() noexcept;
    void onMasterChanged (int sum) noexcept;
    void regenerateTable (int v, std::uint64_t cells, bool newSigns) noexcept;
    void mutate (int v) noexcept;
    void advanceStep() noexcept;
    void onStep() noexcept;
    void triggerStep (int stepIndex, double hostStepMs, bool hostSync) noexcept;
    inline float renderVoice (int v) noexcept;
};
} // namespace ryoz

} // namespace ikt
