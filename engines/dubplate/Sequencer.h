// DUBPLATE — the arrangement as triggers, one 16th step at a time (SEMI Family port).
//
// New file: the step logic of the plugin's ArrangementEngine (stepState, emitLaneStep,
// timelinePass, variationWants, fillParticipates, the jump quantise) with the audio taken
// out. Instead of LaneEvents for the sample voices it returns Hits: which lane fires, how
// late inside the step (swing, 1/32 rolls), velocity, pitch, reverse and the break slice.
// The decisions (cells, energy macro gating, variations, fills, drop-outs, snare rolls,
// half-time, riser / impact placement) follow the plugin line by line; what needed a
// sample (riser length, loop bars from analysis, varispeed loops) uses fixed values:
// risers start 4 bars before the section end, loops are 1 bar, every lane is "loaded".
#pragma once
#include "Timeline.h"

namespace dubplate
{
struct Macros
{
    float energy = 0.5f;  // 0.5 = neutral; below mutes upper layers
    float chaos = 0.3f;   // fill probability
    float fill = 0.35f;   // fill density
    float swing = -1.0f;  // 50..75 overrides the arrangement's swing (panel knob), < 50 = keep
};

struct Hit
{
    int lane = 0;
    float delay = 0.0f;   // in 16th steps after the step (swing, roll)
    float vel = 1.0f;     // 0..1
    float semis = 0.0f;
    bool reverse = false;
    int slice = -1;       // loop lanes: slice index (0..15), -1 = one-shot
};

/** Phrase-locked FX event window at a position (as EventFx::process decides it). */
struct EventState
{
    int bit = -1;         // EventBit index, -1 = none
    float progress = 0.0f;
};

class Sequencer
{
public:
    static constexpr int maxLanes = 48;
    static constexpr int maxHits = 128;

    /** Rebuild the lane tables after the arrangement changed (not real-time cheap: call
        from the audio thread only right after swapping in a new arrangement). */
    void setArrangement (const Arrangement* a, bool loopArrangement = true);
    const Arrangement* arrangement() const { return arr; }

    /** All hits of absolute step `absStep` (16ths since the start); returns the count. */
    int step (long long absStep, const Macros& m, Hit* out, int max);

    /** Where an absolute step lands: section index (-1 = outside), arrangement step. */
    int sectionAt (long long absStep, long long* arrStepOut = nullptr) const;
    bool isFillBar (long long absStep, const Macros& m) const;
    EventState eventAt (double absPpq) const;
    /** Whether a lane plays (cells, mutes, energy, variations, drop-outs) at a step. */
    bool laneActive (int li, long long absStep, const Macros& m) const;

    /** Performance jump to a section on the next multiple of `quantizeBars` bars. */
    void requestJump (int section, int quantizeBars) { pendingJump = section; jumpQuant = quantizeBars < 1 ? 1 : quantizeBars; }
    int pendingJumpSection() const { return pendingJump; }
    /** Force a fill on the bar after absolute step `absStep` (FILL trigger input). */
    void requestFill (long long absStep) { forcedFillBar = arr != nullptr ? tl.arrStep (absStep) / 16 + 1 : -1; if (arr != nullptr && forcedFillBar >= tl.totalBars) forcedFillBar = loop ? 0 : -1; }
    long long forcedFill() const { return forcedFillBar; }
    void resetPosition() { barOffsetBars = 0; pendingJump = -1; if (arr) tl.build (*arr, 0, loop); }
    int barOffset() const { return barOffsetBars; }
    const Timeline& timeline() const { return tl; }

private:
    struct StepState
    {
        bool active = false;
        float level = 0.0f;
        int section = -1;
        long long arrStep = -1;
        bool halfTime = false;
        bool fillBar = false;
        bool dropOut = false;
        bool snareRoll = false;
    };

    const Arrangement* arr = nullptr;
    Timeline tl;
    bool loop = true;
    int barOffsetBars = 0;
    int pendingJump = -1, jumpQuant = 4;
    long long forcedFillBar = -1;   // arrangement bar
    float swingOf (const Macros& m) const { return m.swing >= 50.0f ? m.swing : arr->swing; }
    int nLanes = 0;
    std::array<std::array<int, 8>, maxLanes> children {};
    std::array<int, maxLanes> childCount {};
    bool anySolo = false;

    StepState stepState (int li, long long absStep, const Macros& m) const;
    bool variationWants (const Lane& lane, int bar, int si) const;
    void emitLane (int li, long long k, const Macros& m, Hit* out, int& n, int max) const;
};
} // namespace dubplate
