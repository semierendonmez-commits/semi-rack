// SEMI port: vendored from FLOATER Source/engine (v0.7), namespace fl -> flt; see engines/floater/sources.mk.
#include "Reel.h"
#include <algorithm>
#include <cmath>

namespace flt
{
using namespace dsp;

// SEMI port: the tape is 20 s in Rack as in the plug-in; on the MetaModule (shared plugin RAM, 48 kHz)
// 10 s (eight bars down to 192 BPM, fewer bars below, see updateWindow).
#ifdef METAMODULE
namespace { constexpr double kTapeSeconds = 10.0; constexpr int kMaxPeaks = (int) (kTapeSeconds * 96000.0) / Reel::kPeak + 2; }
#else
namespace { constexpr double kTapeSeconds = 20.0; constexpr int kMaxPeaks = (int) (kTapeSeconds * 192000.0) / Reel::kPeak + 2; }
#endif

// the display's peaks are allocated once, for the highest rate: the editor reads them without a lock,
// so the buffer must never move (a sample-rate change only changes how much of it is used)
Reel::Reel() { display.peaks.assign ((size_t) kMaxPeaks, 0.0f); }

void Reel::prepare (double sampleRate, int)
{
    sr = sampleRate;
    // twenty seconds of tape: eight bars down to 96 BPM, fewer bars below (see updateWindow)
    cap = std::min<int64_t> (((int64_t) (kTapeSeconds * sr) / kPeak + 1) * kPeak, (int64_t) (kMaxPeaks - 1) * kPeak);
    tapeL.assign ((size_t) cap, 0.0f);
    tapeR.assign ((size_t) cap, 0.0f);
    display.peakCap.store ((int) (cap / kPeak), std::memory_order_relaxed);
    dryStep = 1.0f / (0.003f * (float) sr);
    duckK = decayCoef (0.09f, (float) sr);
    splitHz = -1.0f;
    reset();
}

void Reel::reset()
{
    rec = 0;
    barCount = 0;
    windowValid = false;
    winStart = 0;
    spliceLen = numSplices = 0;
    for (auto& v : voices) v.on = false;
    queued = 0;
    dryGain = dryTarget = 1.0f;
    duckEnv = peakAcc = 0.0f;
    peakFill = genesStarted = genesBroken = 0;
    std::fill (tapeL.begin(), tapeL.end(), 0.0f);
    std::fill (tapeR.begin(), tapeR.end(), 0.0f);
    std::fill (display.peaks.begin(), display.peaks.end(), 0.0f);   // in place: no reallocation
    for (auto* f : { &loA, &loB, &loAR, &loBR, &wetA, &wetB, &wetAR, &wetBR }) f->reset();
}

float Reel::tape (int channel, double recIndex) const
{
    const auto& t = channel == 0 ? tapeL : tapeR;
    const double fl = std::floor (recIndex);
    const float frac = (float) (recIndex - fl);
    int64_t i0 = (int64_t) fl % cap;
    if (i0 < 0) i0 += cap;
    const int64_t i1 = (i0 + 1) % cap;
    return t[(size_t) i0] + (t[(size_t) i1] - t[(size_t) i0]) * frac;
}

void Reel::record (float l, float r)
{
    if (settings.hold) return;
    const auto i = (size_t) (rec % cap);
    tapeL[i] = l;
    tapeR[i] = r;
    peakAcc = std::max (peakAcc, std::max (std::abs (l), std::abs (r)));
    if (++peakFill >= kPeak)
    {
        display.peaks[(size_t) ((rec / kPeak) % (cap / kPeak))] = peakAcc;
        peakAcc = 0.0f;
        peakFill = 0;
    }
    ++rec;
}

//==============================================================================
// The splices: the last L whole bars, one per bar. Kept as they are while HOLD is on.
void Reel::updateWindow()
{
    if (barCount < 3) return;
    const int64_t lastBar = bars[(size_t) ((barCount - 1) % 16)];
    const int64_t barLen = lastBar - bars[(size_t) ((barCount - 2) % 16)];
    const int64_t prevLen = bars[(size_t) ((barCount - 2) % 16)] - bars[(size_t) ((barCount - 3) % 16)];
    // only steady bars: not the first one after the transport stopped and started, after HOLD, or a tempo jump
    if (barLen < 1000 || std::abs ((double) (barLen - prevLen)) > 0.1 * (double) barLen) return;
    int L = reelLengthBars (settings.length);
    while (L > 1 && (int64_t) (L + 2) * barLen > cap) L >>= 1;      // slow tempi: fewer bars fit
    if ((int64_t) (L + 2) * barLen > cap) { windowValid = false; return; }   // not even one bar fits: no splices
    L = (int) std::min<int64_t> (L, barCount - 1);
    const int64_t start = bars[(size_t) ((barCount - 1 - L) % 16)];
    const int64_t sl = (lastBar - start) / L;
    if (sl < 1000 || std::abs ((double) (sl - barLen)) > 0.1 * (double) barLen) return;
    winStart = start;
    spliceLen = (int) sl;
    numSplices = L;
    windowValid = true;
}

void Reel::handle (const Event& e)
{
    switch (e.kind)
    {
        case Event::bar:
            if (settings.hold) break;                                   // a held tape keeps no bar marks
            bars[(size_t) (barCount % 16)] = rec;
            ++barCount;
            updateWindow();
            break;
        case Event::gene: startGene (e.g); break;
        case Event::duck: duckEnv = 1.0f; break;
    }
}

void Reel::discardUntil (int64_t t)
{
    for (int q = 0; q < queued;)
        if (queue[(size_t) q].at < t) queue[(size_t) q] = queue[(size_t) --queued];
        else ++q;
}

void Reel::startGene (const Gene& g)
{
    ++genesStarted;
    const bool broken = g.broken && settings.chaos > 0.0f;
    dryTarget = broken ? 0.0f : 1.0f;
    if (! broken) return;
    ++genesBroken;

    Voice* v = &voices[0];
    for (auto& c : voices)
    {
        if (! c.on) { v = &c; break; }
        if (c.age > v->age) v = &c;
    }
    // MORPH: the window against the gene: gaps, seamless, then overlapping into layers
    const float m = clamp (settings.morph, 0.0f, 1.0f);
    const float wf = m < 0.35f ? 0.3f + 2.0f * m : (m <= 0.6f ? 1.0f : 1.0f + 2.0f * (m - 0.6f) / 0.4f);
    const int geneLen = std::max (64, g.length);
    const int sub = wf > 1.0f ? 1 : std::max (1, std::min (4, g.sub));
    const int fadeMin = (int) (0.003 * sr);
    v->segLen = std::max (32, geneLen / sub);
    v->winLen = std::max (32, (int) ((float) v->segLen * wf));
    v->fade = wf > 1.0f ? std::max (fadeMin, (int) ((float) v->winLen * 0.5f * (m - 0.6f) / 0.4f)) : fadeMin;
    v->fade = std::max (1, std::min (v->fade, v->winLen / 2));
    v->life = (sub - 1) * v->segLen + v->winLen;
    v->sub = sub > 1;
    v->gain = wf > 1.0f ? 1.0f / std::sqrt (wf) : 1.0f;
    if (windowValid)
    {
        const int sp = std::max (0, std::min (numSplices - 1, g.splice));
        v->base = winStart + (int64_t) sp * spliceLen;
        v->len = spliceLen;
        v->rel = (double) clamp (g.offset, 0.0f, 1.0f) * spliceLen;
    }
    else
    {
        // no splices yet (the first bars): stutter what just went by
        if (rec < geneLen) { dryTarget = 1.0f; return; }
        v->base = rec - geneLen;
        v->len = geneLen;
        v->rel = 0.0;
    }
    if (g.rate < 0.0f) v->rel += (double) v->segLen * std::abs (g.rate);   // backwards from its end
    v->rel0 = v->rel;
    v->rate = g.rate;
    v->age = 0;
    v->on = true;
}

float Reel::voiceTick (Voice& v, float& outR)
{
    const int a = v.sub ? v.age % v.segLen : v.age;
    if (v.sub && a == 0 && v.age > 0) v.rel = v.rel0;
    float env = 0.0f;
    if (a < v.winLen)
    {
        env = 1.0f;
        if (a < v.fade) env = 0.5f - 0.5f * std::cos (pi * (float) a / (float) v.fade);
        else if (a > v.winLen - v.fade) env = 0.5f - 0.5f * std::cos (pi * (float) (v.winLen - a) / (float) v.fade);
    }
    double p = std::fmod (v.rel, (double) v.len);
    if (p < 0.0) p += v.len;
    const double idx = (double) v.base + p;
    float g = env * v.gain;
    const float ar = (float) std::abs (v.rate);
    if (ar < 0.05f) g *= ar / 0.05f;                                 // stopped: the tape is silent
    v.rel += v.rate;
    if (++v.age >= v.life) v.on = false;
    outR = tape (1, idx) * g;
    return tape (0, idx) * g;
}

//==============================================================================
void Reel::process (float* l, float* r, int n, int64_t now)
{
    const float split = settings.split;
    if (split > 0.0f && std::abs (split - splitHz) > 0.5f)
    {
        for (auto* f : { &loA, &loB, &loAR, &loBR, &wetA, &wetB, &wetAR, &wetBR }) f->set (split, 0.7071f, (float) sr);
        splitHz = split;
    }
    int pos = 0;
    while (pos < n)
    {
        int end = n;
        for (bool again = true; again;)
        {
            again = false;
            for (int q = 0; q < queued; ++q)
            {
                const int64_t rel = queue[(size_t) q].at - now;
                if (rel <= pos)
                {
                    const Event e = queue[(size_t) q];
                    queue[(size_t) q] = queue[(size_t) --queued];
                    handle (e);
                    again = true;
                    break;
                }
                if (rel < end) end = (int) rel;
            }
        }
        for (int i = pos; i < end; ++i)
        {
            const float xl = l[i], xr = r[i];
            record (xl, xr);
            if (dryGain < dryTarget) dryGain = std::min (dryTarget, dryGain + dryStep);
            else if (dryGain > dryTarget) dryGain = std::max (dryTarget, dryGain - dryStep);
            duckEnv *= duckK;
            float wl = 0.0f, wr = 0.0f;
            for (auto& v : voices)
                if (v.on) { float vr; wl += voiceTick (v, vr); wr += vr; }
            // out = x + mix * (reel - x): exactly x while the reel is out, so nothing switches
            const float mix = (1.0f - dryGain) * (1.0f - settings.duck * duckEnv);
            if (split > 0.0f)
            {
                // only the band above SPLIT is replaced: x's own low band (LR4) under the reel's high band
                const float lowL = loB.lp (loA.lp (xl)), lowR = loBR.lp (loAR.lp (xr));
                const float el = wetB.hp (wetA.hp (wl)), er = wetBR.hp (wetAR.hp (wr));
                l[i] = xl + mix * (lowL + el - xl);
                r[i] = xr + mix * (lowR + er - xr);
            }
            else
            {
                l[i] = xl + mix * (wl - xl);
                r[i] = xr + mix * (wr - xr);
            }
        }
        pos = end;
    }
    // recording laps the tape: a window older than the tape holds is gone
    if (windowValid && rec - winStart > cap - 1) windowValid = false;

    auto& D = display;
    D.rec.store (rec, std::memory_order_relaxed);
    D.winStart.store (winStart, std::memory_order_relaxed);
    D.spliceLen.store (spliceLen, std::memory_order_relaxed);
    D.splices.store (numSplices, std::memory_order_relaxed);
    D.valid.store (windowValid, std::memory_order_relaxed);
    D.held.store (settings.hold, std::memory_order_relaxed);
    D.breaking.store (dryTarget < 0.5f, std::memory_order_relaxed);
    D.mix.store (1.0f - dryGain, std::memory_order_relaxed);
    D.genes.store (genesStarted, std::memory_order_relaxed);
    D.broken.store (genesBroken, std::memory_order_relaxed);
    const double wlen = (double) std::max (1, numSplices * spliceLen);
    for (int k = 0; k < kGenes; ++k)
    {
        const auto& v = voices[(size_t) k];
        float p = -1.0f;
        if (v.on && windowValid)
        {
            double rp = std::fmod (v.rel, (double) v.len);
            if (rp < 0.0) rp += v.len;
            p = (float) (((double) (v.base - winStart) + rp) / wlen);
        }
        D.voicePos[(size_t) k].store (p, std::memory_order_relaxed);
    }
}

//==============================================================================
void ReelVoice::trigger (const Strike& s)
{
    if (! reel.hasWindow()) return;
    const auto& p = s.p;
    auto mod = [&s] (int kind) { return (s.kindA == kind ? s.modA : 0.0f) + (s.kindB == kind ? s.modB : 0.0f); };
    ws = reel.windowStart();
    wlen = (int64_t) reel.splices() * reel.spliceLength();
    // where: on a sixteenth grid across the whole reel, so the chops land on the beat they came from
    const int sixteenths = reel.splices() * 16;
    const float color = clamp (p.color + 0.5f * mod (Strike::timbre), 0.0f, 0.999f);
    const int slot = std::min (sixteenths - 1, (int) (color * (float) sixteenths));
    base = ws + (int64_t) ((double) slot * reel.spliceLength() / 16.0);
    static const int div[6] { 32, 16, 8, 4, 2, 1 };
    const int h = (int) std::lround (clamp (p.harm + 0.5f * mod (Strike::harmonics), 0.0f, 1.0f) * 5.0f);
    sliceLen = std::max (64, reel.spliceLength() / div[h]);
    const float semis = std::round ((p.tune - 0.5f) * 48.0f) + 12.0f * mod (Strike::semis) + 7.0f * mod (Strike::degrees);
    rate = std::pow (2.0, (double) clamp (semis, -36.0f, 36.0f) / 12.0);
    envK = sustain ? 1.0f : decayCoef (expMap (clamp (p.decay + 0.5f * mod (Strike::decay), 0.0f, 1.0f), 0.04f, 3.0f), sr);
    const float m = clamp (p.morph + 0.5f * mod (Strike::morph), 0.0f, 1.0f);
    filterMode = m < 0.45f ? 1 : (m > 0.55f ? 2 : 0);
    if (filterMode == 1) filt.set (expMap (m / 0.45f, 250.0f, 20000.0f), 0.8f, sr);
    if (filterMode == 2) filt.set (expMap ((m - 0.55f) / 0.45f, 20.0f, 5000.0f), 0.8f, sr);
    level = s.vel;
    beginDeclick();
    env = 1.0f;
    age = 0;
    rel = 0.0;
    segOut = std::max (32, (int) ((double) sliceLen / rate));
}

void ReelVoice::render (float* out, int n)
{
    if (wlen <= 0) { env = 0.0f; return; }
    const int f = std::max (1, std::min (64, segOut / 2));
    for (int i = 0; i < n; ++i)
    {
        const int a = age % segOut;
        if (a == 0 && age > 0) rel = 0.0;                            // the slice loops while it fades
        float w = 1.0f;
        if (a < f) w = (float) a / (float) f;
        else if (a > segOut - f) w = (float) (segOut - a) / (float) f;
        double p = std::fmod ((double) (base - ws) + rel, (double) wlen);
        if (p < 0.0) p += (double) wlen;
        const double idx = (double) ws + p;
        float x = 0.5f * (reel.tape (0, idx) + reel.tape (1, idx));
        if (filterMode == 1) x = filt.lp (x);
        else if (filterMode == 2) x = filt.hp (x);
        out[i] += declick (x * w * env * level * 1.6f);
        env *= envK;
        rel += rate;
        ++age;
    }
    energy = std::min (1.0f, env * level);
}

float ReelVoice::position() const
{
    if (env < 1.0e-4f || wlen <= 0) return -1.0f;
    double p = std::fmod ((double) (base - ws) + std::fmod (rel, (double) sliceLen), (double) wlen);
    if (p < 0.0) p += (double) wlen;
    return (float) (p / (double) wlen);
}

} // namespace flt
