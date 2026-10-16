// semi-rack: vendored; namespaces wrapped in dnafam:: (see sources.mk for every change).
#include "Learn.h"

#include <algorithm>
#include <cstdint>
#include <cmath>

namespace dnafam::geno
{

int laneOf (const dna::Hit& h, const std::vector<dna::Features>& f)
{
    const dna::Features x = h.slice >= 0 && h.slice < (int) f.size() ? f[(size_t) h.slice] : dna::Features {};
    switch (h.cls)
    {
        case dna::Kick:  return LKick;
        case dna::Ghost: return LGhost;
        case dna::Snare:
            // a "snare" with little crack and air and a lot of body: a tom, a conga, a clap's body
            return (x.crack + x.air < 0.30f && x.body > 0.45f) ? LPerc : LSnare;
        case dna::Hat:
        default:
            if (x.decay > 0.42f && x.air > 0.35f) return LRide;
            if (x.decay > 0.15f) return LOpen;
            return LHat;
    }
}

namespace
{
void steps (const dna::Genome& g, const std::vector<uint8_t>& lanes, std::array<StepInfo, kMaxSteps>& out, int n)
{
    out.fill ({});
    for (size_t i = 0; i < g.hits.size(); ++i)
    {
        const auto& h = g.hits[i];
        if (h.step < 0 || h.step >= n) continue;
        auto& s = out[(size_t) h.step];
        const int l = lanes[i];
        s.mask = (uint16_t) (s.mask | (1u << l));
        if (s.main < 0 || h.level > s.level) { s.main = (int8_t) l; s.level = h.level; }
    }
}

int sym (const StepInfo& s) { return s.main >= 0 ? s.main : kRest; }

void stats (const std::array<StepInfo, kMaxSteps>& st, int n, float out[kSyms][kSyms])
{
    for (int a = 0; a < kSyms; ++a) for (int b = 0; b < kSyms; ++b) out[a][b] = 0.25f;   // Laplace
    for (int s = 0; s < n; ++s) out[sym (st[(size_t) s])][sym (st[(size_t) ((s + 1) % n)])] += 1.0f;
    for (int a = 0; a < kSyms; ++a)
    {
        float t = 0.0f;
        for (int b = 0; b < kSyms; ++b) t += out[a][b];
        for (int b = 0; b < kSyms; ++b) out[a][b] /= t;
    }
}

// how alike two steps are: shared lanes (Jaccard), similar loudness; two rests are alike
float similarity (const StepInfo& x, const StepInfo& y)
{
    if (x.mask == 0 && y.mask == 0) return 1.0f;
    if (x.mask == 0 || y.mask == 0) return 0.0f;
    const int inter = __builtin_popcount ((unsigned) (x.mask & y.mask));
    const int uni = __builtin_popcount ((unsigned) (x.mask | y.mask));
    const float j = (float) inter / (float) std::max (1, uni);
    const float mainSame = x.main == y.main ? 1.0f : 0.0f;
    const float lv = 1.0f - std::min (1.0f, std::abs (x.level - y.level) * 1.5f);
    return 0.5f * j + 0.3f * mainSame + 0.2f * lv;
}

float metric (int j, int e)
{
    float m = (j % 4 == e % 4) ? 1.0f : (j % 2 == e % 2 ? 0.03f : 0.005f);
    if (j % 8 == e % 8) m *= 1.6f;
    if (j % 16 == e % 16) m *= 1.4f;
    return m;
}

void normaliseRows (Plain& m, int n)
{
    for (int x = 0; x <= n; ++x)
    {
        float mx = 0.0f;
        for (int y = 0; y <= n; ++y) mx = std::max (mx, m[(size_t) x][(size_t) y]);
        if (mx <= 0.0f) continue;
        for (int y = 0; y <= n; ++y)
        {
            float v = m[(size_t) x][(size_t) y] * 100.0f / mx;
            m[(size_t) x][(size_t) y] = v < 2.0f ? 0.0f : std::round (v);
        }
    }
}

// the walk on A's steps, with the statistics st, the step that would come next taken from
// the pattern `next` (A itself, or B on A's positions)
void build (const std::array<StepInfo, kMaxSteps>& a, int n, const float st[kSyms][kSyms],
            const std::array<StepInfo, kMaxSteps>& next, int nNext, Plain& out)
{
    for (auto& r : out) r.fill (0.0f);
    out[0][1] = 100.0f;   // S -> step 1
    for (int i = 0; i < n; ++i)
    {
        const int e = (i + 1) % n;
        const auto& want = next[(size_t) (e % std::max (1, nNext))];
        const int si = sym (a[(size_t) i]);
        for (int j = 0; j < n; ++j)
        {
            const float w = metric (j, e) * (0.15f + st[si][sym (a[(size_t) j])]) * (0.25f + similarity (want, a[(size_t) j]));
            out[(size_t) i + 1][(size_t) j + 1] = w;
        }
    }
    normaliseRows (out, n);
}
} // namespace

Learned learn (const dna::Kit& k)
{
    Learned L;
    L.steps = std::clamp (k.g.steps, 1, kMaxSteps);
    L.stepsB = std::clamp (k.gb.steps, 1, kMaxSteps);
    L.hitLane.resize (k.g.hits.size());
    for (size_t i = 0; i < k.g.hits.size(); ++i) L.hitLane[i] = (uint8_t) laneOf (k.g.hits[i], k.fa);
    std::vector<uint8_t> laneB (k.gb.hits.size());
    for (size_t i = 0; i < k.gb.hits.size(); ++i) laneB[i] = (uint8_t) laneOf (k.gb.hits[i], k.fb);
    steps (k.g, L.hitLane, L.a, L.steps);
    for (int s = 0; s < L.steps; ++s)
    {
        L.stepStart[(size_t) s] = (int) L.hitsByStep.size();
        for (int i = 0; i < (int) k.g.hits.size(); ++i)
            if (k.g.hits[(size_t) i].step == s) L.hitsByStep.push_back (i);
    }
    for (int s = L.steps; s <= kMaxSteps; ++s) L.stepStart[(size_t) s] = (int) L.hitsByStep.size();
    steps (k.gb, laneB, L.b, L.stepsB);
    stats (L.a, L.steps, L.statA);
    stats (L.b, L.stepsB, L.statB);
    build (L.a, L.steps, L.statA, L.a, L.steps, L.fromA);
    // B's next steps on A's positions: scale B's loop onto A's
    std::array<StepInfo, kMaxSteps> bOnA {};
    for (int s = 0; s < L.steps; ++s) bOnA[(size_t) s] = L.b[(size_t) ((s * L.stepsB) / L.steps % L.stepsB)];
    build (L.a, L.steps, L.statB, bOnA, L.steps, L.fromB);
    return L;
}

void linear (Plain& m, int n)
{
    for (auto& r : m) r.fill (0.0f);
    for (int x = 0; x <= n; ++x) m[(size_t) x][(size_t) (x < n ? x + 1 : 1)] = 100.0f;
}

void mix (const Plain& a, const Plain& b, float x, int n, Plain& out)
{
    x = std::clamp (x, 0.0f, 1.0f);
    for (int i = 0; i <= n; ++i)
        for (int j = 0; j <= n; ++j)
            out[(size_t) i][(size_t) j] = (1.0f - x) * a[(size_t) i][(size_t) j] + x * b[(size_t) i][(size_t) j];
    normaliseRows (out, n);
}

void temper (Plain& m, float temp, int n)
{
    temp = std::clamp (temp, 0.1f, 10.0f);
    if (std::abs (temp - 1.0f) < 1.0e-3f) return;
    const float e = 1.0f / temp;
    for (int x = 0; x <= n; ++x)
        for (int y = 0; y <= n; ++y)
        {
            auto& v = m[(size_t) x][(size_t) y];
            if (v > 0.0f) v = 100.0f * std::pow (v / 100.0f, e);
        }
    normaliseRows (m, n);
}

void derive (const Plain& genome, const Learned& L, const DeriveParams& p, Plain& call, Plain& resp, Plain& fill)
{
    const int n = std::max (1, L.steps);
    Plain lin {}, g = genome;
    linear (lin, n);
    temper (g, p.temp, n);
    // the start state always goes to step 1; the mix keeps it
    mix (lin, g, p.mutation, n, call);

    // ---- response
    for (auto& r : resp) r.fill (0.0f);
    switch (p.answer)
    {
        case AnsShift:
            for (int i = 0; i <= n; ++i)
                for (int j = 1; j <= n; ++j)
                {
                    const int t = ((j - 1 + p.shift) % n + n) % n + 1;
                    resp[(size_t) i][(size_t) t] = call[(size_t) i][(size_t) j];
                }
            break;
        case AnsInvert:
            for (int i = 1; i <= n; ++i)
            {
                const int e = i % n;   // step (0-based) that would come next after step i-1
                for (int j = 0; j < n; ++j)
                    if (j % 4 == e % 4) resp[(size_t) i][(size_t) j + 1] = 100.0f - call[(size_t) i][(size_t) j + 1] + 4.0f;
            }
            resp[0][1] = 100.0f;
            break;
        case AnsMirror:
            for (int i = 1; i <= n; ++i)
                for (int j = 1; j <= n; ++j)
                    resp[(size_t) i][(size_t) j] = call[(size_t) (n + 1 - i)][(size_t) (n + 1 - j)];
            resp[0][(size_t) n] = 100.0f;
            break;
        case AnsEcho:
        default:
            resp = call;
            break;
    }
    temper (resp, p.answerTemp, n);
    normaliseRows (resp, n);

    // ---- fill: snare / ghost steps repeat (a roll), the rest heads for the nearest one
    for (auto& r : fill) r.fill (0.0f);
    const float roll = std::clamp (p.roll, 0.0f, 1.0f);
    auto isRoll = [&] (int s) { const auto m = L.a[(size_t) s].mask; return (m & ((1u << LSnare) | (1u << LGhost))) != 0; };
    bool any = false;
    for (int s = 0; s < n; ++s) any = any || isRoll (s);
    for (int i = 0; i <= n; ++i)
    {
        for (int j = 0; j <= n; ++j) fill[(size_t) i][(size_t) j] = (1.0f - roll) * call[(size_t) i][(size_t) j];
        if (! any) continue;
        const int s = i - 1;
        if (s >= 0 && isRoll (s)) fill[(size_t) i][(size_t) i] += roll * 100.0f;
        else
        {
            // the next snare / ghost step after this one
            for (int d = 1; d <= n; ++d)
            {
                const int t = ((s < 0 ? 0 : s) + d) % n;
                if (isRoll (t)) { fill[(size_t) i][(size_t) t + 1] += roll * 100.0f; break; }
            }
        }
    }
    normaliseRows (fill, n);
}

Role roleAt (double pos16, int form, int ending)
{
    const double phrase = 16.0 * formBars (form);
    double p = std::fmod (pos16, phrase);
    if (p < 0.0) p += phrase;
    const int et = endingTicks (ending);
    if (et > 0 && p >= phrase - (double) et - 1.0e-6) return RoleFill;
    return p < 16.0 * formCallBars (form) - 1.0e-6 ? RoleCall : RoleResponse;
}

} // namespace dnafam::geno
