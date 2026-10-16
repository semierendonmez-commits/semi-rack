// SEMI port: FLOATER v0.7 ThirdParty/beads/floater_beads.h, namespace floater_beads -> flt_beads (nothing else changed).
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

namespace flt_beads
{

struct Settings
{
    int media = 0;              // 0..3, as above
    float time = 0.5f;          // TIME: how far back, 0..1 of the last 4 seconds in every quality (or the delay time)
    float size = 0.5f;          // SIZE, FLOATER's way: 0 the shortest grains (8 ms) .. 1 the longest (4 s), all the way
                                // up the delay mode. (Beads' own knob runs long reversed grains, shortest at 1/3, long
                                // forward ones; FLOATER uses its forward third to two thirds and up.)
    float shape = 0.5f;         // SHAPE: the grain envelope
    float pitch = 0.0f;         // PITCH in semitones (-24..24)
    float density = 0.5f;       // DENSITY, FLOATER's way: every seed is a grain, and DENSITY adds Beads' own grains
                                // between them at random: none at 0, about 4 a second at 0.5, about 40 at 1.
                                // (In the delay mode it is Beads' own DENSITY: the repeats.)
    float feedback = 0.0f;      // FEEDBACK
    float dryWet = 1.0f;        // DRY/WET
    float reverb = 0.0f;        // REVERB
    float random = 0.0f;        // the attenurandomizers turned left: how far TIME / SIZE / SHAPE / PITCH wander
    bool freeze = false;        // FREEZE
    bool seed = false;          // a trigger on SEED at the start of this block
    bool seedPatched = true;    // false: lane 8 is not playing (its seeds are ignored; DENSITY's grains go on)
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
    unsigned rng = 0x2545f491u; // Beads' own seeds (DENSITY)
    double recorded = 0.0;      // seconds in the buffer since the last reset
};

} // namespace flt_beads
