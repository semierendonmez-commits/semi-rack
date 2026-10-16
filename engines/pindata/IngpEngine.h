// Vendored from semierendonmez-commits/pindata Source/engine/IngpEngine.h (JUCE-free). Semi-rack change: wrapped in namespace pnd (one binary, ~35 engines).
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cmath>
#include <vector>
#include <string>

namespace pnd {

/*  The tst_ingp machine, as built in the Max/MSP 4.6 patch (tst_ingp.mxt, 2008-2010).

    FIELD      64 x 32 = 2048 bits ("coll text1"). Text is turned into bits by writing
               every character's ASCII code in binary without padding, MSB first
               ([p txt2bi] / [p 10to2]); the first 2048 bits fill the field row by row.
    VOICES     32 sine oscillators ([p cyc1]). Voice v reads row v of the field with
               its own step counter ([p seq1]: counter 0..len-1 driven by "moveb").
               A 1 opens the voice, a 0 closes it; the gate is ramped by [line~] over
               SMOOTH ms. Gain = gate * amp^2, L = 1 - pan, R = pan,
               frequency = slider^4 * 20000 Hz.
    CLOCK      [metro 60000/bpm/4]: sixteenth notes. Every tick advances all 32
               counters and, if GENERATIONIZE is on, moves the cursor.
    CURSOR     [p movement]: dx = random(-1,0,1); if dx == 0 then dy = +-1.
               x leaving 0..33 jumps to 32, y leaving 0..33 jumps to 16.
               The cell under the cursor is index (y-1)*64 + (x-1).
    RULE       [p cellauto] + [p rulein]: the cell and its four neighbours
               (self, left, right, up, down) form a 5-bit configuration c
               (self is the MSB); the new value is bit c of the 32-bit rule number.
               Clicking the field applies the rule at the clicked cell.
    STOP       [sel 0] -> "sync": every counter goes back to 0.

    Additions for the plugin are marked "plugin:" and all default to the original
    behaviour.                                                                       */
namespace ingp
{
constexpr int W = 64, H = 32, N = W * H, V = 32;
constexpr uint32_t kDefaultRule = 728349234u;

/** Characters -> bitstream exactly like [p txt2bi] (no padding, zero bytes skipped). */
std::vector<uint8_t> textToBits (const std::string& text, size_t maxBits = N);

/** The 32 rule bits, MSB first, as shown in the original's rule multiSlider. */
inline int ruleBit (uint32_t rule, int config) { return (int) ((rule >> config) & 1u); }

struct WalkState { int x = 0, y = 0; };

enum class WalkArea { original = 0, full = 1 };

class Engine
{
public:
    Engine();

    // ---------------------------------------------------------------- field
    std::array<std::atomic<uint8_t>, N> cells;
    std::atomic<uint32_t> rule { kDefaultRule };

    int  cell (int idx) const noexcept { return (idx >= 0 && idx < N) ? (int) cells[(size_t) idx].load (std::memory_order_relaxed) : 0; }
    void setCell (int idx, int v) noexcept { if (idx >= 0 && idx < N) cells[(size_t) idx].store ((uint8_t) (v ? 1 : 0), std::memory_order_relaxed); }
    void loadBits (const std::vector<uint8_t>& bits);   // fills from 0, the rest cleared
    void clearField();

    /** [p cellauto] + [p rulein] at one index. Returns the new value (or -1 off-field). */
    int  applyRuleAt (int idx) noexcept;
    static int configAt (const Engine& e, int idx) noexcept;

    // ---------------------------------------------------------------- voices
    std::array<std::atomic<float>, V> freq, amp, pan;   // 0..1 sliders, as the multiSliders
    std::array<std::atomic<int>, V>   len;              // 1..64
    std::atomic<uint32_t> muteMask { 0 };               // plugin: per-voice mute

    static double sliderToHz (double s) noexcept { return std::pow (std::max (0.0, s), 4.0) * 20000.0; }
    static double hzToSlider (double hz) noexcept { return std::pow (std::max (0.0, hz) / 20000.0, 0.25); }

    // ---------------------------------------------------------------- clock / walk
    struct TickResult
    {
        uint32_t gatesOn = 0;      // voices that read a 1 this tick
        uint32_t rising = 0;       // 0 -> 1
        uint32_t falling = 0;      // 1 -> 0
        int caIndex = -1;          // last cell visited by the cursor
        int caChanged = 0;         // how many cells the rule flipped this tick
    };

    /** One "moveb" bang. genPerTick: plugin: how many cursor moves per tick (orig 1, 0 = off). */
    TickResult tick (int genPerTick, WalkArea area, uint32_t randomSeedMix) noexcept;
    void syncReset() noexcept;    // "sync": all counters to 0
    /** hybrid: move the cursor and apply the rule `moves` times without a clock tick. */
    int walkCursor (int moves, WalkArea area, uint32_t randomSeedMix) noexcept;

    std::array<std::atomic<int>, V> stepShown;   // for the UI: the step each row last read
    std::atomic<uint32_t> gates { 0 };
    std::atomic<int> cursorX { 0 }, cursorY { 0 }, cursorIdx { -65 };
    std::atomic<uint32_t> caEvents { 0 };        // increments on every rule application
    std::atomic<uint32_t> tickCount { 0 };

    // ---------------------------------------------------------------- audio
    void prepare (double sampleRate) noexcept;
    struct RenderParams
    {
        double smoothMs = 0.0;     // line~ ramp time
        double tuneRatio = 1.0;    // plugin: global transpose
        double width = 1.0;        // plugin: pan width around centre (1 = original)
        double tilt = 0.0;         // plugin: spectral tilt -1..1 (0 = original)
        float  gain = 1.0f;
    };
    /** Starts the [line~] ramps after a tick (call at the tick's sample). */
    void startGateRamps (uint32_t gatesOn, double smoothMs, float accent = 1.0f) noexcept;
    /** hybrid: close every gate (pinball mode: a tick lasts the trig's duration). */
    void closeGates (double smoothMs) noexcept { gates.store (0, std::memory_order_relaxed); startGateRamps (0, smoothMs); }
    void render (float* L, float* R, int n, const RenderParams&) noexcept;

    WalkState walk;
    std::array<int, V> step {};          // counters

private:
    int walkMoves (int moves, WalkArea area, int& lastIndex) noexcept;
    uint32_t rnd() noexcept;
    uint32_t rngState = 0x9e3779b9u;

    double sr = 48000.0;
    struct Voice
    {
        double phase = 0.0;
        double gate = 0.0, gateInc = 0.0, gateTarget = 0.0; int gateLeft = 0;
        float  ampS = 0.0f, panS = 0.5f;   // de-zippered slider values
    };
    std::array<Voice, V> voices {};
    std::vector<float> sine;              // 4096-point table + guard
};
} // namespace ingp

} // namespace pnd
