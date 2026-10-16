// BREAKBEAT engine — see BreakbeatEngine.h for what changed from the plugin.
#include "BreakbeatEngine.h"

#include <algorithm>
#include <cmath>

namespace bbeat
{

const char* cutModeName (int m)
{
    static const char* n[] = { "MOVE", "CUTPROC11", "WARPCUT", "SQPUSHER1", "CHOOSECUT" };
    return m >= 0 && m < NumCutModes ? n[m] : "?";
}

BreakbeatEngine::BreakbeatEngine()
{
    for (auto& s : steps) s.store (0);
    for (auto& b : slotBars) b.store (1.0f);
    for (auto& n : slotSlices) n.store (8);
    for (auto& r : retireBuf) r.store (nullptr);
}

BreakbeatEngine::~BreakbeatEngine()
{
    for (auto& in : incoming) delete in.exchange (nullptr);
    for (auto*& a : active) { delete a; a = nullptr; }
    collectGarbage();
}

void BreakbeatEngine::prepare (double sr, int)
{
    sampleRate = sr > 0.0 ? sr : 44100.0;
    fadeLength = std::max (8, (int) std::round (sampleRate * 0.002));
    tailRemaining = fadeInRemaining = 0;
    tailSample = nullptr;
}

// ---------------------------------------------------------------------------
// Sample exchange
// ---------------------------------------------------------------------------
void BreakbeatEngine::submitSample (int slot, std::unique_ptr<SampleData> data, bool previewIfStopped)
{
    if (slot < 0 || slot >= numSlots || data == nullptr) return;
    incomingPreview[slot].store (previewIfStopped);
    clearRequest[slot].store (false);
    delete incoming[slot].exchange (data.release());
}

void BreakbeatEngine::clearSlot (int slot)
{
    if (slot < 0 || slot >= numSlots) return;
    delete incoming[slot].exchange (nullptr);
    clearRequest[slot].store (true);
}

void BreakbeatEngine::retire (SampleData* s) noexcept
{
    if (s == nullptr) return;
    const int h = retireHead.load (std::memory_order_relaxed);
    auto& cell = retireBuf[(size_t) (h % retireCapacity)];
    if (cell.load (std::memory_order_acquire) != nullptr) return;   // full: leak rather than free here
    cell.store (s, std::memory_order_release);
    retireHead.store (h + 1, std::memory_order_release);
}

void BreakbeatEngine::collectGarbage()
{
    const int h = retireHead.load (std::memory_order_acquire);
    while (retireTail != h)
    {
        auto& cell = retireBuf[(size_t) (retireTail % retireCapacity)];
        delete cell.exchange (nullptr, std::memory_order_acq_rel);
        ++retireTail;
    }
}

const SampleData* BreakbeatEngine::current() const noexcept
{
    if (active[curSlot] != nullptr) return active[curSlot];
    return active[currentLoop == 0 ? 0 : 1] != nullptr ? active[currentLoop == 0 ? 0 : 1] : active[0];
}

void BreakbeatEngine::swapIncoming() noexcept
{
    for (int slot = 0; slot < numSlots; ++slot)
    {
        auto* in = incoming[slot].exchange (nullptr);
        const bool clear = in == nullptr && clearRequest[slot].exchange (false);
        if (in == nullptr && ! clear) continue;

        auto* old = active[slot];
        if (tailSample == old) { tailSample = nullptr; tailRemaining = 0; }
        active[slot] = in;
        retire (old);

        if (slot == curSlot && in != nullptr)
        {
            // install_loaded_sample(): the new sound slides in under the playhead.
            const auto sliceLen = (double) (in->totalFrames / std::max (1, segSlices));
            playPos = (double) (currentSlice % segSlices) * sliceLen;
            fadeInRemaining = P.declick ? fadeLength : 0;
            if (slot == 0 && ! wasRunning && incomingPreview[slot].load())
                startPreview();
        }
    }
}

// ---------------------------------------------------------------------------
// Random (same xorshift32 as breakbeat.c)
// ---------------------------------------------------------------------------
uint32_t BreakbeatEngine::randomU32() noexcept
{
    uint32_t x = rngState;
    if (x == 0) x = 0x9e3779b9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rngState = x;
    return x;
}

float BreakbeatEngine::randCallback (void* ctx)
{
    auto* self = static_cast<BreakbeatEngine*> (ctx);
    return (float) (self->randomU32() & 0x00ffffffu) / 16777216.0f;
}

// ---------------------------------------------------------------------------
// Grid helpers
// ---------------------------------------------------------------------------
double BreakbeatEngine::samplesPerTrigger (bool withSpeed) const noexcept
{
    const double spb = sampleRate * 60.0 / (double) stableBpm * 4.0;
    const double base = spb * (double) segLength / (double) std::max (1, segSlices);
    return withSpeed ? base / (double) speedFactor (segSpeed) : base;
}

double BreakbeatEngine::playbackRate() const noexcept
{
    const auto* s = current();
    if (s == nullptr) return 1.0;
    const double sliceLen = (double) (s->totalFrames / std::max (1, segSlices));
    const double spt = samplesPerTrigger (false);
    if (sliceLen <= 0.0 || spt <= 0.0) return 1.0;
    return sliceLen / spt * (P.tapeSpeed ? (double) speedFactor (segSpeed) : 1.0);
}

void BreakbeatEngine::jumpTo (double newPos) noexcept
{
    if (P.declick && current() != nullptr)
    {
        tailSample = current();
        tailPos = playPos;
        tailRate = playbackRate();
        tailRemaining = fadeLength;
        fadeInRemaining = fadeLength;
    }
    playPos = newPos;
}

int BreakbeatEngine::resolveSample (int loop, int value) noexcept
{
    const int fallback = loop == 0 ? 0 : 1;
    if (value <= 0) return fallback;
    if (value == sampleRandom)
    {
        int loaded[maxLibrary], n = 0;
        for (int i = 0; i < maxLibrary; ++i)
            if (active[firstLibrarySlot + i] != nullptr) loaded[n++] = firstLibrarySlot + i;
        return n > 0 ? loaded[randomU32() % (uint32_t) n] : fallback;
    }
    const int slot = firstLibrarySlot + value - 1;
    return (slot < numSlots && active[slot] != nullptr) ? slot : fallback;
}

void BreakbeatEngine::beginSegment (double startPpq) noexcept
{
    segStartPpq = startPpq;
    segSlices = slicesOf (curSlot);
    segLength = barsOf (curSlot);
    lastBars = segLength; lastSlices = segSlices;
    trigPpq = 4.0 * (double) segLength / (double) segSlices / (double) speedFactor (segSpeed);
    nextTrig = 1;
    currentSlice = 0;
    subSliceActive = false;
    subSliceCounter = 0;
    if (cutMode != CutMove)
    {
        // SEMI port: the cut schedule carries on; only the loop / speed changed under it
        resumeCut (startPpq);
        return;
    }
    jumpTo (0.0);
    restartEnvelope (samplesPerTrigger (true));
    emitNote (100);
    logCut (startPpq, trigPpq, 0.0, false);
}

void BreakbeatEngine::resync (double ppq) noexcept
{
    const double sp = stepPpq();
    const int nSteps = std::clamp (P.seqSteps, 1, maxSteps);
    const auto stepAbs = (int64_t) std::floor (std::max (0.0, ppq) / sp + 1.0e-9);
    curStepAbs = stepAbs;
    const Step st = getStep ((int) (stepAbs % nSteps));
    stepIsFill = st.fill > 0;
    stepFill = std::clamp ((float) st.fill / 100.0f * P.intensity, 0.0f, 1.0f);
    stepFx = (unsigned) st.fx;

    currentLoop = 0;
    curSlot = resolveSample (0, st.sampleA);
    segSpeed = effectiveSpeed (st);
    const auto phraseStartStep = stepAbs - stepAbs % nSteps;
    segStartPpq = (double) (segSpeed == P.globalSpeed && st.sampleA == 0 ? phraseStartStep : stepAbs) * sp;
    segSlices = slicesOf (curSlot);
    segLength = barsOf (curSlot);
    lastBars = segLength; lastSlices = segSlices;
    trigPpq = 4.0 * (double) segLength / (double) segSlices / (double) speedFactor (segSpeed);

    if (cutMode != CutMove)
    {
        cutResync (ppq);
        return;
    }

    const double pos = std::max (0.0, (ppq - segStartPpq) / trigPpq);
    const auto k = (int64_t) std::floor (pos + 1.0e-9);
    currentSlice = (int) (k % segSlices);
    nextTrig = k + 1;
    subSliceActive = false;
    subSliceCounter = 0;
    if (auto* s = current())
    {
        const double sliceLen = (double) (s->totalFrames / segSlices);
        const double perTrig = P.tapeSpeed ? sliceLen : sliceLen / (double) speedFactor (segSpeed);
        playPos = (double) currentSlice * sliceLen + (pos - (double) k) * perTrig;
    }
    restartEnvelope (samplesPerTrigger (true));
    envPos = (pos - (double) k) * hitLen;
    if (pos - (double) k < 1.0e-6) emitNote (100);
}

void BreakbeatEngine::applyGridChanges (double ppqNow) noexcept
{
    const float bars = barsOf (curSlot);
    const int slices = slicesOf (curSlot);
    if (bars == lastBars && slices == lastSlices) return;
    lastBars = bars; lastSlices = slices;
    if (! wasRunning) return;

    // The playing sample's grid changed: re-cut from the current step boundary.
    segStartPpq = (double) curStepAbs * stepPpq();
    segSlices = slices;
    segLength = bars;
    trigPpq = 4.0 * (double) segLength / (double) segSlices / (double) speedFactor (segSpeed);
    if (cutMode != CutMove) { resumeCut (ppqNow); return; }
    const double pos = std::max (0.0, (ppqNow - segStartPpq) / trigPpq);
    const auto k = (int64_t) std::floor (pos + 1.0e-9);
    currentSlice = (int) (k % segSlices);
    nextTrig = k + 1;
    subSliceActive = false;
    if (auto* s = current())
        jumpTo ((double) currentSlice * (double) (s->totalFrames / segSlices));
}

void BreakbeatEngine::resetTransport() noexcept
{
    currentLoop = 0;
    curSlot = 0;
    previewFrames = 0;
    tailRemaining = fadeInRemaining = 0;
    subSliceActive = false;
    ++reseedCount;
    rngState = fixedSeed != 0 ? fixedSeed : (rngState ^ ((reseedCount * 0x9E3779B9u) | 1u));
}

void BreakbeatEngine::startPreview() noexcept
{
    const double spb = (sampleRate * 60.0 / (double) stableBpm) * 4.0;
    currentLoop = 0;
    curSlot = 0;
    segSlices = slicesOf (0);
    segLength = barsOf (0);
    segSpeed = 2;
    previewFrames = (int) (spb * (double) segLength);
    currentSlice = 0;
    subSliceActive = false;
    playPos = 0.0;
    restartEnvelope (1.0e12);
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------
bool BreakbeatEngine::handleStep (int64_t stepAbs, double atPpq, bool consumeRandom) noexcept
{
    curStepAbs = stepAbs;
    const int nSteps = std::clamp (P.seqSteps, 1, maxSteps);
    Step st = getStep ((int) (stepAbs % nSteps));

    // probability: the step's events happen or the step plays plain
    if (st.prob < 100 && randCallback (this) * 100.0f >= (float) st.prob)
    {
        const Step plain;
        st = plain;
    }

    // chaos: every pass varies the written pattern and adds spontaneous events
    if (P.chaos > 0.0f)
    {
        const float c = P.chaos;
        auto wobble = [&] (int v) { return std::clamp (v + (int) ((randCallback (this) - 0.5f) * 200.0f * c), 0, 100); };
        if (st.fill > 0) st.fill = std::max (1, wobble (st.fill));
        else if (randCallback (this) < c * 0.3f) st.fill = 20 + (int) (randCallback (this) * 80.0f * c);
        if (st.bChance > 0) st.bChance = wobble (st.bChance);
        else if (randCallback (this) < c * 0.2f) st.bChance = (int) (randCallback (this) * 100.0f * c);
        if (randCallback (this) < c * 0.15f) st.speed = 1 + (int) (randCallback (this) * 2.99f);
    }

    stepIsFill = st.fill > 0;
    stepFill = std::clamp ((float) st.fill / 100.0f * P.intensity, 0.0f, 1.0f);
    stepFx = (unsigned) st.fx;

    int wanted = 0;
    if (consumeRandom && st.bChance > 0 && randCallback (this) < (float) st.bChance / 100.0f)
        wanted = 1;
    int slot = resolveSample (wanted, wanted == 0 ? st.sampleA : st.sampleB);
    if (wanted == 1 && active[slot] == nullptr) { wanted = 0; slot = resolveSample (0, st.sampleA); }
    const int spd = effectiveSpeed (st);

    const bool change = wanted != currentLoop || slot != curSlot || spd != segSpeed;
    if (! change) return false;

    currentLoop = wanted;
    curSlot = slot;
    segSpeed = spd;
    beginSegment (atPpq);   // restarts on slice 0: that hit replaces the trigger
    return true;
}

void BreakbeatEngine::applyRetrig (int div) noexcept
{
    subSliceActive = div > 1;
    if (subSliceActive) retriggerDivisions = div;
}

void BreakbeatEngine::fireTrigger (double atPpq) noexcept
{
    const auto k = nextTrig++;
    const int beat = (int) (k % segSlices);
    subSliceCounter = 0;

    // pattern lock engages at the bar where it is switched on (or resized)
    if (P.lockBars != lockBarsActive)
    {
        lockBarsActive = P.lockBars;
        if (lockBarsActive > 0) lockStartPpq = std::floor (atPpq / 4.0 + 1.0e-9) * 4.0;
    }
    const bool locked = lockBarsActive > 0 && ! stepIsFill;   // fills stay live over a locked groove
    int sourceKey = lockKey (atPpq);
    if (locked)
    {
        const double win = 4.0 * (double) lockBarsActive;
        double rel = std::fmod (atPpq - lockStartPpq, win);
        if (rel < 0.0) rel += win;
        sourceKey = lockKey (lockStartPpq - win + rel);
    }

    const auto recorded = decisions[(size_t) sourceKey];
    if (locked && recorded.slice >= 0)
    {
        currentSlice = recorded.slice % segSlices;
        applyRetrig (recorded.div);
    }
    else
    {
        slice_n_inputs_t in {};
        in.num_slices = segSlices;
        in.current_slice = currentSlice;
        in.beat_position = beat;
        in.loop_bars = segLength;
        in.complexity = P.complexity;
        in.anchor = P.anchor;
        in.roll = P.roll;
        in.is_fill = stepIsFill ? 1 : 0;
        in.fill = stepFill;
        currentSlice = P.complexity == 0.0f ? beat : slice_n_next (&in, &BreakbeatEngine::randCallback, this);

        static constexpr int retriggerDivs[4] = { 2, 3, 4, 8 };
        const float triggersPerBar = (float) segSlices * speedFactor (segSpeed) / std::max (0.0625f, segLength);
        const float inv = 1.0f / triggersPerBar;
        int fired[4], firedCount = 0;
        for (int i = 0; i < 4; ++i)
        {
            const float pBar = P.retrig[(size_t) i];
            if (pBar <= 0.0f) continue;
            const float pTrig = pBar >= 1.0f ? 1.0f : 1.0f - std::pow (1.0f - pBar, inv);
            if (randCallback (this) < pTrig) fired[firedCount++] = i;
        }
        applyRetrig (firedCount > 0 ? retriggerDivs[fired[randomU32() % (uint32_t) firedCount]] : 0);
        if (locked) decisions[(size_t) sourceKey] = { (int8_t) currentSlice, (int8_t) (subSliceActive ? retriggerDivisions : 0) };
    }
    // the ring always holds what actually played
    if (! stepIsFill)
        decisions[(size_t) lockKey (atPpq)] = { (int8_t) currentSlice, (int8_t) (subSliceActive ? retriggerDivisions : 0) };

    if (auto* s = current())
        jumpTo ((double) currentSlice * (double) (s->totalFrames / segSlices));
    restartEnvelope (samplesPerTrigger (true) / (subSliceActive ? (double) retriggerDivisions : 1.0));
    emitNote (subSliceActive ? 110 : 100);
    logCut (atPpq, trigPpq, (double) currentSlice * 4.0 * segLength / segSlices, subSliceActive);
}

void BreakbeatEngine::handleNote (int slice) noexcept
{
    if (slice < 0 || slice >= segSlices) return;
    currentSlice = slice;
    subSliceActive = false;
    if (auto* s = current())
        jumpTo ((double) slice * (double) (s->totalFrames / segSlices));
    restartEnvelope (samplesPerTrigger (true));
    emitNote (100);
}

void BreakbeatEngine::emitNote (int vel) noexcept
{
    if (hitCount >= maxHits) return;
    Hit& h = hits[(size_t) hitCount++];
    h.sample = eventSample;
    h.slice = currentSlice;
    h.loop = currentLoop;
    h.vel = vel;
}

void BreakbeatEngine::logCut (double ppq, double beats, double srcBeat, bool roll) noexcept
{
    CutRecord& c = cutLog[(size_t) (cutWrite % cutHistory)];
    const double loopBeats = std::max (0.25, 4.0 * (double) segLength);
    c.ppq = ppq;
    c.beats = (float) beats;
    c.offset = (float) std::clamp (std::fmod (srcBeat, loopBeats) / loopBeats, 0.0, 1.0);
    c.roll = roll ? 1 : 0;
    c.loop = (uint8_t) currentLoop;
    c.fill = stepIsFill ? 1 : 0;
    ++cutWrite;
}

// ---------------------------------------------------------------------------
// SEMI port: BBCut cut modes
// ---------------------------------------------------------------------------
void BreakbeatEngine::nextBlock() noexcept
{
    bbcut::Params bp = P.bbcut;
    // a fill step pushes the procedure the way it pushes complexity in MOVE
    if (stepIsFill)
    {
        const float f = stepFill;
        bp.repeatsChance += (1.0f - bp.repeatsChance) * f;
        bp.straightChance *= 1.0f - f;
        bp.activity += (1.0f - bp.activity) * f;
        bp.rollChance += (1.0f - bp.rollChance) * f;
        bp.rollAllowed = std::max (bp.rollAllowed, 1.0f + 3.0f * f);
    }
    proc.chooseblock (bp, blk);
    cutIdx = 0;
    // where the block reads: its own place in the loop, or (scramble) a random eighth
    const double loopBeats = std::max (0.25, 4.0 * (double) segLength);
    double src = std::fmod (blk.phrasePos, loopBeats);
    if (randCallback (this) < P.scramble)
    {
        const int eighths = std::max (1, (int) std::lround (loopBeats * 2.0));
        src = 0.5 * (double) (randomU32() % (uint32_t) eighths);
    }
    blockSrcBeat = src;
}

void BreakbeatEngine::cutResync (double ppq) noexcept
{
    // the procedure starts at the bar line at or before ppq and is run forward to ppq
    proc.reset (cutMode - CutProc11, fixedSeed != 0 ? fixedSeed : randomU32() | 1u);
    double t = std::floor (std::max (0.0, ppq) / 4.0 + 1.0e-9) * 4.0;
    nextBlock();
    for (int guard = 0; guard < 100000; ++guard)
    {
        if (cutIdx >= blk.n) nextBlock();
        const double len = std::max (1.0e-4, (double) blk.cut[cutIdx]);
        if (t + len > ppq + 1.0e-9) break;
        t += len;
        ++cutIdx;
    }
    // ppq lies inside cut cutIdx, which started at t
    curCutPpq = t;
    curCutSrcBeat = blockSrcBeat;
    nextCutPpq = t + std::max (1.0e-4, (double) blk.cut[cutIdx]);
    ++cutIdx;
    resumeCut (ppq);
    if (ppq - t < 1.0e-6) emitNote (100);
}

void BreakbeatEngine::resumeCut (double atPpq) noexcept
{
    const auto* s = current();
    if (s == nullptr) return;
    const double loopBeats = std::max (0.25, 4.0 * (double) segLength);
    const double framesPerBeat = (double) s->totalFrames / loopBeats;
    const double rateK = P.tapeSpeed ? (double) speedFactor (segSpeed) : 1.0;
    const double into = std::max (0.0, atPpq - curCutPpq);
    const double pos = std::fmod (curCutSrcBeat + into * rateK, loopBeats) * framesPerBeat;
    jumpTo (pos);
    currentSlice = std::clamp ((int) (std::fmod (curCutSrcBeat, loopBeats) / loopBeats * segSlices), 0, segSlices - 1);
}

void BreakbeatEngine::fireCut (double atPpq) noexcept
{
    if (cutIdx >= blk.n) nextBlock();
    const double len = std::max (1.0e-4, (double) blk.cut[cutIdx]);
    ++cutIdx;
    curCutPpq = atPpq;
    curCutSrcBeat = blockSrcBeat;
    nextCutPpq = atPpq + len;
    const auto* s = current();
    if (s != nullptr)
    {
        const double loopBeats = std::max (0.25, 4.0 * (double) segLength);
        jumpTo (std::fmod (curCutSrcBeat, loopBeats) * (double) s->totalFrames / loopBeats);
        currentSlice = std::clamp ((int) (std::fmod (curCutSrcBeat, loopBeats) / loopBeats * segSlices), 0, segSlices - 1);
    }
    subSliceActive = false;
    restartEnvelope (len * sampleRate * 60.0 / (double) stableBpm);
    emitNote (blk.roll ? 110 : 100);
    logCut (atPpq, len, curCutSrcBeat, blk.roll);
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
static inline void readFrame (const SampleData& s, double pos, float& l, float& r) noexcept
{
    const int n = s.totalFrames;
    int i0 = (int) pos;
    if (i0 >= n || i0 < 0) i0 = 0;
    const int i1 = (i0 + 1 < n) ? i0 + 1 : 0;
    const float f = (float) (pos - std::floor (pos));
    const float* c0 = s.l.data();
    l = c0[i0] + (c0[i1] - c0[i0]) * f;
    if (s.numChannels > 1)
    {
        const float* c1 = s.r.data();
        r = c1[i0] + (c1[i1] - c1[i0]) * f;
    }
    else r = l;
}

void BreakbeatEngine::renderSpan (float* L, float* R, int from, int to) noexcept
{
    if (to <= from) return;
    const auto* s = current();
    if (s == nullptr || s->totalFrames <= 0)
    {
        std::fill (L + from, L + to, 0.0f);
        std::fill (R + from, R + to, 0.0f);
        return;
    }

    const double rate = playbackRate();
    const double total = (double) s->totalFrames;
    const auto sliceLen = (uint32_t) (s->totalFrames / std::max (1, segSlices));
    const float fadeStep = 1.0f / (float) fadeLength;
    const bool shaped = P.gate < 0.999f;
    const double minRamp = sampleRate * 0.0015;

    for (int i = from; i < to; ++i)
    {
        if (playPos >= total || playPos < 0.0) playPos = 0.0;

        float l, r;
        readFrame (*s, playPos, l, r);
        if (fadeInRemaining > 0)
        {
            const float g = 1.0f - (float) fadeInRemaining * fadeStep;
            l *= g; r *= g;
            --fadeInRemaining;
        }
        if (shaped)
        {
            // gate: full level for gate x hit, then fade over decay x hit
            const double gateLen = (double) P.gate * hitLen;
            const double decLen = std::max (minRamp, (double) P.decay * hitLen);
            const double over = envPos - gateLen;
            const float g = over <= 0.0 ? 1.0f : (float) std::max (0.0, 1.0 - over / decLen);
            l *= g * g; r *= g * g;
        }
        envPos += 1.0;
        if (tailRemaining > 0 && tailSample != nullptr)
        {
            if (tailPos >= (double) tailSample->totalFrames) tailPos = 0.0;
            float tl, tr;
            readFrame (*tailSample, tailPos, tl, tr);
            const float g = (float) tailRemaining * fadeStep;
            l += tl * g; r += tr * g;
            tailPos += tailRate;
            --tailRemaining;
        }
        L[i] = l;
        R[i] = r;

        if (subSliceActive)
        {
            const auto start = (uint32_t) currentSlice * sliceLen;
            const auto partLen = sliceLen / (uint32_t) retriggerDivisions;
            if (playPos >= (double) (start + partLen) && subSliceCounter < retriggerDivisions - 1)
            {
                eventSample = i;
                jumpTo ((double) start);
                ++subSliceCounter;
                envPos = 0.0;
                emitNote (90);
            }
        }
        playPos += rate;
    }
}

void BreakbeatEngine::process (float* outL, float* outR, int n,
                               const EngineParams& p, const TransportInfo& t,
                               const Pad* pads, int numPads)
{
    P = p;
    P.seqSteps = std::clamp (p.seqSteps, 1, maxSteps);
    P.intensity = std::clamp (p.intensity, 0.0f, 2.0f);
    P.chaos = std::clamp (p.chaos, 0.0f, 1.0f);
    P.gate = std::clamp (p.gate, 0.02f, 1.0f);
    P.decay = std::clamp (p.decay, 0.0f, 1.0f);
    hitCount = 0;
    eventSample = 0;

    bool running = false;
    double ppq = 0.0, bpm = 0.0;
    if (t.hostPlaying && t.hasPpq)
    {
        running = true; ppq = t.ppq; bpm = t.bpm > 0.0 ? t.bpm : 120.0;
        internalActive = false;
    }
    else if (p.internalRun)
    {
        if (! internalActive) { internalActive = true; internalPpq = 0.0; }
        running = true; ppq = internalPpq; bpm = p.internalBpm;
    }
    else
    {
        internalActive = false;
        bpm = t.bpm > 0.0 ? t.bpm : p.internalBpm;
    }
    stableBpm = (bpm >= 20.0 && bpm <= 400.0) ? (float) bpm : 120.0f;

    swapIncoming();

    // SEMI port: a new cut mode re-syncs like a locate
    const int wantMode = std::clamp (p.cutMode, 0, NumCutModes - 1);
    const bool modeChanged = wantMode != cutMode;
    cutMode = wantMode;

    bool justSynced = false;
    if (running && ! wasRunning)
    {
        resetTransport();
        resync (ppq);
        wasRunning = true;
        justSynced = true;
    }
    else if (running && (std::abs (ppq - expectedPpq) > 0.05 || modeChanged))
    {
        resetTransport();
        resync (ppq);
        justSynced = true;
    }
    else if (! running && wasRunning)
    {
        wasRunning = false;
        previewFrames = 0;
        subSliceActive = false;
    }

    if (running && ! justSynced)
    {
        const double sp = stepPpq();
        const auto shouldBe = (int64_t) std::floor (std::max (0.0, ppq) / sp + 1.0e-9);
        if (shouldBe < curStepAbs || shouldBe > curStepAbs + 1) curStepAbs = shouldBe;
        applyGridChanges (ppq);
    }
    else if (! running)
    {
        lastBars = barsOf (curSlot); lastSlices = slicesOf (curSlot);
    }

    if (previewRequest.exchange (false) && ! running)
        startPreview();

    if (! running)
    {
        if (previewFrames > 0)
        {
            const int m = std::min (n, previewFrames);
            renderSpan (outL, outR, 0, m);
            if (m < n)
            {
                std::fill (outL + m, outL + n, 0.0f);
                std::fill (outR + m, outR + n, 0.0f);
            }
            previewFrames -= m;
        }
        else
        {
            std::fill (outL, outL + n, 0.0f);
            std::fill (outR, outR + n, 0.0f);
        }
        publishStatus (false);
        status.ppq.store (ppq);
        return;
    }

    const double samplesPerPpq = sampleRate * 60.0 / (double) stableBpm;
    int padIdx = 0;
    auto nextPadSample = [&]() -> int
    {
        while (padIdx < numPads)
        {
            if (pads[padIdx].slice >= 0 && pads[padIdx].slice < segSlices)
                return std::clamp (pads[padIdx].sample, 0, n);
            ++padIdx;
        }
        return n;
    };
    auto toSample = [&] (double atPpq, int from) -> int
    {
        const double off = (atPpq - ppq) * samplesPerPpq;
        if (off >= (double) n) return n;
        return std::max (from, (int) std::ceil (off - 1.0e-7));
    };

    int pos = 0;
    for (int guard = 0; guard < 8192; ++guard)
    {
        const double trigAt = cutMode == CutMove ? segStartPpq + (double) nextTrig * trigPpq : nextCutPpq;
        const double stepAt = (double) (curStepAbs + 1) * stepPpq();
        const bool stepFirst = stepAt <= trigAt + 1.0e-9;
        const int evtSample = toSample (stepFirst ? stepAt : trigAt, pos);
        const int padSample = nextPadSample();
        const int evt = std::min (evtSample, padSample);
        if (evt >= n) break;

        renderSpan (outL, outR, pos, evt);
        pos = evt;
        eventSample = evt;

        if (padSample <= evtSample)
        {
            handleNote (pads[padIdx].slice);
            ++padIdx;
        }
        else if (stepFirst)
        {
            const bool restarted = handleStep (curStepAbs + 1, stepAt, true);
            // MOVE: a restarted segment's slice 0 replaces the trigger; cut modes keep their cut
            if ((! restarted || cutMode != CutMove) && std::abs (trigAt - stepAt) < 1.0e-9)
            {
                if (cutMode == CutMove) fireTrigger (trigAt);
                else fireCut (trigAt);
            }
        }
        else
        {
            if (cutMode == CutMove) fireTrigger (trigAt);
            else fireCut (trigAt);
        }
    }
    renderSpan (outL, outR, pos, n);

    expectedPpq = ppq + (double) n / samplesPerPpq;
    if (internalActive) internalPpq = expectedPpq;
    status.ppq.store (ppq);
    publishStatus (true);
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------
void BreakbeatEngine::publishStatus (bool running) noexcept
{
    status.slice.store (currentSlice);
    status.numSlices.store (segSlices);
    status.loop.store (currentLoop);
    status.slot.store (curSlot);
    status.retrigDiv.store (subSliceActive ? retriggerDivisions : 1);
    status.step.store ((int) (curStepAbs % std::max (1, P.seqSteps)));
    status.numSteps.store (P.seqSteps);
    status.phraseCount.store (curStepAbs / std::max (1, P.seqSteps));
    status.speed.store (segSpeed);
    status.fill.store (stepIsFill ? stepFill : 0.0f);
    status.running.store (running);
    status.locked.store (lockBarsActive > 0);
    status.previewing.store (! running && previewFrames > 0);
    status.internalClock.store (internalActive);
    status.bpm.store (stableBpm);
    status.loopBars.store (segLength);
    if (auto* s = current(); s != nullptr && s->totalFrames > 0)
        status.playhead.store ((float) (playPos / (double) s->totalFrames));
}

} // namespace bbeat
