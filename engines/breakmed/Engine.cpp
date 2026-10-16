#include "Engine.h"

#include <algorithm>
#include <cmath>

namespace bm
{

namespace
{
constexpr double kPi = 3.14159265358979323846;
inline double semisToRatio (double s) { return std::pow (2.0, s / 12.0); }
}

const char* chainName (int c)
{
    static const char* n[] = { "BASE", "P.DN", "P.UP", "SUB", "GLCH", "REV", "DRIL" };
    return (c >= 0 && c < kChains) ? n[c] : "?";
}

const char* chainFx (int c, int semis)
{
    // the tracker commands each chain stands in for
    switch (c)
    {
        case PitchDn:
            return semis == -1 ? "2-1" : semis == -2 ? "2-2" : "2-3";
        case PitchUp:  return semis == 1 ? "1+1" : "1+2";
        case Sub:      return "-12";
        case Glitch:   return "GRN";
        case Reverse:  return "REV";
        case Drill:    return "E9x";
        default:       return "---";
    }
}

namespace
{
constexpr int kWhite[7] = { 0, 2, 4, 5, 7, 9, 11 };
int whiteIndex (int note)   // -1 for black keys
{
    const int pc = ((note % 12) + 12) % 12, oct = (note - pc) / 12;
    for (int i = 0; i < 7; ++i) if (kWhite[i] == pc) return oct * 7 + i;
    return -1;
}
}

int sliceForNote (int note, int root, bool white)
{
    if (! white) return note - root;
    const int a = whiteIndex (note);
    int r = whiteIndex (root);
    if (r < 0) r = whiteIndex (root - 1) + 1;   // a black root counts from the next white key
    return a < 0 ? -1 : a - r;
}

int noteForSlice (int slice, int root, bool white)
{
    if (! white) return root + slice;
    int r = whiteIndex (root);
    if (r < 0) r = whiteIndex (root - 1) + 1;
    const int w = r + slice;
    return (w / 7) * 12 + kWhite[w % 7];
}

Engine::Engine()
{
    for (auto& f : chainFlash) f.store (0.0f);
}

void Engine::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    // capture: room for 8 bars down to 60 BPM, allocated here, never on the audio thread
    const size_t cap = (size_t) (sr * captureSeconds) + 16;   // SEMI port: was sr * 32
    capL.assign (cap, 0.0f);
    capR.assign (cap, 0.0f);
    merged.reserve (2048);
    allNotesOff();
}

void Engine::setPrepared (const Prepared* p)
{
    data = p;
    for (auto& v : voices) v.active = false;
    playerNote = -1;
}

void Engine::allNotesOff()
{
    for (auto& v : voices) v.active = false;
    playerNote = -1;
}

uint32_t Engine::hash (uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
    h ^= h >> 15; h *= 0x2C1B3C6Du;
    h ^= h >> 12; h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

float Engine::rand01 (Voice& v)
{
    v.rng ^= v.rng << 13; v.rng ^= v.rng >> 17; v.rng ^= v.rng << 5;
    return (float) (v.rng >> 8) / 16777216.0f;
}

// ---- the weighted chain selector ----------------------------------------------------------
Engine::Choice Engine::choose (int slice, int note, double ppq, bool hasPpq, const Params& p)
{
    Choice c;
    uint32_t a, b;
    const bool locked = p.lock && hasPpq;
    if (locked)
    {
        const int slots = std::max (1, p.lockBars) * 16;
        const int slot = (int) (((int64_t) std::floor (ppq * 4.0 + 1e-3)) % slots + slots) % slots;
        a = hash (p.seed, (uint32_t) slot, (uint32_t) note);
        b = hash (p.seed ^ 0xA5A5u, (uint32_t) slot, (uint32_t) note);
    }
    else
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; a = rng;
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; b = rng;
    }
    c.r = (int) (a >> 25);   // R in 0..127, as the MIDI-byte routing of the original technique

    float total = 0.0f;
    for (float w : p.weight) total += std::max (0.0f, w);
    int chain = Base;
    if (total > 0.0f)
    {
        float cum = 0.0f;
        for (int i = 0; i < kChains; ++i)
        {
            const float w = std::max (0.0f, p.weight[(size_t) i]);
            if (w <= 0.0f) continue;
            cum += w;
            if ((float) c.r < 128.0f * cum / total - 1e-4f) { chain = i; break; }
            chain = i;
        }
    }

    // pitch chains need at least one interval switched on
    int pool[3] = { 0, 0, 0 };
    int np = 0;
    if (chain == PitchDn) { for (int i = 0; i < 3; ++i) if (p.down[(size_t) i]) pool[np++] = -(i + 1); }
    if (chain == PitchUp) { for (int i = 0; i < 2; ++i) if (p.up[(size_t) i]) pool[np++] = i + 1; }
    if ((chain == PitchDn || chain == PitchUp) && np == 0) chain = Base;
    if (np > 0) c.semis = pool[(b >> 8) % (uint32_t) np];
    if (chain == Sub) c.semis = -12;

    // the rhythmic anchor
    if (p.anchor == Anchor::Downbeat)
    {
        const bool down = hasPpq ? std::fmod (std::fmod (ppq, 4.0) + 4.0, 4.0) < 0.125 : slice == 0;
        if (down) chain = Base;
    }
    else if (p.anchor == Anchor::Kicks && data != nullptr && slice >= 0 && slice < data->numSlices()
             && data->role[(size_t) slice] == RoleKick)
        chain = Base;

    // memory: a variation is usually answered by the original
    const float m = (float) ((b & 0xffffu)) / 65536.0f;
    if (chain != Base && prevChain != Base && m < p.memory) chain = Base;

    if (chain == Base) c.semis = 0;
    c.chain = chain;
    return c;
}

// ---- notes ---------------------------------------------------------------------------------
void Engine::startRelease (Voice& v, float ms)
{
    if (! v.active) return;
    const float samples = std::max (1.0f, ms * 0.001f * (float) sr);
    v.stage = 2;
    v.envStep = std::max (v.env, 1e-4f) / samples;
}

void Engine::noteOn (const Event& e, const Params& p, const Transport& t)
{
    if (data == nullptr || data->numSlices() == 0) return;
    const int slice = sliceForNote (e.note, p.root, p.whiteKeys);
    if (slice < 0 || slice >= data->numSlices()) return;

    Choice ch;
    if (e.forceChain >= 0)
    {
        ch = choose (slice, e.note, e.ppq, e.hasPpq, p);   // keeps the semitone pick random
        ch.chain = e.forceChain;
        if (ch.chain == PitchDn) ch.semis = -(1 + (int) ((rng >> 7) % 3));
        else if (ch.chain == PitchUp) ch.semis = 1 + (int) ((rng >> 7) % 2);
        else if (ch.chain == Sub) ch.semis = -12;
        else ch.semis = 0;
    }
    else
        ch = choose (slice, e.note, e.ppq, e.hasPpq, p);
    prevChain = ch.chain;

    if (p.choke)
        for (auto& v : voices)
            if (v.active) startRelease (v, 1.5f);

    Voice* v = nullptr;
    for (auto& x : voices) if (! x.active) { v = &x; break; }
    if (v == nullptr)
    {
        v = &voices[0];
        for (auto& x : voices) if (x.age > v->age) v = &x;
    }
    *v = Voice {};
    v->active = true;
    v->note = e.note;
    v->slice = slice;
    v->chain = ch.chain;
    v->semis = ch.semis;
    v->held = ! e.oneShot;
    v->oneShot = e.oneShot;
    v->rng = hash ((uint32_t) clock, (uint32_t) e.note, 77u) | 1u;
    v->gain = p.velocity ? 0.25f + 0.75f * std::clamp (e.velocity, 0.0f, 1.0f) : 1.0f;

    const double globalRatio = semisToRatio (p.pitch + p.fine / 100.0);
    const double base = data->rate / sr * globalRatio;
    const int pcmLen = (int) data->pcm.size();
    v->s0 = data->start[(size_t) slice];
    v->s1 = p.thru ? pcmLen : data->start[(size_t) slice + 1];

    const double bpm = (t.valid && t.bpm > 1.0) ? t.bpm
                     : loopBpm ((double) data->srcLen / data->srcRate, data->bars);
    const double sixteenth = 60.0 / bpm / 4.0;

    switch (ch.chain)
    {
        case Base:
            v->pos = v->s0; v->rate = base; break;
        case PitchDn:
        case PitchUp:
            v->pos = v->s0;
            if (p.pitchStretch)
            {
                v->rate = base;
                v->ratio = semisToRatio (ch.semis);
                v->cycle = std::max (16.0, p.cycleMs * 0.001 * data->rate);
                v->cyclePh = 0.0;
                v->anchorA = v->s0;
                v->grainClock = -1.0;    // no previous cycle yet
            }
            else
                v->rate = base * semisToRatio (ch.semis);
            break;
        case Sub:
            v->pos = v->s0; v->rate = base * 0.5; break;
        case Glitch:
        {
            v->pos = v->s0;
            v->rate = base * std::clamp ((double) p.glitchSpeed, 0.1, 1.0);
            v->ratio = base;
            v->g[0] = Grain {};
            v->g[1] = Grain {};
            spawnGrain (*v, 0, p);
            v->g[0].src = v->s0;    // the first grain starts on the hit
            v->g[0].rev = false;
            v->g[0].ph = v->g[0].len * 0.25;   // past the fade-in: keep the attack
            v->grainClock = 0.0;
            break;
        }
        case Reverse:
            v->pos = v->s1 - 1; v->rate = -base; break;
        case Drill:
        {
            const auto& w = data->wet[(size_t) slice];
            const int sl = data->start[(size_t) slice + 1] - data->start[(size_t) slice];
            const double loopLen = std::max (32.0, p.drillLoop * sixteenth * data->rate);
            v->pos = 0.0;
            v->rate = base;
            v->loopB = std::min ((double) w.size() - 2.0, std::max ((double) sl, loopLen));
            v->loopA = std::max (0.0, v->loopB - loopLen);
            v->dir = 1;
            static const double div[3] = { 2.0, 4.0, 8.0 };
            const double period = 60.0 / bpm / div[std::clamp (p.drillRate, 0, 2)];
            v->lfoInc = 1.0 / (period * sr);
            v->lfoPh = 0.0;
            v->s0 = 0;
            v->s1 = (int) w.size();
            v->maxAge = (int64_t) (sr * 60.0 / bpm * (e.oneShot || ! p.gate ? 1.0 : 4.0));
            break;
        }
        default: break;
    }

    if (p.attackMs > 0.05f)
    {
        v->env = 0.0f; v->stage = 0;
        v->envStep = 1.0f / std::max (1.0f, p.attackMs * 0.001f * (float) sr);
    }
    else
    {
        v->env = 1.0f; v->stage = 1;
    }

    // log for the editor
    const uint32_t s = logSerial.load (std::memory_order_relaxed) + 1;
    Hit& h = log[s % kLog];
    h.slice = slice; h.chain = ch.chain; h.semis = ch.semis; h.r = ch.r; h.note = e.note;
    h.velocity = e.velocity; h.serial = s;
    logSerial.store (s, std::memory_order_release);
    chainFlash[(size_t) ch.chain].store (1.0f);
    lastR.store (ch.r);
}

void Engine::noteOff (int note, const Params& p)
{
    for (auto& v : voices)
    {
        if (! v.active || v.note != note || ! v.held) continue;
        v.held = false;
        if (p.gate && ! v.oneShot) startRelease (v, std::max (0.7f, p.releaseMs));
    }
}

// ---- reading ---------------------------------------------------------------------------------
float Engine::read (const std::vector<float>& b, double p, int lo, int hi, bool interp) const
{
    if (p < lo || p >= hi) return 0.0f;
    const int i = (int) p;
    if (! interp) return b[(size_t) i];   // Paula: the sample holds until the next one
    const float f = (float) (p - i);
    const float a = b[(size_t) i];
    const float c = (i + 1 < hi) ? b[(size_t) i + 1] : 0.0f;
    return a + (c - a) * f;
}

void Engine::spawnGrain (Voice& v, int k, const Params& p)
{
    auto& g = v.g[k];
    const double minL = 0.020 * data->rate;
    const double maxL = std::max (minL, p.grainMs * 0.001 * data->rate);
    g.len = minL + (maxL - minL) * rand01 (v);
    const double jitter = ((double) rand01 (v) * 2.0 - 1.0) * p.flux * g.len * 2.0;
    g.src = v.pos + jitter;
    g.rev = rand01 (v) < p.flux * 0.35f;
    g.ph = 0.0;
    g.on = true;
}

float Engine::render (Voice& v, const Params& p)
{
    float s = 0.0f;
    const bool ip = p.interp;
    switch (v.chain)
    {
        case Base:
        case Sub:
            s = readPcm (v.pos, v.s0, v.s1, ip);
            v.pos += v.rate;
            if (v.pos >= v.s1) v.active = false;
            break;
        case PitchDn:
        case PitchUp:
            if (! p.pitchStretch || v.cycle <= 1.0)
            {
                s = readPcm (v.pos, v.s0, v.s1, ip);
                v.pos += v.rate;
                if (v.pos >= v.s1) v.active = false;
                break;
            }
            else
            {
                // Akai-style cyclic stretch: every cycle restarts the read at the timeline,
                // the last cycle fades out under the new one
                const double xf = v.cycle * 0.18;
                float cur = readPcm (v.anchorA + v.cyclePh * v.ratio, v.s0, v.s1, ip);
                float gIn = 1.0f;
                if (v.anchorA > v.s0 && v.cyclePh < xf) gIn = (float) (v.cyclePh / xf);
                s = cur * gIn;
                if (v.grainClock >= 0.0)
                {
                    const double ph = v.grainClock;
                    const float gOut = (float) std::max (0.0, 1.0 - (ph - v.cycle) / xf);
                    s += readPcm (v.anchorB + ph * v.ratio, v.s0, v.s1, ip) * gOut;
                    v.grainClock += v.rate;
                    if (v.grainClock >= v.cycle + xf) v.grainClock = -1.0;
                }
                v.cyclePh += v.rate;
                v.pos += v.rate;
                if (v.cyclePh >= v.cycle)
                {
                    v.anchorB = v.anchorA;
                    v.grainClock = v.cyclePh;
                    v.anchorA += v.cycle;
                    v.cyclePh -= v.cycle;
                }
                if (v.pos >= v.s1) v.active = false;
            }
            break;
        case Glitch:
        {
            for (int k = 0; k < 2; ++k)
            {
                auto& g = v.g[k];
                if (! g.on) continue;
                const double t = g.ph / g.len;
                const double taper = 0.25;
                float w = 1.0f;
                if (t < taper) w = (float) (0.5 - 0.5 * std::cos (kPi * t / taper));
                else if (t > 1.0 - taper) w = (float) (0.5 - 0.5 * std::cos (kPi * (1.0 - t) / taper));
                const double rp = g.rev ? g.src + g.len - g.ph : g.src + g.ph;
                s += readPcm (rp, v.s0, v.s1, ip) * w;
                g.ph += v.ratio;
                if (g.ph >= g.len) spawnGrain (v, k, p);
            }
            // the second grain enters half way through the first
            if (! v.g[1].on && v.g[0].ph >= v.g[0].len * 0.5) spawnGrain (v, 1, p);
            s *= 0.85f;
            v.pos += v.rate;
            if (v.pos >= v.s1) v.active = false;
            break;
        }
        case Reverse:
            s = readPcm (v.pos, v.s0, v.s1, ip);
            v.pos += v.rate;
            if (v.pos < v.s0) v.active = false;
            break;
        case Drill:
        {
            const auto& w = data->wet[(size_t) v.slice];
            s = read (w, v.pos, 0, (int) w.size(), ip);
            v.pos += v.rate * v.dir;
            if (v.dir > 0 && v.pos >= v.loopB) { v.dir = -1; v.pos = 2.0 * v.loopB - v.pos; }
            else if (v.dir < 0 && v.pos <= v.loopA) { v.dir = 1; v.pos = 2.0 * v.loopA - v.pos; }
            // 1/16 square LFO on level and pan, slightly rounded
            const bool hi = v.lfoPh < 0.5;
            const float targetAmp = hi ? 1.0f : 0.22f;
            const float targetPan = (hi ? -1.0f : 1.0f) * 0.85f * p.drillPan;
            const float k = 0.02f;
            v.envTarget += (targetAmp - v.envTarget) * k;   // reuse: smoothed LFO amp
            v.pan += (targetPan - v.pan) * k;
            s *= v.envTarget;
            v.lfoPh += v.lfoInc;
            if (v.lfoPh >= 1.0) v.lfoPh -= 1.0;
            if (v.maxAge > 0 && v.age >= v.maxAge && v.stage != 2) startRelease (v, 25.0f);
            break;
        }
        default: break;
    }

    // de-click at the end of a forward slice
    if (v.chain != Reverse && v.chain != Drill && v.rate > 0.0)
    {
        const double fade = data->rate * 0.0008;
        const double left = v.s1 - v.pos;
        if (left < fade) s *= (float) std::max (0.0, left / fade);
    }
    else if (v.chain == Reverse)
    {
        const double fade = data->rate * 0.0008;
        const double left = v.pos - v.s0;
        if (left < fade) s *= (float) std::max (0.0, left / fade);
    }

    // envelope
    if (v.stage == 0)
    {
        v.env += v.envStep;
        if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 1; }
    }
    else if (v.stage == 2)
    {
        v.env -= v.envStep;
        if (v.env <= 0.0f) { v.env = 0.0f; v.active = false; }
    }
    ++v.age;
    return s * v.env * v.gain;
}

// ---- built-in player -------------------------------------------------------------------------
void Engine::runPlayer (int n, const Params& p, const Transport& t, std::vector<Event>& out)
{
    const bool canRun = p.play && data != nullptr && data->numSlices() > 0
                        && (! p.syncHost || (t.valid && t.playing));
    if (! canRun)
    {
        if (playerNote >= 0) { Event e; e.offset = 0; e.on = false; e.note = playerNote; out.push_back (e); }
        playerNote = -1;
        wasRunning = false;
        playerRunning.store (false);
        lastPpqEnd = -1.0;
        return;
    }
    const double breakBpm = loopBpm ((double) data->srcLen / data->srcRate, data->bars);
    const double bpm = (t.valid && t.bpm > 1.0) ? t.bpm : breakBpm;
    double ppq0;
    if (p.syncHost) ppq0 = t.ppq;
    else
    {
        if (! wasRunning) freePpq = 0.0;
        ppq0 = freePpq;
    }
    const double beatsPerSample = bpm / 60.0 / sr;
    const double ppq1 = ppq0 + n * beatsPerSample;
    if (! p.syncHost) freePpq = ppq1;

    const bool jumped = ! wasRunning || std::abs (ppq0 - lastPpqEnd) > 1e-3;
    lastPpqEnd = ppq1;
    wasRunning = true;
    playerRunning.store (true);
    playPpq.store (ppq0);

    const double loop = data->bars * 4.0;
    struct Trig { double ppq; int slice; };
    Trig trig[kMaxSlices * 2];
    int nt = 0;
    const double base = std::floor (ppq0 / loop) * loop;
    for (int rep = 0; rep < 2; ++rep)
        for (int i = 0; i < data->numSlices(); ++i)
        {
            const double tp = base + rep * loop + data->beat[(size_t) i];
            if (tp >= ppq0 - 1e-9 && tp < ppq1 && nt < kMaxSlices * 2) trig[nt++] = { tp, i };
        }
    if (jumped && nt == 0)
    {
        // started inside a slice: pick it up at once so the loop sounds straight away
        const double pos = ppq0 - base;
        int cur = 0;
        for (int i = 0; i < data->numSlices(); ++i) if (data->beat[(size_t) i] <= pos + 1e-9) cur = i;
        if (pos - data->beat[(size_t) cur] < 0.26) trig[nt++] = { ppq0, cur };
    }
    for (int k = 0; k < nt; ++k)
    {
        const int off = std::clamp ((int) std::floor ((trig[k].ppq - ppq0) / beatsPerSample), 0, n - 1);
        if (playerNote >= 0) { Event e; e.offset = off; e.on = false; e.note = playerNote; out.push_back (e); }
        Event e; e.offset = off; e.on = true; e.note = noteForSlice (trig[k].slice, p.root, p.whiteKeys); e.velocity = 100.0f / 127.0f;
        e.ppq = trig[k].ppq; e.hasPpq = true;
        out.push_back (e);
        playerNote = e.note;
    }
}

// ---- block -----------------------------------------------------------------------------------
void Engine::process (float* outL, float* outR, int n, const std::vector<Event>& events,
                      const Params& p, const Transport& t)
{
    std::fill (outL, outL + n, 0.0f);
    std::fill (outR, outR + n, 0.0f);

    merged.clear();
    for (const auto& e : events) merged.push_back (e);
    runPlayer (n, p, t, merged);
    // insertion sort: stable, and unlike std::stable_sort it never allocates on the audio thread
    auto before = [] (const Event& a, const Event& b)
    {
        if (a.offset != b.offset) return a.offset < b.offset;
        return ! a.on && b.on;     // offs first at the same sample
    };
    for (size_t i = 1; i < merged.size(); ++i)
    {
        Event e = merged[i];
        size_t j = i;
        while (j > 0 && before (e, merged[j - 1])) { merged[j] = merged[j - 1]; --j; }
        merged[j] = e;
    }

    // capture: arm / cancel
    if (captureCancel.exchange (false)) { capState.store ((int) CapState::Idle); capProgress.store (0.0f); }
    int capStartAt = -1;
    const double bpmNow = (t.valid && t.bpm > 1.0) ? t.bpm
                        : (data != nullptr && data->srcLen > 0 ? loopBpm ((double) data->srcLen / data->srcRate, data->bars) : 120.0);
    if (captureReq.exchange (false) && capState.load() == (int) CapState::Idle)
    {
        capBpm = bpmNow;
        capTarget = (int) std::min ((double) capL.size(), p.captureBars * 4.0 * 60.0 / capBpm * sr);
        capPos = 0;
        capLen = 0;
        capState.store ((int) CapState::Waiting);
    }
    if (capState.load() == (int) CapState::Waiting)
    {
        if (t.valid && t.playing)
        {
            const double bps = t.bpm / 60.0 / sr;
            const double nextBar = std::ceil (t.ppq / 4.0 - 1e-9) * 4.0;
            const int off = (int) std::floor ((nextBar - t.ppq) / bps);
            if (off < n) capStartAt = std::max (0, off);
        }
        else
            capStartAt = 0;   // no running host clock: start now
    }

    const float master = std::clamp (p.volume, 0.0f, 64.0f) / 64.0f;
    const float aA500 = (float) (1.0 - std::exp (-2.0 * kPi * 4420.0 / sr));
    // LED: 2-pole lowpass 3090 Hz, Q 0.66 (TPT SVF)
    const float g = (float) std::tan (kPi * 3090.0 / sr);
    const float kq = 1.0f / 0.66f;
    const float a1 = 1.0f / (1.0f + g * (g + kq)), a2 = g * a1, a3 = g * a2;
    const float hpR = (float) std::exp (-2.0 * kPi * 5.0 / sr);

    size_t ei = 0;
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        while (ei < merged.size() && merged[ei].offset <= i)
        {
            const auto& e = merged[ei++];
            if (e.on)
            {
                Event ev = e;
                if (! ev.hasPpq && t.valid && t.playing)
                {
                    ev.ppq = t.ppq + i * t.bpm / 60.0 / sr;
                    ev.hasPpq = true;
                }
                noteOn (ev, p, t);
            }
            else noteOff (e.note, p);
        }
        float l = 0.0f, r = 0.0f;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const float s = render (v, p);
            const float pan = std::clamp (v.pan, -1.0f, 1.0f);
            l += s * std::min (1.0f, 1.0f - pan);
            r += s * std::min (1.0f, 1.0f + pan);
        }
        l *= master; r *= master;

        if (p.filter != Filter::Off)
        {
            float* ch[2] = { &l, &r };
            for (int c = 0; c < 2; ++c)
            {
                float& x = *ch[c];
                aLp[c] += aA500 * (x - aLp[c]);
                x = aLp[c];
                if (p.filter == Filter::Led)
                {
                    const float v3 = x - ledB[c];
                    const float v1 = a1 * ledA[c] + a2 * v3;
                    const float v2 = ledB[c] + a2 * ledA[c] + a3 * v3;
                    ledA[c] = 2.0f * v1 - ledA[c];
                    ledB[c] = 2.0f * v2 - ledB[c];
                    x = v2;
                }
                // Amiga output DC blocker
                const float y = x - hpX[c] + hpR * hpY[c];
                hpX[c] = x; hpY[c] = y; x = y;
            }
        }

        outL[i] = l; outR[i] = r;
        peak = std::max (peak, std::max (std::abs (l), std::abs (r)));

        if (capStartAt == i) { capState.store ((int) CapState::Recording); capStartAt = -1; }
        if (capState.load (std::memory_order_relaxed) == (int) CapState::Recording)
        {
            if (capPos < capTarget) { capL[(size_t) capPos] = l; capR[(size_t) capPos] = r; ++capPos; }
            if (capPos >= capTarget) { capLen = capTarget; capState.store ((int) CapState::Done); }
        }
        ++clock;
    }
    while (ei < merged.size())
    {
        const auto& e = merged[ei++];
        if (e.on) noteOn (e, p, t); else noteOff (e.note, p);
    }

    if (capState.load() == (int) CapState::Recording)
        capProgress.store (capTarget > 0 ? (float) capPos / (float) capTarget : 0.0f);
    else if (capState.load() == (int) CapState::Done)
        capProgress.store (1.0f);

    peakHold = std::max (peak, peakHold * 0.9f);
    outPeak.store (peakHold);
    for (int k = 0; k < kVoices; ++k)
    {
        const auto& v = voices[(size_t) k];
        auto& vw = view[(size_t) k];
        if (! v.active || data == nullptr) { vw.slice.store (-1); continue; }
        double pos = v.pos;
        if (v.chain == Drill)
            pos = data->start[(size_t) v.slice] + std::min (v.pos, (double) (data->start[(size_t) v.slice + 1] - data->start[(size_t) v.slice]));
        vw.slice.store (v.slice);
        vw.chain.store (v.chain);
        vw.pos.store ((float) (pos / std::max<size_t> (1, data->pcm.size())));
        vw.level.store (v.env * v.gain);
    }
}

} // namespace bm
