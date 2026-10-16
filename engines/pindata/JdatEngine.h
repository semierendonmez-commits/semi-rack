// Vendored from semierendonmez-commits/pindata Source/engine/JdatEngine.h (JUCE-free). Semi-rack change: wrapped in namespace pnd (one binary, ~35 engines).
// JDAT layer of PINDATA — the JDAT plug-in's sample-level port of kdr_jdat.mxt,
// extended for the hybrid:
//   * clock mode 2 (external): only ticks handed in by the caller fire;
//   * write modes: the patch's random -1/0/+1 writes, or the loaded data
//     written in order (a moving window of the file) or at random positions;
//   * accent: a tick's gate peak can be scaled (pinball velocity);
//   * seedBuffers(): fill the three buffers from data at once.
//
// The patch (Max 2/4 text format) decoded:
//
//   loadbang -> del 1000 -> metro 120 -> s metro             (the only clock)
//
//   p panel   every tick: 40% chance -> "clear" the LCD and broadcast "clear"
//             then draw random(18,21) rows of 71 random digits on the LCD
//
//   p data1/2/3   (one per multiSlider column)
//      buffer~ dataNNN 11.61 ms  (= 512 samples)
//      every tick:  peek~ buf[random(0,512)] = random(0,3)-1      (-1, 0, +1)
//      on "clear":  uzi 512 writes 0 everywhere
//      source = selector~ 3 { cycle~ HZ | p noise * G | wave~ buf <- phasor~ 25 }
//      gate   = line~ "1 a 1 hold 0 a" fired when p mlts says so
//      out    = source * gate * OUT -> send~ dataN
//
//   p mlts  (one per column) every tick:
//      v = 2^random(0,15);  slider colour = min(v,255);  gate when v <= 255
//
//   p noise   carrier cycle~ at B Hz, frequency-modulated by cycle~ at B*R Hz
//             with deviation B*R*L Hz, where L comes from [line 0.] (20 ms grain)
//             that glides over 4 s to random(lo,hi) with 30% chance per tick.
//             Deviations of 0.5..1 MHz alias into a pitched hiss: the "noise".
//
//   p drone   two voices, selector~ 3 { cycle~ 50 | p noise * 0.12 |
//             wave~ data001 / data003 <- phasor~ 100 } * 0.3
//
//   p master  L = data1 + data2 + drone1      R = data2 + data3 + drone2
//
//   menus     loadbang sends 3 (= "data") to every menu; menu 1 also drives menu 3.
//
// Everything that the patch hard-codes is a constant here. The plug-in adds a
// few controls around it (rate, sync, probabilities, levels), all defaulting to
// the patch's own values.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <random>

namespace pnd {

namespace jdat
{

constexpr int kBufLen    = 512;   // buffer~ dataNNN 11.61 (ms @ 44.1 kHz)
constexpr int kNumData   = 3;
constexpr int kLcdCols   = 71;    // uzi 71, x = n * 5
constexpr int kLcdRowMax = 20;    // counter 0 0 19

enum Source { srcNone = 0, srcSine = 1, srcNoise = 2, srcData = 3 };
enum Interp { interpNone = 0, interpLinear = 1, interpCubic = 2 };

//==============================================================================
// Max's expr random(a, b): an integer in [a, b-1].
struct Rng
{
    std::mt19937 g { 0x6a646174u };
    void seed (uint32_t s) { g.seed (s); }
    int range (int a, int b)
    {
        if (b - 1 <= a) return a;
        std::uniform_int_distribution<int> d (a, b - 1);
        return d (g);
    }
};

//==============================================================================
// cycle~ : 512-point cosine table with linear interpolation.
struct CosTable
{
    float t[513];
    CosTable()
    {
        for (int i = 0; i <= 512; ++i)
            t[i] = (float) std::cos (2.0 * 3.14159265358979323846 * (double) i / 512.0);
    }
    float at (double phase) const
    {
        const double x = phase * 512.0;
        int i = (int) x;
        if (i < 0) i = 0;
        if (i > 511) i = 511;
        const float f = (float) (x - (double) i);
        return t[i] + (t[i + 1] - t[i]) * f;
    }
};

inline const CosTable& cosTable()
{
    static const CosTable c;
    return c;
}

struct Cycle
{
    double phase = 0.0;
    void reset() { phase = 0.0; }
    float tick (double hz, double invSr)
    {
        const float out = cosTable().at (phase);
        phase += hz * invSr;
        phase -= std::floor (phase);
        return out;
    }
};

struct Phasor
{
    double phase = 0.0;
    void reset() { phase = 0.0; }
    double tick (double hz, double invSr)
    {
        const double out = phase;
        phase += hz * invSr;
        phase -= std::floor (phase);
        return out;
    }
};

//==============================================================================
// [line 0.] — control-rate ramp, one output every 20 ms grain.
struct ControlLine
{
    float value = 0.0f, start = 0.0f, target = 0.0f;
    int stepsTotal = 0, stepsDone = 0, grain = 882, counter = 0;

    void reset() { value = start = target = 0.0f; stepsTotal = stepsDone = 0; counter = 0; }

    void setTarget (float t, double ms, double sr)
    {
        start = value;
        target = t;
        stepsTotal = std::max (1, (int) std::lround (ms / 20.0));
        stepsDone = 0;
        grain = std::max (1, (int) std::lround (sr * 0.020));
        counter = grain;
    }

    void tick()
    {
        if (stepsDone >= stepsTotal) return;
        if (--counter <= 0)
        {
            counter = grain;
            ++stepsDone;
            value = start + (target - start) * (float) stepsDone / (float) stepsTotal;
        }
    }
};

//==============================================================================
// line~ with a segment list, e.g. "1 0.1 1 40 0 0.1".
struct LineTilde
{
    struct Seg { float target; double ms; };

    std::array<Seg, 3> segs {};
    int nSeg = 0, cur = 0, remain = 0;
    float value = 0.0f, inc = 0.0f;

    void reset() { nSeg = cur = remain = 0; value = inc = 0.0f; }

    void trigger (const std::array<Seg, 3>& s, double sr)
    {
        segs = s;
        nSeg = 3;
        cur = 0;
        startSeg (sr);
    }

    void startSeg (double sr)
    {
        if (cur >= nSeg) { remain = 0; inc = 0.0f; return; }
        remain = std::max (1, (int) std::lround (segs[(size_t) cur].ms * sr / 1000.0));
        inc = (segs[(size_t) cur].target - value) / (float) remain;
    }

    float tick (double sr)
    {
        if (remain > 0)
        {
            value += inc;
            if (--remain == 0)
            {
                value = segs[(size_t) cur].target;
                ++cur;
                startSeg (sr);
            }
        }
        return value;
    }
};

//==============================================================================
// p noise
struct FmNoise
{
    float base = 400.0f, ratio = 13.1f;
    int lo = 100, hi = 200;
    Cycle carrier, modulator;
    ControlLine depth;

    void reset() { carrier.reset(); modulator.reset(); depth.reset(); }

    void onTick (Rng& rng, int jumpProb, double sr)
    {
        if (rng.range (0, 100) < jumpProb)
            depth.setTarget ((float) rng.range (lo, hi), 4000.0, sr);
    }

    float tick (double invSr)
    {
        depth.tick();
        const double modHz = (double) base * ratio;
        const float m = modulator.tick (modHz, invSr);
        const double f = (double) base + (double) m * modHz * (double) depth.value;
        return carrier.tick (f, invSr);
    }
};

//==============================================================================
// wave~ reading a 512-sample buffer.
inline float readWave (const float* buf, double phase, int interp)
{
    const double x = phase * (double) kBufLen;
    int i = (int) x;
    if (i >= kBufLen) i = kBufLen - 1;
    const float f = (float) (x - (double) i);
    auto at = [buf] (int k) { return buf[(k % kBufLen + kBufLen) % kBufLen]; };

    switch (interp)
    {
        case interpNone: return at (i);
        case interpCubic:
        {
            const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            return ((c3 * f + c2) * f + c1) * f + y1;
        }
        default: return at (i) + (at (i + 1) - at (i)) * f;
    }
}

//==============================================================================
struct DataVoice
{
    // constants from the patch
    float sineHz = 12800.0f;
    float noiseGain = 0.12f;
    float outGain = 0.5f;
    std::array<LineTilde::Seg, 3> env {};

    Cycle sine;
    FmNoise noise;
    Phasor phasor;
    LineTilde gate;

    void reset() { sine.reset(); noise.reset(); phasor.reset(); gate.reset(); }

    float tick (const float* buf, int src, int interp, double invSr, double sr, double& phaseOut)
    {
        // every object in the patch runs all the time; selector~ only picks one
        const float s = sine.tick (sineHz, invSr);
        const float n = noise.tick (invSr) * noiseGain;
        phaseOut = phasor.tick (25.0, invSr);
        const float w = readWave (buf, phaseOut, interp);
        const float g = gate.tick (sr);

        float x = 0.0f;
        switch (src)
        {
            case srcSine:  x = s; break;
            case srcNoise: x = n; break;
            case srcData:  x = w; break;
            default: break;
        }
        return x * g * outGain;
    }
};

struct DroneVoice
{
    Cycle sine;
    FmNoise noise;
    Phasor phasor;

    void reset() { sine.reset(); noise.reset(); phasor.reset(); }

    float tick (const float* buf, int src, int interp, double invSr)
    {
        const float s = sine.tick (50.0, invSr);
        const float n = noise.tick (invSr) * 0.12f;
        const float w = readWave (buf, phasor.tick (100.0, invSr), interp);

        float x = 0.0f;
        switch (src)
        {
            case srcSine:  x = s; break;
            case srcNoise: x = n; break;
            case srcData:  x = w; break;
            default: break;
        }
        return x * 0.3f;
    }
};

//==============================================================================
// What the editor needs to know about each metro tick.
struct TickEvent
{
    uint32_t index = 0;
    uint32_t lcdSeed = 0;              // digits for the lcd, so a locked loop repeats visually too
    int16_t loopPos = -1;              // position inside the pattern lock, -1 = not looping
    bool clear = false;
    uint8_t rows = 0;                  // random(18,21)
    std::array<uint8_t, 3> grey {};    // min(2^r, 255)
    std::array<bool, 3> gate {};
};

// Single-producer / single-consumer ring (audio thread -> editor).
template <typename T, int N>
struct SpscRing
{
    std::array<T, N> items {};
    std::atomic<uint32_t> w { 0 }, r { 0 };

    void push (const T& t)
    {
        const uint32_t wi = w.load (std::memory_order_relaxed);
        if (wi - r.load (std::memory_order_acquire) >= (uint32_t) N) return;   // full: drop
        items[wi % N] = t;
        w.store (wi + 1, std::memory_order_release);
    }
    bool pop (T& t)
    {
        const uint32_t ri = r.load (std::memory_order_relaxed);
        if (ri == w.load (std::memory_order_acquire)) return false;
        t = items[ri % N];
        r.store (ri + 1, std::memory_order_release);
        return true;
    }
};

//==============================================================================
// Supplies the next data value (-1..1) for the data write modes.
struct SymbolSource
{
    virtual ~SymbolSource() = default;
    virtual float next() noexcept = 0;
};

enum WriteMode { writeRandom = 0, writeDataSeq = 1, writeDataScatter = 2 };

struct ExtTick { int offset; float accent; };

struct Params
{
    bool run = true;
    bool sync = false;
    bool external = false;             // hybrid: ticks come only from `ext`
    const ExtTick* ext = nullptr;
    int numExt = 0;
    int writeMode = writeRandom;
    SymbolSource* symbols = nullptr;
    double rateMs = 120.0;
    double divBeats = 0.25;            // 1/16
    std::array<int, 4> src { srcData, srcData, srcData, srcData };   // d1 d2 d3 drone
    std::array<float, 4> level { 1.0f, 1.0f, 1.0f, 1.0f };
    float master = 1.0f;
    bool clip = true;
    int clearProb = 40;
    int jumpProb = 30;
    bool hold = false;
    int interp = interpLinear;
    int loopTicks = 0;                 // pattern lock length in ticks, 0 = off
};

// Gates and clears as they happen, for MIDI out. offset = sample in the process() call.
struct GateOut { int offset; int column; uint8_t grey; };   // column 3 = clear

struct GateSink
{
    std::array<GateOut, 256> items {};
    int count = 0;
    void add (const GateOut& g) { if (count < (int) items.size()) items[(size_t) count++] = g; }
};

struct Transport
{
    bool playing = false;
    double ppq = 0.0;
    double bpm = 120.0;
    bool valid = false;
};

//==============================================================================
class Engine
{
public:
    Engine()
    {
        // p data1 / p data2 / p data3
        const float sines[3]  = { 12800.0f, 3200.0f, 12800.0f };
        const float ngain[3]  = { 0.12f, 0.15f, 0.12f };
        const float outg[3]   = { 0.5f, 0.25f, 0.5f };
        const double att[3]   = { 0.1, 0.2, 0.2 };
        const double hold[3]  = { 40.0, 70.0, 40.0 };
        for (int i = 0; i < 3; ++i)
        {
            auto& v = data[(size_t) i];
            v.sineHz = sines[i];
            v.noiseGain = ngain[i];
            v.outGain = outg[i];
            v.env = { LineTilde::Seg { 1.0f, att[i] }, LineTilde::Seg { 1.0f, hold[i] }, LineTilde::Seg { 0.0f, att[i] } };
            v.noise.base = 533.0f; v.noise.ratio = 17.0f; v.noise.lo = 200; v.noise.hi = 500;
        }
        // p drone: noise at (263,131) feeds drone1, (376,131) feeds drone2
        drone[0].noise.base = 400.0f; drone[0].noise.ratio = 13.1f; drone[0].noise.lo = 100; drone[0].noise.hi = 500;
        drone[1].noise.base = 400.0f; drone[1].noise.ratio = 13.1f; drone[1].noise.lo = 100; drone[1].noise.hi = 200;

        for (auto& b : buffers) b.fill (0.0f);
        for (auto& b : bufView) for (auto& x : b) x.store (0, std::memory_order_relaxed);
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        invSr = 1.0 / sr;
        reset();
    }

    void reset()
    {
        for (auto& v : data) v.reset();
        for (auto& d : drone) d.reset();
        samplesToTick = 0.0;
        wasRunning = false;
        lastSyncIndex = INT64_MIN;
        freeIndex = 0;
        snap.valid = false;
    }

    void seed (uint32_t s) { rng.seed (s); }

    // Fire the next tick at the next sample (metro "bang on start").
    void restartClock() { samplesToTick = 0.0; wasRunning = false; lastSyncIndex = INT64_MIN; forceTick = true; }

    // Renders n samples. Out buffers are overwritten.
    void process (float* outL, float* outR, int n, const Params& p, const Transport& t, GateSink* sink = nullptr)
    {
        sinkNow = sink;
        extPos = 0;
        const double ppqPerSample = t.bpm / 60.0 * invSr;
        const double div = std::max (1.0e-4, p.divBeats);

        for (int i = 0; i < n; ++i)
        {
            // ---- clock -------------------------------------------------------
            while (extPos < p.numExt && p.ext[extPos].offset <= i)
            {
                sampleNow = i;
                accentNow = p.ext[extPos].accent;
                doTick (p, freeIndex++);
                accentNow = 1.0f;
                ++extPos;
            }
            if (p.external)
            {
                // internal clock silent
            }
            else if (p.run)
            {
                if (p.sync)
                {
                    if (t.valid && t.playing)
                    {
                        const double ppq = t.ppq + ppqPerSample * (double) i;
                        const int64_t idx = (int64_t) std::floor (ppq / div + 1.0e-9);
                        if (idx != lastSyncIndex || forceTick)
                        {
                            lastSyncIndex = idx;
                            forceTick = false;
                            sampleNow = i;
                            doTick (p, idx);
                        }
                    }
                    else
                    {
                        lastSyncIndex = INT64_MIN;
                    }
                }
                else
                {
                    if (! wasRunning || forceTick) { samplesToTick = 0.0; forceTick = false; freeIndex = 0; }
                    if (samplesToTick <= 0.0)
                    {
                        sampleNow = i;
                        doTick (p, freeIndex++);
                        samplesToTick += std::max (1.0, p.rateMs * 0.001 * sr);
                    }
                    samplesToTick -= 1.0;
                }
            }
            wasRunning = p.run;

            // ---- audio -------------------------------------------------------
            double ph = 0.0;
            float d[3];
            for (int k = 0; k < 3; ++k)
            {
                d[k] = data[(size_t) k].tick (buffers[(size_t) k].data(), p.src[(size_t) k], p.interp, invSr, sr, ph) * p.level[(size_t) k];
                if (k == 0) phase25.store ((float) ph, std::memory_order_relaxed);
            }
            const float dr1 = drone[0].tick (buffers[0].data(), p.src[3], p.interp, invSr) * p.level[3];
            const float dr2 = drone[1].tick (buffers[2].data(), p.src[3], p.interp, invSr) * p.level[3];

            float l = (d[0] + d[1] + dr1) * p.master;
            float r = (d[1] + d[2] + dr2) * p.master;
            if (p.clip)
            {
                l = std::clamp (l, -1.0f, 1.0f);
                r = std::clamp (r, -1.0f, 1.0f);
            }
            outL[i] = l;
            outR[i] = r;
        }

        for (int k = 0; k < 3; ++k)
            gateView[(size_t) k].store (data[(size_t) k].gate.value, std::memory_order_relaxed);
        sinkNow = nullptr;
    }

    // forget a captured pattern lock (e.g. after a preset load)
    void dropLoop() { snap.valid = false; }

    // hybrid: fill the buffers with data at once (message or audio thread, between blocks)
    void seedBuffers (SymbolSource& s)
    {
        for (int b = 0; b < 3; ++b)
        {
            for (int k = 0; k < kBufLen; ++k) writeBuf (b, k, s.next());
            writePos[(size_t) b] = 0;
        }
        snap.valid = false;
    }
    void clearBuffers()
    {
        for (int b = 0; b < 3; ++b) for (int k = 0; k < kBufLen; ++k) writeBuf (b, k, 0.0f);
    }

    // ---- state the editor reads --------------------------------------------
    SpscRing<TickEvent, 128> events;
    std::array<std::array<std::atomic<int8_t>, kBufLen>, 3> bufView;
    std::array<std::atomic<float>, 3> gateView {};
    std::atomic<float> phase25 { 0.0f };
    std::atomic<uint32_t> tickCount { 0 };

    // ---- exposed for tests --------------------------------------------------
    const std::array<float, kBufLen>& buffer (int i) const { return buffers[(size_t) i]; }
    const DataVoice& dataVoice (int i) const { return data[(size_t) i]; }
    const DroneVoice& droneVoice (int i) const { return drone[(size_t) i]; }
    double sampleRate() const { return sr; }

private:
    void writeBuf (int b, int idx, float v)
    {
        buffers[(size_t) b][(size_t) idx] = v;
        bufView[(size_t) b][(size_t) idx].store ((int8_t) std::lround (std::clamp (v, -1.0f, 1.0f) * 127.0f), std::memory_order_relaxed);
    }

    // Pattern lock: at every loop boundary the first pass captures everything
    // the tick decisions depend on (RNG, buffers, noise glides); later passes
    // restore it, so the same ticks come back in the same order.
    struct Snapshot
    {
        bool valid = false;
        int len = 0;
        std::mt19937 rng;
        std::array<std::array<float, kBufLen>, 3> buffers {};
        std::array<ControlLine, 5> lines {};
    };

    std::array<ControlLine*, 5> noiseLines()
    {
        return { &data[0].noise.depth, &data[1].noise.depth, &data[2].noise.depth, &drone[0].noise.depth, &drone[1].noise.depth };
    }

    int handleLoop (const Params& p, int64_t gridIndex)
    {
        if (p.loopTicks <= 0) { snap.valid = false; return -1; }
        const int len = p.loopTicks;
        const int pos = (int) (((gridIndex % len) + len) % len);
        if (pos == 0)
        {
            if (snap.valid && snap.len == len)
            {
                rng.g = snap.rng;
                for (int b = 0; b < 3; ++b)
                    for (int k = 0; k < kBufLen; ++k)
                        writeBuf (b, k, snap.buffers[(size_t) b][(size_t) k]);
                auto ls = noiseLines();
                for (int k = 0; k < 5; ++k) *ls[(size_t) k] = snap.lines[(size_t) k];
            }
            else
            {
                snap.valid = true;
                snap.len = len;
                snap.rng = rng.g;
                snap.buffers = buffers;
                auto ls = noiseLines();
                for (int k = 0; k < 5; ++k) snap.lines[(size_t) k] = *ls[(size_t) k];
            }
        }
        return (snap.valid && snap.len == len) ? pos : -1;
    }

    void doTick (const Params& p, int64_t gridIndex)
    {
        TickEvent ev;
        ev.index = tickCount.fetch_add (1, std::memory_order_relaxed) + 1;
        ev.loopPos = (int16_t) handleLoop (p, gridIndex);
        ev.lcdSeed = (uint32_t) rng.g();

        // p panel: [if random(0,100)<40 then bang] -> clear, s clear
        ev.clear = rng.range (0, 100) < p.clearProb;
        ev.rows = (uint8_t) rng.range (18, 21);
        if (ev.clear && ! p.hold)
            for (int b = 0; b < 3; ++b)
                for (int k = 0; k < kBufLen; ++k)
                    writeBuf (b, k, 0.0f);

        // p dataN: peek~ buf[random(0,512)] = random(0,3)-1
        if (! p.hold)
            for (int b = 0; b < 3; ++b)
            {
                float v = (float) (rng.range (0, 3) - 1);   // right outlet of [b] fires first
                int idx = rng.range (0, kBufLen);
                if (p.writeMode != writeRandom && p.symbols != nullptr)
                {
                    // hybrid: the file instead of the dice. The random draws above still
                    // happen so a pattern lock loops identically in every mode.
                    v = p.symbols->next();
                    if (p.writeMode == writeDataSeq)
                    {
                        idx = writePos[(size_t) b];
                        writePos[(size_t) b] = (writePos[(size_t) b] + 1) % kBufLen;
                    }
                }
                writeBuf (b, idx, v);
            }

        // p mlts: v = 2^random(0,15), grey = min(v,255), gate if v <= 255
        for (int c = 0; c < 3; ++c)
        {
            const int r = rng.range (0, 15);
            const int v = 1 << r;
            ev.grey[(size_t) c] = (uint8_t) std::min (v, 255);
            ev.gate[(size_t) c] = v <= 255;
            if (ev.gate[(size_t) c])
            {
                auto env = data[(size_t) c].env;
                for (auto& s : env) s.target *= accentNow;
                data[(size_t) c].gate.trigger (env, sr);
                if (sinkNow) sinkNow->add ({ sampleNow, c, ev.grey[(size_t) c] });
            }
        }
        if (ev.clear && sinkNow) sinkNow->add ({ sampleNow, 3, 0 });

        // five p noise instances
        for (auto& v : data) v.noise.onTick (rng, p.jumpProb, sr);
        for (auto& d : drone) d.noise.onTick (rng, p.jumpProb, sr);

        events.push (ev);
    }

    double sr = 44100.0, invSr = 1.0 / 44100.0;
    Rng rng;
    std::array<DataVoice, 3> data;
    std::array<DroneVoice, 2> drone;
    std::array<std::array<float, kBufLen>, 3> buffers {};

    double samplesToTick = 0.0;
    bool wasRunning = false, forceTick = false;
    int64_t lastSyncIndex = INT64_MIN;
    int64_t freeIndex = 0;
    Snapshot snap;
    GateSink* sinkNow = nullptr;
    int sampleNow = 0;
    int extPos = 0;
    float accentNow = 1.0f;
    std::array<int, 3> writePos {};
};

} // namespace jdat

} // namespace pnd
