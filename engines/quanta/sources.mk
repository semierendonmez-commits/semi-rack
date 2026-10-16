# QUANTA engine, vendored from semierendonmez-commits/quanta Source/engine (JUCE-free, namespace qn).
# Changes are marked "SemiFamily" in the files: Arrange.cpp serialises without <sstream>;
# Particles.h/.cpp: fewer particles + shorter fx buffers on MetaModule, setSlotHz(), plain
# shared_ptr sample (audio-thread only).
ENGINE_SOURCES += engines/quanta/Particles.cpp engines/quanta/Arrange.cpp
