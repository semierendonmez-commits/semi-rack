#include "Tb3poCore.h"
#include <algorithm>
#include <cstdlib>

// SEMI Family: vendored from semierendonmez-commits/acid Source/engine; only change: wrapped in namespace acid
// (one plugin binary links ~35 engines).
namespace acid {

const Tb3poScale tb3poScales[] = {
    { "minor",      7, { 0, 2, 3, 5, 7, 8, 10 } },
    { "major",      7, { 0, 2, 4, 5, 7, 9, 11 } },
    { "dorian",     7, { 0, 2, 3, 5, 7, 9, 10 } },
    { "phrygian",   7, { 0, 1, 3, 5, 7, 8, 10 } },
    { "harm minor", 7, { 0, 2, 3, 5, 7, 8, 11 } },
    { "mixolydian", 7, { 0, 2, 4, 5, 7, 9, 10 } },
    { "min pent",   5, { 0, 3, 5, 7, 10 } },
    { "maj pent",   5, { 0, 2, 4, 7, 9 } },
    { "blues",      6, { 0, 3, 5, 6, 7, 10 } },
    { "chromatic", 12, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
};
const int tb3poNumScales = (int) (sizeof (tb3poScales) / sizeof (tb3poScales[0]));

namespace
{
    uint64_t splitmix (uint64_t x)
    {
        x += 0x9E3779B97F4A7C15ull;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        return x ^ (x >> 31);
    }

    enum Field { fGate = 1, fRepeat, fNote, fOctCoin, fSlide, fAccent };
}

double Tb3poCore::draw (int s, int field) const
{
    const uint64_t k = ((uint64_t) pattern.seed << 40)
                     ^ ((uint64_t) pattern.salt[(size_t) s] * 0x9E3779B97F4A7C15ull)
                     ^ ((uint64_t) s << 8) ^ (uint64_t) field;
    return (double) (splitmix (k) >> 11) * (1.0 / 9007199254740992.0);
}

void Tb3poCore::generate (const GenParams& g)
{
    // Module: density 0..14 internally, -7..+7 on the panel.
    const int d = std::clamp (g.density, -7, 7) + 7;
    const int scaleLen = std::max (1, g.scaleLen);

    // apply_density(): gate odds from the distance to the centre.
    const int onOff = std::abs (d - 7);
    const int densProb = 10 + onOff * 14;

    // regenerate_pitches(): how much of the scale is in play.
    const int pcd = std::min (d, 8);
    int available;
    if (pcd > 7)      available = scaleLen - 1;
    else if (pcd < 2) available = pcd;                    // root only, then root + 1
    else
    {
        const int range = std::max (4, scaleLen - 3);
        available = 3 + (pcd - 3) * range / 4;
    }
    available = std::clamp (available, std::min (1, scaleLen - 1), std::max (0, scaleLen - 1));
    if (pcd == 0) available = 0;

    const int repeatProb = 50 - pcd * 6;
    const double slideF = g.slide * 2.0, accentF = g.accent * 2.0;
    const double octThreshold = g.octaves * 200.0;        // module: coin < 80 of 200

    bool prevSlide = false, prevAccent = false;
    for (int s = 0; s < maxSteps; ++s)
    {
        Step& st = steps[(size_t) s];
        st.edited = 0;
        st.gate = draw (s, fGate) * 100.0 < densProb;

        if (s > 0 && draw (s, fRepeat) * 100.0 < repeatProb)
        {
            st.degree = steps[(size_t) s - 1].degree;
            st.octave = steps[(size_t) s - 1].octave;
        }
        else
        {
            st.degree = (uint8_t) std::min (available, (int) (draw (s, fNote) * (available + 1)));
            const double coin = draw (s, fOctCoin) * 200.0;
            st.octave = 0;
            if (coin < octThreshold)
                st.octave = ((int) (coin * 7.0) & 1) ? 1 : -1;
        }

        // Fewer consecutive slides / accents, as in apply_density().
        prevSlide  = draw (s, fSlide)  * 100.0 < (prevSlide  ? 10.0 : 18.0) * slideF;
        prevAccent = draw (s, fAccent) * 100.0 < (prevAccent ? 7.0  : 16.0) * accentF;
        st.slide  = prevSlide;
        st.accent = prevAccent;

        const auto& e = pattern.edits[(size_t) s];
        if (e.on)
        {
            st.edited = 1;
            st.gate   = e.kind != 0;
            st.accent = e.kind == 2;
            st.slide  = e.kind == 3;
            st.degree = (uint8_t) (e.degree % scaleLen);
            st.octave = (int8_t) std::clamp ((int) e.octave, -1, 1);
        }
        if (st.degree >= scaleLen) st.degree = (uint8_t) (st.degree % scaleLen);
    }
}

void Tb3poCore::pushUndo()
{
    undoPattern = pattern;
    undoValid = true;
}

bool Tb3poCore::undo()
{
    if (! undoValid) return false;
    std::swap (pattern, undoPattern);
    return true;
}

void Tb3poCore::newSeed (uint32_t entropy)
{
    pushUndo();
    uint16_t s = (uint16_t) (splitmix (entropy) & 0xFFFF);
    if (s == pattern.seed) s ^= 0x5A5A;
    pattern.seed = s;
    pattern.salt.fill (0);
    clearEdits();
}

void Tb3poCore::mutate (uint32_t entropy, float amount)
{
    pushUndo();
    uint64_t r = splitmix (entropy ^ ((uint64_t) pattern.seed << 32));
    const int n = std::max (1, (int) (maxSteps * amount + 0.5f));
    for (int i = 0; i < n; ++i)
    {
        r = splitmix (r);
        const int s = (int) (r % maxSteps);
        pattern.salt[(size_t) s] = (uint32_t) (r >> 32) | 1u;
        pattern.edits[(size_t) s].on = 0;
    }
}

void Tb3poCore::setStep (int i, int kind, int degree, int octave)
{
    if (i < 0 || i >= maxSteps) return;
    auto& e = pattern.edits[(size_t) i];
    e.on = 1;
    e.kind = (uint8_t) (kind & 3);
    e.degree = (uint8_t) std::clamp (degree, 0, 11);
    e.octave = (int8_t) std::clamp (octave, -1, 1);
}

void Tb3poCore::clearEdits()
{
    for (auto& e : pattern.edits) e = {};
}

void Tb3poCore::store (int b)
{
    if (b < 0 || b >= numBanks) return;
    banks[(size_t) b] = pattern;
    filled[(size_t) b] = true;
}

bool Tb3poCore::recall (int b)
{
    if (b < 0 || b >= numBanks || ! filled[(size_t) b]) return false;
    pushUndo();
    pattern = banks[(size_t) b];
    return true;
}

} // namespace acid
