// VERTEX factory patches. Index 0 is the device as it loads in Live
// (the initial values stored in Vertex 1.3); the others are new.
#pragma once
#include "Engine.h"
#include <vector>

namespace vx
{
struct FactoryPreset { const char* name; const char* category; double bpm; Params params; };

inline Params initParams()
{
    Params p;
    for (int i = 0; i < 8; ++i) { p.step[seqPitch][(size_t) i] = 0.5; p.step[seqVel][(size_t) i] = 1.0; p.step[seqMod1][(size_t) i] = 0.0; p.step[seqMod2][(size_t) i] = 0.0; }
    // osc 1 loads with level 0 and Int pitch off, osc 2 with level 1 and Int on (bpatcher args)
    p.osc[0].level = 0.0; p.osc[0].intPitch = false;
    p.osc[1].level = 1.0; p.osc[1].intPitch = true;
    return p;
}

inline const std::vector<FactoryPreset>& factoryList()
{
    static const std::vector<FactoryPreset> f = []
    {
        std::vector<FactoryPreset> v;
        auto seq = [] (Params& p, int s, std::initializer_list<double> vals) { int i = 0; for (double x : vals) p.step[(size_t) s][(size_t) i++] = x; };
        { v.push_back ({ "INIT (Vertex 1.3)", "INIT", 120, initParams() }); }
        {   // a pitched kick: triangle with a fast pitch sweep, accents in the vel row
            Params p = initParams();
            p.osc[0].shape = shTri; p.osc[0].freq = 48.0; p.osc[0].level = 1.0; p.osc[0].env = 420.0; p.osc[0].intPitch = false;
            p.osc[1].shape = shSin; p.osc[1].freq = 52.0; p.osc[1].level = 0.6; p.osc[1].env = 200.0; p.osc[1].intPitch = false;
            p.pDecay = 45.0; p.aDecay = 380.0; p.curve = -0.7;
            seq (p, seqVel, { 1.0, 0.0, 0.0, 0.35, 0.0, 0.0, 0.8, 0.0 });
            p.cutoff = 104.0; p.res = 0.15; p.drive = 0.45; p.order = 1; p.volume = 1.1;
            v.push_back ({ "Thump Kick", "DRUMS", 124, p });
        }
        {   // pitched toms: pitch row drives both oscillators, filter envelope on velocity
            Params p = initParams();
            p.osc[0].shape = shSin; p.osc[0].freq = 92.0; p.osc[0].level = 1.0; p.osc[0].intPitch = true; p.osc[0].env = 160.0;
            p.osc[1].shape = shTri; p.osc[1].freq = 140.0; p.osc[1].level = 0.5; p.osc[1].env = 90.0;
            p.pitchRange = 0.3; p.pDecay = 110.0; p.aDecay = 260.0;
            seq (p, seqPitch, { 0.8, 0.6, 0.45, 0.8, 0.3, 0.6, 0.2, 0.45 });
            seq (p, seqVel, { 1.0, 0.6, 0.0, 0.9, 0.5, 0.0, 0.8, 0.4 });
            p.cutoff = 82.0; p.res = 0.55; p.fEnv = 45.0; p.fDecay = 140.0; p.fEnvSrc = srcVel;
            v.push_back ({ "Acid Toms", "DRUMS", 118, p });
        }
        {   // noise snare on the back beats with a short tone body
            Params p = initParams();
            p.osc[0].shape = shTri; p.osc[0].freq = 185.0; p.osc[0].level = 0.7; p.osc[0].env = 140.0;
            p.osc[1].level = 0.0;
            p.noise = 0.95; p.noiseColour = 0; p.pDecay = 35.0; p.aDecay = 140.0; p.curve = -0.75;
            seq (p, seqVel, { 0.0, 0.0, 1.0, 0.0, 0.0, 0.25, 1.0, 0.15 });
            p.filterType = 1; p.cutoff = 96.0; p.res = 0.35; p.fEnv = 25.0; p.fDecay = 60.0; p.drive = 0.3; p.volume = 2.0;
            v.push_back ({ "Noise Snare", "DRUMS", 120, p });
        }
        {   // metal: squares in sync, FM depth from MOD 1, band pass
            Params p = initParams();
            p.osc[0].shape = shSqu; p.osc[0].freq = 420.0; p.osc[0].level = 0.6;
            p.osc[1].shape = shSqu; p.osc[1].freq = 1130.0; p.osc[1].level = 0.8; p.osc[1].intPitch = false;
            p.fm = 420.0; p.fmSrc = srcMod1; p.sync = true;
            seq (p, seqMod1, { 0.2, 0.9, 0.5, 0.1, 1.0, 0.3, 0.7, 0.4 });
            seq (p, seqVel, { 1.0, 0.4, 0.7, 0.3, 1.0, 0.5, 0.6, 0.9 });
            p.noise = 0.15; p.aDecay = 110.0; p.curve = -0.85; p.volume = 1.4;
            p.filterType = 1; p.cutoff = 98.0; p.res = 0.72; p.slope = 0;
            v.push_back ({ "Metal Clank", "PERC", 126, p });
        }
        {   // bass line: quantised saw, glide, resonant low pass
            Params p = initParams();
            p.osc[0].shape = shSaw; p.osc[0].freq = 62.0; p.osc[0].level = 1.0; p.osc[0].intPitch = true; p.osc[0].quantize = true;
            p.osc[1].shape = shSqu; p.osc[1].freq = 62.0; p.osc[1].level = 0.5; p.osc[1].quantize = true; p.osc[1].fine = -12.0;
            p.root = 9; p.scale = 1; p.pitchRange = 0.25; p.glide = 0.06; p.glideSrc = srcMod1;
            seq (p, seqPitch, { 0.5, 0.5, 0.75, 0.5, 0.62, 0.5, 0.88, 0.4 });
            seq (p, seqMod1, { 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 });
            seq (p, seqVel, { 1.0, 0.6, 0.9, 0.0, 1.0, 0.7, 0.9, 0.5 });
            p.aDecay = 320.0; p.volume = 0.8; p.cutoff = 58.0; p.res = 0.78; p.fEnv = 62.0; p.fDecay = 180.0; p.drive = 0.35; p.order = 1;
            v.push_back ({ "Bubble Bass", "BASS", 122, p });
        }
        {   // bells: sine pair, FM from MOD 2, long curved decays at 8n
            Params p = initParams();
            p.clock = 2;
            p.osc[0].shape = shSin; p.osc[0].freq = 330.0; p.osc[0].level = 0.6; p.osc[0].intPitch = true; p.osc[0].quantize = true;
            p.osc[1].shape = shSin; p.osc[1].freq = 880.0; p.osc[1].level = 0.8; p.osc[1].quantize = true;
            p.scale = 8; p.root = 2; p.pitchRange = 0.35;
            p.fm = 260.0; p.fmSrc = srcMod2;
            seq (p, seqPitch, { 0.3, 0.7, 0.5, 0.9, 0.6, 0.2, 0.8, 0.4 });
            seq (p, seqMod2, { 0.2, 0.6, 1.0, 0.3, 0.8, 0.1, 0.5, 0.9 });
            seq (p, seqVel, { 1.0, 0.5, 0.8, 0.4, 0.9, 0.3, 0.7, 0.6 });
            p.length = { 7, 8, 8, 5 };
            p.aDecay = 1200.0; p.curve = -0.85; p.cutoff = 122.0; p.res = 0.2; p.volume = 0.9;
            v.push_back ({ "FM Bells", "TONAL", 96, p });
        }
        {   // industrial: driven 24 dB low pass with osc 2 crushing the cutoff, odd lengths
            Params p = initParams();
            p.osc[0].shape = shSqu; p.osc[0].freq = 75.0; p.osc[0].level = 0.9; p.osc[0].intPitch = true; p.osc[0].env = 300.0;
            p.osc[1].shape = shSaw; p.osc[1].freq = 310.0; p.osc[1].level = 0.4;
            p.noise = 0.3; p.noiseSrc = srcMod2; p.pDecay = 60.0;
            p.length = { 5, 7, 3, 8 }; p.rate = { 3, 3, 3, 4 };
            seq (p, seqPitch, { 0.4, 0.7, 0.5, 0.2, 0.9, 0.5, 0.5, 0.5 });
            seq (p, seqVel, { 1.0, 0.5, 0.8, 0.0, 0.9, 0.3, 0.7, 0.5 });
            seq (p, seqMod2, { 0.0, 0.8, 0.2, 1.0, 0.0, 0.5, 0.9, 0.1 });
            p.filterType = 0; p.slope = 1; p.cutoff = 72.0; p.res = 0.45; p.fMod = 40.0; p.fModFrom = 2; p.fModSrc = srcMod1;
            seq (p, seqMod1, { 0.0, 1.0, 0.3, 0.0, 0.7, 0.0, 0.5, 1.0 });
            p.drive = 0.9; p.order = 0; p.aDecay = 200.0; p.volume = 1.4;
            v.push_back ({ "Industrial Loop", "PERC", 128, p });
        }
        {   // rain: 32nd clock, random-looking velocities, pentatonic sines, band pass, swing
            Params p = initParams();
            p.clock = 4; p.swing = 0.25;
            p.osc[0].shape = shSin; p.osc[0].freq = 520.0; p.osc[0].level = 0.8; p.osc[0].intPitch = true; p.osc[0].quantize = true;
            p.osc[1].shape = shTri; p.osc[1].freq = 1040.0; p.osc[1].level = 0.4; p.osc[1].quantize = true;
            p.scale = 9; p.root = 4; p.pitchRange = 0.4;
            p.length = { 7, 5, 8, 8 };
            seq (p, seqPitch, { 0.1, 0.9, 0.4, 0.6, 0.2, 0.8, 0.5, 0.3 });
            seq (p, seqVel, { 1.0, 0.2, 0.6, 0.1, 0.8, 0.3, 0.5, 0.2 });
            p.aDecay = 90.0; p.curve = -0.9; p.filterType = 1; p.cutoff = 104.0; p.res = 0.5; p.volume = 1.8;
            v.push_back ({ "Clock Rain", "TONAL", 110, p });
        }
        {   // drone: slow attack, long decay, two saws beating, cutoff from MOD 1
            Params p = initParams();
            p.clock = 1;
            p.osc[0].shape = shSaw; p.osc[0].freq = 73.4; p.osc[0].level = 0.7; p.osc[0].intPitch = false;
            p.osc[1].shape = shSaw; p.osc[1].freq = 74.1; p.osc[1].level = 0.7; p.osc[1].intPitch = false;
            p.attack = 600.0; p.aDecay = 2000.0; p.curve = -0.3;
            p.cutoff = 66.0; p.cutoffSrc = srcNone; p.res = 0.62; p.fEnv = 40.0; p.fEnvSrc = srcMod1; p.fDecay = 1500.0;
            seq (p, seqMod1, { 0.4, 1.0, 0.6, 0.9 });
            p.length = { 8, 8, 4, 8 };
            p.fMod = 6.0; p.fModFrom = 1; p.drive = 0.25; p.order = 1;
            v.push_back ({ "Wobble Drone", "TONAL", 90, p });
        }
        {   // polymeter: four row lengths, sync on, tri / square
            Params p = initParams();
            p.osc[0].shape = shTri; p.osc[0].freq = 110.0; p.osc[0].level = 0.8; p.osc[0].intPitch = true; p.osc[0].env = 120.0;
            p.osc[1].shape = shSqu; p.osc[1].freq = 260.0; p.osc[1].level = 0.6; p.sync = true;
            p.length = { 7, 5, 3, 8 }; p.pitchRange = 0.35;
            seq (p, seqPitch, { 0.2, 0.7, 0.4, 0.9, 0.3, 0.6, 0.8, 0.5 });
            seq (p, seqVel, { 1.0, 0.4, 0.7, 0.2, 0.9, 0.5, 0.5, 0.5 });
            seq (p, seqMod1, { 0.1, 0.9, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5 });
            p.pDecay = 80.0; p.pDecaySrc = srcMod1; p.aDecay = 200.0; p.aDecaySrc = srcMod1;
            p.cutoff = 92.0; p.res = 0.4; p.fEnv = 30.0;
            v.push_back ({ "Polymeter Perc", "PERC", 120, p });
        }
        {   // played from MIDI: EXT clock, note pitch, MIDI velocity
            Params p = initParams();
            p.clock = 7;
            p.osc[0].shape = shSaw; p.osc[0].freq = 130.8; p.osc[0].level = 0.8; p.osc[0].extPitch = true; p.osc[0].intPitch = false;
            p.osc[1].shape = shSqu; p.osc[1].freq = 130.8; p.osc[1].level = 0.6; p.osc[1].extPitch = true; p.osc[1].intPitch = false; p.osc[1].fine = 0.08;
            p.volumeSrc = srcMidi; p.fEnvSrc = srcMidi; p.aDecay = 450.0; p.cutoff = 70.0; p.res = 0.5; p.fEnv = 55.0; p.fDecay = 260.0;
            v.push_back ({ "MIDI Played (EXT)", "PLAY", 120, p });
        }
        return v;
    }();
    return f;
}
inline int numFactory() { return (int) factoryList().size(); }
inline const FactoryPreset& factory (int i) { return factoryList()[(size_t) std::clamp (i, 0, numFactory() - 1)]; }
} // namespace vx
