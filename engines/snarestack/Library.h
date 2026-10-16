// SEMI Family port: vendored from semierendonmez-commits/snarestack Source/engine (0fa7677). Only change:
// namespace gtbs -> ss::gtbs (the break-slicer engines in the binary use gtbs too).
// The built-in layers: synthesised parts (made here, no samples) and the snares
// of a few breaks, cut out by the break analysis of the sibling plug-ins.
#pragma once

#include "Stack.h"
#include "Analysis.h"

namespace ss
{

/** names of the synthesised layers */
const std::vector<std::string>& synthNames();
/** a synthesised layer by index (48 kHz) */
std::shared_ptr<const Source> synth (int index);

/** the loudest clear snare of a break (its slice, trimmed and faded) */
std::shared_ptr<const Source> snareFromBreak (const gtbs::Sample& analysedBreak, const std::string& name);

} // namespace ss
