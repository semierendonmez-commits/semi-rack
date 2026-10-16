# BREAKBEAT engine (namespace bbeat). Sources: semierendonmez-commits/breakbeat (Source/BreakbeatEngine,
# JUCE removed + BBCut cut modes, see BreakbeatEngine.h), upstream schwung-breakbeat slice_select.c and the
# plugin's slice_select_n.c compiled as C++ in namespace bbeat (SliceSelect.cpp; slice_select_n.h lost its
# extern "C" block), BBCut procedures ported from bencodec/BBenCut (BBCut.h). bb_timing.c is not needed.
ENGINE_SOURCES += engines/breakbeat/SliceSelect.cpp engines/breakbeat/BBCut.cpp engines/breakbeat/BreakbeatEngine.cpp
