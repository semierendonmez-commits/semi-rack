// SEMI port: vendored from FLOATER Source/engine (v0.7), namespace fl -> flt; see engines/floater/sources.mk.
#include "Schwung.h"

#if !defined(TEST) && !defined(METAMODULE)
 #define TEST   // stmlib: the desktop build, no ARM intrinsics (SEMI port: MetaModule keeps them)
#endif
#include "plaits/dsp/voice.h"
#include "braids/macro_oscillator.h"
#include "stmlib/utils/buffer_allocator.h"
#include "clouds/dsp/granular_processor.h"
#ifndef METAMODULE
#include "cloudseed/floater_cloudseed.h"
#endif
#include "parasites/floater_parasites.h"
#include "FloaterBeads.h"   // SEMI port: FLOATER v0.7's Beads bridge, renamed flt_beads (see FloaterBeads.h)

#include <cstring>

namespace flt
{
using namespace dsp;

//==============================================================================
// Plaits
struct PlaitsVoice::Impl
{
    plaits::Voice voice;
    plaits::Patch patch {};
    plaits::Modulations mods {};
    stmlib::BufferAllocator allocator;
    alignas (16) char buffer[32768];
    plaits::Voice::Frame frames[plaits::kMaxBlockSize];
    int avail = 0, index = 0, gateLeft = 0;
    bool pending = false;
    float gain = 1.0f, level = 0.0f;
    Resampler<1> rs;
    dsp::OnePole dc;   // some engines (additive, chord, the 6-ops) carry DC; the effects and the bus should not see it

    // No constructor of its own on purpose: make_unique value-initialises, which zeroes the
    // Plaits and Clouds objects first, as the Schwung wrappers do (their own classes leave
    // members to the caller). Then init().
    void init()
    {
        allocator.Init (buffer, sizeof (buffer));
        voice.Init (&allocator);
        std::memset (&patch, 0, sizeof (patch));
        std::memset (&mods, 0, sizeof (mods));
        patch.decay = 0.5f;
        patch.lpg_colour = 0.5f;
        patch.harmonics = patch.timbre = patch.morph = 0.5f;
        patch.note = 48.0f;
        mods.trigger_patched = true;
        mods.level_patched = true;
    }
};

// Plaits works out its pitches for a 47872.34 Hz clock; here it really runs at 48 kHz.
static const float kPitchFix = -12.0f * std::log2 (48000.0f / 47872.34f);

PlaitsVoice::PlaitsVoice() : impl (std::make_unique<Impl>()) { impl->init(); }
PlaitsVoice::~PlaitsVoice() = default;

void PlaitsVoice::prepare (float sampleRate)
{
    VoiceBase::prepare (sampleRate);
    impl->rs.setRates (48000.0, sampleRate);
    impl->rs.reset();
    impl->dc.set (15.0f, sampleRate);
    impl->avail = impl->index = 0;
    quietLimit = (int) sampleRate;
    quiet = 1 << 30;
}

void PlaitsVoice::trigger (const Strike& s)
{
    auto& I = *impl;
    const auto& m = plaitsModels()[(size_t) std::max (0, std::min (23, model))];
    const bool drum = m.engine >= 21;
    float note = drum ? 12.0f + 60.0f * s.p.tune : 24.0f + 60.0f * s.p.tune;
    float decay = s.p.decay, harm = s.p.harm, timbre = s.p.color, morph = s.p.morph;
    auto apply = [&] (int kind, float v)
    {
        switch (kind)
        {
            case Strike::semis:     note += 12.0f * clamp (v, -1.5f, 1.5f); break;
            case Strike::degrees:   note = std::round (note) + StrikeVoice::degreeToSemis (s.scale, (int) std::lround (clamp (v, -1.5f, 1.5f) * 7.0f)); break;
            case Strike::decay:     decay += 0.4f * v; break;
            case Strike::harmonics: harm += 0.5f * v; break;
            case Strike::timbre:    timbre += 0.5f * v; break;
            case Strike::morph:     morph += 0.5f * v; break;
            default: break;
        }
    };
    apply (s.kindA, s.modA);
    apply (s.kindB, s.modB);
    I.patch.engine = m.engine;
    I.patch.note = note + kPitchFix;
    I.patch.harmonics = clamp (harm, 0.0f, 1.0f);
    I.patch.timbre = clamp (timbre, 0.0f, 1.0f);
    I.patch.morph = clamp (morph, 0.0f, 1.0f);
    I.patch.decay = clamp (decay, 0.0f, 1.0f);
    I.patch.lpg_colour = 0.5f;
    I.patch.frequency_modulation_amount = 0.0f;
    I.patch.timbre_modulation_amount = 0.0f;
    I.patch.morph_modulation_amount = 0.0f;
    I.gain = m.gain * 0.85f;
    I.level = clamp (s.vel, 0.0f, 1.0f);
    I.pending = true;
    I.gateLeft = 960;   // the gate is held 20 ms, like a pad tapped: then the LPG closes at the DECAY rate
    quiet = 0;
}

void PlaitsVoice::render (float* out, int n)
{
    auto& I = *impl;
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const auto f = I.rs.pull ([&]
        {
            if (I.index >= I.avail)
            {
                // small blocks, so a strike lands within a few samples of where it was asked for
                constexpr int kChunk = 8;
                I.mods.trigger = I.pending ? 1.0f : 0.0f;
                // SUSTAIN holds the low pass gate open: the engine drones
                I.mods.level = sustain ? std::max (I.level, 0.6f) : (I.gateLeft > 0 ? I.level : 0.0f);
                I.gateLeft = std::max (0, I.gateLeft - kChunk);
                I.pending = false;
                I.voice.Render (I.patch, I.mods, I.frames, kChunk);
                I.avail = kChunk;
                I.index = 0;
            }
            const float v = -(float) I.frames[I.index++].out / 32767.0f * I.gain;
            return Resampler<1>::Frame { v };
        });
        const float y = I.dc.hp (f[0]);
        out[i] += y;
        peak = std::max (peak, std::abs (y));
    }
    quiet = peak > 1.0e-4f ? 0 : quiet + n;
    energy = std::min (1.0f, peak * 1.4f);
}

//==============================================================================
// Braids
struct BraidsVoice::Impl
{
    braids::MacroOscillator osc;
    uint8_t sync[24] {};
    int16_t buffer[24] {};
    int avail = 0, index = 0;
    Resampler<1> rs;
    dsp::Svf lp;
    dsp::OnePole dc;
    void init() { osc.Init(); osc.set_shape (braids::MACRO_OSC_SHAPE_CSAW); osc.set_pitch (60 << 7); osc.set_parameters (0, 0); }
};

BraidsVoice::BraidsVoice() : impl (std::make_unique<Impl>()) { impl->init(); }
BraidsVoice::~BraidsVoice() = default;

void BraidsVoice::prepare (float sampleRate)
{
    VoiceBase::prepare (sampleRate);
    impl->rs.setRates (96000.0, sampleRate);   // Braids' tables are for its own 96 kHz
    impl->rs.reset();
    impl->dc.set (15.0f, sampleRate);
    impl->lp.reset();
    impl->avail = impl->index = 0;
    env = 0.0f;
    attackLeft = 0;
}

void BraidsVoice::trigger (const Strike& s)
{
    auto& I = *impl;
    const auto& m = braidsModels()[(size_t) std::max (0, std::min ((int) braidsModels().size() - 1, model))];
    const bool drum = m.shape >= 33 && m.shape <= 36;
    float note = drum ? 12.0f + 60.0f * s.p.tune : 24.0f + 60.0f * s.p.tune;
    float decay = s.p.decay, timbre = s.p.color, color = s.p.harm, morph = s.p.morph;
    auto apply = [&] (int kind, float v)
    {
        switch (kind)
        {
            case Strike::semis:     note += 12.0f * clamp (v, -1.5f, 1.5f); break;
            case Strike::degrees:   note = std::round (note) + StrikeVoice::degreeToSemis (s.scale, (int) std::lround (clamp (v, -1.5f, 1.5f) * 7.0f)); break;
            case Strike::decay:     decay += 0.4f * v; break;
            case Strike::harmonics: color += 0.5f * v; break;
            case Strike::timbre:    timbre += 0.5f * v; break;
            case Strike::morph:     morph += 0.5f * v; break;
            default: break;
        }
    };
    apply (s.kindA, s.modA);
    apply (s.kindB, s.modB);
    I.osc.set_shape ((braids::MacroOscillatorShape) m.shape);
    I.osc.set_pitch ((int16_t) std::lround (clamp (note, 0.0f, 120.0f) * 128.0f));
    I.osc.set_parameters ((int16_t) (clamp (timbre, 0.0f, 1.0f) * 32767.0f), (int16_t) (clamp (color, 0.0f, 1.0f) * 32767.0f));
    I.osc.Strike();
    I.lp.set (expMap (clamp (morph, 0.0f, 1.0f), 180.0f, 18000.0f), 0.9f, sr);
    level = clamp (s.vel, 0.0f, 1.0f);
    attackLeft = (int) (0.0015f * sr);
    attackStep = 1.0f / (float) std::max (1, attackLeft);
    envK = decayCoef (expMap (clamp (decay, 0.0f, 1.0f), 0.02f, 3.0f) * 0.35f, sr);
    beginDeclick();
}

void BraidsVoice::render (float* out, int n)
{
    auto& I = *impl;
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const auto f = I.rs.pull ([&]
        {
            if (I.index >= I.avail)
            {
                std::memset (I.sync, 0, sizeof (I.sync));
                I.osc.Render (I.sync, I.buffer, 24);
                I.avail = 24;
                I.index = 0;
            }
            return Resampler<1>::Frame { (float) I.buffer[I.index++] / 32768.0f };
        });
        if (attackLeft > 0) { env = std::min (1.0f, env + attackStep); --attackLeft; }
        else if (sustain) env = std::min (1.0f, env + attackStep);     // SUSTAIN: held at the top
        else env *= envK;
        const float y = I.dc.hp (I.lp.lp (f[0])) * env * level * 0.8f;
        out[i] += declick (y);
        peak = std::max (peak, std::abs (y));
    }
    energy = std::min (1.0f, peak * 1.4f);
}

//==============================================================================
// Clouds
namespace
{
constexpr size_t kLargeBuffer = 163840, kSmallBuffer = 90112;   // as in the Move port
constexpr int kCloudsBlock = 32;
inline short toShort (float v) { return (short) std::lround (clamp (v, -1.0f, 1.0f) * 32767.0f); }
}

struct CloudsScatter::Impl
{
    clouds::GranularProcessor processor;
    std::vector<uint8_t> large = std::vector<uint8_t> (kLargeBuffer), small = std::vector<uint8_t> (kSmallBuffer);
    clouds::ShortFrame in[kCloudsBlock], out[kCloudsBlock];
    void init()
    {
        processor.Init (large.data(), large.size(), small.data(), small.size());
        processor.set_silence (false);
        processor.set_bypass (false);
        processor.set_playback_mode (clouds::PLAYBACK_MODE_GRANULAR);
        processor.set_quality (0);
    }
};

CloudsScatter::CloudsScatter() : impl (std::make_unique<Impl>()) { impl->init(); }
CloudsScatter::~CloudsScatter() = default;

void CloudsScatter::prepare (double hostRate)
{
    bridge.prepare (hostRate, 44100.0, kCloudsBlock);   // the Move port runs Clouds at 44.1 kHz
    dcL.set (15.0f, (float) hostRate);
    dcR.set (15.0f, (float) hostRate);
    reset();
}

void CloudsScatter::reset()
{
    impl->init();
    pendingTrigger = false;
    frozen = false;
}

void CloudsScatter::process (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    bridge.run (inL, inR, outL, outR, n, [this] (const float* l, const float* r, float* ol, float* orr, int b)
    {
        auto& I = *impl;
        auto& P = I.processor;
        P.set_playback_mode ((clouds::PlaybackMode) std::max (0, std::min (3, settings.mode)));
        P.set_quality (0);
        P.set_freeze (frozen);
        auto* p = P.mutable_parameters();
        p->position = clamp (settings.position, 0.0f, 1.0f);
        p->size = clamp (settings.size, 0.0f, 1.0f);
        p->pitch = clamp (settings.pitch, -24.0f, 24.0f);
        p->density = clamp (settings.density, 0.0f, 1.0f);
        p->texture = clamp (settings.texture, 0.0f, 1.0f);
        p->dry_wet = 1.0f;                       // wet only: FLOATER mixes it
        p->stereo_spread = clamp (settings.spread, 0.0f, 1.0f);
        p->feedback = clamp (settings.feedback, 0.0f, 1.0f);
        p->reverb = 0.0f;                        // SPACE does the reverb
        p->freeze = frozen;
        p->trigger = pendingTrigger;
        p->gate = pendingTrigger;
        pendingTrigger = false;
        P.Prepare();
        for (int i = 0; i < b; ++i) { I.in[i].l = toShort (l[i]); I.in[i].r = toShort (r[i]); }
        P.Process (I.in, I.out, (size_t) b);
        // the Move port's level compensation for transposed grains
        const float comp = 1.0f + std::abs (settings.pitch) * 0.07f;
        for (int i = 0; i < b; ++i)
        {
            ol[i] = (float) I.out[i].l / 32768.0f * comp;
            orr[i] = (float) I.out[i].r / 32768.0f * comp;
        }
    });
    for (int i = 0; i < n; ++i) { outL[i] = dcL.hp (outL[i]); outR[i] = dcR.hp (outR[i]); }
}

//==============================================================================
// Parasites
struct ParasitesScatter::Impl
{
    floater_parasites::Processor processor;
};

ParasitesScatter::ParasitesScatter() : impl (std::make_unique<Impl>()) {}
ParasitesScatter::~ParasitesScatter() = default;

void ParasitesScatter::prepare (double hostRate)
{
    bridge.prepare (hostRate, (double) floater_parasites::kRate, floater_parasites::kBlock);
    dcL.set (15.0f, (float) hostRate);
    dcR.set (15.0f, (float) hostRate);
    reset();
}

void ParasitesScatter::reset()
{
    impl->processor.reset (settings.mode);
    pendingTrigger = false;
    frozen = false;
}

void ParasitesScatter::process (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    bridge.run (inL, inR, outL, outR, n, [this] (const float* l, const float* r, float* ol, float* orr, int)
    {
        floater_parasites::Settings s;
        s.mode = settings.mode;
        s.position = clamp (settings.position, 0.0f, 1.0f);
        s.size = clamp (settings.size, 0.0f, 1.0f);
        s.pitch = clamp (settings.pitch, -24.0f, 24.0f);
        s.density = clamp (settings.density, 0.0f, 1.0f);
        s.texture = clamp (settings.texture, 0.0f, 1.0f);
        s.spread = clamp (settings.spread, 0.0f, 1.0f);
        s.feedback = clamp (settings.feedback, 0.0f, 1.0f);
        s.freeze = frozen;
        s.trigger = pendingTrigger;
        pendingTrigger = false;
        impl->processor.process (s, l, r, ol, orr);
    });
    for (int i = 0; i < n; ++i) { outL[i] = dcL.hp (outL[i]); outR[i] = dcR.hp (outR[i]); }
}

//==============================================================================
// Beads
struct BeadsScatter::Impl
{
    flt_beads::Processor processor;
};

BeadsScatter::BeadsScatter() : impl (std::make_unique<Impl>()) {}
BeadsScatter::~BeadsScatter() = default;

void BeadsScatter::prepare (double hostRate)
{
    for (int m = 0; m < 4; ++m)
        bridges[(size_t) m].prepare (hostRate, (double) flt_beads::Processor::rate (m), flt_beads::Processor::block (m));
    dcL.set (15.0f, (float) hostRate);
    dcR.set (15.0f, (float) hostRate);
    media = -1;
    reset();
}

void BeadsScatter::reset()
{
    media = std::max (0, std::min (3, settings.media));
    impl->processor.reset (media);
    pendingTrigger = false;
    frozen = false;
}

void BeadsScatter::process (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    // a new quality setting is a new module: its buffer starts empty (as when the module re-configures its codec)
    if (std::max (0, std::min (3, settings.media)) != media) reset();
    bridges[(size_t) media].run (inL, inR, outL, outR, n, [this] (const float* l, const float* r, float* ol, float* orr, int k)
    {
        flt_beads::Settings s;
        s.media = media;
        s.time = settings.time; s.size = settings.size; s.shape = settings.shape; s.pitch = settings.pitch;
        s.density = settings.density; s.feedback = settings.feedback; s.reverb = settings.reverb; s.random = settings.random;
        s.dryWet = 1.0f;                              // FLOATER mixes it (SCATTER's MIX)
        s.freeze = frozen;
        s.seed = pendingTrigger;
        s.seedPatched = settings.seeded;
        pendingTrigger = false;
        impl->processor.process (s, l, r, ol, orr, k);
    });
    for (int i = 0; i < n; ++i) { outL[i] = dcL.hp (outL[i]); outR[i] = dcR.hp (outR[i]); }
}

//==============================================================================
// CloudSeed
#ifdef METAMODULE
// SEMI port: CloudSeed allocates ~60 MB per instance (DELAY_BUFFER_SIZE 384000 floats per line, 12 lines,
// two channels); far too much for the MetaModule's shared plugin RAM. There SPACE always uses the FDN:
// this stub keeps the class and outputs silence (the module never selects it).
CloudSeedSpace::CloudSeedSpace() {}
CloudSeedSpace::~CloudSeedSpace() {}
void CloudSeedSpace::prepare (double) {}
void CloudSeedSpace::reset() {}
void CloudSeedSpace::set (float, float, float, float, float, float, float, float) {}
void CloudSeedSpace::process (const float*, const float*, float* outL, float* outR, int n)
{
    std::fill (outL, outL + n, 0.0f);
    std::fill (outR, outR + n, 0.0f);
}
#else
CloudSeedSpace::CloudSeedSpace() : cs (fl_cs_create()) {}
CloudSeedSpace::~CloudSeedSpace() { if (cs != nullptr) fl_cs_destroy (cs); }

void CloudSeedSpace::prepare (double hostRate)
{
    bridge.prepare (hostRate, 48000.0, 64);
    dirty = true;
}

void CloudSeedSpace::reset()
{
    if (cs != nullptr) fl_cs_destroy (cs);
    cs = fl_cs_create();
    dirty = true;
}

void CloudSeedSpace::set (float size, float decay, float damp, float diffusion, float modAmount, float modRate, float cross, float predelay)
{
    const std::array<float, 8> v { size, decay, damp, diffusion, modAmount, modRate, cross, predelay };
    for (size_t i = 0; i < v.size(); ++i)
        if (std::abs (v[i] - last[i]) > 0.004f) dirty = true;
    if (! dirty || cs == nullptr) return;
    last = v;
    dirty = false;
    fl_cs_params q;
    q.size = clamp (size, 0.0f, 1.0f) * 0.85f;          // CloudSeed's size runs to a second; keep it a room to a hall
    q.decay = decay > 1.0f ? 1.0f : 0.15f + 0.6f * clamp (decay, 0.0f, 1.0f);  // about 0.3 s .. 13 s; past 1 (INFINITE) its full minute
    q.high_cut = 1.0f - 0.75f * clamp (damp, 0.0f, 1.0f);
    q.low_cut = 0.1f;
    q.diffusion = clamp (diffusion, 0.0f, 1.0f);
    q.mod_amount = clamp (modAmount, 0.0f, 1.0f);
    q.mod_rate = clamp (modRate, 0.0f, 1.0f);
    q.cross_seed = clamp (cross, 0.0f, 1.0f);
    q.predelay = clamp (predelay, 0.0f, 1.0f);
    fl_cs_set (cs, &q);
}

void CloudSeedSpace::process (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    if (cs == nullptr) { std::fill (outL, outL + n, 0.0f); std::fill (outR, outR + n, 0.0f); return; }
    bridge.run (inL, inR, outL, outR, n, [this] (const float* l, const float* r, float* ol, float* orr, int b)
    {
        fl_cs_process (cs, l, r, ol, orr, b);
    });
}

#endif

} // namespace flt
