# LOOPGENOME engine (bass lines, the joint walk, NEUROSTACK's bass synth, the player), vendored
# from semierendonmez-commits/loopgenome Source/engine (cceba64; JUCE-free, nothing stripped).
# Changes: namespaces wrapped (lg -> dnafam::lg, neuro -> dnafam::neuro); includes of the shared
# GENOTYPE files point to ../genotype/; std::popcount -> __builtin_popcount in Joint.cpp.
ENGINE_SOURCES += engines/loopgenome/Bass.cpp engines/loopgenome/Joint.cpp engines/loopgenome/Loop.cpp engines/loopgenome/Neuro.cpp
