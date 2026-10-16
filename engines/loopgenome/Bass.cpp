// semi-rack: vendored; namespaces wrapped in dnafam:: (see sources.mk for every change).
#include "Bass.h"
#include "Joint.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace dnafam::lg
{

//==============================================================================
// STYLE: NEUROSTACK's library, a 16th per character over two bars (x note, - held, . space),
// then { cell, semis, slide, bend, velocity (0 = 100) } for the notes that are not the root.
namespace
{
struct StyleDef { const char* name; const char* cells; std::vector<std::array<int, 5>> notes; };
const std::vector<StyleDef>& library()
{
    static const std::vector<StyleDef> lib {
        { "ROLLER",           "x--.......x--...x--.......x-x-..", { { 28, 12, 0, 0, 0 } } },
        { "ROLLER LONG",      "x-------..x-----x-------....x---", { { 16, -2, 1, 0, 0 }, { 28, 3, 1, 0, 0 } } },
        { "JUNGLE REESE",     "x-----------x---x-------x-------", { { 12, 3, 1, 0, 0 }, { 16, -2, 1, 0, 0 }, { 24, 5, 1, 0, 0 } } },
        { "HALF-TIME",        "x-----........x.x-----..........", { { 14, 7, 0, 0, 0 } } },
        { "UKG 2-STEP",       "x-.x-.....x-.x..x-.x-.....x-.x-.", { { 3, 12, 0, 0, 0 }, { 19, 12, 0, 0, 0 }, { 29, 10, 1, 0, 0 } } },
        { "SPEED GARAGE",     "..x-..x-..x-..x-..x-..x-..x-..x-", { { 18, 12, 0, 0, 0 }, { 22, 12, 0, 0, 0 }, { 30, 7, 0, 0, 0 } } },
        { "ACID",             "x.xx-x.xx.x-xx.xx.xx-x.xx.x-x.x.", { { 3, 12, 1, 0, 0 }, { 7, 12, 0, 0, 0 }, { 13, 3, 1, 0, 0 }, { 19, 12, 1, 0, 0 }, { 23, 10, 0, 0, 0 }, { 28, 7, 1, 0, 0 } } },
        { "STEPPER",          "x-..x-..x-..x-..x-..x-..x-..x...", { { 24, 12, 0, 0, 0 }, { 28, 7, 0, 0, 0 } } },
        { "DRONE + ANSWER",   "x--------.......x-----------....", { { 0, 0, 0, 0, 110 }, { 16, 0, 0, 0, 90 } } },
        { "ROLLING 8THS +12", "x-x-x-x-x-x-x-x-x-x-x-x-x-x-x-x-",
          { { 0, -1, 0, 0, 0 }, { 2, -1, 0, 0, 80 }, { 4, 0, 0, 0, 0 }, { 6, 3, 0, 0, 80 }, { 8, 12, 0, 0, 0 }, { 10, 12, 0, 0, 80 },
            { 12, 12, 0, 0, 0 }, { 14, 12, 0, 0, 80 }, { 16, 0, 0, 0, 0 }, { 18, 0, 0, 0, 80 }, { 20, 0, 0, 0, 0 }, { 22, 0, 0, 0, 80 },
            { 24, 12, 0, 0, 0 }, { 26, 12, 0, 0, 80 }, { 28, 3, 0, 0, 0 }, { 30, 5, 0, 0, 80 } } },
        { "DOTTED ROLLER",    "x---x-----x-----x----.x----.x---",
          { { 0, 5, 0, 0, 0 }, { 4, -2, 1, 0, 0 }, { 10, -2, 0, 0, 0 }, { 16, 0, 0, 0, 0 }, { 22, 0, 0, 0, 0 }, { 28, 0, 0, 0, 0 } } },
        { "SPACE ON ONE",     "....x----.x----.x----.x---------", { { 4, 0, 0, 0, 0 }, { 10, -4, 0, 0, 0 }, { 16, 3, 0, 0, 0 }, { 22, -4, 0, 0, 0 } } },
        { "GAP ROLLER",       "x----x---x-x---.................", { { 5, 2, 0, 0, 0 }, { 9, -1, 0, 0, 0 } } },
        { "HALF-BAR STABS",   "x-----......x------x----........", { { 0, 3, 0, 0, 0 }, { 12, 3, 0, 0, 0 }, { 19, 2, 0, 0, 0 } } },
        { "808 DROPS",        "x-----------....x-------x-------", { { 0, 0, 0, 12, 120 }, { 16, -2, 0, 7, 0 }, { 24, 3, 0, 12, 0 } } },
        { "GARAGE DIVES",     "..x-..x-..x-..x-..x-..x-..x-..x-", { { 2, 0, 0, 7, 0 }, { 10, 0, 0, 7, 0 }, { 18, 0, 0, 7, 0 }, { 26, 3, 0, 12, 0 }, { 30, 5, 0, 0, 0 } } },
    };
    return lib;
}

void holdPitches (BassLine& b)
{
    int semis = 0;
    for (int i = 0; i < b.steps; ++i)
    {
        auto& c = b.cells[(size_t) i];
        if (c.type == BHit) semis = c.semis;
        else if (c.type == BTie) { c.semis = (int8_t) semis; }
    }
}

struct Rng
{
    uint32_t s;
    explicit Rng (uint32_t v) : s (v ? v * 2654435761u + 1u : 0x9E3779B9u) {}
    float unit() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s >> 8) * (1.0f / 16777216.0f); }
};

int pickWeighted (Rng& r, const std::vector<std::pair<int, float>>& w)
{
    float t = 0.0f;
    for (auto& x : w) t += x.second;
    float p = r.unit() * t;
    for (auto& x : w) { p -= x.second; if (p < 0.0f) return x.first; }
    return w.back().first;
}
} // namespace

int numStyles() { return (int) library().size(); }
const char* styleName (int i) { return library()[(size_t) std::clamp (i, 0, numStyles() - 1)].name; }

BassLine style (int i)
{
    const auto& d = library()[(size_t) std::clamp (i, 0, numStyles() - 1)];
    BassLine b;
    b.name = d.name;
    b.steps = 32;
    for (int s = 0; s < 32; ++s)
    {
        const char c = d.cells[s];
        b.cells[(size_t) s].type = (int8_t) (c == 'x' ? BHit : c == '-' ? BTie : BRest);
    }
    for (auto& n : d.notes)
    {
        auto& c = b.cells[(size_t) n[0]];
        c.semis = (int8_t) n[1]; c.slide = (int8_t) n[2]; c.bend = (int8_t) n[3]; c.vel = (uint8_t) (n[4] > 0 ? n[4] : 100);
    }
    holdPitches (b);
    return b;
}

//==============================================================================
BassLine fromKicks (const geno::Learned& L, float density, float pickup, uint32_t seed)
{
    const int n = std::clamp (L.steps, 1, kMaxSteps);
    auto has = [&] (int s, int lane) { const unsigned m = lane == geno::LSnare ? kSnareLanes : (1u << lane); return (L.a[(size_t) (s % n)].mask & m) != 0; };
    BassLine b;
    b.name = "FROM KICKS";
    b.steps = n;
    int kicks = 0;
    for (int s = 0; s < n; ++s) kicks += has (s, geno::LKick);
    if (kicks == 0) { auto st = fit (style (0), n); st.name = "FROM KICKS (no kicks: ROLLER)"; return st; }

    Rng r (seed + 77u);
    // the notes: the root most of the time, then the octave, the flat-7 under, the flat-3,
    // the 4th and the 5th (the analysed loops' degrees)
    const std::vector<std::pair<int, float>> degrees { { 0, 0.50f }, { 12, 0.15f }, { -2, 0.12f }, { 3, 0.11f }, { 5, 0.06f }, { 7, 0.06f } };
    std::vector<int> starts;
    for (int s = 0; s < n; ++s)
    {
        if (! has (s, geno::LKick)) continue;
        const bool barStart = s % 16 == 0 || (s > 0 && ! [&] { for (int q = (s / 16) * 16; q < s; ++q) if (has (q, geno::LKick)) return true; return false; }());
        // a kick right after a note's kick (a double kick) is held through
        if (! starts.empty() && s - starts.back() <= 1) continue;
        if (barStart || r.unit() < density) starts.push_back (s);
    }
    for (size_t i = 0; i < starts.size(); ++i)
    {
        const int s = starts[i];
        const int next = i + 1 < starts.size() ? starts[i + 1] : n;
        auto& c = b.cells[(size_t) s];
        c.type = BHit;
        c.semis = (int8_t) ((s % 32 == 0 || i == 0) ? 0 : pickWeighted (r, degrees));
        c.vel = (uint8_t) (s % 16 == 0 ? 112 : 96);
        c.slide = (int8_t) (r.unit() < 0.12f ? 1 : 0);
        // held until the next note, or the snare (no bass on the snare: the gap the loops leave)
        for (int q = s + 1; q < next; ++q)
        {
            if (has (q, geno::LSnare)) break;
            b.cells[(size_t) q].type = BTie;
        }
    }
    // pickups: a short note in the last beat of a bar (26 % of the analysed bars have one)
    for (int bar = 0; bar * 16 < n; ++bar)
    {
        if (r.unit() >= pickup) continue;
        for (int q : { bar * 16 + 15, bar * 16 + 14, bar * 16 + 13 })
        {
            if (q >= n || has (q, geno::LSnare) || b.cells[(size_t) q].type == BHit) continue;
            auto& c = b.cells[(size_t) q];
            c.type = BHit;
            c.semis = (int8_t) pickWeighted (r, { { 12, 0.35f }, { -2, 0.25f }, { 2, 0.2f }, { 0, 0.2f } });
            c.vel = 84;
            c.slide = 0;
            for (int z = q + 1; z < n && z < bar * 16 + 16; ++z) if (b.cells[(size_t) z].type == BTie) b.cells[(size_t) z].type = BRest;
            break;
        }
    }
    holdPitches (b);
    return b;
}

//==============================================================================
double yin (const float* x, int n, double rate, double fmin, double fmax, float threshold, float* clarity)
{
    const int tauMin = std::max (2, (int) std::floor (rate / fmax));
    const int tauMax = std::min (n / 2, (int) std::ceil (rate / fmin));
    if (clarity != nullptr) *clarity = 0.0f;
    if (tauMax <= tauMin + 2) return 0.0;
    const int W = n - tauMax;
    std::vector<double> d ((size_t) tauMax + 1, 0.0);
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        double s = 0.0;
        for (int i = 0; i < W; ++i) { const double e = (double) x[i] - (double) x[i + tau]; s += e * e; }
        d[(size_t) tau] = s;
    }
    // cumulative mean normalised difference
    std::vector<double> dp ((size_t) tauMax + 1, 1.0);
    double run = 0.0;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        run += d[(size_t) tau];
        dp[(size_t) tau] = run > 0.0 ? d[(size_t) tau] * tau / run : 1.0;
    }
    int best = -1;
    for (int tau = tauMin; tau < tauMax; ++tau)
        if (dp[(size_t) tau] < threshold)
        {
            while (tau + 1 < tauMax && dp[(size_t) tau + 1] < dp[(size_t) tau]) ++tau;
            best = tau;
            break;
        }
    if (best < 0)
    {
        best = tauMin;
        for (int tau = tauMin; tau < tauMax; ++tau) if (dp[(size_t) tau] < dp[(size_t) best]) best = tau;
        if (dp[(size_t) best] > 0.45) return 0.0;
    }
    if (clarity != nullptr) *clarity = (float) std::clamp (1.0 - dp[(size_t) best], 0.0, 1.0);
    double t = best;
    if (best > 1 && best < tauMax)
    {
        const double a = dp[(size_t) best - 1], b = dp[(size_t) best], c = dp[(size_t) best + 1];
        const double den = a - 2.0 * b + c;
        if (std::abs (den) > 1.0e-12) t = best + 0.5 * (a - c) / den;
    }
    return t > 0.0 ? rate / t : 0.0;
}

BassLine fromAudio (const std::vector<float>& mono, double rate, int steps16, const std::string& name)
{
    BassLine b;
    b.name = name;
    b.fromAudio = true;
    b.steps = std::clamp (steps16, 1, kMaxSteps);
    if (mono.size() < 1024 || rate <= 0.0) return b;

    // to ~4 kHz: two one-pole low-passes, then every D-th sample (the fundamentals live below 500 Hz)
    const int D = std::max (1, (int) std::floor (rate / 4000.0));
    const double r2 = rate / D;
    std::vector<float> x;
    x.reserve (mono.size() / (size_t) D + 1);
    {
        const float k = 1.0f - std::exp (-2.0f * 3.14159265f * 900.0f / (float) rate);
        float a = 0.0f, c = 0.0f;
        for (size_t i = 0; i < mono.size(); ++i)
        {
            a += (mono[i] - a) * k;
            c += (a - c) * k;
            if (i % (size_t) D == 0) x.push_back (c);
        }
    }
    // frames every 5 ms: f0 (YIN, 30-500 Hz), clarity, level (from the full-band signal)
    const int hop = std::max (1, (int) std::lround (0.005 * r2));
    const int frame = (int) std::ceil (2.2 * r2 / 30.0);
    const int nf = std::max (0, ((int) x.size() - frame) / hop);
    std::vector<float> midi ((size_t) nf, -1.0f), db ((size_t) nf, -120.0f);
    float peak = -120.0f;
    for (int f = 0; f < nf; ++f)
    {
        float cl = 0.0f;
        const double hz = yin (x.data() + (size_t) f * (size_t) hop, frame, r2, 30.0, 500.0, 0.15f, &cl);
        const size_t s0 = (size_t) f * (size_t) hop * (size_t) D, s1 = std::min (mono.size(), s0 + (size_t) (0.012 * rate));
        double e = 0.0;
        for (size_t i = s0; i < s1; ++i) e += (double) mono[i] * mono[i];
        const float lv = 10.0f * (float) std::log10 (e / (double) std::max<size_t> (1, s1 - s0) + 1.0e-12);
        db[(size_t) f] = lv;
        peak = std::max (peak, lv);
        if (hz > 0.0 && cl > 0.55f) midi[(size_t) f] = (float) (69.0 + 12.0 * std::log2 (hz / 440.0));
    }
    if (nf == 0) return b;

    // cells: the 16th's first 60 % decides voiced / pitch, a rise in level is a re-attack
    const double framesPerCell = (double) mono.size() / (double) D / (double) hop / (double) b.steps;
    std::vector<float> cellMidi ((size_t) b.steps, -1.0f), cellLvl ((size_t) b.steps, -120.0f), cellTail ((size_t) b.steps, -120.0f);
    for (int c = 0; c < b.steps; ++c)
    {
        const int f0 = (int) std::floor (c * framesPerCell), f1 = std::min (nf, (int) std::floor ((c + 0.6) * framesPerCell));
        const int t0 = std::max (0, (int) std::floor ((c + 0.7) * framesPerCell)), t1 = std::min (nf, (int) std::floor ((c + 1.0) * framesPerCell));
        std::vector<float> ps;
        float lv = -120.0f, tail = -120.0f;
        for (int f = f0; f < f1 && f < nf; ++f) { lv = std::max (lv, db[(size_t) f]); if (midi[(size_t) f] > 0.0f) ps.push_back (midi[(size_t) f]); }
        for (int f = t0; f < t1; ++f) tail = std::max (tail, db[(size_t) f]);
        cellLvl[(size_t) c] = lv;
        cellTail[(size_t) c] = tail;
        if (lv > peak - 30.0f && (int) ps.size() * 2 >= std::max (1, f1 - f0))
        {
            std::nth_element (ps.begin(), ps.begin() + (long) ps.size() / 2, ps.end());
            cellMidi[(size_t) c] = ps[ps.size() / 2];
        }
        else if (lv > peak - 24.0f && ! ps.empty())
        {
            std::nth_element (ps.begin(), ps.begin() + (long) ps.size() / 2, ps.end());
            cellMidi[(size_t) c] = ps[ps.size() / 2];
        }
    }
    // the root: the most played pitch (by cells), the lower one on a tie
    std::map<int, int> count;
    for (float m : cellMidi) if (m > 0.0f) count[(int) std::lround (m)]++;
    int root = 36, best = -1;
    for (auto& [note, c] : count) if (c > best) { best = c; root = note; }
    b.root = root;
    for (int c = 0; c < b.steps; ++c)
    {
        auto& cell = b.cells[(size_t) c];
        const float m = cellMidi[(size_t) c];
        if (m <= 0.0f) { cell.type = BRest; continue; }
        const float prev = c > 0 ? cellMidi[(size_t) c - 1] : -1.0f;
        const bool reattack = c > 0 && cellLvl[(size_t) c] - cellTail[(size_t) c - 1] > 4.0f;
        const bool newPitch = prev <= 0.0f || std::abs (m - prev) > 0.6f;
        cell.type = (int8_t) ((newPitch || reattack) ? BHit : BTie);
        cell.semis = (int8_t) std::clamp ((int) std::lround (m) - root, -24, 24);
        cell.vel = (uint8_t) std::clamp ((int) std::lround (100.0f + 2.0f * (cellLvl[(size_t) c] - (peak - 6.0f))), 50, 127);
    }
    holdPitches (b);
    return b;
}

BassLine fromNotes (std::vector<MidiNoteIn> notes, int steps16, const std::string& name)
{
    BassLine b;
    b.name = name;
    b.steps = std::clamp (steps16, 1, kMaxSteps);
    if (notes.empty()) return b;
    std::sort (notes.begin(), notes.end(), [] (const MidiNoteIn& a, const MidiNoteIn& c)
               { return a.start16 < c.start16 || (! (c.start16 < a.start16) && a.note < c.note); });
    std::array<int, kMaxSteps> owner {};
    owner.fill (-1);
    std::map<int, double> weight;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& nt = notes[i];
        const int s = (int) std::lround (nt.start16);
        if (s < 0 || s >= b.steps) continue;
        // the lowest line: a note starting on a cell another lower note started on is dropped
        if (owner[(size_t) s] >= 0 && b.cells[(size_t) s].type == BHit && notes[(size_t) owner[(size_t) s]].note <= nt.note) continue;
        const int len = std::max (1, (int) std::lround (nt.len16));
        weight[nt.note] += len;
        b.cells[(size_t) s].type = BHit;
        owner[(size_t) s] = (int) i;
        for (int q = s + 1; q < s + len && q < b.steps; ++q)
        {
            if (b.cells[(size_t) q].type == BHit) break;
            b.cells[(size_t) q].type = BTie;
            owner[(size_t) q] = (int) i;
        }
    }
    int root = 36;
    double best = -1.0;
    for (auto& [note, w] : weight) if (w > best + 1.0e-9) { best = w; root = note; }
    b.root = root;
    for (int s = 0; s < b.steps; ++s)
        if (owner[(size_t) s] >= 0)
        {
            const auto& nt = notes[(size_t) owner[(size_t) s]];
            auto& c = b.cells[(size_t) s];
            if (c.type == BRest) continue;
            c.semis = (int8_t) std::clamp (nt.note - root, -24, 24);
            c.vel = (uint8_t) std::clamp (nt.vel, 1, 127);
        }
    holdPitches (b);
    return b;
}

BassLine fit (const BassLine& src, int n)
{
    BassLine b = src;
    n = std::clamp (n, 1, kMaxSteps);
    const int m = std::clamp (src.steps, 1, kMaxSteps);
    b.steps = n;
    for (int s = 0; s < n; ++s) b.cells[(size_t) s] = src.cells[(size_t) (s % m)];
    for (int s = n; s < kMaxSteps; ++s) b.cells[(size_t) s] = {};
    // a held cell whose note was cut off (the loop's start, a shorter break) becomes a note,
    // unless the line wraps into it (the last cell holds too)
    if (b.cells[0].type == BTie && b.cells[(size_t) n - 1].type == BRest) b.cells[0].type = BHit;
    return b;
}

} // namespace dnafam::lg
