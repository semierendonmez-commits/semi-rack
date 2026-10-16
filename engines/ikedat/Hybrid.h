// Vendored from semierendonmez-commits/ikedat Source/engine/Hybrid.h (JUCE-free). Semi-rack change: wrapped in namespace ikt (one binary, ~35 engines).
// IKEDAT — the hybrid engine.
//
//   data (any file, as bytes) ──► read head ──┬──► JDAT buffers (point / block writes)
//                                            ├──► IKEDAR rows + wavetable signs (seed)
//                                            └──► Pinball matrix / steps (functions)
//
//   master clock (free BPM or host) ── 16ths ──► JDAT tick, IKEDAR step, data head
//                                   └──────────► Pinball ball (sample accurate)
//                                                   └─ 8 trig lanes ─► targets + MIDI
//
// IKEDAR, JDAT and Pinball are the original ports, unchanged in what they do on
// their own; this file only clocks them, routes trigs and feeds them data.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "DataSet.h"
#include "Ikedar.h"
#include "Jdat.h"
#include "Pinball.h"

namespace ikt {

namespace hy
{
// What a Pinball trig lane does inside the instrument (MIDI goes out regardless).
enum Target
{
    tgNone = 0, tgData1, tgData2, tgData3, tgWave, tgSpec, tgSine, tgIkedar,
    tgMutate, tgClear, tgJump, tgSeed, kNumTargets
};
inline const char* targetName (int t)
{
    static const char* n[] = { "none", "data 1", "data 2", "data 3", "wave 1-3", "spec 4-6", "sine 7-9",
                               "ikedar all", "mutate", "clear", "data jump", "data seed" };
    return (t >= 0 && t < kNumTargets) ? n[t] : "?";
}
constexpr int kDefaultTargets[pb::kLanes] = { tgData1, tgData2, tgData3, tgWave, tgSpec, tgSine, tgMutate, tgJump };

enum IkMode { ikPatch = 0, ikSixteenths = 1, ikTrigOnly = 2 };
enum Scan { scanLinear = 0, scanBall = 1, scanRandom = 2, scanHold = 3 };

struct Controls
{
    bool run = true;
    bool hostSync = false;
    double freeBpm = 125.0;            // 125 BPM 16ths = the 120 ms metro of kdr_jdat

    // Pinball
    pb::Controls pin;
    double patternBars = 1.0;          // relaunch the ball every N bars (0 = never)
    int startStep = 1;
    std::array<int, pb::kLanes> laneTarget {};
    std::array<bool, pb::kLanes> laneOn {};

    // IKEDAR
    int ikMode = ikSixteenths;
    ryoz::Engine::Controls ik;
    float ikBus = 1.0f;

    // JDAT
    jdat::Params jd;
    int jdGates = 2;                   // 0 patch, 1 trig, 2 both
    int jdWrite = 1;                   // 0 patch random, 1 data point, 2 data block
    float jdBus = 1.0f;

    // data
    bool useData = true;               // false = the patches' own random numbers
    int scan = scanLinear;
    int scanBytes = 8;                 // bytes the head moves per 16th
    float seedChance = 0.35f;          // IKEDAR mutations read from data
    bool dataSigns = true;

    float master = 1.0f;
    bool clip = true;

    Controls()
    {
        for (int k = 0; k < pb::kLanes; ++k) { laneTarget[(std::size_t) k] = kDefaultTargets[k]; laneOn[(std::size_t) k] = true; }
    }
};

struct Host
{
    bool valid = false, playing = false;
    double bpm = 120.0, ppq = 0.0;
};

//==============================================================================
class Engine
{
public:
    Engine()
    {
        ryoz::Engine::DataHook h;
        h.ctx = this;
        h.bits = &Engine::dataBits;
        ik.setDataHook (h);
        seq.seed (0x51ed27u);
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        ik.prepare (sr);
        jd.prepare (sr);
        seq.prepare (sr);
        freePos16 = 0.0;
        last16 = INT64_MIN;
        wasRolling = false;
    }

    void seed (std::uint32_t s) { jd.seed (s); seq.seed (s * 2654435761u + 1u); rng = s | 1u; }

    // Data: set from the message thread; the DataSet must stay alive until
    // another one has been swapped in and a block has passed (the processor
    // keeps retired sets for a while).
    void setData (const dat::DataSet* d) noexcept { data.store (d, std::memory_order_release); }
    const dat::DataSet* currentData() const noexcept { return data.load (std::memory_order_acquire); }

    void relaunch() noexcept { relaunchRequest.store (true); }
    void jumpHead (std::uint64_t to) noexcept { headRequest.store ((std::int64_t) to); }

    //--------------------------------------------------------------------------
    void process (float* L, float* R, int n, const Host& host, Controls c, pb::FireSink& out)
    {
        const dat::DataSet* d = data.load (std::memory_order_acquire);
        const bool haveData = c.useData && d != nullptr && ! d->empty();
        liveData = haveData ? d : nullptr;

        // data hook settings for this block
        ryoz::Engine::DataHook h;
        h.ctx = this;
        h.bits = &Engine::dataBits;
        h.seedChance = haveData ? c.seedChance : 0.0f;
        h.signs = haveData && c.dataSigns;
        ik.setDataHook (h);

        if (auto hr = headRequest.exchange (-1); hr >= 0) head = (std::uint64_t) hr;

        const bool useHost = c.hostSync && host.valid;
        const double bpm = useHost ? (host.bpm > 0.0 ? host.bpm : 120.0) : std::max (20.0, c.freeBpm);
        const double s16 = sr * 60.0 / bpm / 4.0;
        const double ms16 = 60000.0 / bpm / 4.0;
        const bool rolling = c.run && (! useHost || host.playing);
        bpmView.store ((float) bpm, std::memory_order_relaxed);
        rollingView.store (rolling, std::memory_order_relaxed);

        if (rolling && ! wasRolling) { seq.launch (c.startStep); last16 = INT64_MIN; if (! useHost) freePos16 = 0.0; }
        if (! rolling && wasRolling) { seq.drain(); seq.killPending(); }
        wasRolling = rolling;
        if (relaunchRequest.exchange (false)) seq.launch (c.startStep);

        c.pin.samplesPer16 = s16;
        ik.beginBlock (c.ik);
        jdat::GateSink jsink;
        jd.beginBlock (&jsink);

        const bool ikAudible = c.ikMode == ikPatch ? ik.patchDacOn() : true;
        float peak = 0.0f;
        double sumSq = 0.0;

        for (int i = 0; i < n; ++i)
        {
            // ---- master clock ------------------------------------------------
            double pos16;
            if (useHost) pos16 = (host.ppq + host.bpm / 60.0 * (double) i / sr) * 4.0;
            else pos16 = freePos16;
            if (! useHost && rolling) freePos16 += 1.0 / s16;

            if (rolling)
            {
                const auto idx = (std::int64_t) std::floor (pos16 + 1.0e-9);
                if (idx != last16)
                {
                    
                    last16 = idx;
                    onSixteenth (idx, c, ms16, i, haveData);
                }
                c.pin.pos16 = pos16;
                const int before = out.count;
                seq.sample (i, c.pin, out);
                for (int k = before; k < out.count; ++k) route (out.items[(std::size_t) k], c, ms16, i);
            }

            if (c.ikMode == ikPatch && c.run) ik.internalClockSample();

            // ---- audio -------------------------------------------------------
            float il = 0.0f, ir = 0.0f;
            if (ikAudible) ik.renderSample (il, ir, c.ik);
            float dv[3], dr[2];
            jd.renderSample (dv, dr, c.jd);
            float l = (il * c.ikBus + (dv[0] + dv[1] + dr[0]) * c.jdBus) * c.master;
            float r = (ir * c.ikBus + (dv[1] + dv[2] + dr[1]) * c.jdBus) * c.master;
            if (c.clip) { l = std::clamp (l, -1.0f, 1.0f); r = std::clamp (r, -1.0f, 1.0f); }
            L[i] = l;
            R[i] = r;
            peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
            sumSq += (double) (l * l + r * r) * 0.5;
        }

        ik.endBlock();
        jd.endBlock();
        jdGates = jsink;
        headView.store (head, std::memory_order_relaxed);
        const float prevPeak = peakView.load (std::memory_order_relaxed);
        peakView.store (std::max (peak, prevPeak * 0.85f), std::memory_order_relaxed);
        rmsView.store ((float) std::sqrt (sumSq / std::max (1, n)), std::memory_order_relaxed);
    }

    // ---- parts (the processor and editor talk to them directly) -------------
    ryoz::SharedState ikShared;
    ryoz::Engine ik { ikShared };
    jdat::Engine jd;
    pb::Sequencer seq;
    jdat::GateSink jdGates;                          // JDAT's own gates of the last block (MIDI)

    // ---- views --------------------------------------------------------------
    std::atomic<std::uint64_t> headView { 0 };
    std::atomic<std::uint32_t> sixteenthView { 0 };
    std::atomic<float> bpmView { 125.0f }, peakView { 0.0f }, rmsView { 0.0f };
    std::atomic<bool> rollingView { false };
    std::array<std::atomic<std::uint32_t>, pb::kLanes> laneFlash {};
    std::atomic<std::uint32_t> muteFlash { 0 }, jumpFlash { 0 }, seedFlash { 0 };

    double sampleRate() const { return sr; }

    // what IKEDAR reads from the data (row / signs); public for tests
    static std::uint64_t dataBits (void* ctx, int voice, int what)
    {
        auto* self = static_cast<Engine*> (ctx);
        const auto* d = self->liveData;
        if (d == nullptr) return 0;
        const std::uint64_t bit = self->head * 8u + (std::uint64_t) voice * 64u + (what ? 4099u : 0u);
        return d->bits64 (bit);
    }

private:
    void onSixteenth (std::int64_t idx, const Controls& c, double ms16, int offset, bool haveData)
    {
        sixteenthView.fetch_add (1, std::memory_order_relaxed);
        const auto* d = liveData;

        // data head
        if (haveData)
        {
            const auto size = (std::uint64_t) d->size();
            switch (c.scan)
            {
                case scanLinear: head = (head + (std::uint64_t) std::max (1, c.scanBytes)) % size; break;
                case scanRandom: head = ((std::uint64_t) nextRandom() << 16 ^ nextRandom()) % size; break;
                case scanBall:
                {
                    // each step owns 1/16 of the file; the head walks inside the region of the ball
                    const int b = std::max (1, seq.ball());
                    const auto region = std::max<std::uint64_t> (1, size / pb::kSteps);
                    ballOffset = (ballOffset + (std::uint64_t) std::max (1, c.scanBytes)) % region;
                    head = ((std::uint64_t) (b - 1) * region + ballOffset) % size;
                    break;
                }
                default: break;
            }
        }

        // JDAT: the patch's metro tick, now on the 16ths
        jdat::HybridTick ht;
        ht.data = haveData ? d : nullptr;
        ht.head = head;
        ht.write = c.jdWrite;
        ht.gates = c.jdGates;
        jd.hybridTick (c.jd, idx, ht, offset);

        // IKEDAR on the 16ths
        if (c.ikMode == ikSixteenths) ik.hybridStep (ms16);

        // Pinball: relaunch every PATTERN LENGTH bars
        if (c.patternBars > 0.0)
        {
            const auto len = std::max<std::int64_t> (1, (std::int64_t) std::llround (c.patternBars * 16.0));
            if (((idx % len) + len) % len == 0) seq.launch (c.startStep);
        }
    }

    void route (const pb::Fire& f, const Controls& c, double ms16, int offset)
    {
        if (f.lane < 0)
        {
            muteFlash.fetch_add (1, std::memory_order_relaxed);
            return;
        }
        const auto lane = (std::size_t) f.lane;
        if (! c.laneOn[lane]) return;
        laneFlash[lane].fetch_add (1, std::memory_order_relaxed);
        const float vel = (float) std::clamp (f.velocity, 0, 127) / 127.0f;
        const double gateMs = std::max (1.0, f.durSamples / sr * 1000.0);
        (void) ms16;
        switch (c.laneTarget[lane])
        {
            case tgData1: jd.fire (0, vel, offset); break;
            case tgData2: jd.fire (1, vel, offset); break;
            case tgData3: jd.fire (2, vel, offset); break;
            case tgWave:  ik.fireVoices (0, 3, vel, gateMs); break;
            case tgSpec:  ik.fireVoices (3, 3, vel, gateMs); break;
            case tgSine:  ik.fireVoices (6, 3, vel, gateMs); break;
            case tgIkedar: ik.fireVoices (0, 9, vel, gateMs); break;
            case tgMutate: ik.autoMutateOne(); break;
            case tgClear: jd.clearBuffers(); break;
            case tgJump:
                if (liveData != nullptr) head = ((std::uint64_t) nextRandom() << 16 ^ nextRandom()) % liveData->size();
                jumpFlash.fetch_add (1, std::memory_order_relaxed);
                break;
            case tgSeed:
                if (liveData != nullptr)
                {
                    const int v = (int) (nextRandom() % (std::uint32_t) ryoz::kNumVoices);
                    ikShared.voices[(std::size_t) v].cells.store (dataBits (this, v, 0), std::memory_order_relaxed);
                    seedFlash.fetch_add (1, std::memory_order_relaxed);
                }
                break;
            default: break;
        }
    }

    std::uint32_t nextRandom() noexcept
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return rng;
    }

    double sr = 44100.0;
    std::atomic<const dat::DataSet*> data { nullptr };
    const dat::DataSet* liveData = nullptr;          // this block's data (audio thread)
    std::uint64_t head = 0, ballOffset = 0;
    double freePos16 = 0.0;
    std::int64_t last16 = INT64_MIN;
    bool wasRolling = false;
    std::atomic<bool> relaunchRequest { false };
    std::atomic<std::int64_t> headRequest { -1 };
    std::uint32_t rng = 0x2545f491u;
};

} // namespace hy

} // namespace ikt
