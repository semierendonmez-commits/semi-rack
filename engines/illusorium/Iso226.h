// ISO 226:2003 equal-loudness contours (normal equal-loudness-level contours).
// Sound pressure level in dB SPL that sounds as loud as `phon` at 1 kHz.
#pragma once
#include <cmath>

namespace il
{
inline float iso226Spl (float phon, float freq)
{
    static const float f[29] = { 20, 25, 31.5f, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000,
                                 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500 };
    static const float af[29] = { 0.532f, 0.506f, 0.480f, 0.455f, 0.432f, 0.409f, 0.387f, 0.367f, 0.349f, 0.330f, 0.315f, 0.301f, 0.288f, 0.276f,
                                  0.267f, 0.259f, 0.253f, 0.250f, 0.246f, 0.244f, 0.243f, 0.243f, 0.243f, 0.242f, 0.242f, 0.245f, 0.254f, 0.271f, 0.301f };
    static const float Lu[29] = { -31.6f, -27.2f, -23.0f, -19.1f, -15.9f, -13.0f, -10.3f, -8.1f, -6.2f, -4.5f, -3.1f, -2.0f, -1.1f, -0.4f, 0.0f,
                                  0.3f, 0.5f, 0.0f, -2.7f, -4.1f, -1.0f, 1.7f, 2.5f, 1.2f, -2.1f, -7.1f, -11.2f, -10.7f, -3.1f };
    static const float Tf[29] = { 78.5f, 68.7f, 59.5f, 51.1f, 44.0f, 37.5f, 31.5f, 26.5f, 22.1f, 17.9f, 14.4f, 11.4f, 8.6f, 6.2f, 4.4f, 3.0f,
                                  2.2f, 2.4f, 3.5f, 1.7f, -1.3f, -4.2f, -6.0f, -5.4f, -1.5f, 6.0f, 12.6f, 13.9f, 12.3f };
    auto at = [&] (int i)
    {
        const float Af = 4.47e-3f * (std::pow (10.0f, 0.025f * phon) - 1.15f)
                       + std::pow (0.4f * std::pow (10.0f, (Tf[i] + Lu[i]) / 10.0f - 9.0f), af[i]);
        return 10.0f / af[i] * std::log10 (Af) - Lu[i] + 94.0f;
    };
    if (freq <= f[0]) return at (0);
    if (freq >= f[28]) return at (28);
    int i = 0;
    while (i < 27 && f[i + 1] < freq) ++i;
    const float t = (std::log (freq) - std::log (f[i])) / (std::log (f[i + 1]) - std::log (f[i]));
    return at (i) + t * (at (i + 1) - at (i));
}
} // namespace il
