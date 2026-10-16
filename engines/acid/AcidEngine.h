// AcidEngine — TB-3PO driving one 303, locked to the host.
//
//   host transport ─► 16th grid (sample-accurate, position derived from ppq,
//                     so loops and locates always land on the right step)
//                          │
//                     TB-3PO core ── notes ──► 303 ─► out
//                          │                    ▲
//                          └─► MIDI out         └── random LFOs on every control
//
// SEMI Family: vendored from semierendonmez-commits/acid Source/engine. Changes (JUCE only, no DSP or
// sequencing change): no juce include; process() takes a mono float buffer and a plain array of
// incoming MIDI (MidiIn) instead of juce::AudioBuffer / juce::MidiBuffer; the emitted events are
// readable through eventCount() / event(i) (the plugin copied them into the MidiBuffer); the
// entropy for New / Mutate comes from setEntropy() instead of juce::Time; jlimit/jmax/jmin ->
// std::clamp/max/min. Wrapped in namespace acid.
#pragma once

#include <array>
#include <cstdint>
#include "Tb3poCore.h"
#include "Voice303.h"
#include "RandomLfo.h"

namespace acid {

// One incoming MIDI message at sample `time` of the block (replaces juce::MidiBuffer).
struct MidiIn { int time = 0; uint8_t data[3] = {0, 0, 0}; int numBytes = 3; };

struct HostTime
{
    bool   valid   = false;
    bool   playing = false;
    double ppq     = 0.0;
    double bpm     = 120.0;
};

struct EngineSettings
{
    Tb3poCore::GenParams gen;
    bool   live = true;                 // accent / slide / octave knobs reshape the line live
    int    scale = 0, root = 9, baseOctave = 2, transpose = 0;
    int    length = 16, direction = 0;
    float  gate = 0.5f;
    int    channel = 1;
    bool   midiOut = true;
    float  masterGain = 1.0f;
    bool   internalRun = false;
    double internalBpm = 128.0;
    int    autoMode = 0;                // 0 off, 1 mutate, 2 new
    int    autoBars = 4;                // 0 = on every gate / trigger
    // play mode — for fills
    int    playMode = 0;                // 0 always, 1 gate (while a note is held), 2 trigger (window per note)
    int    trigSteps = 16;              // trigger window, in 16ths
    bool   retrig = true;               // gate / trigger restarts the pattern at step 1
    int    trigChannel = 0;             // 0 any, 1..16
    std::array<float, Voice303::numFloat> base {};
    std::array<LfoSettings, Voice303::numFloat> lfo {};
};

class AcidEngine
{
public:
    enum class Clock { stopped, host, internal };

    AcidEngine();

    void prepare (double sampleRate);
    Voice303& voice() { return synth; }
    void invalidateMod() { applied.fill (-1.0f); }
    void setRenderAudio (bool b) { renderAudio = b; }   // off in the MIDI FX build   // after devilfish on/off remaps decay

    // Pattern commands (audio thread or under the processor's lock).
    void cmdNew (const EngineSettings& s);
    void cmdMutate (const EngineSettings& s);
    void cmdUndo();
    void cmdSetStep (int i, int kind, int degree, int octave);
    void cmdStore (int bank);
    void cmdRecall (int bank);          // next bar while running, at once when stopped

    // out: n mono samples (overwritten). in: incoming MIDI of this block, sorted by time.
    void process (float* out, int n, const MidiIn* in, int numIn,
                  const HostTime& host, const EngineSettings& s);

    // What process() emitted this block (note on / off to the 303 = the plugin's MIDI out).
    struct Event { int time; uint8_t b[3]; };
    int eventCount() const { return numEvents; }
    const Event& event (int i) const { return events[(size_t) i]; }
    void setEntropy (uint32_t e) { entropyTick = e; }

    // Bring the pattern up to date with `s` now (stopped UI edits, state load).
    void refresh (const EngineSettings& s);

    Tb3poCore& core() { return seq; }
    const Tb3poCore::GenParams& frozen() const { return frozenGen; }
    void setFrozen (const Tb3poCore::GenParams& g) { frozenGen = g; wasLive = false; }

    Clock  clock() const        { return clockMode; }
    double tempo() const        { return currentBpm; }
    int    position() const     { return lastStep; }
    int    pendingRecall() const { return pendingBank; }
    uint32_t noteCount() const  { return notes; }
    float  modValue (int i) const { return effective[(size_t) i]; }
    int    noteFor (int step, const EngineSettings& s) const;
    bool   armed() const { return isArmed; }   // play mode is letting notes through right now

private:
    Tb3poCore::GenParams effectiveGen (const EngineSettings& s) const;
    int  stepFor (long sixteenth, int length, int direction) const;
    void playStep (long k, int t, double samplesPer16, const EngineSettings& s);
    void emit (int t, uint8_t a, uint8_t b, uint8_t c);
    void updateMod (double ppq, const EngineSettings& s);
    void render (float* out, int from, int to, float gain);
    void dispatch (const uint8_t* m);
    bool active (long k, const EngineSettings& s) const;
    void openGate (int pos, long& k, double& ppqStart, double ppqPerSample, double samplesPer16, const EngineSettings& s);
    void closeGate (int pos, const EngineSettings& s);

    Tb3poCore seq;
    Voice303 synth;
    std::array<RandomLfo, Voice303::numFloat> lfos;
    std::array<float, Voice303::numFloat> effective {}, applied {};

    Tb3poCore::GenParams frozenGen, lastGen;
    bool wasLive = true, dirty = true;

    double sampleRate = 44100.0;
    bool   renderAudio = true;
    int    modElapsed = 0;
    Clock  clockMode = Clock::stopped;
    bool   running = false;
    long   startK = 0, lastAutoK = -1;
    double internalPpq = 0.0, modPpq = 0.0, currentBpm = 120.0;

    int  heldNote = -1, offIn = -1, lastStep = 0;

    // play mode state
    std::array<bool, 128> trigHeld {};
    int  trigCount = 0;
    bool gateOpen = false, isArmed = true;
    long trigStartK = 0, trigEndK = 0, stepOffset = 0, lastFiredK = -1000000;
    int  samplesSinceStep = 1 << 30;
    int  pendingBank = -1;
    uint32_t notes = 0, entropy = 0x51ED;

    uint32_t entropyTick = 0;
    std::array<Event, 512> events;
    int numEvents = 0;
    std::array<float, 64> scratch {};
};

} // namespace acid
