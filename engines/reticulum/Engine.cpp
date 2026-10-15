#include "Engine.h"

#include "artifex/core.hpp"
#include "shared/modulation.hpp"
#include "beads/floater_beads.h"

#include <cstring>

namespace rt
{

namespace
{
constexpr int kChunk = 32;

inline float softClip (float x)   // stmlib's SoftClip: x (27 + x^2) / (27 + 9 x^2), +-1 past +-3
{
    if (x < -3.0f) return -1.0f;
    if (x > 3.0f) return 1.0f;
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}
}

Engine::Engine()
    : ax (std::make_unique<artifex_fx::Core>()),
      axMod (std::make_unique<forsitan_mod::Modulation>()),
      beads (std::make_unique<floater_beads::Processor>())
{
    for (int i = 0; i < kTargets; ++i) mods[(size_t) i] = Modulator (0x51ed270bu + 0x9e3779b9u * (uint32_t) (i + 1));
    prepare (48000.0, 512);
}

Engine::~Engine() = default;

void Engine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    dt = (float) (1.0 / sampleRate);
    rete.setSampleRate ((float) sampleRate);
    ax->setRates ((float) sampleRate, artifex_fx::kHardwareBuffer);
    for (int m = 0; m < 4; ++m)
        bridges[(size_t) m].prepare (sampleRate, floater_beads::Processor::rate (m), floater_beads::Processor::block (m));
    beadsMedia = -1;
    const auto n = (size_t) std::max (maxBlock, kChunk);
    for (auto* v : { &wl, &wr, &al, &ar, &bl, &br }) v->assign (n, 0.0f);
    axTrig.assign (n, 0);
    reset();
}

void Engine::reset()
{
    knobsPrimed = false;
    prevDest.fill (false);
    syncIndex.fill (kNoIndex);
    clkLastIndex = kNoIndex;
    clkPhase = 0.0f;
    clkHighLeft = 0.0f;
    dcL = dcR = dcXL = dcXR = 0.0f;
}

void Engine::process (const Params& p, const float* inL, const float* inR, float* outL, float* outR, int n)
{
    if ((int) wl.size() < n)
        for (auto* v : { &wl, &wr, &al, &ar, &bl, &br }) v->assign ((size_t) n, 0.0f);
    if ((int) axTrig.size() < n) axTrig.assign ((size_t) n, 0);

    // transport: the host's when it plays, our own at its tempo when it does not
    Params q = p;
    if (! p.playing) q.ppq = internalPpq;
    const double beatsPerSample = q.bpm / 60.0 / sr;
    internalPpq = (p.playing ? p.ppq : internalPpq) + beatsPerSample * n;

    for (int o = 0; o < n; o += kChunk)
    {
        const int m = std::min (kChunk, n - o);
        Params c = q;
        c.ppq = q.ppq + beatsPerSample * o;
        chunk (c, inL ? inL + o : nullptr, inR ? inR + o : nullptr, outL + o, outR + o, m);
    }
}

void Engine::chunk (const Params& p, const float* inL, const float* inR, float* outL, float* outR, int n)
{
    // ---- the knobs move across the chunk, not in a step at its start
    const std::array<float, kTargets> knob { p.gain[0], p.gain[1], p.gain[2], p.gain[3], p.gain[4], p.gain[5], p.gain[6], p.gain[7],
                                             p.leak, p.scale, p.lvl };
    if (! knobsPrimed) { knobPrev = knob; knobsPrimed = true; }

    if (p.rangeA != lastRangeA) { rndA.rerange (lastRangeA, p.rangeA); lastRangeA = p.rangeA; }
    if (p.rangeB != lastRangeB) { rndB.rerange (lastRangeB, p.rangeB); lastRangeB = p.rangeB; }

    if (const auto s = pendingSeed.exchange (0, std::memory_order_relaxed); s != 0)
    {
        rete.reseed (s);
        vis.reseeds.fetch_add (1, std::memory_order_relaxed);
        vis.push (Visual::eReseed, s, 0.0f, clockSeconds);
    }
    if (pendingFire.exchange (0, std::memory_order_relaxed) > 0) fireLeft = 0.012f;
    if (const int pu = pendingPush.exchange (0, std::memory_order_relaxed); pu != 0)
    {
        if (pu & 1) { rndA.roll (p.rangeA); vis.rollsA.fetch_add (1, std::memory_order_relaxed); vis.push (Visual::eRollA, 1, rndA.values[0], clockSeconds); }
        if (pu & 2) { rndB.roll (p.rangeB); vis.rollsB.fetch_add (1, std::memory_order_relaxed); vis.push (Visual::eRollB, 1, rndB.values[0], clockSeconds); }
        if (pu & 4) { mg.regenerate (p.mgRange, p.mgBias, p.mgSH); vis.mgRolls.fetch_add (1, std::memory_order_relaxed); vis.push (Visual::eMg, 1, 0.0f, clockSeconds); }
    }

    const double beatsPerSample = p.bpm / 60.0 / sr;
    const float beatSec = (float) (60.0 / std::max (1.0, p.bpm));
    const bool pad = p.pad || midiHeld || uiPad.load (std::memory_order_relaxed);
    bool seedThisChunk = false;
    bool freezeGate = false;

    float* l = wl.data();
    float* r = wr.data();
    std::array<float, kNodes> g {};

    for (int i = 0; i < n; ++i)
    {
        const double ppq = p.ppq + beatsPerSample * i;
        const float frac = (float) (i + 1) / (float) n;
        clockSeconds += dt;

        // ---- the clock that presses the button
        bool clk = false;
        if (p.clockMode == cFree)
        {
            clkPhase += p.clockHz * dt / clkPeriodScale;
            if (clkPhase >= 1.0f)
            {
                clkPhase -= std::floor (clkPhase);
                clkPeriodScale = std::exp2 (p.flux * 2.0f * rng.bipolar());
                if (rng.uniform() < p.chance) clkHighLeft = p.width * clkPeriodScale / std::max (0.01f, p.clockHz);
            }
        }
        else if (p.clockMode == cSync)
        {
            const double beats = std::max (1.0e-3, syncBeats (std::max ((int) s4b, p.clockSync)));
            const auto idx = (int64_t) std::floor (ppq / beats);
            if (idx != clkLastIndex)
            {
                if (clkLastIndex != kNoIndex && rng.uniform() < p.chance) clkHighLeft = p.width * (float) (beats * beatSec);
                clkLastIndex = idx;
            }
        }
        if (clkHighLeft > 0.0f) { clk = true; clkHighLeft -= dt; }
        if (fireLeft > 0.0f) fireLeft -= dt;

        const bool in = pad || clk || fireLeft > 0.0f;
        const bool wasHigh = gate.high;
        const uint8_t bits = gate.process (in, p.length, dt);
        if (in != wasHigh)
        {
            if (in) vis.gateEdges.fetch_add (1, std::memory_order_relaxed);
            vis.push (in ? Visual::eGateOn : Visual::eGateOff, 0, 0.0f, clockSeconds);
        }

        // ---- the seven outputs to their destinations, on rising edges
        bool edge[kDests] {};
        for (int d = 0; d < kDests; ++d)
        {
            const bool s = ManualGate::output (bits, p.dest[(size_t) d]);
            edge[d] = s && ! prevDest[(size_t) d];
            prevDest[(size_t) d] = s;
        }
        freezeGate = ManualGate::output (bits, p.dest[dFreeze]);

        if (edge[dRete])
        {
            rete.reseed (rng.u32());
            vis.reseeds.fetch_add (1, std::memory_order_relaxed);
            vis.push (Visual::eReseed, rete.seed, 0.0f, clockSeconds);
        }
        if (edge[dRndA]) { rndA.roll (p.rangeA); vis.rollsA.fetch_add (1, std::memory_order_relaxed); vis.push (Visual::eRollA, 0, rndA.values[0], clockSeconds); }
        if (edge[dRndB]) { rndB.roll (p.rangeB); vis.rollsB.fetch_add (1, std::memory_order_relaxed); vis.push (Visual::eRollB, 0, rndB.values[0], clockSeconds); }
        if (edge[dMg]) { mg.regenerate (p.mgRange, p.mgBias, p.mgSH); vis.mgRolls.fetch_add (1, std::memory_order_relaxed); vis.push (Visual::eMg, 0, 0.0f, clockSeconds); }
        bool modGate = false;
        sinceModGate += dt;
        if (edge[dMod]) { if (sinceModGate > 0.005f && sinceModGate < 16.0f) gatePeriod = sinceModGate; sinceModGate = 0.0f; modGate = true; }
        sinceAxTrig += dt;
        axTrig[(size_t) i] = 0;
        if (edge[dArtifex])
        {
            if (sinceAxTrig > 1.0e-3f && sinceAxTrig < 4.0f) axTrigPeriod = sinceAxTrig;
            sinceAxTrig = 0.0f;
            axTrig[(size_t) i] = 1;
        }
        if (sinceAxTrig > 2.0f) axTrigPeriod = 0.0f;
        if (edge[dSeed]) { seedThisChunk = true; vis.push (Visual::eSeed, 0, 0.0f, clockSeconds); }

        mg.tick (dt, p.mgUni);

        // ---- the eleven modulators
        std::array<float, kTargets> eff {};
        for (int t = 0; t < kTargets; ++t)
        {
            const auto& ms = p.mod[(size_t) t];
            bool tick = false;
            float period = gatePeriod;
            if (ms.sync == sGate) tick = modGate;
            else if (ms.sync != sFree)
            {
                const double beats = syncBeats (ms.sync);
                const auto idx = (int64_t) std::floor (ppq / beats);
                if (idx != syncIndex[(size_t) ms.sync] && syncIndex[(size_t) ms.sync] != kNoIndex) tick = true;
                period = (float) beats * beatSec;
            }
            const float mv = mods[(size_t) t].process (ms, tick, period, dt);
            const float k = knobPrev[(size_t) t] + (knob[(size_t) t] - knobPrev[(size_t) t]) * frac;
            eff[(size_t) t] = std::clamp (k + mv * ms.depth * 0.5f * p.modAmount, 0.0f, 1.0f);
        }
        for (int s = s4b; s < kModSyncs; ++s)
            syncIndex[(size_t) s] = (int64_t) std::floor (ppq / syncBeats (s));

        // ---- the wire matrix: node gain CVs, excite
        auto source = [&] (int col, float ext) -> float
        {
            if (col < kColB) return rndA.values[(size_t) (col - kColA)];
            if (col < kColMg) return rndB.values[(size_t) (col - kColB)];
            if (col < kColExt) return mg.out[(size_t) (col - kColMg)];
            return ext;
        };
        const float extV = 5.0f * 0.5f * ((inL ? inL[i] : 0.0f) + (inR ? inR[i] : 0.0f));
        for (int k = 0; k < kNodes; ++k)
        {
            float v = 0.0f;
            for (uint32_t w = p.wire[(size_t) k]; w != 0; w &= w - 1)
                v += source (__builtin_ctz (w), extV);
            g[(size_t) k] = eff[(size_t) k] + v * 0.2f * p.cvDepth;
        }
        float ex = 0.0f;
        for (uint32_t w = p.wire[kNodes]; w != 0; w &= w - 1)
            ex += source (__builtin_ctz (w), extV);

        rete.process (g.data(), eff[8], eff[9], eff[10], ex, l[i], r[i]);

        if (++trailDiv >= 4)
        {
            trailDiv = 0;
            const int w = vis.trailWrite.load (std::memory_order_relaxed);
            auto& fr = vis.trail[(size_t) (w % Visual::kTrail)];
            for (int k = 0; k < kNodes; ++k) fr[(size_t) k] = rete.fb[k];
            vis.trailWrite.store (w + 1, std::memory_order_release);
        }
        for (int k = 0; k < kNodes; ++k)
            nodeEnv[(size_t) k] += (std::fabs (rete.fb[k]) - nodeEnv[(size_t) k]) * 0.002f;

        if (i == n - 1)
        {
            for (int t = 0; t < kTargets; ++t)
            {
                vis.effective[(size_t) t].store (eff[(size_t) t], std::memory_order_relaxed);
                vis.modOut[(size_t) t].store (mods[(size_t) t].value, std::memory_order_relaxed);
            }
            for (int k = 0; k < kNodes; ++k) vis.nodeGain[(size_t) k].store (std::clamp (g[(size_t) k], 0.0f, 1.0f), std::memory_order_relaxed);
            vis.excite.store (ex, std::memory_order_relaxed);
            vis.gateBits.store (bits, std::memory_order_relaxed);
            vis.gateIn.store (in, std::memory_order_relaxed);
        }
    }
    knobPrev = knob;

    for (int k = 0; k < kNodes; ++k) vis.node[(size_t) k].store (nodeEnv[(size_t) k], std::memory_order_relaxed);
    for (int k = 0; k < kRndOuts; ++k)
    {
        vis.rnd[(size_t) k].store (rndA.values[(size_t) k], std::memory_order_relaxed);
        vis.rnd[(size_t) (k + kRndOuts)].store (rndB.values[(size_t) k], std::memory_order_relaxed);
    }
    for (int k = 0; k < kMg; ++k)
    {
        vis.mg[(size_t) k].store (mg.out[(size_t) k], std::memory_order_relaxed);
        vis.mgHolding[(size_t) k].store (mg.src[(size_t) k].holding, std::memory_order_relaxed);
        vis.mgFreq[(size_t) k].store (mg.src[(size_t) k].freq, std::memory_order_relaxed);
        vis.mgWave[(size_t) k].store (mg.src[(size_t) k].lfoWave, std::memory_order_relaxed);
    }
    vis.seed.store (rete.seed, std::memory_order_relaxed);

    // ---- the effects, on the chunk (in volts; Beads in +-1 inside its own stage)
    const bool freeze = p.bdFreeze || freezeGate;
    if (freeze != vis.frozen.load (std::memory_order_relaxed)) vis.push (Visual::eFreeze, freeze ? 1u : 0u, 0.0f, clockSeconds);
    vis.frozen.store (freeze, std::memory_order_relaxed);
    Params pf = p;
    pf.bdFreeze = freeze;

    auto artifexStage = [&] (const float* il, const float* ir, float* ol, float* orr)
    {
        if (! p.axOn) { if (ol != il) { std::memcpy (ol, il, sizeof (float) * (size_t) n); std::memcpy (orr, ir, sizeof (float) * (size_t) n); } return; }
        forsitan_mod::ModIn min;
        min.dt = dt;
        min.bpm = (float) p.bpm;
        for (int i = 0; i < n; ++i)
        {
            axMod->process (min);
            artifex_fx::Ctl ct;
            ct.mode = std::clamp (p.axMode, 0, 8);
            ct.time = p.axTime;
            ct.feedback = p.axFbk;
            ct.amount = p.axAmt;
            ct.filter = p.axFilter;
            ct.stereo = p.axStereo;
            ct.trig = axTrig[(size_t) i] != 0;
            ct.stepped = axMod->stepped;
            ct.step = axMod->step;
            ct.stepSeconds = axMod->stepSeconds;
            ct.tempoSeconds = axMod->tempoSeconds;
            ct.trigPeriod = axTrigPeriod;
            ct.limiter = true;
            ct.dt = dt;
            const float xl = il[i] * p.axGain, xr = ir[i] * p.axGain;
            float yl = 0.0f, yr = 0.0f;
            ax->process (ct, xl, xr, yl, yr);
            ol[i] = il[i] + (yl - il[i]) * p.axMix;
            orr[i] = ir[i] + (yr - ir[i]) * p.axMix;
        }
    };
    auto beadsStage = [&] (const float* il, const float* ir, float* ol, float* orr)
    {
        if (! p.bdOn) { if (ol != il) { std::memcpy (ol, il, sizeof (float) * (size_t) n); std::memcpy (orr, ir, sizeof (float) * (size_t) n); } return; }
        for (int i = 0; i < n; ++i) { ol[i] = il[i] * 0.2f * p.bdGain; orr[i] = ir[i] * 0.2f * p.bdGain; }
        beadsChunk (pf, ol, orr, n, seedThisChunk || beadsSeedPending);
        beadsSeedPending = false;
        for (int i = 0; i < n; ++i)
        {
            const float yl = ol[i] * 5.0f, yr = orr[i] * 5.0f;
            ol[i] = il[i] + (yl - il[i]) * p.bdMix;
            orr[i] = ir[i] + (yr - ir[i]) * p.bdMix;
        }
    };

    float* a1 = al.data(); float* a2 = ar.data();
    float* b1 = bl.data(); float* b2 = br.data();
    switch (p.order)
    {
        case oBdAx:
            beadsStage (l, r, b1, b2);
            artifexStage (b1, b2, a1, a2);
            break;
        case oParallel:
            artifexStage (l, r, a1, a2);
            beadsStage (l, r, b1, b2);
            // each stage is dry + its own share; side by side, the two shares add on one dry
            for (int i = 0; i < n; ++i)
            {
                a1[i] = a1[i] + b1[i] - l[i];
                a2[i] = a2[i] + b2[i] - r[i];
            }
            break;
        default:
            artifexStage (l, r, a1, a2);
            beadsStage (a1, a2, b1, b2);
            std::memcpy (a1, b1, sizeof (float) * (size_t) n);
            std::memcpy (a2, b2, sizeof (float) * (size_t) n);
            break;
    }

    // ---- out: DC block, width, level, clip; volts to +-1
    const float lvl = std::pow (10.0f, p.level / 20.0f) * 0.2f;
    const float dcK = 1.0f - 2.0f * kPi * 8.0f / (float) sr;
    float clipped = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        float xl = a1[i], xr = a2[i];
        const float yl = xl - dcXL + dcK * dcL; dcXL = xl; dcL = yl;
        const float yr = xr - dcXR + dcK * dcR; dcXR = xr; dcR = yr;
        const float mid = 0.5f * (yl + yr), side = 0.5f * (yl - yr) * p.width2;
        xl = (mid + side) * lvl;
        xr = (mid - side) * lvl;
        if (std::fabs (xl) > 1.0f || std::fabs (xr) > 1.0f) clipped += 1.0f;
        if (p.clip == 1) { xl = softClip (xl); xr = softClip (xr); }
        else if (p.clip == 2) { xl = std::clamp (xl, -1.0f, 1.0f); xr = std::clamp (xr, -1.0f, 1.0f); }
        if (! std::isfinite (xl)) xl = 0.0f;
        if (! std::isfinite (xr)) xr = 0.0f;
        outL[i] = xl;
        outR[i] = xr;
        envL = std::max (std::fabs (xl), envL * 0.9995f);
        envR = std::max (std::fabs (xr), envR * 0.9995f);
        const int w = vis.scopeWrite.load (std::memory_order_relaxed);
        vis.scopeL[(size_t) (w % Visual::kScope)] = xl;
        vis.scopeR[(size_t) (w % Visual::kScope)] = xr;
        vis.scopeWrite.store (w + 1, std::memory_order_release);
    }
    clipEnv += (clipped / (float) n - clipEnv) * 0.05f;
    vis.levelL.store (envL, std::memory_order_relaxed);
    vis.levelR.store (envR, std::memory_order_relaxed);
    vis.clipAmount.store (clipEnv, std::memory_order_relaxed);
    vis.axMode.store (p.axMode, std::memory_order_relaxed);
}

void Engine::beadsChunk (const Params& p, float* l, float* r, int n, bool seed)
{
    const int media = std::clamp (p.bdQuality, 0, 3);
    if (media != beadsMedia)
    {
        beads->reset (media);   // clears the module's memory (no allocation): a new quality starts with an empty buffer
        beadsMedia = media;
    }
    floater_beads::Settings s;
    s.media = media;
    s.time = p.bdTime;
    s.size = p.bdSize;
    s.shape = p.bdShape;
    s.pitch = p.bdPitch;
    s.density = p.bdDensity;
    s.feedback = p.bdFbk;
    s.dryWet = 1.0f;
    s.reverb = p.bdVerb;
    s.random = p.bdRand;
    s.freeze = p.bdFreeze;
    s.seedPatched = p.dest[dSeed] != gOff;
    bool seedNow = seed;
    bridges[(size_t) media].run (l, r, l, r, n, [&] (const float* il, const float* ir, float* ol, float* orr, int k)
    {
        s.seed = seedNow;
        seedNow = false;
        beads->process (s, il, ir, ol, orr, k);
    });
    if (seedNow) beadsSeedPending = true;   // the bridge had no block to give it this chunk
}

} // namespace rt
