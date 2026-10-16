#include "Drone.h"
#include <algorithm>
#include <cmath>

namespace flt
{
using namespace dsp;

void Drone::prepare (double sampleRate)
{
    sr = sampleRate;
    glideK = decayCoef (0.03f, (float) sr);
    duckK = decayCoef (0.22f, (float) sr);
    lpgK = decayCoef (1.1f, (float) sr);
    pingK = decayCoef (0.35f, (float) sr);
    foldK = decayCoef (0.28f, (float) sr);
    followK = decayCoef (0.12f, (float) sr);
    // the decimation filters run at twice the rate: a steep low pass under the host's Nyquist
    aaA.set ((float) sr * 0.42f, 0.54f, (float) (2.0 * sr));
    aaB.set ((float) sr * 0.42f, 1.31f, (float) (2.0 * sr));
    dcL.set (8.0f, (float) sr);
    dcR.set (8.0f, (float) sr);
    levelS.setTime (0.05f, (float) sr);
    openK = 1.0f - std::exp (-1.0f / (0.006f * (float) sr));
    reset();
}

void Drone::reset()
{
    phA = phB = 0.0;
    prevA = prevB = curA = curB = 0.0f;
    pitchSemis = pitchTarget = 0.0f;
    deg = 0;
    duckEnv = lpgEnv = pingEnv = foldEnv = follower = 0.0f;
    openS = -1.0f;
    for (auto* f : { &lpgL, &lpgR, &dcL, &dcR }) f->z = 0.0f;
    for (auto* f : { &pingL, &pingR, &aaA, &aaB }) f->reset();
    levelS.v = levelS.target = 0.0f;
}

void Drone::act (int action, float vel, int degreeJump)
{
    switch (action)
    {
        case actDuck: duckEnv = std::max (duckEnv, vel); break;
        case actLpg:  lpgEnv = std::max (lpgEnv, vel); break;
        case actStep: deg = (deg + 1) % 10; break;
        case actRand: deg = std::max (0, degreeJump) % 10; break;
        case actPing: pingEnv = std::max (pingEnv, vel); break;
        case actFold: foldEnv = std::max (foldEnv, vel); break;
        default: break;
    }
}

// one sample at twice the host rate
float Drone::tick (float fA, float fB, float fmAB, float fmBA, float fold, float ring, float sync, float shape)
{
    const double sr2 = 2.0 * sr;
    // through-zero phase modulation both ways: each reads the other's last value
    const float a = std::sin (twoPi * (float) (phA + (double) (fmAB * prevB)));
    const double bph = phB + (double) (fmBA * prevA);
    const float bs = std::sin (twoPi * (float) bph);
    float b = bs;
    if (shape > 0.0f)
    {
        double fr = bph - std::floor (bph);
        const float tri = 1.0f - 4.0f * std::abs ((float) (fr + 0.25 - std::floor (fr + 0.25)) - 0.5f);
        const float sq = soft (bs * 6.0f) / soft (6.0f);
        b = shape < 0.5f ? lerp (bs, tri, shape * 2.0f) : lerp (tri, sq, (shape - 0.5f) * 2.0f);
    }
    // the folder: the sine driven past its peaks folds back (a 259's timbre section)
    const float af = std::sin (a * (1.0f + 5.0f * fold) * pi * 0.5f);
    float out = 0.68f * af + 0.32f * b;
    out = lerp (out, af * b * 1.4f, ring);

    phA += fA / sr2;
    if (phA >= 1.0)
    {
        phA -= 1.0;
        phB *= (double) (1.0f - sync);            // sync: B pulled back to the start with A (hard at 1)
    }
    phB += fB / sr2;
    phB -= std::floor (phB);
    prevA = a;
    prevB = b;
    curA = af;
    curB = b;
    return out;
}

void Drone::render (float* l, float* r, float* mono, int n)
{
    const auto& S = settings;
    levelS.target = clamp (S.level, 0.0f, 1.0f);
    if (levelS.target <= 0.0f && levelS.v < 1.0e-4f)
    {
        std::fill (mono, mono + n, 0.0f);
        follower *= std::pow (followK, (float) n);
        duckEnv = lpgEnv = pingEnv = foldEnv = 0.0f;
        return;
    }
    const float c = clamp (S.chaos, 0.0f, 1.0f);
    // ORDER <-> CHAOS: the indices, the fold and the ring open together; at the top noise gets into the phases
    // the index in cycles of phase: 1.3 x knob^1.5 (about 8 radians at 1), CHAOS pushes past it
    auto index = [] (float x) { x = clamp (x, 0.0f, 1.8f); return 1.3f * x * std::sqrt (x); };
    const float fmAB = index (S.fmAB + 0.5f * S.fmCv + 1.0f * c * c);
    const float fmBA = index (S.fmBA + 1.0f * c * c);
    const float ring = clamp (S.ring + 0.3f * c, 0.0f, 1.0f);
    const float sync = clamp (S.sync, 0.0f, 1.0f);
    const float shape = clamp (S.shape, 0.0f, 1.0f);
    const float noise = c * c * c * 0.25f;
    const float baseFold = clamp (S.fold + 0.5f * S.foldCv + 0.45f * c, 0.0f, 1.5f);
    pitchTarget = 12.0f * std::log2 (std::max (1.0f, S.rootHz) / 27.5f) + 12.0f * clamp (S.pitchCv, -1.5f, 1.5f);
    if (pitchSemis <= 0.0f) pitchSemis = pitchTarget;
    const float ratio = droneRatio (S.ratio) * std::pow (2.0f, (clamp (S.fine, 0.0f, 1.0f) - 0.5f) / 12.0f);
    const float open0 = clamp (S.bright * (1.0f - S.arc + S.arc * clamp (S.arcValue, 0.0f, 1.0f)) + 0.5f * S.lpgCv, 0.0f, 1.0f);
    const float nyq = (float) sr * 0.45f;
    if (openS < 0.0f) openS = open0;

    for (int i = 0; i < n; ++i)
    {
        pitchSemis = pitchTarget + glideK * (pitchSemis - pitchTarget);
        const float fA = std::min (nyq, 27.5f * std::pow (2.0f, pitchSemis / 12.0f));
        const float fB = std::min (nyq, fA * ratio);
        const float fold = baseFold + 0.8f * foldEnv;
        float y = 0.0f;
        for (int k = 0; k < 2; ++k)
        {
            if (noise > 0.0f) prevB += rng.bi() * noise;
            y = aaB.lp (aaA.lp (tick (fA, fB, fmAB, fmBA, fold, ring, sync, shape)));
        }
        // a little of A on the left and of B on the right: the pair, wide
        float L = y * 0.85f + curA * 0.15f, R = y * 0.85f + curB * 0.15f;
        // the vactrol: one opening for the brightness and the level, struck by LPG, closed by DUCK
        openS += (open0 - openS) * openK;                  // no zipper from a CV that arrives once a block
        const float open = clamp (openS + 0.65f * lpgEnv, 0.0f, 1.0f);
        const float a = std::exp (-twoPi * expMap (open, 70.0f, 15000.0f) / (float) sr);
        lpgL.a = a; lpgR.a = a;
        L = lpgL.lp (L); R = lpgR.lp (R);
        if (pingEnv > 1.0e-4f)
        {
            pingL.set (fA * 3.0f, 9.0f, (float) sr);
            pingR.set (fA * 3.0f * 1.01f, 9.0f, (float) sr);
            L += pingL.bp (L) * pingEnv * 2.5f;
            R += pingR.bp (R) * pingEnv * 2.5f;
        }
        const float g = levelS.tick() * (0.3f + 0.7f * open) * (1.0f - clamp (S.duck, 0.0f, 1.0f) * duckEnv) * 0.5f;
        L = dcL.hp (L) * g;
        R = dcR.hp (R) * g;
        l[i] += L;
        r[i] += R;
        mono[i] = 0.5f * (L + R);
        const float e = std::abs (mono[i]) * 2.5f;
        follower = e > follower ? e : follower * followK;
        duckEnv *= duckK; lpgEnv *= lpgK; pingEnv *= pingK; foldEnv *= foldK;
        if ((++xyDecim & 1) == 0)
        {
            const int w = xyWrite.load (std::memory_order_relaxed);
            xyA[(size_t) w] = curA;
            xyB[(size_t) w] = curB;
            xyWrite.store ((w + 1) % kXY, std::memory_order_release);
        }
    }
    follower = std::min (1.0f, follower);
}

//==============================================================================
void SympatheticStrings::prepare (double sampleRate)
{
    sr = sampleRate;
    for (auto& s : strings) s.line.assign ((size_t) (sr / 18.0) + 4, 0.0f);
    inHp.set (160.0f, (float) sr);
    reset();
}

void SympatheticStrings::reset()
{
    for (auto& s : strings) { std::fill (s.line.begin(), s.line.end(), 0.0f); s.w = 0; s.z = 0.0f; }
    inHp.z = 0.0f;
}

void SympatheticStrings::set (const std::array<float, kStrings>& hz, float decay01, float bright01)
{
    const float t60 = expMap (clamp (decay01, 0.0f, 1.0f), 0.2f, 8.0f);
    damp = 0.04f + 0.6f * (1.0f - clamp (bright01, 0.0f, 1.0f));
    for (int k = 0; k < kStrings; ++k)
    {
        auto& s = strings[(size_t) k];
        const float f = clamp (hz[(size_t) k], 20.0f, (float) sr * 0.3f);
        hzNow[(size_t) k] = f;
        s.delay = clamp ((float) sr / f, 2.0f, (float) s.line.size() - 3.0f);
        s.gain = std::pow (0.001f, s.delay / (t60 * (float) sr));
    }
}

void SympatheticStrings::process (const float* in, float* l, float* r, int n)
{
    for (int i = 0; i < n; ++i)
    {
        const float x = inHp.hp (in[i]) * 0.35f;
        float out[kStrings];
        for (int k = 0; k < kStrings; ++k)
        {
            auto& s = strings[(size_t) k];
            const int len = (int) s.line.size();
            float rp = (float) s.w - s.delay;
            while (rp < 0.0f) rp += (float) len;
            const int i0 = (int) rp;
            const float fr = rp - (float) i0;
            const float y = s.line[(size_t) i0] + (s.line[(size_t) ((i0 + 1) % len)] - s.line[(size_t) i0]) * fr;
            s.z = y + damp * (s.z - y);
            s.line[(size_t) s.w] = soft (x + s.z * s.gain);
            s.w = (s.w + 1) % len;
            out[k] = y;
        }
        l[i] += out[0] + 0.6f * out[2];
        r[i] += out[1] + 0.6f * out[3];
    }
}

} // namespace flt
