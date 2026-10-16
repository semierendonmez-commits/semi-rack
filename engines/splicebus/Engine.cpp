#include "Engine.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace spb
{
using namespace dsp;

namespace
{
int64_t mod64 (int64_t a, int64_t m) { return ((a % m) + m) % m; }
int64_t floorDiv (int64_t a, int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

uint64_t mixKey (uint64_t h, float v) { uint32_t u; std::memcpy (&u, &v, 4); return (h ^ u) * 0x100000001B3ull; }
uint64_t mixKey (uint64_t h, int v) { return (h ^ (uint32_t) v) * 0x100000001B3ull; }

// what the plan of a line depends on: when any of it changes, the line is planned again from its start
uint64_t planHash (const Params& p)
{
    uint64_t h = 0xcbf29ce484222325ull;
    h = mixKey (h, p.chaos); h = mixKey (h, p.turns); h = mixKey (h, p.roll);
    h = mixKey (h, p.budget); h = mixKey (h, p.wall); h = mixKey (h, p.anchor);
    h = mixKey (h, p.loop); h = mixKey (h, (int) p.seed); h = mixKey (h, p.gene);
    return h;
}
} // namespace

Engine::Engine() = default;

void Engine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    const int block = std::max (kChunk, maxBlock);
    reel.prepare (sr);
    splitter.prepare (sr, kChunk);
    for (auto* b : { &lowL, &lowR, &upL, &upR, &dryL, &dryR, &guardBuf }) b->assign ((size_t) block, 0.0f);
    int size = 1;
    while (size < splitter.latencyFor (splitTrack) + kChunk + 8) size <<= 1;
    guardRing.assign ((size_t) size, 0.0f);
    guardMask = size - 1;
    fastA = 1.0f - std::exp (-1.0f / (0.0005f * (float) sr));
    fastR = 1.0f - std::exp (-1.0f / (0.015f * (float) sr));
    slowA = 1.0f - std::exp (-1.0f / (0.030f * (float) sr));
    slowR = 1.0f - std::exp (-1.0f / (0.120f * (float) sr));
    detHpA.set (180.0f, 0.7071f, (float) sr);
    detHpB.set (180.0f, 0.7071f, (float) sr);
    reset();
}

void Engine::reset()
{
    now = 0;
    lastTick = INT64_MIN;
    internalPpq = 0.0;
    wasRunning = false;
    lineIndex = INT64_MIN;
    reel.reset();
    splitter.reset();
    std::fill (guardRing.begin(), guardRing.end(), 0.0f);
    guardW = 0;
    fastEnv = slowEnv = scEnv = guardEnv = guardOut = 0.0f;
    detHpA.reset();
    detHpB.reset();
    refractory = holdLeft = 0;
}

void Engine::setParams (const Params& p)
{
    params = p;
    splitter.setMode (p.split);
    splitter.setFixed (clamp (p.splitHz, 30.0f, 1000.0f));
    splitter.setTrack (clamp (p.ratio, 0.5f, 4.0f), clamp (p.f0Max, 60.0f, 500.0f));
    outGain = dbToGain (clamp (p.outDb, -24.0f, 12.0f));
}

//==============================================================================
// The form
float Engine::rnd (const Params& p, int64_t k, uint32_t purpose, int salt)
{
    const int lb = loopBars (p.loop);
    const int64_t key = lb > 0 ? mod64 (k, (int64_t) lb * 16) : k;
    return hash01 (p.seed, (uint32_t) ((uint64_t) key & 0xffffffffu), (uint32_t) ((uint64_t) key >> 32) ^ (purpose * 7919u), (uint32_t) salt);
}

// TURNAROUND's hierarchy, as FLOATER has it: the bigger the phrase a 4-bar line closes, the earlier and
// the harder the turn before it (window 4 / 8 / 16 / 16 sixteenths before 4 / 8 / 16 / 32 bars)
float Engine::turnIntensity (const Params& p, int64_t k, int* tierOut, int* toLineOut)
{
    const int64_t bar = floorDiv (k, 16);
    const int64_t next = floorDiv (bar, 4) * 4 + 4;
    const int tier = mod64 (next, 32) == 0 ? 3 : (mod64 (next, 16) == 0 ? 2 : (mod64 (next, 8) == 0 ? 1 : 0));
    const int toLine = (int) (next * 16 - k);                                         // 1..64
    if (tierOut != nullptr) *tierOut = tier;
    if (toLineOut != nullptr) *toLineOut = toLine;
    static const int window[4] { 4, 8, 16, 16 };
    static const float weight[4] { 0.35f, 0.6f, 0.85f, 1.0f };
    if (p.turns <= 0.0f || toLine > window[tier]) return 0.0f;
    return clamp (p.turns, 0.0f, 1.0f) * weight[tier] * (1.0f - 0.6f * (float) (toLine - 1) / (float) window[tier]);
}

bool Engine::isSlot (const Params& p, int64_t k, int half)
{
    const double gs = geneSixteenths (p.gene);
    if (gs < 1.0) return true;
    return half == 0 && mod64 (k, (int64_t) gs) == 0;
}

void Engine::planLine (const Params& p, int64_t lineNo, Line& out)
{
    out = Line {};
    const int N = budgetCount (p.budget);
    const int reserve = N > 0 ? (int) std::lround ((float) N * clamp (p.turns, 0.0f, 1.0f)) : 0;
    const int ab = anchorBars (p.anchor);
    const float chaos = clamp (p.chaos, 0.0f, 1.0f);
    out.budget = N;
    for (int s = 0; s < 64; ++s)
        for (int half = 0; half < 2; ++half)
        {
            const int64_t k = lineNo * 64 + s;
            if (! isSlot (p, k, half)) continue;
            auto& d = out.slot[(size_t) (s * 2 + half)];
            int tier = 0, toLine = 64;
            const float ti = turnIntensity (p, k, &tier, &toLine);
            if (p.wall > 0 && tier >= 2 && toLine <= 4)
            {
                d.wall = true;
                d.stop = p.wall == 2 && toLine == 4 && half == 0;
                continue;
            }
            if (ab > 0 && mod64 (k, (int64_t) ab * 16) < 4) { d.anchored = true; continue; }
            d.turn = ti > 0.0f;
            const float pb = chaos + (1.0f - chaos) * ti;
            bool cand = pb > 0.0f && rnd (p, k, 40, half) < pb;
            if (cand && N > 0 && out.used >= (d.turn ? N : N - reserve)) cand = false;     // over the budget
            if (! cand) continue;
            ++out.used;
            d.broken = true;
            d.roll = d.turn && rnd (p, k, 50, half) < clamp (p.roll, 0.0f, 1.0f) * std::min (1.0f, ti * 1.6f);
        }
}

//==============================================================================
void Engine::tick (int64_t k, int64_t at, double tickLen)
{
    const auto& P = params;
    if (mod64 (k, 16) == 0) reel.barLine (at);

    const int64_t ln = floorDiv (k, 64);
    const uint64_t key = planHash (P);
    if (ln != lineIndex || key != planKey) { planLine (P, ln, line); lineIndex = ln; planKey = key; }

    int tier = 0, toLine = 64;
    turnIntensity (P, k, &tier, &toLine);
    if (mod64 (k, 64) == 0) reel.wall (at, false);
    if (P.wall == 1 && tier >= 2 && toLine == 4) reel.wall (at, true);

    const double gs = geneSixteenths (P.gene);
    if (gs < 1.0)
    {
        geneAt (k, at, tickLen, 0);
        geneAt (k, at + (int64_t) (tickLen * 0.5), tickLen, 1);
    }
    else if (mod64 (k, (int64_t) gs) == 0)
        geneAt (k, at, tickLen, 0);

    // what was spent of this line's budget, up to here
    int spent = 0;
    const int s16 = (int) mod64 (k, 64);
    for (int s = 0; s <= s16 * 2 + 1; ++s) spent += line.slot[(size_t) s].broken ? 1 : 0;
    display.spent.store (spent, std::memory_order_relaxed);
    display.turnTier.store (tier, std::memory_order_relaxed);
    display.turnIn.store (toLine, std::memory_order_relaxed);
    display.sixteenth.store (k, std::memory_order_relaxed);
}

// One gene: what the plan says, and if it breaks, what it plays. Every choice is the position's seeded random.
void Engine::geneAt (int64_t k, int64_t at, double tickLen, int half)
{
    const auto& P = params;
    const auto& d = line.slot[(size_t) (mod64 (k, 64) * 2 + half)];
    Reel::Gene g;
    g.length = (int) (geneSixteenths (P.gene) * tickLen);
    if (d.wall)
    {
        if (d.stop)
        {
            // the last beat before a 16 / 32-bar line: the tape under the head runs down to a stop
            g.broken = true;
            g.live = true;
            g.length = (int) (4.0 * tickLen);
            g.rate = 1.0f;
            g.rateEnd = 0.0f;
            reel.gene (at, g);
        }
        return;                     // inside a wall the genes keep out of the way
    }
    if (! d.broken) { reel.gene (at, g); return; }

    int tier = 0, toLine = 64;
    const float ti = turnIntensity (P, k, &tier, &toLine);
    const float c = d.turn ? std::max (clamp (P.chaos, 0.0f, 1.0f), ti) : clamp (P.chaos, 0.0f, 1.0f);
    g.broken = true;
    if (d.roll)
    {
        // a roll of what just went by, tighter towards the line: 2, then 4, then 8 repeats a gene
        g.recent = true;
        g.sub = toLine >= 3 ? 2 : (toLine == 2 ? 4 : 8);
        g.length = (int) std::min ((double) g.length, (double) toLine * tickLen - (half != 0 ? 0.5 * tickLen : 0.0));
        g.length = std::max (g.length, (int) (0.5 * tickLen));
        g.rate = g.rateEnd = (rnd (P, k, 51, half) < 0.25f * c ? 2.0f : 1.0f);
        reel.gene (at, g);
        return;
    }
    const int n = std::max (1, reel.display.splices.load (std::memory_order_relaxed));
    int sp = (int) (clamp (P.organize, 0.0f, 0.999f) * (float) n);
    if (rnd (P, k, 41, half) < 0.5f * c) sp += 1 + (int) (rnd (P, k, 42, half) * (float) (n - 1));
    g.splice = (int) mod64 (sp, n);
    const double pos16 = (double) mod64 (k, 16) + (half != 0 ? 0.5 : 0.0);
    const double off = pos16 / 16.0 + (double) P.slide;
    g.offset = (float) (off - std::floor (off));
    float rate = varispeedRate (P.varispeed);
    if (rnd (P, k, 43, half) < 0.35f * c * c) rate = -rate;
    if (rnd (P, k, 44, half) < 0.25f * c * c * c) rate *= rnd (P, k, 45, half) < 0.5f ? 2.0f : 0.5f;
    if (P.morph > 0.6f && rnd (P, k, 46, half) < (P.morph - 0.6f) / 0.4f)
    {
        static const float ratio[4] { 2.0f, 0.5f, 1.5f, 0.75f };
        rate *= ratio[std::min (3, (int) (rnd (P, k, 47, half) * 4.0f))];
    }
    g.rate = g.rateEnd = rate;
    g.sub = rnd (P, k, 48, half) < 0.35f * c * c ? 2 + std::min (2, (int) (rnd (P, k, 49, half) * 3.0f)) : 1;
    reel.gene (at, g);
}

//==============================================================================
// PROTECT: hits (the input's, or the sidechain's) come through as played. Detected on the input as it
// arrives, applied when it reaches the reel: the latency is a look-ahead, so the guard is already up
// when the attack gets there.
void Engine::detect (const float* inL, const float* inR, const float* scL, const float* scR, int n)
{
    const auto& P = params;
    const int L = splitter.latency();
    const int preroll = std::max (1, std::min (L, (int) (0.002 * sr)));
    const int delay = L - preroll;
    const float up = 1.0f / (float) preroll;
    const float rel = decayCoef (clamp (P.protectRel, 0.005f, 1.0f), (float) sr);
    const float thr = 1.6f + 2.4f * (1.0f - clamp (P.sens, 0.0f, 1.0f));
    const float amount = P.protectBy == 0 ? 0.0f : clamp (P.protect, 0.0f, 1.0f);
    const int hold = (int) (0.02 * sr), refr = (int) (0.04 * sr);
    int hits = 0;
    for (int i = 0; i < n; ++i)
    {
        float x = 0.0f;
        // the input's hits are found above the bass (a click, a crack, a hat), the sidechain's as they are
        if (P.protectBy == 1) x = std::abs (detHpB.hp (detHpA.hp (0.5f * (inL[i] + inR[i]))));
        else if (P.protectBy >= 2 && scL != nullptr) x = 0.5f * std::abs (scL[i] + (scR != nullptr ? scR[i] : scL[i]));
        fastEnv += (x - fastEnv) * (x > fastEnv ? fastA : fastR);
        slowEnv += (x - slowEnv) * (x > slowEnv ? slowA : slowR);
        if (P.protectBy == 3)
        {
            // the key's level, -40 .. -6 dBFS -> 0 .. 1
            const float db = 20.0f * std::log10 (std::max (1.0e-6f, fastEnv));
            const float target = clamp ((db + 40.0f) / 34.0f, 0.0f, 1.0f);
            guardEnv = target > guardEnv ? target : target + rel * (guardEnv - target);
        }
        else
        {
            if (refractory > 0) --refractory;
            else if (P.protectBy != 0 && fastEnv > slowEnv * thr && fastEnv > 0.002f)
            {
                holdLeft = hold;
                refractory = refr;
                ++hits;
            }
            if (holdLeft > 0) { --holdLeft; guardEnv = 1.0f; }
            else guardEnv *= rel;
        }
        guardRing[(size_t) (guardW & guardMask)] = guardEnv * amount;
        const int64_t ri = guardW - delay;
        ++guardW;
        const float target = ri >= 0 ? guardRing[(size_t) (ri & guardMask)] : 0.0f;
        guardOut = target > guardOut ? std::min (target, guardOut + up) : target;
        guardBuf[(size_t) i] = guardOut;
    }
    if (hits > 0) display.hits.fetch_add (hits, std::memory_order_relaxed);
    display.guard.store (guardOut, std::memory_order_relaxed);
}

//==============================================================================
void Engine::process (const float* inL, const float* inR, const float* scL, const float* scR,
                      float* outL, float* outR, int n, const Transport& t)
{
    const double bpm0 = (t.hostHasPosition && t.bpm > 1.0) ? t.bpm : (double) params.bpm;
    const double bpm = std::min (400.0, std::max (20.0, bpm0));
    for (int off = 0; off < n; off += kChunk)
    {
        const int m = std::min (kChunk, n - off);
        Transport tc = t;
        if (t.playing && t.hostHasPosition) tc.ppq = t.ppq + (double) off * bpm / 60.0 / sr;
        processChunk (inL + off, inR + off, scL != nullptr ? scL + off : nullptr, scR != nullptr ? scR + off : nullptr,
                      outL + off, outR + off, m, tc);
    }
}

void Engine::processChunk (const float* inL, const float* inR, const float* scL, const float* scR,
                           float* outL, float* outR, int n, const Transport& t)
{
    const auto& P = params;
    const int L = splitter.latency();

    // ---- the clock: the host's, or our own when the host is stopped and FREE is on
    double bpm = (t.hostHasPosition && t.bpm > 1.0) ? t.bpm : (double) P.bpm;
    double ppq0 = 0.0;
    bool running = false;
    if (t.playing && t.hostHasPosition) { ppq0 = t.ppq; running = true; internalPpq = t.ppq; }
    else if (P.run)
    {
        if (! t.hostHasPosition) bpm = P.bpm;
        ppq0 = internalPpq;
        running = true;
    }
    bpm = std::min (400.0, std::max (20.0, bpm));
    const double ppqPerSample = bpm / 60.0 / sr;
    if (running && ! t.playing) internalPpq += ppqPerSample * n;
    display.running.store (running, std::memory_order_relaxed);
    display.host.store (t.playing && t.hostHasPosition, std::memory_order_relaxed);
    display.bpm.store ((float) bpm, std::memory_order_relaxed);
    if (running) display.ppq.store (ppq0, std::memory_order_relaxed);

    if (running)
    {
        // a 16th lands on the first sample at or after it on the song's timeline, wherever the blocks cut;
        // it reaches the reel L samples later, with the audio it belongs to
        const int64_t c0 = (int64_t) std::llround (ppq0 / ppqPerSample);
        const double tickLen = sr * 60.0 / bpm / 4.0;
        for (int64_t k = (int64_t) std::floor (ppq0 * 4.0) - 1; ; ++k)
        {
            const double tp = (double) k * 0.25;
            const int64_t ts = (int64_t) std::ceil (tp / ppqPerSample - 1.0e-6);
            if (ts >= c0 + n) break;
            if (ts < c0 - 1 || k == lastTick) continue;
            lastTick = k;
            tick (k, now + std::max<int64_t> (0, ts - c0) + L, tickLen);
        }
    }
    else if (wasRunning)
    {
        // stopped: the reel lets go, the wall opens, the next start plans afresh
        Reel::Gene g;
        reel.gene (now + L, g);
        reel.wall (now + L, false);
        lastTick = INT64_MIN;
    }
    wasRunning = running;

    // ---- split, guard, reel
    splitter.process (inL, inR, lowL.data(), lowR.data(), upL.data(), upR.data(), n);
    for (int i = 0; i < n; ++i) { dryL[(size_t) i] = lowL[(size_t) i] + upL[(size_t) i]; dryR[(size_t) i] = lowR[(size_t) i] + upR[(size_t) i]; }
    detect (inL, inR, scL, scR, n);

    Reel::Settings rs;
    rs.length = P.length;
    rs.morph = P.morph;
    rs.hold = P.hold;
    rs.wetHp = P.split == splitOff ? 0.0f : std::max (20.0f, splitter.splitHz());
    reel.set (rs);
    // the scope sees both bands before the reel touches the upper one
    {
        int w = display.scopeWrite.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            display.scopeLow[(size_t) w] = 0.5f * (lowL[(size_t) i] + lowR[(size_t) i]);
            display.scopeUp[(size_t) w] = 0.5f * (upL[(size_t) i] + upR[(size_t) i]);
            w = (w + 1) % Display::kScope;
        }
        display.scopeWrite.store (w, std::memory_order_release);
    }
    reel.process (upL.data(), upR.data(), guardBuf.data(), n, now);

    const float mix = clamp (P.mix, 0.0f, 1.0f);
    float lo = 0.0f, hi = 0.0f, pk = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float wl = lowL[(size_t) i] + upL[(size_t) i], wr = lowR[(size_t) i] + upR[(size_t) i];
        outL[i] = (dryL[(size_t) i] + mix * (wl - dryL[(size_t) i])) * outGain;
        outR[i] = (dryR[(size_t) i] + mix * (wr - dryR[(size_t) i])) * outGain;
        lo = std::max (lo, std::abs (lowL[(size_t) i]));
        hi = std::max (hi, std::abs (upL[(size_t) i]));
        pk = std::max (pk, std::max (std::abs (outL[i]), std::abs (outR[i])));
    }
    auto decay = [] (std::atomic<float>& a, float v) { a.store (std::max (v, a.load (std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed); };
    decay (display.lowLevel, lo);
    decay (display.upLevel, hi);
    decay (display.outLevel, pk);
    display.splitHz.store (splitter.splitHz(), std::memory_order_relaxed);
    display.f0.store (splitter.f0(), std::memory_order_relaxed);
    display.conf.store (splitter.confidence(), std::memory_order_relaxed);
    now += n;
}

} // namespace spb
