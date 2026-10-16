// Vendored into SemiFamily from semierendonmez-commits/illusorium Source/engine (JUCE-free,
// namespace il). Changes, all under #ifdef METAMODULE and marked "SemiFamily": RISSET tape 10 s,
// CONST SPECTRUM <= 8 x 6 oscillators and 4 input copies, SHEPARD <= 4 voices. Chain.h is vendored
// but not used by the Rack module (one illusion at a time).
// ILLUSORIUM - the module contract. Every illusion is one Module: it declares
// its parameters (PSpec), processes a stereo block in place (wet only; the
// chain does on / mix / solo / crossfades), and publishes a few numbers for
// the illusion screen (viz).
#pragma once
#include "Dsp.h"
#include <atomic>
#include <memory>
#include <string>

namespace il
{
enum class PType { Float, Choice, Bool };

struct PSpec
{
    const char* id;          // short id, the parameter id is "<module>_<id>"
    const char* name;        // knob label (short, upper case)
    PType type;
    float lo, hi, def;
    float centre;            // skew centre for float ranges (0 = linear)
    const char* unit;        // "Hz", "ms", "dB", "%", "st", "s", "" ...
    const char* choices;     // "A|B|C" for Choice
    const char* tip;         // tooltip / footer hint
    bool integer = false;    // float shown and stepped as an integer
};

enum Page { pagePitch = 0, pageTime, pageSpace, pagePercept, numPages };

struct MInfo
{
    const char* key;         // parameter prefix, also the chain tile text source
    const char* tile;        // 4-5 letter chain tile label
    const char* name;        // module name
    const char* illusion;    // the illusion and who described it
    int page;
    bool phones;             // needs headphones to work
    const char* physical;    // "in the signal"
    const char* perceived;   // "what you hear"
    const char* grab;        // label of the trigger button (nullptr: none)
    std::vector<PSpec> params;
};

// Host transport for one block
struct Ctx
{
    double sr = 48000.0;
    double bpm = 120.0;
    double ppq = 0.0;        // beat position at the first sample of the block
    bool playing = false;
    double beatsPerSample() const { return bpm / 60.0 / sr; }
    double samplesPerBeat() const { return sr * 60.0 / bpm; }
};

class Module
{
public:
    virtual ~Module() = default;
    virtual const MInfo& info() const = 0;
    virtual void prepare (double sampleRate, int maxBlock) { sr = sampleRate; (void) maxBlock; }
    virtual void reset() {}
    virtual void process (float* L, float* R, int n, const Ctx&) = 0;
    virtual int latency() const { return 0; }
    virtual void trigger() {}                      // GRAB / re-seed
    virtual bool generates() const { return false; }   // makes sound from silence

    // plain parameter values in PSpec order, written by the host every block
    std::array<float, 16> p {};
    int ci (int k) const { return (int) std::lround (p[(size_t) k]); }
    bool on (int k) const { return p[(size_t) k] > 0.5f; }

    // illusion screen read-outs (audio thread writes, UI reads)
    std::array<std::atomic<float>, 48> viz {};
    void setViz (int i, float v) { viz[(size_t) i].store (v, std::memory_order_relaxed); }
    // larger read-outs (spectral modules): 144 values
    std::array<std::atomic<float>, 144> arr {};
    void setArr (int i, float v) { arr[(size_t) i].store (v, std::memory_order_relaxed); }

protected:
    double sr = 48000.0;
};

using ModulePtr = std::unique_ptr<Module>;

// registry (Registry.cpp)
int numModules();
const MInfo& moduleInfo (int index);               // index = processing order
ModulePtr createModule (int index);
int pageOf (int index);
} // namespace il
