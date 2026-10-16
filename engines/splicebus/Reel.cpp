// SEMI port: vendored from SPLICEBUS Source/engine (9cefada); namespaces sb -> spb, substrate -> spb_substrate; see sources.mk.
#include "Reel.h"
#include <algorithm>
#include <cmath>

namespace spb
{
using namespace dsp;

// SEMI port: 20 s of tape in Rack as in the plug-in, 10 s on the MetaModule (shared plugin RAM, 48 kHz).
#ifdef METAMODULE
namespace { constexpr double kTapeSeconds = 10.0; constexpr int kMaxPeaks = (int) (kTapeSeconds * 96000.0) / Reel::kPeak + 2; }
#else
namespace { constexpr double kTapeSeconds = 20.0; constexpr int kMaxPeaks = (int) (kTapeSeconds * 192000.0) / Reel::kPeak + 2; }
#endif

// the display's peaks are allocated once, for the highest rate: the editor reads them without a lock,
// so the buffer must never move (a sample-rate change only changes how much of it is used)
Reel::Reel() { display.peaks.assign ((size_t) kMaxPeaks, 0.0f); }

void Reel::prepare (double sampleRate)
{
    sr = sampleRate;
    // twenty seconds of tape: eight bars down to 96 BPM, fewer bars below (see updateWindow)
    cap = std::min<int64_t> (((int64_t) (kTapeSeconds * sr) / kPeak + 1) * kPeak, (int64_t) (kMaxPeaks - 1) * kPeak);
    tapeL.assign ((size_t) cap, 0.0f);
    tapeR.assign ((size_t) cap, 0.0f);
    display.peakCap.store ((int) (cap / kPeak), std::memory_order_relaxed);
    dryStep = 1.0f / (0.003f * (float) sr);
    hpHz = -1.0f;
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
    wallGain = wallTarget = 1.0f;
    peakAcc = 0.0f;
    peakFill = genesStarted = genesBroken = 0;
    std::fill (tapeL.begin(), tapeL.end(), 0.0f);
    std::fill (tapeR.begin(), tapeR.end(), 0.0f);
    std::fill (display.peaks.begin(), display.peaks.end(), 0.0f);   // in place: no reallocation
    for (auto* f : { &hpA, &hpB, &hpAR, &hpBR }) f->reset();
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
    if ((int64_t) (L + 2) * barLen > cap) { windowValid = false; return; }
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
        case Event::gene:    startGene (e.g); break;
        case Event::wallOn:  wallTarget = 0.0f; break;
        case Event::wallOff: wallTarget = 1.0f; break;
    }
}

void Reel::startGene (const Gene& g)
{
    ++genesStarted;
    dryTarget = g.broken ? 0.0f : 1.0f;
    if (! g.broken) return;
    ++genesBroken;

    Voice* v = &voices[0];
    for (auto& c : voices)
    {
        if (! c.on) { v = &c; break; }
        if (c.age > v->age) v = &c;
    }
    // MORPH: the window against the gene: gaps, seamless, then overlapping into layers
    const float m = clamp (settings.morph, 0.0f, 1.0f);
    float wf = m < 0.35f ? 0.3f + 2.0f * m : (m <= 0.6f ? 1.0f : 1.0f + 2.0f * (m - 0.6f) / 0.4f);
    const int geneLen = std::max (64, g.length);
    const int sub = std::max (1, std::min (8, g.sub));
    if (sub > 1) wf = std::min (wf, 1.0f);                           // a stutter never overlaps itself
    if (g.live) wf = 1.0f;                                           // a tape stop is one piece
    const int fadeMin = (int) (0.003 * sr);
    v->segLen = std::max (32, geneLen / sub);
    v->winLen = std::max (32, (int) ((float) v->segLen * wf));
    v->fade = wf > 1.0f ? std::max (fadeMin, (int) ((float) v->winLen * 0.5f * (m - 0.6f) / 0.4f)) : fadeMin;
    v->fade = std::max (1, std::min (v->fade, v->winLen / 2));
    v->life = (sub - 1) * v->segLen + v->winLen;
    v->sub = sub > 1;
    v->gain = wf > 1.0f ? 1.0f / std::sqrt (wf) : 1.0f;
    if (g.live)
    {
        // the tape under the record head, read no faster than it is written: it can only fall behind
        v->base = rec;
        v->len = (int) std::min<int64_t> (cap - 1, 1 << 30);
        v->rel = 0.0;
    }
    else if (windowValid && ! g.recent)
    {
        const int sp = std::max (0, std::min (numSplices - 1, g.splice));
        v->base = winStart + (int64_t) sp * spliceLen;
        v->len = spliceLen;
        v->rel = (double) clamp (g.offset, 0.0f, 1.0f) * spliceLen;
    }
    else
    {
        // a roll, or no splices yet (the first bars): stutter what just went by
        const int seg = v->segLen;
        if (rec < seg + 1) { dryTarget = 1.0f; return; }
        v->base = rec - seg;
        v->len = seg;
        v->rel = 0.0;
    }
    if (g.rate < 0.0f && ! g.live) v->rel += (double) v->segLen * std::abs (g.rate);   // backwards from its end
    v->rel0 = v->rel;
    v->rate = g.rate;
    v->rateStep = (double) (g.rateEnd - g.rate) / (double) std::max (1, v->life);
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
    v.rate += v.rateStep;
    if (++v.age >= v.life) v.on = false;
    outR = tape (1, idx) * g;
    return tape (0, idx) * g;
}

//==============================================================================
void Reel::process (float* l, float* r, const float* guard, int n, int64_t now)
{
    const float hp = settings.wetHp;
    if (hp > 0.0f && std::abs (hp - hpHz) > 0.5f)
    {
        for (auto* f : { &hpA, &hpB, &hpAR, &hpBR }) f->set (hp, 0.7071f, (float) sr);
        hpHz = hp;
    }
    const float wallStep = 1.0f / (0.004f * (float) sr);
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
            if (wallGain < wallTarget) wallGain = std::min (wallTarget, wallGain + wallStep);
            else if (wallGain > wallTarget) wallGain = std::max (wallTarget, wallGain - wallStep);
            float wl = 0.0f, wr = 0.0f;
            for (auto& v : voices)
                if (v.on) { float vr; wl += voiceTick (v, vr); wr += vr; }
            if (hp > 0.0f) { wl = hpB.hp (hpA.hp (wl)); wr = hpBR.hp (hpAR.hp (wr)); }
            // out = x + mix * (reel - x): exactly x while the reel is out, so nothing switches
            const float g = guard != nullptr ? clamp (guard[i], 0.0f, 1.0f) : 0.0f;
            const float mix = (1.0f - dryGain) * (1.0f - g);
            const float w = wallGain + (1.0f - wallGain) * g;              // a guarded hit comes through a wall too
            l[i] = (xl + mix * (wl - xl)) * w;
            r[i] = (xr + mix * (wr - xr)) * w;
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
    D.walled.store (wallTarget < 0.5f, std::memory_order_relaxed);
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
            if (p < 0.0f || p > 1.0f) p = 1.0f;                       // a roll reads what just went by
        }
        D.voicePos[(size_t) k].store (p, std::memory_order_relaxed);
    }
}

} // namespace spb
