# SEEDWALK engine, vendored from semierendonmez-commits/seedwalk Source/engine (JUCE-free; nothing
# stripped). Namespace sw (Pinball and the rate bridge wrapped into it, see Pinball.h / Rate.h).
ENGINE_SOURCES += engines/seedwalk/Engine.cpp

# Rack's compile.mk appends -std=c++11 after the Makefile's -std=c++17; the engine is C++17
# (std::clamp, if-init, nested namespaces), so re-assert it for the files that include it.
build/engines/seedwalk/Engine.cpp.o: CXXFLAGS += -std=c++17
build/src/modules/Seedwalk.cpp.o: CXXFLAGS += -std=c++17
# Shared third-party code: the same two problems on desktop (std::make_unique in floater_beads.cc,
# ARM inline asm in stmlib without TEST). These belong in engines/thirdparty/sources.mk; set here as
# pattern-specific flags so the Rack build works until then (repeating them there is harmless).
build/engines/thirdparty/%.o: CXXFLAGS += -std=c++17 -DTEST
