// Notes, clips and drum roles. Plain C++20, no JUCE.
// Time is in beats (quarter notes) from the start of the clip.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace dr2
{

enum Tag : int { Same = 0, Changed = 1, Added = 2 };

struct Note
{
    double start = 0.0, length = 0.25;
    int pitch = 36;
    float velocity = 0.8f;
    int tag = Same;          // what DROP II did to it (the editor colours by it)
};

struct Clip
{
    std::vector<Note> notes;
    double length = 16.0;    // beats; whole bars
    int bars() const { return std::max (1, (int) std::lround (length / 4.0)); }
    bool empty() const { return notes.empty(); }
    void sort() { std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start || (a.start == b.start && a.pitch < b.pitch); }); }
};

enum class Role : int { Kick, Snare, Hat, Perc };

// Which note is the kick, which is the snare. General MIDI first; when the clip
// does not use GM (a slicer's pads, a drum rack laid out its own way), the note
// heard most on the "one" is the kick and the one most on beats 2 and 4 is the snare.
struct DrumRoles
{
    int kick = 36, snare = 38;
    std::array<bool, 128> hat {};
    Role of (int pitch) const
    {
        if (pitch == kick) return Role::Kick;
        if (pitch == snare) return Role::Snare;
        if (pitch >= 0 && pitch < 128 && hat[(size_t) pitch]) return Role::Hat;
        return Role::Perc;
    }
};

inline bool near (double a, double b, double tol = 0.03) { return std::abs (a - b) <= tol; }
inline double wrapBeat (double t) { return t - 4.0 * std::floor (t / 4.0); }

inline DrumRoles detectRoles (const Clip& drums)
{
    DrumRoles r;
    std::map<int, int> count, onOne, onBack;
    for (const auto& n : drums.notes)
    {
        count[n.pitch]++;
        const double b = wrapBeat (n.start);
        if (near (b, 0.0) || near (b, 4.0)) onOne[n.pitch]++;
        if (near (b, 1.0) || near (b, 3.0)) onBack[n.pitch]++;
    }
    auto has = [&] (int p) { return count.count (p) > 0; };
    auto best = [&] (std::map<int, int>& m, int exclude)
    {
        int bp = -1, bc = 0;
        for (auto& [p, c] : m) if (p != exclude && c > bc) { bc = c; bp = p; }
        return bp;
    };
    const bool gm = (has (35) || has (36)) && (has (38) || has (40) || has (37) || has (39));
    if (gm)
    {
        r.kick = has (36) ? 36 : 35;
        r.snare = has (38) ? 38 : (has (40) ? 40 : (has (39) ? 39 : 37));
        for (int p : { 42, 44, 46, 51, 53, 59, 49, 57, 55, 52 }) r.hat[(size_t) p] = true;
    }
    else
    {
        r.kick = best (onOne, -1);
        r.snare = best (onBack, r.kick);
        if (r.kick < 0 && ! count.empty()) r.kick = count.begin()->first;
        // hats: the busiest of what is left
        const double perBeat = drums.length > 0 ? 1.0 / drums.length : 0.0;
        for (auto& [p, c] : count)
            if (p != r.kick && p != r.snare && c * perBeat >= 1.0) r.hat[(size_t) p] = true;
    }
    return r;
}

} // namespace dr2
