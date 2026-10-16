// NMS - UNDERTOW: reese / neuro / acid bass generator (neuro bass + TB-3PO ideas).
// (SIGNAL, ESSAIM, BREAKBEAT, STRUCTOR, DISSOLVER are the original Schwung
// modules, hosted by Upstream.cpp.)
#include "Common.h"
#include "Modules.h"

namespace nms
{
namespace
{
const std::vector<std::string> kPhr { "1 BAR", "2 BAR", "4 BAR", "8 BAR" };
const std::vector<std::string> kMeter { "2", "3", "4", "5", "6", "7", "8" };
const std::vector<std::string> kTc { "2", "3", "4", "5", "6", "7", "8", "12", "16", "24", "32" };
const int kTcV[] = { 2, 3, 4, 5, 6, 7, 8, 12, 16, 24, 32 };

// =============================================================================
const std::vector<std::string> kMove { "OFF", "1/4", "1/8", "1/8T", "1/16", "1/16T" };
const float kMoveBeats[] = { 0, 1.0f, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f };
TypeSpec specUndertow()
{
    B b ("UNDERTOW", "BASS GENERATOR", "Reese, neuro and acid bass: detuned saws, folding, wobbles in time, slides", Cat::Synth, 0xff6a5acd, 6);
    b.channel (6);
    b.sel ("mode", "MODE", { "REESE", "NEURO", "ACID" }, 0, 0, 0); b.last().sound = true;
    b.knob ("det", "DETUNE", 0, 1, 0.45f, 2, 0); b.sound(); b.macro (mThickness, 0.5f);
    b.knob ("fold", "FOLD", 0, 1, 0.25f, 4, 0, Map::Lin, "", "Wavefolding (neuro movement)"); b.sound(); b.macro (mHarmonics, 0.5f);
    b.knob ("cut", "CUTOFF", 40, 8000, 420, 6, 0, Map::Exp, "Hz"); b.sound(); b.macro (mTone, 0.5f);
    b.knob ("res", "RESON", 0, 1, 0.35f, 8, 0); b.sound(); b.macro (mResonance, 0.5f);
    b.knob ("env", "ENV", 0, 1, 0.45f, 10, 0, Map::Lin, "", "Filter envelope (accented notes more)"); b.sound(); b.macro (mModulation, 0.4f);
    b.sel ("move", "MOVE", kMove, 2, 0, 2, "Wobble rate, locked to the tempo"); b.last().sound = true; b.macro (mSpeed, 0.3f);
    b.knob ("mamt", "MOVE AMT", 0, 1, 0.4f, 2, 2, Map::Lin, "", "Wobble depth on filter and fold"); b.sound(); b.macro (mModulation, 0.5f);
    b.knob ("drive", "DRIVE", 0, 1, 0.4f, 4, 2); b.sound(); b.macro (mCharacter, 0.5f);
    b.knob ("sub", "SUB", 0, 1, 0.5f, 6, 2); b.sound(); b.macro (mThickness, 0.3f);
    b.knob ("glide", "GLIDE", 0, 1, 0.2f, 8, 2, Map::Lin, "", "Slides between notes (ACID: only into tied notes)"); b.sound();
    b.knob ("dec", "DECAY", 0.05f, 3.0f, 0.5f, 10, 2, Map::Exp, "s"); b.sound(); b.macro (mDecay, 0.5f);
    struct Pos { int c, r; };
    static const Pos at[9] = { { 0, 4 }, { 2, 4 }, { 4, 4 }, { 6, 4 }, { 8, 4 }, { 10, 4 }, { 0, 6 }, { 2, 6 }, { 4, 6 } };
    b.knob ("pitch", "PITCH", -24, 24, -12, at[0].c, at[0].r, Map::Int, " st"); b.macro (mPitch, 0.3f);
    b.knob ("ptn", "PATTERN", 0, 1, 0.52f, at[1].c, at[1].r); b.macro (mMesoC, 0.5f);
    b.knob ("range", "RANGE", 0, 1, 0.2f, at[2].c, at[2].r); b.macro (mRange, 0.5f);
    b.knob ("inten", "INTENSITY", 0, 1, 0.5f, at[3].c, at[3].r, Map::Lin, "", "Note density (the TB-3PO density idea)"); b.macro (mMesoI, 0.5f);
    b.sel ("tcyc", "TRIG CYCLE", kTc, 8, at[4].c, at[4].r); b.macro (mMicroC, 0.4f);
    b.knob ("mptn", "MOD PTN", 0, 1, 0.5f, at[5].c, at[5].r); b.macro (mModulation, 0.3f);
    b.sel ("phrase", "PHRASE", kPhr, 1, at[6].c, at[6].r); b.macro (mMacroC, 0.4f);
    b.sel ("meter", "METER", kMeter, 2, at[7].c, at[7].r);
    b.knob ("mask", "MASK", 0, 1, 0.1f, at[8].c, at[8].r); b.macro (mMacroI, 0.5f);
    b.custom (0, 6, 6, 6, 2);
    b.organic (0);
    addGeneratorPorts (b);
    return b.t;
}
class Undertow : public Generator
{
public:
    Undertow() : Generator (spec()) { for (auto* k : { "pitch", "ptn", "range", "inten", "tcyc", "mptn", "phrase", "meter", "mask" }) idx.push_back (spec().param (k)); }
    static const TypeSpec& spec() { static TypeSpec t = specUndertow(); return t; }
    MelodyParams melody (bool live) const
    {
        auto v = [this, live] (int i) { return live ? spec().toPlain (i, effNorm (i)) : getPlain (i); };
        auto nv = [this, live] (int i) { return live ? effNorm (i) : getNorm (i); };
        MelodyParams m;
        m.seed = seedOf (nv (idx[1]), 301u); m.modSeed = seedOf (nv (idx[5]), 302u);
        m.range = v (idx[2]); m.intensity = v (idx[3]); m.tcyc = kTcV[(int) v (idx[4])]; m.bars = 1 << (int) v (idx[6]); m.meter = 2 + (int) v (idx[7]); m.mask = v (idx[8]);
        return m;
    }
    int previewSteps() const override { return 16; }
    float preview (int64_t s, int& degree) const override { const auto n = melodyAt (melody (false), s); degree = n.degree; return n.on ? n.vel : 0.0f; }
    float hitAt (int64_t s16) const override { const auto n = melodyAt (melody (true), s16); return n.on ? n.vel : 0.0f; }
protected:
    void step (int64_t s) override
    {
        if (s < 0) { cur = melodyAt (melody (true), -1 - s); cur.on = true; return; }
        Note n = melodyAt (melody (true), s);
        disp[0].store ((float) (s % 16), std::memory_order_relaxed);
        Hit h[2] = { { 0.0f, n.vel } };
        if (link (s, h, n.on ? 1 : 0, 303u) > 0) { if (! n.on) { n.on = true; n.vel = h[0].vel; } tied = cur.len > 1.5f; cur = n; addHit (0.0f, n.vel, s); }
    }
    void voice (float vel, int64_t, bool) override
    {
        const float midi = 48.0f + c (idx[0]) + ctx->degreeSemis (cur.degree) + 12.0f * pitchIn;
        target = (float) midiHz (midi);
        const int mode = (int) c (P ("mode"));
        const bool slide = mode == 2 ? tied : true;
        if (amp.v <= 0.001f || ! slide || c (P ("glide")) < 0.01f) freq = target;
        const bool accent = vel > 0.85f;
        amp.trigger (0.002f, c (P ("dec")) * (mode == 2 ? 0.6f : 1.0f), sr);
        fenv.trigger (0.001f, c (P ("dec")) * 0.5f, sr);
        g = vel; acc = accent ? 1.0f : 0.5f; nmod = cur.mod;
        outValues ((midi - 60.0f) / 12.0f, vel, 0.5f + 0.5f * cur.mod);
        disp[1].store (midi, std::memory_order_relaxed);
    }
    void render (float& l, float& r) override
    {
        const float e = amp.tick(), fe = fenv.tick();
        if (e <= 0.0f && amp.st == 0) { l = r = 0; return; }
        const int mode = (int) c (P ("mode"));
        const float gl = c (P ("glide"));
        freq += (target - freq) * (gl < 0.01f ? 1.0f : 1.0f - std::exp (-1.0f / (gl * gl * 0.25f * sr + 1.0f)));
        const float det = c (P ("det")) * (mode == 2 ? 0.0f : 0.012f);
        auto saw = [this] (float& ph, float f) { const float dt = f / sr; ph += dt; ph -= std::floor (ph); return 2.0f * ph - 1.0f - polyBlep (ph, dt); };
        float x = saw (p1, freq * (1.0f + det)) + saw (p2, freq * (1.0f - det)) * (mode == 2 ? 0.0f : 1.0f);
        if (mode == 2) x = x * 0.7f + (p1 < 0.5f ? 0.5f : -0.5f);
        // wobble: a sine locked to the beat grid
        const int mv = (int) c (P ("move"));
        float wob = 0.0f;
        if (mv > 0 && ctx->pos16PerSample > 0)
        {
            const double beats = (ctx->pos16 + ctx->pos16PerSample * kk) / 4.0;
            wob = std::sin ((float) (beats / kMoveBeats[mv]) * kTwoPi + nmod * 2.0f);
        }
        const float ma = c (P ("mamt"));
        // fold (neuro): sine folder driven by the wobble
        const float fold = c (P ("fold")) * (mode == 1 ? 1.0f : 0.4f) * (1.0f + ma * wob);
        if (fold > 0.01f) x = std::sin (x * (1.0f + fold * 5.0f) * kPi * 0.5f);
        sp += freq * 0.5f / sr; sp -= std::floor (sp);
        const float subS = std::sin (kTwoPi * sp) * c (P ("sub"));
        if (++kk2 >= 8)
        {
            kk2 = 0;
            const float oct = c (P ("env")) * 5.0f * fe * acc + ma * wob * 2.0f + nmod * 0.5f;
            const float fc = clampf (c (P ("cut")) * std::exp2 (oct), 25.0f, sr * 0.45f);
            f1.set (fc, 0.6f, sr); f2.set (fc, 0.6f + c (P ("res")) * (mode == 2 ? 14.0f : 6.0f), sr);
        }
        float y = f2.lp (f1.lp (x * 0.5f)) + subS * 0.7f;
        const float d = 1.0f + c (P ("drive")) * 6.0f;
        y = fastTanh (y * d) / std::sqrt (d) * e * g * 1.3f;
        l = r = y;
        ++kk;
    }
    void block (const Ctx& cx) override { Generator::block (cx); kk = 0; }
private:
    int P (const char* k) const { return spec().param (k); }
    std::vector<int> idx;
    Note cur; bool tied = false;
    AD amp, fenv; Svf f1, f2;
    float freq = 55, target = 55, p1 = 0, p2 = 0.37f, sp = 0, g = 0, acc = 0.5f, nmod = 0; int kk = 0, kk2 = 99;
};
} // namespace

std::unique_ptr<Module> createInstr2 (const std::string& id)
{
    if (id == "UNDERTOW") return std::make_unique<Undertow>();
    return nullptr;
}
void instr2Specs (std::vector<const TypeSpec*>& v) { v.push_back (&Undertow::spec()); }
} // namespace nms
