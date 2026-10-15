#include "Clippers.h"

namespace nc
{
//==============================================================================
// Spectral clipper
//
// Streaming STFT, sqrt-Hann analysis and synthesis, hop N/4 (the product
// window sums to 2). Per frame:
//     c = clamp(x, ceiling) - x            (what a hard clip would remove)
//     X = FFT(x w), D = FFT(c w)
//     below ~1.5 kHz D passes untouched   (that is where the peaks live)
//     above, |D_k| <= rho |X_k|            (the clip residual may not be louder
//                                           than the material in that bin)
//     residual' = IFFT(D') w / 2, overlap-added
// output = x delayed by N + residual', then an exact clamp at the ceiling.
// With detail = 0 the residual is unmodified and the result is bit-for-bit
// a hard clip delayed by N (tested).
void SpectralClipper::prepare (double sampleRate, int channels)
{
    sr = sampleRate;
    N = sr <= 50000.0 ? 512 : (sr <= 100000.0 ? 1024 : 2048);   // ~10.7 ms at any rate
    H = N / 4;
    fft.prepare (N);
    win.resize ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = std::sqrt (0.5f - 0.5f * std::cos (2.0f * kPi * (float) i / (float) N));
    chans.assign ((size_t) std::max (1, channels), Chan {});
    for (auto& c : chans) { c.in.assign ((size_t) N, 0.0f); c.acc.assign ((size_t) N, 0.0f); c.dry.assign ((size_t) N, 0.0f); c.dryPos = 0; }
    X.assign ((size_t) N, {});
    D.assign ((size_t) N, {});
    T.assign ((size_t) N, {});
    protect.assign ((size_t) N / 2 + 1, 0.0f);
    const float split = 1500.0f;
    for (int k = 1; k <= N / 2; ++k)
    {
        // smooth one-octave transition into the protected band
        const float oct = std::log2 ((float) k * (float) sr / (float) N / split);
        protect[(size_t) k] = oct <= -0.5f ? 0.0f : (oct >= 0.5f ? 1.0f : 0.5f - 0.5f * std::cos (kPi * (oct + 0.5f)));
    }
    hopPos = 0;
}

void SpectralClipper::reset()
{
    for (auto& c : chans)
    {
        std::fill (c.in.begin(), c.in.end(), 0.0f);
        std::fill (c.acc.begin(), c.acc.end(), 0.0f);
        std::fill (c.dry.begin(), c.dry.end(), 0.0f);
        c.dryPos = 0;
    }
    hopPos = 0;
}

void SpectralClipper::frame (int ci, float ceiling, float detail)
{
    auto& c = chans[(size_t) ci];
    // shift the overlap-add accumulator by one hop (the first hop was output)
    std::copy (c.acc.begin() + H, c.acc.end(), c.acc.begin());
    std::fill (c.acc.end() - H, c.acc.end(), 0.0f);

    bool any = false;
    for (int i = 0; i < N; ++i)
    {
        const float x = c.in[(size_t) i];
        const float r = clampf (x, -ceiling, ceiling) - x;
        if (r < 0.0f || r > 0.0f) any = true;
        D[(size_t) i] = { r * win[(size_t) i], 0.0f };
        X[(size_t) i] = { x * win[(size_t) i], 0.0f };
    }
    if (! any) return;

    if (detail > 0.0f)
    {
        fft.forward (X.data());
        const float rho = std::pow (10.0f, -detail * 30.0f / 20.0f);   // up to -30 dB under the signal
        std::fill (T.begin(), T.end(), std::complex<float> {});
        // A few passes: the HF part of the residual that is withheld leaves
        // overs behind; the next pass clips what is left of them and its LF
        // part takes the reduction instead. Frame-local estimate of the
        // signal so far: x + IFFT(total)/w (window guarded at the edges).
        for (int pass = 0; pass < 3; ++pass)
        {
            if (pass > 0)
            {
                std::copy (T.begin(), T.end(), D.begin());
                fft.inverse (D.data());
                bool more = false;
                for (int i = 0; i < N; ++i)
                {
                    const float w = win[(size_t) i];
                    const float x = c.in[(size_t) i] + D[(size_t) i].real() / std::max (w, 0.25f);
                    const float r = clampf (x, -ceiling, ceiling) - x;
                    if ((r < 0.0f || r > 0.0f) && w > 0.25f) more = true;
                    D[(size_t) i] = { r * w, 0.0f };
                }
                if (! more) break;
            }
            fft.forward (D.data());
            for (int k = 1; k <= N / 2; ++k)
            {
                const float wgt = protect[(size_t) k];
                if (wgt > 0.0f)
                {
                    // budget left in this bin for residual on top of what earlier passes used
                    const float lim = std::abs (X[(size_t) k]) * rho;
                    const float md = std::abs (D[(size_t) k]);
                    const float used = std::abs (T[(size_t) k]);
                    const float room = std::max (0.0f, lim - used);
                    if (md > room && md > 0.0f) D[(size_t) k] *= (md + wgt * (room - md)) / md;
                }
                T[(size_t) k] += D[(size_t) k];
                if (k < N / 2) T[(size_t) (N - k)] = std::conj (T[(size_t) k]);
            }
            T[0] += D[0];
        }
        std::copy (T.begin(), T.end(), D.begin());
        fft.inverse (D.data());
    }
    for (int i = 0; i < N; ++i) c.acc[(size_t) i] += D[(size_t) i].real() * win[(size_t) i] * 0.5f;
}

void SpectralClipper::process (float* const* ch, int nch, int n, float ceiling, float detail)
{
    nch = std::min (nch, (int) chans.size());
    for (int i = 0; i < n; ++i)
    {
        for (int ci = 0; ci < nch; ++ci)
        {
            auto& c = chans[(size_t) ci];
            const float x = ch[ci][i];
            // output: input delayed by N plus the finished residual
            const float dry = c.dry[(size_t) c.dryPos];
            c.dry[(size_t) c.dryPos] = x;
            c.dryPos = (c.dryPos + 1) % N;
            const float y = dry + c.acc[(size_t) hopPos];
            ch[ci][i] = clampf (y, -ceiling, ceiling);
            c.in[(size_t) (N - H + hopPos)] = x;
        }
        if (++hopPos == H)
        {
            hopPos = 0;
            for (int ci = 0; ci < nch; ++ci)
            {
                frame (ci, ceiling, detail);
                auto& in = chans[(size_t) ci].in;
                std::copy (in.begin() + H, in.end(), in.begin());
            }
        }
    }
}

//==============================================================================
// Lookahead limiter (stereo linked)
//   required gain r[n] = min(1, ceiling / max|x_c[n]|)
//   held = min over the last L+1 values of r          (sees L samples ahead)
//   gs   = held if held < gs, else releases toward it  (attack is the box)
//   g    = mean of gs over L+1 samples                 (smooth, reaches the
//                                                       peak's gain in time)
//   out  = x delayed by L times g, then the exact clamp
// Release: 'punch' shortens the release after short transient overs, longer
// overs drift towards release * 1.2 (the 50-60 ms TrackLimit range).
void Limiter::prepare (double sampleRate, int channels, float maxLookaheadMs)
{
    sr = sampleRate;
    maxL = std::max (4, (int) std::ceil (maxLookaheadMs * 0.001 * sr)) + 2;
    dly.assign ((size_t) std::max (1, channels), std::vector<float> ((size_t) maxL + 1, 0.0f));
    reqRing.assign ((size_t) maxL + 1, 1.0f);
    dq.assign ((size_t) maxL + 2, 0);
    boxRing.assign ((size_t) maxL + 1, 1.0f);
    reset();
}

void Limiter::reset()
{
    for (auto& d : dly) std::fill (d.begin(), d.end(), 0.0f);
    std::fill (reqRing.begin(), reqRing.end(), 1.0f);
    std::fill (boxRing.begin(), boxRing.end(), 1.0f);
    dpos = 0; dqHead = dqTail = 0; t = 0;
    gs = 1.0f; sustain = 0.0f; gBox = 1.0f;
    boxSum = (double) (L + 1);
    boxPos = 0;
}

void Limiter::setLookahead (float ms)
{
    const int nl = std::min (maxL - 2, std::max (1, (int) std::round (ms * 0.001 * sr)));
    if (nl != L) { L = nl; reset(); }
}

void Limiter::process (float* const* ch, int nch, int n, float ceiling, float releaseMs, float punch)
{
    nch = std::min (nch, (int) dly.size());
    const int ring = (int) reqRing.size();
    const int W = L + 1;
    const float susCoef = 1.0f - std::exp (-1.0f / (0.2f * (float) sr));
    for (int i = 0; i < n; ++i)
    {
        float pk = 0.0f;
        for (int c = 0; c < nch; ++c) pk = std::max (pk, std::abs (ch[c][i]));
        const float req = pk > ceiling ? ceiling / pk : 1.0f;

        // sliding minimum over the last W samples
        const int slot = (int) (t % ring);
        reqRing[(size_t) slot] = req;
        const int dqn = (int) dq.size();
        while (dqTail != dqHead)
        {
            const int last = (dqTail - 1 + dqn) % dqn;
            if (reqRing[(size_t) (dq[(size_t) last] % ring)] >= req) dqTail = last; else break;
        }
        dq[(size_t) dqTail] = t;
        dqTail = (dqTail + 1) % dqn;
        while (t - dq[(size_t) dqHead] >= W) dqHead = (dqHead + 1) % dqn;
        const float held = reqRing[(size_t) (dq[(size_t) dqHead] % ring)];

        // programme-dependent release
        sustain += ((held < 0.999f ? 1.0f : 0.0f) - sustain) * susCoef;
        const float rel = releaseMs * (1.0f + 0.2f * sustain) * (1.0f - 0.5f * punch * (1.0f - sustain));
        const float rc = 1.0f - std::exp (-1.0f / (std::max (1.0f, rel) * 0.001f * (float) sr));
        if (held < gs) gs = held; else gs += (held - gs) * rc;

        // box average over W
        boxSum += gs - boxRing[(size_t) boxPos];
        boxRing[(size_t) boxPos] = gs;
        boxPos = (boxPos + 1) % W;
        gBox = (float) (boxSum / (double) W);

        // delayed audio
        for (int c = 0; c < nch; ++c)
        {
            auto& d = dly[(size_t) c];
            d[(size_t) dpos] = ch[c][i];
            int r = dpos - L; if (r < 0) r += (int) d.size();
            ch[c][i] = clampf (d[(size_t) r] * gBox, -ceiling, ceiling);
        }
        dpos = (dpos + 1) % (int) dly[0].size();
        ++t;
    }
}

//==============================================================================
void ClipEngine::prepare (double sampleRate, int channels)
{
    sr = sampleRate;
    nch = std::max (1, channels);
    spectral.prepare (sr, nch);
    limiter.prepare (sr, nch, 5.0f);
    xo.assign ((size_t) nch, Crossover4 {});
    dcX.assign ((size_t) nch, 0.0f); dcY.assign ((size_t) nch, 0.0f);
    mode = -1;
    lastLookahead = -1;
    lastX[0] = lastX[1] = lastX[2] = -1;
}

void ClipEngine::reset()
{
    spectral.reset(); limiter.reset();
    for (auto& x : xo) x.reset();
    std::fill (dcX.begin(), dcX.end(), 0.0f); std::fill (dcY.begin(), dcY.end(), 0.0f);
}

int ClipEngine::latencyFor (const Settings& s) const
{
    if (s.mode == modeSpectral) return spectral.latency();
    if (s.mode == modeLimit) return std::min (std::max (1, (int) std::round (s.lookaheadMs * 0.001 * sr)), (int) (5.0 * 0.001 * sr));
    return 0;
}

void ClipEngine::clipBlock (float* const* ch, int n_ch, int n, const Settings& s, float thr)
{
    fn.set (s.shape, s.softness);
    const float inv = 1.0f / thr;
    if (! s.multiband)
    {
        for (int c = 0; c < n_ch; ++c)
            for (int i = 0; i < n; ++i) ch[c][i] = thr * fn (ch[c][i] * inv);
    }
    else
    {
        if (std::abs (s.xover[0] - lastX[0]) + std::abs (s.xover[1] - lastX[1]) + std::abs (s.xover[2] - lastX[2]) > 1.0e-3f)
        {
            const float f1 = s.xover[0], f2 = std::max (f1 * 1.5f, s.xover[1]), f3 = std::max (f2 * 1.5f, s.xover[2]);
            for (auto& x : xo) x.set (sr, f1, f2, f3);
            std::copy (s.xover, s.xover + 3, lastX);
        }
        if (! wasMulti) for (auto& x : xo) x.reset();
        float din[4], dout[4];
        bool anySolo = false;
        for (int b = 0; b < 4; ++b) { din[b] = dbToGain (s.bandDriveDb[b]); dout[b] = dbToGain (s.bandOutDb[b]); anySolo = anySolo || s.bandSolo[b]; bandIn[b] = bandOut[b] = 0.0f; }
        for (int c = 0; c < n_ch; ++c)
            for (int i = 0; i < n; ++i)
            {
                float b4[4];
                xo[(size_t) c].process (ch[c][i], b4);
                float y = 0.0f;
                for (int b = 0; b < 4; ++b)
                {
                    if (anySolo && ! s.bandSolo[b]) continue;
                    if (s.bandBypass[b]) { y += b4[b]; continue; }
                    const float v = b4[b] * din[b];
                    bandIn[b] = std::max (bandIn[b], std::abs (v));
                    const float o = thr * fn (v * inv);
                    bandOut[b] = std::max (bandOut[b], std::abs (o));
                    y += o * dout[b];
                }
                ch[c][i] = y;
            }
    }
    wasMulti = s.multiband;
    if (s.shape == shapeTube)
    {
        // 5 Hz DC blocker for the asymmetric curve
        const float r = 1.0f - (float) (2.0 * 3.14159265358979 * 5.0 / sr);
        for (int c = 0; c < n_ch; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float x = ch[c][i];
                const float y = x - dcX[(size_t) c] + r * dcY[(size_t) c];
                dcX[(size_t) c] = x; dcY[(size_t) c] = y;
                ch[c][i] = y;
            }
    }
}

void ClipEngine::process (float* const* ch, int n_ch, int n, const Settings& s)
{
    n_ch = std::min (n_ch, nch);
    if (s.mode != mode) { mode = s.mode; reset(); }
    const float thr = dbToGain (s.thresholdDb);
    switch (mode)
    {
        case modeSpectral: spectral.process (ch, n_ch, n, thr, s.detail); break;
        case modeLimit:
            if (std::abs (s.lookaheadMs - lastLookahead) > 1.0e-4f) { lastLookahead = s.lookaheadMs; limiter.setLookahead (s.lookaheadMs); }
            limiter.process (ch, n_ch, n, thr, s.releaseMs, s.punch);
            break;
        default: clipBlock (ch, n_ch, n, s, thr); break;
    }
}
} // namespace nc
