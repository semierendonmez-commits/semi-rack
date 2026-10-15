#include "Engine.h"
#include <cmath>
#include <cstring>

namespace tna
{

namespace
{
    float onePole (double sr, double hz) { return (float) (1.0 - std::exp (-2.0 * 3.14159265358979 * hz / sr)); }
    constexpr double kMinBpm = 40.0;
    constexpr double kHistSeconds = 12.0;
    constexpr double kStatSeconds = 0.04;    // each slot is measured over its first 40 ms
}

void Engine::prepare (double sampleRate, int)
{
    sr = sampleRate;
    const int barCap = (int) (sr * 60.0 / kMinBpm * kBeatsPerBar) + 64;
    for (auto& b : bars)
    {
        b.l.assign ((size_t) barCap, 0.0f);
        b.r.assign ((size_t) barCap, 0.0f);
    }
    snapL.assign ((size_t) barCap, 0.0f);
    snapR.assign ((size_t) barCap, 0.0f);
    const int hist = (int) (sr * kHistSeconds);
    histL.assign ((size_t) hist, 0.0f);
    histR.assign ((size_t) hist, 0.0f);
    reset();
}

void Engine::reset()
{
    for (auto& b : bars) b.clearStats();
    cur = 0;
    curAbsBar = INT64_MIN;
    lastCls.fill (Empty);
    lastLevel.fill (0); lastScoreK.fill (0); lastScoreS.fill (0);
    snapCls.fill (Empty);
    snapLevel.fill (0);
    snapLen = 0;
    lpLow = lpMid1 = lpMid2 = lpHigh = 0;
    std::fill (histL.begin(), histL.end(), 0.0f);
    std::fill (histR.begin(), histR.end(), 0.0f);
    histWrite = 0;
    planBlock = INT64_MIN;
    evBoundary = INT64_MIN;
    evStarted = false;
    wet = 0.0f;
    lastCC.fill (-1);
    tele.barsSeen.store (0);
    for (auto& c : tele.cls) c.store (Empty);
}

//==============================================================================
void Engine::startBar (std::int64_t absBar, double spb)
{
    if (curAbsBar != INT64_MIN && absBar == curAbsBar + 1 && bars[cur].length > 0)
    {
        finishBar();
        cur = 1 - cur;
    }
    curAbsBar = absBar;
    bars[cur].clearStats();
    bars[cur].spb = spb;
}

void Engine::finishBar()
{
    auto& b = bars[cur];
    std::array<float, kSlots> lo {}, mi {}, hi {}, tot {};
    float maxTot = 0.0f;
    for (int s = 0; s < kSlots; ++s)
    {
        const int n = b.count[(size_t) s];
        if (n <= 0) continue;
        lo[(size_t) s] = (float) std::sqrt (b.low[(size_t) s] / n);
        mi[(size_t) s] = (float) std::sqrt (b.mid[(size_t) s] / n);
        hi[(size_t) s] = (float) std::sqrt (b.high[(size_t) s] / n);
        tot[(size_t) s] = (float) std::sqrt (b.tot[(size_t) s] / n);
        maxTot = std::max (maxTot, tot[(size_t) s]);
    }
    for (int s = 0; s < kSlots; ++s)
    {
        const auto i = (size_t) s;
        const float rel = maxTot > 1.0e-5f ? tot[i] / maxTot : 0.0f;
        const float sum = lo[i] + mi[i] + hi[i] + 1.0e-9f;
        const float kS = lo[i] / sum, sS = mi[i] / sum, hS = hi[i] / sum;
        // a slot whose level is barely above the end of the one before is a tail, not a hit
        const double pre = b.preCount[i] > 0 ? b.pre[i] / b.preCount[i] : 0.0;
        const double now = b.count[i] > 0 ? b.tot[i] / b.count[i] : 0.0;
        const bool onset = pre <= 1.0e-12 || now > 2.5 * pre;
        int c = Empty;
        if (rel >= 0.10f && onset)
        {
            if (rel < 0.30f)          c = hS > 0.5f ? Hat : Ghost;
            else if (kS > 0.45f)      c = Kick;
            else if (hS > 0.55f)      c = Hat;
            else                      c = Snare;
        }
        lastCls[i] = c;
        lastLevel[i] = rel;
        lastScoreK[i] = kS * rel;
        lastScoreS[i] = (sS + 0.5f * hS) * rel * (c == Snare ? 1.5f : 1.0f);
        tele.cls[i].store (c, std::memory_order_relaxed);
        tele.level[i].store (rel, std::memory_order_relaxed);
    }

    // waveform for the editor
    const int len = std::max (1, b.length);
    float norm = 1.0e-6f;
    std::array<float, kPeaks> pk {};
    for (int p = 0; p < kPeaks; ++p)
    {
        const int a = len * p / kPeaks, e = std::max (a + 1, len * (p + 1) / kPeaks);
        float m = 0.0f;
        for (int k = a; k < e && k < len; ++k)
            m = std::max (m, std::max (std::abs (b.l[(size_t) k]), std::abs (b.r[(size_t) k])));
        pk[(size_t) p] = m;
        norm = std::max (norm, m);
    }
    for (int p = 0; p < kPeaks; ++p) tele.peaks[(size_t) p].store (pk[(size_t) p] / norm, std::memory_order_relaxed);
    tele.barsSeen.fetch_add (1, std::memory_order_relaxed);
}

//==============================================================================
int Engine::pickSlot (SlotClass wanted, int exclude) const
{
    int best = -1;
    float bestScore = 0.0f;
    for (int s = 0; s < kSlots; ++s)
    {
        if (s == exclude) continue;
        const auto i = (size_t) s;
        float score = 0.0f;
        if (wanted == Snare && snapCls[i] == Snare) score = lastScoreS[i];
        else if (wanted == Kick && snapCls[i] == Kick) score = lastScoreK[i];
        else if (wanted == Empty) score = snapLevel[i];      // "loudest"
        if (score > bestScore) { bestScore = score; best = s; }
    }
    if (best < 0 && wanted != Empty) return pickSlot (Empty, exclude);
    return best;
}

void Engine::beginEvent (const GridPos& g)
{
    evStarted = true;
    const auto& b = bars[1 - cur];
    snapLen = tele.barsSeen.load (std::memory_order_relaxed) > 0 ? b.length : 0;
    if (snapLen > 0)
    {
        std::memcpy (snapL.data(), b.l.data(), sizeof (float) * (size_t) snapLen);
        std::memcpy (snapR.data(), b.r.data(), sizeof (float) * (size_t) snapLen);
    }
    snapSpb = b.spb;
    snapCls = lastCls;
    snapLevel = lastLevel;

    Rng rng (evSeed);
    SlotClass want = Snare;
    switch (set.source)
    {
        case RollSource::Snare:   want = Snare; break;
        case RollSource::Kick:    want = Kick;  break;
        case RollSource::Loudest: want = Empty; break;
        case RollSource::Vary:
        {
            const float r = rng.uniform();
            want = r < 0.55f ? Snare : (r < 0.8f ? Kick : Empty);
            break;
        }
    }
    rollSlot = pickSlot (want, -1);
    // with variation the roll sometimes takes the second-best hit of its kind
    if (set.variation > 0.0f && rng.uniform() < set.variation * 0.6f)
    {
        const int alt = pickSlot (want, rollSlot);
        if (alt >= 0 && snapCls[(size_t) alt] == snapCls[(size_t) std::max (0, rollSlot)]) rollSlot = alt;
    }
    rollSlot2 = pickSlot (Snare, rollSlot);
    kickSlot = pickSlot (Kick, -1);

    rollDiv = set.rollStart;
    hitAge = 0.0;
    layerFired = false;
    tapeStart = histWrite;
    tapeRead = 0.0;
    retrigStep = -1;
    ghostStep = -1;
    ghostOn = false;
    tele.rollSlot.store (rollSlot, std::memory_order_relaxed);
    tele.rollSlot2.store (set.layer2 > 0.0f ? rollSlot2 : -1, std::memory_order_relaxed);
    (void) g;
}

float Engine::readSnap (const std::vector<float>& buf, double pos) const
{
    if (pos < 0.0 || snapLen < 2) return 0.0f;
    const int i = (int) pos;
    if (i >= snapLen - 1) return 0.0f;
    const float f = (float) (pos - i);
    return buf[(size_t) i] + f * (buf[(size_t) i + 1] - buf[(size_t) i]);
}

float Engine::readHist (const std::vector<float>& buf, double pos) const
{
    const auto n = (std::int64_t) buf.size();
    if (n < 2) return 0.0f;
    const double fl = std::floor (pos);
    const auto i = (std::int64_t) fl;
    if (i < histWrite - n + 2 || i >= histWrite) return 0.0f;
    const float f = (float) (pos - fl);
    const float a = buf[(size_t) floorMod (i, n)], b = buf[(size_t) floorMod (i + 1, n)];
    return a + f * (b - a);
}

void Engine::publishPlan()
{
    for (int j = 0; j < kEvents; ++j)
    {
        tele.alloc[(size_t) j].store (plan.alloc[(size_t) j], std::memory_order_relaxed);
        tele.desire[(size_t) j].store (plan.desire[(size_t) j], std::memory_order_relaxed);
        tele.amount[(size_t) j].store (plan.amount[(size_t) j], std::memory_order_relaxed);
    }
    tele.budgetBeats.store (plan.budgetBeats, std::memory_order_relaxed);
}

//==============================================================================
void Engine::process (float* L, float* R, int n, const Transport& tp, std::vector<MidiEvent>* midi)
{
    tele.playing.store (tp.playing, std::memory_order_relaxed);
    const float aWet = onePole (sr, 1.0 / 0.0025);   // ~2.5 ms

    if (! tp.playing || tp.bpm <= 1.0)
    {
        // stopped: the break passes untouched; forget the running bar
        for (int i = 0; i < n; ++i)
        {
            wet += (0.0f - wet) * aWet;
            histL[(size_t) floorMod (histWrite, (std::int64_t) histL.size())] = L[i];
            histR[(size_t) floorMod (histWrite, (std::int64_t) histR.size())] = R[i];
            ++histWrite;
        }
        curAbsBar = INT64_MIN;
        evBoundary = INT64_MIN;
        tele.progress.store (-1.0f, std::memory_order_relaxed);
        if (midi != nullptr)
            for (int t = 0; t <= kTiers; ++t)
                if (lastCC[(size_t) t] != 0) { midi->push_back ({ 0, 20 + t, 0, false }); lastCC[(size_t) t] = 0; }
        return;
    }

    const double spb = sr * 60.0 / std::max (kMinBpm, tp.bpm);
    const double bps = 1.0 / spb;
    const float aLow = onePole (sr, 150.0), aM1 = onePole (sr, 250.0), aM2 = onePole (sr, 4500.0), aHi = onePole (sr, 6000.0);
    const int statLen = (int) (kStatSeconds * sr);
    const auto histN = (std::int64_t) histL.size();
    const double edge = 0.002 * sr;   // 2 ms fades at region edges

    // the plan follows the knobs live
    if (planBlock != INT64_MIN) { plan = makePlan (set, planBlock); publishPlan(); }

    int lastTier = -1;
    float lastU = -1.0f;
    bool lastGap = false;
    GridPos g;
    Action lastAction = Action::Off;

    for (int i = 0; i < n; ++i)
    {
        const double ppq = tp.ppq + (double) i * bps;
        const float dryL = L[i], dryR = R[i];

        // ---- capture --------------------------------------------------------------
        const auto absBar = (std::int64_t) std::floor (ppq / kBeatsPerBar);
        if (absBar != curAbsBar) startBar (absBar, spb);
        {
            auto& b = bars[cur];
            const double barPos = ppq - (double) absBar * kBeatsPerBar;
            const int idx = (int) std::floor (barPos * spb + 0.5);
            if (idx >= 0 && idx < (int) b.l.size())
            {
                b.l[(size_t) idx] = dryL;
                b.r[(size_t) idx] = dryR;
                b.length = std::max (b.length, idx + 1);
            }
            const float x = 0.5f * (dryL + dryR);
            lpLow += aLow * (x - lpLow);
            lpMid1 += aM1 * (x - lpMid1);
            lpMid2 += aM2 * (x - lpMid2);
            lpHigh += aHi * (x - lpHigh);
            const float mid = lpMid2 - lpMid1, high = x - lpHigh;
            const int slot = std::min (kSlots - 1, (int) (barPos * 4.0));
            const double inSlot = (barPos - slot * 0.25) * spb;
            if (inSlot >= 0.0 && inSlot < statLen)
            {
                const auto s = (size_t) slot;
                b.low[s] += (double) lpLow * lpLow;
                b.mid[s] += (double) mid * mid;
                b.high[s] += (double) high * high;
                b.tot[s] += (double) x * x;
                b.count[s]++;
            }
            // the last 10 ms before the next slot: tells a new hit from the tail of this one
            const double slotLen = 0.25 * spb;
            if (slot + 1 < kSlots && inSlot >= slotLen - 0.010 * sr && inSlot < slotLen)
            {
                b.pre[(size_t) slot + 1] += (double) x * x;
                b.preCount[(size_t) slot + 1]++;
            }
        }
        histL[(size_t) floorMod (histWrite, histN)] = dryL;
        histR[(size_t) floorMod (histWrite, histN)] = dryR;
        ++histWrite;

        // ---- where are we on the phrase grid ------------------------------------
        g = locate (ppq, set.phraseOffset);
        if (g.block != planBlock)
        {
            planBlock = g.block;
            plan = makePlan (set, planBlock);
            publishPlan();
        }
        if (g.nextBoundary != evBoundary)
        {
            evBoundary = g.nextBoundary;
            evStarted = false;
            evSeed = eventSeed (set.seed, evBoundary);
            tele.rollSlot.store (-1, std::memory_order_relaxed);
            tele.rollSlot2.store (-1, std::memory_order_relaxed);
        }

        const auto& tc = set.tier[(size_t) g.tier];
        const float amount = plan.amount[(size_t) g.event];
        const double gap = (set.gapBeats > 0.0 && g.tier >= set.gapFromTier) ? set.gapBeats : 0.0;
        const double dist = g.beatsToBoundary;
        // action length snaps to the 1/16 grid, so rolls land on the beat
        const double actLen = std::floor (tc.windowBeats * amount * 4.0 + 0.5) / 4.0;
        const bool inGap = gap > 0.0 && dist <= gap;
        const bool ghostZone = tc.action == Action::Ghost && amount > 0.0f && dist <= tc.windowBeats && ! inGap;
        const bool act = (int) tc.action > (int) Action::Ghost && actLen > 0.0 && ! inGap && dist <= gap + actLen;
        if ((ghostZone || act) && ! evStarted) beginEvent (g);

        if (! act) actOn = false;
        float wl = 0.0f, wr = 0.0f, addL = 0.0f, addR = 0.0f;
        float wetTarget = 0.0f;
        float u = -1.0f;

        if (inGap)
        {
            wetTarget = 1.0f;
            const double toBar = dist * spb;
            if (toBar < edge) wetTarget = (float) (toBar / edge);   // open again right on the bar line
        }
        else if (act)
        {
            const double tb = actLen - (dist - gap);          // beats into the action
            const double ud = std::clamp (tb / actLen, 0.0, 1.0);
            u = (float) ud;
            const bool first = ! actOn;
            actOn = true;
            const double tbS = tb * spb;
            const double tail = (dist - gap) * spb;           // samples until the action ends
            const float edgeGain = (float) std::min ({ 1.0, tbS / (0.0005 * sr) + 0.0, tail / edge });
            wetTarget = 1.0f;

            switch (tc.action)
            {
                case Action::Roll:
                {
                    // the repeat length for this point of the roll
                    const double r = set.rollEnd / set.rollStart;
                    double div;
                    if (set.shape == RollShape::Stepped)
                    {
                        const int nd = std::max (0, (int) std::lround (std::log2 (std::max (1.0e-6, set.rollStart / set.rollEnd))));
                        const int seg = std::min (nd, (int) std::floor (ud * (nd + 1) + 1.0e-7));
                        div = nd > 0 ? set.rollStart * std::pow (r, (double) seg / nd) : set.rollStart;
                    }
                    else
                        div = set.rollStart * std::pow (r, std::pow (ud, (double) set.rollCurve));
                    div = std::max (1.0 / 64.0, div);

                    if (first) { hitAge = 0.0; rollDiv = div; tele.hitCount.fetch_add (1, std::memory_order_relaxed); }
                    else if (hitAge >= rollDiv - 1.0e-9)
                    {
                        hitAge -= rollDiv;
                        rollDiv = div;
                        tele.hitCount.fetch_add (1, std::memory_order_relaxed);
                    }

                    const float ratio = std::pow (2.0f, set.rollPitch * u / 12.0f);
                    const float gain = set.rollVelStart + (1.0f - set.rollVelStart) * u;
                    const double ageS = hitAge * spb;
                    const double left = (rollDiv - hitAge) * spb;
                    const float env = (float) std::min ({ 1.0, ageS / (0.0005 * sr), left / (0.003 * sr) });
                    if (rollSlot >= 0)
                    {
                        const double pos = rollSlot * 0.25 * snapSpb + ageS * ratio;
                        wl = readSnap (snapL, pos) * env * gain;
                        wr = readSnap (snapR, pos) * env * gain;
                    }
                    if (set.layer2 > 0.0f && rollSlot2 >= 0 && rollDiv >= 0.124)
                    {
                        const double age2 = hitAge - rollDiv * 0.5;
                        if (age2 >= 0.0)
                        {
                            const double a2 = age2 * spb;
                            const float env2 = (float) std::min ({ 1.0, a2 / (0.0005 * sr), left / (0.003 * sr) });
                            const double pos2 = rollSlot2 * 0.25 * snapSpb + a2 * ratio;
                            const float g2 = set.layer2 * gain * 0.8f * env2;
                            wl += readSnap (snapL, pos2) * g2;
                            wr += readSnap (snapR, pos2) * g2;
                        }
                    }
                    hitAge += bps;
                    break;
                }
                case Action::Retrig:
                {
                    const int step = (int) std::floor (tb * 4.0);
                    if (step != retrigStep)
                    {
                        retrigStep = step;
                        retrigAge = 0.0;
                        Rng rng (evSeed + (std::uint64_t) step * 7919u);
                        retrigSlot = -1;
                        if (rng.uniform() < 0.55f + 0.45f * amount)
                            for (int tries = 0; tries < 6 && retrigSlot < 0; ++tries)
                            {
                                const int s = rng.below (kSlots);
                                if (snapCls[(size_t) s] != Empty || tries == 5) retrigSlot = s;
                            }
                    }
                    if (retrigSlot >= 0)
                    {
                        const double ageS = retrigAge * spb;
                        const double left = (0.25 - retrigAge) * spb;
                        const float env = (float) std::min ({ 1.0, ageS / (0.0005 * sr), left / (0.002 * sr) });
                        const double pos = retrigSlot * 0.25 * snapSpb + ageS;
                        wl = readSnap (snapL, pos) * env;
                        wr = readSnap (snapR, pos) * env;
                    }
                    else
                    {
                        wetTarget = 0.0f;
                    }
                    retrigAge += bps;
                    break;
                }
                case Action::Reverse:
                {
                    const double pos = (double) tapeStart - 1.0 - tbS;
                    wl = readHist (histL, pos);
                    wr = readHist (histR, pos);
                    break;
                }
                case Action::TapeStop:
                {
                    const double rate = std::pow (1.0 - (double) u, 1.6);
                    wl = readHist (histL, (double) tapeStart + tapeRead);
                    wr = readHist (histR, (double) tapeStart + tapeRead);
                    tapeRead += rate;
                    break;
                }
                case Action::Cut:
                default:
                    break;
            }
            wl *= edgeGain;
            wr *= edgeGain;
        }

        if (ghostZone)
        {
            u = (float) std::clamp (1.0 - dist / tc.windowBeats, 0.0, 1.0);
            const auto gstep = (std::int64_t) std::floor (ppq * 4.0);
            if (gstep != ghostStep)
            {
                ghostStep = gstep;
                ghostAge = 0.0;
                const int slot = (int) floorMod (gstep, kSlots);
                const int c = lastCls[(size_t) slot];
                const bool quiet = c == Empty || c == Ghost || c == Hat;
                Rng rng (evSeed ^ (std::uint64_t) gstep * 104729u);
                ghostOn = quiet && kickSlot >= 0 && rng.uniform() < amount * (0.6f + 0.4f * set.variation);
            }
            if (ghostOn)
            {
                const double ageS = ghostAge * spb;
                const double left = (0.25 - ghostAge) * spb;
                const float env = (float) std::min ({ 1.0, ageS / (0.0005 * sr), left / (0.003 * sr), dist * spb / edge });
                const float lvl = set.ghostLevel * (0.55f + 0.45f * u) * env;
                const double pos = kickSlot * 0.25 * snapSpb + ageS;
                addL = readSnap (snapL, pos) * lvl;
                addR = readSnap (snapR, pos) * lvl;
            }
            ghostAge += bps;
        }

        wet += (wetTarget - wet) * aWet;
        const float outL = dryL * (1.0f - wet) + wl + addL;
        const float outR = dryR * (1.0f - wet) + wr + addR;
        L[i] = dryL + (outL - dryL) * set.mix;
        R[i] = dryR + (outR - dryR) * set.mix;

        lastTier = act || ghostZone ? g.tier : -1;
        lastU = u;
        lastGap = inGap;
        lastAction = act || ghostZone ? tc.action : Action::Off;
    }

    // ---- telemetry
    tele.relBar.store ((int) g.relBar, std::memory_order_relaxed);
    tele.block.store ((int) g.block, std::memory_order_relaxed);
    tele.event.store (g.event, std::memory_order_relaxed);
    tele.tier.store (lastTier, std::memory_order_relaxed);
    tele.action.store ((int) lastAction, std::memory_order_relaxed);
    tele.progress.store (lastU, std::memory_order_relaxed);
    tele.inGap.store (lastGap, std::memory_order_relaxed);
    const double lastPpq = tp.ppq + (double) (n - 1) * bps;
    const double blockStartPpq = ((double) g.block * kBlockBars + set.phraseOffset) * kBeatsPerBar;
    tele.barInBlock.store ((float) ((lastPpq - blockStartPpq) / kBeatsPerBar), std::memory_order_relaxed);
    float spent = 0.0f;
    for (int j = 0; j < g.event; ++j) spent += plan.alloc[(size_t) j];
    if (lastU >= 0.0f) spent += plan.alloc[(size_t) g.event] * lastU;
    tele.spent.store (spent, std::memory_order_relaxed);
    spentBeats = spent;

    // ---- CC lanes: 20..23 = the 4 / 8 / 16 / 32 bar turns (0..127 through the action), 24 = budget spent
    if (midi != nullptr)
    {
        for (int t = 0; t < kTiers; ++t)
        {
            const int v = (lastTier == t && lastU >= 0.0f) ? (int) std::lround (lastU * 127.0f) : 0;
            if (v != lastCC[(size_t) t]) { midi->push_back ({ n - 1, 20 + t, v, false }); lastCC[(size_t) t] = v; }
        }
        const int b = plan.budgetBeats > 0.0f ? (int) std::lround (127.0f * std::clamp (spent / plan.budgetBeats, 0.0f, 1.0f)) : 0;
        if (b != lastCC[kTiers]) { midi->push_back ({ n - 1, 24, b, false }); lastCC[kTiers] = b; }
    }
}

} // namespace tna
