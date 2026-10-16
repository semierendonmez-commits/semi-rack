// SEMI Family Rack port: value-row history is written at a stride (16 sixteenths / kHistory) instead
// of every block; marked "SEMI port" below.
#include "Router.h"
#include <cmath>

namespace conduit
{
void Router::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    hist.assign ((size_t) kRows * kHistory, Hist { 0, 0.0f });
    reset();
}

void Router::reset()
{
    for (auto& rr : env) for (auto& e : rr) e = Env {};
    numPending = 0;
    numTrigs = 0;
    dropped = 0;
    for (int r = 0; r < kRows; ++r)
    {
        rowValue[r] = 0.0f; rowHasValue[r] = false; rowLevel[r] = 0.0f; rowHits[r] = 0;
        histHead[r] = histCount[r] = 0; histLast[r] = 0;
        for (int c = 0; c < kCols; ++c) { prevValueOut[r][c] = 0.0f; pinLevel[r][c] = 0.0f; hitCount[r][c] = 0; }
    }
    for (int c = 0; c < kCols; ++c) { out[c] = 0.0f; gate[c] = false; }
}

double Router::samplesPer16() const
{
    const double bpm = blk.bpm > 1.0 ? blk.bpm : 120.0;
    return sr * 60.0 / bpm / 4.0;
}

void Router::begin (const Block& b)
{
    blk = b;
    if (b.sampleRate > 0.0 && b.sampleRate != sr) sr = b.sampleRate;
    numTrigs = 0;
}

void Router::hit (int row, int offset, float strength, double ppq)
{
    if (row < 0 || row >= kRows || patch.row[row].type != SrcEvent) return;
    offset = offset < 0 ? 0 : (offset >= blk.numSamples ? (blk.numSamples > 0 ? blk.numSamples - 1 : 0) : offset);
    ++rowHits[row];
    rowLevel[row] = strength;
    for (int c = 0; c < kCols; ++c)
        if (patch.pin[row][c].on && ! patch.col[c].empty())
            fire (row, c, offset, strength, ppq);
}

void Router::fire (int row, int col, int offset, float strength, double hitPpq)
{
    const auto& p = patch.pin[row][col];
    if (p.prob < 1.0f)
    {
        // the dice follow the song: the event's position when it has one (bounce twice, same result)
        const uint64_t moment = hitPpq >= 0.0 ? ((uint64_t) std::llround (hitPpq * 960.0) | (1ull << 62))
                                              : hitCount[row][col]++;
        if (dice (patch.seed, row, col, moment) >= p.prob) { ++dropped; return; }
    }
    const double delaySamples = (double) (p.delay > 0.0f ? p.delay : 0.0f) * samplesPer16();
    const uint64_t due = blk.sampleTime + (uint64_t) offset + (uint64_t) std::llround (delaySamples);
    if (due < blk.sampleTime + (uint64_t) blk.numSamples)
        apply (row, col, (int) (due - blk.sampleTime), strength);
    else if (numPending < kMaxPending)
        pending[numPending++] = { row, col, due, strength };
}

void Router::apply (int row, int col, int offset, float strength)
{
    const auto& p = patch.pin[row][col];
    if (patch.col[col].type == DstTrigger)
    {
        if (numTrigs < kMaxTrigs && p.amount != 0.0f)
            trigs[numTrigs++] = { col, offset, std::fmin (1.0f, strength * std::fabs (p.amount) * master) };
        return;
    }
    auto& e = env[row][col];
    e.start = blk.sampleTime + (uint64_t) offset;
    e.strength = strength;
    e.on = true;
}

void Router::value (int row, float v)
{
    if (row < 0 || row >= kRows) return;
    rowValue[row] = v;
    rowHasValue[row] = true;
}

float Router::historyAt (int row, uint64_t t) const
{
    const int n = histCount[row];
    if (n == 0) return rowValue[row];
    const Hist* h = hist.data() + (size_t) row * kHistory;
    auto at = [&] (int i) -> const Hist& { return h[(histHead[row] - n + i + kHistory) % kHistory]; };
    if (t <= at (0).t) return at (0).v;
    int lo = 0, hi = n - 1;
    while (lo < hi)
    {
        const int mid = (lo + hi + 1) / 2;
        if (at (mid).t <= t) lo = mid; else hi = mid - 1;
    }
    return at (lo).v;
}

void Router::end()
{
    const uint64_t t0 = blk.sampleTime, tEnd = blk.sampleTime + (uint64_t) blk.numSamples;
    const double sp16 = samplesPer16();

    // delayed hits that land in this block
    for (int i = 0; i < numPending;)
    {
        auto& q = pending[i];
        if (q.due < tEnd)
        {
            apply (q.row, q.col, q.due > t0 ? (int) (q.due - t0) : 0, q.strength);
            pending[i] = pending[--numPending];
        }
        else ++i;
    }

    float sum[kCols] {};

    // the moment the value rows' dice are thrown for: the 16th we are in
    const double pos16 = blk.playing ? std::floor ((blk.ppq + (double) blk.numSamples / (sp16 * 4.0)) * 4.0)
                                     : std::floor ((double) tEnd / sp16);
    const uint64_t moment16 = (uint64_t) (int64_t) pos16 ^ (1ull << 61);

    for (int r = 0; r < kRows; ++r)
    {
        const auto& src = patch.row[r];
        if (src.type == SrcValue)
        {
            const float v = rowValue[r];
            rowLevel[r] = v;
            // SEMI port: one history point per stride, so kHistory points span 16 sixteenths + margin
            const uint64_t stride = (uint64_t) (sp16 * 17.0 / (double) (kHistory - 1)) + 1;
            if (histCount[r] == 0 || t0 >= histLast[r] + stride)
            {
                Hist* h = hist.data() + (size_t) r * kHistory;
                h[histHead[r]] = { t0, v };
                histHead[r] = (histHead[r] + 1) % kHistory;
                if (histCount[r] < kHistory) ++histCount[r];
                histLast[r] = t0;
            }

            for (int c = 0; c < kCols; ++c)
            {
                const auto& p = patch.pin[r][c];
                if (! p.on || patch.col[c].empty()) { pinLevel[r][c] = 0.0f; continue; }
                const float vd = p.delay > 0.0f ? historyAt (r, t0 > (uint64_t) (p.delay * sp16) ? t0 - (uint64_t) (p.delay * sp16) : 0) : v;
                const bool open = p.prob >= 1.0f || dice (patch.seed, r, c, moment16) < p.prob;
                const float level = open && rowHasValue[r] ? p.amount * shape (p.curve, vd, false) : 0.0f;
                pinLevel[r][c] = level;
                if (patch.col[c].type == DstTrigger)
                {
                    const float a = std::fabs (level);
                    if (prevValueOut[r][c] < 0.5f && a >= 0.5f && numTrigs < kMaxTrigs)
                        trigs[numTrigs++] = { c, 0, std::fmin (1.0f, a * master) };
                    prevValueOut[r][c] = a;
                }
                else sum[c] += level;
            }
        }
        else if (src.type == SrcEvent)
        {
            float newest = 0.0f;
            for (int c = 0; c < kCols; ++c)
            {
                auto& e = env[r][c];
                const auto& p = patch.pin[r][c];
                if (! e.on || ! p.on || patch.col[c].empty() || patch.col[c].type == DstTrigger) { pinLevel[r][c] = 0.0f; e.on = e.on && p.on; continue; }
                const double t16 = (double) (tEnd - e.start) / sp16;
                const float len = p.len > 0.01f ? p.len : 0.01f;
                float x = 1.0f - (float) (t16 / (double) len);
                if (x <= 0.0f)
                {
                    if (e.start >= t0) x = 1.0e-3f;    // shorter than the block: still one block long
                    else { e.on = false; pinLevel[r][c] = 0.0f; continue; }
                }
                const float level = p.amount * e.strength * shape (p.curve, x, true);
                pinLevel[r][c] = level;
                sum[c] += level;
                newest = std::fmax (newest, x * e.strength);
            }
            rowLevel[r] = rowLevel[r] * 0.8f;   // the UI's hit flash fades
            (void) newest;
        }
    }

    for (int c = 0; c < kCols; ++c)
    {
        float v = sum[c] * master;
        v = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
        out[c] = v;
        gate[c] = patch.col[c].type == DstGate && v > 0.5f;
    }
}

} // namespace conduit
