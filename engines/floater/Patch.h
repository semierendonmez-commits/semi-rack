// Patch notes: the current patch written out the way patch notes under a modular
// video are written: what clocks what, what is patched where, what each module is set to.
#pragma once

#include "Types.h"
#include <string>
#include <vector>

namespace flt
{

// paragraphs, in signal order: clock, lanes, modules, cv, effects
std::vector<std::string> patchNotes (const Params& p, double bpm, int rotation);

// the division each lane gets after rotation r
inline int laneDivision (const Params& p, int lane, int r) { return p.lanes[(size_t) ((lane + r) % kLanes)].div; }

} // namespace flt
