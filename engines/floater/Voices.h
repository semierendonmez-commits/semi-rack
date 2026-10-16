// The seven modules of the case. Each is triggered by its lane (or a MIDI note);
// the CV patched to it by the pin matrix is read when it is struck.
//   KICK     sine kick with a fast pitch sweep and drive (a BD9 / BD808 kind of voice)
//   MESH     two drum-head tones and a band-passed noise (a mutant snare)
//   FM       two-operator FM pluck, index falling faster than the amplitude (Basimilus Iteritas)
//   OPTO     folded oscillator through a vactrol low pass gate (Optomix / 292)
//   LANTERN  six detuned squares and white noise through a high band, the hats
//   ZAP      sine falling from far above its pitch in a few milliseconds (Cwejman S1 zaps)
//   STRIKE   three plucked strings tuned to a chord of a scale, strummed (Rings in its strum mode)
#pragma once

#include "Dsp.h"
#include "Types.h"
#include <array>
#include <vector>

namespace flt
{

// What a voice is struck with: the knobs, plus the CV from the matrix (bipolar, about -1..1).
struct Strike
{
    float vel = 1.0f;
    VoiceParams p {};
    float modA = 0.0f, modB = 0.0f;   // the voice's two matrix destinations (see Engine::fire)
    // what those destinations move on a Plaits engine
    enum Kind { none = 0, semis, degrees, decay, harmonics, timbre, morph };
    int kindA = none, kindB = none;
    int scale = 0;
};

class VoiceBase
{
public:
    virtual ~VoiceBase() = default;
    virtual void prepare (float sampleRate) { sr = sampleRate; }
    virtual void trigger (const Strike& s) = 0;
    // adds into out (mono), returns nothing; the engine pans and sends
    virtual void render (float* out, int n) = 0;
    virtual bool active() const = 0;
    float energy = 0.0f;              // for the display, 0..1
    // SUSTAIN: the voice holds its note instead of decaying (a drone); off again, it lets go
    virtual void setSustain (bool on) { if (sustain && ! on) release(); sustain = on; }
    bool sustaining() const { return sustain; }

protected:
    virtual void release() {}         // sustain let go: fade out over a moment
    float susK (float k) const { return sustain ? 1.0f : k; }
    bool sustain = false;
    // When struck while still sounding, the old output fades over 2 ms instead of jumping.
    void beginDeclick() { dcValue = last; dcPos = 0; }
    float declick (float y)
    {
        if (dcPos < kDc) { y += dcValue * (1.0f - (float) dcPos / (float) kDc); ++dcPos; }
        last = y;
        return y;
    }
    static constexpr int kDc = 96;
    float sr = 48000.0f, last = 0.0f, dcValue = 0.0f;
    int dcPos = kDc;
    dsp::Rng rng;
};

class Kick final : public VoiceBase
{
public:
    void release() override { ampK = dsp::decayCoef (0.25f, sr); }
    void trigger (const Strike& s) override;
    void render (float* out, int n) override;
    bool active() const override { return amp > 1.0e-4f; }
private:
    double phase = 0;
    float f0 = 50, sweep = 4, pitchEnv = 0, pitchK = 0, amp = 0, ampK = 0, drive = 1, click = 0, clickEnv = 0;
};

class Mesh final : public VoiceBase
{
public:
    void release() override { toneK = noiseK = dsp::decayCoef (0.2f, sr); }
    void trigger (const Strike& s) override;
    void render (float* out, int n) override;
    bool active() const override { return toneAmp > 1.0e-4f || noiseAmp > 1.0e-4f; }
private:
    double p1 = 0, p2 = 0;
    float f = 200, bend = 0, bendK = 0, toneAmp = 0, toneK = 0, noiseAmp = 0, noiseK = 0, toneMix = 0.5f;
    dsp::Svf bp, hp;
};

class Fm final : public VoiceBase
{
public:
    void release() override { ampK = dsp::decayCoef (0.25f, sr); }
    void trigger (const Strike& s) override;
    void render (float* out, int n) override;
    bool active() const override { return amp > 1.0e-4f; }
private:
    double pc = 0, pm = 0;
    float f = 200, ratio = 2, index = 4, idxEnv = 0, idxK = 0, amp = 0, ampK = 0, fb = 0, prevMod = 0;
};

class Opto final : public VoiceBase
{
public:
    void release() override { slowK = dsp::decayCoef (0.4f, sr); }
    void trigger (const Strike& s) override;
    void render (float* out, int n) override;
    bool active() const override { return fast + slow > 1.0e-4f; }
private:
    double phase = 0;
    float f = 300, fold = 1, fast = 0, slow = 0, fastK = 0, slowK = 0, bright = 1, noise = 0, attack = 1, attackK = 0;
    dsp::OnePole lp1, lp2;
};

class Lantern final : public VoiceBase
{
public:
    void release() override { ampK = dsp::decayCoef (0.2f, sr); }
    void trigger (const Strike& s) override;
    void render (float* out, int n) override;
    bool active() const override { return amp > 1.0e-4f; }
private:
    std::array<double, 6> ph {};
    std::array<float, 6> fr {};
    float amp = 0, ampK = 0, metal = 0.5f;
    dsp::Svf bp, hp;
};

class Zap final : public VoiceBase
{
public:
    void release() override { ampK = dsp::decayCoef (0.25f, sr); }
    void trigger (const Strike& s) override;
    void render (float* out, int n) override;
    bool active() const override { return amp > 1.0e-4f; }
private:
    double phase = 0;
    float fEnd = 200, ratio = 20, sweep = 0, sweepK = 0, amp = 0, ampK = 0, ring = 0;
};

class StrikeVoice final : public VoiceBase
{
public:
    void release() override { for (auto& st : strings) st.gain = std::min (st.gain, 0.995f); }
    void prepare (float sampleRate) override;
    void trigger (const Strike& s) override;
    void render (float* out, int n) override;
    bool active() const override { return ringing > 1.0e-5f; }
    int lastDegree = 0;               // for the display / patch notes
    static float degreeToSemis (int scale, int degree);
private:
    static constexpr int kStrings = 6, kLen = 8192;
    struct String
    {
        std::vector<float> buf;
        int w = 0;
        float delay = 100, gain = 0.99f, damp = 0.5f, z = 0;
        int exciteLeft = 0, startIn = 0;
        float exciteAmp = 0;
        dsp::OnePole exLp;
    };
    std::array<String, kStrings> strings;
    int next = 0, quiet = 1 << 20;
    dsp::OnePole dcBlock;
    float ringing = 0;
};

} // namespace flt
