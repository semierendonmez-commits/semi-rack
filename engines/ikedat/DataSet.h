// Vendored from semierendonmez-commits/ikedat Source/engine/DataSet.h (JUCE-free). Semi-rack change: wrapped in namespace ikt (one binary, ~35 engines).
// IKEDAT — data source.
//
// Any file is just bytes. A DataSet keeps the bytes (or an even sample of them
// for very large files) plus everything the sound and the picture read from
// them: a guess at the file type from its magic number, a byte histogram, the
// Shannon entropy of the whole file and of 256 blocks, the printable strings
// inside it (offsets + text, like `strings`) and an FNV-1a hash.
//
// No JUCE in here: the engine tests build it on its own.
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace ikt {

namespace dat
{

struct StringHit
{
    std::uint32_t offset = 0;
    std::string text;
};

struct DataSet
{
    static constexpr std::size_t kMaxBytes   = 32u * 1024u * 1024u;   // bigger files are sampled
    static constexpr int         kBlocks     = 256;
    static constexpr std::size_t kMaxStrings = 4096;

    std::string name;              // file name or "text"
    std::string kind;              // "PNG IMAGE", "WAVE AUDIO", "UTF-8 TEXT", ...
    std::string mime;              // short tag for the screen: PNG, WAV, TXT ...
    std::uint64_t originalSize = 0;
    bool sampled = false;          // true when bytes is an even sample of a bigger file
    std::vector<std::uint8_t> bytes;

    std::array<std::uint32_t, 256> hist {};
    std::array<float, kBlocks> blockEntropy {};   // bits per byte, 0..8
    double entropy = 0.0;                         // bits per byte, 0..8
    double ones = 0.0;                            // fraction of 1 bits
    double textRatio = 0.0;                       // printable ASCII share
    std::uint64_t hash = 0;                       // FNV-1a 64 of the bytes kept
    std::vector<StringHit> strings;              // at most kMaxStrings, spread over the file
    std::size_t stringsTotal = 0;

    std::size_t size() const noexcept { return bytes.size(); }
    bool empty() const noexcept { return bytes.empty(); }

    std::uint8_t byteAt (std::uint64_t i) const noexcept
    {
        return bytes.empty() ? 0 : bytes[(std::size_t) (i % bytes.size())];
    }

    // 64 bits starting at an arbitrary bit position (wraps around the end).
    std::uint64_t bits64 (std::uint64_t bitPos) const noexcept
    {
        if (bytes.empty()) return 0;
        std::uint64_t w = 0;
        const std::uint64_t nbits = (std::uint64_t) bytes.size() * 8u;
        for (int k = 0; k < 64; ++k)
        {
            const std::uint64_t b = (bitPos + (std::uint64_t) k) % nbits;
            if ((bytes[(std::size_t) (b >> 3)] >> (7 - (int) (b & 7u))) & 1u)
                w |= std::uint64_t (1) << k;
        }
        return w;
    }

    // first string at or after a byte offset (wraps), -1 if there are none
    int stringNear (std::uint64_t offset) const noexcept
    {
        if (strings.empty()) return -1;
        const auto o = (std::uint32_t) (bytes.empty() ? 0 : offset % bytes.size());
        std::size_t lo = 0, hi = strings.size();
        while (lo < hi)
        {
            const auto mid = (lo + hi) / 2;
            if (strings[mid].offset < o) lo = mid + 1; else hi = mid;
        }
        return (int) (lo % strings.size());
    }

    //==========================================================================
    static std::unique_ptr<DataSet> fromBytes (std::string name, std::vector<std::uint8_t> data,
                                               std::uint64_t originalSize = 0, bool sampled = false)
    {
        auto d = std::make_unique<DataSet>();
        d->name = std::move (name);
        d->bytes = std::move (data);
        d->originalSize = originalSize ? originalSize : (std::uint64_t) d->bytes.size();
        d->sampled = sampled;
        d->analyse();
        return d;
    }

    static std::unique_ptr<DataSet> fromText (const std::string& text)
    {
        std::vector<std::uint8_t> b (text.begin(), text.end());
        auto d = fromBytes ("text", std::move (b));
        d->kind = "TYPED TEXT";
        d->mime = "TXT";
        return d;
    }

    // Read a file through a callback-free path so tests can use it too.
    // Files over kMaxBytes are read as 256 evenly spaced chunks.
    static std::unique_ptr<DataSet> fromFile (const std::string& path, const std::string& displayName)
    {
        std::FILE* f = std::fopen (path.c_str(), "rb");
        if (f == nullptr) return nullptr;
        std::fseek (f, 0, SEEK_END);
        const long long total = (long long) std::ftell (f);
        std::fseek (f, 0, SEEK_SET);
        if (total <= 0) { std::fclose (f); return nullptr; }

        std::vector<std::uint8_t> buf;
        bool sampled = false;
        if ((unsigned long long) total <= kMaxBytes)
        {
            buf.resize ((std::size_t) total);
            const auto got = std::fread (buf.data(), 1, buf.size(), f);
            buf.resize (got);
        }
        else
        {
            sampled = true;
            constexpr int chunks = 256;
            const std::size_t chunk = kMaxBytes / chunks;
            buf.reserve (kMaxBytes);
            std::vector<std::uint8_t> tmp (chunk);
            for (int c = 0; c < chunks; ++c)
            {
                const long long at = (long long) ((double) (total - (long long) chunk) * c / (chunks - 1));
                std::fseek (f, (long) at, SEEK_SET);
                const auto got = std::fread (tmp.data(), 1, chunk, f);
                buf.insert (buf.end(), tmp.begin(), tmp.begin() + (long) got);
            }
        }
        std::fclose (f);
        if (buf.empty()) return nullptr;
        return fromBytes (displayName, std::move (buf), (std::uint64_t) total, sampled);
    }

    //==========================================================================
    void analyse()
    {
        hist.fill (0);
        blockEntropy.fill (0.0f);
        strings.clear();
        entropy = ones = textRatio = 0.0;
        hash = 0xcbf29ce484222325ull;
        if (bytes.empty()) { kind = "EMPTY"; mime = "---"; return; }

        std::uint64_t popc = 0, printable = 0;
        for (auto b : bytes)
        {
            ++hist[b];
            hash = (hash ^ b) * 0x100000001b3ull;
            popc += (std::uint64_t) popcount8 (b);
            if ((b >= 32 && b < 127) || b == 9 || b == 10 || b == 13) ++printable;
        }
        entropy = entropyOf (hist, bytes.size());
        ones = (double) popc / (double) (bytes.size() * 8u);
        textRatio = (double) printable / (double) bytes.size();

        // block entropy
        const std::size_t n = bytes.size();
        for (int k = 0; k < kBlocks; ++k)
        {
            const std::size_t a = n * (std::size_t) k / kBlocks, e = std::max (a + 1, n * (std::size_t) (k + 1) / kBlocks);
            std::array<std::uint32_t, 256> h {};
            for (std::size_t i = a; i < e && i < n; ++i) ++h[bytes[i]];
            blockEntropy[(std::size_t) k] = (float) entropyOf (h, std::min (e, n) - a);
        }

        // printable runs (min 4), like `strings`. Two passes: count, then keep
        // every k-th so the list spans the whole file even when it is huge.
        auto scan = [this, n] (auto&& emit)
        {
            std::string cur;
            std::uint32_t start = 0;
            for (std::size_t i = 0; i <= n; ++i)
            {
                const int b = i < n ? bytes[i] : 0;
                const bool pr = b >= 32 && b < 127;
                if (pr)
                {
                    if (cur.empty()) start = (std::uint32_t) i;
                    cur.push_back ((char) b);
                    if ((cur.size() >= 48 && b == ' ') || cur.size() >= 72)   // long text: one entry per ~line
                    {
                        emit (start, cur);
                        cur.clear();
                    }
                }
                else
                {
                    if (cur.size() >= 4) emit (start, cur);
                    cur.clear();
                }
            }
        };
        std::size_t total = 0;
        scan ([&total] (std::uint32_t, const std::string&) { ++total; });
        const std::size_t stride = total > kMaxStrings ? (total + kMaxStrings - 1) / kMaxStrings : 1;
        std::size_t k = 0;
        scan ([&] (std::uint32_t at, const std::string& t)
        {
            if (k++ % stride == 0 && strings.size() < kMaxStrings) strings.push_back ({ at, trimmed (t) });
        });
        stringsTotal = total;

        detectKind();
    }

    static int popcount8 (std::uint8_t b) noexcept
    {
        int c = 0;
        while (b) { b &= (std::uint8_t) (b - 1); ++c; }
        return c;
    }

    static double entropyOf (const std::array<std::uint32_t, 256>& h, std::size_t n) noexcept
    {
        if (n == 0) return 0.0;
        double e = 0.0;
        for (auto c : h)
            if (c)
            {
                const double p = (double) c / (double) n;
                e -= p * std::log2 (p);
            }
        return e;
    }

    static std::string hex64 (std::uint64_t v)
    {
        static const char* d = "0123456789abcdef";
        std::string s (16, '0');
        for (int i = 15; i >= 0; --i) { s[(std::size_t) i] = d[v & 15u]; v >>= 4; }
        return s;
    }

private:
    static std::string trimmed (const std::string& s)
    {
        std::size_t a = 0, b = s.size();
        while (a < b && s[a] == ' ') ++a;
        while (b > a && s[b - 1] == ' ') --b;
        return s.substr (a, b - a);
    }

    bool startsWith (const char* m, std::size_t len, std::size_t at = 0) const noexcept
    {
        return bytes.size() >= at + len && std::memcmp (bytes.data() + at, m, len) == 0;
    }

    void detectKind()
    {
        struct Magic { const char* sig; std::size_t len; std::size_t at; const char* kind; const char* mime; };
        static const Magic table[] = {
            { "\x89PNG\r\n\x1a\n", 8, 0, "PNG IMAGE", "PNG" },
            { "\xff\xd8\xff", 3, 0, "JPEG IMAGE", "JPG" },
            { "GIF87a", 6, 0, "GIF IMAGE", "GIF" },
            { "GIF89a", 6, 0, "GIF IMAGE", "GIF" },
            { "BM", 2, 0, "BITMAP IMAGE", "BMP" },
            { "II*\0", 4, 0, "TIFF IMAGE", "TIF" },
            { "MM\0*", 4, 0, "TIFF IMAGE", "TIF" },
            { "WEBP", 4, 8, "WEBP IMAGE", "WEBP" },
            { "WAVE", 4, 8, "WAVE AUDIO", "WAV" },
            { "AVI ", 4, 8, "AVI VIDEO", "AVI" },
            { "AIFF", 4, 8, "AIFF AUDIO", "AIF" },
            { "AIFC", 4, 8, "AIFF-C AUDIO", "AIF" },
            { "fLaC", 4, 0, "FLAC AUDIO", "FLAC" },
            { "OggS", 4, 0, "OGG STREAM", "OGG" },
            { "ID3", 3, 0, "MP3 AUDIO", "MP3" },
            { "MThd", 4, 0, "STANDARD MIDI FILE", "MID" },
            { "ftyp", 4, 4, "MPEG-4 / QUICKTIME", "MP4" },
            { "%PDF", 4, 0, "PDF DOCUMENT", "PDF" },
            { "PK\x03\x04", 4, 0, "ZIP ARCHIVE", "ZIP" },
            { "7z\xbc\xaf\x27\x1c", 6, 0, "7-ZIP ARCHIVE", "7Z" },
            { "\x1f\x8b", 2, 0, "GZIP STREAM", "GZ" },
            { "BZh", 3, 0, "BZIP2 STREAM", "BZ2" },
            { "Rar!", 4, 0, "RAR ARCHIVE", "RAR" },
            { "\x7f" "ELF", 4, 0, "ELF EXECUTABLE", "ELF" },
            { "\xcf\xfa\xed\xfe", 4, 0, "MACH-O 64 BINARY", "MACH" },
            { "\xce\xfa\xed\xfe", 4, 0, "MACH-O BINARY", "MACH" },
            { "\xca\xfe\xba\xbe", 4, 0, "UNIVERSAL BINARY", "MACH" },
            { "MZ", 2, 0, "WINDOWS EXECUTABLE", "EXE" },
            { "SQLite format 3", 15, 0, "SQLITE DATABASE", "SQL" },
            { "ampf", 4, 0, "MAX FOR LIVE DEVICE", "AMXD" },
            { "\xd0\xcf\x11\xe0", 4, 0, "OLE2 DOCUMENT", "DOC" },
            { "{\\rtf", 5, 0, "RTF DOCUMENT", "RTF" },
            { "<?xml", 5, 0, "XML DOCUMENT", "XML" },
            { "#!", 2, 0, "SCRIPT", "SH" },
            { "max v2", 6, 0, "MAX TEXT PATCH", "MXT" },
        };
        for (const auto& m : table)
            if (startsWith (m.sig, m.len, m.at)) { kind = m.kind; mime = m.mime; return; }

        if (bytes.size() >= 2 && ((bytes[0] == 0xff && (bytes[1] & 0xe0) == 0xe0)))
        { kind = "MPEG AUDIO"; mime = "MP3"; return; }

        // skip a UTF-8 BOM / whitespace and look at the first character
        std::size_t i = startsWith ("\xef\xbb\xbf", 3) ? 3 : 0;
        while (i < bytes.size() && (bytes[i] == ' ' || bytes[i] == '\n' || bytes[i] == '\r' || bytes[i] == '\t')) ++i;
        const int c0 = i < bytes.size() ? bytes[i] : 0;
        if (textRatio > 0.95 || validUtf8Share() > 0.97)
        {
            if (c0 == '{' || c0 == '[') { kind = "JSON / TEXT"; mime = "JSON"; return; }
            if (c0 == '<')              { kind = "MARKUP TEXT"; mime = "HTML"; return; }
            kind = "UTF-8 TEXT"; mime = "TXT"; return;
        }
        if (entropy > 7.6) { kind = "HIGH ENTROPY BINARY"; mime = "BIN"; return; }
        kind = "BINARY DATA"; mime = "BIN";
    }

    double validUtf8Share() const noexcept
    {
        std::size_t ok = 0, i = 0;
        const std::size_t n = std::min<std::size_t> (bytes.size(), 1u << 20);
        while (i < n)
        {
            const int b = bytes[i];
            int len = b < 0x80 ? 1 : (b >> 5) == 6 ? 2 : (b >> 4) == 14 ? 3 : (b >> 3) == 30 ? 4 : 0;
            if (len == 0 || i + (std::size_t) len > n) { ++i; continue; }
            bool good = true;
            for (int k = 1; k < len; ++k) if ((bytes[i + (std::size_t) k] & 0xc0) != 0x80) good = false;
            if (good && (b >= 0x80 || b >= 32 || b == 9 || b == 10 || b == 13)) ok += (std::size_t) len;
            i += good ? (std::size_t) len : 1;
        }
        return n ? (double) ok / (double) n : 0.0;
    }
};

} // namespace dat

} // namespace ikt
