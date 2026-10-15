// NEXUSCLIP CTZ - metering: sample peak, 600 ms sliding RMS and peak (crest
// factor = peak - RMS over the same 600 ms), BS.1770 momentary loudness (400 ms
// K-weighted) with a max-momentary hold, as on DP Meter, and gated
// integrated loudness (BS.1770-4: 400 ms blocks every 100 ms, -70 LUFS
// absolute gate, -10 LU relative gate) kept as a 0.1 dB histogram.
#pragma once
#include "Dsp.h"

namespace nc
{
class Meter
{
public:
    void prepare (double sampleRate, int channels)
    {
        sr = sampleRate;
        nch = std::max (1, channels);
        cell = std::max (1, (int) std::round (sr / 1000.0));
        rms.prepare (600);
        mom.prepare (400);
        shelf.assign ((size_t) nch, {}); hp.assign ((size_t) nch, {});
        for (int c = 0; c < nch; ++c) kWeighting (sr, shelf[(size_t) c], hp[(size_t) c]);
        reset();
    }
    void reset()
    {
        rms.reset(); mom.reset();
        for (auto& b : shelf) b.reset();
        for (auto& b : hp) b.reset();
        accSq = accK = 0; accPk = 0; inCell = 0;
        maxMomentary = -120.0f; peakHold = 0.0f; maxRms = 0.0f; cellsSeen = 0;
        resetIntegrated();
    }
    void resetHolds() { maxMomentary = -120.0f; peakHold = 0.0f; maxRms = 0.0f; }
    void resetIntegrated() { histN.fill (0); histE.fill (0.0); integrated = -120.0f; sinceCalc = 0; }

    void process (const float* const* ch, int n_ch, int n)
    {
        n_ch = std::min (n_ch, nch);
        for (int i = 0; i < n; ++i)
        {
            double sq = 0, ksq = 0;
            float pk = 0;
            for (int c = 0; c < n_ch; ++c)
            {
                const float x = ch[c][i];
                pk = std::max (pk, std::abs (x));
                sq += (double) x * x;
                const float k = hp[(size_t) c].process (shelf[(size_t) c].process (x));
                ksq += (double) k * k;
            }
            // RMS of a stereo pair = power averaged over channels
            accSq += sq / (double) std::max (1, n_ch);
            accK += ksq;                     // BS.1770 sums channel powers (G = 1 for L/R)
            accPk = std::max (accPk, pk);
            peakHold = std::max (peakHold, pk);
            if (++inCell == cell)
            {
                rms.push (accSq, accPk);
                mom.push (accK, 0.0f);
                accSq = accK = 0; accPk = 0; inCell = 0;
                if (++cellsSeen >= 400)
                {
                    const float ml = momentaryLufs();
                    maxMomentary = std::max (maxMomentary, ml);
                    maxRms = std::max (maxRms, rmsLin());
                    if (cellsSeen % 100 == 0 && ml > -70.0f)          // gating block every 100 ms
                    {
                        const int b = std::min (799, (int) ((ml + 70.0f) * 10.0f));
                        histN[(size_t) b]++;
                        histE[(size_t) b] += mom.meanSquare (cell);
                    }
                    if (++sinceCalc >= 1000) { sinceCalc = 0; integrated = computeIntegrated(); }
                }
            }
        }
    }

    float rmsLin() const { return (float) std::sqrt (rms.meanSquare (cell)); }
    float peak600() const { return rms.maxPeak(); }
    float crestDb() const { const float r = rmsLin(), p = peak600(); return r > 1.0e-6f && p > 0 ? gainToDb (p) - gainToDb (r) : 0.0f; }
    float momentaryLufs() const { const double ms = mom.meanSquare (cell); return ms > 1.0e-12 ? (float) (-0.691 + 10.0 * std::log10 (ms)) : -120.0f; }
    float maxMomentary = -120.0f;
    float peakHold = 0.0f, maxRms = 0.0f;
    float integrated = -120.0f;

    float computeIntegrated() const
    {
        double e = 0; long long n = 0;
        for (size_t i = 0; i < histN.size(); ++i) { e += histE[i]; n += histN[i]; }
        if (n == 0) return -120.0f;
        const double rel = -0.691 + 10.0 * std::log10 (e / (double) n) - 10.0;
        e = 0; n = 0;
        for (size_t i = 0; i < histN.size(); ++i)
            if (-70.0 + (double) i * 0.1 >= rel - 0.05) { e += histE[i]; n += histN[i]; }
        return n > 0 && e > 0 ? (float) (-0.691 + 10.0 * std::log10 (e / (double) n)) : -120.0f;
    }

private:
    double sr = 48000.0;
    int nch = 2, cell = 48, inCell = 0;
    long long cellsSeen = 0;
    double accSq = 0, accK = 0;
    float accPk = 0;
    Window rms, mom;
    std::array<long long, 800> histN {};
    std::array<double, 800> histE {};
    int sinceCalc = 0;
    std::vector<Biquad> shelf, hp;
};
} // namespace nc
