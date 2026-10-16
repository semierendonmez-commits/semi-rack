// REEL: a tape on the master, after the Make Noise Morphagene (not a model of its firmware).
//
// The reel records the case's master all the time (only the lanes that are not PROTECTed) and cuts
// it into SPLICES at the bar lines: the last 1, 2, 4 or 8 bars, one splice per bar. Out of it it
// plays GENES: short pieces, as long as a clock division, read at a VARISPEED (backwards, stopped,
// forwards), overlapping into pitched layers as MORPH goes up, as the Morphagene does.
//
// The point is order and breakage at the same time:
//   in frequency  below SPLIT the master is never touched; the reel only replaces what is above it
//   per lane      PROTECTed lanes never go into the reel at all
//   in time       genes start only on the grid; whether a gene breaks is decided by the clock's seeded
//                 random (so with LOOP the breakage itself repeats); ANCHOR keeps the first beat of every
//                 4-bar line clean; each kick (DUCK BY) pulls the master back to the dry for a moment
// ORDER <-> CHAOS is one macro over all of it: at 0 the reel is a wire, at 1 every gene breaks and the
// breaks reverse, jump splices, change octave and stutter.
//
// The engine decides the genes (Engine::reelGene) and tells the reel when; the reel plays them.
#pragma once

#include "Dsp.h"
#include "Voices.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace flt
{

inline const std::array<const char*, 6>& geneNames()
{
    static const std::array<const char*, 6> n { "1/32", "1/16", "1/8", "1/4", "1/2", "1 BAR" };
    return n;
}
inline double geneSixteenths (int i)
{
    static const double v[6] { 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };
    return v[i < 0 ? 0 : (i > 5 ? 5 : i)];
}
inline const std::array<const char*, 4>& reelLengthNames()
{
    static const std::array<const char*, 4> n { "1 BAR", "2 BARS", "4 BARS", "8 BARS" };
    return n;
}
inline int reelLengthBars (int i) { return 1 << (i < 0 ? 0 : (i > 3 ? 3 : i)); }

// VARISPEED 0..1 -> playback rate: 0.5 stops, 0.75 is the speed it was recorded at, the ends twice as fast
inline float varispeedRate (float v01)
{
    const float v = dsp::clamp (v01, 0.0f, 1.0f) * 2.0f - 1.0f;
    const float a = std::abs (v);
    const float mag = a < 0.5f ? a * 2.0f : std::pow (2.0f, (a - 0.5f) * 2.0f);
    return v < 0.0f ? -mag : mag;
}

class Reel
{
public:
    static constexpr int kGenes = 8;
    static constexpr int kPeak = 256;            // tape samples per display column
    static constexpr int kMaxSplices = 8;

    struct Settings
    {
        float chaos = 0.0f;       // ORDER <-> CHAOS
        int length = 1;           // index into reelLengthNames()
        float varispeed = 0.75f;
        float morph = 0.45f;      // < 0.35 gaps between genes, then seamless, > 0.6 overlapping layers
        float split = 0.0f;       // 0 = off, else the crossover 40..800 Hz
        float duck = 0.5f;
        bool hold = false;        // stop recording: the splices stay as they are
    };
    struct Gene
    {
        bool broken = false;
        int splice = 0;           // 0..splices-1
        float offset = 0.0f;      // where in the splice, 0..1
        float rate = 1.0f;        // signed, with VARISPEED and any octave already in
        int sub = 1;              // 1 = the gene, 2..4 = it stutters its first 1/sub that many times
        int length = 0;           // samples
    };

    Reel();
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void set (const Settings& s) { settings = s; }
    const Settings& get() const { return settings; }

    // timed events, in the engine's timeline (absolute samples)
    void barLine (int64_t at) { push ({ at, Event::bar, {} }); }
    void gene (int64_t at, const Gene& g) { push ({ at, Event::gene, g }); }
    void duck (int64_t at) { push ({ at, Event::duck, {} }); }
    // the reel is not running (a stem render): forget what was due, so nothing piles up
    void discardUntil (int64_t t);

    // in place: l / r carry what the reel takes and come back with what it gives
    void process (float* l, float* r, int n, int64_t now);

    // the tape, for the REEL voice in a module slot
    bool hasWindow() const { return windowValid; }
    int64_t windowStart() const { return winStart; }
    int spliceLength() const { return spliceLen; }
    int splices() const { return numSplices; }
    float tape (int channel, double recIndex) const;

    // what the editor reads
    struct Display
    {
        std::atomic<int64_t> rec { 0 }, winStart { 0 };
        std::atomic<int> spliceLen { 0 }, splices { 0 }, genes { 0 }, broken { 0 }, peakCap { 1 };
        std::atomic<bool> valid { false }, held { false }, breaking { false };
        std::array<std::atomic<float>, kGenes> voicePos {};     // in the window, 0..1; < 0 = silent
        std::atomic<float> mix { 0.0f };                         // how much of the master is the reel now
        std::vector<float> peaks;                                // one per kPeak tape samples (read without a lock)
    } display;

private:
    struct Event { int64_t at = 0; enum Kind { bar, gene, duck } kind = bar; Gene g {}; };
    struct Voice
    {
        bool on = false;
        int64_t base = 0;         // the splice's first tape sample
        int len = 1;              // the splice's length
        double rel = 0.0, rel0 = 0.0, rate = 1.0;
        int age = 0, segLen = 1, winLen = 1, fade = 1, life = 1;
        float gain = 1.0f;
        bool sub = false;
    };
    void push (const Event& e) { if (queued < (int) queue.size()) queue[(size_t) queued++] = e; }
    void handle (const Event& e);
    void startGene (const Gene& g);
    void updateWindow();
    float voiceTick (Voice& v, float& outR);
    void record (float l, float r);

    Settings settings;
    double sr = 48000.0;
    std::vector<float> tapeL, tapeR;
    int64_t cap = 1, rec = 0;
    std::array<int64_t, 16> bars {};
    int64_t barCount = 0;
    bool windowValid = false;
    int64_t winStart = 0;
    int spliceLen = 0, numSplices = 0;
    std::array<Voice, kGenes> voices {};
    std::array<Event, 64> queue {};
    int queued = 0;
    float dryGain = 1.0f, dryTarget = 1.0f, dryStep = 0.01f, duckEnv = 0.0f, duckK = 0.999f, peakAcc = 0.0f;
    int peakFill = 0, genesStarted = 0, genesBroken = 0;
    // the SPLIT, a Linkwitz-Riley crossover: x's low band under the reel's high band
    dsp::Svf loA, loB, loAR, loBR, wetA, wetB, wetAR, wetBR;
    float splitHz = -1.0f;
};

// A module slot playing the reel: self-sampling. COLOR picks where in the reel (on a sixteenth grid),
// HARM how long a slice (1/32 .. a bar), TUNE transposes it in semitones, DECAY how long it keeps
// looping its slice while it fades, MORPH a low pass (left) or high pass (right).
class ReelVoice final : public VoiceBase
{
public:
    explicit ReelVoice (const Reel& r) : reel (r) {}
    void prepare (float sampleRate) override { VoiceBase::prepare (sampleRate); env = 0.0f; }
    void trigger (const Strike& s) override;
    void render (float* out, int n) override;
    bool active() const override { return env > 1.0e-4f; }
    float position() const;                      // where it reads in the reel window, 0..1, < 0 = silent
private:
    void release() override { envK = dsp::decayCoef (0.3f, sr); }
    const Reel& reel;
    int64_t base = 0, ws = 0, wlen = 0;          // the slice, and the window it was cut from
    int sliceLen = 1, age = 0, segOut = 1;
    double rel = 0.0, rate = 1.0;
    float env = 0.0f, envK = 0.999f, level = 1.0f;
    int filterMode = 0;                          // 0 none, 1 low pass, 2 high pass
    dsp::Svf filt;
};

} // namespace flt
