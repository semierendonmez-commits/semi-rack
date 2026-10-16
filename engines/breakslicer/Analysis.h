// Chapter 1: onset detection. STFT (eq. 1.1), spectral flux (1.2), weighted phase
// deviation (1.3-1.4), rectified complex domain (1.5-1.8), log compression (1.9),
// adaptive median threshold (1.10), plus the sub-band QMF detection function.
// Then: backtracking to the true attack, transient classes for the Markov chain,
// slice maps (onset or grid) and the break's length in bars.
#pragma once

#include "Types.h"
#include <memory>

namespace gtbs
{

struct AnalysisParams
{
    int odf = OdfHybrid;
    float lambda = 10.0f;        // compression, eq. 1.9, [1, 20]
    float alpha = 0.25f;         // moving-average weight, eq. 1.10, [0.1, 0.4]
    float sensitivity = 0.6f;    // 0..1, sets delta (the absolute floor)
    float minGapMs = 45.0f;      // closest two onsets may be
    bool backtrack = true;       // move each onset back to where the attack starts
    bool operator== (const AnalysisParams& o) const
    {
        return odf == o.odf && same (lambda, o.lambda) && same (alpha, o.alpha) && same (sensitivity, o.sensitivity)
            && same (minGapMs, o.minGapMs) && backtrack == o.backtrack;
    }
};

// delta of eq. 1.10 from the sensitivity control
inline float deltaFor (float sensitivity) { return 0.02f + 0.30f * (1.0f - sensitivity) * (1.0f - sensitivity); }

/** Runs the whole of chapter 1 on a break (offline, any thread but the audio one). */
Analysis analyse (const Sample& s, const AnalysisParams& p);

/** All five detection functions, normalised 0..1 (for tests and the display). */
struct OdfSet { int hop = 441; std::vector<float> f[NumOdf]; };
OdfSet detectionFunctions (const Sample& s, float lambda);

/** Validated peaks of a detection function (eq. 1.10 + local maximum + minimum gap). */
std::vector<int> pickPeaks (const std::vector<float>& odf, std::vector<float>& thresholdOut,
                            float alpha, float delta, int minGapFrames);

//==============================================================================
struct SliceMap
{
    std::vector<int> starts;        // source frames, ascending, starts[0] == 0
    std::vector<uint8_t> cls;
    std::vector<float> level;
    int count() const { return (int) starts.size(); }
    int lengthOf (int i, int totalFrames) const
    {
        return (i + 1 < count() ? starts[(size_t) i + 1] : totalFrames) - starts[(size_t) i];
    }
};

enum SliceMode { SliceOnsets = 0, SliceGrid8, SliceGrid16, SliceGrid32, NumSliceModes };

SliceMap buildSlices (const Sample& s, int mode);

/** Break length in bars: the given setting, or the analysis' best guess (0 = auto). */
int resolveBars (const Sample& s, int setting, double hostBpm);

/** Classifies arbitrary slice starts from the analysis' per-slice features. */
void classify (const Sample& s, SliceMap& m);

} // namespace gtbs
