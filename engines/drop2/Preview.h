// A small preview kit so drop 2 can be heard without routing MIDI anywhere:
// a sine kick, a noise snare, a noise hat, a tone perc, and two mono basses
// (CALL: saw + sub, RESPONSE: pulse, brighter). Plain C++, no allocation in render.
#pragma once

#include "Clip.h"

namespace dr2
{

class Preview
{
public:
    enum Lane { Drums = 0, BassA = 1, BassB = 2 };

    void prepare (double sampleRate) { sr = sampleRate; reset(); }
    void reset()
    {
        for (auto& v : drums) v.active = false;
        for (auto& b : bass) { b.gate = false; b.env = 0.0f; b.lp = 0.0f; }
    }

    void noteOn (Lane lane, Role role, int pitch, float vel)
    {
        if (lane == Drums)
        {
            Drum* v = &drums[0];
            for (auto& d : drums) if (! d.active) { v = &d; break; }
            if (v->active) for (auto& d : drums) if (d.age > v->age) v = &d;   // steal the oldest
            *v = Drum {};
            v->active = true;
            v->role = role;
            v->vel = vel;
            v->pitch = pitch;
        }
        else
        {
            auto& b = bass[lane == BassA ? 0 : 1];
            b.gate = true;
            b.pitch = pitch;
            b.vel = vel;
            b.freq = 440.0 * std::pow (2.0, (pitch - 69) / 12.0);
            b.fenv = 1.0f;
        }
    }

    void noteOff (Lane lane, int pitch)
    {
        if (lane == Drums) return;
        auto& b = bass[lane == BassA ? 0 : 1];
        if (b.pitch == pitch) b.gate = false;
    }

    void render (float* L, float* R, int from, int to, float level)
    {
        const float dt = (float) (1.0 / sr);
        for (int i = from; i < to; ++i)
        {
            float x = 0.0f;
            for (auto& d : drums)
            {
                if (! d.active) continue;
                const float t = d.age;
                float y = 0.0f;
                switch (d.role)
                {
                    case Role::Kick:
                    {
                        const float f = 45.0f + 120.0f * std::exp (-t * 38.0f);
                        d.phase += f * dt;
                        y = std::sin (6.2831853f * d.phase) * std::exp (-t * 7.5f) * 1.1f;
                        if (t > 0.6f) d.active = false;
                        break;
                    }
                    case Role::Snare:
                    {
                        d.phase += 185.0f * dt;
                        const float n = noise();
                        d.hp += 0.35f * (n - d.hp);
                        y = (0.35f * std::sin (6.2831853f * d.phase) * std::exp (-t * 30.0f) + 0.7f * (n - d.hp) * std::exp (-t * 16.0f));
                        if (t > 0.4f) d.active = false;
                        break;
                    }
                    case Role::Hat:
                    {
                        const float n = noise();
                        d.hp += 0.6f * (n - d.hp);
                        y = 0.35f * (n - d.hp) * std::exp (-t * (d.pitch == 46 ? 9.0f : 55.0f));
                        if (t > 0.5f) d.active = false;
                        break;
                    }
                    default:
                    {
                        d.phase += (220.0f + 8.0f * (float) (d.pitch % 24)) * dt;
                        y = 0.4f * std::sin (6.2831853f * d.phase) * std::exp (-t * 20.0f);
                        if (t > 0.3f) d.active = false;
                        break;
                    }
                }
                x += y * d.vel;
                d.age += dt;
            }

            for (int k = 0; k < 2; ++k)
            {
                auto& b = bass[k];
                const float target = b.gate ? 1.0f : 0.0f;
                b.env += (target - b.env) * (b.gate ? 0.02f : 0.004f);
                if (b.env < 1.0e-4f && ! b.gate) continue;
                b.phase += (float) (b.freq * dt);
                b.phase -= std::floor (b.phase);
                float osc;
                if (k == 0) osc = 0.55f * (2.0f * b.phase - 1.0f) + 0.6f * std::sin (6.2831853f * b.phase);
                else        osc = 0.5f * (b.phase < 0.3f ? 1.0f : -1.0f);
                b.fenv *= 0.9996f;
                const float cut = (k == 0 ? 0.03f : 0.08f) + 0.18f * b.fenv;
                b.lp += cut * (osc - b.lp);
                x += b.lp * b.env * b.vel * 0.7f;
            }

            x = std::tanh (x * level);
            L[i] += x;
            R[i] += x;
        }
    }

private:
    struct Drum { bool active = false; Role role = Role::Kick; float vel = 1, age = 0, phase = 0, hp = 0; int pitch = 36; };
    struct Bass { bool gate = false; int pitch = 36; float vel = 1, env = 0, phase = 0, lp = 0, fenv = 0; double freq = 55; };

    float noise() { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 8388608.0f - 1.0f; }

    double sr = 48000.0;
    std::array<Drum, 12> drums {};
    std::array<Bass, 2> bass {};
    std::uint32_t seed = 1;
};

} // namespace dr2
