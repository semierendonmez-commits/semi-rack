// CONDUIT's patch: rows are sources (any sibling's event or value output), columns are targets
// (any sibling's input), every crossing is a pin with AMOUNT, CURVE, DELAY, PROBABILITY and LENGTH.
// Plain data, no JUCE: the audio thread gets a copy, the tests build it by hand.
// SEMI Family Rack port: kRows 24 -> 15 and kCols 16 -> 8 (the cable module's sources / outputs).
#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace conduit
{
constexpr int kRows = 15;
constexpr int kCols = 8;
constexpr int kName = 16;

// what an endpoint is
enum EndType : uint8_t
{
    EndNone = 0,
    SrcEvent = 1,     // a row fed by events (kicks, seeds, steps, cuts ...)
    SrcValue = 2,     // a row fed by a value (EPOCH curves, a heat, a MIDI CC)
    DstValue = 3,     // a column that offsets a parameter (-1..1 added to it)
    DstTrigger = 4,   // a column that fires something (a reseed, a splice)
    DstGate = 5       // a column that holds something while above 0.5 (a mute)
};

inline bool isSource (uint8_t t) { return t == SrcEvent || t == SrcValue; }
inline bool isTarget (uint8_t t) { return t == DstValue || t == DstTrigger || t == DstGate; }

// the shape a pin gives its source. For an event row x runs 1 -> 0 over LENGTH (an envelope);
// for a value row x is the value. out = AMOUNT x shape(x).
enum Curve : uint8_t { CurveLin = 0, CurveExp, CurveLog, CurveS, CurveGate, CurveInv, kNumCurves };

inline const char* curveName (int c)
{
    static const char* n[] = { "LIN", "EXP", "LOG", "S", "GATE", "INV" };
    return c >= 0 && c < kNumCurves ? n[c] : "LIN";
}

// event = x is an envelope (1 at the hit, 0 after LENGTH; exactly 0 = not running)
inline float shape (int curve, float x, bool event)
{
    x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
    if (event && x <= 0.0f) return 0.0f;
    switch (curve)
    {
        case CurveExp:  return x * x * x;                       // falls fast: a pump, a ping
        case CurveLog:  return 1.0f - (1.0f - x) * (1.0f - x);  // holds, then falls
        case CurveS:    return x * x * (3.0f - 2.0f * x);
        case CurveGate: return event ? 1.0f : (x >= 0.5f ? 1.0f : 0.0f);   // on for LENGTH / a threshold
        case CurveInv:  return 1.0f - x;                        // an event: a swell up to the end
        default:        return x;
    }
}

// one end of a connection. kind = the plug-in ("SEEDWALK"), inst = which instance ("" or "*" any,
// else its name / track), port = the port ("KICK", "LEVEL"), index = only this `a` of an event
// (a lane, a note), -1 any. For EPOCH, inst is the channel letter.
struct Endpoint
{
    char kind[kName] {};
    char inst[kName] {};
    char port[kName] {};
    int16_t index = -1;
    uint8_t type = EndNone;
    uint8_t pad = 0;

    bool empty() const { return type == EndNone || kind[0] == 0; }
    static void put (char* dst, const char* src) { std::memset (dst, 0, kName); if (src) std::strncpy (dst, src, kName - 1); }
    static Endpoint make (const char* kind, const char* port, uint8_t type, const char* inst = "", int index = -1)
    {
        Endpoint e;
        put (e.kind, kind); put (e.port, port); put (e.inst, inst);
        e.type = type; e.index = (int16_t) index;
        return e;
    }
    std::string label() const
    {
        std::string s = std::string (kind);
        if (inst[0] != 0 && ! (inst[0] == '*' && inst[1] == 0)) s += std::string (":") + inst;
        s += std::string (" ") + port;
        if (index >= 0) s += " " + std::to_string (index + 1);
        return s;
    }
    bool sameAs (const Endpoint& o) const
    {
        return type == o.type && index == o.index && std::strncmp (kind, o.kind, kName) == 0
            && std::strncmp (inst, o.inst, kName) == 0 && std::strncmp (port, o.port, kName) == 0;
    }
};

struct Pin
{
    uint8_t on = 0;
    uint8_t curve = CurveLin;
    uint16_t pad = 0;
    float amount = 1.0f;   // -1..1
    float delay = 0.0f;    // in 16ths, 0..16
    float prob = 1.0f;     // 0..1: per event, or per 16th for a value row
    float len = 1.0f;      // event rows: the envelope's length in 16ths, 0.25..64
};

struct Patch
{
    char name[32] {};
    Endpoint row[kRows];
    Endpoint col[kCols];
    float base[kCols] {};          // where OUT n rests with nothing patched (0..1)
    Pin pin[kRows][kCols];
    uint32_t seed = 1;             // the dice of every PROB

    int usedRows() const { int n = 0; for (int r = 0; r < kRows; ++r) if (! row[r].empty()) n = r + 1; return n; }
    int usedCols() const { int n = 0; for (int c = 0; c < kCols; ++c) if (! col[c].empty()) n = c + 1; return n; }
    int numPins() const { int n = 0; for (auto& rr : pin) for (auto& p : rr) n += p.on ? 1 : 0; return n; }
    void setName (const char* s) { std::memset (name, 0, sizeof name); std::strncpy (name, s, sizeof name - 1); }

    // the row that is this endpoint, or the first free one (added), -1 full
    int findOrAddRow (const Endpoint& e)
    {
        for (int r = 0; r < kRows; ++r) if (row[r].sameAs (e)) return r;
        for (int r = 0; r < kRows; ++r) if (row[r].empty()) { row[r] = e; return r; }
        return -1;
    }
    int findOrAddCol (const Endpoint& e, float restAt = 0.0f)
    {
        for (int c = 0; c < kCols; ++c) if (col[c].sameAs (e)) return c;
        for (int c = 0; c < kCols; ++c) if (col[c].empty()) { col[c] = e; base[c] = restAt; return c; }
        return -1;
    }
    Pin& connect (const Endpoint& from, const Endpoint& to, float amount, int curve = CurveLin, float delay = 0.0f,
                  float prob = 1.0f, float len = 1.0f, float restAt = 0.0f)
    {
        static Pin dummy;
        const int r = findOrAddRow (from), c = findOrAddCol (to, restAt);
        if (r < 0 || c < 0) return dummy;
        auto& p = pin[r][c];
        p.on = 1; p.amount = amount; p.curve = (uint8_t) curve; p.delay = delay; p.prob = prob; p.len = len;
        return p;
    }
    void clearRow (int r) { row[r] = Endpoint {}; for (auto& p : pin[r]) p = Pin {}; }
    void clearCol (int c) { col[c] = Endpoint {}; base[c] = 0.0f; for (auto& rr : pin) rr[c] = Pin {}; }
};

} // namespace conduit
