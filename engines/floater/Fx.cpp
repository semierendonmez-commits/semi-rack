#include "Fx.h"

namespace flt
{
using namespace dsp;

//==============================================================================
void Machine::Biquad::lowpass (float fs, float fc, float q)
{
    const float w0 = twoPi * clamp (fc, 20.0f, fs * 0.45f) / fs, cw = std::cos (w0), al = std::sin (w0) / (2.0f * q);
    const float a0 = 1.0f + al;
    b0 = (1.0f - cw) * 0.5f / a0; b1 = (1.0f - cw) / a0; b2 = b0;
    a1 = -2.0f * cw / a0; a2 = (1.0f - al) / a0;
}

void Machine::set (int m, float rate01)
{
    if (m == last && std::abs (rate01 - lastRate) < 1.0e-4f) return;
    if (m != last) for (auto* f : { &fL1, &fR1, &fL2, &fR2 }) f->reset();
    mode = m; last = m; lastRate = rate01;
    switch (m)
    {
        case 1:   // S950: its clock 7.5..48 kHz, the switched-capacitor filter at the clock's Nyquist
            clockHz = expMap (rate01, 7500.0f, 48000.0f);
            fL1.lowpass (sr, clockHz * 0.45f); fR1 = fL1; fL2 = fL1; fR2 = fL1;
            break;
        case 2:   // S1000: 44.1 kHz, the interpolator rolls the top off from 16 kHz
            clockHz = 44100.0f;
            fL1.lowpass (sr, 16000.0f); fR1 = fL1; fL2.lowpass (sr, 18000.0f); fR2 = fL2;
            break;
        case 3:   // Paula: 3.5..28.8 kHz, the A500's output filter at 4.4 kHz
            clockHz = expMap (rate01, 3500.0f, 28800.0f);
            fL1.lowpass (sr, 4421.0f); fR1 = fL1; fL2.lowpass (sr, std::min (12000.0f, clockHz * 0.5f)); fR2 = fL2;
            break;
        default: break;
    }
}

void Machine::process (float& l, float& r)
{
    if (mode == 0) return;
    phase += (double) clockHz / (double) sr;
    if (phase >= 1.0)
    {
        phase -= std::floor (phase);
        // the converter: a new sample is taken on the machine's clock and held (zero-order hold)
        auto q12 = [] (float x)
        {
            x = clamp (x, -1.0f, 1.0f);
            const float s = x < 0.0f ? -1.0f : 1.0f;
            const float c = std::pow (std::abs (x), 0.8f);
            return s * std::pow (std::trunc (c * 2048.0f) / 2048.0f, 1.25f);
        };
        auto q8 = [] (float x) { return std::floor (clamp (x, -1.0f, 1.0f) * 127.0f) / 127.0f; };
        auto q16 = [] (float x) { return std::round (clamp (x, -1.0f, 1.0f) * 32767.0f) / 32767.0f; };
        switch (mode)
        {
            case 1:  holdL = q12 (fL2.tick (l)); holdR = q12 (fR2.tick (r)); break;
            case 2:  holdL = q16 (l); holdR = q16 (r); break;
            default: holdL = q8 (l); holdR = q8 (r); break;
        }
    }
    else if (mode == 1) { fL2.tick (l); fR2.tick (r); }
    switch (mode)
    {
        case 1:  l = fL1.tick (holdL); r = fR1.tick (holdR); break;
        case 2:  l = fL2.tick (fL1.tick (holdL)); r = fR2.tick (fR1.tick (holdR)); break;
        default: l = fL1.tick (fL2.tick (holdL)); r = fR1.tick (fR2.tick (holdR)); break;
    }
}

//==============================================================================
void ZigZag::prepare (float sampleRate)
{
    sr = sampleRate;
    len = (int) (sr * 4.5f);
    bufL.assign ((size_t) len, 0.0f);
    bufR.assign ((size_t) len, 0.0f);
    timeK = decayCoef (0.12f, sr);
    timeKFast = decayCoef (0.005f, sr);
    breakMixK = decayCoef (0.003f, sr);
    toneL.set (5500.0f, sr); toneR.set (5500.0f, sr);
    lowL.set (90.0f, sr);    lowR.set (90.0f, sr);
    ksL.set (9000.0f, sr);   ksR.set (9000.0f, sr);
    machine.prepare (sr);
    reset();
}

void ZigZag::reset()
{
    std::fill (bufL.begin(), bufL.end(), 0.0f);
    std::fill (bufR.begin(), bufR.end(), 0.0f);
    w = 0; breakLeft = 0; breakMix = 0.0f; time = timeTarget;
    machine.reset();
}

float ZigZag::read (const std::vector<float>& b, float pos) const
{
    while (pos < 0.0f) pos += (float) len;
    while (pos >= (float) len) pos -= (float) len;
    const int i0 = (int) pos;
    const float f = pos - (float) i0;
    const float a = b[(size_t) (i0 % len)], c = b[(size_t) ((i0 + 1) % len)];
    return a + (c - a) * f;
}

void ZigZag::breakNow (int slice, int duration)
{
    sliceLen = std::max (64, std::min (slice, len / 4));
    sliceStart = w - (int) time - sliceLen;
    breakLeft = breakTotal = std::max (sliceLen, duration);
    k = 0;
    backwards = false;
    rollAge = 0.0;
    rollLen = (double) sliceLen;
    tapeRead = 0.0;
}

// one sample of the break, in its style
void ZigZag::breakRead (float& l, float& r)
{
    const int fade = std::max (8, (int) (0.002f * sr));
    const float u = 1.0f - (float) breakLeft / (float) std::max (1, breakTotal);   // 0 .. 1 through the break
    switch (style)
    {
        case roll:
        {
            // TURNAROUND's roll: repeats from the slice down to a quarter of it, climbing 5 semitones, louder
            const float ratio = std::pow (2.0f, 5.0f * u / 12.0f);
            const float env = (float) std::min ({ 1.0, rollAge / (double) fade, (rollLen - rollAge) / (double) fade });
            const float g = (0.55f + 0.45f * u) * std::max (0.0f, env);
            l = read (bufL, (float) sliceStart + (float) rollAge * ratio) * g;
            r = read (bufR, (float) sliceStart + (float) rollAge * ratio) * g;
            rollAge += 1.0;
            if (rollAge >= rollLen) { rollAge = 0.0; rollLen = std::max (32.0, (double) sliceLen * std::pow (0.25, (double) u)); }
            break;
        }
        case reverse:
        {
            const float pos = (float) (sliceStart + sliceLen) - (float) k;
            const float win = std::min (1.0f, (float) breakLeft / (float) fade);
            l = read (bufL, pos) * win;
            r = read (bufR, pos) * win;
            break;
        }
        case tapeStop:
        {
            // the read head slows to a stop, (1 - u)^1.6
            const double rate = std::pow (1.0 - (double) u, 1.6);
            const float win = std::min (1.0f, (float) breakLeft / (float) fade);
            l = read (bufL, (float) sliceStart + (float) tapeRead) * win;
            r = read (bufR, (float) sliceStart + (float) tapeRead) * win;
            tapeRead += rate;
            break;
        }
        default:
        {
            const int kk = k % sliceLen;
            if (kk == 0 && k > 0) backwards = ! backwards;          // zig, then zag
            const int idx = backwards ? sliceLen - 1 - kk : kk;
            const float win = std::min (1.0f, std::min ((float) kk / (float) fade, (float) (sliceLen - kk) / (float) fade));
            l = read (bufL, (float) (sliceStart + idx)) * win;
            r = read (bufR, (float) (sliceStart + idx)) * win;
            break;
        }
    }
}

void ZigZag::process (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    for (int i = 0; i < n; ++i)
    {
        // tuned, the delay time jumps (5 ms glide) as the d0's does on stepped CV; on the clock it glides
        time = timeTarget + (pitched ? timeKFast : timeK) * (time - timeTarget);
        const float t = clamp (time, pitched ? 8.0f : 16.0f, (float) len - 4.0f);
        float dl = read (bufL, (float) w - t);
        float dr = read (bufR, (float) w - t);
        machine.process (dl, dr);

        float sl = dl, sr2 = dr;
        const bool on = breakLeft > 0;
        breakMix = (on ? 1.0f : 0.0f) + breakMixK * (breakMix - (on ? 1.0f : 0.0f));
        if (breakMix > 1.0e-4f)
        {
            float bl = 0.0f, br = 0.0f;
            if (on) { breakRead (bl, br); ++k; --breakLeft; }
            sl = lerp (dl, bl, breakMix);
            sr2 = lerp (dr, br, breakMix);
        }

        if (pitched)
        {
            // the d0 in Karplus-Strong: each channel feeds itself, a one-pole in the loop, the strike from the input
            const float fbL = soft (lowL.hp (ksL.lp (sl * std::min (0.995f, feedback))));
            const float fbR = soft (lowR.hp (ksR.lp (sr2 * std::min (0.995f, feedback))));
            bufL[(size_t) w] = inL[i] + fbL;
            bufR[(size_t) w] = inR[i] + fbR;
        }
        else
        {
            // ping-pong: the left input and the right's echo go left, the left's echo goes right
            const float fbL = soft (lowL.hp (toneL.lp (sr2 * feedback)));
            const float fbR = soft (lowR.hp (toneR.lp (sl * feedback)));
            bufL[(size_t) w] = inL[i] + 0.5f * inR[i] + fbL;
            bufR[(size_t) w] = 0.5f * inR[i] + fbR;
        }
        w = (w + 1) % len;
        outL[i] = sl;
        outR[i] = sr2;
    }
}

//==============================================================================
void Smr::prepare (float sampleRate)
{
    sr = sampleRate;
    reset();
}

void Smr::reset()
{
    for (auto& b : bands) { b.a.reset(); b.b.reset(); }
    for (int i = 0; i < kBands; ++i) { current[(size_t) i] = target[(size_t) i] > 0.0f ? target[(size_t) i] : 200.0f * (float) (i + 1); }
    levels.fill (0.0f);
    counter = 0;
}

void Smr::set (float q01, float glide01)
{
    q = expMap (clamp (q01, 0.0f, 1.0f), 4.0f, 400.0f);
    glideK = decayCoef (expMap (clamp (glide01, 0.0f, 1.0f), 0.002f, 0.8f), sr / 16.0f);
}

void Smr::process (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    // two band passes in a row per channel, each normalised to unity at its centre: a narrow, ringing band
    const float makeup = 1.0f + 0.15f * std::sqrt (q);
    for (int i = 0; i < n; ++i)
    {
        if ((counter++ & 15) == 0)
            for (int k = 0; k < kBands; ++k)
            {
                auto& c = current[(size_t) k];
                c = target[(size_t) k] + glideK * (c - target[(size_t) k]);
                bands[(size_t) k].a.set (c, q, sr);
                bands[(size_t) k].b.set (c, q * 0.5f, sr);
            }
        float l = 0.0f, r = 0.0f;
        for (int k = 0; k < kBands; ++k)
        {
            auto& b = bands[(size_t) k];
            const float x = (k & 1) ? inR[i] : inL[i];              // odds left, evens right (counting from 1)
            float bp, hp;
            b.a.tick (x, bp, hp);
            const float y1 = bp * b.a.k;
            float bp2, hp2;
            b.b.tick (y1, bp2, hp2);
            const float y = bp2 * b.b.k * makeup;
            levels[(size_t) k] = std::max (std::abs (y), levels[(size_t) k] * 0.9995f);
            if (k & 1) r += y; else l += y;
        }
        outL[i] = soft (l);
        outR[i] = soft (r);
    }
}

//==============================================================================
void Scatter::prepare (float sampleRate)
{
    sr = sampleRate;
    len = (int) (sr * 2.5f);
    bufL.assign ((size_t) len, 0.0f);
    bufR.assign ((size_t) len, 0.0f);
    reset();
}

void Scatter::reset()
{
    std::fill (bufL.begin(), bufL.end(), 0.0f);
    std::fill (bufR.begin(), bufR.end(), 0.0f);
    w = 0;
    for (auto& g : grains) g.left = 0;
}

void Scatter::fire (float size, float density, float pitch, float spread, float position, float texture, Rng& rng)
{
    static const float steps[] { 0.5f, 2.0f, 1.5f, 0.75f, 0.25f, 3.0f };
    const int count = 2 + (int) std::lround (density * 14.0f);
    const int length = (int) (expMap (size, 0.012f, 0.32f) * sr);
    const int window = (int) ((float) length * (0.6f + 2.4f * (1.0f - density)) * (float) count * 0.35f);
    int placed = 0;
    for (auto& g : grains)
    {
        if (placed >= count) break;
        if (g.left > 0) continue;
        g.wait = (int) (rng.next() * (float) window);
        g.length = g.left = length;
        float st = 1.0f;
        if (rng.next() < pitch) st = steps[(int) (rng.next() * 5.999f * std::min (1.0f, 0.4f + pitch))];
        if (rng.next() < pitch * 0.3f) st = -st;
        g.step = st;
        // reads from up to a second ago; a grain played forward starts far enough back to never pass the write head
        const float back = (float) length * std::max (1.0f, std::abs (st)) + 64.0f + rng.next() * sr * 0.9f * (0.1f + 1.7f * position);   // POSITION: how far back
        g.pos = (float) w - back + (st < 0.0f ? (float) length * std::abs (st) : 0.0f) + (float) g.wait;
        const float pan = 0.5f + (rng.next() - 0.5f) * spread;
        g.panL = std::cos (pan * pi * 0.5f) * 1.41f;
        g.panR = std::sin (pan * pi * 0.5f) * 1.41f;
        g.shape = 0.35f + 2.4f * texture;                   // TEXTURE: soft wide grains .. narrow clicks
        ++placed;
    }
}

void Scatter::process (const float* inL, const float* inR, float* outL, float* outR, int n, float feed)
{
    for (int i = 0; i < n; ++i)
    {
        float yl = 0.0f, yr = 0.0f;
        for (auto& g : grains)
        {
            if (g.left <= 0) continue;
            if (g.wait > 0) { --g.wait; continue; }
            const float ph = 1.0f - (float) g.left / (float) g.length;
            const float win = std::pow (0.5f - 0.5f * std::cos (twoPi * ph), g.shape);
            float p = g.pos;
            while (p < 0.0f) p += (float) len;
            while (p >= (float) len) p -= (float) len;
            const int i0 = (int) p;
            const float f = p - (float) i0;
            const int i1 = (i0 + 1) % len;
            const float l = bufL[(size_t) i0] + (bufL[(size_t) i1] - bufL[(size_t) i0]) * f;
            const float r = bufR[(size_t) i0] + (bufR[(size_t) i1] - bufR[(size_t) i0]) * f;
            const float mono = 0.5f * (l + r);
            yl += mono * win * g.panL * 0.6f;
            yr += mono * win * g.panR * 0.6f;
            g.pos += g.step;
            --g.left;
        }
        if (! frozen)
        {
            bufL[(size_t) w] = inL[i] + soft (yl * feed);
            bufR[(size_t) w] = inR[i] + soft (yr * feed);
            w = (w + 1) % len;
        }
        outL[i] = yl;
        outR[i] = yr;
    }
}

//==============================================================================
void Space::prepare (float sampleRate)
{
    sr = sampleRate;
    for (auto& l : lines) l.assign (kLen, 0.0f);
    preLen = (int) (0.25f * sr) + 4;
    pre.assign ((size_t) preLen * 2, 0.0f);
    size.setTime (0.08f, sr); decay.setTime (0.08f, sr); damp.setTime (0.08f, sr);
    reset();
}

void Space::reset()
{
    for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
    std::fill (pre.begin(), pre.end(), 0.0f);
    z.fill (0.0f);
    w = pw = 0;
    size.v = size.target; decay.v = decay.target; damp.v = damp.target;
}

void Space::set (float s, float d, float dm)
{
    size.target = clamp (s, 0.0f, 1.0f);
    decay.target = clamp (d, 0.0f, 1.0f);
    damp.target = clamp (dm, 0.0f, 1.0f);
}

void Space::process (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    static const float baseMs[kLines] { 31.7f, 37.1f, 41.9f, 47.3f, 53.3f, 59.9f, 67.1f, 73.7f };
    std::array<float, kLines> lenS {}, g {};
    float dampA = 0.0f;
    const int pdelay = (int) (0.012f * sr);
    for (int i = 0; i < n; ++i)
    {
        if ((i & 15) == 0)
        {
            const float sz = size.v, dc = decay.v, dm = damp.v;
            const float scale = expMap (sz, 0.25f, 2.2f);
            const float t60 = expMap (dc, 0.3f, 14.0f);
            for (int l = 0; l < kLines; ++l)
            {
                lenS[(size_t) l] = std::min ((float) kLen - 8.0f, baseMs[l] * 0.001f * sr * scale);
                g[(size_t) l] = infinite ? 0.99993f : std::pow (0.001f, lenS[(size_t) l] / (t60 * sr));
            }
            dampA = std::exp (-twoPi * expMap (1.0f - dm, 1200.0f, 16000.0f) / sr);
        }
        size.tick(); decay.tick(); damp.tick();

        pre[(size_t) pw] = inL[i];
        pre[(size_t) (pw + preLen)] = inR[i];
        const int rp = (pw - pdelay + preLen) % preLen;
        inGain += ((infinite ? 0.0f : 1.0f) - inGain) * 0.0005f;       // INFINITE: nothing new comes in
        const float il = pre[(size_t) rp] * inGain, ir = pre[(size_t) (rp + preLen)] * inGain;
        pw = (pw + 1) % preLen;

        std::array<float, kLines> o {};
        for (int l = 0; l < kLines; ++l)
        {
            modPh[(size_t) l] += (0.11 + 0.07 * l) / sr;
            if (modPh[(size_t) l] >= 1.0) modPh[(size_t) l] -= 1.0;
            float p = (float) w - lenS[(size_t) l] - (1.0f + std::sin (twoPi * (float) modPh[(size_t) l])) * 0.0004f * sr;
            while (p < 0.0f) p += (float) kLen;
            const int i0 = (int) p;
            const float f = p - (float) i0;
            const auto& b = lines[(size_t) l];
            const float v = b[(size_t) (i0 & (kLen - 1))] + (b[(size_t) ((i0 + 1) & (kLen - 1))] - b[(size_t) (i0 & (kLen - 1))]) * f;
            z[(size_t) l] = v + dampA * (z[(size_t) l] - v);
            o[(size_t) l] = z[(size_t) l] * g[(size_t) l];
        }
        // 8-point Hadamard
        std::array<float, kLines> h = o;
        for (int s = 1; s < kLines; s <<= 1)
            for (int a = 0; a < kLines; a += s << 1)
                for (int b = a; b < a + s; ++b)
                {
                    const float x = h[(size_t) b], y = h[(size_t) (b + s)];
                    h[(size_t) b] = x + y; h[(size_t) (b + s)] = x - y;
                }
        const float norm = 0.35355339f;
        for (int l = 0; l < kLines; ++l)
            lines[(size_t) l][(size_t) w] = h[(size_t) l] * norm + ((l & 1) ? ir : il) * 0.5f;
        w = (w + 1) & (kLen - 1);
        outL[i] = (o[0] - o[2] + o[4] - o[6]) * 0.5f;
        outR[i] = (o[1] - o[3] + o[5] - o[7]) * 0.5f;
    }
}

} // namespace flt
