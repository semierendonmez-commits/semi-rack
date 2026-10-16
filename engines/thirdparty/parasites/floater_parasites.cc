// See floater_parasites.h. Compiled with ThirdParty/parasites ahead of ThirdParty on the include
// path, so "clouds/..." below resolves to the Parasites copies.
#include "floater_parasites.h"

#include <algorithm>
#include <cmath>
#include <vector>

// Parasites was built against a newer stmlib whose headers pulled filter.h in on their own
#include "stmlib/dsp/filter.h"

#define clouds parasites_clouds
#include "clouds/dsp/frame.h"
#include "clouds/dsp/fx/oliverb.h"
#include "clouds/dsp/resonestor.h"
#undef clouds

namespace floater_parasites
{
namespace pc = parasites_clouds;

struct Processor::Impl
{
    pc::Oliverb oliverb;
    pc::Resonestor resonestor;
    std::vector<uint16_t> reverbMemory = std::vector<uint16_t> (16384, 0);
    std::vector<float> combMemory = std::vector<float> (16384, 0.0f);
    // OLIVERB's pre-delay (on the module it is a WSOLA read of the record buffer at POSITION)
    std::vector<float> preL = std::vector<float> (8192, 0.0f), preR = std::vector<float> (8192, 0.0f);
    int preW = 0;
    float preTime = 0.0f;
    int mode = -1;
    pc::FloatFrame frames[kBlock];
};

Processor::Processor() : impl (std::make_unique<Impl>()) { reset (0); }
Processor::~Processor() = default;

void Processor::reset (int mode)
{
    auto& I = *impl;
    I.mode = std::clamp (mode, 0, 1);
    std::fill (I.reverbMemory.begin(), I.reverbMemory.end(), (uint16_t) 0);
    std::fill (I.combMemory.begin(), I.combMemory.end(), 0.0f);
    std::fill (I.preL.begin(), I.preL.end(), 0.0f);
    std::fill (I.preR.begin(), I.preR.end(), 0.0f);
    I.oliverb.Init (I.reverbMemory.data());
    I.resonestor.Init (I.combMemory.data());
}

namespace
{
// the Parasites firmware's own parameter mapping (clouds/dsp/granular_processor.cc, OLIVERB case)
void setOliverb (pc::Oliverb& o, const Settings& s)
{
    o.set_diffusion (0.3f + 0.5f * s.spread);
    o.set_size (0.05f + 0.94f * s.size);
    o.set_mod_rate (s.feedback);
    o.set_mod_amount (s.feedback * 0.6f * 300.0f);
    o.set_ratio (stmlib::SemitonesToRatio (s.pitch));
    const float x = s.pitch, limit = 0.7f, slew = 0.4f;
    const float wet = x < -limit ? 1.0f : x < -limit + slew ? 1.0f - (x + limit) / slew
                    : x < limit - slew ? 0.0f : x < limit ? 1.0f + (x - limit) / slew : 1.0f;
    o.set_pitch_shift_amount (wet);
    if (s.freeze)
    {
        o.set_input_gain (0.0f);
        o.set_decay (1.0f);
        o.set_lp (1.0f);
        o.set_hp (0.0f);
        return;
    }
    o.set_decay (s.density * 1.3f + 0.15f * std::abs (s.pitch) / 24.0f);
    o.set_input_gain (0.5f);
    const float lp = s.texture < 0.5f ? s.texture * 2.0f : 1.0f;
    const float hp = s.texture > 0.5f ? (s.texture - 0.5f) * 2.0f : 0.0f;
    o.set_lp (0.03f + 0.9f * lp);
    o.set_hp (0.01f + 0.2f * hp);
}

// (RESONESTOR case)
void setResonestor (pc::Resonestor& r, const Settings& s)
{
    r.set_pitch (s.pitch);
    r.set_chord (s.size);
    r.set_trigger (s.trigger);
    r.set_burst_damp (s.position);
    r.set_burst_comb (1.0f - s.position);
    r.set_burst_duration (1.0f - s.position);
    r.set_spread_amount (0.25f);
    r.set_stereo (s.spread < 0.5f ? 0.0f : (s.spread - 0.5f) * 2.0f);
    r.set_separation (s.spread > 0.5f ? 0.0f : (0.5f - s.spread) * 2.0f);
    r.set_freeze (s.freeze ? 1.0f : 0.0f);
    r.set_harmonicity (1.0f - s.feedback * 0.5f);
    r.set_distortion (s.feedback * 0.5f);
    const float t = s.texture;
    if (t < 0.5f)
    {
        r.set_narrow (0.001f);
        float l = 1.0f - (0.5f - t) / 0.5f;
        l = l * (1.0f - 0.08f) + 0.08f;
        r.set_damp (l * l);
    }
    else
    {
        r.set_damp (1.0f);
        float n = (t - 0.5f) / 0.5f * 1.35f;
        n *= n * n * n;
        r.set_narrow (0.001f + n * n * 0.6f);
    }
    float d = std::max (0.0f, (s.density - 0.05f) / 0.9f);
    d *= d * d; d *= d * d; d *= d * d;
    r.set_feedback (d * 20.0f);
}
}

void Processor::process (const Settings& s, const float* inL, const float* inR, float* outL, float* outR)
{
    auto& I = *impl;
    if (std::clamp (s.mode, 0, 1) != I.mode) reset (s.mode);
    if (I.mode == 0)
    {
        // pre-delay up to a quarter second, gliding so a moving POSITION does not click
        const float target = std::clamp (s.position, 0.0f, 1.0f) * 0.25f * (float) kRate;
        const int len = (int) I.preL.size();
        for (int i = 0; i < kBlock; ++i)
        {
            I.preL[(size_t) I.preW] = inL[i];
            I.preR[(size_t) I.preW] = inR[i];
            I.preTime += (target - I.preTime) * 0.002f;
            const int rd = (I.preW - (int) I.preTime + len) % len;
            I.frames[i].l = I.preL[(size_t) rd];
            I.frames[i].r = I.preR[(size_t) rd];
            I.preW = (I.preW + 1) % len;
        }
        setOliverb (I.oliverb, s);
        I.oliverb.Process (I.frames, kBlock);
    }
    else
    {
        for (int i = 0; i < kBlock; ++i) { I.frames[i].l = inL[i]; I.frames[i].r = inR[i]; }
        setResonestor (I.resonestor, s);
        I.resonestor.Process (I.frames, kBlock);
    }
    for (int i = 0; i < kBlock; ++i) { outL[i] = I.frames[i].l; outR[i] = I.frames[i].r; }
}

} // namespace floater_parasites
