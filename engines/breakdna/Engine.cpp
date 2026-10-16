// semi-rack: vendored; namespaces wrapped in dnafam:: (see sources.mk for every change).
#include "Engine.h"
#include "Dsp.h"

#include <algorithm>
#include <cmath>

namespace dnafam::dna
{

namespace
{
inline int wrap (int64_t v, int m) { return (int) (((v % m) + m) % m); }
}

void Engine::prepare (double sampleRate, int)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    lookahead = (int) std::lround (sr * 0.025);
    reset();
}

void Engine::reset()
{
    for (auto& v : voices) v.active = false;
    numQueued = 0;
    clock = 0;
    freePos = 0.0;
    wasPlaying = false;
    lastPos = lastMidiPos = -1.0e9;
    noteShift.fill (0);
    stepTaken.fill (-1000000);
    lastKit = nullptr;
}

bool Engine::popHit (HitEvent& h)
{
    const int r = hitRead.load (std::memory_order_relaxed);
    if (r == hitWrite.load (std::memory_order_acquire)) return false;
    h = hits[(size_t) (r & 255)];
    hitRead.store (r + 1, std::memory_order_release);
    return true;
}

void Engine::emitHit (const HitEvent& h)
{
    const int w = hitWrite.load (std::memory_order_relaxed);
    if (w - hitRead.load (std::memory_order_acquire) >= 255) return;   // the editor is not listening
    hits[(size_t) (w & 255)] = h;
    hitWrite.store (w + 1, std::memory_order_release);
}

void Engine::push (const Pending& e)
{
    if (numQueued < kQueue) queue[(size_t) numQueued++] = e;
}

int Engine::classOf (const Params& p, int note, int vel) const
{
    if (note == p.noteK) return Kick;
    if (note == p.noteS) return vel < 40 ? Ghost : Snare;
    if (note == p.noteH || note == p.noteH + 2 || note == p.noteH + 4) return Hat;
    return -1;
}

//==============================================================================
void Engine::trigger (const Kit& k, const Params& p, const std::array<std::atomic<float>, kMaxSteps>& lane,
                      int slice, int step, float amp, bool fromMidi, bool ghost, float offsetMs)
{
    if (slice < 0 || slice >= k.numSlices()) return;
    const int fadeLen = std::max (16, (int) (sr * 0.0015));
    if (p.choke)
        for (auto& v : voices)
            if (v.active && (v.fade < 0 || v.fade > fadeLen)) { v.fade = fadeLen; v.fadeLen = fadeLen; }

    // a free voice, else the oldest
    Voice* use = nullptr;
    for (auto& v : voices) if (! v.active) { use = &v; break; }
    if (use == nullptr)
    {
        use = &voices[0];
        for (auto& v : voices) if (v.age < use->age) use = &v;
    }
    float m = p.morph;
    if (step >= 0 && step < kMaxSteps) m += p.laneAmt * lane[(size_t) step].load (std::memory_order_relaxed);
    m = std::clamp (m, 0.0f, 1.0f);

    Voice& v = *use;
    v.active = true;
    v.slice = slice;
    // from the attack: the hit lands exactly when it is scheduled (the quiet before it is skipped)
    // (half a millisecond before it, so the very first rise is not cut: a constant, not a feel)
    v.start = std::max (k.sa.starts[(size_t) slice], k.attack[(size_t) slice] - (int) (k.rate * 0.0005));
    v.len = std::min (k.sa.starts[(size_t) slice] + k.sliceLength (slice), k.frames) - v.start;
    v.pos = 0.0;
    v.inc = k.rate / sr * std::pow (2.0, (double) p.tune / 12.0);
    v.amp = amp;
    v.morph = m;
    v.env = 1.0f;
    v.decayMul = p.decay >= 0.999f ? 1.0f : (float) std::exp (-1.0 / (0.008 * std::pow (150.0, (double) p.decay) * sr));
    v.fade = -1;
    v.fadeLen = fadeLen;
    v.rampPos = 0;
    v.rampLen = std::max (4, (int) (sr * 0.0005));
    v.age = ++ageCounter;

    HitEvent h;
    h.slice = (int16_t) slice;
    h.bslice = (int16_t) (slice < (int) k.match.size() ? k.match[(size_t) slice] : -1);
    h.step = (int8_t) step;
    h.cls = slice < (int) k.sa.cls.size() ? k.sa.cls[(size_t) slice] : 0;
    h.fromMidi = fromMidi ? 1 : 0;
    h.ghostAdded = ghost ? 1 : 0;
    h.amp = amp;
    h.morph = m;
    h.offsetMs = offsetMs;
    emitHit (h);
}

void Engine::render (const Kit& k, float* L, float* R, int from, int to, const Params& p)
{
    if (to <= from) return;
    for (auto& v : voices)
    {
        if (! v.active) continue;
        const float ml = v.morph * (float) (kLayers - 1);
        const int l0 = std::min (kLayers - 2, (int) ml);
        const float lf = ml - (float) l0;
        const float* a0 = k.L[(size_t) l0].data() + v.start;
        const float* a1 = k.L[(size_t) l0 + 1].data() + v.start;
        const float* b0 = k.R[(size_t) l0].data() + v.start;
        const float* b1 = k.R[(size_t) l0 + 1].data() + v.start;
        const double endPos = (double) v.len - 1.0;
        for (int i = from; i < to; ++i)
        {
            if (v.pos >= endPos) { v.active = false; break; }
            // start the end fade so it lands on the slice end
            if (v.fade < 0 && v.pos + v.inc * (double) v.fadeLen >= endPos) { v.fade = v.fadeLen; }
            const int ip = (int) v.pos;
            const float fr = (float) (v.pos - ip);
            const float xl0 = a0[ip] + fr * (a0[ip + 1] - a0[ip]);
            const float xl1 = a1[ip] + fr * (a1[ip + 1] - a1[ip]);
            const float xr0 = b0[ip] + fr * (b0[ip + 1] - b0[ip]);
            const float xr1 = b1[ip] + fr * (b1[ip + 1] - b1[ip]);
            float g = v.amp * v.env * p.gain;
            if (v.rampPos < v.rampLen) g *= (float) (v.rampPos++) / (float) v.rampLen;
            if (v.fade >= 0)
            {
                g *= (float) v.fade / (float) v.fadeLen;
                if (--v.fade <= 0) { v.active = false; break; }
            }
            L[i] += g * (xl0 + lf * (xl1 - xl0));
            R[i] += g * (xr0 + lf * (xr1 - xr0));
            v.env *= v.decayMul;
            v.pos += v.inc;
        }
    }
}

//==============================================================================
void Engine::process (const Kit* kit, const Params& p, const std::array<std::atomic<float>, kMaxSteps>& lane,
                      const MidiEvent* in, int numIn, float* outL, float* outR, int n,
                      MidiEvent* midiOutBuf, int& numOut)
{
    numOut = 0;
    std::fill (outL, outL + n, 0.0f);
    std::fill (outR, outR + n, 0.0f);

    if (kit != lastKit)
    {
        for (auto& v : voices) v.active = false;   // the layers they read are gone
        lastKit = kit;
        lastPos = lastMidiPos = -1.0e9;
    }

    const double bpm = std::clamp (p.bpm > 0.0 ? p.bpm : 120.0, 20.0, 400.0);
    viewBpm.store (bpm, std::memory_order_relaxed);
    const double sps = 60.0 / bpm * sr / 4.0;                 // samples per 16th
    const bool running = p.playing || p.run == RunAlways;
    // the host stopped: the break stops with it (a short fade, no hanging slices)
    if (wasPlaying && ! running)
        for (auto& v : voices)
            if (v.active && (v.fade < 0 || v.fade > 256)) { v.fade = std::max (16, (int) (sr * 0.004)); v.fadeLen = v.fade; }
    double pos0 = 0.0;
    if (p.playing) pos0 = p.ppq * 4.0 - (double) lookahead / sps;
    else if (running) { if (wasPlaying) freePos = lastPos > -1.0e8 ? lastPos : 0.0; pos0 = freePos; }
    const double pos1 = pos0 + (double) n / sps;
    if (! p.playing && running) freePos = pos1;
    if (! running) { lastPos = lastMidiPos = -1.0e9; }
    // a jump (loop, locate): start from here instead of catching up
    if (running && (pos0 < lastPos - 1.0e-6 || pos0 > lastPos + 1.0)) { lastPos = pos0 - 1.0e-9; lastMidiPos = pos0 - 1.0e-9; }
    wasPlaying = p.playing;

    const int steps = kit != nullptr ? kit->g.steps : 32;
    viewPlaying.store (running ? 1 : 0, std::memory_order_relaxed);
    viewStep.store (running ? wrap ((int64_t) std::floor (pos0) - p.rotate, steps) : -1, std::memory_order_relaxed);

    // ---- actions of this block, in time order
    struct Action { int at; int kind; int idx; int64_t absStep; float playPos; };
    std::array<Action, 192> act {};
    int na = 0;
    auto add = [&] (int at, int kind, int idx, int64_t absStep = 0, float pp = 0.0f)
    {
        if (na < (int) act.size()) act[(size_t) na++] = { std::clamp (at, 0, n - 1), kind, idx, absStep, pp };
    };
    enum { kBreakHit = 0, kPending = 1 };

    const bool haveKit = kit != nullptr && kit->numSlices() > 0;

    // BREAK A: the genome's hits that fall in [lastPos, pos1)
    if (haveKit && running && p.source == SourceBreak)
    {
        const auto& g = kit->g;
        const double from = std::max (lastPos, pos0 - 1.0e-9);
        for (int i = 0; i < (int) g.hits.size(); ++i)
        {
            const auto& h = g.hits[(size_t) i];
            if (h.cls == Ghost && p.ghosts <= 0.001f) continue;
            const double t = (double) h.step + (double) p.rotate + (double) p.feel * h.offset;
            double tt = t + std::ceil ((from - t) / steps) * steps;
            if (tt <= from) tt += steps;
            for (; tt < pos1; tt += steps)
                add ((int) std::floor ((tt - pos0) * sps), kBreakHit, i, (int64_t) std::floor (tt + 0.5), (float) tt);
        }
    }

    // MIDI in
    for (int e = 0; e < numIn; ++e)
    {
        const auto& ev = in[e];
        const int64_t T = clock + ev.offset;
        if (p.source == SourceBreak)
        {
            // the slices as pads, from C3 up; everything at the lookahead so it matches the timeline
            Pending q;
            q.at = T + lookahead;
            q.note = ev.note;
            q.vel = ev.vel;
            q.cls = 255;
            push (q);
            continue;
        }
        if (ev.vel == 0)
        {
            Pending q;
            q.at = T + noteShift[ev.note & 127];
            q.note = ev.note;
            q.vel = 0;
            push (q);
            continue;
        }
        const int c = classOf (p, ev.note, ev.vel);
        int64_t shift = lookahead;
        int sd = -1;
        int64_t s = 0;
        float offMs = 0.0f;
        int vel = ev.vel;
        if (c >= 0 && running && haveKit)
        {
            const double pos = pos0 + (double) ev.offset / sps;
            s = (int64_t) std::floor (pos + 0.5);
            sd = wrap (s - p.rotate, steps);
            const float off = kit->g.offsetFor (sd, c);
            shift = lookahead + (int64_t) std::llround ((double) p.feel * off * sps);
            shift = std::clamp<int64_t> (shift, 0, (int64_t) lookahead + (int64_t) sps);
            offMs = (float) ((double) (shift - lookahead) / sr * 1000.0);
            const float mean = std::max (0.05f, kit->g.meanLevel[c]);
            const float accent = std::clamp (kit->g.levelFor (sd, c) / mean, 0.1f, 3.0f);
            vel = std::clamp ((int) std::lround ((double) ev.vel * std::pow ((double) accent, (double) p.dyn)), 1, 127);
            if (c == Snare || c == Ghost) { stepTaken[(size_t) (s & 63)] = s; lastTaken = s; }
        }
        noteShift[ev.note & 127] = shift;
        Pending q;
        q.at = T + shift;
        q.note = ev.note;
        q.vel = (uint8_t) vel;
        q.cls = (uint8_t) (c >= 0 ? c : 255);
        q.step = (int8_t) sd;
        q.absStep = s;
        q.offsetMs = offMs;
        push (q);
    }

    // MIDI IN: A's ghosts on the steps you left empty
    if (haveKit && running && p.source == SourceMidi && p.ghosts > 0.001f)
    {
        const auto& g = kit->g;
        const double from = std::max (lastMidiPos, pos0 - 1.0e-9);
        for (int64_t s = (int64_t) std::floor (from) + 1; (double) s < pos1; ++s)
        {
            if ((double) s <= from) continue;
            const int sd = wrap (s - p.rotate, steps);
            const int h = g.hitOf (sd, Ghost);
            if (h < 0) continue;
            const float prob = std::min (1.0f, p.ghosts);
            const float r = (float) (gtbs::hash32 (p.seed, (uint32_t) s, 991u) & 0xFFFF) / 65535.0f;
            if (r >= prob) continue;
            const auto& hit = g.hits[(size_t) h];
            const float lvl = std::min (1.0f, hit.level * std::max (1.0f, p.ghosts));
            const int at = (int) std::floor (((double) s - pos0) * sps);
            int64_t shift = lookahead + (int64_t) std::llround ((double) p.feel * hit.offset * sps);
            shift = std::max<int64_t> (shift, 0);
            Pending q;
            q.at = clock + at + shift;
            q.note = (uint8_t) p.noteS;
            q.vel = (uint8_t) std::clamp ((int) std::lround (127.0f * lvl), 1, 127);
            q.cls = Ghost;
            q.step = (int8_t) sd;
            q.ghost = 1;
            q.absStep = s;
            q.offsetMs = (float) ((double) (shift - lookahead) / sr * 1000.0);
            push (q);
        }
    }

    // due queue entries
    for (int i = 0; i < numQueued; ++i)
        if (queue[(size_t) i].at < clock + n)
            add ((int) (queue[(size_t) i].at - clock), kPending, i);

    std::sort (act.begin(), act.begin() + na, [] (const Action& a, const Action& b) { return a.at != b.at ? a.at < b.at : a.kind < b.kind; });

    auto out = [&] (int at, int note, int vel)
    {
        if (numOut < kMaxOut) midiOutBuf[numOut++] = { at, (uint8_t) (note & 127), (uint8_t) std::clamp (vel, 0, 127) };
    };
    auto noteFor = [&] (int c) { return c == Kick ? p.noteK : c == Hat ? p.noteH : p.noteS; };

    int done = 0;
    for (int a = 0; a < na; ++a)
    {
        const auto& ac = act[(size_t) a];
        if (haveKit) render (*kit, outL, outR, done, ac.at, p);
        done = ac.at;
        if (ac.kind == kBreakHit)
        {
            const auto& h = kit->g.hits[(size_t) ac.idx];
            float amp = std::pow (std::max (0.02f, h.level), p.dyn);
            if (h.cls == Ghost) amp = std::min (1.5f, amp * p.ghosts);
            // the lane is drawn over the DNA's steps
            trigger (*kit, p, lane, h.slice, h.step, amp, false, false, (float) kit->g.ms (p.feel * h.offset));
            if (p.midiOut)
            {
                const int note = noteFor (h.cls);
                out (ac.at, note, (int) std::lround (std::min (1.0f, amp) * 127.0f));
                Pending off;
                off.at = clock + ac.at + (int64_t) std::max (1.0, sps * 0.9);
                off.note = (uint8_t) note;
                off.vel = 0;
                off.cls = 254;    // MIDI only
                push (off);
            }
        }
        else
        {
            const Pending q = queue[(size_t) ac.idx];
            queue[(size_t) ac.idx].at = INT64_MAX;   // consumed (compacted below)
            if (q.vel == 0) { if (p.midiOut || q.cls == 254) out (ac.at, q.note, 0); continue; }
            if (q.ghost && stepTaken[(size_t) (q.absStep & 63)] == q.absStep) continue;   // you played that step
            if (q.cls == 255 && p.source == SourceBreak)
            {
                // pads
                if (haveKit && q.note >= 60 && q.note < 60 + kit->numSlices())
                    trigger (*kit, p, lane, q.note - 60, -1, (float) q.vel / 127.0f, true, false, 0.0f);
                if (p.midiOut) out (ac.at, q.note, q.vel);
                continue;
            }
            if (q.cls < NumCls && haveKit)
            {
                const int step = q.step >= 0 ? q.step : 0;
                const int slice = kit->sliceFor (step, q.cls);
                trigger (*kit, p, lane, slice, q.step, (float) q.vel / 127.0f, true, q.ghost != 0, q.offsetMs);
            }
            if (p.midiOut) out (ac.at, q.note, q.vel);
            if (q.ghost)
            {
                Pending off;
                off.at = clock + ac.at + (int64_t) std::max (1.0, sps * 0.5);
                off.note = q.note;
                off.vel = 0;
                off.cls = 254;
                push (off);
            }
        }
    }
    if (haveKit) render (*kit, outL, outR, done, n, p);

    // compact the queue
    int w = 0;
    for (int i = 0; i < numQueued; ++i)
        if (queue[(size_t) i].at != INT64_MAX) queue[(size_t) w++] = queue[(size_t) i];
    numQueued = w;

    if (running) { lastPos = pos1 - 1.0e-9; lastMidiPos = pos1 - 1.0e-9; }
    clock += n;
}

} // namespace dnafam::dna
