// DUBPLATE — arrangement model. SEMI Family port of Source/Model/Arrangement.cpp: the
// section / lane bookkeeping is unchanged; key helpers, Analysis and toVar / fromVar
// (JUCE state) are dropped, isLoop () uses the role preference only (no samples).
#include "Arrangement.h"
#include <algorithm>

namespace dubplate
{
bool Lane::isLoop() const
{
    if (mode == PlayMode::Loop)    return true;
    if (mode == PlayMode::OneShot) return false;
    return roleInfo (role).prefersLoop;
}

int Arrangement::totalBars() const
{
    int n = 0;
    for (auto& s : sections)
        n += s.bars;
    return std::max (1, n);
}

int Arrangement::sectionIndexAtBar (int bar, int* startOut) const
{
    int start = 0;
    for (int i = 0; i < (int) sections.size(); ++i)
    {
        if (bar < start + sections[(size_t) i].bars)
        {
            if (startOut != nullptr) *startOut = start;
            return i;
        }
        start += sections[(size_t) i].bars;
    }
    if (startOut != nullptr) *startOut = start;
    return -1;
}

int Arrangement::sectionStartBar (int index) const
{
    int start = 0;
    for (int i = 0; i < index && i < (int) sections.size(); ++i)
        start += sections[(size_t) i].bars;
    return start;
}

Lane* Arrangement::findLane (int uid)
{
    for (auto& l : lanes)
        if (l.uid == uid) return &l;
    return nullptr;
}

const Lane* Arrangement::findLane (int uid) const
{
    for (auto& l : lanes)
        if (l.uid == uid) return &l;
    return nullptr;
}

int Arrangement::laneIndex (int uid) const
{
    for (int i = 0; i < (int) lanes.size(); ++i)
        if (lanes[(size_t) i].uid == uid) return i;
    return -1;
}

void Arrangement::resizeCells()
{
    for (auto& l : lanes)
        l.cells.resize (sections.size());
}

Lane& Arrangement::addLane (Role role)
{
    Lane l;
    l.uid = nextUid++;
    l.role = role;
    l.bus = roleInfo (role).bus;
    l.duck = roleInfo (role).duckedByKick ? 1.0f : 0.0f;
    l.sync = roleInfo (role).bus == Bus::Drums ? LoopSync::Slice : LoopSync::Varispeed;
    // Automatic gain staging per role: foundation louder, decoration lower, headroom for the master.
    switch (roleInfo (role).bus)
    {
        case Bus::Drums: l.gainDb = (role == Role::Kick || role == Role::Snare || role == Role::Clap) ? -3.0f : -6.0f; break;
        case Bus::Bass:  l.gainDb = (role == Role::Sub || role == Role::KickSub) ? -4.0f : -6.0f; break;
        case Bus::Music: l.gainDb = -8.0f; break;
        case Bus::FX:
        case Bus::numBuses:
        default:         l.gainDb = -9.0f; break;
    }
    l.cells.resize (sections.size());
    lanes.push_back (std::move (l));
    return lanes.back();
}

void Arrangement::removeLane (int uid)
{
    lanes.erase (std::remove_if (lanes.begin(), lanes.end(), [uid] (const Lane& l) { return l.uid == uid; }), lanes.end());
    for (auto& l : lanes)
        if (l.variationOf == uid) l.variationOf = -1;
}

int Arrangement::addVariation (int parentUid)
{
    auto idx = laneIndex (parentUid);
    if (idx < 0)
        return -1;

    Lane copy = lanes[(size_t) idx];
    copy.uid = nextUid++;
    copy.variationOf = parentUid;
    copy.alt = AltRule::Every8;
    lanes.insert (lanes.begin() + idx + 1, std::move (copy));
    return lanes[(size_t) idx + 1].uid;
}
} // namespace dubplate
