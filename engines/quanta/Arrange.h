// QUANTA - arrangement generator and player (no JUCE).
//
// Eight channels. Each has a role (SINE, PULSE, DATA, NOISE, CLOUD), a
// register (LOW / MID / HIGH) and its own sound (a Scene, edited live).
// The arrangement is a row of blocks; a block says which channels play, at
// which pitch and with which 16-step pattern, how much energy it has, and
// which fill leads out of it. A block with no channels is VOID.
//   ACCUMULATE  add a channel per block to saturation, cut, VOID, one tone
//   ALTERNATE   switch between two channel sets; lengths from a bit string
//   PERMUTE     a channel mask whose bits flip block by block
//   CODEX       beat sections of 8/16 bars: kick, click, data, breaks, drops
//   MARKOV      each channel walks on / off by itself
//   ARC         an energy curve: intro, build, drop, break, build, drop, outro
// Fills (the last 1 / 2 / 4 beats of a block): ROLL, BREAK, RISE, DROPOUT
// on the patterns; REPEAT, REVERSE, TAPE, SWEEP, FILTER, CRUSH, THROW as
// master effects.
#pragma once
#include "Particles.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace qn
{
enum BlockType { BSine = 0, BPulse, BData, BNoise, BCloud, kNumBlockTypes, BNone = -1 };
enum Register { RegLow = 0, RegMid, RegHigh, kNumRegs };
constexpr int kLanes = 8;
enum Form { Accumulate = 0, Alternate, Permute, Codex, Markov, Arc, kNumForms };
enum CutQ { CutStep = 0, CutBeat, CutBar };
enum Fill { FillNone = 0, FillRoll, FillBreak, FillRise, FillDropout,
            FillRepeat, FillReverse, FillTape, FillSweep, FillFilter, FillCrush, FillThrow, kNumFills };
inline bool isFxFill (int f) { return f >= FillRepeat; }

const char* blockTypeName (int t);   // "SINE" ... ; BNone -> "--"
const char* regName (int r);
const char* formName (int f);
const char* fillName (int f);
Scene factoryScene (int blockType, int reg = RegMid);

struct ChannelDef { int role = BSine; int reg = RegMid; };
std::array<ChannelDef, kLanes> defaultChannels();
const char* defaultChannelName (int ch);

struct Lane
{
    int type = BNone;
    float hz = 1000.0f;
    uint16_t pattern = 0xffff;
    bool operator== (const Lane& o) const { return type == o.type && (type == BNone || (hz == o.hz && pattern == o.pattern)); }
    bool operator!= (const Lane& o) const { return ! (*this == o); }
};

struct Block
{
    double beats = 32.0;
    std::array<Lane, kLanes> lanes {};
    bool locked = false;
    float energy = 0.5f;
    int fill = FillNone;
    double fillBeats = 1.0;
    bool isVoid() const { for (auto& l : lanes) if (l.type != BNone) return false; return true; }
    int count() const { int n = 0; for (auto& l : lanes) n += l.type != BNone; return n; }
};

struct GenParams
{
    int form = Accumulate;
    int lengthBars = 64;
    int blockBars = 8;
    bool pow2 = true;
    int seed = 0;
    int mutation = 2;
    float extremity = 50.0f;   // %
    float silence = 15.0f;     // %
    int cutq = CutBar;
    float fills = 50.0f;       // % chance of a fill at a block end
    float fxMix = 50.0f;       // % of fills that are master effects
    std::array<ChannelDef, kLanes> channels = defaultChannels();
    uint64_t hash() const;
};

struct Arrangement
{
    std::vector<Block> blocks;
    uint64_t genHash = 0;
    double totalBeats() const { double t = 0; for (auto& b : blocks) t += b.beats; return t; }
    int blockAt (double beats, double* blockStart = nullptr) const;   // -1 past the end
    std::string toString() const;
    static Arrangement fromString (const std::string&);
};

Arrangement generate (const GenParams&, const Arrangement* keep = nullptr);
uint16_t mutatePattern (uint16_t p, int bits, uint32_t& rng);
Lane makeLane (int role, int reg, uint32_t seed, float extremity);   // UI edits

// Live mix per channel, set by the processor every block.
struct ChannelMix { float gain = 1.0f; float tune = 0.0f; };

// Manual (pad) effect: wins over fills while held.
struct ManualFx { int mode = FxNone; double sliceBeats = 0.5; float progress = 0.0f; bool trigger = false; };

class Player
{
public:
    std::array<Scene, kLanes> scenes {};       // one sound per channel (live)
    std::array<ChannelMix, kLanes> mix {};
    float motion = 0.0f;                       // % chance per bar per channel of a pattern move
    Player();
    void reset();
    void render (Engine&, float* L, float* R, int n, Engine::Block eb, const Arrangement&, bool active,
                 float transpose, bool loop, const ManualFx& manual);
    std::atomic<int> playingBlock { -1 };
    std::atomic<double> playingPos { 0.0 };
    std::atomic<int> activeFill { FillNone };
    std::atomic<int> activeFx { FxNone };
    static uint16_t barPattern (uint16_t p, int block, int bar, int ch, float motion);
private:
    int curBlock = -2, curFillState = -1;
    int lastFxMode = FxNone; bool fxWasManual = false;
    std::array<Lane, kLanes> cur {};
    std::array<Scene, kLanes> laneScene {};
};
} // namespace qn
