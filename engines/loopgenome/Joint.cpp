// semi-rack: vendored; namespaces wrapped in dnafam:: (see sources.mk for every change).
#include "Joint.h"

#include <algorithm>
#include <cstdint>
#include <cmath>

namespace dnafam::lg
{

int skeletonOf (const geno::StepInfo& s, int mode)
{
    const int k = (s.mask & (1u << geno::LKick)) ? 1 : 0;
    const int sn = (s.mask & kSnareLanes) ? 2 : 0;
    return mode == SkelKickSnare ? (k | sn) : mode == SkelKick ? k : 0;
}

int drumSym (const geno::StepInfo& s)
{
    if (s.mask & (1u << geno::LKick)) return DKick;
    if (s.mask & kSnareLanes) return DSnare;
    return s.mask != 0 ? DOther : DRest;
}

int bassSym (const BassCell& c)
{
    if (c.type == BRest) return BSRest;
    if (c.type == BTie) return BSTie;
    return ((c.semis % 12) + 12) % 12 == 0 ? BSRoot : BSOther;
}

namespace
{
inline int skelMask (int mode) { return mode == SkelKickSnare ? 3 : mode == SkelKick ? 1 : 0; }

float metric (int j, int e)
{
    float m = (j % 4 == e % 4) ? 1.0f : (j % 2 == e % 2 ? 0.03f : 0.005f);
    if (j % 8 == e % 8) m *= 1.6f;
    if (j % 16 == e % 16) m *= 1.4f;
    return m;
}

float drumsAlike (const geno::StepInfo& x, const geno::StepInfo& y)
{
    if (x.mask == 0 && y.mask == 0) return 1.0f;
    if (x.mask == 0 || y.mask == 0) return 0.0f;
    const int inter = __builtin_popcount ((unsigned) (x.mask & y.mask));
    const int uni = __builtin_popcount ((unsigned) (x.mask | y.mask));
    const float jac = (float) inter / (float) std::max (1, uni);
    const float lv = 1.0f - std::min (1.0f, std::abs (x.level - y.level) * 1.5f);
    return 0.5f * jac + 0.3f * (x.main == y.main ? 1.0f : 0.0f) + 0.2f * lv;
}

float bassAlike (const BassCell& x, const BassCell& y)
{
    if (x.type == BRest && y.type == BRest) return 1.0f;
    if ((x.type == BRest) != (y.type == BRest)) return 0.0f;
    float s = x.type == y.type ? 0.5f : 0.2f;
    if (x.semis == y.semis) s += 0.35f;
    else if (((x.semis - y.semis) % 12 + 12) % 12 == 0) s += 0.2f;
    s += 0.15f * (1.0f - std::min (1.0f, std::abs ((float) x.vel - (float) y.vel) / 60.0f));
    return s;
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
            const float v = m[(size_t) x][(size_t) y] * 100.0f / mx;
            m[(size_t) x][(size_t) y] = v < 2.0f ? 0.0f : std::round (v);
        }
    }
}
} // namespace

Joint learnJoint (const geno::Learned& L, const BassLine& bassIn, int skeleton, float couple)
{
    Joint J;
    const int n = std::clamp (L.steps, 1, kMaxSteps);
    J.steps = n;
    J.bass = fit (bassIn, n);
    for (int s = 0; s < n; ++s)
    {
        J.skel[(size_t) s] = (uint8_t) skeletonOf (L.a[(size_t) s], SkelKickSnare);
        J.dsym[(size_t) s] = (int8_t) drumSym (L.a[(size_t) s]);
        J.bsym[(size_t) s] = (int8_t) bassSym (J.bass.cells[(size_t) s]);
    }
    auto sym = [&] (int s) { return J.dsym[(size_t) s] * NumBassSyms + J.bsym[(size_t) s]; };

    // the joint statistics, Laplace-smoothed
    for (auto& r : J.stat) for (auto& v : r) v = 0.1f;
    for (int s = 0; s < n; ++s) J.stat[sym (s)][sym ((s + 1) % n)] += 1.0f;
    for (auto& r : J.stat)
    {
        float t = 0.0f;
        for (float v : r) t += v;
        for (auto& v : r) v /= t;
    }
    // what the bass does on each kind of drum step
    for (auto& r : J.coupling) for (auto& v : r) v = 0.0f;
    std::array<float, NumDrumSyms> dc {};
    for (int s = 0; s < n; ++s) { J.coupling[J.dsym[(size_t) s]][J.bsym[(size_t) s]] += 1.0f; dc[(size_t) J.dsym[(size_t) s]] += 1.0f; }
    for (int d = 0; d < NumDrumSyms; ++d)
        for (int b = 0; b < NumBassSyms; ++b) J.coupling[d][b] = dc[(size_t) d] > 0.0f ? J.coupling[d][b] / dc[(size_t) d] : 0.0f;

    // readouts
    int onsets = 0, onKick = 0, kicks = 0, kickOn = 0, snares = 0, snareSound = 0, bars = 0, pick = 0;
    for (int s = 0; s < n; ++s)
    {
        const bool hit = J.bass.cells[(size_t) s].type == BHit, kick = (J.skel[(size_t) s] & 1) != 0, sn = (J.skel[(size_t) s] & 2) != 0;
        onsets += hit;
        onKick += hit && kick;
        kicks += kick;
        kickOn += kick && hit;
        snares += sn;
        snareSound += sn && J.bass.cells[(size_t) s].type != BRest;
    }
    for (int b = 0; b * 16 < n; ++b)
    {
        ++bars;
        bool any = false;
        for (int q = b * 16 + 12; q < b * 16 + 16 && q < n; ++q) any = any || J.bass.cells[(size_t) q].type == BHit;
        pick += any;
    }
    J.onsetsOnKick = onsets > 0 ? (float) onKick / (float) onsets : 0.0f;
    J.kicksWithOnset = kicks > 0 ? (float) kickOn / (float) kicks : 0.0f;
    J.bassOnSnare = snares > 0 ? (float) snareSound / (float) snares : 0.0f;
    J.pickups = bars > 0 ? (float) pick / (float) bars : 0.0f;

    // the learned walk
    const int sm = skelMask (skeleton);
    const float cp = std::clamp (couple, 0.0f, 1.0f);
    auto& out = J.fromJoint;
    for (auto& r : out) r.fill (0.0f);
    out[0][1] = 100.0f;
    for (int i = 0; i < n; ++i)
    {
        const int e = (i + 1) % n;
        const int si = sym (i);
        for (int j = 0; j < n; ++j)
        {
            if ((J.skel[(size_t) j] & sm) != (J.skel[(size_t) e] & sm)) continue;
            float w = metric (j, e) * (0.15f + J.stat[si][sym (j)]) * (0.25f + drumsAlike (L.a[(size_t) e], L.a[(size_t) j]));
            if (cp > 0.0f) w *= std::pow (0.25f + bassAlike (J.bass.cells[(size_t) e], J.bass.cells[(size_t) j]), cp * 1.5f);
            // a held cell right after silence would open a note that never started
            if (J.bass.cells[(size_t) j].type == BTie && J.bass.cells[(size_t) i].type == BRest) w *= 1.0f - 0.75f * cp;
            out[(size_t) i + 1][(size_t) j + 1] = w;
        }
    }
    normaliseRows (out, n);
    return J;
}

void maskSkeleton (Plain& m, const Joint& J, int skeleton)
{
    const int sm = skelMask (skeleton);
    const int n = J.steps;
    if (sm == 0 || n <= 0) return;
    for (int i = 1; i <= n; ++i)
    {
        const int e = i % n;    // row i = step i - 1, the loop would go to step i (0-based i % n)
        bool any = false;
        for (int j = 1; j <= n; ++j)
        {
            if ((J.skel[(size_t) j - 1] & sm) != (J.skel[(size_t) e] & sm)) m[(size_t) i][(size_t) j] = 0.0f;
            any = any || m[(size_t) i][(size_t) j] > 0.0f;
        }
        m[(size_t) i][0] = 0.0f;
        if (! any) m[(size_t) i][(size_t) e + 1] = 100.0f;
    }
}

void deriveJoint (const Plain& genome, const geno::Learned& L, const Joint& J, const geno::DeriveParams& p, int skeleton,
                  Plain& call, Plain& resp, Plain& fill)
{
    geno::derive (genome, L, p, call, resp, fill);
    maskSkeleton (call, J, skeleton);
    maskSkeleton (resp, J, skeleton);
    normaliseRows (call, J.steps);
    normaliseRows (resp, J.steps);
}

std::array<int, kMaxSteps> artMap (const Joint& J, int map)
{
    std::array<int, kMaxSteps> a {};
    const int n = std::max (1, J.steps);
    int note = -1;
    for (int s = 0; s < n; ++s)
    {
        const auto& c = J.bass.cells[(size_t) s];
        if (c.type == BHit) ++note;
        switch (map)
        {
            case MapHalves: a[(size_t) s] = s < (n + 1) / 2 ? 1 : 2; break;
            case MapBars:   a[(size_t) s] = 1 + (s / 16) % 4; break;
            case MapNotes:  a[(size_t) s] = c.type == BHit ? 1 + note % 4 : 0; break;
            case MapDrums:  a[(size_t) s] = c.type == BHit ? 1 + std::clamp ((int) J.dsym[(size_t) s], 0, 3) : 0; break;
            default:        a[(size_t) s] = 1; break;
        }
    }
    return a;
}

} // namespace dnafam::lg
