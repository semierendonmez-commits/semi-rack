// NMS - the melodic generators: SAWBIRD (subtractive), CHOWNING (FM),
// SCHAEFER (sample re-order), DIDIMIDI (MIDI / CV pattern generator).
// Each one is a melodic pattern engine (pitch, pattern, range, intensity,
// trig cycle, mod pattern, phrase, meter, mask) quantised to the global scale.
#include "Common.h"
#include "Modules.h"
#include <mutex>

namespace nms
{
namespace
{
const std::vector<std::string> kTc { "2", "3", "4", "5", "6", "7", "8", "12", "16", "24", "32" };
const int kTcV[] = { 2, 3, 4, 5, 6, 7, 8, 12, 16, 24, 32 };
const std::vector<std::string> kPhr { "1 BAR", "2 BAR", "4 BAR", "8 BAR" };
const std::vector<std::string> kMeter { "2", "3", "4", "5", "6", "7", "8" };

struct Pos { int c, r; };
// the nine NMS pattern controls; `at` gives their grid cells in panel order
void melodyControls (B& b, const Pos* at, bool withPitch, float intensity, int tcyc)
{
    int i = 0;
    if (withPitch)
    {
        b.knob ("pitch", "PITCH", -24, 24, 0, at[i].c, at[i].r, Map::Int, " st", "Register of the pattern, in semitones"); b.macro (mPitch, 0.3f); ++i;
        b.knob ("ptn", "PATTERN", 0, 1, 0.31f, at[i].c, at[i].r, Map::Lin, "", "The seeded melodic pattern"); b.macro (mMesoC, 0.5f); ++i;
        b.knob ("range", "RANGE", 0, 1, 0.35f, at[i].c, at[i].r, Map::Lin, "", "Melodic range in scale degrees"); b.macro (mRange, 0.5f); ++i;
    }
    b.knob ("inten", "INTENSITY", 0, 1, intensity, at[i].c, at[i].r, Map::Lin, "", "Note density"); b.macro (mMesoI, 0.5f); ++i;
    b.sel ("tcyc", "TRIG CYCLE", kTc, tcyc, at[i].c, at[i].r, "Length of the trigger cycle, in steps"); b.macro (mMicroC, 0.4f); ++i;
    if (withPitch) { b.knob ("mptn", "MOD PTN", 0, 1, 0.5f, at[i].c, at[i].r, Map::Lin, "", "Per-note modulation pattern"); b.macro (mModulation, 0.4f); ++i; }
    b.sel ("phrase", "PHRASE", kPhr, 1, at[i].c, at[i].r, "Phrase length: the pattern varies every phrase"); b.macro (mMacroC, 0.4f); ++i;
    b.sel ("meter", "METER", kMeter, 2, at[i].c, at[i].r, "Grouping of the 16ths: where the stresses fall"); b.macro (mMesoC, 0.3f); ++i;
    b.knob ("mask", "MASK", 0, 1, 0.15f, at[i].c, at[i].r, Map::Lin, "", "Masks notes across the phrase"); b.macro (mMacroI, 0.5f); ++i;
}

class Melodic : public Generator
{
public:
    explicit Melodic (const TypeSpec& t) : Generator (t)
    {
        iPitch = t.param ("pitch"); iPtn = t.param ("ptn"); iRange = t.param ("range"); iInt = t.param ("inten"); iTc = t.param ("tcyc");
        iMptn = t.param ("mptn"); iPhr = t.param ("phrase"); iMeter = t.param ("meter"); iMask = t.param ("mask");
        salt = (uint32_t) (t.id[0] * 131 + t.id[2]);
    }
    MelodyParams melody (bool live) const
    {
        auto v = [this, live] (int i) { return live ? spec.toPlain (i, effNorm (i)) : getPlain (i); };
        auto nv = [this, live] (int i) { return live ? effNorm (i) : getNorm (i); };
        MelodyParams m;
        m.seed = seedOf (iPtn >= 0 ? nv (iPtn) : nv (seedParam()), salt);
        m.modSeed = seedOf (iMptn >= 0 ? nv (iMptn) : 0.5f, salt + 7);
        m.tcyc = kTcV[(int) v (iTc)]; m.bars = 1 << (int) v (iPhr); m.meter = 2 + (int) v (iMeter);
        m.range = iRange >= 0 ? v (iRange) : 0.0f; m.intensity = v (iInt); m.mask = v (iMask);
        return m;
    }
    int previewSteps() const override { return 16; }
    float preview (int64_t s, int& degree) const override { const auto n = melodyAt (melody (false), s); degree = n.degree; return n.on ? n.vel : 0.0f; }
    float hitAt (int64_t s16) const override { const auto n = melodyAt (melody (true), s16); return n.on ? n.vel : 0.0f; }
protected:
    virtual int seedParam() const { return iPtn; }
    void block (const Ctx& cx) override { Generator::block (cx); mp = melody (true); }
    void step (int64_t s) override
    {
        if (s < 0) { cur = melodyAt (mp, -1 - s); cur.on = true; return; }
        Note n = melodyAt (mp, s);
        disp[0].store ((float) (s % 16), std::memory_order_relaxed);
        Hit h[2] = { { 0.0f, n.vel } };
        const int k2 = link (s, h, n.on ? 1 : 0, mp.seed);
        if (k2 > 0) { if (! n.on) { n.vel = h[0].vel; n.on = true; } cur = n; addHit (0.0f, n.vel, s); }
    }
    float noteMidi (const Note& n) const
    {
        return 48.0f + (iPitch >= 0 ? c (iPitch) : 0.0f) + ctx->degreeSemis (n.degree) + 12.0f * pitchIn;
    }
    void voice (float vel, int64_t s, bool ext) override
    {
        const float midi = noteMidi (cur);
        outValues ((midi - 60.0f) / 12.0f, vel, 0.5f + 0.5f * cur.mod);
        disp[1].store (midi, std::memory_order_relaxed);
        disp[2].store (vel, std::memory_order_relaxed);
        play (midi, vel, clampf (cur.mod + vary, -1.5f, 1.5f), cur.len, s, ext);
    }
    virtual void play (float midi, float vel, float mod, float lenSteps, int64_t s, bool ext) = 0;
    MelodyParams mp;
    Note cur;
    uint32_t salt;
    int iPitch, iPtn, iRange, iInt, iTc, iMptn, iPhr, iMeter, iMask;
};

// =============================================================================
TypeSpec specSawbird()
{
    B b ("SAWBIRD", "SUBTRACTIVE SYNTH", "A super simple synth: sawtooth, filter, envelope. Melodies and leads", Cat::Synth, 0xfff2c81c, 6);
    b.channel (5);
    b.knob ("cutoff", "CUTOFF", 40, 14000, 900, 0, 0, Map::Exp, "Hz"); b.sound(); b.macro (mTone, 0.5f);
    b.knob ("reson", "RESON", 0, 1, 0.35f, 2, 0); b.sound(); b.macro (mResonance, 0.5f);
    b.knob ("fenv", "FILT ENV", 0, 1, 0.45f, 4, 0); b.sound(); b.macro (mModulation, 0.3f);
    b.knob ("atk", "ATTACK", 0.001f, 2.0f, 0.004f, 6, 0, Map::Exp, "s"); b.sound(); b.macro (mAttack, 0.5f);
    b.knob ("dec", "DECAY", 0.02f, 4.0f, 0.32f, 8, 0, Map::Exp, "s"); b.sound(); b.macro (mDecay, 0.5f);
    b.knob ("mod", "MOD", 0, 1, 0.3f, 10, 0, Map::Lin, "", "How far the mod pattern moves the filter per note"); b.sound(); b.macro (mIntensity, 0.5f);
    static const Pos at[9] = { { 0, 2 }, { 2, 2 }, { 4, 2 }, { 6, 2 }, { 8, 2 }, { 10, 2 }, { 0, 4 }, { 2, 4 }, { 4, 4 } };
    melodyControls (b, at, true, 0.45f, 6);
    b.knob ("glide", "GLIDE", 0, 1, 0.1f, 0, 6, Map::Lin, "", "Portamento between notes"); b.sound();
    b.knob ("drive", "DRIVE", 0, 1, 0.25f, 2, 6); b.sound(); b.macro (mCharacter, 0.5f);
    b.knob ("sub", "SUB", 0, 1, 0.3f, 4, 6, Map::Lin, "", "Sub oscillator, one octave down"); b.sound(); b.macro (mThickness, 0.5f);
    b.custom (0, 6, 4, 6, 4);
    b.organic (0);
    addGeneratorPorts (b);
    return b.t;
}
class Sawbird : public Melodic
{
public:
    Sawbird() : Melodic (spec()) {}
    static const TypeSpec& spec() { static TypeSpec t = specSawbird(); return t; }
    void play (float midi, float vel, float mod, float, int64_t, bool) override
    {
        target = (float) midiHz (midi);
        if (env.v <= 0.0f || c (P ("glide")) < 0.01f) freq = target;
        env.trigger (c (P ("atk")), c (P ("dec")), sr);
        g = vel; nmod = mod;
    }
    void render (float& l, float& r) override
    {
        const float e = env.tick();
        if (e <= 0.0f && env.st == 0) { l = r = 0; return; }
        const float gl = c (P ("glide"));
        freq += (target - freq) * (gl < 0.01f ? 1.0f : 1.0f - std::exp (-1.0f / (gl * gl * 0.4f * sr + 1.0f)));
        const float dt = freq / sr;
        auto saw = [dt] (float& ph, float d) { ph += dt * d; ph -= std::floor (ph); return 2.0f * ph - 1.0f - polyBlep (ph, dt * d); };
        float x = saw (ph1, 1.0f) + 0.45f * saw (ph2, 1.0045f);
        phs += dt * 0.5f; phs -= std::floor (phs);
        x += std::sin (kTwoPi * phs) * c (P ("sub")) * 1.2f;
        if (++kk >= 8)
        {
            kk = 0;
            const float oct = c (P ("fenv")) * 6.0f * e + c (P ("mod")) * nmod * 2.0f + 0.3f * std::log2 (freq / 220.0f);
            const float fc = clampf (c (P ("cutoff")) * std::exp2 (oct), 30.0f, sr * 0.45f);
            f1.set (fc, 0.6f, sr); f2.set (fc, 0.6f + c (P ("reson")) * 9.0f, sr);
        }
        float y = f2.lp (f1.lp (x * 0.5f));
        const float d = 1.0f + c (P ("drive")) * 5.0f;
        y = fastTanh (y * d) / std::sqrt (d) * e * g;
        l = r = y;
    }
private:
    int P (const char* k) const { return spec().param (k); }
    AD env; Svf f1, f2;
    float freq = 110, target = 110, ph1 = 0, ph2 = 0.3f, phs = 0, g = 0, nmod = 0; int kk = 99;
};

// =============================================================================
const std::vector<std::string> kRatio { "0.5", "1", "1.5", "2", "2.5", "3", "3.5", "4", "5", "6", "7", "9.1" };
const float kRatioV[] = { 0.5f, 1, 1.5f, 2, 2.5f, 3, 3.5f, 4, 5, 6, 7, 9.1f };
TypeSpec specChowning()
{
    B b ("CHOWNING", "FM SYNTH", "Two-operator FM, four voices: bells, basses, metallic textures", Cat::Synth, 0xffe8362c, 6);
    b.channel (6);
    b.knob ("idx", "2:1", 0, 1, 0.35f, 0, 0, Map::Lin, "", "FM depth of operator 2 into operator 1"); b.sound(); b.macro (mHarmonics, 0.5f);
    b.knob ("x12", "1/2", 0, 1, 0.1f, 2, 0, Map::Lin, "", "Operator 1 back into operator 2 (cross / feedback FM)"); b.sound(); b.macro (mCharShape, 0.4f);
    b.sel ("ratio", "2 PITCH", kRatio, 3, 4, 0, "Frequency ratio of operator 2"); b.last().sound = true; b.macro (mCharacter, 0.3f);
    b.knob ("pan", "PAN", 0, 1, 0.5f, 6, 0, Map::Lin, "", "Stereo spread of the voices"); b.sound(); b.macro (mSpace, 0.4f);
    b.knob ("a1", "1 ATTACK", 0.001f, 2, 0.002f, 8, 0, Map::Exp, "s"); b.sound(); b.macro (mAttack, 0.5f);
    b.knob ("d1", "1 DECAY", 0.02f, 6, 0.9f, 10, 0, Map::Exp, "s"); b.sound(); b.macro (mDecay, 0.5f);
    b.knob ("a2", "2 ATTACK", 0.001f, 2, 0.001f, 0, 2, Map::Exp, "s"); b.sound(); b.macro (mAttack, 0.3f);
    b.knob ("d2", "2 DECAY", 0.02f, 6, 0.4f, 2, 2, Map::Exp, "s"); b.sound(); b.macro (mDecay, 0.4f);
    b.knob ("sptn", "SND PTN", 0, 1, 0.4f, 4, 2, Map::Lin, "", "Per-note pattern on the FM depth"); b.sound(); b.macro (mComplex, 0.4f);
    b.knob ("samt", "SND AMT", 0, 1, 0.3f, 6, 2); b.sound(); b.macro (mModulation, 0.5f);
    b.knob ("eptn", "ENV PTN", 0, 1, 0.6f, 8, 2, Map::Lin, "", "Per-note pattern on the decays"); b.sound();
    b.knob ("eamt", "ENV AMT", 0, 1, 0.25f, 10, 2); b.sound(); b.macro (mModulation, 0.3f);
    static const Pos at[9] = { { 0, 4 }, { 2, 4 }, { 4, 4 }, { 6, 4 }, { 8, 4 }, { 10, 4 }, { 0, 6 }, { 2, 6 }, { 4, 6 } };
    melodyControls (b, at, true, 0.35f, 6);
    b.t.params[(size_t) b.t.param ("pitch")].def = 12;
    b.custom (0, 6, 6, 6, 2);
    b.organic (0);
    addGeneratorPorts (b);
    return b.t;
}
class Chowning : public Melodic
{
public:
    Chowning() : Melodic (spec()) {}
    static const TypeSpec& spec() { static TypeSpec t = specChowning(); return t; }
    void play (float midi, float vel, float, float, int64_t s, bool) override
    {
        auto& v = vo[next]; next = (next + 1) % 4;
        const uint32_t seed = seedOf (c (P ("sptn")), 91), eseed = seedOf (c (P ("eptn")), 93);
        const float sm = hashf (seed, (int32_t) (s % 64), 1) * 2.0f - 1.0f, em = hashf (eseed, (int32_t) (s % 64), 2) * 2.0f - 1.0f;
        v.f = (float) midiHz (midi); v.p1 = v.p2 = 0; v.fb = 0;
        const float ek = std::exp2 (c (P ("eamt")) * em * 1.5f);
        v.e1.trigger (c (P ("a1")), c (P ("d1")) * ek, sr);
        v.e2.trigger (c (P ("a2")), c (P ("d2")) * ek, sr);
        v.idx = c (P ("idx")) * 8.0f * (1.0f + c (P ("samt")) * sm) * (0.6f + 0.4f * vel);
        v.g = vel;
        v.pan = 0.5f + (hashf (seed, (int32_t) s, 3) - 0.5f) * c (P ("pan"));
    }
    void render (float& l, float& r) override
    {
        l = r = 0;
        const float ratio = kRatioV[(int) c (P ("ratio"))], x12 = c (P ("x12"));
        for (auto& v : vo)
        {
            const float e1 = v.e1.tick(), e2 = v.e2.tick();
            if (e1 <= 0.0f && v.e1.st == 0) continue;
            v.p2 += v.f * ratio / sr; v.p2 -= std::floor (v.p2);
            const float m = std::sin (kTwoPi * v.p2 + x12 * 3.0f * v.fb) * v.idx * e2;
            v.p1 += v.f / sr; v.p1 -= std::floor (v.p1);
            const float y = std::sin (kTwoPi * v.p1 + m);
            v.fb = y;
            const float out = y * e1 * v.g * 0.35f;
            l += out * std::cos (v.pan * kPi * 0.5f); r += out * std::sin (v.pan * kPi * 0.5f);
        }
        l = fastTanh (l); r = fastTanh (r);
    }
private:
    int P (const char* k) const { return spec().param (k); }
    struct V { AD e1, e2; float f = 220, p1 = 0, p2 = 0, fb = 0, idx = 0, g = 0, pan = 0.5f; } vo[4];
    int next = 0;
};

// =============================================================================
TypeSpec specSchaefer()
{
    B b ("SCHAEFER", "EXPERIMENTAL SAMPLER", "Sample re-order: every trigger plays from a patterned position, speed and glide", Cat::Sampler, 0xffc51fd0, 6);
    b.channel (2);
    b.custom (0, 0, 0, 10, 2);
    b.mom ("load", "LOAD", 10, 0, 2, "Load a WAV / AIFF / FLAC (or drop a file on the waveform)");
    b.mom ("dflt", "DEFAULT", 10, 1, 2, "Back to the built-in sample");
    b.knob ("spd", "SPEED", -2, 2, 1, 0, 2, Map::Lin, "", "Playback speed (negative = reverse)"); b.sound(); b.macro (mSpeed, 0.4f);
    b.knob ("spdmod", "SPD MOD", 0, 1, 0.2f, 2, 2, Map::Lin, "", "How far the speed pattern moves each hit (octaves)"); b.sound(); b.macro (mInstability, 0.5f);
    b.knob ("spdptn", "SPD PTN", 0, 1, 0.3f, 4, 2); b.sound();
    b.knob ("glide", "GLIDE", 0, 1, 0.0f, 6, 2, Map::Lin, "", "Speed glide time"); b.sound(); b.macro (mModulation, 0.3f);
    b.knob ("gldmod", "GLD MOD", 0, 1, 0.3f, 8, 2); b.sound();
    b.knob ("gldptn", "GLD PTN", 0, 1, 0.6f, 10, 2); b.sound();
    b.knob ("atk", "ATTACK", 0.001f, 1.0f, 0.002f, 0, 4, Map::Exp, "s"); b.sound(); b.macro (mAttack, 0.5f);
    b.knob ("dec", "DECAY", 0.02f, 4.0f, 0.25f, 2, 4, Map::Exp, "s"); b.sound(); b.macro (mDecay, 0.5f);
    b.knob ("envptn", "ENV PTN", 0, 1, 0.45f, 4, 4); b.sound();
    b.knob ("envamt", "ENV AMT", 0, 1, 0.3f, 6, 4); b.sound(); b.macro (mModulation, 0.4f);
    b.knob ("pos", "POSITION", 0, 1, 0.0f, 8, 4, Map::Lin, "", "Start position in the sample"); b.macro (mMicroC, 0.3f);
    b.knob ("posptn", "POS PTN", 0, 1, 0.5f, 10, 4, Map::Lin, "", "The seeded position pattern (also the trigger pattern)"); b.macro (mMesoC, 0.5f);
    b.knob ("posrng", "POS RNG", 0, 1, 0.6f, 0, 6, Map::Lin, "", "Range of the position jumps"); b.macro (mRange, 0.5f);
    static const Pos at[5] = { { 2, 6 }, { 4, 6 }, { 6, 6 }, { 8, 6 }, { 10, 6 } };
    melodyControls (b, at, false, 0.55f, 6);
    b.organic (0);
    addGeneratorPorts (b);
    return b.t;
}
class Schaefer : public Melodic
{
public:
    Schaefer() : Melodic (spec()) { smp = defaultSample(); }
    static const TypeSpec& spec() { static TypeSpec t = specSchaefer(); return t; }
    void setSample (std::shared_ptr<const SampleData> s) override { if (s && s->size() > 16) { smp = std::move (s); for (auto& v : vo) v.on = false; } }
    std::shared_ptr<const SampleData> sample() const override { return smp; }
    void momentary (int i) override { if (i == P ("load")) request.store (1); else if (i == P ("dflt")) request.store (2); }
    int seedParam() const override { return P ("posptn"); }
    void play (float, float vel, float, float, int64_t s, bool) override
    {
        if (! smp) return;
        const uint32_t ps = seedOf (c (P ("posptn")), 51), ss = seedOf (c (P ("spdptn")), 52), gs = seedOf (c (P ("gldptn")), 53), es = seedOf (c (P ("envptn")), 54);
        const int32_t k = (int32_t) (s % 64);
        auto& old = vo[cur]; old.fade = true;
        cur ^= 1;
        auto& v = vo[cur];
        const double n = (double) smp->size();
        double start = c (P ("pos")) + c (P ("posrng")) * (hashf (ps, k, 1) - 0.0f);
        start -= std::floor (start);
        v.pos = start * n;
        const float oct = c (P ("spdmod")) * 2.0f * (hashf (ss, k, 2) * 2.0f - 1.0f) + pitchIn;
        v.target = c (P ("spd")) * std::exp2 (oct) * (float) (smp->rate / sr);
        if (! old.on || c (P ("glide")) < 0.01f) v.speed = v.target; else v.speed = old.speed;
        v.glide = c (P ("glide")) * std::exp2 (c (P ("gldmod")) * (hashf (gs, k, 3) * 2.0f - 1.0f) * 2.0f);
        v.env.trigger (c (P ("atk")), c (P ("dec")) * std::exp2 (c (P ("envamt")) * (hashf (es, k, 4) * 2.0f - 1.0f) * 2.0f), sr);
        v.g = vel; v.on = true; v.fade = false; v.fg = 1.0f;
        disp[3].store ((float) start, std::memory_order_relaxed);
    }
    void render (float& l, float& r) override
    {
        l = r = 0;
        if (! smp) return;
        const double n = (double) smp->size();
        for (auto& v : vo)
        {
            if (! v.on) continue;
            const float e = v.env.tick();
            if (v.fade) { v.fg -= 1.0f / (0.006f * sr); if (v.fg <= 0) { v.on = false; continue; } }
            if (e <= 0.0f && v.env.st == 0) { v.on = false; continue; }
            v.speed += (v.target - v.speed) * (v.glide < 0.005f ? 1.0f : 1.0f - std::exp (-1.0f / (v.glide * sr)));
            const float a = e * v.g * v.fg;
            l += smp->at (smp->l, v.pos) * a; r += smp->at (smp->r, v.pos) * a;
            v.pos += v.speed;
            if (v.pos >= n) v.pos -= n; else if (v.pos < 0) v.pos += n;
            if (&v == &vo[cur]) disp[4].store ((float) (v.pos / n), std::memory_order_relaxed);
        }
    }
private:
    int P (const char* k) const { return spec().param (k); }
    std::shared_ptr<const SampleData> smp;
    struct V { double pos = 0; float speed = 1, target = 1, glide = 0, g = 0, fg = 1; bool on = false, fade = false; AD env; } vo[2];
    int cur = 0;
};

// =============================================================================
// DIDIMIDI: notes and CCs as MIDI out and as patch signals
struct MidiQueue { MidiOut ev[1024]; int n = 0; } gMidi;
// SEMI Family port: no MIDI out in Rack and the queue is a process-wide global shared by every
// NMS instance (Rack runs modules on several threads), so DIDIMIDI's MIDI is not queued; its
// GATE / PITCH / CC outputs still reach the internal patch.
void pushMidi (int64_t, int, int, int) {}

TypeSpec specDidimidi()
{
    B b ("DIDIMIDI", "MIDI GENERATOR", "A MIDI pattern and modulation generator: drives plugins in your DAW, hardware, or other modules by cable", Cat::Midi, 0xfff0b6e0, 6);
    b.channel (3);
    b.knob ("cut", "CUTOFF", 0, 1, 0.5f, 0, 0, Map::Lin, "", "CC 74 and the CUT output, moved by the mod pattern"); b.sound(); b.macro (mTone, 0.5f);
    b.knob ("atk", "ATTACK", 0, 1, 0.1f, 2, 0, Map::Lin, "", "CC 73 and the ATK output"); b.sound(); b.macro (mAttack, 0.5f);
    b.knob ("dec", "DECAY", 0, 1, 0.4f, 4, 0, Map::Lin, "", "CC 72 and the DEC output, also the gate length"); b.sound(); b.macro (mDecay, 0.5f);
    b.knob ("mod", "MOD", 0, 1, 0.3f, 6, 0, Map::Lin, "", "CC 1 and how far the mod pattern moves the CCs"); b.sound(); b.macro (mModulation, 0.5f);
    b.choice ("mch", "MIDI CH", { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16" }, 0, 8, 0, 2); b.fixed();
    b.tog ("prev", "PREVIEW", true, 8, 1, 2, "A small built-in voice so the pattern can be heard without MIDI routing"); b.fixed();
    static const Pos at[9] = { { 0, 2 }, { 2, 2 }, { 4, 2 }, { 6, 2 }, { 8, 2 }, { 0, 4 }, { 2, 4 }, { 4, 4 }, { 6, 4 } };
    melodyControls (b, at, true, 0.4f, 6);
    b.custom (0, 0, 6, 12, 2);
    b.organic (0);
    addGeneratorPorts (b);
    b.out ("gate", "GATE", PortKind::Pulse, "High for the note length");
    b.out ("cut", "CUT", PortKind::CV); b.out ("atkc", "ATK", PortKind::CV); b.out ("decc", "DEC", PortKind::CV);
    return b.t;
}
class Didimidi : public Melodic
{
public:
    Didimidi() : Melodic (spec()) {}
    static const TypeSpec& spec() { static TypeSpec t = specDidimidi(); return t; }
    void block (const Ctx& cx) override { Melodic::block (cx); base = cx.sample; }
    void reset() override { Melodic::reset(); if (noteOn >= 0) { pushMidi (base, 0x80 | chan, noteOn, 0); noteOn = -1; } gateLeft = 0; }
    void play (float midi, float vel, float mod, float lenSteps, int64_t, bool) override
    {
        chan = (int) c (P ("mch"));
        const int64_t now = base + k;
        if (noteOn >= 0) pushMidi (now, 0x80 | chan, noteOn, 0);
        noteOn = (int) clampf (std::round (midi), 0, 127);
        const float m = c (P ("mod"));
        cc[0] = clamp01 (c (P ("cut")) + m * mod * 0.5f); cc[1] = clamp01 (c (P ("atk")) + m * mod * 0.2f);
        cc[2] = clamp01 (c (P ("dec")) - m * mod * 0.3f); cc[3] = m;
        static const int num[4] = { 74, 73, 72, 1 };
        for (int i = 0; i < 4; ++i) pushMidi (now, 0xB0 | chan, num[i], (int) std::lround (cc[i] * 127.0f));
        pushMidi (now, 0x90 | chan, noteOn, std::max (1, (int) std::lround (vel * 127.0f)));
        gateLeft = (int) (stepSamples * (0.25 + 1.75 * cc[2]) * lenSteps);
        freq = (float) midiHz (midi); env.trigger (0.001f + cc[1] * 0.2f, 0.05f + cc[2] * 0.8f, sr); g = vel;
        lp.set (300.0f * std::exp2 (cc[0] * 5.0f), 1.2f, sr);
    }
    void tick (const float* in, float* o) override
    {
        Melodic::tick (in, o);   // writes 0..5, k advances
        if (gateLeft > 0 && --gateLeft == 0 && noteOn >= 0) { pushMidi (base + k, 0x80 | chan, noteOn, 0); noteOn = -1; }
        o[6] = gateLeft > 0 ? 1.0f : 0.0f; o[7] = cc[0]; o[8] = cc[1]; o[9] = cc[2];
    }
    void render (float& l, float& r) override
    {
        const float e = env.tick();
        if (c (P ("prev")) < 0.5f || (e <= 0 && env.st == 0)) { l = r = 0; return; }
        ph += freq / sr; ph -= std::floor (ph);
        const float y = lp.lp ((ph < 0.5f ? 1.0f : -1.0f) * 0.4f + std::sin (kTwoPi * ph) * 0.4f) * e * g * 0.7f;
        l = r = y;
    }
private:
    int P (const char* k) const { return spec().param (k); }
    int64_t base = 0; int noteOn = -1, chan = 0, gateLeft = 0;
    float cc[4] {}, freq = 220, ph = 0, g = 0;
    AD env; Svf lp;
};
} // namespace

int drainMidi (MidiOut* out, int max)
{
    const int n = std::min (max, gMidi.n);
    for (int i = 0; i < n; ++i) out[i] = gMidi.ev[i];
    gMidi.n = 0;
    return n;
}

// the built-in sample: a synthetic two-bar break with a tonal drone, so
// SCHAEFER sounds before anything is loaded (no third-party audio inside)
std::shared_ptr<const SampleData> defaultSample()
{
    static std::shared_ptr<const SampleData> s = []
    {
        auto d = std::make_shared<SampleData>();
        const double sr = 48000.0, bpm = 96.0;
        const int steps = 32; const double st = 60.0 / bpm / 4.0;
        const size_t n = (size_t) (steps * st * sr);
        d->l.assign (n, 0.0f); d->r.assign (n, 0.0f); d->rate = sr; d->name = "NMS BREAK (built-in)";
        Rng rng (77);
        const char* kick  = "x.....x...x.....x.....x..x......";
        const char* snare = "....x.......x.......x.......x..x";
        const char* hat   = "x.x.xxx.x.x.x.xxx.x.x.xxx.x.x.x.";
        for (int k = 0; k < steps; ++k)
        {
            const size_t at = (size_t) (k * st * sr);
            for (size_t i = 0; i < (size_t) (0.5 * sr) && at + i < n; ++i)
            {
                const float t = (float) i / (float) sr;
                float v = 0;
                if (kick[k] == 'x') v += std::sin (kTwoPi * (55.0f * t + 60.0f * (1.0f - std::exp (-t * 30.0f)) / 30.0f)) * std::exp (-t * 7.0f) * 0.9f;
                if (snare[k] == 'x') v += (rng.bi() * 0.6f + std::sin (kTwoPi * 190.0f * t) * 0.4f) * std::exp (-t * 14.0f) * 0.7f;
                if (hat[k] == 'x' && t < 0.08f) v += rng.bi() * std::exp (-t * 70.0f) * 0.25f * (k % 4 == 2 ? 1.6f : 1.0f);
                d->l[at + i] += v; d->r[at + i] += v * (hat[k] == 'x' ? 0.85f : 1.0f);
            }
        }
        for (size_t i = 0; i < n; ++i)
        {
            const float t = (float) i / (float) sr;
            const float pad = (std::sin (kTwoPi * 110.0f * t) + 0.5f * std::sin (kTwoPi * 164.8f * t + std::sin (t * 0.7f))) * 0.12f;
            d->l[i] = fastTanh (d->l[i] + pad); d->r[i] = fastTanh (d->r[i] + pad * 0.9f);
        }
        return std::shared_ptr<const SampleData> (d);
    }();
    return s;
}

std::unique_ptr<Module> createSynth (const std::string& id)
{
    if (id == "SAWBIRD") return std::make_unique<Sawbird>();
    if (id == "CHOWNING") return std::make_unique<Chowning>();
    if (id == "SCHAEFER") return std::make_unique<Schaefer>();
    if (id == "DIDIMIDI") return std::make_unique<Didimidi>();
    return nullptr;
}
void synthSpecs (std::vector<const TypeSpec*>& v) { v.push_back (&Sawbird::spec()); v.push_back (&Chowning::spec()); v.push_back (&Schaefer::spec()); v.push_back (&Didimidi::spec()); }
} // namespace nms
