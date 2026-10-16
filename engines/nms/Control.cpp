// NMS - system and controller modules: TRANSPORT & SCALE, MIXER, PATTERNS,
// LOOP PACK, WANDER, CHAOS. Their panels and jacks live here; the global
// work (pattern memory, blends, MIDI key mapping, chaos noise) is done by the
// processor, which reads these modules' controls and serves their requests.
#include "Common.h"
#include "Modules.h"

namespace nms
{
// ---------------------------------------------------------------------------
void buildScale (Ctx& c, int key, bool minor, int notes, bool twelveTone, int npo)
{
    c.key = key; c.notesPerOct = std::max (1, std::min (64, npo));
    if (twelveTone) { c.scaleLen = c.notesPerOct; for (int i = 0; i < c.scaleLen; ++i) c.scale[(size_t) i] = i; return; }
    notes = std::max (1, std::min (notes, c.notesPerOct));
    if (c.notesPerOct == 12)
    {
        static const std::vector<int> maj[13] = { {}, { 0 }, { 0, 7 }, { 0, 4, 7 }, { 0, 4, 7, 11 }, { 0, 2, 4, 7, 9 }, { 0, 2, 4, 5, 7, 9 }, { 0, 2, 4, 5, 7, 9, 11 },
            { 0, 2, 4, 5, 7, 8, 9, 11 }, { 0, 2, 3, 4, 5, 7, 8, 9, 11 }, { 0, 2, 3, 4, 5, 7, 8, 9, 10, 11 }, { 0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 11 }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } };
        static const std::vector<int> mnr[13] = { {}, { 0 }, { 0, 7 }, { 0, 3, 7 }, { 0, 3, 7, 10 }, { 0, 3, 5, 7, 10 }, { 0, 3, 5, 6, 7, 10 }, { 0, 2, 3, 5, 7, 8, 10 },
            { 0, 2, 3, 5, 7, 8, 10, 11 }, { 0, 2, 3, 5, 6, 7, 8, 10, 11 }, { 0, 1, 2, 3, 5, 6, 7, 8, 10, 11 }, { 0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11 }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } };
        const auto& t = (minor ? mnr : maj)[notes];
        c.scaleLen = (int) t.size();
        for (int i = 0; i < c.scaleLen; ++i) c.scale[(size_t) i] = t[(size_t) i];
        return;
    }
    // other tunings: the notes spread evenly (Euclidean) over the cycle, minor = rotated
    c.scaleLen = notes;
    for (int i = 0; i < notes; ++i) c.scale[(size_t) i] = (int) std::floor ((double) i * c.notesPerOct / notes + (minor ? 0.5 : 0.0));
}

namespace
{
const std::vector<std::string> kKeys { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

// =============================================================================
TypeSpec specTransport()
{
    B b ("TRANSPORT", "TRANSPORT & SCALE", "Tempo, start / stop, swing, wonk, key and scale for the whole instrument", Cat::System, 0xffc8bfa6, 4);
    b.t.unique = true; b.t.removable = false;
    b.choice ("tsrc", "TEMPO FROM", { "HOST", "LOCAL" }, 0, 0, 0, 2, "Tempo from the host or from the TEMPO knob"); b.fixed();
    b.choice ("xsrc", "TRANSPORT", { "HOST", "LOCAL" }, 1, 0, 1, 2, "Start / stop with the host transport, or run locally"); b.fixed();
    b.knob ("tempo", "TEMPO", 40, 240, 116, 2, 0, Map::Lin, " BPM"); b.fixed();
    b.tog ("run", "RUN", true, 4, 0, 2, "Local start / stop"); b.fixed();
    b.mom ("restart", "RESTART", 4, 1, 2, "Back to the first step");
    b.knob ("swing", "SWING", 0, 1, 0.0f, 6, 0, Map::Lin, "", "Delays every second 16th"); b.last().rnd = false;
    b.knob ("wonk", "WONK", 0, 1, 0.0f, 0, 2, Map::Lin, "", "Drunken tempo warp inside each WONK DIV"); b.last().rnd = false;
    b.choice ("wdiv", "WONK DIV", { "1/8", "1/4", "1/2", "1 BAR", "2 BAR" }, 1, 2, 2, 2); b.last().rnd = false;
    b.choice ("key", "KEY", kKeys, 0, 4, 2, 2); b.fixed();
    b.tog ("minor", "MINOR", true, 6, 2, 2); b.fixed();
    b.knob ("notes", "# NOTES", 1, 12, 5, 2, 3, Map::Int, "", "Notes in the scale (5 = pentatonic, 7 = diatonic)"); b.fixed();
    b.choice ("mode", "MODE", { "SCALE SNAP", "12-TONE" }, 0, 4, 3, 2, "Scale snap, or every note of the tuning"); b.fixed();
    b.knob ("npo", "NOTES/OCT", 5, 24, 12, 6, 3, Map::Int, "", "Notes per octave: 12 = Western, others = equal-tempered microtonal"); b.fixed();
    b.choice ("tswing", "SWING ON", { "1/16", "1/8" }, 0, 0, 4, 2); b.fixed();
    b.custom (0, 0, 5, 8, 3);
    b.in ("run", "RUN", PortKind::Pulse, Norm::None, "When patched, a high gate runs the transport");
    b.in ("rst", "RESET", PortKind::Pulse, Norm::None, "Restart on a pulse");
    b.out ("clk", "CLK", PortKind::Pulse, "16th clock"); b.out ("bar", "BAR", PortKind::Pulse); b.out ("runo", "RUN", PortKind::Pulse);
    b.out ("rsto", "RESET", PortKind::Pulse);
    b.out ("kp", "KEY P", PortKind::Pitch, "Keyboard pitch (MIDI notes not taken by LOOP PACK), 1.0 = octave, 0 = C3");
    b.out ("kg", "KEY G", PortKind::Pulse, "Keyboard gate"); b.out ("kv", "KEY VEL", PortKind::CV); b.out ("kt", "KEY TRIG", PortKind::Pulse);
    return b.t;
}
class Transport : public Module
{
public:
    Transport() : Module (spec()) {}
    static const TypeSpec& spec() { static TypeSpec t = specTransport(); return t; }
    void momentary (int i) override { if (i == spec().param ("restart")) request.fetch_or (4); }
    void block (const Ctx& cx) override { Module::block (cx); k = 0; }
    void tick (const float* in, float* o) override
    {
        if (rst.rise (in[1])) request.fetch_or (4);
        externalRun.store (isPatched (0) ? (in[0] > 0.5f ? 1 : 0) : -1, std::memory_order_relaxed);
        const double pos = ctx->pos16 + ctx->pos16PerSample * k++;
        const int64_t s = (int64_t) std::floor (pos + 1e-9);
        if (ctx->running && s != last) { last = s; clk.fire ((int) pulseSamples (sr)); if (s % 16 == 0) bar.fire ((int) pulseSamples (sr)); disp[0].store ((float) (s % 16)); }
        if (ctx->restart != lastRestart) { lastRestart = ctx->restart; rsto.fire ((int) pulseSamples (sr)); last = INT64_MIN; }
        if (ctx->keyTrig != lastKey) { lastKey = ctx->keyTrig; kt.fire ((int) pulseSamples (sr)); }
        o[0] = clk.tick(); o[1] = bar.tick(); o[2] = ctx->running ? 1.0f : 0.0f; o[3] = rsto.tick();
        o[4] = ctx->keyPitch; o[5] = ctx->keyGate; o[6] = ctx->keyVel; o[7] = kt.tick();
        disp[1].store ((float) ctx->bpm, std::memory_order_relaxed); disp[2].store (ctx->running ? 1.0f : 0.0f, std::memory_order_relaxed);
    }
    std::atomic<int> externalRun { -1 };
private:
    Edge rst; Pulse clk, bar, rsto, kt;
    int64_t last = INT64_MIN; int lastRestart = 0, lastKey = 0, k = 0;
};

// =============================================================================
TypeSpec specMixer()
{
    B b ("MIXER", "MIXING CONSOLE", "Eight channel strips with sends and the master bus; every channel takes the end of its module chain", Cat::System, 0xffc8bfa6, 9);
    b.t.unique = true; b.t.removable = false;
    for (int i = 0; i < kChannels; ++i)
    {
        const std::string n = std::to_string (i + 1); const int c = i * 2;
        b.knob ("lvl" + n, "LEVEL " + n, 0, 1, 0.72f, c, 0); b.last().ptn = false; b.last().rnd = false;
        b.knob ("pan" + n, "PAN " + n, -1, 1, 0, c, 2); b.last().rnd = false;
        b.mini ("sa" + n, "SEND A " + n, 0, 1, 0.0f, c, 4); b.last().rnd = false;
        b.mini ("sb" + n, "SEND B " + n, 0, 1, 0.0f, c + 1, 4); b.last().rnd = false;
        b.tog ("mute" + n, "M", false, c, 5, 1, "Mute"); b.fixed();
        b.tog ("solo" + n, "S", false, c + 1, 5, 1, "Solo"); b.fixed();
        b.custom (i, c, 6, 2, 2);
    }
    b.knob ("master", "MASTER", 0, 1, 0.75f, 16, 0); b.fixed();
    b.knob ("reta", "RET A", 0, 1, 0.6f, 16, 2); b.fixed();
    b.mini ("retb", "RET B", 0, 1, 0.6f, 16, 4); b.fixed();
    b.mini ("glue", "GLUE", 0, 1, 0.3f, 17, 4, Map::Lin, "", "Bus compression on the master"); b.fixed();
    b.custom (8, 16, 5, 2, 3);
    for (int i = 0; i < kChannels; ++i)
    {
        const std::string n = std::to_string (i + 1);
        b.in ("l" + n, "CH" + n + " L", PortKind::Audio, Norm::ChannelL, "Normalled to the end of this channel's module chain");
        b.in ("r" + n, "CH" + n + " R", PortKind::Audio, Norm::ChannelR);
    }
    b.in ("ral", "RET A L", PortKind::Audio, Norm::ReturnL, "Normalled to the end of the RET A lane"); b.in ("rar", "RET A R", PortKind::Audio, Norm::ReturnR);
    b.in ("rbl", "RET B L", PortKind::Audio, Norm::ReturnL, "Normalled to the end of the RET B lane"); b.in ("rbr", "RET B R", PortKind::Audio, Norm::ReturnR);
    b.out ("sal", "SEND A L", PortKind::Audio); b.out ("sar", "SEND A R", PortKind::Audio);
    b.out ("sbl", "SEND B L", PortKind::Audio); b.out ("sbr", "SEND B R", PortKind::Audio);
    b.out ("l", "MIX L", PortKind::Audio, "The master (also the plugin output)"); b.out ("r", "MIX R", PortKind::Audio);
    return b.t;
}
class Mixer : public Module
{
public:
    Mixer() : Module (spec())
    {
        for (int i = 0; i < kChannels; ++i)
        {
            const auto n = std::to_string (i + 1);
            iL[i] = P ("lvl" + n); iP[i] = P ("pan" + n); iA[i] = P ("sa" + n); iB[i] = P ("sb" + n); iM[i] = P ("mute" + n); iS[i] = P ("solo" + n);
        }
    }
    static const TypeSpec& spec() { static TypeSpec t = specMixer(); return t; }
    bool isOutput() const override { return true; }
    void block (const Ctx& cx) override
    {
        Module::block (cx);
        anySolo = false;
        for (int i = 0; i < kChannels; ++i) anySolo |= c (iS[i]) > 0.5f;
        for (int i = 0; i < kChannels; ++i)
        {
            const bool on = c (iM[i]) < 0.5f && (! anySolo || c (iS[i]) > 0.5f);
            const float lv = c (iL[i]);
            tg[i] = on ? lv * lv * 1.6f : 0.0f;
        }
        const float m = c (P ("master")); master = m * m * 1.6f;
        const float ra = c (P ("reta")), rb = c (P ("retb")); retA = ra * ra * 1.4f; retB = rb * rb * 1.4f;
        glue = c (P ("glue"));
        rel = std::exp (-1.0f / (0.15f * sr)); att = std::exp (-1.0f / (0.004f * sr));
    }
    void tick (const float* in, float* o) override
    {
        float L = 0, R = 0, aL = 0, aR = 0, bL = 0, bR = 0;
        for (int i = 0; i < kChannels; ++i)
        {
            const float l = in[i * 2], r = in[i * 2 + 1];
            g[i] += (tg[i] - g[i]) * 0.002f;
            if (l == 0.0f && r == 0.0f) { decay (i); continue; }
            const float pan = c (iP[i]);
            const float gl = g[i] * (pan > 0 ? 1.0f - pan : 1.0f), gr = g[i] * (pan < 0 ? 1.0f + pan : 1.0f);
            const float yl = l * gl, yr = r * gr;
            L += yl; R += yr;
            aL += yl * c (iA[i]); aR += yr * c (iA[i]); bL += yl * c (iB[i]); bR += yr * c (iB[i]);
            const float pk = std::max (std::abs (yl), std::abs (yr)), old = disp[(size_t) i].load (std::memory_order_relaxed);
            disp[(size_t) i].store (pk > old ? pk : old * 0.9996f, std::memory_order_relaxed);
        }
        o[0] = aL; o[1] = aR; o[2] = bL; o[3] = bR;
        L += in[16] * retA; R += (isPatched (17) ? in[17] : in[16]) * retA;
        L += in[18] * retB; R += (isPatched (19) ? in[19] : in[18]) * retB;
        // glue: a gentle RMS bus compressor
        if (glue > 0.001f)
        {
            const float lvl = std::max (std::abs (L), std::abs (R));
            env = lvl > env ? att * env + (1 - att) * lvl : rel * env + (1 - rel) * lvl;
            const float thr = 0.5f - 0.3f * glue;
            const float gr = env > thr ? std::pow (thr / env, 0.5f * glue + 0.2f) : 1.0f;
            L *= gr * (1.0f + glue * 0.5f); R *= gr * (1.0f + glue * 0.5f);
        }
        L *= master; R *= master;
        mainL = L; mainR = R; o[4] = L; o[5] = R;
        const float pk = std::max (std::abs (L), std::abs (R)), old = disp[8].load (std::memory_order_relaxed);
        disp[8].store (pk > old ? pk : old * 0.9997f, std::memory_order_relaxed);
    }
private:
    void decay (int i) { disp[(size_t) i].store (disp[(size_t) i].load (std::memory_order_relaxed) * 0.9996f, std::memory_order_relaxed); }
    int P (const std::string& k) const { return spec().param (k); }
    int iL[kChannels] {}, iP[kChannels] {}, iA[kChannels] {}, iB[kChannels] {}, iM[kChannels] {}, iS[kChannels] {};
    float g[kChannels] {}, tg[kChannels] {}, master = 1, retA = 0, retB = 0, glue = 0, env = 0, rel = 0.999f, att = 0.9f;
    bool anySolo = false;
};

// =============================================================================

TypeSpec specPatterns()
{
    B b ("PATTERNS", "PATTERN MEMORY", "Save, recall and blend between patterns: snapshots of every generator and sound control", Cat::Control, 0xffc8bfa6, 5);
    b.t.unique = true;
    b.mom ("add", "ADD", 0, 0, 2, "Save the current state as a new pattern");
    b.mom ("del", "DELETE", 2, 0, 2, "Delete the selected pattern");
    b.mom ("clear", "CLEAR", 4, 0, 2, "Delete every pattern");
    b.choice ("type", "RECALL", { "ALL", "GEN", "SOUND" }, 0, 6, 0, 2, "What a recall changes: everything, only the pattern (generator) controls or only the sound controls"); b.fixed();
    b.mom ("rgen", "RND GEN", 0, 1, 2, "Randomise the generator controls of every module (NMS 'Random Generators')");
    b.mom ("rsnd", "RND SND", 2, 1, 2, "Randomise the sound controls (NMS 'Random Sounds')");
    b.mom ("prev", "< PREV", 4, 1, 2); b.mom ("next", "NEXT >", 6, 1, 2);
    b.knob ("blend", "BLEND", 0, 8, 0.5f, 8, 0, Map::Lin, " s", "Blend time of a recall"); b.fixed();
    b.custom (0, 0, 2, 10, 6);
    b.in ("next", "NEXT", PortKind::Pulse); b.in ("prev", "PREV", PortKind::Pulse); b.in ("rnd", "RANDOM", PortKind::Pulse, Norm::None, "Recall a random pattern");
    b.in ("sel", "SELECT", PortKind::CV, Norm::None, "0..1 chooses the pattern (on every change of the chosen slot)");
    b.out ("chg", "CHANGE", PortKind::Pulse, "A pulse on every recall"); b.out ("idx", "INDEX", PortKind::CV, "Selected pattern / 31");
    return b.t;
}
class Patterns : public Module
{
public:
    Patterns() : Module (spec()) {}
    static const TypeSpec& spec() { static TypeSpec t = specPatterns(); return t; }
    void momentary (int i) override
    {
        const auto& k = spec().params[(size_t) i].key;
        if (k == "add") request.fetch_or (rqAdd); else if (k == "del") request.fetch_or (rqDelete); else if (k == "clear") request.fetch_or (rqClear);
        else if (k == "rgen") request.fetch_or (rqRandGen); else if (k == "rsnd") request.fetch_or (rqRandSnd);
        else if (k == "next") request.fetch_or (rqNext); else if (k == "prev") request.fetch_or (rqPrev);
    }
    void tick (const float* in, float* o) override
    {
        if (e[0].rise (in[0])) request.fetch_or (rqNext);
        if (e[1].rise (in[1])) request.fetch_or (rqPrev);
        if (e[2].rise (in[2])) request.fetch_or (rqRandom);
        if (isPatched (3))
        {
            const int n = (int) disp[0].load (std::memory_order_relaxed);
            const int sel = n > 0 ? std::min (n - 1, (int) (clamp01 (in[3]) * (float) n)) : -1;
            if (sel != lastSel) { lastSel = sel; if (sel >= 0) selectRequest.store (sel); }
        }
        const int cnt = (int) disp[3].load (std::memory_order_relaxed);
        if (cnt != lastChange) { lastChange = cnt; chg.fire ((int) pulseSamples (sr)); }
        o[0] = chg.tick(); o[1] = clamp01 (disp[1].load (std::memory_order_relaxed) / 31.0f);
    }
    std::atomic<int> selectRequest { -1 };
private:
    Edge e[3]; Pulse chg; int lastSel = -2, lastChange = 0;
};

TypeSpec specLoopPack()
{
    B b ("LOOP PACK", "PERFORMANCE CONTROLLER", "Maps the patterns onto MIDI keys: trigger and blend them with one hand", Cat::Control, 0xffc8bfa6, 5);
    b.t.unique = true;
    b.choice ("base", "BASE NOTE", { "C0", "C1", "C2", "C3", "C4", "C5" }, 2, 0, 0, 2, "The first key of the pattern keys; notes below it go to the keyboard (TRANSPORT KEY P / G)"); b.fixed();
    b.choice ("auto", "AUTO-RESET", { "OFF", "GATE" }, 1, 2, 0, 2, "GATE: the macros spring back to the centre when all keys are released"); b.fixed();
    b.mom ("rmac", "RESET MACROS", 4, 0, 4, "All 24 macros back to the centre");
    b.knob ("blend", "BLEND TIME", 0, 8, 1.0f, 8, 0, Map::Lin, " s", "Blend time when a key recalls a pattern"); b.fixed();
    b.choice ("hold", "KEYS", { "LATCH", "HOLD" }, 0, 0, 1, 2, "HOLD: releasing the key blends back to the pattern that was playing before"); b.fixed();
    b.custom (0, 0, 2, 10, 6);
    b.out ("ptn", "PTN", PortKind::CV, "The pattern being played / 31"); b.out ("gate", "KEY GATE", PortKind::Pulse, "High while a pattern key is held");
    b.out ("blend", "BLEND", PortKind::CV, "Blend position 0..1");
    return b.t;
}
class LoopPack : public Module
{
public:
    LoopPack() : Module (spec()) {}
    static const TypeSpec& spec() { static TypeSpec t = specLoopPack(); return t; }
    void momentary (int i) override { if (spec().params[(size_t) i].key == "rmac") request.fetch_or (rqResetMacros); }
    void tick (const float*, float* o) override
    {
        o[0] = clamp01 (disp[1].load (std::memory_order_relaxed) / 31.0f);
        o[1] = disp[4].load (std::memory_order_relaxed) > 0.5f ? 1.0f : 0.0f;
        o[2] = disp[2].load (std::memory_order_relaxed);
    }
};

TypeSpec specWander()
{
    B b ("WANDER", "GENERATIVE CONTROLLER", "A never-ending generative controller: the instrument plays itself, forever", Cat::Control, 0xffc8bfa6, 3);
    b.t.unique = true;
    b.big ("speed", "SPEED", 0, 1, 0.35f, 0, 0, Map::Lin, "", "From one change every 32 bars to one every bar"); b.fixed();
    b.tog ("play", "PLAY", true, 3, 0, 3); b.fixed();
    b.choice ("mode", "MODE", { "PATTERNS", "DRIFT" }, 0, 3, 1, 3, "PATTERNS: blends between stored patterns. DRIFT: wanders away from the current state"); b.fixed();
    b.choice ("tgt", "TARGET", { "ALL", "GEN", "SOUND" }, 0, 3, 2, 3); b.fixed();
    b.knob ("depth", "DEPTH", 0, 1, 0.35f, 0, 3, Map::Lin, "", "How far DRIFT moves"); b.fixed();
    b.custom (0, 2, 3, 4, 2);
    b.custom (1, 0, 5, 6, 3);
    b.in ("spd", "SPEED CV", PortKind::CV); b.in ("play", "PLAY", PortKind::Pulse, Norm::None, "When patched, a high gate plays");
    b.out ("chg", "CHANGE", PortKind::Pulse); b.out ("ph", "PHASE", PortKind::CV);
    return b.t;
}
class Wander : public Module
{
public:
    Wander() : Module (spec()) {}
    static const TypeSpec& spec() { static TypeSpec t = specWander(); return t; }
    void tick (const float* in, float* o) override
    {
        speedCv.store (in[0], std::memory_order_relaxed);
        playGate.store (isPatched (1) ? (in[1] > 0.5f ? 1 : 0) : -1, std::memory_order_relaxed);
        const int cnt = (int) disp[3].load (std::memory_order_relaxed);
        if (cnt != last) { last = cnt; chg.fire ((int) pulseSamples (sr)); }
        o[0] = chg.tick(); o[1] = disp[0].load (std::memory_order_relaxed);
    }
    std::atomic<float> speedCv { 0 };
    std::atomic<int> playGate { -1 };
private:
    Pulse chg; int last = 0;
};

TypeSpec specChaos()
{
    B b ("CHAOS", "MODULATION", "Introduces true random noise to the instrument's controls", Cat::Control, 0xffc8bfa6, 3);
    b.t.unique = true;
    b.knob ("amt", "AMOUNT", 0, 1, 0.15f, 0, 0, Map::Lin, "", "Depth of the noise on every control"); b.fixed();
    b.knob ("rate", "RATE", 0.02f, 20, 0.5f, 2, 0, Map::Exp, "Hz", "How fast the noise moves"); b.fixed();
    b.knob ("smooth", "SMOOTH", 0, 1, 0.8f, 4, 0, Map::Lin, "", "0 = stepped (sample & hold), 1 = smooth"); b.fixed();
    b.choice ("tgt", "TARGET", { "ALL", "GEN", "SOUND" }, 2, 0, 2, 3); b.fixed();
    b.mom ("roll", "ROLL DICE", 3, 2, 3, "One random jump of every control (randomise MEDIUM)");
    b.custom (0, 0, 3, 6, 5);
    b.in ("amt", "AMOUNT CV", PortKind::CV); b.in ("trig", "ROLL", PortKind::Pulse, Norm::None, "Rolls the dice on a pulse");
    b.out ("noise", "NOISE", PortKind::CV, "Smooth random 0..1"); b.out ("sh", "S&H", PortKind::CV, "Stepped random 0..1");
    return b.t;
}
class Chaos : public Module
{
public:
    Chaos() : Module (spec()) {}
    static const TypeSpec& spec() { static TypeSpec t = specChaos(); return t; }
    void momentary (int i) override { if (spec().params[(size_t) i].key == "roll") request.fetch_or (rqChaosRoll); }
    void tick (const float* in, float* o) override
    {
        amtCv.store (in[0], std::memory_order_relaxed);
        if (roll.rise (in[1])) request.fetch_or (rqChaosRoll);
        t += c (1) / sr;
        const int64_t n = (int64_t) std::floor (t);
        const float f = (float) (t - (double) n), sm = c (2);
        const float a = hashf (0xc4a05u, (int32_t) n), b2 = hashf (0xc4a05u, (int32_t) (n + 1));
        const float s = f * f * (3.0f - 2.0f * f);
        o[0] = lerp (a, b2, s * sm + (1.0f - sm) * 0.0f);
        o[1] = a;
        if (++dk >= 256) { dk = 0; disp[(size_t) (8 + hist)].store (o[0]); hist = (hist + 1) % 48; disp[0].store ((float) hist); }
    }
    std::atomic<float> amtCv { 0 };
private:
    Edge roll; double t = 0; int dk = 0, hist = 0;
};

// =============================================================================
TypeSpec specScenes()
{
    B b ("SCENES", "SONG STRUCTURE", "Puts patterns in order with a length in bars: a song. WANDER keeps moving inside each scene", Cat::Control, 0xffc8bfa6, 5);
    b.t.unique = true;
    b.tog ("play", "PLAY", false, 0, 0, 2, "Run the scenes with the transport"); b.fixed();
    b.tog ("loop", "LOOP", true, 2, 0, 2, "Back to the first scene after the last"); b.fixed();
    b.mom ("first", "TO START", 4, 0, 2, "The first scene on the next bar");
    b.knob ("blend", "BLEND", 0, 1, 0.25f, 8, 0, Map::Lin, "", "Blend time into a scene, in bars"); b.fixed();
    b.tog ("winner", "WANDER INSIDE", true, 6, 0, 2, "WANDER drifts around the current scene instead of choosing patterns"); b.fixed();
    std::vector<std::string> pats { "--" };
    for (int i = 1; i <= 32; ++i) pats.push_back (std::to_string (i));
    for (int i = 0; i < 16; ++i)
    {
        ParamSpec p; p.key = "p" + std::to_string (i + 1); p.name = "SCENE " + std::to_string (i + 1); p.kind = Kind::Hidden; p.choices = pats;
        p.min = 0; p.max = 32; p.def = i < 4 ? (float) (i + 1) : 0.0f; p.rnd = false; p.ptn = false; b.add (p);
        ParamSpec q; q.key = "b" + std::to_string (i + 1); q.name = "BARS " + std::to_string (i + 1); q.kind = Kind::Hidden; q.choices = { "1", "2", "4", "8", "16" };
        q.min = 0; q.max = 4; q.def = 2; q.rnd = false; q.ptn = false; b.add (q);
    }
    b.custom (0, 0, 2, 10, 6);
    b.in ("next", "NEXT", PortKind::Pulse, Norm::None, "Jump to the next scene on the next bar");
    b.out ("chg", "CHANGE", PortKind::Pulse); b.out ("pos", "POSITION", PortKind::CV, "Progress through the scene 0..1");
    return b.t;
}
class Scenes : public Module
{
public:
    Scenes() : Module (spec()) {}
    static const TypeSpec& spec() { static TypeSpec t = specScenes(); return t; }
    void momentary (int i) override { if (spec().params[(size_t) i].key == "first") request.fetch_or (1 << 14); }
    void tick (const float* in, float* o) override
    {
        if (nx.rise (in[0])) request.fetch_or (1 << 15);
        const int cnt = (int) disp[3].load (std::memory_order_relaxed);
        if (cnt != last) { last = cnt; chg.fire ((int) pulseSamples (sr)); }
        o[0] = chg.tick(); o[1] = disp[1].load (std::memory_order_relaxed);
    }
private:
    Edge nx; Pulse chg; int last = 0;
};

// =============================================================================
const std::vector<std::string> kShapes { "SINE", "TRI", "RAMP", "SQUARE", "S&H", "DRIFT" };
const std::vector<std::string> kRates { "4 BAR", "2 BAR", "1 BAR", "1/2", "1/4", "1/8", "1/16" };
const double kRateBeats[] = { 16, 8, 4, 2, 1, 0.5, 0.25 };
TypeSpec specModLfo()
{
    B b ("MOD LFO", "MODULATOR", "Two tempo-locked modulators and a random source; route their outputs from the port ticks", Cat::Control, 0xffc8bfa6, 4);
    b.choice ("ashape", "A SHAPE", kShapes, 0, 0, 0, 2); b.fixed();
    b.choice ("arate", "A RATE", kRates, 2, 2, 0, 2); b.fixed();
    b.knob ("adepth", "A DEPTH", 0, 1, 0.8f, 4, 0); b.fixed();
    b.knob ("aofs", "A OFFSET", 0, 1, 0.5f, 6, 0); b.fixed();
    b.choice ("bshape", "B SHAPE", kShapes, 4, 0, 1, 2); b.fixed();
    b.choice ("brate", "B RATE", kRates, 5, 2, 1, 2); b.fixed();
    b.knob ("bdepth", "B DEPTH", 0, 1, 0.6f, 0, 2); b.fixed();
    b.knob ("bofs", "B OFFSET", 0, 1, 0.5f, 2, 2); b.fixed();
    b.knob ("phase", "B PHASE", 0, 1, 0.25f, 4, 2); b.fixed();
    b.knob ("smooth", "SMOOTH", 0, 1, 0.5f, 6, 2, Map::Lin, "", "Glide of the S&H / DRIFT shapes"); b.fixed();
    b.custom (0, 0, 4, 8, 4);
    b.in ("rst", "RESET", PortKind::Pulse);
    b.out ("a", "A", PortKind::CV); b.out ("b", "B", PortKind::CV); b.out ("rnd", "RANDOM", PortKind::CV, "Smooth random at A's rate"); b.out ("ab", "A x B", PortKind::CV);
    return b.t;
}
class ModLfo : public Module
{
public:
    ModLfo() : Module (spec()) {}
    static const TypeSpec& spec() { static TypeSpec t = specModLfo(); return t; }
    void block (const Ctx& cx) override { Module::block (cx); k = 0; }
    void tick (const float* in, float* o) override
    {
        if (rs.rise (in[0])) ofs = beat;
        beat = ctx->running ? (ctx->pos16 + ctx->pos16PerSample * k) / 4.0 : beat + ctx->bpm / 60.0 / sr;
        const double b0 = beat - ofs;
        const float sm = 1.0f - std::exp (-1.0f / ((0.001f + c (P ("smooth")) * 0.25f) * sr));
        auto wave = [&] (int shape, double cyc, uint32_t seed, float& held)
        {
            const double ph = cyc - std::floor (cyc);
            const int32_t n = (int32_t) std::floor (cyc);
            switch (shape)
            {
                case 0: return (float) std::sin (ph * 6.283185307);
                case 1: return (float) (ph < 0.5 ? ph * 4.0 - 1.0 : 3.0 - ph * 4.0);
                case 2: return (float) (ph * 2.0 - 1.0);
                case 3: return ph < 0.5 ? 1.0f : -1.0f;
                case 4: { const float t = hashf (seed, n) * 2.0f - 1.0f; held += (t - held) * sm; return held; }
                default: { const float a = hashf (seed, n) * 2.0f - 1.0f, b2 = hashf (seed, n + 1) * 2.0f - 1.0f; const float f = (float) ph; return a + (b2 - a) * f * f * (3.0f - 2.0f * f); }
            }
        };
        const double ca = b0 / kRateBeats[(int) c (P ("arate"))], cb = b0 / kRateBeats[(int) c (P ("brate"))] + c (P ("phase"));
        const float a = wave ((int) c (P ("ashape")), ca, 11u, ha), bb = wave ((int) c (P ("bshape")), cb, 23u, hb), rnd = wave (5, ca, 37u, hr);
        o[0] = clamp01 (c (P ("aofs")) + a * c (P ("adepth")) * 0.5f);
        o[1] = clamp01 (c (P ("bofs")) + bb * c (P ("bdepth")) * 0.5f);
        o[2] = clamp01 (0.5f + rnd * 0.5f);
        o[3] = o[0] * o[1];
        if (++dk >= 256) { dk = 0; hist = (hist + 1) % 24; disp[(size_t) (8 + hist)].store (o[0]); disp[(size_t) (32 + hist)].store (o[1]); disp[0].store ((float) hist); }
        ++k;
    }
private:
    int P (const char* k2) const { return spec().param (k2); }
    Edge rs; double beat = 0, ofs = 0; float ha = 0, hb = 0, hr = 0; int k = 0, dk = 0, hist = 0;
};
} // namespace

std::unique_ptr<Module> createControl (const std::string& id)
{
    if (id == "TRANSPORT") return std::make_unique<Transport>();
    if (id == "MIXER") return std::make_unique<Mixer>();
    if (id == "PATTERNS") return std::make_unique<Patterns>();
    if (id == "LOOP PACK") return std::make_unique<LoopPack>();
    if (id == "WANDER") return std::make_unique<Wander>();
    if (id == "CHAOS") return std::make_unique<Chaos>();
    if (id == "SCENES") return std::make_unique<Scenes>();
    if (id == "MOD LFO") return std::make_unique<ModLfo>();
    return nullptr;
}
void controlSpecs (std::vector<const TypeSpec*>& v)
{
    v.push_back (&Transport::spec()); v.push_back (&Mixer::spec()); v.push_back (&Patterns::spec()); v.push_back (&LoopPack::spec());
    v.push_back (&Wander::spec()); v.push_back (&Chaos::spec()); v.push_back (&Scenes::spec()); v.push_back (&ModLfo::spec());
}

// generic access the processor needs without knowing the classes
int transportExternalRun (Module* m) { if (auto* t = dynamic_cast<Transport*> (m)) return t->externalRun.load(); return -1; }
int patternsSelect (Module* m) { if (auto* p = dynamic_cast<Patterns*> (m)) return p->selectRequest.exchange (-1); return -1; }
float wanderSpeedCv (Module* m) { if (auto* w = dynamic_cast<Wander*> (m)) return w->speedCv.load(); return 0; }
int wanderPlayGate (Module* m) { if (auto* w = dynamic_cast<Wander*> (m)) return w->playGate.load(); return -1; }
float chaosAmountCv (Module* m) { if (auto* c = dynamic_cast<Chaos*> (m)) return c->amtCv.load(); return 0; }

// ---------------------------------------------------------------------------
const std::vector<const TypeSpec*>& Registry::types()
{
    static const std::vector<const TypeSpec*> t = []
    {
        std::vector<const TypeSpec*> v;
        std::vector<const TypeSpec*> c; controlSpecs (c);
        v.push_back (c[0]); v.push_back (c[1]);
        drumSpecs (v); synthSpecs (v); instr2Specs (v); upstreamSpecs (v); fxSpecs (v);
        for (size_t i = 2; i < c.size(); ++i) v.push_back (c[i]);
        return v;
    }();
    return t;
}
const TypeSpec* Registry::find (const std::string& id) { for (auto* t : types()) if (t->id == id) return t; return nullptr; }
std::unique_ptr<Module> Registry::create (const std::string& id)
{
    if (auto m = createDrum (id)) return m;
    if (auto m = createSynth (id)) return m;
    if (auto m = createFx (id)) return m;
    if (auto m = createInstr2 (id)) return m;
    if (auto m = createUpstream (id)) return m;
    return createControl (id);
}
} // namespace nms
