// SEMI Family port: native substitutes for the original Schwung modules (see P::substitute)
// and null-safe set / cable; the patches themselves are unchanged.
// Factory patches after the NMS prelaunch box: Blank Slate, Dub Techno,
// Glitch Percussion, Poly Bells, Fast Landscape Slow Water, plus two that show
// cable patching. The processor adds default macro routes and deals the
// patterns (FactoryPatch::patterns, seeded) after building.
#include "Rack.h"

namespace nms
{
namespace
{
struct P
{
    Rack& r;
    explicit P (Rack& rk) : r (rk) { r.clear(); }
    // SEMI Family port: the original Schwung modules (SIGNAL, ESSAIM, BREAKBEAT, STRUCTOR,
    // DISSOLVER) are not compiled here; their places in the factory patches take native modules.
    static const char* substitute (const char* type)
    {
        static const char* const map[][2] = { { "SIGNAL", "SKIN" }, { "ESSAIM", "CHOWNING" }, { "BREAKBEAT", "DUFFY" },
                                              { "STRUCTOR", "SINKHOLE" }, { "DISSOLVER", "BAIKAL" } };
        if (Registry::find (type) != nullptr) return type;
        for (auto& e : map) if (std::string (type) == e[0]) return e[1];
        return type;
    }
    Module* add (const char* type, int ch = -1)
    {
        auto m = r.make (substitute (type)); auto* p = m.get(); if (p == nullptr) return nullptr; r.mods.push_back (m);
        if (ch >= 0) set (p, "ch", (float) ch);
        return p;
    }
    void set (Module* m, const char* key, float plain) { if (m == nullptr) return; const int i = m->spec.param (key); if (i >= 0) m->setPlain (i, plain); }
    void setN (Module* m, const char* key, float norm) { if (m == nullptr) return; const int i = m->spec.param (key); if (i >= 0) m->setNorm (i, norm); }
    void cable (Module* a, const char* out, Module* b, const char* in, int col)
    {
        if (a == nullptr || b == nullptr) return;
        const int o = a->spec.out (out), i = b->spec.in (in);
        if (o >= 0 && i >= 0) r.cables.push_back ({ a->id, o, b->id, i, col });
    }
    Module* mix() { return r.findType ("MIXER"); }
    void level (int ch, float v) { set (mix(), ("lvl" + std::to_string (ch + 1)).c_str(), v); }
    void pan (int ch, float v) { set (mix(), ("pan" + std::to_string (ch + 1)).c_str(), v); }
    ~P() { r.compile(); }
};

Module* frame (P& p, float tempo, int key, bool minor, int notes)
{
    auto* t = p.add ("TRANSPORT");
    p.set (t, "tempo", tempo); p.set (t, "key", (float) key); p.set (t, "minor", minor ? 1.0f : 0.0f); p.set (t, "notes", (float) notes);
    return t;
}

void blank (Rack& rk) { P p (rk); frame (p, 120, 0, true, 5); p.add ("MIXER"); p.add ("PATTERNS"); }

void dub (Rack& rk)
{
    P p (rk);
    frame (p, 118, 5, true, 5);
    p.add ("MIXER"); p.add ("PATTERNS"); p.add ("LOOP PACK");
    auto* k = p.add ("DUFFY", 0); p.set (k, "boom", 0.4f); p.set (k, "len", 0.45f);
    auto* h = p.add ("HATTERSLEY", 1); p.set (h, "base", 0.55f); p.set (h, "offset", 2); p.set (h, "len", 0.05f);
    auto* c = p.add ("HANDMAID", 2); p.set (c, "base", 0.12f);
    auto* s = p.add ("SAWBIRD", 3);
    p.set (s, "cutoff", 520); p.set (s, "reson", 0.25f); p.set (s, "fenv", 0.55f); p.set (s, "dec", 0.28f); p.set (s, "range", 0.0f);
    p.set (s, "inten", 0.32f); p.set (s, "tcyc", 1); p.set (s, "pitch", 0); p.set (s, "sub", 0.0f);
    auto* sr = p.add ("ROISIN", 3); p.set (sr, "fb", 0.6f); p.set (sr, "spread", 0.55f);
    auto* sd = p.add ("PYONGYANG", 3); p.set (sd, "time", 2); p.set (sd, "fb", 0.72f); p.set (sd, "lvl", 0.7f); p.set (sd, "throw", 0.5f);
    auto* sv = p.add ("BAIKAL", 3); p.set (sv, "lvl", 0.45f); p.set (sv, "len", 86);
    auto* b = p.add ("CHOWNING", 4); p.set (b, "pitch", -12); p.set (b, "ratio", 1); p.set (b, "idx", 0.2f); p.set (b, "d1", 0.35f); p.set (b, "inten", 0.4f); p.set (b, "range", 0.15f);
    auto* bd = p.add ("PYONGYANG", 4); p.set (bd, "lvl", 0.25f); p.set (bd, "time", 3);
    // the shared dub space on the send / return lanes
    auto* ra = p.add ("BAIKAL", 8); p.set (ra, "lvl", 1.0f); p.set (ra, "len", 84); p.set (ra, "size", 24);
    auto* rb = p.add ("PYONGYANG", 9); p.set (rb, "lvl", 1.0f); p.set (rb, "time", 2); p.set (rb, "fb", 0.65f);
    p.set (p.mix(), "sa2", 0.35f); p.set (p.mix(), "sa3", 0.6f); p.set (p.mix(), "sa4", 0.25f); p.set (p.mix(), "sb3", 0.3f);
    p.level (0, 0.78f); p.level (1, 0.55f); p.level (2, 0.55f); p.level (3, 0.62f); p.level (4, 0.66f);
    p.pan (1, 0.25f); p.pan (2, -0.2f);
}

void glitch (Rack& rk)
{
    P p (rk);
    frame (p, 132, 9, true, 5);
    p.add ("MIXER"); p.add ("PATTERNS"); p.add ("LOOP PACK");
    auto* k = p.add ("DUFFY", 0); p.set (k, "base", 0.55f); p.set (k, "sptn", 0.42f); p.set (k, "oint", 0.3f); p.set (k, "sat", 0.6f);
    auto* sn = p.add ("AARON", 1); p.set (sn, "oint", 0.5f); p.set (sn, "omod", 0.85f);
    auto* c = p.add ("HANDMAID", 2); p.set (c, "base", 0.3f); p.set (c, "sptn", 0.2f);
    auto* h = p.add ("HATTERSLEY", 3); p.set (h, "mult", 3); p.set (h, "base", 0.5f); p.set (h, "grit", 0.5f);
    for (int ch = 0; ch < 4; ++ch)
    {
        auto* g = p.add ("SINKHOLE", ch);
        p.set (g, "cen", 400.0f * (float) (ch + 1)); p.set (g, "lvl", 0.4f + 0.1f * (float) ch); p.set (g, "dbl", 0.2f + 0.15f * (float) ch);
        p.set (g, "aenv", 0.3f); p.set (g, "time", 0.03f + 0.03f * (float) ch);
    }
    p.level (0, 0.76f); p.level (1, 0.62f); p.level (2, 0.55f); p.level (3, 0.5f);
    p.pan (2, -0.3f); p.pan (3, 0.3f);
}

void bells (Rack& rk)
{
    P p (rk);
    frame (p, 100, 2, false, 7);
    p.add ("MIXER"); p.add ("PATTERNS"); p.add ("LOOP PACK");
    for (int i = 0; i < 3; ++i)
    {
        auto* b = p.add ("CHOWNING", i);
        p.set (b, "pitch", (float) (12 * (i - 1) + 12)); p.set (b, "ratio", (float) (i == 0 ? 7 : (i == 1 ? 5 : 11)));
        p.set (b, "idx", 0.25f + 0.12f * (float) i); p.set (b, "d1", 1.6f + (float) i); p.set (b, "inten", 0.25f + 0.08f * (float) i);
        p.set (b, "tcyc", (float) (4 + i * 2)); p.setN (b, "ptn", 0.2f + 0.27f * (float) i); p.set (b, "pan", 0.8f);
        auto* d = p.add ("PYONGYANG", i); p.set (d, "time", (float) (1 + i)); p.set (d, "lvl", 0.35f); p.set (d, "fb", 0.5f);
        auto* v = p.add ("BAIKAL", i); p.set (v, "lvl", 0.5f); p.set (v, "len", 88);
        p.level (i, 0.6f);
    }
    p.pan (0, -0.35f); p.pan (2, 0.35f);
}

void landscape (Rack& rk)
{
    P p (rk);
    frame (p, 84, 7, true, 5);
    p.add ("MIXER"); p.add ("PATTERNS"); p.add ("WANDER"); auto* ch = p.add ("CHAOS");
    p.set (ch, "amt", 0.08f); p.set (ch, "rate", 0.15f);
    auto* s = p.add ("SCHAEFER", 0); p.set (s, "spd", 0.5f); p.set (s, "glide", 0.4f); p.set (s, "dec", 0.9f); p.set (s, "inten", 0.4f);
    auto* sk = p.add ("SINKHOLE", 0); p.set (sk, "lvl", 0.35f);
    auto* sv = p.add ("BAIKAL", 0); p.set (sv, "lvl", 0.7f); p.set (sv, "len", 92); p.set (sv, "size", 30);
    auto* k = p.add ("DUFFY", 1); p.set (k, "base", 0.3f); p.set (k, "pmask", 0.6f); p.set (k, "boom", 0.6f);
    auto* kv = p.add ("BAIKAL", 1); p.set (kv, "lvl", 0.3f);
    auto* b = p.add ("CHOWNING", 2); p.set (b, "inten", 0.2f); p.set (b, "d1", 3.0f); p.set (b, "ratio", 4); p.set (b, "range", 0.6f);
    auto* r = p.add ("ROISIN", 2); p.set (r, "fb", 0.9f);
    auto* d = p.add ("PYONGYANG", 2); p.set (d, "lvl", 0.6f); p.set (d, "fb", 0.8f); p.set (d, "time", 4);
    auto* sk2 = p.add ("SKIN", 3); p.set (sk2, "base", 0.25f); p.set (sk2, "ring", 0.6f);
    auto* sv2 = p.add ("BAIKAL", 3); p.set (sv2, "lvl", 0.55f);
    p.level (0, 0.62f); p.level (1, 0.6f); p.level (2, 0.6f); p.level (3, 0.5f); p.pan (3, 0.3f);
}

// cable patching: the keyboard plays SAWBIRD through TRANSPORT's KEY outputs
void keys (Rack& rk)
{
    P p (rk);
    auto* t = frame (p, 120, 0, true, 7);
    p.add ("MIXER");
    auto* s = p.add ("SAWBIRD", 0);
    p.set (s, "range", 0.0f); p.set (s, "pitch", 0); p.set (s, "dec", 0.6f); p.set (s, "glide", 0.15f);
    auto* d = p.add ("PYONGYANG", 0); p.set (d, "lvl", 0.35f);
    auto* v = p.add ("BAIKAL", 0); p.set (v, "lvl", 0.3f);
    p.cable (t, "kt", s, "trig", 1); p.cable (t, "kp", s, "pitch", 4); p.cable (t, "kv", s, "acc", 2);
}

// cable patching: DIDIMIDI's pitch and gate play SAWBIRD, its CUT output opens ROISIN
void cables (Rack& rk)
{
    P p (rk);
    frame (p, 112, 9, true, 5);
    p.add ("MIXER"); p.add ("PATTERNS");
    auto* m = p.add ("DIDIMIDI", 0); p.set (m, "prev", 0); p.set (m, "inten", 0.55f); p.set (m, "range", 0.5f);
    auto* s = p.add ("SAWBIRD", 1); p.set (s, "range", 0.0f); p.set (s, "pitch", 0);
    auto* r = p.add ("ROISIN", 1); p.set (r, "fb", 0.8f);
    auto* k = p.add ("DUFFY", 2);
    p.cable (m, "trig", s, "trig", 0); p.cable (m, "pitch", s, "pitch", 4); p.cable (m, "vel", s, "acc", 2);
    p.cable (m, "cut", r, "mix", 3); p.cable (k, "trig", r, "trig", 5);
}
// BREAKBEAT, UNDERTOW, SIGNAL, SCENES: a structured break track with the original
// Schwung modules; the bass avoids the break, SIGNAL follows it, STRUCTOR cuts the
// break up, DISSOLVER smears the send, MOD LFO into the bass
void organicBreaks (Rack& rk)
{
    P p (rk);
    frame (p, 168, 9, true, 5);
    p.add ("MIXER"); p.add ("PATTERNS"); p.add ("LOOP PACK");
    auto* sc = p.add ("SCENES"); p.set (sc, "play", 1); p.set (sc, "b1", 3); p.set (sc, "b2", 2); p.set (sc, "b3", 2); p.set (sc, "b4", 1);
    auto* w = p.add ("WANDER"); p.set (w, "speed", 0.6f); p.set (w, "depth", 0.2f);
    auto* lfo = p.add ("MOD LFO"); p.set (lfo, "arate", 3);
    auto* br = p.add (rk.canAdd ("BREAKBEAT") ? "BREAKBEAT" : "DUFFY", 0); p.set (br, "aware", 0.3f);
    auto* st = p.add ("STRUCTOR", 0); p.set (st, "mix", 0.2f);
    auto* bass = p.add ("UNDERTOW", 1); p.set (bass, "mode", 1); p.set (bass, "link", 1); p.set (bass, "lch", 0); p.set (bass, "lamt", 0.7f); p.set (bass, "aware", 0.5f);
    auto* sg = p.add ("SIGNAL", 2); p.set (sg, "link", 2); p.set (sg, "lch", 0); p.set (sg, "lamt", 0.3f); p.set (sg, "aware", 0.5f);
    auto* ds = p.add ("DISSOLVER", 8); p.set (ds, "mix", 1.0f);
    auto* rv = p.add ("BAIKAL", 9); p.set (rv, "lvl", 1.0f); p.set (rv, "len", 80);
    p.set (p.mix(), "sa2", 0.2f); p.set (p.mix(), "sa3", 0.45f); p.set (p.mix(), "sb1", 0.15f); p.set (p.mix(), "sb3", 0.3f);
    p.level (0, 0.8f); p.level (1, 0.7f); p.level (2, 0.45f); p.pan (2, 0.25f);
    p.cable (lfo, "a", bass, "vary", 1);
}

// ESSAIM through DISSOLVER, SIGNAL cut up by STRUCTOR, a percussion that avoids
// SIGNAL; everything AWARE of the energy, WANDER drifting
void swarmGarden (Rack& rk)
{
    P p (rk);
    frame (p, 92, 2, false, 7);
    p.add ("MIXER"); p.add ("PATTERNS");
    auto* w = p.add ("WANDER"); p.set (w, "mode", 1); p.set (w, "speed", 0.3f); p.set (w, "depth", 0.4f);
    auto* ch = p.add ("CHAOS"); p.set (ch, "amt", 0.06f); p.set (ch, "rate", 0.1f);
    auto* es = p.add ("ESSAIM", 0); p.set (es, "aware", 0.5f); p.set (es, "breath", 0.5f);
    auto* ds = p.add ("DISSOLVER", 0); p.set (ds, "mix", 0.45f); p.set (ds, "aware", 0.4f);
    auto* sg = p.add ("SIGNAL", 1); p.set (sg, "aware", 0.5f);
    auto* st = p.add ("STRUCTOR", 1); p.set (st, "mix", 0.45f);
    auto* sk = p.add ("SKIN", 2); p.set (sk, "base", 0.5f); p.set (sk, "aware", 0.3f);
    auto* rv = p.add ("BAIKAL", 8); p.set (rv, "lvl", 1.0f); p.set (rv, "len", 90); p.set (rv, "size", 32); p.set (rv, "aware", 0.7f);
    p.set (p.mix(), "sa1", 0.4f); p.set (p.mix(), "sa2", 0.3f); p.set (p.mix(), "sa3", 0.35f);
    p.level (0, 0.6f); p.level (1, 0.5f); p.level (2, 0.55f); p.pan (1, -0.3f); p.pan (2, 0.3f);
}
} // namespace

const std::vector<FactoryPatch>& factoryPatches()
{
    static const std::vector<FactoryPatch> f {
        { "Dub Techno", "LOOP PACK", dub, 12, 11 }, { "Glitch Percussion", "LOOP PACK", glitch, 12, 23 }, { "Poly Bells", "LOOP PACK", bells, 10, 37 },
        { "Fast Landscape Slow Water", "GENERATIVE", landscape, 8, 41 }, { "Organic Breaks", "SCENES", organicBreaks, 8, 53 },
        { "Swarm Garden", "GENERATIVE", swarmGarden, 6, 67 }, { "Keys > Sawbird", "CABLES", keys, 0, 5 }, { "Didimidi Drives", "CABLES", cables, 4, 7 },
        { "Blank Slate", "INIT", blank, 0, 1 } };
    return f;
}
void buildPatch (Rack& r, int i) { const auto& f = factoryPatches(); if (i >= 0 && i < (int) f.size()) f[(size_t) i].build (r); }
} // namespace nms
