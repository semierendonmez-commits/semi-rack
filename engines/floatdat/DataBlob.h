// SEMI port: vendored from FLOATDAT Source/engine (7e30328); namespaces renamed (ryoz/vtx/pinball/jdat/pdata -> fdat_*), see sources.mk.
// DataBlob — whatever the user loads, turned into the material every layer reads.
//
//   bytes      the file as it is (capped, see kMaxBytes)
//   bits       the bitstream the automata read. How the bytes become bits is the
//              "READ AS" choice:
//                BYTES  every byte MSB first, 8 bits each (any file)
//                TEXT   tst_ingp's [p txt2bi]: each character's code in binary,
//                       no leading zeros, line breaks and zero bytes skipped
//                AUDIO  decoded samples: 1 when the sample is above zero
//                IMAGE  the picture scaled to 64 columns, Bayer-dithered, row by row
//   symbols    the stream JDAT writes into its buffers (-1..1): bytes centred, or
//              audio samples, or pixel brightness
//   analysis   type from the magic number, Shannon entropy (whole file and per
//              block), byte histogram, printable strings with their offsets, CRC-32
//
// Immutable once built; the audio thread only ever reads it.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

namespace fdat_pdata
{
enum class ReadAs { Auto = 0, Bytes = 1, Text = 2, Audio = 3, Image = 4 };

struct FoundString { uint64_t offset; std::string text; };

struct DataBlob
{
    static constexpr size_t kMaxBytes = 32u * 1024u * 1024u;
    static constexpr int kEntropyBlocks = 512;

    // ---- identity --------------------------------------------------------------
    std::string name = "untitled", ext, path;
    std::string typeName = "DATA", magicHex, kindNote;
    uint64_t fileSize = 0;
    bool truncated = false;
    ReadAs used = ReadAs::Bytes;      // what the bits were actually made from
    uint32_t serial = 0;              // bumped by the loader

    // ---- material --------------------------------------------------------------
    std::vector<uint8_t> bytes;
    std::vector<uint8_t> packed;      // bitstream, MSB first
    uint64_t nbits = 0;
    std::vector<float> symbols;       // -1..1

    // ---- extras for AUDIO / IMAGE -----------------------------------------------
    double audioRate = 0.0, audioSeconds = 0.0;
    int audioChannels = 0;
    int imageW = 0, imageH = 0;       // original size
    int rasterRows = 0;               // IMAGE: rows of 64 bits

    // ---- analysis ----------------------------------------------------------------
    std::array<uint32_t, 256> hist {};
    double entropy = 0.0;             // bits per byte, 0..8
    std::vector<float> blockEntropy;  // 0..1 per block of the file
    std::vector<FoundString> strings;
    uint32_t crc32 = 0;
    double printable = 0.0;           // share of printable bytes
    uint64_t lineCount = 0, wordCount = 0;

    // ---- reading -----------------------------------------------------------------
    bool empty() const noexcept { return nbits == 0; }

    inline int bit (uint64_t i) const noexcept
    {
        if (nbits == 0) return 0;
        i %= nbits;
        return (packed[(size_t) (i >> 3)] >> (7 - (int) (i & 7))) & 1;
    }

    // 64 bits starting at `pos`, first bit in bit 0 (IKD cell order)
    uint64_t word64 (uint64_t pos, int width = 64) const noexcept
    {
        uint64_t w = 0;
        for (int k = 0; k < width; ++k)
            if (bit (pos + (uint64_t) k)) w |= (uint64_t) 1 << k;
        return w;
    }

    // the file byte that a bit of the stream came from (exact for BYTES,
    // proportional for TEXT / AUDIO / IMAGE whose bits are not the raw bytes)
    uint64_t byteForBit (uint64_t b) const noexcept
    {
        if (nbits == 0 || bytes.empty()) return 0;
        if (used == ReadAs::Bytes) return (b % nbits) / 8u;
        return (uint64_t) ((double) (b % nbits) / (double) nbits * (double) bytes.size());
    }

    // where the material starts: skip a silent / constant beginning (headers of
    // zeros, digital silence) so a load sounds at once. 64-bit aligned.
    uint64_t firstLively (int window = 2048) const noexcept
    {
        if (nbits <= (uint64_t) window * 2u) return 0;
        for (uint64_t at = 0; at + (uint64_t) window < nbits; at += 512u)
        {
            const int n = popcount (at, window);
            if (n > window / 20 && n < window - window / 20) return at & ~(uint64_t) 63;
        }
        return 0;
    }

    inline uint8_t byteAt (uint64_t i) const noexcept
    {
        if (bytes.empty()) return 0;
        return bytes[(size_t) (i % bytes.size())];
    }

    inline float symbolAt (uint64_t i) const noexcept
    {
        if (symbols.empty()) return 0.0f;
        return symbols[(size_t) (i % symbols.size())];
    }

    // number of 1s in [pos, pos+len)
    int popcount (uint64_t pos, int len) const noexcept
    {
        int n = 0;
        for (int k = 0; k < len; ++k) n += bit (pos + (uint64_t) k);
        return n;
    }

    // ---- building (message / loader thread) ------------------------------------------
    void packBits (const std::vector<uint8_t>& oneBitPerByte)
    {
        nbits = oneBitPerByte.size();
        packed.assign ((size_t) ((nbits + 7) / 8), 0);
        for (size_t i = 0; i < oneBitPerByte.size(); ++i)
            if (oneBitPerByte[i]) packed[i >> 3] |= (uint8_t) (0x80u >> (i & 7));
    }

    void bitsFromBytes()
    {
        packed = bytes;
        nbits = (uint64_t) bytes.size() * 8u;
    }

    // [p txt2bi] / [p 10to2]
    void bitsFromText()
    {
        std::vector<uint8_t> b;
        b.reserve (bytes.size() * 7);
        for (unsigned char ch : bytes)
        {
            if (ch == '\r' || ch == '\n' || ch == 0) continue;
            uint8_t tmp[8]; int n = 0;
            for (unsigned v = ch; v != 0; v >>= 1) tmp[n++] = (uint8_t) (v & 1u);
            while (n > 0) b.push_back (tmp[--n]);
        }
        packBits (b);
    }

    // bytes centred and stretched to the file's own range (text lives in 32..126)
    void symbolsFromBytes()
    {
        symbols.resize (bytes.size());
        int lo = 255, hi = 0;
        for (auto b : bytes) { lo = std::min (lo, (int) b); hi = std::max (hi, (int) b); }
        const float mid = 0.5f * (float) (lo + hi), half = std::max (1.0f, 0.5f * (float) (hi - lo));
        for (size_t i = 0; i < bytes.size(); ++i) symbols[i] = std::clamp (((float) bytes[i] - mid) / half, -1.0f, 1.0f);
    }

    static uint32_t crc (const uint8_t* d, size_t n)
    {
        static uint32_t table[256];
        static bool init = false;
        if (! init)
        {
            for (uint32_t i = 0; i < 256; ++i)
            {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                table[i] = c;
            }
            init = true;
        }
        uint32_t c = 0xFFFFFFFFu;
        for (size_t i = 0; i < n; ++i) c = table[(c ^ d[i]) & 0xff] ^ (c >> 8);
        return c ^ 0xFFFFFFFFu;
    }

    void analyse()
    {
        hist.fill (0);
        size_t printableCount = 0;
        lineCount = wordCount = 0;
        bool inWord = false;
        for (auto b : bytes)
        {
            ++hist[b];
            const bool pr = (b >= 32 && b < 127) || b == '\n' || b == '\r' || b == '\t';
            if (pr) ++printableCount;
            if (b == '\n') ++lineCount;
            const bool ws = b == ' ' || b == '\n' || b == '\r' || b == '\t';
            if (! ws && pr && ! inWord) { ++wordCount; inWord = true; }
            if (ws || ! pr) inWord = false;
        }
        printable = bytes.empty() ? 0.0 : (double) printableCount / (double) bytes.size();
        entropy = entropyOf (hist, bytes.size());

        blockEntropy.assign ((size_t) kEntropyBlocks, 0.0f);
        if (! bytes.empty())
        {
            const size_t per = std::max<size_t> (1, (bytes.size() + kEntropyBlocks - 1) / kEntropyBlocks);
            for (int b = 0; b < kEntropyBlocks; ++b)
            {
                const size_t a = (size_t) b * per;
                if (a >= bytes.size()) { blockEntropy[(size_t) b] = blockEntropy[(size_t) std::max (0, b - 1)]; continue; }
                const size_t e = std::min (bytes.size(), a + per);
                std::array<uint32_t, 256> h {};
                for (size_t i = a; i < e; ++i) ++h[bytes[i]];
                blockEntropy[(size_t) b] = (float) (entropyOf (h, e - a) / 8.0);
            }
        }

        // printable runs of 4+ characters, like strings(1)
        strings.clear();
        std::string cur;
        uint64_t start = 0;
        for (size_t i = 0; i <= bytes.size() && strings.size() < 4000; ++i)
        {
            const int c = i < bytes.size() ? bytes[i] : 0;
            if (c >= 32 && c < 127)
            {
                if (cur.empty()) start = i;
                if (cur.size() < 64) cur.push_back ((char) c);
            }
            else
            {
                if (cur.size() >= 4) strings.push_back ({ start, cur });
                cur.clear();
            }
        }
        crc32 = crc (bytes.data(), bytes.size());
        detectType();
    }

    static double entropyOf (const std::array<uint32_t, 256>& h, size_t n)
    {
        if (n == 0) return 0.0;
        double e = 0.0;
        for (auto c : h)
            if (c) { const double p = (double) c / (double) n; e -= p * std::log2 (p); }
        return e;
    }

    // a few dozen magic numbers; the rest is "DATA" or "TEXT"
    void detectType()
    {
        auto has = [this] (size_t at, const char* sig, size_t n)
        {
            if (bytes.size() < at + n) return false;
            for (size_t i = 0; i < n; ++i) if (bytes[at + i] != (uint8_t) sig[i]) return false;
            return true;
        };
        magicHex.clear();
        static const char* hx = "0123456789ABCDEF";
        for (size_t i = 0; i < std::min<size_t> (8, bytes.size()); ++i)
        {
            magicHex.push_back (hx[bytes[i] >> 4]); magicHex.push_back (hx[bytes[i] & 15]);
            if (i < 7) magicHex.push_back (' ');
        }
        struct M { size_t at; const char* sig; size_t n; const char* type; };
        static const M ms[] = {
            { 0, "\x89PNG", 4, "PNG IMAGE" }, { 0, "\xFF\xD8\xFF", 3, "JPEG IMAGE" }, { 0, "GIF8", 4, "GIF IMAGE" },
            { 0, "BM", 2, "BMP IMAGE" }, { 0, "II*\0", 4, "TIFF IMAGE" }, { 0, "MM\0*", 4, "TIFF IMAGE" },
            { 8, "WEBP", 4, "WEBP IMAGE" }, { 8, "WAVE", 4, "WAV AUDIO" }, { 8, "AIFF", 4, "AIFF AUDIO" }, { 8, "AIFC", 4, "AIFF-C AUDIO" },
            { 0, "fLaC", 4, "FLAC AUDIO" }, { 0, "OggS", 4, "OGG STREAM" }, { 0, "ID3", 3, "MP3 AUDIO" }, { 0, "\xFF\xFB", 2, "MP3 AUDIO" },
            { 4, "ftyp", 4, "MPEG-4 / QT" }, { 0, "MThd", 4, "MIDI FILE" }, { 0, "%PDF", 4, "PDF DOCUMENT" },
            { 0, "PK\x03\x04", 4, "ZIP ARCHIVE" }, { 0, "\x1F\x8B", 2, "GZIP" }, { 0, "7z\xBC\xAF", 4, "7-ZIP ARCHIVE" },
            { 0, "Rar!", 4, "RAR ARCHIVE" }, { 0, "BZh", 3, "BZIP2" }, { 0, "\x7F" "ELF", 4, "ELF EXECUTABLE" },
            { 0, "MZ", 2, "WINDOWS EXECUTABLE" }, { 0, "\xCF\xFA\xED\xFE", 4, "MACH-O 64" }, { 0, "\xCE\xFA\xED\xFE", 4, "MACH-O" },
            { 0, "\xCA\xFE\xBA\xBE", 4, "MACH-O FAT / JAVA" }, { 0, "SQLite format", 13, "SQLITE DATABASE" },
            { 0, "SDIF", 4, "SDIF ANALYSIS" }, { 0, "ampf", 4, "MAX FOR LIVE DEVICE" }, { 0, "{\n\t\"patcher\"", 12, "MAX PATCHER" },
            { 0, "max v2", 6, "MAX TEXT PATCHER" }, { 0, "<?xml", 5, "XML" }, { 0, "<!DOCTYPE html", 14, "HTML" }, { 0, "<html", 5, "HTML" },
            { 0, "{\\rtf", 5, "RTF DOCUMENT" }, { 0, "\xD0\xCF\x11\xE0", 4, "OLE / MS OFFICE" }, { 0, "RIFF", 4, "RIFF CONTAINER" },
            { 0, "FORM", 4, "IFF CONTAINER" }, { 0, "\x1A\x45\xDF\xA3", 4, "MATROSKA / WEBM" }, { 0, "8BPS", 4, "PHOTOSHOP" },
            { 0, "wOFF", 4, "WOFF FONT" }, { 0, "\0\x01\0\0", 4, "TRUETYPE FONT" }, { 0, "OTTO", 4, "OPENTYPE FONT" },
        };
        for (auto& m : ms)
            if (has (m.at, m.sig, m.n)) { typeName = m.type; return; }
        if (bytes.empty()) typeName = "EMPTY";
        else if (printable > 0.95) typeName = "TEXT";
        else typeName = "BINARY DATA";
    }
};

} // namespace fdat_pdata
