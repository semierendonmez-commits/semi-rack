// SEMI Family port: vendored from semierendonmez-commits/snarestack Source/engine (0fa7677). Changes:
// namespace gtbs -> ss::gtbs (the break-slicer engines in the binary use gtbs too), and Player::uses()
// added (the module frees a replaced stack once no voice plays it any more).
// The real-time part of SNARESTACK: MIDI notes play the stack, the balance pad
// re-weights crack / body / tail per hit, velocity and VARY move it a little.
// Allocation free once prepared.
#pragma once

#include "Stack.h"
#include <atomic>

namespace ss
{

struct PlayParams
{
    float bx = 0.5f, by = 2.0f / 3.0f;   // balance pad
    float depthDb = 12.0f;
    float pitch = 0.0f;                   // semitones
    float decay = 1.0f;                   // 0 short .. 1 untouched
    float velSens = 0.8f;                 // 0..1
    float velCrack = 0.5f;                // 0..1: harder hits get more crack
    float vary = 0.0f;                    // 0..1: per-hit drift of the balance and the pitch
    bool keytrack = false;
    bool choke = false;
    float gain = 1.0f;
    uint32_t seed = 1;
};

struct Note { int offset = 0; uint8_t note = 0, vel = 0; };

class Player
{
public:
    static constexpr int kVoices = 12;

    void prepare (double sampleRate);
    void reset();
    void process (const Stack* s, const PlayParams& p, const Note* in, int numIn, float* L, float* R, int n);
    /** SEMI Family port: true while a voice still plays s (audio thread) */
    bool uses (const Stack* s) const { for (const auto& v : voices) if (v.active && v.s == s) return true; return false; }

    // views
    std::atomic<int> hits { 0 };
    std::atomic<float> lastVel { 0.0f };
    std::array<std::atomic<float>, NumComps> lastGainDb {};
    std::atomic<float> meter { 0.0f };    // output peak, decays

private:
    struct Voice
    {
        bool active = false;
        const Stack* s = nullptr;
        double pos = 0.0, inc = 1.0;
        std::array<float, NumComps> g {};
        float env = 1.0f, decayMul = 1.0f;
        int fade = -1, fadeLen = 64;
        int rampPos = 0, rampLen = 48;   // the millisecond before the attack fades in
        uint32_t age = 0;
    };
    void start (const Stack& s, const PlayParams& p, int note, int vel);

    double sr = 48000.0;
    std::array<Voice, kVoices> voices {};
    uint32_t ageCounter = 0, rnd = 1;
    float meterV = 0.0f;
};

} // namespace ss
