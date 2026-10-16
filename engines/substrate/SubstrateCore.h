// SEMI Family port: vendored from semierendonmez-commits/substrate Source/engine (12c7624). Only change:
// namespace substrate -> sbs::substrate (SPLICEBUS carries its own copy of this header).
// SUBSTRATE — low-end engine, stage 1 ("Bass mode")
// Framework-independent DSP core. Header-only, no allocation in process().
//
// Pipeline (all times in input samples, t = newest input sample):
//
//   input t ──► [edit detector] ──► ring X/M (stereo + mono)
//          └──► [decimate ÷D] ──► [YIN f0 tracker]  → f0 estimate for time t - Ltrack
//
//   stage C  (c = t - Ltrack): analysis oscillator φa(c) driven by f0(c),
//            complex baseband b(c) = m(c)·e^{-jφa(c)}, prefix sum S(c)
//   stage O  (n = c - H): one-period moving average of baseband, centred on n
//            → complex amplitude C(n) of the input's fundamental.
//            A one-period boxcar has nulls at every multiple of f0, so the
//            leakage from all harmonics is cancelled exactly for periodic input.
//            fund(n)  = 2·Re{C·e^{jφa(n)}}      (input fundamental, coherent)
//            r = x - fund;  low = sub + mono(LP4_{k·f0}(r));  upper = r - LP4_{k·f0}(r)
//            sub(n)   = 2|C| · cos(φs(n))         (mono resynthesis, phase-continuous)
//            φs follows the input phase through a slow PLL that freezes at edits.
//
//   Edit repair: a cut (sample discontinuity that is not part of the periodic
//   waveform) is detected at input time, and before it reaches stage C the
//   following N samples are replaced by a raised-cosine crossfade from a Burg
//   LPC extrapolation of the material before the cut into the material after it.
//
//   Total latency = Ltrack + H  (≈ 60 ms @ 48 kHz with f0min = 30 Hz).
#pragma once
#include <cmath>
#include <complex>
#include <vector>
#include <array>
#include <algorithm>
#include <cstdint>

namespace sbs::substrate {

static constexpr double kPi = 3.14159265358979323846;
static constexpr double kTwoPi = 2.0 * kPi;

inline double wrapPi (double x) { x = std::fmod (x + kPi, kTwoPi); if (x < 0) x += kTwoPi; return x - kPi; }

//==============================================================================
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void setLowpass (double fs, double fc, double q)
    {
        const double w = kTwoPi * fc / fs, c = std::cos (w), s = std::sin (w), al = s / (2 * q);
        const double a0 = 1 + al;
        b0 = (1 - c) * 0.5 / a0; b1 = (1 - c) / a0; b2 = b0;
        a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    double process (double x) { double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
};

// Zavalishin TPT state-variable filter: stable and click-free under per-sample cutoff modulation.
struct TptSvfLP
{
    double s1 = 0, s2 = 0;
    double process (double x, double g, double k)
    {
        const double hp = (x - (k + g) * s1 - s2) / (1 + g * (g + k));
        const double v1 = g * hp, bp = v1 + s1; s1 = bp + v1;
        const double v2 = g * bp, lp = v2 + s2; s2 = lp + v2;
        return lp;
    }
    void reset() { s1 = s2 = 0; }
};

//==============================================================================
// Burg LPC: a[0]=1, prediction x̂[n] = -Σ_{k=1..p} a[k]·x[n-k]
// Allocation-free: n <= 1024, order <= 32.
inline void burgLpc (const double* x, int n, int order, double* a /* order+1 */)
{
    std::array<double, 1024> fw {}, bw {};
    std::array<double, 33> aa {}, tmp {};
    n = std::min (n, 1024); order = std::min (order, 32);
    for (int i = 0; i < n; ++i) fw[(size_t) i] = bw[(size_t) i] = x[i];
    aa[0] = 1;
    for (int m = 1; m <= order; ++m)
    {
        double num = 0, den = 0;
        for (int i = m; i < n; ++i) { num += fw[(size_t) i] * bw[(size_t) i - 1]; den += fw[(size_t) i] * fw[(size_t) i] + bw[(size_t) i - 1] * bw[(size_t) i - 1]; }
        const double k = den > 1e-30 ? -2.0 * num / den : 0.0;
        for (int i = 0; i <= m; ++i) tmp[(size_t) i] = aa[(size_t) i] + k * aa[(size_t) (m - i)];
        for (int i = 0; i <= m; ++i) aa[(size_t) i] = tmp[(size_t) i];
        for (int i = n - 1; i >= m; --i)
        {
            const double fo = fw[(size_t) i];
            fw[(size_t) i] = fo + k * bw[(size_t) i - 1];
            bw[(size_t) i] = bw[(size_t) i - 1] + k * fo;
        }
    }
    for (int i = 0; i <= order; ++i) a[i] = aa[(size_t) i];
}

//==============================================================================
struct Params
{
    // tracking
    bool   midiMode      = false;  // f0 from MIDI (setMidiF0) instead of the audio tracker
    double f0Max         = 220.0;  // Hz  (f0Min is fixed at construction → fixed latency)
    double yinThreshold  = 0.25;
    // crossover
    double splitRatio    = 1.5;    // mono split = splitRatio · f0   (0.5..2)
    double residualDb    = 0.0;    // non-fundamental lows below the split (mono). <= -60 → removed
    // sub
    double subGainDb     = 0.0;
    double subPhaseDeg   = 0.0;    // offset relative to source fundamental
    int    phaseMode     = 2;      // 0 Free, 1 Follow, 2 Auto (follow, freeze at edits)
    double pllHz         = 10.0;
    double stabilize     = 0.0;    // 0..1: hold sub level through reese/detune beating dips (release 3 ms → 2 s, gated by broadband level), slows phase follow
    // high band
    double highGainDb    = 0.0;
    double highWidth     = 1.0;    // 0 mono .. 2 wide
    // edit repair
    bool   repairOn      = true;
    double sensitivity   = 0.5;    // 0..1
    double fadeMs        = 2.0;
    // output
    int    monitor       = 0;      // 0 mix, 1 low (sub + mono residual), 2 upper, 3 delta
    double outGainDb     = 0.0;
};

struct Meters
{
    double f0 = 0, confidence = 0, subLevel = 0;
    uint64_t editsRepaired = 0;
};

//==============================================================================
class Engine
{
public:
    void prepare (double sampleRate, double f0Min = 30.0)
    {
        fs = sampleRate;
        f0MinFixed = f0Min;
        dec = std::max (1, (int) std::lround (fs / 6000.0));
        fsd = fs / dec;

        // tracker geometry (decimated domain)
        tauMax = (int) std::ceil (fsd / f0MinFixed) + 1;
        W = tauMax;
        dRingSize = 1; while (dRingSize < 2 * (W + tauMax + 4)) dRingSize <<= 1;
        dRing.assign ((size_t) dRingSize, 0.0);
        diff.assign ((size_t) tauMax + 2, 0.0);
        hopD = 16;

        // anti-alias / tracking lowpass (4th-order Butterworth @ 400 Hz)
        aa1.setLowpass (fs, 400.0, 0.5411961); aa2.setLowpass (fs, 400.0, 1.3065630);

        const double filterDelay = 2.613 / (kTwoPi * 400.0) * fs;
        Ltrack = (int) std::lround (dec * (W + tauMax) * 0.5 + filterDelay) + dec * hopD;
        Pmax = fs / f0MinFixed;
        H = (int) std::ceil (Pmax * 0.5) + 4;
        latency = Ltrack + H;

        ringSize = 1; while (ringSize < latency + 4096) ringSize <<= 1;
        mask = ringSize - 1;
        for (auto& r : X) r.assign ((size_t) ringSize, 0.0);
        M.assign ((size_t) ringSize, 0.0);
        Mraw.assign ((size_t) ringSize, 0.0);
        F0trk.assign ((size_t) ringSize, 100.0);
        Conf.assign ((size_t) ringSize, 0.0);
        F0c.assign ((size_t) ringSize, 100.0);
        PhiA.assign ((size_t) ringSize, 0.0);
        S.assign ((size_t) ringSize, {0.0, 0.0});
        EditFlag.assign ((size_t) ringSize, 0);
        MidiF0.assign ((size_t) ringSize, 0.0);

        reset();
    }

    void reset()
    {
        t = 0; decCount = 0; dWrite = 0; hopCount = 0;
        std::fill (dRing.begin(), dRing.end(), 0.0);
        for (auto& r : X) std::fill (r.begin(), r.end(), 0.0);
        std::fill (M.begin(), M.end(), 0.0);
        std::fill (Mraw.begin(), Mraw.end(), 0.0);
        std::fill (S.begin(), S.end(), std::complex<double> (0, 0));
        std::fill (EditFlag.begin(), EditFlag.end(), (uint8_t) 0);
        std::fill (MidiF0.begin(), MidiF0.end(), 0.0);
        aa1.reset(); aa2.reset();
        for (auto& c : lpf) for (auto& f : c) f.reset();
        trackF0 = 100.0; trackConf = 0; pendingF0 = 0; pendingCount = 0; confBase = 0; stableCount = 0; holdHops = 0; inTransition = false; everLocked = false; octaveHold = 0;
        histF0 = { 100, 100, 100 };
        logF0c = std::log (100.0); phiA = 0; phiS = 0;
        lpScale = 1e-4; rmsM = 1e-4; refractory = 0; repairQueueN = 0;
        freeze = 0; ampSm = 0; midiF0Now = 0; envFast = 0; envHold = 0; envMs = 0;
        meters = {};
    }

    int getLatency() const { return latency; }
    void setParams (const Params& p) { prm = p; }
    const Meters& getMeters() const { return meters; }
    void setMidiF0 (double hz) { midiF0Now = hz; } // 0 = note off (hold last)

    // Optional per-sample taps for the editor (valid for the next process() call only).
    // inLow: the input's low band (fundamental + residual below the split), sub: the
    // resynthesised sub (with level), low / up: the two output bands, all mono.
    struct Taps { float* inLow = nullptr; float* sub = nullptr; float* low = nullptr; float* up = nullptr; };
    void setTaps (const Taps& tp) { taps = tp; }

    // in/out may alias. numCh 1 or 2.
    void process (float* const* io, int numCh, int numSamples)
    {
        const double subG = dbToGain (prm.subGainDb), hiG = dbToGain (prm.highGainDb), outG = dbToGain (prm.outGainDb);
        const double resG = prm.residualDb <= -60.0 ? 0.0 : dbToGain (prm.residualDb);
        const double fadeN = std::clamp (prm.fadeMs, 0.25, 8.0) * 0.001 * fs;
        const double sens = std::clamp (prm.sensitivity, 0.0, 1.0);
        const double lpThresh = 30.0 - 24.0 * sens;   // spike factor
        const double stab = std::clamp (prm.stabilize, 0.0, 1.0);
        const double kpll = kTwoPi * prm.pllHz * (1.0 - 0.9 * stab) / fs;
        const double relA = 1.0 - std::exp (-1.0 / (0.003 * std::pow (667.0, stab) * fs));   // 3 ms … 2 s
        const double phOff = prm.subPhaseDeg * kPi / 180.0;
        const double glideA = 1.0 - std::exp (-1.0 / (0.003 * fs));
        const double ampA = 1.0 - std::exp (-1.0 / (0.003 * fs));

        for (int i = 0; i < numSamples; ++i, ++t)
        {
            // ---------------------------------------------------------- input
            const double xl = io[0][i];
            const double xr = numCh > 1 ? io[1][i] : xl;
            const int wi = idx (t);
            X[0][(size_t) wi] = xl; X[1][(size_t) wi] = xr;
            const double m = 0.5 * (xl + xr);
            M[(size_t) wi] = m;
            Mraw[(size_t) wi] = m;   // detector always sees the unrepaired signal
            MidiF0[(size_t) wi] = midiF0Now;
            EditFlag[(size_t) wi] = 0;

            // ---------------------------------------------------------- edit detector (input time)
            if (prm.repairOn) detectEdit (t, lpThresh, (int) fadeN);
            if (repairQueueN > 0) serviceRepairs (t);

            // ---------------------------------------------------------- f0 tracker
            const double y = aa2.process (aa1.process (m));
            if (++decCount >= dec)
            {
                decCount = 0;
                dRing[(size_t) (dWrite & (dRingSize - 1))] = y; ++dWrite;
                if (++hopCount >= hopD) { hopCount = 0; runYin(); }
            }
            // f0 estimate refers to time t - Ltrack (approx centre of YIN window)
            {
                const int64_t tc = t - Ltrack;
                if (tc >= 0)
                {
                    F0trk[(size_t) idx (tc)] = trackF0;
                    Conf[(size_t) idx (tc)] = trackConf;
                }
            }

            // ---------------------------------------------------------- stage C
            const int64_t c = t - Ltrack;
            if (c >= 0)
            {
                const int ci = idx (c);
                double target;
                if (prm.midiMode) { const double mf = MidiF0[(size_t) ci]; target = mf > 0 ? mf : std::exp (logF0c); }
                else target = F0trk[(size_t) ci];
                target = std::clamp (target, f0MinFixed, std::max (f0MinFixed + 1, prm.f0Max));
                logF0c += (std::log (target) - logF0c) * glideA;
                const double f0 = std::exp (logF0c);
                F0c[(size_t) ci] = f0;
                phiA = wrapAccum (phiA + kTwoPi * f0 / fs);
                PhiA[(size_t) ci] = phiA;
                const std::complex<double> b = M[(size_t) ci] * std::complex<double> (std::cos (phiA), -std::sin (phiA));
                const std::complex<double> prev = c > 0 ? S[(size_t) idx (c - 1)] : std::complex<double> (0, 0);
                S[(size_t) ci] = prev + b;
            }

            // ---------------------------------------------------------- stage O
            const int64_t n = c - H;
            double outL = 0, outR = 0;
            float tInLow = 0, tSub = 0, tLow = 0, tUp = 0;
            if (n >= (int64_t) (Pmax + 4))
            {
                const int ni = idx (n);
                const double f0 = F0c[(size_t) ni];
                const double P = std::min (fs / f0, Pmax);
                const std::complex<double> C = (Sinterp ((double) n + 0.5 * P) - Sinterp ((double) n - 0.5 * P)) / P;
                const double pa = PhiA[(size_t) ni];
                const std::complex<double> e (std::cos (pa), std::sin (pa));
                const double fund = 2.0 * (C * e).real();
                const double thetaIn = std::arg (C) + pa;
                const double amp = 2.0 * std::abs (C);

                // --- sub oscillator + PLL
                phiS = wrapAccum (phiS + kTwoPi * f0 / fs);
                if (EditFlag[(size_t) ni]) freeze = (int) (0.040 * fs);
                const bool voiced = Conf[(size_t) ni] > 0.2 || prm.midiMode;
                bool follow = false;
                if (prm.phaseMode == 1) follow = true;
                else if (prm.phaseMode == 2) follow = (freeze == 0) && voiced;
                if (freeze > 0) --freeze;
                if (follow && amp > 1e-5) phiS += kpll * wrapPi (thetaIn + phOff - phiS);
                {
                    const double target = (everLocked || prm.midiMode) ? amp : 0.0;
                    // Broadband level gate: reese/detune beating cancels the fundamental but
                    // not the overall energy, a note ending drops both. So the long Stabilize
                    // release only applies while the broadband level holds.
                    // mean-square over ~2 periods so the envelope does not ripple within a cycle
                    const double xm = 0.5 * (X[0][(size_t) ni] + X[1][(size_t) ni]);
                    envMs += (xm * xm - envMs) * (1.0 / std::max (0.02 * fs, 2.0 * fs / f0));
                    envFast = std::sqrt (std::max (envMs, 0.0));
                    envHold = std::max (envFast, envHold + (envFast - envHold) * relA);
                    const bool noteEnded = envFast < 0.25 * envHold || freeze > 0;
                    if (noteEnded) envHold = envFast;
                    // attack ~3 ms; release follows Stabilize (3 ms … 2 s)
                    ampSm += (target - ampSm) * (target > ampSm ? ampA : (noteEnded ? ampA : relA));
                }
                const double sub = ampSm * std::cos (phiS);

                // --- complementary tracking split of the residual r = x - fund:
                //     lowR = LP4_{k·f0}(r)  → folded to mono, added to the low band
                //     up   = r - lowR       → stays stereo (width / level)
                //     Since up + lowR = r exactly, at default settings the output
                //     reconstructs the input (L+R) while everything below k·f0 is mono.
                const double fc = std::clamp (prm.splitRatio * f0, 20.0, 0.45 * fs);
                const double g = std::tan (kPi * fc / fs);
                const double rl = X[0][(size_t) ni] - fund, rr = X[1][(size_t) ni] - fund;
                const double ll = lpf[0][1].process (lpf[0][0].process (rl, g, 1.0 / 0.5411961), g, 1.0 / 1.3065630);
                const double lr = lpf[1][1].process (lpf[1][0].process (rr, g, 1.0 / 0.5411961), g, 1.0 / 1.3065630);
                double hl = rl - ll, hr = rr - lr;
                const double mid = 0.5 * (hl + hr), side = 0.5 * (hl - hr) * prm.highWidth;
                hl = (mid + side) * hiG; hr = (mid - side) * hiG;
                const double low = sub * subG + 0.5 * (ll + lr) * resG;
                tInLow = (float) (fund + 0.5 * (ll + lr)); tSub = (float) (sub * subG);
                tLow = (float) low; tUp = (float) (0.5 * (hl + hr));

                switch (prm.monitor)
                {
                    case 1:  outL = outR = low; break;
                    case 2:  outL = hl; outR = hr; break;
                    case 3:  outL = X[0][(size_t) ni] - (hl + low); outR = X[1][(size_t) ni] - (hr + low); break;
                    default: outL = hl + low; outR = hr + low; break;
                }
                meters.f0 = f0; meters.confidence = Conf[(size_t) ni]; meters.subLevel = ampSm;
            }
            if (taps.inLow != nullptr) taps.inLow[i] = tInLow;
            if (taps.sub != nullptr)   taps.sub[i] = tSub;
            if (taps.low != nullptr)   taps.low[i] = tLow;
            if (taps.up != nullptr)    taps.up[i] = tUp;
            io[0][i] = (float) (outL * outG);
            if (numCh > 1) io[1][i] = (float) (outR * outG);
        }
    }

private:
    static double dbToGain (double db) { return std::pow (10.0, db / 20.0); }
    static double wrapAccum (double p) { return p >= kTwoPi ? p - kTwoPi : p; }
    int idx (int64_t time) const { return (int) (time & mask); }

    std::complex<double> Sinterp (double pos) const
    {
        const int64_t i0 = (int64_t) std::floor (pos);
        const double fr = pos - (double) i0;
        const auto a = S[(size_t) idx (i0)], b = S[(size_t) idx (i0 + 1)];
        return a + (b - a) * fr;
    }

    //--------------------------------------------------------------------------
    void runYin()
    {
        if (dWrite < (int64_t) (W + tauMax + 2)) return;
        const int64_t start = dWrite - (W + tauMax);
        const int dm = dRingSize - 1;
        auto at = [&] (int64_t k) { return dRing[(size_t) (k & dm)]; };

        const int tauMin = std::max (2, (int) std::floor (fsd / std::max (prm.f0Max, f0MinFixed + 1)));
        double energy = 0;
        for (int j = 0; j < W; ++j) energy += at (start + j) * at (start + j);
        if (energy < 1e-9) { trackConf = 0; return; }   // silence: hold f0

        diff[0] = 1.0;
        double running = 0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double d = 0;
            for (int j = 0; j < W; ++j) { const double v = at (start + j) - at (start + j + tau); d += v * v; }
            running += d;
            diff[(size_t) tau] = running > 0 ? d * tau / running : 1.0;  // CMND
        }
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (diff[(size_t) tau] < prm.yinThreshold)
            {
                while (tau + 1 < tauMax && diff[(size_t) tau + 1] < diff[(size_t) tau]) ++tau;
                best = tau; break;
            }
        if (best < 0)
        {
            double mn = 1e9;
            for (int tau = tauMin; tau < tauMax; ++tau) if (diff[(size_t) tau] < mn) { mn = diff[(size_t) tau]; best = tau; }
        }
        if (best <= 1 || best >= tauMax - 1) { trackConf = 0; return; }
        const double a = diff[(size_t) best - 1], b = diff[(size_t) best], c = diff[(size_t) best + 1];
        const double den = a - 2 * b + c;
        const double tauF = best + (std::abs (den) > 1e-12 ? 0.5 * (a - c) / den : 0.0);
        const double f0 = fsd / tauF;
        const double conf = std::clamp (1.0 - b, 0.0, 1.0);

        // median-of-3
        histF0 = { histF0[1], histF0[2], f0 };
        auto h = histF0; std::sort (h.begin(), h.end());
        const double cand = h[1];
        trackConf = conf;
        if (conf < 0.15) return;                       // unvoiced: hold last pitch

        // Reese guard. In a detuned reese the fundamental periodically cancels and
        // YIN briefly prefers the octave above. If the previous period is still a valid
        // YIN dip, an octave-up candidate has to persist for up to 400 ms (scaled by
        // Stabilize) before it is accepted; a real octave-up note change still passes.
        {
            const int tauPrev = (int) std::lround (fsd / trackF0);
            const bool octaveUp = std::abs (std::log2 (cand / trackF0) - 1.0) < 0.08;
            const double holdHopsMax = prm.stabilize * 0.400 * fsd / hopD;
            if (octaveUp && everLocked && tauPrev > 1 && tauPrev < tauMax && diff[(size_t) tauPrev] < 0.35
                && octaveHold < holdHopsMax)
            {
                ++octaveHold;
                return;
            }
            if (! octaveUp) octaveHold = 0;
        }

        // Transition lock. While the YIN window straddles two notes it often finds
        // their *common* period (e.g. D2→G2 gives ~33 Hz) with fairly high confidence.
        // A note change is announced earlier by a confidence drop, so: when confidence
        // falls clearly below its running baseline we hold the current pitch, and only
        // release once confidence is back near the baseline AND 4 consecutive hops agree.
        // Hold is capped (150 ms) so material that never regains confidence still tracks.
        const double maxHoldHops = 0.150 * fsd / hopD;
        if (! inTransition)
        {
            if (conf < confBase - 0.12 && confBase > 0.6) { inTransition = true; holdHops = 0; stableCount = 0; }
            else
            {
                confBase += (conf - confBase) * 0.1;
                const bool bigJump = std::abs (std::log2 (cand / trackF0)) > 0.1;   // > ~1.2 st
                if (! bigJump) { trackF0 = cand; pendingCount = 0; if (conf > 0.5) everLocked = true; return; }
                // big jump without a confidence drop: require 3 consistent hops
                if (std::abs (pendingF0 / cand - 1.0) < 0.03) ++pendingCount; else { pendingF0 = cand; pendingCount = 1; }
                if (pendingCount >= 3) { trackF0 = cand; pendingCount = 0; if (conf > 0.5) everLocked = true; }
                return;
            }
        }
        // in transition
        ++holdHops;
        if (std::abs (pendingF0 / cand - 1.0) < 0.02) ++stableCount; else { pendingF0 = cand; stableCount = 1; }
        const bool recovered = conf >= std::min (0.97, confBase - 0.03);
        if ((recovered && stableCount >= 4) || (holdHops > maxHoldHops && stableCount >= 4))
        {
            trackF0 = cand; inTransition = false; pendingCount = 0;
            confBase = conf;
        }
    }

    //--------------------------------------------------------------------------
    void detectEdit (int64_t now, double lpThresh, int fadeN)
    {
        if (now < 600) return;
        const double m0 = Mraw[(size_t) idx (now)], m1 = Mraw[(size_t) idx (now - 1)], m2 = Mraw[(size_t) idx (now - 2)];
        const double e = m0 - (2.0 * m1 - m2);
        const double ae = std::abs (e);
        rmsM += (m0 * m0 - rmsM) * (1.0 / (0.010 * fs));
        const double rms = std::sqrt (std::max (rmsM, 1e-12));
        bool hit = false;
        if (refractory > 0) --refractory;
        else if (ae > lpThresh * lpScale && ae > 0.03 * rms + 1e-5)
        {
            // periodic predictor: a real waveform edge repeats one period earlier.
            // The tracked f0 lags the input by Ltrack, so during fast glides the true
            // period can differ by several percent → search ±12 % around it.
            const double P = fs / std::max (trackF0, 1.0);
            const int kLo = std::max (4, (int) std::floor (P * 0.88)), kHi = std::min ((int) std::ceil (P * 1.12), ringSize - 8);
            double best = 1e9;
            for (int k = kLo; k <= kHi; ++k)
            {
                const double ek = Mraw[(size_t) idx (now - k)] - (2.0 * Mraw[(size_t) idx (now - k - 1)] - Mraw[(size_t) idx (now - k - 2)]);
                best = std::min (best, std::abs (e - ek));
            }
            if (best > 0.5 * ae) hit = true;
        }
        if (hit)
        {
            if (repairQueueN < (int) repairQueue.size())
                repairQueue[(size_t) repairQueueN++] = { now, fadeN };
            refractory = (int) (0.004 * fs);
        }
        else
            lpScale += (ae - lpScale) * (1.0 / (0.005 * fs));
        lpScale = std::max (lpScale, 1e-7);
    }

    void serviceRepairs (int64_t now)
    {
        int w = 0;
        for (int r = 0; r < repairQueueN; ++r)
        {
            auto job = repairQueue[(size_t) r];
            if (now >= job.pos + job.n + 2) repairAt (job.pos, job.n);
            else repairQueue[(size_t) w++] = job;
        }
        repairQueueN = w;
    }

    void repairAt (int64_t p, int N)
    {
        constexpr int ctx = 384, order = 16;
        // transient protect: onset from near-silence → shorter fade so the attack survives
        double eb = 0, ea = 0;
        for (int k = 1; k <= 64; ++k) { const double v = M[(size_t) idx (p - k)]; eb += v * v; }
        for (int k = 0; k < 64; ++k)  { const double v = M[(size_t) idx (p + k)]; ea += v * v; }
        if (ea > 16.0 * eb) N = std::max (8, N / 4);

        std::array<double, ctx> buf{};
        std::array<double, order + 1> a{};
        std::array<double, 2048> ext{};
        N = std::min (N, (int) ext.size());
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& R = X[(size_t) ch];
            for (int k = 0; k < ctx; ++k) buf[(size_t) k] = R[(size_t) idx (p - ctx + k)];
            burgLpc (buf.data(), ctx, order, a.data());
            // extrapolate
            std::array<double, order> hist{};
            for (int k = 0; k < order; ++k) hist[(size_t) k] = buf[(size_t) (ctx - 1 - k)]; // hist[0] = x[p-1]
            for (int k = 0; k < N; ++k)
            {
                double pred = 0;
                for (int j = 1; j <= order; ++j) pred -= a[(size_t) j] * hist[(size_t) j - 1];
                ext[(size_t) k] = pred;
                for (int j = order - 1; j > 0; --j) hist[(size_t) j] = hist[(size_t) j - 1];
                hist[0] = pred;
            }
            for (int k = 0; k < N; ++k)
            {
                const double wOld = 0.5 * (1.0 + std::cos (kPi * (k + 0.5) / N));
                const int ii = idx (p + k);
                R[(size_t) ii] = wOld * ext[(size_t) k] + (1.0 - wOld) * R[(size_t) ii];
            }
        }
        for (int k = 0; k < N; ++k) { const int ii = idx (p + k); M[(size_t) ii] = 0.5 * (X[0][(size_t) ii] + X[1][(size_t) ii]); }
        EditFlag[(size_t) idx (p)] = 1;
        ++meters.editsRepaired;
    }

    //--------------------------------------------------------------------------
    double fs = 48000, fsd = 6000, f0MinFixed = 30, Pmax = 1600;
    int dec = 8, tauMax = 200, W = 200, hopD = 16, dRingSize = 1024;
    int Ltrack = 0, H = 0, latency = 0, ringSize = 0, mask = 0;
    int64_t t = 0, dWrite = 0;
    int decCount = 0, hopCount = 0;

    Params prm;
    Meters meters;
    Taps taps;
    Biquad aa1, aa2;
    std::vector<double> dRing, diff;
    std::array<std::vector<double>, 2> X;
    std::vector<double> M, Mraw, F0trk, Conf, F0c, PhiA, MidiF0;
    std::vector<std::complex<double>> S;
    std::vector<uint8_t> EditFlag;
    std::array<std::array<TptSvfLP, 2>, 2> lpf;

    double trackF0 = 100, trackConf = 0, pendingF0 = 0, confBase = 0; int pendingCount = 0, stableCount = 0, holdHops = 0;
    bool inTransition = false, everLocked = false;
    double octaveHold = 0;
    std::array<double, 3> histF0 { 100, 100, 100 };
    double logF0c = 0, phiA = 0, phiS = 0, ampSm = 0, midiF0Now = 0, envFast = 0, envHold = 0, envMs = 0;
    double lpScale = 1e-4, rmsM = 1e-4; int refractory = 0, freeze = 0;
    struct RepairJob { int64_t pos; int n; };
    std::array<RepairJob, 32> repairQueue {}; int repairQueueN = 0;
};

} // namespace sbs::substrate
