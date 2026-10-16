// SEMI port: FLOATER's patch layer without JUCE. Port of Source/Params.h, the parameter layout, state,
// NEW PATCH, MUTATE and the factory patches of Source/PluginProcessor.cpp / Source/Presets.cpp
// (semierendonmez-commits/floater eba7532). The parameter ids, ranges, defaults and the randomisation
// are the plug-in's; the AudioProcessorValueTreeState is replaced by a table of atomics.
//
// Threads: every value is an atomic, so the GUI may set single values while the audio thread reads
// them (as the plug-in's raw parameter values). Bulk operations (factory patch, NEW PATCH, MUTATE,
// apply) are meant for one thread at a time; the Rack module runs them at the start of a block. Their
// ids are short (std::string's small buffer), so they do not allocate once factoryPresets() has been
// built (the constructor does that).
#pragma once

#include "Engine.h"
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace flt
{

namespace ids
{
    inline std::string n1 (const char* p, int i) { return p + std::to_string (i + 1); }
    // ---- clock
    inline constexpr const char* run = "run", *bpm = "bpm", *swing = "swing", *rotate = "rotate", *loop = "loop", *seed = "seed",
                                 *bmax = "bmax", *bshape = "bshape", *scale = "scale", *loose = "loose", *ebb = "ebb";
    // ---- per lane (1..8)
    inline std::string lon (int i)    { return n1 ("lon", i); }
    inline std::string steps (int i)  { return n1 ("steps", i); }
    inline std::string hits (int i)   { return n1 ("hits", i); }
    inline std::string offs (int i)   { return n1 ("offs", i); }
    inline std::string div (int i)    { return n1 ("div", i); }
    inline std::string chance (int i) { return n1 ("chance", i); }
    inline std::string burst (int i)  { return n1 ("burst", i); }
    inline std::string draw (int i)   { return n1 ("draw", i); }
    inline std::string patLo (int i)  { return n1 ("patlo", i); }
    inline std::string patHi (int i)  { return n1 ("pathi", i); }
    inline std::string lock (int i)   { return n1 ("lock", i); }
    inline std::string vary (int i)   { return n1 ("vary", i); }
    inline std::string logic (int i)  { return n1 ("logic", i); }
    inline std::string rprot (int i)  { return n1 ("rprot", i); }
    inline std::string dact (int i)   { return n1 ("dact", i); }
    inline std::string cond (int i, int s)         { return "cond" + std::to_string (i + 1) + "_" + std::to_string (s + 1); }
    inline std::string plock (int i, int w, int s) { return "pl" + std::to_string (i + 1) + "_" + std::to_string (w + 1) + "_" + std::to_string (s + 1); }
    inline std::string seqv (int k)                { return n1 ("seqv", k); }
    // ---- per module (1..7)
    inline std::string tune (int i)   { return n1 ("tune", i); }
    inline std::string decay (int i)  { return n1 ("decay", i); }
    inline std::string color (int i)  { return n1 ("color", i); }
    inline std::string lvl (int i)    { return n1 ("lvl", i); }
    inline std::string pan (int i)    { return n1 ("pan", i); }
    inline std::string send (int i)   { return n1 ("send", i); }
    inline std::string model (int i)  { return n1 ("model", i); }
    inline std::string harm (int i)   { return n1 ("harm", i); }
    inline std::string morph (int i)  { return n1 ("morph", i); }
    inline std::string xmod (int i)   { return n1 ("xmod", i); }
    inline std::string xsrc (int i)   { return n1 ("xsrc", i); }
    inline std::string xamt (int i)   { return n1 ("xamt", i); }
    inline std::string sus (int i)    { return n1 ("sus", i); }
    // ---- scatter (lane 8)
    inline constexpr const char* ssize = "ssize", *sdens = "sdens", *spitch = "spitch", *sspread = "sspread", *sfeed = "sfeed", *smix = "smix",
                                 *smodel = "smodel", *spos = "spos", *stex = "stex", *scpitch = "scpitch", *sbverb = "sbverb";
    // ---- cv
    inline std::string rate (int s)   { return n1 ("rate", s); }
    inline std::string depth (int s)  { return n1 ("depth", s); }
    inline std::string pin (int s, int d) { return "pin" + std::to_string (s + 1) + "_" + std::to_string (d + 1); }
    // ---- fx / bus
    inline constexpr const char* ddiv = "ddiv", *dfb = "dfb", *dbreak = "dbreak", *dmix = "dmix",
                                 *vmodel = "vmodel", *vsize = "vsize", *vdecay = "vdecay", *vdamp = "vdamp", *vmix = "vmix", *pgm = "pgm",
                                 *bcomp = "bcomp", *bchar = "bchar", *drive = "drive", *crunch = "crunch", *trans = "trans", *boom = "boom",
                                 *level = "level";
    inline constexpr const char* dstyle = "dstyle", *dmach = "dmach", *drate = "drate",
                                 *mmix = "mmix", *mfreq = "mfreq", *mq = "mq", *mspread = "mspread", *mglide = "mglide", *mscale = "mscale", *mrot = "mrot";
    inline constexpr const char* turns = "turns", *wall = "wall", *midiout = "midiout", *midich = "midich";
    inline constexpr const char* seqlen = "seqlen", *seqx = "seqx", *seqy = "seqy", *seqglide = "seqglide",
                                 *rchaos = "rchaos", *rlen = "rlen", *rgene = "rgene", *rslide = "rslide", *rvs = "rvs", *rmorph = "rmorph",
                                 *rorg = "rorg", *rorgby = "rorgby", *rsplit = "rsplit", *rduck = "rduck", *rduckby = "rduckby",
                                 *rhold = "rhold", *ranchor = "ranchor", *fdbk = "fdbk", *fdbkt = "fdbkt";
    inline constexpr const char* dlevel = "dlevel", *dpitch = "dpitch", *dratio = "dratio", *dfine = "dfine", *dfmab = "dfmab", *dfmba = "dfmba",
                                 *dring = "dring", *dsync = "dsync", *dfold = "dfold", *dshape = "dshape", *dbright = "dbright", *dchaos = "dchaos",
                                 *dsend = "dsend", *dduck = "dduck", *darc = "darc", *dpcv = "dpcv", *dfcv = "dfcv", *dfoldcv = "dfoldcv", *dlcv = "dlcv",
                                 *rmix = "rmix", *rdecay = "rdecay", *rbright = "rbright", *arclen = "arclen", *arcshape = "arcshape",
                                 *sfreeze = "sfreeze", *vinf = "vinf";

    inline int loopBars (int choice) { static const int b[] { 0, 1, 2, 4, 8 }; return b[choice < 0 ? 0 : (choice > 4 ? 4 : choice)]; }
    inline constexpr int kFirstBeads = 7;   // SCATTER models 7..10: Beads in its four quality settings

    bool neverRandom (const std::string& id);
    bool isModelChoice (const std::string& id);
} // namespace ids

// Choice lists of the menus (the plug-in's).
const std::vector<std::string>& loopNames();
const std::vector<std::string>& burstShapeNames();
const std::vector<std::string>& programChangeNames();
const std::vector<std::string>& scatModelNames();
const std::vector<std::string>& xsrcNames();
const std::vector<std::string>& seqXNames();
const std::vector<std::string>& laneByNames();   // OFF, KICK .. SCATTER
const std::vector<std::string>& verbModelNames();
const std::vector<std::string>& busCharNames();
const std::vector<std::string>& cvChoiceNames();  // --, then the sources
const std::vector<std::string>& logicChoiceNames();
std::vector<std::string> modelNames (int slot);  // the slot's own voice, PLAITS ..., BRAIDS ..., REEL SPLICE

struct ParamDef
{
    enum Kind { real, integer, choice, boolean };
    std::string id, name;
    float min = 0.0f, max = 1.0f, def = 0.0f;
    Kind kind = real;
    std::vector<std::string> choices;   // choice
    bool stepped() const { return kind != real; }
};

class Case
{
public:
    Case();

    static const std::vector<ParamDef>& defs();
    static int index (const std::string& id);          // -1 when unknown
    static int numParams() { return (int) defs().size(); }

    float get (int i) const { return i >= 0 ? values[(size_t) i].load (std::memory_order_relaxed) : 0.0f; }
    float get (const std::string& id) const { return get (index (id)); }
    void set (int i, float plain);                     // clamped, stepped params rounded
    void set (const std::string& id, float plain) { set (index (id), plain); }
    float defaultValue (int i) const { return defs()[(size_t) i].def; }

    // ---- drawn steps, step conditions and locks, SEQ, LOCK's flips (as the plug-in keeps them)
    uint32_t pattern (int lane) const { return patterns[(size_t) lane].load (std::memory_order_relaxed); }
    void setPattern (int lane, uint32_t p) { patterns[(size_t) lane].store (p, std::memory_order_relaxed); }
    int cond (int lane, int step) const { return conds[(size_t) lane][(size_t) step].load (std::memory_order_relaxed); }
    void setCond (int lane, int step, int c);
    float plock (int lane, int which, int step) const { return plocks[(size_t) lane][(size_t) which][(size_t) step].load (std::memory_order_relaxed); }
    void setPlock (int lane, int which, int step, float v);
    float seqValue (int k) const { return seqValues[(size_t) k].load (std::memory_order_relaxed); }
    void setSeqValue (int k, float v);
    void resetMutations() { flipReset.fetch_add (1, std::memory_order_relaxed); }
    void clearStepData (int lane);
    void setLaneDraw (int lane, bool draw);
    void toggleStep (int lane, int step);

    // ---- locks ("lock from random": NEW PATCH and MUTATE leave the control alone)
    bool isLocked (int i) const { return i >= 0 && locks[(size_t) i].load (std::memory_order_relaxed) != 0; }
    bool isLocked (const std::string& id) const { return isLocked (index (id)); }
    void setLocked (int i, bool on) { if (i >= 0) locks[(size_t) i].store (on ? 1 : 0, std::memory_order_relaxed); }

    // ---- the engine's view
    Params currentParams() const;

    // ---- patching
    void newPatch();
    void mutate (float amount = 0.25f);
    struct Preset { std::string name, category; std::map<std::string, float> values; };
    static const std::vector<Preset>& factoryPresets();
    static const std::vector<std::string>& presetCategories();
    void loadFactory (int index);
    void applyValues (const std::map<std::string, float>& values);   // what is not in it goes back to its default
    std::map<std::string, float> snapshotValues() const;             // every parameter + the step data
    // the patch name (fixed buffer: set from the audio thread too) and whether MUTATE has touched it
    char presetName[48] = "Floor Patch";
    bool modified = false;
    int factoryIndex = 0;
    void setName (const char* n);
    void seedRandom (uint32_t s) { rng = s ? s : 1u; }

    // bumped by every bulk change (the module re-reads its panel knobs then)
    std::atomic<uint32_t> serial { 1 };
    void touch() { serial.fetch_add (1, std::memory_order_relaxed); }

private:
    void newPatchV4();
    void writeStepData (std::map<std::string, float>& m) const;
    void readStepData (const std::map<std::string, float>& m);
    float nextFloat() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float) (rng >> 8) * (1.0f / 16777216.0f); }
    int nextInt (int n) { return n <= 1 ? 0 : std::min (n - 1, (int) (nextFloat() * (float) n)); }

    std::unique_ptr<std::atomic<float>[]> values;
    std::unique_ptr<std::atomic<uint8_t>[]> locks;
    std::array<std::atomic<uint32_t>, kLanes> patterns {};
    std::array<std::array<std::atomic<uint8_t>, kMaxSteps>, kLanes> conds {};
    std::array<std::array<std::array<std::atomic<float>, kMaxSteps>, 3>, kLanes> plocks {};
    std::array<std::atomic<float>, 16> seqValues {};
    std::atomic<int> flipReset { 0 };
    uint32_t rng = 0x9E3779B9u;

    // indices looked up once (no string work in currentParams)
    struct Ix;
    static const Ix& ix();
};

} // namespace flt
