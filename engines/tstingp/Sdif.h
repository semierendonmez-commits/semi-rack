// Vendored from semierendonmez-commits tst-ingp Source/engine/Sdif.h (Rack / MetaModule port).
// Only change: namespaces ingp -> tstingp, origpresets -> tstingp_orig (one binary, PINDATA has a copy).
#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cmath>

/*  Minimal SDIF reader for 1TRC (sinusoidal track) frames, standing in for
    CNMAT's SDIF-buffer / SDIF-tuples / threefates chain in [p sdif_read]:
        SDIF-tuples time t columns 1 2 3  ->  index freq amp
        threefates                         ->  f a f a ... ordered by partial index
        [iter] [if $f1!=0] [if $f1<1. then out2 $f1 else $f1]
        amps * level, freqs * freq   ->  [p 2mtslider]                            */
namespace tstingp
{
struct SdifPartial { float index = 0, freq = 0, amp = 0; };
struct SdifFrame { double time = 0; std::vector<SdifPartial> partials; };

struct SdifFile
{
    std::vector<SdifFrame> frames;
    std::string error;

    double duration() const { return frames.empty() ? 0.0 : frames.back().time; }

    bool parse (const void* data, size_t size)
    {
        frames.clear(); error.clear();
        auto* d = static_cast<const uint8_t*> (data);
        auto be32 = [&] (size_t p) -> int32_t { return (int32_t) ((uint32_t) d[p] << 24 | (uint32_t) d[p + 1] << 16 | (uint32_t) d[p + 2] << 8 | d[p + 3]); };
        auto beF = [&] (size_t p) { uint32_t u = (uint32_t) be32 (p); float f; std::memcpy (&f, &u, 4); return f; };
        auto beD = [&] (size_t p) { uint64_t u = 0; for (int i = 0; i < 8; ++i) u = (u << 8) | d[p + (size_t) i]; double v; std::memcpy (&v, &u, 8); return v; };
        if (size < 16 || std::memcmp (d, "SDIF", 4) != 0) { error = "not an SDIF file"; return false; }
        size_t p = 8 + (size_t) be32 (4);
        while (p + 24 <= size)
        {
            const bool trc = std::memcmp (d + p, "1TRC", 4) == 0 || std::memcmp (d + p, "1HRM", 4) == 0;
            const size_t fsize = (size_t) be32 (p + 4);
            const double t = beD (p + 8);
            const int nm = be32 (p + 20);
            size_t q = p + 24;
            SdifFrame fr; fr.time = t;
            for (int m = 0; m < nm && q + 16 <= size; ++m)
            {
                const bool mtrc = std::memcmp (d + q, "1TRC", 4) == 0 || std::memcmp (d + q, "1HRM", 4) == 0;
                const int dt = be32 (q + 4), rows = be32 (q + 8), cols = be32 (q + 12);
                q += 16;
                const int w = dt & 0xff;
                const size_t n = (size_t) rows * (size_t) cols * (size_t) w;
                if (q + n > size) break;
                if (trc && mtrc && cols >= 3 && (w == 4 || w == 8))
                    for (int r = 0; r < rows; ++r)
                    {
                        auto at = [&] (int c) { const size_t o = q + ((size_t) r * (size_t) cols + (size_t) c) * (size_t) w; return w == 4 ? (double) beF (o) : beD (o); };
                        fr.partials.push_back ({ (float) at (0), (float) at (1), (float) at (2) });
                    }
                q += n; q += (8 - n % 8) % 8;
            }
            if (trc && std::isfinite (t)) frames.push_back (std::move (fr));
            if (fsize == 0) break;
            p += 8 + fsize;
        }
        std::sort (frames.begin(), frames.end(), [] (auto& a, auto& b) { return a.time < b.time; });
        if (frames.empty()) { error = "no 1TRC frames"; return false; }
        return true;
    }

    /** The frame SDIF-tuples would return at time t (seconds): the last one at or before t. */
    const SdifFrame* frameAt (double t) const
    {
        if (frames.empty()) return nullptr;
        auto it = std::upper_bound (frames.begin(), frames.end(), t, [] (double v, const SdifFrame& f) { return v < f.time; });
        if (it == frames.begin()) return &frames.front();
        return &*(it - 1);
    }
};

/** [iter] -> [if $f1!=0] -> split -> [p 2mtslider]: up to 32 freq sliders and 32 amp sliders. */
struct SliderLists { std::vector<float> freq, amp; };

inline SliderLists valuesToSliders (const std::vector<double>& values, double freqScale = 1.0, double level = 1.0)
{
    SliderLists out;
    for (double v : values)
    {
        if (v == 0.0) continue;
        if (v < 1.0) { const double a = v * level; if (out.amp.size() < 32) out.amp.push_back ((float) std::sqrt (std::clamp (a, 0.0, 1.0))); }
        else { const double f = v * freqScale; if (out.freq.size() < 32) out.freq.push_back ((float) std::min (1.0, std::pow (f / 20000.0, 0.25))); }
    }
    return out;
}

/** threefates: f a f a ... ordered by partial index. */
inline std::vector<double> frameToValues (const SdifFrame& f)
{
    auto ps = f.partials;
    std::stable_sort (ps.begin(), ps.end(), [] (auto& a, auto& b) { return a.index < b.index; });
    std::vector<double> v; v.reserve (ps.size() * 2);
    for (auto& p : ps) { v.push_back (p.freq); v.push_back (p.amp); }
    return v;
}
} // namespace ingp
