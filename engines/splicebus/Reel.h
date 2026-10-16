// REEL, lifted out of FLOATER's master and made a bus effect (after the Make Noise Morphagene,
// not a model of its firmware).
//
// The reel records what it is given (the band above SPLIT) all the time and cuts it into SPLICES at
// the bar lines: the last 1, 2, 4 or 8 bars, one splice per bar. Out of it it plays GENES: short
// pieces, as long as a clock division, read at a VARISPEED (backwards, stopped, forwards),
// overlapping into pitched layers as MORPH goes up.
//
// What decides the genes (the clock, CHAOS, the turns, the budget, ANCHOR) lives in the engine; the
// reel only plays what it is told, when it is told. What keeps the hits clean (PROTECT) arrives as a
// per-sample guard: 1 = this sample is the input, untouched.
#pragma once

#include "Dsp.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace spb
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

    struct Settings
    {
        int length = 1;           // index into reelLengthNames()
        float morph = 0.45f;      // < 0.35 gaps between genes, then seamless, > 0.6 overlapping layers
        bool hold = false;        // stop recording: the splices stay as they are
        float wetHp = 0.0f;       // the reel's own output high-passed here (Hz, 0 = off): a pitched-down gene stays above SPLIT
    };
    struct Gene
    {
        bool broken = false;
        bool recent = false;      // a roll: read the last piece that went by, not a splice of the window
        bool live = false;        // a tape stop: read what is being recorded, slowing down (rate -> rateEnd)
        int splice = 0;           // 0..splices-1
        float offset = 0.0f;      // where in the splice, 0..1
        float rate = 1.0f;        // signed, with VARISPEED and any octave already in
        float rateEnd = 1.0f;     // the rate at the gene's end (a tape stop ramps to 0)
        int sub = 1;              // 1 = the gene, 2..8 = it stutters its first 1/sub that many times
        int length = 0;           // samples
    };

    Reel();
    void prepare (double sampleRate);
    void reset();
    void set (const Settings& s) { settings = s; }
    const Settings& get() const { return settings; }

    // timed events, in the engine's timeline (absolute samples, already moved by the latency)
    void barLine (int64_t at) { push ({ at, Event::bar, {} }); }
    void gene (int64_t at, const Gene& g) { push ({ at, Event::gene, g }); }
    void wall (int64_t at, bool on) { push ({ at, on ? Event::wallOn : Event::wallOff, {} }); }
    void discardAll() { queued = 0; }
    int pending() const { return queued; }

    // in place: l / r carry the band the reel takes and come back with what it gives.
    // guard: per sample 0..1, how much of the input must come through untouched (nullptr = none)
    void process (float* l, float* r, const float* guard, int n, int64_t now);

    // what the editor reads
    struct Display
    {
        std::atomic<int64_t> rec { 0 }, winStart { 0 };
        std::atomic<int> spliceLen { 0 }, splices { 0 }, genes { 0 }, broken { 0 }, peakCap { 1 };
        std::atomic<bool> valid { false }, held { false }, breaking { false }, walled { false };
        std::array<std::atomic<float>, kGenes> voicePos {};     // in the window, 0..1; < 0 = silent
        std::atomic<float> mix { 0.0f };                         // how much of the band is the reel now
        std::vector<float> peaks;                                // one per kPeak tape samples (read without a lock)
    } display;

private:
    struct Event { int64_t at = 0; enum Kind { bar, gene, wallOn, wallOff } kind = bar; Gene g {}; };
    struct Voice
    {
        bool on = false;
        int64_t base = 0;         // the splice's first tape sample
        int len = 1;              // the splice's length
        double rel = 0.0, rel0 = 0.0, rate = 1.0, rateStep = 0.0;
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
    float tape (int channel, double recIndex) const;

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
    std::array<Event, 128> queue {};
    int queued = 0;
    float dryGain = 1.0f, dryTarget = 1.0f, dryStep = 0.01f, wallGain = 1.0f, wallTarget = 1.0f, peakAcc = 0.0f;
    int peakFill = 0, genesStarted = 0, genesBroken = 0;
    dsp::Svf hpA, hpB, hpAR, hpBR;
    float hpHz = -1.0f;
};

} // namespace spb
