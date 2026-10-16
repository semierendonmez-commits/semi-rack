// BREAKMED - offline DSP: slicing, Paula resampling, the drill reverb.
// Plain C++17, no JUCE, so the engine tests build on their own.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bm
{

constexpr int kMaxSlices = 64;

enum Role : int { RoleKick = 0, RoleSnare, RoleHat, RoleGhost, kNumRoles };
const char* roleLetter (int role);        // "K" "S" "H" "G"

// ---- slicing -----------------------------------------------------------------------
enum class SliceMode : int { Trans = 0, Equal, Grid, Manual };
const char* sliceModeName (int);

struct Onsets
{
    std::vector<int> pos;        // sample positions (attack start)
    std::vector<float> strength; // onset strength, normalised 0..1
};

// Log-energy rise in two bands over 5 ms hops, peaks above median + k * MAD.
Onsets detectOnsets (const float* x, int n, double sr);

// TRANS: onsets kept by THRES (2 = every onset, 64 = only the strongest).
std::vector<int> transientSlices (const Onsets&, int n, double sr, int thres);
// EQUAL: THRES equal parts.
std::vector<int> equalSlices (int n, int count);
// GRID: one slice per 16th of the loop, each start pulled to an onset within 40 % of a step.
std::vector<int> gridSlices (const Onsets&, int n, int bars);

int snapToZero (const float* x, int n, int pos, int maxBack);
std::vector<int> classify (const float* x, int n, double sr, const std::vector<int>& starts);

// Bars of a loop: the count that puts the tempo nearest 140 within 70..190 BPM.
int autoBars (double seconds);
inline double loopBpm (double seconds, int bars) { return seconds > 0 ? bars * 4.0 * 60.0 / seconds : 120.0; }

// ---- Paula ---------------------------------------------------------------------------
// The 36 ProTracker note rates (C-1 .. B-3, PAL clock 3546895 / period), then FULL.
constexpr int kNumRates = 37;
constexpr int kRateFull = 36;
double paulaRate (int index, double fullRate);
std::string paulaRateName (int index);       // "C-1" ...
int defaultRateIndex();                      // F-3 = 22168 Hz

// Band-limited resample (windowed sinc) and n-bit quantisation (Amiga: 8 bit signed).
std::vector<float> resample (const float* x, int n, double srIn, double srOut);
void quantise (std::vector<float>& x, int bits);

// Small mono reverb (Schroeder / Freeverb topology) for the drill tails.
class MonoVerb
{
public:
    void setup (double sr, float size, float damp);
    float process (float in);
private:
    struct Comb { std::vector<float> buf; int idx = 0; float store = 0; };
    struct AllPass { std::vector<float> buf; int idx = 0; };
    Comb comb[8];
    AllPass ap[4];
    float feedback = 0.8f, damp1 = 0.3f, damp2 = 0.7f;
};

// ---- the playable form of a break ------------------------------------------------
// Built off the audio thread whenever the sample, slices, rate, bits or space change.
struct Prepared
{
    double rate = 22168.0;           // pcm rate
    double srcRate = 44100.0;
    int srcLen = 0;
    int bars = 2;
    std::vector<float> pcm;          // the whole break at `rate`, quantised
    std::vector<int> start;          // slice starts in pcm samples, plus the end
    std::vector<int> role;
    std::vector<float> beat;         // slice start in beats from the loop start
    std::vector<std::vector<float>> wet; // DRILL: slice + mono reverb tail, at `rate`, quantised
    int numSlices() const { return (int) role.size(); }
};

struct PrepareSpec
{
    const float* mono = nullptr;
    int n = 0;
    double srcRate = 44100.0;
    std::vector<int> starts;         // in source samples, ascending, first may be > 0
    std::vector<int> roles;
    int bars = 2;
    int rateIndex = 0;
    int bits = 8;
    float space = 0.6f;
};

std::shared_ptr<Prepared> prepare (const PrepareSpec&);

} // namespace bm
