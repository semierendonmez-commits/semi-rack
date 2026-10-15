# Porting a plugin into the series

The reference is `src/modules/Nexusclip.cpp`. Every module follows the same five steps.

## 1. Vendor the engine

Copy the plugin's JUCE-free engine (`Source/engine/*`) unchanged into `engines/<name>/`.
If a file includes JUCE, strip only that part (usually a type alias or a `jassert`) and
note it at the top of the file. Shared third-party code is already in `engines/thirdparty`
(stmlib, Beads, artifex — artifex compiles against the real Rack headers).

Add `engines/<name>/sources.mk` (`ENGINE_SOURCES += engines/<name>/X.cpp`) and
`engines/<name>/sources.cmake` (`list(APPEND ENGINE_SOURCES ${REPO_ROOT}/engines/<name>/X.cpp)`).
Never list `engines/thirdparty` sources again.

MetaModule limits for engine code: no exceptions (`throw`, `try`/`catch`), no
`<sstream>`/`<fstream>`/iostreams, no threads (use the SDK async thread only if a file must be
loaded), keep heap allocation out of `process()`. Wrap anything unavoidable in
`#ifdef METAMODULE`.

## 2. Panel spec

Write `tools/panels/<name>.py` (`SPEC` dict, mm units, item types: `screen`, `knob`
(sizes L/M/S, `hot` = magenta indicator), `jack` (`kind` in/out, `bus`), `led` (size M/S),
`button`, `pad` (magenta/cyan), `plate` (output area), `label`, `box`). Run
`python3 tools/panelgen.py <name>`; check `build/preview/<Slug>.png`. Outputs go on the
cyan plate at the bottom, inputs in the row above, screens at the top under the title.

## 3. Module

* Params / inputs / outputs / lights enums, `configParam` with units and real ranges from the
  plugin's parameter list, `configBypass` for audio paths.
* Rack is per-sample; block engines get a fixed block adapter (see `kBlock` in NEXUSCLIP).
* Audio ±5 V = engine ±1; CV 0–10 V or ±5 V as documented in `configInput`.
* State that is not a param goes in `dataToJson` / `dataFromJson`.
* FamilyBus: use `src/common/FamilyBusCV.hpp` for any BUS jack (4-channel poly, magenta nut).

## 4. Screens

Subclass `semi::Screen`, read a `TripleBuffer<Snapshot>` filled in `process()` when
`UiDivider::tick()` fires, provide a `demo` snapshot for the browser, follow
`docs/METAMODULE_DISPLAY.md`.

## 5. Build both

```
make RACK_DIR=<Rack-SDK>
cmake -B build/mm -G Ninja -S metamodule -DMETAMODULE_SDK_DIR=<sdk> && cmake --build build/mm
```

Update the module's entry in `metamodule/plugin-mm.json` (groups by panel section).
