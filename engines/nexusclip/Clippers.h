// NEXUSCLIP CTZ - the three clipping engines and the bottom saturator.
//
//   CLIP      zero-latency clipper with a choice of transfer curves (Shapes.h),
//             single band or 4 parallel bands (LR4 crossover, per-band drive /
//             out / solo / bypass); the processor adds oversampling and the
//             reverse-linked in/out test
//   SPECTRAL  short-window STFT clipper: the time-domain clip residual is
//             re-shaped per bin so high-frequency detail keeps its own level
//             (the residual above ~1.5 kHz may not exceed the signal there),
//             then a final exact-ceiling safety clip
//   TRACKLIMIT  wideband lookahead envelope limiter, 50-60 ms programme-
//             dependent release with a 'punch' curve, then the safety clip
//
// Every engine ends in an exact clamp, so nothing ever leaves above the
// ceiling regardless of mode.
#pragma once
#include "Dsp.h"
#include "Shapes.h"

namespace nc
{
enum Mode { modeHard = 0, modeSpectral = 1, modeLimit = 2, numModes = 3 };

struct Settings
{
    int mode = modeHard;
    float thresholdDb = 0.0f;  // the clip level
    int shape = shapeHard;
    float softness = 0.0f;     // 0..1
    bool multiband = false;
    float xover[3] { 120.0f, 1000.0f, 6000.0f };
    float bandDriveDb[4] {}, bandOutDb[4] {};
    bool bandSolo[4] {}, bandBypass[4] {};
    float detail = 0.7f;       // spectral: 0..1 how strongly HF detail is protected
    float releaseMs = 55.0f;   // limiter
    float punch = 0.6f;        // limiter: 0..1
    float lookaheadMs = 1.5f;  // limiter
};

//==============================================================================
// Upward ('from the bottom') saturation: unity at the ceiling, up to +6 dB on
// quiet material, so the crest factor shrinks before the clipper sees it.
struct Saturator
{
    static float shape (float u, int ch)
    {
        // normalised so shape(1) == 1 and shape'(0) ~= 2.07
        static const float k = 2.0f, norm = 1.0f / std::tanh (2.0f);
        if (ch == 0) return std::tanh (k * u) * norm;
        // asymmetric: the positive side bends harder -> even harmonics
        const float a = u >= 0.0f ? std::tanh (k * 1.25f * u) / std::tanh (k * 1.25f) : std::tanh (k * 0.8f * u) / std::tanh (k * 0.8f);
        return a;
    }
    static void process (float* x, int n, float ceiling, float amount, int ch)
    {
        if (amount <= 0.0f) return;
        const float inv = 1.0f / ceiling;
        for (int i = 0; i < n; ++i)
        {
            const float u = x[i] * inv;
            // above the ceiling the clipper owns the signal; keep the curve monotone
            const float s = std::abs (u) <= 1.0f ? shape (u, ch) : (u > 0 ? 1.0f : -1.0f) * (std::abs (shape (u > 0 ? 1.0f : -1.0f, ch)) + (std::abs (u) - 1.0f) * 0.5f);
            x[i] = ceiling * (u + amount * (s - u));
        }
    }
};

//==============================================================================
class SpectralClipper
{
public:
    void prepare (double sampleRate, int channels);
    void reset();
    int latency() const { return N; }
    void process (float* const* ch, int nch, int n, float ceiling, float detail);
    int frameSize() const { return N; }

private:
    void frame (int c, float ceiling, float detail);
    double sr = 48000.0;
    int N = 512, H = 128, hopPos = 0;
    Fft fft;
    std::vector<float> win;
    struct Chan { std::vector<float> in, acc, dry; int dryPos = 0; };
    std::vector<Chan> chans;
    std::vector<std::complex<float>> X, D, T;
    std::vector<float> protect;
};

//==============================================================================
class Limiter
{
public:
    void prepare (double sampleRate, int channels, float maxLookaheadMs);
    void reset();
    void setLookahead (float ms);
    int latency() const { return L; }
    void process (float* const* ch, int nch, int n, float ceiling, float releaseMs, float punch);
    float lastGain() const { return gBox; }

private:
    double sr = 48000.0;
    int L = 72, maxL = 256;
    // audio delay
    std::vector<std::vector<float>> dly;
    int dpos = 0;
    // sliding minimum of the required gain (monotone deque over a ring)
    std::vector<float> reqRing;
    std::vector<long long> dq;
    int dqHead = 0, dqTail = 0;
    long long t = 0;
    // release + box
    float gs = 1.0f, sustain = 0.0f;
    std::vector<float> boxRing;
    double boxSum = 0;
    int boxPos = 0;
    float gBox = 1.0f;
};

//==============================================================================
// One engine instance at one sample rate. The processor owns one per
// oversampling factor and switches between them.
class ClipEngine
{
public:
    void prepare (double sampleRate, int channels);
    void reset();
    // latency in samples at this engine's rate for the given settings
    int latencyFor (const Settings&) const;
    void process (float* const* ch, int nch, int n, const Settings&);
    int activeMode() const { return mode; }
    // per-band read-outs of the last block (multiband): peak into / out of each band's clipper
    float bandIn[4] {}, bandOut[4] {};

private:
    void clipBlock (float* const* ch, int nch, int n, const Settings&, float thr);
    double sr = 48000.0;
    int nch = 2, mode = -1;
    float lastLookahead = -1;
    float lastX[3] { -1, -1, -1 };
    bool wasMulti = false;
    ShapeFn fn;
    std::vector<Crossover4> xo;
    std::vector<float> dcX, dcY;     // DC blocker for the asymmetric shape
    SpectralClipper spectral;
    Limiter limiter;
};
} // namespace nc
