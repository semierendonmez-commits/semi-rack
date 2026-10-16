// The SPLIT: what the reel may touch and what it never does.
//
//   OFF    the reel takes everything
//   FIXED  a Linkwitz-Riley low pass at SPLIT Hz; the low band is the input's own, the reel gets the rest
//   TRACK  SUBSTRATE's pitch-tracked split: the input's fundamental (a one-period coherent average, so
//          every harmonic leaks out exactly) plus whatever sits under RATIO x f0. The split follows the
//          bass note, so a high note's fundamental is never cut off and a low note's second harmonic
//          can be given to the reel.
//
// low + up is the input, delayed: in OFF sample for sample, in FIXED / TRACK through the Linkwitz-Riley
// pair's all-pass (flat in level, a phase turn around the split, as any crossover). At CHAOS 0 nothing else.
// The delay is the look-ahead: PROTECT sees a hit before the reel does (3 ms in OFF / FIXED, SUBSTRATE's
// tracking latency, ~60 ms, in TRACK).
#pragma once

#include "Dsp.h"
#include "SubstrateCore.h"
#include <atomic>
#include <memory>
#include <vector>

namespace spb
{

enum SplitMode { splitOff = 0, splitFixed, splitTrack };

class Splitter
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        lookahead = std::max (1, (int) std::lround (0.003 * sr));
        core = std::make_unique<spb_substrate::Engine>();
        core->prepare (sr, 30.0);
        trackLatency = std::max (lookahead, core->getLatency());
        int size = 1;
        while (size < trackLatency + maxBlock + 8) size <<= 1;
        dl[0].assign ((size_t) size, 0.0f);
        dl[1].assign ((size_t) size, 0.0f);
        mask = size - 1;
        scratch[0].assign ((size_t) maxBlock, 0.0f);
        scratch[1].assign ((size_t) maxBlock, 0.0f);
        reset();
    }

    void reset()
    {
        for (auto& d : dl) std::fill (d.begin(), d.end(), 0.0f);
        w = 0;
        for (auto* f : { &a, &b, &aR, &bR, &ha, &hb, &haR, &hbR }) f->reset();
        fixedHz = -1.0f;
        if (core) core->reset();
        splitNow = 0.0f;
        f0Now = 0.0f;
        coreFill = 0;
    }

    // the mode decides the latency; a change resets the split (the processor tells the host)
    void setMode (int m)
    {
        if (m == mode) return;
        mode = m;
        reset();
    }
    int getMode() const { return mode; }
    int latencyFor (int m) const { return m == splitTrack ? trackLatency : lookahead; }
    int latency() const { return latencyFor (mode); }

    void setFixed (float hz) { fixedTarget = hz; }
    void setTrack (float ratio, float f0Max)
    {
        spb_substrate::Params p;
        p.splitRatio = ratio;
        p.f0Max = f0Max;
        p.residualDb = 0.0;
        p.subGainDb = 0.0;
        p.repairOn = false;          // the bus is not edited here: the split only, nothing repaired
        p.monitor = 2;               // the upper band, stereo
        p.crossover = 1;             // Linkwitz-Riley: nothing under the split leaks upwards
        p.highWidth = 1.0;
        p.highGainDb = 0.0;
        p.outGainDb = 0.0;
        p.stabilize = 0.3;
        if (core) core->setParams (p);
        ratioNow = ratio;
    }

    // n <= maxBlock
    void process (const float* inL, const float* inR, float* lowL, float* lowR, float* upL, float* upR, int n)
    {
        const int L = latency();
        for (int i = 0; i < n; ++i)
        {
            dl[0][(size_t) (w & mask)] = inL[i];
            dl[1][(size_t) (w & mask)] = inR[i];
            const int64_t ri = w - L;
            ++w;
            const float xl = ri >= 0 ? dl[0][(size_t) (ri & mask)] : 0.0f;
            const float xr = ri >= 0 ? dl[1][(size_t) (ri & mask)] : 0.0f;
            upL[i] = xl;            // the delayed input, split below
            upR[i] = xr;
        }

        if (mode == splitOff)
        {
            std::fill (lowL, lowL + n, 0.0f);
            std::fill (lowR, lowR + n, 0.0f);
            splitNow = 0.0f;
            return;
        }
        if (mode == splitFixed)
        {
            if (std::abs (fixedTarget - fixedHz) > 0.5f)
            {
                for (auto* f : { &a, &b, &aR, &bR, &ha, &hb, &haR, &hbR }) f->set (fixedTarget, 0.7071f, (float) sr);
                fixedHz = fixedTarget;
            }
            for (int i = 0; i < n; ++i)
            {
                // a Linkwitz-Riley pair: flat when summed, and nothing under the split in the upper band
                const float xl = upL[i], xr = upR[i];
                lowL[i] = b.lp (a.lp (xl));
                lowR[i] = bR.lp (aR.lp (xr));
                upL[i] = hb.hp (ha.hp (xl));
                upR[i] = hbR.hp (haR.hp (xr));
            }
            splitNow = fixedHz;
            return;
        }
        // TRACK: SUBSTRATE gives the upper band at its own latency, the input delayed by the same amount
        // is in upL / upR now; the low band is what is left
        std::copy (inL, inL + n, scratch[0].begin());
        std::copy (inR, inR + n, scratch[1].begin());
        float* io[2] { scratch[0].data(), scratch[1].data() };
        spb_substrate::Engine::Taps taps;
        taps.inLowL = lowL;
        taps.inLowR = lowR;
        core->setTaps (taps);
        core->process (io, 2, n);
        // low = the fundamental + the LR4 low of the rest, up = the LR4 high: low + up is the input with its
        // crossover's all-pass (flat), the fundamental itself exact. Until the pipeline has filled, the delayed
        // input goes through the low band as it is.
        const bool primed = coreFill >= trackLatency + (int64_t) (sr / 30.0) + 16;
        coreFill += n;
        for (int i = 0; i < n; ++i)
        {
            if (primed) { upL[i] = scratch[0][(size_t) i]; upR[i] = scratch[1][(size_t) i]; }
            else { lowL[i] = upL[i]; lowR[i] = upR[i]; upL[i] = upR[i] = 0.0f; }
        }
        const auto& m = core->getMeters();
        f0Now = (float) m.f0;
        splitNow = (float) (m.f0 * ratioNow);
    }

    float splitHz() const { return splitNow; }
    float f0() const { return f0Now; }
    float confidence() const { return core ? (float) core->getMeters().confidence : 0.0f; }

private:
    double sr = 48000.0;
    int mode = splitFixed;
    int lookahead = 144, trackLatency = 144, mask = 0;
    int64_t w = 0;
    std::vector<float> dl[2], scratch[2];
    dsp::Svf a, b, aR, bR, ha, hb, haR, hbR;
    float fixedHz = -1.0f, fixedTarget = 120.0f, ratioNow = 1.5f, splitNow = 0.0f, f0Now = 0.0f;
    int64_t coreFill = 0;
    std::unique_ptr<spb_substrate::Engine> core;
};

} // namespace spb
