// Vendored from semierendonmez-commits ikedar Source/Engine.cpp (Rack / MetaModule port).
// Changes: namespace ryoz -> ikedar_ln, Engine::externalStep() added at the end.
#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ikedar_ln
{
// Voice table, read straight from [p alvoton1..9]:
//   osc       : phasor~ -> wave~ (1-3), the same into pfft~ fltr1..3 (4-6), cycle~ (7-9)
//   curve     : the expr feeding [* 0.] right inlet
//   outGain   : [*~ 0.5] or [*~ 1]
//   routing   : which of [send~ alvomstrL] / [send~ alvomstrR] are connected
//   savedRule : contents of the message box in [p 10to2in8bit] at save time
const VoiceSpec kVoiceSpecs[kNumVoices] = {
    { Osc::Wavetable,         AmpCurve::Squared, 0.5f, true,  true,  27, "WAVE" },
    { Osc::Wavetable,         AmpCurve::Squared, 1.0f, true,  false, 14, "WAVE" },
    { Osc::Wavetable,         AmpCurve::Squared, 1.0f, false, true,  13, "WAVE" },
    { Osc::WavetableSpectral, AmpCurve::Squared, 0.5f, true,  true,  17, "SPEC" },
    { Osc::WavetableSpectral, AmpCurve::Squared, 1.0f, true,  false, 15, "SPEC" },
    { Osc::WavetableSpectral, AmpCurve::Linear3, 1.0f, false, true,  18, "SPEC" },
    { Osc::Sine,              AmpCurve::Linear3, 0.5f, true,  true,  22, "SINE" },
    { Osc::Sine,              AmpCurve::Linear3, 1.0f, true,  false, 15, "SINE" },
    { Osc::Sine,              AmpCurve::Linear3, 1.0f, false, true,  22, "SINE" },
};

static inline int popcount64 (std::uint64_t x) noexcept
{
    int n = 0;
    while (x) { x &= x - 1; ++n; }
    return n;
}

int SharedState::masterSum() const noexcept
{
    int n = 0;
    for (auto& w : master) n += popcount64 (w.load (std::memory_order_relaxed));
    return n;
}

//==============================================================================
void LineEnvelope::start (double amp, double gateMs, double sampleRate) noexcept
{
    const double msToSamples = sampleRate / 1000.0;
    segs[0] = { amp, 0.1 * msToSamples };
    segs[1] = { amp, std::max (0.0, gateMs) * msToSamples };
    segs[2] = { 0.0, 1.0 * msToSamples };
    numSegs = 3;
    seg = 0;
    beginSegment();
}

void LineEnvelope::beginSegment() noexcept
{
    while (seg < numSegs)
    {
        const double n = segs[seg].samples;
        if (n < 1.0) { value = segs[seg].target; ++seg; continue; }  // zero-length ramp = jump
        remaining = n;
        inc = (segs[seg].target - value) / n;
        return;
    }
}

//==============================================================================
SpectralMask::SpectralMask()
{
    constexpr double twoPi = 6.283185307179586;
    for (int n = 0; n < N; ++n)
        window[n] = (float) (0.5 - 0.5 * std::cos (twoPi * n / N));   // Max "hanning", periodic
    for (int k = 0; k < N / 2; ++k)
    {
        cosT[k] = std::cos (twoPi * k / N);
        sinT[k] = std::sin (twoPi * k / N);
    }
    int bits = 0;
    while ((1 << bits) < N) ++bits;
    for (int i = 0; i < N; ++i)
    {
        int r = 0;
        for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        bitrev[i] = r;
    }
    reset();
}

void SpectralMask::reset() noexcept
{
    std::fill (std::begin (inHist), std::end (inHist), 0.0f);
    std::fill (std::begin (inNew), std::end (inNew), 0.0f);
    std::fill (std::begin (outCur), std::end (outCur), 0.0f);
    std::fill (std::begin (ola), std::end (ola), 0.0f);
    pos = 0;
}

void SpectralMask::fft (bool inverse) noexcept
{
    for (int i = 0; i < N; ++i)
        if (i < bitrev[i]) { std::swap (re[i], re[bitrev[i]]); std::swap (im[i], im[bitrev[i]]); }

    for (int len = 2; len <= N; len <<= 1)
    {
        const int half = len / 2, stride = N / len;
        for (int i = 0; i < N; i += len)
            for (int j = 0; j < half; ++j)
            {
                const double wr = cosT[j * stride];
                const double wi = inverse ? sinT[j * stride] : -sinT[j * stride];
                const int a = i + j, b = a + half;
                const double tr = re[b] * wr - im[b] * wi;
                const double ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr;        im[a] += ti;
            }
    }
}

void SpectralMask::frame() noexcept
{
    std::memmove (inHist, inHist + Hop, sizeof (float) * Hop);
    std::memcpy (inHist + Hop, inNew, sizeof (float) * Hop);

    for (int n = 0; n < N; ++n) { re[n] = inHist[n] * window[n]; im[n] = 0.0; }
    fft (false);

    // fftin~ delivers bins 0..63; each is multiplied by fltrN[bin] (0 or 1).
    for (int k = 0; k < N / 2; ++k)
        if (((mask >> k) & 1u) == 0) { re[k] = 0.0; im[k] = 0.0; }
    re[N / 2] = im[N / 2] = 0.0;
    for (int k = 1; k < N / 2; ++k) { re[N - k] = re[k]; im[N - k] = -im[k]; }

    fft (true);

    // Hann analysis * Hann synthesis at overlap 2 averages 0.75 — normalise to unity
    // on average and keep the hop-rate ripple pfft~ produces with these settings.
    constexpr double scale = 1.0 / (N * 0.75);
    for (int n = 0; n < N; ++n) ola[n] += (float) (re[n] * scale) * window[n];

    std::memcpy (outCur, ola, sizeof (float) * Hop);
    std::memmove (ola, ola + Hop, sizeof (float) * Hop);
    std::fill (ola + Hop, ola + N, 0.0f);
}

float SpectralMask::process (float x) noexcept
{
    inNew[pos] = x;
    const float y = outCur[pos];
    if (++pos == Hop) { pos = 0; frame(); }
    return y;
}

//==============================================================================
Engine::Engine (SharedState& s) : shared (s) {}

void Engine::prepare (double sampleRate)
{
    sr = sampleRate > 0 ? sampleRate : 44100.0;
    for (auto& v : voices)
    {
        v.env.reset();
        v.spectral.reset();
        v.phase = 0.0;
        v.lastPitch = v.lastCells = v.lastGain = v.lastDecay = ~0ull;
    }
    for (auto& w : lastMaster) w = ~0ull;
    seenLoadSerial = ~0u;   // force a full resync on the first block
    lastHostQ = -1.0e9;
}

double Engine::pitchToHz (int n) noexcept { return std::pow (n / 50.0, 4.0) * 30000.0 + 20.0; }

double Engine::ampScale (int voice, int n) noexcept
{
    return kVoiceSpecs[voice].curve == AmpCurve::Squared ? std::pow (n / 25.0, 2.0) : n / 50.0 * 3.0;
}

double Engine::stepMsForMasterSum (int sum) noexcept
{
    return sum > 0 ? 60000.0 / (sum * 2.0) * 0.25 : 0.0;
}

std::uint32_t Engine::nextRandom() noexcept
{
    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
    return (std::uint32_t) (rng >> 32);
}

// [p cellam]: iter -> [bucket 2] + [pack 0 0 0] -> [zl rev] -> [p carule] -> [thresh]
// -> [route 0 1] -> append (first element moved to the end).
std::uint64_t Engine::caGeneration (std::uint64_t row, int rule, CarryState& carry) noexcept
{
    std::uint64_t out = 0;
    int bm2 = carry.c2, bm1 = carry.c1;
    for (int k = 0; k < kCells; ++k)
    {
        const int r = (int) ((row >> k) & 1u);
        const int nb = (bm2 << 2) | (bm1 << 1) | r;     // (left, centre, right)
        out |= (std::uint64_t) ((rule >> nb) & 1) << k;
        bm2 = bm1;
        bm1 = r;
    }
    carry.c2 = bm2;   // bucket keeps the last two cells for the next generation
    carry.c1 = bm1;
    return (out >> 1) | ((out & 1u) << 63);
}

void Engine::regenerateTable (int v, std::uint64_t cells, bool newSigns) noexcept
{
    auto& vs = shared.voices[(std::size_t) v];
    std::uint64_t signs;
    if (newSigns)
    {
        signs = ((std::uint64_t) nextRandom() << 32) | nextRandom();
        vs.signs.store (signs, std::memory_order_relaxed);
    }
    else
    {
        signs = vs.signs.load (std::memory_order_relaxed);
    }

    // [p topeek]: peek~ alvNN i  (bit * (random(0,2)*2-1))
    auto& vo = voices[(std::size_t) v];
    for (int i = 0; i < kCells; ++i)
    {
        const bool on = ((cells >> i) & 1u) != 0;
        const bool pos = ((signs >> i) & 1u) != 0;
        vo.table[i] = on ? (pos ? 1.0f : -1.0f) : 0.0f;
    }
    vo.spectral.setMask (cells);   // voices 4-6: peek~ fltrN i bit
}

void Engine::mutate (int v) noexcept
{
    auto& vs = shared.voices[(std::size_t) v];
    auto& vo = voices[(std::size_t) v];
    const int rule = vs.rule.load (std::memory_order_relaxed);

    std::uint64_t oldRow = vs.cells.load (std::memory_order_relaxed), newRow;
    CarryState c;
    do
    {
        c = vo.carry;
        newRow = caGeneration (oldRow, rule, c);
    } while (! vs.cells.compare_exchange_weak (oldRow, newRow, std::memory_order_relaxed));
    vo.carry = c;

    vs.lastRule.store (rule, std::memory_order_relaxed);
    vs.generation.fetch_add (1, std::memory_order_relaxed);

    // [t b b]: the random rule reaches [p rule1] ~10 ms later (through [thresh]),
    // so the freshly drawn rule is the one used by the NEXT generation.
    vs.rule.store (kRulePool[nextRandom() % 10u], std::memory_order_relaxed);

    regenerateTable (v, newRow, true);
    vo.lastCells = newRow;
}

void Engine::onMasterChanged (int sum) noexcept
{
    dacOn = sum > 10;                         // [if $i1>10 then 1 else 0] -> dac~
    if (sum > 25)                             // [if $i1>25 then $i1] / [if $i1>25 then 1 else 0]
    {
        bpmms = stepMsForMasterSum (sum);     // [* 2.] -> [expr 60000/$f1*0.25] -> [s bpmms]
        metroPeriod = bpmms * sr / 1000.0;
        swingDelay = bpmms * swingFrac * sr / 1000.0;   // [expr $f1*12*0.01] -> [del]
        metroOn = true;
        metroCountdown = 0.0;                 // a running metro restarts on every new "1"
        shared.bpm.store ((float) (sum * 2), std::memory_order_relaxed);
    }
    else
    {
        metroOn = false;
    }
}

void Engine::pollState() noexcept
{
    const auto serial = shared.loadSerial.load (std::memory_order_acquire);
    const bool resync = serial != seenLoadSerial;
    seenLoadSerial = serial;

    bool masterChanged = resync;
    for (int w = 0; w < kMasterWords; ++w)
    {
        const auto m = shared.master[(std::size_t) w].load (std::memory_order_relaxed);
        if (m != lastMaster[w]) { lastMaster[w] = m; masterChanged = true; }
    }
    if (masterChanged) onMasterChanged (shared.masterSum());

    for (int v = 0; v < kNumVoices; ++v)
    {
        auto& vs = shared.voices[(std::size_t) v];
        auto& vo = voices[(std::size_t) v];

        const auto p = vs.pitch.load (std::memory_order_relaxed);
        if (p != vo.lastPitch || resync) { vo.lastPitch = p; vo.hz = pitchToHz (popcount64 (p)); }

        const auto g = vs.gain.load (std::memory_order_relaxed);
        if (g != vo.lastGain || resync) { vo.lastGain = g; vo.amp = ampScale (v, popcount64 (g)); }

        const auto d = vs.decay.load (std::memory_order_relaxed);
        if (d != vo.lastDecay || resync)
        {
            vo.lastDecay = d;
            vo.gateFrac = 1.0 - popcount64 (d) / 25.0;
            // [expr (1-($f1/25.))*$f2]: $f2 is a cold inlet fed by [r bpmms], so the gate
            // length in ms is fixed at the moment the DECAY row is edited.
            if (! resync) vs.gateBaseMs.store ((float) bpmms, std::memory_order_relaxed);
        }

        const auto c = vs.cells.load (std::memory_order_relaxed);
        if (c != vo.lastCells || resync) { vo.lastCells = c; regenerateTable (v, c, ! resync); }

        const auto req = vs.mutateRequests.load (std::memory_order_relaxed);
        if (resync)
        {
            vo.seenMutations = req;
            vo.carry = {};
        }
        int guard = 0;
        if (! hold)
            while (vo.seenMutations != req && guard++ < 64) { mutate (v); ++vo.seenMutations; }
        vo.seenMutations = req;
    }
}

void Engine::triggerStep (int stepIndex, double hostStepMs, bool hostSync) noexcept
{
    shared.step.store (stepIndex, std::memory_order_relaxed);
    for (int v = 0; v < kNumVoices; ++v)
    {
        auto& vs = shared.voices[(std::size_t) v];
        auto& vo = voices[(std::size_t) v];
        // [r alvclock] -> [coll alv_NN] -> [* amp] -> [pack 0. gate] -> line~
        const bool on = ((vs.cells.load (std::memory_order_relaxed) >> stepIndex) & 1u) != 0;
        const double base = hostSync ? hostStepMs : (double) vs.gateBaseMs.load (std::memory_order_relaxed);
        vo.env.start (on ? vo.amp : 0.0, vo.gateFrac * base, sr);
    }
}

// Auto-mutate (from the other plug-ins' "evolve"): every N steps one unlocked voice
// may take a generation, as if its MUTATE row had been stroked.
void Engine::onStep() noexcept
{
    if (hold || autoEvery <= 0) return;
    if (++autoCounter < autoEvery) return;
    autoCounter = 0;
    if ((nextRandom() % 10000u) >= (std::uint32_t) (autoChance * 10000.0f)) return;
    int candidates[kNumVoices], n = 0;
    for (int v = 0; v < kNumVoices; ++v)
        if (! locked[(std::size_t) v]) candidates[n++] = v;
    if (n > 0) mutate (candidates[nextRandom() % (std::uint32_t) n]);
}

void Engine::advanceStep() noexcept
{
    step = (step + 1) & (kCells - 1);
    triggerStep (step, 0.0, false);
    onStep();
}

inline float Engine::renderVoice (int v) noexcept
{
    auto& vo = voices[(std::size_t) v];
    const auto& spec = kVoiceSpecs[v];
    const float e = vo.env.tick();
    float y;

    if (spec.osc == Osc::Sine)
    {
        vo.phase += vo.hz / sr;                       // cycle~ at the full frequency
        vo.phase -= std::floor (vo.phase);
        y = (float) std::cos (6.283185307179586 * vo.phase);
    }
    else
    {
        vo.phase += vo.hz * (2.0 / 64.0) / sr;        // [expr ($f1/64.)*2.] -> phasor~
        vo.phase -= std::floor (vo.phase);
        const double pos = vo.phase * kCells;         // wave~ over the 64-sample buffer
        const int i0 = (int) pos;
        const float frac = (float) (pos - i0);
        const float a = vo.table[i0 & (kCells - 1)], b = vo.table[(i0 + 1) & (kCells - 1)];
        y = a + frac * (b - a);
    }

    y *= e;
    if (spec.osc == Osc::WavetableSpectral) y = vo.spectral.process (y);
    return y * spec.outGain;
}

void Engine::process (float* left, float* right, int numSamples, const HostInfo& host, const Controls& ctl) noexcept
{
    hold = ctl.hold;
    autoEvery = ctl.autoEvery;
    autoChance = ctl.autoChance;
    if (std::abs (ctl.swing - swingFrac) > 1.0e-9)
    {
        swingFrac = ctl.swing;
        swingDelay = bpmms * swingFrac * sr / 1000.0;
    }

    pollState();

    const bool useHost = ctl.hostSync && host.valid;   // no transport (e.g. standalone) -> internal clock

    if (restartPending)
    {
        restartPending = false;
        step = -1;
        swingToggle = 0;
        swingPending = false;
        if (metroOn) metroCountdown = 0.0;
    }

    // Host clock: steps of divBeats from the transport, odd steps late by `swing`.
    constexpr int maxEvents = 64;
    int evOffset[maxEvents], evStep[maxEvents], numEv = 0;
    double hostStepMs = 0.0;

    if (useHost && host.playing && host.bpm > 0.0 && ctl.run)
    {
        const double div = std::max (1.0e-3, ctl.divBeats);
        hostStepMs = 60000.0 / host.bpm * div;
        const double dq = host.bpm / 60.0 / div / sr;          // steps per sample
        const double q0 = host.ppq / div, q1 = q0 + dq * numSamples;
        for (long long k = (long long) std::floor (q0) - 1; k <= (long long) std::floor (q1) + 1 && numEv < maxEvents; ++k)
        {
            const double t = (double) k + ((k & 1) ? ctl.swing : 0.0);
            if (t >= q0 - 1.0e-7 && t < q1 - 1.0e-7)   // same epsilon on both edges: no double or missed steps
            {
                evOffset[numEv] = std::min (numSamples - 1, (int) std::floor (std::max (0.0, t - q0) / dq));
                evStep[numEv] = (int) (((k % kCells) + kCells) % kCells);
                ++numEv;
            }
        }
        shared.bpm.store ((float) host.bpm, std::memory_order_relaxed);
        shared.clockRunning.store (true, std::memory_order_relaxed);
    }
    else if (useHost)
    {
        shared.clockRunning.store (false, std::memory_order_relaxed);
    }
    else
    {
        shared.clockRunning.store (metroOn && ctl.run, std::memory_order_relaxed);
    }

    const bool audible = useHost ? true : dacOn;
    shared.dacOn.store (audible, std::memory_order_relaxed);

    int ev = 0;
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        if (useHost)
        {
            while (ev < numEv && evOffset[ev] == i)
            {
                triggerStep (evStep[ev], hostStepMs, true);
                onStep();
                ++ev;
            }
        }
        else if (ctl.run)
        {
            if (metroOn && metroPeriod > 0.0)
            {
                metroCountdown -= 1.0;
                while (metroCountdown <= 0.0)
                {
                    const int out = swingToggle;     // [counter 0 0 1] -> [sel 0 1]
                    swingToggle ^= 1;
                    if (out == 0) advanceStep();
                    else { swingPending = true; swingCountdown = swingDelay; }
                    metroCountdown += metroPeriod;
                }
            }
            if (swingPending)
            {
                swingCountdown -= 1.0;
                if (swingCountdown <= 0.0) { swingPending = false; advanceStep(); }
            }
        }

        float l = 0.0f, r = 0.0f;
        if (audible)   // dac~ off stops the whole DSP chain, oscillators freeze
        {
            for (int v = 0; v < kNumVoices; ++v)
            {
                const float y = renderVoice (v) * ctl.voiceGain[(std::size_t) v];
                if (kVoiceSpecs[v].toLeft) l += y;
                if (kVoiceSpecs[v].toRight) r += y;
            }
        }

        if (ctl.clip)   // the DAC clips at full scale
        {
            l = std::clamp (l, -1.0f, 1.0f);
            r = std::clamp (r, -1.0f, 1.0f);
        }
        left[i] = l;
        right[i] = r;
        peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
    }

    for (int v = 0; v < kNumVoices; ++v)
        shared.voices[(std::size_t) v].envLevel.store ((float) voices[(std::size_t) v].env.value, std::memory_order_relaxed);
    const float prevPeak = shared.peak.load (std::memory_order_relaxed);
    shared.peak.store (std::max (peak, prevPeak * 0.9f), std::memory_order_relaxed);
}

void Engine::externalStep (double stepMs, bool restart) noexcept
{
    if (restart) { step = -1; swingToggle = 0; swingPending = false; }
    step = (step + 1) & (kCells - 1);
    triggerStep (step, stepMs, true);
    onStep();
}
} // namespace ikedar_ln
