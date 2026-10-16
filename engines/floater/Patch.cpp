#include "Patch.h"
#include "Schwung.h"
#include "Reel.h"
#include "Drone.h"
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cctype>
#include <algorithm>

namespace flt
{
namespace
{
std::string f (const char* fmt, ...)
{
    char buf[512];
    va_list a;
    va_start (a, fmt);
    std::vsnprintf (buf, sizeof (buf), fmt, a);
    va_end (a);
    return buf;
}
int pc (float v) { return (int) std::lround (v * 100.0f); }

std::string lower (const char* s)
{
    std::string r (s);
    for (auto& c : r) c = (char) std::tolower ((unsigned char) c);
    return r;
}

const char* character (int lane, const VoiceParams& v)
{
    switch (lane)
    {
        case kick:    return v.color > 0.6f ? "a long pitch dive, driven" : (v.color > 0.3f ? "a short pitch dive" : "nearly a pure sine");
        case mesh:    return v.color > 0.6f ? "mostly noise, snappy" : (v.color > 0.3f ? "head and wires in balance" : "mostly the drum head");
        case fm:      return v.color > 0.6f ? "a high index, metallic" : (v.color > 0.3f ? "a woody pluck" : "a soft, low index");
        case opto:    return v.color > 0.6f ? "folded hard, the gate wide open" : (v.color > 0.3f ? "a little fold" : "a round blip, the gate barely opening");
        case lantern: return v.color > 0.6f ? "the six squares, metallic" : (v.color > 0.3f ? "metal and noise" : "almost only white noise");
        case zap:     return v.color > 0.6f ? "slow falling zaps from far up" : (v.color > 0.3f ? "laser zaps" : "clicks, the fall is almost instant");
        case strike:  return v.color > 0.6f ? "bright strings, a tight strum" : (v.color > 0.3f ? "warm strings" : "dark strings, a slow strum");
        default: return "";
    }
}
}

namespace
{
const char* laneName (int lane1) { static const char* n[] { "--", "KICK", "MESH", "FM", "OPTO", "LANTERN", "ZAP", "STRIKE", "SCATTER" }; return n[std::max (0, std::min (8, lane1))]; }

// LOCK, logic, conditions, step locks and VARY of one lane, as a clause
std::string laneExtras (const LaneParams& L)
{
    std::string s;
    if (L.lock < 0.995f) s += f (", unlocked to %d%% so the pattern walks around what is set (a Turing Machine)", pc (L.lock));
    if (L.logic > 0)
    {
        static const char* how[] { "only together with %s", "merged with %s", "where it or %s plays but not both", "only where %s does not" };
        s += ", " + f (how[std::max (0, std::min (3, (L.logic - 1) / 8))], laneName ((L.logic - 1) % 8 + 1));
    }
    int conds = 0, locked = 0;
    std::string which;
    for (int st = 0; st < std::min (kMaxSteps, L.steps); ++st)
    {
        if (L.cond[(size_t) st] != 0)
        {
            if (conds++ < 3) which += (which.empty() ? "" : ", ") + f ("%d on %s", st + 1, condNames()[(size_t) L.cond[(size_t) st]]);
        }
        bool any = false;
        for (int w = 0; w < 3; ++w) any = any || std::abs (L.plock[(size_t) w][(size_t) st]) > 0.005f;
        locked += any;
    }
    if (conds > 0) s += f (", %d step%s with a condition (%s%s)", conds, conds > 1 ? "s" : "", which.c_str(), conds > 3 ? ", ..." : "");
    if (locked > 0) s += f (", %d step%s locking its own tune, colour or decay", locked, locked > 1 ? "s" : "");
    if (L.vary > 0.01f) s += f (", every hit drifting %d%%", pc (L.vary));
    return s;
}

bool playing (const LaneParams& L) { return L.draw ? (L.pattern & (L.steps >= 32 ? 0xffffffffu : ((1u << std::max (1, L.steps)) - 1u))) != 0 : L.hits > 0; }
}

std::vector<std::string> patchNotes (const Params& p, double bpm, int rotation)
{
    std::vector<std::string> out;
    const auto& names = laneNames();

    // ---- clock
    {
        std::string s = f ("Everything clocked from one master clock at %d BPM, sixteenths", (int) std::lround (bpm));
        if (p.swing > 0.01f) s += f (", %d%% swing", pc (p.swing * 2.0f));
        if (p.loose > 0.66f) s += ", but the hits float free of the grid (anywhere in their step)";
        else if (p.loose > 0.02f) s += f (", the hits %d%% off the grid", pc (p.loose));
        if (p.ebb > 0.02f) s += f (", coming in waves (ebb %d%%)", pc (p.ebb));
        s += f (". The clock runs into the rotating divider (rotation %d), so ", rotation);
        std::string divs;
        for (int i = 0; i < kLanes; ++i)
        {
            if (! p.lanes[(size_t) i].on) continue;
            if (! divs.empty()) divs += ", ";
            divs += f ("%s /%d", names[(size_t) i], laneDivision (p, i, rotation));
        }
        s += divs.empty() ? std::string ("every lane is muted.") : divs + ".";
        if (p.loopBars > 0) s += f (" All the random repeats every %d bar%s (seed %d).", p.loopBars, p.loopBars > 1 ? "s" : "", p.seed);
        else s += f (" The random never repeats (seed %d).", p.seed);
        if (p.turns > 0.01f) s += f (" Before every 4, 8, 16 and 32-bar line the lanes fill (turns %d%%)%s.", pc (p.turns),
                                     p.wall ? ", with a beat of silence before the 16s and 32s" : "");
        out.push_back (s);
    }

    // ---- lanes
    {
        std::string s;
        static const char* shapes[] { "bouncing-ball spacing", "even spacing", "slowing down" };
        for (int i = 0; i < kLanes; ++i)
        {
            const auto& L = p.lanes[(size_t) i];
            if (! L.on || ! playing (L)) continue;
            if (L.draw)
            {
                std::string steps;
                for (int st = 0; st < std::min (32, L.steps); ++st) if ((L.pattern >> st) & 1u) steps += (steps.empty() ? "" : " ") + std::to_string (st + 1);
                s += f ("%s plays the steps it was given (%s of %d)", names[(size_t) i], steps.c_str(), L.steps);
            }
            else
            {
                s += f ("%s plays %d of %d steps", names[(size_t) i], std::min (L.hits, L.steps), L.steps);
                if (L.offset != 0) s += f (" (offset %d)", L.offset);
            }
            s += L.chance < 0.995f ? f (" through a %d%% probability gate", pc (L.chance)) : std::string (", no gate");
            if (L.burst > 0.005f) s += f (", %d%% of the hits burst into up to %d ratchets", pc (L.burst), p.burstMax);
            s += laneExtras (L);
            s += ". ";
        }
        if (! s.empty()) s += f ("Bursts use %s.", shapes[std::max (0, std::min (2, p.burstShape))]);
        out.push_back (s.empty() ? std::string ("No lane plays.") : s);
    }

    // ---- modules
    {
        std::string s;
        for (int v = 0; v < kVoices; ++v)
        {
            if (! p.lanes[(size_t) v].on || ! playing (p.lanes[(size_t) v])) continue;
            const auto& vp = p.voices[(size_t) v];
            if (isPlaits (vp.model))
            {
                const auto& m = plaitsModels()[(size_t) (vp.model - kFirstPlaits)];
                std::string eng = lower (m.name);
                s += f ("%s: a Plaits running its %s engine, %s %d%%, %s %d%%", names[(size_t) v], eng.c_str(),
                        lower (m.timbre).c_str(), pc (vp.color), lower (m.morph).c_str(), pc (vp.morph));
            }
            else if (isReel (vp.model))
                s += f ("%s: plays the REEL itself, slices of %s from %d%% into it, %+d semitones", names[(size_t) v],
                        geneNames()[(size_t) std::max (0, std::min (5, (int) std::lround (vp.harm * 5.0f)))], pc (vp.color),
                        (int) std::lround ((vp.tune - 0.5f) * 48.0f));
            else if (isBraids (vp.model))
                s += f ("%s: a Braids in %s, timbre %d%%, color %d%%, through a low pass at %d%%", names[(size_t) v],
                        braidsModels()[(size_t) (vp.model - kFirstBraids)].name, pc (vp.color), pc (vp.harm), pc (vp.morph));
            else
                s += f ("%s: %s", names[(size_t) v], character (v, vp));
            if (v == strike) s += f (", chords from the %s scale", lower (scaleNames()[(size_t) std::max (0, std::min (5, p.scale))]).c_str());
            if (vp.xmod > 0 && vp.xamt > 0.01f)
            {
                static const char* how[] { "", "ring-modulated by", "phase-modulated by", "opened by the loudness of", "ducked by" };
                s += f (", %s %s at %d%%", how[std::max (0, std::min (4, vp.xmod))],
                        vp.xsrc == kXsrcFeedback ? "the feedback loop's rings" : laneName (vp.xsrc + 1), pc (vp.xamt));
            }
            if (vp.send > 0.05f) s += f (", %d%% to the effects", pc (vp.send));
            s += ". ";
        }
        static const char* cloudsMode[] { "granular", "stretch", "looping delay", "spectral" };
        static const char* beadsQuality[] { "hi-fi", "clean tape", "lo-fi", "dirty tape" };
        if (p.scatModel >= 7)
        {
            const int perSecond = p.scatDensity < 0.001f ? 0 : (int) std::lround (0.5f * std::pow (2.0f, p.scatDensity * 6.3f));
            const char* length = p.scatSize > 0.985f ? "no" : (p.scatSize > 0.6f ? "long" : (p.scatSize > 0.3f ? "medium" : "short"));
            if (p.lanes[scatter].on && playing (p.lanes[scatter]))
                s += f ("Lane 8 seeds a Beads over the whole case (%s): a grain on every hit and about %d more a second of its own, %s grains, pitch %+d, its bursts holding FREEZE.",
                        beadsQuality[std::max (0, std::min (3, p.scatModel - 7))], perSecond, length, p.scatCloudsPitch);
            else
                s += f ("A Beads over the whole case (%s) seeds its own grains, about %d a second, %s grains, pitch %+d.",
                        beadsQuality[std::max (0, std::min (3, p.scatModel - 7))], perSecond, length, p.scatCloudsPitch);
            if (p.scatSize > 0.99f) s += " SIZE is all the way up: it is a delay.";
        }
        else if (p.lanes[scatter].on && playing (p.lanes[scatter]) && p.scatModel == 5)
            s += f ("Lane 8 plays a Clouds running the Parasites firmware in OLIVERB: a modulated plate, size %d%%, shimmer %+d.",
                    pc (p.scatSize), p.scatCloudsPitch);
        else if (p.lanes[scatter].on && playing (p.lanes[scatter]) && p.scatModel == 6)
            s += f ("Lane 8 strikes a Clouds running the Parasites firmware in RESONESTOR: combs tuned to a chord (%d%%), %+d semitones.",
                    pc (p.scatSize), p.scatCloudsPitch);
        else if (p.lanes[scatter].on && playing (p.lanes[scatter]) && p.scatModel > 0)
            s += f ("Lane 8 goes into the TRIG input of a Clouds over the whole case, in %s mode, pitch %+d, its bursts holding FREEZE.",
                    cloudsMode[std::max (0, std::min (3, p.scatModel - 1))], p.scatCloudsPitch);
        else if (p.lanes[scatter].on && playing (p.lanes[scatter]))
            s += f ("Lane 8 triggers the SCATTER over the whole case: clouds of up to %d grains, %s, %s.",
                    2 + (int) std::lround (p.scatDensity * 14.0f),
                    p.scatSize > 0.6f ? "long grains" : (p.scatSize > 0.3f ? "medium grains" : "tiny grains"),
                    p.scatPitch > 0.5f ? "pitched to octaves and fifths" : (p.scatPitch > 0.1f ? "now and then transposed" : "at pitch"));
        out.push_back (s);
    }

    // ---- cv
    {
        std::string s;
        const auto& src = sourceNames();
        const auto& dst = destNames();
        for (int i = 0; i < kSources; ++i)
        {
            std::string to;
            for (int d = 0; d < kDests; ++d)
            {
                const int pin = p.pins[(size_t) i][(size_t) d];
                if (pin == 0) continue;
                if (! to.empty()) to += ", ";
                to += f ("%s (%s)", dst[(size_t) d], pin > 0 ? "+" : "inverted");
            }
            if (to.empty()) continue;
            s += f ("%s at %d%% patched to %s. ", src[(size_t) i], pc (p.depth[(size_t) i]), to.c_str());
        }
        if (s.empty()) s = "Nothing patched in the matrix: every module stays where its knobs are.";
        else
        {
            s += "FORGE is struck by the kick; STEPPED, CHAOS and SMOOTH change on the clock; WOGGLE chases STEPPED and rings past each step.";
            bool seqUsed = false;
            for (int d = 0; d < kDests; ++d) seqUsed = seqUsed || p.pins[seq][(size_t) d] != 0;
            if (seqUsed && p.seqY > 0)
                s += f (" SEQ is a 4 x 4 grid of voltages, X moved on by %s, Y by %s (Cartesian).", p.seqX > 0 ? laneName (p.seqX) : "the clock", laneName (p.seqY));
            else if (seqUsed)
                s += f (" SEQ steps through %d voltages, one on every %s.", std::max (1, std::min (16, p.seqLen)), p.seqX > 0 ? (std::string (laneName (p.seqX)) + " hit").c_str() : "clock step");
        }
        out.push_back (s);
    }

    // ---- effects
    {
        static const char* pgm[] { "", "changing program every bar", "changing program every 2 bars", "changing program every 4 bars", "changing program on every lane 8 hit" };
        static const char* how[] { "looping a slice forward then backward", "rolling a slice faster and higher", "playing a slice backwards", "slowing it to a stop" };
        static const char* mach[] { "", ", through an S950's 12 bits", ", through an S1000", ", through Paula's 8 bits" };
        std::string s;
        if (p.smrMix > 0.01f)
        {
            static const char* by[] { "never rotated", "rotated by KICK", "rotated by MESH", "rotated by FM", "rotated by OPTO", "rotated by LANTERN",
                                      "rotated by ZAP", "rotated by STRIKE", "rotated by SCATTER" };
            s += f ("The sends ring through the SMR, six bands up the %s scale from %d Hz, %s. ",
                    lower (scaleNames()[(size_t) std::max (0, std::min (5, p.smrScale))]).c_str(),
                    (int) std::lround (40.0f * std::pow (16.0f, p.smrFreq)), by[std::max (0, std::min (8, p.smrRotateBy))]);
        }
        if (p.dlyDiv >= kDelayKarplus)
            s += f ("ZIGZAG is a Karplus-Strong string tuned to STRIKE's root%s, %d%% feedback%s", p.dlyDiv > kDelayKarplus ? " an octave up" : "",
                    pc (p.dlyFeedback), mach[std::max (0, std::min (3, p.dlyMachine))]);
        else
            s += f ("The sends go into ZIGZAG at %s with %d%% feedback%s", delayDivisionNames()[(size_t) std::max (0, std::min (6, p.dlyDiv))], pc (p.dlyFeedback),
                    mach[std::max (0, std::min (3, p.dlyMachine))]);
        if (p.dlyBreak > 0.01f) s += f ("; the bursts break its clock %d%% of the time, %s", pc (p.dlyBreak), how[std::max (0, std::min (3, p.dlyStyle))]);
        s += f (". %s, size %d%%, decay %d%%", p.verbModel == 1 ? "SPACE runs CloudSeed" : "SPACE reverb", pc (p.verbSize), pc (p.verbDecay));
        if (p.programChange > 0) s += f (", %s", pgm[std::min (4, p.programChange)]);
        static const char* chr[] { "soft (folding)", "medium", "hard" };
        s += f (". The whole case hits a Drum Buss: compressor %s, %s drive at %d%%, crunch %d%%, boom %d%%",
                p.busComp ? "on" : "off", chr[std::max (0, std::min (2, p.busChar))], pc (p.drive), pc (p.crunch), pc (p.boom));
        if (std::abs (p.trans) > 0.05f) s += f (", transients %+d%%", (int) std::lround (p.trans * 100.0f));
        s += ".";
        if (p.fdbk > 0.01f)
            s += f (" The master comes back %s later through the SMR's bands into the effects (feedback %d%%)%s.", fdbkTimeNames()[(size_t) std::max (0, std::min (3, p.fdbkTime))],
                    pc (p.fdbk), std::any_of (p.voices.begin(), p.voices.end(), [] (const VoiceParams& v) { return v.xmod > 0 && v.xsrc == kXsrcFeedback; })
                                 ? ", and on into the modules through X-MOD" : "");
        out.push_back (s);
    }

    // ---- the drone, the resonator, the beds
    {
        std::string s;
        if (p.dLevel > 0.005f)
        {
            s += f ("Under the drums a DRONE: two oscillators %s apart, %+d semitones from the key", droneRatioNames()[(size_t) std::max (0, std::min (kDroneRatios - 1, p.dRatio))], p.dPitch);
            std::string x;
            if (p.dFmAB > 0.02f) x += f ("%sB into A's phase %d%%", x.empty() ? "" : ", ", pc (p.dFmAB));
            if (p.dFmBA > 0.02f) x += f ("%sA into B's %d%%", x.empty() ? "" : ", ", pc (p.dFmBA));
            if (p.dRing > 0.02f) x += f ("%sring %d%%", x.empty() ? "" : ", ", pc (p.dRing));
            if (p.dSync > 0.02f) x += f ("%s%s sync", x.empty() ? "" : ", ", p.dSync > 0.9f ? "hard" : "soft");
            if (p.dFold > 0.02f) x += f ("%sfolded %d%%", x.empty() ? "" : ", ", pc (p.dFold));
            if (! x.empty()) s += " (" + x + ")";
            s += p.dChaos > 0.6f ? ", pushed into chaos" : (p.dChaos > 0.2f ? f (", CHAOS at %d%%", pc (p.dChaos)) : std::string());
            std::string acts;
            for (int i = 0; i < kLanes; ++i)
            {
                const int a = p.lanes[(size_t) i].droneAct;
                if (a <= 0 || ! p.lanes[(size_t) i].on) continue;
                static const char* how[] { "", "ducks it", "strikes its gate", "steps it up the scale", "jumps it to a random degree", "pings its filter", "hits its folder" };
                acts += (acts.empty() ? "" : ", ") + std::string (laneName (i + 1)) + " " + how[std::max (0, std::min (6, a))];
            }
            if (! acts.empty()) s += ". The lanes play it: " + acts;
            if (p.dArc > 0.02f) s += f (". It opens over %s with the ARC and falls into the line", arcLenNames()[(size_t) std::max (0, std::min (3, p.arcLen))]);
            if (p.dLpgCv > 0 && p.dLpgCv <= kSources) s += f (". %s breathes its gate", sourceNames()[(size_t) (p.dLpgCv - 1)]);
            s += ".";
        }
        if (p.resMix > 0.005f) s += std::string (s.empty() ? "" : " ") + "The drums ring four strings tuned to the drone's chord (a resonator, as Rings' sympathetic strings).";
        std::string held;
        for (int v = 0; v < kVoices; ++v) if (p.voices[(size_t) v].sus) held += (held.empty() ? "" : ", ") + std::string (names[(size_t) v]);
        if (! held.empty()) s += std::string (s.empty() ? "" : " ") + held + " hold" + (held.find (',') == std::string::npos ? "s" : "") + " its note (sustain): the hits re-strike it.";
        if (p.scatFreeze) s += std::string (s.empty() ? "" : " ") + "SCATTER is frozen into a bed.";
        if (p.verbInfinite) s += std::string (s.empty() ? "" : " ") + "SPACE holds its tail for ever.";
        if (! s.empty()) out.push_back (s);
    }

    // ---- the reel
    if (p.reelChaos > 0.005f || std::any_of (p.voices.begin(), p.voices.end(), [] (const VoiceParams& v) { return isReel (v.model); }))
    {
        std::string s = f ("Before the bus the master goes onto a REEL that keeps the last %s, a splice a bar", reelLengthNames()[(size_t) std::max (0, std::min (3, p.reelLength))]);
        s += p.reelHold ? " (held: it stopped recording)." : ".";
        if (p.reelChaos > 0.005f)
        {
            s += f (" On every %s it may break the master into a gene of itself (CHAOS %d%%)", geneNames()[(size_t) std::max (0, std::min (5, p.reelGene))], pc (p.reelChaos));
            const float rate = varispeedRate (p.reelVarispeed);
            if (std::abs (rate - 1.0f) > 0.02f) s += f (", read at %s%.2fx", rate < 0.0f ? "-" : "", std::abs (rate));
            if (p.reelMorph > 0.6f) s += ", the genes overlapping into pitched layers";
            else if (p.reelMorph < 0.35f) s += ", gaps between the genes";
            if (p.reelOrgBy > 0) s += f (", %s moving it on a splice", laneName (p.reelOrgBy));
            s += ". The order kept:";
            std::string kept;
            for (int i = 0; i < kLanes; ++i) if ((p.reelProtect >> i) & 1) kept += (kept.empty() ? " " : ", ") + std::string (names[(size_t) i]);
            if (! kept.empty()) s += kept + " never go" + (kept.find (',') == std::string::npos ? "es" : "") + " on the reel;";
            if (p.reelSplit > 0.0f) s += f (" below %d Hz it never breaks;", (int) std::lround (reelSplitHz (p.reelSplit)));
            if (p.reelAnchor) s += " the first beat of every 4-bar line stays clean;";
            if (p.reelDuck > 0.05f && p.reelDuckBy > 0) s += f (" every %s pulls it back to the dry for a moment;", laneName (p.reelDuckBy));
            s += p.loopBars > 0 ? " the breaks repeat with the loop." : " the breaks never repeat.";
        }
        out.push_back (s);
    }
    return out;
}

} // namespace flt
