# SEMI Family 2.1.0 — 35 modules for VCV Rack 2 and the 4ms MetaModule.

**2.1.0:** five modules have new names: VERTEX is now TYMPAN, N.M.S. is MANIFOLD, TST_INGP is PALIMPSEST, IKEDAR is CELLULA, JDAT is DATUM. Only the names changed: sound, controls and saved patches are the same (the internal module IDs did not change). Checked with VCV Rack 2.6.6.

**MetaModule:** download `release/SemiFamily.mmplugin`, copy it to the SD card or a USB stick, load it under Settings → Plugins.
**VCV Rack:** build from source (see README) or take the `.vcvplugin` from the GitHub Actions build.

Each module has a page with a live screenshot, a description, a first patch and its controls and I/O: see the README gallery.

Checked before this release: every module in a headless run (unpatched, patched, randomize, 44.1 / 96 kHz, file loading, JSON round trip) and live in VCV Rack (screen, context menu, output on a scope); the MetaModule build resolves all symbols.

Known on MetaModule:
- FLOATER, FLOATDAT and MANIFOLD are CPU-heavy; leave headroom in the patch.
- ILLUSORIUM: the MASK illusion is not included on MetaModule.
- SNARESTACK, GENOTYPE and LOOPGENOME rebuild in the background; after loading or changing a setting, the new sound arrives after a few seconds.
- Files: FLOATDAT / PINDATA / IKEDAT read up to 256 KB (WAV up to 10 s); MODPORT .mod up to 4 MB.
