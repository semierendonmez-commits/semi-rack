# BLACKBOX, cable edition. Not a copy of semierendonmez-commits/blackbox Source/engine: that engine records
# the shared-memory buses into a growing std::vector log behind a std::mutex and a recorder thread. The
# cable tape (fixed ring, audio-thread recorder / player) is new; its SMF writer and MIDI map follow
# blackbox Source/engine/Export.cpp and docs/FORMATS.md.
ENGINE_SOURCES += engines/blackbox/CableTape.cpp
