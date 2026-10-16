// Everything the audio thread plays from, built off the audio thread and
// published as one immutable object: the break, its slices, and the break as
// the chosen sampler holds it (section 2.3).
#pragma once

#include "Analysis.h"
#include <algorithm>
#include <cmath>
#include <memory>

namespace gtbs
{

struct MachineParams
{
    int machine = Clean16;
    float rateKHz = 27.5f;     // S950 7.5..48, Paula 3.5..28.8 (clamped per machine)
    float driveDb = 0.0f;      // into the converter
    bool operator== (const MachineParams& o) const { return machine == o.machine && same (rateKHz, o.rateKHz) && same (driveDb, o.driveDb); }
};

inline double machineRateFor (const MachineParams& p, double sourceRate)
{
    switch (p.machine)
    {
        case AkaiS950:   return std::clamp ((double) p.rateKHz, 7.5, 48.0) * 1000.0;
        case AkaiS1000:  return sourceRate >= 47000.0 ? 48000.0 : 44100.0;
        case AmigaPaula: return std::clamp ((double) p.rateKHz, 3.5, 28.8) * 1000.0;
        default:         return sourceRate;
    }
}

MachineBuffer buildMachineBuffer (const Sample& s, const MachineParams& p);

struct Kit
{
    std::shared_ptr<const Sample> sample;
    SliceMap slices;               // source frames
    std::vector<int> mbStarts;     // the same starts in machine frames
    MachineBuffer mb;
    int bars = 2;                  // the break's length
    int sliceMode = 0;
    int serial = 0;

    int numSlices() const { return slices.count(); }
    double breakBeats() const { return bars * 4.0; }
    /** source frame of an 09xx offset (eq. 2.1) */
    int offsetFrame (int p09) const { return (int) std::floor ((double) p09 * (double) sample->frames / 256.0); }
    /** 09xx byte that lands on (or just before) a source frame */
    int offsetByte (int frame) const
    {
        return std::clamp ((int) std::floor ((double) frame * 256.0 / std::max (1, sample->frames)), 0, 255);
    }
    /** slice whose start is nearest a source frame */
    int nearestSlice (int frame) const;
    /** slice playing at a source frame */
    int sliceAt (int frame) const;
};

std::unique_ptr<Kit> buildKit (std::shared_ptr<const Sample> s, int sliceMode, int barsSetting, const MachineParams& mp, double hostBpm);

} // namespace gtbs
