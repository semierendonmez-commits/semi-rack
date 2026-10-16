#include "Particles.h"
#include <algorithm>

namespace qn
{
float Engine::winTable[kNumWins][kWinSize + 1];
float Engine::sineTable[4097];
bool Engine::tablesReady = false;

static float windowShape (int t, double x)
{
    x = std::clamp (x, 0.0, 1.0);
    switch (t)
    {
        case Gauss:
        {
            const double e = std::exp (-0.5 * (0.5 / 0.15) * (0.5 / 0.15));
            return (float) ((std::exp (-0.5 * ((x - 0.5) / 0.15) * ((x - 0.5) / 0.15)) - e) / (1.0 - e));
        }
        case Hann: return (float) (0.5 - 0.5 * std::cos (2.0 * kPi * x));
        case Expodec:
        {
            if (x < 0.005) return (float) (x / 0.005);
            const double u = (x - 0.005) / 0.995, e = std::exp (-5.0);
            return (float) ((std::exp (-5.0 * u) - e) / (1.0 - e));
        }
        case Rexpodec: return windowShape (Expodec, 1.0 - x);
        case Trapez: return (float) std::min ({ 1.0, x / 0.1, (1.0 - x) / 0.1 });
        default: return 1.0f;
    }
}

static void ensureTables();
Engine::Engine() { ensureTables(); }

static void ensureTables()
{
    if (! Engine::tablesReadyFlag())
    {
        Engine::buildTables();
    }
}

void Engine::buildTables()
{
    for (int w = 0; w < kNumWins; ++w)
        for (int i = 0; i <= kWinSize; ++i) winTable[w][i] = windowShape (w, (double) i / kWinSize);
    for (int i = 0; i <= 4096; ++i) sineTable[i] = (float) std::sin (2.0 * kPi * i / 4096.0);
    tablesReady = true;
}

float Engine::window (int type, float x)
{
    if (! tablesReady) buildTables();
    const float f = std::clamp (x, 0.0f, 1.0f) * kWinSize;
    const int i = std::min (kWinSize - 1, (int) f);
    const float fr = f - (float) i;
    const float* t = winTable[std::clamp (type, 0, kNumWins - 1)];
    return t[i] + (t[i + 1] - t[i]) * fr;
}

double Engine::emitPeriodSec (const Scene& s, double bpm)
{
    if (s.sync > 0) return syncBeats (s.sync) * 60.0 / std::max (1.0, bpm);
    return 1.0 / std::clamp ((double) s.density, 0.1, 2000.0);
}

double Engine::particleDurSec (const Scene& s, double hz, double period)
{
    hz = std::max (1.0, hz);
    switch (s.type)
    {
        case Grainlet: return std::clamp (s.cycles / hz, 0.0005, 1.0);
        case Trainlet: return std::clamp (s.impulses / hz, 0.0005, 2.0);
        case Pulsar:   return std::max (0.0002, period * std::clamp (s.duty, 1.0f, 100.0f) * 0.01);
        default:       return std::clamp ((double) s.durMs, 0.5, 200.0) * 0.001;
    }
}

void Engine::prepare (double s)
{
    sr = s > 0 ? s : 48000.0;
    auto hpf = [this] (Biquad& b, double f, double q)
    {
        const double w = 2 * kPi * f / sr, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al;
        b.b0 = (1 + c) / 2 / a0; b.b1 = -(1 + c) / a0; b.b2 = b.b0; b.a1 = -2 * c / a0; b.a2 = (1 - al) / a0;
    };
    auto lpf = [this] (Biquad& b, double f, double q)
    {
        const double w = 2 * kPi * f / sr, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al;
        b.b0 = (1 - c) / 2 / a0; b.b1 = (1 - c) / a0; b.b2 = b.b0; b.a1 = -2 * c / a0; b.a2 = (1 - al) / a0;
    };
    const double ceilHz = std::min (18000.0, sr * 0.45);
    for (int c = 0; c < 2; ++c)
    {   // 4th-order Butterworth pairs (Q 0.541, 1.307)
        hpf (hp[c][0], 18.0, 0.5412); hpf (hp[c][1], 18.0, 1.3066);
        lpf (lp[c][0], ceilHz, 0.5098); lpf (lp[c][1], ceilHz, 0.6013); lpf (lp[c][2], ceilHz, 0.9000); lpf (lp[c][3], ceilHz, 2.5629);
    }
    accL.assign (8192, 0.0f); accR.assign (8192, 0.0f);
#ifdef METAMODULE
    const double ringSec = 2.0, dlySec = 1.5;   // SemiFamily: RAM (repeat / reverse / tape slices up to 1 s)
#else
    const double ringSec = 4.0, dlySec = 2.0;
#endif
    ringL.assign ((size_t) (sr * ringSec), 0.0f); ringR.assign ((size_t) (sr * ringSec), 0.0f);
    dlyL.assign ((size_t) (sr * dlySec), 0.0f); dlyR.assign ((size_t) (sr * dlySec), 0.0f);
    reset();
}

void Engine::reset()
{
    for (auto& e : em) e = Emitter();
    for (auto& p : parts) p.on = false;
    for (auto& c : hp) for (auto& b : c) b.z1 = b.z2 = 0;
    for (auto& c : lp) for (auto& b : c) b.z1 = b.z2 = 0;
    limGain = 1.0f;
    std::fill (ringL.begin(), ringL.end(), 0.0f); std::fill (ringR.begin(), ringR.end(), 0.0f);
    std::fill (dlyL.begin(), dlyL.end(), 0.0f); std::fill (dlyR.begin(), dlyR.end(), 0.0f);
    curFx = FxNone; fxMixSm = 0; throwSm = 0; svf[0] = svf[1] = Svf(); dlyLp[0] = dlyLp[1] = 0;
}

// ---- emitters ----------------------------------------------------------------
void Engine::noteOn (int slot, double hz, float vel)
{
    auto& e = em[(size_t) slot];
    if (e.stage != Off) ++e.gen;      // voice steal: old particles fade
    e.stage = Attack; e.scene = nullptr; e.lane = false; e.hz = hz; e.vel = vel; e.env = 0.0f;
    e.nextFree = 0; e.lastGrid = -1.0e18; e.pendingDelay = -1; e.burstStep = 0; e.age = 0;
}

void Engine::noteOff (int slot) { auto& e = em[(size_t) slot]; if (e.stage != Off && ! e.lane) e.stage = Release; }

void Engine::killSlot (int slot) { auto& e = em[(size_t) slot]; e.stage = Off; e.env = 0; ++e.gen; }

void Engine::setLane (int lane, const Scene* s, double hz, bool on, bool hardCut)
{
    auto& e = em[(size_t) (8 + lane)];
    const bool wasOn = e.stage != Off;
    if (hardCut) ++e.gen;
    if (! on || s == nullptr) { e.stage = Off; e.env = 0; return; }
    e.lane = true; e.scene = s; e.hz = hz; e.vel = 0.6f; e.env = 1.0f; e.stage = Sustain;   // four lanes can stack
    if (! wasOn || hardCut) { e.nextFree = 0; e.lastGrid = -1.0e18; e.pendingDelay = -1; e.burstStep = 0; e.age = 0; }
}

void Engine::advanceEnv (Emitter& e, const Env& v)
{
    if (e.lane) return;
    const float spS = (float) sr * 0.001f;
    switch (e.stage)
    {
        case Attack:  e.env += 1.0f / std::max (1.0f, v.a * spS); if (e.env >= 1.0f) { e.env = 1.0f; e.stage = Decay; } break;
        case Decay:   e.env -= (1.0f - v.s) / std::max (1.0f, v.d * spS); if (e.env <= v.s) { e.env = v.s; e.stage = Sustain; } break;
        case Sustain: e.env = v.s; if (v.s <= 0.0f) e.stage = Off; break;
        case Release: e.env -= 1.0f / std::max (1.0f, v.r * spS); if (e.env <= 0.0f) { e.env = 0.0f; e.stage = Off; } break;
        default: break;
    }
}

void Engine::spawn (int slot, const Scene& s, double bpm)
{
    auto& e = em[(size_t) slot];
    // masking: burst pattern step, then stochastic
    const int step = e.burstStep & 15;
    e.burstStep = (e.burstStep + 1) & 15;
    if (((s.burst >> step) & 1u) == 0) return;
    if (s.mask > 0.0f && rnd01() * 100.0f < s.mask) return;

    double hz = e.hz * std::exp2 (s.fineCents / 1200.0);
    if (s.jitPitch > 0.0f) hz *= std::exp2 (s.jitPitch * rndBi() / 12.0);
    hz = std::clamp (hz, 1.0, std::min (kPitchCeilHz, sr * 0.45));
    const double period = emitPeriodSec (s, bpm);
    const double dur = particleDurSec (s, hz, period);

    // find a slot: free one, else steal the oldest-ish (round robin)
    int idx = -1;
    for (int k = 0; k < kMaxParticles; ++k)
    {
        const int i = (partCursor + k) % kMaxParticles;
        if (! parts[(size_t) i].on) { idx = i; break; }
    }
    if (idx < 0) idx = partCursor;
    partCursor = (idx + 1) % kMaxParticles;

    auto& p = parts[(size_t) idx];
    p = Particle();
    p.on = true; p.type = (uint8_t) s.type; p.wave = (uint8_t) s.wave; p.win = (uint8_t) s.window;
    p.owner = (int16_t) slot; p.gen = e.gen;
    p.len = std::max (2, (int) std::lround (dur * sr));
    p.inc = hz / sr;
    p.incMax = std::min (kPitchCeilHz, sr * 0.45) / sr;
    if (s.type == Glisson && s.glide != 0.0f)
    {
        double dir = s.glideDir == Up ? 1.0 : (s.glideDir == Down ? -1.0 : (rnd() & 1 ? 1.0 : -1.0));
        p.incMul = std::exp2 (dir * s.glide / 12.0 / p.len);
    }
    // phase: coherent for grains (seamless overlap), reset for pulsars / trainlets
    if (s.type == Grain || s.type == Glisson || s.type == Grainlet)
        p.phase = std::fmod ((double) sampleClock * p.inc, 1.0);
    if (s.type == Trainlet)
    {
        p.a = 0.3f + 0.65f * std::clamp (s.chroma, 0.0f, 100.0f) * 0.01f;
        p.K = std::clamp ((int) (0.45 * sr / hz), 1, 64);
        p.aK = std::pow ((double) p.a, p.K); p.aK1 = p.aK * p.a;
        p.norm = (float) ((1.0 - p.a) / (1.0 - p.aK));
    }
    if (s.wave == Sample)
        if (const SampleData* smp = sample.get(); smp && ! smp->data.empty())   // SemiFamily: no atomic_load
        {
            const double N = (double) smp->data.size();
            p.spos = std::clamp (s.samplePos * 0.01, 0.0, 1.0) * N + (s.jitTime > 0 ? rnd01() * s.jitTime * 0.01 * 0.25 * N : 0.0);
            p.sinc = hz / smp->rootHz * smp->sr / sr;
        }
    const double overlap = dur / std::max (1.0e-6, period);
    const float autoGain = (float) (1.0 / std::sqrt (std::max (1.0, overlap)));
    static const float waveGain[kNumWaves] = { 1.0f, 0.6f, 0.5f, 0.6f, 1.0f };
    const float jA = 1.0f - std::clamp (s.jitAmp, 0.0f, 100.0f) * 0.01f * rnd01();
    p.amp = e.env * e.vel * jA * autoGain * waveGain[s.wave];
    const float pan = std::clamp (s.spread, 0.0f, 100.0f) * 0.01f * rndBi();
    p.gl = (float) std::cos ((pan + 1.0f) * kPi * 0.25); p.gr = (float) std::sin ((pan + 1.0f) * kPi * 0.25);

    const int w = eventW.load (std::memory_order_relaxed);
    events[(size_t) w] = { (float) ((double) sampleClock / sr), (float) hz, p.amp, (float) (dur * 1000.0), s.type };
    eventW.store ((w + 1) % kEvents, std::memory_order_release);
}

static inline float blep (double t, double dt)
{
    if (t < dt) { t /= dt; return (float) (t + t - t * t - 1.0); }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
    return 0.0f;
}

float Engine::renderParticle (Particle& p, const SampleData* smp)
{
    const float w = window (p.win, (float) p.age / (float) p.len);
    float v = 0.0f;
    const double inc = std::min (p.inc, p.incMax);
    if (p.type == Trainlet)
    {
        const double th = 2.0 * kPi * p.phase;
        const double c1 = std::cos (th), den = 1.0 - 2.0 * p.a * c1 + (double) p.a * p.a;
        const double num = c1 - p.a - p.aK * std::cos ((p.K + 1) * th) + p.aK1 * std::cos (p.K * th);
        v = (float) (num / std::max (1.0e-9, den)) * p.norm;
    }
    else switch (p.wave)
    {
        case Sine:
        {
            const double f = p.phase * 4096.0; const int i = (int) f; const float fr = (float) (f - i);
            v = sineTable[i] + (sineTable[i + 1] - sineTable[i]) * fr; break;
        }
        case Saw: v = (float) (2.0 * p.phase - 1.0) - blep (p.phase, inc); break;
        case Square:
        {
            double t2 = p.phase + 0.5; t2 -= std::floor (t2);
            v = (p.phase < 0.5 ? 1.0f : -1.0f) + blep (p.phase, inc) - blep (t2, inc); break;
        }
        case Noise: v = rndBi(); break;
        default:
            if (smp != nullptr && ! smp->data.empty())
            {
                const int i0 = (int) p.spos; const int N = (int) smp->data.size();
                if (i0 >= 0 && i0 + 1 < N) { const float fr = (float) (p.spos - i0); v = smp->data[(size_t) i0] + (smp->data[(size_t) i0 + 1] - smp->data[(size_t) i0]) * fr; }
                p.spos += p.sinc;
            }
            break;
    }
    p.phase += inc; p.phase -= std::floor (p.phase);
    if (p.incMul != 1.0) p.inc = std::min (p.inc * p.incMul, p.incMax);
    float a = p.amp * w;
    if (p.fade >= 0) { a *= (float) p.fade / 64.0f; if (--p.fade < 0) p.on = false; }
    if (++p.age >= p.len) p.on = false;
    return v * a;
}

// Transition fx. Replacing modes (repeat, reverse, tape, filters, crush, gate)
// crossfade in and out over ~1.5 ms; THROW feeds a ping-pong delay whose tail
// keeps ringing after the throw ends.
void Engine::fxSample (float& l, float& r, float prog, const Block& b)
{
    const int N = (int) ringL.size();
    ringL[(size_t) ringW] = l; ringR[(size_t) ringW] = r;
    const bool replacing = b.fxMode != FxNone && b.fxMode != FxThrow;
    fxMixSm += ((replacing ? 1.0f : 0.0f) - fxMixSm) * 0.016f;
    float fl = l, fr = r;
    if (fxMixSm > 1.0e-4f)
    {
        const int mode = replacing ? b.fxMode : curFx;
        prog = std::clamp (prog, 0.0f, 1.0f);
        switch (mode)
        {
            case FxRepeat:
                if (repIdx >= repLen)
                {
                    const int k = repIdx % repLen;
                    const int idx = (repStart + k) % N;
                    const float g = std::min ({ 1.0f, (float) k / 24.0f, (float) (repLen - k) / 24.0f });
                    fl = ringL[(size_t) idx] * g; fr = ringR[(size_t) idx] * g;
                }
                ++repIdx; break;
            case FxReverse:
            {
                const int idx = ((repStart - (repIdx % std::max (1, repLen))) % N + N) % N;
                const int k = repIdx % std::max (1, repLen);
                const float g = std::min ({ 1.0f, (float) k / 24.0f, (float) (repLen - k) / 24.0f });
                fl = ringL[(size_t) idx] * g; fr = ringR[(size_t) idx] * g; ++repIdx; break;
            }
            case FxTape:
            {
                const double rate = std::pow (1.0 - prog, 1.6);
                tapePos += rate;
                double pIdx = std::fmod (tapePos, (double) N); if (pIdx < 0) pIdx += N;
                const int i0 = (int) pIdx; const float f = (float) (pIdx - i0); const int i1 = (i0 + 1) % N;
                fl = ringL[(size_t) i0] + (ringL[(size_t) i1] - ringL[(size_t) i0]) * f;
                fr = ringR[(size_t) i0] + (ringR[(size_t) i1] - ringR[(size_t) i0]) * f;
                const float fade = std::pow (std::min (1.0f, (1.0f - prog) * 5.0f), 2.0f);   // spin down into silence
                fl *= fade; fr *= fade; break;
            }
            case FxHpf:
            case FxLpf:
            {
                const double fc = mode == FxHpf ? 20.0 * std::pow (8000.0 / 20.0, (double) prog) : 18000.0 * std::pow (150.0 / 18000.0, (double) prog);
                const float g = (float) std::tan (kPi * std::min (fc, sr * 0.45) / sr), k = 1.0f / 1.6f;
                const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
                float* io[2] = { &fl, &fr };
                for (int c = 0; c < 2; ++c)
                {
                    auto& st = svf[c]; const float v0 = *io[c];
                    const float v3 = v0 - st.ic2, v1 = a1 * st.ic1 + a2 * v3, v2 = st.ic2 + a2 * st.ic1 + a3 * v3;
                    st.ic1 = 2 * v1 - st.ic1; st.ic2 = 2 * v2 - st.ic2;
                    *io[c] = mode == FxHpf ? v0 - k * v1 - v2 : v2;
                }
                break;
            }
            case FxCrush:
            {
                const int dec = 1 + (int) (31.0f * prog);
                if (crushCount++ % dec == 0) { crushHoldL = l; crushHoldR = r; }
                const float q = std::exp2 (15.0f - 12.0f * prog);
                fl = std::round (crushHoldL * q) / q; fr = std::round (crushHoldR * q) / q; break;
            }
            case FxGate: fl = fr = 0.0f; break;
            default: break;
        }
        l = l * (1.0f - fxMixSm) + fl * fxMixSm;
        r = r * (1.0f - fxMixSm) + fr * fxMixSm;
    }
    // THROW: ping-pong delay, 3/16 note, feedback 0.55, damped
    throwSm += ((b.fxMode == FxThrow ? 1.0f : 0.0f) - throwSm) * 0.002f;
    const int D = (int) dlyL.size();
    const int dt = std::clamp ((int) (0.75 * 60.0 / std::max (20.0, b.bpm) * sr), 16, D - 2);
    const int ri = (dlyW - dt + D) % D;
    const float oL = dlyL[(size_t) ri], oR = dlyR[(size_t) ri];
    dlyLp[0] += (oR - dlyLp[0]) * 0.35f; dlyLp[1] += (oL - dlyLp[1]) * 0.35f;
    dlyL[(size_t) dlyW] = l * throwSm + dlyLp[0] * 0.55f;      // cross feedback = ping-pong
    dlyR[(size_t) dlyW] = r * throwSm * 0.3f + dlyLp[1] * 0.55f;
    if (std::abs (dlyL[(size_t) dlyW]) < 1.0e-12f) dlyL[(size_t) dlyW] = 0;
    if (std::abs (dlyR[(size_t) dlyW]) < 1.0e-12f) dlyR[(size_t) dlyW] = 0;
    dlyW = (dlyW + 1) % D;
    l += oL * 0.8f; r += oR * 0.8f;
    ringW = (ringW + 1) % N;
}

void Engine::process (float* L, float* R, int n, const Block& b)
{
    if ((int) accL.size() < n) { accL.resize ((size_t) n); accR.resize ((size_t) n); }
    std::fill (accL.begin(), accL.begin() + n, 0.0f);
    std::fill (accR.begin(), accR.begin() + n, 0.0f);
    const SampleData* smp = sample.get();   // SemiFamily: set on this thread only
    const double beatsPerSample = b.bpm / 60.0 / sr;
    const float gainCoef = 1.0f - std::exp (-1.0f / (0.004f * (float) sr));
    if (b.fxTrigger && b.fxMode != FxNone)
    {
        curFx = b.fxMode;
        const int N = (int) ringL.size();
        repStart = ringW; repIdx = 0; repLen = std::clamp (b.fxLen, 64, N / 2);
        tapePos = (double) ringW; crushCount = 0;
        if (curFx == FxReverse) repStart = (ringW - 1 + N) % N;
    }
    else if (b.fxMode != FxNone && b.fxMode != curFx) curFx = b.fxMode;

    for (int i = 0; i < n; ++i)
    {
        const double ppq = b.ppq + i * beatsPerSample;
        // emitters
        for (auto& e : em) e.gainSm += (e.gainTgt - e.gainSm) * gainCoef;
        for (int s = 0; s < kMaxEmitters; ++s)
        {
            auto& e = em[(size_t) s];
            if (e.stage == Off) continue;
            const Scene* sc = e.scene ? e.scene : b.synth;
            if (sc == nullptr) continue;
            advanceEnv (e, b.env);
            if (e.stage == Off) continue;
            e.age += 1.0 / sr;
            if (sc->sync > 0)
            {
                const double g = std::floor (ppq / syncBeats (sc->sync));
                if (g != e.lastGrid)
                {
                    e.lastGrid = g;
                    if (sc->jitTime > 0.0f) e.pendingDelay = (int) (rnd01() * sc->jitTime * 0.01f * 0.5 * emitPeriodSec (*sc, b.bpm) * sr);
                    else spawn (s, *sc, b.bpm);
                }
                if (e.pendingDelay >= 0 && e.pendingDelay-- == 0) spawn (s, *sc, b.bpm);
            }
            else
            {
                e.nextFree -= 1.0;
                if (e.nextFree <= 0.0)
                {
                    spawn (s, *sc, b.bpm);
                    const double per = sr / std::clamp ((double) sc->density, 0.1, 2000.0);
                    e.nextFree += std::max (1.0, per * (1.0 + 0.95 * std::clamp (sc->jitTime, 0.0f, 100.0f) * 0.01 * rndBi()));
                }
            }
        }
        // particles
        float l = 0, r = 0;
        for (auto& p : parts)
        {
            if (! p.on) continue;
            if (p.fade < 0 && p.gen != em[(size_t) p.owner].gen) p.fade = 63;
            const float v = renderParticle (p, smp) * em[(size_t) p.owner].gainSm;
            l += v * p.gl; r += v * p.gr;
        }
        const float prog = b.fxP0 + (b.fxP1 - b.fxP0) * (float) i / (float) std::max (1, n);
        fxSample (l, r, prog, b);
        accL[(size_t) i] = l; accR[(size_t) i] = r;
        ++sampleClock;
    }

    // safety: 18 Hz HP (4th order), 18 kHz ceiling (8th order), output gain, limiter -0.3 dBFS
    const float tgtGain = std::pow (10.0f, std::clamp (b.outDb, -60.0f, 6.0f) / 20.0f);
    const float gA = 1.0f - std::exp (-1.0f / (0.02f * (float) sr));
    const float rel = 1.0f - std::exp (-1.0f / (0.08f * (float) sr));
    const float ceil = 0.9660509f;
    float pk = 0, minG = 1;
    for (int i = 0; i < n; ++i)
    {
        double l = accL[(size_t) i], r = accR[(size_t) i];
        l = hp[0][1].tick (hp[0][0].tick (l)); r = hp[1][1].tick (hp[1][0].tick (r));
        for (int k = 0; k < 4; ++k) { l = lp[0][k].tick (l); r = lp[1][k].tick (r); }
        outGainSm += (tgtGain - outGainSm) * gA;
        float fl = (float) l * outGainSm, fr = (float) r * outGainSm;
        if (! std::isfinite (fl) || ! std::isfinite (fr)) { fl = fr = 0; reset(); }
        const float peak = std::max (std::abs (fl), std::abs (fr));
        const float need = peak * limGain > ceil ? ceil / peak : 1.0f;
        if (need < limGain) limGain = need; else limGain += (1.0f - limGain) * rel;
        fl = std::clamp (fl * limGain, -ceil, ceil); fr = std::clamp (fr * limGain, -ceil, ceil);
        L[i] = fl; R[i] = fr;
        pk = std::max (pk, std::max (std::abs (fl), std::abs (fr)));
        minG = std::min (minG, limGain);
    }
    int act = 0; for (auto& p : parts) act += p.on ? 1 : 0;
    activeParticles.store (act, std::memory_order_relaxed);
    limiterGr.store (minG, std::memory_order_relaxed);
    outPeak.store (pk, std::memory_order_relaxed);
    clockSec.store ((double) sampleClock / sr, std::memory_order_relaxed);
}
} // namespace qn
