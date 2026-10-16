// SEMI port: FLOATER v0.7 ThirdParty/beads/floater_beads.cc, namespace floater_beads -> flt_beads (nothing else changed).
// See flt_beads.h. Compiled on its own (FloaterBeads), with ThirdParty/ on the include path.
#include "FloaterBeads.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "beads/dsp/processor.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/utils/gate_flags.h"

namespace flt_beads
{

namespace
{
constexpr int kMaxBlock = 24;   // the firmware's kMaxCodecBlockSize
// FLOATER's order (by fidelity) to Beads' enum
beads::Processor::Media toMedia (int m)
{
    switch (std::max (0, std::min (3, m)))
    {
        case 0: return beads::Processor::MEDIA_DIGITAL;
        case 1: return beads::Processor::MEDIA_CLEAN_TAPE;
        case 2: return beads::Processor::MEDIA_VINTAGE_DIGITAL;
        default: return beads::Processor::MEDIA_DIRTY_TAPE;
    }
}
}

struct Processor::Impl
{
    // the module's memories: 512 + 256 KB of delay RAM, 64 KB for the reverb (as in beads.cc)
    std::vector<uint8_t> ram1 = std::vector<uint8_t> (512 * 1024, 0), ram2 = std::vector<uint8_t> (256 * 1024, 0);
    std::vector<uint16_t> reverbRam = std::vector<uint16_t> (32768, 0);
    beads::Processor processor;
    stmlib::GateFlags previousSeed = stmlib::GATE_FLAG_LOW;
};

Processor::Processor() : impl (std::make_unique<Impl>())
{
    uint8_t* buffers[3] { impl->ram1.data(), impl->ram1.data() + 256 * 1024, impl->ram2.data() };
    impl->processor.Init (buffers, impl->reverbRam.data());
    reset (0);
}

Processor::~Processor() = default;

float Processor::rate (int media)
{
    static const float r[4] { 48000.0f, 48000.0f, 32000.0f, 24000.0f };
    return r[std::max (0, std::min (3, media))];
}

int Processor::block (int media) { return (int) rate (media) / 4000; }

void Processor::reset (int media)
{
    currentMedia = std::max (0, std::min (3, media));
    std::fill (impl->ram1.begin(), impl->ram1.end(), 0);
    std::fill (impl->ram2.begin(), impl->ram2.end(), 0);
    std::fill (impl->reverbRam.begin(), impl->reverbRam.end(), 0);
    impl->processor.Configure (toMedia (currentMedia), false);
    impl->previousSeed = stmlib::GATE_FLAG_LOW;
    seedHigh = 0;
    recorded = 0.0;
}

void Processor::process (const Settings& s, const float* inL, const float* inR, float* outL, float* outR, int n)
{
    n = std::min (n, kMaxBlock);
    beads::FloatFrame in[kMaxBlock], out[kMaxBlock];
    bool freeze[kMaxBlock];
    stmlib::GateFlags seed[kMaxBlock];
    // the tape media clip softly on the way in, as the module's input stage does for them
    const bool tape = currentMedia == 1 || currentMedia == 3;
    for (int i = 0; i < n; ++i)
    {
        in[i].l = tape ? stmlib::SoftClip (inL[i]) : inL[i];
        in[i].r = tape ? stmlib::SoftClip (inR[i]) : inR[i];
        freeze[i] = s.freeze;
    }
    // DENSITY. Beads' own knob is silent at noon and runs two ways from it, and with a clock in SEED it only
    // divides that clock: a sparse lane 8 left it silent for seconds. FLOATER's way: every hit of lane 8 is a
    // grain, and DENSITY adds Beads' own grains between them, at random, from none (0) through about 4 a
    // second (0.5) to about 40 (1). Both go into SEED, latched, with Beads' divider at 1 (every pulse a grain).
    // In the delay mode (SIZE all the way up) DENSITY is Beads' own again: it picks the repeats.
    const float dial = std::max (0.0f, std::min (1.0f, s.density));
    const bool delayMode = s.size > 0.985f;
    const float beadsDensity = delayMode ? dial : 0.0f;
    const float ownRate = (delayMode || dial < 0.001f) ? 0.0f : 0.5f * std::pow (2.0f, dial * 6.3f);   // grains a second
    const float ownChance = ownRate / rate (currentMedia);
    // SEED: a 1 ms gate for each seed, lane 8's at the start of the block, Beads' own anywhere in it
    const int gateLength = std::max (2, (int) (rate (currentMedia) * 0.001f));
    if (s.seed && s.seedPatched) seedHigh = gateLength;
    for (int i = 0; i < n; ++i)
    {
        rng = rng * 1664525u + 1013904223u;
        if (ownChance > 0.0f && seedHigh <= 0 && (float) (rng >> 8) * (1.0f / 16777216.0f) < ownChance) seedHigh = gateLength;
        const bool high = seedHigh > 0;
        if (seedHigh > 0) --seedHigh;
        impl->previousSeed = seed[i] = stmlib::ExtractGateFlags (impl->previousSeed, high);
    }

    // the four controls with their attenurandomizers turned left by RANDOM (no CV patched)
    auto rp = [&] (float base, float randomness)
    {
        beads::RandomizedParameter p;
        p.base = base; p.attenuverter = -randomness; p.cv = 0.0f; p.has_cv = false;
        return p;
    };
    const float r = 0.5f * std::max (0.0f, std::min (1.0f, s.random));
    beads::Parameters p;
    // TIME: the same seconds in every quality (the buffers hold 4, 10.7, 8 and 16 s): 0..1 is the last 4 s,
    // and never further back than what has been recorded since the start (so it is never reading silence)
    static const float bufferSeconds[4] { 4.0f, 10.667f, 8.0f, 16.0f };
    if (! s.freeze) recorded = std::min (recorded + (double) n / rate (currentMedia), 60.0);
    const float back = std::min (std::max (0.0f, std::min (1.0f, s.time)) * 4.0f, (float) recorded * 0.9f);
    p.time = rp (back / bufferSeconds[currentMedia], r);
    p.size = rp (0.33334f + 0.66666f * std::max (0.0f, std::min (1.0f, s.size)), r);
    p.shape = rp (std::max (0.0f, std::min (1.0f, s.shape)), r);
    p.pitch = rp (std::max (-24.0f, std::min (24.0f, s.pitch)), 0.3f * r);
    p.monophonic_mode = false;
    beads::ModulatedParameter d;
    d.base = beadsDensity;
    d.cv = 0.0f;
    beads::Processor::MixParameters m;
    m.feedback = std::max (0.0f, std::min (1.0f, s.feedback));
    m.dry_wet = std::max (0.0f, std::min (1.0f, s.dryWet));
    m.reverb_amount = std::max (0.0f, std::min (1.0f, s.reverb));
    m.output_gate = false;
    impl->processor.Process (beads::SEED_BUTTON_STATE_LOCKED, seed, freeze, d, p, m, in, out, (size_t) n);
    for (int i = 0; i < n; ++i) { outL[i] = out[i].l; outR[i] = out[i].r; }
}

} // namespace flt_beads
