// The effects after the modules:
//   ZigZag  ping-pong delay whose read head is broken by the clock: on a break it
//           loops a short slice of what it holds, forward then backward (the Mungo d0
//           fed a gate burst on its clock input); or rolls it, reverses it, stops it
//           (TURNAROUND's moves); tuned to a pitch it is the d0's Karplus-Strong setting
//   Scatter granular cloud over the case's mix, fired by lane 8 (Clouds on its trigger input)
//   Space   eight-line feedback delay network reverb; can jump to a new program on the clock
//           (the RT-60 program change from a divider output)
//   (the bus is Drum Buss from schwung-busdriver, see Schwung.h)
#pragma once

#include "Dsp.h"
#include <array>
#include <vector>

namespace flt
{

// A vintage sampler in the delay's path, after BREAKSLICER's VintageSamplerEmulation:
// a sample clock (zero-order hold), the converter's bits, the machine's output filter.
//   S950   12-bit companded, 7.5..48 kHz, the MF6 anti-alias filter tracking the clock
//   S1000  16-bit at 44.1 kHz, an 8-point interpolator's gentle top
//   PAULA  8-bit, 3.5..28.8 kHz, the A500's fixed 4.4 kHz output filter
class Machine
{
public:
    void prepare (float sampleRate) { sr = sampleRate; reset(); }
    void reset() { phase = 1.0; holdL = holdR = 0.0f; for (auto* f : { &fL1, &fR1, &fL2, &fR2 }) f->reset(); last = -1; lastRate = -1.0f; }
    void set (int machine, float rate01);
    void process (float& l, float& r);
    int machine() const { return mode; }
private:
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void lowpass (float fs, float fc, float q = 0.7071f);
        void reset() { z1 = z2 = 0.0f; }
        float tick (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    };
    float sr = 48000.0f, clockHz = 27500.0f, lastRate = -1.0f;
    int mode = 0, last = -1;
    double phase = 1.0;
    float holdL = 0.0f, holdR = 0.0f;
    Biquad fL1, fR1, fL2, fR2;
};

class ZigZag
{
public:
    enum Style { zigzag = 0, roll, reverse, tapeStop };
    void prepare (float sampleRate);
    void reset();
    void setTime (float samples) { timeTarget = samples; }
    void setFeedback (float f) { feedback = f; }
    void setStyle (int s) { style = s; }
    // pitched: the delay is a Karplus-Strong string (no ping-pong, a brighter loop, feedback to 0.995)
    void setPitched (bool p) { pitched = p; }
    void setMachine (int m, float rate01) { machine.set (m, rate01); }
    // a break: sliceLen samples taken from what the delay holds, played for duration samples in the style
    void breakNow (int sliceLen, int duration);
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);
    bool breaking() const { return breakLeft > 0; }
    int breakStyle() const { return style; }
private:
    float read (const std::vector<float>& b, float pos) const;
    void breakRead (float& l, float& r);
    std::vector<float> bufL, bufR;
    int w = 0, len = 0;
    float sr = 48000, time = 12000, timeTarget = 12000, timeK = 0.999f, timeKFast = 0.99f, feedback = 0.4f;
    dsp::OnePole toneL, toneR, lowL, lowR, ksL, ksR;
    int breakLeft = 0, breakTotal = 1, sliceLen = 0, sliceStart = 0, k = 0, style = 0;
    float breakMix = 0.0f, breakMixK = 0.0f;
    double rollAge = 0.0, rollLen = 1.0, tapeRead = 0.0;
    bool backwards = false, pitched = false;
    Machine machine;
};

// SMR: six resonant band passes, the odd ones to the left, the even ones to the right, as the
// 4ms Spectral Multiband Resonator. Their frequencies come from a scale; ROTATE moves all six
// one scale step up (Devine clocks it from a Circadian Rhythms channel); GLIDE is how they slide there.
class Smr
{
public:
    static constexpr int kBands = 6;
    void prepare (float sampleRate);
    void reset();
    void setFrequencies (const std::array<float, kBands>& hz) { target = hz; }
    void set (float q01, float glide01);
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);
    std::array<float, kBands> levels {};      // per band, for the display
    std::array<float, kBands> current {};     // the gliding frequencies
private:
    struct Band { dsp::Svf a, b; };
    std::array<Band, kBands> bands {};
    std::array<float, kBands> target {};
    float sr = 48000.0f, q = 30.0f, glideK = 0.99f;
    int counter = 0;
};

class Scatter
{
public:
    void prepare (float sampleRate);
    void reset();
    // a cloud: size 0..1 (grain length), density 0..1 (how many), pitch 0..1 (how often not unison), spread 0..1 (stereo)
    void fire (float size, float density, float pitch, float spread, float position, float texture, dsp::Rng& rng);
    void process (const float* inL, const float* inR, float* outL, float* outR, int n, float feed);
    int activeGrains() const { int c = 0; for (auto& g : grains) if (g.left > 0) ++c; return c; }
    void setFrozen (bool on) { frozen = on; }           // the buffer stops recording: the grains replay what it holds
private:
    struct Grain { int wait = 0, left = 0, length = 1; float pos = 0, step = 1, panL = 0.7f, panR = 0.7f, shape = 1.0f; };
    std::vector<float> bufL, bufR;
    std::array<Grain, 48> grains {};
    int w = 0, len = 0;
    bool frozen = false;
    float sr = 48000;
};

class Space
{
public:
    void prepare (float sampleRate);
    void reset();
    void set (float size, float decay, float damp);     // 0..1 each
    void setInfinite (bool on) { infinite = on; }       // the tail held for ever, nothing new comes in
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);
private:
    static constexpr int kLines = 8, kLen = 1 << 16;
    std::array<std::vector<float>, kLines> lines;
    std::array<float, kLines> z {};
    std::array<double, kLines> modPh {};
    std::vector<float> pre;
    int w = 0, pw = 0, preLen = 0;
    float sr = 48000;
    dsp::Smooth size, decay, damp;
    bool infinite = false;
    float inGain = 1.0f;
};


} // namespace flt
