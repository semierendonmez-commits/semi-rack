// SEMI port: vendored from SPLICEBUS Source/engine (9cefada); namespaces sb -> spb, substrate -> spb_substrate; see sources.mk.
// SPLICEBUS: FLOATER's REEL on any channel.
//
//   input ─► SPLIT (OFF / FIXED / TRACK) ─► low band ───────────────────────────────┐
//                         └─► upper band ─► REEL (splices, genes, varispeed, morph) ─┴─► MIX ─► out
//                                               ▲            ▲
//        clock (host / FREE) on 16ths ─ FORM: CHAOS, TURNS (4/8/16/32), BUDGET, ROLL, WALL, ANCHOR, LOOP, SEED
//        input hits / sidechain ─ PROTECT (the guard: a hit comes through as it was played)
//
// Every decision about a gene (does it break, which splice, which way, how fast, a roll, a stop) is a
// pure function of the song position and SEED (LOOP folds the position), worked out a 4-bar line at a
// time, so a bounce is the same every time, starting playback in the middle of a song gives the bar
// what it would have had, and the editor can draw the breaks before they happen.
#pragma once

#include "Dsp.h"
#include "Reel.h"
#include "Splitter.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace spb
{

struct Transport
{
    bool playing = false;
    double bpm = 120.0;
    double ppq = 0.0;              // position at the block's first sample (quarter notes)
    bool hostHasPosition = false;
};

inline float hash01 (uint32_t a, uint32_t b, uint32_t c = 0, uint32_t d = 0)
{
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du ^ (d + 0x27D4EB2Fu) * 0x9E3779B9u;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return (float) (h >> 8) * (1.0f / 16777216.0f);
}

inline const std::array<const char*, 11>& budgetNames()
{
    static const std::array<const char*, 11> n { "FREE", "1", "2", "3", "4", "6", "8", "12", "16", "24", "32" };
    return n;
}
inline int budgetCount (int i) { static const int v[11] { 0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32 }; return v[i < 0 ? 0 : (i > 10 ? 10 : i)]; }
inline const std::array<const char*, 5>& anchorNames() { static const std::array<const char*, 5> n { "OFF", "EVERY BAR", "2 BARS", "4 BARS", "8 BARS" }; return n; }
inline int anchorBars (int i) { static const int v[5] { 0, 1, 2, 4, 8 }; return v[i < 0 ? 0 : (i > 4 ? 4 : i)]; }
inline const std::array<const char*, 6>& loopNames() { static const std::array<const char*, 6> n { "SONG", "1 BAR", "2 BARS", "4 BARS", "8 BARS", "16 BARS" }; return n; }
inline int loopBars (int i) { static const int v[6] { 0, 1, 2, 4, 8, 16 }; return v[i < 0 ? 0 : (i > 5 ? 5 : i)]; }
inline const std::array<const char*, 3>& wallNames() { static const std::array<const char*, 3> n { "OFF", "GAP", "TAPE STOP" }; return n; }
inline const std::array<const char*, 3>& splitNames() { static const std::array<const char*, 3> n { "OFF", "FIXED", "TRACK" }; return n; }
inline const std::array<const char*, 4>& protectNames() { static const std::array<const char*, 4> n { "OFF", "HITS", "SC HITS", "SC LEVEL" }; return n; }

struct Params
{
    // the reel
    float chaos = 0.35f;          // ORDER <-> CHAOS: how many genes break anywhere, and how hard
    int length = 1;               // 1 / 2 / 4 / 8 bars of splices
    int gene = 1;                 // 1/32 .. 1 bar
    float varispeed = 0.75f;
    float morph = 0.45f;
    float organize = 0.0f;        // which splice the genes read
    float slide = 0.0f;           // where in the splice
    bool hold = false;
    // the form (TURNAROUND's hierarchy)
    float turns = 0.5f;           // breaks gather before 4 / 8 / 16 / 32-bar lines, harder before the bigger ones
    float roll = 0.5f;            // a turn's break is a roll of what just went by, faster towards the line
    int budget = 0;               // breaks per 4-bar line (0 = no limit); TURNS keeps a share for the turn
    int wall = 0;                 // the last beat before 16 / 32: off, a gap, a tape stop
    int anchor = 3;               // the first beat of every N-bar line stays clean
    int loop = 0;                 // the breaks repeat every N bars (0: the song decides)
    uint32_t seed = 0x5B1CEu;
    // the split
    int split = splitFixed;
    float splitHz = 120.0f;       // FIXED
    float ratio = 1.5f;           // TRACK: the split at RATIO x f0
    float f0Max = 220.0f;
    // protect
    int protectBy = 1;
    float protect = 0.8f;         // how much of the guard reaches the reel
    float protectRel = 0.08f;     // seconds
    float sens = 0.5f;            // the hit detector
    // out
    float mix = 1.0f;
    float outDb = 0.0f;
    bool run = false;             // FREE: run on the BPM below when the host is stopped
    float bpm = 174.0f;
};

// What the form decides for one gene slot.
struct Decision
{
    bool broken = false, turn = false, roll = false, anchored = false, wall = false, stop = false;
};

class Engine
{
public:
    static constexpr int kChunk = 256;
    static constexpr int kSlots = 128;                 // gene slots in a 4-bar line at most (1/32 genes)

    Engine();
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void setParams (const Params& p);
    const Params& getParams() const { return params; }
    int latency() const { return splitter.latencyFor (params.split); }

    // sidechain may be null. in / out may alias.
    void process (const float* inL, const float* inR, const float* scL, const float* scR,
                  float* outL, float* outR, int n, const Transport& t);

    // ---- the form, as a pure function of the song position (the editor calls it too)
    // a 4-bar line's gene slots: index = 16th within the line * 2 + half
    struct Line { std::array<Decision, kSlots> slot {}; int used = 0, budget = 0; };
    static void planLine (const Params& p, int64_t line, Line& out);
    static float turnIntensity (const Params& p, int64_t k, int* tier = nullptr, int* toLine = nullptr);
    static bool isSlot (const Params& p, int64_t k, int half);

    struct Display
    {
        std::atomic<bool> running { false }, host { false };
        std::atomic<float> bpm { 120.0f }, splitHz { 0.0f }, f0 { 0.0f }, conf { 0.0f }, guard { 0.0f };
        std::atomic<double> ppq { 0.0 };
        std::atomic<int64_t> sixteenth { 0 };
        std::atomic<int> hits { 0 }, turnTier { 0 }, turnIn { 64 }, spent { 0 };
        std::atomic<float> lowLevel { 0.0f }, upLevel { 0.0f }, outLevel { 0.0f };
        static constexpr int kScope = 2048;
        std::array<float, kScope> scopeLow {}, scopeUp {};
        std::atomic<int> scopeWrite { 0 };
    } display;
    Reel reel;

private:
    void processChunk (const float* inL, const float* inR, const float* scL, const float* scR, float* outL, float* outR, int n, const Transport& t);
    void tick (int64_t k, int64_t at, double tickLen);
    void geneAt (int64_t k, int64_t at, double tickLen, int half);
    void detect (const float* inL, const float* inR, const float* scL, const float* scR, int n);
    static float rnd (const Params& p, int64_t k, uint32_t purpose, int salt);

    Params params;
    double sr = 48000.0;
    int64_t now = 0;               // samples processed (the reel's timeline)
    int64_t lastTick = INT64_MIN;
    double internalPpq = 0.0;
    bool wasRunning = false;
    Line line;
    int64_t lineIndex = INT64_MIN;
    uint64_t planKey = 0;
    Splitter splitter;
    std::vector<float> lowL, lowR, upL, upR, dryL, dryR, guardBuf, guardRing;
    int guardMask = 0;
    int64_t guardW = 0;
    // the hit detector
    float fastEnv = 0.0f, slowEnv = 0.0f, fastA = 0.0f, fastR = 0.0f, slowA = 0.0f, slowR = 0.0f, scEnv = 0.0f;
    int refractory = 0, holdLeft = 0;
    dsp::Svf detHpA, detHpB;
    float guardEnv = 0.0f, guardOut = 0.0f;
    float outGain = 1.0f;
};

} // namespace spb
