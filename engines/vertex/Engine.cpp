#include "Engine.h"
#include <cmath>

namespace vx
{
namespace
{
inline double polyBlep (double t, double dt)
{
    if (dt <= 0.0) return 0.0;
    if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}
inline double ftom (double hz) { return 69.0 + 12.0 * std::log2 (std::max (hz, 1.0e-6) / 440.0); }
inline double mtof (double m) { return 440.0 * std::exp2 ((m - 69.0) / 12.0); }
inline long long floorll (double x) { return (long long) std::floor (x); }
inline int posMod (long long a, int m) { const long long r = a % m; return (int) (r < 0 ? r + m : r); }

// swing: the second 16th of every pair is pushed late by `s` of a 16th
inline double swingWarp (double x16, double s)
{
    if (s <= 1.0e-6) return x16;
    const double p = std::floor (x16 * 0.5), u = x16 - 2.0 * p;
    const double w = u < 1.0 + s ? u / (1.0 + s) : 1.0 + (u - 1.0 - s) / (1.0 - s);
    return 2.0 * p + w;
}

struct ScaleDef { const char* name; int n; int iv[12]; };
const ScaleDef kScales[] = {
    { "MAJOR", 7, { 0, 2, 4, 5, 7, 9, 11 } }, { "MINOR", 7, { 0, 2, 3, 5, 7, 8, 10 } }, { "DORIAN", 7, { 0, 2, 3, 5, 7, 9, 10 } },
    { "PHRYGIAN", 7, { 0, 1, 3, 5, 7, 8, 10 } }, { "LYDIAN", 7, { 0, 2, 4, 6, 7, 9, 11 } }, { "MIXOLYD", 7, { 0, 2, 4, 5, 7, 9, 10 } },
    { "LOCRIAN", 7, { 0, 1, 3, 5, 6, 8, 10 } }, { "HARM MIN", 7, { 0, 2, 3, 5, 7, 8, 11 } }, { "MAJ PENTA", 5, { 0, 2, 4, 7, 9 } },
    { "MIN PENTA", 5, { 0, 3, 5, 7, 10 } }, { "BLUES", 6, { 0, 3, 5, 6, 7, 10 } }, { "WHOLE", 6, { 0, 2, 4, 6, 8, 10 } },
    { "HIJAZ", 7, { 0, 1, 4, 5, 7, 8, 10 } }, { "CHROMATIC", 12, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } } };
} // namespace

int Engine::numScales() { return (int) (sizeof (kScales) / sizeof (kScales[0])); }
const char* Engine::scaleName (int i) { return kScales[std::clamp (i, 0, numScales() - 1)].name; }

// quantizePitch: round to a MIDI note, then up to the next degree of the scale
// (dfam_scale_lookup.js); past the last degree goes to the next root.
double Engine::scaleQuantize (double hz, int root, int scale)
{
    const auto& sc = kScales[std::clamp (scale, 0, numScales() - 1)];
    const long long n = (long long) std::lround (ftom (hz));
    const long long rel = n - root;
    const long long oct = (long long) std::floor ((double) rel / 12.0);
    const int i = (int) (rel - oct * 12);
    int deg = 12;
    for (int k = 0; k < sc.n; ++k) if (sc.iv[k] >= i) { deg = sc.iv[k]; break; }
    return mtof ((double) (root + oct * 12 + deg));
}

void Engine::prepare (double s)
{
    sr = s > 0 ? s : 48000.0;
    filter.prepare (sr);
    reset();
}

void Engine::reset()
{
    for (auto& e : env) e = Env();
    for (auto& o : osc) o = Osc();
    filter.reset();
    step = { 0, 0, 0, 0 };
    lastMaster = -1; wasRunning = false; extCount = 0;
    pitchSlide = ampSlide = 0.0;
    for (auto& p : pk) p = 0.0;
}

double Engine::srcValue (int src) const
{
    switch (src)
    {
        case srcMod1:  return cur[seqMod1];
        case srcMod2:  return cur[seqMod2];
        case srcVel:   return cur[seqVel];
        case srcPitch: return cur[seqPitch];
        case srcMidi:  return extVel;
        default:       return 1.0;
    }
}

void Engine::trigger (const Params& p)
{
    // the mod sources of the decays and of the attack are snapshots taken at the trigger
    const double att = p.attack * srcValue (p.attackSrc) * 0.001 * sr;
    const double lv[3] = { srcValue (p.volumeSrc), 1.0, srcValue (p.fEnvSrc) };
    const double dc[3] = { p.aDecay * srcValue (p.aDecaySrc), p.pDecay * srcValue (p.pDecaySrc), p.fDecay * srcValue (p.fDecaySrc) };
    for (int i = 0; i < 3; ++i)
        if (lv[i] > env[i].value())   // retrigger only when the new level is higher (dfam_env's >~)
            env[i].trigger (lv[i], att, std::max (0.5, dc[i]) * 0.001 * sr, p.curve);
    triggers.fetch_add (1, std::memory_order_relaxed);
}

void Engine::process (const Params& p, const Transport& tr, const MidiEvt* ev, int nev, const float* in, float* out, int n)
{
    const bool ext = p.clock >= kNumRates;
    const double mm = ext ? 1.0 : rateMult (p.clock);
    double seqDiv[numSeq];
    for (int s = 0; s < numSeq; ++s) seqDiv[s] = mm * rateMult (p.rate[(size_t) s]);
    auto setSteps = [&] (double x16)
    {
        for (int s = 0; s < numSeq; ++s) step[(size_t) s] = posMod (floorll (x16 / seqDiv[s] + 1.0e-9), std::clamp (p.length[(size_t) s], 1, 8));
    };
    auto refreshCur = [&] { for (int s = 0; s < numSeq; ++s) cur[s] = p.step[(size_t) s][(size_t) step[(size_t) s]]; };
    refreshCur();

    filter.filterType = p.filterType + 1;
    filter.slope = p.slope;
    filter.order = p.order;
    filter.drive = std::clamp (p.drive, 0.0, 1.0);

    // knob position of each oscillator's frequency (the patch adds the pitch seq before ^4.5)
    double knobN[2];
    for (int o = 0; o < 2; ++o) knobN[o] = std::pow (std::clamp ((p.osc[o].freq - 5.0) / 6195.0, 0.0, 1.0), 1.0 / 4.5);
    const double glideSamples = p.glide * sr;   // x glide source below
    const double nyq = sr * 0.45;
    int ei = 0;
    double base[2] { 0, 0 };

    for (int k = 0; k < n; ++k)
    {
        // ---- MIDI ----
        bool extTrig = false;
        while (ei < nev && ev[ei].pos <= k)
        {
            const auto& e = ev[ei++];
            if (e.vel > 0.0f)
            {
                lastNote = e.note;
                extVel = e.vel;
                if (ext) extTrig = true;
            }
        }
        // ---- clock ----
        bool trig = false;
        if (! ext)
        {
            if (tr.running)
            {
                const double x16 = swingWarp ((tr.ppq + tr.ppqPerSample * k) * 4.0, p.swing);
                const long long m = floorll (x16 / mm + 1.0e-9);
                setSteps (x16);
                if (! wasRunning || m != lastMaster) trig = true;
                lastMaster = m;
            }
            wasRunning = tr.running;
        }
        else if (extTrig)
        {
            const double x16 = tr.hostPlaying ? (tr.ppq + tr.ppqPerSample * k) * 4.0 : (double) extCount++;
            setSteps (x16);
            trig = true;
        }
        if (trig || (k & 15) == 0) refreshCur();
        if (trig) trigger (p);

        // ---- envelopes ----
        const double eA = env[0].tick(), eP = env[1].tick(), eF = env[2].tick();
        ampSlide += (eA - ampSlide) / (eA > ampSlide ? 5.0 : 3.0);       // slide~ 5 3
        pitchSlide += (eP - pitchSlide) / (eP > pitchSlide ? 5.0 : 3.0);

        // ---- noise ----
        rng = rng * 1664525u + 1013904223u;
        white = (double) (int32_t) rng / 2147483648.0;
        pk[0] = 0.99886 * pk[0] + white * 0.0555179; pk[1] = 0.99332 * pk[1] + white * 0.0750759;
        pk[2] = 0.96900 * pk[2] + white * 0.1538520; pk[3] = 0.86650 * pk[3] + white * 0.3104856;
        pk[4] = 0.55000 * pk[4] + white * 0.5329522; pk[5] = -0.7616 * pk[5] - white * 0.0168980;
        pink = (pk[0] + pk[1] + pk[2] + pk[3] + pk[4] + pk[5] + pk[6] + white * 0.5362) * 0.11;
        pk[6] = white * 0.115926;
        const double noiseSig = p.noiseColour ? pink : white;
        const double xin = in != nullptr ? (double) in[k] : 0.0;

        // ---- oscillator frequencies (base every 8 samples: knob, pitch seq, MIDI, quantiser) ----
        if ((k & 7) == 0 || trig)
        {
            const double ps = (cur[seqPitch] * 2.0 - 1.0) * p.pitchRange;   // scale~ 0 1 -R R
            for (int o = 0; o < 2; ++o)
            {
                const auto& op = p.osc[o];
                const double nn = std::max (0.0, knobN[o] + (op.intPitch ? ps : 0.0));
                double f = 5.0 + 6195.0 * std::pow (nn, 4.5);
                if (op.extPitch) f = mtof (ftom (f) + (double) (lastNote - 60));
                if (op.fine != 0.0) f *= std::exp2 (op.fine / 12.0);
                if (op.quantize) f = scaleQuantize (f, p.root, p.scale);
                base[o] = std::min (f, nyq);
            }
        }
        double outs[2] = { 0, 0 };
        for (int o = 0; o < 2; ++o)
        {
            auto& S = osc[o];
            const auto& op = p.osc[o];
            // rampsmooth~ glide (linear over the glide time)
            const double gs = op.glide ? glideSamples * srcValue (p.glideSrc) : 0.0;
            if (gs < 1.0 || S.glideTarget < 0.0) { S.freqSm = base[o]; S.glideTarget = base[o]; S.glideLeft = 0; }
            else
            {
                if (base[o] != S.glideTarget) { S.glideTarget = base[o]; S.glideLeft = (int) gs; S.glideInc = (S.glideTarget - S.freqSm) / std::max (1, S.glideLeft); }
                if (S.glideLeft > 0) { S.freqSm += S.glideInc; if (--S.glideLeft == 0) S.freqSm = S.glideTarget; }
            }
            double f = std::max (0.0, S.freqSm + pitchSlide * op.env * srcValue (op.envSrc));
            if (o == 1) f += p.fm * srcValue (p.fmSrc) * (p.fmFrom == 0 ? osc[0].triOut : xin);
            f = std::clamp (f, -nyq, nyq);
            const double dt = std::abs (f) / sr;
            if (o == 0) freqNow[0].store ((float) f, std::memory_order_relaxed); else freqNow[1].store ((float) f, std::memory_order_relaxed);

            // sync input (osc 2 only): the triangle of osc 1
            double peak = 0.5;
            bool reset = false;
            if (o == 1 && p.sync)
            {
                const double sIn = osc[0].triOut;
                if (p.syncMode == 0)
                {
                    peak = std::clamp (sIn, 0.0, 1.0);
                    reset = S.prevSync <= 0.0 && sIn > 0.0 && op.shape == shSqu;   // rect~ sync inlet
                }
                else reset = osc[0].ph < S.prevSync;   // hard sync on osc 1's wrap
                S.prevSync = p.syncMode == 0 ? sIn : osc[0].ph;
            }
            if (reset) S.ph = 0.0;
            const double t = S.ph;
            // triangle~ with peak position
            double tri;
            if (peak <= 1.0e-6) tri = 1.0 - 2.0 * t;
            else if (peak >= 1.0 - 1.0e-6) tri = -1.0 + 2.0 * t;
            else tri = t < peak ? -1.0 + 2.0 * t / peak : 1.0 - 2.0 * (t - peak) / (1.0 - peak);
            S.triOut = tri;
            double y;
            switch (op.shape)
            {
                case shTri: y = tri; break;
                case shSqu: { y = t < 0.5 ? 1.0 : -1.0; y += polyBlep (t, dt); y -= polyBlep (std::fmod (t + 0.5, 1.0), dt); break; }
                case shSin: y = std::cos (2.0 * kPiD * t); break;    // cycle~
                default:    y = 2.0 * t - 1.0 - polyBlep (t, dt); break;   // saw~
            }
            S.ph += f / sr;
            S.ph -= std::floor (S.ph);
            outs[o] = y * op.level * srcValue (op.levelSrc);
        }

        // ---- mix, amp, filter, volume ----
        extSm += (p.extLevel - extSm) * 0.1;            // slide~ 10 10
        double mix = noiseSig * p.noise * srcValue (p.noiseSrc) + outs[0] + xin * extSm + outs[1];
        mix *= ampSlide;
        const double modSig = p.fModFrom == 0 ? noiseSig : (p.fModFrom == 1 ? osc[0].triOut : (p.fModFrom == 2 ? osc[1].triOut : xin));
        const double cp = std::min (140.0, p.cutoff * srcValue (p.cutoffSrc) + eF * p.fEnv + modSig * p.fMod * srcValue (p.fModSrc));
        double y = filter.tick (mix, cp, p.res * srcValue (p.resSrc));
        volSm += (p.volume * 1.0 - volSm) * 0.1;
        y *= volSm;
        if (! std::isfinite (y)) { filter.reset(); y = 0.0; }
        out[k] = (float) y;

        if ((k & 3) == 0)
        {
            const int at = scopeAt.load (std::memory_order_relaxed);
            scope[(size_t) at].store ((float) y, std::memory_order_relaxed);
            scopeAt.store ((at + 1) % kScope, std::memory_order_relaxed);
        }
        if (k == n - 1)
        {
            envAmp.store ((float) eA, std::memory_order_relaxed); envPitch.store ((float) eP, std::memory_order_relaxed);
            envFilt.store ((float) eF, std::memory_order_relaxed); cutoffNow.store ((float) cp, std::memory_order_relaxed);
        }
    }
    for (int s = 0; s < numSeq; ++s) curStep[(size_t) s].store (step[(size_t) s], std::memory_order_relaxed);
    for (int s = 0; s < numSrc; ++s) srcNow[s].store ((float) srcValue (s), std::memory_order_relaxed);
}
} // namespace vx
