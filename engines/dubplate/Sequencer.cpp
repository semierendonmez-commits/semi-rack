// DUBPLATE — step sequencer over the arrangement (SEMI Family port, see Sequencer.h).
// Adapted from Source/Audio/ArrangementEngine.cpp (stepState, emitLaneStep, timelinePass)
// and Source/Audio/EventFx.cpp (event windows).
#include "Sequencer.h"
#include <algorithm>
#include <cmath>

namespace dubplate
{
namespace
{
bool fillParticipates (int profile, FillKind kind)
{
    switch (profile)
    {
        case 0:  return kind == FillKind::Break || kind == FillKind::Snare;
        case 1:  return kind == FillKind::Snare || kind == FillKind::Hat;
        case 2:  return kind == FillKind::Hat || kind == FillKind::Kick;
        case 3:  return kind == FillKind::Bass || kind == FillKind::Kick;
        default: return false;
    }
}

bool isKickish (Role r) { return r == Role::Kick || r == Role::KickSub || r == Role::Punch; }

// Break lanes play their loop as 16 slices per bar (the plugin's Slice sync); every other
// lane plays its 16-step pattern (the plugin's one-shot path), so loop-preferring bass /
// pad lanes still give one trigger per note here.
bool sliceLane (Role r) { return r == Role::Break || r == Role::BreakChop; }

float clamp01 (float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
} // namespace

void Sequencer::setArrangement (const Arrangement* a, bool loopArrangement)
{
    arr = a;
    loop = loopArrangement;
    if (arr == nullptr) { nLanes = 0; return; }
    nLanes = std::min (maxLanes, (int) arr->lanes.size());
    tl.build (*arr, barOffsetBars % std::max (1, arr->totalBars()), loop);
    barOffsetBars = barOffsetBars % std::max (1, arr->totalBars());

    for (int li = 0; li < nLanes; ++li) childCount[(size_t) li] = 0;
    for (int ci = 0; ci < nLanes; ++ci)
    {
        const int parent = arr->lanes[(size_t) ci].variationOf;
        if (parent < 0) continue;
        const int pi = arr->laneIndex (parent);
        if (pi >= 0 && pi < nLanes && childCount[(size_t) pi] < 8)
            children[(size_t) pi][(size_t) childCount[(size_t) pi]++] = ci;
    }
    anySolo = false;
    for (int li = 0; li < nLanes; ++li) anySolo = anySolo || arr->lanes[(size_t) li].solo;
}

bool Sequencer::variationWants (const Lane& lane, int bar, int si) const
{
    switch (lane.alt)
    {
        case AltRule::Every8:    return ((bar - tl.sectionStart (si)) / 8) % 2 == 1;
        case AltRule::Drop2Only: return arr->sections[(size_t) si].type == SectionType::Drop && tl.dropOrdinal[(size_t) si] >= 1;
        case AltRule::Random:    return hash01 (arr->seed, (uint32_t) lane.uid, (uint32_t) (bar / 8)) > 0.5f;
        case AltRule::Manual:    return true;
    }
    return true;
}

Sequencer::StepState Sequencer::stepState (int li, long long absStep, const Macros& m) const
{
    StepState st;
    st.arrStep = tl.arrStep (absStep);
    if (st.arrStep < 0) return st;

    const int bar = (int) (st.arrStep / 16);
    const int si = tl.sectionOfBar (bar);
    if (si < 0) return st;
    st.section = si;

    const auto& lane = arr->lanes[(size_t) li];
    const auto& sec = arr->sections[(size_t) si];
    const auto& info = roleInfo (lane.role);
    const Cell cell = si < (int) lane.cells.size() ? lane.cells[(size_t) si] : Cell {};

    st.level = cell.on ? cell.level : 1.0f;
    const bool muted = lane.mute || (anySolo && ! lane.solo);
    if (muted) { st.level = 0.0f; return st; }

    st.active = cell.on;

    // Energy macro mutes the upper layers.
    const int limit = m.energy < 0.5f ? 1 + (int) (m.energy / 0.5f * 4.0f) : 5;
    if (info.priority > limit && info.bus != Bus::FX) st.active = false;

    // Variations.
    if (lane.variationOf >= 0)
    {
        if (lane.alt != AltRule::Manual && arr->findLane (lane.variationOf) != nullptr)
            st.active = st.active && variationWants (lane, bar, si);
    }
    else
    {
        for (int c = 0; c < childCount[(size_t) li]; ++c)
        {
            const auto& child = arr->lanes[(size_t) children[(size_t) li][(size_t) c]];
            if (child.alt == AltRule::Manual) continue;
            const bool childOn = si < (int) child.cells.size() && child.cells[(size_t) si].on;
            if (childOn && variationWants (child, bar, si)) { st.active = false; break; }
        }
    }

    const int secEnd = tl.sectionEnd (si);
    const double stepPpqArr = st.arrStep * 0.25;
    st.halfTime = (sec.events & evHalfTime) != 0 && info.bus == Bus::Drums;

    if ((sec.events & evDropOut) && (isKickish (lane.role) || info.bus == Bus::Bass)
        && stepPpqArr >= secEnd * 4.0 - std::max (1.0, (double) arr->eventLength))
    {
        st.active = false;
        st.dropOut = true;
    }

    const int barInSec = bar - tl.sectionStart (si);
    const int every = std::max (1, arr->fill.every);
    const float prob = clamp01 (arr->fill.probability * (0.4f + m.chaos * 1.2f));
    st.fillBar = (((barInSec + 1) % every == 0) && hash01 (arr->seed, (uint32_t) bar, 77u) < prob) || bar == forcedFillBar;
    st.snareRoll = (sec.events & evSnareRoll) != 0 && bar >= secEnd - 2;
    return st;
}

bool Sequencer::laneActive (int li, long long absStep, const Macros& m) const
{
    if (arr == nullptr || li < 0 || li >= nLanes) return false;
    return stepState (li, absStep, m).active;
}

int Sequencer::sectionAt (long long absStep, long long* arrStepOut) const
{
    if (arr == nullptr) return -1;
    const long long s = tl.arrStep (absStep);
    if (arrStepOut != nullptr) *arrStepOut = s;
    return s < 0 ? -1 : tl.sectionOfBar ((int) (s / 16));
}

bool Sequencer::isFillBar (long long absStep, const Macros& m) const
{
    if (arr == nullptr) return false;
    const long long s = tl.arrStep (absStep);
    if (s < 0) return false;
    const int bar = (int) (s / 16);
    const int si = tl.sectionOfBar (bar);
    if (si < 0) return false;
    const int every = std::max (1, arr->fill.every);
    const float prob = clamp01 (arr->fill.probability * (0.4f + m.chaos * 1.2f));
    return (((bar - tl.sectionStart (si) + 1) % every == 0) && hash01 (arr->seed, (uint32_t) bar, 77u) < prob) || bar == forcedFillBar;
}

EventState Sequencer::eventAt (double absPpq) const
{
    EventState e;
    if (arr == nullptr) return e;
    const double ap = tl.arrPpq (absPpq);
    if (ap < 0.0) return e;
    const int si = tl.sectionOfBar ((int) (ap / 4.0));
    if (si < 0) return e;
    const auto ev = arr->sections[(size_t) si].events;
    const double secStart = tl.sectionStart (si) * 4.0, secEnd = tl.sectionEnd (si) * 4.0;
    const double toEnd = secEnd - ap;
    const double lenScale = std::max (0.25, std::min (4.0, (double) arr->eventLength));

    if (ev & evSweep)
    {
        const double p = (ap - secStart) / std::max (1.0, secEnd - secStart);
        if (p > 0.5) { e.bit = 0; e.progress = (float) ((p - 0.5) * 2.0); }
    }
    const double rewindLen = 2.0 * lenScale, stopLen = 2.0 * lenScale, brakeLen = 1.0 * lenScale, repLen = 2.0 * lenScale;
    if ((ev & evRewind) && toEnd <= rewindLen)          { e.bit = 2; e.progress = (float) (1.0 - toEnd / rewindLen); }
    else if ((ev & evTapeStop) && toEnd <= stopLen)     { e.bit = 3; e.progress = (float) (1.0 - toEnd / stopLen); }
    else if ((ev & evVinylBrake) && toEnd <= brakeLen)  { e.bit = 9; e.progress = (float) (1.0 - toEnd / brakeLen); }
    else if ((ev & evBeatRepeat) && toEnd <= repLen)    { e.bit = 7; e.progress = (float) (1.0 - toEnd / repLen); }
    else if ((ev & evSiren) && toEnd <= 8.0 * lenScale) { e.bit = 6; e.progress = (float) (1.0 - toEnd / (8.0 * lenScale)); }
    else
    {
        // throws and the drop-out have no FX of their own in EventFx; the plugin opens the
        // sends / cuts the bass for the last beat(s): show them as events too
        const double throwLen = std::max (0.5, (double) arr->eventLength);
        const double dropLen = std::max (1.0, (double) arr->eventLength);
        if ((ev & evDelayThrow) && toEnd <= throwLen)    { e.bit = 4; e.progress = (float) (1.0 - toEnd / throwLen); }
        else if ((ev & evVerbThrow) && toEnd <= throwLen) { e.bit = 5; e.progress = (float) (1.0 - toEnd / throwLen); }
        else if ((ev & evDropOut) && toEnd <= dropLen)   { e.bit = 1; e.progress = (float) (1.0 - toEnd / dropLen); }
        else if ((ev & evSnareRoll) && toEnd <= 8.0)     { if (e.bit < 0) { e.bit = 10; e.progress = (float) (1.0 - toEnd / 8.0); } }
    }
    return e;
}

void Sequencer::emitLane (int li, long long k, const Macros& m, Hit* out, int& n, int max) const
{
    const auto& lane = arr->lanes[(size_t) li];
    const auto& info = roleInfo (lane.role);
    const auto st = stepState (li, k, m);
    if (! st.active || st.section < 0) return;

    auto push = [&] (float delaySteps, float vel, float semis, bool reverse, int slice)
    {
        if (n >= max || vel <= 0.0f) return;
        Hit h; h.lane = li; h.delay = delaySteps; h.vel = clamp01 (vel * st.level); h.semis = semis; h.reverse = reverse; h.slice = slice;
        out[n++] = h;
    };

    const int s16 = (int) (st.arrStep % 16);
    const auto& grid = arr->fill.grid;
    const float density = clamp01 (arr->fill.density * (0.5f + m.fill));
    auto row = [&] (int r)
    {
        return st.fillBar && fillParticipates (arr->fill.profile, info.fill) && grid[(size_t) r][(size_t) s16]
               && hash01 (arr->seed, (uint32_t) st.arrStep, (uint32_t) (r + 11)) < 0.35f + density * 0.65f;
    };
    const bool fRoll = row (0), fStutter = row (1), fReverse = row (2), fPitch = row (3);
    const int secStartStep = tl.sectionStart (st.section) * 16;

    if (sliceLane (lane.role))
    {
        const int sliceCount = std::max (1, lane.resolvedLoopBars()) * 16;
        long long ls;
        bool retrigger = true;
        if (st.halfTime)
        {
            const long long local = st.arrStep - secStartStep;
            retrigger = (local & 1) == 0;
            ls = (secStartStep + local / 2) % sliceCount;
        }
        else ls = st.arrStep % sliceCount;
        if (! retrigger) return;

        const float swing = swingOf (m);
        const bool garageSwing = swing > 52.0f && ! st.halfTime;
        const float swingSteps = ((s16 & 1) && garageSwing) ? (swing - 50.0f) / 100.0f * 0.5f * 4.0f : 0.0f;
        if (info.fill == FillKind::Bass && fRoll) return;
        const int idx = (int) ((fStutter ? (ls - 1 + sliceCount) : ls) % sliceCount) % 16;
        const float semis = lane.pitch + (fPitch ? 7.0f : 0.0f);   // the plugin's x1.4983 slice pitch
        push (swingSteps, 1.0f, semis, fReverse, idx);
        if (fRoll) push (swingSteps + 0.5f, 1.0f, semis, fReverse, idx);
        return;
    }

    float vel = 0.0f, semis = lane.pitch;
    bool reverse = false, extra32 = false;

    if (lane.role == Role::Riser || lane.role == Role::Impact)
    {
        const int secStart = tl.sectionStart (st.section), secEnd = tl.sectionEnd (st.section);
        long long trigStep = (long long) secStart * 16;
        if (lane.role == Role::Riser)
        {
            const int lenBars = std::max (1, std::min (secEnd - secStart, 4));
            trigStep = (long long) (secEnd - lenBars) * 16;
        }
        if (st.arrStep != trigStep) return;
        vel = 1.0f;
    }
    else
    {
        int pIdx = s16;
        if (st.halfTime)
        {
            const long long local = st.arrStep - secStartStep;
            if (local & 1) return;
            pIdx = (int) ((local / 2) % 16);
        }
        vel = lane.pattern[(size_t) pIdx] / 127.0f;
        semis += lane.stepPitch[(size_t) pIdx];

        if (st.snareRoll && info.fill == FillKind::Snare)
        {
            const int barsLeft = tl.sectionEnd (st.section) - (int) (st.arrStep / 16);
            const float t = barsLeft >= 2 ? s16 / 32.0f : 0.5f + s16 / 32.0f;
            vel = 0.3f + 0.7f * t;
            extra32 = barsLeft < 2;
        }

        switch (info.fill)
        {
            case FillKind::Snare:
                if (fRoll)    { extra32 = true; vel = std::max (vel, 0.5f + 0.5f * s16 / 15.0f); }
                if (fStutter) vel = std::max (vel, 0.7f);
                if (fReverse) { reverse = true; vel = std::max (vel, 0.8f); }
                if (fPitch)   semis += 1.0f + s16 / 3.0f;
                break;
            case FillKind::Kick:
                if (fStutter) vel = std::max (vel, 0.8f);
                if (fRoll)    extra32 = vel > 0.0f;
                break;
            case FillKind::Hat:
                if (arr->fill.profile == 2 && (fRoll || fStutter)) vel = 0.0f;
                else if (fRoll) { extra32 = true; vel = std::max (vel, 0.6f); }
                if (fReverse) reverse = true;
                break;
            case FillKind::Bass:
                if (fRoll)    vel = 0.0f;
                if (fStutter) vel = std::max (vel, 0.8f);
                if (fReverse) reverse = true;
                if (fPitch)   semis += 12.0f;
                break;
            default: break;
        }
    }

    if (vel <= 0.0f) return;
    const float swingSteps = (! st.halfTime && (s16 & 1)) ? (swingOf (m) - 50.0f) / 100.0f * 0.5f * 4.0f : 0.0f;
    push (swingSteps, vel, semis, reverse, -1);
    if (extra32) push (swingSteps + 0.5f, vel * 0.8f, semis, reverse, -1);
}

int Sequencer::step (long long k, const Macros& m, Hit* out, int max)
{
    if (arr == nullptr || nLanes == 0) return 0;

    // performance jump, quantised to bars (as timelinePass)
    if (k % 16 == 0 && pendingJump >= 0 && (k / 16) % jumpQuant == 0)
    {
        if (pendingJump < tl.numSections)
        {
            const long long absBar = k / 16;
            const long long target = tl.sectionStart (pendingJump);
            long long off = (target - absBar) % tl.totalBars;
            if (off < 0) off += tl.totalBars;
            barOffsetBars = (int) off;
            tl.build (*arr, barOffsetBars, loop);
        }
        pendingJump = -1;
    }

    // a forced fill (FILL input) lasts its one bar
    if (forcedFillBar >= 0 && k % 16 == 0 && tl.arrStep (k) / 16 == (forcedFillBar + 1) % tl.totalBars)
        forcedFillBar = -1;
    int n = 0;
    for (int li = 0; li < nLanes; ++li)
        emitLane (li, k, m, out, n, max);
    return n;
}
} // namespace dubplate
