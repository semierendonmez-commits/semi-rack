// SEMI port: compiles schwung-breakbeat's slice selection (upstream/slice_select.c, unchanged,
// MIT, Matt Estela / Charles Vestal) and the plugin's 8/16/32-slice generalisation
// (upstream/slice_select_n.c, unchanged) as C++ inside namespace bbeat, so no C symbol of
// theirs is global in the shared SemiFamily binary.
#include <math.h>
#include <stdint.h>

namespace bbeat {
#include "upstream/slice_select.c"
#include "upstream/slice_select_n.c"
} // namespace bbeat
