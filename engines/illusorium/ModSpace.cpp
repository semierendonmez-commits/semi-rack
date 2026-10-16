// SPACE page: Huggins pitch, binaural beats, octave illusion, the dichotic
// melody illusions, Franssen, Haas, Doppler, HRTF cues, externalisation.
#include "Spec.h"

namespace il
{
//==============================================================================
// HUGGINS - the same noise in both ears, except a narrow band whose phase is
// turned around in one ear. Each ear alone hears flat noise; together the
// brain hears a faint tone at that band.
class Huggins : public Module
{
public:
    const MInfo& info() const override
    {
        static const MInfo m { "hug", "HUGNS", "HUGGINS", "Huggins binaural pitch (Cramer & Huggins 1958)", pageSpace, true,
            "Identical noise in both ears, but one ear's phase turns 360 degrees through a narrow band.",
            "A faint tone at that band, 'inside the head' - though neither ear alone contains it.", nullptr,
            { F ("note", "PITCH", 36, 96, 69, 0, "note", "The hidden pitch (FIXED) or the melody's root"),
              F ("q", "SHARPNESS", 2, 40, 12, 10, "", "Q of the phase transition: higher = clearer, purer pitch"),
              F ("noise", "NOISE", -60, 0, -12, 0, "dB", "Noise level (the carrier of the illusion)"),
              F ("input", "INPUT", -60, 0, 0, 0, "dB", "Your signal through the same phase trick (-60 = noise only)"),
              F ("depth", "DEPTH", 0, 100, 100, 0, "%", "Blend between no phase shift and the full transition"),
              C ("mode", "MODE", "FIXED|MELODY|RANDOM", 1, "A fixed pitch, a wandering melody, or random notes"),
              C ("rate", "RATE", divChoices(), 6, "Note length"),
              C ("scale", "SCALE", "MAJOR|MINOR|PENTA|CHROM", 2, "Scale of the hidden melody"),
              C ("ear", "EAR", "RIGHT|LEFT", 0, "Which ear gets the phase transition"),
              C ("color", "NOISE", "WHITE|PINK", 0, "Noise colour") } };
        return m;
    }
    bool generates() const override { return true; }
    void prepare (double s, int) override { sr = s; reset(); }
    void reset() override { for (auto& a : ap) a.reset(); clock.reset(); deg = 0; curHz = 0; }
    void process (float* L, float* R, int n, const Ctx& ctx) override
    {
        static const int scales[4][12] = { { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 4, 7, 9 }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } };
        static const int sizes[4] = { 7, 7, 5, 12 };
        const int mode = ci (5), sc = std::min (3, ci (7));
        const float noise = p[2] <= -59.5f ? 0.0f : dbToGain (p[2]), inG = p[3] <= -59.5f ? 0.0f : dbToGain (p[3]), depth = p[4] * 0.01f;
        const bool left = ci (8) == 1, pink = ci (9) == 1;
        for (int i0 = 0; i0 < n; i0 += 32)
        {
            const int m = std::min (32, n - i0);
            long long idx;
            if (clock.tick (ctx, i0, divBeats (ci (6)), idx))
            {
                if (mode == 1)
                {
                    const float r = hashUni ((uint32_t) idx, 5u);
                    deg += r < 0.4f ? 1 : (r < 0.8f ? -1 : (r < 0.9f ? 2 : -2));
                    deg = std::max (-sizes[sc], std::min (sizes[sc] + 2, deg));
                }
                else if (mode == 2) deg = (int) (hashUni ((uint32_t) idx, 9u) * (float) (sizes[sc] + 1));
                else deg = 0;
            }
            const int oct = (int) std::floor ((float) deg / (float) sizes[sc]);
            const int d = deg - oct * sizes[sc];
            const float note = p[0] + (float) (12 * oct + scales[sc][d]);
            const float hz = midiToHz (note);
            curHz = curHz <= 0 ? hz : curHz + 0.3f * (hz - curHz);
            for (auto& a : ap) a.set (Biquad::AP, curHz, p[1], 0, sr);
            for (int i = i0; i < i0 + m; ++i)
            {
                const float nz = noise > 0 ? nzg.get (pink) * noise : 0.0f;
                const float sl = nz + L[i] * inG, sr2 = nz + R[i] * inG;
                if (left) { L[i] = lerpf (sl, ap[0].process (sl), depth); R[i] = sr2; }
                else { L[i] = sl; R[i] = lerpf (sr2, ap[1].process (sr2), depth); }
            }
        }
        setViz (0, curHz); setViz (1, (float) deg); setViz (2, left ? 1.0f : 0.0f); setViz (3, p[1]); setViz (4, depth);
    }
private:
    std::array<Biquad, 2> ap {};
    Noise nzg;
    StepClock clock;
    int deg = 0;
    float curHz = 0;
};

//==============================================================================
// BINAURAL - two slightly different frequencies, one per ear. There is no beat
// in either ear; the beat is made by the brain.
class Binaural : public Module
{
public:
    const MInfo& info() const override
    {
        static const MInfo m { "bin", "BINRL", "BINAURAL", "Binaural beats (Dove 1839, Oster 1973)", pageSpace, true,
            "Left ear: f - b/2. Right ear: f + b/2. No ear on its own contains a beat.",
            "A slow wobble at b Hz that seems to sit in the middle of the head.", nullptr,
            { F ("carrier", "CARRIER", 60, 1000, 220, 250, "Hz", "TONE: the centre frequency of the two tones"),
              F ("beat", "BEAT", 0.5f, 40, 6, 6, "Hz", "Difference between the ears (FREE sync)"),
              F ("level", "LEVEL", -48, 0, -18, 0, "dB", "TONE / MONAURAL: tone level"),
              F ("drift", "DRIFT", 0, 100, 0, 0, "%", "Slow wander of the beat rate"),
              C ("mode", "MODE", "TONE|SHIFT|MONAURAL", 0, "TONE: two tones. SHIFT: shift your signal down in one ear, up in the other. MONAURAL: both tones in both ears (a real, physical beat to compare)"),
              C ("sync", "SYNC", "FREE|1/16|1/8|1/4|1/2|1 BAR", 0, "Beat locked to the host tempo"),
              C ("wave", "WAVE", "SINE|SOFT", 0, "Tone shape") } };
        return m;
    }
    bool generates() const override { return ci (4) != 1; }
    void prepare (double s, int) override { sr = s; for (auto& f : fs) f.prepare (sr); reset(); }
    void reset() override { for (auto& f : fs) f.reset(); pl = pr = 0; t = 0; }
    void process (float* L, float* R, int n, const Ctx& ctx) override
    {
        static const double syncB[] = { 0, 0.25, 0.5, 1, 2, 4 };
        const int mode = ci (4), sync = ci (5);
        float beat = p[1];
        if (sync > 0) beat = (float) (ctx.bpm / 60.0 / syncB[sync]);
        beat *= 1.0f + p[3] * 0.005f * (float) std::sin (t * 0.21);
        const float fc = p[0], lvl = dbToGain (p[2]);
        const bool soft = ci (6) == 1;
        auto wave = [soft] (double ph) { const float s = (float) std::sin (6.283185307179586 * ph); return soft ? s * (1.0f - 0.15f * s * s) * 1.15f : s; };
        for (int i = 0; i < n; ++i)
        {
            if (mode == 1)
            {
                L[i] = fs[0].process (L[i], -0.5f * beat);
                R[i] = fs[1].process (R[i], 0.5f * beat);
            }
            else
            {
                pl += (fc - 0.5 * beat) / sr; pr += (fc + 0.5 * beat) / sr;
                pl -= std::floor (pl); pr -= std::floor (pr);
                const float a = wave (pl) * lvl, b = wave (pr) * lvl;
                if (mode == 0) { L[i] += a; R[i] += b; }
                else { const float m = 0.5f * (a + b); L[i] += m; R[i] += m; }
            }
        }
        t += n / sr;
        double d = pr - pl; d -= std::floor (d);
        setViz (0, fc); setViz (1, beat); setViz (2, (float) d); setViz (3, (float) mode); setViz (4, lvl);
    }
private:
    std::array<FreqShifter, 2> fs;
    double pl = 0, pr = 0, t = 0;
};

//==============================================================================
// OCTAVE - Deutsch's octave illusion: a low and a high tone an octave apart,
// switching ears every 250 ms. Most people hear the high tone in one ear and
// the low tone in the other, alternating.
class OctaveIllusion : public Module
{
public:
    const MInfo& info() const override
    {
        static const MInfo m { "oct", "OCTAV", "OCTAVE", "Octave illusion (Deutsch 1974)", pageSpace, true,
            "Low in one ear, the octave in the other; the two swap ears on every step.",
            "A high tone in one ear alternating with a low tone in the OTHER ear (which ear varies by person).", nullptr,
            { FI ("interval", "INTERVAL", 1, 24, 12, "st", "Distance between the low and the high copy (12 = the octave)"),
              F ("xfade", "XFADE", 0, 50, 3, 0, "ms", "Crossfade at each swap"),
              F ("tone", "TONE", 200, 1000, 400, 400, "Hz", "TONE source: the low tone (Deutsch used 400 / 800 Hz)"),
              F ("level", "TONE LVL", -48, 0, -18, 0, "dB", "TONE source level"),
              F ("grain", "GRAIN", 20, 120, 50, 0, "ms", "INPUT: grain of the pitch shifter"),
              C ("rate", "RATE", divChoices(), 4, "Time between swaps (250 ms = 1/8 at 120 BPM)"),
              C ("src", "SOURCE", "INPUT|TONE|BOTH", 0, "Your signal, Deutsch's sine stimulus, or both"),
              C ("start", "START", "HIGH LEFT|HIGH RIGHT", 0, "Which ear starts with the high copy") } };
        return m;
    }
    bool generates() const override { return ci (6) != 0; }
    void prepare (double s, int) override { sr = s; sh.prepare (sr, 130.0f); reset(); }
    void reset() override { sh.reset(); clock.reset(); state = 0; sm = 0; ph = 0; }
    void process (float* L, float* R, int n, const Ctx& ctx) override
    {
        const float ratio = std::pow (2.0f, std::round (p[0]) / 12.0f), lvl = dbToGain (p[3]);
        const int src = ci (6);
        sh.setWindow (p[4]); sh.setRatio (ratio);
        OnePole xf; xf.setMs (std::max (0.05f, p[1] * 0.4f), sr);
        for (int i = 0; i < n; ++i)
        {
            long long idx;
            if (clock.tick (ctx, i, divBeats (ci (5)), idx)) state = (int) ((idx + ci (7)) & 1);
            sm = (float) state + xf.a * (sm - (float) state);
            const float m = 0.5f * (L[i] + R[i]);
            float lo = 0, hi = 0;
            if (src != 1) { lo += m; hi += sh.process (m); }
            if (src != 0)
            {
                ph += p[2] / sr; ph -= std::floor (ph);
                lo += (float) std::sin (6.283185307179586 * ph) * lvl;
                hi += (float) std::sin (6.283185307179586 * ph * ratio) * lvl;   // ratio is integer at the octave: phase-locked
            }
            // state 0: high left, low right. state 1: swapped.
            L[i] = hi * (1.0f - sm) + lo * sm;
            R[i] = lo * (1.0f - sm) + hi * sm;
        }
        setViz (0, sm); setViz (1, (float) state); setViz (2, std::round (p[0])); setViz (3, (float) src);
    }
private:
    Shifter sh;
    StepClock clock;
    int state = 0;
    float sm = 0;
    double ph = 0;
};

//==============================================================================
// DICHOTIC - Deutsch's scale, chromatic and cambiata illusions route the notes
// of a melody to alternating ears; listeners hear them regrouped by pitch
// (high notes in one ear, low notes in the other). GLISSANDO maps pitch to
// position, which is what the brain does with the glissando illusion.
class Dichotic : public Module
{
public:
    const MInfo& info() const override
    {
        static const MInfo m { "dic", "DICHO", "DICHOTIC", "Scale, chromatic, cambiata and glissando illusions (Deutsch 1975-1995)", pageSpace, true,
            "Successive notes are sent to alternating ears (or panned by pitch).",
            "The notes regroup by pitch: a smooth high line in one ear, a smooth low line in the other.", nullptr,
            { F ("width", "WIDTH", 0, 100, 100, 0, "%", "How hard the ears are separated (0 = the original stereo)"),
              F ("xfade", "XFADE", 1, 40, 5, 0, "ms", "Crossfade when a note changes ear"),
              F ("split", "SPLIT", 100, 4000, 600, 600, "Hz", "PITCH SORT: crossover. GLISSANDO: centre of the pitch-to-pan map"),
              F ("range", "RANGE", 1, 5, 3, 0, "oct", "GLISSANDO: pitch range spread across the stereo field"),
              F ("sens", "SENSITIVITY", 0, 100, 60, 0, "%", "ONSETS: note detection sensitivity"),
              C ("mode", "MODE", "SCALE|CHROMATIC|CAMBIATA|PITCH SORT|GLISSANDO", 0, "Ear pattern after Deutsch's illusions, or pitch-driven routing"),
              C ("step", "STEP ON", "GRID|ONSETS", 0, "Change ears on the beat grid or on every detected note"),
              C ("rate", "RATE", divChoices(), 4, "GRID: note length") } };
        return m;
    }
    void prepare (double s, int) override { sr = s; yin.prepare (sr); reset(); }
    void reset() override
    {
        clock.reset(); idx = 0; pan = 0; panS = 0; lastF = -1;
        fast.set (0.5f, 30, sr); slow.set (20, 200, sr); refr = 0; pitch = 0;
        histE.fill (0.0f); histP.fill (0.0f);
    }
    void process (float* L, float* R, int n, const Ctx& ctx) override
    {
        static const int pat[3][8] = { { -1, 1, -1, 1, -1, 1, -1, 1 }, { -1, 1, 1, -1, -1, 1, 1, -1 }, { -1, 1, -1, -1, 1, -1, 1, 1 } };
        const int mode = ci (5);
        const float width = p[0] * 0.01f;
        const bool onsets = ci (6) == 1;
        const float thr = 1.3f + (1.0f - p[4] * 0.01f) * 3.0f;
        if (p[2] != lastF) { lastF = p[2]; for (int c = 0; c < 2; ++c) { lo[c][0].set (Biquad::LP, p[2], 0.7071, 0, sr); lo[c][1].set (Biquad::LP, p[2], 0.7071, 0, sr); hi[c][0].set (Biquad::HP, p[2], 0.7071, 0, sr); hi[c][1].set (Biquad::HP, p[2], 0.7071, 0, sr); } }
        OnePole xf; xf.setMs (p[1] * 0.4f, sr);
        OnePole gl; gl.setMs (30.0f, sr);
        for (int i = 0; i < n; ++i)
        {
            const float m = 0.5f * (L[i] + R[i]);
            bool stepNow = false;
            long long gi;
            if (! onsets) stepNow = clock.tick (ctx, i, divBeats (ci (7)), gi);
            else
            {
                const float f = fast.process (m), s = slow.process (m);
                if (refr > 0) --refr;
                else if (f > s * thr && f > 1.0e-3f) { stepNow = true; refr = (int) (0.06 * sr); }
            }
            if (mode == 4 && yin.push (m) && yin.conf > 0.5f) pitch = yin.hz;
            if (stepNow)
            {
                ++idx;
                if (mode < 3) pan = (float) pat[mode][(size_t) (idx & 7)];
                for (int k = 15; k > 0; --k) { histE[(size_t) k] = histE[(size_t) k - 1]; histP[(size_t) k] = histP[(size_t) k - 1]; }
                histE[0] = pan; histP[0] = pitch;
            }
            if (mode == 4 && pitch > 0)
                pan = clampf (std::log2 (pitch / p[2]) / (p[3] * 0.5f), -1.0f, 1.0f);
            if (mode < 3) panS = pan + xf.a * (panS - pan);
            else if (mode == 4) panS = pan + gl.a * (panS - pan);
            float yl, yr;
            if (mode == 3)
            {
                const float l0 = lo[0][1].process (lo[0][0].process (L[i])), l1 = lo[1][1].process (lo[1][0].process (R[i]));
                const float h0 = hi[0][1].process (hi[0][0].process (L[i])), h1 = hi[1][1].process (hi[1][0].process (R[i]));
                yl = l0 + l1; yr = h0 + h1;
                panS = 0;
            }
            else
            {
                float gL, gR;
                panGains (panS, gL, gR);
                yl = m * gL * 1.4142f; yr = m * gR * 1.4142f;
            }
            L[i] = lerpf (L[i], yl, width); R[i] = lerpf (R[i], yr, width);
        }
        setViz (0, panS); setViz (1, pitch); setViz (2, (float) mode);
        for (int k = 0; k < 16; ++k) { setViz (3 + k, histE[(size_t) k]); setViz (19 + k, histP[(size_t) k]); }
        setViz (35, (float) (idx & 0xffff));
    }
private:
    StepClock clock;
    Yin yin;
    Env fast, slow;
    Biquad lo[2][2], hi[2][2];
    long long idx = 0;
    int refr = 0;
    float pan = 0, panS = 0, lastF = -1, pitch = 0;
    std::array<float, 16> histE {}, histP {};
};

//==============================================================================
// FRANSSEN - the attack comes from one side, the sustain from the other. The
// whole sound is heard where the attack came from (in a reverberant room).
class Franssen : public Module
{
public:
    const MInfo& info() const override
    {
        static const MInfo m { "fra", "FRANS", "FRANSSEN", "Franssen effect (Franssen 1960)", pageSpace, false,
            "Each attack is panned one way; everything after the attack is panned the other way.",
            "The whole note is heard where its attack came from, the sustain 'follows' it.", nullptr,
            { F ("tpan", "ATTACK", -100, 100, -100, 0, "%", "Where the attacks go"),
              F ("span", "SUSTAIN", -100, 100, 100, 0, "%", "Where everything else goes"),
              F ("sens", "SENSITIVITY", 0, 100, 60, 0, "%", "How easily an attack is detected"),
              F ("hold", "ATTACK LEN", 1, 100, 15, 0, "ms", "How long the attack side is held"),
              F ("rel", "RELEASE", 10, 500, 80, 80, "ms", "Crossfade from attack side to sustain side"),
              F ("keep", "KEEP", 0, 100, 0, 0, "%", "Original stereo mixed back in") } };
        return m;
    }
    void prepare (double s, int) override { sr = s; reset(); }
    void reset() override { fast.set (0.2f, 8, sr); slow.set (12, 150, sr); w = 0; holdC = 0; }
    void process (float* L, float* R, int n, const Ctx&) override
    {
        const float thr = 0.25f + (1.0f - p[2] * 0.01f) * 2.5f, keep = p[5] * 0.01f;
        const int hold = (int) (p[3] * 0.001 * sr);
        const float rel = (float) std::exp (-1.0 / (0.001 * p[4] * sr));
        float tl, tr, sl, srr;
        panGains (p[0] * 0.01f, tl, tr); panGains (p[1] * 0.01f, sl, srr);
        float fe = 0, se = 0;
        for (int i = 0; i < n; ++i)
        {
            const float m = 0.5f * (L[i] + R[i]);
            const float f = fast.process (m), s = slow.process (m);
            const float det = clampf ((f / (s + 1.0e-5f) - 1.0f) / thr, 0.0f, 1.0f);
            if (det > 0.5f) holdC = hold;
            if (holdC > 0) { --holdC; w = std::max (w, det > 0.5f ? 1.0f : w); }
            else w *= rel;
            const float T = m * w, S = m * (1.0f - w);
            L[i] = (T * tl + S * sl) * 1.4142f + keep * L[i];
            R[i] = (T * tr + S * srr) * 1.4142f + keep * R[i];
            fe = std::max (fe, f); se = std::max (se, s);
        }
        setViz (0, w); setViz (1, se); setViz (2, fe); setViz (3, p[0] * 0.01f); setViz (4, p[1] * 0.01f);
    }
private:
    Env fast, slow;
    float w = 0;
    int holdC = 0;
};

//==============================================================================
// HAAS - the precedence effect: the same sound from both sides, one side
// 1-40 ms late, is heard as one sound coming from the early side.
class Haas : public Module
{
public:
    const MInfo& info() const override
    {
        static const MInfo m { "haas", "HAAS", "HAAS", "Precedence effect (Wallach 1949, Haas 1951)", pageSpace, false,
            "Both channels carry the same sound; one is delayed by a few milliseconds.",
            "One sound, coming from the early side - not an echo, even with the late side louder.", nullptr,
            { F ("delay", "DELAY", 0, 40, 12, 0, "ms", "Delay of the late side (1-40 ms fuses into one sound)"),
              F ("level", "LATE LVL", -12, 6, 0, 0, "dB", "Level of the late side (up to about +6 dB it still loses)"),
              F ("lowcut", "MONO BELOW", 20, 1000, 150, 200, "Hz", "Below this both sides stay identical (mono-safe bass)"),
              F ("mdepth", "MOD DEPTH", 0, 10, 0, 0, "ms", "Delay modulation"),
              F ("mrate", "MOD RATE", 0.05f, 5, 0.3f, 0.5f, "Hz", "Modulation rate"),
              C ("lead", "EARLY SIDE", "LEFT|RIGHT", 0, "The side the sound is heard from"),
              C ("input", "INPUT", "MONO|STEREO", 0, "MONO sums the input first (strongest effect)") } };
        return m;
    }
    void prepare (double s, int) override { sr = s; for (auto& d : dl) d.prepare ((int) (sr * 0.06) + 8); reset(); }
    void reset() override { for (auto& d : dl) d.reset(); for (auto& l : lp) l.reset(); cur = -1; ph = 0; }
    void process (float* L, float* R, int n, const Ctx&) override
    {
        const bool leftLead = ci (5) == 0, mono = ci (6) == 0;
        const float lvl = dbToGain (p[1]);
        for (auto& l : lp) l.setHz (p[2], sr);
        for (auto& l : lp2) l.setHz (p[2], sr);
        OnePole sm; sm.setMs (40.0f, sr);
        for (int i = 0; i < n; ++i)
        {
            ph += p[4] / sr; ph -= std::floor (ph);
            const float target = (p[0] + p[3] * 0.5f * (1.0f + (float) std::sin (6.283185307179586 * ph))) * 0.001f * (float) sr;
            cur = cur < 0 ? target : target + sm.a * (cur - target);
            float a = L[i], b = R[i];
            if (mono) a = b = 0.5f * (L[i] + R[i]);
            float& late = leftLead ? b : a;
            const int c = leftLead ? 1 : 0;
            dl[(size_t) c].push (late);
            const float low = lp[(size_t) c].process (late);
            const float d = dl[(size_t) c].cubic (std::max (1.0f, cur));
            const float dLow = lp2[(size_t) c].process (d);
            late = low + (d - dLow) * lvl;
            L[i] = a; R[i] = b;
        }
        setViz (0, cur / (float) sr * 1000.0f); setViz (1, leftLead ? 0.0f : 1.0f); setViz (2, p[1]); setViz (3, p[2]);
    }
private:
    std::array<DelayLine, 2> dl;
    std::array<OnePole, 2> lp {}, lp2 {};
    float cur = -1;
    double ph = 0;
};

//==============================================================================
// DOPPLER - a moving source: the travel time to the listener changes, so the
// pitch slides up while it approaches and down while it leaves.
class Doppler : public Module
{
public:
    const MInfo& info() const override
    {
        static const MInfo m { "dop", "DOPLR", "DOPPLER", "Doppler shift of a moving source (Doppler 1842)", pageSpace, false,
            "A variable delay (distance / speed of sound), distance loudness, air absorption and panning.",
            "A source flying past or around you; its pitch bends as it approaches and leaves.", nullptr,
            { F ("speed", "SPEED", 1, 80, 20, 20, "m/s", "Source speed (FREE sync)"),
              F ("dist", "CLOSEST", 1, 30, 4, 5, "m", "Closest distance to the listener (ORBIT: radius)"),
              F ("len", "PATH", 10, 200, 60, 60, "m", "Length of the fly-by path"),
              F ("amount", "DOPPLER", 0, 200, 100, 0, "%", "Scales the pitch bend (100 % = physical)"),
              F ("air", "AIR", 0, 100, 50, 0, "%", "High frequencies lost with distance"),
              F ("width", "WIDTH", 0, 100, 100, 0, "%", "Stereo movement"),
              C ("path", "PATH", "PASS|ORBIT|FIGURE 8|RANDOM", 0, "Fly-by, circle around the head, a figure eight, or a random flight"),
              C ("sync", "SYNC", "FREE|1 BAR|2 BARS|4 BARS|8 BARS", 0, "Time for one pass locked to the host tempo"),
              C ("dir", "DIRECTION", "L > R|R > L", 0, "Direction of travel") } };
        return m;
    }
    void prepare (double s, int) override { sr = s; dl.prepare ((int) (sr * 1.4) + 8); reset(); }
    void reset() override { dl.reset(); u = 0; lastDelay = -1; airL = {}; airR = {}; rx = 0; ry = 5; tx = 0; ty = 5; rng.seed (11); }
    void process (float* L, float* R, int n, const Ctx& ctx) override
    {
        static const double syncB[] = { 0, 4, 8, 16, 32 };
        const int path = ci (6), sync = ci (7);
        const float speed = p[0], dist = p[1], len = p[2], amt = p[3] * 0.01f, air = p[4] * 0.01f, width = p[5] * 0.01f;
        const float dirS = ci (8) == 0 ? 1.0f : -1.0f;
        const float circ = path == 1 ? kTwoPi * dist : (path == 2 ? len * 1.5f : len);
        const double perSample = sync > 0 ? ctx.beatsPerSample() / syncB[sync] : (double) speed / circ / sr;
        float ratio = 1.0f, gainV = 0, x = 0, y = dist;
        for (int i = 0; i < n; ++i)
        {
            if (sync > 0) u = fracd ((ctx.ppq + (double) i * ctx.beatsPerSample()) / syncB[sync]);
            else { u += perSample; u -= std::floor (u); }
            float edge = 1.0f;
            switch (path)
            {
                case 0: x = dirS * ((float) u - 0.5f) * len; y = dist; edge = clampf ((float) std::min (u, 1.0 - u) / 0.06f, 0.0f, 1.0f); break;
                case 1: { const float a = dirS * kTwoPi * (float) u; x = dist * std::sin (a); y = dist * std::cos (a); break; }
                case 2: { const float a = dirS * kTwoPi * (float) u; x = 0.5f * len * std::sin (a); y = dist + 0.25f * len * std::sin (2.0f * a) + 0.25f * len; break; }
                default:
                {
                    if ((++wander & 4095) == 0) { tx = rng.bi() * len * 0.5f; ty = rng.bi() * len * 0.5f; }
                    const float k = (float) (speed / sr) / std::max (1.0f, std::hypot (tx - rx, ty - ry));
                    rx += (tx - rx) * std::min (1.0f, k); ry += (ty - ry) * std::min (1.0f, k);
                    x = rx; y = ry;
                }
            }
            const float d = std::max (0.6f, std::hypot (x, y));
            const float delay = d / 343.0f * (float) sr * amt + 2.0f;
            if (lastDelay >= 0) ratio = 1.0f - (delay - lastDelay);
            lastDelay = delay;
            const float m = 0.5f * (L[i] + R[i]);
            dl.push (m);
            const float s = dl.cubic (delay);
            const float g = edge / std::pow (std::max (1.0f, d), 0.85f);
            const float az = std::atan2 (x, y);
            float gl, gr;
            panGains (std::sin (az) * width, gl, gr);
            if ((i & 31) == 0) { const float fc = 18000.0f / (1.0f + air * d / 8.0f); airL.setHz (fc, sr); airR.setHz (fc, sr); }
            L[i] = airL.process (s * g * gl * 1.4142f);
            R[i] = airR.process (s * g * gr * 1.4142f);
            gainV = g;
        }
        setViz (0, x); setViz (1, y); setViz (2, ratio); setViz (3, gainV); setViz (4, (float) path); setViz (5, len); setViz (6, dist);
    }
private:
    DelayLine dl;
    OnePole airL, airR;
    Rng rng;
    double u = 0;
    float lastDelay = -1, rx = 0, ry = 5, tx = 0, ty = 5;
    unsigned wander = 0;
};

//==============================================================================
// HRTF - a parametric head model (Brown & Duda 1998): interaural time
// difference, head shadow, pinna echoes (the elevation cue), a shoulder echo
// and a rear shelf. Puts a sound at an azimuth / elevation on headphones.
class Hrtf : public Module
{
public:
    const MInfo& info() const override
    {
        static const MInfo m { "hrtf", "HRTF", "HRTF", "Pinna, head-shadow and ITD cues (Brown & Duda 1998)", pageSpace, true,
            "Per-ear delay, head-shadow filter, five pinna echoes and a shoulder echo; nothing but filters and delays.",
            "The sound has a place: left / right, but also above, below and behind you.", nullptr,
            { F ("az", "AZIMUTH", -180, 180, 60, 0, "deg", "0 front, 90 right, 180 behind, -90 left"),
              F ("el", "ELEVATION", -40, 90, 0, 0, "deg", "Up / down (the pinna cue)"),
              F ("dist", "DISTANCE", 0.3f, 10, 1.5f, 1.5f, "m", "Distance (level and air)"),
              F ("pinna", "PINNA", 0, 100, 80, 0, "%", "Strength of the pinna echoes"),
              F ("head", "HEAD", 7, 11, 8.75f, 0, "cm", "Head radius"),
              F ("mrate", "MOVE RATE", 0.02f, 2, 0.2f, 0.2f, "Hz", "Motion speed"),
              F ("mdepth", "MOVE DEPTH", 0, 180, 90, 0, "deg", "Motion range (BOUNCE / ELEVATE / RANDOM)"),
              C ("motion", "MOTION", "OFF|ORBIT|ELEVATE|BOUNCE|RANDOM", 0, "Automatic movement of the source") } };
        return m;
    }
    void prepare (double s, int) override
    {
        sr = s;
        for (auto& e : ears) { e.itd.prepare ((int) (sr * 0.004) + 8); e.pin.prepare ((int) (sr * 0.004) + 8); }
        reset();
    }
    void reset() override { for (auto& e : ears) { e.itd.reset(); e.pin.reset(); e.hs = {}; e.rear.reset(); e.d = -1; } t = 0; raz = 0; rel = 0; rng.seed (3); air.reset(); air2.reset(); }
    void process (float* L, float* R, int n, const Ctx&) override
    {
        const int motion = ci (7);
        const float a = p[4] * 0.01f, c = 343.0f, pin = p[3] * 0.01f, md = p[6];
        float az = p[0], el = p[1];
        const float sc = (float) (sr / 44100.0);
        static const float rho[5] = { 0.5f, -1.0f, 0.5f, -0.25f, 0.25f }, An[5] = { 1, 5, 5, 5, 5 }, Bn[5] = { 2, 4, 7, 11, 13 }, Dn[5] = { 1, 0.5f, 0.5f, 0.5f, 0.5f };
        for (int i0 = 0; i0 < n; i0 += 32)
        {
            const int m = std::min (32, n - i0);
            t += m / sr;
            const float ph = (float) (t * p[5]);
            float A = az, E = el;
            switch (motion)
            {
                case 1: A = az + 360.0f * frac (ph); break;
                case 2: E = el + md * 0.5f * std::sin (kTwoPi * ph); break;
                case 3: A = az + md * std::sin (kTwoPi * ph); break;
                case 4:
                {
                    const long long cell = (long long) std::floor (ph);
                    if (cell != lastCell) { lastCell = cell; tAz = az + rng.bi() * md; tEl = el + rng.bi() * md * 0.4f; }
                    raz += 0.03f * (tAz - raz); rel += 0.03f * (tEl - rel); A = raz; E = rel; break;
                }
                default: break;
            }
            A = std::fmod (A + 540.0f, 360.0f) - 180.0f;
            E = clampf (E, -40.0f, 90.0f);
            curAz = A; curEl = E;
            const float ar = A * kPi / 180.0f, er = E * kPi / 180.0f;
            const float sx = std::sin (ar) * std::cos (er);
            for (int e = 0; e < 2; ++e)
            {
                auto& ear = ears[(size_t) e];
                const float cosT = e == 1 ? sx : -sx;                  // angle to this ear's axis
                const float th = std::acos (clampf (cosT, -1.0f, 1.0f)); // 0 = straight into the ear
                const float thDeg = th * 180.0f / kPi;
                // head shadow
                const float alpha = (1.0f + 0.05f) + (1.0f - 0.05f) * std::cos (thDeg / 150.0f * kPi);
                const float w0 = c / a, K = 2.0f * (float) sr;
                ear.hs.b0 = (2 * w0 + alpha * K) / (2 * w0 + K);
                ear.hs.b1 = (2 * w0 - alpha * K) / (2 * w0 + K);
                ear.hs.a1 = (2 * w0 - K) / (2 * w0 + K);
                // interaural time (Woodworth), relative to the head centre, kept positive
                const float tau = th < kPi * 0.5f ? -(a / c) * std::cos (th) : (a / c) * (th - kPi * 0.5f);
                ear.dTarget = (tau + a / c) * (float) sr + 2.0f;
                // pinna echoes (azimuth folded to the front hemisphere)
                const float af = std::abs (A) > 90.0f ? (A > 0 ? 180.0f - A : -180.0f - A) : A;
                for (int k = 0; k < 5; ++k)
                {
                    const float tn = An[k] * std::cos (af * kPi / 360.0f) * std::sin (Dn[k] * (90.0f - E) * kPi / 180.0f) + Bn[k];
                    ear.tau[(size_t) k] = std::max (1.0f, tn * sc);
                }
                // shoulder echo
                ear.shoulder = (1.2f * (1.0f - (E + 40.0f) / 180.0f) + 0.3f) * 0.001f * (float) sr;
                const float back = std::abs (A) > 90.0f ? (std::abs (A) - 90.0f) / 90.0f : 0.0f;
                ear.rear.set (Biquad::HighShelf, 3500, 0.7, -7.0 * back, sr);
                ear.gain = 1.0f;
            }
            const float dist = std::max (0.3f, p[2]);
            const float dg = std::min (2.0f, 1.0f / dist);
            air.setHz (20000.0f / (1.0f + dist * 0.08f), sr); air2.a = air.a;
            OnePole sm; sm.setMs (20.0f, sr);
            for (int i = i0; i < i0 + m; ++i)
            {
                const float x = air.process (0.5f * (L[i] + R[i])) * dg;
                float out[2];
                for (int e = 0; e < 2; ++e)
                {
                    auto& ear = ears[(size_t) e];
                    ear.d = ear.d < 0 ? ear.dTarget : ear.dTarget + sm.a * (ear.d - ear.dTarget);
                    ear.itd.push (x);
                    const float xd = ear.itd.cubic (ear.d);
                    // head shadow (one-pole-one-zero)
                    const float y = ear.hs.b0 * xd + ear.hs.b1 * ear.hs.x1 - ear.hs.a1 * ear.hs.y1;
                    ear.hs.x1 = xd; ear.hs.y1 = y;
                    ear.pin.push (y);
                    float z = y;
                    for (int k = 0; k < 5; ++k) z += pin * rho[k] * ear.pin.read (ear.tau[(size_t) k]);
                    z += 0.3f * ear.pin.read (std::min (ear.shoulder, (float) ear.pin.size() - 8.0f));
                    out[e] = ear.rear.process (z) * 0.8f;
                }
                L[i] = out[0]; R[i] = out[1];
            }
        }
        const float itdUs = (ears[0].d - ears[1].d) / (float) sr * 1.0e6f;
        setViz (0, curAz); setViz (1, curEl); setViz (2, p[2]); setViz (3, itdUs);
        setViz (4, ears[1].tau[1] > 0 ? (float) sr / (2.0f * ears[1].tau[1]) : 0.0f);
        setViz (5, ears[0].hs.b0); setViz (6, ears[1].hs.b0);
    }
private:
    struct HS { float b0 = 1, b1 = 0, a1 = 0, x1 = 0, y1 = 0; };
    struct Ear { DelayLine itd, pin; HS hs; Biquad rear; std::array<float, 5> tau {}; float shoulder = 40, d = -1, dTarget = 2, gain = 1; };
    std::array<Ear, 2> ears;
    OnePole air, air2;
    Rng rng;
    double t = 0;
    float raz = 0, rel = 0, tAz = 0, tEl = 0, curAz = 0, curEl = 0;
    long long lastCell = -1;
};

//==============================================================================
// HOLO - out-of-head localisation on headphones: speaker-like crossfeed,
// early reflections and decorrelation move the image out of the skull.
class Holo : public Module
{
public:
    const MInfo& info() const override
    {
        static const MInfo m { "hol", "HOLO", "HOLO", "Externalisation, out-of-head localisation (holophonics)", pageSpace, true,
            "Crossfeed with an interaural delay, a ring of early reflections and decorrelated sides.",
            "On headphones the sound leaves the middle of your head and sits out in a room.", nullptr,
            { F ("dist", "DISTANCE", 0, 100, 60, 0, "%", "Macro: how far out of the head"),
              F ("xfeed", "CROSSFEED", -24, -3, -9, 0, "dB", "Level of each side in the opposite ear"),
              F ("xcut", "XF CUT", 300, 2000, 700, 700, "Hz", "Crossfeed low-pass (the head shadow)"),
              F ("room", "ROOM", 0, 100, 40, 0, "%", "Early reflection level"),
              F ("size", "SIZE", 2, 30, 8, 8, "m", "Room size"),
              F ("decor", "DECORREL", 0, 100, 50, 0, "%", "Decorrelation of the sides"),
              F ("angle", "ANGLE", 10, 60, 30, 0, "deg", "Virtual speaker angle") } };
        return m;
    }
    void prepare (double s, int) override
    {
        sr = s;
        for (auto& d : xd) d.prepare ((int) (sr * 0.003) + 8);
        for (auto& d : er) d.prepare ((int) (sr * 0.25) + 8);
        for (auto& c : ap) for (auto& d : c) d.prepare ((int) (sr * 0.02) + 8);
        reset();
    }
    void reset() override
    {
        for (auto& d : xd) d.reset();
        for (auto& d : er) d.reset();
        for (auto& c : ap) for (auto& d : c) d.reset();
        for (auto& l : xl) l.reset();
        for (auto& l : el) l.reset();
    }
    void process (float* L, float* R, int n, const Ctx&) override
    {
        static const float tapT[8] = { 0.31f, 0.47f, 0.62f, 0.79f, 0.93f, 1.11f, 1.29f, 1.46f };
        const float dist = p[0] * 0.01f;
        const float xg = dbToGain (p[1]) * (0.4f + 0.6f * dist), room = p[3] * 0.01f * dist, dec = p[5] * 0.01f;
        const float itd = 0.00065f * std::sin (p[6] * kPi / 180.0f) * (float) sr;
        for (auto& l : xl) l.setHz (p[2], sr);
        for (auto& l : el) l.setHz (5000.0f, sr);
        const float base = p[4] / 343.0f * (float) sr * 0.5f;
        static const int apLen[2][2] = { { 113, 337 }, { 149, 271 } };
        for (int i = 0; i < n; ++i)
        {
            const float a = L[i], b = R[i];
            xd[0].push (a); xd[1].push (b);
            float ol = a + xg * xl[0].process (xd[1].cubic (std::max (1.0f, itd)));
            float orr = b + xg * xl[1].process (xd[0].cubic (std::max (1.0f, itd)));
            er[0].push (a); er[1].push (b);
            float el0 = 0, el1 = 0, g = 0.75f;
            for (int k = 0; k < 8; ++k)
            {
                const float dk = std::min ((float) er[0].size() - 8.0f, base * tapT[k] * (1.0f + 0.07f * (float) (k & 1)));
                const float s0 = er[(size_t) (k & 1)].read (dk), s1 = er[(size_t) ((k + 1) & 1)].read (dk * 1.09f);
                el0 += s0 * g * ((k % 3) == 0 ? -1.0f : 1.0f); el1 += s1 * g * ((k % 3) == 1 ? -1.0f : 1.0f);
                g *= 0.78f;
            }
            ol += el[0].process (el0) * room * 0.6f;
            orr += el[1].process (el1) * room * 0.6f;
            // decorrelation: two Schroeder all-passes per side, different lengths
            for (int c = 0; c < 2; ++c)
            {
                float& v = c == 0 ? ol : orr;
                float y = v;
                for (int k = 0; k < 2; ++k)
                {
                    auto& d = ap[(size_t) c][(size_t) k];
                    const float del = d.at (apLen[c][k] * (int) std::max (1.0, sr / 48000.0));
                    const float w = y + 0.5f * del;
                    d.push (w);
                    y = del - 0.5f * w;
                }
                v = lerpf (v, y, dec * 0.6f);
            }
            const float norm = 1.0f / (1.0f + xg * 0.5f + room * 0.3f);
            L[i] = ol * norm; R[i] = orr * norm;
        }
        setViz (0, dist); setViz (1, room); setViz (2, p[4]); setViz (3, p[6]); setViz (4, dec);
    }
private:
    std::array<DelayLine, 2> xd, er;
    std::array<std::array<DelayLine, 2>, 2> ap;
    std::array<OnePole, 2> xl {}, el {};
};

ModulePtr makeHuggins() { return std::make_unique<Huggins>(); }
ModulePtr makeBinaural() { return std::make_unique<Binaural>(); }
ModulePtr makeOctave() { return std::make_unique<OctaveIllusion>(); }
ModulePtr makeDichotic() { return std::make_unique<Dichotic>(); }
ModulePtr makeFranssen() { return std::make_unique<Franssen>(); }
ModulePtr makeHaas() { return std::make_unique<Haas>(); }
ModulePtr makeDoppler() { return std::make_unique<Doppler>(); }
ModulePtr makeHrtf() { return std::make_unique<Hrtf>(); }
ModulePtr makeHolo() { return std::make_unique<Holo>(); }
} // namespace il
