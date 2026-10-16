// SEMI port: FLOATER Source/Presets.cpp (eba7532), JUCE types replaced by std ones; the patches are unchanged.
// Factory patches. Each is written the way a floater case gets patched: the clock and
// the lanes first, then the modules, then what the CV moves, then the effects.
#include "Case.h"

namespace
{
using namespace flt;

struct B
{
    explicit B (const char* n, const char* c = "FLOOR") : name (n), category (c) {}
    std::string name, category;
    std::map<std::string, float> v;

    B& set (const std::string& id, float x) { v[id] = x; return *this; }
    B& clock (float bpmUnused, float swing, int rotate, int loopChoice, int seed, int bmax, int bshape, int scale)
    {
        (void) bpmUnused;
        return set (ids::swing, swing).set (ids::rotate, (float) rotate).set (ids::loop, (float) loopChoice).set (ids::seed, (float) seed)
              .set (ids::bmax, (float) bmax).set (ids::bshape, (float) bshape).set (ids::scale, (float) scale);
    }
    B& lane (int i, int steps, int hits, int offs, int div, float chance, float burst, bool on = true)
    {
        return set (ids::lon (i), on ? 1.0f : 0.0f).set (ids::steps (i), (float) steps).set (ids::hits (i), (float) hits).set (ids::offs (i), (float) offs)
              .set (ids::div (i), (float) div).set (ids::chance (i), chance).set (ids::burst (i), burst);
    }
    B& voice (int i, float tune, float decay, float color, float lvl, float pan, float send)
    {
        return set (ids::tune (i), tune).set (ids::decay (i), decay).set (ids::color (i), color).set (ids::lvl (i), lvl).set (ids::pan (i), pan).set (ids::send (i), send);
    }
    B& scat (float size, float dens, float pitch, float spread, float feed, float mix)
    {
        return set (ids::ssize, size).set (ids::sdens, dens).set (ids::spitch, pitch).set (ids::sspread, spread).set (ids::sfeed, feed).set (ids::smix, mix);
    }
    B& src (int s, float rate, float depth) { return set (ids::rate (s), rate).set (ids::depth (s), depth); }
    B& pin (int s, int d, int sign) { return set (ids::pin (s, d), (float) sign); }
    B& delay (int div, float fb, float brk, float mix) { return set (ids::ddiv, (float) div).set (ids::dfb, fb).set (ids::dbreak, brk).set (ids::dmix, mix); }
    B& space (float size, float decay, float damp, float mix, int pgm) { return set (ids::vsize, size).set (ids::vdecay, decay).set (ids::vdamp, damp).set (ids::vmix, mix).set (ids::pgm, (float) pgm); }
    // the Drum Buss: compressor on/off, character (0 soft, 1 medium, 2 hard), drive, crunch, transients, boom, level dB
    B& bus (bool comp, int chr, float drive, float crunch, float trans, float boom, float level)
    {
        return set (ids::bcomp, comp ? 1.0f : 0.0f).set (ids::bchar, (float) chr).set (ids::drive, drive).set (ids::crunch, crunch)
              .set (ids::trans, trans).set (ids::boom, boom).set (ids::level, level);
    }
    // a Schwung module in a slot: 0 = the slot's own voice, 1..24 = the Plaits engines in menu order
    B& model (int i, int m, float harm, float morph) { return set (ids::model (i), (float) m).set (ids::harm (i), harm).set (ids::morph (i), morph); }
    B& hm (int i, float harm, float morph) { return set (ids::harm (i), harm).set (ids::morph (i), morph); }
    B& clouds (int mode, float position, float texture, int pitch) { return set (ids::smodel, (float) mode).set (ids::spos, position).set (ids::stex, texture).set (ids::scpitch, (float) pitch); }
    B& cloudseed() { return set (ids::vmodel, 1.0f); }
    B& turns (float t, bool wall) { return set (ids::turns, t).set (ids::wall, wall ? 1.0f : 0.0f); }
    // a drawn line ("x...x...") instead of the Euclidean pattern; its length sets STEPS
    B& draw (int i, const char* line)
    {
        uint32_t m = 0;
        int n = 0;
        for (; line[n] != 0 && n < 32; ++n) if (line[n] == 'x') m |= 1u << n;
        return set (ids::draw (i), 1.0f).set (ids::steps (i), (float) n).set (ids::patLo (i), (float) (m & 0xffffu)).set (ids::patHi (i), (float) (m >> 16));
    }
    // a Braids shape in a slot, by its name
    B& braids (int i, const char* name, float timbre, float color, float filter)
    {
        int idx = 0;
        for (int k = 0; k < (int) flt::braidsModels().size(); ++k) if (std::string (flt::braidsModels()[(size_t) k].name) == name) idx = k;
        return set (ids::model (i), (float) (flt::kFirstBraids + idx)).set (ids::color (i), timbre).set (ids::harm (i), color).set (ids::morph (i), filter);
    }
    // ZIGZAG's break style (0 zigzag, 1 roll, 2 reverse, 3 tape stop) and machine (0 clean, 1 S950, 2 S1000, 3 Paula)
    B& zig (int style, int machine, float rate) { return set (ids::dstyle, (float) style).set (ids::dmach, (float) machine).set (ids::drate, rate); }
    // the SMR: mix, freq, q, spread, glide, scale, rotated by lane (0 off, 1..8)
    B& smr (float mix, float freq, float q, float spread, float glide, int scale, int rotBy)
    {
        return set (ids::mmix, mix).set (ids::mfreq, freq).set (ids::mq, q).set (ids::mspread, spread).set (ids::mglide, glide)
              .set (ids::mscale, (float) scale).set (ids::mrot, (float) rotBy);
    }
    // ---- v0.4: the sequencing
    B& lockv (int i, float lock) { return set (ids::lock (i), lock); }
    B& vary (int i, float x) { return set (ids::vary (i), x); }
    B& logic (int i, int op, int other) { return set (ids::logic (i), (float) (1 + op * 8 + other)); }
    B& cond (int i, int step, int c) { return set (ids::cond (i, step), (float) c); }
    // step locks: which 0 tune, 1 colour, 2 decay; one value per character of `steps` ('.' = none, digits -4..+4 as 0..8 around '4')
    B& locks (int i, int which, const char* steps)
    {
        for (int st = 0; steps[st] != 0 && st < 32; ++st)
            if (steps[st] >= '0' && steps[st] <= '8') set (ids::plock (i, which, st), (float) (steps[st] - '4') / 8.0f);
        return *this;
    }
    B& seq (std::initializer_list<float> values, int len, int x, int y, float glide)
    {
        int k = 0;
        for (auto x0 : values) if (k < 16) set (ids::seqv (k++), x0);
        return set (ids::seqlen, (float) len).set (ids::seqx, (float) x).set (ids::seqy, (float) y).set (ids::seqglide, glide);
    }
    // ---- v0.4: the sound design
    B& xm (int slot, int type, int src, float amt) { return set (ids::xmod (slot), (float) type).set (ids::xsrc (slot), (float) src).set (ids::xamt (slot), amt); }
    B& fb (float amount, int time) { return set (ids::fdbk, amount).set (ids::fdbkt, (float) time); }
    B& reel (float chaos, int len, int gene, float slide, float varispeed, float morph, float split, float duck, bool anchor)
    {
        return set (ids::rchaos, chaos).set (ids::rlen, (float) len).set (ids::rgene, (float) gene).set (ids::rslide, slide).set (ids::rvs, varispeed)
              .set (ids::rmorph, morph).set (ids::rsplit, split).set (ids::rduck, duck).set (ids::ranchor, anchor ? 1.0f : 0.0f);
    }
    B& organize (float org, int by) { return set (ids::rorg, org).set (ids::rorgby, (float) by); }
    // ---- v0.5: the DRONE (level, pitch from the key, ratio index, FM B>A, FM A>B, ring, sync, fold, shape B, bright, chaos)
    B& drone (float level, int pitch, int ratio, float fmab, float fmba, float ring, float sync, float fold, float shape, float bright, float chaos)
    {
        return set (ids::dlevel, level).set (ids::dpitch, (float) pitch).set (ids::dratio, (float) ratio).set (ids::dfmab, fmab).set (ids::dfmba, fmba)
              .set (ids::dring, ring).set (ids::dsync, sync).set (ids::dfold, fold).set (ids::dshape, shape).set (ids::dbright, bright).set (ids::dchaos, chaos);
    }
    B& dact (int lane, int act) { return set (ids::dact (lane), (float) act); }
    B& dmix (float send, float duck) { return set (ids::dsend, send).set (ids::dduck, duck); }
    B& dcv (int pitchCv, int fmCv, int foldCv) { return set (ids::dpcv, (float) pitchCv).set (ids::dfcv, (float) fmCv).set (ids::dfoldcv, (float) foldCv); }
    B& arcd (int len, float shape, float amount) { return set (ids::arclen, (float) len).set (ids::arcshape, shape).set (ids::darc, amount); }
    B& res (float mix, float decay, float bright) { return set (ids::rmix, mix).set (ids::rdecay, decay).set (ids::rbright, bright); }
    B& sustain (int slot) { return set (ids::sus (slot), 1.0f); }
    B& beds (bool freeze, bool infinite) { return set (ids::sfreeze, freeze ? 1.0f : 0.0f).set (ids::vinf, infinite ? 1.0f : 0.0f); }
    // the lanes that go round the reel, bit i = lane i
    B& protect (int mask) { for (int i = 0; i < kLanes; ++i) set (ids::rprot (i), (mask >> i) & 1 ? 1.0f : 0.0f); return *this; }
    Case::Preset done() const { return { name, category, v }; }
};
}

const std::vector<std::string>& Case::presetCategories()
{
    static const std::vector<std::string> c { "FLOOR", "DEVINE", "SCHWUNG", "SEQUENCE", "REEL", "DRONE", "FREE", "TEXTURE", "GLITCH" };
    return c;
}

const std::vector<Case::Preset>& Case::factoryPresets()
{
    static const std::vector<Preset> presets = []
    {
        std::vector<Preset> a;

        // 1. the default: a little of everything, one floor case
        a.push_back (B { "Floor Patch" }
            .clock (150, 0.0f, 0, 2, 41, 5, 0, 0)
            .lane (kick, 16, 3, 0, 1, 1.0f, 0.05f).lane (mesh, 16, 2, 4, 1, 1.0f, 0.15f).lane (fm, 16, 5, 2, 1, 0.7f, 0.25f)
            .lane (opto, 12, 5, 0, 1, 0.6f, 0.2f).lane (lantern, 16, 11, 0, 1, 0.85f, 0.4f).lane (zap, 16, 3, 3, 1, 0.45f, 0.3f)
            .lane (strike, 16, 3, 0, 2, 0.75f, 0.0f).lane (scatter, 16, 2, 6, 4, 0.8f, 0.0f)
            .voice (kick, 0.35f, 0.4f, 0.45f, 0.86f, 0.0f, 0.05f).voice (mesh, 0.5f, 0.35f, 0.55f, 0.74f, 0.0f, 0.25f)
            .voice (fm, 0.55f, 0.25f, 0.45f, 0.66f, -0.35f, 0.35f).voice (opto, 0.45f, 0.45f, 0.4f, 0.7f, 0.4f, 0.3f)
            .voice (lantern, 0.55f, 0.2f, 0.45f, 0.6f, 0.25f, 0.15f).voice (zap, 0.4f, 0.25f, 0.5f, 0.58f, -0.5f, 0.4f)
            .voice (strike, 0.45f, 0.55f, 0.55f, 0.62f, 0.15f, 0.45f)
            .scat (0.4f, 0.55f, 0.35f, 0.75f, 0.1f, 0.5f)
            .src (smooth, 0.4f, 0.6f).src (stepped, 0.6f, 0.7f).src (pong, 0.45f, 0.7f).src (lfo, 0.25f, 0.35f).src (forge, 0.5f, 0.6f).src (chaos, 0.5f, 0.5f)
            .pin (pong, fmIndex, 1).pin (stepped, strNote, 1).pin (smooth, optoColor, 1).pin (forge, kPitch, 1).pin (chaos, zapSweep, -1).pin (lfo, dlyTime, 1)
            .delay (4, 0.45f, 0.3f, 0.28f).space (0.6f, 0.5f, 0.4f, 0.22f, 2).bus (false, 1, 0.14f, 0.08f, 0.12f, 0.15f, -3.0f).done());

        // 2. slow scattered gates, bouncing-ball CV everywhere, pentatonic strings and granular dust between the beats
        a.push_back (B { "Bouncing Ball Scatter", "TEXTURE" }
            .clock (110, 0.0f, 0, 0, 7, 6, 0, 1)
            .lane (kick, 16, 3, 0, 1, 0.9f, 0.0f).lane (mesh, 16, 1, 8, 2, 0.85f, 0.2f).lane (fm, 13, 5, 0, 1, 0.55f, 0.55f)
            .lane (opto, 16, 4, 2, 2, 0.5f, 0.3f).lane (lantern, 16, 6, 1, 1, 0.6f, 0.5f).lane (zap, 7, 2, 0, 3, 0.4f, 0.4f)
            .lane (strike, 16, 5, 0, 2, 0.7f, 0.1f).lane (scatter, 16, 5, 1, 1, 0.75f, 0.2f)
            .voice (kick, 0.25f, 0.55f, 0.3f, 0.82f, 0.0f, 0.1f).voice (mesh, 0.4f, 0.4f, 0.35f, 0.6f, 0.0f, 0.4f)
            .voice (fm, 0.62f, 0.2f, 0.55f, 0.66f, -0.45f, 0.45f).voice (opto, 0.55f, 0.6f, 0.3f, 0.65f, 0.5f, 0.4f)
            .voice (lantern, 0.6f, 0.15f, 0.3f, 0.5f, 0.35f, 0.3f).voice (zap, 0.6f, 0.2f, 0.7f, 0.45f, -0.6f, 0.55f)
            .voice (strike, 0.5f, 0.75f, 0.65f, 0.72f, 0.2f, 0.55f)
            .scat (0.25f, 0.8f, 0.7f, 1.0f, 0.25f, 0.65f)
            .src (pong, 0.3f, 0.9f).src (stepped, 0.75f, 0.8f).src (smooth, 0.3f, 0.5f).src (lfo, 0.15f, 0.3f)
            .pin (pong, fmIndex, 1).pin (pong, zapSweep, -1).pin (pong, scatSize, 1).pin (stepped, strNote, 1).pin (smooth, optoColor, 1).pin (lfo, strBright, 1)
            .delay (2, 0.5f, 0.15f, 0.3f).space (0.8f, 0.7f, 0.35f, 0.32f, 0).bus (false, 1, 0.07f, 0.09f, 0.07f, 0.10f, -3.0f).done());

        // 3. vactrol percussion, the case clocked slow; noise hats through a gate; a Karplus-like delay broken by bursts
        a.push_back (B { "Opto Drift", "TEXTURE" }
            .clock (96, 0.12f, 0, 0, 1906, 5, 0, 0)
            .lane (kick, 16, 4, 0, 1, 1.0f, 0.0f).lane (mesh, 16, 0, 0, 1, 1.0f, 0.0f, false).lane (fm, 16, 3, 5, 2, 0.6f, 0.3f)
            .lane (opto, 16, 7, 1, 1, 0.75f, 0.35f).lane (lantern, 16, 9, 2, 1, 0.8f, 0.3f).lane (zap, 16, 2, 7, 2, 0.5f, 0.2f)
            .lane (strike, 16, 0, 0, 1, 1.0f, 0.0f, false).lane (scatter, 16, 1, 4, 8, 0.6f, 0.0f)
            .voice (kick, 0.3f, 0.5f, 0.25f, 0.85f, 0.0f, 0.0f).voice (fm, 0.3f, 0.35f, 0.3f, 0.55f, -0.3f, 0.3f)
            .voice (opto, 0.4f, 0.6f, 0.45f, 0.8f, 0.0f, 0.5f).voice (lantern, 0.45f, 0.12f, 0.1f, 0.6f, 0.3f, 0.2f)
            .voice (zap, 0.3f, 0.3f, 0.35f, 0.5f, 0.45f, 0.4f)
            .scat (0.6f, 0.3f, 0.0f, 0.5f, 0.0f, 0.35f)
            .src (smooth, 0.25f, 0.7f).src (stepped, 0.5f, 0.6f).src (chaos, 0.6f, 0.6f)
            .pin (smooth, optoColor, 1).pin (stepped, dlyTime, 1).pin (chaos, hatDecay, 1).pin (smooth, fmRatio, -1)
            .delay (2, 0.7f, 0.55f, 0.35f).space (0.7f, 0.55f, 0.5f, 0.25f, 0).bus (false, 1, 0.10f, 0.04f, 0.10f, 0.18f, -3.0f).done());

        // 4. a mutant kick with fast function bursts on its pitch and decay, mesh snare, pitch-swept delays, big reverb
        a.push_back (B { "Mesh Drums" }
            .clock (128, 0.0f, 0, 2, 175, 4, 1, 0)
            .lane (kick, 16, 5, 0, 1, 0.95f, 0.1f).lane (mesh, 16, 2, 4, 1, 1.0f, 0.2f).lane (fm, 16, 0, 0, 1, 1.0f, 0.0f, false)
            .lane (opto, 16, 3, 7, 2, 0.5f, 0.0f).lane (lantern, 16, 8, 2, 1, 0.9f, 0.25f).lane (zap, 16, 2, 11, 1, 0.5f, 0.4f)
            .lane (strike, 16, 0, 0, 1, 1.0f, 0.0f, false).lane (scatter, 16, 1, 12, 2, 0.7f, 0.0f)
            .voice (kick, 0.4f, 0.45f, 0.75f, 0.9f, 0.0f, 0.1f).voice (mesh, 0.55f, 0.45f, 0.6f, 0.8f, 0.0f, 0.45f)
            .voice (opto, 0.3f, 0.4f, 0.5f, 0.55f, 0.4f, 0.4f).voice (lantern, 0.5f, 0.25f, 0.6f, 0.6f, -0.25f, 0.2f)
            .voice (zap, 0.45f, 0.2f, 0.4f, 0.5f, 0.5f, 0.5f)
            .scat (0.3f, 0.4f, 0.5f, 0.8f, 0.0f, 0.35f)
            .src (forge, 0.7f, 0.85f).src (lfo, 0.2f, 0.6f).src (stepped, 0.5f, 0.5f)
            .pin (forge, kPitch, 1).pin (forge, kDecay, -1).pin (lfo, dlyTime, 1).pin (stepped, meshTone, 1)
            .delay (3, 0.55f, 0.2f, 0.32f).space (0.95f, 0.8f, 0.3f, 0.3f, 0).bus (true, 1, 0.21f, 0.07f, 0.18f, 0.20f, -3.0f).done());

        // 5. strummed strings on a quantised sequence, soft drums, the scatter as a shimmer
        a.push_back (B { "Strummed Resonator", "TEXTURE" }
            .clock (120, 0.0f, 0, 3, 1231, 3, 2, 2)
            .lane (kick, 16, 2, 0, 1, 1.0f, 0.0f).lane (mesh, 16, 1, 8, 2, 1.0f, 0.0f).lane (fm, 16, 0, 0, 1, 1.0f, 0.0f, false)
            .lane (opto, 16, 0, 0, 1, 1.0f, 0.0f, false).lane (lantern, 16, 8, 0, 1, 0.8f, 0.1f).lane (zap, 16, 0, 0, 1, 1.0f, 0.0f, false)
            .lane (strike, 16, 7, 0, 1, 0.85f, 0.3f).lane (scatter, 16, 2, 2, 2, 0.8f, 0.0f)
            .voice (kick, 0.3f, 0.5f, 0.2f, 0.72f, 0.0f, 0.1f).voice (mesh, 0.45f, 0.4f, 0.4f, 0.55f, 0.0f, 0.5f)
            .voice (lantern, 0.4f, 0.18f, 0.2f, 0.42f, 0.35f, 0.3f).voice (strike, 0.55f, 0.85f, 0.7f, 0.85f, 0.0f, 0.55f)
            .scat (0.65f, 0.7f, 0.65f, 1.0f, 0.3f, 0.55f)
            .src (stepped, 0.8f, 0.9f).src (smooth, 0.3f, 0.6f).src (lfo, 0.1f, 0.4f)
            .pin (stepped, strNote, 1).pin (smooth, strBright, 1).pin (lfo, scatSize, 1)
            .delay (4, 0.4f, 0.0f, 0.25f).space (0.85f, 0.8f, 0.25f, 0.4f, 0).bus (false, 1, 0.03f, 0.11f, 0.05f, 0.10f, -3.0f).done());

        // 6. the delay as an instrument: high feedback, the bursts breaking its clock, zaps and chaos
        a.push_back (B { "d0 Zigzag", "GLITCH" }
            .clock (140, 0.0f, 2, 0, 404, 7, 0, 5)
            .lane (kick, 16, 4, 0, 1, 1.0f, 0.0f).lane (mesh, 16, 2, 4, 1, 0.9f, 0.3f).lane (fm, 16, 6, 1, 1, 0.6f, 0.5f)
            .lane (opto, 16, 3, 3, 1, 0.5f, 0.4f).lane (lantern, 16, 6, 2, 2, 0.6f, 0.5f).lane (zap, 16, 7, 0, 1, 0.6f, 0.6f)
            .lane (strike, 16, 0, 0, 1, 1.0f, 0.0f, false).lane (scatter, 16, 2, 0, 4, 0.5f, 0.0f)
            .voice (kick, 0.45f, 0.3f, 0.6f, 0.82f, 0.0f, 0.0f).voice (mesh, 0.6f, 0.25f, 0.7f, 0.65f, 0.0f, 0.5f)
            .voice (fm, 0.7f, 0.15f, 0.8f, 0.55f, -0.4f, 0.6f).voice (opto, 0.6f, 0.3f, 0.7f, 0.55f, 0.4f, 0.55f)
            .voice (lantern, 0.65f, 0.1f, 0.75f, 0.5f, 0.2f, 0.4f).voice (zap, 0.5f, 0.15f, 0.8f, 0.6f, -0.2f, 0.7f)
            .scat (0.15f, 0.6f, 0.4f, 0.9f, 0.0f, 0.35f)
            .src (chaos, 0.8f, 0.9f).src (stepped, 0.9f, 0.7f).src (pong, 0.6f, 0.6f)
            .pin (chaos, zapSweep, 1).pin (chaos, fmRatio, 1).pin (stepped, dlyTime, -1).pin (pong, fmIndex, 1)
            .delay (1, 0.78f, 0.85f, 0.42f).space (0.4f, 0.4f, 0.5f, 0.15f, 1).bus (true, 1, 0.24f, 0.07f, 0.15f, 0.10f, -3.0f).done());

        // 7. the divider as the composition: every lane on its own division, the rotation moved by CHAOS
        a.push_back (B { "Rotating Divider" }
            .clock (160, 0.0f, 0, 3, 88, 4, 0, 4)
            .lane (kick, 16, 4, 0, 1, 1.0f, 0.0f).lane (mesh, 16, 3, 4, 2, 1.0f, 0.1f).lane (fm, 16, 5, 0, 3, 0.8f, 0.2f)
            .lane (opto, 16, 7, 1, 4, 0.8f, 0.2f).lane (lantern, 16, 12, 0, 1, 0.9f, 0.3f).lane (zap, 16, 3, 2, 5, 0.7f, 0.3f)
            .lane (strike, 16, 4, 0, 6, 0.9f, 0.0f).lane (scatter, 16, 2, 0, 8, 0.9f, 0.0f)
            .voice (kick, 0.35f, 0.35f, 0.5f, 0.85f, 0.0f, 0.05f).voice (mesh, 0.5f, 0.3f, 0.5f, 0.7f, 0.0f, 0.3f)
            .voice (fm, 0.5f, 0.3f, 0.5f, 0.6f, -0.5f, 0.35f).voice (opto, 0.5f, 0.4f, 0.55f, 0.6f, 0.5f, 0.35f)
            .voice (lantern, 0.5f, 0.15f, 0.5f, 0.55f, 0.2f, 0.15f).voice (zap, 0.35f, 0.25f, 0.6f, 0.5f, -0.3f, 0.45f)
            .voice (strike, 0.4f, 0.6f, 0.5f, 0.6f, 0.3f, 0.45f)
            .scat (0.35f, 0.5f, 0.3f, 0.7f, 0.0f, 0.45f)
            .src (chaos, 0.2f, 0.9f).src (smooth, 0.4f, 0.5f).src (pong, 0.4f, 0.5f)
            .pin (chaos, rotate, 1).pin (smooth, fmIndex, 1).pin (pong, optoColor, 1).pin (smooth, strNote, 1)
            .delay (3, 0.4f, 0.2f, 0.25f).space (0.6f, 0.55f, 0.4f, 0.22f, 3).bus (false, 1, 0.14f, 0.07f, 0.12f, 0.15f, -3.0f).done());

        // 8. fast, dense and broken: short decays, heavy bursts, everything slewed by the clock
        a.push_back (B { "Systik Glitch", "GLITCH" }
            .clock (174, 0.0f, 0, 1, 2016, 8, 0, 3)
            .lane (kick, 16, 5, 0, 1, 0.9f, 0.2f).lane (mesh, 16, 3, 4, 1, 0.9f, 0.4f).lane (fm, 16, 9, 1, 1, 0.6f, 0.6f)
            .lane (opto, 13, 6, 0, 1, 0.5f, 0.5f).lane (lantern, 16, 14, 0, 1, 0.8f, 0.6f).lane (zap, 16, 5, 3, 1, 0.5f, 0.7f)
            .lane (strike, 16, 2, 6, 2, 0.6f, 0.3f).lane (scatter, 16, 3, 1, 2, 0.6f, 0.3f)
            .voice (kick, 0.5f, 0.2f, 0.65f, 0.85f, 0.0f, 0.0f).voice (mesh, 0.65f, 0.15f, 0.75f, 0.7f, 0.0f, 0.3f)
            .voice (fm, 0.75f, 0.08f, 0.7f, 0.55f, -0.6f, 0.4f).voice (opto, 0.7f, 0.2f, 0.85f, 0.5f, 0.6f, 0.35f)
            .voice (lantern, 0.7f, 0.06f, 0.8f, 0.5f, 0.15f, 0.2f).voice (zap, 0.6f, 0.1f, 0.3f, 0.55f, -0.3f, 0.45f)
            .voice (strike, 0.6f, 0.4f, 0.8f, 0.5f, 0.4f, 0.5f)
            .scat (0.1f, 0.85f, 0.6f, 1.0f, 0.2f, 0.45f)
            .src (stepped, 1.0f, 0.9f).src (chaos, 1.0f, 0.8f).src (forge, 0.9f, 0.7f).src (pong, 0.8f, 0.7f)
            .pin (stepped, fmRatio, 1).pin (chaos, fmIndex, 1).pin (forge, kPitch, 1).pin (pong, hatDecay, 1).pin (stepped, zapSweep, -1).pin (chaos, scatSize, 1)
            .delay (0, 0.55f, 0.7f, 0.3f).space (0.35f, 0.35f, 0.5f, 0.15f, 4).bus (true, 1, 0.28f, 0.08f, 0.20f, 0.18f, -3.0f).done());

        // 9. every slot a Plaits: the analogue drum models, a modal voice, particles; Clouds and CloudSeed after them
        a.push_back (B { "Plaits Floor", "SCHWUNG" }
            .clock (150, 0.0f, 0, 2, 309, 5, 0, 0)
            .lane (kick, 16, 4, 0, 1, 1.0f, 0.05f).lane (mesh, 16, 2, 4, 1, 1.0f, 0.2f).lane (fm, 16, 5, 3, 1, 0.6f, 0.3f)
            .lane (opto, 12, 4, 1, 2, 0.6f, 0.2f).lane (lantern, 16, 11, 0, 1, 0.85f, 0.45f).lane (zap, 16, 3, 5, 1, 0.45f, 0.4f)
            .lane (strike, 16, 3, 0, 2, 0.8f, 0.0f).lane (scatter, 16, 3, 2, 2, 0.8f, 0.2f)
            .voice (kick, 0.33f, 0.45f, 0.55f, 0.88f, 0.0f, 0.0f).model (kick, 1, 0.45f, 0.5f)
            .voice (mesh, 0.5f, 0.35f, 0.6f, 0.75f, 0.0f, 0.3f).model (mesh, 2, 0.55f, 0.45f)
            .voice (fm, 0.6f, 0.4f, 0.55f, 0.6f, -0.4f, 0.4f).model (fm, 4, 0.3f, 0.6f)
            .voice (opto, 0.45f, 0.5f, 0.35f, 0.6f, 0.45f, 0.35f).model (opto, 8, 0.6f, 0.55f)
            .voice (lantern, 0.6f, 0.25f, 0.55f, 0.6f, 0.25f, 0.15f).model (lantern, 3, 0.5f, 0.35f)
            .voice (zap, 0.5f, 0.3f, 0.5f, 0.55f, -0.55f, 0.45f).model (zap, 15, 0.4f, 0.6f)
            .voice (strike, 0.42f, 0.6f, 0.55f, 0.6f, 0.2f, 0.5f).model (strike, 5, 0.3f, 0.5f)
            .scat (0.45f, 0.5f, 0.3f, 0.9f, 0.15f, 0.55f).clouds (1, 0.4f, 0.55f, 0)
            .src (pong, 0.4f, 0.7f).src (stepped, 0.6f, 0.8f).src (forge, 0.6f, 0.6f).src (smooth, 0.3f, 0.5f)
            .pin (pong, fmIndex, 1).pin (stepped, strNote, 1).pin (forge, kPitch, 1).pin (smooth, optoColor, 1).pin (stepped, zapSweep, -1)
            .delay (3, 0.45f, 0.35f, 0.25f).space (0.65f, 0.5f, 0.35f, 0.24f, 2).cloudseed()
            .bus (true, 1, 0.17f, 0.06f, 0.15f, 0.2f, -3.0f).turns (0.4f, false).done());

        // 10. strummed strings into Clouds: lane 8 on its TRIG, its bursts on FREEZE; a long CloudSeed
        a.push_back (B { "Clouds Strum", "SCHWUNG" }
            .clock (118, 0.0f, 0, 3, 1215, 4, 2, 2)
            .lane (kick, 16, 2, 0, 1, 1.0f, 0.0f).lane (mesh, 16, 1, 8, 2, 1.0f, 0.0f).lane (fm, 16, 0, 0, 1, 1.0f, 0.0f, false)
            .lane (opto, 16, 3, 2, 2, 0.5f, 0.0f).lane (lantern, 16, 8, 0, 1, 0.8f, 0.1f).lane (zap, 16, 0, 0, 1, 1.0f, 0.0f, false)
            .lane (strike, 16, 7, 0, 1, 0.85f, 0.3f).lane (scatter, 16, 4, 1, 1, 0.8f, 0.35f)
            .voice (kick, 0.3f, 0.5f, 0.2f, 0.7f, 0.0f, 0.1f).voice (mesh, 0.45f, 0.4f, 0.4f, 0.55f, 0.0f, 0.5f)
            .voice (opto, 0.6f, 0.5f, 0.3f, 0.5f, 0.4f, 0.4f)
            .voice (lantern, 0.4f, 0.18f, 0.2f, 0.42f, 0.35f, 0.3f).voice (strike, 0.55f, 0.85f, 0.7f, 0.85f, 0.0f, 0.55f).hm (strike, 0.5f, 0.45f)
            .scat (0.6f, 0.5f, 0.3f, 1.0f, 0.35f, 0.65f).clouds (1, 0.6f, 0.7f, 12)
            .src (stepped, 0.8f, 0.9f).src (smooth, 0.3f, 0.6f).src (lfo, 0.1f, 0.4f)
            .pin (stepped, strNote, 1).pin (smooth, strBright, 1).pin (lfo, scatSize, 1)
            .delay (4, 0.4f, 0.0f, 0.2f).space (0.9f, 0.85f, 0.25f, 0.4f, 0).cloudseed()
            .bus (false, 0, 0.07f, 0.02f, 0.0f, 0.1f, -3.0f).done());

        // 11. slow random gates, an FM pluck, additive harmonics on the sequenced voice, grains between the beats,
        //     the reverb's program changed on every lane 8 hit
        a.push_back (B { "Variatic Scatter", "DEVINE" }
            .clock (104, 0.0f, 0, 0, 2015, 6, 0, 1)
            .lane (kick, 16, 3, 0, 1, 0.9f, 0.0f).lane (mesh, 16, 1, 8, 2, 0.8f, 0.2f).lane (fm, 13, 6, 0, 1, 0.5f, 0.55f)
            .lane (opto, 16, 3, 2, 2, 0.5f, 0.2f).lane (lantern, 16, 5, 1, 1, 0.6f, 0.5f).lane (zap, 7, 2, 0, 3, 0.4f, 0.4f)
            .lane (strike, 16, 6, 0, 2, 0.75f, 0.1f).lane (scatter, 16, 5, 1, 1, 0.7f, 0.25f)
            .voice (kick, 0.25f, 0.55f, 0.3f, 0.82f, 0.0f, 0.1f).model (kick, 1, 0.35f, 0.55f)
            .voice (mesh, 0.4f, 0.4f, 0.35f, 0.6f, 0.0f, 0.4f)
            .voice (fm, 0.62f, 0.25f, 0.6f, 0.66f, -0.45f, 0.45f).model (fm, 6, 0.4f, 0.3f)
            .voice (opto, 0.55f, 0.6f, 0.3f, 0.65f, 0.5f, 0.4f)
            .voice (lantern, 0.6f, 0.15f, 0.3f, 0.5f, 0.35f, 0.3f)
            .voice (zap, 0.6f, 0.2f, 0.7f, 0.45f, -0.6f, 0.55f)
            .voice (strike, 0.5f, 0.55f, 0.5f, 0.72f, 0.2f, 0.55f).model (strike, 7, 0.55f, 0.45f)
            .scat (0.3f, 0.6f, 0.5f, 1.0f, 0.2f, 0.6f).clouds (1, 0.7f, 0.4f, 7)
            .src (pong, 0.3f, 0.9f).src (stepped, 0.75f, 0.8f).src (smooth, 0.3f, 0.5f)
            .pin (pong, fmIndex, 1).pin (pong, zapSweep, -1).pin (pong, scatSize, 1).pin (stepped, strNote, 1).pin (smooth, strBright, 1)
            .delay (2, 0.5f, 0.2f, 0.3f).space (0.7f, 0.65f, 0.35f, 0.3f, 4).cloudseed()
            .bus (false, 1, 0.10f, 0.04f, 0.1f, 0.15f, -3.0f).done());

        // 12. 170 jungle on the floor case: the turns fill every 4 / 8 / 16 / 32, the wall before the big ones
        a.push_back (B { "Turnaround Floor" }
            .clock (170, 0.04f, 0, 2, 1704, 6, 0, 0)
            .lane (kick, 16, 3, 0, 1, 1.0f, 0.05f).lane (mesh, 16, 2, 4, 1, 1.0f, 0.25f).lane (fm, 16, 4, 3, 1, 0.5f, 0.3f)
            .lane (opto, 16, 3, 7, 1, 0.5f, 0.25f).lane (lantern, 16, 10, 0, 1, 0.8f, 0.4f).lane (zap, 16, 2, 11, 2, 0.5f, 0.5f)
            .lane (strike, 16, 2, 0, 4, 0.7f, 0.0f).lane (scatter, 16, 2, 6, 4, 0.7f, 0.0f)
            .voice (kick, 0.3f, 0.35f, 0.6f, 0.9f, 0.0f, 0.0f).model (kick, 1, 0.6f, 0.4f)
            .voice (mesh, 0.55f, 0.3f, 0.65f, 0.82f, 0.0f, 0.3f).hm (mesh, 0.7f, 0.55f)
            .voice (fm, 0.55f, 0.2f, 0.6f, 0.55f, -0.4f, 0.35f).voice (opto, 0.5f, 0.3f, 0.6f, 0.5f, 0.4f, 0.35f)
            .voice (lantern, 0.6f, 0.12f, 0.65f, 0.55f, 0.2f, 0.15f).model (lantern, 3, 0.55f, 0.3f)
            .voice (zap, 0.45f, 0.2f, 0.5f, 0.5f, -0.3f, 0.5f).voice (strike, 0.4f, 0.5f, 0.6f, 0.5f, 0.3f, 0.5f)
            .scat (0.25f, 0.6f, 0.4f, 0.9f, 0.0f, 0.4f)
            .src (forge, 0.7f, 0.8f).src (stepped, 0.8f, 0.7f).src (chaos, 0.6f, 0.6f)
            .pin (forge, kPitch, 1).pin (stepped, meshTone, 1).pin (chaos, zapSweep, 1).pin (stepped, dlyTime, -1)
            .delay (1, 0.5f, 0.5f, 0.28f).space (0.5f, 0.45f, 0.45f, 0.18f, 3)
            .bus (true, 1, 0.24f, 0.10f, 0.25f, 0.3f, -3.0f).turns (0.85f, true).done());

        // 13. "Ascension" (2015): slow random gate bursts, bouncing-ball CV on their timing, a Basimilus FM pluck,
        //     Rings into an RT-60 whose program a divider output changes, Clouds scattering between the beats,
        //     Braids in HARM playing the sequenced notes
        a.push_back (B { "Ascension", "DEVINE" }
            .clock (100, 0.0f, 0, 0, 2015, 6, 0, 0)
            .lane (kick, 16, 3, 0, 1, 0.9f, 0.1f).draw (kick, "x.........x.....")
            .lane (mesh, 16, 2, 4, 1, 0.9f, 0.15f).lane (fm, 16, 5, 1, 1, 0.55f, 0.6f)
            .lane (opto, 16, 3, 2, 2, 0.6f, 0.35f).lane (lantern, 16, 7, 1, 1, 0.55f, 0.55f).lane (zap, 13, 2, 0, 3, 0.35f, 0.4f)
            .lane (strike, 16, 6, 0, 2, 0.8f, 0.0f).lane (scatter, 16, 4, 0, 5, 0.85f, 0.3f)
            .voice (kick, 0.3f, 0.45f, 0.4f, 0.82f, 0.0f, 0.05f)
            .voice (mesh, 0.5f, 0.35f, 0.45f, 0.62f, 0.0f, 0.35f).hm (mesh, 0.55f, 0.5f)
            .voice (fm, 0.58f, 0.22f, 0.62f, 0.66f, -0.45f, 0.45f).hm (fm, 0.45f, 0.25f)
            .voice (opto, 0.5f, 0.6f, 0.55f, 0.62f, 0.45f, 0.6f).model (opto, 4, 0.4f, 0.55f)
            .voice (lantern, 0.6f, 0.15f, 0.3f, 0.48f, 0.35f, 0.25f)
            .voice (zap, 0.55f, 0.2f, 0.65f, 0.45f, -0.6f, 0.55f)
            .voice (strike, 0.48f, 0.5f, 0.45f, 0.7f, 0.15f, 0.5f).braids (strike, "HARM", 0.45f, 0.55f, 0.7f)
            .scat (0.25f, 0.45f, 0.4f, 1.0f, 0.15f, 0.55f).clouds (1, 0.6f, 0.45f, 0)
            .src (pong, 0.3f, 0.9f).src (stepped, 0.7f, 0.85f).src (smooth, 0.35f, 0.5f)
            .pin (pong, rotate, 1).pin (pong, fmIndex, 1).pin (stepped, strNote, 1).pin (smooth, strBright, 1).pin (pong, scatSize, -1)
            .delay (2, 0.45f, 0.2f, 0.22f).zig (0, 0, 0.5f).space (0.75f, 0.65f, 0.3f, 0.3f, 4).cloudseed()
            .bus (false, 1, 0.1f, 0.03f, 0.1f, 0.15f, -3.0f).done());

        // 14. "Harmonic Symmetry" (2015): Nord Drum kick and snare, noise hats through an Optomix on a clock divider
        //     a slow sine rotates, a bass, a Mysteron pluck on the RCD's 5th output, a tELHARMONIC lead in a major
        //     scale, and an SMR clocked by its own channel ringing into Clouds
        a.push_back (B { "Harmonic Symmetry", "DEVINE" }
            .clock (118, 0.0f, 0, 2, 816, 3, 1, 1)
            .lane (kick, 16, 2, 0, 1, 1.0f, 0.0f).draw (kick, "x.........x.....")
            .lane (mesh, 16, 2, 4, 1, 1.0f, 0.0f).draw (mesh, "....x.......x...")
            .lane (fm, 16, 3, 0, 5, 0.9f, 0.0f).lane (opto, 16, 4, 0, 2, 0.9f, 0.0f)
            .lane (lantern, 16, 12, 0, 1, 0.9f, 0.15f).lane (zap, 16, 0, 0, 1, 1.0f, 0.0f, false)
            .lane (strike, 16, 8, 0, 1, 0.85f, 0.0f).lane (scatter, 16, 2, 2, 4, 0.7f, 0.25f)
            .voice (kick, 0.3f, 0.5f, 0.35f, 0.85f, 0.0f, 0.05f).voice (mesh, 0.5f, 0.4f, 0.5f, 0.7f, 0.0f, 0.3f)
            .voice (fm, 0.5f, 0.55f, 0.5f, 0.55f, -0.3f, 0.45f).model (fm, 5, 0.3f, 0.45f)
            .voice (opto, 0.18f, 0.45f, 0.35f, 0.72f, 0.0f, 0.1f).model (opto, 10, 0.2f, 0.3f)
            .voice (lantern, 0.55f, 0.12f, 0.1f, 0.5f, 0.25f, 0.2f)
            .voice (strike, 0.6f, 0.55f, 0.55f, 0.58f, -0.2f, 0.55f).model (strike, 7, 0.6f, 0.45f)
            .scat (0.55f, 0.6f, 0.2f, 1.0f, 0.2f, 0.5f).clouds (1, 0.5f, 0.6f, 0)
            .src (lfo, 0.08f, 0.9f).src (stepped, 0.75f, 0.85f).src (smooth, 0.3f, 0.5f)
            .pin (lfo, rotate, 1).pin (stepped, strNote, 1).pin (smooth, scatSize, 1).pin (smooth, smrFreq, 1)
            .smr (0.45f, 0.35f, 0.82f, 0.25f, 0.4f, 1, opto + 1)
            .delay (4, 0.4f, 0.0f, 0.18f).space (0.85f, 0.8f, 0.3f, 0.36f, 0).cloudseed()
            .bus (false, 0, 0.08f, 0.02f, 0.05f, 0.1f, -3.0f).done());

        // 15. the broken delay: one sine through an Optomix struck by an SCM output, into a d0 in its Karplus-Strong
        //     setting whose time the Wogglebug's stepped CV moves, gate bursts on its clock
        a.push_back (B { "Broken Delay d0", "DEVINE" }
            .clock (126, 0.0f, 3, 0, 913, 7, 0, 3)
            .lane (kick, 16, 4, 0, 1, 1.0f, 0.0f).lane (mesh, 16, 0, 0, 1, 1.0f, 0.0f, false).lane (fm, 16, 0, 0, 1, 1.0f, 0.0f, false)
            .lane (opto, 16, 7, 2, 1, 0.7f, 0.6f).lane (lantern, 16, 9, 0, 1, 0.7f, 0.4f).lane (zap, 16, 3, 5, 2, 0.4f, 0.6f)
            .lane (strike, 16, 0, 0, 1, 1.0f, 0.0f, false).lane (scatter, 16, 0, 0, 1, 1.0f, 0.0f, false)
            .voice (kick, 0.32f, 0.4f, 0.55f, 0.85f, 0.0f, 0.0f)
            .voice (opto, 0.45f, 0.25f, 0.3f, 0.72f, 0.0f, 0.85f).hm (opto, 0.05f, 0.0f)
            .voice (lantern, 0.5f, 0.12f, 0.05f, 0.5f, 0.3f, 0.2f).voice (zap, 0.45f, 0.15f, 0.5f, 0.45f, -0.4f, 0.7f)
            .voice (strike, 0.42f, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f)
            .src (stepped, 0.85f, 0.8f).src (woggle, 0.55f, 0.7f).src (chaos, 0.7f, 0.6f)
            .pin (stepped, dlyTime, 1).pin (woggle, optoColor, 1).pin (chaos, zapSweep, 1)
            .delay (flt::kDelayKarplus, 0.9f, 0.75f, 0.5f).zig (0, 1, 0.55f).space (0.45f, 0.4f, 0.5f, 0.16f, 0)
            .bus (true, 1, 0.2f, 0.05f, 0.15f, 0.15f, -3.0f).done());

        // 16. "Vactrol Drift" (2013): everything clocked by a Wogglebug's clocked and chaotic burst outputs at a low tempo,
        //     a high-probability random channel into a gate delay, noise hats through an Optomix, sample percussion
        //     struck by a g0, a Space on "Corridors"
        a.push_back (B { "Vactrol Drift", "DEVINE" }
            .clock (84, 0.08f, 0, 0, 1310, 5, 0, 3)
            .lane (kick, 16, 4, 0, 1, 0.9f, 0.1f).lane (mesh, 16, 2, 4, 2, 0.7f, 0.3f).lane (fm, 16, 0, 0, 1, 1.0f, 0.0f, false)
            .lane (opto, 16, 6, 1, 1, 0.55f, 0.5f).lane (lantern, 16, 11, 0, 1, 0.65f, 0.5f).lane (zap, 16, 4, 3, 1, 0.3f, 0.6f)
            .lane (strike, 16, 0, 0, 1, 1.0f, 0.0f, false).lane (scatter, 16, 2, 6, 2, 0.5f, 0.3f)
            .voice (kick, 0.3f, 0.55f, 0.6f, 0.86f, 0.0f, 0.05f)
            .voice (mesh, 0.5f, 0.4f, 0.5f, 0.6f, 0.0f, 0.35f).braids (mesh, "DRUM", 0.5f, 0.45f, 0.75f)
            .voice (opto, 0.4f, 0.45f, 0.4f, 0.7f, 0.35f, 0.4f)
            .voice (lantern, 0.5f, 0.15f, 0.0f, 0.55f, -0.3f, 0.25f)
            .voice (zap, 0.5f, 0.25f, 0.5f, 0.5f, 0.5f, 0.5f).model (zap, 4, 0.6f, 0.35f)
            .scat (0.4f, 0.4f, 0.2f, 0.8f, 0.0f, 0.4f)
            .src (woggle, 0.35f, 0.8f).src (chaos, 0.65f, 0.7f).src (forge, 0.6f, 0.5f)
            .pin (woggle, optoColor, 1).pin (woggle, kPitch, -1).pin (chaos, hatDecay, 1).pin (forge, zapSweep, 1).pin (chaos, rotate, 1)
            .delay (4, 0.55f, 0.35f, 0.25f).zig (2, 0, 0.5f).space (0.8f, 0.7f, 0.45f, 0.3f, 2)
            .bus (false, 1, 0.12f, 0.03f, 0.1f, 0.12f, -3.0f).done());

        // 17. order and breakage at once: the groove plays, the REEL breaks everything above 220 Hz on the
        //     sixteenths, the kick and the snare never go on it, the first beat of every line comes back clean
        a.push_back (B { "Reel Order", "REEL" }
            .clock (172, 0.0f, 0, 2, 4410, 5, 0, 0)
            .lane (kick, 16, 3, 0, 1, 1.0f, 0.0f).draw (kick, "x.........x.....")
            .lane (mesh, 16, 2, 4, 1, 1.0f, 0.1f).draw (mesh, "....x.......x...")
            .lane (fm, 16, 5, 2, 1, 0.75f, 0.2f).lane (opto, 12, 5, 0, 1, 0.6f, 0.2f)
            .lane (lantern, 16, 12, 0, 1, 0.9f, 0.3f).lane (zap, 16, 3, 3, 1, 0.45f, 0.3f)
            .lane (strike, 16, 3, 0, 2, 0.75f, 0.0f).lane (scatter, 16, 2, 6, 4, 0.8f, 0.0f)
            .voice (kick, 0.32f, 0.4f, 0.5f, 0.9f, 0.0f, 0.0f).voice (mesh, 0.55f, 0.35f, 0.6f, 0.8f, 0.0f, 0.2f)
            .voice (fm, 0.55f, 0.25f, 0.5f, 0.62f, -0.4f, 0.35f).voice (opto, 0.45f, 0.45f, 0.45f, 0.62f, 0.4f, 0.3f)
            .voice (lantern, 0.6f, 0.15f, 0.55f, 0.55f, 0.25f, 0.15f).voice (zap, 0.4f, 0.2f, 0.5f, 0.5f, -0.5f, 0.4f)
            .voice (strike, 0.45f, 0.55f, 0.55f, 0.58f, 0.2f, 0.45f)
            .scat (0.35f, 0.5f, 0.3f, 0.8f, 0.0f, 0.4f)
            .src (forge, 0.6f, 0.7f).src (stepped, 0.7f, 0.7f).src (pong, 0.45f, 0.6f)
            .pin (forge, kPitch, 1).pin (stepped, strNote, 1).pin (pong, fmIndex, 1)
            .delay (3, 0.4f, 0.25f, 0.22f).space (0.55f, 0.45f, 0.45f, 0.18f, 2)
            .reel (0.5f, 1, 1, 0.0f, 0.75f, 0.45f, 0.4f, 0.6f, true).organize (0.0f, mesh + 1).protect (0b11)
            .bus (true, 1, 0.2f, 0.06f, 0.15f, 0.2f, -3.0f).turns (0.4f, false).done());

        // 18. self-sampling: the case plays itself back. OPTO and ZAP play slices of the reel (what the FM, the hats
        //     and the strings left on it a bar ago), pitched and chopped on the sixteenth grid
        a.push_back (B { "Self Sample", "REEL" }
            .clock (140, 0.0f, 0, 2, 1404, 4, 0, 1)
            .lane (kick, 16, 4, 0, 1, 1.0f, 0.0f).lane (mesh, 16, 2, 4, 1, 0.9f, 0.1f).lane (fm, 16, 5, 1, 1, 0.7f, 0.3f)
            .lane (opto, 16, 5, 3, 1, 0.7f, 0.3f).lane (lantern, 16, 9, 0, 1, 0.8f, 0.3f).lane (zap, 16, 3, 2, 2, 0.6f, 0.4f)
            .lane (strike, 16, 3, 0, 2, 0.8f, 0.0f).lane (scatter, 16, 1, 0, 8, 0.6f, 0.0f)
            .voice (kick, 0.3f, 0.45f, 0.45f, 0.88f, 0.0f, 0.0f).voice (mesh, 0.5f, 0.35f, 0.5f, 0.72f, 0.0f, 0.25f)
            .voice (fm, 0.6f, 0.3f, 0.6f, 0.6f, -0.45f, 0.4f)
            .voice (opto, 0.5f, 0.35f, 0.3f, 0.75f, 0.4f, 0.3f).model (opto, flt::kReelModel, 0.2f, 0.7f)
            .voice (lantern, 0.55f, 0.15f, 0.4f, 0.5f, 0.3f, 0.2f)
            .voice (zap, 0.62f, 0.5f, 0.75f, 0.7f, -0.4f, 0.45f).model (zap, flt::kReelModel, 0.4f, 0.3f)
            .voice (strike, 0.5f, 0.6f, 0.55f, 0.6f, 0.15f, 0.5f)
            .locks (opto, 1, "2.6.0.8.3.5.1.7.").locks (zap, 0, "4..7..2...6..0..")
            .scat (0.3f, 0.5f, 0.3f, 0.8f, 0.0f, 0.35f)
            .src (stepped, 0.7f, 0.7f).src (smooth, 0.3f, 0.5f)
            .pin (stepped, strNote, 1).pin (smooth, fmIndex, 1)
            .delay (4, 0.4f, 0.2f, 0.2f).space (0.6f, 0.5f, 0.4f, 0.2f, 0)
            .reel (0.15f, 1, 2, 0.0f, 0.75f, 0.5f, 0.0f, 0.5f, true).protect (0b1)
            .bus (true, 1, 0.16f, 0.05f, 0.1f, 0.15f, -3.0f).done());

        // 19. a pattern that lives: the busy lanes unlocked (Turing Machine), ghost notes every other pass, a fill
        //     only in the turns, the hats only where the kick is not, a phrase of tunes locked on the FM's steps
        a.push_back (B { "Turing Floor", "SEQUENCE" }
            .clock (165, 0.03f, 0, 0, 1652, 5, 0, 0)
            .lane (kick, 16, 4, 0, 1, 1.0f, 0.0f).lane (mesh, 16, 2, 4, 1, 1.0f, 0.1f)
            .lane (fm, 16, 7, 1, 1, 0.9f, 0.2f).lockv (fm, 0.75f)
            .lane (opto, 16, 5, 2, 1, 0.8f, 0.25f).lockv (opto, 0.65f)
            .lane (lantern, 16, 16, 0, 1, 0.85f, 0.3f).logic (lantern, flt::logicNot, kick).lockv (lantern, 0.85f)
            .lane (zap, 16, 6, 1, 1, 0.9f, 0.4f).cond (zap, 1, flt::kCondFill).cond (zap, 4, flt::kCondFill).cond (zap, 9, flt::kCondFill).cond (zap, 12, flt::kCondFill).cond (zap, 14, flt::kCondFill)
            .lane (strike, 16, 3, 0, 2, 0.8f, 0.0f).lane (scatter, 16, 1, 6, 4, 0.7f, 0.0f)
            .cond (mesh, 4, 0).draw (mesh, "....x..x....x.x.").cond (mesh, 7, 1).cond (mesh, 14, flt::kCondPre)
            .voice (kick, 0.3f, 0.4f, 0.55f, 0.88f, 0.0f, 0.0f).vary (kick, 0.1f)
            .voice (mesh, 0.52f, 0.32f, 0.6f, 0.78f, 0.0f, 0.2f).vary (mesh, 0.3f)
            .voice (fm, 0.5f, 0.22f, 0.55f, 0.62f, -0.35f, 0.35f).locks (fm, 0, "4.6.2.4.7.4.1.5.").vary (fm, 0.2f)
            .voice (opto, 0.45f, 0.35f, 0.5f, 0.6f, 0.4f, 0.3f).vary (opto, 0.4f)
            .voice (lantern, 0.6f, 0.12f, 0.55f, 0.55f, 0.2f, 0.1f).locks (lantern, 2, "2.6.3.7.2.6.3.8.").vary (lantern, 0.35f)
            .voice (zap, 0.42f, 0.2f, 0.5f, 0.5f, -0.4f, 0.45f)
            .voice (strike, 0.45f, 0.55f, 0.55f, 0.55f, 0.2f, 0.45f)
            .scat (0.25f, 0.5f, 0.3f, 0.8f, 0.0f, 0.35f)
            .src (forge, 0.6f, 0.7f).src (woggle, 0.5f, 0.6f)
            .pin (forge, kPitch, 1).pin (woggle, optoColor, 1)
            .delay (1, 0.45f, 0.35f, 0.22f).space (0.5f, 0.45f, 0.45f, 0.16f, 3)
            .bus (true, 1, 0.22f, 0.08f, 0.2f, 0.25f, -3.0f).turns (0.8f, true).done());

        // 20. a Cartesian sequencer (after Rene): X moved by the kick, Y by the hats, playing the strings and the
        //     SMR's root; RESONESTOR on lane 8 rings the chord the grid lands on
        a.push_back (B { "Cartesian Rain", "SEQUENCE" }
            .clock (112, 0.0f, 0, 0, 1122, 4, 2, 2)
            .lane (kick, 16, 3, 0, 1, 0.9f, 0.0f).lane (mesh, 16, 1, 8, 2, 0.9f, 0.0f).lane (fm, 16, 0, 0, 1, 1.0f, 0.0f, false)
            .lane (opto, 16, 4, 2, 2, 0.6f, 0.2f).lane (lantern, 16, 7, 1, 1, 0.7f, 0.3f).lane (zap, 16, 0, 0, 1, 1.0f, 0.0f, false)
            .lane (strike, 16, 6, 0, 1, 0.85f, 0.15f).lane (scatter, 16, 3, 2, 2, 0.8f, 0.2f)
            .voice (kick, 0.28f, 0.5f, 0.3f, 0.8f, 0.0f, 0.05f).voice (mesh, 0.45f, 0.4f, 0.45f, 0.6f, 0.0f, 0.35f)
            .voice (opto, 0.55f, 0.55f, 0.35f, 0.55f, 0.4f, 0.45f)
            .voice (lantern, 0.55f, 0.15f, 0.25f, 0.45f, -0.3f, 0.3f)
            .voice (strike, 0.5f, 0.7f, 0.6f, 0.72f, 0.0f, 0.6f)
            .seq ({ -0.8f, -0.2f, 0.3f, 0.9f, -0.5f, 0.1f, 0.6f, -0.9f, 0.2f, 0.8f, -0.3f, 0.4f, -0.6f, 0.0f, 0.7f, -0.1f }, 16, kick + 1, lantern + 1, 0.15f)
            .scat (0.45f, 0.75f, 0.0f, 0.75f, 0.35f, 0.5f).set (ids::smodel, 6.0f).set (ids::spos, 0.35f).set (ids::stex, 0.45f).set (ids::scpitch, -12.0f)
            .src (seq, 0.5f, 0.85f).src (smooth, 0.3f, 0.5f).src (lfo, 0.1f, 0.4f)
            .pin (seq, strNote, 1).pin (seq, smrFreq, 1).pin (smooth, strBright, 1).pin (lfo, optoColor, 1)
            .smr (0.35f, 0.3f, 0.8f, 0.3f, 0.5f, 2, 0)
            .delay (4, 0.4f, 0.0f, 0.2f).space (0.85f, 0.75f, 0.3f, 0.32f, 0).cloudseed()
            .bus (false, 0, 0.06f, 0.02f, 0.05f, 0.1f, -3.0f).done());

        // 21. the case listening to itself: the master back through the SMR half a beat later, OPTO ring-modulated by
        //     the loop's rings, the hats ducked by the kick, OLIVERB shimmering on lane 8
        a.push_back (B { "Feedback Garden", "TEXTURE" }
            .clock (92, 0.0f, 0, 0, 920, 5, 0, 1)
            .lane (kick, 16, 3, 0, 1, 0.9f, 0.0f).lane (mesh, 16, 1, 8, 2, 0.8f, 0.2f).lane (fm, 13, 4, 0, 1, 0.5f, 0.4f)
            .lane (opto, 16, 5, 1, 1, 0.6f, 0.3f).lane (lantern, 16, 8, 0, 1, 0.7f, 0.3f).lane (zap, 16, 2, 5, 2, 0.4f, 0.4f)
            .lane (strike, 16, 3, 0, 2, 0.75f, 0.0f).lane (scatter, 16, 2, 4, 4, 0.8f, 0.3f)
            .voice (kick, 0.27f, 0.55f, 0.35f, 0.82f, 0.0f, 0.0f).voice (mesh, 0.45f, 0.45f, 0.4f, 0.55f, 0.0f, 0.4f)
            .voice (fm, 0.6f, 0.3f, 0.5f, 0.55f, -0.45f, 0.5f)
            .voice (opto, 0.5f, 0.55f, 0.45f, 0.68f, 0.4f, 0.5f).xm (opto, 1, flt::kXsrcFeedback, 0.7f)
            .voice (lantern, 0.55f, 0.18f, 0.3f, 0.48f, 0.3f, 0.3f).xm (lantern, 4, kick, 0.8f)
            .voice (zap, 0.55f, 0.2f, 0.6f, 0.45f, -0.5f, 0.6f)
            .voice (strike, 0.5f, 0.75f, 0.6f, 0.62f, 0.15f, 0.6f)
            .scat (0.75f, 0.6f, 0.0f, 0.8f, 0.35f, 0.5f).set (ids::smodel, 5.0f).set (ids::spos, 0.4f).set (ids::stex, 0.35f).set (ids::scpitch, 12.0f)
            .src (smooth, 0.25f, 0.6f).src (woggle, 0.4f, 0.6f)
            .pin (smooth, smrFreq, 1).pin (woggle, strNote, 1)
            .smr (0.25f, 0.35f, 0.85f, 0.4f, 0.6f, 1, strike + 1)
            .fb (0.55f, 1)
            .delay (4, 0.5f, 0.15f, 0.25f).space (0.8f, 0.7f, 0.4f, 0.28f, 2)
            .bus (true, 0, 0.08f, 0.03f, 0.0f, 0.12f, -3.0f).done());

        // 22. "Strum": Rings strummed by a random clock into a Clouds on the Parasites firmware, the random clock
        //     holding its freeze (here: lane 8's bursts), the strings fed back round the SMR
        a.push_back (B { "Strum Parasites", "DEVINE" }
            .clock (100, 0.0f, 0, 0, 2014, 6, 0, 2)
            .lane (kick, 16, 2, 0, 1, 0.9f, 0.0f).lane (mesh, 16, 1, 8, 2, 0.8f, 0.1f).lane (fm, 16, 0, 0, 1, 1.0f, 0.0f, false)
            .lane (opto, 16, 3, 2, 2, 0.5f, 0.2f).lane (lantern, 16, 6, 0, 1, 0.6f, 0.3f).lane (zap, 16, 0, 0, 1, 1.0f, 0.0f, false)
            .lane (strike, 16, 7, 0, 1, 0.8f, 0.35f).lane (scatter, 16, 3, 1, 2, 0.75f, 0.45f)
            .voice (kick, 0.3f, 0.5f, 0.25f, 0.72f, 0.0f, 0.05f).voice (mesh, 0.45f, 0.4f, 0.4f, 0.55f, 0.0f, 0.4f)
            .voice (opto, 0.55f, 0.5f, 0.3f, 0.5f, 0.4f, 0.45f)
            .voice (lantern, 0.4f, 0.18f, 0.2f, 0.42f, 0.35f, 0.3f)
            .voice (strike, 0.55f, 0.85f, 0.7f, 0.85f, 0.0f, 0.7f).hm (strike, 0.5f, 0.6f)
            .scat (0.7f, 0.65f, 0.0f, 0.9f, 0.4f, 0.6f).set (ids::smodel, 5.0f).set (ids::spos, 0.2f).set (ids::stex, 0.55f).set (ids::scpitch, 0.0f)
            .src (stepped, 0.8f, 0.9f).src (smooth, 0.3f, 0.6f).src (woggle, 0.45f, 0.5f)
            .pin (stepped, strNote, 1).pin (smooth, strBright, 1).pin (woggle, scatSize, 1)
            .fb (0.3f, 2)
            .delay (4, 0.4f, 0.0f, 0.18f).space (0.9f, 0.85f, 0.25f, 0.35f, 0).cloudseed()
            .bus (false, 0, 0.06f, 0.02f, 0.0f, 0.1f, -3.0f).done());

        // 23. every slot moving another at audio rate: the FM ring-modulated by the kick, the hats ducked by it, OPTO
        //     phase-modulated by the FM, ZAP opened by the snare; the reel stuttering it all on the 32nds
        a.push_back (B { "Cross Patch", "GLITCH" }
            .clock (174, 0.0f, 0, 1, 1741, 7, 0, 3)
            .lane (kick, 16, 5, 0, 1, 0.95f, 0.15f).lane (mesh, 16, 3, 4, 1, 0.9f, 0.35f).lane (fm, 16, 9, 1, 1, 0.65f, 0.5f)
            .lane (opto, 13, 6, 0, 1, 0.55f, 0.4f).lane (lantern, 16, 14, 0, 1, 0.85f, 0.5f).lane (zap, 16, 5, 3, 1, 0.55f, 0.6f)
            .lane (strike, 16, 2, 6, 2, 0.6f, 0.3f).lane (scatter, 16, 3, 1, 2, 0.6f, 0.3f)
            .voice (kick, 0.48f, 0.22f, 0.65f, 0.86f, 0.0f, 0.0f).voice (mesh, 0.62f, 0.18f, 0.7f, 0.72f, 0.0f, 0.3f)
            .voice (fm, 0.7f, 0.12f, 0.7f, 0.6f, -0.55f, 0.4f).xm (fm, 1, kick, 0.75f)
            .voice (opto, 0.66f, 0.22f, 0.8f, 0.55f, 0.55f, 0.35f).xm (opto, 2, fm, 0.8f)
            .voice (lantern, 0.68f, 0.08f, 0.8f, 0.52f, 0.15f, 0.2f).xm (lantern, 4, kick, 0.9f)
            .voice (zap, 0.58f, 0.14f, 0.35f, 0.58f, -0.3f, 0.45f).xm (zap, 3, mesh, 0.85f)
            .voice (strike, 0.6f, 0.4f, 0.8f, 0.5f, 0.4f, 0.5f)
            .scat (0.12f, 0.85f, 0.6f, 1.0f, 0.2f, 0.4f)
            .src (stepped, 1.0f, 0.9f).src (chaos, 1.0f, 0.8f).src (forge, 0.9f, 0.7f)
            .pin (stepped, fmRatio, 1).pin (chaos, fmIndex, 1).pin (forge, kPitch, 1).pin (chaos, scatSize, 1)
            .delay (0, 0.5f, 0.6f, 0.28f).zig (1, 1, 0.45f).space (0.35f, 0.35f, 0.5f, 0.14f, 4)
            .reel (0.35f, 0, 0, 0.0f, 0.75f, 0.3f, 0.3f, 0.7f, true).protect (0b1)
            .bus (true, 2, 0.26f, 0.1f, 0.2f, 0.18f, -3.0f).done());

        // 24. a complex oscillator pair under the floor: B in A's phase and A in B's, folded; the kick ducks it,
        //     the snare steps it up the scale, the FM lane strikes its gate; CHAOS breathes with WOGGLE
        a.push_back (B { "Complex Drone", "DRONE" }
            .clock (126, 0.0f, 0, 0, 2601, 5, 0, 0)
            .lane (kick, 16, 3, 0, 1, 1.0f, 0.0f).draw (kick, "x.........x.....").lane (mesh, 16, 2, 4, 1, 1.0f, 0.1f)
            .lane (fm, 16, 3, 2, 2, 0.6f, 0.2f).lane (opto, 16, 0, 0, 1, 1.0f, 0.0f, false).lane (lantern, 16, 7, 1, 1, 0.7f, 0.3f)
            .lane (zap, 16, 2, 5, 2, 0.4f, 0.4f).lane (strike, 16, 0, 0, 1, 1.0f, 0.0f, false).lane (scatter, 16, 1, 6, 4, 0.6f, 0.0f)
            .voice (kick, 0.3f, 0.5f, 0.55f, 0.88f, 0.0f, 0.0f).voice (mesh, 0.5f, 0.35f, 0.55f, 0.72f, 0.0f, 0.25f)
            .voice (fm, 0.55f, 0.2f, 0.5f, 0.5f, -0.4f, 0.4f).voice (lantern, 0.55f, 0.12f, 0.45f, 0.5f, 0.3f, 0.15f)
            .voice (zap, 0.45f, 0.2f, 0.5f, 0.45f, -0.5f, 0.5f).voice (strike, 0.42f, 0.5f, 0.5f, 0.5f, 0.0f, 0.3f)
            .scat (0.5f, 0.5f, 0.2f, 0.8f, 0.2f, 0.35f)
            .drone (0.5f, -12, 6, 0.45f, 0.25f, 0.1f, 0.0f, 0.35f, 0.3f, 0.6f, 0.25f).dmix (0.35f, 0.8f).dcv (0, 1 + woggle, 1 + pong)
            .dact (kick, actDuck).dact (mesh, actStep).dact (fm, actLpg)
            .src (woggle, 0.35f, 0.6f).src (pong, 0.3f, 0.5f).src (forge, 0.6f, 0.6f)
            .pin (forge, kPitch, 1).pin (woggle, zapSweep, 1)
            .delay (4, 0.45f, 0.2f, 0.22f).space (0.75f, 0.65f, 0.4f, 0.28f, 0).cloudseed()
            .bus (true, 1, 0.16f, 0.04f, 0.1f, 0.2f, -3.0f).done());

        // 25. the drums in the drone's key: every hit rings four sympathetic strings tuned to its chord;
        //     the drone itself quiet, FOLDed by the hats
        a.push_back (B { "Sympathetic Kit", "DRONE" }
            .clock (98, 0.04f, 0, 2, 2502, 4, 0, 2)
            .lane (kick, 16, 3, 0, 1, 1.0f, 0.0f).lane (mesh, 16, 2, 4, 1, 1.0f, 0.15f).lane (fm, 16, 4, 1, 1, 0.6f, 0.3f)
            .lane (opto, 16, 3, 2, 2, 0.6f, 0.2f).lane (lantern, 16, 9, 0, 1, 0.8f, 0.3f).lane (zap, 16, 0, 0, 1, 1.0f, 0.0f, false)
            .lane (strike, 16, 0, 0, 1, 1.0f, 0.0f, false).lane (scatter, 16, 0, 0, 1, 1.0f, 0.0f, false)
            .voice (kick, 0.3f, 0.45f, 0.4f, 0.85f, 0.0f, 0.0f).voice (mesh, 0.5f, 0.4f, 0.7f, 0.75f, 0.0f, 0.3f)
            .voice (fm, 0.5f, 0.25f, 0.55f, 0.55f, -0.4f, 0.3f).voice (opto, 0.5f, 0.4f, 0.4f, 0.6f, 0.4f, 0.3f)
            .voice (lantern, 0.55f, 0.15f, 0.4f, 0.5f, 0.3f, 0.2f)
            .drone (0.22f, -24, 5, 0.15f, 0.0f, 0.0f, 0.0f, 0.15f, 0.0f, 0.45f, 0.0f).dmix (0.2f, 0.7f)
            .dact (kick, actDuck).dact (lantern, actFold)
            .res (0.6f, 0.8f, 0.55f)
            .src (smooth, 0.3f, 0.5f).src (stepped, 0.6f, 0.6f)
            .pin (smooth, optoColor, 1).pin (stepped, fmIndex, 1)
            .delay (3, 0.35f, 0.1f, 0.15f).space (0.7f, 0.6f, 0.4f, 0.22f, 0)
            .bus (true, 0, 0.1f, 0.03f, 0.1f, 0.15f, -3.0f).done());

        // 26. slow time under fast drums: the drone opens over sixteen bars with the ARC and falls into
        //     every 16-bar line; the turns fill the drums before it; RAND jumps it on the zaps
        a.push_back (B { "Arc Sixteen", "DRONE" }
            .clock (172, 0.0f, 0, 0, 1616, 6, 0, 0)
            .lane (kick, 16, 3, 0, 1, 1.0f, 0.05f).draw (kick, "x.........x.....").lane (mesh, 16, 2, 4, 1, 1.0f, 0.25f)
            .lane (fm, 16, 4, 3, 1, 0.5f, 0.3f).lane (opto, 16, 0, 0, 1, 1.0f, 0.0f, false).lane (lantern, 16, 10, 0, 1, 0.8f, 0.4f)
            .lane (zap, 16, 2, 11, 2, 0.5f, 0.5f).lane (strike, 16, 0, 0, 1, 1.0f, 0.0f, false).lane (scatter, 16, 2, 6, 4, 0.7f, 0.0f)
            .voice (kick, 0.3f, 0.35f, 0.6f, 0.9f, 0.0f, 0.0f).voice (mesh, 0.55f, 0.3f, 0.65f, 0.8f, 0.0f, 0.25f)
            .voice (fm, 0.55f, 0.2f, 0.6f, 0.5f, -0.4f, 0.3f).voice (lantern, 0.6f, 0.12f, 0.65f, 0.5f, 0.2f, 0.15f)
            .voice (zap, 0.45f, 0.2f, 0.5f, 0.5f, -0.3f, 0.5f)
            .scat (0.3f, 0.6f, 0.4f, 0.9f, 0.0f, 0.35f)
            .drone (0.55f, -12, 3, 0.3f, 0.2f, 0.0f, 0.4f, 0.25f, 0.5f, 0.35f, 0.15f).dmix (0.4f, 0.85f).arcd (2, 0.92f, 1.0f).dcv (0, 1 + arc, 1 + arc)
            .dact (kick, actDuck).dact (zap, actRand)
            .src (arc, 0.5f, 0.8f).src (forge, 0.7f, 0.8f).src (stepped, 0.8f, 0.7f)
            .pin (forge, kPitch, 1).pin (arc, dlyTime, -1).pin (stepped, meshTone, 1)
            .delay (1, 0.5f, 0.45f, 0.25f).space (0.5f, 0.45f, 0.45f, 0.18f, 3)
            .bus (true, 1, 0.24f, 0.1f, 0.25f, 0.3f, -3.0f).turns (0.85f, true).done());

        // 27. beds: a Plaits CHORD and the strings held (SUSTAIN), the lanes re-striking them; SCATTER frozen into
        //     a bed of OLIVERB; sparse drums over it
        a.push_back (B { "Sustain Beds", "DRONE" }
            .clock (84, 0.0f, 0, 0, 840, 4, 2, 2)
            .lane (kick, 16, 2, 0, 1, 0.9f, 0.0f).lane (mesh, 16, 1, 8, 2, 0.8f, 0.1f).lane (fm, 16, 0, 0, 1, 1.0f, 0.0f, false)
            .lane (opto, 16, 3, 2, 4, 0.7f, 0.0f).lane (lantern, 16, 5, 1, 1, 0.6f, 0.2f).lane (zap, 16, 0, 0, 1, 1.0f, 0.0f, false)
            .lane (strike, 16, 2, 0, 8, 0.8f, 0.0f).lane (scatter, 16, 2, 4, 4, 0.8f, 0.3f)
            .voice (kick, 0.28f, 0.55f, 0.3f, 0.78f, 0.0f, 0.05f).voice (mesh, 0.45f, 0.45f, 0.4f, 0.55f, 0.0f, 0.45f)
            .voice (opto, 0.42f, 0.6f, 0.5f, 0.55f, -0.3f, 0.5f).model (opto, 16, 0.4f, 0.55f).sustain (opto)
            .voice (lantern, 0.45f, 0.18f, 0.25f, 0.42f, 0.35f, 0.3f)
            .voice (strike, 0.5f, 0.9f, 0.65f, 0.55f, 0.25f, 0.6f).sustain (strike)
            .scat (0.75f, 0.6f, 0.0f, 0.9f, 0.4f, 0.55f).set (ids::smodel, 5.0f).set (ids::spos, 0.3f).set (ids::stex, 0.4f).set (ids::scpitch, 12.0f)
            .beds (true, false)
            .src (smooth, 0.2f, 0.6f).src (lfo, 0.08f, 0.5f)
            .pin (smooth, strBright, 1).pin (lfo, optoColor, 1)
            .delay (5, 0.45f, 0.0f, 0.2f).space (0.9f, 0.85f, 0.35f, 0.38f, 0).cloudseed()
            .bus (false, 0, 0.05f, 0.02f, 0.0f, 0.1f, -3.0f).done());

        // ---- FREE: patches measured against reference recordings (off the grid, the drone in front)
        // 28. after a long low recording: the hits float free of the grid at about 2.5 a second, often in
        //     pairs 100 ms apart; a folded A at 44 Hz and B an octave up, a quarter tone sharp (it beats);
        //     the LFO breathes its gate at 2.8 Hz; the ARC turns the section over every 32 bars
        a.push_back (B { "Low Instrument", "FREE" }
            .clock (100, 0.0f, 0, 0, 4803, 3, 1, 0).set (ids::loose, 1.0f).set (ids::ebb, 0.8f)
            .lane (kick, 16, 3, 0, 2, 0.85f, 0.1f).lane (mesh, 16, 5, 2, 1, 0.8f, 0.4f).lane (fm, 16, 3, 0, 2, 0.9f, 0.45f)
            .lane (opto, 16, 4, 5, 2, 0.8f, 0.2f).lane (lantern, 16, 7, 1, 1, 0.7f, 0.5f).lane (zap, 16, 3, 3, 2, 0.8f, 0.2f)
            .lane (strike, 16, 0, 0, 1, 1.0f, 0.0f, false).lane (scatter, 16, 3, 0, 2, 0.6f, 0.2f)
            .voice (kick, 0.22f, 0.6f, 0.25f, 0.55f, 0.0f, 0.1f).voice (mesh, 0.6f, 0.3f, 0.95f, 0.6f, 0.2f, 0.3f)
            .voice (fm, 0.55f, 0.25f, 0.7f, 0.55f, -0.45f, 0.35f)
            .voice (opto, 0.45f, 0.55f, 0.75f, 0.55f, 0.4f, 0.3f).voice (lantern, 0.65f, 0.45f, 0.8f, 0.85f, 0.55f, 0.3f)
            .voice (zap, 0.55f, 0.45f, 0.75f, 0.55f, -0.3f, 0.4f).voice (strike, 0.342f, 0.5f, 0.5f, 0.5f, 0.0f, 0.2f)
            .scat (0.4f, 0.6f, 0.5f, 0.8f, 0.35f, 0.7f).set (ids::stex, 0.75f)
            .drone (0.25f, -12, 6, 0.3f, 0.1f, 0.0f, 0.0f, 0.65f, 0.65f, 0.95f, 0.4f).set (ids::dfine, 0.77f).dmix (0.35f, 0.2f)
            .dact (kick, actOff).dact (opto, actLpg).dact (zap, actRand)
            .dcv (0, 1 + smooth, 1 + woggle).set (ids::dlcv, (float) (1 + lfo)).arcd (3, 0.6f, 0.6f)
            .res (0.25f, 0.7f, 0.4f)
            .src (lfo, 0.757f, 0.35f).src (smooth, 0.15f, 0.6f).src (woggle, 0.3f, 0.6f)
            .delay (3, 0.3f, 0.0f, 0.15f).space (0.8f, 0.7f, 0.3f, 0.35f, 0).cloudseed()
            .bus (true, 0, 0.1f, 0.02f, 0.0f, 0.0f, 0.0f).done());

        // 29. after a generative patch of two complex oscillators: mono, noisier, the key wandering (SMOOTH on
        //     the pitch, RAND on two lanes); B eleven times A, A into B: a formant of sidebands round B;
        //     sparse hits with long gaps, the gate breathing at 3 Hz
        a.push_back (B { "Forge Drift", "FREE" }
            .clock (123, 0.0f, 0, 0, 3203, 3, 1, 5).set (ids::loose, 1.0f).set (ids::ebb, 0.85f)
            .lane (kick, 16, 2, 0, 3, 0.8f, 0.0f).lane (mesh, 16, 3, 0, 3, 0.9f, 0.6f).lane (fm, 16, 3, 0, 3, 0.9f, 0.6f)
            .lane (opto, 16, 0, 0, 1, 1.0f, 0.0f, false).lane (lantern, 16, 3, 0, 3, 0.9f, 0.6f).lane (zap, 16, 2, 0, 3, 0.8f, 0.3f)
            .lane (strike, 16, 0, 0, 1, 1.0f, 0.0f, false).lane (scatter, 16, 2, 0, 3, 0.6f, 0.3f)
            .voice (kick, 0.2f, 0.65f, 0.3f, 0.45f, 0.0f, 0.1f).voice (mesh, 0.55f, 0.3f, 0.9f, 0.6f, 0.0f, 0.3f)
            .voice (fm, 0.6f, 0.22f, 0.8f, 0.55f, 0.0f, 0.3f).voice (lantern, 0.5f, 0.35f, 0.9f, 0.8f, 0.0f, 0.25f)
            .voice (zap, 0.6f, 0.5f, 0.8f, 0.5f, 0.0f, 0.4f).voice (strike, 0.36f, 0.5f, 0.5f, 0.5f, 0.0f, 0.2f)
            .scat (0.3f, 0.25f, 0.4f, 0.1f, 0.2f, 0.45f)
            .drone (0.5f, -12, 12, 0.05f, 0.8f, 0.0f, 0.0f, 0.45f, 0.3f, 0.75f, 0.45f).dmix (0.3f, 0.0f)
            .dact (kick, actOff).dact (mesh, actRand).dact (zap, actRand).dact (fm, actPing)
            .dcv (1 + smooth, 1 + chaos, 0).set (ids::dlcv, (float) (1 + lfo)).arcd (2, 0.5f, 0.4f)
            .src (lfo, 0.769f, 0.22f).src (smooth, 0.08f, 0.35f).src (chaos, 0.3f, 0.5f)
            .delay (4, 0.35f, 0.1f, 0.12f).space (0.6f, 0.55f, 0.5f, 0.2f, 0)
            .bus (true, 0, 0.12f, 0.05f, 0.0f, 0.0f, 2.0f).done());

        // 30. after the designed creature sounds: a 48 Hz FM drone ring-modulated by B two octaves under it
        //     (a 12 Hz flutter), B bending A at the same rate (the 12 Hz vibrato they carry); swelling over 4 bars;
        //     zaps falling, dense grains
        a.push_back (B { "Creature FM", "FREE" }
            .clock (110, 0.0f, 0, 0, 3012, 4, 1, 2).set (ids::loose, 0.7f).set (ids::ebb, 0.4f)
            .lane (kick, 16, 2, 0, 2, 0.7f, 0.0f).lane (mesh, 16, 0, 0, 1, 1.0f, 0.0f, false).lane (fm, 16, 3, 2, 1, 0.5f, 0.35f)
            .lane (opto, 16, 2, 5, 2, 0.6f, 0.0f).lane (lantern, 16, 0, 0, 1, 1.0f, 0.0f, false).lane (zap, 16, 3, 1, 1, 0.5f, 0.2f)
            .lane (strike, 16, 0, 0, 1, 1.0f, 0.0f, false).lane (scatter, 16, 3, 2, 2, 0.8f, 0.4f)
            .voice (kick, 0.24f, 0.6f, 0.4f, 0.4f, 0.0f, 0.1f).voice (fm, 0.6f, 0.5f, 0.9f, 0.6f, -0.5f, 0.4f).hm (fm, 0.8f, 0.6f)
            .voice (opto, 0.45f, 0.6f, 0.85f, 0.55f, 0.5f, 0.35f).voice (zap, 0.7f, 0.6f, 0.85f, 0.6f, 0.3f, 0.45f)
            .voice (strike, 0.38f, 0.5f, 0.5f, 0.5f, 0.0f, 0.2f)
            .scat (0.35f, 0.85f, 0.5f, 0.9f, 0.4f, 0.6f)
            .drone (0.6f, -12, 0, 1.0f, 0.15f, 0.55f, 0.0f, 0.9f, 0.4f, 1.0f, 0.4f).dmix (0.3f, 0.3f)
            .dact (kick, actDuck).dact (opto, actFold).dact (zap, actPing)
            .dcv (0, 1 + woggle, 1 + pong).arcd (0, 0.5f, 0.8f)
            .src (woggle, 0.35f, 0.6f).src (pong, 0.3f, 0.5f)
            .pin (woggle, zapSweep, 1)
            .delay (3, 0.4f, 0.2f, 0.18f).space (0.85f, 0.8f, 0.4f, 0.32f, 0).cloudseed()
            .bus (true, 0, 0.12f, 0.05f, 0.0f, 0.0f, 1.0f).done());

        // 31. after the interface sounds: high, short and mono; hats and zaps in even bursts about ten a second,
        //     off the grid; the drone a quiet formant at 1.6 kHz (B sixteen times A), struck open by the hats
        a.push_back (B { "Data Clicks", "FREE" }
            .clock (132, 0.0f, 0, 0, 1846, 6, 1, 5).set (ids::loose, 0.6f).set (ids::ebb, 0.5f)
            .lane (kick, 16, 0, 0, 1, 1.0f, 0.0f, false).lane (mesh, 16, 0, 0, 1, 1.0f, 0.0f, false).lane (fm, 16, 4, 1, 1, 0.5f, 0.3f)
            .lane (opto, 16, 0, 0, 1, 1.0f, 0.0f, false).lane (lantern, 16, 5, 2, 1, 0.6f, 0.5f).lane (zap, 16, 3, 6, 1, 0.5f, 0.4f)
            .lane (strike, 16, 0, 0, 1, 1.0f, 0.0f, false).lane (scatter, 16, 2, 3, 2, 0.6f, 0.3f)
            .voice (mesh, 0.85f, 0.3f, 0.9f, 0.6f, 0.0f, 0.2f).voice (fm, 0.95f, 0.3f, 0.6f, 0.65f, 0.0f, 0.25f)
            .voice (lantern, 0.85f, 0.7f, 0.85f, 0.8f, 0.0f, 0.2f).voice (zap, 0.95f, 0.35f, 0.3f, 0.7f, 0.0f, 0.3f)
            .voice (strike, 0.4f, 0.5f, 0.5f, 0.5f, 0.0f, 0.2f)
            .scat (0.15f, 0.8f, 0.6f, 0.05f, 0.2f, 0.3f)
            .drone (0.05f, 12, 13, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.65f, 0.1f).dmix (0.2f, 0.0f)
            .dact (kick, actOff).dact (lantern, actLpg).dact (zap, actStep)
            .src (stepped, 0.7f, 0.6f).pin (stepped, zapSweep, 1).pin (stepped, fmIndex, 1)
            .delay (1, 0.3f, 0.4f, 0.15f).space (0.35f, 0.3f, 0.5f, 0.12f, 0)
            .bus (true, 2, 0.1f, 0.15f, 0.3f, 0.0f, -3.0f).done());

        // ---- BEADS (Mutable Instruments, open sourced 12 October 2026) on SCATTER
        // 32. every hit of lane 8 seeds a grain of the whole case, hi-fi: short grains just behind the head,
        //     an octave up now and then (SPREAD turns the attenurandomizers), Beads' own plate under them
        a.push_back (B { "Beads Seeds", "TEXTURE" }
            .clock (128, 0.0f, 0, 2, 3201, 4, 1, 2)
            .lane (kick, 16, 4, 0, 1, 1.0f, 0.0f).lane (mesh, 16, 2, 4, 1, 0.9f, 0.15f).lane (fm, 16, 5, 2, 1, 0.6f, 0.3f)
            .lane (opto, 16, 3, 5, 2, 0.6f, 0.2f).lane (lantern, 16, 9, 1, 1, 0.7f, 0.3f).lane (zap, 16, 2, 11, 2, 0.5f, 0.3f)
            .lane (strike, 16, 0, 0, 1, 1.0f, 0.0f, false).lane (scatter, 16, 7, 1, 1, 0.8f, 0.2f)
            .voice (kick, 0.28f, 0.45f, 0.4f, 0.8f, 0.0f, 0.05f).voice (mesh, 0.5f, 0.35f, 0.6f, 0.65f, 0.0f, 0.25f)
            .voice (fm, 0.55f, 0.25f, 0.65f, 0.5f, -0.4f, 0.3f).voice (opto, 0.45f, 0.45f, 0.6f, 0.5f, 0.4f, 0.3f)
            .voice (lantern, 0.6f, 0.3f, 0.6f, 0.5f, 0.3f, 0.2f).voice (zap, 0.6f, 0.3f, 0.7f, 0.45f, -0.3f, 0.3f)
            .scat (0.32f, 0.5f, 0.0f, 0.25f, 0.3f, 0.6f).set (ids::smodel, 7.0f).set (ids::spos, 0.04f).set (ids::stex, 0.6f)
            .set (ids::scpitch, 12.0f).set (ids::sbverb, 0.35f)
            .src (woggle, 0.4f, 0.6f).src (stepped, 0.6f, 0.6f)
            .pin (woggle, scatSize, 1).pin (stepped, fmIndex, 1)
            .delay (3, 0.3f, 0.1f, 0.12f).space (0.7f, 0.6f, 0.4f, 0.2f, 0)
            .bus (true, 1, 0.12f, 0.05f, 0.1f, 0.15f, -3.0f).done());

        // 33. Beads on its dirty tape (24 kHz, 8 bits, wow and flutter), seeding itself at random: long grains an
        //     octave down from eight seconds back, frozen by the bursts; the hits off the grid, in waves
        a.push_back (B { "Dirty Tape Beads", "TEXTURE" }
            .clock (96, 0.0f, 0, 0, 2402, 3, 1, 0).set (ids::loose, 0.8f).set (ids::ebb, 0.6f)
            .lane (kick, 16, 3, 0, 2, 0.8f, 0.0f).lane (mesh, 16, 3, 2, 2, 0.7f, 0.3f).lane (fm, 16, 3, 5, 2, 0.7f, 0.3f)
            .lane (opto, 16, 2, 3, 4, 0.7f, 0.0f).lane (lantern, 16, 5, 1, 1, 0.6f, 0.3f).lane (zap, 16, 0, 0, 1, 1.0f, 0.0f, false)
            .lane (strike, 16, 2, 0, 4, 0.7f, 0.0f).lane (scatter, 16, 0, 0, 1, 1.0f, 0.0f, false)
            .voice (kick, 0.24f, 0.55f, 0.3f, 0.6f, 0.0f, 0.1f).voice (mesh, 0.55f, 0.35f, 0.8f, 0.55f, 0.2f, 0.3f)
            .voice (fm, 0.5f, 0.3f, 0.6f, 0.5f, -0.4f, 0.35f).voice (opto, 0.4f, 0.55f, 0.6f, 0.5f, 0.4f, 0.35f)
            .voice (lantern, 0.6f, 0.4f, 0.7f, 0.5f, 0.5f, 0.3f).voice (strike, 0.4f, 0.8f, 0.6f, 0.6f, -0.2f, 0.5f)
            .scat (0.72f, 0.82f, 0.0f, 0.45f, 0.45f, 0.65f).set (ids::smodel, 10.0f).set (ids::spos, 0.5f).set (ids::stex, 0.35f)
            .set (ids::scpitch, -12.0f).set (ids::sbverb, 0.5f)
            .drone (0.25f, -12, 6, 0.2f, 0.0f, 0.0f, 0.0f, 0.5f, 0.6f, 0.7f, 0.2f).dmix (0.3f, 0.4f).set (ids::dlcv, (float) (1 + lfo))
            .src (lfo, 0.75f, 0.3f).src (smooth, 0.12f, 0.6f)
            .pin (smooth, scatSize, 1)
            .delay (4, 0.3f, 0.0f, 0.1f).space (0.85f, 0.75f, 0.45f, 0.3f, 0).cloudseed()
            .bus (true, 0, 0.1f, 0.03f, 0.0f, 0.05f, -2.0f).done());

        // 34. SIZE all the way up: Beads is a delay on clean tape, lane 8 its clock (every 4th hit sets the taps),
        //     the feedback high; the zaps and the FM breaking through it
        a.push_back (B { "Beads Delay", "GLITCH" }
            .clock (140, 0.0f, 0, 2, 1404, 5, 0, 3)
            .lane (kick, 16, 4, 0, 1, 1.0f, 0.1f).lane (mesh, 16, 2, 4, 1, 1.0f, 0.2f).lane (fm, 16, 6, 1, 1, 0.6f, 0.4f)
            .lane (opto, 16, 0, 0, 1, 1.0f, 0.0f, false).lane (lantern, 16, 8, 0, 1, 0.8f, 0.3f).lane (zap, 16, 3, 3, 1, 0.6f, 0.4f)
            .lane (strike, 16, 0, 0, 1, 1.0f, 0.0f, false).lane (scatter, 16, 4, 0, 1, 1.0f, 0.0f)
            .voice (kick, 0.3f, 0.4f, 0.5f, 0.85f, 0.0f, 0.0f).voice (mesh, 0.5f, 0.3f, 0.6f, 0.65f, 0.0f, 0.3f)
            .voice (fm, 0.6f, 0.2f, 0.75f, 0.5f, -0.5f, 0.4f).voice (lantern, 0.65f, 0.2f, 0.6f, 0.45f, 0.4f, 0.2f)
            .voice (zap, 0.65f, 0.3f, 0.8f, 0.5f, 0.4f, 0.45f)
            .scat (1.0f, 0.25f, 0.0f, 0.2f, 0.55f, 0.5f).set (ids::smodel, 8.0f).set (ids::spos, 0.4f).set (ids::stex, 0.5f)
            .set (ids::scpitch, 0.0f).set (ids::sbverb, 0.2f)
            .src (chaos, 0.5f, 0.5f).pin (chaos, zapSweep, 1)
            .delay (2, 0.2f, 0.3f, 0.1f).space (0.5f, 0.45f, 0.4f, 0.15f, 0)
            .bus (true, 2, 0.15f, 0.1f, 0.2f, 0.15f, -3.0f).done());
        return a;

    }();
    return presets;
}
