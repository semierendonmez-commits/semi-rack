// DUBPLATE — genre templates. SEMI Family port of Source/Model/GenreTemplates.h
// (builtInTemplateNames returns std::vector<std::string>; otherwise unchanged).
#pragma once
#include <string>
#include <vector>
#include "Arrangement.h"

namespace dubplate
{
namespace GenreTemplates
{
    /** A ready-to-play template: sections, role lanes, patterns, events and fill profile.
        Lanes come without samples (the Rack port drives gate outputs). */
    Arrangement makeTemplate (Genre genre);

    /** An empty arrangement for building from scratch. */
    Arrangement makeBlank (Genre genre);

    /** Recomputes one section's cells from its energy and type (skips locked lanes unless forced). */
    void applyEnergy (Arrangement& arr, int sectionIndex, bool includeLocked = false);

    /** Places the genre's phrase-locked FX events on the sections. */
    void applyDefaultEvents (Arrangement& arr);

    /** Whether a role should play in a section of the given type and energy. Returns level 0 = off. */
    float cellLevelFor (Role role, Genre genre, SectionType type, float energy);

    std::vector<std::string> builtInTemplateNames();
}
} // namespace dubplate
