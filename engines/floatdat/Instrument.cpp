// SEMI port: see Instrument.h. Ported from FLOATDAT Source/PluginProcessor.cpp (processBlock, fireLane,
// moveHead, feedEngines, fltHit, fltDataPatch, pinballFunction, ikdRandomise, loadFactory, the layout's
// defaults). juce::Random -> xorshift; LinearSmoothedValue -> one-pole (30 ms); MIDI out -> LaneGate.
#include "Instrument.h"
#include "IkdFactory.h"
#include <algorithm>
#include <cmath>

namespace fdat
{
using namespace fdat_pdata;
namespace ry = fdat_ryoz;

const std::vector<std::string>& destNames()
{
    static const std::vector<std::string> n { "midi only", "ikd step", "ikd mutate", "flt hit", "flt burst", "flt mutate", "flt data patch",
                                              "flt unflip", "vtx kick", "vtx tone", "vtx ghost", "jdat tick", "jdat clear", "data step",
                                              "data jump", "data region", "vis cut", "vis flash", "all tick" };
    return n;
}
const std::vector<std::string>& trigNames() { static const std::vector<std::string> n { "none", "trig1", "trig2", "trig3", "trig4", "trig5", "trig6", "trig7", "trig8", "mute" }; return n; }
const std::vector<std::string>& engineClockNames() { static const std::vector<std::string> n { "own", "grid", "pinball" }; return n; }
const std::vector<std::string>& fltClockNames() { static const std::vector<std::string> n { "own", "pinball", "both" }; return n; }
const std::vector<std::string>& fltMapNames() { static const std::vector<std::string> n { "same lane", "by step", "by data", "round robin" }; return n; }
const std::vector<std::string>& feedNames() { static const std::vector<std::string> n { "off", "on load", "on move" }; return n; }
const std::vector<std::string>& divNames() { static const std::vector<std::string> n { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }; return n; }
const std::vector<std::string>& autoNames() { static const std::vector<std::string> n { "off", "1", "2", "4", "8", "16", "32", "64" }; return n; }
const std::vector<std::string>& sourceNames() { static const std::vector<std::string> n { "none", "sine", "noise", "data" }; return n; }
const std::vector<std::string>& interpNames() { static const std::vector<std::string> n { "none", "linear", "cubic" }; return n; }
const std::vector<std::string>& writeNames() { static const std::vector<std::string> n { "random", "data trit", "data scatter", "data raw" }; return n; }
const std::vector<std::string>& loopNames() { static const std::vector<std::string> n { "off", "16", "32", "64", "128", "256" }; return n; }
const std::vector<std::string>& scanNames() { static const std::vector<std::string> n { "hold", "step", "run", "region" }; return n; }
const std::vector<std::string>& stepSizeNames() { static const std::vector<std::string> n { "1 bit", "1 byte", "1 row", "ikd block", "flt block", "1 kbyte" }; return n; }
const std::vector<std::string>& vtxFeedNames() { static const std::vector<std::string> n { "off", "tuning", "tuning + timbre" }; return n; }
const std::vector<std::string>& factoryNames()
{
    static const std::vector<std::string> n { "floatdat", "free originals", "markov drunk", "data rain", "region walk", "floater case", "barcode grid",
                                              "datamatic burst", "nuclear cave", "reel turing" };
    return n;
}
const std::vector<std::string>& sceneNames()
{
    static const std::vector<std::string> n { "barcode", "matrix", "space", "network", "scatter", "field", "scan", "numbers",
                                              "tunnel", "interference", "codex", "superposition", "tiles", "bitmap", "ticker", "horizon",
                                              "case", "reel", "orbit", "attractor", "vortex", "ghost" };
    return n;
}
const std::vector<std::string>& visAutoNames() { static const std::vector<std::string> n { "manual", "every bar", "every 4 bars", "on step", "random", "on onset" }; return n; }

namespace
{
const double kDivBeats[] { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
const int kAutoSteps[] { 0, 1, 2, 4, 8, 16, 32, 64 };
const int kLoopTicks[] { 0, 16, 32, 64, 128, 256 };
const uint64_t kStepBits[] { 1, 8, 64, 576, 2048, 8192 };
const int kDefaultDest[pb::kLanes] { dIkdStep, dFltHit, dJdatTick, dVtxTone, dFltBurst, dVtxKick, dDataStep, dVisCut };

float dbToGain (float db) { return db <= -59.9f ? 0.0f : std::pow (10.0f, db / 20.0f); }
template <typename T> T lim (T lo, T hi, T v) { return std::max (lo, std::min (hi, v)); }

uint64_t spreadBits (int width, int n)
{
    uint64_t w = 0;
    n = lim (0, width, n);
    for (int k = 0; k < n; ++k)
        w |= uint64_t (1) << lim (0, width - 1, (int) ((double) k * width / std::max (1, n)));
    return w;
}
int popcount32 (uint32_t m) { int c = 0; for (; m; m &= m - 1) ++c; return c; }
} // namespace

Settings::Settings()
{
    for (int l = 0; l < pb::kLanes; ++l) { lanes[(size_t) l].note = 36 + l; lanes[(size_t) l].choke = true; lanes[(size_t) l].dest = kDefaultDest[l]; }
}

float Instrument::Symbols::next() noexcept
{
    if (blob == nullptr || blob->symbols.empty()) return 0.0f;
    const uint64_t i = pos++;
    if (! trit) return blob->symbolAt (i);
    if (blob->used == ReadAs::Audio || blob->used == ReadAs::Image)
    {
        const float v = blob->symbolAt (i);
        return v > 0.33f ? 1.0f : (v < -0.33f ? -1.0f : 0.0f);
    }
    return (float) ((int) (blob->byteAt (i) % 3u) - 1);
}

struct Instrument::Block
{
    int n = 0;
    double sixteenth = 1.0;
    std::array<ry::Engine::Controls::ExtStep, 128> ikdExt {};
    int nIkd = 0;
    std::array<fdat_jdat::ExtTick, 128> jdatExt {};
    int nJdat = 0;
    void addIkd (int o, double gateMs, float acc) { if (nIkd < (int) ikdExt.size()) ikdExt[(size_t) nIkd++] = { o, gateMs, acc }; }
    void addJdat (int o, float acc) { if (nJdat < (int) jdatExt.size()) jdatExt[(size_t) nJdat++] = { o, acc }; }
};

//==============================================================================
Instrument::Instrument()
{
    buf.assign ((size_t) kMaxBlock * 9, 0.0f);
    setSeed (0x5eed1234u);
    loadFactory (0);
}

void Instrument::setSeed (uint32_t s)
{
    pinball.seed (s);
    fltRobin = (int) (s % 8u);
    jdat.seed (s * 7u + 1u);
    vtx.seed (s * 31u + 7u);
    laneRng.seed (s * 13u + 5u);
    rs = s ? s : 1u;
    fltCase.seedRandom (s * 17u + 3u);
}

void Instrument::prepare (double sampleRate)
{
    sr = sampleRate > 0 ? sampleRate : 44100.0;
    ikd.prepare (sr);
    jdat.prepare (sr);
    vtx.prepare (sr);
    numVtxHits = 0;
    pinball.prepare (sr);
    flt.prepare (sr, kMaxBlock);
    numFltBursts = 0;
    smVtx = dbToGain (S.lvlVtx); smMaster = dbToGain (S.master); smIkd = dbToGain (S.lvlIkd); smFlt = dbToGain (S.lvlFlt); smJdat = dbToGain (S.lvlJdat);
    numPending = 0;
    wasPlaying = false;
    seenSerial = 0;
}

float Instrument::accentFor (int velocity) const
{
    const float depth = (float) S.velDepth * 0.01f;
    return lim (0.0f, 1.27f, 1.0f + depth * ((float) velocity / 100.0f - 1.0f));
}

void Instrument::moveHead (uint64_t newHead, bool feed)
{
    const auto* b = current;
    if (b == nullptr || b->nbits == 0) return;
    head = newHead % b->nbits;
    ++tele.dataMoves;
    if (feed) feedEngines (false);
}

void Instrument::feedEngines (bool onLoad)
{
    const auto* b = current;
    if (b == nullptr || b->nbits == 0) return;
    const uint64_t h = head;
    auto wants = [onLoad] (int f) { return onLoad ? f >= 1 : f == 2; };
    if (wants (S.ikdFeed))
    {
        for (int v = 0; v < ry::kNumVoices; ++v)
        {
            if (ikdLocks[(size_t) v]) continue;
            auto& vs = ikdShared.voices[(size_t) v];
            vs.cells.store (b->word64 (h + (uint64_t) v * 64u), std::memory_order_relaxed);
            if (S.ikdFeedSnd)
            {
                const uint64_t base = b->byteForBit (h) + 1152u + (uint64_t) v * 3u;
                vs.pitch.store (spreadBits (ry::kPitchBits, 4 + b->byteAt (base) * 30 / 255), std::memory_order_relaxed);
                vs.gain.store (spreadBits (ry::kGainBits, 2 + b->byteAt (base + 1) * 14 / 255), std::memory_order_relaxed);
                vs.decay.store (spreadBits (ry::kDecayBits, b->byteAt (base + 2) * 25 / 255), std::memory_order_relaxed);
            }
        }
    }
    if (wants (S.fltFeed)) fltFeedPending = true;
    if (wants (S.jdatFeed) && ! b->symbols.empty())
    {
        symbols.blob = b;
        symbols.pos = (uint64_t) ((double) h / (double) b->nbits * (double) b->symbols.size());
        symbols.trit = S.jdatWrite != 3;
        jdat.seedBuffers (symbols);
    }
}

void Instrument::fireLane (int lane, int velocity, float durMs, int offset, bool midi, Block& blk)
{
    lane = lim (0, pb::kLanes - 1, lane);
    ++tele.laneHits[(size_t) lane];
    tele.laneVel[(size_t) lane] = velocity;
    // the plug-in's [makenote] -> [midiout]: a gate for the module
    if (midi && S.pbOut && velocity > 0 && nGates < (int) laneGates.size())
        laneGates[(size_t) nGates++] = { offset, lane, velocity, std::max (1.0f, durMs) };

    const float acc = accentFor (velocity);
    const int dest = S.lanes[(size_t) lane].dest;
    const auto* b = current;
    const uint64_t stepBitsNow = kStepBits[lim (0, 5, S.dataStep)];
    switch (dest)
    {
        case dIkdStep:   blk.addIkd (offset, durMs, acc); break;
        case dIkdMutate:
        {
            int cand[ry::kNumVoices], nc = 0;
            for (int v = 0; v < ry::kNumVoices; ++v) if (! ikdLocks[(size_t) v]) cand[nc++] = v;
            if (nc > 0) ikdShared.voices[(size_t) cand[laneRng.below (nc)]].mutateRequests.fetch_add (1, std::memory_order_relaxed);
            break;
        }
        case dFltHit:    fltHit (lane, velocity, offset, lane, false); break;
        case dFltBurst:  fltHit (lane, velocity, offset, lane, true); break;
        case dFltMutate: ++fltMutateRequests; break;
        case dFltPatch:  ++fltPatchRequests; break;
        case dFltUnflip: fltCase.resetMutations(); break;
        case dVtxKick:
        case dVtxTone:
        case dVtxGhost:
            if (numVtxHits < (int) vtxHits.size())
                vtxHits[(size_t) numVtxHits++] = { offset, dest - dVtxKick, acc, std::max (20.0f, durMs) };
            break;
        case dJdatTick:  blk.addJdat (offset, acc); break;
        case dJdatClear: jdat.clearBuffers(); break;
        case dDataStep:  moveHead (head + stepBitsNow, true); break;
        case dDataJump:
            if (b != nullptr && b->nbits > 0) moveHead ((uint64_t) (laneRng.unit() * (double) b->nbits) & ~(uint64_t) 63, true);
            break;
        case dDataRegion:
            if (b != nullptr && b->nbits > 0)
                moveHead ((b->nbits / 16u * (uint64_t) std::max (0, pinball.currentStep.load())) & ~(uint64_t) 63, true);
            break;
        case dVisCut:   ++tele.sceneCuts; break;
        case dVisFlash: ++tele.flashes; break;
        case dAllTick:
            blk.addIkd (offset, durMs, acc);
            fltHit (lane, velocity, offset, lane, false);
            blk.addJdat (offset, acc);
            break;
        default: break;
    }
}

void Instrument::fltHit (int lane, int velocity, int offset, int pbLane, bool burst)
{
    int target = lane;
    switch (S.fltMap)
    {
        case 1: target = std::max (0, pinball.currentStep.load()) % flt::kLanes; break;
        case 2: if (const auto* b = current; b != nullptr) target = b->byteAt (b->byteForBit (head) + (uint64_t) pbLane) % flt::kLanes; break;
        case 3: target = fltRobin; fltRobin = (fltRobin + 1) % flt::kLanes; break;
        default: break;
    }
    target = lim (0, flt::kLanes - 1, target);
    const float depth = (float) S.fltVel * 0.01f;
    const int vel = lim (1, 127, (int) std::lround (100.0f + depth * (float) (velocity - 100)));
    if (numFltHits < (int) fltHits.size()) fltHits[(size_t) numFltHits++] = { std::max (0, offset), target, vel };
    ++tele.fltHits;
    if (burst)
    {
        const int count = S.fltBurst;
        double gap = 60.0 / std::max (20.0, tele.bpm) / 8.0 * sr;
        int64_t at = sampleClock + offset;
        for (int k = 1; k < count && numFltBursts < (int) fltBursts.size(); ++k)
        {
            at += (int64_t) gap;
            gap *= 0.72;
            fltBursts[(size_t) numFltBursts++] = { at, target, std::max (8, vel - 14 * k) };
        }
    }
}

//==============================================================================
void Instrument::process (float* L, float* R, int n, const Transport& tr)
{
    n = std::min (n, kMaxBlock);
    Block blk;
    blk.n = n;
    nGates = 0;
    numFltHits = 0;

    // ---- FLT: what the plug-in does on the message thread
    if (fltNewRequest) { fltNewRequest = false; fltCase.newPatch(); }
    if (fltFeedPending) { fltFeedPending = false; fltDataPatch (true, S.fltFeedSnd, S.fltFeedModel); }
    if (fltMutateRequests > 0) { fltCase.mutate (std::min (0.6f, 0.12f * (float) fltMutateRequests)); fltMutateRequests = 0; }
    if (fltPatchRequests > 0) { fltPatchRequests = 0; fltDataPatch (true, true, S.fltFeedModel); }

    // ---- transport
    const bool run = S.run;
    const bool useHost = tr.host;
    const double bpm = useHost && tr.bpm > 0.0 ? tr.bpm : (double) S.bpm;
    const bool playing = useHost ? tr.playing : run;
    double ppq = useHost ? tr.ppq : freePpq;
    const double ppqPerSample = bpm / 60.0 / sr;
    const double ppqEnd = ppq + ppqPerSample * n;
    blk.sixteenth = 60.0 / bpm / 4.0 * sr;
    tele.bpm = bpm; tele.ppq = ppq; tele.playing = playing;

    // ---- data: a new blob?
    if (const auto* b = current; b != nullptr && b->serial != seenSerial)
    {
        seenSerial = b->serial;
        head = b->firstLively();
        headFrac = 0.0;
        lastFedRow = ~0ull;
        symbols.blob = b;
        symbols.pos = 0;
        feedEngines (true);
    }
    if (headRequest >= 0)
    {
        const auto hr = (uint64_t) headRequest;
        headRequest = -1;
        headFrac = 0.0;
        moveHead (hr, true);
        if (S.ikdFeed == 1 || S.fltFeed == 1) feedEngines (true);
    }
    if (laneFireRequests != 0)
    {
        for (int l = 0; l < pb::kLanes; ++l)
            if (laneFireRequests & (1u << l)) fireLane (l, laneFireVel[(size_t) l], (float) (S.steps[0].dur * blk.sixteenth / sr * 1000.0), 0, true, blk);
        laneFireRequests = 0;
    }

    // ---- pinball
    for (int s = 0; s < pb::kSteps; ++s)
    {
        auto& sp = pinball.steps[(size_t) s];
        const auto& st = S.steps[(size_t) s];
        sp.trig = st.trig; sp.velocity = st.vel; sp.trigProb = st.prob; sp.duration = st.dur; sp.delay = st.dly; sp.delayProb = st.dprob;
    }
    const double mult = S.pbMult;
    const int startStep = S.pbStart;
    const double patternBeats = std::max (0.5, (double) S.pbLen) * 4.0;
    pb::EventList<512> pev;
    std::array<int, 64> resets {};
    int nResets = 0;
    if (playing && ! wasPlaying) resets[(size_t) nResets++] = 0;
    if (playing)
    {
        for (int64_t k = (int64_t) std::floor (ppq / patternBeats) + 1; (double) k * patternBeats < ppqEnd && nResets < 63; ++k)
        {
            const int off = lim (0, n - 1, (int) std::floor (((double) k * patternBeats - ppq) / ppqPerSample));
            if (nResets == 0 || resets[(size_t) nResets - 1] != off) resets[(size_t) nResets++] = off;
        }
        if (wasPlaying && std::abs (std::fmod (ppq, patternBeats)) < 1.0e-9 && (nResets == 0 || resets[0] != 0))
        {
            for (int i = nResets; i > 0; --i) resets[(size_t) i] = resets[(size_t) i - 1];
            resets[0] = 0; ++nResets;
        }
    }
    const bool manualReset = resetRequest, manualHalt = haltRequest;
    resetRequest = haltRequest = false;
    if (! playing && wasPlaying)
    {
        pb::EventList<512> tmp;
        pinball.halt (0, tmp);
        numPending = 0;
    }
    pinball.setTiming (blk.sixteenth, mult);
    if (manualHalt) pinball.halt (0, pev);
    if (manualReset) pinball.reset (0, startStep, pev);
    if (jumpRequest >= 0) { pinball.jump (jumpRequest, 0, pev); jumpRequest = -1; }
    if (playing || manualReset || pinball.currentStep.load() >= 0)
    {
        int pos = 0, ri = 0;
        while (pos < n)
        {
            const int segEnd = ri < nResets ? resets[(size_t) ri] : n;
            if (segEnd > pos)
            {
                const int before = pev.count;
                pinball.process (segEnd - pos, blk.sixteenth, mult, pev);
                for (int i = before; i < pev.count; ++i) pev.items[(size_t) i].offset += pos;
                pos = segEnd;
            }
            if (ri < nResets && resets[(size_t) ri] == pos)
            {
                pinball.reset (pos, startStep, pev);
                ++ri;
            }
            else if (segEnd >= n) break;
        }
    }
    const double swing = S.pbSwing;
    const int scan = S.dataScan;
    for (int i = 0; i < pev.count; ++i)
    {
        const auto& e = pev.items[(size_t) i];
        switch (e.type)
        {
            case pb::Event::Enter:
                if (scan == 1) moveHead (head + kStepBits[lim (0, 5, S.dataStep)], true);
                if (scan == 3 && current != nullptr && current->nbits > 0)
                    moveHead ((current->nbits / 16u * (uint64_t) std::max (0, (int) e.step)) & ~(uint64_t) 63, true);
                break;
            case pb::Event::Fire:
                ++tele.stepFires[(size_t) lim (0, 15, (int) e.step)];
                break;
            case pb::Event::Trig:
            {
                const double beat = ppq + ppqPerSample * e.offset;
                const auto g = (int64_t) std::floor (beat * 4.0 / mult + 1.0e-9);
                int64_t at = sampleClock + e.offset;
                if ((g & 1) && swing > 0.0) at += (int64_t) (swing * 0.5 * blk.sixteenth * mult);
                if (numPending < (int) pending.size()) pending[(size_t) numPending++] = { at, e.lane, e.velocity, e.durMs, true };
                break;
            }
            case pb::Event::Choke:
                for (int l = 0; l < pb::kLanes; ++l)
                    if ((e.chokeMask & (1u << l)) && S.lanes[(size_t) l].choke && nGates < (int) laneGates.size())
                        laneGates[(size_t) nGates++] = { e.offset, l, 0, 0.0f };   // velocity 0: the gate closes
                break;
            case pb::Event::Reset:
                numPending = 0;
                for (int l = 0; l < pb::kLanes && nGates < (int) laneGates.size(); ++l) laneGates[(size_t) nGates++] = { e.offset, l, 0, 0.0f };
                break;
            default: break;
        }
    }
    for (int i = 0; i < numPending;)
    {
        auto& pf = pending[(size_t) i];
        if (pf.at < sampleClock + n)
        {
            fireLane (pf.lane, pf.velocity, pf.durMs, (int) lim<int64_t> (0, n - 1, pf.at - sampleClock), pf.midi, blk);
            pending[(size_t) i] = pending[(size_t) --numPending];
        }
        else ++i;
    }

    // ---- data scan RUN
    if (scan == 2 && playing && current != nullptr && current->nbits > 0)
    {
        headFrac += (double) S.dataRate * 64.0 * n / sr;
        if (headFrac >= 1.0)
        {
            const auto adv = (uint64_t) headFrac;
            headFrac -= (double) adv;
            const uint64_t nh = (head + adv) % current->nbits;
            head = nh;
            if (nh / 64u != lastFedRow) { lastFedRow = nh / 64u; moveHead (nh, true); }
        }
    }

    // ---- render
    float* ikL = buf.data();
    float* ikR = ikL + kMaxBlock;
    float* jdL = ikR + kMaxBlock;
    float* jdR = jdL + kMaxBlock;
    float* flL = jdR + kMaxBlock;
    float* flR = flL + kMaxBlock;
    float* vxL = flR + kMaxBlock;
    float* vxR = vxL + kMaxBlock;
    float* exc = vxR + kMaxBlock;
    auto sortByOffset = [] (auto* a, int cnt)
    {
        for (int i = 1; i < cnt; ++i)
            for (int j = i; j > 0 && a[j].offset < a[j - 1].offset; --j) std::swap (a[j], a[j - 1]);
    };
    sortByOffset (blk.ikdExt.data(), blk.nIkd);
    sortByOffset (blk.jdatExt.data(), blk.nJdat);

    // IKD
    {
        ry::Engine::Controls c;
        const int clk = S.ikdClk;
        c.run = run;
        c.hostSync = clk == 1;
        c.external = clk == 2;
        c.divBeats = kDivBeats[lim (0, 5, S.ikdDiv)];
        c.swing = S.ikdSwing * 0.01;
        c.clip = false;
        c.hold = S.ikdHold;
        c.autoEvery = kAutoSteps[lim (0, 7, S.ikdAuto)];
        c.autoChance = (float) S.ikdChance * 0.01f;
        const float g[3] { dbToGain (S.ikdWave), dbToGain (S.ikdSpec), dbToGain (S.ikdSine) };
        for (int v = 0; v < ry::kNumVoices; ++v)
        {
            c.voiceGain[(size_t) v] = g[v / 3];
            ikd.setLocked (v, ikdLocks[(size_t) v]);
        }
        c.ext = blk.ikdExt.data();
        c.numExt = blk.nIkd;
        ry::Engine::HostInfo h;
        h.valid = true; h.playing = playing; h.bpm = bpm; h.ppq = ppq;
        const int stepBefore = ikdShared.step.load();
        ikd.process (ikL, ikR, n, h, c);
        if (ikdShared.step.load() != stepBefore) ++tele.ikdSteps;
    }

    // FLT on our transport: "own" its lanes on the clock, "pinball" only the struck lanes, "both"
    {
        for (int i = 0; i < numFltBursts;)
        {
            auto& fb = fltBursts[(size_t) i];
            if (fb.at < sampleClock + n)
            {
                if (numFltHits < (int) fltHits.size()) fltHits[(size_t) numFltHits++] = { (int) lim<int64_t> (0, n - 1, fb.at - sampleClock), fb.lane, fb.vel };
                fltBursts[(size_t) i] = fltBursts[(size_t) --numFltBursts];
            }
            else ++i;
        }
        for (int k = 0; k < numFltHits; ++k) flt.hit (fltHits[(size_t) k].lane, (float) fltHits[(size_t) k].vel / 127.0f, fltHits[(size_t) k].offset);
        flt::Params fp = fltCase.currentParams();
        fp.run = false;          // FLOATDAT owns the transport
        flt.setParams (fp);
        flt::Transport ft;
        ft.playing = playing && S.fltClk != 1;
        ft.hostHasPosition = ft.playing;
        ft.bpm = bpm;
        ft.ppq = ppq;
        flt.process (flL, flR, n, ft);
        for (int l = 0; l < flt::kLanes; ++l) tele.fltLaneHits[(size_t) l] = (uint32_t) flt.display.fired[(size_t) l].load (std::memory_order_relaxed);
    }

    // JDAT
    {
        fdat_jdat::Params jp;
        const int clk = S.jdatClk;
        jp.run = run;
        jp.sync = clk == 1;
        jp.external = clk == 2;
        jp.ext = blk.jdatExt.data();
        jp.numExt = blk.nJdat;
        jp.rateMs = S.jdatRate;
        jp.divBeats = kDivBeats[lim (0, 5, S.jdatDiv)];
        for (int i = 0; i < 4; ++i)
        {
            jp.src[(size_t) i] = S.jdatSrc[(size_t) i];
            jp.level[(size_t) i] = dbToGain (S.jdatLvl[(size_t) i]);
        }
        if (S.jdatLink) jp.src[2] = jp.src[0];
        jp.master = 1.0f;
        jp.clip = false;
        jp.clearProb = S.jdatClear;
        jp.jumpProb = S.jdatJump;
        jp.hold = S.jdatHold;
        jp.interp = S.jdatInterp;
        jp.loopTicks = kLoopTicks[lim (0, 5, S.jdatLoop)];
        const int wm = S.jdatWrite;
        jp.writeMode = wm == 0 ? fdat_jdat::writeRandom : (wm == 2 ? fdat_jdat::writeDataScatter : fdat_jdat::writeDataSeq);
        symbols.trit = wm != 3;
        if (symbols.blob != current) symbols.blob = current;
        jp.symbols = &symbols;
        fdat_jdat::Transport t;
        t.valid = true; t.playing = playing; t.ppq = ppq; t.bpm = bpm;
        const auto ticksBefore = jdat.tickCount.load();
        jdat.process (jdL, jdR, n, jp, t, nullptr);
        if (jdat.tickCount.load() != ticksBefore) ++tele.jdatTicks;
    }

    // VTX: the other layers excite the ghost network; data can tune the voices
    {
        for (int i = 0; i < n; ++i) exc[i] = (ikL[i] + ikR[i] + flL[i] + flR[i] + jdL[i] + jdR[i]) * (1.0f / 3.0f);
        fdat_vtx::Params vp;
        vp.kickLevel = dbToGain (S.vtxKickLvl); vp.toneLevel = dbToGain (S.vtxToneLvl);
        vp.ghostLevel = dbToGain (S.vtxGhostLvl); vp.driftLevel = dbToGain (S.vtxDriftLvl);
        vp.kickOwn = S.vtxKickOwn && playing; vp.kickRate = S.vtxKickRate; vp.kickProb = S.vtxKickProb;
        vp.kickPitch = S.vtxKickPitch; vp.kickDecay = S.vtxKickDecay; vp.kickDrive = S.vtxKickDrive;
        vp.kickRevmix = S.vtxKickRev; vp.kickPredelay = S.vtxKickPre;
        vp.toneOwn = S.vtxToneOwn && playing; vp.toneProb = S.vtxToneProb; vp.toneFreq = S.vtxToneFreq;
        vp.toneFeedback = S.vtxToneFb; vp.toneCollapse = S.vtxToneCollapse; vp.toneMutation = S.vtxToneMut; vp.toneCrush = S.vtxToneCrush;
        vp.gSens = S.vtxGSens; vp.gFeedback = S.vtxGFb; vp.gPitch = S.vtxGPitch; vp.gDetune = S.vtxGDetune;
        vp.gRate = S.vtxGRate; vp.gBright = S.vtxGBright; vp.gSelf = S.vtxGSelf; vp.gSat = S.vtxGSat; vp.gInput = S.vtxGInput;
        vp.driftFreq = S.vtxDriftFreq; vp.driftDetune = S.vtxDriftDetune;
        const int feed = S.vtxFeed;
        if (const auto* b = current; feed > 0 && b != nullptr && b->nbits > 0)
        {
            const uint64_t hb = b->byteForBit (head);
            auto u = [b, hb] (uint64_t k) { return (float) b->byteAt (hb + k) / 255.0f; };
            vp.toneFreq = 80.0f * std::pow (100.0f, u (1100));
            vp.kickPitch = 38.0f + 52.0f * u (1101);
            vp.gPitch = 150.0f * std::pow (20.0f, u (1102));
            vp.driftFreq = 27.5f * std::pow (2.0f, std::round (u (1103) * 24.0f) / 12.0f);
            if (feed > 1)
            {
                vp.toneMutation = lim (0.0f, 1.0f, (float) b->entropy / 8.0f);
                vp.toneCrush = lim (0.0f, 0.9f, 1.0f - (float) b->printable);
                vp.toneCollapse = 0.05f + 0.6f * u (1104);
                vp.gDetune = 0.92f * u (1105);
                vp.gBright = u (1106);
            }
        }
        auto* hh = vtxHits.data();
        for (int i = 1; i < numVtxHits; ++i)
            for (int j = i; j > 0 && hh[j].offset < hh[j - 1].offset; --j) std::swap (hh[j], hh[j - 1]);
        vtx.process (vxL, vxR, n, exc, vp, hh, numVtxHits);
        numVtxHits = 0;
    }

    // ---- mix
    const float smK = 1.0f - std::exp (-1.0f / (0.03f * 0.25f * (float) sr));   // ~ a 30 ms ramp
    const float tV = dbToGain (S.lvlVtx), tI = dbToGain (S.lvlIkd), tF = dbToGain (S.lvlFlt), tJ = dbToGain (S.lvlJdat), tM = dbToGain (S.master);
    const float dcR = (float) std::exp (-2.0 * M_PI * 8.0 / sr);
    const float rel = (float) std::exp (-1.0 / (0.12 * sr));
    double sIk = 0, sIg = 0, sJd = 0, sVx = 0, sOut = 0;
    float pk = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        smIkd += (tI - smIkd) * smK; smFlt += (tF - smFlt) * smK; smJdat += (tJ - smJdat) * smK; smVtx += (tV - smVtx) * smK; smMaster += (tM - smMaster) * smK;
        const float il = ikL[i] * smIkd, ir = ikR[i] * smIkd, gl = flL[i] * smFlt, gr = flR[i] * smFlt, jl = jdL[i] * smJdat, jr = jdR[i] * smJdat;
        sIk += il * il + ir * ir; sIg += gl * gl + gr * gr; sJd += jl * jl + jr * jr;
        const float vl = vxL[i] * smVtx, vr = vxR[i] * smVtx;
        sVx += vl * vl + vr * vr;
        float l = (il + gl + jl + vl) * smMaster, r = (ir + gr + jr + vr) * smMaster;
        if (S.dcBlock)
        {
            const float yl = l - dcX[0] + dcR * dcY[0], yr = r - dcX[1] + dcR * dcY[1];
            dcX[0] = l; dcX[1] = r; dcY[0] = yl; dcY[1] = yr;
            l = yl; r = yr;
        }
        if (S.limit)
        {
            const float pkNow = std::max (std::abs (l), std::abs (r));
            const float want = pkNow > 0.98f ? 0.98f / pkNow : 1.0f;
            limEnv = want < limEnv ? want : want + (limEnv - want) * rel;
            l *= limEnv; r *= limEnv;
        }
        if (S.clip) { l = lim (-1.0f, 1.0f, l); r = lim (-1.0f, 1.0f, r); }
        if (! std::isfinite (l) || ! std::isfinite (r)) { l = r = 0.0f; }
        sOut += l * l + r * r;
        pk = std::max (pk, std::max (std::abs (l), std::abs (r)));
        L[i] = l;
        R[i] = r;
    }
    auto smooth = [n] (float& a, double sum) { a = std::max ((float) std::sqrt (sum / std::max (1, 2 * n)), a * 0.85f); };
    smooth (tele.rmsVtx, sVx); smooth (tele.rmsIkd, sIk); smooth (tele.rmsFlt, sIg); smooth (tele.rmsJdat, sJd); smooth (tele.rmsOut, sOut);
    tele.peakOut = std::max (pk, tele.peakOut * 0.9f);

    if (! useHost && playing) freePpq = ppqEnd;
    if (! playing && ! useHost) freePpq = 0.0;
    wasPlaying = playing;
    sampleClock += n;
}

//==============================================================================
void Instrument::pinballFunction (Fn f)
{
    auto setRow = [this] (int x, std::initializer_list<std::pair<int, float>> w)
    {
        for (int y = 0; y < pb::kStates; ++y) pinball.matrix[(size_t) x][(size_t) y].store (0.0f);
        for (auto& p : w) if (p.first >= 0 && p.first < pb::kStates) pinball.matrix[(size_t) x][(size_t) p.first].store (p.second);
    };
    switch (f)
    {
        case Fn::RandomizeTrigs: for (auto& s : S.steps) s.trig = irand (10); break;
        case Fn::ClearSteps: for (auto& s : S.steps) s = Settings::Step(); break;
        case Fn::ClearMatrix: pinball.clearMatrix(); break;
        case Fn::ClearAll: pinballFunction (Fn::ClearSteps); pinball.clearMatrix(); break;
        case Fn::MatrixChain: pinball.setDefaultMatrix(); break;
        case Fn::MatrixDrunk:
            for (int x = 0; x < pb::kStates; ++x)
            {
                const int s = std::max (1, x);
                setRow (x, { { s == 1 ? 2 : s - 1, 35.0f }, { s + 1 > 16 ? 15 : s + 1, 65.0f }, { s, x == 0 ? 0.0f : 10.0f } });
            }
            break;
        case Fn::MatrixPingPong:
            for (int x = 0; x < pb::kStates; ++x) setRow (x, { { x < 16 ? x + 1 : 15, 100.0f } });
            for (int x = 9; x < 16; ++x) pinball.matrix[(size_t) x][(size_t) (x - 1)].store (40.0f);
            break;
        case Fn::MatrixJumps:
            for (int x = 0; x < pb::kStates; ++x) setRow (x, { { x < 16 ? x + 1 : 1, 70.0f }, { 1 + irand (16), 30.0f } });
            break;
        case Fn::MatrixRandom:
            for (int x = 0; x < pb::kStates; ++x)
            {
                for (int y = 0; y < pb::kStates; ++y) pinball.matrix[(size_t) x][(size_t) y].store (0.0f);
                const int k = 1 + irand (3);
                for (int j = 0; j < k; ++j) pinball.matrix[(size_t) x][(size_t) (1 + irand (16))].store ((float) (20 + irand (81)));
            }
            break;
        case Fn::RandomizeSteps:
            for (auto& s : S.steps)
            {
                s.vel = 50 + irand (78);
                s.prob = urand() < 0.6f ? 100 : 30 + irand (70);
                s.dur = (float) lim (0.25, 64.0, std::pow (2.0, irand (5) - 1));
                s.dly = urand() < 0.75f ? 0.0f : (float) (irand (4) * 0.5);
                s.dprob = urand() < 0.5f ? 100 : 25 + irand (75);
            }
            break;
    }
}

void Instrument::ikdMutate (int voice)
{
    for (int v = 0; v < ry::kNumVoices; ++v)
        if ((voice < 0 && ! ikdLocks[(size_t) v]) || v == voice)
            ikdShared.voices[(size_t) v].mutateRequests.fetch_add (1, std::memory_order_relaxed);
}

void Instrument::ikdRandomise (IkdRandom what)
{
    auto randomBits = [this] (int width, int cnt)
    {
        uint64_t w = 0;
        cnt = lim (0, width, cnt);
        int have = 0;
        while (have < cnt) { const uint64_t b = uint64_t (1) << irand (width); if (! (w & b)) { w |= b; ++have; } }
        return w;
    };
    for (int v = 0; v < ry::kNumVoices; ++v)
    {
        if (ikdLocks[(size_t) v]) continue;
        auto& vs = ikdShared.voices[(size_t) v];
        if (what != IkdRandom::Sound)
        {
            const float density = 0.15f + 0.45f * urand();
            uint64_t w = 0;
            for (int i = 0; i < ry::kCells; ++i) if (urand() < density) w |= uint64_t (1) << i;
            vs.cells.store (w);
        }
        if (what != IkdRandom::Pattern)
        {
            vs.pitch.store (randomBits (ry::kPitchBits, 2 + irand (37)));
            vs.gain.store (randomBits (ry::kGainBits, 3 + irand (14)));
            vs.decay.store (randomBits (ry::kDecayBits, irand (25)));
        }
    }
}

void Instrument::fltDataPatch (bool lanes, bool sound, bool models)
{
    using namespace flt;
    const auto* b = current;
    if (b == nullptr || b->nbits == 0) return;
    const uint64_t h = head;
    const uint64_t hb = b->byteForBit (h);
    auto set = [this] (const std::string& pid, float plain) { if (! fltCase.isLocked (pid)) fltCase.set (pid, plain); };
    auto u = [b, hb] (uint64_t k) { return (float) b->byteAt (hb + k) / 255.0f; };
    if (lanes)
    {
        for (int l = 0; l < kLanes; ++l)
        {
            if (fltCase.isLocked (ids::draw (l))) continue;
            const int steps = 8 + (int) (b->byteAt (hb + 600u + (uint64_t) l) % 25u);
            uint32_t m = (uint32_t) (b->word64 (h + (uint64_t) l * 32u, 32) & 0xffffffffu);
            if (steps < 32) m &= (1u << steps) - 1u;
            if (popcount32 (m) > steps * 5 / 8) m &= (uint32_t) b->word64 (h + 4096u + (uint64_t) l * 32u, 32);
            if (m == 0) m = 1u;
            fltCase.setPattern (l, m);
            set (ids::steps (l), (float) steps);
            set (ids::lon (l), 1.0f);
            fltCase.set (ids::draw (l), 1.0f);
        }
    }
    if (sound)
    {
        for (int v = 0; v < kVoices; ++v)
        {
            const uint64_t o = 640u + (uint64_t) v * 6u;
            set (ids::tune (v), 0.15f + 0.7f * u (o));
            set (ids::decay (v), 0.08f + 0.6f * u (o + 1));
            set (ids::color (v), 0.05f + 0.9f * u (o + 2));
            set (ids::harm (v), 0.1f + 0.8f * u (o + 3));
            set (ids::morph (v), 0.1f + 0.8f * u (o + 4));
            set (ids::send (v), v == kick ? 0.05f : 0.05f + 0.5f * u (o + 5));
        }
        set (ids::ssize, 0.1f + 0.7f * u (700)); set (ids::sdens, 0.2f + 0.7f * u (701)); set (ids::spitch, 0.8f * u (702));
        set (ids::dbreak, 0.6f * u (703)); set (ids::dfb, 0.2f + 0.5f * u (704));
        set (ids::seed, (float) ((b->crc32 + (uint32_t) hb) % 10000u));
        for (int k = 0; k < 16; ++k) fltCase.setSeqValue (k, (float) b->byteAt (hb + 800u + (uint64_t) k) / 127.5f - 1.0f);
        for (int l = 0; l < kLanes; ++l)
        {
            const uint64_t row = hb + 900u + (uint64_t) l * 32u;
            std::array<int, 16> hist {};
            for (uint64_t k = 0; k < 32; ++k) ++hist[(size_t) (b->byteAt (row + k) >> 4)];
            double e = 0.0;
            for (int c : hist) if (c > 0) { const double q = c / 32.0; e -= q * std::log2 (q); }
            set (ids::lock (l), (float) lim (0.55, 1.0, 1.0 - (e / 4.0) * 0.45));
            if (! lanes) continue;
            for (int st = 0; st < kMaxSteps; ++st)
            {
                const int v = b->byteAt (row + (uint64_t) st);
                fltCase.setCond (l, st, v > 236 ? 1 + v % 11 : (v < 6 ? kCondFill : 0));
                for (int w = 0; w < 3; ++w)
                    fltCase.setPlock (l, w, st, (v & (7 << w)) == (7 << w) && v > 200 ? (float) b->byteAt (row + 64u + (uint64_t) (st * 3 + w)) / 127.5f - 1.0f : 0.0f);
            }
        }
        set (ids::rchaos, (float) lim (0.0, 1.0, (b->entropy - 3.0) / 5.0));
        set (ids::rmorph, 0.2f + 0.6f * u (960));
        set (ids::rvs, 0.5f + 0.4f * u (961));
    }
    if (models)
    {
        const int nModels = kFirstBraids + (int) braidsModels().size();
        for (int v = 0; v < kVoices; ++v)
        {
            const int x = b->byteAt (hb + 720u + (uint64_t) v);
            set (ids::model (v), x < 85 ? 0.0f : (float) (1 + (x * 7919) % (nModels - 1)));
        }
        // the plug-in's v0.4 had five SCATTER models here (grains, Clouds x4)
        set (ids::smodel, (float) (b->byteAt (hb + 730u) % 5u));
        set (ids::vmodel, (float) (b->byteAt (hb + 731u) & 1u));
    }
    fltCase.touch();
}

//==============================================================================
void Instrument::loadFactory (int index)
{
    index = lim (0, (int) factoryNames().size() - 1, index);
    factoryIndex = index;

    // everything back to the defaults first, but the visual, the clock and RUN
    {
        const Settings keep = S;
        S = Settings();
        S.run = keep.run; S.bpm = keep.bpm;
        S.visScene = keep.visScene; S.visAuto = keep.visAuto; S.visFlash = keep.visFlash; S.visDensity = keep.visDensity;
    }
    // IKD: the patch's own preset
    for (int w = 0; w < ry::kMasterWords; ++w) ikdShared.master[(size_t) w].store (ry::factory::kMaster[w]);
    const auto baseMs = (float) ry::Engine::stepMsForMasterSum (ikdShared.masterSum());
    uint32_t rr = (uint32_t) (index * 7919 + 13);
    auto next64 = [&rr] { uint64_t v = 0; for (int k = 0; k < 2; ++k) { rr ^= rr << 13; rr ^= rr >> 17; rr ^= rr << 5; v = (v << 32) | rr; } return v; };
    for (int v = 0; v < ry::kNumVoices; ++v)
    {
        auto& vs = ikdShared.voices[(size_t) v];
        vs.pitch.store (ry::factory::kPitch[v]);
        vs.cells.store (ry::factory::kCells[v]);
        vs.gain.store (ry::factory::kGain[v]);
        vs.decay.store (ry::factory::kDecay[v]);
        vs.rule.store (ry::kVoiceSpecs[v].savedRule);
        vs.lastRule.store (ry::kVoiceSpecs[v].savedRule);
        vs.gateBaseMs.store (baseMs);
        vs.signs.store (next64());
    }
    ikdShared.loadSerial.fetch_add (1, std::memory_order_release);

    // FLT: one of FLOATER's patches per preset (the plug-in's indices, in the v0.4 list order; the same
    // patches by name in FLOATER v0.7's list)
    static const char* fltPatchFor[] { "Floor Patch", "Rotating Divider", "Bouncing Ball Scatter", "Systik Glitch", "Plaits Floor", "Ascension",
                                       "Mesh Drums", "Broken Delay d0", "Feedback Garden", "Turing Floor" };
    {
        const auto& f = flt::Case::factoryPresets();
        int fi = 0;
        for (int i = 0; i < (int) f.size(); ++i) if (f[(size_t) i].name == fltPatchFor[index]) fi = i;
        fltCase.loadFactory (fi);
    }

    pinball.setDefaultMatrix();
    struct St { int trig, vel, prob; float dur, dly; int dprob; };
    auto setSteps = [this] (std::initializer_list<St> list)
    {
        int s = 0;
        for (auto& x : list)
        {
            S.steps[(size_t) s] = { x.trig, x.vel, x.prob, x.dur, x.dly, x.dprob };
            if (++s >= pb::kSteps) break;
        }
    };
    auto w = [this] (int x, int y, float v) { pinball.matrix[(size_t) x][(size_t) y].store (v); };
    auto dest = [this] (int l, int d) { S.lanes[(size_t) l].dest = d; };

    switch (index)
    {
        case 0:
        default:
            setSteps ({ { 1, 112, 100, 2, 0, 100 }, { 3, 90, 100, 1, 0, 100 }, { 2, 100, 100, 4, 0, 100 }, { 1, 80, 70, 1, 0, 100 },
                        { 3, 70, 100, 1, 0.5f, 50 }, { 5, 100, 60, 2, 0, 100 }, { 2, 96, 100, 2, 1, 40 }, { 1, 104, 100, 2, 0, 100 },
                        { 3, 120, 100, 1, 0, 100 }, { 4, 100, 50, 1, 0, 100 }, { 2, 88, 100, 8, 0, 100 }, { 6, 100, 30, 1, 0, 100 },
                        { 1, 100, 100, 3, 0, 100 }, { 9, 100, 100, 1, 0, 100 }, { 7, 100, 100, 1, 0, 100 }, { 8, 100, 100, 1, 0, 100 } });
            w (4, 9, 40); w (8, 1, 30); w (12, 5, 35); w (16, 1, 60); w (16, 9, 40);
            S.pbLen = 4; S.pbSwing = 0.1f;
            break;
        case 1:
            S.ikdClk = 0; S.fltClk = 0; S.jdatClk = 0;
            for (int l = 0; l < pb::kLanes; ++l) dest (l, dMidi);
            S.ikdFeed = 0; S.fltFeed = 0; S.jdatWrite = 0;
            setSteps ({ { 1, 100, 100, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 }, { 2, 100, 100, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 },
                        { 1, 100, 100, 1, 0, 100 }, { 3, 100, 100, 1, 0, 100 }, { 2, 100, 100, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 },
                        { 1, 100, 100, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 }, { 2, 100, 100, 1, 0, 100 }, { 4, 100, 100, 1, 0, 100 },
                        { 1, 100, 100, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 }, { 2, 100, 100, 1, 0, 100 }, { 9, 100, 100, 1, 0, 100 } });
            break;
        case 2:
            pinballFunction (Fn::MatrixDrunk);
            setSteps ({ { 1, 110, 100, 1, 0, 100 }, { 3, 80, 90, 1, 0, 100 }, { 2, 100, 100, 3, 0, 100 }, { 3, 70, 80, 1, 0.5f, 60 },
                        { 1, 90, 100, 1, 0, 100 }, { 5, 100, 70, 1, 0, 100 }, { 2, 90, 100, 2, 0, 100 }, { 3, 110, 100, 1, 0, 100 },
                        { 4, 100, 60, 1, 0, 100 }, { 1, 100, 100, 2, 0, 100 }, { 3, 80, 100, 1, 0.25f, 50 }, { 2, 100, 100, 6, 0, 100 },
                        { 7, 100, 50, 1, 0, 100 }, { 1, 120, 100, 1, 0, 100 }, { 6, 100, 40, 1, 0, 100 }, { 8, 100, 100, 1, 0, 100 } });
            S.pbLen = 8; S.pbSwing = 0.2f;
            break;
        case 3:
            S.dataScan = 2; S.dataRate = 6.0f; S.jdatWrite = 3;
            S.jdatClk = 1; S.fltClk = 2; S.fltFeed = 2;
            S.fltMap = 2;
            setSteps ({ { 1, 120, 100, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 }, { 1, 70, 60, 1, 0, 100 }, { 4, 100, 50, 1, 0, 100 },
                        { 1, 100, 100, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 }, { 8, 100, 40, 1, 0, 100 }, { 1, 90, 100, 1, 0.5f, 50 },
                        { 1, 120, 100, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 }, { 1, 70, 60, 1, 0, 100 }, { 4, 100, 50, 1, 0, 100 },
                        { 1, 100, 100, 1, 0, 100 }, { 9, 100, 100, 1, 0, 100 }, { 1, 90, 100, 2, 0, 100 }, { 8, 100, 100, 1, 0, 100 } });
            break;
        case 4:
            S.dataScan = 3;
            pinballFunction (Fn::MatrixJumps);
            setSteps ({ { 1, 110, 100, 2, 0, 100 }, { 2, 90, 100, 2, 0, 100 }, { 3, 100, 100, 1, 0, 100 }, { 1, 80, 100, 1, 0, 100 },
                        { 2, 70, 100, 4, 0, 100 }, { 3, 100, 100, 1, 0, 100 }, { 1, 96, 100, 2, 0.5f, 50 }, { 5, 104, 100, 1, 0, 100 },
                        { 2, 120, 100, 2, 0, 100 }, { 3, 100, 100, 1, 0, 100 }, { 1, 88, 100, 2, 0, 100 }, { 2, 100, 100, 1, 0, 100 },
                        { 3, 100, 100, 3, 0, 100 }, { 1, 100, 100, 1, 0, 100 }, { 9, 100, 100, 1, 0, 100 }, { 8, 100, 100, 1, 0, 100 } });
            S.pbLen = 16;
            break;
        case 5:
            S.fltClk = 2; S.lvlFlt = 0.0f; S.lvlIkd = -9.0f; S.lvlJdat = -14.0f;
            S.fltFeed = 0;
            dest (0, dFltBurst); dest (1, dFltMutate); dest (2, dFltPatch); dest (3, dIkdStep);
            S.fltMap = 3;
            setSteps ({ { 1, 110, 100, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 }, { 4, 100, 100, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 },
                        { 1, 90, 70, 1, 0.5f, 50 }, { 0, 100, 100, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 }, { 2, 100, 20, 1, 0, 100 },
                        { 1, 110, 100, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 }, { 4, 80, 100, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 },
                        { 1, 100, 60, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 }, { 3, 100, 10, 1, 0, 100 }, { 8, 100, 100, 1, 0, 100 } });
            w (16, 1, 70); w (16, 5, 30); w (8, 13, 25);
            S.pbLen = 8;
            break;
        case 6:
            S.ikdClk = 1; S.fltClk = 0; S.jdatClk = 1;
            S.ikdSwing = 0.0f;
            dest (0, dVisFlash); dest (1, dDataStep); dest (2, dIkdMutate); dest (3, dVisCut);
            setSteps ({ { 1, 127, 100, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 }, { 3, 100, 100, 1, 0, 100 },
                        { 0, 100, 100, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 }, { 2, 100, 100, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 },
                        { 1, 90, 100, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 }, { 3, 100, 100, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 },
                        { 0, 100, 100, 1, 0, 100 }, { 2, 100, 100, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 }, { 4, 100, 100, 1, 0, 100 } });
            S.pbLen = 2;
            break;
        case 8:
            S.lvlVtx = 0.0f; S.vtxGhostLvl = -3.0f; S.vtxDriftLvl = -14.0f;
            S.vtxGFb = 0.78f; S.vtxGSelf = 0.45f; S.vtxGInput = 0.8f;
            S.vtxToneOwn = true; S.vtxToneProb = 0.35f; S.vtxFeed = 2;
            S.lvlIkd = -10.0f; S.lvlJdat = -16.0f;
            dest (0, dVtxKick); dest (1, dVtxGhost); dest (2, dVtxTone); dest (3, dFltHit);
            setSteps ({ { 1, 120, 100, 4, 0, 100 }, { 0, 100, 100, 1, 0, 100 }, { 3, 90, 60, 6, 0, 100 }, { 0, 100, 100, 1, 0, 100 },
                        { 2, 100, 50, 1, 0.5f, 50 }, { 0, 100, 100, 1, 0, 100 }, { 4, 80, 70, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 },
                        { 1, 110, 80, 4, 0, 100 }, { 0, 100, 100, 1, 0, 100 }, { 3, 90, 50, 8, 0, 100 }, { 2, 70, 40, 1, 0, 100 },
                        { 0, 100, 100, 1, 0, 100 }, { 4, 90, 60, 1, 0, 100 }, { 3, 100, 30, 2, 0, 100 }, { 8, 100, 100, 1, 0, 100 } });
            w (16, 1, 70); w (16, 9, 30); w (6, 12, 30);
            S.pbLen = 8; S.pbMult = 1.5f;
            break;
        case 9:
            S.fltClk = 2; S.fltFeed = 2; S.fltFeedSnd = true;
            S.dataScan = 1; S.dataStep = 4;
            S.lvlFlt = 0.0f; S.lvlIkd = -12.0f;
            dest (2, dFltUnflip); dest (5, dVtxKick);
            setSteps ({ { 1, 100, 100, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 }, { 6, 110, 60, 1, 0, 100 },
                        { 2, 90, 100, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 },
                        { 1, 100, 100, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 }, { 6, 100, 40, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 },
                        { 2, 90, 100, 1, 0, 100 }, { 0, 100, 100, 1, 0, 100 }, { 3, 100, 25, 1, 0, 100 }, { 8, 100, 100, 1, 0, 100 } });
            w (16, 1, 100);
            S.pbLen = 4;
            break;
        case 7:
            S.ikdWave = -60.0f; S.ikdSine = -60.0f; S.ikdSpec = 3.0f;
            S.fltClk = 1; S.lvlFlt = -4.0f;
            for (int l = 0; l < pb::kLanes; ++l) if (l % 2 == 1) dest (l, dFltBurst);
            S.ikdAuto = 3; S.ikdChance = 70;
            pinballFunction (Fn::MatrixRandom);
            pinballFunction (Fn::RandomizeTrigs);
            break;
    }
    seenSerial = 0;   // feed the data again
}

} // namespace fdat
