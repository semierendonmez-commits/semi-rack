// Vendored from SEEDWALK Source/engine (JUCE-free already). Only change: the plugin.hpp include below.
#include "Engine.h"

// Semi-rack port (not in the plugin): artifex's headers include <rack.hpp>, whose own
// `#include <plugin.hpp>` resolves to src/plugin.hpp under the repo's -Isrc. Including the repo's
// plugin.hpp first makes that inner include a no-op (#pragma once), as in every module file.
#ifndef SEEDWALK_ENGINE_TEST   // tests/seedwalk_engine.cpp builds the engine with a Rack shim instead
#include "../../src/plugin.hpp"
#endif

#include "artifex/core.hpp"
#include "shared/modulation.hpp"
#include "beads/floater_beads.h"

#include <cstring>

namespace sw
{

namespace
{
constexpr int kChunk = 32;

inline float softClip (float x)   // stmlib's SoftClip
{
    if (x < -3.0f) return -1.0f;
    if (x > 3.0f) return 1.0f;
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

inline float frob (const Matrix& m)
{
    float s = 0.0f;
    for (auto& row : m) for (auto x : row) s += x * x;
    return std::sqrt (s);
}
}

Engine::Engine()
    : ax (std::make_unique<artifex_fx::Core>()),
      axMod (std::make_unique<forsitan_mod::Modulation>()),
      beads (std::make_unique<floater_beads::Processor>())
{
    // sixteen nets to start from: seeds in a row, a gentle middle
    for (int s = 0; s < kSteps; ++s)
    {
        Net n;
        n.seed = 0x5eed0000u + (uint32_t) s * 0x9e37u + 1u;
        for (auto& g : n.gain) g = 0.8f;
        n.leak = 0.45f + 0.02f * (float) s;
        n.scale = 0.45f + 0.025f * (float) s;
        n.lvl = 0.5f;
        nets[(size_t) s].store (n);
    }
    for (int l = 0; l < kLanes; ++l)
    {
        static const int d[kLanes] { lExcite, lKick, lHold, lDip, lSnap, lBeadsSeed, lArtifex, lBeadsFreeze };
        lanes[(size_t) l].store ({ 36 + l, d[l], false });
    }
    seq.seed (0x51ed5eedu);
    seq.matrix[(size_t) kSteps][1].store (100.0f);   // the device's 1..16 run, closed into a loop
    prepare (48000.0, 512);
}

Engine::~Engine() = default;

void Engine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    dt = (float) (1.0 / sampleRate);
    rete.setSampleRate ((float) sampleRate);
    seq.prepare (sampleRate);
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
    dcL = dcR = dcXL = dcXR = 0.0f;
    started = false;
    wasPlaying = false;
    patternIndex = INT64_MIN;
    exEnv.fill (0.0f);
    holdLeft.fill (0); dipLeft.fill (0); freezeLeft.fill (0); noteLeft.fill (0);
    noteOn.fill (false);
    dipEnv = 0.0f;
    midiEnv = 0.0f;
    if (! hasNet)
    {
        // start in step 1's net
        const Net n = nets[0].load();
        Rete::matrixFor (n.seed, to.m);
        to.gain = n.gain; to.leak = n.leak; to.scale = n.scale; to.lvl = n.lvl; to.norm = frob (to.m);
        from = now = to;
        glideT = 1.0f;
        toStep = 0;
        rete.matrix = now.m;
        hasNet = true;
    }
}

Net Engine::currentNet() const
{
    // the gains / leak / scale / lvl of where it is; the seed of the step it is gliding to
    Net n;
    n.seed = toStep >= 0 ? nets[(size_t) toStep].seed.load() : 1u;
    for (int k = 0; k < kNodes; ++k) n.gain[(size_t) k] = vis.gainNow[(size_t) k].load();
    n.leak = vis.leakNow.load();
    n.scale = vis.scaleNow.load();
    n.lvl = vis.lvlNow.load();
    return n;
}

void Engine::process (const Params& p, const float* inL, const float* inR, float* outL, float* outR, int n, MidiOut* midi)
{
    if ((int) wl.size() < n)
        for (auto* v : { &wl, &wr, &al, &ar, &bl, &br }) v->assign ((size_t) n, 0.0f);
    if ((int) axTrig.size() < n) axTrig.assign ((size_t) n, 0);

    const bool playing = p.playing;
    const double beatsPerSample = std::max (1.0, p.bpm) / 60.0 / sr;
    sixteenth = sr * 60.0 / std::max (1.0, p.bpm) / 4.0;
    timeMult = std::clamp (p.timeMult, 0.0625, 64.0);
    seq.setTiming (sixteenth, timeMult);
    for (int s = 0; s < kSteps; ++s) seq.steps[(size_t) s] = steps[(size_t) s].load();

    if (pendingFlush.exchange (false, std::memory_order_relaxed) && midi != nullptr)
        for (int l = 0; l < kLanes; ++l) if (noteOn[(size_t) l]) { midi->add (0, noteNum[(size_t) l], 0, false); noteOn[(size_t) l] = false; }

    const double ppq0 = playing ? p.ppq : internalPpq;
    internalPpq = ppq0 + beatsPerSample * n;

    // transport
    bool resetNow = pendingReset.exchange (false, std::memory_order_relaxed);
    const bool advance = p.run == rAlways || playing;
    if (playing && ! wasPlaying) resetNow = true;
    if (! started && advance) resetNow = true;
    if (p.run == rHost && ! playing && wasPlaying)
    {
        events.clear();
        seq.halt (0, events);
        if (midi != nullptr)
            for (int l = 0; l < kLanes; ++l) if (noteOn[(size_t) l]) { midi->add (0, noteNum[(size_t) l], 0, false); noteOn[(size_t) l] = false; }
        holdLeft.fill (0); dipLeft.fill (0); freezeLeft.fill (0);
        vis.push (Visual::eHalt, 0, 0, 0.0f, clockSeconds);
    }
    wasPlaying = playing;
    if (advance) started = true;

    for (int o = 0; o < n; o += kChunk)
    {
        const int m = std::min (kChunk, n - o);
        events.clear();
        if (resetNow) { seq.reset (0, p.startStep, events); resetNow = false; patternIndex = INT64_MIN; }
        if (pendingHalt.exchange (false, std::memory_order_relaxed)) seq.halt (0, events);
        if (const int j = pendingJump.exchange (-1, std::memory_order_relaxed); j >= 0) seq.jump (j, 0, events);
        if (advance)
        {
            const double ppqA = ppq0 + beatsPerSample * o;
            int cut = -1;
            if (p.patternBars > 0.0)
            {
                const double len = p.patternBars * 4.0;
                const auto i0 = (int64_t) std::floor (ppqA / len);
                const auto i1 = (int64_t) std::floor ((ppqA + beatsPerSample * m) / len);
                if (patternIndex == INT64_MIN) patternIndex = i0;
                if (i1 != patternIndex)
                {
                    cut = std::clamp ((int) std::ceil (((double) i1 * len - ppqA) / beatsPerSample - 1.0e-6), 0, m - 1);
                    patternIndex = i1;
                }
            }
            if (cut < 0) seq.process (m, sixteenth, timeMult, events);
            else
            {
                if (cut > 0) seq.process (cut, sixteenth, timeMult, events);
                seq.reset (cut, p.startStep, events);
                pinball::EventList<512> rest;
                seq.process (m - cut, sixteenth, timeMult, rest);
                for (int k = 0; k < rest.count; ++k) { auto e = rest.items[(size_t) k]; e.offset += cut; events.add (e); }
            }
        }
        chunk (p, inL ? inL + o : nullptr, inR ? inR + o : nullptr, outL + o, outR + o, m, midi, o);
    }
}

void Engine::startGlide (int step, const Params& p)
{
    const Net n = nets[(size_t) std::clamp (step, 0, kSteps - 1)].load();
    from = now;
    from.norm = frob (from.m);
    Rete::matrixFor (n.seed, to.m);
    to.gain = n.gain;
    to.leak = n.leak; to.scale = n.scale; to.lvl = n.lvl;
    to.norm = frob (to.m);
    const double g = (double) p.glide * sixteenth * timeMult;
    if (g < 1.0) { glideT = 1.0f; glideStep = 0.0f; }
    else { glideT = 0.0f; glideStep = (float) (1.0 / g); }
    fromStep = toStep;
    toStep = step;
    vis.arrivals.fetch_add (1, std::memory_order_relaxed);
    vis.push (Visual::eArrive, step, fromStep, (float) (g / sr), clockSeconds);
}

void Engine::handle (const pinball::Event& e, const Params& p, MidiOut* midi, int base)
{
    using E = pinball::Event;
    switch (e.type)
    {
        case E::Enter: vis.ballStep.store (e.step, std::memory_order_relaxed); break;
        case E::Fire:
            vis.fires.fetch_add (1, std::memory_order_relaxed);
            vis.push (Visual::eFire, e.step, 0, 0.0f, clockSeconds);
            startGlide (e.step, p);
            break;
        case E::Reset:
            vis.resets.fetch_add (1, std::memory_order_relaxed);
            vis.halted.store (false, std::memory_order_relaxed);
            vis.push (Visual::eReset, 0, 0, 0.0f, clockSeconds);
            break;
        case E::Halt:
            vis.halted.store (true, std::memory_order_relaxed);
            vis.push (Visual::eHalt, 0, 0, 0.0f, clockSeconds);
            break;
        case E::Idle:
            vis.push (Visual::eIdle, 0, 0, 0.0f, clockSeconds);
            break;
        case E::Choke:
            for (int l = 0; l < kLanes; ++l)
            {
                if (! (e.chokeMask & (1u << l))) continue;
                const Lane ln = lanes[(size_t) l].load();
                if (! ln.choke) continue;
                exEnv[(size_t) l] = 0.0f;
                holdLeft[(size_t) l] = dipLeft[(size_t) l] = freezeLeft[(size_t) l] = 0;
                if (noteOn[(size_t) l])
                {
                    if (midi != nullptr) midi->add (base + e.offset, noteNum[(size_t) l], 0, false);
                    noteOn[(size_t) l] = false;
                    noteLeft[(size_t) l] = 0;
                }
            }
            break;
        case E::Trig:
        {
            const int l = std::clamp ((int) e.lane, 0, kLanes - 1);
            const Lane ln = lanes[(size_t) l].load();
            const float vel = (float) e.velocity / 127.0f;
            const int dur = std::max (1, (int) (e.durMs * 0.001 * sr));
            vis.laneHits[(size_t) l].fetch_add (1, std::memory_order_relaxed);
            vis.push (Visual::eTrig, l, e.step, vel, clockSeconds);
            if (midi != nullptr)
            {
                if (noteOn[(size_t) l]) midi->add (base + e.offset, noteNum[(size_t) l], 0, false);
                noteNum[(size_t) l] = std::clamp (ln.note, 0, 127);
                midi->add (base + e.offset, noteNum[(size_t) l], std::max (1, (int) e.velocity), true);
            }
            noteOn[(size_t) l] = true;
            noteLeft[(size_t) l] = dur;
            switch (ln.dest)
            {
                case lExcite:
                    exEnv[(size_t) l] = vel;
                    exCoef[(size_t) l] = std::pow (0.05f, 1.0f / (float) dur);
                    break;
                case lKick: rete.kick (rng.u32(), vel); break;
                case lHold: holdLeft[(size_t) l] = dur; break;
                case lDip: dipLeft[(size_t) l] = dur; break;
                case lSnap: glideT = 1.0f; break;
                case lBeadsSeed: seedThisChunk = true; break;
                case lBeadsFreeze: freezeLeft[(size_t) l] = dur; break;
                case lArtifex:
                    if (sinceAxTrig > 1.0e-3f && sinceAxTrig < 4.0f) axTrigPeriod = sinceAxTrig;
                    sinceAxTrig = 0.0f;
                    axTrig[(size_t) std::clamp (e.offset, 0, (int) axTrig.size() - 1)] = 1;
                    break;
                default: break;
            }
            break;
        }
        default: break;
    }
}

void Engine::evaluate (const Params& p)
{
    const float k = walk::curve (p.curve, glideT);
    for (int i = 0; i < kNodes; ++i)
        for (int j = 0; j < kNodes; ++j)
        {
            const float a = from.m[(size_t) i][(size_t) j];
            now.m[(size_t) i][(size_t) j] = a + (to.m[(size_t) i][(size_t) j] - a) * k;
        }
    if (p.keepNorm)
    {
        const float target = from.norm + (to.norm - from.norm) * k;
        const float have = frob (now.m);
        if (have > 1.0e-6f)
        {
            const float s = target / have;
            for (auto& row : now.m) for (auto& x : row) x *= s;
        }
    }
    for (int i = 0; i < kNodes; ++i) now.gain[(size_t) i] = from.gain[(size_t) i] + (to.gain[(size_t) i] - from.gain[(size_t) i]) * k;
    now.leak = from.leak + (to.leak - from.leak) * k;
    now.scale = from.scale + (to.scale - from.scale) * k;
    now.lvl = from.lvl + (to.lvl - from.lvl) * k;
    rete.matrix = now.m;
}

void Engine::chunk (const Params& p, const float* inL, const float* inR, float* outL, float* outR, int n, MidiOut* midi, int base)
{
    seedThisChunk = false;
    bool freezeGate = false;
    float* l = wl.data();
    float* r = wr.data();
    std::fill (axTrig.begin(), axTrig.begin() + n, (uint8_t) 0);

    if (const float v = pendingExcite.exchange (0.0f, std::memory_order_relaxed); v > 0.0f)
    {
        midiEnv = std::max (midiEnv, v);
        midiCoef = std::pow (0.05f, 1.0f / (float) (0.25 * sr));
        vis.push (Visual::eMidiIn, 0, 0, v, clockSeconds);
    }

    const float dipK = 1.0f - std::exp (-1.0f / (0.015f * (float) sr));
    int ei = 0;
    std::array<float, kNodes> g {};
    float exV = 0.0f;
    bool holding = false;
    float scaleEff = now.scale;

    for (int i = 0; i < n; ++i)
    {
        clockSeconds += dt;
        sinceAxTrig += dt;
        while (ei < events.count && events.items[(size_t) ei].offset <= i)
            handle (events.items[(size_t) ei++], p, midi, base);

        // lanes
        holding = false;
        bool dipping = false, freezing = false;
        exV = 0.0f;
        for (int k = 0; k < kLanes; ++k)
        {
            if (exEnv[(size_t) k] > 1.0e-4f) { exV += exEnv[(size_t) k]; exEnv[(size_t) k] *= exCoef[(size_t) k]; }
            else exEnv[(size_t) k] = 0.0f;
            if (holdLeft[(size_t) k] > 0) { --holdLeft[(size_t) k]; holding = true; }
            if (dipLeft[(size_t) k] > 0) { --dipLeft[(size_t) k]; dipping = true; }
            if (freezeLeft[(size_t) k] > 0) { --freezeLeft[(size_t) k]; freezing = true; }
            if (noteOn[(size_t) k] && noteLeft[(size_t) k] > 0 && --noteLeft[(size_t) k] == 0)
            {
                if (midi != nullptr) midi->add (base + i, noteNum[(size_t) k], 0, false);
                noteOn[(size_t) k] = false;
            }
        }
        freezeGate |= freezing;
        if (midiEnv > 1.0e-4f) { exV += midiEnv; midiEnv *= midiCoef; } else midiEnv = 0.0f;

        // the glide
        if (glideT < 1.0f)
        {
            if (! holding) glideT = std::min (1.0f, glideT + glideStep);
            evaluate (p);
        }
        else if (glideT >= 1.0f && glideStep >= 0.0f)
        {
            glideStep = -1.0f;   // one last evaluation at 1, then rest
            evaluate (p);
        }
        dipEnv += ((dipping ? 1.0f : 0.0f) - dipEnv) * dipK;
        const float glideDip = glideT < 1.0f ? p.dip * std::sin (kPi * glideT) : 0.0f;
        // a dip closes the nodes (the loop opens and the net falls quiet) and lowers the drive
        const float closed = std::clamp (dipEnv + glideDip, 0.0f, 1.0f);
        scaleEff = now.scale + p.drive - 0.25f * closed;
        for (int k = 0; k < kNodes; ++k) g[(size_t) k] = std::clamp (now.gain[(size_t) k] + p.open, 0.0f, 1.0f) * (1.0f - closed);
        const float ext = 5.0f * 0.5f * ((inL ? inL[i] : 0.0f) + (inR ? inR[i] : 0.0f));
        const float excite = exV * 10.0f * p.excite + ext * p.ext;
        rete.process (g.data(), now.leak, scaleEff, now.lvl, excite, l[i], r[i]);

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
    }
    // events after the chunk's last sample (none should be, but never drop one)
    while (ei < events.count) handle (events.items[(size_t) ei++], p, midi, base);

    // ---- telemetry
    vis.matrixNow = now.m;
    for (int k = 0; k < kNodes; ++k)
    {
        vis.gainNow[(size_t) k].store (now.gain[(size_t) k], std::memory_order_relaxed);
        vis.node[(size_t) k].store (nodeEnv[(size_t) k], std::memory_order_relaxed);
    }
    vis.leakNow.store (now.leak, std::memory_order_relaxed);
    vis.scaleNow.store (now.scale, std::memory_order_relaxed);
    vis.lvlNow.store (now.lvl, std::memory_order_relaxed);
    vis.dipNow.store ((now.scale + p.drive - scaleEff) * 4.0f, std::memory_order_relaxed);
    vis.morph.store (walk::curve (p.curve, glideT), std::memory_order_relaxed);
    vis.fromStep.store (fromStep, std::memory_order_relaxed);
    vis.toStep.store (toStep, std::memory_order_relaxed);
    vis.holding.store (holding, std::memory_order_relaxed);
    vis.excite.store (exV * 10.0f * p.excite, std::memory_order_relaxed);
    vis.idle.store (seq.currentStep.load (std::memory_order_relaxed) < 0, std::memory_order_relaxed);
    for (int k = 0; k < kLanes; ++k)
    {
        const int d = lanes[(size_t) k].dest.load (std::memory_order_relaxed);
        float v = exEnv[(size_t) k];
        if (d == lHold) v = holdLeft[(size_t) k] > 0 ? 1.0f : 0.0f;
        else if (d == lDip) v = dipLeft[(size_t) k] > 0 ? 1.0f : 0.0f;
        else if (d == lBeadsFreeze) v = freezeLeft[(size_t) k] > 0 ? 1.0f : 0.0f;
        else if (d != lExcite) v = noteOn[(size_t) k] ? 1.0f : 0.0f;
        vis.laneEnv[(size_t) k].store (v, std::memory_order_relaxed);
    }
    if (++heatDiv >= 8)
    {
        heatDiv = 0;
        float mg = 0.0f;
        for (int k = 0; k < kNodes; ++k) mg += std::clamp (g[(size_t) k], 0.0f, 1.0f);
        vis.heatNow.store (walk::heatOf (now.m, scaleEff, mg / (float) kNodes, now.leak), std::memory_order_relaxed);
    }

    // ---- the effects, on the chunk (in volts; Beads in +-1 inside its own stage)
    const bool freeze = p.bdFreeze || freezeGate;
    vis.frozen.store (freeze, std::memory_order_relaxed);
    Params pf = p;
    pf.bdFreeze = freeze;
    const bool seedNow = seedThisChunk || beadsSeedPending;

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
            ct.trigPeriod = sinceAxTrig > 2.0f ? 0.0f : axTrigPeriod;
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
        beadsSeedPending = false;
        beadsChunk (pf, ol, orr, n, seedNow);
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
            for (int i = 0; i < n; ++i) { a1[i] = a1[i] + b1[i] - l[i]; a2[i] = a2[i] + b2[i] - r[i]; }
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
        beads->reset (media);
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
    bool seedPatched = false;
    for (int k = 0; k < kLanes; ++k) seedPatched |= lanes[(size_t) k].dest.load (std::memory_order_relaxed) == lBeadsSeed;
    s.seedPatched = seedPatched;
    bool seedNow = seed;
    bridges[(size_t) media].run (l, r, l, r, n, [&] (const float* il, const float* ir, float* ol, float* orr, int k)
    {
        s.seed = seedNow;
        seedNow = false;
        beads->process (s, il, ir, ol, orr, k);
    });
    if (seedNow) beadsSeedPending = true;
}

} // namespace sw
