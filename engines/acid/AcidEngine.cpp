#include "AcidEngine.h"
#include <algorithm>
#include <cmath>

// SEMI Family: see AcidEngine.h for the (JUCE-only) changes.
namespace acid {

namespace
{
    constexpr int controlBlock = 32;     // modulation + gate resolution (< 1 ms)

    long floorMod (long a, long m) { const long r = a % m; return r < 0 ? r + m : r; }

    uint32_t hash32 (uint32_t x)
    {
        x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
        return x;
    }
}

AcidEngine::AcidEngine()
{
    for (size_t i = 0; i < lfos.size(); ++i)
        lfos[i] = RandomLfo (0x9E3779B9u * (uint32_t) (i + 1));
    applied.fill (-1.0f);
}

void AcidEngine::prepare (double sr)
{
    sampleRate = sr > 0 ? sr : 44100.0;
    synth.prepare (sampleRate);
    applied.fill (-1.0f);
    heldNote = -1;
    offIn = -1;
}

//==============================================================================
Tb3poCore::GenParams AcidEngine::effectiveGen (const EngineSettings& s) const
{
    auto g = s.gen;
    if (! s.live)
    {
        g.accent  = frozenGen.accent;
        g.slide   = frozenGen.slide;
        g.octaves = frozenGen.octaves;
    }
    return g;
}

void AcidEngine::refresh (const EngineSettings& s)
{
    if (! s.live && wasLive) frozenGen = s.gen;
    wasLive = s.live;
    const auto g = effectiveGen (s);
    seq.generate (g);
    lastGen = g;
    dirty = false;
}

void AcidEngine::cmdNew (const EngineSettings& s)
{
    frozenGen = s.gen;                    // with live off, New/Mutate pick up the knobs
    entropy = hash32 (entropy + entropyTick);
    seq.newSeed (entropy);
    dirty = true;
}

void AcidEngine::cmdMutate (const EngineSettings& s)
{
    frozenGen = s.gen;
    entropy = hash32 (entropy + entropyTick);
    seq.mutate (entropy);
    dirty = true;
}

void AcidEngine::cmdUndo()                       { if (seq.undo()) dirty = true; }
void AcidEngine::cmdSetStep (int i, int k, int d, int o) { seq.setStep (i, k, d, o); dirty = true; }
void AcidEngine::cmdStore (int b)                { seq.store (b); }

void AcidEngine::cmdRecall (int b)
{
    if (! seq.bankFilled (b)) return;
    if (running) { pendingBank = b; return; }
    seq.recall (b);
    dirty = true;
}

int AcidEngine::noteFor (int i, const EngineSettings& s) const
{
    const auto& sc = tb3poScales[std::clamp (s.scale, 0, tb3poNumScales - 1)];
    const auto& st = seq.step (i);
    const int d = st.degree % sc.len;
    const int n = 12 * (s.baseOctave + 1) + s.root + sc.semis[d] + 12 * st.octave + s.transpose;
    return std::clamp (n, 0, 127);
}

int AcidEngine::stepFor (long k, int length, int direction) const
{
    const int L = std::clamp (length, 1, Tb3poCore::maxSteps);
    switch (direction)
    {
        case 1: return L - 1 - (int) floorMod (k, L);
        case 2:
        {
            if (L < 2) return 0;
            const long period = 2L * (L - 1);
            const long p = floorMod (k, period);
            return (int) (p < L ? p : period - p);
        }
        case 3: return (int) (hash32 ((uint32_t) k ^ ((uint32_t) seq.pattern.seed << 16)) % (uint32_t) L);
        default: return (int) floorMod (k, L);
    }
}

//==============================================================================
void AcidEngine::emit (int t, uint8_t a, uint8_t b, uint8_t c)
{
    const uint8_t m[3] = { a, b, c };
    dispatch (m);
    if (numEvents < (int) events.size())
        events[(size_t) numEvents++] = { t, { a, b, c } };
}

void AcidEngine::dispatch (const uint8_t* m)
{
    synth.midi (m, 3);
}

bool AcidEngine::active (long k, const EngineSettings& s) const
{
    switch (s.playMode)
    {
        case 1:  return gateOpen;
        case 2:  return k >= trigStartK && k < trigEndK;
        default: return true;
    }
}

// A gate opened / a trigger arrived at sample `pos`; `k` is the next 16th not yet played.
void AcidEngine::openGate (int pos, long& k, double& ppqStart, double ppqPerSample, double samplesPer16,
                           const EngineSettings& s)
{
    long first = k;
    bool playNow = false;
    if (clockMode == Clock::internal)
    {
        // Free-running: put a 16th exactly here so the fill starts on the note.
        ppqStart = (double) k * 0.25 - pos * ppqPerSample;
    }
    else if (lastFiredK == k - 1
             && samplesSinceStep < (int) std::min (0.03 * sampleRate, samplesPer16 * 0.4))
    {
        // Played a hair late (or by hand): catch the 16th we just passed.
        first = k - 1;
        playNow = true;
    }

    gateOpen = true;
    trigStartK = first;
    trigEndK = first + std::max (1, s.trigSteps);
    if (s.retrig) stepOffset = first;

    if (s.autoMode != 0 && s.autoBars == 0)
    {
        if (s.autoMode == 1) cmdMutate (s); else cmdNew (s);
    }
    if (playNow) playStep (first, pos, samplesPer16, s);
}

void AcidEngine::closeGate (int pos, const EngineSettings& s)
{
    gateOpen = false;
    if (s.playMode == 1 && heldNote >= 0)
    {
        emit (pos, (uint8_t) (0x80 | ((s.channel - 1) & 0x0F)), (uint8_t) heldNote, 0);
        heldNote = -1;
        offIn = -1;
    }
}

void AcidEngine::playStep (long k, int t, double samplesPer16, const EngineSettings& s)
{
    if (pendingBank >= 0 && floorMod (k, 16) == 0)
    {
        seq.recall (pendingBank);
        pendingBank = -1;
        dirty = true;
    }

    const int bars = std::max (1, s.autoBars);
    if (s.autoMode != 0 && s.autoBars > 0 && k > startK && (s.playMode == 0 || active (k, s)) && floorMod (k, 16L * bars) == 0 && k != lastAutoK)
    {
        lastAutoK = k;
        if (s.autoMode == 1) cmdMutate (s); else cmdNew (s);
    }

    // Module: density / scale changes take effect on the next clock.
    const auto g = effectiveGen (s);
    if (dirty || g != lastGen)
    {
        seq.generate (g);
        lastGen = g;
        dirty = false;
    }

    lastFiredK = k;
    samplesSinceStep = 0;
    const uint8_t ch = (uint8_t) (0x90 | ((s.channel - 1) & 0x0F));
    const uint8_t chOff = (uint8_t) (0x80 | ((s.channel - 1) & 0x0F));

    const long kk = (s.playMode != 0 && s.retrig) ? k - stepOffset : k;
    const int i = stepFor (kk, s.length, s.direction);
    const int prev = (s.playMode != 0 && s.retrig && kk == 0) ? -1 : stepFor (kk - 1, s.length, s.direction);

    isArmed = active (k, s);
    if (! isArmed)
    {
        if (heldNote >= 0) { emit (t, chOff, (uint8_t) heldNote, 0); heldNote = -1; offIn = -1; }
        return;                                  // the grid runs on, silently
    }
    lastStep = i;

    if (! seq.plays (i, prev)) return;

    const auto& st = seq.step (i);
    const int note = noteFor (i, s);
    const uint8_t vel = st.accent ? 127 : 80;      // Open303: velocity >= 100 is an accent
    const bool legato = heldNote >= 0 && offIn < 0;

    if (! (legato && note == heldNote))
    {
        emit (t, ch, (uint8_t) note, vel);
        if (heldNote >= 0) emit (t, chOff, (uint8_t) heldNote, 0);   // after the on: Open303 slides
    }
    heldNote = note;
    ++notes;
    for (auto& l : lfos) l.trigger();

    offIn = st.slide ? -1 : std::max (1, (int) std::lround (s.gate * samplesPer16));
}

void AcidEngine::updateMod (double ppq, const EngineSettings& s)
{
    const double dt = modElapsed / sampleRate;
    modElapsed = 0;
    for (int j = 0; j < Voice303::numFloat; ++j)
    {
        const auto& l = s.lfo[(size_t) j];
        float v = s.base[(size_t) j];
        const float mod = lfos[(size_t) j].value (ppq, l, dt);
        if (l.depth > 1.0e-4f)
            v = std::clamp (v + l.depth * mod, 0.0f, 1.0f);
        effective[(size_t) j] = v;
        if (std::abs (v - applied[(size_t) j]) > 4.0e-4f)
        {
            synth.setFloat (j, v);
            applied[(size_t) j] = v;
        }
    }
}

void AcidEngine::render (float* L, int from, int to, float gain)
{
    modElapsed += to - from;
    if (! renderAudio || L == nullptr) return;
    for (int p = from; p < to;)
    {
        const int n = std::min ((int) scratch.size(), to - p);
        synth.render (scratch.data(), n);
        for (int i = 0; i < n; ++i)
        {
            const float x = scratch[(size_t) i] * gain;
            L[p + i] = x;
        }
        p += n;
    }
}

//==============================================================================
void AcidEngine::process (float* out, int n, const MidiIn* midiIn, int numIn,
                          const HostTime& host, const EngineSettings& s)
{
    for (int i = 0; i < n; ++i) out[i] = 0.0f;
    numEvents = 0;

    const bool hostRolling = host.valid && host.playing;
    const Clock mode = hostRolling ? Clock::host
                     : ((s.internalRun || s.playMode != 0) ? Clock::internal : Clock::stopped);
    if (s.playMode == 0) { isArmed = true; gateOpen = false; trigCount = 0; trigHeld.fill (false); }
    const double bpm = std::clamp ((host.valid && host.bpm > 1.0) ? host.bpm : s.internalBpm, 20.0, 400.0);
    currentBpm = bpm;
    const double ppqPerSample = bpm / 60.0 / sampleRate;
    const double samplesPer16 = 0.25 / ppqPerSample;

    if (! s.live && wasLive) frozenGen = s.gen;
    wasLive = s.live;

    const bool nowRunning = mode != Clock::stopped;
    if (nowRunning && (! running || mode != clockMode))
    {
        if (mode == Clock::internal && ! running) internalPpq = 0.0;
        const double p0 = mode == Clock::host ? host.ppq : internalPpq;
        startK = (long) std::ceil (p0 * 4.0 - 1.0e-7);
        lastAutoK = -1;
    }
    if (nowRunning && mode == Clock::host && clockMode != Clock::host && s.playMode == 2)
        trigEndK = trigStartK;                   // transport start: no window left over from before
    if (! nowRunning && running && heldNote >= 0)
    {
        emit (0, (uint8_t) (0x80 | ((s.channel - 1) & 0x0F)), (uint8_t) heldNote, 0);
        heldNote = -1;
        offIn = -1;
    }
    running = nowRunning;
    clockMode = mode;

    if (! running)
    {
        const auto g = effectiveGen (s);
        if (dirty || g != lastGen) { seq.generate (g); lastGen = g; dirty = false; }
    }

    double ppqStart = mode == Clock::host ? host.ppq : internalPpq;
    long k = (long) std::ceil (ppqStart * 4.0 - 1.0e-7);
    auto sampleOf = [&] (long kk)
    {
        return (int) std::ceil (((double) kk * 0.25 - ppqStart) / ppqPerSample - 1.0e-6);
    };

    const MidiIn* in = midiIn;
    const MidiIn* const inEnd = midiIn + (midiIn != nullptr ? numIn : 0);
    const uint8_t chanByte = (uint8_t) ((s.channel - 1) & 0x0F);

    int pos = 0;
    while (pos < n)
    {
        const int next16  = running ? std::max (pos, sampleOf (k)) : n;
        const int nextOff = (heldNote >= 0 && offIn >= 0) ? pos + offIn : n;
        const int nextIn  = in != inEnd ? std::max (pos, in->time) : n;

        if (nextOff <= pos)                              // gate ends (before a step on the same sample)
        {
            emit (pos, (uint8_t) (0x80 | chanByte), (uint8_t) heldNote, 0);
            heldNote = -1;
            offIn = -1;
            continue;
        }
        if (nextIn <= pos && in != inEnd)       // MIDI before a 16th on the same sample: a trigger on the grid catches it
        {
            const auto meta = *in++;
            if (meta.numBytes < 2) continue;
            const uint8_t st = meta.data[0] & 0xF0;
            if (st != 0x80 && st != 0x90) continue;
            const int mch = (meta.data[0] & 0x0F) + 1;
            const uint8_t vel = meta.numBytes >= 3 ? meta.data[2] : (uint8_t) 0;
            const bool on = st == 0x90 && vel > 0;
            const int note = meta.data[1] & 0x7F;

            if (s.playMode != 0 && (s.trigChannel == 0 || s.trigChannel == mch))
            {
                if (on)
                {
                    const bool wasOpen = trigCount > 0;
                    if (! trigHeld[(size_t) note]) { trigHeld[(size_t) note] = true; ++trigCount; }
                    if (s.playMode == 2 || ! wasOpen) openGate (pos, k, ppqStart, ppqPerSample, samplesPer16, s);
                }
                else if (trigHeld[(size_t) note])
                {
                    trigHeld[(size_t) note] = false;
                    if (--trigCount == 0 && s.playMode == 1) closeGate (pos, s);
                }
                continue;
            }
            if ((meta.data[0] & 0x0F) == chanByte)
            {
                const uint8_t m[3] = { meta.data[0], meta.data[1], vel };
                dispatch (m);
                if (on) for (auto& l : lfos) l.trigger();
            }
            continue;
        }
        if (running && next16 <= pos && next16 < n)
        {
            playStep (k++, pos, samplesPer16, s);
            continue;
        }

        const int end = std::min ({ n, pos + controlBlock, running ? next16 : n, nextOff, nextIn });
        const double ppqHere = running ? ppqStart + pos * ppqPerSample : modPpq + pos * ppqPerSample;
        updateMod (ppqHere, s);
        render (out, pos, std::max (end, pos + 1), s.masterGain);
        if (offIn >= 0) offIn -= std::max (end, pos + 1) - pos;
        samplesSinceStep = (int) std::min<long> (1L << 30, (long) samplesSinceStep + (std::max (end, pos + 1) - pos));
        pos = std::max (end, pos + 1);
    }

    if (mode == Clock::internal) internalPpq = ppqStart + n * ppqPerSample;
    modPpq = running ? ppqStart + n * ppqPerSample : modPpq + n * ppqPerSample;

    for (int i = 0; i < numEvents; ++i)
        events[(size_t) i].time = std::clamp (events[(size_t) i].time, 0, std::max (0, n - 1));
}

} // namespace acid
