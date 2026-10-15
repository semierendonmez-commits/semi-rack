// FLOATER's bridge to Mutable Instruments Beads (the texture synthesizer, MIT, open sourced on
// 12 October 2026). Plain C++ on both sides: the Beads sources stay unchanged and are compiled in their
// own library; FLOATER sees only this.
//
// The firmware runs its codec at a rate that depends on the quality setting (Beads' four "media"):
//   0 HI-FI      48 kHz, 16-bit buffer
//   1 CLEAN TAPE 48 kHz in and out, the grains at 24 kHz, 12-bit, soft clipping
//   2 LO-FI      32 kHz, 12-bit (the "vintage digital" setting)
//   3 DIRTY TAPE 24 kHz, 8-bit mu-law, wow and flutter, hard soft clipping
// rate() and block() say what to feed it (blocks of codec rate / 4000, as on the module).
#pragma once

#include <memory>

namespace floater_beads
{

struct Settings
{
    int media = 0;              // 0..3, as above
    float time = 0.5f;          // TIME: where in the buffer (or the delay time)
    float size = 0.5f;          // SIZE: grain length; fully clockwise the delay mode
    float shape = 0.5f;         // SHAPE: the grain envelope
    float pitch = 0.0f;         // PITCH in semitones (-24..24)
    float density = 0.5f;       // DENSITY: seeded, fully left every seed, towards noon every 2nd .. 16th, noon none,
                                //          right of noon a random share; unseeded, noon none, left regular grains,
                                //          right random ones, faster outwards
    float feedback = 0.0f;      // FEEDBACK
    float dryWet = 1.0f;        // DRY/WET
    float reverb = 0.0f;        // REVERB
    float random = 0.0f;        // the attenurandomizers turned left: how far TIME / SIZE / SHAPE / PITCH wander
    bool freeze = false;        // FREEZE
    bool seed = false;          // a trigger on SEED at the start of this block
    bool seedPatched = true;    // false: nothing in SEED, Beads seeds its own grains from DENSITY
};

class Processor
{
public:
    Processor();
    ~Processor();
    static float rate (int media);
    static int block (int media);
    void reset (int media);
    // n = block (media) frames at rate (media)
    void process (const Settings& s, const float* inL, const float* inR, float* outL, float* outR, int n);
    int media() const { return currentMedia; }
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    int currentMedia = -1;
    int seedHigh = 0;           // frames left of the seed gate
};

} // namespace floater_beads
