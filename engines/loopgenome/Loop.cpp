// semi-rack: vendored; namespaces wrapped in dnafam:: (see sources.mk for every change).
#include "Loop.h"

#include <algorithm>
#include <cmath>

namespace dnafam::lg
{

namespace
{
double resetLength (const geno::Params& p)
{
    return 16.0 * (p.resetBars > 0.0f ? (double) p.resetBars : (double) geno::formBars (p.form));
}
bool onReset (double pos16, double len)
{
    double r = std::fmod (pos16, len);
    if (r < 0.0) r += len;
    return r < 1.0e-6 || len - r < 1.0e-6;
}
} // namespace

BassMove bassFor (const Bundle& b, const Brain& brain, const Params& p, int64_t k, int step, int role, int prev, int& cur)
{
    BassMove m;
    const auto& J = b.joint;
    const int n = std::max (1, J.steps);
    const double mult = std::clamp ((double) p.mult, 0.25, 4.0);
    m.pos = (double) k * mult + ((k & 1) ? (double) std::clamp (p.swing, 0.0f, 1.0f) * 0.5 * mult : 0.0);
    m.step = step;
    m.role = role;
    if (step < 0 || step >= n) { m.cell.type = BRest; return m; }
    m.cell = J.bass.cells[(size_t) step];
    m.jump = prev >= 0 && step != (prev + 1) % n;
    const auto& sb = brain.bass[(size_t) step];
    if (sb.mute.load (std::memory_order_relaxed) != 0) m.cell.type = BRest;

    if (role == geno::RoleFill)
    {
        const double phrase = 16.0 * geno::formBars (p.form);
        const double here = (double) k * mult, next = (double) (k + 1) * mult;
        const bool last = (int64_t) std::floor ((next - 1.0e-9) / phrase) != (int64_t) std::floor ((here + 1.0e-9) / phrase) || p.ending == geno::EndOff;
        if (p.fillBass == FillDrop) m.cell.type = BRest;
        else if (p.fillBass == FillPickup && last)
        {
            m.cell.type = BHit;
            m.cell.semis = (int8_t) std::lround (p.pickupSemis);
            m.cell.vel = 92;
            m.cell.slide = 0;
            m.cell.bend = 0;
        }
    }
    // a held cell the ball jumped into keeps the note that sounds (it is not a new one)
    int a = sb.art.load (std::memory_order_relaxed);
    if (role == geno::RoleResponse && a > 0) a = (a - 1 + p.answerArts) % neuro::NumArts + 1;
    if (role == geno::RoleFill && p.fillArt > 0) a = p.fillArt;
    if (a > 0 && a - 1 != cur) { m.art = a - 1; cur = a - 1; }
    m.oct = std::clamp (sb.oct.load (std::memory_order_relaxed), -1, 1);
    return m;
}

OfflineWalk walkOffline (const Bundle& b, Brain& brain, const Params& p, int bars, uint32_t seed)
{
    OfflineWalk w;
    if (b.kit == nullptr || b.learned.steps <= 0) return w;
    walk::Walker wk;
    wk.seed (seed);
    const double mult = std::clamp ((double) p.mult, 0.25, 4.0);
    const double len = resetLength (p);
    const int64_t ticks = (int64_t) std::floor (16.0 * bars / mult + 1.0e-9);
    std::array<geno::Note, 64> tmp {};
    int prev = -1, cur = -1;
    for (int64_t k = 0; k < ticks; ++k)
    {
        int s = -1, r = 0;
        const bool rs = k == 0 || onReset ((double) k * mult, len);
        if (rs) prev = -1;
        const int c = geno::playTick (b, brain, p, wk, k, rs, tmp.data(), (int) tmp.size(), s, r);
        for (int i = 0; i < c; ++i) w.drums.push_back (tmp[(size_t) i]);
        w.bass.push_back (bassFor (b, brain, p, k, s, r, prev, cur));
        w.steps.push_back (s);
        prev = s;
    }
    std::sort (w.drums.begin(), w.drums.end(), [] (const geno::Note& a, const geno::Note& c) { return a.pos < c.pos; });
    return w;
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

bool Engine::popHit (geno::HitEvent& h) { return pop (hits, hitWrite, hitRead, h); }
bool Engine::popMove (geno::MoveEvent& m) { return pop (moves, moveWrite, moveRead, m); }
bool Engine::popBass (BassViewEvent& e) { return pop (bassEv, bassWrite, bassRead, e); }

void Engine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    bass.prepare (sr, maxBlock);
    bassL.assign ((size_t) std::max (512, maxBlock), 0.0f);
    bassR.assign ((size_t) std::max (512, maxBlock), 0.0f);
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
    prevStep = curArt = -1;
    bassNote = -1;
    bass.reset();
}

//==============================================================================
void Engine::trigger (const dna::Kit& k, const Params& p, int slice, int lane, float amp, int step, int role, float offMs)
{
    if (slice < 0 || slice >= k.numSlices()) return;
    const int fadeLen = std::max (16, (int) (sr * 0.0015));
    for (auto& v : voices)
    {
        if (! v.active || (v.fade >= 0 && v.fade <= fadeLen)) continue;
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
    v.env = 1.0f;
    v.decayMul = p.decay >= 0.999f ? 1.0f : (float) std::exp (-1.0 / (0.008 * std::pow (150.0, (double) p.decay) * sr));
    v.fade = -1;
    v.fadeLen = fadeLen;
    v.rampPos = 0;
    v.rampLen = std::max (4, (int) (sr * 0.0005));
    v.age = ++ageCounter;

    geno::HitEvent h;
    h.slice = (int16_t) slice;
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
        const float* a0 = k.L[0].data() + v.start;
        const float* b0 = k.R[0].data() + v.start;
        const double endPos = (double) v.len - 1.0;
        for (int i = from; i < to; ++i)
        {
            if (v.pos >= endPos) { v.active = false; break; }
            if (v.fade < 0 && v.pos + v.inc * (double) v.fadeLen >= endPos) v.fade = v.fadeLen;
            const int ip = (int) v.pos;
            const float fr = (float) (v.pos - ip);
            float g = v.amp * v.env * p.gain;
            if (v.rampPos < v.rampLen) g *= (float) (v.rampPos++) / (float) v.rampLen;
            if (v.fade >= 0)
            {
                g *= (float) v.fade / (float) v.fadeLen;
                if (--v.fade <= 0) { v.active = false; break; }
            }
            L[i] += g * (a0[ip] + fr * (a0[ip + 1] - a0[ip]));
            R[i] += g * (b0[ip] + fr * (b0[ip + 1] - b0[ip]));
            v.env *= v.decayMul;
            v.pos += v.inc;
        }
    }
}

//==============================================================================
void Engine::process (const Bundle* b, Brain& brain, const Params& p, const geno::MidiEvent* in, int numIn,
                      float* outL, float* outR, int n, geno::MidiEvent* midiOutBuf, int& numOut, uint8_t* outChannel)
{
    numOut = 0;
    std::fill (outL, outL + n, 0.0f);
    std::fill (outR, outR + n, 0.0f);
    for (int l = 0; l < geno::NumLanes; ++l) lastChoke[(size_t) l] = brain.lanes[(size_t) l].choke.load (std::memory_order_relaxed) != 0;

    const bool haveKit = b != nullptr && b->kit != nullptr && b->kit->numSlices() > 0 && b->learned.steps > 0 && b->joint.steps > 0;
    if (b != lastBundle)
    {
        for (auto& v : voices) v.active = false;
        int w = 0;
        for (int i = 0; i < numQueued; ++i) if (queue[(size_t) i].kind == KMidiOff) queue[(size_t) w++] = queue[(size_t) i];
        numQueued = w;
        lastBundle = b;
        nextTick = INT64_MIN;
        prevStep = curArt = -1;
    }
    if (p.seed != seedSeen) { walker.seed (p.seed * 2654435761u + 7u); seedSeen = p.seed; }

    const double bpm = std::clamp (p.bpm > 0.0 ? p.bpm : 174.0, 20.0, 400.0);
    viewBpm.store (bpm, std::memory_order_relaxed);
    const double sps = 60.0 / bpm * sr / 4.0;
    const double mult = std::clamp ((double) p.mult, 0.25, 4.0);
    const bool running = p.playing || p.run == geno::RunAlways;
    viewPlaying.store (running ? 1 : 0, std::memory_order_relaxed);

    auto out = [&] (int at, int note, int vel, int ch)
    {
        if (numOut < kMaxOut)
        {
            outChannel[numOut] = (uint8_t) ch;
            midiOutBuf[numOut++] = { std::clamp (at, 0, n - 1), (uint8_t) (note & 127), (uint8_t) std::clamp (vel, 0, 127) };
        }
    };
    auto bassOff = [&] (int at)
    {
        if (bassNote >= 0 && p.midiOut) out (at, bassNote, 0, p.bassChannel);
        bassNote = -1;
    };

    if (wasRunning && ! running)
    {
        for (auto& v : voices)
            if (v.active && (v.fade < 0 || v.fade > 256)) { v.fade = std::max (16, (int) (sr * 0.004)); v.fadeLen = v.fade; }
        int w = 0;
        for (int i = 0; i < numQueued; ++i) if (queue[(size_t) i].timed) queue[(size_t) w++] = queue[(size_t) i];
        numQueued = w;
        nextTick = INT64_MIN;
        viewStep.store (-1, std::memory_order_relaxed);
        bassOff (0);
        prevStep = curArt = -1;
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
    if (const int a = auditionRequest.exchange (-1); a >= 0) bass.auditionRequest.store (a);

    if (running && haveKit)
    {
        const bool jumped = nextTick == INT64_MIN || pos0 < lastPos - 1.0e-6 || pos0 > lastPos + 1.0 || std::abs (mult - lastMult) > 1.0e-9;
        if (jumped)
        {
            int w = 0;
            for (int i = 0; i < numQueued; ++i) if (queue[(size_t) i].timed) queue[(size_t) w++] = queue[(size_t) i];
            numQueued = w;
            nextTick = (int64_t) std::ceil (pos0 / mult - 1.0e-9);
            lastMult = mult;
            pendingReset = true;
        }
        const double len = resetLength (p);
        const double gap16 = std::clamp ((double) p.snareGapMs * 0.001 * bpm / 15.0, 0.0, 0.45 * mult);
        std::array<geno::Note, 64> tmp {};
        while ((double) nextTick * mult - 0.5 * mult < pos1)
        {
            const bool rs = pendingReset || onReset ((double) nextTick * mult, len);
            if (rs) { pendingReset = false; viewHalted.store (0); prevStep = -1; }
            int s = -1, r = 0;
            const int c = geno::playTick (*b, brain, p, walker, nextTick, rs, tmp.data(), (int) tmp.size(), s, r);
            Pending mv;
            mv.kind = KMove;
            mv.pos = (double) nextTick * mult;
            mv.step = s;
            mv.role = r;
            mv.lane = walker.lastFrom - 1;
            mv.vel = (uint8_t) (c > 0 ? 1 : 0);
            push (mv);
            if (p.drumsOn)
                for (int i = 0; i < c; ++i)
                {
                    Pending q;
                    q.kind = KHit;
                    q.pos = tmp[(size_t) i].pos;
                    q.hit = tmp[(size_t) i].hit;
                    q.lane = tmp[(size_t) i].lane;
                    q.step = tmp[(size_t) i].step;
                    q.role = tmp[(size_t) i].role;
                    q.amp = tmp[(size_t) i].amp;
                    push (q);
                    if (((1u << q.lane) & kSnareLanes) != 0 && gap16 > 0.0)
                    {
                        Pending g;
                        g.kind = KGap;
                        g.pos = q.pos - gap16;
                        push (g);
                    }
                }
            if (p.bassOn)
            {
                Pending bq;
                bq.kind = KBass;
                bq.bm = bassFor (*b, brain, p, nextTick, s, r, prevStep, curArt);
                bq.pos = bq.bm.pos;
                push (bq);
            }
            prevStep = s;
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
                q.kind = KPad;
                q.timed = true;
                q.at = clock + ev.offset;
                q.step = ev.note - 60;
                q.amp = (float) ev.vel / 127.0f;
                push (q);
            }
            continue;
        }
        out (ev.offset, ev.note, ev.vel, 1);
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
    std::sort (act.begin(), act.begin() + na, [] (const Action& a, const Action& c) { return a.at != c.at ? a.at < c.at : a.idx < c.idx; });

    int ne = 0;
    auto addExt = [&] (const neuro::ExtEvent& x) { if (ne < (int) ext.size()) ext[(size_t) ne++] = x; };

    int done = 0;
    for (int a = 0; a < na; ++a)
    {
        const int at = act[(size_t) a].at;
        if (haveKit) render (*b->kit, outL, outR, done, at, p);
        done = at;
        auto& qq = queue[(size_t) act[(size_t) a].idx];
        const Pending q = qq;
        qq.consumed = true;
        switch (q.kind)
        {
            case KMove:
            {
                viewStep.store (q.step, std::memory_order_relaxed);
                viewRole.store (q.role, std::memory_order_relaxed);
                geno::MoveEvent m;
                m.from = (int8_t) q.lane;
                m.to = (int8_t) q.step;
                m.role = (int8_t) q.role;
                m.fired = q.vel;
                emit (moves, moveWrite, moveRead, m);
                break;
            }
            case KMidiOff: out (at, q.note, 0, q.ch); break;
            case KGap:
            {
                neuro::ExtEvent x; x.at = at; x.kind = neuro::ExtGap; addExt (x);
                bassOff (at);
                break;
            }
            case KBass:
            {
                const auto& bm = q.bm;
                if (bm.art >= 0)
                {
                    neuro::ExtEvent x;
                    x.at = at; x.kind = neuro::ExtArt; x.art = (int8_t) bm.art; x.step = (int8_t) bm.step; x.oct = (int8_t) bm.oct;
                    x.vel = 100;
                    addExt (x);
                    viewArt.store (bm.art, std::memory_order_relaxed);
                }
                neuro::ExtEvent x;
                x.at = at;
                x.kind = neuro::ExtCell;
                x.type = (int8_t) (bm.cell.type == BHit ? neuro::CellHit : bm.cell.type == BTie ? neuro::CellTie : neuro::CellRest);
                x.semis = (int8_t) (bm.cell.semis + 12 * bm.oct);
                x.slide = bm.cell.slide;
                x.bend = bm.cell.bend;
                x.vel = bm.cell.vel;
                x.step = (int8_t) bm.step;
                if (bm.cell.type == BHit && p.bassP.gate < 0.999f && haveKit)
                {
                    int l = 1;
                    const auto& J = b->joint;
                    while (l < J.steps && J.bass.cells[(size_t) ((bm.step + l) % J.steps)].type == BTie) ++l;
                    x.gateSamples = (int) ((double) l * mult * sps * std::max (0.05f, p.bassP.gate));
                }
                addExt (x);
                if (bm.jump) viewJumps.fetch_add (1, std::memory_order_relaxed);
                BassViewEvent be;
                be.step = (int8_t) bm.step; be.art = (int8_t) bm.art; be.role = (int8_t) bm.role; be.type = bm.cell.type; be.jump = bm.jump ? 1 : 0;
                emit (bassEv, bassWrite, bassRead, be);
                // MIDI: the bass on its own channel
                if (bm.cell.type == BRest) bassOff (at);
                else if (bm.cell.type == BHit)
                {
                    bassOff (at);
                    if (p.midiOut)
                    {
                        bassNote = std::clamp (p.bassRoot + 12 * p.bassP.octave + bm.cell.semis + 12 * bm.oct, 0, 127);
                        out (at, bassNote, bm.cell.vel, p.bassChannel);
                    }
                }
                break;
            }
            case KPad:
                if (haveKit) trigger (*b->kit, p, q.step, geno::LFx, q.amp, -1, 0, 0.0f);
                break;
            case KHit:
            default:
            {
                if (! haveKit || q.hit < 0 || q.hit >= (int) b->kit->g.hits.size()) break;
                const auto& h = b->kit->g.hits[(size_t) q.hit];
                trigger (*b->kit, p, h.slice, q.lane, q.amp, q.step, q.role, (float) b->kit->g.ms (p.feel * h.offset));
                if (q.lane == geno::LKick && q.amp > 0.15f)
                {
                    neuro::ExtEvent x; x.at = at; x.kind = neuro::ExtKick; addExt (x);
                }
                if (p.midiOut)
                {
                    const int note = brain.lanes[(size_t) q.lane].note.load (std::memory_order_relaxed);
                    out (at, note, (int) std::lround (std::min (1.0f, q.amp) * 127.0f), 1);
                    Pending off;
                    off.kind = KMidiOff;
                    off.timed = true;
                    off.at = clock + at + (int64_t) std::max (1.0, 0.5 * mult * sps);
                    off.note = (uint8_t) note;
                    off.ch = 1;
                    push (off);
                }
                break;
            }
        }
    }
    if (haveKit) render (*b->kit, outL, outR, done, n, p);

    int w = 0;
    for (int i = 0; i < numQueued; ++i) if (! queue[(size_t) i].consumed) queue[(size_t) w++] = queue[(size_t) i];
    numQueued = w;

    // ---- the bass, driven by what the ball decided
    {
        neuro::Params bp = p.bassP;
        bp.external = true;
        bp.source = neuro::SrcSelf;
        bp.root = p.bassRoot;
        bp.playing = p.playing && haveKit && p.bassOn;
        bp.run = (p.run == geno::RunAlways && haveKit && p.bassOn) ? neuro::RunAlways : neuro::RunHost;
        bp.ppq = p.ppq;
        bp.bpm = bpm;
        bp.duck = p.duck;
        bp.duckSidechain = false;
        bp.seed = p.seed;
        const int m = std::min (n, (int) bassL.size());
        for (int start = 0; start < n; start += m)
        {
            const int len = std::min (m, n - start);
            // events of this chunk, offsets made relative
            std::array<neuro::ExtEvent, 256> local {};
            int nl = 0;
            for (int i = 0; i < ne; ++i)
                if (ext[(size_t) i].at >= start && ext[(size_t) i].at < start + len) { local[(size_t) nl] = ext[(size_t) i]; local[(size_t) nl++].at -= start; }
            bass.process (brain.dummyNeuro, bp, nullptr, 0, bassL.data(), bassR.data(), len, nullptr, local.data(), nl);
            for (int i = 0; i < len; ++i) { outL[start + i] += bassL[(size_t) i] * p.bassLevel; outR[start + i] += bassR[(size_t) i] * p.bassLevel; }
        }
    }
    // the sum of the break and the bass: a soft knee above -2 dB, never over 0 dBFS
    for (float* ch : { outL, outR })
        for (int i = 0; i < n; ++i)
        {
            const float a = std::abs (ch[i]);
            if (a > 0.8f) ch[i] = std::copysign (0.8f + 0.2f * std::tanh ((a - 0.8f) * 5.0f), ch[i]);
        }

    if (running) lastPos = pos1 - 1.0e-9;
    else lastPos = -1.0e9;
    wasRunning = running;
    clock += n;
}

} // namespace dnafam::lg
