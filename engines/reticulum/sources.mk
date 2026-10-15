# RETICULUM engine, vendored unchanged from semierendonmez-commits/reticulum Source/engine
# (e603af2; JUCE-free, nothing stripped). Namespaces rt and fl (the rate bridge).
ENGINE_SOURCES += engines/reticulum/Engine.cpp

# Rack's compile.mk appends -std=c++11 after the Makefile's -std=c++17; the engine is C++17
# (std::clamp, if-init), so re-assert it for the files that include it.
build/engines/reticulum/Engine.cpp.o: CXXFLAGS += -std=c++17
build/src/modules/Reticulum.cpp.o: CXXFLAGS += -std=c++17
# Shared third-party code needs the same on desktop (see engines/seedwalk/sources.mk);
# repeated here so this engine builds on its own. Harmless when set twice.
build/engines/thirdparty/%.o: CXXFLAGS += -std=c++17 -DTEST
# artifex includes <rack.hpp>, whose own `#include <plugin.hpp>` resolves to src/plugin.hpp under
# -Isrc. Force the repo's plugin.hpp first (as every module file does) so that inner include is
# a no-op; this keeps Engine.cpp byte-identical to the plug-in's.
build/engines/reticulum/Engine.cpp.o: CXXFLAGS += -include src/plugin.hpp
