// Vendored from semierendonmez-commits/pindata Source/engine/Pinball.h (JUCE-free). Semi-rack change: wrapped in namespace pnd (one binary, ~35 engines).
// Pinball — C++ port of the "Pinball 2" Max for Live device (pinball_2.1.amxd,
// pinball2step.maxpat), the Markov-chain trigger sequencer at the centre of PINDATA.
//
// The device, decoded:
//
//   16 steps   [bpatcher pinball2step #1..#16]. Every step listens to one broadcast
//              index. When the index equals its own number:
//                [t b b] -> delay probability passes ? Delay x 1/16 x mult : 0  -> [del]
//                then [t b b b]:
//                  duration message  = Duration x 1/16 (not scaled by mult)
//                  velocity message  only if the trig probability passes
//                  [del 1/16 x mult] -> its own index to [prob] (the step is "done")
//              Any other index (or "stop") cancels its pending [del]s.
//   prob       [prob] fed by a 17 x 17 [matrixctrl] (range 0..100): cell (x, y, w)
//              is the weight from state x to state y. State 0 is "before step 1",
//              1..16 are the steps. A done step sets the state to itself and draws
//              the next one; a row without weights stops the ball.
//              Default matrix: x -> x+1 at 100 for x = 0..15 (a plain 1..16 run).
//   reset      [what~] on the transport: every Pattern Length bars (0.5..64) ->
//              "Manual Reset": stop everything, flush the notes, state =
//              Starting Step - 1, draw. With the default matrix that is the start step.
//   halt       "Hault": -1 to every step: the ball stops until the next reset.
//   trigs      each step's menu picks none / trig1..trig8 / mute. A trig plays its
//              lane's note through [makenote @repeatmode 1] (velocity, duration).
//              A lane that fires sends "mute" to every OTHER lane; "mute" steps send
//              it to all lanes. A lane whose Mute toggle is on stops its note on "mute"
//              (a choke group).
//   swing      notes that land while the 1/16 x mult grid counter is even are pushed
//              late by swing x 0.5 x 1/16 x mult.
//   functions  Randomize Trigs (every step: random(0,10)), Clear Steps, Clear Matrix,
//              Clear All.
//
// Sample-accurate, no allocation, no JUCE. The caller provides the 1/16 length in
// samples, the grid position for swing, and the reset points.
#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace pnd {

namespace pinball
{
constexpr int kSteps = 16, kStates = 17, kLanes = 8;
constexpr int kTrigNone = 0, kTrigMute = 9;   // 1..8 = lanes

struct StepParams
{
    int trig = kTrigNone;     // 0 none, 1..8 lane, 9 mute
    int velocity = 100;       // 0..127
    int trigProb = 100;       // %
    double duration = 5.0;    // 1/16 notes (note length)
    double delay = 0.0;       // 1/16 notes
    int delayProb = 100;      // %
};

struct Event
{
    enum Type : uint8_t { Trig, Choke, Enter, Fire, Reset, Halt, Idle };
    int offset = 0;           // sample in the block
    Type type = Trig;
    int8_t lane = -1;         // Trig: lane 0..7
    int8_t step = -1;         // 0..15 (Enter / Fire / Trig)
    uint8_t velocity = 0;
    uint8_t chokeMask = 0;    // Choke: lanes that receive "mute"
    float durMs = 0.0f;
};

template <int N>
struct EventList
{
    std::array<Event, N> items {};
    int count = 0;
    void clear() noexcept { count = 0; }
    void add (const Event& e) noexcept { if (count < N) items[(size_t) count++] = e; }
};

// xorshift; the device uses Max's random, any uniform source will do
struct Rand
{
    uint32_t s = 0x2545F491u;
    void seed (uint32_t v) noexcept { s = v ? v : 0x2545F491u; }
    uint32_t next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    double unit() noexcept { return (next() >> 8) * (1.0 / 16777216.0); }
    int below (int n) noexcept { return n <= 1 ? 0 : (int) (next() % (uint32_t) n); }
};

class Sequencer
{
public:
    // ---- the device's state ----------------------------------------------------
    // matrix[from][to], 0..100. Atomics so the editor can draw while the audio
    // thread reads; the [prob] object reads the weights at draw time as well.
    std::array<std::array<std::atomic<float>, kStates>, kStates> matrix;
    std::array<StepParams, kSteps> steps {};   // written by the caller before process()

    Sequencer() { setDefaultMatrix(); }

    void setDefaultMatrix() noexcept
    {
        for (int x = 0; x < kStates; ++x)
            for (int y = 0; y < kStates; ++y)
                matrix[(size_t) x][(size_t) y].store ((y == x + 1 && x < kSteps) ? 100.0f : 0.0f, std::memory_order_relaxed);
    }
    void clearMatrix() noexcept
    {
        for (auto& row : matrix) for (auto& w : row) w.store (0.0f, std::memory_order_relaxed);
    }

    void seed (uint32_t s) noexcept { rnd.seed (s); }
    void prepare (double sampleRate) noexcept { sr = sampleRate > 0 ? sampleRate : 44100.0; stopAll(); state = 0; }

    // ---- control (audio thread, between or inside blocks) --------------------------
    // Manual Reset / pattern boundary: cancel, flush, state = start - 1, draw.
    void reset (int offset, int startStep, EventList<512>& out) noexcept
    {
        stopAll();
        halted = false;
        Event r; r.offset = offset; r.type = Event::Reset; out.add (r);
        state = std::clamp (startStep - 1, 0, kSteps - 1);
        drawNext (offset, out);
    }

    // "Hault": the ball stops until the next reset
    void halt (int offset, EventList<512>& out) noexcept
    {
        stopAll();
        halted = true;
        Event h; h.offset = offset; h.type = Event::Halt; out.add (h);
    }

    // a step's button: jump straight to it
    void jump (int step, int offset, EventList<512>& out) noexcept
    {
        if (step < 0 || step >= kSteps) return;
        halted = false;
        enter (step, offset, out);
    }

    // The 1/16 length in samples and the Time Multiplier: set before reset() / jump()
    // so a step entered there is timed right (process() sets them too).
    void setTiming (double sixteenthSamples, double mult) noexcept
    {
        sixteenth = std::max (1.0, sixteenthSamples);
        timeMult = std::clamp (mult, 0.01, 64.0);
    }

    // Advance by n samples. sixteenthSamples = one 1/16 at the current tempo,
    // mult = Time Multiplier. Events land in `out` with block offsets.
    void process (int n, double sixteenthSamples, double mult, EventList<512>& out) noexcept
    {
        int t = 0;
        setTiming (sixteenthSamples, mult);
        while (t < n)
        {
            if (phase == Phase::Idle) break;
            const double left = countdown;
            if (left > (double) (n - t))
            {
                countdown -= (double) (n - t);
                break;
            }
            const int at = t + std::max (0, (int) std::ceil (left - 1.0e-9));
            if (at >= n) { countdown -= (double) (n - t); break; }
            t = at;
            countdown = 0.0;
            if (phase == Phase::Delay) fire (t, out);
            else if (phase == Phase::Advance) { phase = Phase::Idle; state = current + 1; drawNext (t, out); }
        }
    }

    // ---- telemetry --------------------------------------------------------------
    std::atomic<int> currentStep { -1 };        // 0..15, -1 = idle
    std::atomic<uint32_t> fireCount { 0 };
    bool isHalted() const noexcept { return halted; }
    int stateNow() const noexcept { return state; }

private:
    enum class Phase { Idle, Delay, Advance };

    void stopAll() noexcept { phase = Phase::Idle; countdown = 0.0; current = -1; currentStep.store (-1, std::memory_order_relaxed); }

    // [prob]: weighted draw from the row of the current state
    void drawNext (int offset, EventList<512>& out) noexcept
    {
        if (halted) return;
        double total = 0.0;
        const auto& row = matrix[(size_t) std::clamp (state, 0, kStates - 1)];
        for (int y = 0; y < kStates; ++y) total += std::max (0.0f, row[(size_t) y].load (std::memory_order_relaxed));
        if (total <= 0.0)
        {
            stopAll();
            Event e; e.offset = offset; e.type = Event::Idle; out.add (e);
            return;
        }
        double pick = rnd.unit() * total;
        int next = kStates - 1;
        for (int y = 0; y < kStates; ++y)
        {
            pick -= std::max (0.0f, row[(size_t) y].load (std::memory_order_relaxed));
            if (pick < 0.0) { next = y; break; }
        }
        state = next;
        if (next == 0)   // state 0 is no step: the ball rests until a reset
        {
            stopAll();
            Event e; e.offset = offset; e.type = Event::Idle; out.add (e);
            return;
        }
        enter (next - 1, offset, out);
    }

    void enter (int step, int offset, EventList<512>& out) noexcept
    {
        current = step;
        state = step + 1;
        currentStep.store (step, std::memory_order_relaxed);
        const auto& sp = steps[(size_t) step];
        Event e; e.offset = offset; e.type = Event::Enter; e.step = (int8_t) step; out.add (e);
        // [p probability] on the delay: pass -> Delay x 1/16 x mult, fail -> 0
        double d = 0.0;
        if (sp.delay > 0.0 && pass (sp.delayProb)) d = sp.delay * sixteenth * timeMult;
        phase = Phase::Delay;
        countdown = d;
        if (d <= 0.0) fire (offset, out);
    }

    void fire (int offset, EventList<512>& out) noexcept
    {
        const auto& sp = steps[(size_t) std::clamp (current, 0, kSteps - 1)];
        Event f; f.offset = offset; f.type = Event::Fire; f.step = (int8_t) current; out.add (f);
        fireCount.fetch_add (1, std::memory_order_relaxed);
        const float durMs = (float) (sp.duration * sixteenth / sr * 1000.0);
        if (sp.trig != kTrigNone && pass (sp.trigProb))
        {
            if (sp.trig == kTrigMute)
            {
                Event c; c.offset = offset; c.type = Event::Choke; c.chokeMask = 0xff; c.step = (int8_t) current; out.add (c);
            }
            else
            {
                const int lane = std::clamp (sp.trig - 1, 0, kLanes - 1);
                // [p 213]: a firing lane sends "mute" to all the others
                Event c; c.offset = offset; c.type = Event::Choke; c.chokeMask = (uint8_t) (0xff & ~(1u << lane)); c.step = (int8_t) current; out.add (c);
                Event e; e.offset = offset; e.type = Event::Trig; e.lane = (int8_t) lane; e.step = (int8_t) current;
                e.velocity = (uint8_t) std::clamp (sp.velocity, 0, 127); e.durMs = durMs; out.add (e);
            }
        }
        // [del 1/16 x mult] -> index -> [prob]
        phase = Phase::Advance;
        countdown = sixteenth * timeMult;
    }

    bool pass (int percent) noexcept
    {
        // [random 2147483521] / 2147483520. < p * 0.01
        return rnd.unit() < (double) percent * 0.01;
    }

    Rand rnd;
    double sr = 44100.0, sixteenth = 5512.5, timeMult = 1.0;
    Phase phase = Phase::Idle;
    double countdown = 0.0;
    int current = -1, state = 0;
    bool halted = false;
};

// Functions menu
inline void randomizeTrigs (std::array<StepParams, kSteps>& s, Rand& r) { for (auto& p : s) p.trig = r.below (10); }

} // namespace pinball

} // namespace pnd
