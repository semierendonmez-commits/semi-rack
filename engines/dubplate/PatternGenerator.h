// DUBPLATE — pattern / bassline / form / mutation generators. SEMI Family port of
// Source/Model/PatternGenerator.h (unchanged).
#pragma once
#include "Arrangement.h"

namespace dubplate
{
/** Genre-aware generators for one-shot patterns, basslines, song form and mutation.
    Everything is seeded so results are reproducible. */
namespace PatternGenerator
{
    struct StepPattern
    {
        std::array<uint8_t, 16> velocity {};
        std::array<int8_t, 16> pitch {};
    };

    StepPattern defaultPattern (Role role, Genre genre, uint32_t seed, float complexity);

    void generateDrums (Arrangement& arr, float complexity, uint32_t seed);
    void generateBassline (Arrangement& arr, float complexity, uint32_t seed);
    void generateForm (Arrangement& arr, uint32_t seed, bool vip);
    void mutate (Arrangement& arr, float amount, uint32_t seed);

    std::array<bool, 16> defaultGhostKick (Genre genre);
    std::array<std::array<bool, 16>, 4> defaultFillGrid (int profile);
}
} // namespace dubplate
