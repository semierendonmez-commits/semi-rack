# FLOATER engine, vendored from semierendonmez-commits/floater Source/engine (eba7532, v0.7; JUCE-free).
# Changes for the series (each marked "SEMI port" in the file):
#   * namespace fl -> flt (fl is RETICULUM's rate bridge; every file, mechanical rename)
#   * Schwung.cpp: TEST is not forced on the MetaModule (stmlib keeps its ARM code there); CloudSeed is a
#     silent stub on the MetaModule (~60 MB per instance); includes cloudseed/floater_cloudseed.h and the
#     v0.7 Beads bridge below instead of beads/floater_beads.h
#   * Reel.cpp: the tape is 10 s on the MetaModule (20 s in Rack, as in the plug-in)
#   * FloaterBeads.*: FLOATER v0.7's own bridge to Beads (ThirdParty/beads/floater_beads.*, namespace
#     floater_beads -> flt_beads). The shared engines/thirdparty/beads/floater_beads.* is an older copy
#     (DENSITY / TIME / SIZE mapped differently); it is left alone for RETICULUM and SEEDWALK.
#   * Case.*: new, the JUCE-free port of the plug-in's patch layer (parameter layout, factory patches,
#     NEW PATCH, MUTATE, locks) from Source/PluginProcessor.cpp and Source/Presets.cpp.
ENGINE_SOURCES += engines/floater/Voices.cpp engines/floater/Fx.cpp engines/floater/Engine.cpp engines/floater/Patch.cpp
ENGINE_SOURCES += engines/floater/Schwung.cpp engines/floater/Reel.cpp engines/floater/Drone.cpp engines/floater/FloaterBeads.cc
ENGINE_SOURCES += engines/floater/Case.cpp engines/floater/Presets.cpp
