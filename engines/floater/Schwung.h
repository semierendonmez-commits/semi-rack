// The Schwung modules inside FLOATER. Each one is the module's own DSP, unchanged, from
// the Schwung catalogue (the same sources the Schwung AU shell builds), driven directly
// instead of through Move's int16 / 128-frame slot so that strikes stay sample-accurate:
//   Plaits    j3threejay/move-anything-plaits    (Mutable Instruments, MIT)  a model for any module slot
//   Braids    charlesvestal/schwung-braids       (Mutable Instruments, MIT)  a model for any module slot
//   Clouds    filliformes/verglas-move           (Mutable Instruments, MIT)  a model for SCATTER
//   CloudSeed charlesvestal/schwung-cloudseed    (Ghost Note Audio, MIT)     a model for SPACE
//   Drum Buss legsmechanical/schwung-busdriver   (MIT)                       the bus
//   Parasites mqtthiqs/parasites (Clouds' OLIVERB and RESONESTOR, MIT)       two more models for SCATTER
//             (not in the Schwung catalogue: the alternative Clouds firmware of Devine's "Strum" patch)
#pragma once

#include "Voices.h"
#include "Rate.h"
#include <array>
#include <memory>

namespace flt
{

// ---- the model menu of a module slot: its own voice, then the Plaits engines ----------
struct PlaitsModel { const char* name; int engine; const char* harm; const char* timbre; const char* morph; float gain; };

// in menu order (percussion first, the way a case gets patched); engine = Plaits' own index
inline const std::array<PlaitsModel, 24>& plaitsModels()
{
    static const std::array<PlaitsModel, 24> m {{
        { "BASS DRUM",   21, "ATTACK", "PUNCH",  "DECAY",  1.0f },
        { "SNARE DRUM",  22, "BALANCE","TONE",   "DECAY",  1.0f },
        { "HI-HAT",      23, "METAL",  "TONE",   "DECAY",  1.0f },
        { "MODAL",       20, "MATERIAL","BRIGHT", "DAMP",  1.0f },
        { "STRING",      19, "INHARM", "BRIGHT", "DAMP",   1.0f },
        { "FM 2-OP",     10, "RATIO",  "INDEX",  "FDBK",   2.5f },
        { "ADDITIVE",    12, "BUMPS",  "SLOPE",  "SHIFT",  1.0f },
        { "WAVESHAPE",    9, "OVERTN", "SHAPE",  "FOLD",   1.0f },
        { "V.ANALOG",     8, "DETUNE", "CUTOFF", "SHAPE",  1.0f },
        { "VA VCF",       0, "DETUNE", "CUTOFF", "RESO",   1.0f },
        { "PHASE DIST",   1, "RATIO",  "PHASE",  "DIST",   1.0f },
        { "GRAIN",       11, "POS",    "SIZE",   "DENS",   1.1f },
        { "SWARM",       16, "DETUNE", "SPREAD", "CHAOS",  0.9f },
        { "NOISE",       17, "FREQ",   "SLOPE",  "RUGGED", 1.2f },
        { "PARTICLE",    18, "SPREAD", "EXTINCT","SCATTER",1.2f },
        { "CHORD",       14, "CHORD",  "TIMBRE", "WAVE",   0.8f },
        { "WAVETABLE",   13, "BANK",   "POINTER","MIRROR", 1.2f },
        { "WAVE TERRAIN", 5, "TERRAIN","X",      "Y",      1.8f },
        { "STRING MACH",  6, "DETUNE", "TONE",   "ENV",    1.5f },
        { "6-OP I",       2, "PRESET", "MOD LVL","STRETCH",8.0f },
        { "6-OP II",      3, "PRESET", "MOD LVL","STRETCH",8.0f },
        { "6-OP III",     4, "PRESET", "MOD LVL","STRETCH",8.0f },
        { "CHIPTUNE",     7, "ARP",    "DUTY",   "FILTER", 1.2f },
        { "SPEECH",      15, "PROSODY","PHONEME","FORMANT",1.3f },
    }};
    return m;
}

// Braids: the macro oscillator Devine sequences in "Ascension" (its HARM mode), at its own 96 kHz.
// COLOR = TIMBRE, HARM = COLOR (Braids' two knobs), MORPH = a low pass after it, DECAY = an AD envelope.
struct BraidsModel { const char* name; int shape; };
inline const std::array<BraidsModel, 31>& braidsModels()
{
    static const std::array<BraidsModel, 31> m {{
        { "CSAW", 0 }, { "MORPH", 1 }, { "SQR SYNC", 7 }, { "SAW SYNC", 8 }, { "3xSAW", 9 }, { "3xRING", 13 }, { "SWARM", 14 }, { "COMB", 15 },
        { "TOY", 16 }, { "ZLPF", 17 }, { "ZBPF", 19 }, { "VOSIM", 21 }, { "VOWEL", 22 }, { "HARM", 24 }, { "FM", 25 }, { "FBFM", 26 },
        { "WTFM", 27 }, { "PLUK", 28 }, { "BOWD", 29 }, { "BLOW", 30 }, { "FLUT", 31 }, { "BELL", 32 }, { "DRUM", 33 }, { "KICK", 34 },
        { "CYMB", 35 }, { "SNAR", 36 }, { "WTBL", 37 }, { "WMAP", 38 }, { "TWNQ", 42 }, { "CLKN", 43 }, { "QPSK", 46 },
    }};
    return m;
}

// a slot's MODEL value: 0 its own voice, then the Plaits engines, then the Braids shapes, then REEL
// (the slot plays slices of the master tape: self-sampling, see Reel.h)
inline constexpr int kFirstPlaits = 1, kFirstBraids = 25, kReelModel = 56;
inline int numModels() { return kReelModel + 1; }
inline bool isPlaits (int model) { return model >= kFirstPlaits && model < kFirstBraids; }
inline bool isBraids (int model) { return model >= kFirstBraids && model < kReelModel; }
inline bool isReel (int model) { return model == kReelModel; }

// The native voice's names for HARM / COLOR / MORPH, per slot.
struct NativeLabels { const char* model; const char* harm; const char* color; const char* morph; };
inline const std::array<NativeLabels, kVoices>& nativeLabels()
{
    static const std::array<NativeLabels, kVoices> n {{
        { "BD9 KICK",    "CLICK",  "SWEEP",  "DRIVE"  },
        { "MESH SNARE",  "SNAP",   "WIRES",  "BAND"   },
        { "FM PLUCK",    "RATIO",  "INDEX",  "FDBK"   },
        { "OPTO LPG",    "FOLD",   "GATE",   "NOISE"  },
        { "LANTERN HAT", "BAND",   "METAL",  "AIR"    },
        { "S1 ZAP",      "RING",   "FALL",   "HEIGHT" },
        { "RINGS STRUM", "CHORD",  "BRIGHT", "STRUM"  },
    }};
    return n;
}

//==============================================================================
// One Plaits voice (all its engines), rendered at 48 kHz and resampled to the host.
class PlaitsVoice final : public VoiceBase
{
public:
    PlaitsVoice();
    ~PlaitsVoice() override;
    void prepare (float sampleRate) override;
    void trigger (const Strike& s) override;
    void render (float* out, int n) override;
    bool active() const override { return sustain || quiet < quietLimit; }
    void setModel (int menuIndex) { model = menuIndex; }   // 0..23 in plaitsModels() order
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    int model = 0, quiet = 1 << 30, quietLimit = 48000;
};

//==============================================================================
// One Braids macro oscillator, rendered at 96 kHz in 24-sample blocks and resampled to the host.
class BraidsVoice final : public VoiceBase
{
public:
    BraidsVoice();
    ~BraidsVoice() override;
    void prepare (float sampleRate) override;
    void trigger (const Strike& s) override;
    void render (float* out, int n) override;
    bool active() const override { return sustain || env > 1.0e-4f || attackLeft > 0; }
    void setModel (int menuIndex) { model = menuIndex; }   // 0..30 in braidsModels() order
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    int model = 0, attackLeft = 0;
    float env = 0.0f, envK = 0.999f, attackStep = 1.0f, level = 1.0f;
};

//==============================================================================
// Clouds on SCATTER: the slot's audio in, lane 8 on its TRIG input, bursts on FREEZE.
class CloudsScatter
{
public:
    CloudsScatter();
    ~CloudsScatter();
    void prepare (double hostRate);
    void reset();
    struct Settings { int mode = 0; float position = 0.5f, size = 0.5f, pitch = 0.0f, density = 0.5f, texture = 0.5f,
                      spread = 1.0f, feedback = 0.0f; };
    void set (const Settings& s) { settings = s; }
    void trigger() { pendingTrigger = true; }
    void freeze (bool on) { frozen = on; }
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    Settings settings;
    bool pendingTrigger = false, frozen = false;
    BlockBridge bridge;
    dsp::OnePole dcL, dcR;
};

//==============================================================================
// Parasites on SCATTER: OLIVERB (a shimmering, modulated plate) and RESONESTOR (four combs per voice,
// tuned to a chord, struck by lane 8), at Clouds' own 32 kHz.
class ParasitesScatter
{
public:
    ParasitesScatter();
    ~ParasitesScatter();
    void prepare (double hostRate);
    void reset();
    struct Settings { int mode = 0; float position = 0.5f, size = 0.5f, pitch = 0.0f, density = 0.5f, texture = 0.5f,
                      spread = 0.5f, feedback = 0.0f; };
    void set (const Settings& s) { settings = s; }
    void trigger() { pendingTrigger = true; }
    void freeze (bool on) { frozen = on; }
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    Settings settings;
    bool pendingTrigger = false, frozen = false;
    BlockBridge bridge;
    dsp::OnePole dcL, dcR;
};

//==============================================================================
// Mutable Instruments Beads on SCATTER (open sourced 12 October 2026): the texture synthesizer after Clouds.
// Its four quality settings run at their own rates (48 / 48 with 24 kHz grains / 32 / 24 kHz), each through
// its own bridge, made ready in prepare() so switching never allocates. Every hit of lane 8 seeds a grain and
// DENSITY adds Beads' own grains between them (see floater_beads.h).
// Bursts on lane 8 (and SCATTER's FREEZE) hold FREEZE.
class BeadsScatter
{
public:
    BeadsScatter();
    ~BeadsScatter();
    void prepare (double hostRate);
    void reset();
    struct Settings { int media = 0; float time = 0.5f, size = 0.5f, shape = 0.5f, pitch = 0.0f, density = 0.25f, feedback = 0.0f,
                      reverb = 0.0f, random = 0.0f; bool seeded = true; };
    void set (const Settings& s) { settings = s; }
    void trigger() { pendingTrigger = true; }
    void freeze (bool on) { frozen = on; }
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    Settings settings;
    bool pendingTrigger = false, frozen = false;
    std::array<BlockBridge, 4> bridges;
    int media = -1;
    dsp::OnePole dcL, dcR;
};

//==============================================================================
// CloudSeed on SPACE.
class CloudSeedSpace
{
public:
    CloudSeedSpace();
    ~CloudSeedSpace();
    void prepare (double hostRate);
    void reset();
    // size, decay, damp as SPACE's knobs; program 0.. picks diffusion / modulation / stereo / pre-delay
    void set (float size, float decay, float damp, float diffusion, float modAmount, float modRate, float cross, float predelay);
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);
private:
    void* cs = nullptr;
    BlockBridge bridge;
    std::array<float, 8> last {};
    bool dirty = true;
};

} // namespace flt
