// Vendored from semierendonmez-commits tst-ingp Source/engine/IngpEngine.cpp (Rack / MetaModule port).
// Only change: namespaces ingp -> tstingp, origpresets -> tstingp_orig (one binary, PINDATA has a copy).
#include "IngpEngine.h"
#include <algorithm>

namespace tstingp
{
std::vector<uint8_t> textToBits (const std::string& text, size_t maxBits)
{
    // [p txt2bi]: each line of [text] -> [atoi] -> [iter] -> [p 10to2] -> [iter] -> coll.
    // [p 10to2] builds the binary digits by repeatedly taking %2 and prepending, so the
    // result is MSB first with no leading zeros; a 0 produces nothing.
    std::vector<uint8_t> bits;
    bits.reserve (maxBits);
    for (unsigned char ch : text)
    {
        if (ch == '\r' || ch == '\n') continue;   // [text] hands over lines, not terminators
        if (ch == 0) continue;
        uint8_t tmp[8]; int n = 0;
        for (unsigned v = ch; v != 0; v >>= 1) tmp[n++] = (uint8_t) (v & 1u);
        while (n > 0)
        {
            bits.push_back (tmp[--n]);
            if (bits.size() >= maxBits) return bits;   // [if $i1<2048 then $i1]
        }
    }
    return bits;
}

Engine::Engine()
{
    for (auto& c : cells) c.store (0);
    for (int v = 0; v < V; ++v)
    {
        freq[(size_t) v].store (0.5f); amp[(size_t) v].store (0.5f); pan[(size_t) v].store (0.5f);
        len[(size_t) v].store (64); stepShown[(size_t) v].store (0);
    }
    sine.resize (4097);
    for (int i = 0; i <= 4096; ++i) sine[(size_t) i] = (float) std::sin (2.0 * 3.14159265358979323846 * i / 4096.0);
}

void Engine::loadBits (const std::vector<uint8_t>& bits)
{
    for (int i = 0; i < N; ++i) setCell (i, i < (int) bits.size() ? bits[(size_t) i] : 0);
}

void Engine::clearField() { for (int i = 0; i < N; ++i) setCell (i, 0); }

int Engine::configAt (const Engine& e, int idx) noexcept
{
    // [t i i i i i] fires right to left: self, -1, +1, -64, +64 -> [thresh] list,
    // which [p 0-7]..[p 24-31] read as $i1..$i5 (self is the most significant bit).
    const int c = e.cell (idx), l = e.cell (idx - 1), r = e.cell (idx + 1), u = e.cell (idx - 64), d = e.cell (idx + 64);
    return (c << 4) | (l << 3) | (r << 2) | (u << 1) | d;
}

int Engine::applyRuleAt (int idx) noexcept
{
    if (idx < 0 || idx >= N) return -1;
    const int nv = ruleBit (rule.load (std::memory_order_relaxed), configAt (*this, idx));
    setCell (idx, nv);
    caEvents.fetch_add (1, std::memory_order_relaxed);
    return nv;
}

uint32_t Engine::rnd() noexcept
{
    rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
    return rngState;
}

void Engine::syncReset() noexcept
{
    for (int v = 0; v < V; ++v) { step[(size_t) v] = 0; stepShown[(size_t) v].store (0); }
}

Engine::TickResult Engine::tick (int genPerTick, WalkArea area, uint32_t seedMix) noexcept
{
    TickResult res;
    rngState ^= seedMix * 0x2545F491u;
    if (rngState == 0) rngState = 0x9e3779b9u;

    const uint32_t prev = gates.load (std::memory_order_relaxed);
    const uint32_t mutes = muteMask.load (std::memory_order_relaxed);

    // [p sequences]: 32 x [p seq1] — counter 0..len-1, + (voice-1)*64, -> coll -> sqNN
    for (int v = 0; v < V; ++v)
    {
        const int L = std::clamp (len[(size_t) v].load (std::memory_order_relaxed), 1, 64);
        int& s = step[(size_t) v];
        if (s >= L) s = 0;
        const int bit = cell (v * W + s);
        stepShown[(size_t) v].store (s, std::memory_order_relaxed);
        s = (s + 1) % L;
        if (bit && ! (mutes & (1u << v))) res.gatesOn |= 1u << v;
    }
    res.rising = res.gatesOn & ~prev;
    res.falling = prev & ~res.gatesOn;
    gates.store (res.gatesOn, std::memory_order_relaxed);

    // [p cellular] -> [gate] -> [p movement] -> [p cellauto] -> coll
    for (int g = 0; g < genPerTick; ++g)
    {
        int& x = walk.x; int& y = walk.y;
        const int dx = (int) (rnd() % 3u) - 1;              // [expr random(0,3)-1]
        if (dx == 0)
        {
            const int dy = (int) (rnd() % 2u) * 2 - 1;      // [expr random(0,2)*2-1]
            y += dy;
            if (area == WalkArea::original) { if (y > 33 || y < 0) y = 16; }
            else { if (y < 1) y = H; if (y > H) y = 1; }
        }
        x += dx;
        if (area == WalkArea::original) { if (x > 33 || x < 0) x = 32; }
        else { if (x < 1) x = W; if (x > W) x = 1; }

        if (area == WalkArea::full) { y = ((y - 1) % H + H) % H + 1; x = ((x - 1) % W + W) % W + 1; }
        const int idx = (y - 1) * W + (x - 1);              // [expr (($i2-1)*64)+($i1-1)]
        const int before = cell (idx);
        const int after = applyRuleAt (idx);
        if (after >= 0 && after != before) ++res.caChanged;
        res.caIndex = idx;
    }
    cursorX.store (walk.x); cursorY.store (walk.y);
    if (genPerTick > 0) cursorIdx.store ((walk.y - 1) * W + (walk.x - 1));
    tickCount.fetch_add (1, std::memory_order_relaxed);
    return res;
}

void Engine::prepare (double sampleRate) noexcept
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    for (int v = 0; v < V; ++v)
    {
        auto& vo = voices[(size_t) v];
        vo.ampS = amp[(size_t) v].load(); vo.panS = pan[(size_t) v].load();
    }
}

void Engine::startGateRamps (uint32_t gatesOn, double smoothMs) noexcept
{
    // [pack 0 0.] -> [$1 $2] -> [line~]: ramp from wherever it is to the new target
    const int ramp = (int) std::llround (std::max (0.0, smoothMs) * 0.001 * sr);
    for (int v = 0; v < V; ++v)
    {
        auto& vo = voices[(size_t) v];
        const double target = (gatesOn >> v) & 1u ? 1.0 : 0.0;
        vo.gateTarget = target;
        if (ramp <= 0) { vo.gate = target; vo.gateLeft = 0; vo.gateInc = 0; }
        else { vo.gateLeft = ramp; vo.gateInc = (target - vo.gate) / ramp; }
    }
}

void Engine::render (float* L, float* R, int n, const RenderParams& p) noexcept
{
    const double nyq = sr * 0.49;
    const float dz = (float) (1.0 - std::exp (-1.0 / (0.004 * sr)));   // 4 ms de-zipper
    for (int v = 0; v < V; ++v)
    {
        auto& vo = voices[(size_t) v];
        if (vo.gate == 0.0 && vo.gateLeft == 0 && vo.gateTarget == 0.0)
        {
            // silent: keep the oscillator running so phase stays continuous like cycle~
            const double hz = sliderToHz (freq[(size_t) v].load (std::memory_order_relaxed)) * p.tuneRatio;
            vo.phase = std::fmod (vo.phase + hz / sr * n, 1.0);
            if (! (vo.phase >= 0.0 && vo.phase < 1.0)) vo.phase = 0.0;   // port fix: negative / NaN hz
            vo.ampS = amp[(size_t) v].load (std::memory_order_relaxed);
            vo.panS = pan[(size_t) v].load (std::memory_order_relaxed);
            continue;
        }
        const double hz = sliderToHz (freq[(size_t) v].load (std::memory_order_relaxed)) * p.tuneRatio;
        const double inc = hz / sr;
        float tiltG = 1.0f;
        if (p.tilt != 0.0 && hz > 1.0)
            tiltG = (float) std::clamp (std::pow (hz / 1000.0, p.tilt * 0.75), 0.05, 4.0);
        const bool audible = hz < nyq;
        const float aT = amp[(size_t) v].load (std::memory_order_relaxed);
        const float pT = pan[(size_t) v].load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            vo.ampS += (aT - vo.ampS) * dz;
            vo.panS += (pT - vo.panS) * dz;
            if (vo.gateLeft > 0) { vo.gate += vo.gateInc; if (--vo.gateLeft == 0) vo.gate = vo.gateTarget; }
            const double pos = vo.phase * 4096.0;
            // port fix: clamp the table index (a phase that ever left 0..1 read past the table)
            const int i0 = std::clamp ((int) pos, 0, 4095); const float fr = (float) (pos - i0);
            const float s = sine[(size_t) i0] + (sine[(size_t) i0 + 1] - sine[(size_t) i0]) * fr;   // [cycle~]
            // port fix: wrap fully; above the sample rate inc >= 1 and one subtraction is not enough
            vo.phase += inc; vo.phase -= std::floor (vo.phase);
            if (! (vo.phase >= 0.0 && vo.phase < 1.0)) vo.phase = 0.0;
            if (! audible) continue;
            const float g = s * (float) vo.gate * vo.ampS * vo.ampS * tiltG * p.gain;   // * line~ * pow(amp,2)
            float pn = 0.5f + (vo.panS - 0.5f) * (float) p.width;
            pn = pn < 0 ? 0 : (pn > 1 ? 1 : pn);
            L[i] += g * (1.0f - pn);                                                     // [expr 1.-$f1]
            R[i] += g * pn;
        }
    }
}
} // namespace ingp
