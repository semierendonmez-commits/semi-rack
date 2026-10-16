// Vendored into SemiFamily from semierendonmez-commits/quanta Source/engine; toString / fromString
// rewritten without <sstream> (marked SemiFamily). Everything else unchanged.
#include "Arrange.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace qn
{
const char* blockTypeName (int t)
{
    static const char* n[] = { "SINE", "PULSE", "DATA", "NOISE", "CLOUD" };
    return t >= 0 && t < kNumBlockTypes ? n[t] : "--";
}
const char* regName (int r) { static const char* n[] = { "LOW", "MID", "HIGH" }; return n[std::clamp (r, 0, 2)]; }
const char* formName (int f)
{
    static const char* n[] = { "ACCUMULATE", "ALTERNATE", "PERMUTE", "CODEX", "MARKOV", "ARC" };
    return n[std::clamp (f, 0, kNumForms - 1)];
}
const char* fillName (int f)
{
    static const char* n[] = { "--", "ROLL", "BREAK", "RISE", "DROPOUT", "REPEAT", "REVERSE", "TAPE", "SWEEP", "FILTER", "CRUSH", "THROW" };
    return n[std::clamp (f, 0, kNumFills - 1)];
}

std::array<ChannelDef, kLanes> defaultChannels()
{
    return { { { BPulse, RegLow }, { BPulse, RegHigh }, { BData, RegMid }, { BData, RegHigh },
               { BNoise, RegMid }, { BSine, RegLow }, { BSine, RegHigh }, { BCloud, RegMid } } };
}
const char* defaultChannelName (int ch)
{
    static const char* n[] = { "KICK", "CLICK", "DATA A", "DATA B", "NOISE", "SINE LO", "SINE HI", "CLOUD" };
    return n[std::clamp (ch, 0, kLanes - 1)];
}

Scene factoryScene (int t, int reg)
{
    Scene s;
    s.jitTime = s.jitPitch = s.jitAmp = 0; s.mask = 0; s.burst = 0xffff; s.fineCents = 0; s.glide = 0;
    switch (t)
    {
        case BSine:   // overlapping phase-coherent Hann grains = a steady pure tone
            s.type = Grain; s.density = 10; s.sync = 0; s.durMs = 200; s.wave = Sine; s.window = Hann; s.spread = 0; break;
        case BPulse:  // grainlets: a 4 kHz pulse is a short click, 50 Hz a kick
            s.type = Grainlet; s.cycles = reg == RegLow ? 5.0f : 6.0f; s.sync = 3; s.wave = Sine; s.window = Expodec; s.spread = reg == RegLow ? 0.0f : 40.0f; break;
        case BData:   // pulsars on a grid, masked by the lane's bit pattern
            s.type = Pulsar; s.sync = reg == RegLow ? 3 : 4; s.duty = reg == RegHigh ? 15.0f : (reg == RegLow ? 50.0f : 30.0f);
            s.wave = reg == RegHigh ? Square : Sine; s.window = Rect; s.spread = reg == RegHigh ? 60.0f : 30.0f; break;
        case BNoise:
            s.type = Grain; s.sync = 3; s.durMs = 30; s.wave = Noise; s.window = Rect; s.spread = 60; break;
        default:      // CLOUD
            s.type = Glisson; s.density = 300; s.sync = 0; s.durMs = 40; s.wave = Sine; s.window = Gauss;
            s.glide = 12; s.glideDir = RandomDir; s.jitTime = 100; s.jitPitch = 24; s.jitAmp = 60; s.spread = 100; break;
    }
    return s;
}

uint64_t GenParams::hash() const
{
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h] (uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); };
    mix ((uint64_t) form); mix ((uint64_t) lengthBars); mix ((uint64_t) blockBars); mix (pow2 ? 1 : 0); mix ((uint64_t) seed);
    mix ((uint64_t) mutation); mix ((uint64_t) std::lround (extremity * 10)); mix ((uint64_t) std::lround (silence * 10)); mix ((uint64_t) cutq);
    mix ((uint64_t) std::lround (fills * 10)); mix ((uint64_t) std::lround (fxMix * 10));
    for (auto& c : channels) { mix ((uint64_t) (c.role + 1)); mix ((uint64_t) c.reg); }
    return h | 1ull;
}

int Arrangement::blockAt (double beats, double* start) const
{
    double t = 0;
    for (int i = 0; i < (int) blocks.size(); ++i)
    {
        if (beats < t + blocks[(size_t) i].beats) { if (start) *start = t; return i; }
        t += blocks[(size_t) i].beats;
    }
    return -1;
}

// SemiFamily: printf / strtod instead of <sstream> (MetaModule has no iostreams). Same text format.
std::string Arrangement::toString() const
{
    std::string o;
    char b[64];
    std::snprintf (b, sizeof b, "v2 %llu %zu", (unsigned long long) genHash, blocks.size());
    o += b;
    for (auto& bl : blocks)
    {
        std::snprintf (b, sizeof b, " %g %d %g %d %g", bl.beats, bl.locked ? 1 : 0, (double) bl.energy, bl.fill, bl.fillBeats);
        o += b;
        for (auto& l : bl.lanes) { std::snprintf (b, sizeof b, " %d %g %u", l.type, (double) l.hz, (unsigned) l.pattern); o += b; }
    }
    return o;
}

Arrangement Arrangement::fromString (const std::string& s)
{
    Arrangement a;
    const char* p = s.c_str();
    char* end = nullptr;
    auto num = [&] (double& v) { v = std::strtod (p, &end); if (end == p) return false; p = end; return true; };
    while (*p == ' ') ++p;
    if (std::strncmp (p, "v2 ", 3) != 0) return {};
    p += 3;
    double n = 0;
    a.genHash = std::strtoull (p, &end, 10);
    if (end == p) return {};
    p = end;
    if (! num (n) || n < 0 || n > 4096) return {};
    for (size_t i = 0; i < (size_t) n; ++i)
    {
        Block b; double lk = 0, en = 0, fi = 0;
        if (! (num (b.beats) && num (lk) && num (en) && num (fi) && num (b.fillBeats))) return {};
        b.energy = (float) en; b.locked = lk != 0; b.fill = std::clamp ((int) fi, 0, kNumFills - 1);
        for (auto& l : b.lanes)
        {
            double t = 0, hz = 0, pt = 0;
            if (! (num (t) && num (hz) && num (pt))) return {};
            l.type = std::clamp ((int) t, -1, kNumBlockTypes - 1); l.hz = (float) hz; l.pattern = (uint16_t) (unsigned) pt;
        }
        b.beats = std::clamp (b.beats, 0.25, 4096.0);
        a.blocks.push_back (b);
    }
    return a;
}

// ---------------------------------------------------------------------------
namespace
{
struct Rng
{
    uint32_t s;
    explicit Rng (uint32_t seed) : s (seed * 2654435761u + 0x6d2b79f5u) { if (s == 0) s = 1; next(); next(); }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float u() { return (float) (next() >> 8) / 16777216.0f; }
    int i (int n) { return n <= 1 ? 0 : (int) (next() % (uint32_t) n); }
    bool p (float prob) { return u() < prob; }
    float logu (float lo, float hi) { return lo * std::pow (hi / lo, u()); }
};

int popcount (unsigned m) { int c = 0; while (m) { c += m & 1u; m >>= 1; } return c; }

uint16_t randomPattern (Rng& r, int minOn = 3, int maxOn = 13)
{
    for (;;)
    {
        const uint16_t p = (uint16_t) ((r.next() & r.next()) | (r.next() & 0x1111));
        const int c = popcount (p);
        if (c >= minOn && c <= maxOn) return p;
    }
}

uint16_t kickPattern (Rng& r)
{
    uint16_t p = 0x1111;                               // four on the floor (bit i = step i)
    const float u = r.u();
    if (u < 0.35f) p |= (uint16_t) (1u << (1 + r.i (15)));
    else if (u < 0.55f) p &= (uint16_t) ~(1u << (4 * (1 + r.i (3))));
    else if (u < 0.75f) { static const uint16_t broken[] = { 0x0921, 0x0491, 0x2111, 0x1081, 0x4211 }; p = broken[r.i (5)]; }
    return p;
}

float pickHz (int role, int reg, Rng& r, float extremity)
{
    if (role == BNoise) return 1000.0f;
    if (role == BCloud) return reg == RegLow ? r.logu (80.0f, 600.0f) : (reg == RegHigh ? r.logu (2000.0f, 8000.0f) : r.logu (200.0f, 3000.0f));
    if (role == BPulse && reg == RegLow) return r.logu (45.0f, 62.0f);   // kicks stay kicks
    if (r.p (extremity * 0.01f))
    {
        const bool low = reg == RegLow || (reg == RegMid && r.p (0.5f));
        return low ? r.logu (25.0f, 60.0f) : r.logu (9000.0f, 16000.0f);
    }
    static const float R[3][3][2] = {   // [role SINE PULSE DATA][reg][lo, hi]
        { { 40, 150 }, { 300, 2000 }, { 3000, 9000 } },
        { { 45, 62 }, { 400, 2000 }, { 2000, 6000 } },
        { { 150, 600 }, { 1000, 5000 }, { 4000, 11000 } } };
    const int ri = role == BSine ? 0 : (role == BPulse ? 1 : 2);
    return r.logu (R[ri][reg][0], R[ri][reg][1]);
}

Lane lane (const ChannelDef& c, Rng& r, float extremity)
{
    Lane l; l.type = c.role; l.hz = pickHz (c.role, c.reg, r, extremity);
    if (c.role == BPulse) l.pattern = c.reg == RegLow ? kickPattern (r) : randomPattern (r, 4, 10);
    else if (c.role == BData || c.role == BNoise) l.pattern = randomPattern (r);
    else l.pattern = 0xffff;
    return l;
}
} // namespace

uint16_t mutatePattern (uint16_t p, int bits, uint32_t& s)
{
    Rng r (s); s = r.next();
    for (int k = 0; k < bits; ++k) p ^= (uint16_t) (1u << r.i (16));
    if (p == 0) p = (uint16_t) (1u << r.i (16));
    return p;
}

Lane makeLane (int role, int reg, uint32_t seed, float extremity)
{
    Rng r (seed);
    ChannelDef c; c.role = role; c.reg = reg;
    return role == BNone ? Lane() : lane (c, r, extremity);
}

Arrangement generate (const GenParams& g, const Arrangement* keep)
{
    Rng r ((uint32_t) g.seed * 7919u + (uint32_t) g.form * 104729u + 17u);
    const auto& ch = g.channels;
    const double totalBeats = std::clamp (g.lengthBars, 16, 512) * 4.0;
    const double blockBeats = std::clamp (g.pow2 ? (1 << (int) std::floor (std::log2 (std::max (1, g.blockBars)))) : g.blockBars, 1, 32) * 4.0;
    const float ex = std::clamp (g.extremity, 0.0f, 100.0f);

    std::array<Lane, kLanes> P;
    auto repalette = [&] (float share) { for (int c = 0; c < kLanes; ++c) if (share >= 1.0f || r.p (share)) P[(size_t) c] = lane (ch[(size_t) c], r, ex); };
    repalette (1.0f);
    auto find = [&ch] (int role, int reg) -> int
    {
        for (int c = 0; c < kLanes; ++c) if (ch[(size_t) c].role == role && ch[(size_t) c].reg == reg) return c;
        for (int c = 0; c < kLanes; ++c) if (ch[(size_t) c].role == role) return c;
        return -1;
    };
    auto bit = [] (int c) { return c < 0 ? 0u : (1u << c); };
    std::vector<Block> out;
    std::vector<bool> energySet, fillForced;
    auto add = [&] (double beats, unsigned mask, float energy = -1.0f, int fill = FillNone, double fillBeats = 2.0)
    {
        Block b; b.beats = beats;
        for (int c = 0; c < kLanes; ++c) if (mask & (1u << c)) b.lanes[(size_t) c] = P[(size_t) c];
        b.energy = energy; b.fill = fill; b.fillBeats = fillBeats;
        out.push_back (b); energySet.push_back (energy >= 0.0f); fillForced.push_back (fill != FillNone);
    };
    auto len = [&out] () { double t = 0; for (auto& b : out) t += b.beats; return t; };

    const int kick = find (BPulse, RegLow), click = find (BPulse, RegHigh), dataA = find (BData, RegMid), dataB = find (BData, RegHigh);
    const int noise = find (BNoise, RegMid), sineLo = find (BSine, RegLow), sineHi = find (BSine, RegHigh), cloud = find (BCloud, RegMid);

    switch (g.form)
    {
        case Accumulate:
        {
            while (len() < totalBeats)
            {
                std::vector<int> order;
                if (kick >= 0) order.push_back (kick);
                std::vector<int> rest;
                for (int c = 0; c < kLanes; ++c) if (c != kick) rest.push_back (c);
                for (size_t i = rest.size(); i > 1; --i) std::swap (rest[i - 1], rest[(size_t) r.i ((int) i)]);
                order.insert (order.end(), rest.begin(), rest.end());
                const int K = std::min ((int) order.size(), 4 + r.i (5));
                unsigned m = 0;
                for (int k = 0; k < K && len() < totalBeats; ++k)
                {
                    m |= 1u << order[(size_t) k];
                    add (blockBeats, m, (float) (k + 1) / (float) K, k == K - 1 ? (r.p (0.5f) ? FillTape : FillThrow) : FillNone, 2.0);
                }
                add (std::max (4.0, blockBeats * 0.5), 0, 0.0f);
                const int s = r.p (0.5f) ? sineHi : sineLo;
                if (s >= 0) { P[(size_t) s] = lane (ch[(size_t) s], r, std::max (ex, 60.0f)); add (blockBeats, bit (s), 0.15f); }
                repalette (0.5f);
            }
            break;
        }
        case Alternate:
        {
            const uint32_t bitsA = r.next(), bitsB = r.next();
            unsigned A = 0, B = 0;
            for (int c = 0; c < kLanes; ++c)
            {
                const int role = ch[(size_t) c].role;
                if ((role == BPulse || role == BData || role == BNoise) && r.p (0.6f)) A |= 1u << c;
                if ((role == BSine || role == BCloud) && r.p (0.7f)) B |= 1u << c;
            }
            if (A == 0) A = bit (kick >= 0 ? kick : 0);
            if (B == 0) B = bit (sineLo >= 0 ? sineLo : kLanes - 1);
            for (int k = 0; len() < totalBeats; ++k)
            {
                const bool a = (bitsA >> (k & 31)) & 1u, longer = (bitsB >> (k & 31)) & 1u;
                add (longer ? blockBeats : std::max (4.0, blockBeats * 0.5), a ? A : (B | ((k & 2) ? bit (dataB) : 0u)));
                if ((k & 7) == 7) repalette (0.4f);
            }
            break;
        }
        case Permute:
        {
            unsigned m = bit (dataA >= 0 ? dataA : 0);
            while (popcount (m) < 3) m |= 1u << r.i (kLanes);
            for (int i = 0; len() < totalBeats; ++i)
            {
                add (blockBeats, m);
                const int flips = std::max (1, g.mutation / 3);
                for (int f = 0; f < flips; ++f) if (r.p (0.7f)) m ^= 1u << r.i (kLanes);
                if (m == 0) m = bit (dataA >= 0 ? dataA : 0);
            }
            break;
        }
        case Codex:
        {
            const double sec = r.p (0.5f) ? 32.0 : 64.0;
            int k = 0;
            while (len() < totalBeats)
            {
                switch (k % 6)
                {
                    case 0: add (sec, bit (kick) | bit (sineLo)); break;
                    case 1: add (sec, bit (kick) | bit (click) | bit (dataA), -1, FillRoll, 4.0); break;
                    case 2: add (4.0, 0, 0.0f); break;
                    case 3: add (sec, bit (kick) | bit (click) | bit (dataA) | bit (dataB) | bit (noise) | bit (sineHi), 1.0f, FillTape, 2.0); break;
                    case 4: add (sec, bit (sineLo) | bit (dataB) | bit (cloud), 0.35f, FillSweep, 4.0); break;
                    default: add (sec, bit (kick) | bit (dataA) | bit (dataB) | bit (sineLo)); repalette (0.35f); break;
                }
                ++k;
            }
            break;
        }
        case Markov:
        {
            unsigned m = bit (kick >= 0 ? kick : 0) | bit (dataA);
            while (len() < totalBeats)
            {
                for (int c = 0; c < kLanes; ++c)
                {
                    const bool on = (m >> c) & 1u;
                    if (on ? r.p (0.25f) : r.p (0.22f)) m ^= 1u << c;
                }
                const double b = blockBeats * (r.p (0.3f) ? 0.5 : (r.p (0.2f) ? 2.0 : 1.0));
                add (std::max (4.0, b), m);
                if (r.p (0.3f)) repalette (0.3f);
            }
            break;
        }
        default:   // ARC
        {
            const int dropOrder[8] = { kick, dataA, click, sineLo, noise, dataB, sineHi, cloud };
            const int breakOrder[8] = { sineLo, cloud, dataB, sineHi, dataA, noise, click, kick };
            auto maskFor = [&] (float e, const int* order)
            {
                unsigned m = 0; const int k = std::max (1, (int) std::lround (e * 8.0f));
                for (int i = 0, n = 0; i < 8 && n < k; ++i) if (order[i] >= 0) { m |= 1u << order[i]; ++n; }
                return m;
            };
            struct Seg { float e; bool brk; int fill; double fb; };
            const Seg arc[] = { { 0.2f, false, FillNone, 2 }, { 0.4f, false, FillNone, 2 }, { 0.6f, false, FillNone, 2 }, { 0.8f, false, FillRoll, 4 },
                                { 1.0f, false, FillNone, 2 }, { 1.0f, false, FillTape, 2 }, { 0.3f, true, FillNone, 2 }, { 0.35f, true, FillSweep, 4 },
                                { 0.55f, false, FillNone, 2 }, { 0.85f, false, FillRise, 4 }, { 1.0f, false, FillNone, 2 }, { 1.0f, false, FillThrow, 2 },
                                { 0.2f, true, FillNone, 2 } };
            int k = 0;
            while (len() < totalBeats)
            {
                const auto& s = arc[k % 13];
                int fill = s.fill;
                if (fill == FillRoll && r.p (0.4f)) fill = r.p (0.5f) ? FillRise : FillSweep;
                add (blockBeats, maskFor (s.e, s.brk ? breakOrder : dropOrder), s.e, fill, s.fb);
                if (k % 13 == 12) repalette (0.6f);
                ++k;
            }
            break;
        }
    }

    // ---- shared passes -------------------------------------------------------
    for (size_t i = 1; i < out.size(); ++i)                                  // silence
        if (! out[i].isVoid() && ! fillForced[i] && r.p (std::clamp (g.silence, 0.0f, 100.0f) * 0.01f * 0.5f))
        { out[i].lanes = {}; out[i].energy = 0.0f; energySet[i] = true; }
    if (g.cutq != CutBar)                                                     // cut quantise
        for (auto& b : out)
            if (b.beats > 1.0 && r.p (0.35f))
            {
                const double unit = g.cutq == CutBeat ? 1.0 : 0.25;
                const int maxUnits = (int) std::min (b.beats / unit - 1.0, 4.0 / unit - 1.0);
                if (maxUnits >= 1) b.beats -= unit * (1 + r.i (maxUnits));
            }
    uint32_t ms = r.next();                                                   // continuing patterns mutate
    for (size_t i = 1; i < out.size(); ++i)
        for (int l = 0; l < kLanes; ++l)
        {
            auto& cur = out[i].lanes[(size_t) l];
            const auto& prev = out[i - 1].lanes[(size_t) l];
            if ((cur.type == BData || cur.type == BNoise || (cur.type == BPulse && ch[(size_t) l].reg != RegLow)) && cur.type == prev.type)
                cur.pattern = mutatePattern (prev.pattern, std::clamp (g.mutation, 0, 16), ms);
        }
    for (size_t i = 0; i < out.size(); ++i) if (! energySet[i]) out[i].energy = (float) out[i].count() / (float) kLanes;
    // fills at block ends
    const float fp = std::clamp (g.fills, 0.0f, 100.0f) * 0.01f, fx = std::clamp (g.fxMix, 0.0f, 100.0f) * 0.01f;
    for (size_t i = 0; i < out.size(); ++i)
    {
        auto& b = out[i];
        if (fp <= 0.0f) { b.fill = FillNone; continue; }
        if (fillForced[i] || b.isVoid() || b.beats < 2.0) { if (b.beats < 2.0) b.fill = FillNone; continue; }
        const auto& nx = out[(i + 1) % out.size()];
        const float de = nx.energy - b.energy;
        const bool big = std::abs (de) > 0.25f || nx.isVoid();
        if (! r.p (std::min (1.0f, fp * (big ? 1.4f : 0.55f)))) continue;
        static const int up[] = { FillRoll, FillRise, FillDropout, FillSweep, FillRepeat };
        static const int down[] = { FillBreak, FillDropout, FillTape, FillThrow, FillReverse, FillFilter };
        static const int same[] = { FillRoll, FillBreak, FillRepeat, FillCrush, FillReverse };
        const int* cand = de > 0.15f ? up : (de < -0.15f ? down : same);
        const int nc = de > 0.15f ? 5 : (de < -0.15f ? 6 : 5);
        const bool wantFx = r.p (fx);
        int pick = cand[r.i (nc)];
        for (int t = 0; t < 8 && isFxFill (pick) != wantFx; ++t) pick = cand[r.i (nc)];
        b.fill = pick;
        const float u = r.u();
        b.fillBeats = std::min (b.beats * 0.5, u < 0.45f ? 1.0 : (u < 0.8f ? 2.0 : 4.0));
    }
    // trim to the length
    double acc = 0;
    std::vector<Block> fin;
    for (auto& b : out)
    {
        if (acc >= totalBeats - 1.0e-9) break;
        Block c = b; c.beats = std::min (c.beats, totalBeats - acc);
        c.fillBeats = std::min (c.fillBeats, c.beats * 0.5);
        fin.push_back (c); acc += c.beats;
    }
    if (keep != nullptr)
        for (size_t i = 0; i < keep->blocks.size() && i < fin.size(); ++i)
            if (keep->blocks[i].locked) fin[i] = keep->blocks[i];
    Arrangement a; a.blocks = std::move (fin); a.genHash = g.hash();
    return a;
}

// ---------------------------------------------------------------------------
Player::Player()
{
    const auto d = defaultChannels();
    for (int c = 0; c < kLanes; ++c) scenes[(size_t) c] = factoryScene (d[(size_t) c].role, d[(size_t) c].reg);
}

void Player::reset() { curBlock = -2; curFillState = -1; lastFxMode = FxNone; for (auto& l : cur) l = Lane(); }

uint16_t Player::barPattern (uint16_t p, int block, int bar, int ch, float motion)
{
    if (motion <= 0.0f || bar <= 0) return p;
    uint32_t h = (uint32_t) (block * 131 + bar * 7919 + ch * 31 + 1);
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    if ((float) (h % 1000) * 0.1f >= motion) return p;
    switch ((h >> 10) % 3)
    {
        case 0: { const int k = 1 + (int) ((h >> 12) % 3); return (uint16_t) ((p << k) | (p >> (16 - k))); }
        case 1: { uint16_t q = (uint16_t) (p ^ (1u << ((h >> 14) % 16))); return q ? q : p; }
        default: { uint16_t q = (uint16_t) (p ^ (1u << ((h >> 14) % 16)) ^ (1u << ((h >> 18) % 16))); return q ? q : p; }
    }
}

static int fasterSync (int sync, int k)
{
    // straight 1/4 1/8 1/16 1/32 1/64 = 1..5, triplets 1/4T..1/32T = 6..9
    if (sync <= 0) return 0;
    if (sync <= 5) return std::min (5, sync + k);
    return std::min (9, sync + k);
}

void Player::render (Engine& e, float* L, float* R, int n, Engine::Block eb, const Arrangement& a, bool active,
                     float transpose, bool loop, const ManualFx& manual)
{
    const double total = a.totalBeats();
    const double sr = eb.sampleRate > 0 ? eb.sampleRate : 48000.0;
    const double bps = eb.bpm / 60.0 / sr;      // beats per sample
    int i = 0;
    bool firstChunk = true;
    while (i < n)
    {
        const double pos = eb.ppq + i * bps;
        int blk = -1; double start = 0, p = std::max (0.0, pos);
        if (active && total > 0.0)
        {
            if (loop) p -= std::floor (p / total) * total;
            blk = (loop || pos < total) ? a.blockAt (p, &start) : -1;
            playingPos.store (p, std::memory_order_relaxed);
        }
        const Block* B = blk >= 0 ? &a.blocks[(size_t) blk] : nullptr;
        double untilBeats = 1.0e9;
        bool inFill = false; float fillProg = 0.0f;
        int bar = 0;
        if (B != nullptr)
        {
            const double inBlock = p - start;
            bar = (int) std::floor (inBlock / 4.0 + 1.0e-9);
            untilBeats = std::min (B->beats - inBlock, (bar + 1) * 4.0 - inBlock);
            if (B->fill != FillNone)
            {
                const double fs = B->beats - B->fillBeats;
                if (inBlock >= fs - 1.0e-9)
                {
                    inFill = true;
                    fillProg = (float) std::clamp ((inBlock - fs) / std::max (1.0e-6, B->fillBeats), 0.0, 1.0);
                    const double sub = B->fill == FillRise ? 0.125 : (B->fill == FillRoll ? B->fillBeats / 3.0 : 1.0e9);
                    if (sub < 1.0e8) untilBeats = std::min (untilBeats, sub * (std::floor ((inBlock - fs) / sub + 1.0e-9) + 1.0) - (inBlock - fs));
                }
                else untilBeats = std::min (untilBeats, fs - inBlock);
            }
        }
        // ---- lanes ----
        const bool blockChanged = blk != curBlock;
        int lowest = -1;
        if (B) for (int c = 0; c < kLanes; ++c) if (B->lanes[(size_t) c].type != BNone) { lowest = c; break; }
        for (int c = 0; c < kLanes; ++c)
        {
            const Lane base = B ? B->lanes[(size_t) c] : Lane();
            const bool hardCut = blockChanged && base != cur[(size_t) c];
            cur[(size_t) c] = base;
            if (base.type == BNone) { if (hardCut || blockChanged) e.setLane (c, nullptr, 0, false, hardCut); continue; }
            auto& sc = laneScene[(size_t) c];
            sc = scenes[(size_t) c];
            if (base.type == BPulse || base.type == BData || base.type == BNoise)
                sc.burst = barPattern (base.pattern, blk, bar, c, motion);
            float gain = mix[(size_t) c].gain * (0.65f + 0.35f * B->energy);
            double hz = base.hz * std::exp2 ((transpose + mix[(size_t) c].tune) / 12.0);
            if (inFill)
                switch (B->fill)
                {
                    case FillRoll:
                        if (base.type == BPulse || base.type == BData || base.type == BNoise)
                        {
                            const int k = 1 + std::min (2, (int) (fillProg * 3.0f));
                            if (sc.sync > 0) sc.sync = fasterSync (sc.sync, k); else sc.density *= (float) (1 << k);
                            sc.burst = 0xffff;
                        }
                        break;
                    case FillBreak: if (c != lowest) gain = 0.0f; break;
                    case FillRise: hz *= std::exp2 ((double) fillProg); break;
                    case FillDropout: gain = 0.0f; break;
                    default: break;
                }
            e.setLane (c, &sc, hz, true, hardCut);
            e.setLaneGain (c, gain);
        }
        curBlock = blk;
        // ---- effects ----
        int chunk = n - i;
        if (B != nullptr) { const double s = untilBeats / bps; if (s < chunk) chunk = std::max (1, (int) std::ceil (s - 1.0e-9)); }
        Engine::Block sub = eb;
        sub.ppq = pos;
        sub.fxMode = FxNone; sub.fxTrigger = false;
        if (manual.mode != FxNone)
        {
            sub.fxMode = manual.mode;
            sub.fxP0 = sub.fxP1 = manual.progress;
            sub.fxLen = (int) (manual.sliceBeats / bps);
            sub.fxTrigger = (manual.trigger && firstChunk) || ! fxWasManual || lastFxMode != manual.mode;
            fxWasManual = true;
            curFillState = -1;
        }
        else
        {
            fxWasManual = false;
            if (inFill && isFxFill (B->fill))
            {
                static const int map[] = { FxNone, FxNone, FxNone, FxNone, FxNone, FxRepeat, FxReverse, FxTape, FxHpf, FxLpf, FxCrush, FxThrow };
                sub.fxMode = map[B->fill];
                sub.fxP0 = fillProg;
                sub.fxP1 = (float) std::min (1.0, fillProg + chunk * bps / std::max (1.0e-6, B->fillBeats));
                const double slice = B->fill == FillRepeat ? (B->fillBeats >= 2.0 ? 0.5 : 0.25) : B->fillBeats;
                sub.fxLen = (int) (slice / bps);
                const int key = blk * 16 + B->fill;
                sub.fxTrigger = key != curFillState;
                curFillState = key;
            }
            else curFillState = -1;
        }
        lastFxMode = sub.fxMode;
        activeFill.store (inFill ? B->fill : FillNone, std::memory_order_relaxed);
        activeFx.store (sub.fxMode, std::memory_order_relaxed);
        playingBlock.store (blk, std::memory_order_relaxed);
        e.process (L + i, R + i, chunk, sub);
        i += chunk;
        firstChunk = false;
    }
}
} // namespace qn
