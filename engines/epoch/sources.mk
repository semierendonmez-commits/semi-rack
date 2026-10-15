ENGINE_SOURCES += engines/epoch/Score.cpp engines/epoch/Plan.cpp engines/epoch/Analyzer.cpp

# The engine is C++17 (std::clamp, aggregate init with member defaults). Rack's compile.mk appends
# -std=c++11 after the Makefile's -std=c++17, so restate it last for the files that need it.
build/engines/epoch/%.cpp.o: CXXFLAGS += -std=c++17
build/src/modules/Epoch.cpp.o: CXXFLAGS += -std=c++17
