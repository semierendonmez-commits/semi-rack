# FLOATDAT engines, vendored from semierendonmez-commits/floatdat Source/engine (7e30328, JUCE-free):
# IKD (ryoz_ikedar_ln), VTX (Vortessa), JDAT (kdr_jdat), Pinball 2, DataBlob. Changes (marked "SEMI port"):
# namespaces ryoz / vtx / pinball / jdat / pdata -> fdat_ryoz / fdat_vtx / fdat_pinball / fdat_jdat / fdat_pdata
# (IKEDAR, IKEDAT, PINDATA, JDAT ... carry the same engines). Its FLOATER copy (Source/flt/engine, v0.4) is not
# vendored: FLOATDAT uses engines/floater (v0.7, a superset) and its Case.
# New: Data.* (Source/data/DataLoader without JUCE; WAV decoded by the module, no image decoder),
# Examples.h (Resources/texts), Instrument.* (Source/PluginProcessor without JUCE / MIDI / video).
ENGINE_SOURCES += engines/floatdat/IkdEngine.cpp engines/floatdat/Vtx.cpp engines/floatdat/Data.cpp engines/floatdat/Instrument.cpp
