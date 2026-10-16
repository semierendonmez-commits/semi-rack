#include "Common.h"

namespace nms
{
static const char* kMacroNames[kMacros] = {
    "PITCH", "RANGE", "MICRO INTENSE", "MICRO COMPLEX", "MESO INTENSE", "MESO COMPLEX", "MACRO INTENSE", "MACRO COMPLEX",
    "MODULATION", "COMPLEX", "ATTACK", "DECAY", "ACCENT", "SPEED", "INTENSITY", "CHARACTER",
    "TONE", "RESONANCE", "CHARACTER", "NOISE", "HARMONICS", "INSTABILITY", "THICKNESS", "SPACE" };
const char* macroName (int i) { return i >= 0 && i < kMacros ? kMacroNames[i] : ""; }
const char* macroGroup (int i) { return i < 8 ? "GENERATORS" : (i < 16 ? "SHAPES" : "SOUND"); }

std::string fmt (float v, int dec) { char b[32]; std::snprintf (b, sizeof b, "%.*f", dec, (double) v); return b; }
std::string hzText (float f)
{
    if (f < 1.0f) return fmt (f, 3) + " Hz";
    if (f < 100.0f) return fmt (f, 1) + " Hz";
    if (f < 1000.0f) return fmt (f, 0) + " Hz";
    return fmt (f / 1000.0f, 2) + " kHz";
}
std::string secsText (float s) { return s < 1.0f ? fmt (s * 1000.0f, s < 0.01f ? 1 : 0) + " ms" : fmt (s, 2) + " s"; }

std::string Module::text (int i) const
{
    const auto& p = spec.params[(size_t) i];
    const float v = getPlain (i);
    if (! p.choices.empty()) return p.choices[(size_t) std::lround (v)];
    if (p.kind == Kind::Toggle) return v > 0.5f ? "ON" : "OFF";
    if (p.unit == "Hz") return hzText (v);
    if (p.unit == "s") return secsText (v);
    if (p.map == Map::Int) return (v > 0 && p.min < 0 ? "+" : "") + fmt (v, 0) + p.unit;
    if (p.unit == "%") return fmt (v * 100.0f, 0) + "%";
    if (p.unit.empty() && p.min == 0.0f && p.max == 1.0f) return fmt (v * 100.0f, 0);
    const float span = p.max - p.min;
    return fmt (v, span >= 100 ? 0 : (span >= 10 ? 1 : 2)) + (p.unit.empty() ? "" : " " + p.unit);
}

// ---------------------------------------------------------------------------
int rhythmHits (const RhythmParams& r, int64_t s, Hit* out)
{
    int n = 0;
    const int64_t phraseSteps = (int64_t) r.bars * 16;
    const int64_t p = (((s + r.offset) % phraseSteps) + phraseSteps) % phraseSteps;
    const int inBar = (int) (p % 16), bar = (int) (p / 16);
    const float level = metricLevel (inBar, r.meter);
    // the stress pattern loops every bar: metrical weight + a seeded variation
    const float imp = 0.7f * level + 0.3f * hashf (r.seed, inBar, 1);
    bool on = imp > 1.0f - r.base * 1.05f;
    // phrase mask: bars after the first thin out, the last bar can fill
    if (r.pmask > 0.0f)
    {
        if (bar > 0 && hashf (r.seed, bar, 3) < r.pmask * 0.8f && hashf (r.seed, (int32_t) p, 4) < r.pmask * 0.7f && level < 0.9f) on = false;
        if (bar == r.bars - 1 && r.pmask > 0.5f && inBar >= 12 && hashf (r.seed, (int32_t) p, 5) < (r.pmask - 0.5f) * 1.6f) on = true;
    }
    const float vel = clamp01 (1.0f - r.samt * (1.0f - imp) * 0.9f);
    if (on) out[n++] = { 0.0f, vel };
    // ornaments on the last step of each ornament cycle
    if (r.oint > 0.0f && ((p + 1) % std::max (1, r.ocyc)) == 0 && hashf (r.seed, (int32_t) p, 6) < r.oint)
    {
        const float ov = 0.25f + 0.75f * r.olvl;
        if (r.omod < 0.25f) out[n++] = { on ? 0.5f : 0.0f, ov * 0.6f };                                // ghost
        else if (r.omod < 0.5f) { out[n++] = { 0.0f, ov * 0.5f }; out[n++] = { 0.12f, ov }; }          // flam
        else if (r.omod < 0.75f) { out[n++] = { 0.0f, ov }; out[n++] = { 0.5f, ov * 0.8f }; }          // ratchet 2
        else
        {
            const int k = 3 + (hashf (r.seed, (int32_t) p, 7) > 0.5f ? 1 : 0);                           // ratchet 3 / 4
            for (int j = 0; j < k; ++j) out[n++] = { (float) j / (float) k, ov * (1.0f - 0.12f * (float) j) };
        }
    }
    return n;
}

Note melodyAt (const MelodyParams& m, int64_t s)
{
    Note nt;
    const int tc = std::max (1, m.tcyc);
    const int c = (int) (((s % tc) + tc) % tc);
    const int inBar = (int) (((s % 16) + 16) % 16);
    const float level = metricLevel (inBar, m.meter);
    nt.on = hashf (m.seed, c, 1) < m.intensity * (0.55f + 0.45f * level) * 1.15f;
    const int64_t ps = (int64_t) m.bars * 16;
    const int64_t phraseIdx = s >= 0 ? s / ps : (s - ps + 1) / ps;
    if (m.mask > 0.0f && hashf (m.seed, (int32_t) (((s % ps) + ps) % ps), 3) < m.mask * 0.75f && level < 0.9f) nt.on = false;
    const int span = (int) std::lround (m.range * 14.0f);
    int deg = (int) std::lround (hashf (m.seed, c, 4) * (float) span) - span / 4;
    static const int var[8] = { 0, 0, 2, -1, 3, -2, 4, 1 };
    if (m.range > 0.25f) deg += var[(int) (hashf (m.seed, (int32_t) (phraseIdx % 4), 2) * 7.99f)];
    nt.degree = deg;
    nt.vel = clamp01 (0.55f + 0.45f * level * (0.6f + 0.4f * hashf (m.seed, c, 5)));
    nt.mod = hashf (m.modSeed, c, 7) * 2.0f - 1.0f;
    nt.len = 1.0f + std::floor (hashf (m.seed, c, 8) * 3.0f * (1.0f - m.intensity));
    return nt;
}

// ---------------------------------------------------------------------------
void addGeneratorPorts (B& b)
{
    b.in ("clk", "CLK", PortKind::Pulse, Norm::None, "External step clock: when patched, every pulse is one step of the pattern");
    b.in ("trig", "TRIG", PortKind::Pulse, Norm::None, "External triggers: when patched, they play the sound instead of the internal pattern");
    b.in ("pitch", "PITCH", PortKind::Pitch, Norm::None, "Pitch offset (1.0 = one octave), the NMS 'pitch offset' input");
    b.in ("vary", "VARY", PortKind::CV, Norm::None, "Vary curve: adds to the module's modulation cycle");
    b.in ("acc", "LEVEL", PortKind::CV, Norm::None, "Trigger level: when patched, sets the velocity of every hit");
    b.out ("l", "OUT L", PortKind::Audio);
    b.out ("r", "OUT R", PortKind::Audio);
    b.out ("trig", "TRIG", PortKind::Pulse, "A pulse on every hit (feeds the FX on this channel)");
    b.out ("pitch", "PITCH", PortKind::Pitch, "Pitch of the last hit");
    b.out ("vel", "VEL", PortKind::CV, "Velocity of the last hit");
    b.out ("mod", "MOD", PortKind::CV, "The modulation cycle / pattern value");
}

void Generator::block (const Ctx& cx)
{
    Module::block (cx);
    k = 0;
    stepSamples = cx.pos16PerSample > 0 ? 1.0 / (cx.pos16PerSample * rate()) : cx.sr * 0.125;
    if (cx.restart != lastRestart) { lastRestart = cx.restart; lastStep = INT64_MIN; extStep = -1; }
}

void Generator::addHit (float frac, float vel, int64_t s)
{
    if (nPending >= 32) return;
    pending[nPending++] = { (int) std::lround (frac * stepSamples), vel, s };
}

int Generator::link (int64_t s, Hit* h, int n, uint32_t seed)
{
    if (iLink < 0 || ctx == nullptr || ctx->lanes == nullptr) return n;
    const int mode = (int) getPlain (iLink);
    if (mode == 0) return n;
    auto* other = (*ctx->lanes)[(size_t) std::max (0, std::min (kChannels - 1, (int) getPlain (iLch)))];
    if (other == nullptr || other == this) return n;
    const double r = rate();
    const double s16d = (double) s / r;
    if (std::abs (s16d - std::round (s16d)) > 1e-6) return n;   // between 16ths: nothing to compare with
    const float ov = other->hitAt ((int64_t) std::llround (s16d));
    if (ov <= 0.0f || hashf (seed, (int32_t) s, 31) >= getPlain (iLamt)) return n;
    if (mode == 1) return 0;                                    // AVOID
    if (n == 0) { h[0] = { 0.0f, std::min (1.0f, ov * 0.85f) }; return 1; }   // FOLLOW
    return n;
}

void Generator::fire (float vel, int64_t s, bool ext)
{
    activity.store (std::min (16.0f, activity.load (std::memory_order_relaxed) + vel), std::memory_order_relaxed);
    trigOut.fire ((int) pulseSamples (sr));
    voice (vel, s, ext);
}

void Generator::tick (const float* in, float* o)
{
    pitchIn = in[2]; vary = in[3]; acc = isPatched (4) ? clamp01 (in[4]) : -1.0f;
    const bool extTrig = isPatched (1);
    if (isPatched (0))
    {
        if (clk.rise (in[0])) { ++extStep; if (! extTrig) step (extStep); }
    }
    else if (ctx->running)
    {
        const double pos = (ctx->pos16 + ctx->pos16PerSample * k) * rate();
        const int64_t s = (int64_t) std::floor (pos + 1.0e-9);
        if (s != lastStep)
        {
            const bool first = lastStep == INT64_MIN;
            lastStep = s;
            if (! extTrig && (! first || pos - (double) s < 0.05)) step (s);
        }
    }
    if (extTrig && trg.rise (in[1]))
    {
        ++extStep;
        step (-1 - extStep);   // lets synths advance their note; drums ignore negative steps
        fire (acc >= 0 ? acc : 0.85f, extStep, true);
    }
    for (int i = 0; i < nPending;)
    {
        if (pending[i].left <= 0)
        {
            fire (acc >= 0 ? acc * pending[i].vel : pending[i].vel, pending[i].s, false);
            pending[i] = pending[--nPending];
        }
        else { --pending[i].left; ++i; }
    }
    float l = 0, r = 0;
    render (l, r);
    o[0] = l; o[1] = r;
    o[2] = trigOut.tick();
    o[3] = oPitch; o[4] = oVel; o[5] = oMod;
    ++k;
}
} // namespace nms
