// semi-rack: vendored; namespaces wrapped in dnafam:: (see sources.mk for every change).
// Walker: the "Pinball 2" Markov sequencer (nouserid, 2023), locked to the host grid.
//
// The Max for Live device, as PINDATA decoded it (pindata/Source/engine/Pinball.h):
//
//   * N steps and a (N+1) x (N+1) weight matrix (matrixctrl, 0..100). Row x is
//     "from", column y is "to". State 0 is "before step 1", state s+1 is step s.
//   * A step that fires sets the state to itself and draws the next one from its
//     row ([prob]: weighted draw). A row without weights stops the ball; drawing
//     state 0 rests it. Both wait for the next reset.
//   * Every Pattern Length bars: reset, state = Starting Step - 1, draw.
//   * Hault: the ball stops until the next reset.
//   * Default matrix: x -> x + 1 at 100, a plain 1..N run.
//
// One change, on purpose: in the device a step's Delay postpones everything after it
// (the delays add up until the next reset). Here the ball moves on a fixed grid of
// 1/16 x Time Multiplier, and a step's delay only moves that step's own notes, so the
// walk never drifts from the bar. Swing is applied by the caller, as in the device.
//
// Plain C++, no JUCE, no allocation.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

namespace dnafam::walk
{
constexpr int kMaxSteps = 64;
constexpr int kMaxStates = kMaxSteps + 1;

struct Rand
{
    uint32_t s = 0x2545F491u;
    void seed (uint32_t v) noexcept { s = v ? v : 0x2545F491u; }
    uint32_t next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    double unit() noexcept { return (next() >> 8) * (1.0 / 16777216.0); }
    int below (int n) noexcept { return n <= 1 ? 0 : (int) (next() % (uint32_t) n); }
};

/** A weight matrix the editor can draw while the audio thread reads. */
struct Matrix
{
    std::array<std::array<std::atomic<float>, kMaxStates>, kMaxStates> w;

    Matrix() { clear(); }
    float get (int from, int to) const noexcept { return w[(size_t) from][(size_t) to].load (std::memory_order_relaxed); }
    void set (int from, int to, float v) noexcept { w[(size_t) from][(size_t) to].store (v, std::memory_order_relaxed); }
    void clear() noexcept { for (auto& r : w) for (auto& x : r) x.store (0.0f, std::memory_order_relaxed); }
    /** x -> x + 1 for the first n steps (the device's default), the last step back to the first */
    void linear (int n) noexcept
    {
        clear();
        for (int x = 0; x <= n; ++x) set (x, x < n ? x + 1 : 1, 100.0f);
    }
    template <typename Plain> void copyFrom (const Plain& p) noexcept
    {
        for (int x = 0; x < kMaxStates; ++x)
            for (int y = 0; y < kMaxStates; ++y) set (x, y, p[(size_t) x][(size_t) y]);
    }
};

using Plain = std::array<std::array<float, kMaxStates>, kMaxStates>;

class Walker
{
public:
    void seed (uint32_t s) noexcept { rnd.seed (s); }

    /** Pattern boundary / Manual Reset: state = start - 1 (start is 1-based). */
    void reset (int startStep, int numSteps) noexcept
    {
        halted = false;
        idle = false;
        state = std::clamp (startStep - 1, 0, std::max (0, numSteps - 1));
    }
    void halt() noexcept { halted = true; }
    void jump (int step) noexcept { halted = idle = false; pendingJump = step; }

    /** One tick of the grid: returns the step the ball lands on (0..n-1), or -1. */
    int tick (const Matrix& m, int numSteps) noexcept
    {
        if (pendingJump >= 0)
        {
            const int s = std::min (pendingJump, numSteps - 1);
            pendingJump = -1;
            state = s + 1;
            return s;
        }
        if (halted || idle || numSteps <= 0) return -1;
        const int n = std::min (numSteps, kMaxSteps);
        const int from = std::clamp (state, 0, n);
        double total = 0.0;
        for (int y = 0; y <= n; ++y) total += std::max (0.0f, m.get (from, y));
        if (total <= 0.0) { idle = true; return -1; }
        double pick = rnd.unit() * total;
        int next = n;
        for (int y = 0; y <= n; ++y)
        {
            pick -= std::max (0.0f, m.get (from, y));
            if (pick < 0.0) { next = y; break; }
        }
        if (next == 0) { idle = true; state = 0; return -1; }
        state = next;
        lastFrom = from;
        return next - 1;
    }

    bool isHalted() const noexcept { return halted; }
    bool isIdle() const noexcept { return idle; }
    int stateNow() const noexcept { return state; }
    int lastFrom = 0;
    Rand rnd;

private:
    int state = 0;
    int pendingJump = -1;
    bool halted = false, idle = false;
};

} // namespace dnafam::walk
