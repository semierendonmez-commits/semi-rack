// PITCH page: missing fundamental, combination tones, repetition pitch,
// constant spectrum melody, Shepard / Risset glissando, tritone paradox,
// octave scrambling (mysterious melody).
#include "Spec.h"

namespace il
{
//==============================================================================
// PHANTOM - missing fundamental. The low band is replaced by its harmonics:
// the ear still hears the fundamental (virtual pitch).
class Phantom : public Module
{
public:
    const MInfo& info() const override
    {
        static const MInfo m { "pha", "PHANT", "PHANTOM", "Missing fundamental (Seebeck 1841, Schouten 1940)", pagePitch, false,
            "The fundamental below the cutoff is removed; only its harmonics 2f, 3f, 4f... are generated.",
            "The bass note is still heard at f, even on speakers that cannot play it.", nullptr,
            { F ("cut", "CUTOFF", 40, 300, 110, 100, "Hz", "Fundamentals below this are replaced by their harmonics"),
              F ("drive", "DRIVE", 0, 100, 50, 0, "%", "How hard the harmonic generator is pushed"),
              F ("harm", "HARMONICS", -24, 12, 0, 0, "dB", "Level of the generated harmonics"),
              F ("remove", "REMOVE F0", 0, 100, 100, 0, "%", "How much of the real fundamental is taken out (100 % = only the phantom remains)"),
              F ("sub", "SUB OCT", 0, 100, 0, 0, "%", "Adds a real sub-octave (f/2) from a flip-flop divider"),
              F ("tilt", "TILT", -100, 100, 0, 0, "%", "Dark (low harmonics) to bright (high harmonics)"),
              C ("shape", "SHAPE", "RECT|ASYM|ODD|CHEBY", 1, "Harmonic generator: RECT even, ASYM even + odd, ODD odd only, CHEBY a Chebyshev series shaped by TILT"),
              C ("top", "UPPER", "x3|x5|x8", 1, "Highest harmonic kept, as a multiple of the cutoff") } };
        return m;
    }
    void prepare (double s, int) override { sr = s; reset(); }
    void reset() override
    {
        for (auto& c : ch) c = {};
        zc = 0; zcCount = 0; zcWin = 0; lastSign = false; f0 = 0;
        lastCut = lastTop = lastTilt = -1;
    }
    void process (float* L, float* R, int n, const Ctx&) override
    {
        const float cut = p[0], drive = p[1] * 0.01f, hg = dbToGain (p[2]), remove = p[3] * 0.01f, sub = p[4] * 0.01f, tilt = p[5] * 0.01f;
        const int shape = ci (6);
        const float top = ci (7) == 0 ? 3.0f : (ci (7) == 1 ? 5.0f : 8.0f);
        if (cut != lastCut || top != lastTop || tilt != lastTilt)
        {
            lastCut = cut; lastTop = top; lastTilt = tilt;
            for (auto& c : ch)
            {
                c.lp1.set (Biquad::LP, cut, 0.7071, 0, sr); c.lp2.set (Biquad::LP, cut, 0.7071, 0, sr);
                c.hp1.set (Biquad::HP, cut, 0.7071, 0, sr); c.hp2.set (Biquad::HP, cut, 0.7071, 0, sr);
                c.ph1.set (Biquad::HP, cut * 1.2, 0.7071, 0, sr); c.ph2.set (Biquad::HP, cut * 1.2, 0.7071, 0, sr);
                c.pl.set (Biquad::LP, cut * top, 0.7071, 0, sr);
                c.shelf.set (Biquad::HighShelf, cut * 2.5, 0.7, 9.0 * tilt, sr);
                c.sublp.set (Biquad::LP, cut * 0.75, 0.7071, 0, sr);
                c.env.set (4.0f, 60.0f, sr);
            }
        }
        const float d = 1.0f + 6.0f * drive;
        const float tw = 0.5f * std::pow (2.0f, tilt * 1.5f);
        const float w2 = 1.0f, w3 = tw, w4 = tw * tw, w5 = tw * tw * tw, wn = 1.0f / (w2 + w3 + w4 + w5);
        float* io[2] = { L, R };
        float envSum = 0, harmSum = 0, subSum = 0;
        for (int c = 0; c < 2; ++c)
        {
            auto& s = ch[(size_t) c];
            float* x = io[c];
            for (int i = 0; i < n; ++i)
            {
                const float in = x[i];
                const float lo = s.lp2.process (s.lp1.process (in));
                const float hi = s.hp2.process (s.hp1.process (in));
                const float e = s.env.process (lo);
                const float u = clampf (lo / (e + 1.0e-4f), -1.5f, 1.5f);
                float h = 0;
                switch (shape)
                {
                    case 0: h = (std::abs (u) - 0.637f) * (0.8f + 0.6f * drive); break;
                    case 1: h = std::tanh (d * (u + 0.45f * u * u)) - std::tanh (d * 0.25f); break;
                    case 2: h = std::tanh (d * u) / std::tanh (d); break;
                    default:
                    {
                        const float v = clampf (u, -1.0f, 1.0f), v2 = v * v;
                        h = (w2 * (2 * v2 - 1) + w3 * (4 * v2 * v - 3 * v) + w4 * (8 * v2 * v2 - 8 * v2 + 1) + w5 * (16 * v2 * v2 * v - 20 * v2 * v + 5 * v)) * wn * (0.6f + drive);
                    }
                }
                h = s.dc.process (h * e);
                h = s.pl.process (s.ph2.process (s.ph1.process (h)));
                if (shape != 3) h = s.shelf.process (h);
                h *= hg * 1.6f;
                // flip-flop sub-octave: toggles on every positive-going zero crossing of the low band
                const bool pos = lo > 0.02f * e;
                if (pos && ! s.subWas) s.flip = ! s.flip;
                if (lo < -0.02f * e) s.subWas = false; else if (pos) s.subWas = true;
                const float sq = s.sublp.process ((s.flip ? 1.0f : -1.0f) * e) * sub;
                x[i] = hi + lo * (1.0f - remove) + h + sq;
                if (c == 0)
                {
                    envSum += e; harmSum += std::abs (h); subSum += std::abs (sq);
                    const bool sign = lo > 0.0f;
                    if (sign && ! lastSign && e > 1.0e-4f) ++zcCount;
                    lastSign = sign;
                    if (++zcWin >= (int) (sr * 0.25)) { if (zcCount > 1) f0 = 0.6f * f0 + 0.4f * (float) zcCount / 0.25f; zcCount = 0; zcWin = 0; }
                }
            }
        }
        const float inv = 1.0f / (float) std::max (1, n);
        setViz (0, f0); setViz (1, envSum * inv); setViz (2, harmSum * inv); setViz (3, remove); setViz (4, cut); setViz (5, subSum * inv);
        setViz (6, (float) shape); setViz (7, top);
    }
private:
    struct Ch { Biquad lp1, lp2, hp1, hp2, ph1, ph2, pl, shelf, sublp; Env env; DcBlock dc; bool flip = false, subWas = false; };
    std::array<Ch, 2> ch;
    float lastCut = -1, lastTop = -1, lastTilt = -1, f0 = 0;
    int zc = 0, zcCount = 0, zcWin = 0;
    bool lastSign = false;
};

//==============================================================================
// TARTINI - combination tones. A nonlinearity on a band of the input makes the
// difference tones (f2 - f1, 2 f1 - f2) the ear itself produces, audible.
class Tartini : public Module
{
public:
    const MInfo& info() const override
    {
        static const MInfo m { "tar", "TARTN", "TARTINI", "Combination tones (Tartini 1714, Sorge 1745)", pagePitch, false,
            "A square / cubic law on a band of the input writes f2 - f1 and 2f1 - f2 into the signal.",
            "The 'third tone' violinists hear under a double stop: a low note nobody plays.", nullptr,
            { F ("focus", "FOCUS", 200, 5000, 1200, 1200, "Hz", "Centre of the band the tones are taken from"),
              F ("width", "WIDTH", 0.5f, 6, 3, 0, "oct", "Width of that band"),
              F ("drive", "DRIVE", 0, 100, 50, 0, "%", "Gain into the nonlinearity (more = denser intermodulation)"),
              F ("level", "LEVEL", -30, 12, 0, 0, "dB", "Level of the combination tones relative to the band"),
              F ("lp", "KEEP BELOW", 50, 2000, 700, 400, "Hz", "Low-pass on the result: keeps the difference tones, drops the sum tones"),
              F ("carrier", "CARRIER", 20, 2000, 220, 200, "Hz", "RING law only: the internal oscillator"),
              C ("law", "LAW", "SQUARE|CUBIC|BOTH|RING", 2, "SQUARE: f2 - f1. CUBIC: 2f1 - f2 (the strongest one in the ear). RING: f +/- carrier") } };
        return m;
    }
    void prepare (double s, int) override { sr = s; reset(); }
    void reset() override { for (auto& c : ch) c = {}; ph = 0; gainS = 0; last = {}; }
    void process (float* L, float* R, int n, const Ctx&) override
    {
        const float focus = p[0], w = p[1], g = dbToGain (p[2] * 0.36f), lvl = dbToGain (p[3]), lpf = p[4], car = p[5];
        const int law = ci (6);
        const std::array<float, 4> now { focus, w, lpf, (float) law };
        if (now != last)
        {
            last = now;
            for (auto& c : ch)
            {
                c.band.set (focus / std::pow (2.0f, w * 0.5f), focus * std::pow (2.0f, w * 0.5f), sr);
                c.l1.set (Biquad::LP, lpf, 0.7071, 0, sr); c.l2.set (Biquad::LP, lpf, 0.7071, 0, sr);
                c.eb.set (5, 120, sr); c.ey.set (5, 120, sr);
            }
        }
        float* io[2] = { L, R };
        float be = 0, ye = 0;
        for (int c = 0; c < 2; ++c)
        {
            auto& s = ch[(size_t) c];
            double phl = ph;
            for (int i = 0; i < n; ++i)
            {
                const float b = s.band.process (io[c][i]);
                const float eb = s.eb.process (b);
                const float v = std::tanh (b * g * 2.0f);
                float y;
                switch (law)
                {
                    case 0: y = v * v; break;
                    case 1: y = v * v * v; break;
                    case 2: y = v * v + v * v * v; break;
                    default: phl += car / sr; y = v * (float) std::sin (6.283185307179586 * phl); break;
                }
                if (law != 3) y = s.l2.process (s.l1.process (y));
                y = s.dc.process (y);
                const float ey = s.ey.process (y);
                // auto gain: the result sits LEVEL dB relative to the band it came from
                const float target = ey > 1.0e-6f ? clampf (eb / ey, 0.0f, 400.0f) : 0.0f;
                s.ag += 0.0005f * (target - s.ag);
                io[c][i] += y * s.ag * lvl;
                if (c == 0) { be += eb; ye += std::abs (y * s.ag * lvl); }
            }
            if (c == 1) ph = phl - std::floor (phl);
        }
        setViz (0, be / (float) std::max (1, n)); setViz (1, ye / (float) std::max (1, n)); setViz (2, focus); setViz (3, (float) law);
        setViz (4, w); setViz (5, lpf); setViz (6, car);
    }
private:
    struct Ch { Band band; Biquad l1, l2; DcBlock dc; Env eb, ey; float ag = 0; };
    std::array<Ch, 2> ch;
    double ph = 0;
    float gainS = 0;
    std::array<float, 4> last {};
};

//==============================================================================
// REPITCH - repetition pitch: a sound plus a delayed copy of itself has a
// pitch at 1 / delay, even when the sound is noise.
class Repitch : public Module
{
public:
    const MInfo& info() const override
    {
        static const MInfo m { "rep", "REPIT", "REPITCH", "Repetition pitch / rippled noise (Huygens 1693, Bilsen 1966)", pagePitch, false,
            "The signal is added to copies of itself delayed by T: a comb of ripples every 1/T Hz.",
            "A clear pitch at 1/T, even from pure noise or a fountain's splash.", nullptr,
            { F ("note", "PITCH", 24, 96, 57, 0, "note", "The pitch heard: the delay is 1 / f"),
              F ("fine", "FINE", -50, 50, 0, 0, "ct", "Fine tune in cents"),
              F ("fb", "AMOUNT", 0, 98, 70, 0, "%", "Echo gain: the strength of the ripple (and of the pitch)"),
              F ("damp", "DAMP", 1000, 20000, 8000, 5000, "Hz", "Low-pass inside the loop"),
              F ("noise", "NOISE", -60, 0, -60, 0, "dB", "Noise fed in, to hear the pitch appear from noise (-60 = off)"),
              F ("spread", "SPREAD", 0, 50, 0, 0, "ct", "Left / right detune of the delay"),
              C ("pol", "POLARITY", "+|-", 0, "Negative echo: the pitch drops an octave and goes hollow"),
              C ("form", "FORM", "FEEDBACK|IRN x4|IRN x8", 0, "FEEDBACK comb, or iterated rippled noise (4 or 8 add-and-delay stages)") } };
        return m;
    }
    void prepare (double s, int) override
    {
        sr = s;
        for (auto& c : ch) for (auto& d : c.d) d.prepare ((int) (sr / 25.0) + 8);
        reset();
    }
    void reset() override { for (auto& c : ch) { for (auto& d : c.d) d.reset(); c.lp = {}; c.y = 0; } nz = {}; }
    void process (float* L, float* R, int n, const Ctx&) override
    {
        const float f = midiToHz (p[0] + p[1] * 0.01f);
        const float g = p[2] * 0.01f * (ci (6) == 1 ? -1.0f : 1.0f);
        const float noise = p[4] <= -59.5f ? 0.0f : dbToGain (p[4]);
        const int form = ci (7);
        const float sp = p[5] / 1200.0f;
        float* io[2] = { L, R };
        for (int c = 0; c < 2; ++c)
        {
            auto& s = ch[(size_t) c];
            s.lp.setHz (p[3], sr);
            const float T = clampf ((float) (sr / f) * std::pow (2.0f, (c == 0 ? -0.5f : 0.5f) * sp), 2.0f, (float) s.d[0].size() - 8.0f);
            for (int i = 0; i < n; ++i)
            {
                float x = io[c][i] + (noise > 0 ? nz.white() * noise * 0.5f : 0.0f);
                if (form == 0)
                {
                    const float fbk = s.lp.process (s.d[0].cubic (T - 1.0f));
                    const float y = x + g * fbk;
                    s.d[0].push (y);
                    io[c][i] = y * (1.0f - 0.5f * std::abs (g));
                }
                else
                {
                    const int stages = form == 1 ? 4 : 8;
                    float v = x;
                    for (int k = 0; k < stages; ++k)
                    {
                        s.d[(size_t) k].push (v);
                        v = v + g * s.d[(size_t) k].cubic (T);
                    }
                    io[c][i] = s.lp.process (v / std::pow (1.0f + std::abs (g), (float) stages) * 1.4f);
                }
            }
        }
        setViz (0, f); setViz (1, std::abs (g)); setViz (2, (float) form); setViz (3, g < 0 ? 1.0f : 0.0f);
    }
private:
    struct Ch { std::array<DelayLine, 8> d; OnePole lp; float y = 0; };
    std::array<Ch, 2> ch;
    Noise nz;
};

//==============================================================================
// CONSTANT SPECTRUM - families of harmonic series, each 0.1 Hz above the last.
// The spectrum stays the same, but harmonic h beats with a period of 1/(h step):
// harmonics swell in and out one after another and a melody appears.
class ConstSpectrum : public Module
{
public:
    const MInfo& info() const override
    {
        static const MInfo m { "csm", "CONST", "CONST SPECTRUM", "Constant spectrum melody (beating harmonic families)", pagePitch, false,
            "Many harmonic series a fraction of a hertz apart: the long-term spectrum never changes.",
            "A slow melody: each harmonic swells out of the chord at its own beat period.", "RESTART",
            { F ("base", "BASE", 30, 220, 55, 80, "Hz", "Fundamental of the first family"),
              F ("step", "STEP", 0.01f, 1, 0.1f, 0.1f, "Hz", "Offset between families (the beat rate of harmonic h is h x STEP x families)"),
              F ("level", "LEVEL", -48, 0, -12, 0, "dB", "Generator level"),
              F ("detune", "DETUNE", 0.02f, 1, 0.2f, 0.2f, "%", "INPUT: pitch offset between the micro-shifted copies of the input"),
              FI ("fam", "FAMILIES", 2, 16, 10, "", "Number of harmonic families"),
              FI ("harm", "HARMONICS", 2, 16, 10, "", "Harmonics per family"),
              C ("src", "SOURCE", "GENERATOR|INPUT|BOTH", 0, "GENERATOR: the classic additive construction. INPUT: the same idea on your signal (micro-shifted copies)") } };
        return m;
    }
    bool generates() const override { return ci (6) != 1; }
    void prepare (double s, int) override
    {
        sr = s;
        for (auto& c : ch) for (auto& sh : c) { sh.prepare (sr, 120.0f); sh.setWindow (90.0f); }
        reset();
    }
    void reset() override { restart(); for (auto& c : ch) for (auto& sh : c) sh.reset(); }
    void trigger() override { restartReq = true; }
    void process (float* L, float* R, int n, const Ctx&) override
    {
        if (restartReq) { restartReq = false; restart(); }
        const float base = p[0], step = p[1], lvl = dbToGain (p[2]), det = p[3] * 0.01f;
#ifdef METAMODULE
        // SemiFamily: at most 8 families x 6 harmonics (48 oscillators) on MetaModule
        const int fam = juce_clamp ((int) std::lround (p[4]), 2, 8), har = juce_clamp ((int) std::lround (p[5]), 2, 6);
#else
        const int fam = juce_clamp ((int) std::lround (p[4]), 2, 16), har = juce_clamp ((int) std::lround (p[5]), 2, 16);
#endif
        const int src = ci (6);
        if (base != lb || step != ls || fam != lf || har != lh)
        {
            lb = base; ls = step; lf = fam; lh = har;
            nOsc = 0;
            for (int j = 0; j < fam; ++j)
                for (int h = 1; h <= har; ++h)
                {
                    const double f = h * (base + j * step);
                    if (f > sr * 0.45) continue;
                    auto& o = osc[(size_t) nOsc++];
                    const double w = 6.283185307179586 * f / sr;
                    o.cr = (float) std::cos (w); o.sr = (float) std::sin (w);
                    // keep the phase of an oscillator that already existed: restart only on RESTART
                    if (o.c == 0 && o.s == 0) { o.c = 1; o.s = 0; }
                }
        }
        const float amp = lvl * 2.5f / (float) (fam * har);
#ifdef METAMODULE
        const int K = std::min (fam, 4);   // SemiFamily: 4 input copies on MetaModule
#else
        const int K = std::min (fam, 8);
#endif
        for (int k = 0; k < K; ++k) { ch[0][(size_t) k].setRatio (1.0f + (float) k * det); ch[1][(size_t) k].setRatio (1.0f + (float) k * det * 1.01f); }
        for (int i = 0; i < n; ++i)
        {
            float g = 0;
            if (src != 1)
                for (int k = 0; k < nOsc; ++k)
                {
                    auto& o = osc[(size_t) k];
                    const float c = o.c * o.cr - o.s * o.sr, s = o.s * o.cr + o.c * o.sr;
                    o.c = c; o.s = s;
                    g += s;
                }
            if (++renorm >= 512)
            {
                renorm = 0;
                for (int k = 0; k < nOsc; ++k) { auto& o = osc[(size_t) k]; const float m = 1.0f / std::sqrt (o.c * o.c + o.s * o.s); o.c *= m; o.s *= m; }
            }
            float yl = 0, yr = 0;
            if (src != 0)
            {
                for (int k = 0; k < K; ++k) { yl += ch[0][(size_t) k].process (L[i]); yr += ch[1][(size_t) k].process (R[i]); }
                yl /= (float) K; yr /= (float) K;
            }
            const float gen = g * amp;
            L[i] = (src == 0 ? 0.0f : yl) + (src == 1 ? 0.0f : gen) + (src == 0 ? L[i] : 0.0f);
            R[i] = (src == 0 ? 0.0f : yr) + (src == 1 ? 0.0f : gen) + (src == 0 ? R[i] : 0.0f);
        }
        t += (double) n / sr;
        setViz (0, (float) t); setViz (1, base); setViz (2, step); setViz (3, (float) fam); setViz (4, (float) har); setViz (5, (float) src);
    }
private:
    static int juce_clamp (int v, int a, int b) { return v < a ? a : (v > b ? b : v); }
    void restart()
    {
        for (auto& o : osc) { o.c = 1; o.s = 0; }
        t = 0; lb = -1;
    }
    struct Osc { float c = 1, s = 0, cr = 1, sr = 0; };
    std::array<Osc, 256> osc {};
    int nOsc = 0, renorm = 0, lf = -1, lh = -1;
    float lb = -1, ls = -1;
    double t = 0;
    bool restartReq = false;
    std::array<std::array<Shifter, 8>, 2> ch;
};

//==============================================================================
// SHEPARD - N voices an interval apart glide up (or down) forever under a
// fixed raised-cosine envelope: the top voice fades out as a new one fades in
// at the bottom. SHIFT pitch-shifts the input, FILTER sweeps band-passes,
// FLANGE sweeps combs (the barber-pole flanger).
class Shepard : public Module
{
public:
    const MInfo& info() const override
    {
        static const MInfo m { "shep", "SHEPD", "SHEPARD", "Shepard tone, Risset glissando (Shepard 1964, Risset 1969)", pagePitch, false,
            "Copies an interval apart glide together; a fixed envelope fades the top one out and a new one in at the bottom.",
            "A pitch that rises (or falls) forever and never arrives: the sonic barber pole.", nullptr,
            { F ("cycle", "SPEED", 0.5f, 60, 8, 6, "s", "Seconds for the voices to climb one INTERVAL (FREE sync)"),
              F ("centre", "CENTRE", 60, 4000, 440, 500, "Hz", "Centre of the envelope (FILTER / FLANGE: centre of the sweep)"),
              FI ("interval", "INTERVAL", 1, 24, 12, "st", "Spacing of the voices: 12 = octaves (the classic), 7 = fifths"),
              F ("grain", "GRAIN", 20, 150, 70, 0, "ms", "SHIFT: grain size of the pitch shifters"),
              F ("reso", "RESONANCE", 1, 40, 10, 8, "", "FILTER: band-pass Q. FLANGE: comb feedback"),
              F ("tone", "ENVELOPE", 0, 100, 60, 0, "%", "SHIFT: how much the fixed spectral envelope (CENTRE) shapes the sum"),
              C ("mode", "MODE", "SHIFT|FILTER|FLANGE", 0, "SHIFT the input's pitch, sweep FILTERs, or sweep combs (barber-pole FLANGE)"),
              C ("dir", "DIRECTION", "UP|DOWN|STILL", 0, "Endless rise, endless fall, or frozen"),
              C ("motion", "MOTION", "GLIDE|STEPS", 0, "GLIDE: Risset's continuous glissando. STEPS: Shepard's semitone scale"),
              C ("voices", "VOICES", "2|3|4|5|6|8", 3, "Number of voices"),
              C ("sync", "SYNC", "FREE|1/4|1/2|1 BAR|2 BARS|4 BARS|8 BARS", 0, "Time per INTERVAL locked to the host tempo") } };
        return m;
    }
    void prepare (double s, int) override
    {
        sr = s;
        for (auto& c : ch)
            for (auto& v : c.v) { v.sh.prepare (sr, 160.0f); v.dl.prepare ((int) (sr / 15.0) + 8); }
        reset();
    }
    void reset() override
    {
        for (auto& c : ch) { for (auto& v : c.v) { v.sh.reset(); v.dl.reset(); v.bp.reset(); v.y = 0; } c.tone.reset(); }
        pos = 0; lastCentre = -1;
    }
    void process (float* L, float* R, int n, const Ctx& ctx) override
    {
        static const int vc[] = { 2, 3, 4, 5, 6, 8 };
        static const double syncBeats[] = { 0, 1, 2, 4, 8, 16, 32 };
#ifdef METAMODULE
        const int N = std::min (4, vc[std::min (5, ci (9))]);   // SemiFamily: at most 4 voices on MetaModule
#else
        const int N = vc[std::min (5, ci (9))];
#endif
        const int mode = ci (6), dir = ci (7), sync = ci (10);
        const bool steps = ci (8) == 1;
        const float interval = std::round (p[2]);
        const float centre = p[1], reso = p[4], tone = p[5] * 0.01f;
        if (centre != lastCentre)
        {
            lastCentre = centre;
            for (auto& c : ch) c.tone.set (centre / 4.0f, centre * 4.0f, sr);
        }
        for (auto& c : ch) for (auto& v : c.v) v.sh.setWindow (p[3]);
        const float sgn = dir == 0 ? 1.0f : (dir == 1 ? -1.0f : 0.0f);
        const float fb = 0.45f + 0.5f * clampf ((reso - 1.0f) / 39.0f, 0.0f, 1.0f);
        const float makeup = mode == 1 ? std::sqrt (reso) * 0.9f : 1.0f;
        float* io[2] = { L, R };
        const int sub = 16;
        for (int i0 = 0; i0 < n; i0 += sub)
        {
            const int m = std::min (sub, n - i0);
            // position of the voices, 0..1 = one INTERVAL
            if (sync > 0) pos = (float) fracd (sgn * (ctx.ppq + (double) i0 * ctx.beatsPerSample()) / syncBeats[sync]);
            else { pos += sgn * (float) m / (float) (p[0] * sr); pos -= std::floor (pos); }
            float q = pos;
            if (steps && interval > 1.0f) q = std::floor (pos * interval) / interval;
            for (int k = 0; k < N; ++k)
            {
                const float u = frac (((float) k + q) / (float) N);
                const float semis = (u - 0.5f) * (float) N * interval;
                gains[(size_t) k] = 2.0f / (float) N * sqr (std::sin (kPi * u));
                offs[(size_t) k] = semis;
                const float f = clampf (centre * std::pow (2.0f, semis / 12.0f), 20.0f, (float) sr * 0.45f);
                for (auto& c : ch)
                {
                    auto& v = c.v[(size_t) k];
                    if (mode == 0) v.sh.setRatio (std::pow (2.0f, semis / 12.0f));
                    else if (mode == 1) v.bp.set (Biquad::BP, f, reso, 0, sr);
                    else v.T = (float) sr / f;
                }
            }
            for (int c = 0; c < 2; ++c)
            {
                auto& s = ch[(size_t) c];
                for (int i = i0; i < i0 + m; ++i)
                {
                    const float x = io[c][i];
                    float y = 0;
                    for (int k = 0; k < N; ++k)
                    {
                        auto& v = s.v[(size_t) k];
                        float o;
                        if (mode == 0) o = v.sh.process (x);
                        else if (mode == 1) o = v.bp.process (x);
                        else { o = x + fb * v.dl.cubic (std::max (2.0f, v.T - 1.0f)); v.dl.push (o); o *= (1.0f - fb); }
                        y += o * gains[(size_t) k];
                    }
                    if (mode == 0) y = lerpf (y, s.tone.process (y) * 1.3f, tone);
                    io[c][i] = y * makeup;
                }
            }
        }
        setViz (0, pos); setViz (1, (float) N); setViz (18, (float) mode); setViz (19, (float) dir); setViz (20, centre); setViz (21, interval);
        for (int k = 0; k < 8; ++k) { setViz (2 + k, k < N ? gains[(size_t) k] : 0.0f); setViz (10 + k, k < N ? offs[(size_t) k] : 0.0f); }
    }
private:
    struct Voice { Shifter sh; Biquad bp; DelayLine dl; float T = 100, y = 0; };
    struct Ch { std::array<Voice, 8> v; Band tone; };
    std::array<Ch, 2> ch;
    std::array<float, 8> gains {}, offs {};
    float pos = 0, lastCentre = -1;
};

//==============================================================================
// TRITONE - octave-ambiguous (Shepard-like) copies of the input, transposed
// back and forth by a tritone under a fixed spectral envelope. Whether the
// step sounds up or down depends on the listener and on CENTRE.
class Tritone : public Module
{
public:
    const MInfo& info() const override
    {
        static const MInfo m { "tri", "TRITN", "TRITONE", "Tritone paradox (Deutsch 1986)", pagePitch, false,
            "Octave-blurred copies under a fixed envelope, stepping by half an octave and back.",
            "The same step is heard going UP by some listeners and DOWN by others.", nullptr,
            { FI ("interval", "INTERVAL", 1, 11, 6, "st", "The step. 6 = the tritone (maximally ambiguous)"),
              F ("centre", "CENTRE", 100, 3000, 500, 500, "Hz", "Centre of the fixed spectral envelope: moving it flips the heard direction"),
              F ("width", "WIDTH", 0.5f, 5, 2, 0, "oct", "Width of the envelope"),
              F ("blur", "OCT BLUR", 0, 100, 70, 0, "%", "Level of the octave-up and octave-down copies (the octave ambiguity)"),
              F ("glide", "GLIDE", 0, 200, 10, 0, "ms", "Portamento between steps"),
              F ("grain", "GRAIN", 20, 120, 50, 0, "ms", "Grain size of the pitch shifters"),
              C ("rate", "RATE", divChoices(), 6, "Step length"),
              C ("pattern", "PATTERN", "ALT|UP-DOWN|RANDOM|HOLD", 0, "ALT: 0, +i, 0, +i. UP-DOWN: 0, +i, 0, -i. RANDOM. HOLD: stay transposed") } };
        return m;
    }
    void prepare (double s, int) override
    {
        sr = s;
        for (auto& c : ch) for (auto& sh : c.sh) sh.prepare (sr, 130.0f);
        reset();
    }
    void reset() override { for (auto& c : ch) { for (auto& sh : c.sh) sh.reset(); c.env.reset(); } clock.reset(); cur = 0; target = 0; lc = -1; }
    void process (float* L, float* R, int n, const Ctx& ctx) override
    {
        const float iv = std::round (p[0]), centre = p[1], w = p[2], blur = p[3] * 0.01f;
        const int pattern = ci (7);
        const double div = divBeats (ci (6));
        if (centre != lc || w != lw) { lc = centre; lw = w; for (auto& c : ch) c.env.set (centre / std::pow (2.0f, w * 0.5f), centre * std::pow (2.0f, w * 0.5f), sr); }
        for (auto& c : ch) for (auto& sh : c.sh) sh.setWindow (p[5]);
        OnePole gl; gl.setMs (p[4], sr);
        const float a = gl.a;
        float* io[2] = { L, R };
        const float norm = 1.0f / (1.0f + 2.0f * blur);
        for (int i0 = 0; i0 < n; i0 += 16)
        {
            const int m = std::min (16, n - i0);
            long long idx;
            if (clock.tick (ctx, i0, div, idx))
            {
                stepIdx = idx;
                switch (pattern)
                {
                    case 0: target = (idx & 1) ? iv : 0.0f; break;
                    case 1: { const int k = (int) (idx & 3); target = k == 1 ? iv : (k == 3 ? -iv : 0.0f); break; }
                    case 2: target = hashUni ((uint32_t) idx, 77u) < 0.5f ? 0.0f : iv; break;
                    default: target = iv;
                }
            }
            for (int k = 0; k < m; ++k) cur = target + std::pow (a, 1.0f) * (cur - target);
            for (int c = 0; c < 2; ++c)
                for (int o = 0; o < 3; ++o) ch[(size_t) c].sh[(size_t) o].setRatio (std::pow (2.0f, (cur + 12.0f * (float) (o - 1)) / 12.0f));
            for (int c = 0; c < 2; ++c)
            {
                auto& s = ch[(size_t) c];
                for (int i = i0; i < i0 + m; ++i)
                {
                    const float x = io[c][i];
                    const float y = (s.sh[1].process (x) + blur * (s.sh[0].process (x) + s.sh[2].process (x))) * norm;
                    io[c][i] = s.env.process (y) * 1.6f;
                }
            }
        }
        setViz (0, cur); setViz (1, (float) (stepIdx & 3)); setViz (2, centre); setViz (3, iv); setViz (4, w); setViz (5, blur);
    }
private:
    struct Ch { std::array<Shifter, 3> sh; Band env; };
    std::array<Ch, 2> ch;
    StepClock clock;
    long long stepIdx = 0;
    float cur = 0, target = 0, lc = -1, lw = -1;
};

//==============================================================================
// SCRAMBLE - every note keeps its pitch class but lands in a random octave
// (Deutsch's mysterious melody: a familiar tune becomes unrecognisable).
class Scramble : public Module
{
public:
    const MInfo& info() const override
    {
        static const MInfo m { "scr", "SCRMB", "SCRAMBLE", "Mysterious melody / octave scrambling (Deutsch 1972)", pagePitch, false,
            "Each step is moved by whole octaves: every pitch class stays exactly the same.",
            "The tune is gone: the contour is broken although every note is still 'the same note'.", "RESEED",
            { FI ("range", "RANGE", 1, 3, 2, "oct", "Largest octave jump"),
              F ("chance", "CHANCE", 0, 100, 70, 0, "%", "Probability that a step is moved"),
              F ("glide", "GLIDE", 0, 300, 4, 0, "ms", "Portamento between octaves"),
              F ("grain", "GRAIN", 20, 120, 45, 0, "ms", "Grain size of the pitch shifter"),
              FI ("seed", "SEED", 0, 99, 7, "", "The scramble pattern"),
              C ("rate", "RATE", divChoices(), 4, "Step length (one note)"),
              C ("dir", "DIRECTION", "BOTH|UP|DOWN", 0, "Octaves up and down, only up or only down"),
              C ("len", "LOOP", "4|8|16|FREE", 2, "Pattern repeats after this many steps (FREE: never)") } };
        return m;
    }
    void prepare (double s, int) override { sr = s; for (auto& sh : sh2) sh.prepare (sr, 130.0f); reset(); }
    void reset() override { for (auto& sh : sh2) sh.reset(); clock.reset(); cur = 0; target = 0; hist.fill (0.0f); }
    void trigger() override { reseed += 13; }
    void process (float* L, float* R, int n, const Ctx& ctx) override
    {
        const int range = (int) std::lround (p[0]), dir = ci (6), lenSel = ci (7);
        const float chance = p[1] * 0.01f;
        const int seed = (int) std::lround (p[4]) + reseed;
        const double div = divBeats (ci (5));
        for (auto& sh : sh2) sh.setWindow (p[3]);
        OnePole gl; gl.setMs (p[2], sr);
        float* io[2] = { L, R };
        for (int i0 = 0; i0 < n; i0 += 16)
        {
            const int m = std::min (16, n - i0);
            long long idx;
            if (clock.tick (ctx, i0, div, idx))
            {
                const int L4[] = { 4, 8, 16, 0 };
                const long long pos = L4[lenSel] > 0 ? ((idx % L4[lenSel]) + L4[lenSel]) % L4[lenSel] : idx;
                int o = 0;
                if (hashUni ((uint32_t) pos, (uint32_t) seed) < chance)
                {
                    const float r = hashUni ((uint32_t) pos * 7u + 3u, (uint32_t) seed + 101u);
                    if (dir == 0) { o = (int) std::floor (r * (2 * range + 1)) - range; if (o == 0) o = r < 0.5f ? -1 : 1; }
                    else o = (1 + (int) std::floor (r * range)) * (dir == 1 ? 1 : -1);
                    o = std::max (-range, std::min (range, o));
                }
                target = (float) o;
                for (int k = 15; k > 0; --k) hist[(size_t) k] = hist[(size_t) k - 1];
                hist[0] = target;
                ++steps;
            }
            for (int k = 0; k < m; ++k) cur = target + gl.a * (cur - target);
            const float r = std::pow (2.0f, cur);
            sh2[0].setRatio (r); sh2[1].setRatio (r);
            for (int c = 0; c < 2; ++c) for (int i = i0; i < i0 + m; ++i) io[c][i] = sh2[(size_t) c].process (io[c][i]);
        }
        setViz (0, cur); setViz (17, (float) steps);
        for (int k = 0; k < 16; ++k) setViz (1 + k, hist[(size_t) k]);
    }
private:
    std::array<Shifter, 2> sh2;
    StepClock clock;
    std::array<float, 16> hist {};
    float cur = 0, target = 0;
    int reseed = 0;
    long long steps = 0;
};

ModulePtr makePhantom() { return std::make_unique<Phantom>(); }
ModulePtr makeTartini() { return std::make_unique<Tartini>(); }
ModulePtr makeRepitch() { return std::make_unique<Repitch>(); }
ModulePtr makeConstSpectrum() { return std::make_unique<ConstSpectrum>(); }
ModulePtr makeShepard() { return std::make_unique<Shepard>(); }
ModulePtr makeTritone() { return std::make_unique<Tritone>(); }
ModulePtr makeScramble() { return std::make_unique<Scramble>(); }
} // namespace il
