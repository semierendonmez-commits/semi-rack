// Shared types of the Generative Tracker Break Slicer engine (no JUCE here, so the
// engine builds and tests on its own).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gtbs
{

/** exact equality for settings that are copied, never computed (no -Wfloat-equal noise) */
template <typename F> inline bool same (F a, F b) noexcept { return ! (a < b) && ! (b < a); }

constexpr int kMaxSlices   = 128;
constexpr int kMaxRows     = 64;
constexpr int kTicksPerBeat = 24;     // T_tick = 2.5 / B seconds  ==  1/24 beat
constexpr int kSubTicks    = 16;      // a tick divided into 16 for the drill (eq. 2.3)
constexpr int kSubBlock    = 32;      // parameter update granularity (section 3.3)

// Transient classes of the Markov chain (section 4.2)
enum Cls : uint8_t { Kick = 0, Snare, Ghost, Hat, NumCls };
inline const char* clsName (int c)   { static const char* n[] = { "KICK", "SNARE", "GHOST", "HAT" }; return n[c & 3]; }
inline char clsLetter (int c)        { static const char n[] = { 'K', 'S', 'G', 'H' }; return n[c & 3]; }

// DSP chains of the routing matrix (section 4.1, same order as DSPTargetChain)
enum Chain : uint8_t { PassThrough = 0, PitchShift, SubOctave, ReverseBuffer, ReverbDrill, NumChains };
inline const char* chainShort (int c) { static const char* n[] = { "---", "PIT", "SUB", "REV", "DRL" }; return n[c % NumChains]; }
inline const char* chainName (int c)  { static const char* n[] = { "PASS", "PITCH", "SUB OCT", "REVERSE", "REVERB DRILL" }; return n[c % NumChains]; }

// Onset detection functions (chapter 1)
enum Odf : uint8_t { OdfSF = 0, OdfWPD, OdfRCD, OdfQMF, OdfHybrid, NumOdf };

// Sampler hardware (section 2.3)
enum Machine : uint8_t { Clean16 = 0, AkaiS950, AkaiS1000, AmigaPaula, NumMachines };

// Retrigger volume trajectories (eq. 2.5)
enum RetrigVol : uint8_t { RvNone = 0, RvDecay, RvBoost, RvExp };

//==============================================================================
// One tracker row.
struct Cell
{
    uint8_t trig = 0;      // 0 empty (the slice keeps playing), 1 trigger, 2 note cut
    uint8_t off09 = 0;     // 09xx sample offset byte
    int16_t slice = -1;    // slice the offset snaps to (-1: raw offset)
    uint8_t cls = Kick;    // transient class of that slice
    uint8_t chain = PassThrough;
    uint8_t rmode = RvNone;// Rxy: volume trajectory
    uint8_t ramt = 0;      //      x: 1..8
    uint8_t ry = 0;        //      y: interval, 0 = no retrigger
    uint8_t drill = 0;     //      1: y counts sub-ticks (eq. 2.3), 0: ticks
    int8_t  sweep = 0;     // pitch travel over the row's repeats, semitones (eq. 2.4)
    int8_t  pitch = 0;     // semitones for the PitchShift chain
    uint8_t vol = 64;      // Cxx, 0..64
    uint8_t natural = 0;   // 1: the break's own hit at this position (not recut)
    uint8_t fill = 0;      // generated as part of a fill
};

struct PatternView
{
    std::array<Cell, kMaxRows> cells {};
    std::array<uint8_t, kMaxRows> pinned {};
    int rows = 16;
    int serial = 0;
};

//==============================================================================
// A break and everything the analysis found in it. Immutable once published.
struct Analysis
{
    int hop = 441;                       // frames per ODF frame (10 ms)
    std::vector<float> odf;              // selected detection function, 0..1
    std::vector<float> threshold;        // T(n), same scale
    std::vector<int> peakFrames;         // validated onsets (ODF peaks), sample frames
    std::vector<int> starts;             // slice starts after backtracking, sample frames
    std::vector<uint8_t> cls;            // class per slice
    std::vector<float> level;            // peak level per slice, 0..1
    std::vector<std::array<float, 4>> bands; // QMF band energy share per slice
    double tempo = 0.0;                  // estimated from the ODF autocorrelation
};

struct Sample
{
    std::string name;
    double rate = 44100.0;
    int frames = 0;
    std::vector<float> l, r;
    Analysis an;
    int serial = 0;
};

// The break as the chosen sampler holds it (decimated, quantised). Immutable once published.
struct MachineBuffer
{
    int machine = Clean16;
    double rate = 44100.0;               // the sampler's clock
    int frames = 0;
    std::vector<float> l, r;
    double ratio = 1.0;                  // machine frames per source frame
    int sampleSerial = 0;
    int serial = 0;
};

} // namespace gtbs
