# SEMI Family — VCV Rack + 4ms MetaModule

Plugin ailesinin Eurorack serisi. Tek kaynak iki hedefe derlenir: VCV Rack 2 (`plugin.so` / `.vcvplugin`) ve 4ms MetaModule (`SemiFamily.mmplugin`). Paneller Early Cyber ile klasik Eurorack arası bir dilde: koyu panel, çerçeveli OLED ekranlar, eteklikli knob'lar ve somunlu jack'ler.

| modül | ne |
|---|---|
| NEXUSCLIP | Clipper: drive and saturation into CLIP (seven curves), SPECTRAL or LIMIT at a hard ceiling, with a live transfer-curve screen |
| RETICULUM | Eight-node rete noise net with Lissajous scope, node matrix and drunk/walk/random modulation |
| SEEDWALK | Markov ball walking sixteen pinned rete nets with glided matrix interpolation; walk range, hold and per-net randomise |
| EPOCH | Long clock: a macro timeline of energy and silence that conducts the family over the BUS cable |
| TURNAROUND | Stereo phrase-end modulator: 4/8/16/32 bar turns, edit budget, rolls, wall and landing gap |
| FLOATER | A floater case: one clock, a rotating divider, eight Euclidean lanes with chance and bursts, Plaits / Braids / Clouds / Beads modules, a pin matrix and the REEL |
| FLOATDAT | IKEDAR x FLOATER x VORTESSA x JDAT played by the Pinball 2 Markov sequencer; any file is the material, drawn as datamatic scenes |
| SPLICEBUS | FLOATER's REEL as a bus effect: splices and genes on the clock, ORDER <> CHAOS over a split that keeps the bass whole, TURNAROUND's phrase form |
| IKEDAR | ryoz_ikedar_ln: nine 64-cell automata as rhythm, wavetable and FFT mask; sine/click datamatics voice with bit-matrix and barcode screens |
| JDAT | kdr_jdat: a metro writes random (or any file's) data into three 512-sample buffers read as sound; LCD digit field, buffer barcodes, gate outs |
| TST_INGP | tst_ingp: text as a 64 x 32 bit field read by 32 sines, a cellular-automaton cursor rewriting it as it plays; field and rule screens |
| PINDATA | IKEDAR x tst_ingp x JDAT played by the Pinball 2 Markov sequencer, fed by any file; datamatic scenes, lane gate/pitch/velocity outs, BUS |
| IKEDAT | ikedar x jdat x Pinball 2: a datamatic instrument fed by any file, live datamatics visuals as the UI, lane gate/pitch/velocity outs, BUS |
| DATASCREEN | The family's visual hub: one big screen rendering 26 datamatic scenes (PINDATA, FLOATDAT films, case/reel/orbit/attractor/vortex/ghost) from audio, lane/step events, the BUS cable and any file |
| BREAKDNA | Groove-DNA transfer and A/B spectral cross-synthesis of two breaks, clocked |
| GENOTYPE | Break-learned Pinball Markov walk: the walk a break wants, learned from its DNA |
| LOOPGENOME | Drums and neuro bass learned as one loop; one Pinball ball picks the drum slice and the bass articulation |
| BREAKMED | Generative tracker jungle break slicer: weighted chains (pitch, sub, glitch, reverse, drill), Paula sampler, hit log |
| BREAKSLICER | Generative tracker break slicer: onset analysis, Markov pattern with 09xx / Rxy / drills, routing matrix, S950 / S1000 / Paula samplers |
| BREAKBEAT | schwung-breakbeat slicer with BBCut cut procedures (CutProc11, WarpCut, SQPusher1, ChooseCut), loop A/B phrase, retriggers, cut display |
| NEUROSTACK | Neuro bass voice: eight layers on one phase switched by the Pinball Markov walk, a 32-step rhythm, pitch-tracked split over one clean sub |
| SNARESTACK | Four snare layers (built-in or WAV) aligned in time and phase into one hit, re-weighted as crack / body / tail |
| SUBSTRATE | Automatic DnB low end: pitch-tracked mono split, a rebuilt phase-continuous sub, edit repair; low-end map and phase scope |
| DROP II | Drop-2 switch-up generator for DnB: from drop-1 drums + bass and IDENTITY it writes drop 2 (half-time, kick shift, bass lock, flip, call/response) |
| CALL & RESPONSE | Call and response phrase generator (Side Brain C&R Generator / Nordl3, MIT): two mono voices as V/OCT, gate and velocity |
| ACID | TB-3PO x 303: generative acid line (Phazerville TB-3PO) and its 303 voice (schwung-303 / Open303), with the line as V/OCT, gate, accent and slide |
| QUANTA | Microsound particle synth after Curtis Roads (grain, glisson, grainlet, trainlet, pulsar) with an arrangement generator after Ryoji Ikeda's structures |
| ILLUSORIUM | 26 psychoacoustic illusions as one stereo effect (Shepard, Risset, Deutsch, Huggins, Zwicker, Warren, Bregman, Haas, Franssen and more), one at a time |
| VERTEX | Percussion voice after Vertex 1.3 by aorist (aorist.io, Max for Live): two oscillators, noise, Sallen & Key filter (port of sm.sallenkey~ by Surreal Machines), three envelopes and four 8-step rows |
| N.M.S. | Modular rebuild of Tim Exile's NMS (New Modular System): generative factory patches on 8 channel lanes, 24 macros, patterns, WANDER, CHAOS |
| MODPORT | ProTracker .mod player (1-8 channels, pt2-clone replayer): one row per clock or free tempo, per-channel gate and V/OCT, Amiga stereo mix |
| DUBPLATE | Genre arrangement builder (jungle, DnB, UK garage, speed garage): template sections and role lanes as gates, fills, variations, FX events, FamilyBus |
| CONDUIT | Family patch bay: BUS + trigger / CV rows into a pin matrix (amount, curve, delay, probability, length) driving 8 CV / trigger / gate outputs |
| BLACKBOX | Family flight recorder: records the BUS and 4 CV + 4 gate lanes into a ring buffer, replay / recall from a cursor, MIDI file export |
| SETBOOK | Family scene memory: 8 scenes x 8 CV channels with timed morph, curves, X/Y pad, BUS scene / EPOCH cues; in Rack also morphs every SEMI module's knobs |

35 modül. Motorlar plugin repolarından mümkün olduğunca değiştirilmeden kopyalandı. Değişen yerler her motorun `sources.mk` dosyasında yazıyor.

## Derleme

```sh
# VCV Rack (Rack SDK 2.6.x)
make RACK_DIR=/path/to/Rack-SDK -j8 && make dist RACK_DIR=/path/to/Rack-SDK

# MetaModule (arm-none-eabi-gcc 12.2/12.3/15.3 PATH'te)
cmake -S metamodule -B build/mm -G Ninja -DMETAMODULE_SDK_DIR=/path/to/metamodule-plugin-sdk
cmake --build build/mm            # -> dist/metamodule/SemiFamily.mmplugin

# Paneller (SVG + 240 px PNG + layout başlıkları), tek kaynak: tools/panels/*.py
pip install fonttools cairosvg && python3 tools/panelgen.py
```

## BUS kablosu (FamilyBus CV)

Dört kanallı tek bir poly kablo kullanılır, çünkü MetaModule poly kabloyu 4 sesle sınırlıyor. Kanallar: POS, ENERGY, SCENE, EVENT. Ayrıntılar `src/common/FamilyBusCV.hpp` dosyasında. Örnek zincir: EPOCH `BUS·4` → TURNAROUND `BUS·4` → (sonraki kardeş).

## Belgeler

- `docs/METAMODULE_DISPLAY.md`: MetaModule ekran kuralları (yenileme hızı, katman, kırpma, çizgi kalınlığı, bellek bütçesi)
- `docs/PORTING.md`: yeni bir plugin'i seriye taşımanın beş adımı
- `tests/`: motorların çevrimdışı testleri (plugin build'ine dahil değil)

GPL-3.0-or-later. Üçüncü parti kodlar (stmlib, Beads: MIT; artifex/forsitan: GPL-3.0) `engines/thirdparty` altında, kendi lisanslarıyla.

## Yayın öncesi lisans notları

- VERTEX: orijinal M4L cihazının (aorist) lisansı yok, kamuya açık yayından önce izin gerekiyor.
- NMS: Tim Exile'ın Reaktor paketinden; yayından önce izin isteyin (engines/nms/NOTICE.md).
- TST_INGP: gömülü örnek metinler ve SDIF analizleri yazarlarından onay gerektiriyor.
- CALL & RESPONSE: Nordl3 motoru MIT; Side Brain katmanının lisansı belirtilmemiş.
- ACID / FLOATER / RETICULUM: GPL-3.0 kod içeriyor (schwung-303, TB-3PO, artifex), paket GPL-3.0-or-later.
