// Vendored from semierendonmez-commits/ikedat Source/engine/Pinball.h (JUCE-free). Semi-rack change: wrapped in namespace ikt (one binary, ~35 engines).
// IKEDAT — the Pinball sequencer, ported from the Max for Live device
// "Pinball 2" (pinball_2.0.amxd, sequencer designed by nouserid, 2023).
//
// What the device does, decoded from its patcher (see docs/PORTING_NOTES.md):
//
//   * 16 steps, each a [pinball2step] bpatcher: TRIG (none / trig1..8 / mute),
//     TRIG PROB, DEL (delay in 16ths), DEL PROB, VEL, DUR (16ths).
//   * A 17 x 17 [matrixctrl] (range 101) feeds a [prob] object: row = the step
//     the ball is on, column = where it may go next, value = weight 0..100.
//     State 0 is the drain: a ball that goes to 0 (or sits on a row with no
//     weights) stops until the next relaunch.
//   * When the ball lands on a step: with DEL PROB the step waits DEL x 16th x
//     TIME MULT, then (TRIG PROB) fires its trig lane with VEL / DUR, then one
//     16th x TIME MULT later [prob] is banged and the ball moves on.
//   * Trigs on odd 16ths of the transport are late by SWING.
//   * Every PATTERN LENGTH bars (and on RESET) the ball is relaunched at
//     STARTING STEP. HAULT freezes the ball.
//   * 8 trig lanes -> [makenote] with a note number and an on/off (mute) each;
//     "mute" as a step's trig stops every sounding note.
//
// The port is sample accurate; everything above is a constant or a control.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace ikt {

namespace pb
{
constexpr int kSteps  = 16;
constexpr int kStates = 17;      // 0 = drain, 1..16 = steps
constexpr int kLanes  = 8;
constexpr int kTrigNone = 0, kTrigMute = 9;   // 1..8 = lanes

struct StepParams
{
    int trig = kTrigNone;
    int trigProb = 100;          // %
    float delay = 0.0f;          // 16ths
    int delayProb = 100;         // %
    int velocity = 100;          // 1..127
    float duration = 5.0f;       // 16ths (the device's default)
};

using Matrix = std::array<std::array<std::uint8_t, kStates>, kStates>;

struct Controls
{
    double samplesPer16 = 5512.5;   // 1/16 at the current tempo
    double timeMult = 1.0;          // TIME MULTIPLIER 0.25 .. 4
    double swing = 0.0;             // 0..1 -> odd 16ths late by up to half a 16th
    double pos16 = 0.0;             // transport position in 16ths at this sample (swing parity)
    bool halt = false;
    std::array<StepParams, kSteps> steps {};
    const Matrix* matrix = nullptr;
};

struct Fire
{
    int offset = 0;              // sample in the block
    int lane = 0;                // 0..7, -1 = mute
    int velocity = 100;
    double durSamples = 0.0;
    int step = 0;                // 1..16
};

struct FireSink
{
    std::array<Fire, 128> items {};
    int count = 0;
    void add (const Fire& f) noexcept { if (count < (int) items.size()) items[(std::size_t) count++] = f; }
};

struct Transition { std::uint8_t from = 0, to = 0; std::int8_t lane = -2; std::uint32_t serial = 0; };

template <typename T, int N>
struct Ring
{
    std::array<T, N> items {};
    std::atomic<std::uint32_t> w { 0 }, r { 0 };
    void push (const T& t) noexcept
    {
        const auto wi = w.load (std::memory_order_relaxed);
        if (wi - r.load (std::memory_order_acquire) >= (std::uint32_t) N) return;
        items[wi % N] = t;
        w.store (wi + 1, std::memory_order_release);
    }
    bool pop (T& t) noexcept
    {
        const auto ri = r.load (std::memory_order_relaxed);
        if (ri == w.load (std::memory_order_acquire)) return false;
        t = items[ri % N];
        r.store (ri + 1, std::memory_order_release);
        return true;
    }
};

//==============================================================================
class Sequencer
{
public:
    void prepare (double) noexcept { nPending = 0; toNext = -1.0; ballState = 0; }
    void seed (std::uint32_t s) noexcept { rng = s ? s : 0x1234567u; }

    // RESET / relaunch: the ball appears on `startStep` at the next sample.
    void launch (int startStep) noexcept
    {
        launchPending = startStep < 1 ? 1 : (startStep > kSteps ? kSteps : startStep);
    }
    void drain() noexcept { ballState = 0; toNext = -1.0; launchPending = 0; }
    void killPending() noexcept { nPending = 0; }

    int ball() const noexcept { return ballState; }
    std::uint32_t moves() const noexcept { return serial; }

    Ring<Transition, 512> transitions;            // for the picture
    std::atomic<int> ballView { 0 };

    // One sample.
    void sample (int offset, const Controls& c, FireSink& sink) noexcept
    {
        if (launchPending > 0)
        {
            const int s = launchPending;
            launchPending = 0;
            transitions.push ({ (std::uint8_t) ballState, (std::uint8_t) s, -3, ++serial });   // -3 = launch
            land (s, c);
        }
        else if (! c.halt && toNext >= 0.0)
        {
            toNext -= 1.0;
            if (toNext <= 0.0)
            {
                // [prob] banged: weighted pick of the next state from the matrix row
                const int from = ballState;
                const int to = pick (from, c);
                if (to <= 0)
                {
                    transitions.push ({ (std::uint8_t) from, 0, -1, ++serial });
                    ballState = 0;
                    toNext = -1.0;
                }
                else
                {
                    land (to, c, from);
                }
            }
        }

        // trigs waiting for their delay / swing
        for (int k = 0; k < nPending;)
        {
            auto& p = pending[(std::size_t) k];
            p.countdown -= 1.0;
            if (p.countdown <= 0.0)
            {
                sink.add ({ offset, p.lane, p.velocity, p.dur, p.step });
                pending[(std::size_t) k] = pending[(std::size_t) --nPending];
            }
            else ++k;
        }
        ballView.store (ballState, std::memory_order_relaxed);
    }

    // exposed for tests
    int pick (int from, const Controls& c) noexcept
    {
        if (c.matrix == nullptr || from < 0 || from >= kStates) return 0;
        const auto& row = (*c.matrix)[(std::size_t) from];
        int sum = 0;
        for (auto w : row) sum += w;
        if (sum <= 0) return 0;
        int r = (int) (next() % (std::uint32_t) sum);
        for (int k = 0; k < kStates; ++k)
        {
            r -= row[(std::size_t) k];
            if (r < 0) return k;
        }
        return 0;
    }

private:
    struct Pending { double countdown = 0.0; int lane = 0; int velocity = 100; double dur = 0.0; int step = 0; };

    std::uint32_t next() noexcept
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return rng;
    }
    bool chance (int pct) noexcept { return pct >= 100 || (pct > 0 && (int) (next() % 100u) < pct); }

    void land (int s, const Controls& c, int from = -1) noexcept
    {
        ballState = s;
        const auto& st = c.steps[(std::size_t) (s - 1)];
        const double s16 = c.samplesPer16;
        const double mult = c.timeMult;

        // [p probability] on DEL PROB: the delay happens or it is 0
        const double d = chance (st.delayProb) ? (double) st.delay * s16 * mult : 0.0;

        int lane = -2;
        if (st.trig != kTrigNone && chance (st.trigProb))
        {
            lane = st.trig == kTrigMute ? -1 : st.trig - 1;
            // swing: a trig landing on an odd 16th of the transport is late
            const double at16 = c.pos16 + d / s16;
            const long long idx = (long long) std::floor (at16 + 1.0e-6);
            const double sw = (idx & 1) ? c.swing * 0.5 * s16 : 0.0;
            if (nPending < (int) pending.size())
                pending[(std::size_t) nPending++] = { std::max (0.5, d + sw), lane,
                                                      st.velocity, (double) st.duration * s16, s };
        }
        if (from >= 0) transitions.push ({ (std::uint8_t) from, (std::uint8_t) s, (std::int8_t) lane, ++serial });
        toNext = d + s16 * mult;
    }

    std::uint32_t rng = 0x9e3779b9u;
    int ballState = 0, launchPending = 0;
    double toNext = -1.0;
    std::array<Pending, 48> pending {};
    int nPending = 0;
    std::uint32_t serial = 0;
};

//==============================================================================
// Defaults: the matrix is a plain 1 -> 2 -> ... -> 16 -> 1 chain, so a new
// instance behaves like an ordinary 16-step sequencer until you bend it.
inline Matrix chainMatrix()
{
    Matrix m {};
    for (int s = 1; s <= kSteps; ++s) m[(std::size_t) s][(std::size_t) (s % kSteps + 1)] = 100;
    return m;
}

} // namespace pb

} // namespace ikt
