// MODPORT for Rack / MetaModule: the transport around the vendored replayer.
// New file (not in the plugin): the plugin plays with the DAW transport, Rack has
// CLK / RST instead. Header-only, no allocation, no exceptions.
//
//   FREE   the song's own timing (CIA BPM or VBlank) times a tempo factor.
//   CLOCK  one clock edge = one row. Between edges the row's ticks are spread
//          evenly over the measured clock period (so slides, arpeggios and retrigs
//          keep their shape); the tick that would start the next row waits for the
//          next edge, and an early edge first runs the rest of the row's ticks.
//
// Audio is rendered as per-channel stems (filtered like the plugin's stems) and
// mixed with the plugin's LRRL panning, so the module gets the mix and per-channel
// gates / pitches from one render.
#pragma once
#include "Player.h"

#include <algorithm>
#include <cmath>

namespace modport
{
namespace drv
{

constexpr int kMaxBlock = 64;

// Amiga LRRL (as Player.cpp's kLeft)
constexpr bool kLeftCh[kMaxChannels] = { true, false, false, true, true, false, false, true };

struct Driver
{
    Player* player = nullptr;
    double sr = 48000.0;
    double tickLeft = 0.0;      // samples until the next tick (free) / the next in-row tick (clock)
    bool clockMode = false;
    double clockPeriod = 0.0;   // samples per row in clock mode (measured by the module)
    double tempo = 1.0;         // free mode: 1 = the song's own speed
    float stereo = 0.5f;        // 0 = mono, 1 = hard Amiga panning
    int trigOffset[kMaxChannels];   // offset of the last trigger in this block, -1 = none
    int curOffset = 0;

    Driver() { std::fill (trigOffset, trigOffset + kMaxChannels, -1); }

    void attach (Player* p)
    {
        player = p;
        tickLeft = 0.0;
        if (player != nullptr)
            player->onTrigger = [this] (const Trigger& t) { if (t.channel >= 0 && t.channel < kMaxChannels) trigOffset[t.channel] = curOffset; };
    }

    double freeTickSamples() const
    {
        const double sec = player->isVBlank() ? (313.0 * 227.0) / kPaulaClock : 1.0 / ciaBpmToHz (std::max (32, player->getTempo()));
        return sec * sr / std::max (0.05, tempo);
    }

    // next tick reads a new row (or the next row of a pattern delay)
    bool atRowBoundary() const { return player->getTick() >= player->getSpeed() - 1; }

    // Clock edge (clock mode): finish the current row, then start the next one now.
    void clockEdge()
    {
        if (player == nullptr || player->module() == nullptr) return;
        for (int guard = 0; ! atRowBoundary() && guard < 256; ++guard) player->tick();
        player->tick();
        tickLeft = clockPeriod > 0.0 ? clockPeriod / std::max (1, player->getSpeed()) : 1.0e9;
    }

    void restart()
    {
        if (player != nullptr && player->module() != nullptr) player->restart();
        tickLeft = 0.0;
    }

    // Renders [from, to) of the block into stems (stems[c] + from ...). running = ticks allowed.
    void render (float* const* stems, int from, int to, bool running)
    {
        int pos = from;
        float* s[kMaxChannels] {};
        while (pos < to)
        {
            int m = to - pos;
            if (running)
            {
                if (tickLeft <= 0.0)
                {
                    if (! clockMode)
                    {
                        curOffset = pos;
                        player->tick();
                        tickLeft += freeTickSamples();
                    }
                    else if (! atRowBoundary())
                    {
                        curOffset = pos;
                        player->tick();
                        tickLeft += clockPeriod > 0.0 ? clockPeriod / std::max (1, player->getSpeed()) : 1.0e9;
                    }
                    else tickLeft = 1.0e9;   // wait for the next clock edge
                }
                m = std::min (m, std::max (1, (int) std::ceil (tickLeft)));
            }
            for (int c = 0; c < kMaxChannels; ++c) s[c] = stems[c] + pos;
            player->renderTail (nullptr, nullptr, s, m);
            if (running) tickLeft -= m;
            pos += m;
        }
    }

    // Stems -> LRRL stereo mix (as Player::render: stems are already scaled by 2 / numCh).
    void mix (float* const* stems, int numCh, float* L, float* R, int n) const
    {
        const float sepL = 0.5f + 0.5f * stereo, sepR = 0.5f - 0.5f * stereo;
        for (int i = 0; i < n; ++i) { L[i] = 0.0f; R[i] = 0.0f; }
        for (int c = 0; c < numCh; ++c)
        {
            const float gl = kLeftCh[c] ? sepL : sepR, gr = kLeftCh[c] ? sepR : sepL;
            for (int i = 0; i < n; ++i) { L[i] += stems[c][i] * gl; R[i] += stems[c][i] * gr; }
        }
    }
};

// 1 V/oct, 0 V = C-2 (period 428), as MOD Importer maps C-2 to MIDI 60 (Rack: 0 V = C4 = MIDI 60).
inline float periodToVolts (int period) { return period > 0 ? (float) std::log2 (428.0 / (double) period) : 0.0f; }

} // namespace drv
} // namespace modport
