// NMS — the instrument around the rack (SEMI Family port, see Host.h).
// Adapted from Source/PluginProcessor.cpp; JUCE calls replaced (jlimit / jmin -> std,
// juce::Random -> JRandom, SpinLock / MidiBuffer / APVTS removed).
#include "Host.h"
#include <cstdio>
#include <cstring>
#include <limits>

namespace nms
{
namespace
{
bool patternable (const Module& m) { return m.spec.cat != Cat::System && m.spec.cat != Cat::Control; }
template <typename T> T lim (T lo, T hi, T v) { return v < lo ? lo : (v > hi ? hi : v); }
constexpr double kTwoPiD = 6.283185307179586;
}

// ---------------------------------------------------------------------------- build
bool Host::build (int index, double sr)
{
    const auto& f = factoryPatches();
    index = lim (0, (int) f.size() - 1, index);
    sampleRate = sr > 1000.0 ? sr : 48000.0;
    ctx = Ctx();
    ctx.sr = sampleRate;
    rack.prepare (sampleRate);
    buildPatch (rack, index);
    if (rack.mods.empty()) return false;
    routes.clear();
    for (auto& m : rack.mods) addDefaultRoutes (*m);
    dealPatterns (f[(size_t) index].patterns, f[(size_t) index].seed);
    for (auto& m : macro) m = 0.5f;
    patchIndex = index;
    patchName = f[(size_t) index].name;
    buildView();
    return true;
}

void Host::addDefaultRoutes (const Module& m)
{
    for (size_t i = 0; i < m.n; ++i)
    {
        const auto& p = m.spec.params[i];
        if (p.macro >= 0 && routes.size() < 2000) routes.push_back ({ 1, p.macro, m.id, (int) i, p.macroAmt });
    }
}

Pattern Host::snapshot() const
{
    Pattern p;
    for (auto& m : rack.mods)
    {
        if (! patternable (*m)) continue;
        std::vector<float> v (m->n, std::numeric_limits<float>::quiet_NaN());
        for (size_t i = 0; i < m->n; ++i) if (m->spec.params[i].ptn) v[i] = m->getNorm ((int) i);
        p.values[m->id] = std::move (v);
    }
    return p;
}

void Host::dealPatterns (int count, uint32_t seed)
{
    patterns.clear();
    if (count <= 0) { currentPattern = -1; return; }
    const auto base = snapshot();
    JRandom r ((int64_t) seed * 7919);
    for (int k = 0; k < count; ++k)
    {
        Pattern p = base;
        p.hue = std::fmod (0.28f + 0.037f * (float) k, 1.0f);
        if (k > 0)
            for (auto& [id, vals] : p.values)
            {
                auto* m = rack.find (id);
                if (! m) continue;
                const float depth = 0.15f + 0.35f * (float) (k % 4) / 3.0f;
                for (size_t i = 0; i < vals.size(); ++i)
                {
                    const auto& ps = m->spec.params[i];
                    if (std::isnan (vals[i]) || ! ps.rnd) continue;
                    const int steps = m->spec.steps ((int) i);
                    if (steps > 1) { if (r.nextFloat() < depth * 0.5f) vals[i] = lim (0.0f, 1.0f, vals[i] + (r.nextBool() ? 1.0f : -1.0f) / (float) (steps - 1)); }
                    else vals[i] = lim (0.0f, 1.0f, vals[i] + (r.nextFloat() - 0.5f) * depth);
                }
            }
        patterns.push_back (std::move (p));
    }
    currentPattern = 0;
}

void Host::buildView()
{
    view = RouteView();
    auto copyName = [] (char* dst, const std::string& s) { std::snprintf (dst, 11, "%s", s.c_str()); };
    for (auto& mp : rack.mods)
    {
        auto* m = mp.get();
        if (m->spec.chained)
        {
            const int l = lim (0, kLanes - 1, m->channel());
            auto& ln = view.lanes[l];
            if (ln.n < RouteView::kPerLane) { copyName (ln.names[ln.n], m->spec.id); ln.fx[ln.n] = m->spec.cat == Cat::Fx; ++ln.n; }
        }
        else if (m->spec.id != "MIXER" && view.numTop < RouteView::kTop)
            copyName (view.top[view.numTop++], m->spec.id == "TRANSPORT" ? std::string ("TRANSPORT") : m->spec.id);
    }
    for (auto& c : rack.cables)
    {
        if (view.numCables >= RouteView::kCables) break;
        auto* s = rack.find (c.src); auto* d = rack.find (c.dst);
        if (! s || ! d) continue;
        auto& v = view.cables[view.numCables++];
        v.srcLane = s->spec.chained ? lim (0, kLanes - 1, s->channel()) : -1;
        v.dstLane = d->spec.chained ? lim (0, kLanes - 1, d->channel()) : -1;
        std::snprintf (v.label, sizeof v.label, "%s>%s", s->spec.outs[(size_t) c.out].name.c_str(), d->spec.ins[(size_t) c.in].name.c_str());
    }
    for (int c = 0; c < kChannels; ++c)
    {
        view.trigMod[c] = -1; view.trigOut[c] = -1;
        if (auto* src = rack.laneSrc[(size_t) c])
        {
            view.trigMod[c] = rack.index (src->id);
            view.trigOut[c] = src->spec.out ("trig");
        }
    }
}

// ---------------------------------------------------------------------------- patterns
void Host::recall (int index, double seconds, int type, bool fromWander)
{
    if (index < 0 || index >= (int) patterns.size()) return;
    if (type < 0) { auto* p = rack.findType ("PATTERNS"); type = p ? (int) param (p, "type") : 0; }
    blendItems.clear();
    for (auto& [id, vals] : patterns[(size_t) index].values)
    {
        auto* m = rack.find (id);
        if (m == nullptr || m->n != vals.size()) continue;
        for (size_t i = 0; i < vals.size(); ++i)
        {
            if (std::isnan (vals[i])) continue;
            const auto& ps = m->spec.params[i];
            if ((type == 1 && ps.sound) || (type == 2 && ! ps.sound)) continue;
            float to = vals[i], br = 0.0f;
            if (fromWander)
            {
                const int wd = m->spec.param ("wdepth"), bp = m->spec.param ("breath");
                if (wd >= 0) to = m->getNorm ((int) i) + (to - m->getNorm ((int) i)) * m->getPlain (wd);
                if (bp >= 0) br = m->getPlain (bp) * ((i % 2) ? 1.0f : -1.0f);
            }
            if (blendItems.size() < blendItems.capacity()) blendItems.push_back ({ m, (int) i, m->getNorm ((int) i), to, br });
        }
    }
    blendDur = seconds; blendT = 0.0;
    if (seconds <= 0.01) stepBlend (0);
    currentPattern = index;
    ++patternChanges;
}

void Host::recallBlended (int index)
{
    double secs = 0.5;
    if (auto* p = rack.findType ("PATTERNS")) secs = param (p, "blend");
    recall (index, secs);
}

void Host::nextPattern (int dir)
{
    const int np = (int) patterns.size();
    if (np > 0) recallBlended (((currentPattern + dir) % np + np) % np);
}

void Host::randomPattern()
{
    const int np = (int) patterns.size();
    if (np > 0) recallBlended (rnd.nextInt (np));
}

void Host::stepBlend (int n)
{
    if (blendT >= 1.0) return;
    blendT = blendDur <= 0.01 ? 1.0 : std::min (1.0, blendT + (double) n / (sampleRate * blendDur));
    const float t = (float) blendT, s = t * t * (3.0f - 2.0f * t);
    for (auto& b : blendItems)
    {
        const auto& ps = b.m->spec.params[(size_t) b.p];
        const int steps = b.m->spec.steps (b.p);
        float v = b.from + (b.to - b.from) * s + b.breath * 0.25f * std::sin (t * kPi);
        if (steps > 1 && ps.kind != Kind::Knob && ps.kind != Kind::Big) v = s < 0.5f ? b.from : b.to;   // switches flip half way
        b.m->setNorm (b.p, v);
    }
    blendPos = t;
}

void Host::randomize (int amount, int type)
{
    auto& r = rnd;
    for (auto& m : rack.mods)
    {
        if (! patternable (*m)) continue;
        for (size_t i = 0; i < m->n; ++i)
        {
            const auto& p = m->spec.params[i];
            if (! p.rnd || p.kind == Kind::Momentary) continue;
            if ((type == 1 && p.sound) || (type == 2 && ! p.sound)) continue;
            const int steps = m->spec.steps ((int) i);
            const float v = m->getNorm ((int) i);
            float nv = v;
            if (steps > 0)
            {
                const float st = 1.0f / (float) std::max (1, steps - 1);
                if (amount == 0) { if (r.nextFloat() < 0.08f) nv = v + (r.nextBool() ? st : -st); }
                else if (amount == 1) { if (r.nextFloat() < 0.3f) nv = v + (r.nextBool() ? st : -st); }
                else nv = (float) r.nextInt (steps) * st;
            }
            else if (amount == 0) nv = v + (r.nextFloat() - 0.5f) * 0.08f;
            else if (amount == 1) { const float b0 = std::floor (v * 4.0f) / 4.0f; nv = b0 + r.nextFloat() * 0.25f; }
            else nv = r.nextFloat();
            m->setNorm ((int) i, clamp01 (nv));
        }
    }
}

// ---------------------------------------------------------------------------- keys (midiEvent)
void Host::noteOn (int note, float vel)
{
    auto* lp = rack.findType ("LOOP PACK");
    const int base = lp ? 24 + 12 * (int) param (lp, "base") : 1000;
    if (note >= base)
    {
        const int idx = note - base;
        if (numPatternKeys == 0) previousPattern = currentPattern;
        int k = 0;
        for (int i = 0; i < numPatternKeys; ++i) if (patternKeys[i] != idx) patternKeys[k++] = patternKeys[i];
        numPatternKeys = k;
        if (numPatternKeys < 16) patternKeys[numPatternKeys++] = idx;
        heldPatternKeys = numPatternKeys;
        recall (idx, param (lp, "blend"));
        return;
    }
    int k = 0;
    for (int i = 0; i < numHeld; ++i) if (held[i] != note) held[k++] = held[i];
    numHeld = k;
    if (numHeld < 16) held[numHeld++] = note;
    ctx.keyPitch = (float) (note - 48) / 12.0f; ctx.keyGate = 1.0f; ctx.keyVel = vel; ++ctx.keyTrig;
}

void Host::noteOff (int note)
{
    auto* lp = rack.findType ("LOOP PACK");
    const int base = lp ? 24 + 12 * (int) param (lp, "base") : 1000;
    if (note >= base)
    {
        int k = 0;
        for (int i = 0; i < numPatternKeys; ++i) if (patternKeys[i] != note - base) patternKeys[k++] = patternKeys[i];
        numPatternKeys = k;
        heldPatternKeys = numPatternKeys;
        if (numPatternKeys == 0)
        {
            if (param (lp, "hold") > 0.5f && previousPattern >= 0) recall (previousPattern, param (lp, "blend"));
            if (param (lp, "auto") > 0.5f) macroResetRequest = true;
        }
        else recall (patternKeys[numPatternKeys - 1], param (lp, "blend"));
        return;
    }
    int k = 0;
    for (int i = 0; i < numHeld; ++i) if (held[i] != note) held[k++] = held[i];
    numHeld = k;
    if (numHeld == 0) ctx.keyGate = 0.0f;
    else ctx.keyPitch = (float) (held[numHeld - 1] - 48) / 12.0f;
}

// ---------------------------------------------------------------------------- transport
double Host::warp (double x) const
{
    if (wonk > 0.001f)
    {
        const double y = x / wonkDiv, b = std::floor (y);
        double g = y - b;
        g += wonk * 0.12 * std::sin (kTwoPiD * g);
        x = (b + g) * wonkDiv;
    }
    if (swing > 0.001f)
    {
        const double u = swingUnit, c = x / (2.0 * u), b = std::floor (c);
        const double f = (c - b) * 2.0, p = 1.0 + swing * 0.5;
        const double f2 = f < p ? f / p : 1.0 + (f - p) / (2.0 - p);
        x = (b * 2.0 + f2) * u;
    }
    return x;
}

void Host::transport (const ExtTransport& e)
{
    auto* t = rack.findType ("TRANSPORT");
    double bpm = 120.0; bool running = true;
    const bool hostLocked = e.known && e.playing;
    if (t)
    {
        bpm = e.known ? e.bpm : (double) param (t, "tempo");
        running = e.known ? e.playing : param (t, "run") > 0.5f;
        const int ext = transportExternalRun (t);
        if (ext >= 0) running = ext == 1;
        if (t->request.fetch_and (~rqRestart) & rqRestart) { rawPos = 0.0; ++ctx.restart; }
        swing = param (t, "swing"); wonk = param (t, "wonk"); swingUnit = param (t, "tswing") > 0.5f ? 2 : 1;
        static const double wd[] = { 2, 4, 8, 16, 32 };
        wonkDiv = wd[lim (0, 4, (int) param (t, "wdiv"))];
        buildScale (ctx, (int) param (t, "key"), param (t, "minor") > 0.5f, (int) param (t, "notes"), param (t, "mode") > 0.5f, (int) param (t, "npo"));
    }
    else buildScale (ctx, 0, true, 5, false, 12);
    if (restartPending) { restartPending = false; rawPos = 0.0; ++ctx.restart; }
    if (hostLocked) rawPos = e.pos16;
    if (running && ! wasRunning && ! hostLocked) { ++ctx.restart; rawPos = std::ceil (rawPos - 1e-9); }
    wasRunning = running;
    ctx.bpm = lim (20.0, 400.0, bpm);
    ctx.running = running;
}

// ---------------------------------------------------------------------------- modulation
void Host::modulation (int n)
{
    float mv[kMacros];
    for (int m = 0; m < kMacros; ++m) mv[m] = (macro[m] - 0.5f) * 2.0f;
    for (auto& mp : rack.mods) for (size_t i = 0; i < mp->n; ++i) mp->setMod ((int) i, 0.0f);
    for (auto& r : routes)
    {
        auto* m = rack.find (r.mod);
        if (m == nullptr || r.param < 0 || r.param >= (int) m->n) continue;
        m->setMod (r.param, m->modOffset (r.param) + r.amount * mv[lim (0, kMacros - 1, r.src)]);
    }
    // AWARE: the ENERGY bus moves each module along its macro assignments
    const float eb = (energy - 0.5f) * 2.0f;
    for (auto& mp : rack.mods)
    {
        auto* m = mp.get();
        const int ai = m->spec.param ("aware");
        if (ai < 0) continue;
        const float aw = m->getPlain (ai);
        if (aw < 0.001f) continue;
        for (size_t i = 0; i < m->n; ++i)
        {
            const auto& ps = m->spec.params[i];
            if (ps.macro < 0 || (ps.kind != Kind::Knob && ps.kind != Kind::Big) || m->spec.steps ((int) i) > 1) continue;
            const float dir = (ps.macro == 23 || ps.macro == 11) ? -1.0f : 1.0f;    // SPACE, DECAY
            m->setMod ((int) i, m->modOffset ((int) i) + aw * eb * ps.macroAmt * dir * 0.6f);
        }
    }
    // CHAOS: smooth (or stepped) value noise on every control of the chosen type. Without a
    // CHAOS module in the patch the panel knob drives it with CHAOS's defaults.
    timeSec += (double) n / sampleRate;
    auto* c = rack.findType ("CHAOS");
    const float amt = c ? lim (0.0f, 1.0f, param (c, "amt") + chaosAmountCv (c)) : chaosKnob;
    const int tgt = c ? (int) param (c, "tgt") : 2;
    const double t = timeSec * (c ? param (c, "rate") : 0.5f);
    const float sm = c ? param (c, "smooth") : 0.8f;
    if (amt > 0.001f)
        for (auto& mp : rack.mods)
        {
            auto* m = mp.get();
            if (! patternable (*m)) continue;
            for (size_t i = 0; i < m->n; ++i)
            {
                const auto& ps = m->spec.params[i];
                if (! ps.rnd || ps.kind == Kind::Momentary || (tgt == 1 && ps.sound) || (tgt == 2 && ! ps.sound) || m->spec.steps ((int) i) > 1) continue;
                const uint32_t seed = (uint32_t) (m->id * 131 + (int) i * 7 + 5);
                const auto k = (int32_t) std::floor (t);
                const float f = (float) (t - std::floor (t)), s = f * f * (3.0f - 2.0f * f);
                const float a = hashf (seed, k) * 2.0f - 1.0f, b = hashf (seed, k + 1) * 2.0f - 1.0f;
                m->setMod ((int) i, m->modOffset ((int) i) + amt * 0.5f * (a + (b - a) * s * sm));
            }
        }
}

void Host::wander (int n)
{
    auto* w = rack.findType ("WANDER");
    if (w == nullptr) return;
    bool play = param (w, "play") > 0.5f;
    const int gate = wanderPlayGate (w);
    if (gate >= 0) play = gate == 1;
    if (! play) return;
    const float speed = lim (0.0f, 1.0f, param (w, "speed") + wanderSpeedCv (w));
    const double periodBeats = 4.0 * std::exp2 (5.0 * (1.0 - speed));
    wanderPhase += (double) n / sampleRate * ctx.bpm / 60.0 / periodBeats;
    w->disp[0].store ((float) wanderPhase, std::memory_order_relaxed);
    if (wanderPhase < 1.0) return;
    wanderPhase -= std::floor (wanderPhase);
    const double secs = periodBeats * 60.0 / ctx.bpm * 0.7;
    const int tgt = (int) param (w, "tgt");
    bool inside = false;
    if (auto* sc = rack.findType ("SCENES")) inside = param (sc, "play") > 0.5f && param (sc, "winner") > 0.5f;
    if (param (w, "mode") < 0.5f && patterns.size() >= 2 && ! inside)
    {
        int idx = rnd.nextInt ((int) patterns.size());
        if (idx == currentPattern) idx = (idx + 1) % (int) patterns.size();
        recall (idx, secs, tgt, true);
    }
    else
    {   // DRIFT: wander away from where we are (SCENES with WANDER INSIDE: around the scene)
        blendItems.clear();
        const float depth = param (w, "depth");
        for (auto& mp : rack.mods)
        {
            auto* m = mp.get();
            if (! patternable (*m)) continue;
            for (size_t i = 0; i < m->n; ++i)
            {
                const auto& ps = m->spec.params[i];
                if (! ps.rnd || ! ps.ptn || (tgt == 1 && ps.sound) || (tgt == 2 && ! ps.sound)) continue;
                const float from = m->getNorm ((int) i);
                const int steps = m->spec.steps ((int) i);
                float to = from;
                const int wd = m->spec.param ("wdepth"), bp = m->spec.param ("breath");
                const float md = wd >= 0 ? m->getPlain (wd) : 1.0f;
                if (steps > 1) { if (rnd.nextFloat() < depth * 0.3f * md) to = lim (0.0f, 1.0f, from + (rnd.nextBool() ? 1.0f : -1.0f) / (float) (steps - 1)); }
                else to = lim (0.0f, 1.0f, from + (rnd.nextFloat() * 2.0f - 1.0f) * depth * 0.35f * md);
                const float br = bp >= 0 ? m->getPlain (bp) * (rnd.nextBool() ? 1.0f : -1.0f) : 0.0f;
                if (blendItems.size() < blendItems.capacity()) blendItems.push_back ({ m, (int) i, from, to, br });
            }
        }
        blendDur = secs; blendT = 0.0;
    }
    w->disp[3].store ((float) ++wanderChanges, std::memory_order_relaxed);
}

// SCENES: on every bar line, count down the scene; at its end recall the next one
void Host::scenes()
{
    auto* sc = rack.findType ("SCENES");
    if (sc == nullptr) { sceneIdx = -1; return; }
    char key[16];
    auto P = [&] (char c, int i) { std::snprintf (key, sizeof key, "%c%d", c, i & 31); return param (sc, key); };
    const int req = sc->request.exchange (0);
    if (req & rqSceneFirst) { sceneIdx = -1; sceneBarsLeft = 0; }
    if (ctx.restart != sceneRestart) { sceneRestart = ctx.restart; sceneIdx = -1; sceneBarsLeft = 0; sceneBar = -1; }
    if (param (sc, "play") < 0.5f || ! ctx.running) return;
    const long long bar = (long long) std::floor (ctx.pos16 / 16.0 + 1e-9);
    const bool nextReq = (req & rqSceneNext) != 0;
    if (bar != sceneBar || sceneIdx < 0)
    {
        if (bar != sceneBar) --sceneBarsLeft;
        sceneBar = bar;
        if (sceneBarsLeft <= 0 || sceneIdx < 0 || nextReq)
        {
            static const int bars[] = { 1, 2, 4, 8, 16 };
            int found = -1;
            for (int k = 1; k <= 16; ++k)
            {
                int i = sceneIdx + k;
                if (i >= 16) { if (param (sc, "loop") < 0.5f && sceneIdx >= 0) break; i -= 16; }
                const int pat = (int) P ('p', i + 1) - 1;
                if (pat >= 0 && pat < (int) patterns.size()) { found = i; break; }
            }
            if (found >= 0)
            {
                sceneIdx = found;
                sceneBars = bars[lim (0, 4, (int) P ('b', found + 1))];
                sceneBarsLeft = sceneBars;
                recall ((int) P ('p', found + 1) - 1, param (sc, "blend") * 240.0 / ctx.bpm, -1, false);
                wanderPhase = 0.0;
                sc->disp[3].store (sc->disp[3].load() + 1.0f);
            }
        }
    }
    sc->disp[0].store ((float) sceneIdx);
    const double inBar = ctx.pos16 / 16.0 - std::floor (ctx.pos16 / 16.0);
    sc->disp[1].store ((float) lim (0.0, 1.0, ((double) (sceneBars - sceneBarsLeft) + inBar) / std::max (1, sceneBars)));
}

// requests the plugin served on its message-thread timer (PATTERNS / CHAOS / LOOP PACK jacks
// and the PATTERNS select CV); the ones that allocate (add / delete patterns) are ignored
void Host::serviceRequests()
{
    for (auto& mp : rack.mods)
    {
        auto* m = mp.get();
        if (m->spec.id == "TRANSPORT" || m->spec.id == "SCENES") continue;
        const int bits = m->request.exchange (0);
        if (m->spec.id == "PATTERNS") { const int s = patternsSelect (m); if (s >= 0) recallBlended (s); }
        if (! bits) continue;
        if (bits & rqRandGen) randomize (2, 1);
        if (bits & rqRandSnd) randomize (2, 2);
        const int np = (int) patterns.size();
        if (np > 0)
        {
            if (bits & rqNext) nextPattern (1);
            if (bits & rqPrev) nextPattern (-1);
            if (bits & rqRandom) randomPattern();
        }
        if (bits & rqResetMacros) macroResetRequest = true;
        if (bits & rqChaosRoll) randomize (1);
    }
}

// ---------------------------------------------------------------------------- process
void Host::process (float* L, float* R, int n, const ExtTransport& ext)
{
    if (n <= 0) return;
    serviceRequests();
    transport (ext);
    for (int l = 0; l < 4; ++l) ctx.lfo[l] = 0.0f;
    modulation (n);
    wander (n);
    scenes();
    {   // ENERGY: activity of every generator, decaying over a bar, smoothed over two
        const double barSec = 240.0 / ctx.bpm, dt = (double) n / sampleRate;
        const float dec = (float) std::exp (-dt / barSec);
        float sum = 0; int gens = 0;
        for (auto& mp : rack.mods)
            if (patternable (*mp) && mp->spec.cat != Cat::Fx)
            {
                const float a = mp->activity.load (std::memory_order_relaxed) * dec;
                mp->activity.store (a, std::memory_order_relaxed);
                sum += std::min (1.0f, a / 8.0f); ++gens;
            }
        const float target = gens > 0 ? sum / (float) gens : 0.0f;
        energy += (target - energy) * (float) (1.0 - std::exp (-dt / (2.0 * barSec)));
        ctx.energy = energy;
    }

    const double inc = ctx.running ? ctx.bpm / 60.0 * 4.0 / sampleRate : 0.0;
    int pos = 0;
    while (pos < n)
    {
        const int len = std::min (n - pos, 32);
        const double a = warp (rawPos), b = warp (rawPos + inc * len);
        ctx.pos16 = a; ctx.pos16PerSample = (b - a) / len;
        rack.process (ctx, L + pos, R + pos, len);
        stepBlend (len);
        rawPos += inc * len;
        pos += len;
    }
    MidiOut sink[8];
    while (drainMidi (sink, 8) > 0) {}

    const float g = std::pow (10.0f, outDb / 20.0f);
    for (int i = 0; i < n; ++i)
    {
        float l = L[i] * g, r = R[i] * g;
        if (softClip) { l = std::tanh (l); r = std::tanh (r); }
        L[i] = l; R[i] = r;
    }

    for (const char* id : { "PATTERNS", "LOOP PACK" })
        if (auto* m = rack.findType (id))
        {
            m->disp[0].store ((float) patterns.size(), std::memory_order_relaxed); m->disp[1].store ((float) currentPattern, std::memory_order_relaxed);
            m->disp[2].store (blendPos, std::memory_order_relaxed); m->disp[3].store ((float) patternChanges, std::memory_order_relaxed);
            m->disp[4].store (heldPatternKeys > 0 ? 1.0f : 0.0f, std::memory_order_relaxed);
        }

    // EVOLVE: a randomiser roll every N bars (the plugin's evolve, KNOBS target)
    const long long bar = (long long) std::floor (ctx.pos16 / 16.0);
    if (bar != lastBar)
    {
        if (lastBar >= 0 && ctx.running && evolveBars > 0 && bar % evolveBars == 0) randomize (evolveAmount);
        lastBar = bar;
    }
}
} // namespace nms
