// SEMI port: see Data.h. build() is Source/data/DataLoader.cpp's, the audio path takes already decoded samples.
#include "Data.h"
#include "Examples.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

namespace fdat_pdata
{
const std::vector<std::string>& readAsNames()
{
    static const std::vector<std::string> n { "auto", "bytes", "text", "audio", "image" };
    return n;
}

bool isAudioType (const std::string& t)
{
    return t == "WAV AUDIO" || t == "AIFF AUDIO" || t == "AIFF-C AUDIO" || t == "FLAC AUDIO" || t == "OGG STREAM" || t == "MP3 AUDIO";
}
static bool isImageType (const std::string& t) { return t == "PNG IMAGE" || t == "JPEG IMAGE" || t == "GIF IMAGE"; }

// AUDIO: normalise, bits = sample above zero (as the plug-in)
static bool buildAudio (DataBlob& b, const std::vector<float>* audio, double rate, int channels)
{
    if (audio == nullptr || audio->empty()) return false;
    const size_t frames = std::min<size_t> (audio->size(), 4u * 1024u * 1024u);
    b.audioRate = rate;
    b.audioChannels = channels;
    b.audioSeconds = (double) audio->size() / std::max (1.0, rate);
    b.symbols.assign (audio->begin(), audio->begin() + (long) frames);
    float peak = 1.0e-6f;
    for (float s : b.symbols) peak = std::max (peak, std::abs (s));
    for (auto& s : b.symbols) s /= peak;
    std::vector<uint8_t> bits (frames);
    for (size_t i = 0; i < frames; ++i) bits[i] = b.symbols[i] > 0.0f ? 1 : 0;
    b.packBits (bits);
    b.used = ReadAs::Audio;
    char buf[64];
    std::snprintf (buf, sizeof buf, "%.1f KHZ  %d CH  %.2f S", b.audioRate / 1000.0, b.audioChannels, b.audioSeconds);
    b.kindNote = buf;
    return true;
}

void build (DataBlob& b, ReadAs requested, const std::vector<float>* audio, double audioRate, int audioChannels)
{
    b.analyse();
    b.symbols.clear();
    b.kindNote.clear();
    b.imageW = b.imageH = b.rasterRows = 0;
    b.audioRate = b.audioSeconds = 0.0;
    b.audioChannels = 0;

    ReadAs mode = requested;
    if (mode == ReadAs::Auto)
    {
        if (isAudioType (b.typeName)) mode = ReadAs::Audio;
        else if (isImageType (b.typeName)) mode = ReadAs::Image;
        else if (b.typeName == "TEXT") mode = ReadAs::Text;
        else mode = ReadAs::Bytes;
    }
    bool ok = false;
    if (mode == ReadAs::Audio) ok = buildAudio (b, audio, audioRate, audioChannels);
    if (! ok && (mode == ReadAs::Audio || mode == ReadAs::Image))
    {
        // SEMI port: no image decoder (and audio only as WAV, decoded by the module)
        b.kindNote = mode == ReadAs::Audio ? "NOT DECODABLE AS AUDIO - READ AS BYTES" : "NOT DECODABLE AS IMAGE - READ AS BYTES";
        mode = ReadAs::Bytes;
    }
    if (mode == ReadAs::Text)
    {
        b.bitsFromText();
        b.symbolsFromBytes();
        b.used = ReadAs::Text;
        if (b.kindNote.empty()) b.kindNote = std::to_string (b.wordCount) + " WORDS  " + std::to_string (b.lineCount) + " LINES";
        if (b.nbits == 0) { b.bitsFromBytes(); b.used = ReadAs::Bytes; }
    }
    else if (mode == ReadAs::Bytes)
    {
        b.bitsFromBytes();
        b.symbolsFromBytes();
        b.used = ReadAs::Bytes;
        if (b.kindNote.empty()) b.kindNote = "8 BITS / BYTE  MSB FIRST";
    }
    static std::atomic<uint32_t> serials { 1 };
    b.serial = serials.fetch_add (1);
}

bool fromBytes (DataBlob& b, std::vector<uint8_t> bytes, const std::string& name, const std::string& path,
                uint64_t fileSize, ReadAs requested, const std::vector<float>* audio, double rate, int channels)
{
    if (bytes.empty()) return false;
    b.name = name;
    const auto dot = name.find_last_of ('.');
    b.ext = dot == std::string::npos ? std::string() : name.substr (dot + 1);
    b.path = path;
    if (bytes.size() > DataBlob::kMaxBytes) bytes.resize (DataBlob::kMaxBytes);
    b.fileSize = fileSize > 0 ? fileSize : (uint64_t) bytes.size();
    b.truncated = b.fileSize > bytes.size();
    b.bytes = std::move (bytes);
    build (b, requested, audio, rate, channels);
    return true;
}

bool fromText (DataBlob& b, const std::string& text, const std::string& name, ReadAs requested)
{
    std::vector<uint8_t> m (text.begin(), text.end());
    if (m.empty()) m.push_back (' ');
    return fromBytes (b, std::move (m), name, {}, 0, requested == ReadAs::Auto ? ReadAs::Text : requested);
}

std::string exampleText (int index) { return kExampleTexts[std::max (1, std::min (9, index)) - 1]; }

std::string exampleName (int index)
{
    const int i = std::max (1, std::min (9, index));
    return "example " + std::to_string (i) + " - " + kExampleNames[i - 1];
}
} // namespace fdat_pdata
