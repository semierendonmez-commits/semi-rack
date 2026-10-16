// NMS - the FX modules: BAIKAL (reverb), PYONGYANG (dub delay), ROISIN
// (triggered resonator), SINKHOLE (triggered filter glitch). Their inputs
// are normalled along the channel: audio and triggers of the modules before
// them on the same mixer channel. "Ptn" controls pick a seeded value per
// trigger, the matching "Mod" / "Amt" control scales it.
#include "Common.h"
#include "Modules.h"

namespace nms
{
namespace
{
void fxPorts (B& b, bool trig)
{
    b.in ("l", "IN L", PortKind::Audio, Norm::ChainL, "Normalled to the modules before this one on its channel");
    b.in ("r", "IN R", PortKind::Audio, Norm::ChainR, "Normalled to the channel chain (or IN L when only that is patched)");
    if (trig) b.in ("trig", "TRIG", PortKind::Pulse, Norm::ChainTrig, "Normalled to the triggers of the instrument before it on the channel");
    b.in ("mix", "MIX CV", PortKind::CV, Norm::None, "Adds to the wet level");
    b.out ("l", "OUT L", PortKind::Audio); b.out ("r", "OUT R", PortKind::Audio);
}

class Fx : public Module
{
public:
    using Module::Module;
protected:
    float ptn (int seedParam, int salt) const { return hashf (seedOf (c (seedParam), (uint32_t) salt), (int32_t) (trigN % 16), (uint32_t) salt) * 2.0f - 1.0f; }
    int64_t trigN = 0;
    Edge trig;
    int P (const char* k) const { return spec.param (k); }
};

// =============================================================================
TypeSpec specBaikal()
{
    B b ("BAIKAL", "REVERB", "A reverb, named after the deepest lake on earth", Cat::Fx, 0xff1f7a74, 3);
    b.channel (0);
    b.knob ("lvl", "LEVEL", 0, 1, 0.3f, 0, 0); b.sound(); b.macro (mSpace, 0.5f);
    b.knob ("diff", "DIFFUSION", 0, 1, 0.6f, 2, 0); b.sound(); b.macro (mComplex, 0.3f);
    b.knob ("size", "SIZE", -10, 40, 18, 0, 2); b.sound(); b.macro (mSpace, 0.4f);
    b.knob ("len", "LENGTH", 40, 100, 74, 2, 2); b.sound(); b.macro (mDecay, 0.4f);
    b.knob ("hid", "HI DAMP", 0, 6, 3, 0, 4); b.sound(); b.macro (mTone, -0.4f);
    b.knob ("lod", "LO DAMP", 0, 6, 1, 2, 4); b.sound();
    b.custom (0, 0, 6, 6, 2);
    b.organic (2);
    fxPorts (b, false);
    return b.t;
}
class Baikal : public Fx
{
public:
    Baikal() : Fx (spec()) {}
    static const TypeSpec& spec() { static TypeSpec t = specBaikal(); return t; }
    void prepare (double s) override { Fx::prepare (s); rev.prepare (s); }
    void reset() override { rev.reset(); }
    void block (const Ctx& cx) override
    {
        Fx::block (cx);
        rp.size = c (P ("size")); rp.decay = c (P ("len")); rp.hiCut = c (P ("hid")); rp.loCut = c (P ("lod"));
        rp.diffusion = 0.3f + 0.52f * c (P ("diff")); rp.spin = 0.8f;
    }
    void tick (const float* in, float* o) override
    {
        const float l = in[0], r = in[1];
        float wl, wr; rev.tick (l * 0.6f, r * 0.6f, wl, wr, rp);
        const float lv = clamp01 (c (P ("lvl")) + in[2]) * 1.5f;
        o[0] = l + wl * lv; o[1] = r + wr * lv;
        const float pk = std::abs (wl) + std::abs (wr), old = disp[0].load (std::memory_order_relaxed);
        disp[0].store (pk > old ? pk : old * 0.9995f, std::memory_order_relaxed);
    }
private:
    Reverb rev; RevParams rp;
};

// =============================================================================
const std::vector<std::string> kTime { "1/16", "1/8", "3/16", "1/4", "3/8", "1/2" };
const float kTimeV[] = { 1, 2, 3, 4, 6, 8 };
TypeSpec specPyongyang()
{
    B b ("PYONGYANG", "DUB DELAY", "A dub delay: filtered, saturated, wobbling repeats; THROW opens the send on chosen triggers", Cat::Fx, 0xff4a4f55, 4);
    b.channel (0);
    b.sel ("time", "TIME", kTime, 2, 0, 0, "Delay time, synced to the tempo"); b.sound(); b.macro (mSpeed, 0.3f);
    b.knob ("fb", "FEEDBACK", 0, 1.1f, 0.62f, 2, 0); b.sound(); b.macro (mDecay, 0.4f);
    b.knob ("cut", "CUTOFF", 200, 7000, 1400, 4, 0, Map::Exp, "Hz"); b.sound(); b.macro (mTone, 0.5f);
    b.knob ("res", "RESON", 0, 1, 0.3f, 6, 0); b.sound(); b.macro (mResonance, 0.5f);
    b.knob ("width", "WIDTH", 0, 1, 0.7f, 0, 2, Map::Lin, "", "Ping-pong between the sides"); b.sound(); b.macro (mSpace, 0.3f);
    b.knob ("lspd", "LFO SPD", 0.05f, 6, 0.35f, 2, 2, Map::Exp, "Hz"); b.sound();
    b.knob ("lamt", "LFO AMT", 0, 1, 0.2f, 4, 2, Map::Lin, "", "Tape wobble"); b.sound(); b.macro (mInstability, 0.5f);
    b.knob ("diff", "DIFFUSE", 0, 1, 0.3f, 6, 2, Map::Lin, "", "Smears the repeats"); b.sound();
    b.knob ("drive", "DRIVE", 0, 1, 0.3f, 0, 4); b.sound(); b.macro (mCharacter, 0.4f);
    b.knob ("lvl", "LEVEL", 0, 1, 0.5f, 2, 4); b.sound(); b.macro (mSpace, 0.4f);
    b.knob ("throw", "THROW", 0, 1, 0.0f, 4, 4, Map::Lin, "", "0: the send is always open. Higher: only some triggers are thrown into the delay"); b.macro (mMicroI, 0.4f);
    b.knob ("tptn", "THR PTN", 0, 1, 0.5f, 6, 4, Map::Lin, "", "Which triggers are thrown");
    b.custom (0, 0, 6, 8, 2);
    b.organic (1);
    fxPorts (b, true);
    return b.t;
}
class Pyongyang : public Fx
{
public:
    Pyongyang() : Fx (spec()) {}
    static const TypeSpec& spec() { static TypeSpec t = specPyongyang(); return t; }
    void prepare (double s) override { Fx::prepare (s); for (auto& d : dl) d.init ((int) (s * 4.2)); for (auto& d : ap) d.init (2048); }
    void reset() override { for (auto& d : dl) d.clear(); for (auto& d : ap) d.clear(); f[0].clear(); f[1].clear(); send = 1; }
    void block (const Ctx& cx) override
    {
        Fx::block (cx);
        const double s16 = 60.0 / std::max (20.0, cx.bpm) / 4.0;
        target = (float) std::min (4.0 * sr, kTimeV[(int) c (P ("time"))] * s16 * sr);
        for (auto& x : f) x.set (c (P ("cut")), 0.6f + c (P ("res")) * 5.0f, sr);
    }
    void tick (const float* in, float* o) override
    {
        const float l = in[0], r = in[1];
        if (trig.rise (in[2]))
        {
            ++trigN;
            if (c (P ("throw")) > 0.01f && (ptn (P ("tptn"), 61) * 0.5f + 0.5f) < c (P ("throw"))) throwLeft = (int) (target * 0.9f);
        }
        const float th = c (P ("throw"));
        float sendT = th < 0.01f ? 1.0f : (1.0f - th) * 0.6f;
        if (throwLeft > 0) { --throwLeft; sendT = 1.0f; }
        send += (sendT - send) * 0.002f;
        if (time <= 0) time = target;
        time += (target - time) * 0.0005f;
        lph += c (P ("lspd")) / sr; lph -= std::floor (lph);
        const float wob = std::sin (kTwoPi * lph) * c (P ("lamt")) * 0.006f * sr;
        const float fb = c (P ("fb")), w = c (P ("width")), dr = 1.0f + c (P ("drive")) * 4.0f, df = c (P ("diff")) * 0.7f;
        float rd[2] = { dl[0].read (std::max (2.0f, time + wob)), dl[1].read (std::max (2.0f, time - wob)) };
        float y[2];
        for (int ch = 0; ch < 2; ++ch)
        {
            float x = f[ch].bp (rd[ch]) * 1.6f + f[ch].ic2 * 0.0f;
            x = fastTanh (x * dr) / std::sqrt (dr);
            const float a = ap[ch].read (300.0f + 200.0f * (float) ch);
            const float v = x + df * a; ap[ch].push (v); x = a - df * v;
            y[ch] = x;
        }
        const float inL = l * send, inR = r * send;
        // ping-pong: width crosses the feedback between the sides
        dl[0].push (inL + (y[0] * (1.0f - w) + y[1] * w) * fb);
        dl[1].push (inR * (1.0f - w) + (y[1] * (1.0f - w) + y[0] * w) * fb);
        const float lv = clamp01 (c (P ("lvl")) + in[3]) * 1.4f;
        o[0] = l + rd[0] * lv; o[1] = r + rd[1] * lv;
        if (! std::isfinite (o[0]) || ! std::isfinite (o[1])) { reset(); o[0] = l; o[1] = r; }
        if (++dk >= 64) { dk = 0; disp[0].store (send); disp[1].store (std::abs (rd[0]) + std::abs (rd[1])); }
    }
private:
    DelayLine dl[2], ap[2]; Svf f[2];
    float time = 0, target = 1000, lph = 0, send = 1; int throwLeft = 0, dk = 0;
};

// =============================================================================
TypeSpec specRoisin()
{
    B b ("ROISIN", "TRIGGERED RESONATOR", "Feed it audio and a trigger, and it rings: four resonators tuned to the scale", Cat::Fx, 0xffee8f80, 5);
    b.channel (0);
    b.knob ("spread", "SPREAD", 0, 1, 0.4f, 0, 0, Map::Lin, "", "Interval spread of the four resonators"); b.sound(); b.macro (mHarmonics, 0.5f);
    b.knob ("sptn", "SPR PTN", 0, 1, 0.3f, 2, 0); b.sound();
    b.knob ("smod", "SPR MOD", 0, 1, 0.2f, 4, 0); b.sound(); b.macro (mComplex, 0.4f);
    b.knob ("base", "BASE", 0, 1, 0.35f, 6, 0, Map::Lin, "", "Root of the resonators, in scale degrees"); b.sound(); b.macro (mPitch, 0.4f);
    b.knob ("bptn", "BSE PTN", 0, 1, 0.6f, 8, 0); b.sound();
    b.knob ("bamt", "BSE AMT", 0, 1, 0.2f, 0, 2); b.sound(); b.macro (mRange, 0.5f);
    b.knob ("dptn", "DAMP P", 0, 1, 0.2f, 2, 2); b.sound();
    b.knob ("damp", "DAMP", 0, 1, 0.4f, 4, 2); b.sound(); b.macro (mTone, -0.4f);
    b.knob ("fb", "FEEDBACK", 0, 1, 0.75f, 6, 2, Map::Lin, "", "Ring time"); b.sound(); b.macro (mDecay, 0.4f);
    b.knob ("fm", "FM", 0, 1, 0.0f, 8, 2, Map::Lin, "", "The input bends the resonators"); b.sound(); b.macro (mNoise, 0.4f);
    b.knob ("cut", "CUTOFF", 100, 12000, 3000, 0, 4, Map::Exp, "Hz"); b.sound(); b.macro (mTone, 0.4f);
    b.knob ("res", "RESONANCE", 0, 1, 0.2f, 2, 4); b.sound(); b.macro (mResonance, 0.5f);
    b.knob ("famt", "FILT AMT", 0, 1, 0.3f, 4, 4); b.sound(); b.macro (mModulation, 0.4f);
    b.knob ("atk", "ATTACK", 0.0005f, 0.5f, 0.002f, 6, 4, Map::Exp, "s"); b.sound(); b.macro (mAttack, 0.5f);
    b.knob ("dec", "DECAY", 0.01f, 2.0f, 0.12f, 8, 4, Map::Exp, "s", "How long the input excites the resonators after a trigger"); b.sound(); b.macro (mDecay, 0.4f);
    b.knob ("rptn", "RES PTN", 0, 1, 0.5f, 0, 6); b.sound();
    b.knob ("rmod", "RES MOD", 0, 1, 0.2f, 2, 6); b.sound();
    b.knob ("fptn", "FILT PTN", 0, 1, 0.5f, 4, 6); b.sound();
    b.knob ("fmod", "FLT MOD", 0, 1, 0.3f, 6, 6); b.sound(); b.macro (mModulation, 0.3f);
    b.knob ("hp", "HP", 20, 2000, 80, 8, 6, Map::Exp, "Hz"); b.sound();
    b.organic (1);
    fxPorts (b, true);
    return b.t;
}
class Roisin : public Fx
{
public:
    Roisin() : Fx (spec()) {}
    static const TypeSpec& spec() { static TypeSpec t = specRoisin(); return t; }
    void prepare (double s) override { Fx::prepare (s); for (auto& d : dl) d.init ((int) (s / 20.0) + 8); retune(); }
    void reset() override { for (auto& d : dl) d.clear(); for (auto& l : lp) l.s = 0; out[0].clear(); out[1].clear(); hpf[0].clear(); hpf[1].clear(); }
    void block (const Ctx& cx) override
    {
        Fx::block (cx);
        if (! tuned) retune();
        const float fo = c (P ("famt")) * 5.0f * env.v + c (P ("fmod")) * fv * 2.0f;
        const float res = clamp01 (c (P ("res")) + c (P ("rmod")) * rv * 0.5f);
        for (auto& x : out) x.set (clampf (c (P ("cut")) * std::exp2 (fo), 40.0f, sr * 0.45f), 0.6f + res * 8.0f, sr);
        for (auto& x : hpf) x.set (c (P ("hp")), 0.7f, sr);
        const float dmp = clamp01 (c (P ("damp")) + c (P ("dptn")) * dv * 0.5f);
        for (auto& l : lp) l.set (12000.0 * std::exp2 (-dmp * 5.0), sr);
        fb = 0.6f + 0.395f * c (P ("fb"));
    }
    void tick (const float* in, float* o) override
    {
        const float l = in[0], r = in[1];
        if (trig.rise (in[2])) { ++trigN; env.trigger (c (P ("atk")), c (P ("dec")), sr); retune(); }
        const float e = env.tick();
        const float ex = (l + r) * 0.5f * e;
        const float fm = c (P ("fm")) * (l + r) * 0.5f * 0.3f;
        float sum[2] = { 0, 0 };
        for (int i = 0; i < 4; ++i)
        {
            const float d = std::max (2.0f, len[i] * (1.0f + fm));
            const float y = dl[i].read (d);
            dl[i].push (ex + lp[i].lp (y) * fb);
            sum[i & 1] += y; sum[(i + 1) & 1] += y * 0.4f;
        }
        float wl = hpf[0].hp (out[0].lp (sum[0] * 0.4f)), wr = hpf[1].hp (out[1].lp (sum[1] * 0.4f));
        wl = fastTanh (wl); wr = fastTanh (wr);
        const float mix = clamp01 (0.8f + in[3]);
        o[0] = l * 0.6f + wl * mix; o[1] = r * 0.6f + wr * mix;
        if (! std::isfinite (o[0]) || ! std::isfinite (o[1])) { reset(); o[0] = l; o[1] = r; }
        if (++dk >= 64) { dk = 0; disp[0].store (e); for (int i = 0; i < 4; ++i) disp[(size_t) (1 + i)].store (freq[i]); }
    }
private:
    void retune()
    {
        if (ctx == nullptr) return;
        tuned = true;
        fv = ptn (P ("fptn"), 71); rv = ptn (P ("rptn"), 72); dv = ptn (P ("dptn"), 73);
        const float sp = clamp01 (c (P ("spread")) + c (P ("smod")) * ptn (P ("sptn"), 74) * 0.5f);
        const int root = (int) std::lround (c (P ("base")) * 21.0f + c (P ("bamt")) * ptn (P ("bptn"), 75) * 7.0f);
        const int off[4] = { 0, (int) std::lround (sp * 4.0f), (int) std::lround (sp * 7.0f), (int) std::lround (sp * 11.0f) };
        for (int i = 0; i < 4; ++i)
        {
            freq[i] = (float) midiHz (36.0 + ctx->degreeSemis (root + off[i]) + (i == 3 && sp < 0.05f ? 12.0f : 0.0f));
            len[i] = sr / freq[i];
        }
    }
    DelayLine dl[4]; OnePole lp[4]; Svf out[2], hpf[2]; AD env;
    float len[4] { 100, 100, 100, 100 }, freq[4] {}, fb = 0.9f, fv = 0, rv = 0, dv = 0; int dk = 0; bool tuned = false;
};

// =============================================================================
TypeSpec specSinkhole()
{
    B b ("SINKHOLE", "FILTER GLITCH", "A triggered filter glitch: stutters, sweeps and drops chunks out of your sound", Cat::Fx, 0xffc24a1c, 5);
    b.channel (0);
    b.knob ("cen", "CENTRE", 80, 8000, 900, 0, 0, Map::Exp, "Hz"); b.sound(); b.macro (mTone, 0.5f);
    b.knob ("cptn", "CEN PTN", 0, 1, 0.4f, 2, 0); b.sound();
    b.knob ("cmod", "CEN MOD", 0, 1, 0.4f, 4, 0); b.sound(); b.macro (mModulation, 0.4f);
    b.knob ("spr", "SPREAD", 0, 1, 0.4f, 6, 0, Map::Lin, "", "Distance of the two band filters"); b.sound(); b.macro (mHarmonics, 0.4f);
    b.knob ("sptn", "SPR PTN", 0, 1, 0.6f, 8, 0); b.sound();
    b.knob ("smod", "SPR MOD", 0, 1, 0.2f, 0, 2); b.sound();
    b.knob ("fenv", "FILT ENV", 0, 1, 0.4f, 2, 2); b.sound(); b.macro (mModulation, 0.3f);
    b.knob ("pd", "PHS DST", 0, 1, 0.15f, 4, 2, Map::Lin, "", "Phase distortion of the wet signal"); b.sound(); b.macro (mCharacter, 0.4f);
    b.knob ("aenv", "AMP ENV", 0, 1, 0.5f, 6, 2, Map::Lin, "", "How deep each trigger drops the sound out"); b.sound(); b.macro (mMicroI, 0.4f);
    b.knob ("res", "RESON", 0, 1, 0.4f, 8, 2); b.sound(); b.macro (mResonance, 0.5f);
    b.knob ("time", "TIME", 0.005f, 0.3f, 0.06f, 0, 4, Map::Exp, "s", "Length of the stuttered chunk"); b.sound(); b.macro (mSpeed, -0.3f);
    b.knob ("lvl", "LEVEL", 0, 1, 0.6f, 2, 4, Map::Lin, "", "Wet level of the glitch"); b.sound(); b.macro (mIntensity, 0.4f);
    b.knob ("fb", "FEEDBACK", 0, 1, 0.5f, 4, 4, Map::Lin, "", "How long the stutter repeats"); b.sound();
    b.knob ("atk", "ATTACK", 0.0005f, 0.3f, 0.001f, 6, 4, Map::Exp, "s"); b.sound(); b.macro (mAttack, 0.5f);
    b.knob ("dec", "DECAY", 0.01f, 1.5f, 0.15f, 8, 4, Map::Exp, "s"); b.sound(); b.macro (mDecay, 0.5f);
    b.knob ("rptn", "RES PTN", 0, 1, 0.5f, 0, 6); b.sound();
    b.knob ("rmod", "RES MOD", 0, 1, 0.2f, 2, 6); b.sound();
    b.knob ("fptn", "FILT PTN", 0, 1, 0.5f, 4, 6); b.sound();
    b.knob ("fmod", "FLT MOD", 0, 1, 0.3f, 6, 6); b.sound();
    b.knob ("dbl", "DOUBLE", 0, 1, 0.25f, 8, 6, Map::Lin, "", "Chance that a stutter plays at double speed"); b.sound(); b.macro (mMicroC, 0.4f);
    b.organic (1);
    fxPorts (b, true);
    return b.t;
}
class Sinkhole : public Fx
{
public:
    Sinkhole() : Fx (spec()) {}
    static const TypeSpec& spec() { static TypeSpec t = specSinkhole(); return t; }
    void prepare (double s) override { Fx::prepare (s); for (auto& b : buf) b.init ((int) (s * 0.7)); }
    void reset() override { for (auto& b : buf) b.clear(); for (auto& f : bp) f.clear(); stut = false; }
    void block (const Ctx& cx) override
    {
        Fx::block (cx);
        const float e = env.v;
        const float oc = c (P ("fenv")) * 4.0f * e + c (P ("cmod")) * cv * 2.0f + c (P ("fmod")) * fv;
        const float sp = clamp01 (c (P ("spr")) + c (P ("smod")) * sv * 0.5f) * 2.0f;
        const float q = 0.8f + clamp01 (c (P ("res")) + c (P ("rmod")) * rv * 0.5f) * 10.0f;
        const float f0 = c (P ("cen")) * std::exp2 (oc);
        for (int ch = 0; ch < 2; ++ch) { bp[ch * 2].set (f0 * std::exp2 (-sp * 0.5f), q, sr); bp[ch * 2 + 1].set (f0 * std::exp2 (sp * 0.5f), q, sr); }
    }
    void tick (const float* in, float* o) override
    {
        const float l = in[0], r = in[1];
        buf[0].push (l); buf[1].push (r);
        if (trig.rise (in[2]))
        {
            ++trigN;
            env.trigger (c (P ("atk")), c (P ("dec")), sr);
            cv = ptn (P ("cptn"), 81); sv = ptn (P ("sptn"), 82); rv = ptn (P ("rptn"), 83); fv = ptn (P ("fptn"), 84);
            chunk = c (P ("time")) * sr; rpos = 0; stut = true; rep = 1.0f;
            speed = (ptn (P ("cptn"), 85) * 0.5f + 0.5f) < c (P ("dbl")) ? 2.0f : 1.0f;
        }
        const float e = env.tick();
        float x[2] = { l, r };
        if (stut)
        {
            for (int ch = 0; ch < 2; ++ch) x[ch] = buf[ch].read (chunk - std::fmod (rpos, chunk) + chunk * 0.0f + 1.0f) * rep;
            rpos += speed;
            if (rpos >= chunk) { rpos -= chunk; rep *= c (P ("fb")); if (rep < 0.02f) stut = false; }
        }
        const float pd = c (P ("pd"));
        float w[2];
        for (int ch = 0; ch < 2; ++ch)
        {
            float y = bp[ch * 2].bp (x[ch]) + bp[ch * 2 + 1].bp (x[ch]);
            y = pd > 0.001f ? lerp (y, std::sin (clampf (y, -1.5f, 1.5f) * kPi * 0.5f * (1.0f + pd * 3.0f)), pd) : y;
            w[ch] = fastTanh (y * 1.4f);
        }
        const float gain = 1.0f - c (P ("aenv")) * e;
        const float mix = clamp01 (clamp01 (c (P ("lvl")) + in[3]) * (stut ? 1.0f : e));
        o[0] = lerp (l, w[0], mix) * gain; o[1] = lerp (r, w[1], mix) * gain;
        if (! std::isfinite (o[0]) || ! std::isfinite (o[1])) { reset(); o[0] = l; o[1] = r; }
        if (++dk >= 64) { dk = 0; disp[0].store (e); disp[1].store (stut ? rep : 0.0f); }
    }
private:
    DelayLine buf[2]; Svf bp[4]; AD env;
    float chunk = 1000, rpos = 0, rep = 0, speed = 1, cv = 0, sv = 0, rv = 0, fv = 0; bool stut = false; int dk = 0;
};
} // namespace

std::unique_ptr<Module> createFx (const std::string& id)
{
    if (id == "BAIKAL") return std::make_unique<Baikal>();
    if (id == "PYONGYANG") return std::make_unique<Pyongyang>();
    if (id == "ROISIN") return std::make_unique<Roisin>();
    if (id == "SINKHOLE") return std::make_unique<Sinkhole>();
    return nullptr;
}
void fxSpecs (std::vector<const TypeSpec*>& v) { v.push_back (&Baikal::spec()); v.push_back (&Pyongyang::spec()); v.push_back (&Roisin::spec()); v.push_back (&Sinkhole::spec()); }
} // namespace nms
