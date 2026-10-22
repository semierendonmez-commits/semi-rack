# SEMI Family 2.0.0 — 35 modules for VCV Rack 2 and the 4ms MetaModule.

**MetaModule:** download `release/SemiFamily.mmplugin`, copy it to the SD card or a USB stick, load it under Settings → Plugins.
**VCV Rack:** build from source (see README) or take the `.vcvplugin` from the GitHub Actions build.

Each module has a page with a live screenshot, a description, a first patch and its controls and I/O: see the README gallery.

Checked before this release: every module in a headless run (unpatched, patched, randomize, 44.1 / 96 kHz, file loading, JSON round trip) and live in VCV Rack (screen, context menu, output on a scope); the MetaModule build resolves all symbols.

Known on MetaModule:
- FLOATER, FLOATDAT and N.M.S. are CPU-heavy; leave headroom in the patch.
- ILLUSORIUM: the MASK illusion is not included on MetaModule.
- SNARESTACK, GENOTYPE and LOOPGENOME rebuild in the background; after loading or changing a setting, the new sound arrives after a few seconds.
- Files: FLOATDAT / PINDATA / IKEDAT read up to 256 KB (WAV up to 10 s); MODPORT .mod up to 4 MB.
