// VERTEX - port of sm.sallenkey~ (Pete Dowling & Matt Jackson, Surreal
// Machines 2015, smFilterPack) as frozen inside Vertex 1.3: the gen~ code of
// sm_sallenkey_setup / sm_sallenkey / sm_sallenkey_functions, line by line.
//   cutoff in MIDI pitch (cutoffmode 1), cutoffsmooth 0, resonancesmooth 20 ms
//   nonlinear 1, oversampling 2 (4x spline), order pre (0) / post (1)
// Exact math replaces the cheap approximations (expA, tnA, sin_32...): they
// were written to match these functions.
#pragma once
#include <cmath>
#include <algorithm>

namespace vx
{
constexpr double kPiD = 3.14159265358979323846;

struct DcBlock   // gen~ dcblock
{
    double x1 = 0, y1 = 0;
    inline double tick (double x) { const double y = x - x1 + 0.9997 * y1; x1 = x; y1 = y; return y; }
    void clear() { x1 = y1 = 0; }
};

inline double tnhL (double x)   // HQ tanh Lambert approximation, clipped
{
    const double x2 = x * x;
    const double a = (((x2 + 378) * x2 + 17325) * x2 + 135135) * x;
    const double b = ((28 * x2 + 3150) * x2 + 62370) * x2 + 135135;
    return std::clamp (a / b, -1.0, 1.0);
}

// gen~ interp(..., mode="spline"): 4-point Catmull-Rom between y1 and y2
inline double spline (double t, double y0, double y1, double y2, double y3)
{
    return y1 + 0.5 * t * (y2 - y0 + t * (2.0 * y0 - 5.0 * y1 + 4.0 * y2 - y3 + t * (3.0 * (y1 - y2) + y3 - y0)));
}

inline double rsindrive (double xin, double w)   // sine shaper, makeup gain 1/sin(w)
{
    const double outdrive = 1.0 / std::sin (std::min (w, kPiD * 0.5)) - 0.000597;
    return std::sin (xin * w) * outdrive;
}

struct Rsin4x   // rsin4x(): fixed 4x oversampling with the patch's spline voodoo
{
    double umTm1 = 0, umT0 = 0, umT1 = 0;
    double d1[3] {}, d2[3] {}, d3[3] {};   // dmT0, dmT1, dmT2 per branch
    double doX0 = 0;
    inline double tick (double XIN, double w)
    {
        const double x0 = XIN;
        const double x1 = spline (0.25, umTm1, umT0, umT1, x0);
        const double x2 = spline (0.5, umTm1, umT0, umT1, x0);
        const double x3 = spline (0.75, umTm1, umT0, umT1, x0);
        const double y1 = rsindrive (x1, w), y2 = rsindrive (x2, w), y3 = rsindrive (x3, w), y4 = rsindrive (umT1, w);
        const double y2_1 = spline (0.25, y2, d1[0], d1[1], d1[2]);
        const double y3_1 = spline (0.5, y3, d2[0], d2[1], d2[2]);
        const double y4_1 = spline (0.75, y4, d3[0], d3[1], d3[2]);
        const double y0 = (doX0 + y2_1 + y3_1 + y4_1) * 0.25;
        umTm1 = umT0; umT0 = umT1; umT1 = x0;
        doX0 = y1;
        d1[2] = d1[1]; d1[1] = d1[0]; d1[0] = y2;
        d2[2] = d2[1]; d2[1] = d2[0]; d2[0] = y3;
        d3[2] = d3[1]; d3[1] = d3[0]; d3[0] = y4;
        return y0;
    }
    void clear() { *this = Rsin4x(); }
};

struct Nonlin   // rsinNonlin(): 0 = bypass, 1 = 1x, 2 = 4x; always dc-blocked
{
    Rsin4x os; DcBlock dc;
    inline double tick (double x, double drive, int over)
    {
        double y = x;
        if (over == 1) y = rsindrive (x, drive);
        else if (over == 2) y = os.tick (x, drive);
        return dc.tick (y);
    }
    void clear() { os.clear(); dc.clear(); }
};

struct SkfStage   // skfTanNlto(): one nonlinear 12 dB Sallen & Key section
{
    double ic1 = 0, ic2 = 0; DcBlock dc;
    inline double tick (double v0, const double* a, double k, int ft)
    {
        const double v1 = a[0] * ic2 + a[1] * ic1 + tnhL (a[2] * v0);
        const double v2 = a[3] * ic2 + tnhL (a[4] * v1);
        double y = ft == 2 ? v1 - v2 : (ft == 3 ? v0 - v2 : v2);
        ic1 = 2.0 * (v1 - k * v2) - ic1;
        ic2 = 2.0 * v2 - ic2;
        if (! std::isfinite (ic1) || ! std::isfinite (ic2)) { ic1 = ic2 = 0; y = 0; }
        return dc.tick (y);
    }
    void clear() { ic1 = ic2 = 0; dc.clear(); }
};

class SallenKey
{
public:
    // filterType 1 LP, 2 BP, 3 HP; slope 0 = 12 dB, 1 = 24 dB; order 0 = drive pre, 1 = post
    int filterType = 1, slope = 0, order = 0;
    double drive = 0.0;   // 0..1

    void prepare (double sampleRate) { sr = sampleRate; t20 = std::exp (-1.0 / (0.020 * sr)); t15 = std::exp (-1.0 / (0.015111 * sr)); ad = 0.693147 / std::max (0.022666 * sr, 1.0); reset(); }
    void reset()
    {
        pre.clear(); post.clear(); s1.clear(); s2.clear(); out.clear();
        drvZ = (0.1 * kPiD); kSm = 0; katW = 1; attW = 0.5; first = true;
    }
    static double mtof (double p) { return 440.0 * std::exp2 ((p - 69.0) / 12.0); }

    // x: audio, cutoffPitch: MIDI pitch, res: 0..1
    inline double tick (double x, double cutoffPitch, double res)
    {
        // ---- setup (sm_sallenkey_setup) ----
        const double cf = std::clamp (mtof (cutoffPitch), 5.0, sr / 2.25);
        double Kt = std::clamp (res, 0.0, 1.0) * 2.0;
        if (first) { kSm = Kt; first = false; }
        kSm = t20 * kSm + Kt * (1.0 - t20);
        double K = kSm;
        // ---- core (sm_sallenkey) ----
        const double drs = (drive * 0.9 + 0.1) * kPiD;
        drvZ += (drs - drvZ) * ad;                 // logsmooth
        const int over = 2;
        if (K > 1.0 && drive >= 0.5)
        {
            const double kitten = ((std::clamp (drive, 0.5, 1.0) - 0.5) * 0.1997) * ((slope * 0.92) + 1.07) + (over != 2 ? 0.07053 : 0.0);
            const double z = t15;
            katW = z * katW + (1.0 - kitten * kitten) * (1.0 - z);
            const double t = std::clamp (K, 1.0, 2.0) - 1.0, m = (1.0 - std::cos (t * kPiD)) * 0.5;   // cosine interp
            K = K + (K * katW - K) * m;
        }
        K *= 0.95;
        const int overPre = order == 1 ? 0 : over, overPost = order == 0 ? 0 : over;
        double X = pre.tick (x, drvZ, overPre);
        // skfTanCoef
        const double g0 = std::tan (kPiD * (cf / sr)), g1 = 1.0 + g0;
        const double a0 = 1.0 / ((g1 * g1) - (g0 * K));
        double a[5];
        a[0] = K * a0; a[1] = g1 * a0; a[2] = g0 * a[1]; a[3] = 1.0 / g1; a[4] = g0 * a[3];
        constexpr double s12 = 0.70710678118654752;
        double Y = s1.tick (X * s12, a, K, filterType);
        if (slope >= 1) Y = s2.tick (Y * s12, a, K, filterType);
        const double z = t15;
        attW = z * attW + (1.0 / (slope + 2.0)) * (1.0 - z);
        Y *= attW;
        Y = post.tick (Y, drvZ, overPost);
        return out.tick (Y);
    }
private:
    double sr = 48000.0, t20 = 0.999, t15 = 0.999, ad = 0.001, drvZ = 0.3, kSm = 0, katW = 1, attW = 0.5;
    bool first = true;
    Nonlin pre, post; SkfStage s1, s2; DcBlock out;
};
} // namespace vx
