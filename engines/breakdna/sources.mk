# BREAKDNA engine (analysis, genome, kit / spectral layers, the player), vendored from
# semierendonmez-commits/breakdna Source/engine (792df1c; JUCE-free, nothing stripped).
# Shared by GENOTYPE and LOOPGENOME (their copies of Types/Analysis/Dna/Dsp/Fft are byte-identical).
# Changes: every namespace wrapped in dnafam:: (gtbs -> dnafam::gtbs, dna -> dnafam::dna) so it
# cannot meet BREAKSLICER / BREAKMED's gtbs. TripleBuffer.h not vendored (unused by the engine).
ENGINE_SOURCES += engines/breakdna/Analysis.cpp engines/breakdna/Dna.cpp engines/breakdna/Engine.cpp
