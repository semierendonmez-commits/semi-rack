# NMS engine, vendored from semierendonmez-commits/nms Source/engine (JUCE-free, namespace nms).
# Changes: Rack.h + outValue () accessor; Synths.cpp pushMidi is a no-op (no MIDI out, global
# queue shared across instances); Patches.cpp substitutes native modules for the original
# Schwung modules and is null-safe. Not vendored: Upstream.cpp / UpstreamSpecs.inc /
# Resampler.h / Source/upstream (SIGNAL, ESSAIM, BREAKBEAT, STRUCTOR, DISSOLVER), replaced
# by UpstreamStub.cpp. Host.* is new: the JUCE-free audio side of PluginProcessor.
# Credits: NOTICE.md.
ENGINE_SOURCES += engines/nms/Common.cpp engines/nms/Control.cpp engines/nms/Drums.cpp engines/nms/Fx.cpp
ENGINE_SOURCES += engines/nms/Instruments2.cpp engines/nms/Patches.cpp engines/nms/Rack.cpp engines/nms/Synths.cpp
ENGINE_SOURCES += engines/nms/UpstreamStub.cpp engines/nms/Host.cpp
