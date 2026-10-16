# GENOTYPE engine (Pinball 2 walker, learned matrix, call / response / fill, the player), vendored
# from semierendonmez-commits/genotype Source/engine (1d06ebb; JUCE-free, nothing stripped).
# LOOPGENOME's copies of Walker/Learn/Genotype are byte-identical and use this one.
# Changes: namespaces wrapped (geno -> dnafam::geno, walk -> dnafam::walk: the Pinball copy of
# GENOTYPE + LOOPGENOME); includes of the shared BREAKDNA files point to ../breakdna/;
# std::popcount (C++20 <bit>) -> __builtin_popcount in Learn.cpp (the Rack build is C++17).
ENGINE_SOURCES += engines/genotype/Learn.cpp engines/genotype/Genotype.cpp
