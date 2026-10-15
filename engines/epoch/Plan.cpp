// Vendored from semierendonmez-commits/epoch Source/engine. Only change: the EpochLink.h include
// (shared-memory DAW bus) is replaced by Roles.h, which carries just the Role enum.
#include "Plan.h"
#include "Roles.h"   // Rack port: was ../../link/EpochLink.h (only the Role enum is used here)
#include <algorithm>
#include <cmath>

namespace epoch
{

const char* sourceName (int s)
{
    static const char* n[] = { "ENERGY", "SPACE", "HOLE", "DENSITY", "TENSION", "HEAT", "ARC", "TURN", "CALM", "DRIFT", "BUDGET" };
    return (s >= 0 && s < kNumSources) ? n[s] : "--";
}

PlanParams PlanParams::defaults()
{
    PlanParams p;
    using namespace epochlink;
    struct D { int role, src; float lo, hi, curve, smooth; int cc; };
    const D d[kSlots] = {
        { Energy,    SrcEnergy,  0.0f, 1.0f,  0.0f, 0.0f, 20 },
        { Density,   SrcDensity, 0.0f, 1.0f,  0.0f, 0.5f, 21 },
        { Chaos,     SrcHeat,    0.0f, 0.9f,  0.3f, 1.0f, 22 },
        { Mutation,  SrcTension, 0.05f, 0.8f, 0.2f, 0.0f, 23 },
        { Duck,      SrcEnergy,  0.1f, 0.7f,  0.4f, 2.0f, 24 },
        { Space,     SrcHole,    0.0f, 1.0f,  0.0f, 0.0f, 25 },
        { Variation, SrcBudget,  0.0f, 1.0f,  0.0f, 0.0f, 26 },
        { Arc,       SrcArc,     0.0f, 1.0f,  0.0f, 0.0f, 27 },
    };
    for (int s = 0; s < kSlots; ++s)
    {
        auto& c = p.slots[(size_t) s];
        c.role = d[s].role; c.source = d[s].src; c.lo = d[s].lo; c.hi = d[s].hi;
        c.curve = d[s].curve; c.smoothBeats = d[s].smooth; c.cc = d[s].cc;
    }
    return p;
}

void Plan::allocate()
{
    for (auto& v : src) v.assign ((size_t) kMaxSteps, 0.0f);
    for (auto& v : out) v.assign ((size_t) kMaxSteps, 0.0f);
    turnLevel.assign ((size_t) kMaxSteps, 0);
    barSection.assign ((size_t) kMaxBars, -1);
}

float Plan::at (int slot, double step) const
{
    if (steps <= 0) return 0.0f;
    step = std::clamp (step, 0.0, (double) (steps - 1));
    const int i0 = (int) step;
    const int i1 = std::min (steps - 1, i0 + 1);
    const float f = (float) (step - (double) i0);
    const auto& o = out[(size_t) slot];
    return o[(size_t) i0] + (o[(size_t) i1] - o[(size_t) i0]) * f;
}

static float clamp01 (float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static float smoothstep (float x) { x = clamp01 (x); return x * x * (3.0f - 2.0f * x); }

float shapeValue (float x, const SlotConfig& c)
{
    x = clamp01 (x);
    if (c.invert) x = 1.0f - x;
    if (std::abs (c.curve) > 1.0e-3f) x = std::pow (x, std::pow (4.0f, c.curve));
    return clamp01 (c.lo + (c.hi - c.lo) * x);
}

void render (const Score& score, const PlanParams& pp, Plan& plan)
{
    const int bars = std::clamp (score.bars, 4, kMaxBars);
    const int steps = bars * kStepsPerBar;
    plan.bars = bars;
    plan.steps = steps;

    auto& E = plan.src[SrcEnergy];
    auto& S = plan.src[SrcSpace];
    auto& H = plan.src[SrcHole];
    auto& D = plan.src[SrcDensity];
    auto& T = plan.src[SrcTension];
    auto& X = plan.src[SrcHeat];
    auto& A = plan.src[SrcArc];
    auto& U = plan.src[SrcTurn];
    auto& C = plan.src[SrcCalm];
    auto& R = plan.src[SrcDrift];
    auto& B = plan.src[SrcBudget];

    // ---- the drawn curves, sampled in the middle of every 16th
    for (int i = 0; i < steps; ++i)
    {
        const double bar = ((double) i + 0.5) / kStepsPerBar;
        E[(size_t) i] = score.energy.eval (bar);
        S[(size_t) i] = std::clamp (score.space.eval (bar) * pp.spaceScale, 0.0f, 0.95f);
    }

    // ---- sections: which one each step is in
    const int nSec = (int) score.sections.size();

    // ---- holes: one decision per grain, a two-state chain whose stationary share is the SPACE
    // curve and whose stay probability is CLUSTER; the share it has realised so far nudges the next
    // decision so the plan lands on the drawn share over the whole set, not only on average.
    const int grain = std::clamp (pp.grainSteps, 2, 128);
    const float rho = std::clamp (pp.cluster, 0.0f, 0.97f);
    bool prev = false;
    double wanted = 0.0, got = 0.0;
    for (int u = 0; u * grain < steps; ++u)
    {
        const int a = u * grain, b = std::min (steps, a + grain);
        float s = 0.0f;
        for (int i = a; i < b; ++i) s += S[(size_t) i];
        s /= (float) (b - a);
        float p = prev ? rho + (1.0f - rho) * s : (1.0f - rho) * s;
        if (u == 0) p = s;
        p += 0.12f * (float) (wanted - got);
        p = s <= 0.0005f ? 0.0f : std::clamp (p, 0.0f, 0.98f);
        const bool hole = hash01 (pp.seed, (uint32_t) u) < p;
        wanted += s;
        got += hole ? 1.0 : 0.0;
        prev = hole;
        for (int i = a; i < b; ++i) H[(size_t) i] = hole ? 1.0f : 0.0f;
    }
    if (pp.anchor)
    {
        for (int i = 0; i < steps; i += 4 * kStepsPerBar)               // the first beat of each 4-bar line
            for (int k = 0; k < 4 && i + k < steps; ++k) H[(size_t) (i + k)] = 0.0f;
        for (const auto& sec : score.sections)                           // a drop lands
            if (sec.role == Role::Drop || sec.role == Role::Wall)
            {
                const int i0 = (int) std::round (sec.bar * kStepsPerBar);
                for (int k = 0; k < kStepsPerBar && i0 + k < steps; ++k) if (i0 + k >= 0) H[(size_t) (i0 + k)] = 0.0f;
            }
    }

    // ---- tension, turns, arcs, drift
    static const int lineBars[4] = { 4, 8, 16, 32 };
    static const float lineW[4] = { 0.22f, 0.32f, 0.48f, 0.72f };
    const int turnLv = std::clamp (pp.turnLevel, 0, 3);
    const double driftPeriod = 8.0 / std::max (0.05f, pp.driftRate);
    int sec = -1;
    for (int i = 0; i < steps; ++i)
    {
        const double bar = (double) i / kStepsPerBar;
        while (sec + 1 < nSec && score.sections[(size_t) sec + 1].bar <= bar + 1.0e-9) ++sec;
        const double secStart = sec >= 0 ? score.sections[(size_t) sec].bar : 0.0;
        const double secEnd = sec >= 0 ? score.sectionEnd (sec) : (nSec > 0 ? score.sections[0].bar : (double) bars);

        float keep = 1.0f;
        for (int l = 0; l < 4; ++l)
        {
            const double L = lineBars[l];
            const float ph = (float) (std::fmod (bar, L) / L);
            keep *= 1.0f - lineW[l] * smoothstep ((ph - 0.75f) / 0.25f);
        }
        const double toEnd = secEnd - bar;
        if (secEnd < (double) bars + 0.5 && toEnd < 8.0) keep *= 1.0f - 0.9f * smoothstep ((float) (1.0 - toEnd / 8.0));
        T[(size_t) i] = clamp01 ((1.0f - keep) * pp.tensionDepth);

        // the last beat before a line of the turn level
        const int stepInBar = i % kStepsPerBar;
        const int barIdx = i / kStepsPerBar;
        bool turn = false;
        if (stepInBar >= 12)
        {
            const int next = barIdx + 1;
            for (int l = turnLv; l < 4; ++l) if (next % lineBars[l] == 0) turn = true;
            for (const auto& s : score.sections) if ((int) std::round (s.bar) == next && next > 0) turn = true;
        }
        U[(size_t) i] = turn ? 1.0f : 0.0f;
        uint8_t lv = 0;
        if (stepInBar == 0)
        {
            for (int l = 0; l < 4; ++l) if (barIdx % lineBars[l] == 0) lv = (uint8_t) (l + 1);
            for (const auto& s : score.sections) if ((int) std::round (s.bar) == barIdx) lv = 5;
        }
        plan.turnLevel[(size_t) i] = lv;
        if (stepInBar == 0) plan.barSection[(size_t) barIdx] = (int16_t) sec;

        const float p = (float) ((bar - secStart) / std::max (1.0, secEnd - secStart));
        A[(size_t) i] = p < 0.62f ? smoothstep (p / 0.62f) : smoothstep ((1.0f - p) / 0.38f);

        const double x = bar / driftPeriod;
        const auto k = (uint32_t) (int64_t) std::floor (x);
        const float f = smoothstep ((float) (x - std::floor (x)));
        const float h0 = hash01 (pp.seed ^ 0xD1F7u, k), h1 = hash01 (pp.seed ^ 0xD1F7u, k + 1);
        R[(size_t) i] = h0 + (h1 - h0) * f;
    }

    const float bmax = std::max (1.0f, pp.budgetMax);
    for (int i = 0; i < steps; ++i)
    {
        const float e = E[(size_t) i], t = T[(size_t) i];
        D[(size_t) i] = e * (1.0f - H[(size_t) i]);
        X[(size_t) i] = clamp01 (0.62f * e + 0.55f * t - 0.08f);
        C[(size_t) i] = 1.0f - e;
        // the budget holds for each 8-bar block: read at the block's start, so an edit plan never changes mid-phrase
        const int blockStart = (i / (8 * kStepsPerBar)) * 8 * kStepsPerBar;
        const int probe = std::min (steps - 1, blockStart + 7 * kStepsPerBar);   // the turn the block ends on
        const float bv = clamp01 (0.45f * E[(size_t) blockStart] + 0.55f * T[(size_t) probe]);
        B[(size_t) i] = std::round (bv * bmax) / bmax;
    }

    // ---- the slots
    for (int s = 0; s < kSlots; ++s)
    {
        const auto& c = pp.slots[(size_t) s];
        auto& o = plan.out[(size_t) s];
        plan.roles[(size_t) s] = (uint8_t) (c.on ? c.role : 0);
        const auto& in = plan.src[(size_t) std::clamp (c.source, 0, kNumSources - 1)];
        const float lagSteps = std::max (0.0f, c.smoothBeats) * 4.0f;
        const float a = lagSteps > 0.01f ? std::exp (-1.0f / lagSteps) : 0.0f;
        float y = shapeValue (in[0], c);
        for (int i = 0; i < steps; ++i)
        {
            const float v = shapeValue (in[(size_t) i], c);
            y = a > 0.0f ? v + (y - v) * a : v;
            o[(size_t) i] = c.on ? y : 0.0f;
        }
    }

    // ---- what the plan does, for the editor and the tests
    double se = 0.0, ss = 0.0, sh = 0.0;
    int runs = 0;
    for (int i = 0; i < steps; ++i)
    {
        se += E[(size_t) i];
        ss += S[(size_t) i];
        sh += H[(size_t) i];
        if (H[(size_t) i] > 0.5f && (i == 0 || H[(size_t) i - 1] < 0.5f)) ++runs;
    }
    plan.meanEnergy = (float) (se / steps);
    plan.targetSpace = (float) (ss / steps);
    plan.plannedSpace = (float) (sh / steps);
    plan.holes = runs;
    plan.meanHoleBars = runs > 0 ? (float) (sh / runs / kStepsPerBar) : 0.0f;
}

} // namespace epoch
