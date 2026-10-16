// SEMI Family port: vendored from semierendonmez-commits/neurostack Source/engine (936bb86). Only change:
// namespaces neuro -> nst::neuro and walk -> nst::walk (other engines in the binary use both names).
#include "Neuro.h"

#include <algorithm>

namespace nst::neuro
{
using dsp::kTwoPi;

namespace
{
inline double noteHz (double note) { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }
bool onReset (double pos16, double len)
{
    if (len <= 0.0) return false;
    double r = std::fmod (pos16, len);
    if (r < 0.0) r += len;
    return r < 1.0e-6 || len - r < 1.0e-6;
}
// tempo-synced LFO, from MOTION: 2 bars, 1 bar, 1/2, 1/4 (a beat), dotted 1/8, 1/8 triplet, 1/8.
// The bass loops analysed move at 1/4 to 2 cycles per beat (peaks at 1 bar, 1/2, a beat, dotted 1/8).
inline double lfoCyclesPerBeat (float motion)
{
    static const double c[] = { 0.125, 0.25, 0.5, 1.0, 4.0 / 3.0, 3.0, 2.0 };
    static const int order[] = { 0, 1, 2, 3, 4, 6, 5 };   // slow to fast: ..., dotted 1/8, 1/8, 1/8 triplet
    return c[order[std::clamp ((int) (motion * 6.99f), 0, 6)]];
}
inline uint32_t hash32 (uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}
struct Lcg { uint32_t s; float unit() { s = s * 1664525u + 1013904223u; return (float) (s >> 8) * (1.0f / 16777216.0f); } };
inline float white (uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (int32_t) s * (1.0f / 2147483648.0f); }
inline float lerp (float a, float b, float t) { return a + (b - a) * t; }
// every model's output trimmed to the same loudness: at LEVEL 50 % (the default) the layers sit
// about 14 dB under a 70 % sub (the analysed loops: most of the energy below 80 Hz, the layers ~15 dB down)
constexpr float kModelTrim[NumModels] { 0.272f, 0.282f, 0.224f, 0.178f, 0.275f, 0.115f, 0.112f, 0.484f, 0.501f, 0.218f, 0.513f, 0.955f };
} // namespace

void Engine::prepare (double sampleRate, int)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
}

void Engine::reset()
{
    for (auto& v : voices) v = Voice {};
    clock = 0;
    phi = 0.0;
    f0 = f0Target = 55.0;
    bend = 0.0;
    stepOct = 0;
    numHeld = 0;
    latched = -1;
    keyEnv = keyTarget = 0.0f;
    keyDown = false;
    sumEnvSmooth = 0.0f;
    xa.reset(); xb.reset(); xc.reset(); xd.reset();
    dly.fill (0.0f);
    dlyPos = 0;
    freePos = 0.0;
    lastPos = -1.0e9;
    nextTick = INT64_MIN;
    wasRunning = false;
    pendingReset = false;
    numQueued = 0;
    nextCell = INT64_MIN;
    rhyGate = rhySlide = rhyOct = gateCut = false;
    rhySemis = 0;
    gateOffAt = INT64_MAX;
    bendNow = bendStep = 0.0;
    varCycle = budgetBlock = INT64_MIN;
    budgetLeft = varChanged = 0;
    duckEnv = scEnv = duckNow = 0.0f;
    duckAtk = false;
    velSmooth = 1.0f;
    arcPos = 0.0;
}

void Engine::emit (const FireEvent& e)
{
    const int w = fireWrite.load (std::memory_order_relaxed);
    if (w - fireRead.load (std::memory_order_acquire) >= (int) fires.size() - 1) return;
    fires[(size_t) (w % (int) fires.size())] = e;
    fireWrite.store (w + 1, std::memory_order_release);
}

bool Engine::popFire (FireEvent& e)
{
    const int r = fireRead.load (std::memory_order_relaxed);
    if (r == fireWrite.load (std::memory_order_acquire)) return false;
    e = fires[(size_t) (r % (int) fires.size())];
    fireRead.store (r + 1, std::memory_order_release);
    return true;
}

//==============================================================================
void Engine::startArt (int a, float vel, int64_t gate, const Params& p, bool chokeOthers, float sweep)
{
    const float xf = (float) std::max (1.0, p.xfadeMs * 0.001 * sr);
    if (chokeOthers)
        for (int j = 0; j < NumArts; ++j)
            if (j != a && voices[(size_t) j].active && p.art[(size_t) j].choke)
            {
                voices[(size_t) j].target = 0.0f;
                voices[(size_t) j].rate = 1.0f / xf;
                voices[(size_t) j].gateLeft = 0;
            }
    auto& v = voices[(size_t) a];
    if (! v.active || v.env < 1.0e-4f)
    {
        // joins on the master phase: its fundamental sits where every other layer's does
        v.p1 = v.p2 = v.p3 = v.pm = phi;
        v.f1.reset(); v.f2.reset(); v.f3.reset(); v.f4.reset();
        v.fb = 0.0f;
        v.cohC = v.cohS = v.cohE = 0.0f;
    }
    v.active = true;
    v.target = 1.0f;
    v.rate = 1.0f / std::max (xf, (float) (p.art[(size_t) a].attackMs * 0.001 * sr));
    v.gateLeft = gate;
    v.gateTotal = std::max<int64_t> (1, gate);
    v.elapsed = 0;
    v.sweep = sweep;
    v.vel = vel;
    v.e = 1.0f;
    const double t = 0.02 * std::pow (30.0, (double) p.art[(size_t) a].motion);
    v.eDecay = (float) std::exp (-4.6 / (t * sr));
    v.lfo = 0.0;                       // the switch restarts the motion
    keyArt = a;
    viewArt.store (a, std::memory_order_relaxed);
}

void Engine::releaseAll (float ms)
{
    const float r = 1.0f / (float) std::max (1.0, (double) ms * 0.001 * sr);
    for (auto& v : voices)
        if (v.active) { v.target = 0.0f; v.rate = r; v.gateLeft = 0; }
}

void Engine::fire (const Pending& e, const Params& p)
{
    FireEvent fe;
    fe.step = (int8_t) e.step;
    fe.art = (int8_t) (e.mute ? -2 : e.art);
    fe.from = (int8_t) e.from;
    fe.vel = (uint8_t) std::clamp ((int) std::lround (e.vel * 100.0f), 0, 255);
    emit (fe);
    viewStep.store (e.step, std::memory_order_relaxed);
    if (e.mute) { releaseAll (p.xfadeMs); return; }
    if (e.art < 0 || e.art >= NumArts) return;   // hold: the last one keeps going
    stepOct = e.oct;
    startArt (e.art, e.vel, (int64_t) std::max (1.0f, e.durSamples), p, true, e.mod);
}

//==============================================================================
float Engine::renderArt (int model, Voice& v, const ArtParams& ap, double lfoInc)
{
    const double dphi = f0 / sr;
    const float tone = ap.tone, motion = ap.motion, grit = ap.grit;
    const bool ctl = (clock & 7) == 0;          // filter coefficients at control rate
    v.lfo += lfoInc;
    if (v.lfo >= 1.0) v.lfo -= 1.0;
    const float lfoSin = (float) std::sin (kTwoPi * v.lfo);
    const float lfoUni = 0.5f - 0.5f * (float) std::cos (kTwoPi * v.lfo);
    float y = 0.0f;
    switch (model)
    {
        case Reese:
        {
            // three saws: one on the master phase, two detuned around it
            const double d = 0.002 + 0.022 * motion * motion;
            v.p2 = dsp::wrap (v.p2 + dphi * (1.0 + d));
            v.p3 = dsp::wrap (v.p3 + dphi * (1.0 - d * 0.93));
            const float x = (dsp::saw (phi, dphi) + dsp::saw (v.p2, dphi * (1.0 + d)) + dsp::saw (v.p3, dphi * (1.0 - d))) * 0.36f;
            if (ctl) v.f1.set (f0 * (1.5 + 46.0 * tone * tone), sr, 0.7 + 0.8 * motion);
            y = dsp::drive (v.f1.low (x), grit);
            break;
        }
        case Wobble:
        {
            const float x = 0.62f * dsp::saw (phi, dphi) + 0.38f * (dsp::saw (phi, dphi) - dsp::saw (dsp::wrap (phi + 0.5), dphi));
            if (ctl) v.f1.set (f0 * (1.2 + (2.0 + 60.0 * tone) * lfoUni * lfoUni), sr, 2.2);
            y = dsp::drive (v.f1.low (x) * 1.1f, 0.15f + grit);
            break;
        }
        case Growl:
        {
            // phase modulation (ratio 1, an index that breathes with the LFO) through moving formants
            const float idx = 0.6f + tone * 5.5f * (0.4f + 0.6f * lfoUni);
            const float x = (float) std::sin (kTwoPi * phi + idx * std::sin (kTwoPi * phi));
            const float vow = lfoUni;   // a -> o -> u
            if (ctl)
            {
                const double fA = vow < 0.5f ? lerp (730.0f, 570.0f, vow * 2.0f) : lerp (570.0f, 300.0f, vow * 2.0f - 1.0f);
                const double fB = vow < 0.5f ? lerp (1090.0f, 840.0f, vow * 2.0f) : lerp (840.0f, 870.0f, vow * 2.0f - 1.0f);
                v.f1.set (fA, sr, 5.0);
                v.f2.set (fB, sr, 6.0);
            }
            y = dsp::drive (0.35f * x + 1.6f * v.f1.band (x) + 1.3f * v.f2.band (x), 0.2f + grit);
            break;
        }
        case FmStab:
        {
            // ratio 2 modulator, an index that falls after every trigger, feedback = GRIT
            v.pm = dsp::wrap (v.pm + dphi * 2.0);
            const float idx = (0.8f + 14.0f * tone) * (0.15f + 0.85f * v.e);
            const float x = (float) std::sin (kTwoPi * phi + idx * std::sin (kTwoPi * v.pm) + grit * 1.4f * v.fb);
            v.fb = x;
            y = dsp::drive (x, 0.3f * grit);
            break;
        }
        case Hoover:
        {
            // three pulse-width-modulated saws diving into the note after every trigger
            const double r = std::pow (2.0, (double) tone * 12.0 * v.e / 12.0);
            v.p1 = dsp::wrap (v.p1 + dphi * r);
            v.p2 = dsp::wrap (v.p2 + dphi * r * 1.0071);
            v.p3 = dsp::wrap (v.p3 + dphi * r * 0.9932);
            const double w = 0.5 + 0.35 * lfoSin;
            float x = 0.0f;
            for (double q : { v.p1, v.p2, v.p3 }) x += dsp::saw (q, dphi * r) - dsp::saw (dsp::wrap (q + w), dphi * r);
            if (ctl) v.f1.set (f0 * 14.0, sr, 0.7);
            y = dsp::drive (v.f1.low (x * 0.3f), grit);
            break;
        }
        case Talk:
        {
            // a saw through three formants, "o" to "i" as the trigger envelope falls (yoi)
            const float x = dsp::saw (phi, dphi);
            const float t = 1.0f - v.e;
            if (ctl)
            {
                const double sp = 0.6 + 0.8 * tone;
                v.f1.set (lerp (570.0f, 270.0f, t), sr, 5.0 * sp);
                v.f2.set (lerp (840.0f, 2290.0f, t), sr, 7.0 * sp);
                v.f3.set (lerp (2410.0f, 3010.0f, t), sr, 8.0 * sp);
            }
            y = dsp::drive (0.12f * x + 1.4f * v.f1.band (x) + 1.2f * v.f2.band (x) + 0.7f * v.f3.band (x), 0.15f + grit);
            break;
        }
        case Screech:
        {
            // a saw hard-synced to the master phase, its ratio swept by the LFO, high-passed
            const double ratio = 1.0 + (1.0 + 7.0 * tone) * (0.35 + 0.65 * lfoUni);
            const double before = v.p1;
            v.p1 += dphi * ratio;
            if (phi < dphi) v.p1 = phi * ratio;          // the master wrapped: sync
            else if (v.p1 >= 1.0) v.p1 -= 1.0;
            (void) before;
            const float x = (float) (2.0 * v.p1 - 1.0);
            if (ctl)
            {
                v.f1.set (f0 * 2.0, sr, 0.7);
                v.f2.set (std::min (sr * 0.4, f0 * ratio), sr, 3.0 + 4.0 * motion);
            }
            const float h = v.f1.high (x);
            y = dsp::drive (0.6f * h + 0.8f * v.f2.band (h), 0.25f + grit);
            break;
        }
        case Drop808:
        {
            // a sine that falls into the note after every trigger (a resampled 808 kick as the bass)
            const double r = std::pow (2.0, (double) tone * 24.0 * v.e * v.e / 12.0);
            v.p1 = dsp::wrap (v.p1 + dphi * r);
            const float x = (float) std::sin (kTwoPi * v.p1) * (0.85f + 0.3f * v.e);
            y = dsp::drive (x, 0.1f + grit);
            break;
        }
        case SolidFm:
        {
            // DX100 "Solid Bass": 2 operators, ratio 1 (MOTION above half: 2), an index that falls, feedback = GRIT
            const double ratio = motion > 0.5f ? 2.0 : 1.0;
            v.pm = dsp::wrap (v.pm + dphi * ratio);
            const float mod = (float) std::sin (kTwoPi * v.pm + grit * 1.2f * v.fb);
            v.fb = mod;
            const float idx = 0.4f + 2.6f * tone + 5.0f * v.e;
            y = dsp::drive ((float) std::sin (kTwoPi * phi + idx * mod), 0.15f);
            break;
        }
        case Hollow:
        {
            // a pulse through a resonant low-pass, no sustain: it dies away even while held
            const double pw = 0.5 - 0.42 * (double) grit;
            const float x = dsp::saw (phi, dphi) - dsp::saw (dsp::wrap (phi + pw), dphi);
            if (ctl) v.f1.set (f0 * (1.4 + 30.0 * tone * v.e), sr, 4.5);
            y = v.f1.low (x) * 0.9f * v.e;
            break;
        }
        case SemiSines:
        {
            // sines a fraction of a semitone apart (up to one): the beating is the movement
            const double det = 0.05 + 0.95 * motion;
            v.p2 = dsp::wrap (v.p2 + dphi * std::pow (2.0, det / 12.0));
            const float l2 = 0.3f + 0.7f * tone;
            const float x = ((float) std::sin (kTwoPi * phi) + l2 * (float) std::sin (kTwoPi * v.p2)) / (1.0f + l2);
            y = dsp::drive (x, grit);
            break;
        }
        case Snarl:
        default:
        {
            // a folded sine plus a snarl of band-passed noise, crushed by GRIT
            const float amt = 1.0f + 6.0f * tone * (0.4f + 0.6f * lfoUni);
            float x = (float) std::sin (amt * std::sin (kTwoPi * phi));
            if (ctl) v.f1.set (f0 * (3.0 + 10.0 * motion), sr, 4.0);
            x += 0.45f * v.f1.band (white (v.noise));
            const int hold = 1 + (int) (grit * grit * 14.0f);
            if (++v.holdCount >= hold) { v.holdCount = 0; v.hold = x; }
            const float steps = 8.0f + (1.0f - grit) * 120.0f;
            y = std::round (v.hold * steps) / steps;
            break;
        }
    }
    v.e *= v.eDecay;
    return y;
}

//==============================================================================
// The variation: every two bars (a cycle of 32 cells) the rhythm is copied and a few rules may
// edit it. Rules (rates from the 119 bass loops analysed, and the roller lesson):
//   DROP FIRST  the first beat of a bar left empty (19 % of the loops)        start of a block
//   PICKUP      a short note in the last beat of the cycle (26 %)              end of a block
//   TENSION     a flat-2 a 16th before a root note                            end of a block
//   ECHO        a note repeated a 16th later, quieter
//   OCTAVE      a short note after the snare an octave up, on the layers only (the sub stays)
// Probability = rule rate x VARIATION x 4 x the place in the phrase (x2 at a 16-bar boundary,
// x1.5 at 8, x1 at 4, x0.5 elsewhere); BUDGET edits per 8 bars at most, so a roller stays a roller.
void Engine::buildVariant (const Brain& b, const Params& p, int64_t cycle)
{
    varCycle = cycle;
    const int len = b.rhythmLen.load (std::memory_order_relaxed) == 16 ? 16 : kRhythm;
    std::array<VCell, kRhythm> base {};
    for (int i = 0; i < kRhythm; ++i)
    {
        const auto& c = b.rhythm[(size_t) (i % len)];
        VCell v;
        v.type = (int8_t) c.type.load (std::memory_order_relaxed);
        v.semis = (int8_t) std::clamp (snapToScale (c.semis.load (std::memory_order_relaxed), p.scale), -48, 48);
        v.slide = (int8_t) c.slide.load (std::memory_order_relaxed);
        v.bend = (int8_t) std::clamp (c.bend.load (std::memory_order_relaxed), -48, 48);
        v.vel = (uint8_t) std::clamp (c.vel.load (std::memory_order_relaxed), 1, 127);
        base[(size_t) i] = v;
    }
    var = base;
    varChanged = 0;
    const int barsPerCycle = p.rhythmHalf ? 4 : 2;
    const int64_t bar0 = cycle * barsPerCycle;
    const int64_t block = (bar0 >= 0 ? bar0 : bar0 - 7) / 8;
    if (block != budgetBlock) { budgetBlock = block; budgetLeft = std::max (0, p.budget); }
    if (p.variation > 0.0f && budgetLeft > 0)
    {
        Lcg rng { hash32 (p.seed * 7919u + 13u, (uint32_t) (cycle & 0xffffffff)) };
        auto hier = [] (int64_t boundaryBar)
        {
            const int64_t m = boundaryBar < 0 ? -boundaryBar : boundaryBar;
            return m % 16 == 0 ? 2.0f : m % 8 == 0 ? 1.5f : m % 4 == 0 ? 1.0f : 0.5f;
        };
        const float mStart = hier (bar0), mEnd = hier (bar0 + barsPerCycle);
        const auto& sc = scaleSteps (p.scale);
        const int second = p.scale == ScaleChromatic ? 2 : sc[1], third = p.scale == ScaleChromatic ? 3 : sc[2];
        const int seventh = (p.scale == ScaleChromatic ? 10 : sc[6]) - 12, fifth = 7;
        auto vocab = [&] (float r)
        {
            // pickup notes: the root and its octave first, then the flat-7 under, the 2nd, the 5th
            return r < 0.3f ? 0 : r < 0.6f ? 12 : r < 0.8f ? seventh : r < 0.9f ? second : (r < 0.95f ? fifth : third);
        };
        auto fire = [&] (float rate, float m) { return budgetLeft > 0 && rng.unit() < rate * 4.0f * p.variation * m; };
        auto setHit = [&] (int i, int semis, int vel)
        {
            auto& c = var[(size_t) i];
            c.type = CellHit; c.semis = (int8_t) semis; c.vel = (uint8_t) vel; c.slide = 0; c.bend = 0;
        };
        // DROP FIRST: the first beat of bar 1 (or, less often, bar 2) empty; the note after it starts there
        if (fire (0.19f, mStart))
        {
            const int b0 = rng.unit() < 0.7f ? 0 : 16;
            if (var[(size_t) b0].type != CellRest)
            {
                for (int i = b0; i < b0 + 4; ++i) var[(size_t) i].type = CellRest;
                for (int i = b0 + 4; i < b0 + 16; ++i)
                    if (var[(size_t) i].type != CellRest) { var[(size_t) i].type = CellHit; break; }
                --budgetLeft;
            }
        }
        // PICKUP: a short note on the "a" of the last beat (cell 30), the end of the note before it
        if (fire (0.26f, mEnd))
        {
            setHit (30, vocab (rng.unit()), 108);
            var[31].type = CellTie; var[31].semis = var[30].semis;
            --budgetLeft;
        }
        // TENSION: a flat-2 a 16th before a root note in bar 2
        if (fire (0.15f, mEnd))
            for (int i = 18; i < 32; ++i)
                if (var[(size_t) i].type == CellHit && ((var[(size_t) i].semis % 12) + 12) % 12 == 0 && var[(size_t) i - 1].type != CellHit)
                {
                    setHit (i - 1, var[(size_t) i].semis + 1, 70);
                    --budgetLeft;
                    break;
                }
        // ECHO: a note struck again a 16th later, quieter
        if (fire (0.15f, 1.0f))
        {
            const int start = (int) (rng.unit() * 30.0f);
            for (int k = 0; k < 30; ++k)
            {
                const int i = (start + k) % 30;
                if (var[(size_t) i].type == CellHit && var[(size_t) i + 1].type != CellHit && var[(size_t) i + 2].type != CellHit)
                {
                    setHit (i + 1, var[(size_t) i].semis, std::max (20, (int) var[(size_t) i].vel * 6 / 10));
                    --budgetLeft;
                    break;
                }
            }
        }
        // OCTAVE: a short note right after the snare up an octave, on the layers only
        if (fire (0.12f, 1.0f))
            for (int i : { 5, 6, 13, 14, 21, 22, 29 })
            {
                if (var[(size_t) i].type != CellHit) continue;
                int l = 1;
                while (i + l < kRhythm && var[(size_t) (i + l)].type == CellTie) ++l;
                if (l > 2) continue;
                for (int k = i; k < i + l; ++k) var[(size_t) k].oct = 1;
                --budgetLeft;
                break;
            }
    }
    int edits = 0;
    for (int i = 0; i < kRhythm; ++i)
    {
        const auto& a = base[(size_t) i];
        auto& v = var[(size_t) i];
        v.changed = (int8_t) (a.type != v.type || a.semis != v.semis || a.vel != v.vel || v.oct != 0);
        varChanged += v.changed;
        viewVariant[(size_t) i].store (v.type | ((v.semis + 64) << 2) | (v.oct << 9) | (v.changed << 10), std::memory_order_relaxed);
    }
    edits = std::max (0, p.budget) - budgetLeft;
    viewEdits.store (edits, std::memory_order_relaxed);
}

//==============================================================================
void Engine::process (Brain& brain, const Params& p, const MidiEvent* in, int numIn, float* outL, float* outR, int n,
                      const float* sidechain)
{
    if (p.seed != seedSeen) { walker.seed (p.seed * 2654435761u + 11u); seedSeen = p.seed; }
    const double bpm = std::clamp (p.bpm > 0.0 ? p.bpm : 174.0, 20.0, 400.0);
    viewBpm.store (bpm, std::memory_order_relaxed);
    const double sps = 60.0 / bpm * sr / 4.0;
    const double mult = std::clamp ((double) p.mult, 0.25, 64.0);
    const bool running = p.playing || p.run == RunAlways;
    viewPlaying.store (running ? 1 : 0, std::memory_order_relaxed);

    if (wasRunning && ! running)
    {
        numQueued = 0;
        nextTick = INT64_MIN;
        nextCell = INT64_MIN;
        viewStep.store (-1, std::memory_order_relaxed);
        viewCell.store (-1, std::memory_order_relaxed);
        if (p.source != SrcMidi || numHeld == 0) releaseAll (p.releaseMs);
    }
    double pos0 = 0.0;
    if (p.playing) pos0 = p.ppq * 4.0;
    else if (running) { if (wasRunning && lastPos > -1.0e8) freePos = lastPos; pos0 = freePos; }
    const double pos1 = pos0 + (double) n / sps;
    if (! p.playing && running) freePos = pos1;
    if (! running) freePos = 0.0;

    if (haltRequest.exchange (0) != 0) { walker.halt(); viewHalted.store (1); }
    if (const int j = jumpRequest.exchange (-1); j >= 0) { walker.jump (j); viewHalted.store (0); }
    if (resetRequest.exchange (0) != 0) pendingReset = true;
    if (const int a = auditionRequest.exchange (-1); a >= 0 && a < NumArts)
    {
        auditionLeft = (int64_t) (sps * 8.0);
        if (keyEnv < 1.0e-3f) { phi = 0.0; subHalf = 0.0; }
        startArt (a, 1.0f, auditionLeft, p, true);
    }

    // ---- the walk: ticks of this block, into the queue (a delay can push one into a later block)
    if (running)
    {
        const bool jumped = nextTick == INT64_MIN || pos0 < lastPos - 1.0e-6 || pos0 > lastPos + 1.0 || std::abs (mult - lastMult) > 1.0e-9;
        if (jumped)
        {
            numQueued = 0;
            nextTick = (int64_t) std::ceil (pos0 / mult - 1.0e-9);
            lastMult = mult;
            pendingReset = true;
        }
        while ((double) nextTick * mult < pos1)
        {
            const double t = (double) nextTick * mult;
            const bool rs = pendingReset || onReset (t, 16.0 * p.resetBars);
            if (rs) { walker.reset (p.startStep, kSteps); pendingReset = false; viewHalted.store (0); }
            const int from = walker.stateNow() - 1;
            const int s = walker.tick (brain.matrix, kSteps);
            if (s >= 0)
            {
                const auto& se = brain.steps[(size_t) s];
                Pending q;
                q.step = s;
                q.from = from;
                const int trig = se.trig.load (std::memory_order_relaxed);
                double shift = (nextTick & 1) ? (double) std::clamp (p.swing, 0.0f, 1.0f) * 0.5 * mult : 0.0;
                const float d = se.delay.load (std::memory_order_relaxed);
                if (d > 0.0f && walker.rnd.unit() * 100.0 < (double) se.dprob.load (std::memory_order_relaxed)) shift += (double) d * mult;
                const bool pass = walker.rnd.unit() * 100.0 < (double) se.prob.load (std::memory_order_relaxed);
                q.art = (pass && trig >= 1 && trig <= NumArts && ! p.art[(size_t) trig - 1].mute) ? trig - 1 : -1;
                q.mute = pass && trig == kTrigMute;
                q.vel = se.vel.load (std::memory_order_relaxed) / 100.0f;
                // the device: Duration x 1/16 (not x TIME); in STEPS it counts the ball's steps
                q.durSamples = (float) (se.dur.load (std::memory_order_relaxed) * sps * (p.durInSteps ? mult : 1.0));
                q.mod = se.mod.load (std::memory_order_relaxed);
                q.oct = se.oct.load (std::memory_order_relaxed);
                q.at = clock + (int64_t) std::floor (std::max (0.0, t + shift - pos0) * sps);
                if (numQueued < (int) queue.size()) queue[(size_t) numQueued++] = q;
            }
            ++nextTick;
        }
        viewTick.store (nextTick, std::memory_order_relaxed);
    }

    // ---- the rhythm: its cells in this block (a cell is a 16th, or an 1/8 at HALF RATE)
    const double cellLen = p.rhythmHalf ? 2.0 : 1.0;
    const double swingAmt = (double) std::clamp (p.swing, 0.0f, 1.0f) * 0.5;
    std::array<int64_t, 64> cellG {};
    std::array<int, 64> cellAt {};
    int nc = 0;
    const bool rhythmLive = running && p.rhythmOn;
    if (rhythmLive)
    {
        if (nextCell == INT64_MIN || pos0 < lastPos - 1.0e-6 || pos0 > lastPos + 1.0 || p.rhythmHalf != lastHalf)
            nextCell = (int64_t) std::ceil (pos0 / cellLen - 1.0e-9);
        lastHalf = p.rhythmHalf;
        for (;; ++nextCell)
        {
            const double t = ((double) nextCell + ((nextCell & 1) ? swingAmt : 0.0)) * cellLen;
            if (t >= pos1 || nc >= (int) cellG.size()) break;
            cellG[(size_t) nc] = nextCell;
            cellAt[(size_t) nc++] = (int) std::clamp ((int64_t) std::floor (std::max (0.0, t - pos0) * sps), (int64_t) 0, (int64_t) n - 1);
        }
    }
    else { nextCell = INT64_MIN; rhyGate = false; gateCut = false; gateOffAt = INT64_MAX; rhyOct = false; bendNow = 0.0; }

    // ---- SNARE GAP: the note ends this long before the snare (2 and 4; at HALF RATE, 3)
    std::array<int, 16> gapAt {};
    int ng = 0;
    if (rhythmLive && p.snareGapMs > 0.01f)
    {
        const double g16 = (double) p.snareGapMs * 0.001 * bpm / 15.0;
        for (int64_t s = (int64_t) std::ceil (pos0 + g16 - 1.0e-9); (double) s - g16 < pos1 && ng < (int) gapAt.size(); ++s)
        {
            const int inBar = (int) (((s % 16) + 16) % 16);
            if (! (p.rhythmHalf ? inBar == 8 : (inBar == 4 || inBar == 12))) continue;
            gapAt[(size_t) ng++] = (int) std::clamp ((int64_t) std::floor (std::max (0.0, (double) s - g16 - pos0) * sps), (int64_t) 0, (int64_t) n - 1);
        }
    }
    // ---- the kick template (the duck without a sidechain)
    std::array<int, 16> kickAt {};
    int nk = 0;
    if (running && p.duck > 0.001f && ! p.duckSidechain && p.kick != KickOff)
    {
        for (int64_t s = (int64_t) std::ceil (pos0 - 1.0e-9); (double) s < pos1 && nk < (int) kickAt.size(); ++s)
        {
            const int c = (int) (((s % 32) + 32) % 32), b = c % 16;
            bool k = false;
            switch (p.kick)
            {
                case KickDnb:     k = b == 0 || b == 10; break;
                case KickTwoStep: k = c == 0 || c == 10 || c == 16 || c == 18 || c == 26; break;
                case KickFour:    k = b % 4 == 0; break;
                case KickHalf:    k = c == 0 || c == 26; break;
                default: break;
            }
            if (k) kickAt[(size_t) nk++] = (int) std::clamp ((int64_t) std::floor (std::max (0.0, (double) s - pos0) * sps), (int64_t) 0, (int64_t) n - 1);
        }
    }

    // ---- the events of this block, in time order: kicks, gaps, rhythm cells, MIDI, queue entries
    struct Ev { int at; int kind; int idx; };
    std::array<Ev, 64 + 512 + 64 + 32> evs {};
    int ne = 0;
    for (int i = 0; i < nk; ++i) evs[(size_t) ne++] = { kickAt[(size_t) i], 4, i };
    for (int i = 0; i < ng; ++i) evs[(size_t) ne++] = { gapAt[(size_t) i], 3, i };
    for (int i = 0; i < nc; ++i) evs[(size_t) ne++] = { cellAt[(size_t) i], 2, i };
    for (int i = 0; i < numQueued; ++i)
        if (queue[(size_t) i].at < clock + n) evs[(size_t) ne++] = { (int) std::max<int64_t> (0, queue[(size_t) i].at - clock), 0, i };
    for (int i = 0; i < numIn && ne < (int) evs.size(); ++i) evs[(size_t) ne++] = { std::clamp (in[i].offset, 0, n - 1), 1, i };
    std::sort (evs.begin(), evs.begin() + ne, [] (const Ev& a, const Ev& b) { return a.at != b.at ? a.at < b.at : a.kind > b.kind; });

    // per-block constants
    const float keyAtk = 1.0f - std::exp (-1.0f / (float) (0.002 * sr));
    const float keyRel = 1.0f - std::exp (-1.0f / (float) (std::max (1.0f, p.releaseMs) * 0.001 * sr));
    const float relRate = 1.0f / (float) std::max (1.0, p.releaseMs * 0.001 * sr);
    const double glideK = p.glideMs > 0.05f ? std::exp (-1.0 / (p.glideMs * 0.001 * sr)) : 0.0;
    const double slideK = std::exp (-1.0 / (std::max (45.0f, p.glideMs) * 0.001 * sr));   // a slid rhythm note
    const double driftInc = p.drift > 0.001f ? 1.0 / (16.0 * std::max (0.5f, p.driftBars) * sps) : 0.0;
    driftPos = running ? std::fmod (pos0 / (16.0 * std::max (0.5f, p.driftBars)), 1.0) : driftPos;
    const float envK = 1.0f - std::exp (-1.0f / (float) (0.002 * sr));
    const float cohK = 1.0f - std::exp (-1.0f / (float) (0.03 * sr));
    std::array<double, NumArts> lfoInc {};
    for (int a = 0; a < NumArts; ++a) lfoInc[(size_t) a] = bpm / 60.0 * lfoCyclesPerBeat (p.art[(size_t) a].motion) / sr;
    const int dlyMask = (int) dly.size() - 1;
    // the kick's duck, the phrase's arc, the cell's bend
    const float duckAtkK = 1.0f - std::exp (-1.0f / (float) (0.0015 * sr));
    const float scAtkK = 1.0f - std::exp (-1.0f / (float) (0.001 * sr));
    const float duckRelK = std::exp (-1.0f / (float) (std::max (5.0f, p.duckRelMs) * 0.001 * sr));
    const double arcLen = 16.0 * std::max (1.0f, p.arcBars);
    const double arcInc = p.arcAmt > 0.001f && running ? 1.0 / (arcLen * sps) : 0.0;
    if (running) { arcPos = std::fmod (pos0 / arcLen, 1.0); if (arcPos < 0.0) arcPos += 1.0; }
    else arcPos = 0.0;

    auto pitchTarget = [&]
    {
        int note = p.root;
        if (p.source == SrcMidi && numHeld > 0) note = held[(size_t) numHeld - 1];
        else if (p.source == SrcLatch && latched >= 0) note = latched;
        else if (p.source == SrcMidi && latched >= 0) note = latched;    // the last note, while it releases
        const double nn = (double) note + 12.0 * (p.octave + stepOct) + bend + (rhythmLive ? (double) rhySemis + bendNow : 0.0);
        if (std::abs (nn - noteNow) > 1.0e-9) { noteNow = nn; f0Target = noteHz (nn); }
    };
    auto keyGate = [&]
    {
        if (auditionLeft > 0) return true;
        bool g = false;
        switch (p.source)
        {
            case SrcSelf:  g = running; break;
            case SrcLatch: g = running && latched >= 0; break;
            default:       g = numHeld > 0; break;
        }
        return g && (! rhythmLive || rhyGate);
    };

    int done = 0;
    int ei = 0;
    while (done < n)
    {
        const int until = ei < ne ? evs[(size_t) ei].at : n;
        // ---- render [done, until)
        for (int i = done; i < until; ++i)
        {
            if (auditionLeft > 0) --auditionLeft;
            if (clock >= gateOffAt) { rhyGate = false; gateCut = true; gateOffAt = INT64_MAX; }     // GATE %
            if (std::abs (bendStep) > 0.0)
            {
                if (std::abs (bendNow) <= std::abs (bendStep)) { bendNow = 0.0; bendStep = 0.0; }
                else bendNow -= bendStep;
            }
            pitchTarget();
            const double gk = (rhythmLive && rhySlide) ? slideK : glideK;
            if (gk > 0.0) f0 = f0Target * std::pow (f0 / f0Target, gk);
            else f0 = f0Target;
            driftPos += driftInc;
            if (driftPos >= 1.0) driftPos -= 1.0;
            const float dS = p.drift * (float) std::sin (kTwoPi * driftPos), dC = p.drift * (float) std::cos (kTwoPi * driftPos);
            const double dphi = f0 / sr;
            phi += dphi;
            if (phi >= 1.0) phi -= 1.0;

            keyTarget = keyGate() ? 1.0f : 0.0f;
            keyEnv += (keyTarget - keyEnv) * (keyTarget > keyEnv ? keyAtk : keyRel);

            // ARC: TONE opens over the first half of the phrase, GRIT and DRIVE rise over the second
            float arcT = 0.0f, arcG = 0.0f;
            if (arcInc > 0.0)
            {
                arcPos += arcInc;
                if (arcPos >= 1.0) arcPos -= 1.0;
                arcT = p.arcAmt * (float) std::min (1.0, 2.0 * arcPos);
                arcG = p.arcAmt * (float) std::max (0.0, 2.0 * arcPos - 1.0);
            }
            // the cell's velocity: the layers' level and (VEL > TONE) their brightness
            const float velTarget = rhythmLive ? 0.6f + 0.4f * rhyVel / 0.787f : 1.0f;
            velSmooth += (velTarget - velSmooth) * envK;
            const float toneVel = rhythmLive ? p.velTone * 0.5f * (rhyVel - 0.787f) : 0.0f;

            float top = 0.0f, envSum = 0.0f;
            const float c = (float) std::cos (kTwoPi * phi), s = (float) std::sin (kTwoPi * phi);
            // OCTAVE (the variation): the layers an octave up on their own phase x 2, the sub stays
            const bool octUp = rhythmLive && rhyOct;
            const double phiSave = phi, f0Save = f0;
            if (octUp) { phi = dsp::wrap (2.0 * phi); f0 *= 2.0; }
            for (int a = 0; a < NumArts; ++a)
            {
                auto& v = voices[(size_t) a];
                if (! v.active) continue;
                if (v.gateLeft > 0 && --v.gateLeft == 0) { v.target = 0.0f; v.rate = relRate; }
                if (v.env < v.target) v.env = std::min (v.target, v.env + v.rate);
                else if (v.env > v.target) v.env = std::max (v.target, v.env - v.rate);
                // modulation: the step's sweep over its gate, the slow drift
                auto ap = p.art[(size_t) a];
                if (v.elapsed < v.gateTotal) ++v.elapsed;
                const float prog = (float) v.elapsed / (float) v.gateTotal;
                ap.tone = std::clamp (ap.tone + 0.6f * v.sweep * prog + 0.35f * dS + 0.45f * arcT + toneVel, 0.0f, 1.0f);
                ap.motion = std::clamp (ap.motion + 0.2f * dC, 0.0f, 1.0f);
                ap.grit = std::clamp (ap.grit + 0.5f * arcG, 0.0f, 1.0f);
                const float y = renderArt (ap.model, v, ap, lfoInc[(size_t) a]) * kModelTrim[std::clamp (ap.model, 0, NumModels - 1)] * v.env * v.vel * ap.level;
                v.cohC += (y * c - v.cohC) * cohK;
                v.cohS += (y * s - v.cohS) * cohK;
                v.cohE += (y * y - v.cohE) * cohK;
                top += y;
                envSum += v.env * v.vel * (ap.level > 0.0f ? 1.0f : 0.0f);
                if (v.env <= 0.0f && v.target <= 0.0f) v.active = false;
            }
            if (octUp) { phi = phiSave; f0 = f0Save; }
            top *= keyEnv * velSmooth;
            // DRIVE before the split: whatever it adds down low is taken out with the rest
            const float drv = std::min (1.0f, p.drive + 0.5f * arcG);
            if (drv > 0.001f) top = dsp::drive (top, drv) * (1.0f - 0.25f * drv);
            sumEnvSmooth += (std::min (1.0f, envSum) - sumEnvSmooth) * envK;

            // ---- the split at XOVER x f0 (two Butterworth sections, the upper band is the rest)
            if ((clock & 7) == 0)
            {
                const double fc = std::clamp ((double) p.xover * f0, 30.0, 1500.0);
                xa.set (fc, sr, 0.7071);
                xb.set (fc, sr, 0.7071);
                xc.set (fc, sr, 0.7071);
                xd.set (fc, sr, 0.7071);
            }
            // Linkwitz-Riley: the upper band is high-passed (not "the rest"), so the fundamental
            // and its phase-shifted copy never leak above the split
            const float lowBand = xb.low (xa.low (top));
            float up = xd.high (xc.high (top));

            // ---- the sub: a sine on the master phase (or an octave under), following the switches
            if (phi < dphi) subHalf = subHalf > 0.25 ? 0.0 : 0.5;     // the master wrapped
            const double sp = p.subOct < 0 ? phi * 0.5 + subHalf : phi;
            float sub = (float) std::sin (kTwoPi * sp) * p.sub * sumEnvSmooth * keyEnv;
            if (p.subDrive > 0.001f) sub = dsp::drive (sub, p.subDrive) * std::max (0.2f, p.sub * sumEnvSmooth * keyEnv);
            // ---- the duck: a kick template, or the sidechain (4:1 above -24 dB); the sub all the way, the layers half
            float duckAmt = 0.0f;
            if (p.duckSidechain)
            {
                const float x = sidechain != nullptr ? std::abs (sidechain[i]) : 0.0f;
                scEnv += (x - scEnv) * (x > scEnv ? scAtkK : 1.0f - duckRelK);
                const float over = 20.0f * std::log10 (scEnv + 1.0e-9f) + 24.0f;
                if (over > 0.0f && p.duck > 0.001f) duckAmt = p.duck * (1.0f - std::pow (10.0f, -std::min (24.0f, over * 0.75f) / 20.0f));
            }
            else
            {
                if (duckAtk) { duckEnv += (1.0f - duckEnv) * duckAtkK; if (duckEnv > 0.98f) duckAtk = false; }
                else duckEnv *= duckRelK;
                duckAmt = p.duck * 0.75f * duckEnv;    // a full kick: about -12 dB
            }
            duckNow = duckAmt;
            sub *= 1.0f - duckAmt;
            const float layDuck = 1.0f - 0.5f * duckAmt;
            up *= layDuck;
            const float low = sub + p.body * lowBand * layDuck;

            // ---- above: width (a moving delay on the side only: the mono sum stays clean)
            dly[(size_t) dlyPos] = up;
            chorus += 0.35 / sr;
            if (chorus >= 1.0) chorus -= 1.0;
            const double dms = 6.0 + 1.8 * std::sin (kTwoPi * chorus);
            const double rp = (double) dlyPos - dms * 0.001 * sr;
            const int i0 = (int) std::floor (rp);
            const float fr = (float) (rp - i0);
            const float dd = dly[(size_t) (i0 & dlyMask)] * (1.0f - fr) + dly[(size_t) ((i0 + 1) & dlyMask)] * fr;
            dlyPos = (dlyPos + 1) & dlyMask;
            const float side = p.width * 0.5f * dd;

            float L = (low + up + side) * p.gain, R = (low + up - side) * p.gain;
            if (std::abs (L) > 0.95f) L = std::copysign (0.95f + 0.05f * std::tanh ((std::abs (L) - 0.95f) * 20.0f), L);
            if (std::abs (R) > 0.95f) R = std::copysign (0.95f + 0.05f * std::tanh ((std::abs (R) - 0.95f) * 20.0f), R);
            outL[i] = L;
            outR[i] = R;

            if (++scopeDecim >= 2)
            {
                scopeDecim = 0;
                const int w = scopeWrite.load (std::memory_order_relaxed);
                scopePhase[(size_t) w] = (float) phi;
                scopeUp[(size_t) w] = up;
                scopeLow[(size_t) w] = low;
                scopeWrite.store ((w + 1) % kScope, std::memory_order_release);
            }
            ++clock;
        }
        done = until;
        if (ei >= ne) break;

        // ---- the event at `until`
        const auto& ev = evs[(size_t) ei++];
        if (ev.kind == 4) { duckAtk = true; continue; }
        if (ev.kind == 3) { if (rhyGate) { rhyGate = false; gateCut = true; gateOffAt = INT64_MAX; } continue; }
        if (ev.kind == 2)
        {
            const int64_t k = cellG[(size_t) ev.idx];
            const int64_t cyc = k >= 0 ? k / kRhythm : -((-k + kRhythm - 1) / kRhythm);
            if (cyc != varCycle) buildVariant (brain, p, cyc);
            const int ci = (int) (k - cyc * kRhythm);
            const auto& c = var[(size_t) ci];
            viewCell.store (ci, std::memory_order_relaxed);
            if (c.type == CellRest) { rhyGate = false; gateCut = false; gateOffAt = INT64_MAX; continue; }
            if (c.type == CellTie)
            {
                // a held cell keeps the note as it is (a note GATE or the SNARE GAP closed stays closed)
                if (! rhyGate && ! gateCut)
                {
                    rhyGate = true;          // joined in the middle of a note
                    rhySemis = c.semis;
                    rhyVel = (float) c.vel / 127.0f;
                }
                rhyOct = c.oct != 0;
                continue;
            }
            const bool wasOpen = rhyGate;
            rhyGate = true;
            gateCut = false;
            rhySemis = c.semis;
            rhySlide = c.slide != 0;
            rhyVel = (float) c.vel / 127.0f;
            rhyOct = c.oct != 0;
            if (c.bend != 0)
            {
                bendNow = (double) c.bend;
                bendStep = bendNow / std::max (1.0, (double) p.bendMs * 0.001 * sr);
            }
            else bendNow = bendStep = 0.0;
            int l = 1;
            while (ci + l < kRhythm && var[(size_t) (ci + l)].type == CellTie) ++l;
            gateOffAt = p.gate < 0.999f ? clock + (int64_t) ((double) l * cellLen * sps * std::max (0.05f, p.gate)) : INT64_MAX;
            // a new note: the articulations' trigger envelopes start again (the roller's punch)
            for (auto& v : voices) if (v.active) v.e = 1.0f;
            if (! wasOpen && keyEnv < 1.0e-3f) { phi = 0.0; subHalf = 0.0; }
            continue;
        }
        if (ev.kind == 0)
        {
            auto& q = queue[(size_t) ev.idx];
            fire (q, p);
            q.at = INT64_MAX;
        }
        else
        {
            const auto& m = in[ev.idx];
            if (m.bend) { bend = (double) m.bendValue * p.bendRange; continue; }
            if (m.vel > 0)
            {
                const bool fromSilence = numHeld == 0 && keyEnv < 1.0e-3f;
                for (int k = 0; k < numHeld; ++k) if (held[(size_t) k] == m.note) { for (int j = k; j + 1 < numHeld; ++j) held[(size_t) j] = held[(size_t) j + 1]; --numHeld; break; }
                if (numHeld < (int) held.size()) held[(size_t) numHeld++] = m.note;
                latched = m.note;
                if (fromSilence) { phi = 0.0; subHalf = 0.0; pitchTarget(); f0 = f0Target; }
                // the walk is not moving: the key plays the last articulation itself
                if (! running) startArt (keyArt, (float) m.vel / 127.0f, -1, p, true);
            }
            else
            {
                for (int k = 0; k < numHeld; ++k) if (held[(size_t) k] == m.note) { for (int j = k; j + 1 < numHeld; ++j) held[(size_t) j] = held[(size_t) j + 1]; --numHeld; break; }
                if (numHeld == 0 && ! running) releaseAll (p.releaseMs);
            }
        }
    }

    // a walk with no key (MIDI source): nothing is held, the articulations still run but are gated
    int w = 0;
    for (int i = 0; i < numQueued; ++i) if (queue[(size_t) i].at != INT64_MAX) queue[(size_t) w++] = queue[(size_t) i];
    numQueued = w;

    // views
    viewF0.store ((float) f0, std::memory_order_relaxed);
    viewXover.store ((float) std::clamp ((double) p.xover * f0, 30.0, 1500.0), std::memory_order_relaxed);
    viewSub.store (p.sub * sumEnvSmooth * keyEnv, std::memory_order_relaxed);
    viewDuck.store (duckNow, std::memory_order_relaxed);
    viewArc.store ((float) arcPos, std::memory_order_relaxed);
    for (int a = 0; a < NumArts; ++a)
    {
        const auto& v = voices[(size_t) a];
        viewEnv[(size_t) a].store (v.active ? v.env * keyEnv : 0.0f, std::memory_order_relaxed);
        viewPhase[(size_t) a].store ((float) std::atan2 (v.cohS, v.cohC), std::memory_order_relaxed);
        const float fund = 2.0f * std::sqrt (v.cohC * v.cohC + v.cohS * v.cohS);
        viewCoh[(size_t) a].store (v.cohE > 1.0e-9f ? std::min (1.0f, fund / std::sqrt (2.0f * v.cohE)) : 0.0f, std::memory_order_relaxed);
    }

    if (running) lastPos = pos1 - 1.0e-9;
    else lastPos = -1.0e9;
    wasRunning = running;
}

} // namespace nst::neuro
