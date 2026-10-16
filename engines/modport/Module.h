// MODPORT engine: the .mod file itself.
// Loads every ProTracker-style variant up to 8 channels and writes it back
// byte for byte (or converted to the standard tag for its channel count).
//
//   31 samples, tag at 1080: M.K. M!K! M&K! N.T. FLT4 LARD NSMS   (4 ch)
//                            1CHN..8CHN  10CH.. (rejected above 8)  TDZ1..TDZ3
//                            6CHN 8CHN (FastTracker / OctaMED / TakeTracker)
//                            CD61 CD81 (Octalyser)  OKTA OCTA (Oktalyzer / OctaMED)
//                            FA04 FA06 FA08 (Digital Tracker, 4 extra bytes)
//                            FLT8 (StarTrekker: 4-channel patterns played in pairs)
//                            M.K. with 8 channels (Mod's Grave WOW, by file size)
//   15 samples, no tag:      Ultimate SoundTracker / SoundTracker 2.x
//
// No JUCE in here: the tests link it on its own.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace modport
{

constexpr int kMaxChannels = 8;
constexpr int kMaxSamples = 31;
constexpr int kRows = 64;

struct Cell
{
    std::uint16_t period = 0;   // 12-bit Amiga period, 0 = no note
    std::uint8_t sample = 0;    // 0 = none, 1..31 (up to 255 in damaged files)
    std::uint8_t effect = 0;    // 0x0..0xF
    std::uint8_t param = 0;
    bool empty() const { return period == 0 && sample == 0 && effect == 0 && param == 0; }
};

struct Sample
{
    std::array<char, 22> rawName {};     // kept byte for byte (junk after the NUL included)
    std::uint16_t lengthWords = 0;       // as stored
    std::uint8_t finetuneByte = 0;       // as stored (low nibble = signed finetune)
    std::uint8_t volumeByte = 0;         // as stored (> 64 is clamped on playback)
    std::uint16_t loopStartWords = 0;    // as stored
    std::uint16_t loopLengthWords = 0;   // as stored
    std::vector<std::int8_t> data;       // lengthWords * 2 bytes (zero padded if the file was cut short)

    std::string name() const;            // printable
    int finetune() const { const int f = finetuneByte & 0x0F; return f >= 8 ? f - 16 : f; }
    int volume() const { return volumeByte > 64 ? 64 : volumeByte; }
    std::uint32_t lengthBytes() const { return (std::uint32_t) lengthWords * 2u; }
    bool looped() const { return loopLengthWords > 1; }
};

enum class Timing { Cia, VBlank };   // CIA: Fxx >= 32 sets BPM; VBlank: every Fxx sets speed (50 Hz ticks)
enum class Flow { ProTracker, Octalyser, DigitalTracker };   // pattern loop (E6x) rules

enum class Layout
{
    Standard31,     // 1084-byte header, tag at 1080
    DigitalTracker, // FA0x: 4 more bytes after the tag
    StarTrekker8,   // FLT8: pairs of 4-channel patterns, order numbers doubled
    ModsGrave,      // M.K. tag, 8 channels (WOW)
    SoundTracker15  // 600-byte header, no tag
};

struct Module
{
    std::array<char, 20> rawTitle {};
    std::string tag;                 // the 4 tag bytes as read ("" for 15-sample files)
    std::string formatName;          // human readable, e.g. "ProTracker M.K."
    Layout layout = Layout::Standard31;
    int channels = 4;
    int numSamples = 31;             // 15 or 31 as stored
    std::array<Sample, kMaxSamples> samples {};
    int songLength = 1;              // 1..128
    std::uint8_t restartByte = 0;
    std::array<std::uint8_t, 128> orders {};    // as played (FLT8 already halved)
    std::array<std::uint8_t, 128> rawOrders {}; // as stored (written back)
    std::vector<std::vector<Cell>> patterns;    // each kRows * channels
    Flow flow = Flow::ProTracker;
    bool vblankOnly = false;         // NoiseTracker / SoundTracker: no BPM command at all
    bool highFxx = false;            // some Fxx >= 0x20: CIA or VBlank must be decided (detectTiming)
    Timing timing = Timing::Cia;     // as detected; the plug-in can override it
    bool ultimate = false;           // Ultimate SoundTracker: byte 471 = tempo, effects 1 = arpeggio, 2 = bend
    bool extendedPeriods = false;    // notes outside ProTracker's 3 octaves or > 4 channels
    std::vector<std::uint8_t> trailing; // bytes after the sample data (unused patterns, tracker extras): written back as they were
    std::vector<std::string> notes;  // what the loader had to repair

    std::string title() const;
    int numPatterns() const { return (int) patterns.size(); }
    const Cell& cell (int pattern, int row, int ch) const;
    int patternAt (int order) const;             // -1 when the order points nowhere
    int usedSamples() const;
};

struct LoadResult
{
    bool ok = false;
    std::string error;
    Module module;
};

LoadResult loadModule (const std::uint8_t* data, std::size_t size);

enum class SaveAs
{
    Original,   // the layout and tag the file came with (exact round trip)
    Standard    // M.K. (4 ch, M!K! above 64 patterns), otherwise nCHN, 31 samples
};

std::vector<std::uint8_t> saveModule (const Module&, SaveAs = SaveAs::Original);
std::string standardTagFor (int channels, int numPatterns);

// ---- notes / periods --------------------------------------------------------
// ProTracker's 16 finetuned tables of 36 notes (C-1..B-3), with PT's padding.
extern const std::int16_t kPeriodTable[37 * 16 + 15];
// Five octaves (C-0..B-4) per finetune for files beyond ProTracker's range.
std::int16_t extendedPeriod (int finetuneNibble, int noteIndex /* 0..59, C-0 = 0 */);
// Note index (0 = C-0 ... 59 = B-4) of the nearest finetune-0 period, -1 for 0.
int periodToNoteIndex (int period);
std::string noteName (int period);   // "C-2", "---"
// MIDI note for a period, 428 (C-2) -> 60 as the Ableton extension maps it.
int periodToMidi (int period);

} // namespace modport
