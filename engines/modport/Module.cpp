#include "Module.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace modport
{

// ProTracker 2.3 period table (via pt2-clone, BSD-3, Olav Sørensen), including the
// 15 words that follow it in PT's memory: arpeggio on finetune -1 reads into them.
const std::int16_t kPeriodTable[37 * 16 + 15] = {
    856,808,762,720,678,640,604,570,538,508,480,453, 428,404,381,360,339,320,302,285,269,254,240,226, 214,202,190,180,170,160,151,143,135,127,120,113,0,
    850,802,757,715,674,637,601,567,535,505,477,450, 425,401,379,357,337,318,300,284,268,253,239,225, 213,201,189,179,169,159,150,142,134,126,119,113,0,
    844,796,752,709,670,632,597,563,532,502,474,447, 422,398,376,355,335,316,298,282,266,251,237,224, 211,199,188,177,167,158,149,141,133,125,118,112,0,
    838,791,746,704,665,628,592,559,528,498,470,444, 419,395,373,352,332,314,296,280,264,249,235,222, 209,198,187,176,166,157,148,140,132,125,118,111,0,
    832,785,741,699,660,623,588,555,524,495,467,441, 416,392,370,350,330,312,294,278,262,247,233,220, 208,196,185,175,165,156,147,139,131,124,117,110,0,
    826,779,736,694,655,619,584,551,520,491,463,437, 413,390,368,347,328,309,292,276,260,245,232,219, 206,195,184,174,164,155,146,138,130,123,116,109,0,
    820,774,730,689,651,614,580,547,516,487,460,434, 410,387,365,345,325,307,290,274,258,244,230,217, 205,193,183,172,163,154,145,137,129,122,115,109,0,
    814,768,725,684,646,610,575,543,513,484,457,431, 407,384,363,342,323,305,288,272,256,242,228,216, 204,192,181,171,161,152,144,136,128,121,114,108,0,
    907,856,808,762,720,678,640,604,570,538,508,480, 453,428,404,381,360,339,320,302,285,269,254,240, 226,214,202,190,180,170,160,151,143,135,127,120,0,
    900,850,802,757,715,675,636,601,567,535,505,477, 450,425,401,379,357,337,318,300,284,268,253,238, 225,212,200,189,179,169,159,150,142,134,126,119,0,
    894,844,796,752,709,670,632,597,563,532,502,474, 447,422,398,376,355,335,316,298,282,266,251,237, 223,211,199,188,177,167,158,149,141,133,125,118,0,
    887,838,791,746,704,665,628,592,559,528,498,470, 444,419,395,373,352,332,314,296,280,264,249,235, 222,209,198,187,176,166,157,148,140,132,125,118,0,
    881,832,785,741,699,660,623,588,555,524,494,467, 441,416,392,370,350,330,312,294,278,262,247,233, 220,208,196,185,175,165,156,147,139,131,123,117,0,
    875,826,779,736,694,655,619,584,551,520,491,463, 437,413,390,368,347,328,309,292,276,260,245,232, 219,206,195,184,174,164,155,146,138,130,123,116,0,
    868,820,774,730,689,651,614,580,547,516,487,460, 434,410,387,365,345,325,307,290,274,258,244,230, 217,205,193,183,172,163,154,145,137,129,122,115,0,
    862,814,768,725,684,646,610,575,543,513,484,457, 431,407,384,363,342,323,305,288,272,256,242,228, 216,203,192,181,171,161,152,144,136,128,121,114,0,
    774,1800,2314,3087,4113,4627,5400,6426,6940,7713, 8739,9253,24625,12851,13365
};

namespace
{
inline std::uint16_t be16 (const std::uint8_t* p) { return (std::uint16_t) ((p[0] << 8) | p[1]); }
inline void putBe16 (std::uint8_t* p, std::uint16_t v) { p[0] = (std::uint8_t) (v >> 8); p[1] = (std::uint8_t) v; }

std::string printable (const char* s, int n)
{
    std::string out;
    for (int i = 0; i < n && s[i] != 0; ++i)
    {
        const auto c = (unsigned char) s[i];
        out += (c >= 32 && c < 127) ? (char) c : ' ';
    }
    while (! out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

Cell decodeCell (const std::uint8_t* p)
{
    Cell c;
    c.sample = (std::uint8_t) ((p[0] & 0xF0) | (p[2] >> 4));
    c.period = (std::uint16_t) (((p[0] & 0x0F) << 8) | p[1]);
    c.effect = (std::uint8_t) (p[2] & 0x0F);
    c.param = p[3];
    return c;
}

void encodeCell (const Cell& c, std::uint8_t* p)
{
    p[0] = (std::uint8_t) ((c.sample & 0xF0) | ((c.period >> 8) & 0x0F));
    p[1] = (std::uint8_t) (c.period & 0xFF);
    p[2] = (std::uint8_t) (((c.sample & 0x0F) << 4) | (c.effect & 0x0F));
    p[3] = c.param;
}

struct TagInfo { const char* tag; int channels; const char* name; Layout layout; };

const TagInfo kTags[] = {
    { "M.K.", 4, "ProTracker M.K.", Layout::Standard31 },
    { "M!K!", 4, "ProTracker M!K! (>64 patterns)", Layout::Standard31 },
    { "M&K!", 4, "NoiseTracker M&K!", Layout::Standard31 },
    { "N.T.", 4, "NoiseTracker N.T.", Layout::Standard31 },
    { "FLT4", 4, "StarTrekker FLT4", Layout::Standard31 },
    { "LARD", 4, "unknown tracker LARD", Layout::Standard31 },
    { "NSMS", 4, "unknown tracker NSMS", Layout::Standard31 },
    { "CD61", 6, "Octalyser CD61", Layout::Standard31 },
    { "CD81", 8, "Octalyser CD81", Layout::Standard31 },
    { "OKTA", 8, "Oktalyzer OKTA", Layout::Standard31 },
    { "OCTA", 8, "OctaMED OCTA", Layout::Standard31 },
    { "TDZ1", 1, "TakeTracker TDZ1", Layout::Standard31 },
    { "TDZ2", 2, "TakeTracker TDZ2", Layout::Standard31 },
    { "TDZ3", 3, "TakeTracker TDZ3", Layout::Standard31 },
    { "TDZ4", 4, "TakeTracker TDZ4", Layout::Standard31 },
    { "FA04", 4, "Digital Tracker FA04", Layout::DigitalTracker },
    { "FA06", 6, "Digital Tracker FA06", Layout::DigitalTracker },
    { "FA08", 8, "Digital Tracker FA08", Layout::DigitalTracker },
    { "FLT8", 8, "StarTrekker FLT8", Layout::StarTrekker8 },
};

bool isDigit (char c) { return c >= '0' && c <= '9'; }

// returns channels (0 = unknown tag, -n = n channels but too many)
int channelsFromTag (const char* t, std::string& name, Layout& layout)
{
    for (const auto& k : kTags)
        if (std::memcmp (t, k.tag, 4) == 0)
        {
            name = k.name;
            layout = k.layout;
            return k.channels;
        }
    layout = Layout::Standard31;
    if (isDigit (t[0]) && std::memcmp (t + 1, "CHN", 3) == 0)
    {
        const int n = t[0] - '0';
        name = std::string (1, t[0]) + "CHN (" + (n == 6 || n == 8 ? "FastTracker / OctaMED" : "TakeTracker") + ")";
        return n == 0 ? 0 : (n <= kMaxChannels ? n : -n);
    }
    if (isDigit (t[0]) && isDigit (t[1]) && (std::memcmp (t + 2, "CH", 2) == 0 || std::memcmp (t + 2, "CN", 2) == 0))
    {
        const int n = (t[0] - '0') * 10 + (t[1] - '0');
        name = std::string (t, 4) + " (FastTracker 2)";
        return n <= kMaxChannels ? n : -n;
    }
    return 0;
}

int maxOrder (const std::uint8_t* orders, int count)
{
    int m = 0;
    for (int i = 0; i < count; ++i)
    {
        if (orders[i] > 0x7F) break;   // garbage in the order list (dragnet.mod)
        m = std::max (m, (int) orders[i]);
    }
    return m;
}

// Ultimate SoundTracker / SoundTracker 15-sample check (after libxmp's st_test, MIT).
bool looksLikeSoundTracker (const std::uint8_t* d, std::size_t size)
{
    if (size < 600) return false;
    for (int i = 0; i < 20; ++i)
    {
        if (d[i] == 0 || d[i] == '\r') break;
        if (i == 4 && d[4] == 0x13 && d[5] == 0x88) break;   // Super Ski 2 names its songs "SONG\x13\x88"
        if (d[i] < 32 || d[i] > 126) return false;
    }
    std::uint32_t smpBytes = 0;
    for (int s = 0; s < 15; ++s)
    {
        const auto* p = d + 20 + s * 30;
        for (int i = 1; i < 22; ++i)   // the first byte is random in some rips
        {
            if (p[i] == 0 || p[i] == '\r') break;   // junk after the NUL / CR is allowed (pennylane.mod, gamemusic.mod)
            if ((p[i] < 32 || p[i] > 126) && p[i] != '\r' && p[i] != '\n') return false;
        }
        const int len = be16 (p + 22), fine = p[24], vol = p[25], ls = be16 (p + 26), ll = be16 (p + 28);
        if (vol > 64 || (fine & 0xF0) != 0 || len > 0x8000 || ll > 0x8000) return false;
        if (len == 0 && ls == 4462 && ll == 2078) continue;   // bad rip of fin-nv1.mod
        if ((ls >> 1) > len) return false;
        if (len != 0 && (ls >> 1) == len) return false;
        if (len == 0 && ls > 0) return false;
        smpBytes += (std::uint32_t) len * 2u;
    }
    const int len = d[470];
    if (len == 0 || len > 128) return false;
    for (int i = 0; i < 128; ++i)
        if (d[472 + i] > 0x7F) return false;
    if (smpBytes < 8) return false;
    int pats = maxOrder (d + 472, 128) + 1;
    const int shortPats = maxOrder (d + 472, len) + 1;
    if (600 + (std::size_t) pats * 1024 > size + 1024) pats = shortPats;   // patterns after the song length not stored
    if (600 + (std::size_t) pats * 1024 > size + 1024) return false;
    // patterns must hold plausible events: sample <= 15, period 0 or 28..1712
    int bad = 0;
    for (int p = 0; p < pats && 600 + (std::size_t) (p + 1) * 1024 <= size; ++p)
        for (int e = 0; e < 256; ++e)
        {
            const Cell c = decodeCell (d + 600 + p * 1024 + e * 4);
            if (c.sample > 15 || (c.period != 0 && (c.period < 28 || c.period > 1712))) ++bad;
        }
    return bad <= 16;
}
} // namespace

//==============================================================================
std::string Sample::name() const { return printable (rawName.data(), 22); }
std::string Module::title() const { return printable (rawTitle.data(), 20); }

const Cell& Module::cell (int pattern, int row, int ch) const
{
    static const Cell none;
    if (pattern < 0 || pattern >= (int) patterns.size() || row < 0 || row >= kRows || ch < 0 || ch >= channels) return none;
    return patterns[(std::size_t) pattern][(std::size_t) (row * channels + ch)];
}

int Module::patternAt (int order) const
{
    if (order < 0 || order >= 128) return -1;
    const int p = orders[(std::size_t) order];
    return p < (int) patterns.size() ? p : -1;
}

int Module::usedSamples() const
{
    int n = 0;
    for (const auto& s : samples) if (s.lengthWords > 1) ++n;
    return n;
}

//==============================================================================
LoadResult loadModule (const std::uint8_t* d, std::size_t size)
{
    LoadResult res;
    auto& m = res.module;

    if (d == nullptr || size < 600)
    {
        res.error = "file too small to be a .mod";
        return res;
    }

    // ---- identify --------------------------------------------------------------
    int channels = 0;
    std::string name;
    Layout layout = Layout::Standard31;
    if (size >= 1084)
        channels = channelsFromTag (reinterpret_cast<const char*> (d + 1080), name, layout);

    if (channels < 0)
    {
        res.error = std::to_string (-channels) + "-channel module: MODPORT plays up to 8 channels";
        return res;
    }
    if (channels == 0)
    {
        if (! looksLikeSoundTracker (d, size))
        {
            res.error = "not a ProTracker-style .mod (unknown format tag)";
            return res;
        }
        layout = Layout::SoundTracker15;
        channels = 4;
        name = "SoundTracker (15 samples)";
    }

    m.layout = layout;
    m.channels = channels;
    m.formatName = name;
    m.numSamples = layout == Layout::SoundTracker15 ? 15 : 31;
    m.tag = layout == Layout::SoundTracker15 ? std::string() : std::string (reinterpret_cast<const char*> (d + 1080), 4);
    std::memcpy (m.rawTitle.data(), d, 20);

    std::uint32_t smpBytes = 0;
    for (int s = 0; s < m.numSamples; ++s)
    {
        const auto* p = d + 20 + s * 30;
        auto& smp = m.samples[(std::size_t) s];
        std::memcpy (smp.rawName.data(), p, 22);
        smp.lengthWords = be16 (p + 22);
        smp.finetuneByte = p[24];
        smp.volumeByte = p[25];
        smp.loopStartWords = be16 (p + 26);
        smp.loopLengthWords = be16 (p + 28);
        smpBytes += smp.lengthBytes();
    }

    const int hdr = m.numSamples * 30 + 20;   // 470 or 950
    m.songLength = d[hdr];
    m.restartByte = d[hdr + 1];
    std::memcpy (m.rawOrders.data(), d + hdr + 2, 128);
    if (m.songLength == 0 || m.songLength > 128)
    {
        m.notes.push_back ("song length " + std::to_string (m.songLength) + " repaired");
        m.songLength = m.songLength == 0 ? 1 : 128;
    }

    std::size_t pos = layout == Layout::SoundTracker15 ? 600 : 1084;
    if (layout == Layout::DigitalTracker) pos += 4;

    int storedPatterns = maxOrder (m.rawOrders.data(), 128) + 1;

    // Mod's Grave WOW: an 8-channel file that says M.K. (by the file size, as libxmp)
    if (m.tag == "M.K.")
    {
        bool maybeWow = m.restartByte == 0;
        for (int s = 0; s < 31; ++s)
        {
            const auto& smp = m.samples[(std::size_t) s];
            if (smp.lengthWords && (smp.finetuneByte != 0 || smp.volumeByte != 64)) maybeWow = false;
        }
        if (maybeWow && 1084 + (std::size_t) storedPatterns * 2048 + smpBytes == (size & ~(std::size_t) 1))
        {
            m.layout = layout = Layout::ModsGrave;
            m.channels = channels = 8;
            m.formatName = "Mod's Grave WOW (M.K., 8 channels)";
        }
    }

    // SoundTracker files that only count the patterns up to the song length
    if (layout == Layout::SoundTracker15)
    {
        const int shortPats = maxOrder (m.rawOrders.data(), m.songLength) + 1;
        const auto expected = [&] (int p) { return 600 + (std::size_t) p * 1024 + smpBytes; };
        if (size < expected (storedPatterns) && size == expected (shortPats))
        {
            storedPatterns = shortPats;
            m.notes.push_back ("SoundTracker order list quirk: patterns after the song length ignored");
        }
    }

    // ---- patterns ----------------------------------------------------------------
    const bool flt8 = layout == Layout::StarTrekker8;
    for (int i = 0; i < 128; ++i)
        m.orders[(std::size_t) i] = flt8 ? (std::uint8_t) (m.rawOrders[(std::size_t) i] >> 1) : m.rawOrders[(std::size_t) i];

    const int filePatChannels = flt8 ? 4 : channels;
    const std::size_t patBytes = (std::size_t) kRows * (std::size_t) filePatChannels * 4;
    const int numPatterns = flt8 ? maxOrder (m.orders.data(), 128) + 1 : storedPatterns;
    if (flt8) storedPatterns = numPatterns * 2;

    if (pos + (std::size_t) storedPatterns * patBytes > size)
    {
        const auto fit = (int) ((size - std::min (size, pos)) / patBytes);
        if (fit == 0)
        {
            res.error = "file is cut short inside the patterns";
            return res;
        }
        m.notes.push_back ("file cut short: " + std::to_string (storedPatterns - fit) + " pattern(s) missing, left empty");
    }

    m.patterns.assign ((std::size_t) numPatterns, std::vector<Cell> ((std::size_t) (kRows * channels)));
    auto readStored = [&] (int stored, std::vector<Cell>& dst, int chOffset)
    {
        const std::size_t base = pos + (std::size_t) stored * patBytes;
        for (int r = 0; r < kRows; ++r)
            for (int c = 0; c < filePatChannels; ++c)
            {
                const std::size_t o = base + ((std::size_t) r * (std::size_t) filePatChannels + (std::size_t) c) * 4;
                if (o + 4 <= size) dst[(std::size_t) (r * channels + c + chOffset)] = decodeCell (d + o);
            }
    };
    for (int p = 0; p < numPatterns; ++p)
    {
        if (flt8)
        {
            readStored (p * 2, m.patterns[(std::size_t) p], 0);
            readStored (p * 2 + 1, m.patterns[(std::size_t) p], 4);
        }
        else
        {
            readStored (p, m.patterns[(std::size_t) p], 0);
        }
    }
    pos += (std::size_t) storedPatterns * patBytes;

    // Ultimate SoundTracker: tempo byte instead of restart, only effects 1 / 2
    if (layout == Layout::SoundTracker15)
    {
        bool ust = m.restartByte >= 0x40;
        for (int s = 0; s < 15; ++s)
        {
            const auto& smp = m.samples[(std::size_t) s];
            if (smp.finetuneByte != 0 || smp.lengthWords > 0x1387 || smp.loopStartWords > 9999 || smp.loopLengthWords > 0x1387) ust = false;
        }
        for (const auto& pat : m.patterns)
            for (const auto& c : pat)
            {
                if (c.effect != 0 && c.effect != 1 && c.effect != 2) ust = false;
                if (c.effect == 1 && c.param == 0) ust = false;
                if (c.effect == 2 && (c.param & 0x0F) != 0 && (c.param & 0xF0) != 0) ust = false;
            }
        m.ultimate = ust;
        m.formatName = ust ? "Ultimate SoundTracker (15 samples)" : "SoundTracker 2.x (15 samples)";
    }

    // ---- sample data ---------------------------------------------------------------
    std::uint32_t missing = 0;
    for (int s = 0; s < m.numSamples; ++s)
    {
        auto& smp = m.samples[(std::size_t) s];
        const std::size_t n = smp.lengthBytes();
        smp.data.assign (n, 0);
        const std::size_t avail = pos < size ? std::min (n, size - pos) : 0;
        if (avail > 0) std::memcpy (smp.data.data(), d + pos, avail);
        missing += (std::uint32_t) (n - avail);
        pos += n;
    }
    if (missing > 0)
        m.notes.push_back ("file cut short: " + std::to_string (missing) + " bytes of sample data missing, filled with silence");
    if (pos < size)
    {
        m.trailing.assign (d + pos, d + size);
        if (size - pos > 4) m.notes.push_back (std::to_string (size - pos) + " bytes after the sample data (kept for .mod export)");
    }

    // ---- sanity notes ---------------------------------------------------------------
    int badLoops = 0;
    for (int s = 0; s < m.numSamples; ++s)
    {
        const auto& smp = m.samples[(std::size_t) s];
        if (smp.looped() && (std::uint32_t) smp.loopStartWords + smp.loopLengthWords > smp.lengthWords) ++badLoops;
    }
    if (badLoops > 0)
        m.notes.push_back (std::to_string (badLoops) + " sample loop(s) reach past the sample end (played as an Amiga would)");

    // tracker-specific rules
    if (m.tag == "CD61" || m.tag == "CD81") m.flow = Flow::Octalyser;
    else if (layout == Layout::DigitalTracker) m.flow = Flow::DigitalTracker;
    m.vblankOnly = m.tag == "M&K!" || m.tag == "N.T." || (layout == Layout::SoundTracker15 && ! m.ultimate);
    for (const auto& pat : m.patterns)
        for (const auto& c : pat)
            if (c.effect == 0xF && c.param >= 0x20) m.highFxx = true;
    m.timing = m.vblankOnly ? Timing::VBlank : Timing::Cia;

    // ProTracker's three octaves, unless the file clearly writes beyond them
    // (a stray 107 in a ProTracker file is a typo, not a fourth octave)
    int outside = 0;
    bool far = false;
    for (const auto& pat : m.patterns)
        for (const auto& c : pat)
            if (c.period != 0 && (c.period < 113 || c.period > 856))
            {
                ++outside;
                if (c.period < 90 || c.period > 1000) far = true;
            }
    const bool beyond = channels > 4 || far || outside >= 4;
    m.extendedPeriods = beyond;

    res.ok = true;
    return res;
}

//==============================================================================
std::string standardTagFor (int channels, int numPatterns)
{
    if (channels == 4) return numPatterns > 64 ? "M!K!" : "M.K.";
    return std::to_string (channels) + "CHN";
}

std::vector<std::uint8_t> saveModule (const Module& m, SaveAs as)
{
    Layout layout = m.layout;
    std::string tag = m.tag;
    if (as == SaveAs::Standard || layout == Layout::ModsGrave)
    {
        layout = Layout::Standard31;
        tag = standardTagFor (m.channels, m.numPatterns());
    }
    if (layout == Layout::Standard31 && tag.size() != 4) tag = standardTagFor (m.channels, m.numPatterns());

    const bool st15 = layout == Layout::SoundTracker15;
    const bool flt8 = layout == Layout::StarTrekker8;
    const int numSamples = st15 ? 15 : 31;
    const int filePatChannels = flt8 ? 4 : m.channels;
    const int storedPatterns = flt8 ? m.numPatterns() * 2 : m.numPatterns();

    std::vector<std::uint8_t> out;
    const std::size_t hdr = st15 ? 600 : (layout == Layout::DigitalTracker ? 1088 : 1084);
    out.resize (hdr, 0);
    std::memcpy (out.data(), m.rawTitle.data(), 20);
    for (int s = 0; s < numSamples; ++s)
    {
        const auto& smp = m.samples[(std::size_t) s];
        auto* p = out.data() + 20 + s * 30;
        std::memcpy (p, smp.rawName.data(), 22);
        putBe16 (p + 22, smp.lengthWords);
        p[24] = smp.finetuneByte;
        p[25] = smp.volumeByte;
        putBe16 (p + 26, smp.loopStartWords);
        putBe16 (p + 28, smp.loopLengthWords);
    }
    const int ho = numSamples * 30 + 20;
    out[(std::size_t) ho] = (std::uint8_t) m.songLength;
    out[(std::size_t) ho + 1] = m.restartByte;
    if (m.layout == layout)
        std::memcpy (out.data() + ho + 2, m.rawOrders.data(), 128);
    else
        for (int i = 0; i < 128; ++i)
            out[(std::size_t) (ho + 2 + i)] = flt8 ? (std::uint8_t) (m.orders[(std::size_t) i] * 2) : m.orders[(std::size_t) i];
    if (! st15)
    {
        std::memcpy (out.data() + 1080, tag.data(), 4);
        if (layout == Layout::DigitalTracker)
        {
            out[1084] = 0x00; out[1085] = 0x40; out[1086] = 0x00; out[1087] = 0x00;
        }
    }

    const std::size_t patBytes = (std::size_t) kRows * (std::size_t) filePatChannels * 4;
    const std::size_t patBase = out.size();
    out.resize (patBase + (std::size_t) storedPatterns * patBytes, 0);
    for (int sp = 0; sp < storedPatterns; ++sp)
    {
        const int p = flt8 ? sp / 2 : sp;
        const int chOff = flt8 ? (sp & 1) * 4 : 0;
        for (int r = 0; r < kRows; ++r)
            for (int c = 0; c < filePatChannels; ++c)
                encodeCell (m.patterns[(std::size_t) p][(std::size_t) (r * m.channels + c + chOff)],
                            out.data() + patBase + (std::size_t) sp * patBytes + ((std::size_t) r * (std::size_t) filePatChannels + (std::size_t) c) * 4);
    }

    for (int s = 0; s < numSamples; ++s)
    {
        const auto& smp = m.samples[(std::size_t) s];
        const auto* b = reinterpret_cast<const std::uint8_t*> (smp.data.data());
        out.insert (out.end(), b, b + smp.data.size());
        out.resize (out.size() + (smp.lengthBytes() - std::min<std::size_t> (smp.lengthBytes(), smp.data.size())), 0);
    }
    if (layout == m.layout && as == SaveAs::Original) out.insert (out.end(), m.trailing.begin(), m.trailing.end());
    return out;
}

//==============================================================================
std::int16_t extendedPeriod (int ft, int note)
{
    ft &= 15;
    note = std::max (0, std::min (59, note));
    const int oct = note / 12, n = note % 12;
    const std::int16_t* t = &kPeriodTable[ft * 37];
    if (oct >= 1 && oct <= 3) return t[(oct - 1) * 12 + n];
    if (oct == 0) return (std::int16_t) (t[n] * 2);
    return (std::int16_t) ((t[24 + n] + 1) / 2);   // octave 4
}

int periodToNoteIndex (int period)
{
    if (period <= 0) return -1;
    int best = 0, bestDiff = 1 << 30;
    for (int i = 0; i < 60; ++i)
    {
        const int d = std::abs (extendedPeriod (0, i) - period);
        if (d < bestDiff) { bestDiff = d; best = i; }
    }
    return best;
}

std::string noteName (int period)
{
    static const char* names[12] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };
    const int n = periodToNoteIndex (period);
    if (n < 0) return "---";
    return std::string (names[n % 12]) + std::to_string (n / 12);
}

int periodToMidi (int period)
{
    if (period <= 0) return -1;
    return 60 + (int) std::lround (12.0 * std::log2 (428.0 / (double) period));
}

} // namespace modport
