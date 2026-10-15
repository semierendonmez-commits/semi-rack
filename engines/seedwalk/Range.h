// Semi-rack addition (not in the plugin): the walk's range. The ball only visits the nets
// FROM..TO: every shape is drawn by the plugin's makeShape on the survey restricted to those
// nets and mapped back to the sixteen. Header-only so the module and tests/seedwalk_engine.cpp
// share it.
//   * FROM > TO wraps round 16 > 1 (FROM 15, TO 2 = 15 16 1 2); FROM == TO sits on that net.
//   * Inside the range no net repeats itself and none is a dead end (two nets ping-pong), CLEAR
//     excepted (empty: the ball rests).
//   * Rows of the nets outside the range are the entry row: the next step goes back in.
//   * kJump (appended after CLEAR): every step jumps to any other net of the range, equally likely.
#pragma once

#include "Walk.h"
#include <cstdint>

namespace sw::range
{
constexpr int kJump = walk::kShapes;
constexpr int kNumShapes = walk::kShapes + 1;

// the nets FROM..TO (0-based, inclusive), in walk order; returns how many
inline int nets (int from, int to, int* idx)
{
    int n = 0;
    if (from <= to)
        for (int k = from; k <= to; ++k) idx[n++] = k;
    else
    {
        for (int k = from; k < kSteps; ++k) idx[n++] = k;
        for (int k = 0; k <= to; ++k) idx[n++] = k;
    }
    return n;
}

inline uint16_t mask (const int* idx, int n)
{
    uint16_t m = 0;
    for (int k = 0; k < n; ++k) m |= (uint16_t) (1u << idx[k]);
    return m;
}

// `sub` is scratch (a Survey is ~7 KB: keep it off the audio thread's stack)
inline walk::Pin shape (int shape, const walk::Survey& full, walk::Survey& sub, const int* idx, int n, float spread, uint32_t seed)
{
    using walk::Pin;
    constexpr int S = walk::kStates;
    Pin m {};
    uint16_t in = mask (idx, n);
    if (n <= 0) return m;
    if (shape == walk::shClear) return m;
    if (shape == kJump || n == 1)
    {
        for (int a = 0; a < n; ++a)
        {
            m[0][(size_t) idx[a] + 1] = 100.0f;
            for (int b = 0; b < n; ++b)
                if (b != a || n == 1) m[(size_t) idx[a] + 1][(size_t) idx[b] + 1] = 100.0f;
        }
    }
    else
    {
        for (int a = 0; a < n; ++a)
        {
            sub.prof[(size_t) a] = full.prof[(size_t) idx[a]];
            for (int b = 0; b < n; ++b)
            {
                sub.dist[(size_t) a][(size_t) b] = full.dist[(size_t) idx[a]][(size_t) idx[b]];
                sub.chr[(size_t) a][(size_t) b] = full.chr[(size_t) idx[a]][(size_t) idx[b]];
            }
        }
        const Pin s = walk::makeShape (shape, sub, n, spread, seed);
        for (int b = 0; b < n; ++b) m[0][(size_t) idx[b] + 1] = s[0][(size_t) b + 1];
        for (int a = 0; a < n; ++a)
            for (int b = 0; b < n; ++b)
                m[(size_t) idx[a] + 1][(size_t) idx[b] + 1] = a == b ? 0.0f : s[(size_t) a + 1][(size_t) b + 1];
        for (int a = 0; a < n; ++a)
        {
            float sum = 0.0f;
            for (int y = 1; y < S; ++y) sum += m[(size_t) idx[a] + 1][(size_t) y];
            if (sum <= 0.0f) m[(size_t) idx[a] + 1][(size_t) idx[(a + 1) % n] + 1] = 100.0f;
        }
        float sum0 = 0.0f;
        for (int y = 1; y < S; ++y) sum0 += m[0][(size_t) y];
        if (sum0 <= 0.0f) m[0][(size_t) idx[0] + 1] = 100.0f;
    }
    for (int k = 0; k < kSteps; ++k)
        if (! (in >> k & 1)) m[(size_t) k + 1] = m[0];
    return m;
}

} // namespace sw::range
