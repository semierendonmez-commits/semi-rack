// SEMI Family port: vendored from semierendonmez-commits/snarestack Source/engine (0fa7677). Only change:
// namespace gtbs -> ss::gtbs (the break-slicer engines in the binary use gtbs too).
#include "Player.h"
#include <algorithm>

namespace ss
{

namespace
{
inline float uniform (uint32_t& s)   // xorshift, -1..1
{
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return (float) (s & 0xFFFFFF) / (float) 0x7FFFFF - 1.0f;
}
}

void Player::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    reset();
}

void Player::reset()
{
    for (auto& v : voices) v.active = false;
    meterV = 0.0f;
}

void Player::start (const Stack& s, const PlayParams& p, int note, int vel)
{
    const int fadeLen = std::max (16, (int) (sr * 0.002));
    if (p.choke)
        for (auto& v : voices) if (v.active && (v.fade < 0 || v.fade > fadeLen)) { v.fade = fadeLen; v.fadeLen = fadeLen; }

    Voice* use = nullptr;
    for (auto& v : voices) if (! v.active) { use = &v; break; }
    if (use == nullptr) { use = &voices[0]; for (auto& v : voices) if (v.age < use->age) use = &v; }

    if (rnd == 0) rnd = p.seed * 2654435761u + 1u;
    const float vn = std::clamp ((float) vel / 127.0f, 0.0f, 1.0f);
    auto gdb = balanceGains (p.bx, p.by, p.depthDb);
    gdb[Crack] += p.velCrack * 12.0f * (vn - 0.8f);
    if (p.vary > 0.0f)
        for (auto& g : gdb) g += p.vary * 2.0f * uniform (rnd);
    const float amp = (1.0f - p.velSens) + p.velSens * std::pow (vn, 1.3f);
    float semis = p.pitch + (p.keytrack ? (float) (note - 60) : 0.0f);
    if (p.vary > 0.0f) semis += p.vary * 0.25f * uniform (rnd);

    Voice& v = *use;
    v.active = true;
    v.s = &s;
    v.pos = std::max (0.0, (double) s.onset - s.rate * 0.001);   // skip all but 1 ms of the pre-roll
    v.inc = s.rate / sr * std::pow (2.0, (double) semis / 12.0);
    for (int k = 0; k < NumComps; ++k) v.g[(size_t) k] = amp * std::pow (10.0f, gdb[(size_t) k] / 20.0f);
    v.env = 1.0f;
    v.decayMul = p.decay >= 0.999f ? 1.0f : (float) std::exp (-1.0 / (0.02 * std::pow (50.0, (double) p.decay) * sr));
    v.fade = -1;
    v.fadeLen = fadeLen;
    v.rampPos = 0;
    v.rampLen = std::max (8, (int) (sr * 0.001));
    v.age = ++ageCounter;

    hits.fetch_add (1, std::memory_order_relaxed);
    lastVel.store (vn, std::memory_order_relaxed);
    for (int k = 0; k < NumComps; ++k) lastGainDb[(size_t) k].store (gdb[(size_t) k], std::memory_order_relaxed);
}

void Player::process (const Stack* s, const PlayParams& p, const Note* in, int numIn, float* L, float* R, int n)
{
    std::fill (L, L + n, 0.0f);
    std::fill (R, R + n, 0.0f);
    int e = 0, done = 0;
    auto renderTo = [&] (int to)
    {
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const auto& st = *v.s;
            const double end = (double) st.frames - 2.0;
            const float* c0 = st.cL[Crack].data(); const float* c1 = st.cL[Body].data(); const float* c2 = st.cL[Tail].data();
            const float* d0 = st.cR[Crack].data(); const float* d1 = st.cR[Body].data(); const float* d2 = st.cR[Tail].data();
            for (int i = done; i < to; ++i)
            {
                if (v.pos >= end) { v.active = false; break; }
                const int ip = (int) v.pos;
                const float fr = (float) (v.pos - ip);
                auto at = [ip, fr] (const float* x) { return x[ip] + fr * (x[ip + 1] - x[ip]); };
                float gl = v.g[Crack] * at (c0) + v.g[Body] * at (c1) + v.g[Tail] * at (c2);
                float gr = v.g[Crack] * at (d0) + v.g[Body] * at (d1) + v.g[Tail] * at (d2);
                float g = v.env * p.gain;
                if (v.rampPos < v.rampLen) g *= (float) (v.rampPos++) / (float) v.rampLen;
                if (v.fade >= 0)
                {
                    g *= (float) v.fade / (float) v.fadeLen;
                    if (--v.fade <= 0) { v.active = false; break; }
                }
                L[i] += g * gl;
                R[i] += g * gr;
                if (v.pos > (double) st.onset) v.env *= v.decayMul;
                v.pos += v.inc;
            }
        }
        done = to;
    };
    for (; e < numIn; ++e)
    {
        const auto& nt = in[e];
        renderTo (std::clamp (nt.offset, done, n));
        if (nt.vel > 0 && s != nullptr && s->frames > 4) start (*s, p, nt.note, nt.vel);
    }
    renderTo (n);

    float pk = 0.0f;
    for (int i = 0; i < n; ++i) pk = std::max (pk, std::max (std::abs (L[i]), std::abs (R[i])));
    meterV = std::max (pk, meterV * (float) std::exp (-(double) n / (sr * 0.25)));
    meter.store (meterV, std::memory_order_relaxed);
}

} // namespace ss
