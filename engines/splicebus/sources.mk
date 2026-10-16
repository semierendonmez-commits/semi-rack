# SPLICEBUS engine, vendored from semierendonmez-commits/splicebus Source/engine (9cefada, JUCE-free).
# Changes (marked "SEMI port"): namespaces sb -> spb and substrate -> spb_substrate (SUBSTRATE's own port
# will want "substrate"); Reel.cpp keeps 10 s of tape on the MetaModule (20 s in Rack, as in the plug-in).
ENGINE_SOURCES += engines/splicebus/Engine.cpp engines/splicebus/Reel.cpp engines/splicebus/Patch.cpp
# Patch.*: new, the JUCE-free port of the plug-in's parameter layout, presets and MUTATE.
