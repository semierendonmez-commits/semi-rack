#include "Engine.h"

#include <algorithm>
#include <climits>
#include <cmath>

namespace gtbs
{

namespace
{
// Where each class likes to sit in a bar of sixteenths (jungle / DnB grammar:
// kick on 1 and the "and" of 3, snare on 2 and 4, ghosts on the e/a, hats on 8ths).
constexpr float priorTable[NumCls][16] = {
    { 1.00f, 0.15f, 0.35f, 0.10f, 0.05f, 0.10f, 0.25f, 0.10f, 0.30f, 0.10f, 0.85f, 0.20f, 0.05f, 0.10f, 0.30f, 0.15f },
    { 0.05f, 0.10f, 0.10f, 0.15f, 1.00f, 0.15f, 0.20f, 0.30f, 0.10f, 0.30f, 0.10f, 0.20f, 1.00f, 0.20f, 0.30f, 0.25f },
    { 0.05f, 0.60f, 0.30f, 0.60f, 0.05f, 0.60f, 0.30f, 0.60f, 0.25f, 0.60f, 0.20f, 0.60f, 0.05f, 0.60f, 0.35f, 0.60f },
    { 0.20f, 0.30f, 0.60f, 0.30f, 0.15f, 0.30f, 0.60f, 0.30f, 0.50f, 0.30f, 0.60f, 0.30f, 0.15f, 0.30f, 0.60f, 0.30f },
};

// transparent below -2 dBFS, then a smooth knee that never passes 0 dBFS
inline float softClip (float x) noexcept
{
    const float a = std::abs (x);
    if (a <= 0.8f) return x;
    const float y = 0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f);
    return x < 0.0f ? -y : y;
}

// the generator's random stream: splitmix64, seeded per row (no 5 KB mt19937 state to seed 64 times)
struct RowRng
{
    uint64_t s;
    explicit RowRng (uint32_t seed) noexcept : s (0x9E3779B97F4A7C15ull ^ seed) {}
    uint32_t next() noexcept
    {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return (uint32_t) ((z ^ (z >> 31)) >> 32);
    }
};
inline float u01 (RowRng& r) noexcept { return (float) (r.next() >> 8) * (1.0f / 16777216.0f); }

inline uint32_t quant (float v, float steps = 40.0f) noexcept { return (uint32_t) std::lround (v * steps); }
} // namespace

//==============================================================================
Engine::Engine()
{
    // S1000: 8-point windowed sinc, 64 fractional phases
    for (int p = 0; p < 64; ++p)
    {
        const double frac = p / 64.0;
        double sum = 0.0;
        for (int j = 0; j < 8; ++j)
        {
            const double x = (double) (j - 3) - frac;
            const double sinc = std::abs (x) < 1.0e-9 ? 1.0 : std::sin (kPi * x) / (kPi * x);
            const double w = 0.5 + 0.5 * std::cos (kPi * x / 4.0);
            sincTable[(size_t) (p * 8 + j)] = (float) (sinc * w);
            sum += sinc * w;
        }
        for (int j = 0; j < 8; ++j) sincTable[(size_t) (p * 8 + j)] /= (float) sum;
    }
    for (auto& h : viewHead) h.store (-1.0f);
}

void Engine::prepare (double sampleRate, int)
{
    fs = sampleRate;
    reverb.prepare (fs);
    reset();
}

void Engine::reset()
{
    for (auto& v : voices) v.active = false;
    reverb.reset();
    s950L.reset(); s950R.reset(); a500L.reset(); a500R.reset(); ledL.reset(); ledR.reset();
    filterMachine = -1;
    playing = false;
    repActive = false;
    curRowG = -1;
}

void Engine::setKit (const Kit* k) noexcept
{
    if (k == kit) return;
    const Kit* old = kit;
    kit = k;
    if (old != nullptr && k != nullptr && old->sample->serial == k->sample->serial && k->mb.frames > 4)
    {
        // the same break, rebuilt (another sampler, clock, drive or slicing): every sounding voice
        // carries on from the same point in the new buffer, crossfaded, so turning RATE never gaps
        const auto& nmb = k->mb;
        const float step = 1.0f / (float) std::max (1.0, fs * 0.006);
        for (auto& v : voices)
        {
            if (! v.active || v.out || v.src == &nmb || v.src == nullptr) continue;
            Voice* w = nullptr;
            for (auto& c : voices) if (! c.active) { w = &c; break; }
            if (w == nullptr) break;
            const double scale = nmb.ratio / std::max (1.0e-9, v.src->ratio);
            *w = v;
            w->src = &nmb;
            w->pos = v.pos * scale;
            w->lo = v.lo * scale;
            w->hi = v.hi >= (double) v.src->frames - 0.5 ? (double) nmb.frames : std::min ((double) nmb.frames, v.hi * scale);
            w->inc = v.inc * scale;
            w->srcScale = (float) (1.0 / nmb.ratio);
            w->fadeIn = 0.0f;
            w->fadeInStep = step;
            v.out = true;
            v.fadeOut = 0.0f;
            v.fadeOutStep = step;
        }
    }
    else
        // another break: the voices keep reading the buffer they started on (the processor keeps
        // it alive) and fade out, so the new break takes over cleanly
        releaseAll (fs * 0.008);
    uint32_t sig = 0x811C9DC5u;
    if (kit != nullptr)
    {
        for (int s : kit->slices.starts) sig = hash32 (sig, (uint32_t) s);
        sig = hash32 (sig, (uint32_t) kit->bars, (uint32_t) kit->sample->serial);
    }
    if (sig != sliceSig)
    {
        sliceSig = sig;
        for (int r = 0; r < kMaxRows; ++r)
        {
            if (pinned[(size_t) r] && kit != nullptr && cells[(size_t) r].trig == 1)
            {
                // keep the user's 09xx byte, re-snap it to the new slices
                auto& c = cells[(size_t) r];
                const int sl = kit->nearestSlice (kit->offsetFrame (c.off09));
                c.slice = (int16_t) sl;
                c.cls = sl >= 0 && sl < kit->numSlices() ? kit->slices.cls[(size_t) sl] : (uint8_t) Kick;
            }
            else if (! pinned[(size_t) r])
                generated[(size_t) r] = 0;
        }
        patternDirty = true;
    }
}

void Engine::applyEdit (const Edit& e) noexcept
{
    const int r = std::clamp (e.row, 0, kMaxRows - 1);
    switch (e.type)
    {
        case Edit::SetCell:       cells[(size_t) r] = e.cell; pinned[(size_t) r] = 1; generated[(size_t) r] = 1; break;
        case Edit::Pin:           pinned[(size_t) r] = 1; break;
        case Edit::Unpin:         pinned[(size_t) r] = 0; break;
        case Edit::Regenerate:    pinned[(size_t) r] = 0; ++salt[(size_t) r]; generated[(size_t) r] = 0; break;
        case Edit::Clear:         cells[(size_t) r] = Cell {}; pinned[(size_t) r] = 1; generated[(size_t) r] = 1; break;
        case Edit::RegenerateAll: for (int i = 0; i < kMaxRows; ++i) if (! pinned[(size_t) i]) { ++salt[(size_t) i]; generated[(size_t) i] = 0; } break;
        case Edit::UnpinAll:      pinned.fill (0); break;
    }
    patternDirty = true;
}

//==============================================================================
bool Engine::isFillBar (int64_t g, int spd, int every) noexcept
{
    if (every <= 0) return false;
    const double beat = (double) g * spd / (double) kTicksPerBeat;
    const int64_t bar = (int64_t) std::floor (beat / 4.0 + 1.0e-9);
    return bar % every == every - 1;
}

double Engine::rowStartPpq (int64_t g, double rowLen, float swing) const noexcept
{
    double t = (double) g * rowLen;
    if (g & 1) t += (double) swing * rowLen / 3.0;      // odd rows late, up to a triplet feel
    return t;
}

int Engine::naturalSlice (int64_t g, const GenParams& gp, int* currentOut) const noexcept
{
    if (kit == nullptr || kit->numSlices() == 0) return -1;
    const double rowLen = gp.speed / (double) kTicksPerBeat;
    const double bb = kit->breakBeats();
    const double L = (double) kit->sample->frames;
    double beat = std::fmod ((double) g * rowLen, bb);
    if (beat < 0.0) beat += bb;
    const double frame = beat / bb * L;
    const double rowFrames = rowLen / bb * L;
    if (currentOut != nullptr) *currentOut = kit->sliceAt ((int) frame);
    // the hit that belongs to this row: starts within the row (a little early counts too)
    int best = -1; float bestLevel = -1.0f;
    const double lo = frame - 0.4 * rowFrames, hi = frame + 0.6 * rowFrames;
    const auto& st = kit->slices.starts;
    for (int i = 0; i < (int) st.size(); ++i)
    {
        double s = st[(size_t) i];
        if (s > L - 0.4 * rowFrames && lo < 0.0) s -= L;    // wrap: the last hit can belong to row 0
        if (s >= lo && s < hi && kit->slices.level[(size_t) i] > bestLevel)
        {
            best = i;
            bestLevel = kit->slices.level[(size_t) i];
        }
    }
    return best;
}

void Engine::generateCell (Cell& out, int64_t g, int r, const GenParams& gp, uint32_t s, int prev, bool withFill) const noexcept
{
    out = Cell {};
    if (kit == nullptr || kit->numSlices() == 0) return;
    RowRng rng (hash32 (gp.seed, (uint32_t) r, s));

    const double rowLen = gp.speed / (double) kTicksPerBeat;
    const double beatInBar = std::fmod ((double) g * rowLen, 4.0);
    const int pos16 = std::clamp ((int) std::floor (beatInBar * 4.0 + 1.0e-6), 0, 15);
    const bool onBeat = std::abs (beatInBar - std::round (beatInBar)) < 1.0e-6;
    const bool fillBar = withFill && isFillBar (g, gp.speed, gp.fillEvery);
    const float fillBoost = fillBar ? gp.fill * (0.35f + 0.65f * (float) (beatInBar / 4.0)) : 0.0f;
    out.fill = fillBar && gp.fill > 0.0f ? 1 : 0;

    const int natural = naturalSlice (g, gp);
    const float a = u01 (rng), b = u01 (rng), c = u01 (rng);

    // ---- trigger or not
    bool trig;
    if (natural >= 0)
    {
        float keep = gp.density <= 0.5f ? 0.25f + 1.5f * gp.density : 1.0f;
        if (onBeat) keep = std::max (keep, gp.anchor);
        trig = a < keep + fillBoost * 0.5f;
        if (! trig && gp.density < 0.35f && b < 0.5f) { out.trig = 2; return; }   // note cut: a gap
    }
    else
    {
        float add = gp.density > 0.5f ? (gp.density - 0.5f) * 1.6f : 0.0f;
        add += fillBoost * 0.55f;
        if (pos16 % 2 == 0) add *= 1.3f;                   // 8th positions first
        trig = a < add;
    }
    if (! trig) return;
    out.trig = 1;

    // ---- recut from the Markov chain, or the break's own hit
    const int L = kit->sample->frames;
    const double breakBeat = std::fmod ((double) g * rowLen, kit->breakBeats());
    const double frame = breakBeat / kit->breakBeats() * L;
    float recut = gp.complexity * (onBeat ? (1.0f - gp.anchor) : 1.0f) + fillBoost * 0.6f;
    int slice = natural;
    if (natural < 0 || b < recut)
    {
        std::array<float, NumCls> w {};
        std::array<int, NumCls> count {};
        for (auto cl : kit->slices.cls) ++count[cl];
        float sum = 0.0f;
        for (int k = 0; k < NumCls; ++k)
        {
            if (count[(size_t) k] == 0) continue;
            const float prior = std::max (0.02f, priorTable[k][pos16]);
            w[(size_t) k] = gp.markov.prob (prev, k) * std::pow (prior, gp.barFeel * 2.0f);
            sum += w[(size_t) k];
        }
        if (sum <= 0.0f)
        {
            // the chain wants a class this break does not have: fall back on the bar's grammar
            for (int k = 0; k < NumCls; ++k)
                if (count[(size_t) k] > 0) { w[(size_t) k] = std::max (0.02f, priorTable[k][pos16]); sum += w[(size_t) k]; }
        }
        int cls = Snare;
        if (sum > 0.0f)
        {
            float x = c * sum;
            for (int k = 0; k < NumCls; ++k) { x -= w[(size_t) k]; if (x <= 0.0f && w[(size_t) k] > 0.0f) { cls = k; break; } }
            if (x > 0.0f) for (int k = NumCls - 1; k >= 0; --k) if (w[(size_t) k] > 0.0f) { cls = k; break; }
        }
        // a slice of that class, nearer the row's place in the break the less SHUFFLE there is
        const float scale = 0.02f + 0.6f * gp.shuffle;
        float tot = 0.0f;
        const int n = kit->numSlices();
        for (int i = 0; i < n; ++i)
            if (kit->slices.cls[(size_t) i] == cls)
            {
                double d = std::abs ((double) kit->slices.starts[(size_t) i] - frame) / L;
                d = std::min (d, 1.0 - d);
                tot += std::exp (-(float) d / scale);
            }
        float x = u01 (rng) * tot;
        slice = -1;
        for (int i = 0; i < n && slice < 0; ++i)
            if (kit->slices.cls[(size_t) i] == cls)
            {
                double d = std::abs ((double) kit->slices.starts[(size_t) i] - frame) / L;
                d = std::min (d, 1.0 - d);
                x -= std::exp (-(float) d / scale);
                if (x <= 0.0f) slice = i;
            }
        if (slice < 0) slice = std::max (0, kit->sliceAt ((int) frame));
        out.natural = 0;
    }
    else
        out.natural = 1;

    out.slice = (int16_t) slice;
    out.cls = kit->slices.cls[(size_t) slice];
    out.off09 = (uint8_t) kit->offsetByte (kit->slices.starts[(size_t) slice]);

    // ---- Rxy retrigger / drill
    const float rr = u01 (rng), rd = u01 (rng), rm = u01 (rng), ra = u01 (rng);
    if (rd < gp.drill * (1.0f + fillBoost))
    {
        out.drill = 1;
        out.ry = (uint8_t) std::clamp (gp.drillY + (int) std::floor (ra * 5.0f) - 2, 1, 15);
        out.rmode = gp.retrigVol == 0 ? (uint8_t) (rm < 0.6f ? RvDecay : RvExp) : (uint8_t) gp.retrigVol;
        out.ramt = (uint8_t) std::clamp (1 + (int) (ra * (float) gp.retrigAmt), 1, 8);
        out.sweep = (int8_t) std::clamp ((int) std::lround (gp.sweep), -24, 24);
    }
    else if (rr < gp.retrig * (1.0f + 2.0f * fillBoost))
    {
        static const int opts[3] = { 3, 2, 1 };
        int y = opts[std::min (2, (int) (ra * ra * 3.0f))];
        while (y >= gp.speed && y > 1) --y;
        out.ry = (uint8_t) y;
        out.rmode = gp.retrigVol == 0 ? (uint8_t) (1 + std::min (2, (int) (rm * 3.0f))) : (uint8_t) gp.retrigVol;
        out.ramt = (uint8_t) std::clamp (1 + (int) (rm * (float) gp.retrigAmt), 1, 8);
        out.sweep = (int8_t) std::clamp ((int) std::lround (gp.sweep * 0.5f), -24, 24);
    }

    // ---- routing matrix (section 4.1): one uniform R against the weights
    out.chain = (uint8_t) ProbabilisticRoutingEngine::evaluate (gp.weights, u01 (rng));
    const float ps = u01 (rng);
    out.pitch = out.chain == PitchShift ? (int8_t) ((gp.pitchBoth && ps < 0.5f) ? -gp.pitchSemis : gp.pitchSemis) : (int8_t) 0;

    float vol = 1.0f - gp.human * 0.4f * u01 (rng);
    if (! out.natural && out.cls == Ghost) vol *= 0.8f;
    out.vol = (uint8_t) std::clamp ((int) std::lround (vol * 64.0f), 1, 64);
}

uint32_t Engine::keyFor (const GenParams& gp) const noexcept
{
    uint32_t h = hash32 (sliceSig, gp.seed, (uint32_t) (gp.rows * 64 + gp.speed));
    const float f[] = { gp.complexity, gp.anchor, gp.density, gp.shuffle, gp.retrig, gp.drill, gp.fill, gp.human, gp.sweep / 24.0f };
    for (float v : f) h = hash32 (h, quant (v));
    for (float v : gp.weights) h = hash32 (h, quant (v));
    for (auto& row : gp.markov.p) for (float v : row) h = hash32 (h, quant (v));
    h = hash32 (h, (uint32_t) (gp.fillEvery * 1000 + gp.retrigVol * 100 + gp.retrigAmt * 10), (uint32_t) (gp.drillY * 100 + gp.pitchSemis + 50));
    return hash32 (h, gp.pitchBoth ? 1u : 0u);
}

void Engine::regenerateAll (const GenParams& gp, bool force) noexcept
{
    const int64_t start = curRowG + 1;
    int prev = prevCls;
    for (int i = 0; i < rows; ++i)
    {
        const int64_t g = start + i;
        const int r = (int) (((g % rows) + rows) % rows);
        if (pinned[(size_t) r] || (locked && ! force && generated[(size_t) r])) { if (cells[(size_t) r].trig == 1) prev = cells[(size_t) r].cls; continue; }
        generateCell (cells[(size_t) r], g, r, gp, salt[(size_t) r], prev);
        generated[(size_t) r] = 1;
        rowKey[(size_t) r] = modKey;
        if (cells[(size_t) r].trig == 1) prev = cells[(size_t) r].cls;
    }
    patternDirty = true;
}

void Engine::publishPattern() noexcept
{
    if (! patternDirty) return;
    patternDirty = false;
    auto& pv = patternOut.writeBuffer();
    pv.cells = cells;
    pv.pinned = pinned;
    pv.rows = rows;
    pv.serial = ++patternSerial;
    patternOut.publish();
}

//==============================================================================
void Engine::emitHit (int offset, int row, int slice, uint8_t cls, uint8_t chain, float vol, bool repeat, bool pad, bool natural, uint8_t variant) noexcept
{
    if (hitCount < (int) hits.size())
    {
        auto& h = hits[(size_t) hitCount++];
        h.offset = offset; h.row = row; h.slice = slice; h.cls = cls; h.chain = chain; h.vol = vol; h.repeat = repeat; h.pad = pad;
        h.natural = natural; h.variant = variant;
    }
    if (! repeat)
    {
        lastHitSlice.store (slice, std::memory_order_relaxed);
        lastHitChain.store (chain, std::memory_order_relaxed);
        hitCounter.fetch_add (1, std::memory_order_relaxed);
    }
}

void Engine::releaseAll (double fadeSamples) noexcept
{
    const float step = 1.0f / (float) std::max (1.0, fadeSamples);
    for (auto& v : voices)
        if (v.active && ! v.out)
        {
            v.out = true;
            v.fadeOut = 0.0f;
            v.fadeOutStep = step;
        }
}

void Engine::startVoice (int slice, int off09, uint8_t chain, float vol, double pitchRatio, bool snap,
                         const PlayParams& pp, int, double minFade) noexcept
{
    if (kit == nullptr || kit->mb.frames < 4) return;
    const auto& mb = kit->mb;
    const int n = kit->numSlices();
    double start;
    int sl;
    if (snap && slice >= 0 && slice < n)
    {
        sl = slice;
        start = kit->mbStarts[(size_t) slice];
    }
    else
    {
        // eq. 2.1: S_start = floor (P09 * L_total / 256)
        const int f = kit->offsetFrame (off09);
        start = std::floor ((double) f * mb.ratio);
        sl = kit->sliceAt (f);
    }
    const double nextStart = sl + 1 < n ? (double) kit->mbStarts[(size_t) sl + 1] : (double) mb.frames;
    const double sliceLen = std::max (16.0, nextStart - start);

    double rate = mb.rate / fs * pitchRatio;
    if (pp.fit == 1)
    {
        const double breakBpm = kit->breakBeats() * 60.0 / ((double) kit->sample->frames / kit->sample->rate);
        rate *= std::clamp (bpm / std::max (1.0, breakBpm), 0.25, 4.0);
    }
    if (chain == SubOctave) rate *= 0.5;

    // the break simply goes on (a natural hit right where the voice already reads):
    // no crossfade, no retrigger, so a straight run plays sample for sample
    if (chain != ReverseBuffer && chain != ReverbDrill)
        for (auto& c : voices)
            if (c.active && ! c.out && c.src == &mb && c.inc > 0.0 && std::abs (c.inc - rate) < 1.0e-9
                && std::abs (c.pos - start) < 1.5 && c.fadeIn >= 1.0f)
            {
                for (auto& o : voices) if (&o != &c && o.active && ! o.out) { o.out = true; o.fadeOut = 0.0f; o.fadeOutStep = 1.0f / (float) std::max (1.0, (double) pp.xfadeMs * 0.001 * fs); }
                c.vol = vol;
                c.env = 1.0f;
                c.holdLeft = -1;
                if (pp.gate < 0.999f)
                {
                    c.holdLeft = (int64_t) std::max (1.0, (double) pp.gate * sliceLen / rate);
                    c.envStep = 1.0f / (float) std::max (1.0, (double) pp.decayMs * 0.001 * fs);
                }
                return;
            }

    const double xf = std::max (1.0, std::min ((double) pp.xfadeMs * 0.001 * fs, minFade));
    releaseAll (xf);

    // a free voice, or the oldest fading one
    Voice* v = nullptr;
    for (auto& c : voices) if (! c.active) { v = &c; break; }
    if (v == nullptr)
    {
        float best = -1.0f;
        for (auto& c : voices) if (c.out && c.fadeOut > best) { best = c.fadeOut; v = &c; }
        if (v == nullptr) v = &voices[(size_t) (voiceRound++ % kVoices)];
    }

    *v = Voice {};
    v->active = true;
    v->src = &mb;
    v->vol = vol;
    v->send = chain == ReverbDrill ? 1.0f : 0.0f;
    v->fadeIn = 0.0f;
    v->fadeInStep = 1.0f / (float) xf;
    v->srcScale = (float) (1.0 / mb.ratio);
    if (chain == ReverseBuffer)
    {
        // the slice backwards, ending on its own attack (at most one beat long)
        const double beat = mb.rate * 60.0 / std::max (20.0, bpm) / pitchRatio;
        const double len = std::min (sliceLen, beat);
        v->lo = start;
        v->hi = std::min ((double) mb.frames, start + len);
        v->pos = v->hi - 1.0;
        v->inc = -rate;
    }
    else
    {
        v->lo = start;
        v->hi = (double) mb.frames;
        v->pos = start;
        v->inc = rate;
    }
    if (pp.gate < 0.999f)
    {
        const double outLen = sliceLen / std::abs (v->inc);
        v->holdLeft = (int64_t) std::max (1.0, (double) pp.gate * outLen);
        v->envStep = 1.0f / (float) std::max (1.0, (double) pp.decayMs * 0.001 * fs);
    }
}

//==============================================================================
void Engine::handleRow (int64_t g, double startPpq, double nextStartPpq, int offset, const GenParams& gp, const PlayParams& pp) noexcept
{
    const int r = (int) (((g % rows) + rows) % rows);
    const int64_t pass = g >= 0 ? g / rows : (g - rows + 1) / rows;
    curRowG = g;
    curRowStart = startPpq;
    curRowEnd = nextStartPpq;
    repActive = false;

    const bool fillNow = isFillBar (g, gp.speed, gp.fillEvery) && gp.fill > 0.0f;
    if (! generated[(size_t) r] || (! pinned[(size_t) r] && ! locked && rowKey[(size_t) r] != modKey))
    {
        // never written, or an LFO moved a generator control since: write it again as it plays
        generateCell (cells[(size_t) r], g, r, gp, salt[(size_t) r], prevCls, false);
        generated[(size_t) r] = 1;
        rowKey[(size_t) r] = modKey;
        patternDirty = true;
    }
    passSeen[(size_t) r] = pass;
    viewRow.store (r, std::memory_order_relaxed);
    viewPass.store ((int) pass, std::memory_order_relaxed);

    // fills and variations are played, never written into the pattern: the groove always comes back
    Cell c = cells[(size_t) r];
    uint8_t variant = 0;
    if (! pinned[(size_t) r] && ! locked)
    {
        if (fillNow)
        {
            generateCell (c, g, r, gp, salt[(size_t) r], prevCls, true);       // same dice as the row, plus the fill
            variant = 2;
        }
        else if (gp.mutate > 0.0f)
        {
            const float u = (float) (hash32 (gp.seed ^ 0xA5A5u, (uint32_t) g, salt[(size_t) r]) >> 8) / 16777216.0f;
            if (u < gp.mutate)
            {
                generateCell (c, g, r, gp, salt[(size_t) r] + 0x9E3779B9u * (uint32_t) (pass + 1), prevCls, false);
                variant = 1;
            }
        }
    }
    viewVariant.store (variant, std::memory_order_relaxed);
    if (c.trig == 2) { releaseAll (std::max (1.0, (double) pp.xfadeMs * 0.001 * fs)); return; }
    if (c.trig != 1 || kit == nullptr || kit->numSlices() == 0) return;

    // ---- retrigger plan (eq. 2.3 - 2.5) before the first hit, which is repeat k = 0
    Cell rc = c;
    if (rc.chain == ReverbDrill && rc.ry == 0)
    {
        rc.ry = (uint8_t) std::clamp (gp.drillY, 1, 15);
        rc.drill = 1;
        rc.rmode = RvDecay;
        rc.ramt = 6;
    }
    float v0 = (float) rc.vol / 64.0f;
    float first = v0;
    if (rc.ry > 0)
    {
        const double tTick = 2.5 / std::max (1.0, bpm);                          // T_tick = 2.5 / B
        const double s = rc.drill ? std::floor (rc.ry * tTick * fs / kSubTicks)  // S_retrig, eq. 2.3
                                  : std::floor (rc.ry * tTick * fs);
        const int64_t S = std::max<int64_t> (8, (int64_t) s);
        const double rowSamples = (nextStartPpq - startPpq) / ppqPerSample;
        const int K = std::clamp ((int) std::ceil (rowSamples / (double) S) - 1, 0, 4096);
        if (K > 0)
        {
            repActive = true;
            repK = 1;
            repKMax = K;
            repCell = rc;
            repIntervalSamples = S;
            repIntervalPpq = (double) S * ppqPerSample;
            repNextPpq = startPpq + repIntervalPpq;
            repDp = (float) rc.sweep / (float) K;
            const float amt = (float) std::clamp ((int) rc.ramt, 1, 8);
            switch (rc.rmode)
            {
                case RvDecay: repV0 = v0; repDv = amt / 8.0f * v0 / (float) K; break;
                case RvBoost: repV0 = v0 * (1.0f - amt / 8.5f); repDv = (v0 - repV0) / (float) K; first = repV0; break;
                case RvExp:   repV0 = v0; repGamma = std::pow (std::max (5.0e-4f, std::pow (1.0f - amt / 8.5f, 3.0f)), 1.0f / (float) K); break;
                default:      repV0 = v0; repDv = 0.0f; break;
            }
        }
    }

    const double pitch = std::pow (2.0, (double) rc.pitch / 12.0);
    startVoice (rc.slice, rc.off09, rc.chain, first, pitch, gp.snap, pp, offset,
                repActive ? (double) repIntervalSamples * 0.5 : 1.0e9);
    prevCls = rc.cls;
    emitHit (offset, r, rc.slice, rc.cls, rc.chain, first, false, false, rc.natural != 0, variant);
}

void Engine::handleRepeat (int offset, const PlayParams& pp) noexcept
{
    const int k = repK;
    float v;
    switch (repCell.rmode)
    {
        case RvDecay: v = std::max (0.0f, repV0 - (float) k * repDv); break;          // eq. 2.5, linear decay
        case RvBoost: v = std::min (1.0f, repV0 + (float) k * repDv); break;          // linear boost
        case RvExp:   v = repV0 * std::pow (repGamma, (float) k); break;              // exponential
        default:      v = repV0; break;
    }
    // eq. 2.4: f_k = f0 * 2^(k * dp / 12)
    const double pitch = std::pow (2.0, (double) repCell.pitch / 12.0) * std::pow (2.0, (double) k * repDp / 12.0);
    if (v > 1.0e-4f)
    {
        startVoice (repCell.slice, repCell.off09, repCell.chain, v, pitch, true, pp, offset, (double) repIntervalSamples * 0.5);
        emitHit (offset, (int) (((curRowG % rows) + rows) % rows), repCell.slice, repCell.cls, repCell.chain, v, true, false);
    }
    ++repK;
    if (repK > repKMax) repActive = false;
    else repNextPpq += repIntervalPpq;
}

//==============================================================================
inline void Engine::readVoice (Voice& v, float& l, float& r) const noexcept
{
    const auto& mb = *v.src;
    const int last = mb.frames - 1;
    const float* L = mb.l.data();
    const float* R = mb.r.empty() ? L : mb.r.data();
    const int i = (int) std::floor (v.pos);
    switch (mb.machine)
    {
        case AkaiS950:
        case AmigaPaula:
        {
            // zero-order hold at the sampler's clock (the template's drop-sample latch)
            const int k = std::clamp (i, 0, last);
            l = L[k]; r = R[k];
            return;
        }
        case AkaiS1000:
        {
            const float frac = (float) (v.pos - i);
            const float* t = &sincTable[(size_t) (std::min (63, (int) (frac * 64.0f)) * 8)];
            float al = 0.0f, ar = 0.0f;
            for (int j = 0; j < 8; ++j)
            {
                const int k = std::clamp (i + j - 3, 0, last);
                al += t[j] * L[k];
                ar += t[j] * R[k];
            }
            l = al; r = ar;
            return;
        }
        default:
        {
            // 4-point Hermite
            const float f = (float) (v.pos - i);
            auto herm = [f] (float xm1, float x0, float x1, float x2)
            {
                const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
                const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
                return ((c3 * f + c2) * f + c1) * f + x0;
            };
            const int a = std::clamp (i - 1, 0, last), b = std::clamp (i, 0, last), c = std::clamp (i + 1, 0, last), d = std::clamp (i + 2, 0, last);
            l = herm (L[a], L[b], L[c], L[d]);
            r = (R == L) ? l : herm (R[a], R[b], R[c], R[d]);
            return;
        }
    }
}

void Engine::updateOutputFilters (const PlayParams& pp) noexcept
{
    if (kit == nullptr) return;
    const int m = kit->mb.machine;
    const double rate = kit->mb.rate;
    if (m == filterMachine && same (rate, filterRate) && same (pp.mfilter, filterAmt) && pp.amigaModel == filterModel) return;
    if (m != filterMachine) { s950L.reset(); s950R.reset(); a500L.reset(); a500R.reset(); ledL.reset(); ledR.reset(); }
    filterMachine = m; filterRate = rate; filterAmt = pp.mfilter; filterModel = pp.amigaModel;
    // MF6CN-50 tracks the clock: its corner is a fraction of the sampler's Nyquist
    const double fc = std::min (0.45 * fs, 0.5 * rate * (0.25 + 0.75 * (double) std::clamp (pp.mfilter, 0.0f, 1.0f)));
    s950L.lowpass (fs, fc); s950R.lowpass (fs, fc);
    a500L.lowpass (fs, 4421.0); a500R.lowpass (fs, 4421.0);
    ledL.lowpass (fs, 3090.53, 0.660225); ledR.lowpass (fs, 3090.53, 0.660225);
}

void Engine::render (float* outL, float* outR, int from, int to, const PlayParams& pp) noexcept
{
    const int machine = kit != nullptr ? kit->mb.machine : Clean16;
    const double endFade = fs * 0.0025;           // 2.5 ms where a voice runs out of slice
    const float targetLevel = pp.level, targetRet = pp.revReturn;
    for (int i = from; i < to; ++i)
    {
        float dl = 0.0f, dr = 0.0f, sl = 0.0f, sr = 0.0f;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            float l, r;
            readVoice (v, l, r);
            float g = v.vol * v.env;
            if (v.fadeIn < 1.0f) { g *= gIn (v.fadeIn); v.fadeIn = std::min (1.0f, v.fadeIn + v.fadeInStep); }
            if (v.out)
            {
                g *= gOut (v.fadeOut);
                v.fadeOut += v.fadeOutStep;
                if (v.fadeOut >= 1.0f) v.active = false;
            }
            if (v.holdLeft >= 0)
            {
                if (v.holdLeft > 0) --v.holdLeft;
                else { v.env -= v.envStep; if (v.env <= 0.0f) { v.env = 0.0f; v.active = false; } }
            }
            // a short fade where the readable range ends
            const double remain = (v.inc > 0.0 ? (v.hi - 1.0 - v.pos) : (v.pos - v.lo)) / std::abs (v.inc);
            if (remain < endFade) g *= (float) std::max (0.0, remain / endFade);
            v.pos += v.inc;
            if (v.pos >= v.hi - 1.0 || v.pos < v.lo) v.active = false;
            dl += l * g; dr += r * g;
            sl += l * g * v.send; sr += r * g * v.send;
        }

        // the sampler's output stage
        switch (machine)
        {
            case AkaiS950:   dl = s950L.process (dl); dr = s950R.process (dr); break;
            case AmigaPaula:
                if (pp.amigaModel == 0) { dl = a500L.process (dl); dr = a500R.process (dr); }
                if (pp.led) { dl = ledL.process (dl); dr = ledR.process (dr); }
                break;
            default: break;
        }

        float rl, rr;
        reverb.process (sl, sr, rl, rr);
        levelSm += 0.002f * (targetLevel - levelSm);
        returnSm += 0.002f * (targetRet - returnSm);
        outL[i] = softClip ((dl + rl * returnSm) * levelSm);
        outR[i] = softClip ((dr + rr * returnSm) * levelSm);
    }
}

//==============================================================================
void Engine::process (float* outL, float* outR, int n, const Transport& t, const GenParams& gp, const PlayParams& pp,
                      const PadEvent* pads, int numPads) noexcept
{
    hitCount = 0;
    std::fill (outL, outL + n, 0.0f);
    std::fill (outR, outR + n, 0.0f);

    // ---- pattern structure and the controls it follows
    const int newRows = std::clamp (gp.rows, 1, kMaxRows), newSpeed = std::clamp (gp.speed, 1, 24);
    bool force = false;
    if (newRows != rows || newSpeed != speed) { rows = newRows; speed = newSpeed; force = true; }
    locked = gp.lock;
    modKey = keyFor (gp);
    const uint32_t k = gp.userKey != 0 ? hash32 (gp.userKey, sliceSig, (uint32_t) (rows * 64 + speed)) : modKey;
    if (k != key || force)
    {
        key = k;
        if (! locked || force) regenerateAll (gp, force);
    }
    bool pending = false;
    for (int r = 0; r < rows; ++r) if (! generated[(size_t) r]) pending = true;
    if (pending)
    {
        const int64_t start = curRowG + 1;
        int prev = prevCls;
        for (int i = 0; i < rows; ++i)
        {
            const int64_t g = start + i;
            const int r = (int) (((g % rows) + rows) % rows);
            if (! generated[(size_t) r])
            {
                generateCell (cells[(size_t) r], g, r, gp, salt[(size_t) r], prev);
                generated[(size_t) r] = 1; rowKey[(size_t) r] = modKey;
                patternDirty = true;
            }
            if (cells[(size_t) r].trig == 1) prev = cells[(size_t) r].cls;
        }
    }

    reverb.set (pp.revSize, pp.revDamp);
    updateOutputFilters (pp);

    // ---- transport
    const double rowLen = speed / (double) kTicksPerBeat;
    bool play = false;
    double ppq0 = 0.0;
    if (t.valid && t.playing) { play = true; ppq0 = t.ppq; bpm = std::max (1.0, t.bpm); }
    else if (pp.run)
    {
        play = true;
        if (! playing) internalPpq = 0.0;
        ppq0 = internalPpq;
        bpm = std::clamp (pp.internalBpm, 20.0, 400.0);
    }
    ppqPerSample = bpm / 60.0 / fs;
    if (play)
    {
        const bool jump = ! playing || std::abs (ppq0 - expectedPpq) > 0.02;
        if (jump)
        {
            releaseAll (fs * 0.004);
            repActive = false;
            int64_t g = (int64_t) std::floor (ppq0 / rowLen + 1.0e-9);
            const double gs = rowStartPpq (g, rowLen, pp.swing);
            if (gs < ppq0 - rowLen * 0.25) ++g;           // too far into the row: wait for the next one
            nextRow = g;
            nextRowPpq = std::max (ppq0, rowStartPpq (g, rowLen, pp.swing));
            curRowG = g - 1;
        }
        playing = true;
    }
    else
    {
        if (playing) { releaseAll (fs * 0.01); repActive = false; }
        playing = false;
    }
    viewPlaying.store (playing ? 1 : 0, std::memory_order_relaxed);
    blockPpq = ppq0;

    auto toOffset = [this] (double ppq) -> int
    {
        const double s = (ppq - blockPpq) / ppqPerSample;
        if (s <= 0.0) return 0;
        return s >= (double) INT_MAX / 2 ? INT_MAX / 2 : (int) std::ceil (s - 1.0e-6);
    };

    int pos = 0, padIdx = 0;
    while (pos < n)
    {
        const int end = std::min (n, pos + kSubBlock);
        for (int guard = 0; guard < 4096; ++guard)
        {
            int sRow = INT_MAX, sRep = INT_MAX, sPad = INT_MAX;
            if (playing)
            {
                sRow = toOffset (nextRowPpq);
                if (repActive) sRep = toOffset (repNextPpq);
            }
            if (padIdx < numPads) sPad = std::max (0, pads[padIdx].offset);
            const int s = std::min ({ sRow, sRep, sPad });
            if (s >= end) break;
            const int at = std::max (s, pos);
            render (outL, outR, pos, at, pp);
            pos = at;
            if (sPad == s)
            {
                const auto& pe = pads[padIdx++];
                if (pe.on && kit != nullptr && pe.slice >= 0 && pe.slice < kit->numSlices())
                {
                    // section 4.1: every MIDI note-on draws R and routes the slice
                    padRouting.updateDistribution (gp.weights);
                    const auto chain = padRouting.evaluateNextSliceTarget();
                    const int ps = (gp.pitchBoth && (padRng() & 1)) ? -gp.pitchSemis : gp.pitchSemis;
                    const double pitch = chain == PitchShift ? std::pow (2.0, ps / 12.0) : 1.0;
                    startVoice (pe.slice, kit->offsetByte (kit->slices.starts[(size_t) pe.slice]), chain,
                                pe.velocity, pitch, true, pp, pos, 1.0e9);
                    emitHit (pos, -1, pe.slice, kit->slices.cls[(size_t) pe.slice], chain, pe.velocity, false, true);
                }
                continue;
            }
            if (sRow <= sRep)
            {
                const int64_t g = nextRow;
                const double st = nextRowPpq;
                ++nextRow;
                nextRowPpq = rowStartPpq (nextRow, rowLen, pp.swing);
                handleRow (g, st, nextRowPpq, pos, gp, pp);
            }
            else
                handleRepeat (pos, pp);
        }
        render (outL, outR, pos, end, pp);
        pos = end;
    }

    if (playing)
    {
        expectedPpq = blockPpq + (double) n * ppqPerSample;
        if (! (t.valid && t.playing)) internalPpq = expectedPpq;
    }

    // ---- views
    int vi = 0;
    for (auto& v : voices)
        if (v.active && ! v.out && vi < kViews)
        {
            viewHead[(size_t) vi].store ((float) (v.pos * v.srcScale), std::memory_order_relaxed);
            viewGain[(size_t) vi].store (v.vol * v.env, std::memory_order_relaxed);
            ++vi;
        }
    for (; vi < kViews; ++vi) viewHead[(size_t) vi].store (-1.0f, std::memory_order_relaxed);
    float pl = 0.0f, pr = 0.0f;
    for (int i = 0; i < n; ++i) { pl = std::max (pl, std::abs (outL[i])); pr = std::max (pr, std::abs (outR[i])); }
    peakL.store (pl, std::memory_order_relaxed);
    peakR.store (pr, std::memory_order_relaxed);
    publishPattern();
}

} // namespace gtbs
