// Vendored from semierendonmez-commits/ikedat Source/engine/IkedarFactory.h (JUCE-free). Semi-rack change: wrapped in namespace ikt (one binary, ~35 engines).
// Generated from the original patch preset slot 1 (ryoz_ikedar_ln.mxt, vpreset).
// Bit i corresponds to slider i (left to right) of the original multiSlider.
#pragma once
#include <cstdint>

namespace ikt {
namespace ryoz::factory {
inline constexpr std::uint64_t kMaster[3] = {0xfcc07fe007fc0f80ull, 0x003f0f3fc0003e47ull, 0x0000000000007f00ull};
inline constexpr std::uint64_t kPitch[9] = {0x000000800847e000ull, 0x000000703e0fffffull, 0x0000e324c3c59020ull, 0x0000000400c07800ull, 0x000000000000f800ull, 0x000200007e00fc3full, 0x000001e080007000ull, 0x0001fc320fffbfffull, 0x0000007f8e3fffffull};
inline constexpr std::uint64_t kCells[9] = {0x5fb7fff3b5fe0c6dull, 0xa5ff5d7ffffffff4ull, 0xffffffcfe1e223fdull, 0x36003468003868caull, 0x09868000010861abull, 0x85c0000000000b5bull, 0x4bebfd4fffffca05ull, 0xabc081c0000a0866ull, 0x304000000002cb2dull};
inline constexpr std::uint64_t kGain[9] = {0x00000003e007fe00ull, 0x00000007000e0000ull, 0x0000000380000000ull, 0x0000000000000000ull, 0x00000067df9fc000ull, 0x000000100063e000ull, 0x0003f0003ffc0000ull, 0x0000000000400000ull, 0x0000000200700000ull};
inline constexpr std::uint64_t kDecay[9] = {0x0000000001801fe3ull, 0x0000000001feffffull, 0x0000000000003ffcull, 0x00000000000001c0ull, 0x0000000000033c30ull, 0x000000000080c00full, 0x0000000000000000ull, 0x000000000013ce3full, 0x00000000003ffecfull};
}

} // namespace ikt
