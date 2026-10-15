// Vendored unchanged from semierendonmez-commits/epoch Source/engine (Rack / MetaModule port).
// MEASURE: a reference recording becomes a score. Streaming (a 32-minute file never sits in
// memory), plain C++. Per 11.6 ms hop it keeps the level and the level of the first difference
// (onsets live in the highs); at the end it finds onsets, silent stretches and the energy per
// bar, and draws the ENERGY and SPACE curves through them with as few points as the shape needs.
//
// The numbers it reports are the ones FLOATER v0.6 compared its renders against:
// onsets per second, the interval CV, the share of time in gaps longer than 1.5 s.
#pragma once

#include "Score.h"
#include <string>
#include <vector>

namespace epoch
{

struct Measure
{
    double seconds = 0.0;
    double onsetsPerSecond = 0.0;
    double intervalCv = 0.0;
    double gapShare = 0.0;          // time in silent stretches longer than 1.5 s
    double spaceShare = 0.0;        // time in silent stretches longer than a quarter second (what SPACE draws)
    double meanEnergy = 0.0;
    int bars = 0;
    std::vector<float> energyPerBar, spacePerBar;
};

class Analyzer
{
public:
    explicit Analyzer (double sampleRate);
    void push (const float* mono, int n);      // any block size
    // bpm: the tempo the bars are counted in; maxBars: the timeline's limit (longer files are squeezed)
    Measure finish (double bpm, int maxBars = kMaxBars);
    double seconds() const { return (double) total / sr; }

private:
    double sr;
    int hop;
    int fill = 0;
    double accX = 0.0, accD = 0.0;
    float last = 0.0f;
    long long total = 0;
    std::vector<float> levelDb, diffDb;         // per hop
};

// the measure drawn as a score (curves simplified to the points the shape needs)
Score scoreFromMeasure (const Measure&, const std::string& title, double bpm);

// Ramer-Douglas-Peucker on a sampled curve; returns the kept indices
std::vector<int> simplify (const std::vector<float>& v, float epsilon, int maxPoints);

} // namespace epoch
