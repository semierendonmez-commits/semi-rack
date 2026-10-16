// NMS - the drum generators: DUFFY (kick), AARON (snare), HANDMAID (clap),
// HATTERSLEY (hi-hat), SKIN (percussion). Each one is a rhythm generator
// (phrase, meter, stress, ornaments, modulation cycle) and a sound.
// Control names and layout follow the NMS panels; the DSP is a rebuild.
#include "Common.h"
#include "Modules.h"

namespace nms
{
namespace
{
const std::vector<std::string> kBars { "1 BAR", "2 BAR", "4 BAR", "8 BAR" };
const std::vector<std::string> kMeter { "2", "3", "4", "5", "6", "7", "8" };
const std::vector<std::string> kMult { "1/4", "1/2", "x1", "x2", "x3", "x4" };
const std::vector<std::string> kOrn { "2", "3", "4", "6", "8", "12", "16", "32" };
const std::vector<std::string> kModCyc { "2", "3", "4", "6", "8", "16", "32", "64" };
const int kOrnV[] = { 2, 3, 4, 6, 8, 12, 16, 32 };
const int kModV[] = { 2, 3, 4, 6, 8, 16, 32, 64 };
const double kMultV[] = { 0.25, 0.5, 1.0, 2.0, 3.0, 4.0 };

// the 14 pattern controls of every drum generator, rows 0-3 and the first two of row 6
void drumPattern (B& b, int ch, float base, int meter, int bars)
{
    b.channel (ch);
    b.sel ("plen", "PHR LEN", kBars, bars, 0, 0, "Phrase length"); b.macro (mMacroC, 0.4f);
    b.sel ("meter", "METER", kMeter, meter, 2, 0, "Grouping of the 16ths: where the stresses fall"); b.macro (mMesoC, 0.35f);
    b.knob ("pmask", "PHR MASK", 0, 1, 0.2f, 4, 0, Map::Lin, "", "Phrase mask: later bars thin out, the last bar fills"); b.macro (mMacroI, 0.5f);
    b.knob ("base", "BASE", 0, 1, base, 6, 0, Map::Lin, "", "Base density of the pattern"); b.macro (mMesoI, 0.5f);
    b.sel ("mult", "MULT", kMult, 2, 8, 0, "Step rate against the 16th grid");
    b.knob ("offset", "OFFSET", 0, 15, 0, 10, 0, Map::Int, "", "Rotates the pattern");
    b.knob ("sptn", "STR PTN", 0, 1, 0.37f, 0, 2, Map::Lin, "", "Stress pattern: the seeded shape of the bar"); b.macro (mMesoC, 0.5f);
    b.knob ("samt", "STR AMT", 0, 1, 0.5f, 2, 2, Map::Lin, "", "Stress amount: accent against weak steps"); b.macro (mAccent, 0.5f);
    b.sel ("ocyc", "ORN CYCL", kOrn, 4, 4, 2, "Ornament cycle"); b.macro (mMicroC, 0.4f);
    b.knob ("oint", "ORN INT", 0, 1, 0.2f, 6, 2, Map::Lin, "", "Ornament intensity: how often ornaments play"); b.macro (mMicroI, 0.5f);
    b.knob ("olvl", "ORN LVL", 0, 1, 0.5f, 8, 2, Map::Lin, "", "Ornament level"); b.macro (mIntensity, 0.4f);
    b.knob ("omod", "ORN MOD", 0, 1, 0.4f, 10, 2, Map::Lin, "", "Ornament type: ghost, flam, ratchet 2, ratchet 3-4"); b.macro (mCharShape, 0.5f);
    b.sel ("mcyc", "MOD CYC", kModCyc, 5, 0, 6, "Modulation cycle length in steps"); b.macro (mModulation, 0.3f);
    b.knob ("mofs", "MOD OFS", 0, 1, 0.2f, 2, 6, Map::Lin, "", "Modulation amount: how far the cycle moves the sound"); b.macro (mModulation, 0.5f);
    b.custom (0, 4, 6, 8, 2);
    b.organic (0);
    addGeneratorPorts (b);
}

class Drum : public Generator
{
public:
    explicit Drum (const TypeSpec& t) : Generator (t)
    {
        const auto& s = t;
        iPlen = s.param ("plen"); iMeter = s.param ("meter"); iPmask = s.param ("pmask"); iBase = s.param ("base"); iMult = s.param ("mult");
        iOff = s.param ("offset"); iSptn = s.param ("sptn"); iSamt = s.param ("samt"); iOcyc = s.param ("ocyc"); iOint = s.param ("oint");
        iOlvl = s.param ("olvl"); iOmod = s.param ("omod"); iMcyc = s.param ("mcyc"); iMofs = s.param ("mofs");
        salt = (uint32_t) (t.id[0] * 131 + t.id[1]);
    }
    RhythmParams rhythm (bool live) const
    {
        auto v = [this, live] (int i) { return live ? spec.toPlain (i, effNorm (i)) : getPlain (i); };
        RhythmParams r;
        r.seed = seedOf (live ? effNorm (iSptn) : getNorm (iSptn), salt);
        r.bars = 1 << (int) v (iPlen); r.meter = 2 + (int) v (iMeter); r.offset = (int) v (iOff); r.ocyc = kOrnV[(int) v (iOcyc)];
        r.base = v (iBase); r.pmask = v (iPmask); r.samt = v (iSamt); r.oint = v (iOint); r.olvl = v (iOlvl); r.omod = v (iOmod);
        return r;
    }
    int previewSteps() const override { return 16 << (int) getPlain (iPlen); }
    float hitAt (int64_t s16) const override
    {
        const double r = kMultV[(int) spec.toPlain (iMult, effNorm (iMult))];
        const double sd = (double) s16 * r;
        if (std::abs (sd - std::round (sd)) > 1e-6) return 0.0f;
        Hit h[8]; const int n = rhythmHits (rhythm (true), (int64_t) std::llround (sd), h);
        float v = 0; for (int i = 0; i < n; ++i) if (h[i].frac < 0.01f) v = std::max (v, h[i].vel);
        return v;
    }
    float preview (int64_t s, int&) const override
    {
        Hit h[8]; const int n = rhythmHits (rhythm (false), s, h);
        float v = 0; for (int i = 0; i < n; ++i) v = std::max (v, h[i].vel);
        return v;
    }
protected:
    double rate() const override { return kMultV[(int) c (iMult)]; }
    void block (const Ctx& cx) override { Generator::block (cx); rp = rhythm (true); }
    void step (int64_t s) override
    {
        if (s < 0) return;
        Hit h[8]; int n = rhythmHits (rp, s, h);
        n = link (s, h, n, rp.seed);
        for (int i = 0; i < n; ++i) addHit (h[i].frac, h[i].vel, s);
        const int64_t ps = (int64_t) rp.bars * 16;
        disp[0].store ((float) ((((s + rp.offset) % ps) + ps) % ps - rp.offset), std::memory_order_relaxed);
        disp[1].store ((float) ((s % ps + ps) % ps), std::memory_order_relaxed);
    }
    // modulation cycle value -1..1 (triangle) scaled by MOD OFS, plus the VARY input
    float modAt (int64_t s) const
    {
        const int mc = kModV[(int) c (iMcyc)];
        const float t = (float) (((s % mc) + mc) % mc) / (float) mc;
        return clampf ((1.0f - 4.0f * std::abs (t - 0.5f)) * c (iMofs) + vary, -1.5f, 1.5f);
    }
    void voice (float vel, int64_t s, bool) override
    {
        const float m = modAt (s);
        outValues (pitchIn, vel, 0.5f + 0.5f * clampf (m, -1, 1));
        hit (vel, m);
        disp[2].store (vel, std::memory_order_relaxed);
    }
    virtual void hit (float vel, float mod) = 0;
    RhythmParams rp;
    Rng noise { 0x1234567u };
    uint32_t salt = 1;
    int iPlen, iMeter, iPmask, iBase, iMult, iOff, iSptn, iSamt, iOcyc, iOint, iOlvl, iOmod, iMcyc, iMofs;
};

// =============================================================================
TypeSpec specDuffy()
{
    B b ("DUFFY", "KICK GENERATOR", "Kick pattern and sound generator: four-to-the-floor to broken and unpredictable", Cat::Drum, 0xff4f6f9c, 6);
    drumPattern (b, 0, 0.42f, 2, 0);
    b.t.params[(size_t) b.t.param ("sptn")].def = 0.05f;
    b.knob ("pitch", "PITCH", 30, 110, 48, 0, 4, Map::Exp, "Hz"); b.sound(); b.macro (mPitch, 0.3f);
    b.knob ("len", "LENGTH", 0.06f, 1.6f, 0.38f, 2, 4, Map::Exp, "s"); b.sound(); b.macro (mDecay, 0.5f);
    b.knob ("snap", "SNAP", 0, 1, 0.5f, 4, 4); b.sound(); b.macro (mAttack, 0.4f);
    b.knob ("thud", "THUD", 0, 1, 0.35f, 6, 4); b.sound(); b.macro (mThickness, 0.4f);
    b.knob ("boom", "BOOM", 0, 1, 0.25f, 8, 4); b.sound(); b.macro (mSpace, 0.3f);
    b.knob ("sat", "SATURATION", 0, 1, 0.3f, 10, 4); b.sound(); b.macro (mCharacter, 0.5f);
    return b.t;
}
class Duffy : public Drum
{
public:
    Duffy() : Drum (spec()) {}
    static const TypeSpec& spec() { static TypeSpec t = specDuffy(); return t; }
    void hit (float vel, float m) override
    {
        f0 = c (P ("pitch")) * std::exp2 (m * 0.3f + pitchIn);
        amp.trigger (0.0005f, c (P ("len")) * (0.6f + 0.4f * vel), sr);
        pEnv.trigger (0.0f, 0.012f + 0.04f * (1.0f - c (P ("snap"))), sr);
        click.trigger (0.0f, 0.004f, sr);
        thudE.trigger (0.0005f, 0.035f, sr);
        boomE.trigger (0.004f, c (P ("len")) * 2.2f, sr);
        g = vel; ph = 0; ph2 = 0;
    }
    void render (float& l, float& r) override
    {
        const float a = amp.tick();
        if (a <= 0 && boomE.v <= 0) { l = r = 0; return; }
        const float pe = pEnv.tick(), sweep = 1.0f + c (P ("snap")) * 7.0f * pe;
        ph += f0 * sweep / sr; ph -= std::floor (ph);
        ph2 += f0 * 0.5f * (1.0f + pe) / sr; ph2 -= std::floor (ph2);
        float y = std::sin (kTwoPi * ph) * a;
        y += noise.bi() * click.tick() * c (P ("snap")) * 0.5f;
        const float te = thudE.tick();
        y += thudLp.lp (noise.bi() * 3.0f + std::sin (kTwoPi * ph * 2.0f)) * te * c (P ("thud")) * 0.8f;
        y += std::sin (kTwoPi * ph2) * boomE.tick() * c (P ("boom")) * 0.55f;
        const float d = 1.0f + c (P ("sat")) * 6.0f;
        y = fastTanh (y * d) / fastTanh (d) * g * 0.9f;
        l = r = y;
    }
    void prepare (double s) override { Drum::prepare (s); thudLp.set (380.0, s); }
private:
    int P (const char* k) const { return spec().param (k); }
    AD amp, pEnv, click, thudE, boomE;
    OnePole thudLp;
    float f0 = 50, g = 0, ph = 0, ph2 = 0;
};

// =============================================================================
TypeSpec specAaron()
{
    B b ("AARON", "SNARE GENERATOR", "Snare pattern and sound generator", Cat::Drum, 0xffe8743c, 6);
    drumPattern (b, 1, 0.28f, 2, 1);
    b.t.params[(size_t) b.t.param ("offset")].def = 4;
    b.knob ("snap", "SNAP", 0, 1, 0.55f, 0, 4); b.sound(); b.macro (mAttack, 0.4f);
    b.knob ("len", "LENGTH", 0.05f, 1.2f, 0.22f, 2, 4, Map::Exp, "s"); b.sound(); b.macro (mDecay, 0.5f);
    b.knob ("crisp", "CRISP", 0, 1, 0.5f, 4, 4); b.sound(); b.macro (mTone, 0.5f);
    b.knob ("pinch", "PINCH", 0, 1, 0.35f, 6, 4); b.sound(); b.macro (mCharacter, 0.4f);
    b.knob ("whack", "WHACK", 0, 1, 0.5f, 8, 4); b.sound(); b.macro (mThickness, 0.4f);
    b.knob ("ring", "RING", 0, 1, 0.3f, 10, 4); b.sound(); b.macro (mResonance, 0.5f);
    return b.t;
}
class Aaron : public Drum
{
public:
    Aaron() : Drum (spec()) {}
    static const TypeSpec& spec() { static TypeSpec t = specAaron(); return t; }
    void hit (float vel, float m) override
    {
        tune = std::exp2 (m * 0.25f + pitchIn);
        body.trigger (0.0005f, 0.04f + c (P ("ring")) * 0.45f, sr);
        nz.trigger (0.0008f, c (P ("len")) * (0.7f + 0.3f * vel), sr);
        tr.trigger (0.0f, 0.006f, sr);
        pe.trigger (0.0f, 0.03f, sr);
        g = vel; ph1 = ph2 = 0;
        hp.set (700.0f + c (P ("crisp")) * 5000.0f, 0.7f, sr);
        bp.set (2500.0f + c (P ("crisp")) * 3500.0f, 0.9f, sr);
    }
    void render (float& l, float& r) override
    {
        const float be = body.tick(), ne = nz.tick();
        if (be <= 0 && ne <= 0) { l = r = 0; return; }
        const float p = 1.0f + c (P ("pinch")) * 0.9f * pe.tick();
        ph1 += 185.0f * tune * p / sr; ph1 -= std::floor (ph1);
        ph2 += 331.0f * tune * p / sr; ph2 -= std::floor (ph2);
        const float wh = c (P ("whack"));
        float bodyS = (std::sin (kTwoPi * ph1) + 0.6f * std::sin (kTwoPi * ph2)) * be * (0.35f + 0.65f * wh);
        bodyS = fastTanh (bodyS * (1.0f + wh * 3.0f));
        const float w = noise.bi();
        float n = hp.hp (w) * 0.7f + bp.bp (w) * 0.6f;
        n *= ne + tr.tick() * c (P ("snap")) * 2.0f;
        const float y = (bodyS * 0.6f + n * 0.75f) * g;
        l = r = fastTanh (y);
    }
private:
    int P (const char* k) const { return spec().param (k); }
    AD body, nz, tr, pe;
    Svf hp, bp;
    float tune = 1, g = 0, ph1 = 0, ph2 = 0;
};

// =============================================================================
TypeSpec specHandmaid()
{
    B b ("HANDMAID", "CLAP GENERATOR", "Clap pattern and sound generator", Cat::Drum, 0xff7b3fb0, 6);
    drumPattern (b, 2, 0.25f, 2, 1);
    b.t.params[(size_t) b.t.param ("offset")].def = 4; b.t.params[(size_t) b.t.param ("sptn")].def = 0.71f;
    b.knob ("loose", "LOOSE", 0, 1, 0.4f, 0, 4); b.sound(); b.macro (mInstability, 0.5f);
    b.knob ("smear", "SMEAR", 0, 1, 0.35f, 2, 4); b.sound(); b.macro (mDecay, 0.5f);
    b.knob ("bright", "BRIGHT", 0, 1, 0.5f, 4, 4); b.sound(); b.macro (mTone, 0.5f);
    b.knob ("pinch", "PINCH", 0, 1, 0.3f, 6, 4); b.sound(); b.macro (mResonance, 0.5f);
    b.knob ("spread", "SPREAD", 0, 1, 0.5f, 8, 4); b.sound(); b.macro (mSpace, 0.4f);
    b.knob ("pan", "PAN", -1, 1, 0.0f, 10, 4); b.sound();
    return b.t;
}
class Handmaid : public Drum
{
public:
    Handmaid() : Drum (spec()) {}
    static const TypeSpec& spec() { static TypeSpec t = specHandmaid(); return t; }
    void hit (float vel, float m) override
    {
        const float lo = c (P ("loose"));
        float t = 0;
        for (int i = 0; i < 4; ++i)
        {
            bursts[i] = (int) (t * sr);
            t += 0.006f + lo * 0.014f * (0.6f + 0.8f * noise.uni());
        }
        tailAt = (int) (t * sr); pos = 0;
        tail.trigger (0.001f, 0.06f + c (P ("smear")) * 0.7f, sr);
        tail.st = 0; tail.v = 0;
        g = vel;
        const float f = (900.0f + c (P ("bright")) * 2600.0f) * std::exp2 (m * 0.4f + pitchIn);
        for (auto& b : bp) b.set (f, 0.8f + c (P ("pinch")) * 6.0f, sr);
        active = true;
    }
    void render (float& l, float& r) override
    {
        if (! active) { l = r = 0; return; }
        float e = 0; int which = -1;
        for (int i = 0; i < 4; ++i)
            if (pos >= bursts[i] && pos < bursts[i] + (int) (0.012f * sr)) { e = std::exp (-(float) (pos - bursts[i]) / (0.0025f * sr)); which = i; }
        if (pos == tailAt) tail.trigger (0.001f, 0.06f + c (P ("smear")) * 0.7f, sr);
        const float te = tail.tick();
        ++pos;
        if (pos > tailAt && te <= 0.0f) { active = false; l = r = 0; return; }
        const float w = noise.bi();
        const float x = (e * 1.2f + te * 0.55f) * g;
        const float yl = bp[0].bp (w) * x, yr = bp[1].bp (noise.bi()) * x;
        const float sp = c (P ("spread")) * (which >= 0 ? ((which & 1) ? 1.0f : -1.0f) : 0.3f);
        const float pn = clampf (c (P ("pan")) + sp * 0.6f, -1.0f, 1.0f);
        const float a = (pn + 1.0f) * kPi * 0.25f;
        const float mono = (yl + yr) * 0.5f;
        const float wide = c (P ("spread"));
        l = fastTanh (lerp (mono, yl, wide) * std::cos (a) * 2.4f);
        r = fastTanh (lerp (mono, yr, wide) * std::sin (a) * 2.4f);
    }
private:
    int P (const char* k) const { return spec().param (k); }
    int bursts[4] {}, tailAt = 0, pos = 0;
    bool active = false;
    AD tail;
    Svf bp[2];
    float g = 0;
};

// =============================================================================
TypeSpec specHattersley()
{
    B b ("HATTERSLEY", "HI-HAT GENERATOR", "Hi-hat pattern and sound generator", Cat::Drum, 0xff3dbf6a, 6);
    drumPattern (b, 3, 0.72f, 2, 0);
    b.t.params[(size_t) b.t.param ("oint")].def = 0.35f; b.t.params[(size_t) b.t.param ("omod")].def = 0.6f; b.t.params[(size_t) b.t.param ("samt")].def = 0.7f;
    b.knob ("atk", "ATTACK", 0.0002f, 0.03f, 0.0008f, 0, 4, Map::Exp, "s"); b.sound(); b.macro (mAttack, 0.5f);
    b.knob ("len", "LENGTH", 0.015f, 1.2f, 0.07f, 2, 4, Map::Exp, "s"); b.sound(); b.macro (mDecay, 0.5f);
    b.knob ("bright", "BRIGHT", 0, 1, 0.6f, 4, 4); b.sound(); b.macro (mTone, 0.5f);
    b.knob ("grit", "GRIT", 0, 1, 0.15f, 6, 4); b.sound(); b.macro (mNoise, 0.5f);
    b.knob ("pinch", "PINCH", 0, 1, 0.3f, 8, 4); b.sound(); b.macro (mHarmonics, 0.4f);
    b.knob ("pan", "PAN", -1, 1, 0.2f, 10, 4); b.sound();
    return b.t;
}
class Hattersley : public Drum
{
public:
    Hattersley() : Drum (spec()) {}
    static const TypeSpec& spec() { static TypeSpec t = specHattersley(); return t; }
    void hit (float vel, float m) override
    {
        env.trigger (c (P ("atk")), c (P ("len")) * std::exp2 (m * 1.2f) * (0.7f + 0.3f * vel), sr);
        g = vel;
        hp.set (3500.0f + c (P ("bright")) * 6500.0f, 0.9f, sr);
        bp.set (7000.0f + c (P ("bright")) * 4000.0f, 1.2f, sr);
        tune = (1.0f + c (P ("pinch")) * 0.8f) * std::exp2 (pitchIn);
    }
    void render (float& l, float& r) override
    {
        const float e = env.tick();
        if (e <= 0) { l = r = 0; return; }
        static const float f[6] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
        float m = 0;
        for (int i = 0; i < 6; ++i) { ph[i] += f[i] * tune / sr; ph[i] -= std::floor (ph[i]); m += ph[i] < 0.5f ? 1.0f : -1.0f; }
        float x = m * 0.16f + noise.bi() * 0.45f;
        x = hp.hp (x) * 0.8f + bp.bp (x) * 0.5f;
        // grit: sample-and-hold decimation + drive
        const int hold = 1 + (int) (c (P ("grit")) * 10.0f);
        if (++hc >= hold) { hc = 0; held = x; }
        x = lerp (x, held, std::min (1.0f, c (P ("grit")) * 2.0f));
        x = fastTanh (x * (1.0f + c (P ("grit")) * 4.0f)) * e * g;
        const float a = (c (P ("pan")) + 1.0f) * kPi * 0.25f;
        l = x * std::cos (a) * 1.3f; r = x * std::sin (a) * 1.3f;
    }
private:
    int P (const char* k) const { return spec().param (k); }
    AD env; Svf hp, bp;
    float ph[6] {}, g = 0, tune = 1, held = 0; int hc = 0;
};

// =============================================================================
TypeSpec specSkin()
{
    B b ("SKIN", "PERCUSSION GENERATOR", "Percussion pattern and sound generator: hand drums to djembes, all with a synthetic flavour", Cat::Drum, 0xff2ab8b8, 6);
    drumPattern (b, 4, 0.42f, 1, 1);
    b.t.params[(size_t) b.t.param ("sptn")].def = 0.58f; b.t.params[(size_t) b.t.param ("mofs")].def = 0.45f;
    b.knob ("speed", "SPEED", 0, 1, 0.5f, 0, 4, Map::Lin, "", "Pitch envelope speed"); b.sound(); b.macro (mSpeed, 0.5f);
    b.knob ("strength", "STRENGTH", 0, 1, 0.5f, 2, 4, Map::Lin, "", "Strike strength: bend depth and noise"); b.sound(); b.macro (mAttack, 0.4f);
    b.knob ("ping", "PING", 0, 1, 0.4f, 4, 4, Map::Lin, "", "Mode brightness / resonance"); b.sound(); b.macro (mResonance, 0.5f);
    b.knob ("pitch", "PITCH", 70, 700, 190, 6, 4, Map::Exp, "Hz"); b.sound(); b.macro (mPitch, 0.4f);
    b.knob ("spread", "SPREAD", 0, 1, 0.4f, 8, 4); b.sound(); b.macro (mSpace, 0.4f);
    b.knob ("ring", "RING", 0, 1, 0.35f, 10, 4); b.sound(); b.macro (mDecay, 0.5f);
    return b.t;
}
class Skin : public Drum
{
public:
    Skin() : Drum (spec()) {}
    static const TypeSpec& spec() { static TypeSpec t = specSkin(); return t; }
    void hit (float vel, float m) override
    {
        f0 = c (P ("pitch")) * std::exp2 (m * 0.5f + pitchIn);
        const float ring = 0.05f + c (P ("ring")) * 1.2f;
        static const float ratio[3] = { 1.0f, 1.59f, 2.14f }, decayK[3] = { 1.0f, 0.6f, 0.4f };
        const float ping = c (P ("ping"));
        for (int i = 0; i < 3; ++i) { md[i].ph = 0; md[i].ratio = ratio[i]; md[i].a = (i == 0 ? 1.0f : 0.35f + 0.5f * ping) * vel; md[i].dec = std::exp (-6.9f / (ring * decayK[i] * (0.5f + ping) * sr)); }
        bend.trigger (0.0f, 0.005f + (1.0f - c (P ("speed"))) * 0.12f, sr);
        nz.trigger (0.0f, 0.004f + c (P ("strength")) * 0.02f, sr);
        bpf.set (f0 * 3.0f, 1.5f + ping * 4.0f, sr);
    }
    void render (float& l, float& r) override
    {
        const float bd = 1.0f + c (P ("strength")) * 1.2f * bend.tick();
        float y[3];
        for (int i = 0; i < 3; ++i)
        {
            auto& m = md[i];
            m.ph += f0 * m.ratio * bd / sr; m.ph -= std::floor (m.ph);
            y[i] = std::sin (kTwoPi * m.ph) * m.a; m.a *= m.dec;
        }
        const float n = bpf.bp (noise.bi()) * nz.tick() * c (P ("strength")) * 1.2f;
        const float sp = c (P ("spread"));
        l = fastTanh ((y[0] + y[1] * (1.0f - sp * 0.8f) + y[2] * (1.0f + sp * 0.5f) + n) * 0.6f);
        r = fastTanh ((y[0] + y[1] * (1.0f + sp * 0.5f) + y[2] * (1.0f - sp * 0.8f) + n) * 0.6f);
    }
private:
    int P (const char* k) const { return spec().param (k); }
    struct Mode { float ph = 0, ratio = 1, a = 0, dec = 0.999f; } md[3];
    AD bend, nz; Svf bpf;
    float f0 = 200;
};
} // namespace

std::unique_ptr<Module> createDrum (const std::string& id)
{
    if (id == "DUFFY") return std::make_unique<Duffy>();
    if (id == "AARON") return std::make_unique<Aaron>();
    if (id == "HANDMAID") return std::make_unique<Handmaid>();
    if (id == "HATTERSLEY") return std::make_unique<Hattersley>();
    if (id == "SKIN") return std::make_unique<Skin>();
    return nullptr;
}
void drumSpecs (std::vector<const TypeSpec*>& v) { v.push_back (&Duffy::spec()); v.push_back (&Aaron::spec()); v.push_back (&Handmaid::spec()); v.push_back (&Hattersley::spec()); v.push_back (&Skin::spec()); }
} // namespace nms
