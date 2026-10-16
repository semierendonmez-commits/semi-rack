// PERCEPT page: Zwicker tone, Yanny / Laurel, auditory masking, equal-loudness
// contours. (Phantom words is in ModTime.cpp.)
#include "Spec.h"
#include "Iso226.h"

namespace il
{
//==============================================================================
// ZWICKER - noise with a spectral notch, switched off: for a few seconds
// afterwards a faint tone is heard at the frequency of the notch (a negative
// auditory afterimage).
class Zwicker : public Module
{
public:
    const MInfo& info() const override
    {
        static const MInfo m { "zwi", "ZWICK", "ZWICKER", "Zwicker tone, auditory afterimage (Zwicker 1964)", pagePercept, false,
            "Broadband noise with a gap (notch) in its spectrum, switched on and off on the beat.",
            "When the noise stops, a faint ghost tone at the notch hangs on for a few seconds.", nullptr,
            { F ("notch", "NOTCH", 500, 8000, 2000, 2000, "Hz", "Where the spectral gap is (the ghost tone's pitch)"),
              F ("width", "WIDTH", 0.25f, 2, 0.5f, 0, "oct", "Width of the gap"),
              F ("noise", "NOISE", -48, 0, -12, 0, "dB", "Level of the notched noise"),
              F ("ontime", "NOISE ON", 0.5f, 16, 4, 0, "beats", "How long the noise plays"),
              F ("offtime", "SILENCE", 0.5f, 16, 4, 0, "beats", "Silence after it (where the afterimage lives)"),
              F ("ghost", "HELPER", 0, 100, 0, 0, "%", "A real, very quiet decaying tone in the silence to show where to listen (0 = pure illusion)"),
              F ("carve", "CARVE IN", 0, 100, 100, 0, "%", "Also cut the same notch into your signal"),
              C ("color", "NOISE", "WHITE|PINK", 0, "Noise colour") } };
        return m;
    }
    bool generates() const override { return true; }
    void prepare (double s, int) override { sr = s; reset(); }
    void reset() override { for (auto& c : ch) for (auto& b : c.bp) b.reset(); g = 0; off = 0; ph = 0; lastF = -1; }
    void process (float* L, float* R, int n, const Ctx& ctx) override
    {
        const float f = p[0], w = p[1], lvl = dbToGain (p[2]), ghost = p[5] * 0.01f, carve = p[6] * 0.01f;
        const bool pink = ci (7) == 1;
        if (f != lastF || w != lastW)
        {
            lastF = f; lastW = w;
            const float q = 1.0f / (std::pow (2.0f, w * 0.5f) - std::pow (2.0f, -w * 0.5f));
            for (auto& c : ch) for (auto& b : c.bp) b.set (Biquad::BP, f, q, 0, sr);
        }
        const double cyc = std::max (0.5f, p[3]) + std::max (0.5f, p[4]);
        OnePole fade; fade.setMs (8.0f, sr);
        float* io[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            const double beat = ctx.ppq + (double) i * ctx.beatsPerSample();
            const bool onNow = std::fmod (beat, cyc) < p[3];
            const float t = onNow ? 1.0f : 0.0f;
            g = t + fade.a * (g - t);
            off = onNow ? 0.0 : off + 1.0 / sr;
            ph += f / sr; ph -= std::floor (ph);
            const float gh = onNow ? 0.0f : ghost * 0.03f * lvl * (float) std::exp (-off / 1.4) * (float) std::sin (6.283185307179586 * ph);
            for (int c = 0; c < 2; ++c)
            {
                auto& s = ch[(size_t) c];
                const float nz = s.nz.get (pink) * lvl * g;
                float y1 = nz - s.bp[0].process (nz);
                y1 = y1 - s.bp[1].process (y1);
                float x = io[c][i];
                float x1 = x - s.bp[2].process (x);
                x1 = x1 - s.bp[3].process (x1);
                io[c][i] = lerpf (x, x1, carve) + y1 + gh;
            }
        }
        setViz (0, g); setViz (1, (float) off); setViz (2, f); setViz (3, w); setViz (4, (float) std::fmod (ctx.ppq, cyc) / (float) cyc); setViz (5, p[3] / (float) cyc);
    }
private:
    struct Ch { std::array<Biquad, 4> bp {}; Noise nz; };
    std::array<Ch, 2> ch;
    float g = 0, lastF = -1, lastW = -1;
    double off = 0, ph = 0;
};

//==============================================================================
// YANNY - the same recording is heard as two different words depending on
// which part of the spectrum dominates. A tilt around a pivot moves the
// balance; FLIP switches back and forth.
class Yanny : public Module
{
public:
    const MInfo& info() const override
    {
        static const MInfo m { "yan", "YANNY", "YANNY/LAUREL", "Yanny or Laurel (2018): spectral weighting of an ambiguous word", pagePercept, false,
            "A tilt around a pivot: the high band (YANNY) or the low band (LAUREL) is made to dominate.",
            "The same voice says a different word as the balance moves.", nullptr,
            { F ("morph", "MORPH", -100, 100, 0, 0, "%", "-100 = LAUREL (low weight), +100 = YANNY (high weight)"),
              F ("pivot", "PIVOT", 500, 4000, 1500, 1500, "Hz", "Crossover between the two readings"),
              F ("depth", "DEPTH", 0, 24, 12, 0, "dB", "Maximum tilt"),
              F ("focus", "FOCUS", 1, 8, 8, 0, "oct", "Band-limit around the pivot (8 = off)"),
              F ("flip", "FLIP", 0, 4, 0, 0, "Hz", "Switch automatically between the two readings"),
              F ("smooth", "SMOOTH", 0, 100, 50, 0, "%", "Softness of the flips") } };
        return m;
    }
    void prepare (double s, int) override { sr = s; reset(); }
    void reset() override { for (auto& c : ch) c = {}; cur = 0; ph = 0; last = {}; }
    void process (float* L, float* R, int n, const Ctx&) override
    {
        const float depth = p[2], pivot = p[1], focus = p[3];
        float* io[2] = { L, R };
        for (int i0 = 0; i0 < n; i0 += 32)
        {
            const int m = std::min (32, n - i0);
            float target = p[0] * 0.01f;
            if (p[4] > 0.001f)
            {
                ph += p[4] * m / sr; ph -= std::floor (ph);
                const float amp = std::abs (target) > 0.05f ? std::abs (target) : 1.0f;
                const float sw = std::sin (kTwoPi * (float) ph);
                const float k = 1.0f + (1.0f - p[5] * 0.01f) * 20.0f;
                target = amp * clampf (sw * k, -1.0f, 1.0f);
            }
            cur += 0.25f * (target - cur);
            const std::array<float, 4> now { std::round (cur * 200.0f) / 200.0f, pivot, depth, focus };
            if (now != last)
            {
                last = now;
                for (auto& c : ch)
                {
                    c.lo.set (Biquad::LowShelf, pivot * 0.7f, 0.6, -now[0] * depth, sr);
                    c.hi.set (Biquad::HighShelf, pivot * 1.4f, 0.6, now[0] * depth, sr);
                    if (focus < 7.95f) c.band.set (pivot / std::pow (2.0f, focus * 0.5f), pivot * std::pow (2.0f, focus * 0.5f), sr);
                }
            }
            for (int c = 0; c < 2; ++c)
            {
                auto& s = ch[(size_t) c];
                for (int i = i0; i < i0 + m; ++i)
                {
                    float y = s.hi.process (s.lo.process (io[c][i]));
                    if (focus < 7.95f) y = s.band.process (y);
                    io[c][i] = y * dbToGain (-std::abs (cur) * depth * 0.35f);
                }
            }
        }
        setViz (0, cur); setViz (1, pivot); setViz (2, depth); setViz (3, focus);
    }
private:
    struct Ch { Biquad lo, hi; Band band; };
    std::array<Ch, 2> ch;
    float cur = 0;
    double ph = 0;
    std::array<float, 4> last {};
};

//==============================================================================
// MASK - simultaneous and forward masking, as in a perceptual codec. A
// spreading function over the Bark scale and a decay over time give a masking
// threshold; CODEC throws away everything under it (what MP3 does), INVERT
// keeps only that (the part you were never hearing).
class Mask : public Module
{
public:
    static constexpr int N = 1024, H = 256;
    const MInfo& info() const override
    {
        static const MInfo m { "msk", "MASK", "MASK", "Auditory masking, the psychoacoustic codec (Zwicker, Fletcher; MPEG-1 1992)", pagePercept, false,
            "Every spectral component under the masking threshold of its louder neighbours is removed (or kept alone).",
            "CODEC: the sound barely changes although much of it is gone. INVERT: the hidden layer you never heard.", nullptr,
            { F ("offset", "THRESHOLD", -30, 30, 0, 0, "dB", "Moves the masking threshold: up = more is thrown away"),
              F ("spread", "SPREAD", 5, 40, 12, 0, "dB/Bk", "Upward spread of masking (lower = a wider mask)"),
              F ("post", "POST-MASK", 0, 200, 50, 0, "ms", "Forward (temporal) masking after a loud sound"),
              F ("quant", "STARVE", 0, 100, 0, 0, "%", "Bit starving: coarse requantisation of what is kept"),
              F ("reveal", "REVEAL", 0, 30, 18, 0, "dB", "INVERT: gain on the masked layer"),
              F ("floor", "SPL @ 0dBFS", 70, 120, 96, 0, "dB", "Calibration for the threshold in quiet"),
              C ("mode", "MODE", "CODEC|INVERT", 0, "CODEC: remove what is masked. INVERT: keep only what is masked") } };
        return m;
    }
    int latency() const override { return N; }
    void prepare (double s, int) override
    {
        sr = s;
        fft.prepare (N);
        win.resize (N);
        for (int i = 0; i < N; ++i) win[(size_t) i] = std::sqrt (0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) N));
        for (int k = 0; k <= N / 2; ++k)
        {
            const float f = (float) k * (float) sr / (float) N;
            bark[(size_t) k] = std::min (24, (int) (13.0f * std::atan (0.00076f * f) + 3.5f * std::atan (sqr (f / 7500.0f))));
            const float fk = std::max (20.0f, f) / 1000.0f;
            ath[(size_t) k] = 3.64f * std::pow (fk, -0.8f) - 6.5f * std::exp (-0.6f * sqr (fk - 3.3f)) + 1.0e-3f * fk * fk * fk * fk;
        }
        binsIn.fill (0);
        for (int k = 1; k <= N / 2; ++k) ++binsIn[(size_t) bark[(size_t) k]];
        reset();
    }
    void reset() override
    {
        for (auto& c : ch) { c.in.assign (N, 0.0f); c.out.assign (N, 0.0f); }
        w = 0; hop = 0; prevT.fill (0.0f);
    }
    void process (float* L, float* R, int n, const Ctx&) override
    {
        for (int i = 0; i < n; ++i)
        {
            ch[0].in[(size_t) w] = L[i]; ch[1].in[(size_t) w] = R[i];
            L[i] = ch[0].out[(size_t) w]; R[i] = ch[1].out[(size_t) w];
            ch[0].out[(size_t) w] = 0; ch[1].out[(size_t) w] = 0;
            w = (w + 1) % N;
            if (++hop >= H) { hop = 0; frame(); }
        }
    }
private:
    void frame()
    {
        const float offset = p[0], upper = p[1], quant = p[3] * 0.01f, reveal = dbToGain (p[4]), floorSpl = p[5];
        const bool invert = ci (6) == 1;
        const float postA = p[2] <= 0.5f ? 0.0f : (float) std::exp (-(double) H / sr / (p[2] * 0.001));
        for (int c = 0; c < 2; ++c)
            for (int j = 0; j < N; ++j) bins[(size_t) c][(size_t) j] = { ch[(size_t) c].in[(size_t) ((w + j) % N)] * win[(size_t) j], 0.0f };
        fft.forward (bins[0].data()); fft.forward (bins[1].data());
        // band energies (power of the mid), N^2 normalisation to full-scale sine = 0 dB
        const float norm = 4.0f / ((float) N * (float) N) * 2.0f;
        std::array<float, 25> E {};
        for (int k = 1; k <= N / 2; ++k)
        {
            pw[(size_t) k] = 0.5f * (std::norm (bins[0][(size_t) k]) + std::norm (bins[1][(size_t) k])) * norm;
            E[(size_t) bark[(size_t) k]] += pw[(size_t) k];
        }
        // spreading: 27 dB/Bark downwards, SPREAD dB/Bark upwards; offset 10 + 0.5 b dB (between tonal and noise maskers)
        std::array<float, 25> T {};
        for (int b = 0; b < 25; ++b)
        {
            float s = 0;
            for (int j = 0; j < 25; ++j)
            {
                if (E[(size_t) j] <= 0) continue;
                const int d = b - j;
                const float att = d >= 0 ? upper * (float) d : 27.0f * (float) -d;
                s += E[(size_t) j] * std::pow (10.0f, -att / 10.0f);
            }
            float t = s * std::pow (10.0f, -(10.0f + 0.5f * (float) b - offset) / 10.0f);
            t = std::max (t, prevT[(size_t) b] * postA);           // forward masking
            prevT[(size_t) b] = t;
            T[(size_t) b] = t;
        }
        std::array<float, 48> dSig {}, dThr {}, dCut {}, dCnt {};
        for (int k = 1; k <= N / 2; ++k)
        {
            const int b = bark[(size_t) k];
            const float thrBin = std::max (T[(size_t) b] / (float) std::max (1, binsIn[(size_t) b]), std::pow (10.0f, (ath[(size_t) k] - floorSpl) / 10.0f));
            const bool masked = pw[(size_t) k] < thrBin;
            float g = invert ? (masked ? reveal : 0.0f) : (masked ? 0.0f : 1.0f);
            if (! invert && ! masked && quant > 0.0f)
            {
                // requantise the magnitude in steps relative to the threshold (coarser = fewer bits)
                const float snr = 10.0f * std::log10 (pw[(size_t) k] / thrBin + 1e-12f);
                const float step = 1.0f + quant * 18.0f;
                const float q = std::floor (snr / step) * step;
                g *= std::pow (10.0f, (q - snr) / 20.0f);
            }
            for (int c = 0; c < 2; ++c) { bins[(size_t) c][(size_t) k] *= g; if (k < N / 2) bins[(size_t) c][(size_t) (N - k)] *= g; }
            // display bands: 48 log bands 30 Hz .. 20 kHz
            const float f = (float) k * (float) sr / (float) N;
            if (f >= 30.0f && f < 20000.0f)
            {
                const int di = std::min (47, (int) (48.0f * std::log (f / 30.0f) / std::log (20000.0f / 30.0f)));
                dSig[(size_t) di] += pw[(size_t) k]; dThr[(size_t) di] += thrBin; dCut[(size_t) di] += masked ? 1.0f : 0.0f; dCnt[(size_t) di] += 1.0f;
            }
        }
        bins[0][0] *= invert ? 0.0f : 1.0f; bins[1][0] *= invert ? 0.0f : 1.0f;
        if (invert)
        {
            // the revealed layer never gets louder than the input it came from
            double eIn = 0, eOut = 0;
            for (int k = 1; k <= N / 2; ++k) { eIn += pw[(size_t) k]; eOut += 0.5 * (std::norm (bins[0][(size_t) k]) + std::norm (bins[1][(size_t) k])) * norm; }
            if (eOut > eIn && eOut > 0)
            {
                const float s = (float) std::sqrt (eIn / eOut);
                for (auto& b : bins) for (auto& v : b) v *= s;
            }
        }
        fft.inverse (bins[0].data()); fft.inverse (bins[1].data());
        for (int c = 0; c < 2; ++c)
            for (int j = 0; j < N; ++j) ch[(size_t) c].out[(size_t) ((w + j) % N)] += bins[(size_t) c][(size_t) j].real() * win[(size_t) j] * 0.5f;
        float cut = 0, cnt = 0;
        for (int d = 0; d < 48; ++d)
        {
            if (dCnt[(size_t) d] > 0)
            {
                setArr (d, 10.0f * std::log10 (dSig[(size_t) d] + 1e-14f));
                setArr (48 + d, 10.0f * std::log10 (dThr[(size_t) d] + 1e-14f));
                setArr (96 + d, dCut[(size_t) d] / dCnt[(size_t) d]);
                cut += dCut[(size_t) d]; cnt += dCnt[(size_t) d];
            }
            else { setArr (d, -200.0f); setArr (48 + d, -200.0f); setArr (96 + d, -1.0f); }
        }
        setViz (0, cnt > 0 ? cut / cnt : 0.0f); setViz (1, invert ? 1.0f : 0.0f); setViz (2, offset);
    }
    struct Ch { std::vector<float> in, out; };
    std::array<Ch, 2> ch;
    Fft fft;
    std::vector<float> win;
    std::array<std::array<std::complex<float>, N>, 2> bins {};
    std::array<float, N / 2 + 1> pw {}, ath {};
    std::array<int, N / 2 + 1> bark {};
    std::array<int, 25> binsIn {};
    std::array<float, 25> prevT {};
    int w = 0, hop = 0;
};

//==============================================================================
// LOUDNESS - the ear is much less sensitive to lows and highs at low levels
// (equal-loudness contours). This EQ adds what the ear loses at the listening
// level relative to the mixing level, from the ISO 226:2003 contours.
class Loudness : public Module
{
public:
    const MInfo& info() const override
    {
        static const MInfo m { "lou", "LOUD", "LOUDNESS", "Equal-loudness contours (Fletcher & Munson 1933, ISO 226:2003)", pagePercept, false,
            "A 10-band EQ that adds the difference between two equal-loudness contours.",
            "Played quietly, the mix keeps the bass and air it has when played loud.", nullptr,
            { F ("listen", "LISTEN", 20, 90, 50, 0, "phon", "Level you listen at (STATIC)"),
              F ("ref", "REFERENCE", 60, 100, 83, 0, "phon", "Level the mix was balanced at"),
              F ("amount", "AMOUNT", 0, 150, 100, 0, "%", "Strength of the compensation (negative contour above 100 %)"),
              F ("calib", "CALIBRATE", 60, 100, 83, 0, "dB", "DYNAMIC: SPL of a -18 dBFS RMS signal on your speakers"),
              F ("speed", "SPEED", 50, 3000, 600, 600, "ms", "DYNAMIC: how fast the level is followed"),
              C ("mode", "MODE", "STATIC|DYNAMIC", 0, "STATIC: fixed listening level. DYNAMIC: follows the playback level") } };
        return m;
    }
    void prepare (double s, int) override { sr = s; reset(); }
    void reset() override { for (auto& c : eq) for (auto& b : c) b.reset(); lastG.fill (999.0f); ms = 0; level = 50; }
    void process (float* L, float* R, int n, const Ctx&) override
    {
        static const float fb[10] = { 31.5f, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
        const bool dyn = ci (5) == 1;
        const float ref = p[1], amt = p[2] * 0.01f;
        if (dyn)
        {
            const float a = (float) std::exp (-1.0 / (0.001 * p[4] * sr));
            for (int i = 0; i < n; ++i) { const float x = 0.5f * (L[i] + R[i]); ms = x * x + a * (ms - x * x); }
            level = clampf (p[3] + (10.0f * std::log10 (ms * 2.0f + 1e-12f) + 18.0f), 20.0f, ref);
        }
        else level = std::min (p[0], 100.0f);
        for (int b = 0; b < 10; ++b)
        {
            const float f = std::min (fb[b], 12500.0f);
            float g = (iso226Spl (level, f) - iso226Spl (ref, f)) - (level - ref);
            g = clampf (g * amt, -12.0f, 24.0f);
            if (std::abs (g - lastG[(size_t) b]) > 0.05f)
            {
                lastG[(size_t) b] = g;
                for (auto& c : eq) c[(size_t) b].set (Biquad::Peak, fb[b], b == 0 || b == 9 ? 0.8 : 1.1, g, sr);
            }
            setViz (2 + b, g);
        }
        float* io[2] = { L, R };
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                float y = io[c][i];
                for (auto& b : eq[(size_t) c]) y = b.process (y);
                io[c][i] = y;
            }
        setViz (0, level); setViz (1, ref);
    }
private:
    std::array<std::array<Biquad, 10>, 2> eq {};
    std::array<float, 10> lastG {};
    float ms = 0, level = 50;
};

ModulePtr makeZwicker() { return std::make_unique<Zwicker>(); }
ModulePtr makeYanny() { return std::make_unique<Yanny>(); }
ModulePtr makeMask() { return std::make_unique<Mask>(); }
ModulePtr makeLoudness() { return std::make_unique<Loudness>(); }
} // namespace il
