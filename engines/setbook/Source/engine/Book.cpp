#include "Book.h"
#include <algorithm>
#include <cmath>

namespace sb
{

float distance (const Scene& a, const Scene& b)
{
    double sum = 0.0;
    int n = 0;
    for (const auto& pa : a.parts)
    {
        const auto* pb = b.part (pa.key());
        if (pb == nullptr) continue;
        for (int i = 0; i < pa.size(); ++i)
        {
            const int j = pb->find (pa.desc[(size_t) i].id);
            if (j < 0) continue;
            sum += std::fabs ((double) pa.values[(size_t) i] - (double) pb->values[(size_t) j]);
            ++n;
        }
    }
    return n == 0 ? -1.0f : (float) (sum / n);
}

void sortCues (std::vector<Cue>& c)
{
    std::stable_sort (c.begin(), c.end(), [] (const Cue& a, const Cue& b) { return a.bar < b.bar; });
    if ((int) c.size() > kMaxCues) c.resize ((size_t) kMaxCues);
}

double Placement::rel (double ppq) const
{
    double r = ppq - originPpq;
    if (loopQ > 0.0)
    {
        r = std::fmod (r, loopQ);
        if (r < 0.0) r += loopQ;
    }
    return r;
}

int cueAt (const Cue* cues, int n, double relQ)
{
    int best = -1;
    for (int i = 0; i < n; ++i)
        if (cues[i].bar * 4.0 <= relQ + 1e-9) best = i;
        else break;
    return best;
}

Crossing cross (const Cue* cues, int n, const Placement& pl, double prevPpq, double ppq, bool wasPlaying, bool playing)
{
    Crossing c;
    if (! playing || n <= 0) return c;
    const double now = pl.rel (ppq);
    // a start, a locate backwards, a jump of more than two bars: the lane is "inside" a cue now
    if (! wasPlaying || ppq < prevPpq - 1e-6 || ppq - prevPpq > 8.0)
    {
        c.located = true;
        c.chase = cueAt (cues, n, now);
        return c;
    }
    const double was = pl.rel (prevPpq);
    auto fire = [&] (double lo, double hi)        // (lo, hi]
    {
        for (int i = 0; i < n && c.numFired < 8; ++i)
        {
            const double q = cues[i].bar * 4.0;
            if (q > lo + 1e-9 && q <= hi + 1e-9) c.fired[c.numFired++] = i;
        }
    };
    if (pl.loopQ > 0.0 && now < was)              // the lane wrapped
    {
        fire (was, pl.loopQ);
        fire (-1.0, now);
    }
    else
        fire (was, now);                          // before the origin `was` is negative: bar 0 fires on arrival
    return c;
}

double launchPpq (double ppq, double quantQ, bool playing)
{
    if (! playing || quantQ <= 0.0) return ppq;
    const double k = std::ceil (ppq / quantQ - 1e-6);
    return k * quantQ;
}

const char* defaultSceneName (int i)
{
    static const char* n[kScenes] = { "INTRO", "BUILD", "DROP", "BREAK", "DROP 2", "SWITCH", "OUTRO", "DRONE",
                                      "SCENE 9", "SCENE 10", "SCENE 11", "SCENE 12", "SCENE 13", "SCENE 14", "SCENE 15", "SCENE 16" };
    return n[(i % kScenes + kScenes) % kScenes];
}

} // namespace sb
