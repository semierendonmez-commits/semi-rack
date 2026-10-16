// Vendored into SemiFamily from semierendonmez-commits/quanta Source/engine. Changes (marked
// "SemiFamily"): kMaxParticles 64 and shorter fx buffers on MetaModule; setSlotHz() added (V/OCT
// moves a held voice without restarting it); the sample is a plain shared_ptr (set on the audio
// thread only; the Rack module hands it over through semi::Slot) instead of atomic_load/store.
// QUANTA - particle engine (no JUCE).
//
// Five particle types after Curtis Roads, "Microsound" (2001):
//   GRAIN     windowed waveform fragment (phase-coherent, so overlapping
//             Hann grains at 50% sum to a steady tone)
//   GLISSON   grain whose pitch glides during its life
//   GRAINLET  grain whose duration is tied to its frequency (N cycles)
//   TRAINLET  band-limited impulse train as one particle; chroma = tilt
//   PULSAR    pulsaret (duty x period) + silence; period = 1 / density,
//             formant = pitch. Masking per pulsar (stochastic + burst).
// An Emitter spawns particles of one Scene at a pitch; MIDI voices and the
// arrangement's lanes are both emitters. Output chain ends in fixed safety:
// 18 Hz high-pass (DC/subsonic), 18 kHz ceiling, hard limiter at -0.3 dBFS.
#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace qn
{
constexpr double kPi = 3.141592653589793;
constexpr double kPitchCeilHz = 18000.0;   // no particle pitch above this
enum PType { Grain = 0, Glisson, Grainlet, Trainlet, Pulsar, kNumTypes };
enum Wave { Sine = 0, Saw, Square, Noise, Sample, kNumWaves };
enum Win { Gauss = 0, Hann, Expodec, Rexpodec, Trapez, Rect, kNumWins };
enum GlideDir { Up = 0, Down, RandomDir };

constexpr int kNumSync = 10;
// 0 = free, then 1/4 1/8 1/16 1/32 1/64 1/4T 1/8T 1/16T 1/32T (in beats)
inline double syncBeats (int s)
{
    static const double b[kNumSync] = { 0, 1.0, 0.5, 0.25, 0.125, 0.0625, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 1.0 / 12.0 };
    return b[s < 0 ? 0 : (s >= kNumSync ? kNumSync - 1 : s)];
}
inline double midiHz (double n) { return 440.0 * std::exp2 ((n - 69.0) / 12.0); }

struct Scene
{
    int type = Pulsar;
    float density = 40.0f;      // particles / s (free)
    int sync = 0;               // 0 free, else syncBeats index
    float durMs = 25.0f;
    float fineCents = 0.0f;
    int wave = Sine;
    int window = Gauss;
    float duty = 25.0f;         // % (pulsar)
    float mask = 0.0f;          // % stochastic masking
    uint16_t burst = 0xffff;    // 16-step burst pattern, bit i = step i on
    float glide = 0.0f;         // semitones (glisson)
    int glideDir = Up;
    float cycles = 8.0f;        // grainlet
    float impulses = 8.0f;      // trainlet
    float chroma = 50.0f;       // %
    float jitTime = 0.0f;       // %
    float jitPitch = 0.0f;      // semitones
    float jitAmp = 0.0f;        // %
    float spread = 50.0f;       // %
    float samplePos = 0.0f;     // %
};

struct Env
{
    float a = 5, d = 200, s = 0.8f, r = 300;   // ms, ms, 0..1, ms
};

struct SampleData
{
    std::vector<float> data;
    double sr = 48000.0;
    double rootHz = 261.6255653; // C4 plays at original speed
};

// Transition / performance effects on the master (before the safety chain)
enum FxMode { FxNone = 0, FxRepeat, FxReverse, FxTape, FxHpf, FxLpf, FxCrush, FxThrow, FxGate, kNumFx };

// UI read-out: one entry per spawned particle
struct SpawnEvent { float tSec, hz, amp, durMs; int type; };

class Engine
{
public:
#ifdef METAMODULE
    static constexpr int kMaxParticles = 64;    // SemiFamily: Cortex-A7 budget (~140 instr. per particle-sample)
#else
    static constexpr int kMaxParticles = 1024;
#endif
    static constexpr int kMaxEmitters = 16;   // 8 MIDI voices + 8 lanes
    static constexpr int kWinSize = 1024;

    Engine();
    void prepare (double sampleRate);
    void reset();

    // ---- emitters ----------------------------------------------------------
    // MIDI voices use slots 0..7 with the shared synth scene; lanes 8..15.
    void noteOn (int slot, double hz, float velocity);
    void noteOff (int slot);
    void killSlot (int slot);                         // stop now, fade particles
    void setSlotHz (int slot, double hz) { em[(size_t) slot].hz = hz; }   // SemiFamily: new particles only
    void setLane (int lane, const Scene*, double hz, bool on, bool hardCut);
    void setLaneGain (int lane, float g) { em[(size_t) (8 + lane)].gainTgt = g; }   // smoothed, applies to sounding particles too
    bool slotActive (int slot) const { return em[(size_t) slot].stage != Off; }
    double slotAge (int slot) const { return em[(size_t) slot].age; }

    struct Block
    {
        const Scene* synth = nullptr;
        Env env;
        double bpm = 120.0, ppq = 0.0;
        double sampleRate = 0.0;         // filled by the engine's owner for the player
        // transition / performance effect for this call
        int fxMode = FxNone;
        float fxP0 = 0.0f, fxP1 = 0.0f;  // progress at the first / after the last sample (0..1)
        int fxLen = 0;                   // REPEAT slice / REVERSE length in samples
        bool fxTrigger = false;          // a new effect starts at the first sample
        float outDb = -6.0f;
    };
    void process (float* L, float* R, int n, const Block&);

    // SemiFamily: audio thread only (was atomic_store / atomic_load)
    void setSample (std::shared_ptr<const SampleData> s) { sample = std::move (s); }
    const std::shared_ptr<const SampleData>& getSample() const { return sample; }

    // read-outs
    static constexpr int kEvents = 2048;
    std::array<SpawnEvent, kEvents> events {};
    std::atomic<int> eventW { 0 };
    std::atomic<double> clockSec { 0.0 };
    std::atomic<int> activeParticles { 0 };
    std::atomic<float> limiterGr { 0.0f }, outPeak { 0.0f };

    // helpers, public for tests
    static float window (int type, float x);          // x in [0, 1]
    static double particleDurSec (const Scene&, double hz, double periodSec);
    static double emitPeriodSec (const Scene&, double bpm);

private:
    enum Stage { Off = 0, Attack, Decay, Sustain, Release };
    struct Emitter
    {
        Stage stage = Off;
        const Scene* scene = nullptr;   // null = use block synth scene
        bool lane = false;
        double hz = 440.0;
        float vel = 1.0f, env = 0.0f;
        double nextFree = 0.0;          // samples until next free-running emission
        double lastGrid = -1.0;         // last grid index emitted (sync)
        int pendingDelay = -1;          // jittered sync emission waiting
        int burstStep = 0;
        double age = 0.0;
        uint32_t gen = 0;               // bumps on hard cut / kill
        float gainTgt = 1.0f, gainSm = 1.0f;
    };
    struct Particle
    {
        bool on = false;
        uint8_t type = 0, wave = 0, win = 0;
        int16_t owner = -1;
        uint32_t gen = 0;
        int age = 0, len = 1, fade = -1;
        double phase = 0, inc = 0, incMul = 1, incMax = 0.375;
        float amp = 0, gl = 0, gr = 0;
        float a = 0.5f, norm = 1; int K = 1;    // trainlet
        double spos = 0, sinc = 0;              // sample
        double aK = 0, aK1 = 0;
    };

    void spawn (int slot, const Scene&, double bpm);
    float renderParticle (Particle&, const SampleData*);
    void advanceEnv (Emitter&, const Env&);
    uint32_t rnd() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
    float rnd01() { return (float) (rnd() >> 8) * (1.0f / 16777216.0f); }
    float rndBi() { return rnd01() * 2.0f - 1.0f; }

    double sr = 48000.0;
    uint64_t sampleClock = 0;
    std::array<Emitter, kMaxEmitters> em {};
    std::array<Particle, kMaxParticles> parts {};
    int partCursor = 0;
    uint32_t rs = 0x9e3779b9u;
    static float winTable[kNumWins][kWinSize + 1];
    static float sineTable[4097];
    static bool tablesReady;
public:
    static void buildTables();
    static bool tablesReadyFlag() { return tablesReady; }
private:
    std::shared_ptr<const SampleData> sample;

    // safety chain
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        inline double tick (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    };
    Biquad hp[2][2], lp[2][4];
    float limGain = 1.0f;
    // transition fx
    std::vector<float> ringL, ringR, dlyL, dlyR;
    int ringW = 0, dlyW = 0;
    int curFx = FxNone, repStart = 0, repIdx = 0, repLen = 1;
    double tapePos = 0.0;
    float fxMixSm = 0.0f, throwSm = 0.0f, crushHoldL = 0, crushHoldR = 0; int crushCount = 0;
    struct Svf { float ic1 = 0, ic2 = 0; };
    Svf svf[2];
    float dlyLp[2] {};
    void fxSample (float& l, float& r, float prog, const Block& b);
    float outGainSm = 0.5f;
    std::vector<float> accL, accR;
};
} // namespace qn
