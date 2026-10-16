#include "Engine.h"
#include "busdriver/dsp/drumbuss.h"
#include <climits>

namespace flt
{
using namespace dsp;


namespace
{
int64_t mod64 (int64_t a, int64_t m) { return ((a % m) + m) % m; }
int64_t floorDiv (int64_t a, int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
const int everyTicks[5] { 16, 8, 4, 2, 1 };
int ticksForRate (float rate) { return everyTicks[std::min (4, std::max (0, (int) (rate * 4.999f)))]; }
bool isFillLane (int i) { return i == mesh || i == fm || i == opto || i == lantern || i == zap; }
}

struct Engine::DrumBus
{
    drumbuss::DrumBuss d;
    std::vector<float> io;
    bool comp = true; int character = 1; float drive = -1, crunch = -1, trans = -9, boom = -1;
};

Engine::~Engine() = default;

Engine::Engine()
{
    for (auto& p : plaits) p = std::make_unique<PlaitsVoice>();
    for (auto& b : braids) b = std::make_unique<BraidsVoice>();
    for (auto& r : reelVoices) r = std::make_unique<ReelVoice> (reel);
    clouds = std::make_unique<CloudsScatter>();
    parasites = std::make_unique<ParasitesScatter>();
    beads = std::make_unique<BeadsScatter>();
    seed = std::make_unique<CloudSeedSpace>();
    bus = std::make_unique<DrumBus>();
    voices[kick]    = std::make_unique<Kick>();
    voices[mesh]    = std::make_unique<Mesh>();
    voices[fm]      = std::make_unique<Fm>();
    voices[opto]    = std::make_unique<Opto>();
    voices[lantern] = std::make_unique<Lantern>();
    voices[zap]     = std::make_unique<Zap>();
    auto s = std::make_unique<StrikeVoice>();
    strikeVoice = s.get();
    voices[strike]  = std::move (s);
}

void Engine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    for (auto& v : voices) v->prepare ((float) sr);
    zig.prepare ((float) sr);
    scat.prepare ((float) sr);
    space.prepare ((float) sr);
    for (auto& p : plaits) p->prepare ((float) sr);
    for (auto& b : braids) b->prepare ((float) sr);
    for (auto& r : reelVoices) r->prepare ((float) sr);
    reel.prepare (sr, kChunk);
    drone.prepare (sr);
    res.prepare (sr);
    smr.prepare ((float) sr);
    clouds->prepare (sr);
    parasites->prepare (sr);
    beads->prepare (sr);
    seed->prepare (sr);
    bus->d.setSampleRate ((float) sr);
    bus->d.reset();
    bus->drive = -1.0f;   // forces the first apply
    bus->io.assign ((size_t) kChunk * 2, 0.0f);
    for (auto& b : vraw) b.assign ((size_t) kChunk, 0.0f);
    for (auto* b : { &vbuf, &xbuf, &fbTap, &dryL, &dryR, &pdryL, &pdryR, &sendL, &sendR, &zl, &zr, &gl, &gr, &sl, &sr2, &inL, &inR, &ml, &mr,
                     &droneL, &droneR, &droneMono, &resIn })
        b->assign ((size_t) kChunk, 0.0f);
    // the feedback loop's memory: half a bar at 20 BPM would be 3 s; four is plenty
    histCap = (int64_t) (4.0 * sr);
    histL.assign ((size_t) histCap, 0.0f);
    histR.assign ((size_t) histCap, 0.0f);
    smrRing.assign ((size_t) kSmrRing, 0.0f);
    fbHpL.set (60.0f, (float) sr); fbHpR.set (60.0f, (float) sr);
    fbLpL.set (9000.0f, (float) sr); fbLpR.set (9000.0f, (float) sr);
    xAtk = 1.0f - decayCoef (0.002f, (float) sr);
    xRel = decayCoef (0.08f, (float) sr);
    (void) maxBlock;      // everything runs in chunks of kChunk
    reset();
}

void Engine::reset()
{
    now = 0;
    internalPpq = 0.0;
    freeCounter = 0;
    currentKey = 0;
    queued = 0;
    lastTick = LLONG_MIN / 2;
    controlLeft = 0;
    smoothTarget = 0; smoothTime = 0; pongH = 1; pongV = 0; lfoPh = 0; forgeEnv = 0; forgePh = 0; chaosX = 0.4f;
    sources.fill (0.0f);
    dests.fill (0.0f);
    progSize = progDecay = progDamp = 0.0f;
    progDiff = 0.7f; progMod = 0.3f; progRate = 0.3f; progCross = 0.5f; progPre = 0.05f;
    freezeUntil = 0;
    numFired = 0;
    smrRot = 0; traceTick = 0; woggleV = 0.0f;
    flips.fill (0u);
    lastCond.fill (false);
    lastFlipReset = params.flipReset;
    seqXPos = seqYPos = 0; seqTarget = 0.0f; organizeStep = 0;
    panOff.fill (0.0f);
    for (auto& x : xmods) { x.line.fill (0.0f); x.w = 0; x.env = 0.0f; }
    std::fill (histL.begin(), histL.end(), 0.0f);
    std::fill (histR.begin(), histR.end(), 0.0f);
    std::fill (smrRing.begin(), smrRing.end(), 0.0f);
    fbLevel = 0.0f;
    smr.reset();
    zig.reset();
    scat.reset();
    space.reset();
    reel.reset();
    drone.reset();
    res.reset();
    arcPhase = arcRate = 0.0;
    susOn.fill (false);
    susModel.fill (-1);
    seedIn = 1.0f;
}

float Engine::rnd (uint32_t purpose, int lane)
{
    return hash01 ((uint32_t) params.seed, (uint32_t) ((uint64_t) currentKey & 0xffffffffu),
                   (uint32_t) ((uint64_t) currentKey >> 32) ^ (purpose * 7919u), (uint32_t) lane);
}

void Engine::hit (int lane, float velocity, int offset)
{
    if (lane < 0 || lane >= kLanes) return;
    schedule ({ now + std::max (0, offset), lane, clamp (velocity, 0.0f, 1.0f), false, -1, 0 });
}

void Engine::schedule (const Event& e)
{
    if (queued < (int) queue.size()) queue[(size_t) queued++] = e;
}

//==============================================================================
// One 16th of the master clock. k is the 16th's index on the timeline.
void Engine::tick (int64_t k, int64_t at, double bpm)
{
    const auto& P = params;
    currentKey = P.loopBars > 0 ? mod64 (k, (int64_t) P.loopBars * 16) : (int64_t) freeCounter;
    ++freeCounter;
    const double tickLen = sr * 60.0 / bpm / 4.0;

    // the loop starts again: the mutations, the conditions' memory and the SEQ go back to where they
    // were, so with LOOP every random decision repeats. (The steps, the A:B passes, ANCHOR and the turns
    // follow the song: a lane whose length does not divide the loop keeps drifting against it.)
    if (P.loopBars > 0 && currentKey == 0)
    {
        flips.fill (0u);
        lastCond.fill (false);
        seqXPos = seqYPos = 0;
        organizeStep = 0;
    }
    if (P.flipReset != lastFlipReset) { flips.fill (0u); lastFlipReset = P.flipReset; }

    // ---- the CV sources that move on the clock
    if (mod64 (k, ticksForRate (P.rate[stepped])) == 0) sources[stepped] = rnd (11) * 2.0f - 1.0f;
    if (mod64 (k, ticksForRate (P.rate[chaos])) == 0)
    {
        const float r = 3.58f + 0.41f * P.rate[chaos];
        chaosX = r * chaosX * (1.0f - chaosX);
        if (chaosX < 0.001f || chaosX > 0.999f) chaosX = 0.1f + 0.8f * rnd (12);
        sources[chaos] = chaosX * 2.0f - 1.0f;
    }
    const int smoothEvery = ticksForRate (P.rate[smooth]) * 2;
    if (mod64 (k, smoothEvery) == 0) { smoothTarget = rnd (13) * 2.0f - 1.0f; smoothTime = (float) (tickLen * smoothEvery * 0.5 / sr); }
    if (P.seqX == 0 && mod64 (k, ticksForRate (P.rate[seq])) == 0) seqStep (true, false);
    updateDests();

    // ---- the rotating divider: lane i takes the division set on lane (i + rotation)
    const int rot = (int) mod64 (P.rotate + (int) std::lround (dests[rotate] * 4.0f), kLanes);
    display.rotation.store (rot, std::memory_order_relaxed);

    // ---- phrase turns (TURNAROUND's hierarchy): the bigger the phrase a bar line closes,
    // the earlier and harder the lanes fill before it; WALL drops out the beat before 16 and 32
    {
        const int64_t bar = floorDiv (k, 16);
        const int64_t line = floorDiv (bar, 4) * 4 + 4;                               // the next 4-bar line
        const int tier = mod64 (line, 32) == 0 ? 3 : (mod64 (line, 16) == 0 ? 2 : (mod64 (line, 8) == 0 ? 1 : 0));
        const int64_t sixteenths = line * 16 - k;                                     // 1..64
        static const int window[4] { 4, 8, 16, 16 };                                 // in 16ths
        static const float weight[4] { 0.35f, 0.6f, 0.85f, 1.0f };
        float intensity = 0.0f;
        if (P.turns > 0.0f && sixteenths <= window[tier])
            intensity = P.turns * weight[tier] * (1.0f - 0.6f * (float) (sixteenths - 1) / (float) window[tier]);
        turnChance = intensity;
        turnBurst = intensity * 0.8f;
        turnFill = intensity * 0.5f;
        wallNow = P.wall && tier >= 2 && sixteenths <= 4;
        display.turnTier.store (tier, std::memory_order_relaxed);
        display.turnIn.store ((int) sixteenths, std::memory_order_relaxed);
        display.turning.store (intensity > 0.0f, std::memory_order_relaxed);
        display.walled.store (wallNow, std::memory_order_relaxed);
    }

    tickReel (k, at, tickLen);

    // ---- ARC: a phrase-long rise and fall, set right on every 16th, run smoothly between them
    {
        static const int bars[4] { 4, 8, 16, 32 };
        const int64_t len = (int64_t) bars[std::max (0, std::min (3, P.arcLen))] * 16;
        arcPhase = (double) mod64 (k, len) / (double) len;
        arcRate = 1.0 / ((double) len * tickLen / sr);
    }

    // ---- pass 1: each lane's gate on this 16th (pattern, LOCK, fills, conditions)
    std::array<bool, kLanes> gate {};
    std::array<int, kLanes> divOf {};
    for (int i = 0; i < kLanes; ++i)
    {
        const auto& L = P.lanes[(size_t) i];
        const int div = std::max (1, std::min (8, P.lanes[(size_t) ((i + rot) % kLanes)].div));
        divOf[(size_t) i] = div;
        display.div[(size_t) i].store (div, std::memory_order_relaxed);
        if (mod64 (k, div) != 0) continue;
        const int steps = std::max (1, std::min (kMaxSteps, L.steps));
        const int s = (int) mod64 (floorDiv (k, div), steps);
        display.step[(size_t) i].store (s, std::memory_order_relaxed);
        gate[(size_t) i] = laneGate (i, k, s, steps, div);
    }

    // ---- EBB: the references come in waves (dense stretches, then seconds of silence: their gaps are
    // longer and more clustered than chance). One seeded level a bar, eased from bar to bar; under 35% of
    // the way up the lanes fall silent (about a third of the time), at the top they play at up to 3.4 times
    // their chance: 3.4 makes the wave's mean 1, so EBB moves the hits about without thinning them
    float ebbGain = 1.0f;
    if (P.ebb > 0.0f)
    {
        const int64_t bar = floorDiv (currentKey, 16);
        auto level = [&] (int64_t b) { if (P.loopBars > 0) b = mod64 (b, P.loopBars); return hash01 ((uint32_t) P.seed, (uint32_t) ((uint64_t) b & 0xffffffffu), 77u * 7919u, 99u); };
        const float x = (float) mod64 (currentKey, 16) / 16.0f;
        const float a = level (bar) + (level (bar + 1) - level (bar)) * x * x * (3.0f - 2.0f * x);
        const float u = clamp ((a - 0.35f) / 0.65f, 0.0f, 1.0f);
        ebbGain = (1.0f - P.ebb) + P.ebb * 3.4f * u * u * (3.0f - 2.0f * u);
    }

    // ---- pass 2: logic between lanes, then the probability gate and the bursts
    for (int i = 0; i < kLanes; ++i)
    {
        const auto& L = P.lanes[(size_t) i];
        if (mod64 (k, divOf[(size_t) i]) != 0) continue;              // a lane only plays on its own division
        bool g = gate[(size_t) i];
        if (L.logic > 0)
        {
            const int op = (L.logic - 1) / 8, other = (L.logic - 1) % 8;
            if (other != i) g = logicApply (op, g, gate[(size_t) other]);
        }
        if (! L.on || wallNow || ! g) continue;
        const int div = divOf[(size_t) i];
        const int s = display.step[(size_t) i].load (std::memory_order_relaxed);
        const float chance = (L.chance + (1.0f - L.chance) * turnChance) * ebbGain;
        if (rnd (1, i) >= chance) continue;                           // the probability gate

        float vel = 0.68f + 0.27f * rnd (3, i);
        if (mod64 (k, 16) == 0) vel = 1.0f;                            // the one
        // VARY plays a hit a little late now and then (never early: it stays behind the grid)
        int64_t late = (int64_t) (rnd (55, i) * L.vary * 0.004 * sr);
        // FREE: off the grid. The reference recordings have no grid at all (a hit is as far from the nearest 16th as
        // chance would put it): each hit lands anywhere in its step, late, by the clock's seeded random
        if (P.loose > 0.0f) late += (int64_t) (rnd (57, i) * P.loose * tickLen * (double) div * 0.95);
        const float burst = L.burst + (1.0f - L.burst) * turnBurst * (i == kick ? 0.3f : 1.0f);
        if (rnd (2, i) < burst)                                        // a burst instead of one hit
        {
            const int n = 2 + (int) (rnd (4, i) * (float) (std::max (2, P.burstMax) - 1) + 0.5f);
            // on the grid a burst fills its step; FREE, its hits come about 100 ms apart (the references' doubles)
            const double span = (1.0 - P.loose) * tickLen * (double) div * 0.96 + (double) P.loose * (double) (n - 1) * 0.1 * sr;
            // interval weights: bouncing ball (each shorter), even, slowing
            double w[9], sum = 0.0;
            for (int j = 0; j < n - 1; ++j)
            {
                w[j] = P.burstShape == 0 ? std::pow (0.62, j) : (P.burstShape == 1 ? 1.0 : std::pow (1.5, j));
                sum += w[j];
            }
            double t = 0.0;
            for (int j = 0; j < n; ++j)
            {
                const float v = vel * (P.burstShape == 2 ? 0.55f + 0.45f * (float) j / (float) (n - 1) : std::pow (0.86f, (float) j));
                schedule ({ at + late + (int64_t) t, i, v, true, s, j });
                if (j < n - 1) t += span * w[j] / sum;
            }
            display.bursts[(size_t) i].fetch_add (1, std::memory_order_relaxed);
            if (i == scatter) freezeUntil = std::max (freezeUntil, at + (int64_t) span);   // a burst on lane 8 holds FREEZE
            if (rnd (5, i) < P.dlyBreak)                               // the burst also breaks the delay's clock
                zig.breakNow ((int) (tickLen / (rnd (6, i) < 0.5f ? 4.0 : 2.0)), (int) (tickLen * (1 + (int) (rnd (7, i) * 3.0f))));
        }
        else
            schedule ({ at + late, i, vel, false, s, 0 });
    }

    if (rnd (8) < P.dlyBreak * 0.05f)
        zig.breakNow ((int) (tickLen / 8.0), (int) (tickLen * 2.0));

    // ---- the reverb's program change on the clock
    if (P.programChange >= 1 && P.programChange <= 3 && mod64 (k, 16) == 0)
    {
        const int bars = P.programChange == 1 ? 1 : (P.programChange == 2 ? 2 : 4);
        if (mod64 (k / 16, bars) == 0) newProgram();
    }
}

// Does lane i fire on step s of this 16th? The pattern as LOCK has left it, the turn's fills, the condition.
bool Engine::laneGate (int i, int64_t k, int s, int steps, int div)
{
    const auto& L = params.lanes[(size_t) i];
    bool hit = laneHits (L, steps, s) != (((flips[(size_t) i] >> s) & 1u) != 0);
    // LOCK: as the step goes by it may flip for the next pass (the Turing Machine's register)
    if (L.lock < 1.0f && rnd (60, i) < std::pow (1.0f - L.lock, 1.5f) * 0.5f)
        flips[(size_t) i] ^= 1u << s;
    uint32_t live = 0;
    for (int st = 0; st < steps; ++st)
        if (laneHits (L, steps, st) != (((flips[(size_t) i] >> st) & 1u) != 0)) live |= 1u << st;
    display.live[(size_t) i].store (live, std::memory_order_relaxed);

    // a turn adds hits to the lanes that fill (not the kick, the strings or the scatter)
    if (! hit && turnFill > 0.0f && isFillLane (i) && rnd (9, i) < turnFill) hit = true;
    const int c = L.cond[(size_t) s];
    if (! hit || c == 0) return hit;
    int a = 0, b = 1;
    bool pass;
    if (condRatio (c, a, b))
    {
        const int64_t round = floorDiv (floorDiv (k, div), steps);   // how many times the lane has gone round
        pass = mod64 (round, b) == a - 1;
        lastCond[(size_t) i] = pass;
    }
    else if (c == kCondFill || c == kCondNotFill)
    {
        pass = (turnChance > 0.0f) == (c == kCondFill);
        lastCond[(size_t) i] = pass;
    }
    else
        pass = lastCond[(size_t) i] == (c == kCondPre);
    return pass;
}

//==============================================================================
// REEL on the clock: bar lines cut the splices, genes start on the grid.
void Engine::tickReel (int64_t k, int64_t at, double tickLen)
{
    if (mod64 (k, 16) == 0) reel.barLine (at);
    const double gs = geneSixteenths (params.reelGene);
    if (gs < 1.0)
    {
        reelGene (k, at, tickLen, 0);
        reelGene (k, at + (int64_t) (tickLen * 0.5), tickLen, 1);
    }
    else if (mod64 (k, (int64_t) gs) == 0)
        reelGene (k, at, tickLen, 0);
}

// One gene: does it break, and if it does, what does it play. Every decision is the clock's seeded
// random, so with LOOP the breaks repeat; CHAOS scales all of them.
void Engine::reelGene (int64_t k, int64_t at, double tickLen, uint32_t half)
{
    const auto& P = params;
    const float chaos = clamp (P.reelChaos, 0.0f, 1.0f);
    const int salt = (int) half;
    Reel::Gene g;
    g.length = (int) (geneSixteenths (P.reelGene) * tickLen);
    const bool anchored = P.reelAnchor && mod64 (k, 64) < 4;          // the first beat of every 4-bar line
    g.broken = chaos > 0.0f && ! anchored && rnd (40, salt) < chaos;
    const int n = std::max (1, reel.splices());
    int sp = (int) (clamp (P.reelOrganize, 0.0f, 0.999f) * (float) n) + organizeStep;
    if (rnd (41, salt) < 0.5f * chaos) sp += 1 + (int) (rnd (42, salt) * (float) (n - 1));
    g.splice = (int) mod64 (sp, n);
    const double pos16 = (double) mod64 (k, 16) + (half != 0 ? 0.5 : 0.0);
    const double off = pos16 / 16.0 + (double) P.reelSlide;
    g.offset = (float) (off - std::floor (off));
    float rate = varispeedRate (P.reelVarispeed);
    if (rnd (43, salt) < 0.35f * chaos * chaos) rate = -rate;
    if (rnd (44, salt) < 0.25f * chaos * chaos * chaos) rate *= rnd (45, salt) < 0.5f ? 2.0f : 0.5f;
    // overlapping genes (MORPH past 0.6) come back an octave or a fifth away, as layers do on the Morphagene
    if (P.reelMorph > 0.6f && rnd (46, salt) < (P.reelMorph - 0.6f) / 0.4f)
    {
        static const float ratio[4] { 2.0f, 0.5f, 1.5f, 0.75f };
        rate *= ratio[std::min (3, (int) (rnd (47, salt) * 4.0f))];
    }
    g.rate = rate;
    g.sub = rnd (48, salt) < 0.35f * chaos * chaos ? 2 + std::min (2, (int) (rnd (49, salt) * 3.0f)) : 1;
    reel.gene (at, g);
}

void Engine::seqStep (bool x, bool y)
{
    const auto& P = params;
    if (x) ++seqXPos;
    if (y) ++seqYPos;
    int idx;
    if (P.seqY > 0) idx = (seqYPos & 3) * 4 + (seqXPos & 3);
    else idx = seqXPos % std::max (1, std::min (16, P.seqLen));
    seqTarget = clamp (P.seqValues[(size_t) idx], -1.0f, 1.0f);
    display.seqIndex.store (idx, std::memory_order_relaxed);
    display.seqX.store (seqXPos & 3, std::memory_order_relaxed);
    display.seqY.store (seqYPos & 3, std::memory_order_relaxed);
}

void Engine::newProgram()
{
    progSize  = (rnd (21) - 0.5f) * 0.9f;
    progDecay = (rnd (22) - 0.5f) * 0.8f;
    progDamp  = (rnd (23) - 0.5f) * 0.8f;
    // what only CloudSeed has: diffusion, modulation, stereo, pre-delay
    progDiff  = 0.3f + 0.7f * rnd (24);
    progMod   = 0.6f * rnd (25);
    progRate  = 0.6f * rnd (26);
    progCross = rnd (27);
    progPre   = 0.3f * rnd (28) * rnd (28, 1);
    ++program;
    display.program.store (program, std::memory_order_relaxed);
}

//==============================================================================
// The step's locks and VARY's drift on the knobs a module is struck with.
void Engine::applyStepAndVary (const Event& e, Strike& s)
{
    const auto& L = params.lanes[(size_t) e.lane];
    if (e.step >= 0 && e.step < kMaxSteps)
    {
        s.p.tune  = clamp (s.p.tune  + 0.5f * L.plock[0][(size_t) e.step], 0.0f, 1.0f);
        s.p.color = clamp (s.p.color + 0.5f * L.plock[1][(size_t) e.step], 0.0f, 1.0f);
        s.p.decay = clamp (s.p.decay + 0.5f * L.plock[2][(size_t) e.step], 0.0f, 1.0f);
    }
    if (L.vary <= 0.0f) return;
    const int salt = e.lane * 16 + e.sub;
    const float v = L.vary;
    s.p.tune  = clamp (s.p.tune  + (rnd (50, salt) - 0.5f) * v * 0.04f, 0.0f, 1.0f);
    s.p.decay = clamp (s.p.decay + (rnd (51, salt) - 0.5f) * v * 0.25f, 0.0f, 1.0f);
    s.p.color = clamp (s.p.color + (rnd (52, salt) - 0.5f) * v * 0.25f, 0.0f, 1.0f);
    s.vel *= 1.0f - v * 0.3f * rnd (53, salt);
    if (e.lane < kVoices) panOff[(size_t) e.lane] = (rnd (54, salt) - 0.5f) * v * 0.6f;
}

void Engine::fire (const Event& e, int offset)
{
    const auto& P = params;
    display.fired[(size_t) e.lane].fetch_add (1, std::memory_order_relaxed);
    if (numFired < (int) fired.size()) fired[(size_t) numFired++] = { chunkBase + offset, e.lane, e.vel };
    if (P.smrRotateBy == e.lane + 1 && ! e.burst)
    {
        ++smrRot;                                                   // the SMR's ROTATE input
        display.smrRotation.store (smrRot, std::memory_order_relaxed);
    }
    if (P.seqX == e.lane + 1 || P.seqY == e.lane + 1) seqStep (P.seqX == e.lane + 1, P.seqY == e.lane + 1);
    if (P.reelOrgBy == e.lane + 1 && ! e.burst) ++organizeStep;
    if (P.reelDuckBy == e.lane + 1) reel.duck (now + offset);
    // the lane plays the DRONE (a burst acts once)
    if (const int act = P.lanes[(size_t) e.lane].droneAct; act > 0 && e.sub == 0)
        drone.act (act, e.vel, (int) (rnd (70, e.lane) * 10.0f));
    if (e.lane == scatter)
    {
        const auto& L = P.lanes[scatter];
        const bool locked = e.step >= 0 && e.step < kMaxSteps;
        const float size = clamp (P.scatSize + 0.5f * dests[scatSize] + (locked ? 0.5f * L.plock[0][(size_t) e.step] : 0.0f), 0.0f, 1.0f);
        const float texture = clamp (P.scatTexture + (locked ? 0.5f * L.plock[1][(size_t) e.step] : 0.0f), 0.0f, 1.0f);
        if (P.scatModel == 0)
            scat.fire (size, P.scatDensity * (0.5f + 0.5f * e.vel), P.scatPitch, P.scatSpread, P.scatPosition, texture, rng);
        else if (P.scatModel <= 4)
            clouds->trigger();
        else if (P.scatModel <= 6)
            parasites->trigger();
        else
            beads->trigger();
        if (P.programChange == 4) newProgram();
        return;
    }
    Strike s;
    s.vel = e.vel;
    s.p = P.voices[(size_t) e.lane];
    s.scale = P.scale;
    applyStepAndVary (e, s);
    using K = Strike::Kind;
    switch (e.lane)
    {
        case kick:    s.modA = dests[kPitch];    s.kindA = K::semis;   s.modB = dests[kDecay];    s.kindB = K::decay; forgeEnv = 1.0f; forgePh = 0.0f; break;
        case mesh:    s.modA = dests[meshTone];  s.kindA = K::timbre;  break;
        case fm:      s.modA = dests[fmIndex];   s.kindA = K::timbre;  s.modB = dests[fmRatio];   s.kindB = K::harmonics; break;
        case opto:    s.modA = dests[optoColor]; s.kindA = K::timbre;  break;
        case lantern: s.modA = dests[hatDecay];  s.kindA = K::decay;   break;
        case zap:     s.modA = dests[zapSweep];  s.kindA = K::morph;   break;
        case strike:  s.modA = dests[strNote];   s.kindA = K::degrees; s.modB = dests[strBright]; s.kindB = K::timbre; break;
        default: break;
    }
    // the slot's own voice, or the Plaits engine, Braids shape or reel patched into it
    if (isPlaits (s.p.model))
    {
        auto& pv = *plaits[(size_t) e.lane];
        pv.setModel (s.p.model - kFirstPlaits);
        pv.trigger (s);
        return;
    }
    if (isBraids (s.p.model))
    {
        auto& bv = *braids[(size_t) e.lane];
        bv.setModel (s.p.model - kFirstBraids);
        bv.trigger (s);
        return;
    }
    if (isReel (s.p.model))
    {
        reelVoices[(size_t) e.lane]->trigger (s);
        return;
    }
    voices[(size_t) e.lane]->trigger (s);
    if (e.lane == strike) display.degree.store (strikeVoice->lastDegree, std::memory_order_relaxed);
}

//==============================================================================
void Engine::control (float dt)
{
    const auto& P = params;
    // SMOOTH: slews to a new random level set on the clock (Wogglebug smooth)
    const float k = smoothTime > 0.0f ? std::exp (-dt / smoothTime) : 0.0f;
    sources[smooth] = smoothTarget + k * (sources[smooth] - smoothTarget);
    // PONG: a ball dropped from the top, losing a quarter of its speed per bounce, dropped again when it settles
    const float g = expMap (P.rate[pong], 1.5f, 60.0f);
    pongV -= g * dt;
    pongH += pongV * dt;
    if (pongH < 0.0f)
    {
        pongH = -pongH;
        pongV = -pongV * 0.78f;
        if (pongV < std::sqrt (2.0f * g) * 0.12f) { pongH = 1.0f; pongV = 0.0f; }
    }
    sources[pong] = clamp (pongH, 0.0f, 1.0f) * 2.0f - 1.0f;
    // LFO
    lfoPh += expMap (P.rate[lfo], 0.03f, 12.0f) * dt;
    lfoPh -= std::floor (lfoPh);
    sources[lfo] = std::sin (twoPi * lfoPh);
    // FORGE: a fast rippling burst struck by the kick (Control Forge on the BD9)
    forgePh += expMap (P.rate[forge], 5.0f, 70.0f) * dt;
    forgePh -= std::floor (forgePh);
    forgeEnv *= std::exp (-dt / 0.16f);
    sources[forge] = forgeEnv * (0.55f + 0.45f * (1.0f - 2.0f * std::abs (forgePh - 0.5f) * 2.0f));
    // WOGGLE: the Wogglebug's woggle chases the stepped voltage through a resonant slew: steps with
    // decaying sinusoids at their edges. RATE is how fast it catches up.
    {
        const float w = twoPi * expMap (P.rate[woggle], 0.6f, 14.0f), zeta = 0.14f;
        woggleV += (w * w * (sources[stepped] - sources[woggle]) - 2.0f * zeta * w * woggleV) * dt;
        sources[woggle] = clamp (sources[woggle] + woggleV * dt, -1.6f, 1.6f);
    }
    // ARC: up for SHAPE of the phrase, down into the line (unipolar: it opens what it is patched to)
    {
        arcPhase = std::min (0.999999, arcPhase + arcRate * dt);
        const float ph = (float) arcPhase, rise = 0.05f + 0.9f * clamp (P.arcShape, 0.0f, 1.0f);
        const float v = ph < rise ? ph / rise : 1.0f - (ph - rise) / (1.0f - rise);
        sources[arc] = v * v * (3.0f - 2.0f * v);
    }
    // DRONE: how loud the drone is (unipolar)
    sources[droneSrc] = drone.envelope();
    // SEQ: the step it is on, gliding there if asked
    {
        const float gk = P.seqGlide > 0.0f ? std::exp (-dt / (P.seqGlide * 0.3f)) : 0.0f;
        sources[seq] = seqTarget + gk * (sources[seq] - seqTarget);
    }
    updateDests();
    if ((traceTick++ & 7) == 0)
    {
        const int wi = display.traceWrite.load (std::memory_order_relaxed);
        for (int s = 0; s < kSources; ++s) display.trace[(size_t) s][(size_t) wi] = clamp (sources[(size_t) s], -1.0f, 1.0f);
        display.traceWrite.store ((wi + 1) % Display::kTrace, std::memory_order_release);
    }

    // the continuous destinations: the delay time, the reverb
    const bool karplus = P.dlyDiv >= kDelayKarplus;
    float dtime;
    if (karplus)
    {
        // tuned to STRIKE's root (TUNE on that slot), an octave up for KARPLUS 8VA; DLY.TIME bends it an octave
        const float root = 40.0f * std::pow (2.0f, std::round (12.0f * std::log2 (expMap (P.voices[strike].tune, 40.0f, 400.0f) / 40.0f)) / 12.0f);
        const float hz = root * (P.dlyDiv > kDelayKarplus ? 2.0f : 1.0f) * std::pow (2.0f, clamp (dests[dlyTime], -1.0f, 1.0f));
        dtime = (float) sr / std::max (20.0f, hz);
    }
    else
        dtime = (float) (delayDivisionBeats (P.dlyDiv) * 60.0 / std::max (20.0, lastBpm) * sr) * std::pow (2.0f, clamp (dests[dlyTime], -1.0f, 1.0f));
    zig.setPitched (karplus);
    zig.setTime (dtime);
    zig.setFeedback (P.dlyFeedback * (karplus ? 1.0f : 1.02f));
    zig.setStyle (P.dlyStyle);
    zig.setMachine (P.dlyMachine, P.dlyRate);

    // SMR: six bands up the scale from FREQ, SPREAD scale steps apart, ROTATE steps up
    // (the bands are worked out even with SMR off, so its display shows where it would ring)
    {
        const float root = expMap (P.smrFreq, 40.0f, 640.0f) * std::pow (2.0f, clamp (dests[smrFreq], -1.5f, 1.5f));
        const int spread = 1 + (int) std::lround (P.smrSpread * 4.0f);
        std::array<float, Smr::kBands> hz {};
        for (int b = 0; b < Smr::kBands; ++b)
            hz[(size_t) b] = std::min ((float) sr * 0.42f, root * std::pow (2.0f, StrikeVoice::degreeToSemis (P.smrScale, (smrRot % 20) + b * spread) / 12.0f));
        smr.setFrequencies (hz);
        smr.set (P.smrQ, P.smrGlide);
        const bool on = P.smrMix > 0.0f;
        for (int b = 0; b < Smr::kBands; ++b)
        {
            display.smrLevel[(size_t) b].store (on ? smr.levels[(size_t) b] : 0.0f, std::memory_order_relaxed);
            display.smrHz[(size_t) b].store (on ? smr.current[(size_t) b] : hz[(size_t) b], std::memory_order_relaxed);
        }
    }
    controlDrone (dt);
    updateSustain();
    const bool prog = P.programChange > 0;
    const float vs = P.verbSize + (prog ? progSize : 0.0f), vd = P.verbDecay + (prog ? progDecay : 0.0f), vm = P.verbDamp + (prog ? progDamp : 0.0f);
    if (P.verbModel == 1)
        seed->set (vs, P.verbInfinite ? 1.6f : vd, vm, prog ? progDiff : 0.7f, prog ? progMod : 0.3f, prog ? progRate : 0.3f, prog ? progCross : 0.5f, prog ? progPre : 0.05f);
    else
        space.set (vs, vd, vm);
    space.setInfinite (P.verbInfinite);

    for (int s = 0; s < kSources; ++s) display.source[(size_t) s].store (sources[(size_t) s], std::memory_order_relaxed);
    for (int d = 0; d < kDests; ++d) display.dest[(size_t) d].store (dests[(size_t) d], std::memory_order_relaxed);
}

float Engine::cvOf (int choice) const
{
    if (choice <= 0 || choice > kSources) return 0.0f;
    return sources[(size_t) (choice - 1)] * params.depth[(size_t) (choice - 1)];
}

// The DRONE in the case's key (STRIKE's root), PITCH semitones from it, on the scale degree the lanes moved
// it to; its CV inputs from the matrix's sources; the RESONATOR in its chord.
void Engine::controlDrone (float dt)
{
    (void) dt;
    const auto& P = params;
    const float key = 40.0f * std::pow (2.0f, std::round (12.0f * std::log2 (expMap (P.voices[strike].tune, 40.0f, 400.0f) / 40.0f)) / 12.0f);
    const int deg = drone.degree();
    const float semis = (float) P.dPitch + StrikeVoice::degreeToSemis (P.scale, deg);
    Drone::Settings d;
    d.level = P.dLevel;
    d.rootHz = key * std::pow (2.0f, semis / 12.0f);
    d.ratio = P.dRatio; d.fine = P.dFine;
    d.fmAB = P.dFmAB; d.fmBA = P.dFmBA; d.ring = P.dRing; d.sync = P.dSync; d.fold = P.dFold; d.shape = P.dShape;
    d.bright = P.dBright; d.chaos = P.dChaos; d.duck = P.dDuck;
    d.pitchCv = cvOf (P.dPitchCv); d.fmCv = cvOf (P.dFmCv); d.foldCv = cvOf (P.dFoldCv); d.lpgCv = cvOf (P.dLpgCv);
    d.arc = P.dArc; d.arcValue = sources[arc];
    drone.set (d);
    display.droneHz.store (P.dLevel > 0.0f ? d.rootHz : 0.0f, std::memory_order_relaxed);
    display.droneDegree.store (deg, std::memory_order_relaxed);
    // the resonator's four strings: the drone's degree, and two, four and six scale steps above it
    std::array<float, SympatheticStrings::kStrings> hz {};
    static const int up[SympatheticStrings::kStrings] { 0, 2, 4, 6 };
    for (int k = 0; k < SympatheticStrings::kStrings; ++k)
    {
        hz[(size_t) k] = key * std::pow (2.0f, ((float) P.dPitch + 12.0f + StrikeVoice::degreeToSemis (P.scale, deg + up[k])) / 12.0f);
        display.resHz[(size_t) k].store (P.resMix > 0.0f ? hz[(size_t) k] : 0.0f, std::memory_order_relaxed);
    }
    res.set (hz, P.resDecay, P.resBright);
}

// SUSTAIN: the slot's voice (whichever model is in it) holds its note; switched on, it is struck once so
// it sounds before any hit; switched off, it lets go.
void Engine::updateSustain()
{
    const auto& P = params;
    for (int v = 0; v < kVoices; ++v)
    {
        const bool want = P.voices[(size_t) v].sus && P.solo < 0;
        const int model = P.voices[(size_t) v].model;
        if (want == susOn[(size_t) v] && (! want || model == susModel[(size_t) v])) continue;
        VoiceBase* all[4] { voices[(size_t) v].get(), plaits[(size_t) v].get(), braids[(size_t) v].get(), reelVoices[(size_t) v].get() };
        VoiceBase* active = isPlaits (model) ? all[1] : (isBraids (model) ? all[2] : (isReel (model) ? all[3] : all[0]));
        for (auto* vb : all) vb->setSustain (want && vb == active);
        if (want) sustainStrike (v);
        susOn[(size_t) v] = want;
        susModel[(size_t) v] = model;
    }
}

void Engine::sustainStrike (int v)
{
    const auto& P = params;
    Strike s;
    s.vel = 0.85f;
    s.p = P.voices[(size_t) v];
    s.scale = P.scale;
    if (isPlaits (s.p.model)) { plaits[(size_t) v]->setModel (s.p.model - kFirstPlaits); plaits[(size_t) v]->trigger (s); }
    else if (isBraids (s.p.model)) { braids[(size_t) v]->setModel (s.p.model - kFirstBraids); braids[(size_t) v]->trigger (s); }
    else if (isReel (s.p.model)) reelVoices[(size_t) v]->trigger (s);
    else voices[(size_t) v]->trigger (s);
}

void Engine::updateDests()
{
    for (int d = 0; d < kDests; ++d)
    {
        float v = 0.0f;
        for (int s = 0; s < kSources; ++s)
            if (const int p = params.pins[(size_t) s][(size_t) d]; p != 0)
                v += (float) p * sources[(size_t) s] * params.depth[(size_t) s];
        dests[(size_t) d] = clamp (v, -1.5f, 1.5f);
    }
}

//==============================================================================
// Each slot into its own buffer (X-MOD reads one slot's audio while it plays another).
void Engine::render (int from, int to)
{
    const int n = to - from;
    if (n <= 0) return;
    for (int v = 0; v < kVoices; ++v)
    {
        auto& voice = *voices[(size_t) v];
        auto& pv = *plaits[(size_t) v];
        auto& bv = *braids[(size_t) v];
        auto& rv = *reelVoices[(size_t) v];
        const bool a = voice.active(), b = pv.active(), c = bv.active(), d = rv.active();
        if (! a) voice.energy = 0.0f;
        if (! b) pv.energy = 0.0f;
        if (! c) bv.energy = 0.0f;
        if (! d) rv.energy = 0.0f;
        float* dst = vraw[(size_t) v].data() + from;
        std::fill (dst, dst + n, 0.0f);
        if (a) voice.render (dst, n);
        if (b) pv.render (dst, n);
        if (c) bv.render (dst, n);
        if (d) rv.render (dst, n);
    }
    drone.render (droneL.data() + from, droneR.data() + from, droneMono.data() + from, n);
}

// X-MOD: slot v's audio x moved by another slot's audio (or the SMR's rings of the feedback loop).
const float* Engine::crossMod (int v, const float* x, int n)
{
    const auto& vp = params.voices[(size_t) v];
    if (vp.xmod <= 0 || vp.xamt <= 0.0f) return x;
    const float a = clamp (vp.xamt, 0.0f, 1.0f);
    const float* src;
    if (vp.xsrc == kXsrcFeedback) src = fbTap.data();
    else if (vp.xsrc == kXsrcDrone) src = droneMono.data();
    else
    {
        const int s = std::max (0, std::min (kVoices - 1, vp.xsrc));
        if (s == v) return x;
        src = vraw[(size_t) s].data();
    }
    auto& st = xmods[(size_t) v];
    float* y = xbuf.data();
    switch (vp.xmod)
    {
        case 1:   // RING: multiplied by the other (softly limited so a loud source does not explode it)
            for (int i = 0; i < n; ++i) y[i] = x[i] * (1.0f - a + a * soft (src[i] * 3.0f));
            break;
        case 2:   // PM: read through a short delay the other moves: phase modulation for any voice
        {
            const float base = 0.0006f * (float) sr, depth = 0.0005f * (float) sr * a;
            const int len = (int) st.line.size();
            for (int i = 0; i < n; ++i)
            {
                st.line[(size_t) st.w] = x[i];
                const float d = base + depth * clamp (src[i] * 2.0f, -1.0f, 1.0f);
                float rp = (float) st.w - d;
                while (rp < 0.0f) rp += (float) len;
                const int i0 = (int) rp % len;
                const float fr = rp - std::floor (rp);
                y[i] = st.line[(size_t) i0] + (st.line[(size_t) ((i0 + 1) % len)] - st.line[(size_t) i0]) * fr;
                st.w = (st.w + 1) % len;
            }
            break;
        }
        default:  // FOLLOW opens it with the other's loudness; DUCK closes it
        {
            const bool follow = vp.xmod == 3;
            for (int i = 0; i < n; ++i)
            {
                const float e = std::abs (src[i]);
                st.env = e > st.env ? st.env + (e - st.env) * xAtk : st.env * xRel;
                const float open = std::min (1.0f, st.env * 4.0f);
                y[i] = x[i] * (follow ? 1.0f - a + a * open : 1.0f - a * open);
            }
            break;
        }
    }
    return y;
}

void Engine::mixVoices (int n)
{
    const auto& P = params;
    for (int v = 0; v < kVoices; ++v)
    {
        const float pan = clamp (P.voices[(size_t) v].pan + panOff[(size_t) v], -1.0f, 1.0f) * 0.5f + 0.5f;
        panL[(size_t) v] = std::cos (pan * pi * 0.5f) * 1.41421f;
        panR[(size_t) v] = std::sin (pan * pi * 0.5f) * 1.41421f;
    }
    for (int v = 0; v < kVoices; ++v)
    {
        if (P.solo >= 0 && P.solo != v) continue;                  // a stem: only its lane is heard
        const float* x = crossMod (v, vraw[(size_t) v].data(), n);
        const auto& vp = P.voices[(size_t) v];
        const float lvl = vp.level * vp.level * 1.1f;
        const float pl = panL[(size_t) v] * lvl, pr = panR[(size_t) v] * lvl, snd = vp.send;
        // PROTECTed lanes go round the reel
        const bool prot = ((P.reelProtect >> v) & 1) != 0;
        float* dl = prot ? pdryL.data() : dryL.data();
        float* dr = prot ? pdryR.data() : dryR.data();
        for (int i = 0; i < n; ++i)
        {
            const float s = x[i];
            dl[i] += s * pl;
            dr[i] += s * pr;
            sendL[(size_t) i] += s * pl * snd;
            sendR[(size_t) i] += s * pr * snd;
            resIn[(size_t) i] += s * lvl;
        }
    }
    // the DRONE: round the reel unless its PROTECT is off, into the effects at its SEND
    if (P.solo < 0)
    {
        const bool prot = ((P.reelProtect >> 8) & 1) != 0;
        float* dl = prot ? pdryL.data() : dryL.data();
        float* dr = prot ? pdryR.data() : dryR.data();
        for (int i = 0; i < n; ++i)
        {
            dl[i] += droneL[(size_t) i];
            dr[i] += droneR[(size_t) i];
            sendL[(size_t) i] += droneL[(size_t) i] * P.dSend;
            sendR[(size_t) i] += droneR[(size_t) i] * P.dSend;
        }
    }
}

// RESONATOR: the drums excite four strings in the drone's chord
void Engine::runResonator (int n)
{
    const auto& P = params;
    if (P.resMix <= 0.0f || P.solo >= 0) return;
    std::fill (zl.begin(), zl.begin() + n, 0.0f);
    std::fill (zr.begin(), zr.begin() + n, 0.0f);
    res.process (resIn.data(), zl.data(), zr.data(), n);
    const bool prot = ((P.reelProtect >> 8) & 1) != 0;
    float* dl = prot ? pdryL.data() : dryL.data();
    float* dr = prot ? pdryR.data() : dryR.data();
    const float g = P.resMix * 1.2f;
    for (int i = 0; i < n; ++i)
    {
        dl[i] += zl[(size_t) i] * g;
        dr[i] += zr[(size_t) i] * g;
        sendL[(size_t) i] += zl[(size_t) i] * g * 0.5f;
        sendR[(size_t) i] += zr[(size_t) i] * g * 0.5f;
    }
}

//==============================================================================
// FEEDBACK: the master a clock division ago, saturated and band-limited, into the SMR's input
// (and so on into the delay, the reverb and, through X-MOD's FDBK, the modules).
void Engine::feedbackReturn (int n)
{
    const auto& P = params;
    // the FDBK source of X-MOD: the SMR's rings, one chunk behind (what the SMR made a moment ago)
    for (int i = 0; i < n; ++i)
        fbTap[(size_t) i] = smrRing[(size_t) mod64 (now + i - kChunk, kSmrRing)];
    if (P.fdbk <= 0.0f || P.solo >= 0) { fbLevel *= 0.9f; return; }
    const float d = clamp ((float) (fdbkTimeBeats (P.fdbkTime) * 60.0 / std::max (20.0, lastBpm) * sr),
                           (float) kChunk, (float) (histCap - kChunk));
    const int64_t delay = (int64_t) d;
    const float g = clamp (P.fdbk, 0.0f, 1.0f) * 1.2f;
    float pk = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const auto j = (size_t) mod64 (now + i - delay, histCap);
        const float l = fbLpL.lp (fbHpL.hp (soft (histL[j] * g)));
        const float r = fbLpR.lp (fbHpR.hp (soft (histR[j] * g)));
        sendL[(size_t) i] += l;
        sendR[(size_t) i] += r;
        pk = std::max (pk, std::max (std::abs (l), std::abs (r)));
    }
    fbLevel = std::max (pk, fbLevel * 0.9f);
}

void Engine::runEffects (float* outL, float* outR, int n)
{
    const auto& P = params;
    const int solo = P.solo;
    bool xFeedback = false;
    for (const auto& vp : P.voices) xFeedback = xFeedback || (vp.xmod > 0 && vp.xsrc == kXsrcFeedback);
    // SMR on the sends: its rings go to the mix and on into the delay and the reverb; it also runs, unheard,
    // when the feedback loop or an X-MOD needs its rings
    const bool smrOn = P.smrMix > 0.0f || (P.fdbk > 0.0f && solo < 0) || xFeedback;
    if (smrOn)
    {
        smr.process (sendL.data(), sendR.data(), ml.data(), mr.data(), n);
        for (int i = 0; i < n; ++i) smrRing[(size_t) mod64 (now + i, kSmrRing)] = 0.5f * (ml[(size_t) i] + mr[(size_t) i]);
        const float g = P.smrMix * 1.5f;
        const float back = P.smrMix > 0.0f ? g * 0.5f : (P.fdbk > 0.0f && solo < 0 ? 0.35f : 0.0f);
        for (int i = 0; i < n; ++i)
        {
            const float l = ml[(size_t) i], r = mr[(size_t) i];
            dryL[(size_t) i] += l * g; dryR[(size_t) i] += r * g;
            sendL[(size_t) i] += l * back; sendR[(size_t) i] += r * back;
        }
    }
    zig.process (sendL.data(), sendR.data(), zl.data(), zr.data(), n);
    for (int i = 0; i < n; ++i)
    {
        inL[(size_t) i] = dryL[(size_t) i] + pdryL[(size_t) i] + zl[(size_t) i] * P.dlyMix;
        inR[(size_t) i] = dryR[(size_t) i] + pdryR[(size_t) i] + zr[(size_t) i] * P.dlyMix;
    }
    const bool frozen = now < freezeUntil || P.scatFreeze;          // lane 8's bursts, or the bed held
    scat.setFrozen (P.scatFreeze);
    display.frozen.store (frozen && P.scatModel > 0, std::memory_order_relaxed);
    if (P.scatModel == 0)
        scat.process (inL.data(), inR.data(), gl.data(), gr.data(), n, P.scatFeed);
    else if (P.scatModel <= 4)
    {
        CloudsScatter::Settings cs;
        cs.mode = P.scatModel - 1;
        cs.position = P.scatPosition;
        cs.size = clamp (P.scatSize + 0.5f * dests[scatSize], 0.0f, 1.0f);
        cs.pitch = (float) P.scatCloudsPitch;
        cs.density = P.scatDensity;
        cs.texture = P.scatTexture;
        cs.spread = P.scatSpread;
        cs.feedback = P.scatFeed;
        clouds->set (cs);
        clouds->freeze (frozen);
        clouds->process (inL.data(), inR.data(), gl.data(), gr.data(), n);
    }
    else if (P.scatModel >= 7)
    {
        // Beads: POSITION is its TIME, TEXTURE its SHAPE, SPREAD its attenurandomizers (turned left)
        BeadsScatter::Settings bs;
        bs.media = P.scatModel - 7;
        bs.time = P.scatPosition;
        bs.size = clamp (P.scatSize + 0.5f * dests[scatSize], 0.0f, 1.0f);
        bs.shape = P.scatTexture;
        bs.pitch = (float) P.scatCloudsPitch;
        bs.density = P.scatDensity;
        bs.feedback = P.scatFeed;
        bs.reverb = P.scatReverb;
        bs.random = P.scatSpread;
        // seeded only when lane 8 can actually fire: an empty lane leaves SEED unpatched (Beads seeds itself)
        bool lane8 = false;
        const auto& L8 = P.lanes[scatter];
        const int steps8 = std::max (1, std::min (kMaxSteps, L8.steps));
        for (int s = 0; s < steps8 && L8.on && L8.chance > 0.0f && ! lane8; ++s) lane8 = laneHits (L8, steps8, s);
        bs.seeded = lane8;
        beads->set (bs);
        beads->freeze (frozen);
        beads->process (inL.data(), inR.data(), gl.data(), gr.data(), n);
    }
    else
    {
        ParasitesScatter::Settings ps;
        ps.mode = P.scatModel - 5;
        ps.position = P.scatPosition;
        ps.size = clamp (P.scatSize + 0.5f * dests[scatSize], 0.0f, 1.0f);
        ps.pitch = (float) P.scatCloudsPitch;
        ps.density = P.scatDensity;
        ps.texture = P.scatTexture;
        ps.spread = P.scatSpread;
        ps.feedback = P.scatFeed;
        parasites->set (ps);
        parasites->freeze (frozen);
        parasites->process (inL.data(), inR.data(), gl.data(), gr.data(), n);
    }
    if (solo >= 0 && solo != scatter) std::fill (gl.begin(), gl.begin() + n, 0.0f), std::fill (gr.begin(), gr.begin() + n, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        inL[(size_t) i] = sendL[(size_t) i] + 0.6f * zl[(size_t) i] + 0.5f * gl[(size_t) i];
        inR[(size_t) i] = sendR[(size_t) i] + 0.6f * zr[(size_t) i] + 0.5f * gr[(size_t) i];
    }
    if (P.verbModel == 1)
    {
        // INFINITE: CloudSeed at its longest decay, its input closed
        for (int i = 0; i < n; ++i)
        {
            seedIn += ((P.verbInfinite ? 0.0f : 1.0f) - seedIn) * 0.0005f;
            inL[(size_t) i] *= seedIn;
            inR[(size_t) i] *= seedIn;
        }
        seed->process (inL.data(), inR.data(), sl.data(), sr2.data(), n);
    }
    else space.process (inL.data(), inR.data(), sl.data(), sr2.data(), n);
    if (solo == scatter)
    {
        // the SCATTER stem: its clouds and what they send to SPACE, without the modules under them
        for (int i = 0; i < n; ++i)
        {
            outL[i] = gl[(size_t) i] * P.scatMix * 1.4f + sl[(size_t) i] * P.verbMix * 1.6f * 0.5f;
            outR[i] = gr[(size_t) i] * P.scatMix * 1.4f + sr2[(size_t) i] * P.verbMix * 1.6f * 0.5f;
        }
        return;
    }
    const bool scatProtected = ((P.reelProtect >> scatter) & 1) != 0;
    for (int i = 0; i < n; ++i)
    {
        // what the reel takes: the unprotected modules and the returns (SCATTER's unless it is protected)
        const float scL = gl[(size_t) i] * P.scatMix * 1.4f, scR = gr[(size_t) i] * P.scatMix * 1.4f;
        outL[i] = dryL[(size_t) i] + zl[(size_t) i] * P.dlyMix + (scatProtected ? 0.0f : scL) + sl[(size_t) i] * P.verbMix * 1.6f;
        outR[i] = dryR[(size_t) i] + zr[(size_t) i] * P.dlyMix + (scatProtected ? 0.0f : scR) + sr2[(size_t) i] * P.verbMix * 1.6f;
        if (scatProtected) { pdryL[(size_t) i] += scL; pdryR[(size_t) i] += scR; }
    }
}

// REEL on what is not protected, then the protected lanes added back; the result is the master,
// which the feedback loop remembers.
void Engine::runReel (float* outL, float* outR, int n)
{
    const auto& P = params;
    if (P.solo < 0)
    {
        Reel::Settings rs;
        rs.chaos = P.reelChaos;
        rs.length = P.reelLength;
        rs.varispeed = P.reelVarispeed;
        rs.morph = P.reelMorph;
        rs.split = reelSplitHz (P.reelSplit);
        rs.duck = P.reelDuck;
        rs.hold = P.reelHold;
        reel.set (rs);
        reel.process (outL, outR, n, now);
    }
    else
        reel.discardUntil (now + n);
    for (int i = 0; i < n; ++i) { outL[i] += pdryL[(size_t) i]; outR[i] += pdryR[(size_t) i]; }
    if (P.solo >= 0) return;
    for (int i = 0; i < n; ++i)
    {
        const auto j = (size_t) mod64 (now + i, histCap);
        histL[j] = outL[i];
        histR[j] = outR[i];
    }
}

// The bus: Drum Buss (schwung-busdriver); the stems skip it.
void Engine::runBus (float* outL, float* outR, int n)
{
    const auto& P = params;
    if (P.solo >= 0) return;
    auto& B = *bus;
    auto& bp = B.d.p;
    if (P.busComp != B.comp || P.busChar != B.character || std::abs (P.drive - B.drive) > 1.0e-4f
        || std::abs (P.crunch - B.crunch) > 1.0e-4f || std::abs (P.trans - B.trans) > 1.0e-4f || std::abs (P.boom - B.boom) > 1.0e-4f)
    {
        B.comp = P.busComp; B.character = P.busChar; B.drive = P.drive; B.crunch = P.crunch; B.trans = P.trans; B.boom = P.boom;
        bp.comp = P.busComp;
        bp.driveType = std::max (0, std::min (2, P.busChar));
        bp.drive = clamp (P.drive, 0.0f, 1.0f);
        bp.crunch = clamp (P.crunch, 0.0f, 1.0f);
        bp.dampHz = expMap (1.0f - 0.7f * P.crunch, 2500.0f, 20000.0f);   // Damp follows Crunch: its antidote
        bp.trans = clamp (P.trans, -1.0f, 1.0f);
        bp.boom = clamp (P.boom, 0.0f, 1.0f);
        bp.boomHz = 50.0f;
        bp.boomDecay = 0.5f;
        bp.trim = dbToGain (-6.0f);       // the case runs hot; Drum Buss wants a drum bus level
        bp.outGain = dbToGain (-3.0f);    // Drum Buss adds about 3 dB at rest
        bp.dryWet = 1.0f;
        B.d.applyAll();
    }
    for (int i = 0; i < n; ++i) { B.io[(size_t) (2 * i)] = outL[i]; B.io[(size_t) (2 * i + 1)] = outR[i]; }
    B.d.process (B.io.data(), n);
    const float lv = dbToGain (P.level);
    for (int i = 0; i < n; ++i)
    {
        outL[i] = clamp (B.io[(size_t) (2 * i)] * lv, -1.5f, 1.5f);
        outR[i] = clamp (B.io[(size_t) (2 * i + 1)] * lv, -1.5f, 1.5f);
    }
}

void Engine::writeDisplay (int n)
{
    const auto& P = params;
    for (int v = 0; v < kVoices; ++v)
    {
        display.energy[(size_t) v].store (std::max ({ voices[(size_t) v]->energy, plaits[(size_t) v]->energy,
                                                      braids[(size_t) v]->energy, reelVoices[(size_t) v]->energy }), std::memory_order_relaxed);
        display.slotReel[(size_t) v].store (reelVoices[(size_t) v]->position(), std::memory_order_relaxed);
    }
    if (P.scatModel == 0) display.energy[scatter].store (std::min (1.0f, (float) scat.activeGrains() / 12.0f), std::memory_order_relaxed);
    else
    {
        float pk = 0.0f;
        for (int i = 0; i < n; ++i) pk = std::max (pk, std::max (std::abs (gl[(size_t) i]), std::abs (gr[(size_t) i])));
        display.energy[scatter].store (std::min (1.0f, pk * 2.0f), std::memory_order_relaxed);
    }
    display.grains.store (scat.activeGrains(), std::memory_order_relaxed);
    display.breaking.store (zig.breaking(), std::memory_order_relaxed);
    display.feedback.store (fbLevel, std::memory_order_relaxed);
}

//==============================================================================
void Engine::process (float* outL, float* outR, int n, const Transport& t)
{
    numFired = 0;
    // the host's position advances through the chunks; the internal clock advances itself
    const double bpm = clamp ((float) ((t.hostHasPosition && t.bpm > 1.0) ? t.bpm : (double) params.bpm), 20.0f, 400.0f);
    for (int off = 0; off < n; off += kChunk)
    {
        const int m = std::min (kChunk, n - off);
        Transport tc = t;
        if (t.playing && t.hostHasPosition) tc.ppq = t.ppq + (double) off * bpm / 60.0 / sr;
        chunkBase = off;
        processChunk (outL + off, outR + off, m, tc);
    }
    // the scope
    int w = display.scopeWrite.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        display.scopeL[(size_t) w] = outL[i];
        display.scopeR[(size_t) w] = outR[i];
        w = (w + 1) % Display::kScope;
    }
    display.scopeWrite.store (w, std::memory_order_release);
}

void Engine::processChunk (float* outL, float* outR, int n, const Transport& t)
{
    const auto& P = params;

    // ---- the master clock: the host's, or our own when the host is stopped and RUN is on
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
    bpm = clamp ((float) bpm, 20.0f, 400.0f);
    lastBpm = bpm;
    const double ppqPerSample = bpm / 60.0 / sr;
    if (running && ! t.playing) internalPpq += ppqPerSample * n;
    display.running.store (running, std::memory_order_relaxed);
    display.host.store (t.playing && t.hostHasPosition, std::memory_order_relaxed);
    display.bpm.store ((float) bpm, std::memory_order_relaxed);
    if (running) display.ppq.store (ppq0, std::memory_order_relaxed);

    if (running)
    {
        // in samples on the song's timeline: a 16th lands on the first sample at or after it, wherever
        // the host's blocks (and our chunks) happen to cut
        const int64_t c0 = (int64_t) std::llround (ppq0 / ppqPerSample);
        const float swing = clamp (P.swing, 0.0f, 0.5f);
        for (int64_t k = (int64_t) std::floor (ppq0 * 4.0) - 1; ; ++k)
        {
            const double tp = (double) k * 0.25 + ((k & 1) ? swing * 0.25 : 0.0);
            const int64_t ts = (int64_t) std::ceil (tp / ppqPerSample - 1.0e-6);
            if (ts >= c0 + n) break;
            // a sample of slack at the start: a host whose positions round a little ahead must not lose a 16th
            // that fell on the boundary (the one just played is lastTick)
            if (ts < c0 - 1 || k == lastTick) continue;
            lastTick = k;
            tick (k, now + std::max<int64_t> (0, ts - c0), bpm);
        }
    }

    for (auto* b : { &dryL, &dryR, &pdryL, &pdryR, &sendL, &sendR, &droneL, &droneR, &resIn }) std::fill (b->begin(), b->begin() + n, 0.0f);

    int pos = 0;
    while (pos < n)
    {
        if (controlLeft <= 0) { control ((float) kControl / (float) sr); controlLeft = kControl; }
        int end = std::min (n, pos + controlLeft);
        // fire what is due, find the next event inside this segment
        bool firedAny = true;
        while (firedAny)
        {
            firedAny = false;
            for (int q = 0; q < queued; ++q)
            {
                const int64_t rel = queue[(size_t) q].at - now;
                if (rel <= pos)
                {
                    const Event e = queue[(size_t) q];
                    queue[(size_t) q] = queue[(size_t) --queued];
                    fire (e, pos);
                    firedAny = true;
                    break;
                }
                if (rel < end) end = (int) rel;
            }
        }
        render (pos, end);
        controlLeft -= end - pos;
        pos = end;
    }

    feedbackReturn (n);
    mixVoices (n);
    runResonator (n);
    runEffects (outL, outR, n);
    runReel (outL, outR, n);
    runBus (outL, outR, n);
    writeDisplay (n);
    now += n;
}

} // namespace flt
