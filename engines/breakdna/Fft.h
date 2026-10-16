// semi-rack: vendored; namespaces wrapped in dnafam:: (see sources.mk for every change).
// Iterative radix-2 complex FFT. Used offline (onset analysis), never on the audio thread.
#pragma once

#include <cmath>
#include <complex>
#include <vector>

namespace dnafam::gtbs
{

class Fft
{
public:
    explicit Fft (int sizeLog2) : n (1 << sizeLog2), rev ((size_t) n), tw ((size_t) n / 2)
    {
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < sizeLog2; ++b)
                if (i & (1 << b)) r |= 1 << (sizeLog2 - 1 - b);
            rev[(size_t) i] = r;
        }
        for (int k = 0; k < n / 2; ++k)
        {
            const double a = -2.0 * 3.14159265358979323846 * (double) k / (double) n;
            tw[(size_t) k] = { (float) std::cos (a), (float) std::sin (a) };
        }
    }

    int size() const { return n; }

    // in-place forward transform
    void forward (std::vector<std::complex<float>>& x) const
    {
        for (int i = 0; i < n; ++i)
            if (rev[(size_t) i] > i) std::swap (x[(size_t) i], x[(size_t) rev[(size_t) i]]);
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < half; ++j)
                {
                    const auto w = tw[(size_t) (j * step)];
                    const auto u = x[(size_t) (i + j)];
                    const auto v = x[(size_t) (i + j + half)] * w;
                    x[(size_t) (i + j)] = u + v;
                    x[(size_t) (i + j + half)] = u - v;
                }
        }
    }

private:
    int n;
    std::vector<int> rev;
    std::vector<std::complex<float>> tw;
};

} // namespace dnafam::gtbs
