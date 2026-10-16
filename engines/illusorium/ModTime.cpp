// TIME page: Risset rhythm, speech-to-song, illusory continuity, illusory
// discontinuity, auditory streaming. (Phantom words lives on PERCEPT but
// shares the loop buffer code, so it is here too.)
#include "Spec.h"

namespace il
{
// a stereo capture buffer
struct Tape
{
    std::vector<float> l, r;
    int len = 0;
    void allocate (int n) { l.assign ((size_t) std::max (16, n), 0.0f); r.assign ((size_t) std::max (16, n), 0.0f); len = 0; }
    int capacity() const { return (int) l.size(); }
    float readL (double pos) const { return interp (l, pos); }
    float readR (double pos) const { return interp (r, pos); }
private:
    float interp (const std::vector<float>& b, double pos) const
    {
        if (len < 2) return 0.0f;
        pos = std::fmod (pos, (double) len);
        if (pos < 0) pos += len;
        const int i = (int) pos;
        const float f = (float) (pos - i);
        const int j = i + 1 >= len ? 0 : i + 1;
        return b[(size_t) i] + f * (b[(size_t) j] - b[(size_t) i]);
    }
};

//==============================================================================
// RISSET - an eternal accelerando. A captured loop plays in K layers whose
// tempi are octaves apart (x1, x2, x4...). All layers speed up together; a
// fixed envelope fades the fastest out while a new slowest one fades in, so
// the tempo keeps rising and never gets anywhere.
class Risset : public Module
{
public:
    const MInfo& info() const override
    {
        static const MInfo m { "ris", "RISST", "RISSET", "Risset rhythm, eternal accelerando (Risset 1960s, Stowell 2011)", pageTime, false,
            "The same loop in layers at x1, x2, x4... tempo, all accelerating, crossfaded under a fixed envelope.",
            "A beat that speeds up forever but never gets faster.", "GRAB",
            { F ("cycle", "CYCLE", 1, 32, 8, 8, "bars", "Bars for the tempo to double (one full cycle of the illusion)"),
              F ("fade", "FADE", 1, 30, 4, 0, "ms", "SLICE: fade on every retriggered slice"),
              F ("sharp", "FOCUS", 0.5f, 4, 1, 1.5f, "", "Envelope sharpness: higher = fewer audible layers at once"),
              F ("level", "LEVEL", -12, 12, 0, 0, "dB", "Output level of the layers"),
              C ("loop", "LOOP", "2 BEATS|1 BAR|2 BARS|4 BARS", 1, "Length of the captured loop"),
              C ("dir", "DIRECTION", "ACCEL|DECEL", 0, "Eternal accelerando or eternal ritardando"),
              C ("layers", "LAYERS", "2|3|4|5", 2, "Number of tempo layers"),
              C ("mode", "MODE", "SLICE|TAPE", 0, "SLICE: slices retriggered at their own pitch (tempo only). TAPE: varispeed, the pitch rises too"),
              C ("grab", "CAPTURE", "ONCE|EVERY CYCLE|LIVE", 0, "ONCE: grab when switched on or on GRAB. EVERY CYCLE: re-grab each cycle. LIVE: keep recording"),
              C ("steps", "SLICES", "1/4|1/8|1/16", 2, "SLICE: slice grid") } };
        return m;
    }
#ifdef METAMODULE
    // SemiFamily: 10 s stereo tape on MetaModule (RAM); longer loops are cut to the tape
    void prepare (double s, int) override { sr = s; tape.allocate ((int) std::min (sr * 10.0, 4800000.0)); reset(); }
#else
    void prepare (double s, int) override { sr = s; tape.allocate ((int) std::min (sr * 24.0, 4800000.0)); reset(); }
#endif
    void reset() override
    {
        tape.len = 0; state = Waiting; recPos = 0; phi = 0; Q = 1; voices = {}; lastStep.fill (-1);
        grabReq = true; lastCycle = -1;
    }
    void trigger() override { grabReq = true; }
    void process (float* L, float* R, int n, const Ctx& ctx) override
    {
        static const double loops[] = { 2, 4, 8, 16 };
        static const double sliceB[] = { 1.0, 0.5, 0.25 };
        const double loopB = loops[std::min (3, ci (4))];
        const int K = 2 + std::min (3, ci (6));
        const bool decel = ci (5) == 1, tapeMode = ci (7) == 1;
        const int grab = ci (8);
        const double slice = sliceB[std::min (2, ci (9))];
        const float lvl = dbToGain (p[3]), sharp = p[2];
        const double cycleBeats = std::max (1.0f, p[0]) * 4.0;
        const double bps = ctx.beatsPerSample();
        const int fadeS = std::max (8, (int) (p[1] * 0.001 * sr));
        const double c = std::pow (2.0, (double) K * 0.5);

        for (int i = 0; i < n; ++i)
        {
            const double beat = ctx.ppq + (double) i * bps;
            const float inL = L[i], inR = R[i];
            // ---- capture -------------------------------------------------------------
            if (grabReq && state != Recording)
            {
                // wait for the next loop boundary so the loop lands on the grid
                if (lastBeat >= 0 && std::floor (beat / loopB) != std::floor (lastBeat / loopB)) { grabReq = false; state = Recording; recPos = 0; recLen = (int) std::min ((double) tape.capacity(), loopB * ctx.samplesPerBeat()); capSpb = ctx.samplesPerBeat(); }
            }
            if (state == Recording || (grab == 2 && state == Playing))
            {
                if (state == Recording)
                {
                    tape.l[(size_t) recPos] = inL; tape.r[(size_t) recPos] = inR;
                    if (++recPos >= recLen) { tape.len = recLen; state = Playing; }
                }
                else if (tape.len > 0)
                {
                    const int w = (int) std::fmod (beat / loopB * tape.len, (double) tape.len);
                    tape.l[(size_t) w] = 0.5f * (tape.l[(size_t) w] + inL); tape.r[(size_t) w] = 0.5f * (tape.r[(size_t) w] + inR);
                }
            }
            lastBeat = beat;
            if (state != Playing) { continue; }   // passes the input while waiting / recording

            // ---- the accelerando ---------------------------------------------------------
            phi += (decel ? -bps : bps) / cycleBeats;
            // at the wrap every layer takes over its neighbour's place (same position, same speed)
            if (phi >= 1.0)
            {
                phi -= 1.0; Q *= 0.5;
                for (auto& v : voices) ++v.layer;
                for (int k = 4; k > 0; --k) lastStep[(size_t) k] = lastStep[(size_t) k - 1];
                lastStep[0] = -1;
            }
            if (phi < 0.0)
            {
                phi += 1.0; Q *= 2.0;
                for (auto& v : voices) --v.layer;
                for (int k = 0; k < 4; ++k) lastStep[(size_t) k] = lastStep[(size_t) k + 1];
                lastStep[4] = -1;
            }
            if (grab == 1)
            {
                const long long cyc = (long long) std::floor (beat / cycleBeats);
                if (lastCycle >= 0 && cyc != lastCycle) grabReq = true;
                lastCycle = cyc;
            }
            Q += std::pow (2.0, phi) / c * bps;
            if (Q > 1.0e6) Q = std::fmod (Q, loopB);
            for (int k = 0; k < K; ++k)
            {
                const double u = ((double) k + phi) / (double) K;
                gains[(size_t) k] = (float) std::pow (sqr ((float) std::sin (3.14159265358979 * u)), sharp);
                speeds[(size_t) k] = (float) (std::pow (2.0, (double) k + phi) / c);
                posB[(size_t) k] = std::fmod (std::ldexp (Q, k), loopB);
            }
            float ol = 0, orr = 0;
            const double spbT = tape.len / loopB;   // samples per loop beat in the tape
            if (tapeMode)
            {
                for (int k = 0; k < K; ++k)
                {
                    const double pos = posB[(size_t) k] * spbT;
                    ol += tape.readL (pos) * gains[(size_t) k];
                    orr += tape.readR (pos) * gains[(size_t) k];
                }
            }
            else
            {
                for (int k = 0; k < K; ++k)
                {
                    const long long st = (long long) std::floor (posB[(size_t) k] / slice);
                    if (st != lastStep[(size_t) k])
                    {
                        lastStep[(size_t) k] = st;
                        auto& v = voices[(size_t) (vNext++ % voices.size())];
                        v.pos = (double) st * slice * spbT;
                        v.len = (int) std::min (slice * spbT, slice * ctx.samplesPerBeat() / std::max (0.05f, speeds[(size_t) k]));
                        v.age = 0; v.layer = k; v.on = true;
                        ++ticks[(size_t) k];
                    }
                }
                for (auto& v : voices)
                {
                    if (! v.on) continue;
                    if (v.layer < 0 || v.layer >= K || v.age >= v.len) { v.on = false; continue; }
                    const int f = std::min (fadeS, v.len / 2 + 1);
                    float e = 1.0f;
                    if (v.age < f) e = (float) v.age / (float) f;
                    else if (v.len - v.age < f) e = (float) (v.len - v.age) / (float) f;
                    const float g = e * gains[(size_t) v.layer];
                    ol += tape.readL (v.pos + v.age) * g;
                    orr += tape.readR (v.pos + v.age) * g;
                    ++v.age;
                }
            }
            const float norm = lvl * 1.4f / std::sqrt ((float) K * 0.5f);
            L[i] = ol * norm; R[i] = orr * norm;
        }
        setViz (0, (float) phi); setViz (1, (float) K);
        for (int k = 0; k < 5; ++k)
        {
            setViz (2 + k, k < K ? gains[(size_t) k] : 0.0f);
            setViz (7 + k, k < K ? speeds[(size_t) k] : 0.0f);
            setViz (14 + k, k < K ? (float) (posB[(size_t) k] / loopB) : 0.0f);
            setViz (20 + k, (float) (ticks[(size_t) k] & 0xffff));
        }
        setViz (12, state == Waiting ? 0.0f : (state == Recording ? 1.0f : 2.0f));
        setViz (13, recLen > 0 ? (float) recPos / (float) recLen : 0.0f);
        setViz (19, (float) loopB); setViz (25, decel ? 1.0f : 0.0f); setViz (26, tapeMode ? 1.0f : 0.0f);
    }
private:
    enum State { Waiting, Recording, Playing };
    struct Voice { double pos = 0; int len = 0, age = 0, layer = 0; bool on = false; };
    Tape tape;
    State state = Waiting;
    int recPos = 0, recLen = 0;
    double capSpb = 24000, phi = 0, Q = 1, lastBeat = -1;
    long long lastCycle = -1;
    bool grabReq = true;
    std::array<Voice, 48> voices {};
    size_t vNext = 0;
    std::array<long long, 5> lastStep {};
    std::array<float, 5> gains {}, speeds {};
    std::array<double, 5> posB {};
    std::array<long long, 5> ticks {};
};

//==============================================================================
// STS - speech-to-song. A spoken phrase repeated a few times starts to sound
// sung: the pitch contour of speech turns into melody.
class SpeechToSong : public Module
{
public:
    const MInfo& info() const override
    {
        static const MInfo m { "sts", "S>SNG", "SPEECH>SONG", "Speech-to-song illusion (Deutsch 1995, 2011)", pageTime, false,
            "A phrase is captured and repeated exactly, with a short gap between repeats.",
            "After a few repeats the spoken words start to sound sung - and stay that way.", "GRAB",
            { F ("len", "LENGTH", 0.25f, 4, 1.5f, 1.2f, "s", "Length of the captured phrase (FREE sync)"),
              F ("gap", "GAP", 0, 1000, 250, 300, "ms", "Silence between repeats"),
              F ("decay", "DECAY", 0, 30, 0, 0, "%", "Level drop per repeat"),
              F ("fade", "FADE", 2, 50, 10, 0, "ms", "Fade at both ends of the phrase"),
              F ("live", "LIVE", 0, 100, 0, 0, "%", "Input kept under the repeats"),
              C ("reps", "REPEATS", "2|4|8|12|16|ENDLESS", 3, "Repeats before the next phrase is taken"),
              C ("grab", "CAPTURE", "AUTO|HOLD", 0, "AUTO: after the repeats, take the next phrase. HOLD: keep this one until GRAB"),
              C ("sync", "SYNC", "FREE|1 BEAT|2 BEATS|1 BAR|2 BARS", 0, "Phrase length in beats instead of seconds") } };
        return m;
    }
    void prepare (double s, int) override { sr = s; tape.allocate ((int) (sr * 9.0)); reset(); }
    void reset() override { tape.len = 0; state = Rec; pos = 0; rep = 0; }
    void trigger() override { grabReq = true; }
    void process (float* L, float* R, int n, const Ctx& ctx) override
    {
        static const int repsT[] = { 2, 4, 8, 12, 16, 1 << 30 };
        static const double syncB[] = { 0, 1, 2, 4, 8 };
        const int sync = ci (7);
        const int lenS = (int) std::min ((double) tape.capacity(), sync > 0 ? syncB[sync] * ctx.samplesPerBeat() : (double) p[0] * sr);
        const int gapS = (int) (p[1] * 0.001 * sr);
        const float decay = p[2] * 0.01f, live = p[4] * 0.01f;
        const int fadeS = std::max (4, (int) (p[3] * 0.001 * sr));
        const int reps = repsT[std::min (5, ci (5))];
        const bool hold = ci (6) == 1;
        if (grabReq) { grabReq = false; state = Rec; pos = 0; rep = 0; }
        for (int i = 0; i < n; ++i)
        {
            const float inL = L[i], inR = R[i];
            if (state == Rec)
            {
                tape.l[(size_t) pos] = inL; tape.r[(size_t) pos] = inR;
                if (++pos >= lenS) { tape.len = lenS; state = Play; pos = 0; rep = 0; }
                continue;   // the input passes while recording
            }
            const int period = tape.len + gapS;
            float ol = 0, orr = 0;
            if (pos < tape.len)
            {
                float e = 1.0f;
                if (pos < fadeS) e = (float) pos / (float) fadeS;
                else if (tape.len - pos < fadeS) e = (float) (tape.len - pos) / (float) fadeS;
                e *= std::pow (1.0f - decay, (float) rep);
                ol = tape.l[(size_t) pos] * e; orr = tape.r[(size_t) pos] * e;
            }
            L[i] = ol + inL * live; R[i] = orr + inR * live;
            if (++pos >= period)
            {
                pos = 0;
                if (++rep >= reps && ! hold) { state = Rec; pos = 0; }
                if (hold && rep > 100000) rep = reps;
            }
        }
        setViz (0, state == Rec ? 0.0f : 1.0f); setViz (1, (float) rep); setViz (2, (float) (reps > 1000 ? 0 : reps));
        setViz (3, state == Rec ? (float) pos / (float) std::max (1, lenS) : (float) pos / (float) std::max (1, tape.len + gapS));
        setViz (4, (float) tape.len / (float) sr); setViz (5, (float) gapS / (float) sr);
    }
private:
    enum State { Rec, Play };
    Tape tape;
    State state = Rec;
    int pos = 0, rep = 0;
    bool grabReq = false;
};

//==============================================================================
// WORDS - phantom words: a short loop of speech in both ears, offset in time.
// The brain keeps 'hearing' new words and phrases that are not there.
class PhantomWords : public Module
{
public:
    const MInfo& info() const override
    {
        static const MInfo m { "wrd", "WORDS", "PHANTOM WORDS", "Phantom words (Deutsch 2003)", pagePercept, true,
            "One short loop, played in the left ear and, shifted in time, in the right.",
            "Words and phrases nobody said pop out of the loop and change as you listen.", "GRAB",
            { F ("len", "LENGTH", 0.15f, 1.5f, 0.45f, 0.45f, "s", "Loop length (one or two syllables works best)"),
              F ("offset", "OFFSET", 0, 100, 50, 0, "%", "Right ear's offset into the loop"),
              F ("fade", "FADE", 1, 40, 8, 0, "ms", "Crossfade at the loop point"),
              F ("drift", "DRIFT", 0, 100, 0, 0, "%", "Slow wander of the offset"),
              F ("live", "LIVE", 0, 100, 0, 0, "%", "Input kept under the loop"),
              C ("grab", "CAPTURE", "AUTO|HOLD", 1, "AUTO: re-grab on the bar grid (EVERY). HOLD: keep the loop until GRAB"),
              C ("every", "EVERY", "1 BAR|2 BARS|4 BARS|8 BARS", 2, "AUTO capture period"),
              C ("rev", "REVERSE", "OFF|RIGHT|BOTH", 0, "Play one or both ears backwards (even more ambiguous)") } };
        return m;
    }
    void prepare (double s, int) override { sr = s; for (auto& t : tapes) t.allocate ((int) (sr * 1.6)); reset(); }
    void reset() override { for (auto& t : tapes) t.len = 0; cur = 0; recPos = -1; play = 0; drift = 0; lastBar = -1; grabReq = true; }
    void trigger() override { grabReq = true; }
    void process (float* L, float* R, int n, const Ctx& ctx) override
    {
        static const double every[] = { 4, 8, 16, 32 };
        const int lenS = std::min (tapes[0].capacity(), (int) (p[0] * sr));
        const float off = p[1] * 0.01f, live = p[4] * 0.01f, dr = p[3] * 0.01f;
        const int fadeS = std::max (4, (int) (p[2] * 0.001 * sr));
        const bool autoGrab = ci (5) == 0;
        const int rev = ci (7);
        for (int i = 0; i < n; ++i)
        {
            const double beat = ctx.ppq + (double) i * ctx.beatsPerSample();
            const long long bar = (long long) std::floor (beat / every[std::min (3, ci (6))]);
            if (autoGrab && lastBar >= 0 && bar != lastBar) grabReq = true;
            lastBar = bar;
            if (grabReq && recPos < 0) { grabReq = false; recPos = 0; recLen = lenS; }
            const float inL = L[i], inR = R[i];
            if (recPos >= 0)
            {
                auto& t = tapes[(size_t) (1 - cur)];
                t.l[(size_t) recPos] = 0.5f * (inL + inR);
                if (++recPos >= recLen) { t.len = recLen; cur = 1 - cur; recPos = -1; play = 0; }
            }
            const auto& t = tapes[(size_t) cur];
            float ol = 0, orr = 0;
            if (t.len > 0)
            {
                drift += 1.0 / sr;
                const double o = std::fmod ((double) off + dr * 0.25 * std::sin (drift * 0.3), 1.0);
                const double pl = std::fmod (play, (double) t.len);
                double pr = std::fmod (pl + (o < 0 ? o + 1.0 : o) * t.len, (double) t.len);
                const double rl = rev == 2 ? t.len - 1 - pl : pl;
                const double rr = rev >= 1 ? t.len - 1 - pr : pr;
                auto win = [&] (double ps) { const double d = std::min (ps, (double) t.len - ps); return (float) std::min (1.0, d / fadeS); };
                ol = t.readL (rl) * win (pl);
                orr = t.readL (rr) * win (pr);
                play += 1.0;
                if (play >= t.len) play -= t.len;
                vizL = (float) (pl / t.len); vizR = (float) (pr / t.len);
            }
            L[i] = (t.len > 0 ? ol : inL) + inL * live;
            R[i] = (t.len > 0 ? orr : inR) + inR * live;
        }
        setViz (0, vizL); setViz (1, vizR); setViz (2, (float) tapes[(size_t) cur].len / (float) sr); setViz (3, recPos >= 0 ? (float) recPos / (float) std::max (1, recLen) : -1.0f);
        setViz (4, off);
    }
private:
    std::array<Tape, 2> tapes;
    int cur = 0, recPos = -1, recLen = 0;
    double play = 0, drift = 0;
    long long lastBar = -1;
    bool grabReq = true;
    float vizL = 0, vizR = 0;
};

//==============================================================================
// CONTINUITY - gaps cut into the sound and filled with louder noise shaped
// like the sound: the ear restores what was cut out.
class Continuity : public Module
{
public:
    const MInfo& info() const override
    {
        static const MInfo m { "con", "CONTI", "CONTINUITY", "Illusory continuity, phonemic restoration (Warren 1970)", pageTime, false,
            "The sound is cut out on a rhythm and every gap is filled with noise shaped like it.",
            "The sound seems to carry on unbroken behind the noise, words included.", nullptr,
            { F ("gap", "GAP", 5, 90, 35, 0, "%", "Length of the gap, in percent of the step"),
              F ("level", "FILL", -12, 18, 6, 0, "dB", "Noise level relative to the sound (the illusion needs it louder)"),
              F ("fade", "FADE", 1, 30, 4, 0, "ms", "Edges of the gap"),
              F ("chance", "CHANCE", 0, 100, 100, 0, "%", "Probability of a gap on each step"),
              F ("track", "SHAPE", 0, 100, 100, 0, "%", "How closely the noise follows the sound's spectrum (8 bands)"),
              C ("rate", "RATE", divChoices(), 4, "Step length"),
              C ("fill", "FILL WITH", "NOISE|SHAPED|SILENCE", 1, "SILENCE: hear the gaps for comparison (no illusion)"),
              C ("place", "PLACE", "END|START|MIDDLE|RANDOM", 0, "Where the gap sits in the step") } };
        return m;
    }
    void prepare (double s, int) override { sr = s; reset(); }
    void reset() override
    {
        static const float fc[8] = { 90, 180, 360, 720, 1440, 2880, 5760, 11520 };
        for (int b = 0; b < 8; ++b)
        {
            an[(size_t) b].set (Biquad::BP, fc[b], 1.4, 0, sr);
            for (auto& c : syn) c[(size_t) b].set (Biquad::BP, fc[b], 1.4, 0, sr);
            envs[(size_t) b].set (3, 40, sr);
        }
        broad.set (3, 40, sr);
        clock.reset(); gate = 1; inGap = false; hist.fill (0.0f); gapA = 1; gapB = 1;
    }
    void process (float* L, float* R, int n, const Ctx& ctx) override
    {
        const double div = divBeats (ci (5));
        const float gap = p[0] * 0.01f, fill = dbToGain (p[1]), chance = p[3] * 0.01f, track = p[4] * 0.01f;
        const int mode = ci (6), place = ci (7);
        OnePole fade; fade.setMs (p[2] * 0.4f, sr);
        float fillSum = 0;
        for (int i = 0; i < n; ++i)
        {
            long long idx;
            if (clock.tick (ctx, i, div, idx))
            {
                const bool on = hashUni ((uint32_t) idx, 991u) < chance;
                float a = 1.0f - gap;
                if (place == 1) a = 0.0f; else if (place == 2) a = 0.5f - gap * 0.5f;
                else if (place == 3) a = hashUni ((uint32_t) idx, 17u) * (1.0f - gap);
                gapA = on ? a : 2.0f; gapB = on ? a + gap : 2.0f;
                for (int k = 15; k > 0; --k) hist[(size_t) k] = hist[(size_t) k - 1];
                hist[0] = on ? 1.0f : 0.0f;
            }
            const float ph = (float) clock.phase (ctx, i, div);
            inGap = ph >= gapA && ph < gapB;
            gate = fade.a * (gate - (inGap ? 0.0f : 1.0f)) + (inGap ? 0.0f : 1.0f);
            const float mid = 0.5f * (L[i] + R[i]);
            // analysis runs only outside the gaps (the held envelope fills them)
            float bandE[8];
            for (int b = 0; b < 8; ++b)
            {
                const float y = an[(size_t) b].process (mid);
                bandE[b] = inGap ? envs[(size_t) b].z : envs[(size_t) b].process (y);
            }
            const float be = inGap ? broad.z : broad.process (mid);
            float fl = 0, fr = 0;
            if (gate < 0.999f && mode != 2)
            {
                const float wl = nz[0].white(), wr = nz[1].white();
                if (mode == 1)
                {
                    float sl = 0, sr2 = 0;
                    for (int b = 0; b < 8; ++b)
                    {
                        sl += syn[0][(size_t) b].process (wl) * bandE[b];
                        sr2 += syn[1][(size_t) b].process (wr) * bandE[b];
                    }
                    fl = lerpf (wl * be, sl * 3.2f, track); fr = lerpf (wr * be, sr2 * 3.2f, track);
                }
                else { fl = wl * be * 1.7f; fr = wr * be * 1.7f; }
            }
            L[i] = L[i] * gate + fl * fill * (1.0f - gate);
            R[i] = R[i] * gate + fr * fill * (1.0f - gate);
            fillSum += std::abs (fl) * (1.0f - gate);
        }
        setViz (0, gate); setViz (17, fillSum / (float) std::max (1, n)); setViz (18, gap); setViz (19, (float) mode);
        setViz (20, gapA > 1.5f ? -1.0f : gapA);
        for (int k = 0; k < 16; ++k) setViz (1 + k, hist[(size_t) k]);
    }
private:
    std::array<Biquad, 8> an {};
    std::array<std::array<Biquad, 8>, 2> syn {};
    std::array<Env, 8> envs {};
    Env broad;
    std::array<Noise, 2> nz;
    StepClock clock;
    float gate = 1, gapA = 2, gapB = 2;
    bool inGap = false;
    std::array<float, 16> hist {};
};

//==============================================================================
// DISCONTINUITY - weak, short noise bursts on top of an unbroken sound: the
// sound seems to stop under each burst (illusory discontinuity).
class Discontinuity : public Module
{
public:
    const MInfo& info() const override
    {
        static const MInfo m { "dis", "DISCN", "DISCONTINUITY", "Illusory discontinuity (Kluender & Jenison 1992; Riecke et al.)", pageTime, false,
            "The sound is never interrupted; quiet band-limited noise bursts are laid on top of it.",
            "The sound seems to break up under every burst, a stutter that is not there.", nullptr,
            { F ("level", "BURST", -40, 0, -18, 0, "dB", "Burst level relative to the sound (weak = non-masking)"),
              F ("len", "LENGTH", 20, 200, 60, 0, "ms", "Burst length"),
              F ("freq", "BAND", 200, 8000, 1000, 1000, "Hz", "Centre of the noise band"),
              F ("width", "WIDTH", 0.5f, 6, 2, 0, "oct", "Width of the noise band"),
              F ("dip", "DIP", 0, 12, 0, 0, "dB", "Optional real dip of the sound under the burst (0 = pure illusion)"),
              F ("chance", "CHANCE", 0, 100, 100, 0, "%", "Probability of a burst on each step"),
              C ("rate", "RATE", divChoices(), 6, "Step length"),
              C ("track", "FOLLOW", "OFF|ON", 1, "Burst level follows the loudness of the sound") } };
        return m;
    }
    void prepare (double s, int) override { sr = s; reset(); }
    void reset() override { clock.reset(); age = 1 << 30; env.set (5, 80, sr); hist.fill (0.0f); lastF = -1; }
    void process (float* L, float* R, int n, const Ctx& ctx) override
    {
        const float lvl = dbToGain (p[0]), dip = dbToGain (-p[4]), chance = p[5] * 0.01f;
        const int len = std::max (16, (int) (p[1] * 0.001 * sr));
        const bool track = ci (7) == 1;
        if (p[2] != lastF || p[3] != lastW) { lastF = p[2]; lastW = p[3]; for (auto& b : band) b.set (p[2] / std::pow (2.0f, p[3] * 0.5f), p[2] * std::pow (2.0f, p[3] * 0.5f), sr); }
        float vz = 0;
        for (int i = 0; i < n; ++i)
        {
            long long idx;
            if (clock.tick (ctx, i, divBeats (ci (6)), idx))
            {
                const bool on = hashUni ((uint32_t) idx, 313u) < chance;
                if (on) age = 0;
                for (int k = 15; k > 0; --k) hist[(size_t) k] = hist[(size_t) k - 1];
                hist[0] = on ? 1.0f : 0.0f;
            }
            const float e = env.process (0.5f * (L[i] + R[i]));
            float w = 0;
            if (age < len) { w = sqr (std::sin (kPi * (float) age / (float) len)); ++age; }
            const float a = lvl * (track ? e * 2.5f : 0.08f) * w;
            const float g = 1.0f - (1.0f - dip) * w;
            L[i] = L[i] * g + band[0].process (nz[0].white()) * a * 2.0f;
            R[i] = R[i] * g + band[1].process (nz[1].white()) * a * 2.0f;
            vz = std::max (vz, w);
        }
        setViz (0, vz); setViz (17, p[2]);
        for (int k = 0; k < 16; ++k) setViz (1 + k, hist[(size_t) k]);
    }
private:
    StepClock clock;
    Env env;
    std::array<Band, 2> band;
    std::array<Noise, 2> nz;
    int age = 1 << 30;
    float lastF = -1, lastW = -1;
    std::array<float, 16> hist {};
};

//==============================================================================
// STREAM - auditory streaming. The sound alternates between two versions (A, B)
// in a galloping ABA- rhythm. Close and slow: one galloping stream. Far apart
// and fast: two separate streams, and the gallop disappears.
class Stream : public Module
{
public:
    const MInfo& info() const override
    {
        static const MInfo m { "stm", "STRM", "STREAM", "Auditory streaming (Bregman & Campbell 1971, van Noorden 1975)", pageTime, false,
            "One sound alternates between version A and version B in a fixed ABA- rhythm.",
            "Close and slow: one galloping line. Far apart and fast: two separate lines, no gallop.", nullptr,
            { F ("sep", "SEPARATION", 0, 24, 7, 0, "st", "Distance between A and B (pitch or band)"),
              F ("duty", "DUTY", 20, 100, 70, 0, "%", "Length of each tone within its step"),
              F ("fade", "FADE", 1, 20, 3, 0, "ms", "Tone edges"),
              F ("focus", "FOCUS", 200, 4000, 800, 800, "Hz", "BAND modes: centre of band A"),
              F ("q", "Q", 0.5f, 8, 2, 2, "", "BAND modes: band width"),
              F ("pan", "PAN", 0, 100, 80, 0, "%", "PAN modes: A left, B right"),
              F ("grain", "GRAIN", 20, 120, 40, 0, "ms", "PITCH mode: grain size"),
              C ("pattern", "PATTERN", "ABA-|ABAB|AAB-|ABBA", 0, "ABA- is the classic gallop"),
              C ("rate", "RATE", divChoices(), 2, "Step length (the tone repetition time)"),
              C ("mode", "SPLIT BY", "PITCH|BAND|PAN|BAND+PAN", 0, "What separates A from B") } };
        return m;
    }
    void prepare (double s, int) override { sr = s; for (auto& sh : shB) sh.prepare (sr, 130.0f); reset(); }
    void reset() override { for (auto& sh : shB) sh.reset(); clock.reset(); g = 0; tok = 2; last = {}; }
    void process (float* L, float* R, int n, const Ctx& ctx) override
    {
        static const char* pats[] = { "ABA-", "ABAB", "AAB-", "ABBA" };
        const char* pat = pats[std::min (3, ci (7))];
        const double div = divBeats (ci (8));
        const int mode = ci (9);
        const float sep = p[0], duty = p[1] * 0.01f, pan = p[5] * 0.01f;
        const float ratio = std::pow (2.0f, sep / 12.0f);
        for (auto& sh : shB) { sh.setWindow (p[6]); sh.setRatio (ratio); }
        const std::array<float, 3> now { p[3], p[4], sep };
        if (now != last)
        {
            last = now;
            for (int c = 0; c < 2; ++c)
            {
                bpA[(size_t) c].set (Biquad::BP, p[3], p[4], 0, sr);
                bpB[(size_t) c].set (Biquad::BP, p[3] * ratio, p[4], 0, sr);
            }
        }
        OnePole fade; fade.setMs (p[2] * 0.4f, sr);
        float pAl, pAr, pBl, pBr;
        panGains (-pan, pAl, pAr); panGains (pan, pBl, pBr);
        const float bandMk = std::sqrt (p[4]) * 1.2f;
        for (int i = 0; i < n; ++i)
        {
            long long idx;
            if (clock.tick (ctx, i, div, idx)) { const char t = pat[(size_t) (((idx % 4) + 4) % 4)]; tok = t == 'A' ? 0 : (t == 'B' ? 1 : 2); stepI = (int) (((idx % 4) + 4) % 4); }
            const float ph = (float) clock.phase (ctx, i, div);
            const float tgt = (tok < 2 && ph < duty) ? 1.0f : 0.0f;
            g = tgt + fade.a * (g - tgt);
            const float xl = L[i], xr = R[i];
            float yl, yr;
            const float bl = shB[0].process (xl), br = shB[1].process (xr);
            const float al2 = bpA[0].process (xl), ar2 = bpA[1].process (xr), bl2 = bpB[0].process (xl), br2 = bpB[1].process (xr);
            if (tok == 0)
            {
                if (mode == 0) { yl = xl; yr = xr; }
                else if (mode == 1) { yl = al2 * bandMk; yr = ar2 * bandMk; }
                else if (mode == 2) { const float m = 0.5f * (xl + xr); yl = m * pAl * 1.41f; yr = m * pAr * 1.41f; }
                else { const float m = 0.5f * (al2 + ar2) * bandMk; yl = m * pAl * 1.41f; yr = m * pAr * 1.41f; }
            }
            else
            {
                if (mode == 0) { yl = bl; yr = br; }
                else if (mode == 1) { yl = bl2 * bandMk; yr = br2 * bandMk; }
                else if (mode == 2) { const float m = 0.5f * (xl + xr); yl = m * pBl * 1.41f; yr = m * pBr * 1.41f; }
                else { const float m = 0.5f * (bl2 + br2) * bandMk; yl = m * pBl * 1.41f; yr = m * pBr * 1.41f; }
            }
            L[i] = yl * g; R[i] = yr * g;
        }
        setViz (0, (float) tok); setViz (1, (float) stepI); setViz (2, sep); setViz (3, (float) (div * 60000.0 / ctx.bpm));
        setViz (4, (float) mode); setViz (5, (float) ci (7)); setViz (6, g);
    }
private:
    std::array<Shifter, 2> shB;
    std::array<Biquad, 2> bpA {}, bpB {};
    StepClock clock;
    float g = 0;
    int tok = 2, stepI = 0;
    std::array<float, 3> last {};
};

ModulePtr makeRisset() { return std::make_unique<Risset>(); }
ModulePtr makeSpeechToSong() { return std::make_unique<SpeechToSong>(); }
ModulePtr makePhantomWords() { return std::make_unique<PhantomWords>(); }
ModulePtr makeContinuity() { return std::make_unique<Continuity>(); }
ModulePtr makeDiscontinuity() { return std::make_unique<Discontinuity>(); }
ModulePtr makeStream() { return std::make_unique<Stream>(); }
} // namespace il
