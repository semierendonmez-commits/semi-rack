# Port fix in engines/thirdparty/open303/rosic_MipMappedWaveTable.h: prototypeTable has the +4 guard
# entries that initPrototypeTable() writes (it wrote 4 doubles past the array).
# ACID engine (TB-3PO x 303), vendored from semierendonmez-commits/acid Source/engine. Namespace acid.
# Changes: AcidEngine.h/.cpp lose their JUCE types (see the header); everything wrapped in namespace acid;
# Voice303.cpp includes schwung303/plugin.cpp (unchanged, GPL-3.0) with P303 / drive renamed by macro.
# The 303 core is the shared Open303 in engines/thirdparty/open303 (engines/tp_open303).
# phazerville-tb3po/ (GPL-3.0) is the reference applet Tb3poCore is rebuilt from; not compiled.
ENGINE_SOURCES += engines/acid/AcidEngine.cpp engines/acid/Tb3poCore.cpp engines/acid/Voice303.cpp
