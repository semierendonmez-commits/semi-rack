35 modules for the **4ms MetaModule** (also built for VCV Rack 2 from the same code): break slicers, neuro bass and low end, noise nets, datamatic instruments, a microsound synth, psychoacoustic effects, and a set of structure tools that conduct the rest over a shared BUS cable. Most modules have live screens (scopes, tracker logs, matrices, break waveforms, datamatic scenes) designed to stay readable and light on the MetaModule's display.

Every module has its own page with a screenshot, what it does, a first patch, and its full list of controls, inputs and outputs: see the [module gallery](https://github.com/semierendonmez-commits/semi-rack#modules).

## Download

Download `SemiFamily-v2.1.2.mmplugin` below, copy it to an SD card or USB stick and load it on the MetaModule under *Settings → Plugins*.

## The modules

- **Breaks and drums:** BREAKMED, BREAKSLICER, BREAKBEAT (BBCut cut procedures), BREAKDNA (groove DNA and spectral cross-synthesis of two breaks), GENOTYPE and LOOPGENOME (Markov walks learned from a break), SNARESTACK (snare layer aligner), TYMPAN (percussion voice), DROP II, TURNAROUND
- **Bass and low end:** NEUROSTACK, SUBSTRATE, ACID (TB-3PO line and Open303 voice)
- **Generative voices and noise:** RETICULUM, SEEDWALK, FLOATER, QUANTA (microsound after Curtis Roads), MANIFOLD, CALL & RESPONSE, MODPORT (ProTracker .mod player, up to 8 channels)
- **Data and datamatics:** CELLULA, DATUM, PALIMPSEST, PINDATA, IKEDAT, FLOATDAT, DATASCREEN (any file can be the sound and the picture)
- **Effects:** NEXUSCLIP (clipper / spectral clipper / limiter), ILLUSORIUM (26 psychoacoustic illusions), SPLICEBUS
- **Structure and patching:** EPOCH (long-form clock), DUBPLATE (genre arrangement builder), CONDUIT (patch bay), BLACKBOX (CV/gate recorder with MIDI export), SETBOOK (scene memory with morphing)

Several modules share a 4-channel poly **BUS** cable (position, energy, scene, event), so EPOCH can conduct TURNAROUND, DUBPLATE, SETBOOK and the others.

## What changed during the MetaModule beta

Thanks to everyone who tested on hardware.

- **Knobs now show their position on the MetaModule.** The MetaModule draws a knob as one image rotated with its value; it was drawing only the knob's base. The MetaModule build now uses a single image with base, cap and pointer, the pointer drawn bolder for the small screen. Nothing changes in VCV Rack.
- **Fixed a crash when a module is removed while it is still preparing material** (the data abort seen on BREAKMED in the test rig). Every module that builds in the background (break slicing, snare stacks, learned walks and so on) now waits for that work to finish before it goes.
- **Fixed three out-of-bounds bugs** found by running every module with all parameters at 25 % and every jack patched, under AddressSanitizer: PALIMPSEST (crash on negative or very high oscillator frequencies from CV), FLOATER (a table read one past its end in the granular voice) and ACID (a table written past its end at start-up). Also fixed a race between loading a patch and a file still being read in the background.
- Five modules were renamed: VERTEX is now TYMPAN, N.M.S. is MANIFOLD, TST_INGP is PALIMPSEST, IKEDAR is CELLULA, JDAT is DATUM. Only the names changed; saved patches load as before.

## Known on MetaModule

- FLOATER, FLOATDAT and MANIFOLD are CPU-heavy; leave headroom in the patch.
- ILLUSORIUM: the MASK illusion is not included on MetaModule.
- SNARESTACK, GENOTYPE and LOOPGENOME rebuild in the background; after loading a file or changing a setting, the new sound arrives a few seconds later.
- Files: FLOATDAT, PINDATA and IKEDAT read up to 256 KB (WAV up to 10 s); MODPORT reads .mod files up to 4 MB.

## How this release was checked

Every module ran headless (unpatched, patched, randomized, at 44.1 and 96 kHz, with files loaded, saved and restored) and live in VCV Rack 2.6.6 (screens, context menus, outputs on a scope), and again with all parameters at 25 % and every jack patched under AddressSanitizer and UBSan. The MetaModule build resolves every symbol against plugin SDK 2.4.0 (it needs firmware with that plugin API).

## Credits

Built on open-source work: Mutable Instruments (Clouds, Braids, Plaits, Beads via artifex, stmlib), Open303 by Robin Schmidt, TB-3PO by Logarhythm and djphazer, pt2-clone by 8bitbubsy, BBCut after Nick Collins, the Call & Response generator by Nordl3, and forsitan / artifex. Each one keeps its own license in `engines/thirdparty` and in the engine folders. The package is GPL-3.0-or-later.

Reports and patches are welcome in [Issues](https://github.com/semierendonmez-commits/semi-rack/issues), or on the MetaModule forum.
