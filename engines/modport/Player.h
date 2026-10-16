// MODPORT engine: the replayer.
// SEMI Family port: vendored from semierendonmez-commits/modport; one change, setSpeedTempo () added.
//
// A port of the ProTracker 2.3D replay routine as reconstructed by pt2-clone
// (Olav Sørensen, BSD-3), widened to 8 channels, with a Paula model per channel:
// DMA latches (sample swap, loops taking effect when the current block ends),
// period / volume refetch timing, BLEP-synthesised steps (aciddose's minBLEP),
// and the Amiga 500 / 1200 output filters including the "LED" filter (E0x).
//
// Files from 6 / 8 channel trackers and notes outside ProTracker's three
// octaves use the same routine with a five-octave period table and wider
// slide limits, the way FastTracker / OctaMED modules expect.
#pragma once

#include "Module.h"

#include <array>
#include <bitset>
#include <cstdint>
#include <functional>
#include <vector>

namespace modport
{

enum class Interp { Amiga, Linear, Raw };          // Amiga = steps with minBLEP (pt2-clone)
enum class Model { A500, A1200, Off };              // output filter model
enum class Led { Auto, Off, On };                   // E0x LED filter: follow the song / never / always

struct Blep
{
    static constexpr int kNS = 16, kRNS = 31;
    int index = 0, samplesLeft = 0;
    float buffer[kRNS + 1] {};
    float lastValue = 0.0f;
    void add (float offset, float amplitude);
    float run (float input);
};

struct OnePole { float a0 = 0, b1 = 0, s = 0; void setup (double sr, double fc); float lp (float x) { s = x * a0 + s * b1; return s; } float hp (float x) { s = x * a0 + s * b1; return x - s; } };
struct TwoPole { float a1 = 0, a2 = 0, b1 = 0, b2 = 0, z[4] {}; void setup (double sr, double fc, double q); float lp (float x); void clear() { z[0] = z[1] = z[2] = z[3] = 0; } };

// The Amiga output stage for one signal (the mix uses two, each stem one).
struct AmigaFilter
{
    OnePole lo, hi;
    TwoPole led;
    bool useLo = true, useHi = true;
    void setup (double sr, Model m);
    float process (float x, bool ledOn);
    void clear() { lo.s = hi.s = 0; led.clear(); }
};

struct ChannelView
{
    int period = 0;        // what Paula is playing now
    int volume = 0;        // 0..64, after tremolo
    int sample = 0;        // 1..31, 0 = none yet
    bool active = false;
    bool audible = false;  // playing sample data (not the silent word after a one-shot sample)
    float level = 0.0f;    // peak of the last rendered block, 0..1
};

struct Trigger
{
    int channel = 0, sample = 0, period = 0, notePeriod = 0, volume = 0;
    int order = 0, row = 0;
    std::uint32_t offsetBytes = 0;   // 9xx start
};

class Player
{
public:
    Player();

    // The player keeps its own copy of the sample data (EFx rewrites it, as on the Amiga).
    void setModule (const Module* m);
    const Module* module() const { return mod; }

    void prepare (double sampleRate);
    void restart (int order = 0, int row = 0);   // song start state, all voices off
    // Rack port (SEMI Family): keep the running speed / tempo across a jump made with
    // restart (order) (restart () resets them to 6 / 125, a Bxx jump would keep them).
    void setSpeedTempo (int s, int b) { speed = s < 1 ? 1 : s; bpm = b < 32 ? 32 : b; tickCount = speed - 1; }

    // ---- options ------------------------------------------------------------------
    void setInterp (Interp i) { interp = i; }
    void setModel (Model m);
    void setLed (Led l) { ledMode = l; }
    void setStereo (float separation01) { stereo = separation01; }
    void setMuted (int chn, bool m) { if (chn >= 0 && chn < kMaxChannels) muted[(std::size_t) chn] = m; }
    bool isMuted (int chn) const { return chn >= 0 && chn < kMaxChannels && muted[(std::size_t) chn]; }
    void setTiming (Timing t) { vblank = t == Timing::VBlank; }
    bool isVBlank() const { return vblank; }
    // Host tempo: a row at the song's own start tempo becomes a 1/16 at the host's BPM.
    void setHostTempo (bool on, double tempoBpm) { hostTempo = on; hostBpm = tempoBpm > 1.0 ? tempoBpm : 120.0; }

    // ---- rendering -------------------------------------------------------------------
    void render (float* L, float* R, int n);              // adds nothing, overwrites
    void renderStems (float* const* stems, int n);        // one mono buffer per channel, filtered
    void skip (int n);                                    // advance time without mixing
    void renderTail (float* L, float* R, float* const* stems, int n);   // voices ring on, no new ticks (export tails)
    bool tick();                                          // one replayer tick (tests / tracing)
    int renderTick (float* L, float* R, int cap);         // one tick and its audio; returns samples written

    // seek to the first time the song reaches a beat / second / (order,row): returns false if never
    bool seekBeats (double beats);
    bool seekSeconds (double seconds);
    bool seekPosition (int order, int row);
    bool seekTo (double target, bool inBeats);

    // ---- state ------------------------------------------------------------------------
    int getOrder() const { return curOrder; }      // position of the row being played
    int getRow() const { return curRow; }
    int getPattern() const { return curPattern; }
    int getSpeed() const { return speed; }
    int getTempo() const { return bpm; }
    int getTick() const { return tickCount; }
    bool hasEnded() const { return ended; }        // the song came back to a row it played (or stopped)
    bool stoppedByF00() const { return stopSong; }
    double seconds() const { return songSeconds; }
    double beats() const { return songBeats; }
    int startTempo() const { return refTempo; }
    bool ledFilterOn() const { return ledOn; }
    ChannelView channelView (int chn) const;
    std::uint64_t totalTicks() const { return ticksDone; }

    // called for each sample trigger (DMA restart), after the row's effects
    std::function<void (const Trigger&)> onTrigger;

    // Song length by playing it through silently (one pass, stops at the loop point).
    struct Scan { double seconds = 0, beats = 0; int rowsPlayed = 0; bool loops = false; std::vector<double> orderSeconds; };
    static Scan scan (const Module&, double sampleRateForTicks = 48000.0, int timing = -1);
    // CIA or VBlank for ProTracker files with Fxx >= 0x20 (libxmp's heuristic, MIT)
    static Timing detectTiming (const Module&);

private:
    struct Chan
    {
        std::int64_t n_start = -1, n_wavestart = -1, n_loopstart = -1;   // offsets into mem, -1 = NULL
        std::int8_t n_volume = 0, n_toneportdirec = 0, n_pattpos = 0, n_loopcount = 0;
        std::uint8_t n_wavecontrol = 0, n_glissfunk = 0, n_sampleoffset = 0, n_toneportspeed = 0;
        std::uint8_t n_vibratocmd = 0, n_tremolocmd = 0, n_finetune = 0, n_funkoffset = 0, n_samplenum = 0;
        std::uint8_t n_vibratopos = 0, n_tremolopos = 0;
        std::int16_t n_period = 0, n_note = 0, n_wantedperiod = 0;
        std::uint16_t n_cmd = 0, n_length = 0, n_replen = 0;
        int index = 0;
        int sampleShown = 0;
        std::uint32_t offsetApplied = 0;
    };

    struct Voice
    {
        bool active = false, justStarted = false, nextStage = false, finished = false;
        std::int8_t dat[2] {};
        std::uint32_t loc = 0, storedLoc = 0;
        std::uint16_t lenCounter = 0, storedLen = 0;
        int sampleCounter = 0;
        float sample = 0, next = 0;          // current step value (vol applied), next value for LINEAR
        float delta = 0, phase = 0, storedDelta = 0;
        float blepPhase = 0, blepDelta = 0;
        float storedVol = 0;
        int period = 0, volume = 0;
        Blep blep;
        float peak = 0;
    };

    // replayer (names follow PT / pt2-clone)
    void playVoice (Chan&);
    void setPeriod (Chan&);
    void checkMoreEffects (Chan&);
    void checkEffects (Chan&);
    void chkefx2 (Chan&);
    void eCommands (Chan&);
    void setTonePorta (Chan&);
    void tonePortNoChange (Chan&);
    void vibrato2 (Chan&);
    void tremolo (Chan&);
    void arpeggio (Chan&);
    void portaUp (Chan&);
    void portaDown (Chan&);
    void volumeSlide (Chan&);
    void sampleOffset (Chan&);
    void doRetrg (Chan&);
    void updateFunk (Chan&);
    void jumpLoop (Chan&);
    void setDMA();
    void nextPosition();
    int resolveOrder (int pos);   // skips marker entries (0xFE) / ends on 0xFF
    double tickSeconds() const;
    Cell fetch (int pattern, int row, int ch) const;
    const std::int16_t* periods (int finetune) const;
    int numNotes() const { return extended ? 60 : 36; }

    // paula
    void paulaPeriod (int chn, int period);
    void paulaVolume (int chn, int vol);
    void paulaLength (int chn, std::uint16_t len) { voices[(std::size_t) chn].storedLen = len; }
    void paulaData (int chn, std::int64_t ptr) { voices[(std::size_t) chn].storedLoc = ptr < 0 ? 0u : (std::uint32_t) ptr; }
    void dmaOff (int chn) { voices[(std::size_t) chn].active = false; }
    void dmaOn (int chn);
    void nextSample (Voice&, bool doBlep);
    float peekNext (const Voice&) const;
    inline std::int8_t memAt (std::uint32_t i) const { return i < mem.size() ? mem[i] : 0; }

    void mixVoices (float* L, float* R, float* const* stems, int n);
    void skipVoice (Voice&, int n);
    void fastFetch (Voice&, std::uint64_t words);
    double samplesPerTick() const;
    double beatsPerTick() const { return (double) refTempo / (24.0 * (double) bpm); }

    const Module* mod = nullptr;
    std::vector<std::int8_t> mem, memPristine;
    std::array<std::int64_t, kMaxSamples> sampleBase {};
    std::array<std::uint32_t, kMaxSamples> sampleSkip {};   // SoundTracker: playback starts at the loop
    std::array<std::int16_t, 16 * 61> extTable {};
    bool extended = false;

    std::array<Chan, kMaxChannels> ch {};
    std::array<Voice, kMaxChannels> voices {};
    std::array<bool, kMaxChannels> muted {};
    int numCh = 4;

    // song state
    int modPos = 0, modPattern = 0, row = 0, tickCount = 0, speed = 6, bpm = 125, refTempo = 125;
    int curOrder = 0, curRow = 0, curPattern = 0;
    int ciaSetBPM = -1;
    std::int8_t pBreakPosition = 0;
    bool posJumpAssert = false, pBreakFlag = false, stopSong = false, ended = false, wrapPending = false;
    std::uint8_t pattDelTime = 0, pattDelTime2 = 0, lowMask = 0xFF;
    std::uint32_t dmaconTemp = 0;
    std::vector<std::bitset<64>> visited;
    double songSeconds = 0, songBeats = 0;
    std::uint64_t ticksDone = 0;

    // audio
    double sr = 48000.0, tickRemain = 0.0;
    float periodToDeltaDiv = 1.0f;
    Interp interp = Interp::Amiga;
    Model model = Model::A500;
    Led ledMode = Led::Auto;
    bool ledOn = false;
    float stereo = 0.5f;
    bool hostTempo = false;
    bool vblank = false;
    // E6x with one loop for all channels (Octalyser, Digital Tracker)
    int gLoopRow = 0, gLoopCount = 0;
    bool rowLoopJumped = false, rowLoopSeen = false;
    double hostBpm = 120.0;
    std::array<AmigaFilter, 2> mixFilter {};
    std::array<AmigaFilter, kMaxChannels> stemFilter {};
    std::vector<float> scratch;
};

constexpr double kPaulaClock = 28375160.0 / 8.0;   // PAL colour clock, 3546895 Hz
double ciaBpmToHz (int bpm);

} // namespace modport
