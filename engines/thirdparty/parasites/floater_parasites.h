// FLOATER's wrapper around two modes of the Parasites firmware for Clouds (Matthias Puech,
// on Emilie Gillet's code, MIT): OLIVERB and RESONESTOR. They run at Clouds' own 32 kHz in its
// 32-frame blocks; the caller bridges the host rate.
//
// The Parasites sources are compiled here in their own namespace (the token `clouds` renamed to
// `parasites_clouds` around the includes), so their fx_engine / frame / resources, which differ
// from the Move port of Clouds that FLOATER also contains, never meet it. The sources are unchanged.
#pragma once

#include <memory>

namespace floater_parasites
{

inline constexpr int kRate = 32000;
inline constexpr int kBlock = 32;

struct Settings
{
    int mode = 0;            // 0 OLIVERB, 1 RESONESTOR
    float position = 0.5f;   // OLIVERB: pre-delay; RESONESTOR: burst damping / comb / length
    float size = 0.5f;       // OLIVERB: room size; RESONESTOR: chord
    float pitch = 0.0f;      // semitones -24..24: OLIVERB's shimmer, RESONESTOR's root
    float density = 0.5f;    // OLIVERB: decay; RESONESTOR: feedback (how long the combs ring)
    float texture = 0.5f;    // tilt (OLIVERB: low pass / high pass; RESONESTOR: damping / narrowing)
    float spread = 0.5f;     // OLIVERB: diffusion; RESONESTOR: stereo / separation
    float feedback = 0.0f;   // OLIVERB: modulation; RESONESTOR: harmonicity and distortion
    bool freeze = false;
    bool trigger = false;    // RESONESTOR: a burst into the combs (lane 8)
};

class Processor
{
public:
    Processor();
    ~Processor();
    void reset (int mode);
    // exactly kBlock frames at kRate, wet only
    void process (const Settings& s, const float* inL, const float* inR, float* outL, float* outR);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace floater_parasites
