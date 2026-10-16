// semi-rack: vendored; namespaces wrapped in dnafam:: (see sources.mk for every change).
#include "Genotype.h"
#include "../breakdna/Dsp.h"

#include <algorithm>
#include <cmath>

namespace dnafam::geno
{

namespace
{
double resetLength (const Params& p)
{
    return 16.0 * (p.resetBars > 0.0f ? (double) p.resetBars : (double) formBars (p.form));
}
bool onReset (double pos16, double len)
{
    double r = std::fmod (pos16, len);
    if (r < 0.0) r += len;
    return r < 1.0e-6 || len - r < 1.0e-6;
}
} // namespace

int playTick (const Bundle& b, Brain& brain, const Params& p, walk::Walker& w, int64_t k, bool resetHere,
              Note* out, int maxOut, int& stepOut, int& roleOut)
{
    const auto& L = b.learned;
    const auto& g = b.kit->g;
    const int n = std::max (1, L.steps);
    const double mult = std::clamp ((double) p.mult, 0.25, 4.0);
    const double pos = (double) k * mult;
    if (resetHere) w.reset (p.startStep, n);
    const Role role = roleAt (pos, p.form, p.ending);
    const walk::Matrix& m = role == RoleCall ? brain.call : role == RoleResponse ? brain.resp : brain.fill;
    const int s = w.tick (m, n);
    stepOut = s;
    roleOut = (int) role;
    if (s < 0 || s >= n) return 0;
    const auto& se = brain.steps[(size_t) s];
    if (se.mute.load (std::memory_order_relaxed) != 0) return 0;
    // [p probability] on the trig, then on the delay
    if (w.rnd.unit() * 100.0 >= (double) se.prob.load (std::memory_order_relaxed)) return 0;
    double shift = (k & 1) ? (double) std::clamp (p.swing, 0.0f, 1.0f) * 0.5 * mult : 0.0;
    const float d = se.delay.load (std::memory_order_relaxed);
    if (d > 0.0f && w.rnd.unit() * 100.0 < (double) se.delayProb.load (std::memory_order_relaxed)) shift += (double) d * mult;
    const float velScale = se.vel.load (std::memory_order_relaxed) * 0.01f;

    int c = 0;
    for (int q = L.stepStart[(size_t) s]; q < L.stepStart[(size_t) s + 1] && c < maxOut; ++q)
    {
        const int i = L.hitsByStep[(size_t) q];
        const auto& h = g.hits[(size_t) i];
        const int lane = L.hitLane[(size_t) i];
        if (brain.lanes[(size_t) lane].mute.load (std::memory_order_relaxed) != 0) continue;
        if (lane == LGhost && p.ghosts <= 0.001f) continue;
        float amp = std::pow (std::max (0.02f, h.level), p.dyn);
        if (lane == LGhost) amp = std::min (1.5f, amp * p.ghosts);
        amp *= velScale;
        if (amp <= 0.0005f) continue;
        Note nt;
        nt.pos = pos + shift + std::clamp ((double) p.feel * (double) h.offset, -0.45 * mult, 2.0);
        nt.hit = i;
        nt.lane = lane;
        nt.step = s;
        nt.role = (int) role;
        nt.amp = amp;
        out[c++] = nt;
    }
    return c;
}

std::vector<Note> walkOffline (const Bundle& b, Brain& brain, const Params& p, int bars, uint32_t seed)
{
    std::vector<Note> notes;
    if (b.kit == nullptr || b.learned.steps <= 0) return notes;
    walk::Walker w;
    w.seed (seed);
    const double mult = std::clamp ((double) p.mult, 0.25, 4.0);
    const double len = resetLength (p);
    const int64_t ticks = (int64_t) std::floor (16.0 * bars / mult + 1.0e-9);
    std::array<Note, 64> tmp {};
    for (int64_t k = 0; k < ticks; ++k)
    {
        int s = -1, r = 0;
        const int c = playTick (b, brain, p, w, k, k == 0 || onReset ((double) k * mult, len), tmp.data(), (int) tmp.size(), s, r);
        for (int i = 0; i < c; ++i) notes.push_back (tmp[(size_t) i]);
    }
    std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& c) { return a.pos < c.pos; });
    return notes;
}

//==============================================================================
template <typename T, size_t N>
void Engine::emit (std::array<T, N>& ring, std::atomic<int>& wr, std::atomic<int>& rd, const T& v)
{
    const int w = wr.load (std::memory_order_relaxed);
    if (w - rd.load (std::memory_order_acquire) >= (int) N - 1) return;
    ring[(size_t) (w % (int) N)] = v;
    wr.store (w + 1, std::memory_order_release);
}

template <typename T, size_t N>
bool Engine::pop (std::array<T, N>& ring, std::atomic<int>& wr, std::atomic<int>& rd, T& v)
{
    const int r = rd.load (std::memory_order_relaxed);
    if (r == wr.load (std::memory_order_acquire)) return false;
    v = ring[(size_t) (r % (int) N)];
    rd.store (r + 1, std::memory_order_release);
    return true;
}

bool Engine::popHit (HitEvent& h) { return pop (hits, hitWrite, hitRead, h); }
bool Engine::popMove (MoveEvent& m) { return pop (moves, moveWrite, moveRead, m); }

void Engine::prepare (double sampleRate, int)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
}

void Engine::reset()
{
    for (auto& v : voices) v.active = false;
    numQueued = 0;
    clock = 0;
    freePos = 0.0;
    lastPos = -1.0e9;
    nextTick = INT64_MIN;
    wasRunning = false;
    lastBundle = nullptr;
}

//==============================================================================
void Engine::trigger (const dna::Kit& k, const Params& p, int slice, int lane, float amp, int step, int role, float offMs)
{
    if (slice < 0 || slice >= k.numSlices()) return;
    const int fadeLen = std::max (16, (int) (sr * 0.0015));
    for (auto& v : voices)
    {
        if (! v.active || (v.fade >= 0 && v.fade <= fadeLen)) continue;
        // MONO: a break, every hit cuts the last. Else the device's "mute": a lane with
        // choke on stops when another lane fires.
        if (p.mono || (v.lane != lane && lastChoke[(size_t) v.lane])) { v.fade = fadeLen; v.fadeLen = fadeLen; }
    }
    Voice* use = nullptr;
    for (auto& v : voices) if (! v.active) { use = &v; break; }
    if (use == nullptr)
    {
        use = &voices[0];
        for (auto& v : voices) if (v.age < use->age) use = &v;
    }
    Voice& v = *use;
    v.active = true;
    v.slice = slice;
    v.lane = lane;
    v.start = std::max (k.sa.starts[(size_t) slice], k.attack[(size_t) slice] - (int) (k.rate * 0.0005));
    v.len = std::min (k.sa.starts[(size_t) slice] + k.sliceLength (slice), k.frames) - v.start;
    v.pos = 0.0;
    v.inc = k.rate / sr * std::pow (2.0, (double) p.tune / 12.0);
    v.amp = amp;
    v.morph = std::clamp (p.morph, 0.0f, 1.0f);
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
    h.lane = (int8_t) lane;
    h.role = (int8_t) role;
    h.amp = amp;
    h.offsetMs = offMs;
    emit (hits, hitWrite, hitRead, h);
}

void Engine::render (const dna::Kit& k, float* L, float* R, int from, int to, const Params& p)
{
    if (to <= from) return;
    for (auto& v : voices)
    {
        if (! v.active) continue;
        const float ml = v.morph * (float) (dna::kLayers - 1);
        const int l0 = std::min (dna::kLayers - 2, (int) ml);
        const float lf = ml - (float) l0;
        const float* a0 = k.L[(size_t) l0].data() + v.start;
        const float* a1 = k.L[(size_t) l0 + 1].data() + v.start;
        const float* b0 = k.R[(size_t) l0].data() + v.start;
        const float* b1 = k.R[(size_t) l0 + 1].data() + v.start;
        const double endPos = (double) v.len - 1.0;
        for (int i = from; i < to; ++i)
        {
            if (v.pos >= endPos) { v.active = false; break; }
            if (v.fade < 0 && v.pos + v.inc * (double) v.fadeLen >= endPos) v.fade = v.fadeLen;
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
void Engine::process (const Bundle* b, Brain& brain, const Params& p, const MidiEvent* in, int numIn,
                      float* outL, float* outR, int n, MidiEvent* midiOutBuf, int& numOut)
{
    numOut = 0;
    std::fill (outL, outL + n, 0.0f);
    std::fill (outR, outR + n, 0.0f);
    for (int l = 0; l < NumLanes; ++l) lastChoke[(size_t) l] = brain.lanes[(size_t) l].choke.load (std::memory_order_relaxed) != 0;

    const bool haveKit = b != nullptr && b->kit != nullptr && b->kit->numSlices() > 0 && b->learned.steps > 0;
    if (b != lastBundle)
    {
        for (auto& v : voices) v.active = false;
        int w = 0;   // pending hits point into the old genome
        for (int i = 0; i < numQueued; ++i) if (queue[(size_t) i].hit < 0) queue[(size_t) w++] = queue[(size_t) i];
        numQueued = w;
        lastBundle = b;
        nextTick = INT64_MIN;
    }
    if (p.seed != seedSeen) { walker.seed (p.seed * 2654435761u + 7u); seedSeen = p.seed; }

    const double bpm = std::clamp (p.bpm > 0.0 ? p.bpm : 120.0, 20.0, 400.0);
    viewBpm.store (bpm, std::memory_order_relaxed);
    const double sps = 60.0 / bpm * sr / 4.0;
    const double mult = std::clamp ((double) p.mult, 0.25, 4.0);
    const bool running = p.playing || p.run == RunAlways;
    viewPlaying.store (running ? 1 : 0, std::memory_order_relaxed);

    auto out = [&] (int at, int note, int vel)
    {
        if (numOut < kMaxOut) midiOutBuf[numOut++] = { std::clamp (at, 0, n - 1), (uint8_t) (note & 127), (uint8_t) std::clamp (vel, 0, 127) };
    };

    if (wasRunning && ! running)
    {
        for (auto& v : voices)
            if (v.active && (v.fade < 0 || v.fade > 256)) { v.fade = std::max (16, (int) (sr * 0.004)); v.fadeLen = v.fade; }
        int w = 0;   // drop what the walk had decided, keep the note offs
        for (int i = 0; i < numQueued; ++i) if (queue[(size_t) i].timed) queue[(size_t) w++] = queue[(size_t) i];
        numQueued = w;
        nextTick = INT64_MIN;
        viewStep.store (-1, std::memory_order_relaxed);
    }

    double pos0 = 0.0;
    if (p.playing) pos0 = p.ppq * 4.0;
    else if (running) { if (wasRunning && lastPos > -1.0e8) freePos = lastPos; pos0 = freePos; }
    const double pos1 = pos0 + (double) n / sps;
    if (! p.playing && running) freePos = pos1;
    if (! running) freePos = 0.0;

    // ---- control
    if (haltRequest.exchange (0) != 0) { walker.halt(); viewHalted.store (1); }
    if (const int j = jumpRequest.exchange (-1); j >= 0) { walker.jump (j); viewHalted.store (0); }
    if (resetRequest.exchange (0) != 0) pendingReset = true;

    if (running && haveKit)
    {
        // a jump (loop, locate, start) or a new TIME: start the walk from here
        const bool jumped = nextTick == INT64_MIN || pos0 < lastPos - 1.0e-6 || pos0 > lastPos + 1.0 || std::abs (mult - lastMult) > 1.0e-9;
        if (jumped)
        {
            int w = 0;
            for (int i = 0; i < numQueued; ++i) if (queue[(size_t) i].timed) queue[(size_t) w++] = queue[(size_t) i];
            numQueued = w;
            nextTick = (int64_t) std::ceil (pos0 / mult - 1.0e-9);   // late hits of the first tick play at once
            lastMult = mult;
            pendingReset = true;
        }
        const double len = resetLength (p);
        std::array<Note, 64> tmp {};
        while ((double) nextTick * mult - 0.5 * mult < pos1)
        {
            const bool rs = pendingReset || onReset ((double) nextTick * mult, len);
            if (rs) { pendingReset = false; viewHalted.store (0); }
            int s = -1, r = 0;
            const int c = playTick (*b, brain, p, walker, nextTick, rs, tmp.data(), (int) tmp.size(), s, r);
            // the ball's move shows when its tick is reached
            Pending mv;
            mv.pos = (double) nextTick * mult;
            mv.hit = -2;
            mv.step = s;
            mv.role = r;
            mv.lane = walker.lastFrom - 1;
            mv.vel = (uint8_t) (c > 0 ? 1 : 0);
            push (mv);
            for (int i = 0; i < c; ++i)
            {
                Pending q;
                q.pos = tmp[(size_t) i].pos;
                q.hit = tmp[(size_t) i].hit;
                q.lane = tmp[(size_t) i].lane;
                q.step = tmp[(size_t) i].step;
                q.role = tmp[(size_t) i].role;
                q.amp = tmp[(size_t) i].amp;
                push (q);
            }
            ++nextTick;
        }
        viewTick.store (nextTick, std::memory_order_relaxed);
    }

    // ---- MIDI in: C3 up plays the slices as pads, everything else goes through
    for (int e = 0; e < numIn; ++e)
    {
        const auto& ev = in[e];
        if (haveKit && ev.note >= 60 && ev.note < 60 + b->kit->numSlices())
        {
            if (ev.vel > 0)
            {
                Pending q;
                q.timed = true;
                q.at = clock + ev.offset;
                q.hit = -3;
                q.step = ev.note - 60;
                q.amp = (float) ev.vel / 127.0f;
                push (q);
            }
            continue;
        }
        out (ev.offset, ev.note, ev.vel);
    }

    // ---- due entries, in time order
    struct Action { int at; int idx; };
    std::array<Action, kQueue> act {};
    int na = 0;
    for (int i = 0; i < numQueued; ++i)
    {
        const auto& q = queue[(size_t) i];
        int at = -1;
        if (q.timed) { if (q.at < clock + n) at = (int) std::max<int64_t> (0, q.at - clock); }
        else if (running && q.pos < pos1) at = (int) std::floor (std::max (0.0, q.pos - pos0) * sps);
        if (at >= 0) act[(size_t) na++] = { std::min (at, n - 1), i };
    }
    std::sort (act.begin(), act.begin() + na, [] (const Action& a, const Action& c) { return a.at < c.at; });

    int done = 0;
    for (int a = 0; a < na; ++a)
    {
        const int at = act[(size_t) a].at;
        if (haveKit) render (*b->kit, outL, outR, done, at, p);
        done = at;
        Pending q = queue[(size_t) act[(size_t) a].idx];
        queue[(size_t) act[(size_t) a].idx].hit = -9;   // consumed
        queue[(size_t) act[(size_t) a].idx].timed = false;
        queue[(size_t) act[(size_t) a].idx].pos = 1.0e300;
        if (q.hit == -2)
        {
            viewStep.store (q.step, std::memory_order_relaxed);
            viewRole.store (q.role, std::memory_order_relaxed);
            MoveEvent m;
            m.from = (int8_t) q.lane;
            m.to = (int8_t) q.step;
            m.role = (int8_t) q.role;
            m.fired = q.vel;
            emit (moves, moveWrite, moveRead, m);
            continue;
        }
        if (q.hit == -1 && q.timed) { out (at, q.note, 0); continue; }   // a note off
        if (! haveKit) continue;
        if (q.hit == -3)
        {
            trigger (*b->kit, p, q.step, LFx, q.amp, -1, 0, 0.0f);
            continue;
        }
        if (q.hit < 0 || q.hit >= (int) b->kit->g.hits.size()) continue;
        const auto& h = b->kit->g.hits[(size_t) q.hit];
        const float offMs = (float) b->kit->g.ms (p.feel * h.offset);
        trigger (*b->kit, p, h.slice, q.lane, q.amp, q.step, q.role, offMs);
        if (p.midiOut)
        {
            const int note = brain.lanes[(size_t) q.lane].note.load (std::memory_order_relaxed);
            out (at, note, (int) std::lround (std::min (1.0f, q.amp) * 127.0f));
            Pending off;
            off.timed = true;
            off.at = clock + at + (int64_t) std::max (1.0, 0.5 * mult * sps);
            off.note = (uint8_t) note;
            off.hit = -1;
            push (off);
        }
    }
    if (haveKit) render (*b->kit, outL, outR, done, n, p);

    int w = 0;
    for (int i = 0; i < numQueued; ++i) if (queue[(size_t) i].hit != -9) queue[(size_t) w++] = queue[(size_t) i];
    numQueued = w;

    if (running) lastPos = pos1 - 1.0e-9;
    else lastPos = -1.0e9;
    wasRunning = running;
    clock += n;
}

} // namespace dnafam::geno
