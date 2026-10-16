// Offline test of the FLOATDAT port (engines/floatdat + engines/floater). Not part of the plugin build.
//   sh tests/floater_engine.sh builds the shared objects; then see tests/floatdat_engine.sh.
// Checks: an example text and a binary blob load (bits, symbols, type, entropy); every factory preset plays
// finite, bounded and heard on its own clock and on a host clock; the pinball fires lanes and they come out as
// gates; the read head moves (data step / scan run); FLT DATA PATCH writes drawn lanes from the bits; IKD
// randomise and the pinball functions stay finite.
#include "floatdat/Data.h"
#include "floatdat/Instrument.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

static int checks = 0, failures = 0;
#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        ++checks;                                                           \
        if (! (cond)) {                                                     \
            ++failures;                                                     \
            std::printf ("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond);     \
            std::printf (__VA_ARGS__);                                      \
            std::printf ("\n");                                             \
        }                                                                   \
    } while (0)

struct Stats { double rms = 0; float peak = 0; bool finite = true; int gates = 0; };

static Stats run (fdat::Instrument& in, double sr, double seconds, bool host, double bpm = 120.0)
{
    Stats s;
    float l[64], r[64];
    double ppq = 0.0, sum = 0.0;
    const int64_t total = (int64_t) (seconds * sr);
    for (int64_t done = 0; done < total; done += 64)
    {
        fdat::Transport t;
        t.host = host; t.playing = host; t.bpm = bpm; t.ppq = ppq;
        in.process (l, r, 64, t);
        ppq += 64 * bpm / 60.0 / sr;
        s.gates += in.numGates();
        for (int i = 0; i < 64; ++i)
        {
            if (! std::isfinite (l[i]) || ! std::isfinite (r[i])) s.finite = false;
            s.peak = std::max (s.peak, std::max (std::abs (l[i]), std::abs (r[i])));
            sum += (double) l[i] * l[i] + (double) r[i] * r[i];
        }
    }
    s.rms = std::sqrt (sum / (2.0 * (double) total));
    return s;
}

int main()
{
    const double sr = 48000.0;
    auto text = std::make_unique<fdat_pdata::DataBlob>();
    CHECK (fdat_pdata::fromText (*text, fdat_pdata::exampleText (1), fdat_pdata::exampleName (1)), "text");
    CHECK (text->nbits > 1000 && text->used == fdat_pdata::ReadAs::Text && text->typeName == "TEXT", "text blob: %llu bits, %s", (unsigned long long) text->nbits, text->typeName.c_str());
    std::vector<uint8_t> bin (20000);
    uint32_t x = 1;
    for (auto& b : bin) { x = x * 1664525u + 1013904223u; b = (uint8_t) (x >> 24); }
    bin[0] = 0x7F; bin[1] = 'E'; bin[2] = 'L'; bin[3] = 'F';
    auto blob = std::make_unique<fdat_pdata::DataBlob>();
    CHECK (fdat_pdata::fromBytes (*blob, bin, "a.out", "/tmp/a.out", 0, fdat_pdata::ReadAs::Auto), "bytes");
    CHECK (blob->typeName == "ELF EXECUTABLE" && blob->nbits == bin.size() * 8 && blob->entropy > 7.5, "binary: %s, entropy %.2f", blob->typeName.c_str(), blob->entropy);
    std::vector<float> audio (48000);
    for (size_t i = 0; i < audio.size(); ++i) audio[i] = std::sin (i * 0.05f);
    std::vector<uint8_t> riff { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E' };
    auto wav = std::make_unique<fdat_pdata::DataBlob>();
    fdat_pdata::fromBytes (*wav, riff, "s.wav", "", 0, fdat_pdata::ReadAs::Auto, &audio, 48000.0, 1);
    CHECK (wav->used == fdat_pdata::ReadAs::Audio && wav->nbits == audio.size(), "audio blob");

    static fdat::Instrument in;
    in.prepare (sr);
    in.setBlob (text.get());
    for (int p = 0; p < (int) fdat::factoryNames().size(); ++p)
    {
        in.loadFactory (p);
        in.setBlob (p % 2 ? blob.get() : text.get());
        const Stats a = run (in, sr, 4.0, false);
        const Stats b = run (in, sr, 4.0, true, 140.0);
        std::printf ("%-16s own: rms %.3f peak %.2f gates %d | host: rms %.3f gates %d\n", fdat::factoryNames()[(size_t) p].c_str(), a.rms, a.peak, a.gates, b.rms, b.gates);
        CHECK (a.finite && b.finite && a.peak <= 1.0f && b.peak <= 1.0f, "preset %d not finite / bounded", p);
        CHECK (a.rms > 0.003 && b.rms > 0.003, "preset %d silent (%.4f %.4f)", p, a.rms, b.rms);
        CHECK (a.gates > 0, "preset %d: no lane gates", p);
    }

    // the head moves with SCAN STEP
    in.loadFactory (0);
    in.S.dataScan = 1;
    in.setBlob (blob.get());
    run (in, sr, 0.1, false);
    const uint64_t h0 = in.head;
    run (in, sr, 2.0, false);
    CHECK (in.head != h0, "the head did not move");

    // FLT DATA PATCH: drawn lanes from the bits
    in.fltDataPatch (true, true, true);
    int drawn = 0;
    for (int l = 0; l < flt::kLanes; ++l) drawn += in.fltCase.get (flt::ids::draw (l)) > 0.5f && in.fltCase.pattern (l) != 0;
    CHECK (drawn == flt::kLanes, "data patch drew %d lanes", drawn);

    // the functions
    in.ikdRandomise (fdat::Instrument::IkdRandom::All);
    in.pinballFunction (fdat::Instrument::Fn::MatrixRandom);
    in.pinballFunction (fdat::Instrument::Fn::RandomizeSteps);
    in.requestFltNewPatch();
    const Stats z = run (in, sr, 3.0, false);
    CHECK (z.finite && z.peak <= 1.0f, "after the functions");
    in.fireLaneNow (2, 120);
    float l[64], r[64];
    in.process (l, r, 64, fdat::Transport {});
    CHECK (in.numGates() >= 1, "a fired lane gives a gate");

    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
