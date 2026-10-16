// NMS - shared pieces of the module implementations: the spec builder, the
// rhythm engine of the drum generators (phrase / meter / stress / ornament),
// the melodic engine of the synths and the clock + trigger plumbing every
// generator shares.
#pragma once
#include "Module.h"
#include <cstdio>

namespace nms
{
std::string fmt (float v, int dec);
std::string hzText (float f);
std::string secsText (float s);

struct B
{
    TypeSpec t;
    B (const char* id, const char* title, const char* blurb, Cat cat, uint32_t colour, int units)
    { t.id = id; t.title = title; t.blurb = blurb; t.cat = cat; t.colour = colour; t.units = units; }
    int add (ParamSpec p) { const int i = (int) t.params.size(); t.index[p.key] = i; t.params.push_back (std::move (p)); return i; }
    ParamSpec& last() { return t.params.back(); }
    int add (ParamSpec p, bool) { return add (std::move (p)); }
    int knob (const std::string& k, const std::string& nm, float lo, float hi, float def, int col, int row, Map m = Map::Lin, const char* unit = "", const char* tip = "")
    { ParamSpec p; p.key = k; p.name = nm; p.min = lo; p.max = hi; p.def = def; p.col = col; p.row = row; p.map = m; p.unit = unit; p.tip = tip; p.bipolar = lo < 0 && hi > 0; return add (p); }
    int sel (const std::string& k, const std::string& nm, std::vector<std::string> ch, int def, int col, int row, const char* tip = "")
    { ParamSpec p; p.key = k; p.name = nm; p.choices = std::move (ch); p.min = 0; p.max = (float) p.choices.size() - 1; p.def = (float) def; p.col = col; p.row = row; p.tip = tip; return add (p); }
    int big (const std::string& k, const std::string& nm, float lo, float hi, float def, int col, int row, Map m = Map::Lin, const char* unit = "", const char* tip = "")
    { const int i = knob (k, nm, lo, hi, def, col, row, m, unit, tip); last().kind = Kind::Big; last().w = last().h = 3; return i; }
    int mini (const std::string& k, const std::string& nm, float lo, float hi, float def, int col, int row, Map m = Map::Lin, const char* unit = "", const char* tip = "")
    { const int i = knob (k, nm, lo, hi, def, col, row, m, unit, tip); last().kind = Kind::Mini; last().w = last().h = 1; return i; }
    int tog (const std::string& k, const std::string& nm, bool def, int col, int row, int w = 2, const char* tip = "")
    { ParamSpec p; p.key = k; p.name = nm; p.kind = Kind::Toggle; p.map = Map::Int; p.min = 0; p.max = 1; p.def = def ? 1.0f : 0.0f; p.col = col; p.row = row; p.w = w; p.h = 1; p.tip = tip; return add (p); }
    int choice (const std::string& k, const std::string& nm, std::vector<std::string> ch, int def, int col, int row, int w = 2, const char* tip = "")
    { ParamSpec p; p.key = k; p.name = nm; p.kind = Kind::Choice; p.choices = std::move (ch); p.min = 0; p.max = (float) p.choices.size() - 1; p.def = (float) def; p.col = col; p.row = row; p.w = w; p.h = 1; p.tip = tip; return add (p); }
    int mom (const std::string& k, const std::string& nm, int col, int row, int w = 2, const char* tip = "")
    { ParamSpec p; p.key = k; p.name = nm; p.kind = Kind::Momentary; p.map = Map::Int; p.rnd = false; p.ptn = false; p.col = col; p.row = row; p.w = w; p.h = 1; p.tip = tip; return add (p); }
    void macro (int m, float amt) { last().macro = m; last().macroAmt = amt; }
    void sound() { last().sound = true; }
    void fixed() { last().rnd = false; last().ptn = false; }
    void channel (int def)
    {
        ParamSpec p; p.key = "ch"; p.name = "CHANNEL"; p.kind = Kind::Hidden; p.choices = { "1", "2", "3", "4", "5", "6", "7", "8", "RET A", "RET B" };
        p.min = 0; p.max = 9; p.def = (float) def; p.rnd = false; p.ptn = false; p.w = p.h = 0; add (p);
        t.chained = true;
    }
    // the organic row (row 8): 0 = generator (LINK, LISTEN, AMT, AWARE, DEPTH, BREATH),
    // 1 = FX with a trigger input (LISTEN, AWARE, DEPTH, BREATH), 2 = FX without (AWARE, DEPTH, BREATH)
    void organic (int kind)
    {
        int c = 0;
        auto mini2 = [this, &c] (const char* k, const char* nm, float def, const char* tip)
        { knob (k, nm, 0, 1, def, c, 8, Map::Lin, "", tip); last().kind = Kind::Mini; last().w = 2; last().h = 1; fixed(); c += 2; };
        if (kind == 0)
        {
            choice ("link", "LINK", { "OFF", "AVOID", "FOLLOW" }, 0, c, 8, 2, "AVOID: rest when the listened channel hits. FOLLOW: also play when it hits"); fixed(); c += 2;
            choice ("lch", "LISTEN", { "CH 1", "CH 2", "CH 3", "CH 4", "CH 5", "CH 6", "CH 7", "CH 8" }, 0, c, 8, 2, "The channel LINK listens to"); fixed(); c += 2;
            mini2 ("lamt", "AMOUNT", 0.8f, "How often LINK acts");
        }
        else if (kind == 1)
        {
            choice ("lch", "LISTEN", { "CHAIN", "CH 1", "CH 2", "CH 3", "CH 4", "CH 5", "CH 6", "CH 7", "CH 8" }, 0, c, 8, 2, "Where the triggers come from: the chain, or another channel's instrument"); fixed(); c += 2;
        }
        mini2 ("aware", "AWARE", 0.0f, "How much the ENERGY of the whole instrument moves this module (along its macro assignments)");
        mini2 ("wdepth", "DEPTH", 1.0f, "How far WANDER may move this module (0 = it stays)");
        mini2 ("breath", "BREATH", 0.2f, "WANDER transitions swell through a breath instead of a straight line");
    }
    void in (const std::string& k, const std::string& nm, PortKind kd, Norm nr = Norm::None, const char* tip = "") { t.ins.push_back ({ k, nm, kd, nr, tip }); }
    void out (const std::string& k, const std::string& nm, PortKind kd, const char* tip = "") { t.outs.push_back ({ k, nm, kd, Norm::None, tip }); }
    void custom (int id, int col, int row, int w, int h) { t.custom.push_back ({ id, col, row, w, h }); }
};

// the standard macro numbers of the NMS framework
enum Macro { mPitch, mRange, mMicroI, mMicroC, mMesoI, mMesoC, mMacroI, mMacroC,
             mModulation, mComplex, mAttack, mDecay, mAccent, mSpeed, mIntensity, mCharShape,
             mTone, mResonance, mCharacter, mNoise, mHarmonics, mInstability, mThickness, mSpace };

inline float pulseSamples (float sr) { return 0.005f * sr; }

// metrical weight of a 16th inside a bar, for a grouping of `meter` 16ths
inline float metricLevel (int inBar, int meter)
{
    const int g = inBar % std::max (1, meter);
    if (inBar == 0) return 1.0f;
    if (g == 0) return 0.8f;
    if (meter % 2 == 0 && g == meter / 2) return 0.4f;
    return (inBar % 2 == 0) ? 0.35f : 0.2f;
}

struct Hit { float frac, vel; };

// --------------------------------------------------------------------------
// Drum rhythm engine. Pure function of the parameters and the step.
struct RhythmParams
{
    uint32_t seed = 1; int bars = 1, meter = 4, offset = 0, ocyc = 8;
    float base = 0.5f, pmask = 0, samt = 0.5f, oint = 0, olvl = 0.5f, omod = 0;
};
int rhythmHits (const RhythmParams&, int64_t step, Hit* out);   // returns count (<= 6)

// Melodic engine of the synths / sampler / MIDI generator
struct MelodyParams
{
    uint32_t seed = 1, modSeed = 2; int tcyc = 8, bars = 1, meter = 4;
    float range = 0.3f, intensity = 0.5f, mask = 0;
};
struct Note { bool on = false; int degree = 0; float vel = 0.8f, mod = 0, len = 1; };
Note melodyAt (const MelodyParams&, int64_t step);

// --------------------------------------------------------------------------
// The clock + trigger plumbing shared by every generator.
// Inputs 0..4: CLK, TRIG, PITCH, VARY, ACC.  Outputs 0..5: L, R, TRIG, PITCH, VEL, MOD.
void addGeneratorPorts (B&);
class Generator : public Module
{
public:
    void block (const Ctx& cx) override;
    void tick (const float* in, float* out) override;
    explicit Generator (const TypeSpec& t) : Module (t) { iLink = t.param ("link"); iLch = t.param ("lch"); iLamt = t.param ("lamt"); }
    void reset() override { lastStep = INT64_MIN; extStep = -1; nPending = 0; }
protected:
    // LINK: thin out (AVOID) or add (FOLLOW) hits against the listened channel; returns the new count
    int link (int64_t s, Hit* h, int n, uint32_t seed);
    int iLink = -1, iLch = -1, iLamt = -1;
    // called on every step of the module's own grid; push hits with addHit
    virtual void step (int64_t s) = 0;
    // a hit has to sound now; `s` is the pattern step it belongs to
    virtual void voice (float vel, int64_t s, bool external) = 0;
    // stereo audio for one sample
    virtual void render (float& l, float& r) = 0;
    virtual double rate() const { return 1.0; }     // steps per 16th
    void addHit (float frac, float vel, int64_t s);
    void outValues (float pitchOct, float vel, float mod) { oPitch = pitchOct; oVel = vel; oMod = mod; }
    float vary = 0.0f, acc = -1.0f, pitchIn = 0.0f;  // live CV inputs
    int k = 0;
    double stepSamples = 0;
private:
    struct Pending { int left; float vel; int64_t s; };
    Pending pending[32];
    int nPending = 0;
    int64_t lastStep = INT64_MIN, extStep = -1;
    int lastRestart = 0;
    Edge clk, trg;
    Pulse trigOut;
    float oPitch = 0, oVel = 0, oMod = 0;
    void fire (float vel, int64_t s, bool ext);
};
} // namespace nms
