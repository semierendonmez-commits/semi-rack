// TB-3PO pattern generator — the Phazerville / Hemisphere applet's algorithm
// (Logarhythm1, djphazer; third_party/phazerville-tb3po/TB3PO.h), rebuilt for
// a DAW:
//
//  * The pattern is a pure function of (seed, density, scale size, and the
//    accent / slide / octave amounts). Density runs -7..+7 exactly like the
//    module: |density| sets how many steps gate, the negative side narrows
//    the pitch set down to the root alone at -7. Returning to a value
//    returns the same line.
//  * Every step draws from its own hash of (seed, step, salt), so a gate that
//    appears at density 3 is still there at density 5 — breakdowns are
//    reversible. Mutate re-salts a handful of steps; New picks a new seed.
//  * Hand-edited steps are kept as overrides on top of the generated line.
// SEMI Family: vendored from semierendonmez-commits/acid Source/engine; only change: wrapped in namespace acid
// (one plugin binary links ~35 engines).
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace acid {

class Tb3poCore
{
public:
    static constexpr int maxSteps = 32;
    static constexpr int numBanks = 8;

    struct GenParams
    {
        int   density  = 4;      // -7..+7
        float accent   = 0.5f;   // 0..1, 0.5 = module's odds
        float slide    = 0.5f;   // 0..1, 0.5 = module's odds
        float octaves  = 0.4f;   // 0..1, chance of a ±1 octave jump (module: 40 %)
        int   scaleLen = 7;

        bool operator== (const GenParams& o) const
        {
            return density == o.density && accent == o.accent && slide == o.slide
                && octaves == o.octaves && scaleLen == o.scaleLen;
        }
        bool operator!= (const GenParams& o) const { return ! (*this == o); }
    };

    struct Step
    {
        uint8_t gate = 0, accent = 0, slide = 0;
        uint8_t degree = 0;     // index into the scale
        int8_t  octave = 0;     // -1, 0, +1
        uint8_t edited = 0;     // comes from an override
    };

    // Hand edit: kind 0 rest, 1 note, 2 accent, 3 slide.
    struct Override { uint8_t on = 0, kind = 0, degree = 0; int8_t octave = 0; };

    struct Pattern
    {
        uint16_t seed = 0x3B03;
        std::array<uint32_t, maxSteps> salt {};
        std::array<Override, maxSteps> edits {};
    };

    void generate (const GenParams&);

    void newSeed (uint32_t entropy);
    void mutate (uint32_t entropy, float amount = 0.25f);
    void setStep (int i, int kind, int degree, int octave);
    void clearEdits();

    void pushUndo();
    bool undo();
    bool canUndo() const { return undoValid; }

    void store (int bank);
    bool recall (int bank);
    bool bankFilled (int b) const { return filled[(size_t) b]; }

    const Step& step (int i) const { return steps[(size_t) i]; }
    // A step sounds when gated, or when the step before it slides into it.
    bool plays (int i, int prev) const
    {
        return steps[(size_t) i].gate || (prev >= 0 && steps[(size_t) prev].slide);
    }

    Pattern pattern;
    std::array<Pattern, numBanks> banks {};
    std::array<bool, numBanks> filled {};

private:
    double draw (int step, int field) const;

    std::array<Step, maxSteps> steps {};
    Pattern undoPattern;
    bool undoValid = false;
};

// Scales: semitone offsets, -1 terminated.
struct Tb3poScale { const char* name; int len; int8_t semis[12]; };
extern const Tb3poScale tb3poScales[];
extern const int tb3poNumScales;

} // namespace acid
