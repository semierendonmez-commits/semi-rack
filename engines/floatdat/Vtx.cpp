// SEMI port: vendored from FLOATDAT Source/engine (7e30328); namespaces renamed (ryoz/vtx/pinball/jdat/pdata -> fdat_*), see sources.mk.
#include "Vtx.h"

namespace fdat_vtx
{
namespace
{
inline float clampf (float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline double wrap01 (double x) { return x - std::floor (x); }
inline float fixdenorm (float x) { return std::abs (x) < 1.0e-20f ? 0.0f : x; }
}

//==============================================================================
void Cave::prepare (double sr)
{
    scale = sr / 44100.0;
    pd.setSize ((int) std::ceil (22050 * scale) + 4);
    ap1.setSize ((int) std::ceil (2200 * scale) + 4);
    ap2.setSize ((int) std::ceil (3300 * scale) + 4);
    ap3.setSize ((int) std::ceil (5300 * scale) + 4);
    fb.setSize ((int) std::ceil (16000 * scale) + 4);
    clear();
}

void Cave::clear()
{
    for (auto* r : { &pd, &ap1, &ap2, &ap3, &fb }) r->clear();
    ap1Out = ap2Out = ap3Out = lp = fbSig = 0.0f;
}

float Cave::process (float in, double predelaySamples, float g1, float g2, float g3, double tailDelay, float tailFb, float lpCoeff) noexcept
{
    // predelay
    const int pdN = std::clamp ((int) (predelaySamples * scale), 1, pd.size() - 2);
    const float pdOut = pd.tap (pdN);
    pd.write (in);
    // the patch's allpasses: new = -g * x + d + g * previous out; written: x + g * d
    auto ap = [] (Ring& r, int D, float g, float x, float& out)
    {
        const float d = r.tap (D);
        const float nw = -g * x + d + g * out;
        r.write (x + g * d);
        out = nw;
    };
    ap (ap1, std::min (ap1.size() - 2, (int) (2073 * scale)), g1, pdOut, ap1Out);
    ap (ap2, std::min (ap2.size() - 2, (int) (3137 * scale)), g2, ap1Out, ap2Out);
    ap (ap3, std::min (ap3.size() - 2, (int) (4987 * scale)), g3, ap2Out, ap3Out);
    float x = ap3Out;
    if (lpCoeff > 0.0f) { lp += lpCoeff * (ap3Out - lp); x = lp; }
    const int tD = std::clamp ((int) (tailDelay * scale), 1, fb.size() - 2);
    fbSig = fixdenorm (x + fb.tap (tD) * tailFb);
    fb.write (fbSig);
    ap1Out = fixdenorm (ap1Out); ap2Out = fixdenorm (ap2Out); ap3Out = fixdenorm (ap3Out); lp = fixdenorm (lp);
    return fbSig * 0.5f;
}

//==============================================================================
void Engine::prepare (double sampleRate)
{
    sr = sampleRate > 0 ? sampleRate : 44100.0;
    kCave.prepare (sr);
    tCave.prepare (sr);
    comb1.setSize ((int) std::ceil (8192 * sr / 44100.0) + 4);
    comb2.setSize ((int) std::ceil (8192 * sr / 44100.0) + 4);
    for (auto& d : gDel) d.setSize ((int) sr + 4);
    reset();
}

void Engine::reset()
{
    kPrevPhase = kClk = 0; kEnvPh = kPunchPh = kClickPh = 1; kOsc = kSub = 0; kHp = 0;
    tPrev = tClk = 0; tGrainPh = 1; tNoisePh = 0; tScatter = 0; r1 = r2 = r3 = r4 = r5 = r6 = 0; tSmooth = 0; toneGateLeft = 0;
    comb1.clear(); comb2.clear(); kCave.clear(); tCave.clear();
    gk.fill (0); gLfo.fill (0); gPh.fill (0); gFb.fill (0); gLpA.fill (0); gLpB.fill (0);
    gOutL = gOutR = gEnv = gPrevIn = 0;
    for (auto& d : gDel) d.clear();
    for (auto* arr : { &gSelfDc, &gTapDc, &gResDc, &gLoopDc, &gNodeDc, &gSatDc, &gWrDc }) for (auto& d : *arr) d.reset();
    gInDc.reset(); gOutLDc.reset(); gOutRDc.reset();
    dLfo.fill (0); dOsc.fill (0); dBeat1 = dBeat2 = dRm = dH4 = 0;
}

//==============================================================================
void Engine::process (float* outL, float* outR, int n, const float* excite, const Params& P, const Hit* hits, int numHits)
{
    const float fsr = (float) sr;
    const float tw = (float) twoPi;
    int hi = 0;
    const float hpC = 1.0f - std::exp (-tw * 120.0f / fsr);
    const float lpC = 1.0f - std::exp (-tw * 3000.0f / fsr);
    const float pdKick = P.kickPredelay * 44.1f;                  // ms at the patch's 44.1 kHz
    double kEnvMax = 0, tEnvMax = 0, gE = 0;

    for (int i = 0; i < n; ++i)
    {
        bool kickHit = false, toneHit = false; float ghostStrike = 0.0f;
        while (hi < numHits && hits[hi].offset <= i)
        {
            const auto& h = hits[hi++];
            if (h.voice == 0) { kickHit = true; kVel = clampf (h.vel, 0.05f, 1.27f); }
            else if (h.voice == 1) { toneHit = true; tVel = clampf (h.vel, 0.05f, 1.27f); toneGateLeft = std::max<int64_t> (toneGateLeft, (int64_t) (h.gateMs * 0.001f * fsr)); }
            else ghostStrike += h.vel * 0.6f;
        }

        // ================= CAVE KICK (ftrve_bd) =================
        float kick = 0.0f;
        {
            kClk = wrap01 (kClk + P.kickRate / sr);
            const bool edge = kClk < kPrevPhase;
            kPrevPhase = kClk;
            const float n01 = (noise() + 1.0f) * 0.5f;
            bool fire = kickHit || (P.kickOwn && edge && n01 < P.kickProb);
            if (fire) { ++kicks; if (! kickHit) kVel = 1.0f; }
            kEnvPh = fire ? 0.0 : std::min (1.0, kEnvPh + 1.0 / (std::max (0.02f, P.kickDecay) * sr));
            kClickPh = fire ? 0.0 : std::min (1.0, kClickPh + 1.0 / (0.003 * sr));
            kPunchPh = fire ? 0.0 : std::min (1.0, kPunchPh + 1.0 / (0.18 * sr));
            const float env = std::exp (-(float) kEnvPh * 12.0f);
            const float clickEnv = std::exp (-(float) kClickPh * 400.0f);
            const float punchEnv = std::exp (-(float) kPunchPh * 8.0f);
            const float sweep = std::exp (-(float) kEnvPh * fsr * 0.003f * 40.0f);
            const float inst = P.kickPitch + P.kickPitch * 2.5f * sweep;
            kOsc = wrap01 (kOsc + inst / sr);
            const float body = std::sin ((float) kOsc * tw) * env;
            kSub = wrap01 (kSub + std::max (P.kickPitch * 0.5f, 35.0f) / sr);
            const float sub = std::sin ((float) kSub * tw) * std::exp (-(float) kEnvPh * 7.0f) * 0.5f;
            const float click = noise() * clickEnv * 0.3f;
            const float punch = std::sin ((float) kOsc * tw) * punchEnv * 0.4f;
            // the patch fires all the time; struck now and then, the sub's floor (exp(-7)) would hum on:
            // the last 5% of the envelope's run fades everything out
            const float tail = std::min (1.0f, (1.0f - (float) kEnvPh) * 20.0f);
            const float driven = (body + sub + punch) * tail * (1.0f + P.kickDrive * 12.0f);
            const float sat = driven / (1.0f + std::abs (driven));
            const float kSat = clampf (sat + sat * sat * sat * P.kickDrive * 0.3f, -1.0f, 1.0f);
            const float dry = clampf (kSat + click, -1.0f, 1.0f);
            kHp += hpC * (dry - kHp);
            const float rev = kCave.process (dry - kHp, pdKick, 0.6f, 0.55f, 0.5f, 15007.0, 0.42f, lpC);
            kick = (dry * (1.0f - P.kickRevmix) + rev * P.kickRevmix) * kVel;
            kEnvMax = std::max (kEnvMax, (double) env);
        }

        // ================= FDBK TONE (ftrve_fdbd) =================
        float tone = 0.0f;
        {
            const float grainRate = 20.0f + P.toneCollapse * 180.0f;
            tClk = wrap01 (tClk + grainRate / sr);
            const bool edge = tClk < tPrev;
            tPrev = tClk;
            const float n01 = (noise() + 1.0f) * 0.5f;
            const bool open = P.toneOwn || toneGateLeft > 0;
            if (toneGateLeft > 0) --toneGateLeft;
            const bool fire = toneHit || (open && edge && n01 < P.toneProb);
            if (fire) ++grains;
            const float scatterNoise = noise() * P.toneMutation * 0.8f;
            tScatter = fire ? (tScatter * 0.3f + scatterNoise) : tScatter * 0.998f;
            const float fNow = clampf (P.toneFreq * std::exp (tScatter * 2.5f), 20.0f, 18000.0f);
            const float f2 = clampf (fNow * 1.618f, 20.0f, 18000.0f);
            const float f3 = clampf (fNow * 0.5f + fNow * P.toneMutation * 0.3f, 10.0f, 9000.0f);
            toneFreqNow = fNow;
            tGrainPh = fire ? 0.0 : std::min (1.0, tGrainPh + 1.0 / (sr * 0.005 + sr * P.toneCollapse * 0.02));
            const float grainEnv = std::exp (-(float) tGrainPh * 6.0f);
            tNoisePh = wrap01 (tNoisePh + fNow / sr);
            const float grainNoise = noise() * 0.7f + std::sin ((float) tNoisePh * tw) * 0.3f;
            const float exc = grainNoise * grainEnv * (fire ? 1.0f : 0.0f) * (fire ? tVel : 1.0f);

            const float rq = 0.99f - (1.0f - P.toneFeedback) * 0.98f;
            const float w1 = tw * fNow / fsr;
            const float r1n = exc + rq * 2.0f * std::cos (w1) * r1 - rq * rq * r2;
            r2 = r1; r1 = fixdenorm (r1n);
            const float res1 = r1 * (1.0f - rq);
            const float rq2 = rq * 0.9f, w2 = tw * f2 / fsr;
            const float r3n = exc * 0.5f + rq2 * 2.0f * std::cos (w2) * r3 - rq2 * rq2 * r4;
            r4 = r3; r3 = fixdenorm (r3n);
            const float res2 = r3 * (1.0f - rq2) * 0.6f;
            const float rq3 = rq * 0.85f, w3 = tw * f3 / fsr;
            const float r5n = (res1 * P.toneFeedback + exc * 0.3f) + rq3 * 2.0f * std::cos (w3) * r5 - rq3 * rq3 * r6;
            r6 = r5; r5 = fixdenorm (r5n);
            const float res3 = r5 * (1.0f - rq3) * 0.4f;

            const double cd1 = std::clamp ((double) fsr / fNow, 1.0, (double) comb1.size() - 2);
            float cin = res1 + res2 + comb1.read (cd1) * P.toneFeedback * 0.7f;
            cin = cin / (1.0f + std::abs (cin) * 0.4f);
            comb1.write (fixdenorm (cin));
            const double cd2 = std::clamp ((double) fsr / f2, 1.0, (double) comb2.size() - 2);
            float c2 = res3 + cin * 0.3f + comb2.read (cd2) * P.toneFeedback * 0.5f;
            c2 = c2 / (1.0f + std::abs (c2) * 0.4f);
            comb2.write (fixdenorm (c2));
            const float mixed = cin * 0.5f + c2 * 0.3f + res3 * 0.2f;

            // crush: sample-and-hold on the noise phase's clock, then bits
            const float crushRate = std::max (1.0f, std::floor ((1.0f - P.toneCrush) * 31.0f) + 1.0f);
            const float counter = std::fmod (std::floor ((float) tNoisePh * fsr), crushRate);
            if (counter < 1.0f) tSmooth = mixed;
            const float bits = std::max (1.0f, (1.0f - P.toneCrush) * 16.0f);
            const float levels = std::pow (2.0f, bits);
            const float q = std::floor (tSmooth * levels) / levels;
            float dry = tSmooth * (1.0f - P.toneCrush * 0.7f) + q * P.toneCrush * 0.7f;
            dry = dry / (1.0f + std::abs (dry) * 0.3f);
            const float rev = tCave.process (dry, 6050.0, 0.7f, 0.65f, 0.6f, 8000.0 + P.toneCollapse * 7000.0, 0.55f, 0.0f);
            float fin = (dry * 0.65f + rev * 0.35f) * 1.8f;
            tone = fin / (1.0f + std::abs (fin) * 0.3f);
            tEnvMax = std::max (tEnvMax, (double) std::abs (tone));
        }

        // ================= GHOST NET (ghostnetwork v4) =================
        float gL = 0.0f, gR = 0.0f;
        if (P.ghostLevel > 0.0005f)
        {
            const float inRaw = clampf ((excite != nullptr ? excite[i] * P.gInput : 0.0f) + ghostStrike * (noise() * 0.5f + 0.5f), -1.0f, 1.0f);
            const float inSig = gInDc (inRaw);
            const float inAbs = std::abs (inSig);
            const float knobs[8] { P.gSens, P.gFeedback, P.gPitch, P.gDetune, P.gRate, P.gBright, P.gSelf, P.gSat };
            for (int k = 0; k < 8; ++k) gk[(size_t) k] = gk[(size_t) k] + (knobs[k] - gk[(size_t) k]) * 0.0015f;
            const float k1 = gk[0], k2 = gk[1], k3 = gk[2], k4 = gk[3], k5 = gk[4], k6 = gk[5], k7 = gk[6], k8 = gk[7];
            gEnv = gEnv + (inAbs - gEnv) * 0.0025f;
            const float diffIn = inSig - gPrevIn;
            const float exciteIn = clampf (inSig * (0.08f + k1 * 0.65f) + diffIn * 0.95f, -0.8f, 0.8f);

            const double rateBase = 0.003 + k5 * 0.18;
            const double lr[4] { 0.71, 1.00, 1.37, 1.91 };
            for (int k = 0; k < 4; ++k) gLfo[(size_t) k] = wrap01 (gLfo[(size_t) k] + rateBase * lr[k] / sr);
            const float l1 = std::sin ((float) gLfo[0] * tw);
            const float l2 = std::sin ((float) gLfo[1] * tw + l1 * 0.45f);
            const float l3 = std::sin ((float) gLfo[2] * tw + l2 * 0.35f);
            const float l4 = std::sin ((float) gLfo[3] * tw + l3 * 0.25f);

            const float base = clampf (k3, 120.0f, 5000.0f);
            const float rr[4] { 1.000f, 1.414f, 1.933f, 2.771f };
            for (int k = 0; k < 4; ++k) gPh[(size_t) k] = wrap01 (gPh[(size_t) k] + base * rr[k] / sr);
            const float osc[4] { std::sin ((float) gPh[0] * tw + gFb[1] * 1.1f), std::sin ((float) gPh[1] * tw + gFb[2] * 1.0f),
                                 std::sin ((float) gPh[2] * tw + gFb[3] * 1.05f), std::sin ((float) gPh[3] * tw + gFb[0] * 0.95f) };
            const float selfAmt = 0.002f + k7 * 0.055f;
            const float selfG[4] { 1.0f, 0.92f, 0.88f, 0.84f };
            float self[4];
            for (int k = 0; k < 4; ++k) self[k] = gSelfDc[(size_t) k] (osc[k] * selfAmt * selfG[k]);

            const float fund = clampf (fsr / base, 3.0f, fsr * 0.2f);
            const float det = 0.5f + k4 * 16.0f;
            const float L[4] { l1, l2, l3, l4 };
            const float mA[4] { 0.7f, 0.8f, 0.9f, 1.0f }, mB[4] { 0.22f, 0.25f, 0.28f, 0.32f };
            const int other[4] { 2, 3, 0, 1 };
            float d[4], loop[4];
            const float lpA = 0.04f + k6 * 0.18f, lpB = 0.025f + k6 * 0.10f;
            const float bodyMix = 0.05f + (1.0f - k6) * 0.04f, resMix = 0.72f + k6 * 0.22f;
            for (int k = 0; k < 4; ++k)
            {
                const float dt = clampf (fund * rr[k] + L[k] * det * mA[k] + L[other[k]] * det * mB[k], 3.0f + (float) k, fsr * 0.2f);
                d[k] = gTapDc[(size_t) k] (gDel[(size_t) k].read (dt));
                gLpA[(size_t) k] = fixdenorm (gLpA[(size_t) k] + (d[k] - gLpA[(size_t) k]) * lpA);
                gLpB[(size_t) k] = fixdenorm (gLpB[(size_t) k] + (gLpA[(size_t) k] - gLpB[(size_t) k]) * lpB);
                const float res = gResDc[(size_t) k] (d[k] - gLpB[(size_t) k]);
                loop[k] = gLoopDc[(size_t) k] (res * resMix + gLpB[(size_t) k] * bodyMix);
            }
            const float fb = 0.70f + k2 * 0.24f;
            const float mx[4] { (loop[0] + loop[1] + loop[2] + loop[3]) * 0.5f, (loop[0] - loop[1] + loop[2] - loop[3]) * 0.5f,
                                (loop[0] + loop[1] - loop[2] - loop[3]) * 0.5f, (loop[0] - loop[1] - loop[2] + loop[3]) * 0.5f };
            const float energy = (std::abs (gFb[0]) + std::abs (gFb[1]) + std::abs (gFb[2]) + std::abs (gFb[3])) * 0.25f;
            const float reinj = clampf (energy * (0.035f + k1 * 0.10f), 0.0f, 0.14f);
            const float excAmt = 0.10f + k1 * 0.42f;
            const float eG[4] { 1.0f, 0.96f, 1.01f, 0.94f };
            const int fbx[4] { 2, 3, 0, 1 };
            const int oscx[4] { 1, 2, 3, 0 };
            const float drive = 0.90f + k8 * 0.72f;
            float sat[4];
            for (int k = 0; k < 4; ++k)
            {
                const float node = gNodeDc[(size_t) k] (mx[k] * fb + exciteIn * excAmt * eG[k] + self[k] + gFb[(size_t) fbx[k]] * 0.03f + reinj * osc[oscx[k]] * 0.5f);
                sat[k] = gSatDc[(size_t) k] (std::tanh (node * drive));
            }
            for (int k = 0; k < 4; ++k)
            {
                const float wr = gWrDc[(size_t) k] (clampf (sat[k] + sat[(k + 1) % 4] * 0.08f - sat[(k + 3) % 4] * 0.03f, -0.97f, 0.97f));
                gDel[(size_t) k].write (wr);
            }
            const float stL = (sat[0] + sat[2]) * 0.26f + (loop[0] - loop[2]) * 0.22f;
            const float stR = (sat[1] + sat[3]) * 0.26f + (loop[1] - loop[3]) * 0.22f;
            gOutL = fixdenorm (gOutL + (stL - gOutL) * 0.16f);
            gOutR = fixdenorm (gOutR + (stR - gOutR) * 0.16f);
            gL = clampf (gOutLDc (gOutL) * 0.82f + inSig * 0.015f, -0.92f, 0.92f);
            gR = clampf (gOutRDc (gOutR) * 0.82f + inSig * 0.015f, -0.92f, 0.92f);
            for (int k = 0; k < 4; ++k) { gFb[(size_t) k] = fixdenorm (sat[k]); ghostNode[(size_t) k] = sat[k]; }
            gPrevIn = inSig;
            gE += energy;
        }

        // ================= DRIFT (driftnoise) =================
        float dL = 0.0f, dR = 0.0f;
        if (P.driftLevel > 0.0005f)
        {
            const double lf[5] { 0.073, 0.037, 0.051, 0.019, 0.011 };
            float lfo[5];
            for (int k = 0; k < 5; ++k) { dLfo[(size_t) k] = wrap01 (dLfo[(size_t) k] + lf[k] / sr); lfo[k] = std::cos ((float) dLfo[(size_t) k] * tw); }
            const float b = P.driftFreq, dt = P.driftDetune;
            const float hz[5] { b, b * (1.0f + 0.003f * dt + 0.001f * lfo[0]), b * (1.0f - 0.002f * dt + 0.0015f * lfo[1]),
                                b * 2.0f * (1.0f + 0.0012f * lfo[2]), b * 3.0f * (1.0f - 0.0008f * lfo[3]) };
            float s[5];
            for (int k = 0; k < 5; ++k) { dOsc[(size_t) k] = wrap01 (dOsc[(size_t) k] + hz[k] / sr); s[k] = std::cos ((float) dOsc[(size_t) k] * tw); }
            const float cluster = (s[0] + s[1] + s[2]) * 0.33f + s[3] * 0.30f + s[4] * 0.12f;
            const float envL = (lfo[0] * 0.25f + 0.75f) * (lfo[2] * 0.15f + 0.85f);
            const float microL = noise() * 0.007f * (lfo[1] * 0.5f + 0.5f);
            dL = cluster * 0.22f * envL + microL;
            dBeat1 = wrap01 (dBeat1 + b * 1.0015 / sr); dBeat2 = wrap01 (dBeat2 + b * 0.9985 / sr);
            dRm = wrap01 (dRm + b * 1.5 / sr); dH4 = wrap01 (dH4 + b * 4.0 * (1.0 + 0.002 * lfo[4]) / sr);
            const float beating = (std::cos ((float) dBeat1 * tw) + std::cos ((float) dBeat2 * tw)) * 0.45f * (lfo[4] * 0.35f + 0.65f);
            const float ring = noise() * std::cos ((float) dRm * tw) * 0.28f;
            const float breath = noise() * (lfo[3] * 0.40f + 0.60f) * 0.055f;
            const float harm4 = std::cos ((float) dH4 * tw) * 0.07f * (lfo[1] * 0.4f + 0.6f);
            const float envR = (lfo[1] * 0.20f + 0.80f) * (lfo[3] * 0.18f + 0.82f);
            dR = (beating * 0.40f + ring * 0.28f + breath + harm4) * envR;
            // the patch's master amp is 0.06; here the level knob sits on top of a 4x lift
            dL *= 0.24f; dR *= 0.24f;
        }

        outL[i] = kick * P.kickLevel + tone * P.toneLevel + gL * P.ghostLevel + dL * P.driftLevel;
        outR[i] = kick * P.kickLevel + tone * P.toneLevel + gR * P.ghostLevel + dR * P.driftLevel;
    }
    kickEnv = (float) kEnvMax;
    toneEnv = (float) tEnvMax;
    ghostEnergy = n > 0 ? (float) (gE / n) : 0.0f;
    driftLevel = P.driftLevel;
}
} // namespace fdat_vtx
