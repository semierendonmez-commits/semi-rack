// SEMI port: see Case.h. Ported from FLOATER Source/PluginProcessor.cpp (createParameterLayout,
// currentParams, newPatch, newPatchV4, mutate, step data, applyValues, snapshotValues, setLaneDraw,
// toggleStep) and Source/Params.h; juce::Random is replaced by a xorshift.
#include "Case.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <set>
#include <unordered_map>

namespace flt
{

namespace ids
{
bool neverRandom (const std::string& id)
{
    return id == run || id == bpm || id == level || id == midiout || id == midich || id == wall || id == rhold;
}

static bool startsWith (const std::string& s, const char* p) { return s.rfind (p, 0) == 0; }

bool isModelChoice (const std::string& id)
{
    return startsWith (id, "model") || startsWith (id, "draw") || id == smodel || id == vmodel || id == bchar || id == scale
        || id == dstyle || id == dmach || id == mscale || id == mrot || id == ddiv
        || startsWith (id, "logic") || startsWith (id, "xmod") || startsWith (id, "xsrc") || startsWith (id, "rprot")
        || id == seqx || id == seqy || id == rorgby || id == rduckby || id == ranchor || id == fdbkt
        || startsWith (id, "dact") || startsWith (id, "sus") || id == dratio || id == dpcv || id == dfcv || id == dfoldcv || id == dlcv || id == arclen
        || id == sfreeze || id == vinf;
}
} // namespace ids

template <size_t N>
static std::vector<std::string> list (const std::array<const char*, N>& a) { return std::vector<std::string> (a.begin(), a.end()); }

const std::vector<std::string>& loopNames() { static const std::vector<std::string> n { "FREE", "1 BAR", "2 BARS", "4 BARS", "8 BARS" }; return n; }
const std::vector<std::string>& burstShapeNames() { static const std::vector<std::string> n { "BOUNCE", "EVEN", "SLOWING" }; return n; }
const std::vector<std::string>& programChangeNames() { static const std::vector<std::string> n { "OFF", "EVERY BAR", "EVERY 2", "EVERY 4", "ON LANE 8" }; return n; }
const std::vector<std::string>& scatModelNames()
{
    static const std::vector<std::string> n { "FLOATER GRAINS", "CLOUDS GRANULAR", "CLOUDS STRETCH", "CLOUDS LOOPER", "CLOUDS SPECTRAL",
                                              "OLIVERB", "RESONESTOR", "BEADS HI-FI", "BEADS CLEAN TAPE", "BEADS LO-FI", "BEADS DIRTY TAPE" };
    return n;
}
const std::vector<std::string>& xsrcNames() { static const std::vector<std::string> n { "KICK", "MESH", "FM", "OPTO", "HATS", "ZAP", "STRK", "FDBK", "DRONE" }; return n; }
const std::vector<std::string>& seqXNames() { static const std::vector<std::string> n { "CLOCK", "KICK", "MESH", "FM", "OPTO", "LANTERN", "ZAP", "STRIKE", "SCATTER" }; return n; }
const std::vector<std::string>& laneByNames() { static const std::vector<std::string> n { "OFF", "KICK", "MESH", "FM", "OPTO", "LANTERN", "ZAP", "STRIKE", "SCATTER" }; return n; }
const std::vector<std::string>& verbModelNames() { static const std::vector<std::string> n { "FDN", "CLOUDSEED" }; return n; }
const std::vector<std::string>& busCharNames() { static const std::vector<std::string> n { "SOFT", "MED", "HARD" }; return n; }
const std::vector<std::string>& cvChoiceNames()
{
    static const std::vector<std::string> n = [] { std::vector<std::string> v { "--" }; for (auto* s : sourceNames()) v.push_back (s); return v; }();
    return n;
}
const std::vector<std::string>& logicChoiceNames()
{
    static const std::vector<std::string> n = []
    {
        std::vector<std::string> v { "--" };
        static const char* lanes[] { "KICK", "MESH", "FM", "OPTO", "HATS", "ZAP", "STRK", "SCAT" };
        for (auto* op : logicOpNames()) for (auto* l : lanes) v.push_back (std::string (op) + " " + l);
        return v;
    }();
    return n;
}
std::vector<std::string> modelNames (int slot)
{
    std::vector<std::string> m { nativeLabels()[(size_t) std::max (0, std::min (kVoices - 1, slot))].model };
    for (auto& p : plaitsModels()) m.push_back (std::string ("PLAITS ") + p.name);
    for (auto& b : braidsModels()) m.push_back (std::string ("BRAIDS ") + b.name);
    m.push_back ("REEL SPLICE");
    return m;
}

//==============================================================================
const std::vector<ParamDef>& Case::defs()
{
    static const std::vector<ParamDef> v = []
    {
        std::vector<ParamDef> d;
        auto fl01 = [&] (const std::string& id, const std::string& name, float def) { d.push_back ({ id, name, 0.0f, 1.0f, def, ParamDef::real, {} }); };
        auto rng = [&] (const std::string& id, const std::string& name, float lo, float hi, float def) { d.push_back ({ id, name, lo, hi, def, ParamDef::real, {} }); };
        auto in = [&] (const std::string& id, const std::string& name, int lo, int hi, int def) { d.push_back ({ id, name, (float) lo, (float) hi, (float) def, ParamDef::integer, {} }); };
        auto ch = [&] (const std::string& id, const std::string& name, const std::vector<std::string>& c, int def)
        { d.push_back ({ id, name, 0.0f, (float) c.size() - 1.0f, (float) def, ParamDef::choice, c }); };
        auto bo = [&] (const std::string& id, const std::string& name, bool def) { d.push_back ({ id, name, 0.0f, 1.0f, def ? 1.0f : 0.0f, ParamDef::boolean, {} }); };

        // ---- clock
        bo (ids::run, "Run", true);
        rng (ids::bpm, "Tempo", 40.0f, 220.0f, 150.0f);
        rng (ids::swing, "Swing", 0.0f, 0.5f, 0.0f);
        fl01 (ids::loose, "Free", 0.0f);
        fl01 (ids::ebb, "Ebb", 0.0f);
        in (ids::rotate, "Rotate", 0, 7, 0);
        ch (ids::loop, "Loop", loopNames(), 2);
        in (ids::seed, "Seed", 0, 9999, 1);
        in (ids::bmax, "Burst Max", 2, 8, 4);
        ch (ids::bshape, "Burst Shape", burstShapeNames(), 0);
        ch (ids::scale, "Scale", list (scaleNames()), 0);
        // ---- lanes
        for (int i = 0; i < kLanes; ++i)
        {
            const std::string n = std::string (laneNames()[(size_t) i]) + " ";
            bo (ids::lon (i), n + "On", true);
            in (ids::steps (i), n + "Steps", 1, kMaxSteps, 16);
            in (ids::hits (i), n + "Hits", 0, kMaxSteps, 4);
            in (ids::offs (i), n + "Offset", 0, kMaxSteps - 1, 0);
            in (ids::div (i), n + "Division", 1, 8, 1);
            fl01 (ids::chance (i), n + "Chance", 1.0f);
            fl01 (ids::burst (i), n + "Burst", 0.0f);
            bo (ids::draw (i), n + "Draw", false);
            fl01 (ids::lock (i), n + "Lock", 1.0f);
            fl01 (ids::vary (i), n + "Vary", 0.0f);
            ch (ids::logic (i), n + "Logic", logicChoiceNames(), 0);
            bo (ids::rprot (i), n + "Reel Protect", i == kick);
            ch (ids::dact (i), n + "Drone Act", list (droneActNames()), i == kick ? 1 : 0);
        }
        // ---- modules
        for (int i = 0; i < kVoices; ++i)
        {
            const std::string n = std::string (laneNames()[(size_t) i]) + " ";
            fl01 (ids::tune (i), n + "Tune", 0.5f);
            fl01 (ids::decay (i), n + "Decay", 0.4f);
            fl01 (ids::color (i), n + "Color", 0.5f);
            fl01 (ids::lvl (i), n + "Level", 0.8f);
            rng (ids::pan (i), n + "Pan", -1.0f, 1.0f, 0.0f);
            fl01 (ids::send (i), n + "Send", 0.2f);
            ch (ids::model (i), n + "Model", modelNames (i), 0);
            ch (ids::xmod (i), n + "X-Mod", list (xmodNames()), 0);
            ch (ids::xsrc (i), n + "X-Mod Source", xsrcNames(), i == kick ? 1 : 0);
            fl01 (ids::xamt (i), n + "X-Mod Amount", 0.5f);
            bo (ids::sus (i), n + "Sustain", false);
            fl01 (ids::harm (i), n + "Harm", i == fm ? 0.35f : 0.5f);
            fl01 (ids::morph (i), n + "Morph", 0.5f);
        }
        // ---- scatter
        fl01 (ids::ssize, "Scatter Size", 0.35f);
        fl01 (ids::sdens, "Scatter Density", 0.5f);
        fl01 (ids::spitch, "Scatter Pitch", 0.3f);
        fl01 (ids::sspread, "Scatter Spread", 0.6f);
        fl01 (ids::sfeed, "Scatter Feedback", 0.0f);
        fl01 (ids::smix, "Scatter Mix", 0.5f);
        ch (ids::smodel, "Scatter Model", scatModelNames(), 0);
        fl01 (ids::spos, "Scatter Position", 0.5f);
        fl01 (ids::stex, "Scatter Texture", 0.5f);
        fl01 (ids::sbverb, "Beads Reverb", 0.2f);
        in (ids::scpitch, "Clouds Pitch", -24, 24, 0);
        // ---- cv
        for (int s = 0; s < kSources; ++s)
        {
            const std::string n = std::string (sourceNames()[(size_t) s]) + " ";
            fl01 (ids::rate (s), n + "Rate", 0.4f);
            fl01 (ids::depth (s), n + "Depth", 0.5f);
            for (int dd = 0; dd < kDests; ++dd) in (ids::pin (s, dd), n + "> " + destNames()[(size_t) dd], -1, 1, 0);
        }
        // ---- fx / bus
        ch (ids::ddiv, "Delay Time", list (delayDivisionNames()), 3);
        ch (ids::dstyle, "Break Style", list (breakStyleNames()), 0);
        ch (ids::dmach, "Delay Machine", list (machineNames()), 0);
        fl01 (ids::drate, "Machine Rate", 0.5f);
        fl01 (ids::mmix, "SMR Mix", 0.0f);
        fl01 (ids::mfreq, "SMR Freq", 0.4f);
        fl01 (ids::mq, "SMR Q", 0.6f);
        fl01 (ids::mspread, "SMR Spread", 0.3f);
        fl01 (ids::mglide, "SMR Glide", 0.3f);
        ch (ids::mscale, "SMR Scale", list (scaleNames()), 0);
        ch (ids::mrot, "SMR Rotate By", laneByNames(), 0);
        fl01 (ids::dfb, "Delay Feedback", 0.45f);
        fl01 (ids::dbreak, "Delay Break", 0.3f);
        fl01 (ids::dmix, "Delay Mix", 0.3f);
        ch (ids::vmodel, "Space Model", verbModelNames(), 0);
        fl01 (ids::vsize, "Space Size", 0.6f);
        fl01 (ids::vdecay, "Space Decay", 0.5f);
        fl01 (ids::vdamp, "Space Damp", 0.4f);
        fl01 (ids::vmix, "Space Mix", 0.25f);
        ch (ids::pgm, "Program Change", programChangeNames(), 0);
        bo (ids::bcomp, "Bus Comp", false);
        ch (ids::bchar, "Bus Character", busCharNames(), 1);
        fl01 (ids::drive, "Bus Drive", 0.15f);
        fl01 (ids::crunch, "Bus Crunch", 0.05f);
        rng (ids::trans, "Bus Transients", -1.0f, 1.0f, 0.0f);
        fl01 (ids::boom, "Bus Boom", 0.15f);
        rng (ids::level, "Level", -24.0f, 6.0f, -3.0f);
        fl01 (ids::turns, "Turns", 0.0f);
        bo (ids::wall, "Wall", false);
        bo (ids::midiout, "MIDI Out", false);
        in (ids::midich, "MIDI Channel", 1, 16, 10);
        // ---- SEQ
        in (ids::seqlen, "Seq Length", 1, 16, 16);
        ch (ids::seqx, "Seq X", seqXNames(), 0);
        ch (ids::seqy, "Seq Y", laneByNames(), 0);
        fl01 (ids::seqglide, "Seq Glide", 0.0f);
        // ---- the REEL
        fl01 (ids::rchaos, "Reel Chaos", 0.0f);
        ch (ids::rlen, "Reel Length", list (reelLengthNames()), 1);
        ch (ids::rgene, "Reel Gene", list (geneNames()), 2);
        fl01 (ids::rslide, "Reel Slide", 0.0f);
        fl01 (ids::rvs, "Reel Varispeed", 0.75f);
        fl01 (ids::rmorph, "Reel Morph", 0.45f);
        fl01 (ids::rorg, "Reel Organize", 0.0f);
        ch (ids::rorgby, "Reel Organize By", laneByNames(), 0);
        fl01 (ids::rsplit, "Reel Split", 0.0f);
        fl01 (ids::rduck, "Reel Duck", 0.5f);
        ch (ids::rduckby, "Reel Duck By", laneByNames(), 1);
        bo (ids::rhold, "Reel Hold", false);
        bo (ids::ranchor, "Reel Anchor", true);
        // ---- FEEDBACK
        fl01 (ids::fdbk, "Feedback", 0.0f);
        ch (ids::fdbkt, "Feedback Time", list (fdbkTimeNames()), 1);
        // ---- the DRONE
        fl01 (ids::dlevel, "Drone Level", 0.0f);
        in (ids::dpitch, "Drone Pitch", -24, 24, -12);
        ch (ids::dratio, "Drone Ratio", list (droneRatioNames()), 6);
        fl01 (ids::dfine, "Drone Fine", 0.5f);
        fl01 (ids::dfmab, "Drone FM B>A", 0.2f);
        fl01 (ids::dfmba, "Drone FM A>B", 0.0f);
        fl01 (ids::dring, "Drone Ring", 0.0f);
        fl01 (ids::dsync, "Drone Sync", 0.0f);
        fl01 (ids::dfold, "Drone Fold", 0.1f);
        fl01 (ids::dshape, "Drone Shape B", 0.0f);
        fl01 (ids::dbright, "Drone Bright", 0.55f);
        fl01 (ids::dchaos, "Drone Chaos", 0.0f);
        fl01 (ids::dsend, "Drone Send", 0.3f);
        fl01 (ids::dduck, "Drone Duck", 0.6f);
        fl01 (ids::darc, "Drone Arc", 0.0f);
        ch (ids::dpcv, "Drone Pitch CV", cvChoiceNames(), 0);
        ch (ids::dfcv, "Drone FM CV", cvChoiceNames(), 0);
        ch (ids::dfoldcv, "Drone Fold CV", cvChoiceNames(), 0);
        ch (ids::dlcv, "Drone LPG CV", cvChoiceNames(), 0);
        bo (ids::rprot (kLanes), "Drone Reel Protect", true);
        fl01 (ids::rmix, "Resonator Mix", 0.0f);
        fl01 (ids::rdecay, "Resonator Decay", 0.5f);
        fl01 (ids::rbright, "Resonator Bright", 0.5f);
        ch (ids::arclen, "Arc Length", list (arcLenNames()), 2);
        fl01 (ids::arcshape, "Arc Shape", 0.8f);
        bo (ids::sfreeze, "Scatter Freeze", false);
        bo (ids::vinf, "Space Infinite", false);
        return d;
    }();
    return v;
}

static const std::unordered_map<std::string, int>& indexMap()
{
    static const std::unordered_map<std::string, int> m = []
    {
        std::unordered_map<std::string, int> r;
        const auto& d = Case::defs();
        for (int i = 0; i < (int) d.size(); ++i) r[d[(size_t) i].id] = i;
        return r;
    }();
    return m;
}

int Case::index (const std::string& id)
{
    const auto& m = indexMap();
    auto it = m.find (id);
    return it == m.end() ? -1 : it->second;
}

struct Case::Ix
{
    int loose, ebb, dlcv, sbverb, run, bpm, swing, rotate, loop, seed, bmax, bshape, scale;
    std::array<int, kLanes> lon, steps, hits, offs, div, chance, burst, draw, lock, vary, logic, rprot, dact;
    std::array<int, kVoices> tune, decay, color, lvl, pan, send, model, harm, morph, xmod, xsrc, xamt, sus;
    int ssize, sdens, spitch, sspread, sfeed, smix, smodel, spos, stex, scpitch;
    std::array<int, kSources> rate, depth;
    std::array<std::array<int, kDests>, kSources> pin;
    int dstyle, dmach, drate, mmix, mfreq, mq, mspread, mglide, mscale, mrot;
    int ddiv, dfb, dbreak, dmix, vmodel, vsize, vdecay, vdamp, vmix, pgm;
    int bcomp, bchar, drive, crunch, trans, boom, level, turns, wall;
    int seqlen, seqx, seqy, seqglide, rchaos, rlen, rgene, rslide, rvs, rmorph, rorg, rorgby, rsplit, rduck, rduckby, rhold, ranchor, fdbk, fdbkt;
    int dlevel, dpitch, dratio, dfine, dfmab, dfmba, dring, dsync, dfold, dshape, dbright, dchaos, dsend, dduck, darc, dpcv, dfcv, dfoldcv,
        rmix, rdecay, rbright, arclen, arcshape, sfreeze, vinf, dprot;
};

const Case::Ix& Case::ix()
{
    static const Ix x = []
    {
        Ix r {};
        auto P = [] (const std::string& id) { return index (id); };
        r.loose = P (ids::loose); r.ebb = P (ids::ebb); r.sbverb = P (ids::sbverb); r.dlcv = P (ids::dlcv);
        r.run = P (ids::run); r.bpm = P (ids::bpm); r.swing = P (ids::swing); r.rotate = P (ids::rotate); r.loop = P (ids::loop);
        r.seed = P (ids::seed); r.bmax = P (ids::bmax); r.bshape = P (ids::bshape); r.scale = P (ids::scale);
        for (int i = 0; i < kLanes; ++i)
        {
            const auto k = (size_t) i;
            r.lon[k] = P (ids::lon (i)); r.steps[k] = P (ids::steps (i)); r.hits[k] = P (ids::hits (i)); r.offs[k] = P (ids::offs (i));
            r.div[k] = P (ids::div (i)); r.chance[k] = P (ids::chance (i)); r.burst[k] = P (ids::burst (i)); r.draw[k] = P (ids::draw (i));
            r.lock[k] = P (ids::lock (i)); r.vary[k] = P (ids::vary (i)); r.logic[k] = P (ids::logic (i)); r.rprot[k] = P (ids::rprot (i));
            r.dact[k] = P (ids::dact (i));
        }
        for (int i = 0; i < kVoices; ++i)
        {
            const auto k = (size_t) i;
            r.tune[k] = P (ids::tune (i)); r.decay[k] = P (ids::decay (i)); r.color[k] = P (ids::color (i));
            r.lvl[k] = P (ids::lvl (i)); r.pan[k] = P (ids::pan (i)); r.send[k] = P (ids::send (i));
            r.model[k] = P (ids::model (i)); r.harm[k] = P (ids::harm (i)); r.morph[k] = P (ids::morph (i));
            r.xmod[k] = P (ids::xmod (i)); r.xsrc[k] = P (ids::xsrc (i)); r.xamt[k] = P (ids::xamt (i)); r.sus[k] = P (ids::sus (i));
        }
        r.ssize = P (ids::ssize); r.sdens = P (ids::sdens); r.spitch = P (ids::spitch); r.sspread = P (ids::sspread); r.sfeed = P (ids::sfeed); r.smix = P (ids::smix);
        r.smodel = P (ids::smodel); r.spos = P (ids::spos); r.stex = P (ids::stex); r.scpitch = P (ids::scpitch);
        for (int s = 0; s < kSources; ++s)
        {
            r.rate[(size_t) s] = P (ids::rate (s)); r.depth[(size_t) s] = P (ids::depth (s));
            for (int d = 0; d < kDests; ++d) r.pin[(size_t) s][(size_t) d] = P (ids::pin (s, d));
        }
        r.dstyle = P (ids::dstyle); r.dmach = P (ids::dmach); r.drate = P (ids::drate);
        r.mmix = P (ids::mmix); r.mfreq = P (ids::mfreq); r.mq = P (ids::mq); r.mspread = P (ids::mspread); r.mglide = P (ids::mglide);
        r.mscale = P (ids::mscale); r.mrot = P (ids::mrot);
        r.ddiv = P (ids::ddiv); r.dfb = P (ids::dfb); r.dbreak = P (ids::dbreak); r.dmix = P (ids::dmix);
        r.vmodel = P (ids::vmodel); r.vsize = P (ids::vsize); r.vdecay = P (ids::vdecay); r.vdamp = P (ids::vdamp); r.vmix = P (ids::vmix); r.pgm = P (ids::pgm);
        r.bcomp = P (ids::bcomp); r.bchar = P (ids::bchar); r.drive = P (ids::drive); r.crunch = P (ids::crunch); r.trans = P (ids::trans);
        r.boom = P (ids::boom); r.level = P (ids::level); r.turns = P (ids::turns); r.wall = P (ids::wall);
        r.seqlen = P (ids::seqlen); r.seqx = P (ids::seqx); r.seqy = P (ids::seqy); r.seqglide = P (ids::seqglide);
        r.rchaos = P (ids::rchaos); r.rlen = P (ids::rlen); r.rgene = P (ids::rgene); r.rslide = P (ids::rslide); r.rvs = P (ids::rvs);
        r.rmorph = P (ids::rmorph); r.rorg = P (ids::rorg); r.rorgby = P (ids::rorgby); r.rsplit = P (ids::rsplit); r.rduck = P (ids::rduck);
        r.rduckby = P (ids::rduckby); r.rhold = P (ids::rhold); r.ranchor = P (ids::ranchor); r.fdbk = P (ids::fdbk); r.fdbkt = P (ids::fdbkt);
        r.dlevel = P (ids::dlevel); r.dpitch = P (ids::dpitch); r.dratio = P (ids::dratio); r.dfine = P (ids::dfine); r.dfmab = P (ids::dfmab);
        r.dfmba = P (ids::dfmba); r.dring = P (ids::dring); r.dsync = P (ids::dsync); r.dfold = P (ids::dfold); r.dshape = P (ids::dshape);
        r.dbright = P (ids::dbright); r.dchaos = P (ids::dchaos); r.dsend = P (ids::dsend); r.dduck = P (ids::dduck); r.darc = P (ids::darc);
        r.dpcv = P (ids::dpcv); r.dfcv = P (ids::dfcv); r.dfoldcv = P (ids::dfoldcv); r.rmix = P (ids::rmix); r.rdecay = P (ids::rdecay);
        r.rbright = P (ids::rbright); r.arclen = P (ids::arclen); r.arcshape = P (ids::arcshape); r.sfreeze = P (ids::sfreeze); r.vinf = P (ids::vinf);
        r.dprot = P (ids::rprot (kLanes));
        return r;
    }();
    return x;
}

//==============================================================================
Case::Case()
{
    const auto& d = defs();
    values.reset (new std::atomic<float>[d.size()]);
    locks.reset (new std::atomic<uint8_t>[d.size()]);
    for (size_t i = 0; i < d.size(); ++i) { values[i].store (d[i].def); locks[i].store (0); }
    for (auto& p : patterns) p.store (0);
    for (auto& l : conds) for (auto& c : l) c.store (0);
    for (auto& l : plocks) for (auto& w : l) for (auto& v : w) v.store (0.0f);
    for (auto& v : seqValues) v.store (0.0f);
    (void) ix();
    (void) factoryPresets();
    loadFactory (0);
}

void Case::set (int i, float plain)
{
    if (i < 0 || i >= numParams()) return;
    const auto& d = defs()[(size_t) i];
    float v = std::max (d.min, std::min (d.max, plain));
    if (d.stepped()) v = std::round (v);
    values[(size_t) i].store (v, std::memory_order_relaxed);
}

void Case::setCond (int lane, int step, int c)
{
    conds[(size_t) lane][(size_t) step].store ((uint8_t) std::max (0, std::min (kConds - 1, c)), std::memory_order_relaxed);
}

void Case::setPlock (int lane, int which, int step, float v)
{
    plocks[(size_t) lane][(size_t) which][(size_t) step].store (std::max (-1.0f, std::min (1.0f, v)), std::memory_order_relaxed);
}

void Case::setSeqValue (int k, float v) { seqValues[(size_t) k].store (std::max (-1.0f, std::min (1.0f, v)), std::memory_order_relaxed); }

//==============================================================================
Params Case::currentParams() const
{
    const auto& r = ix();
    auto L = [this] (int i) { return values[(size_t) i].load (std::memory_order_relaxed); };
    auto I = [&] (int i) { return (int) std::lround (L (i)); };
    Params p;
    p.reelProtect = 0;
    p.run = L (r.run) > 0.5f; p.bpm = L (r.bpm); p.swing = L (r.swing); p.loose = L (r.loose); p.ebb = L (r.ebb); p.rotate = I (r.rotate);
    p.loopBars = ids::loopBars (I (r.loop)); p.seed = I (r.seed); p.burstMax = I (r.bmax); p.burstShape = I (r.bshape); p.scale = I (r.scale);
    for (size_t i = 0; i < (size_t) kLanes; ++i)
    {
        auto& l = p.lanes[i];
        l.on = L (r.lon[i]) > 0.5f; l.steps = I (r.steps[i]); l.hits = I (r.hits[i]); l.offset = I (r.offs[i]);
        l.div = I (r.div[i]); l.chance = L (r.chance[i]); l.burst = L (r.burst[i]);
        l.draw = L (r.draw[i]) > 0.5f; l.pattern = patterns[i].load (std::memory_order_relaxed);
        l.lock = L (r.lock[i]); l.vary = L (r.vary[i]); l.logic = I (r.logic[i]);
        for (size_t st = 0; st < (size_t) kMaxSteps; ++st)
        {
            l.cond[st] = conds[i][st].load (std::memory_order_relaxed);
            for (size_t w = 0; w < 3; ++w) l.plock[w][st] = plocks[i][w][st].load (std::memory_order_relaxed);
        }
        if (L (r.rprot[i]) > 0.5f) p.reelProtect |= 1 << i;
        l.droneAct = I (r.dact[i]);
    }
    for (size_t i = 0; i < (size_t) kVoices; ++i)
    {
        auto& v = p.voices[i];
        v.tune = L (r.tune[i]); v.decay = L (r.decay[i]); v.color = L (r.color[i]); v.level = L (r.lvl[i]); v.pan = L (r.pan[i]); v.send = L (r.send[i]);
        v.model = I (r.model[i]); v.harm = L (r.harm[i]); v.morph = L (r.morph[i]);
        v.xmod = I (r.xmod[i]); v.xsrc = I (r.xsrc[i]); v.xamt = L (r.xamt[i]); v.sus = L (r.sus[i]) > 0.5f;
    }
    p.scatSize = L (r.ssize); p.scatDensity = L (r.sdens); p.scatPitch = L (r.spitch); p.scatSpread = L (r.sspread); p.scatFeed = L (r.sfeed); p.scatMix = L (r.smix);
    p.scatModel = I (r.smodel); p.scatPosition = L (r.spos); p.scatTexture = L (r.stex); p.scatCloudsPitch = I (r.scpitch); p.scatReverb = L (r.sbverb);
    for (size_t s = 0; s < (size_t) kSources; ++s)
    {
        p.rate[s] = L (r.rate[s]); p.depth[s] = L (r.depth[s]);
        for (size_t d = 0; d < (size_t) kDests; ++d) p.pins[s][d] = I (r.pin[s][d]);
    }
    p.dlyStyle = I (r.dstyle); p.dlyMachine = I (r.dmach); p.dlyRate = L (r.drate);
    p.smrMix = L (r.mmix); p.smrFreq = L (r.mfreq); p.smrQ = L (r.mq); p.smrSpread = L (r.mspread); p.smrGlide = L (r.mglide);
    p.smrScale = I (r.mscale); p.smrRotateBy = I (r.mrot);
    p.dlyDiv = I (r.ddiv); p.dlyFeedback = L (r.dfb); p.dlyBreak = L (r.dbreak); p.dlyMix = L (r.dmix);
    p.verbModel = I (r.vmodel); p.verbSize = L (r.vsize); p.verbDecay = L (r.vdecay); p.verbDamp = L (r.vdamp); p.verbMix = L (r.vmix); p.programChange = I (r.pgm);
#ifdef METAMODULE
    p.verbModel = 0;   // SEMI port: no CloudSeed on the MetaModule (see Schwung.cpp)
#endif
    p.busComp = L (r.bcomp) > 0.5f; p.busChar = I (r.bchar); p.drive = L (r.drive); p.crunch = L (r.crunch); p.trans = L (r.trans);
    p.boom = L (r.boom); p.level = L (r.level);
    p.turns = L (r.turns); p.wall = L (r.wall) > 0.5f;
    for (size_t k = 0; k < 16; ++k) p.seqValues[k] = seqValues[k].load (std::memory_order_relaxed);
    p.seqLen = I (r.seqlen); p.seqX = I (r.seqx); p.seqY = I (r.seqy); p.seqGlide = L (r.seqglide);
    p.reelChaos = L (r.rchaos); p.reelLength = I (r.rlen); p.reelGene = I (r.rgene); p.reelSlide = L (r.rslide); p.reelVarispeed = L (r.rvs);
    p.reelMorph = L (r.rmorph); p.reelOrganize = L (r.rorg); p.reelOrgBy = I (r.rorgby); p.reelSplit = L (r.rsplit); p.reelDuck = L (r.rduck);
    p.reelDuckBy = I (r.rduckby); p.reelHold = L (r.rhold) > 0.5f; p.reelAnchor = L (r.ranchor) > 0.5f;
    p.fdbk = L (r.fdbk); p.fdbkTime = I (r.fdbkt);
    p.flipReset = flipReset.load (std::memory_order_relaxed);
    p.dLevel = L (r.dlevel); p.dPitch = I (r.dpitch); p.dRatio = I (r.dratio); p.dFine = L (r.dfine); p.dFmAB = L (r.dfmab); p.dFmBA = L (r.dfmba);
    p.dRing = L (r.dring); p.dSync = L (r.dsync); p.dFold = L (r.dfold); p.dShape = L (r.dshape); p.dBright = L (r.dbright); p.dChaos = L (r.dchaos);
    p.dSend = L (r.dsend); p.dDuck = L (r.dduck); p.dArc = L (r.darc); p.dPitchCv = I (r.dpcv); p.dFmCv = I (r.dfcv); p.dFoldCv = I (r.dfoldcv); p.dLpgCv = I (r.dlcv);
    p.resMix = L (r.rmix); p.resDecay = L (r.rdecay); p.resBright = L (r.rbright); p.arcLen = I (r.arclen); p.arcShape = L (r.arcshape);
    p.scatFreeze = L (r.sfreeze) > 0.5f; p.verbInfinite = L (r.vinf) > 0.5f;
    if (L (r.dprot) > 0.5f) p.reelProtect |= 256;
    return p;
}

//==============================================================================
void Case::newPatch()
{
    auto u = [this] { return nextFloat(); };
    auto between = [&] (float a, float b) { return a + (b - a) * u(); };
    auto pick = [&] (std::initializer_list<int> l) { return *(l.begin() + nextInt ((int) l.size())); };
    auto put = [&] (const std::string& id, float v) { if (! isLocked (id) && ! ids::neverRandom (id)) set (id, v); };

    struct Role { int stepsA, stepsB, hitsLo, hitsHi; float chLo, chHi, buLo, buHi; std::initializer_list<int> divs; };
    const Role roles[kLanes] {
        { 16, 16, 2, 5, 0.85f, 1.0f, 0.0f, 0.1f,  { 1, 1, 1, 2 } },
        { 16, 32, 1, 4, 0.8f, 1.0f, 0.0f, 0.25f,  { 1, 1, 2 } },
        { 12, 16, 3, 9, 0.4f, 0.9f, 0.0f, 0.4f,   { 1, 1, 2, 3 } },
        { 7, 16, 2, 7, 0.3f, 0.85f, 0.0f, 0.3f,   { 1, 2, 3, 5 } },
        { 16, 16, 5, 14, 0.6f, 1.0f, 0.15f, 0.6f, { 1, 1, 2 } },
        { 5, 16, 1, 5, 0.2f, 0.7f, 0.0f, 0.5f,    { 1, 2, 3, 7 } },
        { 16, 24, 1, 4, 0.5f, 0.9f, 0.0f, 0.2f,   { 2, 3, 4, 6 } },
        { 16, 16, 1, 3, 0.5f, 0.9f, 0.0f, 0.15f,  { 2, 4, 5, 8 } },
    };
    for (int i = 0; i < kLanes; ++i)
    {
        const auto& r = roles[i];
        const int steps = u() < 0.6f ? r.stepsA : pick ({ r.stepsA, r.stepsB, 12, 13, 24, 7, 32 });
        put (ids::lon (i), 1.0f);
        put (ids::steps (i), (float) steps);
        put (ids::hits (i), (float) std::min (steps, r.hitsLo + nextInt (r.hitsHi - r.hitsLo + 1)));
        put (ids::offs (i), i == mesh ? (float) pick ({ 4, 4, 2, 6 }) : (u() < 0.6f ? 0.0f : (float) nextInt (steps)));
        put (ids::div (i), (float) *(r.divs.begin() + nextInt ((int) r.divs.size())));
        put (ids::chance (i), between (r.chLo, r.chHi));
        put (ids::burst (i), between (r.buLo, r.buHi));
        static const std::initializer_list<const char*> lines[kLanes] {
            { "x.........x.....", "x.x.......x..x..", "x......x..x.....", "x..x..x...x..x..", "x.....x...x....." },
            { "....x.......x...", "....x..x....x...", "....x.......x..x", "....x.....x.x..." },
            { "..x...x...x.x...", "x..x..x..x..x..." }, { ".x..x...x.x....x", "...x...x...x..x." },
            { "x.x.x.x.x.x.x.x.", "xxx.xxx.xxx.xxx.", "..x...x...x...x.", "x.xxx.xxx.x.xxxx" },
            { ".......x.......x", "...x.......x.x.." }, { "x.......x.....x.", "x.....x.....x..." }, { "......x........x", "..x.......x....." },
        };
        if (! isLocked (ids::draw (i)))
        {
            const bool draw = (i <= mesh || i == lantern) ? u() < 0.4f : u() < 0.15f;
            if (draw)
            {
                const char* line = *(lines[i].begin() + nextInt ((int) lines[i].size()));
                uint32_t m = 0;
                int len = 0;
                for (; line[len] != 0 && len < 32; ++len) if (line[len] == 'x') m |= 1u << len;
                setPattern (i, m);
                put (ids::steps (i), (float) len);
            }
            set (ids::draw (i), draw ? 1.0f : 0.0f);
        }
    }
    put (ids::rotate, u() < 0.6f ? 0.0f : (float) nextInt (8));
    put (ids::swing, u() < 0.6f ? 0.0f : between (0.04f, 0.22f));
    const bool free = u() < 0.2f;
    put (ids::loose, free ? between (0.3f, 1.0f) : 0.0f);
    put (ids::ebb, free && u() < 0.7f ? between (0.3f, 0.9f) : 0.0f);
    put (ids::loop, (float) pick ({ 0, 1, 2, 2, 3 }));
    put (ids::seed, (float) nextInt (10000));
    put (ids::bmax, (float) pick ({ 3, 4, 5, 6, 8 }));
    put (ids::bshape, (float) pick ({ 0, 0, 1, 2 }));
    put (ids::scale, (float) nextInt (6));

    for (int i = 0; i < kVoices; ++i)
    {
        put (ids::tune (i), between (0.15f, 0.85f));
        put (ids::decay (i), between (0.1f, i == strike || i == opto ? 0.8f : 0.6f));
        put (ids::color (i), between (0.05f, 0.95f));
        put (ids::lvl (i), between (0.6f, 0.9f));
        put (ids::pan (i), i == kick || i == mesh ? 0.0f : between (-0.7f, 0.7f));
        put (ids::send (i), i == kick ? between (0.0f, 0.15f) : between (0.05f, 0.6f));
        put (ids::harm (i), between (0.1f, 0.9f));
        put (ids::morph (i), between (0.1f, 0.9f));
        static const std::initializer_list<int> fits[kVoices] {
            { 1 }, { 2 }, { 6, 4, 7, 11 }, { 4, 8, 10, 12 }, { 3, 14 }, { 15, 13, 11, 14 }, { 5, 4, 16, 19 },
        };
        static const std::initializer_list<int> braidsFits[kVoices] {
            { 23 }, { 25, 22 }, { 14, 15, 16, 17 }, { 7, 9, 11, 12 }, { 24, 28 }, { 29, 30, 5, 8 }, { 13, 17, 18, 21 },
        };
        const float roll = u();
        put (ids::model (i), roll < 0.25f ? (float) *(fits[i].begin() + nextInt ((int) fits[i].size()))
                         : (roll < 0.45f ? (float) (kFirstBraids + *(braidsFits[i].begin() + nextInt ((int) braidsFits[i].size()))) : 0.0f));
    }
    const float scatModel = u() < 0.5f ? 0.0f : (float) (1 + nextInt (10));
    put (ids::smodel, scatModel);
    put (ids::spos, between (0.1f, 0.9f)); put (ids::stex, between (0.1f, 0.9f));
    put (ids::scpitch, u() < 0.6f ? 0.0f : (float) pick ({ -12, -7, -5, 5, 7, 12 }));
    put (ids::ssize, between (0.1f, 0.8f)); put (ids::sdens, between (0.2f, 0.9f)); put (ids::spitch, between (0.0f, 0.8f));
    put (ids::sspread, between (0.3f, 1.0f)); put (ids::sfeed, between (0.0f, 0.4f)); put (ids::smix, between (0.25f, 0.7f));
    if ((int) get (ids::smodel) >= ids::kFirstBeads)
    {
        put (ids::sspread, between (0.0f, 0.5f));
        put (ids::sbverb, between (0.0f, 0.5f));
    }

    for (int s = 0; s < kSources; ++s)
    {
        put (ids::rate (s), between (0.1f, 0.9f));
        put (ids::depth (s), between (0.3f, 0.9f));
        for (int d = 0; d < kDests; ++d) put (ids::pin (s, d), 0.0f);
    }
    const int npins = 3 + nextInt (5);
    uint32_t used = 0;
    for (int k = 0; k < npins; ++k)
    {
        const int d = nextInt (kDests);
        if ((used >> d) & 1u || (d == rotate && u() < 0.6f)) continue;
        used |= 1u << d;
        const int s = d == kPitch && u() < 0.6f ? (int) forge : nextInt (kSources);
        put (ids::pin (s, d), u() < 0.75f ? 1.0f : -1.0f);
    }

    put (ids::ddiv, (float) pick ({ 1, 2, 3, 3, 4, 5 }));
    put (ids::dfb, between (0.2f, 0.7f)); put (ids::dbreak, between (0.0f, 0.6f)); put (ids::dmix, between (0.15f, 0.45f));
    put (ids::vsize, between (0.3f, 0.9f)); put (ids::vdecay, between (0.2f, 0.75f)); put (ids::vdamp, between (0.1f, 0.7f)); put (ids::vmix, between (0.1f, 0.35f));
    put (ids::pgm, (float) pick ({ 0, 0, 1, 2, 3, 4 }));
    put (ids::vmodel, u() < 0.5f ? 1.0f : 0.0f);
    put (ids::dstyle, (float) pick ({ 0, 0, 1, 2, 3 }));
    put (ids::dmach, (float) pick ({ 0, 0, 1, 2, 3 }));
    put (ids::drate, between (0.2f, 0.8f));
    if (u() < 0.15f) put (ids::ddiv, (float) pick ({ kDelayKarplus, kDelayKarplus + 1 }));
    put (ids::mmix, u() < 0.6f ? 0.0f : between (0.2f, 0.6f));
    put (ids::mfreq, between (0.2f, 0.6f)); put (ids::mq, between (0.4f, 0.85f)); put (ids::mspread, between (0.0f, 0.6f));
    put (ids::mglide, between (0.0f, 0.6f)); put (ids::mscale, (float) nextInt (6)); put (ids::mrot, (float) pick ({ 0, 2, 4, 5, 8 }));
    put (ids::bcomp, u() < 0.7f ? 1.0f : 0.0f);
    put (ids::bchar, (float) pick ({ 0, 1, 1, 2 }));
    put (ids::drive, between (0.0f, 0.3f));
    put (ids::crunch, between (0.0f, 0.15f));
    put (ids::trans, between (-0.2f, 0.4f));
    put (ids::boom, between (0.0f, 0.4f));
    put (ids::turns, u() < 0.4f ? 0.0f : between (0.3f, 0.9f));
    newPatchV4();

    std::snprintf (presetName, sizeof presetName, "new patch %d", (int) get (ids::seed));
    modified = false;
    factoryIndex = -1;
    touch();
}

void Case::newPatchV4()
{
    auto u = [this] { return nextFloat(); };
    auto between = [&] (float a, float b) { return a + (b - a) * u(); };
    auto pick = [&] (std::initializer_list<int> l) { return *(l.begin() + nextInt ((int) l.size())); };
    auto put = [&] (const std::string& id, float v) { if (! isLocked (id) && ! ids::neverRandom (id)) set (id, v); };

    for (int i = 0; i < kLanes; ++i)
    {
        const bool busy = i == fm || i == opto || i == lantern || i == zap;
        put (ids::lock (i), busy && u() < 0.35f ? between (0.55f, 0.95f) : 1.0f);
        put (ids::vary (i), u() < 0.5f ? 0.0f : between (0.05f, i == kick ? 0.2f : 0.5f));
        put (ids::logic (i), 0.0f);
        if (isLocked (ids::draw (i))) continue;
        clearStepData (i);
        if (busy && u() < 0.4f)
            for (int k = 0, n = 1 + nextInt (3); k < n; ++k)
                setCond (i, nextInt (16), pick ({ 1, 2, 6, 7, 12, 14 }));
        if ((i == fm || i == opto || i == strike || i == lantern) && u() < 0.35f)
        {
            const int which = i == lantern ? 1 : 0;
            for (int st = 0; st < 16; ++st)
                if (u() < 0.5f) setPlock (i, which, st, which == 0 ? (float) pick ({ -5, -3, 0, 2, 4, 7 }) / 24.0f : between (-0.8f, 0.8f));
        }
    }
    if (u() < 0.4f)
    {
        const int which = nextInt (3);
        if (which == 0) put (ids::logic (lantern), (float) (1 + logicNot * 8 + kick));
        if (which == 1) put (ids::logic (zap), (float) (1 + logicXor * 8 + mesh));
        if (which == 2) put (ids::logic (opto), (float) (1 + logicAnd * 8 + lantern));
    }
    resetMutations();

    for (int k = 0; k < 16; ++k) setSeqValue (k, u() < 0.3f ? 0.0f : between (-1.0f, 1.0f));
    put (ids::seqlen, (float) pick ({ 3, 4, 5, 7, 8, 16 }));
    put (ids::seqx, (float) pick ({ 0, 1, 2, 5, 3 }));
    put (ids::seqy, u() < 0.25f ? (float) pick ({ 2, 5, 7 }) : 0.0f);
    put (ids::seqglide, u() < 0.6f ? 0.0f : between (0.1f, 0.6f));

    for (int i = 0; i < kVoices; ++i) put (ids::xmod (i), 0.0f);
    if (u() < 0.3f)
    {
        static const int pairs[][3] { { fm, 1, kick }, { opto, 2, fm }, { lantern, 4, kick }, { zap, 1, opto }, { strike, 3, mesh }, { opto, 1, 7 } };
        const auto& pr = pairs[nextInt (6)];
        put (ids::xmod (pr[0]), (float) pr[1]);
        put (ids::xsrc (pr[0]), (float) pr[2]);
        put (ids::xamt (pr[0]), between (0.3f, 0.8f));
    }
    put (ids::fdbk, u() < 0.15f ? between (0.2f, 0.6f) : 0.0f);
    put (ids::fdbkt, (float) pick ({ 0, 1, 1, 2, 3 }));

    const bool reel = u() < 0.4f;
    put (ids::rchaos, reel ? between (0.15f, 0.6f) : 0.0f);
    put (ids::rlen, (float) pick ({ 0, 1, 1, 2 }));
    put (ids::rgene, (float) pick ({ 1, 1, 2, 2, 3, 0 }));
    put (ids::rslide, u() < 0.6f ? 0.0f : between (0.0f, 1.0f));
    put (ids::rvs, u() < 0.7f ? 0.75f : (float) pick ({ 25, 87, 100, 62 }) / 100.0f);
    put (ids::rmorph, between (0.3f, 0.8f));
    put (ids::rorg, between (0.0f, 1.0f));
    put (ids::rorgby, u() < 0.3f ? (float) pick ({ 2, 7, 8 }) : 0.0f);
    put (ids::rsplit, u() < 0.4f ? between (0.25f, 0.55f) : 0.0f);
    put (ids::rduck, between (0.3f, 0.8f));
    put (ids::rduckby, 1.0f);
    put (ids::ranchor, u() < 0.8f ? 1.0f : 0.0f);
    for (int i = 0; i < kLanes; ++i) put (ids::rprot (i), i == kick || (i == mesh && u() < 0.3f) ? 1.0f : 0.0f);
    if (u() < 0.12f)
    {
        const int slot = pick ({ opto, zap, fm });
        put (ids::model (slot), (float) kReelModel);
        put (ids::harm (slot), between (0.2f, 0.6f));
    }

    for (int i = 0; i < kLanes; ++i) put (ids::dact (i), 0.0f);
    const bool drone = u() < 0.45f;
    put (ids::dlevel, drone ? between (0.3f, 0.6f) : 0.0f);
    put (ids::dpitch, (float) pick ({ -24, -12, -12, -5, 0, 7 }));
    put (ids::dratio, (float) pick ({ 1, 3, 4, 5, 6, 6, 7, 8, 11, 12 }));
    put (ids::dfine, u() < 0.5f ? 0.5f : between (0.35f, 0.65f));
    put (ids::dfmab, between (0.0f, 0.6f)); put (ids::dfmba, u() < 0.5f ? 0.0f : between (0.0f, 0.5f));
    put (ids::dring, u() < 0.7f ? 0.0f : between (0.2f, 0.7f)); put (ids::dsync, u() < 0.75f ? 0.0f : between (0.3f, 1.0f));
    put (ids::dfold, between (0.0f, 0.5f)); put (ids::dshape, between (0.0f, 0.7f));
    put (ids::dbright, between (0.3f, 0.8f)); put (ids::dchaos, u() < 0.7f ? between (0.0f, 0.3f) : between (0.4f, 0.9f));
    put (ids::dsend, between (0.1f, 0.5f)); put (ids::dduck, between (0.4f, 0.9f));
    const bool arcOn = u() < 0.5f;
    put (ids::darc, arcOn ? between (0.4f, 1.0f) : 0.0f);
    put (ids::arclen, (float) pick ({ 0, 1, 2, 2, 3 })); put (ids::arcshape, between (0.5f, 0.95f));
    put (ids::dfcv, u() < 0.3f ? (float) (1 + pick ({ smooth, woggle, arc })) : 0.0f);
    put (ids::dfoldcv, u() < 0.3f ? (float) (1 + pick ({ pong, lfo, forge })) : 0.0f);
    put (ids::dlcv, u() < 0.3f ? (float) (1 + pick ({ lfo, smooth, woggle })) : 0.0f);
    if (drone)
    {
        put (ids::dact (kick), (float) actDuck);
        put (ids::dact (pick ({ mesh, fm, zap, opto })), (float) pick ({ actStep, actRand, actLpg, actFold, actPing }));
    }
    put (ids::rmix, u() < 0.25f ? between (0.2f, 0.55f) : 0.0f);
    put (ids::rdecay, between (0.3f, 0.8f)); put (ids::rbright, between (0.3f, 0.8f));
    for (int i = 0; i < kVoices; ++i) put (ids::sus (i), 0.0f);
    if (u() < 0.15f) put (ids::sus (pick ({ opto, strike, fm })), 1.0f);
    put (ids::sfreeze, u() < 0.06f ? 1.0f : 0.0f);
    put (ids::vinf, 0.0f);
}

void Case::mutate (float amount)
{
    const auto& d = defs();
    for (int i = 0; i < (int) d.size(); ++i)
    {
        const auto& def = d[(size_t) i];
        const auto& id = def.id;
        if (isLocked (i) || ids::neverRandom (id) || ids::isModelChoice (id) || id == ids::loop || id.rfind ("lon", 0) == 0 || id == ids::bcomp) continue;
        if (id.rfind ("pin", 0) == 0)
        {
            if (nextFloat() < amount * 0.04f) set (i, (float) (nextInt (3) - 1));
            continue;
        }
        if (def.stepped())
        {
            if (nextFloat() < amount * 0.25f) set (i, get (i) + (nextFloat() < 0.5f ? 1.0f : -1.0f));
        }
        else if (nextFloat() < 0.5f)
        {
            // the plug-in drifts the normalised value by up to amount * 0.3
            const float span = def.max - def.min;
            const float n01 = (get (i) - def.min) / span;
            const float m = std::max (0.0f, std::min (1.0f, n01 + (nextFloat() * 2.0f - 1.0f) * amount * 0.3f));
            set (i, def.min + m * span);
        }
    }
    modified = true;
    touch();
}

//==============================================================================
void Case::writeStepData (std::map<std::string, float>& m) const
{
    for (int i = 0; i < kLanes; ++i)
        for (int st = 0; st < kMaxSteps; ++st)
        {
            if (const int c = cond (i, st); c != 0) m[ids::cond (i, st)] = (float) c;
            for (int w = 0; w < 3; ++w)
                if (const float v = plock (i, w, st); std::abs (v) > 1.0e-4f) m[ids::plock (i, w, st)] = v;
        }
    for (int k = 0; k < 16; ++k) m[ids::seqv (k)] = seqValue (k);
}

void Case::readStepData (const std::map<std::string, float>& m)
{
    auto value = [&m] (const std::string& id) { auto it = m.find (id); return it != m.end() ? it->second : 0.0f; };
    for (int i = 0; i < kLanes; ++i)
        for (int st = 0; st < kMaxSteps; ++st)
        {
            setCond (i, st, (int) std::lround (value (ids::cond (i, st))));
            for (int w = 0; w < 3; ++w) setPlock (i, w, st, value (ids::plock (i, w, st)));
        }
    for (int k = 0; k < 16; ++k) setSeqValue (k, value (ids::seqv (k)));
    resetMutations();
}

void Case::clearStepData (int lane)
{
    for (int st = 0; st < kMaxSteps; ++st)
    {
        setCond (lane, st, 0);
        for (int w = 0; w < 3; ++w) setPlock (lane, w, st, 0.0f);
    }
}

void Case::applyValues (const std::map<std::string, float>& v)
{
    readStepData (v);
    for (int i = 0; i < kLanes; ++i)
    {
        auto get16 = [&] (const std::string& id) { auto it = v.find (id); return it != v.end() ? (uint32_t) std::max (0, std::min (65535, (int) it->second)) : 0u; };
        setPattern (i, get16 (ids::patLo (i)) | (get16 (ids::patHi (i)) << 16));
    }
    const auto& d = defs();
    for (int i = 0; i < (int) d.size(); ++i)
    {
        if (d[(size_t) i].id == ids::run || d[(size_t) i].id == ids::bpm) continue;
        auto it = v.find (d[(size_t) i].id);
        set (i, it != v.end() ? it->second : d[(size_t) i].def);
    }
    touch();
}

std::map<std::string, float> Case::snapshotValues() const
{
    std::map<std::string, float> m;
    const auto& d = defs();
    for (int i = 0; i < (int) d.size(); ++i) m[d[(size_t) i].id] = get (i);
    for (int i = 0; i < kLanes; ++i)
    {
        const auto pat = pattern (i);
        m[ids::patLo (i)] = (float) (pat & 0xffffu);
        m[ids::patHi (i)] = (float) (pat >> 16);
    }
    writeStepData (m);
    return m;
}

void Case::loadFactory (int index)
{
    const auto& f = factoryPresets();
    index = std::max (0, std::min ((int) f.size() - 1, index));
    applyValues (f[(size_t) index].values);
    setName (f[(size_t) index].name.c_str());
    modified = false;
    factoryIndex = index;
}

void Case::setName (const char* n)
{
    std::snprintf (presetName, sizeof presetName, "%s", n);
}

void Case::setLaneDraw (int lane, bool draw)
{
    lane = std::max (0, std::min (kLanes - 1, lane));
    if (draw && get (ids::draw (lane)) < 0.5f)
        setPattern (lane, euclidMask ((int) get (ids::steps (lane)), (int) get (ids::hits (lane)), (int) get (ids::offs (lane))));
    set (ids::draw (lane), draw ? 1.0f : 0.0f);
}

void Case::toggleStep (int lane, int step)
{
    if (step < 0 || step >= 32) return;
    setLaneDraw (lane, true);
    setPattern (lane, pattern (lane) ^ (1u << step));
}

} // namespace flt
