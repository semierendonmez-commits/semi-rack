// Vendored unchanged from semierendonmez-commits/epoch Source/engine (Rack / MetaModule port).
#include "Score.h"
#include <algorithm>
#include <cmath>

namespace epoch
{

const char* shapeName (Shape s)
{
    static const char* n[] = { "LINEAR", "SMOOTH", "HOLD", "EXP", "LOG" };
    return n[(int) s < (int) Shape::kNum ? (int) s : 0];
}

const char* roleName (Role r)
{
    static const char* n[] = { "INTRO", "BUILD", "DROP", "BREAK", "ROLL", "OUTRO", "DRIFT", "WALL" };
    return n[(int) r < (int) Role::kNum ? (int) r : 0];
}

static float clamp01 (float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

float Curve::eval (double bar) const
{
    if (pts.empty()) return 0.0f;
    if (bar <= pts.front().bar) return pts.front().value;
    if (bar >= pts.back().bar) return pts.back().value;
    // binary search: the last point at or before `bar`
    size_t lo = 0, hi = pts.size() - 1;
    while (hi - lo > 1)
    {
        const size_t mid = (lo + hi) / 2;
        if (pts[mid].bar <= bar) lo = mid; else hi = mid;
    }
    const auto& a = pts[lo];
    const auto& b = pts[hi];
    const double span = b.bar - a.bar;
    float f = span > 1.0e-9 ? (float) ((bar - a.bar) / span) : 1.0f;
    switch (a.shape)
    {
        case Shape::Hold:   f = 0.0f; break;
        case Shape::Smooth: f = f * f * (3.0f - 2.0f * f); break;
        case Shape::Exp:    f = f * f * f; break;
        case Shape::Log:    f = 1.0f - (1.0f - f) * (1.0f - f) * (1.0f - f); break;
        default: break;
    }
    return clamp01 (a.value + (b.value - a.value) * f);
}

void Curve::sort()
{
    std::stable_sort (pts.begin(), pts.end(), [] (const Point& a, const Point& b) { return a.bar < b.bar; });
}

int Curve::nearest (double bar, float value, double barsPerUnit, double maxDist) const
{
    int best = -1;
    double bd = maxDist * maxDist;
    for (size_t i = 0; i < pts.size(); ++i)
    {
        const double dx = (pts[i].bar - bar) / barsPerUnit, dy = (double) (pts[i].value - value);
        const double d = dx * dx + dy * dy;
        if (d < bd) { bd = d; best = (int) i; }
    }
    return best;
}

void Score::sortSections()
{
    std::stable_sort (sections.begin(), sections.end(), [] (const Section& a, const Section& b) { return a.bar < b.bar; });
}

int Score::sectionAt (double bar) const
{
    int idx = -1;
    for (size_t i = 0; i < sections.size(); ++i)
        if (sections[i].bar <= bar + 1.0e-9) idx = (int) i;
    return idx;
}

double Score::sectionEnd (int index) const
{
    if (index + 1 < (int) sections.size()) return sections[(size_t) index + 1].bar;
    return (double) bars;
}

void Score::clampTo (int newBars)
{
    bars = std::clamp (newBars, 4, kMaxBars);
    for (auto* c : { &energy, &space })
    {
        const float endV = c->eval ((double) bars);
        c->pts.erase (std::remove_if (c->pts.begin(), c->pts.end(), [&] (const Point& p) { return p.bar > (double) bars; }), c->pts.end());
        if (c->pts.empty() || c->pts.back().bar < (double) bars) c->pts.push_back ({ (double) bars, endV, Shape::Linear });
    }
    sections.erase (std::remove_if (sections.begin(), sections.end(), [&] (const Section& s) { return s.bar >= (double) bars; }), sections.end());
}

void Score::rescale (int newBars)
{
    newBars = std::clamp (newBars, 4, kMaxBars);
    const double k = (double) newBars / (double) std::max (1, bars);
    for (auto* c : { &energy, &space })
        for (auto& p : c->pts) p.bar = std::round (p.bar * k * 4.0) / 4.0;
    for (auto& s : sections) s.bar = std::round (s.bar * k);
    bars = newBars;
}

//==============================================================================
namespace
{
    struct Build
    {
        Score s;
        Build (const char* title, int bars, double bpm) { s.title = title; s.bars = bars; s.bpm = bpm; }
        Build& sec (double bar, Role r, const char* name = nullptr)
        {
            s.sections.push_back ({ bar, r, name != nullptr ? name : roleName (r) });
            return *this;
        }
        Build& e (double bar, float v, Shape sh = Shape::Smooth) { s.energy.pts.push_back ({ bar, v, sh }); return *this; }
        Build& sp (double bar, float v, Shape sh = Shape::Smooth) { s.space.pts.push_back ({ bar, v, sh }); return *this; }
        Score done() { s.energy.sort(); s.space.sort(); s.sortSections(); return s; }
    };

    const Template kTemplates[] = {
        { "DNB DJ TOOL",      "DNB",      "32 in, 64 drop, 32 break, 16 build, 64 drop, 32 out: the mixable shape" },
        { "JUNGLE ROLLER",    "DNB",      "one long roll, energy breathing every 32 bars, a short break in the middle" },
        { "HALF-TIME DRIFT",  "DNB",      "low and heavy, the space opens in the long middle" },
        { "UKG 2-STEP",       "GARAGE",   "16 in, verse, chorus, a break, verse, double chorus, 16 out" },
        { "SPEED GARAGE",     "GARAGE",   "32 in, drop, 16 break, the long second drop" },
        { "LOW INSTRUMENT",   "MEASURED", "from the 4.8-min 'instrument' reference: 3.4 % of the time in gaps over 1.5 s, slow swells" },
        { "FORGE DRIFT",      "MEASURED", "from the 32-min Vhikk XS reference: 17.5 % gaps, the space comes and goes in long waves" },
        { "EBB 27",           "MEASURED", "FLOATER's FREE / EBB patches: about a quarter of the time silent, energy in waves" },
        { "IKEDA BLOCKS",     "FORM",     "hard cuts between 16-bar blocks, full or nearly empty, no fades" },
        { "SINGLE ARC",       "FORM",     "eight minutes, one rise, one fall" },
        { "FLAT",             "FORM",     "one level, no holes: start drawing" },
    };
}

int numTemplates() { return (int) (sizeof (kTemplates) / sizeof (kTemplates[0])); }
const Template& templateInfo (int i) { return kTemplates[std::clamp (i, 0, numTemplates() - 1)]; }

Score makeTemplate (int i)
{
    switch (std::clamp (i, 0, numTemplates() - 1))
    {
        case 0:  // DNB DJ TOOL, 240 bars at 172 = 5:35
            return Build ("DNB DJ TOOL", 240, 172.0)
                .sec (0, Role::Intro).sec (32, Role::Drop).sec (96, Role::Break).sec (128, Role::Build)
                .sec (144, Role::Drop, "DROP 2").sec (208, Role::Outro)
                .e (0, 0.30f).e (24, 0.45f).e (32, 0.86f, Shape::Linear).e (64, 0.82f).e (96, 0.90f, Shape::Hold)
                .e (96, 0.32f).e (124, 0.38f, Shape::Exp).e (144, 0.80f, Shape::Linear).e (144.01, 0.94f).e (176, 0.90f).e (208, 0.96f, Shape::Hold)
                .e (208, 0.55f).e (240, 0.30f)
                .sp (0, 0.06f).sp (32, 0.02f, Shape::Hold).sp (96, 0.22f).sp (124, 0.10f).sp (144, 0.02f, Shape::Hold).sp (208, 0.06f).sp (240, 0.12f)
                .done();
        case 1:  // JUNGLE ROLLER, 256 bars at 170
        {
            Build b ("JUNGLE ROLLER", 256, 170.0);
            b.sec (0, Role::Intro).sec (32, Role::Roll).sec (128, Role::Break).sec (144, Role::Roll, "ROLL 2").sec (224, Role::Outro);
            b.e (0, 0.38f).e (32, 0.74f, Shape::Linear);
            for (int k = 0; k < 3; ++k) b.e (48 + 32 * k, 0.84f).e (64 + 32 * k, 0.72f);
            b.e (128, 0.40f, Shape::Linear).e (144, 0.78f, Shape::Linear);
            for (int k = 0; k < 2; ++k) b.e (160 + 32 * k, 0.88f).e (176 + 32 * k, 0.78f);
            b.e (224, 0.70f).e (256, 0.32f);
            b.sp (0, 0.05f).sp (32, 0.03f).sp (128, 0.14f).sp (144, 0.03f).sp (224, 0.05f).sp (256, 0.10f);
            return b.done();
        }
        case 2:  // HALF-TIME DRIFT, 192 bars at 170 (felt at 85)
            return Build ("HALF-TIME DRIFT", 192, 170.0)
                .sec (0, Role::Intro).sec (16, Role::Drop).sec (64, Role::Drift).sec (128, Role::Drop, "DROP 2").sec (176, Role::Outro)
                .e (0, 0.25f).e (16, 0.70f, Shape::Log).e (64, 0.64f).e (96, 0.34f).e (128, 0.62f, Shape::Exp).e (160, 0.82f).e (176, 0.6f).e (192, 0.22f)
                .sp (0, 0.10f).sp (16, 0.06f).sp (64, 0.12f).sp (96, 0.30f).sp (124, 0.18f).sp (128, 0.05f).sp (192, 0.15f)
                .done();
        case 3:  // UKG 2-STEP, 160 bars at 132 = 4:51
            return Build ("UKG 2-STEP", 160, 132.0)
                .sec (0, Role::Intro).sec (16, Role::Roll, "VERSE").sec (48, Role::Drop, "CHORUS").sec (64, Role::Break)
                .sec (80, Role::Roll, "VERSE 2").sec (112, Role::Drop, "CHORUS x2").sec (144, Role::Outro)
                .e (0, 0.35f).e (16, 0.62f, Shape::Linear).e (44, 0.70f).e (48, 0.86f, Shape::Linear).e (64, 0.86f, Shape::Hold)
                .e (64, 0.38f).e (78, 0.52f).e (80, 0.64f, Shape::Linear).e (108, 0.72f).e (112, 0.92f, Shape::Linear).e (144, 0.90f, Shape::Hold)
                .e (144, 0.58f).e (160, 0.30f)
                .sp (0, 0.08f).sp (16, 0.05f).sp (48, 0.03f).sp (64, 0.20f).sp (80, 0.05f).sp (112, 0.02f).sp (144, 0.06f).sp (160, 0.12f)
                .done();
        case 4:  // SPEED GARAGE, 192 bars at 135
            return Build ("SPEED GARAGE", 192, 135.0)
                .sec (0, Role::Intro).sec (32, Role::Drop).sec (64, Role::Break).sec (80, Role::Build).sec (96, Role::Drop, "DROP 2").sec (160, Role::Outro)
                .e (0, 0.40f).e (28, 0.55f).e (32, 0.88f, Shape::Linear).e (64, 0.86f, Shape::Hold).e (64, 0.36f).e (80, 0.42f, Shape::Exp)
                .e (96, 0.82f, Shape::Linear).e (96.01, 0.95f).e (160, 0.92f, Shape::Hold).e (160, 0.6f).e (192, 0.34f)
                .sp (0, 0.04f).sp (32, 0.02f).sp (64, 0.18f).sp (80, 0.08f).sp (96, 0.02f).sp (160, 0.05f).sp (192, 0.10f)
                .done();
        case 5:  // LOW INSTRUMENT: 4.8 min at 120 BPM = 144 bars
        {
            Build b ("LOW INSTRUMENT", 144, 120.0);
            b.sec (0, Role::Drift, "SWELL").sec (48, Role::Drift, "BODY").sec (112, Role::Drift, "TAIL");
            const float lv[] = { 0.42f, 0.56f, 0.48f, 0.60f, 0.52f, 0.62f, 0.50f, 0.58f, 0.46f, 0.36f };
            for (int k = 0; k < 10; ++k) b.e (k * 16.0, lv[k]);
            b.sp (0, 0.04f).sp (40, 0.02f).sp (72, 0.05f).sp (104, 0.03f).sp (144, 0.04f);
            return b.done();
        }
        case 6:  // FORGE DRIFT: 32 min at 64 BPM = 512 bars
        {
            Build b ("FORGE DRIFT", 512, 64.0);
            b.sec (0, Role::Drift, "I").sec (128, Role::Drift, "II").sec (256, Role::Drift, "III").sec (384, Role::Drift, "IV");
            for (int k = 0; k <= 16; ++k) b.e (k * 32.0, 0.38f + 0.12f * std::sin ((float) k * 1.3f) + 0.05f * std::sin ((float) k * 0.41f));
            for (int k = 0; k <= 8; ++k) b.sp (k * 64.0, 0.175f + 0.07f * std::sin ((float) k * 1.7f + 0.6f));
            return b.done();
        }
        case 7:  // EBB 27
        {
            Build b ("EBB 27", 256, 120.0);
            b.sec (0, Role::Drift, "EBB");
            for (int k = 0; k <= 16; ++k) b.e (k * 16.0, (k & 1) ? 0.66f : 0.40f);
            for (int k = 0; k <= 8; ++k) b.sp (k * 32.0, (k & 1) ? 0.20f : 0.34f);
            return b.done();
        }
        case 8:  // IKEDA BLOCKS
        {
            Build b ("IKEDA BLOCKS", 256, 130.0);
            const float lv[] = { 0.95f, 0.18f, 0.88f, 0.10f, 1.00f, 0.92f, 0.22f, 0.98f, 0.12f, 0.90f, 0.30f, 0.96f, 0.08f, 0.99f, 0.86f, 0.15f };
            const float sv[] = { 0.0f, 0.45f, 0.02f, 0.60f, 0.0f, 0.0f, 0.40f, 0.02f, 0.55f, 0.0f, 0.35f, 0.0f, 0.70f, 0.0f, 0.02f, 0.50f };
            for (int k = 0; k < 16; ++k)
            {
                b.sec (k * 16.0, lv[k] > 0.5f ? Role::Wall : Role::Break, nullptr);
                b.e (k * 16.0, lv[k], Shape::Hold).sp (k * 16.0, sv[k], Shape::Hold);
            }
            b.e (256, 0.15f, Shape::Hold).sp (256, 0.5f, Shape::Hold);
            return b.done();
        }
        case 9:  // SINGLE ARC: 8 minutes at 128 = 256 bars
            return Build ("SINGLE ARC", 256, 128.0)
                .sec (0, Role::Intro, "RISE").sec (160, Role::Drop, "PEAK").sec (192, Role::Outro, "FALL")
                .e (0, 0.12f).e (160, 0.92f, Shape::Exp).e (192, 0.98f).e (256, 0.10f)
                .sp (0, 0.25f).sp (120, 0.08f).sp (160, 0.0f).sp (192, 0.04f).sp (256, 0.30f)
                .done();
        default:
            return Build ("FLAT", 256, 172.0).sec (0, Role::Intro, "START").e (0, 0.6f).e (256, 0.6f).sp (0, 0.0f).sp (256, 0.0f).done();
    }
}

} // namespace epoch
