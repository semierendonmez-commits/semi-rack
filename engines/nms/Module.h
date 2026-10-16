// NMS - module framework (no JUCE).
//
// Every module (DUFFY, SAWBIRD, BAIKAL, ...) is described by a TypeSpec: its
// panel controls (placed on a 28 px grid), input and output jacks, and for
// each control its NMS parameter type (generator = pattern, sound) and its
// default relative macro (the 24 standard macros of the framework).
// Controls are stored normalised 0..1 in atomics; the UI, the pattern
// memory, the randomiser and the audio thread share them without locks.
//
// Signals: audio +-1; CV 0..1; pitch in octaves (1.0 = one octave);
// pulses 0/1 (rising edge through 0.5 = trigger).
//
// Channels: as in NMS, every instrument / FX module sits on a mixer channel.
// Unpatched inputs are NORMALLED along the channel chain: an FX takes the
// audio and triggers of the modules before it on its channel, the mixer
// channel takes the end of the chain. A patch cable always wins.
#pragma once
#include "Dsp.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace nms
{
enum class Kind { Knob, Big, Mini, Toggle, Choice, Momentary, Hidden };
enum class Map { Lin, Exp, Int };
enum class PortKind { Audio, CV, Pitch, Pulse };
enum class Cat { System, Drum, Synth, Sampler, Midi, Fx, Control };
// normalling of an input when no cable is plugged into it
enum class Norm { None, ChainL, ChainR, ChainTrig, ChannelL, ChannelR, ReturnL, ReturnR };

constexpr int kMacros = 24;
constexpr int kRows = 9;         // panel grid rows: 8 + the organic row (LINK / LISTEN / AWARE / DEPTH / BREATH)
constexpr int kChannels = 8;
constexpr int kLanes = 10;      // 8 mixer channels + the two send / return lanes (RET A, RET B)
const char* macroName (int i);   // "PITCH", "RANGE", ... "SPACE"
const char* macroGroup (int i);  // GENERATORS / SHAPES / SOUND

struct ParamSpec
{
    std::string key, name, unit;
    Kind kind = Kind::Knob;
    Map map = Map::Lin;
    float min = 0, max = 1, def = 0;
    std::vector<std::string> choices;
    int col = 0, row = 0, w = 2, h = 2;
    bool rnd = true;                          // randomiser / chaos may move it
    bool ptn = true;                          // stored in patterns
    bool sound = false;                       // NMS type: 0 generator, 1 sound
    bool bipolar = false;
    int macro = -1; float macroAmt = 0.0f;    // default relative macro
    int page = -1;                            // upstream modules: shown only on this page (-1 = always)
    std::string tip;
};

struct PortSpec { std::string key, name; PortKind kind; Norm norm = Norm::None; std::string tip; };
struct CustomArea { int id, col, row, w, h; };

struct TypeSpec
{
    std::string id, title, blurb;
    Cat cat = Cat::Drum;
    uint32_t colour = 0xff808080;             // the module's NMS colour
    int units = 4;
    bool unique = false, removable = true, chained = false;
    std::vector<ParamSpec> params;
    std::vector<PortSpec> ins, outs;
    std::vector<CustomArea> custom;
    std::unordered_map<std::string, int> index;
    int param (const std::string& k) const { auto i = index.find (k); return i == index.end() ? -1 : i->second; }
    int in (const std::string& k) const { for (size_t i = 0; i < ins.size(); ++i) if (ins[i].key == k) return (int) i; return -1; }
    int out (const std::string& k) const { for (size_t i = 0; i < outs.size(); ++i) if (outs[i].key == k) return (int) i; return -1; }

    float toPlain (int i, float n) const
    {
        const auto& p = params[(size_t) i];
        n = clamp01 (n);
        if (! p.choices.empty()) return (float) std::lround (n * (float) (p.choices.size() - 1));
        switch (p.map)
        {
            case Map::Exp: return p.min * std::pow (p.max / p.min, n);
            case Map::Int: return (float) std::lround (p.min + n * (p.max - p.min));
            default:       return p.min + n * (p.max - p.min);
        }
    }
    float toNorm (int i, float v) const
    {
        const auto& p = params[(size_t) i];
        if (! p.choices.empty()) return p.choices.size() > 1 ? clamp01 (v / (float) (p.choices.size() - 1)) : 0.0f;
        if (p.map == Map::Exp) return clamp01 (std::log (std::max (v, 1.0e-9f) / p.min) / std::log (p.max / p.min));
        return p.max > p.min ? clamp01 ((v - p.min) / (p.max - p.min)) : 0.0f;
    }
    int steps (int i) const
    {
        const auto& p = params[(size_t) i];
        if (! p.choices.empty()) return (int) p.choices.size();
        if (p.kind == Kind::Toggle || p.kind == Kind::Momentary) return 2;
        if (p.map == Map::Int) return (int) std::lround (p.max - p.min) + 1;
        return 0;
    }
};

// global musical state, handed to every module once per sub-block
struct Ctx
{
    double sr = 48000.0;
    double bpm = 120.0;
    double pos16 = 0.0, pos16PerSample = 0.0;   // 16th-note position incl. swing / wonk
    bool running = true;
    int restart = 0;                             // bumped on restart
    int64_t sample = 0;                          // running sample counter
    // keyboard (MIDI notes not taken by LOOP PACK)
    float keyPitch = 0.0f, keyGate = 0.0f, keyVel = 0.0f;
    int keyTrig = 0;
    // scale
    int key = 0, notesPerOct = 12;
    std::array<int, 64> scale {};               // steps of one cycle, in notesPerOct units
    int scaleLen = 7;
    float lfo[4] {};
    float energy = 0.5f;                         // global activity 0..1 (ENERGY bus)
    const std::array<class Module*, 10>* lanes = nullptr;   // the first instrument of every lane (for LINK)
    // degree (any integer) -> semitones from C
    float degreeSemis (int degree) const
    {
        const int n = std::max (1, scaleLen);
        const int oct = (int) std::floor ((double) degree / n);
        const int d = degree - oct * n;
        const float stepSemis = 12.0f / (float) std::max (1, notesPerOct);
        return (float) key + (float) (oct * notesPerOct + scale[(size_t) d]) * stepSemis;
    }
};

class Module
{
public:
    explicit Module (const TypeSpec& t) : spec (t), n ((size_t) t.params.size())
    {
        norm.reset (new std::atomic<float>[n]);
        modOff.reset (new std::atomic<float>[n]);
        cache.assign (n, 0.0f);
        for (size_t i = 0; i < n; ++i) { norm[i].store (t.toNorm ((int) i, t.params[i].def)); modOff[i].store (0.0f); }
        for (auto& d : disp) d.store (0.0f);
        ch = t.param ("ch");
    }
    virtual ~Module() = default;

    const TypeSpec& spec;
    int id = 0;
    const size_t n;

    float getNorm (int i) const { return norm[(size_t) i].load (std::memory_order_relaxed); }
    void setNorm (int i, float v) { norm[(size_t) i].store (clamp01 (v), std::memory_order_relaxed); }
    float getPlain (int i) const { return spec.toPlain (i, getNorm (i)); }
    void setPlain (int i, float v) { setNorm (i, spec.toNorm (i, v)); }
    float effNorm (int i) const { return clamp01 (getNorm (i) + modOff[(size_t) i].load (std::memory_order_relaxed)); }
    float modOffset (int i) const { return modOff[(size_t) i].load (std::memory_order_relaxed); }
    void setMod (int i, float v) { modOff[(size_t) i].store (v, std::memory_order_relaxed); }
    void resetToDefaults() { for (size_t i = 0; i < n; ++i) if (spec.params[i].key != "ch") norm[i].store (spec.toNorm ((int) i, spec.params[i].def)); }
    int channel() const { return ch >= 0 ? (int) getPlain (ch) : -1; }   // 0-based

    virtual void prepare (double sampleRate) { sr = (float) sampleRate; }
    virtual void reset() {}
    virtual void block (const Ctx& c)
    {
        ctx = &c;
        for (size_t i = 0; i < n; ++i) if (spec.params[i].kind != Kind::Hidden || spec.params[i].ptn) cache[i] = spec.toPlain ((int) i, effNorm ((int) i));
    }
    virtual void tick (const float* in, float* out) = 0;
    virtual void momentary (int /*param*/) {}
    virtual std::string text (int i) const;
    std::atomic<int> request { 0 };        // UI requests the processor serves (SCHAEFER: 1 load, 2 default)
    uint64_t patched = 0;
    bool isPatched (int in) const { return (patched >> in) & 1u; }

    std::array<std::atomic<float>, 64> disp {};

    // pattern visualiser: number of steps shown, and the hit (velocity, 0 = none) and degree of a step
    virtual int previewSteps() const { return 0; }
    virtual float preview (int64_t /*step*/, int& /*degree*/) const { return 0.0f; }
    // LINK: the velocity this module plays at a 16th step (pure function of its controls)
    virtual float hitAt (int64_t /*step16*/) const { return 0.0f; }
    std::atomic<float> activity { 0.0f };   // decaying sum of hit velocities (ENERGY)
    // SCHAEFER: the sample (set by the processor under the rack lock)
    virtual void setSample (std::shared_ptr<const SampleData>) {}
    virtual std::shared_ptr<const SampleData> sample() const { return nullptr; }
    // modules that read sample files themselves (BREAKBEAT): slot 0 = A, 1 = B
    virtual bool setSampleFile (const std::string&, int) { return false; }
    virtual std::string sampleName() const { return {}; }
    virtual bool isOutput() const { return false; }
    float mainL = 0, mainR = 0;

protected:
    float c (int i) const { return cache[(size_t) i]; }
    float cn (int i, float cv) const { return clamp01 (effNorm (i) + cv); }
    float plainCv (int i, float cv) const { return spec.toPlain (i, cn (i, cv)); }
    float sr = 48000.0f;
    const Ctx* ctx = nullptr;
    std::unique_ptr<std::atomic<float>[]> norm, modOff;
    std::vector<float> cache;
    int ch = -1;
};

struct Registry
{
    static const std::vector<const TypeSpec*>& types();
    static const TypeSpec* find (const std::string& id);
    static std::unique_ptr<Module> create (const std::string& id);
};

// MIDI events produced by DIDIMIDI, collected by the processor
struct MidiOut { int64_t sample; int status, d1, d2; };
} // namespace nms
