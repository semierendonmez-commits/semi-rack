#include "CableTape.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace bbxcv
{

//==============================================================================
Tape::Tape (size_t capacity) : buf (capacity > 16 ? capacity : 16), cap (capacity > 16 ? capacity : 16) {}

uint64_t Tape::lowerBound (uint64_t lo, uint64_t hi, uint32_t time) const
{
    while (lo < hi)
    {
        const uint64_t mid = lo + (hi - lo) / 2;
        if (at (mid).t < time) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

bool Tape::copyOut (std::vector<Rec>& out, size_t maxRecords) const
{
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const uint32_t g0 = generation();
        const uint64_t w0 = end();
        const uint64_t lo = w0 > cap ? w0 - cap : 0;
        uint64_t n = w0 - lo;
        if (n > (uint64_t) maxRecords) n = (uint64_t) maxRecords;
        const uint64_t start = w0 - n;
        out.resize ((size_t) n);
        for (uint64_t i = 0; i < n; ++i) out[(size_t) i] = at (start + i);
        const uint64_t w1 = end();
        if (generation() != g0) continue;
        // the writer overwrote logical indices below w1 + 1 - cap while we copied (the +1: a write in flight)
        const uint64_t safe = w1 + 1 > cap ? w1 + 1 - cap : 0;
        if (safe > start)
        {
            const uint64_t drop = safe - start;
            if (drop >= n) continue;
            out.erase (out.begin(), out.begin() + (std::ptrdiff_t) drop);
        }
        return ! out.empty();
    }
    out.clear();
    return false;
}

//==============================================================================
namespace
{
void apply (LaneState& st, const Rec& r)
{
    if (r.lane >= kLanes) return;
    const int l = r.lane;
    switch (r.kind)
    {
        case KValue: st.value[l] = dec (r.v); st.known[l] = true; break;
        case KKey:
            if (isGateLane (l)) { st.on[l] = r.v > 0; st.value[l] = dec (r.v); }
            else st.value[l] = dec (r.v);
            st.known[l] = true;
            break;
        case KOn: st.on[l] = true; st.value[l] = dec (r.v); st.known[l] = true; break;
        case KOff: st.on[l] = false; st.known[l] = true; break;
        default: break;
    }
}

int lineIndex (float volts)   // 1..4 = 4, 8, 16, 32-bar lines
{
    return volts < 3.75f ? 1 : volts < 6.25f ? 2 : volts < 8.75f ? 3 : 4;
}

float voltsToBars (float v) { return v / 10.0f * 32.0f; }
}

void stateBefore (const Tape& tape, uint64_t idx, LaneState& st, int keyEvery)
{
    st = LaneState {};
    st.known[LEvent] = true;   // pulses are not a state
    int unresolved = kLanes - 1;
    const uint64_t window = (uint64_t) (keyEvery > 0 ? keyEvery : 1024) * 2 + 64;
    uint64_t lo = tape.begin();
    if (idx > window && idx - window > lo) lo = idx - window;
    for (uint64_t i = idx; i > lo && unresolved > 0;)
    {
        --i;
        const Rec& r = tape.at (i);
        if (r.lane >= kLanes || st.known[r.lane]) continue;
        if (r.kind == KValue || r.kind == KKey || r.kind == KOn || r.kind == KOff)
        {
            apply (st, r);
            --unresolved;
        }
    }
}

//==============================================================================
void Recorder::put (uint32_t t, int lane, int kind, int16_t v)
{
    Rec r;
    r.t = t;
    r.lane = (uint8_t) lane;
    r.kind = (uint8_t) kind;
    r.v = v;
    tape.push (r);
    if (kind != KKey && ++sinceKey >= settings.keyEvery) keyGroup (t);
}

void Recorder::keyGroup (uint32_t t)
{
    for (int l = 0; l < kLanes; ++l)
    {
        if (isValueLane (l) && haveVal[l]) put (t, l, KKey, enc (lastVal[l]));
        else if (isGateLane (l)) put (t, l, KKey, gate[l] ? enc (gateV[l] > 0.01f ? gateV[l] : 10.0f) : (int16_t) 0);
    }
    sinceKey = 0;
}

void Recorder::reset()
{
    msAcc = 0.0;
    tapeMs = 0;
    started = false;
    wasArmed = false;
    for (int l = 0; l < kLanes; ++l) { lastVal[l] = 0.0f; lastT[l] = 0; haveVal[l] = false; gate[l] = false; gateV[l] = 0.0f; }
    pulseHigh = false;
    sinceKey = 0;
    markCount = 0;
    markReq = false;
}

void Recorder::mark() { markReq = true; }

void Recorder::sample (const float* volts, bool armed, float sampleTime)
{
    if (! armed)
    {
        wasArmed = false;
        return;
    }
    const uint32_t t = tapeMs;
    if (! wasArmed)
    {
        // (re)armed: every value lane records where it is now, gates are compared afresh
        for (int l = 0; l < kLanes; ++l) haveVal[l] = false;
        if (! started) { started = true; sinceKey = 0; }
        wasArmed = true;
    }

    // gates and the EVENT pulse: every sample
    for (int l = LGate1; l <= LGate4; ++l)
    {
        const float v = volts[l];
        const bool g = gate[l] ? v > 0.1f : v > 1.0f;
        if (g != gate[l])
        {
            gate[l] = g;
            gateV[l] = g ? v : 0.0f;
            put (t, l, g ? KOn : KOff, g ? enc (v) : (int16_t) 0);
        }
    }
    {
        const float v = volts[LEvent];
        const bool high = pulseHigh ? v > 0.5f : v > 1.0f;
        if (high && ! pulseHigh) put (t, LEvent, KPulse, enc (v));
        pulseHigh = high;
    }
    if (markReq)
    {
        markReq = false;
        put (t, LMark, KMark, (int16_t) (++markCount & 0x7fff));
    }

    // value lanes: on the first sample and then once per ms
    const bool first = ! haveVal[LPos];
    msAcc += (double) sampleTime * 1000.0;
    bool tick = first;
    while (msAcc >= 1.0)
    {
        msAcc -= 1.0;
        ++tapeMs;
        tick = true;
    }
    if (! tick) return;
    const uint32_t tv = first ? t : tapeMs;
    for (int l = 0; l < kLanes; ++l)
    {
        if (! isValueLane (l)) continue;
        const float v = volts[l];
        if (! haveVal[l])
        {
            put (tv, l, KValue, enc (v));
            haveVal[l] = true; lastVal[l] = v; lastT[l] = tv;
            continue;
        }
        const float d = std::fabs (v - lastVal[l]);
        const float db = l == LScene ? 0.2f : settings.deadband;
        if (d >= db && (d > 0.5f || l == LScene || (float) (tv - lastT[l]) >= settings.rateMs))
        {
            put (tv, l, KValue, enc (v));
            lastVal[l] = v; lastT[l] = tv;
        }
    }
}

//==============================================================================
void Player::start (uint32_t fromMs, int keyEvery)
{
    gen = tape.generation();
    const uint64_t lo = tape.begin(), hi = tape.end();
    idx = tape.lowerBound (lo, hi, fromMs);
    stateBefore (tape, idx, state, keyEvery);
    nowMs = fromMs;
    pulseV = pulseLeft = 0.0f;
    for (int l = 0; l < kLanes; ++l) { gateMin[l] = 0.0f; offPending[l] = false; }
    playing = true;
}

bool Player::sample (float sampleTime, uint32_t untilMs)
{
    if (! playing) return false;
    if (tape.generation() != gen) { playing = false; return false; }
    nowMs += (double) sampleTime * 1000.0;
    const uint64_t hi = tape.end();
    if (idx < tape.begin()) idx = tape.begin();   // rolled off under us (should not happen while paused)
    while (idx < hi)
    {
        const Rec& r = tape.at (idx);
        if ((double) r.t > nowMs) break;
        if (r.lane < kLanes)
        {
            const int l = r.lane;
            if (r.kind == KPulse) { pulseV = dec (r.v); pulseLeft = 0.001f; }
            else if (r.kind == KOn) { state.on[l] = true; state.value[l] = dec (r.v); gateMin[l] = 0.001f; offPending[l] = false; }
            else if (r.kind == KOff) { if (gateMin[l] > 0.0f) offPending[l] = true; else state.on[l] = false; }
            else apply (state, r);
        }
        ++idx;
    }
    for (int l = LGate1; l <= LGate4; ++l)
        if (gateMin[l] > 0.0f)
        {
            gateMin[l] -= sampleTime;
            if (gateMin[l] <= 0.0f && offPending[l]) { state.on[l] = false; offPending[l] = false; }
        }
    if (pulseLeft > 0.0f) pulseLeft -= sampleTime;
    if (nowMs >= (double) untilMs) { playing = false; return false; }
    return true;
}

float Player::out (int lane) const
{
    if (lane == LEvent) return pulseLeft > 0.0f ? pulseV : 0.0f;
    if (isGateLane (lane)) return state.on[lane] ? state.value[lane] : 0.0f;
    return lane >= 0 && lane < kLanes ? state.value[lane] : 0.0f;
}

//==============================================================================
void buildView (const Tape& tape, uint32_t t0, uint32_t t1, int keyEvery, View& v)
{
    v = View {};
    v.t0 = t0;
    v.t1 = t1;
    for (auto& s : v.scene) s = -1;
    if (t1 <= t0) return;
    const uint64_t lo = tape.begin(), hi = tape.end();
    if (hi <= lo) return;
    uint64_t idx = tape.lowerBound (lo, hi, t0);
    LaneState st;
    stateBefore (tape, idx, st, keyEvery);
    int prevBar = st.known[LPos] ? (int) std::floor (voltsToBars (st.value[LPos])) : -1;
    const double span = (double) (t1 - t0);
    for (int c = 0; c < kViewCols; ++c)
    {
        const double te = (double) t0 + span * (c + 1) / kViewCols;
        uint8_t seen = 0;
        for (int k = 0; k < 4; ++k) if (st.on[LGate1 + k]) seen |= (uint8_t) (1u << k);
        while (idx < hi)
        {
            const Rec& r = tape.at (idx);
            if ((double) r.t >= te) break;
            if (r.kind == KPulse && r.lane == LEvent) v.event[c] = (uint8_t) std::max<int> (v.event[c], lineIndex (dec (r.v)));
            else if (r.kind == KMark) v.mark[c] = 1;
            else
            {
                apply (st, r);
                if (r.kind == KOn && isGateLane (r.lane)) seen |= (uint8_t) (1u << (r.lane - LGate1));
                if (r.lane == LPos && (r.kind == KValue || r.kind == KKey))
                {
                    const int bar = (int) std::floor (voltsToBars (st.value[LPos]));
                    if (prevBar >= 0 && bar != prevBar)
                        v.bar[c] = (uint8_t) std::max<int> (v.bar[c], (bar % 8 == 0) ? 2 : 1);
                    prevBar = bar;
                }
            }
            ++idx;
        }
        v.energy[c] = st.value[LEnergy];
        v.scene[c] = st.known[LScene] ? (int8_t) std::max (0, std::min (99, (int) std::lround (st.value[LScene] / 0.5f))) : (int8_t) -1;
        for (int k = 0; k < 4; ++k) v.cv[k][c] = st.value[LCv1 + k];
        v.gates[c] = seen;
    }
    v.any = true;
}

//==============================================================================
// Standard MIDI File, format 1 (the writer follows BLACKBOX's Export.cpp)
namespace
{
struct Ev
{
    int64_t tick;
    int order;
    uint8_t b[8];
    uint8_t n;
    std::string text;   // meta text (empty otherwise)
};

struct Track
{
    std::string name;
    int channel = 0;
    std::vector<Ev> ev;
    int notes = 0, ccs = 0;

    void raw (int64_t tick, int order, std::initializer_list<uint8_t> bytes)
    {
        Ev e { tick, order, {}, 0, {} };
        for (uint8_t x : bytes) if (e.n < 8) e.b[e.n++] = x;
        ev.push_back (e);
    }
    void meta (int64_t tick, uint8_t type, const std::string& text)
    {
        Ev e { tick, 0, { 0xFF, type }, 2, text.size() > 127 ? text.substr (0, 127) : text };
        ev.push_back (e);
    }
    void on (int64_t tick, int note, int vel)
    {
        raw (tick, 2, { (uint8_t) (0x90 | channel), (uint8_t) std::clamp (note, 0, 127), (uint8_t) std::clamp (vel, 1, 127) });
        ++notes;
    }
    void off (int64_t tick, int note) { raw (tick, 1, { (uint8_t) (0x80 | channel), (uint8_t) std::clamp (note, 0, 127), 0 }); }
    void cc (int64_t tick, int num, int v)
    {
        raw (tick, 3, { (uint8_t) (0xB0 | channel), (uint8_t) std::clamp (num, 0, 127), (uint8_t) std::clamp (v, 0, 127) });
        ++ccs;
    }
    void program (int64_t tick, int p) { raw (tick, 3, { (uint8_t) (0xC0 | channel), (uint8_t) (p & 127) }); }
};

void vlq (std::vector<uint8_t>& o, uint32_t v)
{
    uint8_t buf[5];
    int n = 0;
    buf[n++] = v & 0x7F;
    while ((v >>= 7) != 0) buf[n++] = (uint8_t) (0x80 | (v & 0x7F));
    while (n-- > 0) o.push_back (buf[n]);
}
void be32 (std::vector<uint8_t>& o, uint32_t v) { for (int s = 24; s >= 0; s -= 8) o.push_back ((uint8_t) (v >> s)); }
void be16 (std::vector<uint8_t>& o, uint16_t v) { o.push_back ((uint8_t) (v >> 8)); o.push_back ((uint8_t) v); }

std::vector<uint8_t> writeSmf (std::vector<Track>& tracks, int division)
{
    std::vector<uint8_t> o { 'M', 'T', 'h', 'd' };
    be32 (o, 6);
    be16 (o, 1);
    be16 (o, (uint16_t) tracks.size());
    be16 (o, (uint16_t) division);
    for (auto& t : tracks)
    {
        std::stable_sort (t.ev.begin(), t.ev.end(), [] (const Ev& a, const Ev& b) { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });
        std::vector<uint8_t> d;
        int64_t last = 0;
        for (const auto& e : t.ev)
        {
            const int64_t tk = std::max<int64_t> (0, e.tick);
            vlq (d, (uint32_t) (tk - last));
            last = tk;
            d.insert (d.end(), e.b, e.b + e.n);
            if (e.n == 2 && e.b[0] == 0xFF)
            {
                if (e.b[1] == 0x51 || e.b[1] == 0x58) continue;   // fixed-size metas carry their bytes in text
                vlq (d, (uint32_t) e.text.size());
                d.insert (d.end(), e.text.begin(), e.text.end());
            }
        }
        vlq (d, 0);
        d.push_back (0xFF); d.push_back (0x2F); d.push_back (0);
        o.insert (o.end(), { 'M', 'T', 'r', 'k' });
        be32 (o, (uint32_t) d.size());
        o.insert (o.end(), d.begin(), d.end());
    }
    return o;
}
}

MidiResult exportMidi (const std::vector<Rec>& recs, const MidiOptions& opt)
{
    MidiResult res;
    uint32_t t0 = opt.t0, t1 = opt.t1;
    if (! recs.empty())
    {
        t0 = std::max (t0, recs.front().t);
        t1 = std::min (t1, recs.back().t);
    }
    if (t1 < t0) t1 = t0;
    const int div = opt.division > 0 ? opt.division : 960;

    // the state at the region's start, and where the region starts in recs
    LaneState st;
    st.known[LEvent] = true;
    size_t i0 = 0;
    while (i0 < recs.size() && recs[i0].t < t0) apply (st, recs[i0++]);
    size_t i1 = i0;
    while (i1 < recs.size() && recs[i1].t <= t1) ++i1;

    // the tempo: how fast POS moved through the region (bars -> quarter notes)
    {
        double bars = 0.0;
        float prev = st.known[LPos] ? voltsToBars (st.value[LPos]) : -1.0f;
        uint32_t ta = 0, tb = 0;
        bool have = false;
        for (size_t i = i0; i < i1; ++i)
        {
            const Rec& r = recs[i];
            if (r.lane != LPos || (r.kind != KValue && r.kind != KKey)) continue;
            const float b = voltsToBars (dec (r.v));
            if (prev >= 0.0f)
            {
                float d = b - prev;
                if (d < -16.0f) d += 32.0f;
                if (d > 0.0f && d < 2.0f)
                {
                    if (! have) { ta = r.t; have = true; }
                    bars += d;
                    tb = r.t;
                }
            }
            prev = b;
        }
        if (have && bars > 0.5 && tb > ta + 500)
        {
            res.bpm = std::clamp (bars * 4.0 / ((double) (tb - ta) / 60000.0), 40.0, 300.0);
            res.tempoFromBus = true;
        }
    }
    const double ticksPerMs = res.bpm / 60000.0 * div;
    auto tick = [&] (uint32_t t) { return (int64_t) std::llround ((double) (t - std::min (t, t0)) * ticksPerMs); };
    const int64_t endTick = tick (t1) + 1;

    std::vector<Track> tracks (6);
    Track& cond = tracks[0];
    cond.name = "BLACKBOX";
    Track& bus = tracks[1];
    bus.name = "BUS";
    bus.channel = 0;
    for (int k = 0; k < 4; ++k)
    {
        tracks[(size_t) (2 + k)].name = opt.pitched[k] ? "LANE " + std::to_string (k + 1) + " (V/OCT)" : "LANE " + std::to_string (k + 1);
        tracks[(size_t) (2 + k)].channel = k + 1;
    }
    {
        const uint32_t us = (uint32_t) std::llround (60000000.0 / res.bpm);
        Ev e { 0, 0, { 0xFF, 0x51, 0x03, (uint8_t) (us >> 16), (uint8_t) (us >> 8), (uint8_t) us }, 6, {} };
        cond.ev.push_back (e);
        Ev ts { 0, 0, { 0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08 }, 7, {} };
        cond.ev.push_back (ts);
        cond.meta (0, 0x03, "BLACKBOX");
        bus.meta (0, 0x03, "BUS");
        for (int k = 0; k < 4; ++k) tracks[(size_t) (2 + k)].meta (0, 0x03, tracks[(size_t) (2 + k)].name);
    }

    auto ccOf = [&] (float volts) { return (int) std::lround (std::clamp (opt.cvBipolar ? (volts + 5.0f) / 10.0f : volts / 10.0f, 0.0f, 1.0f) * 127.0f); };
    auto noteOf = [&] (int k) { return opt.pitched[k] ? std::clamp (60 + (int) std::lround (st.value[LCv1 + k] * 12.0f), 0, 127) : 36 + k; };

    // the state at tick 0
    int lastEnergy = -1, lastScene = -1, lastCc[4] = { -1, -1, -1, -1 };
    int held[4] = { -1, -1, -1, -1 };
    if (st.known[LEnergy]) { lastEnergy = ccOf (st.value[LEnergy]); bus.cc (0, 20, lastEnergy); }
    if (st.known[LScene])
    {
        lastScene = (int) std::lround (st.value[LScene] / 0.5f);
        bus.program (0, lastScene);
        cond.meta (0, 0x06, "SCENE " + std::to_string (lastScene));
    }
    for (int k = 0; k < 4; ++k)
    {
        if (! opt.pitched[k] && st.known[LCv1 + k]) { lastCc[k] = ccOf (st.value[LCv1 + k]); tracks[(size_t) (2 + k)].cc (0, 70 + k, lastCc[k]); }
        if (st.on[LGate1 + k])
        {
            held[k] = noteOf (k);
            tracks[(size_t) (2 + k)].on (0, held[k], (int) std::lround (st.value[LGate1 + k] / 10.0f * 127.0f));
        }
    }

    for (size_t i = i0; i < i1; ++i)
    {
        const Rec& r = recs[i];
        const int64_t tk = tick (r.t);
        if (r.kind == KMark) { cond.meta (tk, 0x06, "MARK " + std::to_string ((int) r.v)); continue; }
        if (r.lane >= kLanes) continue;
        const int l = r.lane;
        if (r.kind == KPulse && l == LEvent)
        {
            bus.on (tk, 48 + lineIndex (dec (r.v)) - 1, 100);
            bus.off (tk + div / 4, 48 + lineIndex (dec (r.v)) - 1);
            continue;
        }
        if (isGateLane (l))
        {
            const int k = l - LGate1;
            Track& tr = tracks[(size_t) (2 + k)];
            if (r.kind == KOn)
            {
                if (held[k] >= 0) tr.off (tk, held[k]);
                apply (st, r);
                held[k] = noteOf (k);
                tr.on (tk, held[k], (int) std::lround (dec (r.v) / 10.0f * 127.0f));
            }
            else if (r.kind == KOff)
            {
                apply (st, r);
                if (held[k] >= 0) tr.off (std::max<int64_t> (tk, 1), held[k]);
                held[k] = -1;
            }
            continue;
        }
        apply (st, r);
        if (l == LEnergy)
        {
            const int v = ccOf (st.value[l]);
            if (v != lastEnergy) { bus.cc (tk, 20, v); lastEnergy = v; }
        }
        else if (l == LScene)
        {
            const int s = (int) std::lround (st.value[l] / 0.5f);
            if (s != lastScene)
            {
                bus.program (tk, s);
                cond.meta (tk, 0x06, "SCENE " + std::to_string (s));
                lastScene = s;
            }
        }
        else if (l >= LCv1 && l <= LCv4)
        {
            const int k = l - LCv1;
            if (opt.pitched[k]) continue;
            const int v = ccOf (st.value[l]);
            if (v != lastCc[k]) { tracks[(size_t) (2 + k)].cc (tk, 70 + k, v); lastCc[k] = v; }
        }
    }
    for (int k = 0; k < 4; ++k)
        if (held[k] >= 0) tracks[(size_t) (2 + k)].off (endTick, held[k]);

    // keep the conductor, BUS when it has anything, and the lanes that played
    std::vector<Track> keep;
    keep.push_back (std::move (tracks[0]));
    for (size_t k = 1; k < tracks.size(); ++k)
        if (tracks[k].notes + tracks[k].ccs > 0 || (k == 1 && lastScene >= 0)) keep.push_back (std::move (tracks[k]));
    for (auto& t : keep) { res.notes += t.notes; res.ccs += t.ccs; }
    res.tracks = (int) keep.size();
    res.bytes = writeSmf (keep, div);
    return res;
}

} // namespace bbxcv
