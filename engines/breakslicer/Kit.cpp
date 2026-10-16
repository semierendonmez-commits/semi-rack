#include "Kit.h"
#include "Dsp.h"

#include <algorithm>

namespace gtbs
{

MachineBuffer buildMachineBuffer (const Sample& s, const MachineParams& p)
{
    MachineBuffer mb;
    mb.machine = p.machine;
    mb.sampleSerial = s.serial;
    const double rate = machineRateFor (p, s.rate);
    mb.rate = rate;
    const float drive = std::pow (10.0f, p.driveDb / 20.0f);
    const bool stereo = ! s.r.empty();

    auto convert = [&] (const std::vector<float>& src) -> std::vector<float>
    {
        std::vector<float> v;
        VintageSamplerEmulation emu;
        switch (p.machine)
        {
            case AkaiS950:
            {
                // the input MF6 (switched-capacitor 6th-order Butterworth) tracks the clock,
                // then the 12-bit converter samples at that clock
                Butterworth<6> aa;
                aa.lowpass (s.rate, std::min (0.45 * rate, 0.45 * s.rate));
                std::vector<float> f (src.size());
                for (size_t i = 0; i < src.size(); ++i) f[i] = aa.process (src[i] * drive);
                v = std::abs (rate - s.rate) < 1.0 ? f : resampleSinc (f, s.rate, rate);
                emu.setMode (VintageSamplerEmulation::EmulationMode::AkaiS950_12Bit);
                break;
            }
            case AkaiS1000:
            {
                std::vector<float> f (src.size());
                for (size_t i = 0; i < src.size(); ++i) f[i] = src[i] * drive;
                v = std::abs (rate - s.rate) < 1.0 ? f : resampleSinc (f, s.rate, rate);
                emu.setMode (VintageSamplerEmulation::EmulationMode::AkaiS1000_16Bit);
                break;
            }
            case AmigaPaula:
            {
                // Amiga digitisers had next to no anti-alias filtering: a mild 2-pole, then the
                // nearest sample at each clock tick (that drop-sample decimation is the grit)
                Butterworth<2> aa;
                aa.lowpass (s.rate, std::min (0.5 * rate, 0.45 * s.rate));
                const double step = s.rate / rate;
                const int n = std::max (1, (int) std::floor ((double) src.size() / step));
                v.resize ((size_t) n);
                size_t j = 0;
                float last = 0.0f;
                for (int i = 0; i < n; ++i)
                {
                    const size_t target = std::min (src.size() - 1, (size_t) std::lround ((double) i * step));
                    while (j <= target) { last = aa.process (src[j] * drive); ++j; }
                    v[(size_t) i] = last;
                }
                emu.setMode (VintageSamplerEmulation::EmulationMode::AmigaPaula8Bit);
                break;
            }
            default:
                v = src;
                if (! same (drive, 1.0f)) for (auto& x : v) x *= drive;
                break;
        }
        // the converter: each stored sample latched once (phase steps by exactly 1)
        double phase = 0.0;
        for (auto& x : v)
        {
            phase += 1.0;
            x = p.machine == Clean16 ? std::clamp (x, -1.0f, 1.0f) : emu.processSample (x, phase, 1.0);
        }
        return v;
    };

    mb.l = convert (s.l);
    if (stereo) mb.r = convert (s.r);
    mb.frames = (int) mb.l.size();
    mb.ratio = (double) mb.frames / std::max (1, s.frames);
    return mb;
}

int Kit::nearestSlice (int frame) const
{
    const auto& st = slices.starts;
    if (st.empty()) return -1;
    auto it = std::lower_bound (st.begin(), st.end(), frame);
    if (it == st.end()) return (int) st.size() - 1;
    const int i = (int) (it - st.begin());
    if (i > 0 && frame - st[(size_t) i - 1] < *it - frame) return i - 1;
    return i;
}

int Kit::sliceAt (int frame) const
{
    const auto& st = slices.starts;
    if (st.empty()) return -1;
    auto it = std::upper_bound (st.begin(), st.end(), frame);
    return std::max (0, (int) (it - st.begin()) - 1);
}

std::unique_ptr<Kit> buildKit (std::shared_ptr<const Sample> s, int sliceMode, int barsSetting, const MachineParams& mp, double hostBpm)
{
    auto k = std::make_unique<Kit>();
    k->sample = s;
    k->sliceMode = sliceMode;
    k->slices = buildSlices (*s, sliceMode);
    k->bars = resolveBars (*s, barsSetting, hostBpm);
    k->mb = buildMachineBuffer (*s, mp);
    k->mbStarts.resize (k->slices.starts.size());
    for (size_t i = 0; i < k->slices.starts.size(); ++i)
        k->mbStarts[i] = std::clamp ((int) std::floor ((double) k->slices.starts[i] * k->mb.ratio), 0, std::max (0, k->mb.frames - 1));
    return k;
}

} // namespace gtbs
