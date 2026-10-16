#include "Voices.h"

namespace flt
{
using namespace dsp;

//==============================================================================
// KICK  tune 32..90 Hz, decay 60 ms..1.6 s, color: sweep depth, click and drive.
// modA = K.PITCH (+-12 st), modB = K.DECAY (x0.25..x4)
void Kick::trigger (const Strike& s)
{
    beginDeclick();
    f0 = expMap (s.p.tune, 32.0f, 90.0f) * std::pow (2.0f, clamp (s.modA, -1.5f, 1.5f));
    const float dec = expMap (s.p.decay, 0.06f, 1.6f) * std::pow (4.0f, clamp (s.modB, -1.0f, 1.0f));
    sweep = 1.5f + 6.0f * s.p.color;                       // start this many times the pitch
    pitchEnv = 1.0f;
    pitchK = decayCoef (0.006f + 0.03f * (1.0f - s.p.color), sr);
    amp = s.vel;
    ampK = susK (decayCoef (dec * 0.3f, sr));
    drive = 1.0f + 6.0f * s.p.morph * s.p.morph;          // MORPH: drive
    click = 0.05f + 0.85f * s.p.harm;                       // HARM: click
    clickEnv = 1.0f;
    phase = 0.0;                                            // from zero: the thump comes from the dive, not a step
}

void Kick::render (float* out, int n)
{
    const float clickK = decayCoef (0.0012f, sr);
    for (int i = 0; i < n; ++i)
    {
        const float f = f0 * (1.0f + sweep * pitchEnv);
        pitchEnv *= pitchK;
        phase += f / sr;
        if (phase >= 1.0) phase -= 1.0;
        float y = std::sin (twoPi * (float) phase);
        y = soft (y * drive) / soft (drive) * amp;
        y += click * clickEnv * amp * rng.bi() * 0.5f;
        clickEnv *= clickK;
        amp *= ampK;
        out[i] += declick (y);
    }
    energy = amp;
}

//==============================================================================
// MESH  tune 120..340 Hz, decay 40..600 ms (the noise), color: tone -> noise and snap.
// modA = M.TONE (shifts color)
void Mesh::trigger (const Strike& s)
{
    beginDeclick();
    f = expMap (s.p.tune, 120.0f, 340.0f);
    const float c = clamp (s.p.color + 0.5f * s.modA, 0.0f, 1.0f);
    toneMix = 1.0f - c;
    const float dec = expMap (s.p.decay, 0.04f, 0.6f);
    toneAmp = s.vel;
    toneK = susK (decayCoef (0.05f + 0.1f * s.p.decay, sr));
    noiseAmp = s.vel;
    noiseK = susK (decayCoef (dec * 0.35f, sr));
    bend = 0.15f + 1.1f * s.p.harm;                         // HARM: snap, how far the heads bend
    bendK = decayCoef (0.004f + 0.016f * (1.0f - s.p.harm), sr);
    bp.set (expMap (s.p.morph, 1200.0f, 8000.0f) * (0.8f + 0.4f * c), 0.9f, sr);   // MORPH: the wires' band
    hp.set (380.0f, 0.7f, sr);
    p1 = p2 = 0.0;
}

void Mesh::render (float* out, int n)
{
    for (int i = 0; i < n; ++i)
    {
        const float fb = f * (1.0f + bend);
        bend *= bendK;
        p1 += fb / sr;          if (p1 >= 1.0) p1 -= 1.0;
        p2 += fb * 1.58 / sr;   if (p2 >= 1.0) p2 -= 1.0;
        const float tone = (std::sin (twoPi * (float) p1) + 0.6f * std::sin (twoPi * (float) p2)) * toneAmp;
        const float nz = hp.hp (bp.bp (rng.bi()) * 2.2f + rng.bi() * 0.25f) * noiseAmp;
        float y = tone * (0.35f + 0.65f * toneMix) + nz * (0.4f + 0.9f * (1.0f - toneMix));
        toneAmp *= toneK;
        noiseAmp *= noiseK;
        out[i] += declick (soft (y * 1.2f) * 0.8f);
    }
    energy = std::max (toneAmp, noiseAmp);
}

//==============================================================================
// FM  tune 50..1200 Hz, decay 20 ms..1.2 s, color: index (1..10) and a little feedback.
// modA = FM.INDEX, modB = FM.RATIO (moves along the ratio table)
namespace { const float ratios[] { 0.5f, 1.0f, 1.41f, 2.0f, 2.76f, 3.5f, 4.2f, 5.4f, 7.0f }; }

void Fm::trigger (const Strike& s)
{
    beginDeclick();
    f = expMap (s.p.tune, 50.0f, 1200.0f);
    const float dec = expMap (s.p.decay, 0.02f, 1.2f);
    const float ci = clamp (s.p.color + 0.5f * s.modA, 0.0f, 1.0f);
    index = 1.0f + 9.0f * ci;
    fb = 0.4f * s.p.morph;                                  // MORPH: feedback
    const float rp = clamp (s.p.harm + 0.5f * s.modB, 0.0f, 0.999f) * 8.0f;   // HARM: ratio
    const int ri = (int) rp;
    ratio = lerp (ratios[ri], ratios[ri + 1], rp - (float) ri);
    idxEnv = 1.0f;
    idxK = decayCoef (dec * 0.12f, sr);
    amp = s.vel;
    ampK = susK (decayCoef (dec * 0.3f, sr));
    pc = pm = 0.0;
    prevMod = 0.0f;
}

void Fm::render (float* out, int n)
{
    for (int i = 0; i < n; ++i)
    {
        pm += f * ratio / sr; if (pm >= 1.0) pm -= 1.0;
        const float m = std::sin (twoPi * (float) pm + fb * prevMod * pi);
        prevMod = m;
        pc += f / sr; if (pc >= 1.0) pc -= 1.0;
        const float y = std::sin (twoPi * (float) pc + index * idxEnv * m) * amp;
        idxEnv *= idxK;
        amp *= ampK;
        out[i] += declick (y * 0.8f);
    }
    energy = amp;
}

//==============================================================================
// OPTO  tune 60..1500 Hz, decay 80 ms..2.5 s (the vactrol's slow tail),
// color: wavefold and how far the gate opens. modA = OPTO.COL
void Opto::trigger (const Strike& s)
{
    beginDeclick();
    f = expMap (s.p.tune, 60.0f, 1500.0f);
    const float c = clamp (s.p.color + 0.5f * s.modA, 0.0f, 1.0f);
    fold = 1.0f + 5.0f * s.p.harm;                          // HARM: wavefold
    bright = 0.6f + 2.4f * c;                               // COLOR: how far the gate opens
    noise = 0.3f * s.p.morph;                               // MORPH: noise into the gate
    // a vactrol is lit fast and goes dark in two stages: a quick drop, then a long tail
    fast = 0.62f * s.vel;
    slow = 0.38f * s.vel;
    attack = 0.0f;
    attackK = decayCoef (0.0015f, sr);                      // a vactrol takes a millisecond or two to light
    fastK = decayCoef (0.018f, sr);
    slowK = susK (decayCoef (expMap (s.p.decay, 0.08f, 2.5f) * 0.4f, sr));
}

void Opto::render (float* out, int n)
{
    for (int i = 0; i < n; ++i)
    {
        if ((i & 15) == 0)
        {
            const float L = clamp ((fast + slow) * attack, 0.0f, 1.0f);
            const float cut = clamp (40.0f * std::pow (400.0f, L) * bright, 30.0f, sr * 0.45f);
            lp1.set (cut, sr);
            lp2.set (cut * 1.3f, sr);
        }
        phase += f / sr; if (phase >= 1.0) phase -= 1.0;
        const float tri = 4.0f * std::abs ((float) phase - 0.5f) - 1.0f;
        const float folded = std::sin (tri * fold * pi * 0.5f) + noise * rng.bi();
        attack = 1.0f + attackK * (attack - 1.0f);
        const float L = (fast + slow) * attack;
        const float y = lp2.lp (lp1.lp (folded)) * std::pow (L, 1.4f);
        fast *= fastK;
        slow *= slowK;
        out[i] += declick (y * 1.1f);
    }
    energy = fast + slow;
}

//==============================================================================
// LANTERN  tune: the metal's pitch (x0.6..x1.8), decay 8..900 ms,
// color: noise -> metal. modA = HAT.DEC (x0.25..x4)
void Lantern::trigger (const Strike& s)
{
    beginDeclick();
    static const float base[6] { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };   // the 808's six squares
    const float k = expMap (s.p.tune, 0.6f, 1.8f);
    for (int i = 0; i < 6; ++i) fr[(size_t) i] = base[i] * k * 1.7f;
    metal = s.p.color;
    amp = s.vel;
    ampK = susK (decayCoef (expMap (s.p.decay, 0.008f, 0.9f) * 0.3f * std::pow (4.0f, clamp (s.modA, -1.0f, 1.0f)), sr));
    bp.set (expMap (s.p.harm, 5000.0f, 13000.0f) * std::sqrt (k), 1.2f, sr);   // HARM: the band
    hp.set (expMap (s.p.morph, 2500.0f, 9000.0f), 0.7f, sr);                 // MORPH: air
}

void Lantern::render (float* out, int n)
{
    for (int i = 0; i < n; ++i)
    {
        float sq = 0.0f;
        for (size_t o = 0; o < 6; ++o)
        {
            ph[o] += fr[o] / sr; if (ph[o] >= 1.0) ph[o] -= 1.0;
            sq += ph[o] < 0.5 ? 1.0f : -1.0f;
        }
        const float src = sq * 0.17f * metal + rng.bi() * (1.0f - metal * 0.7f);
        const float y = hp.hp (bp.bp (src) * 1.6f + src * 0.3f) * amp;
        amp *= ampK;
        out[i] += declick (y * 0.9f);
    }
    energy = amp;
}

//==============================================================================
// ZAP  tune: where it lands, 40..800 Hz; decay 20..600 ms;
// color: how long the fall takes (2..120 ms) and how high it starts. modA = ZAP.SWP
void Zap::trigger (const Strike& s)
{
    beginDeclick();
    fEnd = expMap (s.p.tune, 40.0f, 800.0f);
    const float c = clamp (s.p.color + 0.5f * s.modA, 0.0f, 1.0f);
    ratio = 3.0f + 80.0f * s.p.morph * (0.5f + c);          // MORPH: how high it starts
    sweep = 1.0f;
    sweepK = decayCoef (expMap (c, 0.002f, 0.12f), sr);
    amp = s.vel;
    ampK = susK (decayCoef (expMap (s.p.decay, 0.02f, 0.6f) * 0.3f, sr));
    ring = 0.6f * s.p.harm;                                 // HARM: ring modulation
    phase = 0.0;
}

void Zap::render (float* out, int n)
{
    for (int i = 0; i < n; ++i)
    {
        const float fr = clamp (fEnd * (1.0f + ratio * sweep), 10.0f, sr * 0.45f);
        sweep *= sweepK;
        phase += fr / sr; if (phase >= 1.0) phase -= 1.0;
        float y = std::sin (twoPi * (float) phase);
        y *= 1.0f - ring + ring * std::sin (twoPi * (float) phase * 2.71f);
        y *= amp;
        amp *= ampK;
        out[i] += declick (y * 0.8f);
    }
    energy = amp;
}

//==============================================================================
// STRIKE  tune: root 40..400 Hz (on a semitone), decay: string feedback (0.2..8 s),
// color: brightness. modA = STR.NOTE (+-7 scale degrees), modB = STR.BRT
float StrikeVoice::degreeToSemis (int scale, int degree)
{
    static const int s0[] { 0, 3, 5, 7, 10 }, s1[] { 0, 2, 4, 7, 9 }, s2[] { 0, 2, 3, 5, 7, 9, 10 },
                     s3[] { 0, 2, 4, 6, 8, 10 }, s4[] { 0, 2, 3, 7, 8 }, s5[] { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    const int* t; int len;
    switch (scale)
    {
        case 1: t = s1; len = 5; break;
        case 2: t = s2; len = 7; break;
        case 3: t = s3; len = 6; break;
        case 4: t = s4; len = 5; break;
        case 5: t = s5; len = 12; break;
        default: t = s0; len = 5; break;
    }
    const int oct = degree >= 0 ? degree / len : -((-degree + len - 1) / len);
    const int k = degree - oct * len;
    return (float) (oct * 12 + t[k]);
}

void StrikeVoice::prepare (float sampleRate)
{
    VoiceBase::prepare (sampleRate);
    dcBlock.set (18.0f, sampleRate);
    for (auto& s : strings) { s.buf.assign (kLen, 0.0f); s.w = 0; s.z = 0; s.exciteLeft = 0; }
}

void StrikeVoice::trigger (const Strike& s)
{
    const float root = std::round (12.0f * std::log2 (expMap (s.p.tune, 40.0f, 400.0f) / 40.0f));   // semitones above 40 Hz
    const int degree = (int) std::lround (clamp (s.modA, -1.5f, 1.5f) * 7.0f);
    lastDegree = degree;
    const float t60 = expMap (s.p.decay, 0.2f, 8.0f);
    const float bright = clamp (s.p.color + 0.5f * s.modB, 0.0f, 1.0f);
    for (int k = 0; k < 3; ++k)
    {
        auto& st = strings[(size_t) next];
        next = (next + 1) % kStrings;
        const int step = (int) std::lround (s.p.harm * 4.0f);   // HARM: unison, seconds, triads, fourths, wide
        const float semis = root + degreeToSemis (s.scale, degree + step * k);
        const float hz = 40.0f * std::pow (2.0f, semis / 12.0f);
        st.delay = clamp (sr / hz, 2.0f, (float) kLen - 4.0f);
        st.gain = sustain ? 0.99995f : std::pow (0.001f, st.delay / (t60 * sr));   // SUSTAIN: the strings ring on
        st.damp = 0.15f + 0.8f * bright;
        st.exciteLeft = (int) (0.004f * sr);
        st.startIn = (int) ((float) k * 0.04f * s.p.morph * sr);   // MORPH: the strum
        st.exciteAmp = s.vel * (k == 0 ? 1.0f : 0.8f);
        st.exLp.set (600.0f + 9000.0f * bright, sr);
    }
    ringing = 1.0f;
    quiet = 0;
}

void StrikeVoice::render (float* out, int n)
{
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        float y = 0.0f;
        for (auto& st : strings)
        {
            float ex = 0.0f;
            if (st.startIn > 0) --st.startIn;
            else if (st.exciteLeft > 0) { ex = st.exLp.lp (rng.bi()) * st.exciteAmp; --st.exciteLeft; }
            float rp = (float) st.w - st.delay;
            while (rp < 0.0f) rp += (float) kLen;
            const int i0 = (int) rp;
            const float fr = rp - (float) i0;
            const float a = st.buf[(size_t) (i0 % kLen)], b = st.buf[(size_t) ((i0 + 1) % kLen)];
            const float d = a + (b - a) * fr;
            // loop filter: one-pole low pass, brightness = damp
            st.z = st.z + st.damp * (d - st.z);
            const float v = st.z * st.gain + ex;
            st.buf[(size_t) st.w] = v;
            st.w = (st.w + 1) % kLen;
            y += d;
        }
        y = soft (dcBlock.hp (y) * 0.6f);
        peak = std::max (peak, std::abs (y));
        out[i] += y;
    }
    // the strings are silent only once nothing has come out for longer than the longest string
    quiet = peak > 1.0e-5f ? 0 : quiet + n;
    ringing = quiet < kLen + 2048 ? 1.0f : 0.0f;
    energy = std::min (1.0f, peak * 1.5f);
}

} // namespace flt
