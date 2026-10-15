// The macro roles of EPOCH, copied verbatim from link/EpochLink.h (semierendonmez-commits/epoch).
// Rack / MetaModule port: only the Role enum and roleName() are vendored. The rest of EpochLink.h
// is the shared-memory DAW bus (mmap / CreateFileMapping), which Rack does not use: in Rack the
// plan travels on the FamilyBus CV cable (src/common/FamilyBusCV.hpp) and the CV outs.
#pragma once

#include <cstdint>

namespace epochlink
{

// What a macro means to the siblings. A slot carries one role; a reader asks for a role.
enum Role : uint8_t
{
    None = 0,
    Energy,      // overall intensity of the set
    Density,     // how many events (energy, minus the holes)
    Chaos,       // FLOATER REEL CHAOS, FLOATDAT, RETICULUM SCALE ...
    Mutation,    // GENOTYPE MUTATION, IKEDAR mutate chance ...
    Duck,        // NEUROSTACK DUCK, FLOATER DUCK
    Space,       // 1 = be silent now (the hole gate)
    Variation,   // NEUROSTACK VARIATION / BUDGET, TURNAROUND edit budget
    Tension,     // rises into phrase and section ends
    Arc,         // rise and fall inside the section (FLOATER ARC)
    Drive,
    Bright,
    Wet,
    kNumRoles
};

inline const char* roleName (int r)
{
    static const char* n[] = { "--", "ENERGY", "DENSITY", "CHAOS", "MUTATION", "DUCK", "SPACE",
                               "VARIATION", "TENSION", "ARC", "DRIVE", "BRIGHT", "WET" };
    return (r >= 0 && r < kNumRoles) ? n[r] : "--";
}

} // namespace epochlink
