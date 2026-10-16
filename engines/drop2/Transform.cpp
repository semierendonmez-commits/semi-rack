#include "Transform.h"
#include <set>

namespace dr2
{

namespace
{
    int grid16 (double t) { return (int) std::floor (t * 4.0 + 0.5); }

    float jaccard (const std::set<long>& a, const std::set<long>& b, bool& present)
    {
        present = ! a.empty() || ! b.empty();
        if (! present) return 1.0f;
        int inter = 0;
        for (auto v : a) if (b.count (v)) ++inter;
        const int uni = (int) a.size() + (int) b.size() - inter;
        return uni > 0 ? (float) inter / (float) uni : 1.0f;
    }

    // one bass voice at a time: sort, merge notes on the same start, cut overlaps
    void monophonic (Clip& c)
    {
        c.sort();
        std::vector<Note> out;
        for (const auto& n : c.notes)
        {
            if (! out.empty() && std::abs (out.back().start - n.start) < 1.0e-6)
            {
                if (n.velocity > out.back().velocity) { auto tag = std::max (out.back().tag, n.tag); out.back() = n; out.back().tag = tag; }
                continue;
            }
            out.push_back (n);
        }
        for (size_t i = 0; i + 1 < out.size(); ++i)
        {
            const double room = out[i + 1].start - out[i].start;
            if (out[i].length > room - 0.005) out[i].length = std::max (0.0625, room - 0.005);
        }
        if (! out.empty())
        {
            auto& last = out.back();
            if (last.start + last.length > c.length) last.length = std::max (0.0625, c.length - last.start);
        }
        c.notes = std::move (out);
    }

    bool firstInBar (const Clip& c, size_t idx)
    {
        const int bar = (int) std::floor (c.notes[idx].start / 4.0 + 1.0e-9);
        for (size_t k = 0; k < c.notes.size(); ++k)
            if (k != idx && (int) std::floor (c.notes[k].start / 4.0 + 1.0e-9) == bar && c.notes[k].start < c.notes[idx].start - 1.0e-9)
                return false;
        return true;
    }
}

float similarity (const Clip& dA, const Clip& bA, const Clip& dB, const Clip& bB, const DrumRoles& roles)
{
    std::set<long> k[2], s[2], h[2], p[2], b[2];
    const Clip* drums[2] { &dA, &dB };
    const Clip* bass[2] { &bA, &bB };
    for (int i = 0; i < 2; ++i)
    {
        for (const auto& n : drums[i]->notes)
        {
            const long g = grid16 (n.start);
            switch (roles.of (n.pitch))
            {
                case Role::Kick:  k[i].insert (g); break;
                case Role::Snare: s[i].insert (g); break;
                case Role::Hat:   h[i].insert (g); break;
                default:          p[i].insert (g * 128 + n.pitch); break;
            }
        }
        for (const auto& n : bass[i]->notes) b[i].insert (grid16 (n.start) * 128L + n.pitch);
    }
    float sum = 0.0f, wsum = 0.0f;
    bool present = false;
    const std::pair<float, int> parts[] { { 0.30f, 0 }, { 0.25f, 1 }, { 0.10f, 2 }, { 0.05f, 3 }, { 0.30f, 4 } };
    std::set<long>* sets[5][2] { { &k[0], &k[1] }, { &s[0], &s[1] }, { &h[0], &h[1] }, { &p[0], &p[1] }, { &b[0], &b[1] } };
    for (auto [w, i] : parts)
    {
        const float j = jaccard (*sets[i][0], *sets[i][1], present);
        if (! present) continue;
        sum += w * j;
        wsum += w;
    }
    return wsum > 0.0f ? sum / wsum : 1.0f;
}

Result generate (const Clip& drums, const Clip& bass, const Settings& s, std::uint32_t seed)
{
    Result r;
    r.drums = drums;
    r.bassA = bass;
    r.bassB.length = bass.length;
    for (auto& n : r.drums.notes) n.tag = Same;
    for (auto& n : r.bassA.notes) n.tag = Same;
    r.drums.sort();
    r.bassA.sort();

    const auto roles = detectRoles (drums);
    const float id = std::clamp (s.identity, 0.0f, 1.0f);
    const bool protect = id >= 0.25f;
    auto prob = [id] (float amount) { return std::clamp ((1.0f - id) * amount * 1.3f, 0.0f, 1.0f); };
    Rng rng (seed);

    // ---- HALF-TIME: the bars from the switch point on -------------------------------------
    if (s.halftime)
    {
        const double L = drums.length;
        const int bars = drums.bars();
        const int k0 = std::clamp ((int) std::floor (id * (float) bars + 0.5f), 0, bars);
        const double from = k0 * 4.0, span = L - from;
        if (span > 0.0)
        {
            auto stretched = [&] (Role role) { return role == Role::Kick || role == Role::Snare || role == Role::Perc || (role == Role::Hat && ! s.halftimeHats); };
            std::vector<Note> keep;
            for (const auto& n : r.drums.notes)
                if (n.start < from - 1.0e-9 || ! stretched (roles.of (n.pitch))) keep.push_back (n);
            for (const auto& n : drums.notes)
                if (n.start < span * 0.5 - 1.0e-9 && stretched (roles.of (n.pitch)))
                {
                    Note m = n;
                    m.start = from + n.start * 2.0;
                    m.length = std::min (n.length * 2.0, 1.0);
                    m.tag = Changed;
                    keep.push_back (m);
                }
            r.drums.notes = std::move (keep);
            r.drums.sort();

            const double bfrom = std::min (from, bass.length), bspan = bass.length - bfrom;
            std::vector<Note> bkeep;
            for (const auto& n : r.bassA.notes) if (n.start < bfrom - 1.0e-9) bkeep.push_back (n);
            for (const auto& n : bass.notes)
                if (n.start < bspan * 0.5 - 1.0e-9)
                {
                    Note m = n;
                    m.start = bfrom + n.start * 2.0;
                    m.length = n.length * 2.0;
                    m.tag = Changed;
                    bkeep.push_back (m);
                }
            r.bassA.notes = std::move (bkeep);
            monophonic (r.bassA);
        }
    }

    // ---- KICK SHIFT -----------------------------------------------------------------------
    if (s.kickShift)
    {
        const float p = prob (s.kickAmount);
        const double L = r.drums.length;
        for (size_t i = 0; i < r.drums.notes.size(); ++i)
        {
            auto& n = r.drums.notes[i];
            if (roles.of (n.pitch) != Role::Kick) continue;
            if (protect && near (wrapBeat (n.start), 0.0)) continue;   // the one stays
            if (rng.uniform() >= p) continue;
            for (int tries = 0; tries < 8; ++tries)
            {
                const int k = 1 + rng.below (std::max (1, s.kickMax));
                const double dir = rng.uniform() < 0.6f ? 1.0 : -1.0;
                const double t = n.start + dir * k * 0.25;
                if (t < 0.0 || t >= L - 1.0e-9) continue;
                if (protect && near (wrapBeat (t), 0.0)) continue;
                bool clash = false;
                for (size_t j = 0; j < r.drums.notes.size() && ! clash; ++j)
                {
                    if (j == i) continue;
                    const auto& o = r.drums.notes[j];
                    const auto ro = roles.of (o.pitch);
                    if (ro == Role::Snare && std::abs (o.start - t) < 0.12) clash = true;
                    if (ro == Role::Kick && std::abs (o.start - t) < 0.12) clash = true;
                }
                if (clash) continue;
                n.start = t;
                n.tag = Changed;
                break;
            }
        }
        r.drums.sort();
    }

    // ---- BASS LOCK --------------------------------------------------------------------------
    if (s.bassLock && ! r.bassA.notes.empty())
    {
        std::vector<double> targets;
        for (const auto& n : r.drums.notes)
        {
            const auto ro = roles.of (n.pitch);
            if (ro == Role::Kick || (s.lockTo == 1 && ro == Role::Snare)) targets.push_back (n.start);
        }
        std::sort (targets.begin(), targets.end());
        const float p = prob (s.lockAmount);
        for (size_t i = 0; i < r.bassA.notes.size(); ++i)
        {
            auto& n = r.bassA.notes[i];
            if (protect && firstInBar (r.bassA, i)) continue;
            if (rng.uniform() >= p) continue;
            double best = 1.0e9;
            for (double t : targets) if (std::abs (t - n.start) < std::abs (best - n.start)) best = t;
            if (std::abs (best - n.start) <= 0.5 + 1.0e-9 && std::abs (best - n.start) > 1.0e-6)
            {
                n.start = best;
                n.tag = Changed;
            }
        }
        monophonic (r.bassA);
    }

    // ---- FLIP -------------------------------------------------------------------------------
    if (s.flip && ! r.bassA.notes.empty())
    {
        int root = 127;
        for (const auto& n : r.bassA.notes) root = std::min (root, n.pitch);
        const float p = prob (s.flipAmount);
        const int bars = r.bassA.bars();
        for (int b = 0; b < bars; ++b)
        {
            if (protect && b == 0) continue;
            if (s.flipMode == 2 && (b % 2) == 0) continue;
            const float pb = s.flipMode == 2 ? std::min (1.0f, p * 1.5f) : p;
            if (rng.uniform() >= pb) continue;
            for (auto& n : r.bassA.notes)
            {
                if ((int) std::floor (n.start / 4.0 + 1.0e-9) != b) continue;
                int q = n.pitch;
                switch (s.flipMode)
                {
                    case 0: q += 12; break;
                    case 1: q -= 12; break;
                    case 2: q += 12; break;
                    default:
                        q = 2 * root - n.pitch;
                        while (q < root - 5) q += 12;
                        break;
                }
                while (q < 12) q += 12;
                while (q > 108) q -= 12;
                if (q != n.pitch) { n.pitch = q; n.tag = Changed; }
            }
        }
    }

    // ---- CALL / RESPONSE ---------------------------------------------------------------------
    if (s.split)
    {
        const double unit = kUnits[std::clamp (s.splitUnit, 0, 2)];
        std::vector<Note> call;
        for (auto n : r.bassA.notes)
        {
            const int u = (int) std::floor (n.start / unit + 1.0e-9);
            if (u % 2 == 1)
            {
                n.pitch = std::clamp (n.pitch + s.respInterval, 0, 127);
                if (s.respInterval != 0) n.tag = Changed;
                r.bassB.notes.push_back (n);
            }
            else call.push_back (n);
        }
        r.bassA.notes = std::move (call);
    }

    // ---- what is left of drop 1
    Clip both = r.bassA;
    both.notes.insert (both.notes.end(), r.bassB.notes.begin(), r.bassB.notes.end());
    const Clip none;
    r.identity = similarity (drums, bass, r.drums, both, roles);
    r.idDrums = similarity (drums, none, r.drums, none, roles);
    r.idBass = similarity (none, bass, none, both, roles);
    return r;
}

} // namespace dr2
